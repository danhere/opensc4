// Our cSC4NetworkOccupant, third piece (build 641; src/core/netocc_sim.h has
// the class map, the fields and the slots): the geometry (altitude, normal,
// occupied cell, the position and the boxes, neighbour connections), the
// paths (the path information, the path connection table) and the edge
// store. Each function is a detour at the game's address, because the
// bridge, tunnel and pipe classes hold the same code in their vtables (the
// edge store is also used through the thunks of other classes).
//
// Every function was written from the disassembly. `self` is the object's
// start (the cISC4NetworkOccupant part) unless a function says `part` (the
// occupant part's box query gets the object + 4) or `store` (the edge store
// at the object + 0x108). The detours are plain functions, so they clobber
// ECX and EDX where the game's code did not. The game's functions that touch
// neither (callers of the LTCG build may keep values in them across the call;
// to be covered by the register scan before a batch): GetAltitude
// 0x006197F0 and SetRotationAndFlip 0x00604DF0 touch neither; GetNormal
// 0x0061B6C0, GetOccupiedCell 0x0061B010 (ECX is pushed and popped) and the
// edge store's Get 0x00619B70 (its first entry found) and Set 0x00619A40
// (the first entry's key, or an empty first entry) keep ECX; Get on its first
// entry keeps EDX as well, and ReleasePathInfo 0x00619680 keeps EDX when
// there is no path information.
//
// Slots 74 to 80 of the interface, which of them are ours:
//   74 0x00604DF0  a byte store into the rotation and flip byte: ours.
//   75 0x006193D0  the position (the occupant part's slot 8 forwards here):
//                  ours.
//   76 0x0061AC70  builds the piece's models and sets their lighting: drawing
//                  (calls 0x007D4530, 0x007D4070, 0x007D29F0, 0x007D41E0,
//                  0x007D4280 on a model object and 0x00619550): left.
//   77 0x0061C950  looks a model up in (or adds it to) the hash map at
//                  0x00B45278 through the resource service: model cache, left.
//   78 0x0061C5F0  releases every model of that cache (0x008090E0) and
//                  clears it: left.
//   79 0x0061BB60  works out the corners' lighting bytes with the light
//                  object it is given (its slots 0x30 and 0x4C, normals
//                  through 0x005D6EA0) and sets flag 0x8000: lighting, left.
//   80 0x0061A430  the bounding box: ours.
//
// Virtual calls, each count from the asm's pushes for that call (and where
// the callee is ours or known, its own ret N):
//   - the interface's own slots (this object): 0x28 SetNetworkFlag one, 0x30
//     (the flags) none, 0x58 IsOfType one, 0x5C PieceId none, 0x98
//     GetEdgeTypesFlag one, 0xC4 GetPathInfo none;
//   - the occupant part (object + 4): 0x20 (GetPosition, forwards to slot 75)
//     one: the out pointer;
//   - the resource manager (global 0x00B45254): 0x0C GetResource five (the
//     key, 0xA9667793, the pointer for the answer, 0, 0);
//   - the network manager (0x00602680): 0x3C one (the piece ID or its
//     override), answering the key's instance;
//   - the resource found (the path resource): 0x40 none (answers a byte), 0x60
//     none (answers another resource), 0x64 one (this, answers the path
//     information), 0x08 none (Release);
//   - the resource the 0x60 call answered: 0x44 one (1);
//   - the path information: 0x74 four (the path ID override +0x168, 0, +0x16C,
//     a pointer to four floats), 0x68 one (this), 0xA0 one (this), 0x6C one
//     (this), 0x08 none (Release);
//   - the streams (as in src/core/netocc_life.cpp): 0x14 one (SetUint8 the
//     value, GetUint8 the pointer), 0x1C one (the uint16), 0x24 one (the
//     uint32), 0x48 none.
// Direct callees: 0x00620EE0 (cdecl, two: the edge value and the side; the
// bits 0 to 1 of the side's byte are tested), 0x00602680 (cdecl, none), the
// allocator 0x005E55E0 and 0x005E5620 (cdecl, one).
#include "core/netocc_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::netocc_sim {
namespace {

template <typename R, typename... Args>
R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

// The result of the stream's GetError as the game turns it into the
// function's answer (neg al, sbb eax, eax, inc eax): 1 for no error.
uint32_t NoError(void* stream)
{
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// A float's bits and back (the box and the vectors are handled as words,
// compared as floats, so that a copy never goes through the FPU).
float AsFloat(uint32_t bits)
{
    float value = 0;
    std::memcpy(&value, &bits, 4);
    return value;
}

uint32_t AsBits(float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// The edge store's layout (the object + 0x108; the offsets here are from the
// store): count (byte), +4 the first key (byte), +8 its value, +0xC the
// pointer to the further entries (8 bytes each: key byte, value at +4).
namespace store_field {
constexpr std::size_t kCount = 0x0, kKey = 0x4, kValue = 0x8, kEntries = 0xC;
} // namespace store_field

// Copies `bytes` bytes as the game's rep movs does (dwords, then the rest);
// the memory does not overlap.
void CopyBytes(void* to, const void* from, std::size_t bytes)
{
    if (bytes)
        std::memcpy(to, from, bytes);
}

// ====================================================================================
// simulators.netocc.geometry
// ====================================================================================

// Slot 47 (0x00619320; ret 8: the side and the network type). Not of the
// type: false. Else the type's edge value (slot 38) and the game's 0x00620EE0
// on it and the side: whether the side's byte has a connection in its low
// two bits (the shift is 8 x side, taken modulo 32). AL is that function's.
uint8_t __fastcall ConnectsToNeighbor(char* self, void*, uint32_t side, uint32_t type)
{
    static bool logged = false;
    FirstCall("ConnectsToNeighbor", logged);
    if (!Virtual<uint8_t>(self, slot::kIsOfType, type))
        return 0;
    const uint32_t value = Virtual<uint32_t>(self, slot::kGetEdgeTypesFlag, type);
    return static_cast<uint8_t>(
        CallCdecl<uint32_t, uint32_t, uint32_t>(addr::cSC4NetworkOccupant_EdgeActive, value, side));
}

// Slot 56 (0x006197F0; ret 8; the height at a world position, in ST0): the
// position less corner 0's x and z gives (a, b). With a <= b (the first
// triangle: corners 0, 1, 2; an unordered compare takes the other) the
// result is (a * (h2 - h1) + (h1 - h0) * b) / 16 + h0, else
// ((h2 - h3) * b + (h3 - h0) * a) / 16 + h0, h0 to h3 being the corner heights
// (+0xA0, +0xB8, +0xD0, +0xE8) and the 1/16 the float at 0x00B0B384. The
// game works on the FPU at 53 bits and keeps every intermediate; doubles do
// the same. Touches neither ECX nor EDX.
double __fastcall GetAltitude(char* self, void*, float x, float z)
{
    static bool logged = false;
    FirstCall("GetAltitude", logged);
    const double a = static_cast<double>(x) - static_cast<double>(At<float>(self, field::kCorners));
    const double b = static_cast<double>(z) - static_cast<double>(At<float>(self, field::kCorners + 8));
    const double h0 = static_cast<double>(At<float>(self, field::kVertexHeights));
    const double h1 = static_cast<double>(At<float>(self, field::kVertexHeights + field::kVertexStride));
    const double h2 = static_cast<double>(At<float>(self, field::kVertexHeights + 2 * field::kVertexStride));
    const double h3 = static_cast<double>(At<float>(self, field::kVertexHeights + 3 * field::kVertexStride));
    const double scale = static_cast<double>(Global<float>(addr::cSC4NetworkOccupant_AltitudeScale));
    if (a <= b) {
        const double first = a * (h2 - h1);
        const double second = (h1 - h0) * b;
        const double sum = first + second;
        const double scaled = sum * scale;
        return scaled + h0;
    }
    const double first = (h2 - h3) * b;
    const double second = (h3 - h0) * a;
    const double sum = first + second;
    const double scaled = sum * scale;
    return scaled + h0;
}

// Slot 57 (0x0061B6C0; ret 0xC: the out pointer, then x and z). The same
// (a, b) as GetAltitude. The out vector's y is 16.0; with b > a (ordered)
// x is h0 - h3 and z h0 - h1, else x is h1 - h2 and z h3 - h2 (floats stored
// from the FPU's differences). The answer is the out pointer. The game's
// header says it takes two floats and returns one; it does not.
uint32_t* __fastcall GetNormal(char* self, void*, uint32_t* out, float x, float z)
{
    static bool logged = false;
    FirstCall("GetNormal", logged);
    const double a = static_cast<double>(x) - static_cast<double>(At<float>(self, field::kCorners));
    const double b = static_cast<double>(z) - static_cast<double>(At<float>(self, field::kCorners + 8));
    const float h0 = At<float>(self, field::kVertexHeights);
    const float h1 = At<float>(self, field::kVertexHeights + field::kVertexStride);
    const float h2 = At<float>(self, field::kVertexHeights + 2 * field::kVertexStride);
    const float h3 = At<float>(self, field::kVertexHeights + 3 * field::kVertexStride);
    out[1] = 0x41800000u;
    float first = 0, second = 0;
    if (b > a) {
        first = static_cast<float>(static_cast<double>(h0) - static_cast<double>(h3));
        second = static_cast<float>(static_cast<double>(h0) - static_cast<double>(h1));
    } else {
        first = static_cast<float>(static_cast<double>(h1) - static_cast<double>(h2));
        second = static_cast<float>(static_cast<double>(h3) - static_cast<double>(h2));
    }
    std::memcpy(&out[0], &first, 4);
    std::memcpy(&out[2], &second, 4);
    return out;
}

// Slot 59 (0x0061B010; ret 8: two out pointers): the cell of corner 0, each
// coordinate floored the game's way: (value * 1/16 + 12582912.0) stored as a
// float, its bits less 0x4B400000 (added 0xB4C00000). The first goes to the
// first pointer, then the second; the game's ECX is saved and restored.
void __fastcall GetOccupiedCell(char* self, void*, uint32_t* x, uint32_t* z)
{
    static bool logged = false;
    FirstCall("GetOccupiedCell", logged);
    const double scale = static_cast<double>(Global<float>(addr::cSC4NetworkOccupant_OriginScale));
    const double magic = static_cast<double>(Global<float>(addr::cSC4NetworkOccupant_RoundMagic));
    const double scaledX = static_cast<double>(At<float>(self, field::kCorners)) * scale;
    const float storedX = static_cast<float>(scaledX + magic);
    uint32_t bits = 0;
    std::memcpy(&bits, &storedX, 4);
    *x = bits + 0xB4C00000u;
    const double scaledZ = static_cast<double>(At<float>(self, field::kCorners + 8)) * scale;
    const float storedZ = static_cast<float>(scaledZ + magic);
    std::memcpy(&bits, &storedZ, 4);
    *z = bits + 0xB4C00000u;
}

// The occupant part's slot 10 (0x00619360; `part` is the object + 4; ret 8:
// two out pointers): the box the interface's slot 80 stored at +0x124 (low
// x, y, z) to the first pointer and +0x130 (high x, y, z) to the second, the
// words copied.
void __fastcall GetBoundingBox(char* part, void*, uint32_t* low, uint32_t* high)
{
    static bool logged = false;
    FirstCall("GetBoundingBox", logged);
    for (std::size_t i = 0; i < 3; ++i)
        low[i] = At<uint32_t>(part, field::kPairs - field::kOccupant + 4 * i);
    for (std::size_t i = 0; i < 3; ++i)
        high[i] = At<uint32_t>(part, field::kPairs - field::kOccupant + 0xC + 4 * i);
}

// Slot 74 (0x00604DF0; ret 4): the byte stored as rotation and flip. Touches
// neither ECX nor EDX.
void __fastcall SetRotationAndFlip(char* self, void*, uint32_t value)
{
    static bool logged = false;
    FirstCall("SetRotationAndFlip", logged);
    At<uint8_t>(self, field::kRotationFlip) = static_cast<uint8_t>(value);
}

// Slot 75 (0x006193D0; ret 4: the out vector; the occupant part's slot 8
// jumps here with the object). x and z start as corner 0's, y as -FLT_MAX
// (0xFF7FFFFF) raised to corner 0's height when that is above the float at
// 0x00AA7D74 (-FLT_MAX); then for corners 1 to 3 x and z are added (float
// stores of the FPU's sums) and y is raised to the corner's height when it is
// above it; at the end x and z are multiplied by the float at 0x00A8821C
// (0.25): the average position of the four corners, at the highest corner.
void __fastcall GetPosition(char* self, void*, uint32_t* out)
{
    static bool logged = false;
    FirstCall("GetPosition", logged);
    float x = AsFloat(At<uint32_t>(self, field::kCorners));
    float y = AsFloat(0xFF7FFFFFu);
    float z = AsFloat(At<uint32_t>(self, field::kCorners + 8));
    if (static_cast<double>(AsFloat(At<uint32_t>(self, field::kCorners + 4))) >
        static_cast<double>(Global<float>(addr::cSC4NetworkOccupant_PositionFloor)))
        y = AsFloat(At<uint32_t>(self, field::kCorners + 4));
    for (std::size_t corner = 1; corner < 4; ++corner) {
        char* const record = self + field::kCorners + corner * field::kVertexStride;
        x = static_cast<float>(static_cast<double>(AsFloat(At<uint32_t>(record, 0))) + static_cast<double>(x));
        z = static_cast<float>(static_cast<double>(AsFloat(At<uint32_t>(record, 8))) + static_cast<double>(z));
        if (static_cast<double>(AsFloat(At<uint32_t>(record, 4))) > static_cast<double>(y))
            y = AsFloat(At<uint32_t>(record, 4));
    }
    const double scale = static_cast<double>(Global<float>(addr::cSC4NetworkOccupant_PositionScale));
    x = static_cast<float>(static_cast<double>(x) * scale);
    z = static_cast<float>(static_cast<double>(z) * scale);
    out[0] = AsBits(x);
    out[1] = AsBits(y);
    out[2] = AsBits(z);
}

// One point's x, y, z against the low and high of the box: a coordinate
// below the low one replaces it, one above the high one replaces that (the
// words copied; unordered compares change nothing). The game goes low x, y,
// z then high x, y, z for each point.
void Widen(const char* point, uint32_t* low, uint32_t* high)
{
    uint32_t bits[3];
    std::memcpy(bits, point, sizeof bits);
    for (std::size_t i = 0; i < 3; ++i)
        if (AsFloat(bits[i]) < AsFloat(low[i]))
            low[i] = bits[i];
    for (std::size_t i = 0; i < 3; ++i)
        if (AsFloat(bits[i]) > AsFloat(high[i]))
            high[i] = bits[i];
}

// Slot 80 (0x0061A430; ret 8: the low and the high out pointers; the
// interface's Init calls it with +0x124 and +0x130). The occupant part's
// slot 8 (virtual: position, which fills the first pointer) is asked, its
// answer copied to the second, then the box is widened by the four corners
// and by each of the three points of every path record.
void __fastcall ComputeBoundingBox(char* self, void*, uint32_t* low, uint32_t* high)
{
    static bool logged = false;
    FirstCall("ComputeBoundingBox", logged);
    Virtual<void>(self + field::kOccupant, 0x20, low);
    high[0] = low[0];
    high[1] = low[1];
    high[2] = low[2];
    for (std::size_t corner = 0; corner < 4; ++corner)
        Widen(self + field::kCorners + corner * field::kVertexStride, low, high);
    // The count is a signed division of the bytes between begin and end.
    const int32_t span = static_cast<int32_t>(At<std::uintptr_t>(self, field::kPaths + 4) -
                                              At<std::uintptr_t>(self, field::kPaths));
    const int32_t count = span / 0x4C;
    for (int32_t i = 0; i < count; ++i) {
        const char* const record = At<char*>(self, field::kPaths) + static_cast<std::size_t>(i) * 0x4C;
        Widen(record + 4, low, high);
        Widen(record + 0x1C, low, high);
        Widen(record + 0x34, low, high);
    }
}

// ====================================================================================
// simulators.netocc.paths
// ====================================================================================

// Slot 49 (0x0061B510; no arguments; the answer is the path information, the
// object at +0x140). With one already there it is just returned. Else the
// resource key {0x296678F7, group 0x69668828, instance} is made, the group
// 0xA966883F and the instance from the path ID override when there is one,
// the instance the network manager's slot 0x3C answers for the piece ID (or
// the override); the resource manager is asked for it as class 0xA9667793.
// With a resource: with an override, when its slot 0x40 says no its slot 0x60
// may give another resource, which gets slot 0x44 (1) and replaces the first
// (released); the resource's slot 0x64 (this) gives the path information,
// the resource is released; when there is path information it is stored, and
// with an override it is made again by its slot 0x74 from the path ID
// override words and the corners 0 and 2 (x, z each) after asking the
// occupant part for its position (the answer is not used), the first one
// released; then its slot 0x68 (this). Whatever the path information is
// now, slot 0xA0 (this) is called on it, the eight words of the path
// connection table (+0x144) zeroed and slot 0x6C (this) called (the
// information object fills the table through AddPathConnection).
void* __fastcall GetPathInfo(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetPathInfo", logged);
    if (At<void*>(self, field::kReference))
        return At<void*>(self, field::kReference);
    struct Key {
        uint32_t type;
        uint32_t group;
        uint32_t instance;
    } key;
    void* resource = nullptr;
    uint32_t id = Virtual<uint32_t>(self, slot::kPieceId);
    key.type = 0x296678F7u;
    key.group = 0x69668828u;
    if (At<uint32_t>(self, field::kPathIDOverride)) {
        key.group = 0xA966883Fu;
        id = At<uint32_t>(self, field::kPathIDOverride);
    }
    void* const network = CallCdecl<void*>(addr::SC4_GetNetworkManager);
    key.instance = Virtual<uint32_t>(network, 0x3C, id);
    void* const manager = Global<void*>(addr::cSC4NetworkOccupant_ResourceManager);
    Virtual<uint8_t>(manager, 0xC, &key, 0xA9667793u, &resource, 0u, 0u);
    if (resource) {
        if (At<uint32_t>(self, field::kPathIDOverride)) {
            if (!Virtual<uint8_t>(resource, 0x40)) {
                void* const other = Virtual<void*>(resource, 0x60);
                if (other) {
                    Virtual<void>(other, 0x44, 1u);
                    Virtual<uint32_t>(resource, 0x8);
                    resource = other;
                }
            }
        }
        void* const info = Virtual<void*>(resource, 0x64, self);
        Virtual<uint32_t>(resource, 0x8);
        if (info) {
            At<void*>(self, field::kReference) = info;
            if (At<uint32_t>(self, field::kPathIDOverride)) {
                uint32_t points[4] = {At<uint32_t>(self, field::kCorners), At<uint32_t>(self, field::kCorners + 8),
                                      At<uint32_t>(self, field::kCorners + 2 * field::kVertexStride),
                                      At<uint32_t>(self, field::kCorners + 2 * field::kVertexStride + 8)};
                uint32_t position[3];
                Virtual<void>(self + field::kOccupant, 0x20, static_cast<uint32_t*>(position));
                At<void*>(self, field::kReference) =
                    Virtual<void*>(info, 0x74, At<uint32_t>(self, field::kPathOriginX), 0u,
                                   At<uint32_t>(self, field::kPathOriginY), static_cast<uint32_t*>(points));
                Virtual<uint32_t>(info, 0x8);
            }
            if (void* const now = At<void*>(self, field::kReference))
                Virtual<void>(now, 0x68, self);
        }
    }
    if (void* const now = At<void*>(self, field::kReference)) {
        Virtual<void>(now, 0xA0, self);
        for (std::size_t i = 0; i < 8; ++i)
            At<uint32_t>(self, field::kPathTable + 4 * i) = 0;
        Virtual<void>(At<void*>(self, field::kReference), 0x6C, self);
    }
    return At<void*>(self, field::kReference);
}

// Slot 50 (0x00619680): the path information released and forgotten. Keeps
// EDX when there is none.
void __fastcall ReleasePathInfo(char* self, void*)
{
    static bool logged = false;
    FirstCall("ReleasePathInfo", logged);
    if (void* const info = At<void*>(self, field::kReference)) {
        Virtual<uint32_t>(info, 0x8);
        At<void*>(self, field::kReference) = nullptr;
    }
}

// The count of the set bits 0 to 3 of one byte.
uint8_t CountBits(uint8_t value)
{
    uint8_t count = 0;
    for (uint32_t bit = 0; bit < 4; ++bit)
        count = static_cast<uint8_t>(count + ((value & (1u << bit)) != 0));
    return count;
}

// Slot 53 (0x006196A0; ret 0xC: the path type, then two bytes, `from` and
// `to`; the header names this slot AddPathConnection and the next one
// PathConnectionExists: the code is the other way round). Without flag 0x2000
// the path information is made first (slot 49). The table at +0x144 holds
// four bytes for each path type, indexed by `from`, with a bit for each `to`
// (0 to 3; the mask is 1 << (to modulo 32), as a byte). 0xFF in a byte means
// all. Both 0xFF: the number of bits set in the type's four bytes; `from` 0xFF
// and `to` given: the number of the four bytes that have the bit; `from` given
// and `to` 0xFF: the number of bits set in that byte; both given: whether the
// bit is set. The answer is in AL (a byte count).
uint8_t __fastcall PathConnectionExists(char* self, void*, uint32_t type, uint32_t from, uint32_t to)
{
    static bool logged = false;
    FirstCall("PathConnectionExists", logged);
    if (!(Virtual<uint32_t>(self, slot::kGetNetworkFlag) & flag::kPathInfo))
        Virtual<void>(self, slot::kGetPathInfo);
    const uint8_t from8 = static_cast<uint8_t>(from);
    const uint8_t to8 = static_cast<uint8_t>(to);
    const uint8_t mask = static_cast<uint8_t>(1u << (to8 & 31));
    uint8_t count = 0;
    if (from8 == 0xFF) {
        char* const row = self + field::kPathTable + type * 4;
        if (to8 == 0xFF) {
            for (std::size_t i = 0; i < 4; ++i)
                count = static_cast<uint8_t>(count + CountBits(At<uint8_t>(row, i)));
        } else {
            for (std::size_t i = 0; i < 4; ++i)
                count = static_cast<uint8_t>(count + ((At<uint8_t>(row, i) & mask) != 0));
        }
        return count;
    }
    const uint8_t value = At<uint8_t>(self, field::kPathTable + type * 4 + from8);
    if (to8 == 0xFF)
        return CountBits(value);
    return (mask & value) != 0;
}

// Slot 54 (0x00619790; ret 0xC: the path type, `from`, `to`): with `from`
// not 0xFF the bit of `to` (1 << (to modulo 32), as a byte) set in the
// type's byte for `from` and in the byte for `from` of type 0 (the first
// row; the same table), then flag 0x2000 set (SetNetworkFlag) in any case.
void __fastcall AddPathConnection(char* self, void*, uint32_t type, uint32_t from, uint32_t to)
{
    static bool logged = false;
    FirstCall("AddPathConnection", logged);
    const uint8_t from8 = static_cast<uint8_t>(from);
    if (from8 != 0xFF) {
        const uint8_t mask = static_cast<uint8_t>(1u << (static_cast<uint8_t>(to) & 31));
        At<uint8_t>(self, field::kPathTable + type * 4 + from8) |= mask;
        At<uint8_t>(self, field::kPathTable + from8) |= mask;
    }
    Virtual<void>(self, slot::kSetNetworkFlag, uint32_t{flag::kPathInfo});
}

// ====================================================================================
// simulators.netocc.edgestore
// ====================================================================================

// Get (0x00619B70; this is the store; ret 4: the key, a byte): the first
// entry's value when its key matches (even for key 0 in an empty store), else
// the value of the further entry with the key, else 0. Its early return
// touches neither ECX nor EDX.
uint32_t __fastcall StoreGet(char* store, void*, uint32_t keyArgument)
{
    static bool logged = false;
    FirstCall("StoreGet", logged);
    const uint8_t key = static_cast<uint8_t>(keyArgument);
    if (At<uint8_t>(store, store_field::kKey) == key)
        return At<uint32_t>(store, store_field::kValue);
    const uint8_t count = At<uint8_t>(store, store_field::kCount);
    if (count != 0) {
        char* const entries = At<char*>(store, store_field::kEntries);
        uint8_t i = 0;
        do {
            if (key == At<uint8_t>(entries, i * 8u))
                return At<uint32_t>(entries, i * 8u + 4);
            ++i;
        } while (i < count);
    }
    return 0;
}

// Set (0x00619A40; ret 8: the key (a byte) and the value):
//   - the first entry's key: its value replaced;
//   - else an empty first value (0): its key and value taken;
//   - else a zero value removes every further entry with the key (the last
//     entry moved into its place, the array freed when it was the only one;
//     the walk goes on at the next index);
//   - else an existing further entry's value replaced, or a new array one
//     entry longer (copy, the new entry last, the old array freed).
void __fastcall StoreSet(char* store, void*, uint32_t keyArgument, uint32_t value)
{
    static bool logged = false;
    FirstCall("StoreSet", logged);
    const uint8_t key = static_cast<uint8_t>(keyArgument);
    if (key == At<uint8_t>(store, store_field::kKey)) {
        At<uint32_t>(store, store_field::kValue) = value;
        return;
    }
    if (At<uint32_t>(store, store_field::kValue) == 0) {
        At<uint8_t>(store, store_field::kKey) = key;
        At<uint32_t>(store, store_field::kValue) = value;
        return;
    }
    if (value == 0) {
        if (At<uint8_t>(store, store_field::kCount) == 0)
            return;
        uint8_t i = 0;
        do {
            char* const entries = At<char*>(store, store_field::kEntries);
            if (At<uint8_t>(entries, i * 8u) == key) {
                const uint8_t count = At<uint8_t>(store, store_field::kCount);
                if (count == 1) {
                    At<uint8_t>(store, store_field::kCount) = 0;
                    CallCdecl<void, void*>(addr::SC4_operator_delete, entries);
                    At<char*>(store, store_field::kEntries) = nullptr;
                } else {
                    if (i != static_cast<uint32_t>(count) - 1) {
                        At<uint32_t>(entries, i * 8u) = At<uint32_t>(entries, count * 8u - 8);
                        At<uint32_t>(entries, i * 8u + 4) = At<uint32_t>(entries, count * 8u - 4);
                    }
                    --At<uint8_t>(store, store_field::kCount);
                }
            }
            ++i;
        } while (i < At<uint8_t>(store, store_field::kCount));
        return;
    }
    const uint8_t count = At<uint8_t>(store, store_field::kCount);
    if (count != 0) {
        char* const entries = At<char*>(store, store_field::kEntries);
        uint8_t i = 0;
        do {
            if (At<uint8_t>(entries, i * 8u) == key) {
                At<uint32_t>(entries, i * 8u + 4) = value;
                return;
            }
            ++i;
        } while (i < At<uint8_t>(store, store_field::kCount));
    }
    char* const fresh = CallCdecl<char*, std::size_t>(addr::SC4_operator_new, std::size_t{count} * 8 + 8);
    const std::size_t bytes = std::size_t{At<uint8_t>(store, store_field::kCount)} << 3;
    CopyBytes(fresh, At<char*>(store, store_field::kEntries), bytes);
    At<uint8_t>(fresh, At<uint8_t>(store, store_field::kCount) * 8u) = key;
    At<uint32_t>(fresh, At<uint8_t>(store, store_field::kCount) * 8u + 4) = value;
    CallCdecl<void, void*>(addr::SC4_operator_delete, At<char*>(store, store_field::kEntries));
    const uint8_t grown = static_cast<uint8_t>(At<uint8_t>(store, store_field::kCount) + 1);
    At<char*>(store, store_field::kEntries) = fresh;
    At<uint8_t>(store, store_field::kCount) = grown;
}

// Assign (0x006199D0; the interface's slot 44 jumps here; ret 4: the other
// store; the answer is this store): the old array freed and forgotten, the
// count byte, the dwords at +4 and +8 copied, and with a count a new array
// of count * 8 bytes made and filled from the other's.
char* __fastcall StoreAssign(char* store, void*, char* source)
{
    static bool logged = false;
    FirstCall("StoreAssign", logged);
    if (void* const old = At<char*>(store, store_field::kEntries)) {
        CallCdecl<void, void*>(addr::SC4_operator_delete, old);
        At<char*>(store, store_field::kEntries) = nullptr;
    }
    const uint8_t count = At<uint8_t>(source, store_field::kCount);
    At<uint8_t>(store, store_field::kCount) = count;
    At<uint32_t>(store, store_field::kKey) = At<uint32_t>(source, store_field::kKey);
    At<uint32_t>(store, store_field::kValue) = At<uint32_t>(source, store_field::kValue);
    if (count != 0) {
        char* const fresh = CallCdecl<char*, std::size_t>(addr::SC4_operator_new, std::size_t{count} * 8);
        At<char*>(store, store_field::kEntries) = fresh;
        const std::size_t bytes = std::size_t{At<uint8_t>(store, store_field::kCount)} << 3;
        CopyBytes(fresh, At<char*>(source, store_field::kEntries), bytes);
    }
    return store;
}

// Write (0x00619C10; ret 4: the stream): version 2 (uint16), the count and
// the first key (bytes), the first value, then each further entry's key and
// value. True when the stream has no error.
uint32_t __fastcall StoreWrite(char* store, void*, void* stream)
{
    static bool logged = false;
    FirstCall("StoreWrite", logged);
    Virtual<uint8_t>(stream, 0x1C, 2u);
    Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(store, store_field::kCount)});
    Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(store, store_field::kKey)});
    Virtual<uint8_t>(stream, 0x24, At<uint32_t>(store, store_field::kValue));
    if (At<uint8_t>(store, store_field::kCount) != 0) {
        uint8_t i = 0;
        do {
            Virtual<uint8_t>(stream, 0x14, uint32_t{At<uint8_t>(At<char*>(store, store_field::kEntries), i * 8u)});
            Virtual<uint8_t>(stream, 0x24, At<uint32_t>(At<char*>(store, store_field::kEntries), i * 8u + 4));
            ++i;
        } while (i < At<uint8_t>(store, store_field::kCount));
    }
    return NoError(stream);
}

// Read (0x00619C90; ret 4: the stream): the reverse of Write. A version that
// is not 2 answers false (AL only; the game's upper bytes are not
// reproduced). The old array is freed first; the count, key and value are
// read into the store, then an array of count * 8 bytes is made and each
// entry's key byte and value read into it. True when the stream has no
// error. The array's padding bytes stay as the allocator gave them.
uint32_t __fastcall StoreRead(char* store, void*, void* stream)
{
    static bool logged = false;
    FirstCall("StoreRead", logged);
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 2)
        return 0;
    if (void* const old = At<char*>(store, store_field::kEntries)) {
        CallCdecl<void, void*>(addr::SC4_operator_delete, old);
        At<char*>(store, store_field::kEntries) = nullptr;
    }
    Virtual<uint8_t>(stream, 0x14, store);
    Virtual<uint8_t>(stream, 0x14, store + store_field::kKey);
    Virtual<uint8_t>(stream, 0x24, store + store_field::kValue);
    const uint8_t count = At<uint8_t>(store, store_field::kCount);
    if (count != 0) {
        At<char*>(store, store_field::kEntries) =
            CallCdecl<char*, std::size_t>(addr::SC4_operator_new, std::size_t{count} * 8);
        if (At<uint8_t>(store, store_field::kCount) > 0) {
            uint8_t i = 0;
            do {
                Virtual<uint8_t>(stream, 0x14, At<char*>(store, store_field::kEntries) + i * 8u);
                Virtual<uint8_t>(stream, 0x24, At<char*>(store, store_field::kEntries) + i * 8u + 4);
                ++i;
            } while (i < At<uint8_t>(store, store_field::kCount));
        }
    }
    return NoError(stream);
}

// ====================================================================================
// Installation
// ====================================================================================

struct Detour {
    std::uintptr_t address;
    void* ours;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

// Hooks to leave out, for bisecting: OPENSC4_NETOCC_SKIP lists function
// addresses (hex, eight digits), as in src/core/netocc_state.cpp.
bool Skipped(std::uintptr_t address)
{
    char list[1024] = {};
    if (!GetEnvironmentVariableA("OPENSC4_NETOCC_SKIP", list, sizeof list))
        return false;
    char wanted[16];
    std::snprintf(wanted, sizeof wanted, "%08X", static_cast<unsigned>(address));
    return std::strstr(list, wanted) != nullptr;
}

template <std::size_t N>
bool InstallDetours(const Detour (&detours)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : detours)
        if (!addr_check::Find(detour.address)) {
            log::Write("netocc %s: no address entry for 0x%08X", what, static_cast<unsigned>(detour.address));
            return false;
        }
    unsigned installed = 0;
    for (const Detour& detour : detours) {
        if (Skipped(detour.address))
            continue;
        void* original = nullptr;
        if (!hook::Install(*addr_check::Find(detour.address), detour.ours, &original))
            return false;
        ++installed;
    }
    log::Write("netocc %s: %u detours", what, installed);
    return true;
}

const Detour kGeometry[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_ConnectsToNeighbor, ConnectsToNeighbor),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetAltitude, GetAltitude),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetNormal, GetNormal),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetOccupiedCell, GetOccupiedCell),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetBoundingBox, GetBoundingBox),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_SetRotationAndFlip, SetRotationAndFlip),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetPosition, GetPosition),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_ComputeBoundingBox, ComputeBoundingBox),
};

const Detour kPaths[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_GetPathInfo, GetPathInfo),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_ReleasePathInfo, ReleasePathInfo),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_PathConnectionExists, PathConnectionExists),
    OPENSC4_DETOUR(addr::cSC4NetworkOccupant_AddPathConnection, AddPathConnection),
};

const Detour kEdgeStore[] = {
    OPENSC4_DETOUR(addr::cSC4EdgeConnectionStore_Get, StoreGet),
    OPENSC4_DETOUR(addr::cSC4EdgeConnectionStore_Set, StoreSet),
    OPENSC4_DETOUR(addr::cSC4EdgeConnectionStore_Assign, StoreAssign),
    OPENSC4_DETOUR(addr::cSC4EdgeConnectionStore_Write, StoreWrite),
    OPENSC4_DETOUR(addr::cSC4EdgeConnectionStore_Read, StoreRead),
};

bool InstallGeometry(registry::Mode) { return InstallDetours(kGeometry, "geometry"); }
bool InstallPaths(registry::Mode) { return InstallDetours(kPaths, "paths"); }
bool InstallEdgeStore(registry::Mode) { return InstallDetours(kEdgeStore, "edgestore"); }

const bool g_registered[] = {
    registry::Register({"simulators.netocc.geometry", "simulators/netocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkOccupant_GetAltitude,
                        "Our cSC4NetworkOccupant geometry: ConnectsToNeighbor, GetAltitude, GetNormal, "
                        "GetOccupiedCell, the boxes, the position and SetRotationAndFlip (8 detours; "
                        "src/core/netocc_geometry.cpp)",
                        false, InstallGeometry}),
    registry::Register({"simulators.netocc.paths", "simulators/netocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkOccupant_GetPathInfo,
                        "Our cSC4NetworkOccupant path information and the path connection table: GetPathInfo, "
                        "ReleasePathInfo, PathConnectionExists, AddPathConnection (4 detours; "
                        "src/core/netocc_geometry.cpp)",
                        false, InstallPaths}),
    registry::Register({"simulators.netocc.edgestore", "simulators/netocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4EdgeConnectionStore_Get,
                        "Our cSC4EdgeConnectionStore (the network occupant's edge store): Get, Set, Assign, Write "
                        "and Read (5 detours; src/core/netocc_geometry.cpp)",
                        false, InstallEdgeStore}),
};

} // namespace
} // namespace opensc4::netocc_sim
