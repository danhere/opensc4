// Our subway occupant (build 641; the class map, the vtables and the fields
// are in src/core/subwayocc_sim.h): the subway's own functions, each a detour
// at the game's address, because the thunks of the other vtable parts jump
// to them and a maker class calls the constructor and the pool allocation.
// The intermediate class between the subway and the network occupant (the
// pipe's base too; `SC4PipeBase::` in data/addresses/pipeocc.toml) is not
// this file's: its functions are called at their addresses, with the
// argument counts below.
//
// Every function was written from the disassembly. The detours are plain
// functions, so they clobber ECX and EDX. The game's functions that touch
// neither are the two constant answers 0x00652CD0 and 0x00652CE0 (virtual
// calls only); the height 0x00652D70 keeps ECX and EDX and changes AX (the
// FPU status word); the pool's free 0x00653520 keeps ECX (its tail jumps
// want the pool there) and the statics 0x00653640 touch only AL when they
// are done already (its two callers test the byte first). `self` is the
// object's start (the interface part) unless a function says `part` (Write
// and Read get the cIGZSerializable part, the object + 8; the occupant
// part's Init the object + 4) or `vbase` (the piece key gets the virtual
// base, the object + 0x2D4).
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the streams (out vtable 0x00AA2310, in vtable 0x00AA2280): 0x1C
//     uint16 one argument, 0x48 GetError none;
//   - the object's own slots: 0x4 AddRef none (0x0065A860), 0x24
//     HasAnyNetworkFlag one (0x00604D30), 0x5C the piece ID none
//     (0x00459960), 0x60 GetRotation none (0x00604DC0: the byte at +0x104
//     and 0x7F in all of EAX), 0xE0 the height two (0x00652D70, ret 8), 0x158
//     the intermediate class's slot 86 one (0x0065ADD0, ret 4), 0x18C its
//     slot 99 one (0x0065A200, ret 4: 0, 1 or 2 in EAX, a byte stored
//     through the argument for 1), 0x1A8 slot 106 none (0x00653130); the
//     occupant part's 0x14 IsInitialized none (0x00458A40);
//   - the intermediate class: constructor 0x0065C330 one (the flag, ret 4),
//     destructor 0x0065C0C0 none, Init 0x0065C6B0 three (ret 0xC), occupant
//     Init 0x0065C070 none, Write 0x0065B140 one (ret 4), Read 0x0065E510
//     one (ret 4), QueryInterface 0x0065A080 two (ret 8); cdecl, no
//     arguments: 0x0065AE80 and 0x0065B040;
//   - the resource manager (global 0x00B45254): 0xC five (as in
//     src/core/netocc_geometry.cpp's GetPathInfo); the path resource: 0x44
//     one, 0x64 one, 0x8 none; the path information: 0x78 none, 0xA0 and
//     0x6C one (for 0x44, 0x64 and 0x78 the evidence is the game's own
//     frame: it reads its locals by ESP offsets after each call, which are
//     right only with those counts);
//   - the hash table's Begin 0x004B0390 (the out pair, ret 4; this the
//     table);
//   - the vector 0x0048C290 (count, ret 4; this the vector), the allocator's
//     Free 0x0090CF63 (cdecl, one), the page allocator 0x0090D8E0 (cdecl,
//     one), the pool's Grow 0x0090CF9A (none), ReleaseBlock 0x0090D031 and
//     ReopenBlock 0x0090CFF9 (one each, ret 4).
#include "core/subwayocc_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/occ_vtables.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::subwayocc_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("subwayocc: first %s", name);
}

namespace {

template <typename R, typename... Args>
R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

// The result of the stream's GetError, as the game turns it into the
// function's answer (neg al, sbb eax, eax, inc eax): 1 for no error.
uint32_t NoError(void* stream)
{
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// A word read as the float it holds.
float AsFloat(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

// ====================================================================================
// simulators.subwayocc.slots: the small slots and the height
// ====================================================================================

// Slot 0 (0x00652D00, ret 8; the other parts' thunks add or take off their
// offset and jump here): the subway's IID (its class ID) gives the object
// itself, referenced through slot 1; every other ID goes to the
// intermediate class's QueryInterface by a tail jump. The game's answer in
// the first case is AL = 1 and the rest of EAX from AddRef.
uint32_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid == kIidSubway) {
        *object = self;
        const uint32_t count = Virtual<uint32_t>(self, 0x4);
        return (count & 0xFFFFFF00u) | 1u;
    }
    return CallThis<uint32_t, uint32_t, void**>(addr::SC4PipeBase_QueryInterface, self, iid, object);
}

// Slot 101 (0x00652CD0): the address of the descriptor the intermediate
// class's occupant Init stores at +0x26C. Only EAX is touched.
std::uintptr_t __fastcall GetDescriptor(void*, void*)
{
    static bool logged = false;
    FirstCall("GetDescriptor", logged);
    return addr::cSC4SubwayOccupant_Descriptor;
}

// Slot 105 (0x00652CE0): the flags a new piece starts with (0x07000000).
// Only EAX is touched.
uint32_t __fastcall GetDefaultFlags(void*, void*)
{
    static bool logged = false;
    FirstCall("GetDefaultFlags", logged);
    return 0x07000000u;
}

// The virtual base's slot 17 (0x00652D30 behind the thunk 0x00652FC0, which
// takes the base's displacement word, zero, off the pointer; `vbase` is the
// object + 0x2D4; one argument, the out word): when the piece has any of the
// bits 0x06000000 in its network flags (slot 9) the word at +0xFC, else the
// class ID. The game's EAX is slot 9's answer in the first case and the out
// pointer in the second.
uint32_t __fastcall GetPieceKey(char* vbase, void*, uint32_t* out)
{
    static bool logged = false;
    FirstCall("GetPieceKey", logged);
    char* const self = vbase - field::kVbase;
    const uint32_t any = Virtual<uint32_t>(self, slot::kHasAnyNetworkFlag, 0x06000000u);
    if (static_cast<uint8_t>(any)) {
        *out = At<uint32_t>(self, field::kPieceKey);
        return any;
    }
    *out = kClassId;
    return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(out));
}

// Slot 56 (0x00652D70, ret 8: x and z, floats; the answer in ST0): the
// height of the piece's surface at a point, from the four corner records at
// +0x270 (slot 106 makes them), less 0.5. With u = x less corner 0's x and
// v = z less corner 0's z: when u is not greater than v, corner 0's height
// plus (corner 1's less corner 0's) * v / 16 plus (corner 2's less corner
// 1's) * u / 16; when u is greater, or one of them is not a number (the
// game's `test ah, 0x41` and `jp` after comparing u with v), corner 0's
// plus (corner 3's less corner 0's) * u / 16 plus (corner 2's less corner
// 3's) * v / 16. The FPU's operand order is kept: each difference times u
// or v, then times 0.0625 (the float at 0x00AAA250), then added; 0.5 is the
// float at 0x00AAA574. The game's function reads no other register and
// changes only AX.
double __fastcall GetAltitude(char* self, void*, float x, float z)
{
    static bool logged = false;
    FirstCall("GetAltitude", logged);
    const auto height = [self](std::size_t corner) {
        return static_cast<double>(At<float>(self, field::kCorners + corner * kCornerStride + 4));
    };
    double sum = height(0);
    const double u = static_cast<double>(x) - static_cast<double>(At<float>(self, field::kCorners));
    const double v = static_cast<double>(z) - static_cast<double>(At<float>(self, field::kCorners + 8));
    if (u <= v) {
        const double first = (height(1) - height(0)) * v;
        sum = sum + first * 0.0625;
        const double second = (height(2) - height(1)) * u;
        sum = sum + second * 0.0625;
    } else {
        const double first = (height(3) - height(0)) * u;
        sum = sum + first * 0.0625;
        const double second = (height(2) - height(3)) * v;
        sum = sum + second * 0.0625;
    }
    return sum - 0.5;
}

// ====================================================================================
// simulators.subwayocc.paths: the corner records and the path information
// ====================================================================================

// One 0x18-byte corner record copied word by word, as the game does.
void CopyCorner(char* to, char* from)
{
    for (std::size_t offset = 0; offset < kCornerStride; offset += 4)
        At<uint32_t>(to, offset) = At<uint32_t>(from, offset);
}

// The four corner records taken as they are from the vector at +0x1B4 (the
// vector's start is read again for each record; its size is not looked at).
void CopyPlainCorners(char* self)
{
    for (std::size_t i = 0; i < 4; ++i)
        CopyCorner(self + field::kCorners + i * kCornerStride, At<char*>(self, field::kPlain) + i * kCornerStride);
}

// One corner's height lowered, as the game's fld, fsub, fadd st(0), fsubr
// and fstp do it: twice the difference of two heights in the vector at
// +0x1E4 (its start read again each time; `a` and `b` are byte offsets in
// it) taken off the float.
void LowerCorner(char* self, uint32_t corner, std::size_t a, std::size_t b)
{
    char* const wide = At<char*>(self, field::kWide);
    const double difference = static_cast<double>(At<float>(wide, a)) - static_cast<double>(At<float>(wide, b));
    const double twice = difference + difference;
    float& height = At<float>(self, field::kCorners + corner * kCornerStride + 4);
    height = static_cast<float>(static_cast<double>(height) - twice);
}

// Slot 106 (0x00653130; no arguments, no answer; GetPathInfo is its only
// caller): the four corner records at +0x270. A piece whose key (+0xFC) is
// neither 0x300 nor 0x4B00 gets the four records of the vector at +0x1B4.
// For those two keys slot 99 is asked first (its argument a local byte,
// which nothing here reads); then, when the vector at +0x1E4 has fewer than
// eight records (the byte difference divided by 24 as a signed number,
// compared unsigned), the four records of the vector at +0x1B4 again. Else
// with r = 4 less the rotation (slot 24) as a byte, the records 4, 5, 7 and
// 6 of the vector at +0x1E4 go to the corners r, r + 1, r + 2 and r + 3
// (each modulo 4); and when slot 99 answered 0 each of those corners'
// heights is then lowered by twice a difference of heights in the vector:
// record 4 less record 0, 5 less 1, 7 less 11 and 6 less 10 (the game reads
// records 10 and 11 without looking for them).
void __fastcall UpdateCorners(char* self, void*)
{
    static bool logged = false;
    FirstCall("UpdateCorners", logged);
    const uint32_t key = At<uint32_t>(self, field::kPieceKey);
    if (key != kKeySpecialA && key != kKeySpecialB) {
        CopyPlainCorners(self);
        return;
    }
    uint8_t flag = 0;
    const uint32_t answer = Virtual<uint32_t>(self, slot::kUseWide, &flag);
    const int32_t bytes = static_cast<int32_t>(At<uint32_t>(self, field::kWide + 4) - At<uint32_t>(self, field::kWide));
    if (static_cast<uint32_t>(bytes / 24) < 8) {
        CopyPlainCorners(self);
        return;
    }
    const uint32_t turned = static_cast<uint8_t>(4 - Virtual<uint8_t>(self, slot::kGetRotation));
    const uint32_t corner[4] = {turned & 3, (turned + 1) & 3, (turned + 2) & 3, (turned + 3) & 3};
    constexpr std::size_t kRecord[4] = {0x60, 0x78, 0xA8, 0x90};
    for (std::size_t i = 0; i < 4; ++i)
        CopyCorner(self + field::kCorners + corner[i] * kCornerStride, At<char*>(self, field::kWide) + kRecord[i]);
    if (answer != 0)
        return;
    LowerCorner(self, corner[0], 0x64, 0x4);
    LowerCorner(self, corner[1], 0x7C, 0x1C);
    LowerCorner(self, corner[2], 0xAC, 0x10C);
    LowerCorner(self, corner[3], 0x94, 0xF4);
}

// The number of buckets of a hash table (the words between the pointers at
// +4 and +8; the byte difference shifted right as a signed number).
uint32_t BucketCount(char* table)
{
    return static_cast<uint32_t>(static_cast<int32_t>(At<uint32_t>(table, 0x8) - At<uint32_t>(table, 0x4)) >> 2);
}

// Slot 49 (0x006537E0; no arguments; the answer is the path information, the
// object at +0x140). With one already there it is just returned. Else: when
// the byte at +0x262 is clear, slot 86 (0); slot 106 (the corner records);
// the resource key {0x296678F7, 0xA966883F, instance}, the instance the
// piece ID (slot 23) with the bits 0x06000000, or the path ID override when
// there is one; the resource manager is asked for it as class 0xA9667793.
// With a resource: its slot 0x44 (0), its slot 0x64 (this) gives the path
// information, the resource is released. With path information: its slot
// 0x78 answers a hash table of paths (Begin 0x004B0390 gives the first node
// and the table; a node: +0 the next one, +4 the key, +8 and +0xC the start
// and the end of its points, 12 bytes each, +0x14 a float, the length); for
// every node the length is cleared and for every point the height (slot 56
// for its x and z) is stored as its y, and from the second point on the
// distance from the point before, sqrt(dz * dz + dy * dy + dx * dx) as the
// FPU makes it, is added to the length (the point before starts as three
// times the word at 0x00B46824 and is not read for the first point). The
// next node is the node's link or, at the end of a chain, the first node of
// the next bucket that has one, after the bucket of the key (the table's
// buckets are the words between +4 and +8). Then the information is stored
// at +0x140. Whatever the path information is now, slot 0xA0 (this) is
// called on it, the eight words of the path connection table (+0x144)
// zeroed and slot 0x6C (this) called.
void* __fastcall GetPathInfo(char* self, void*)
{
    static bool logged = false;
    FirstCall("GetPathInfo", logged);
    if (At<void*>(self, field::kPathInfo))
        return At<void*>(self, field::kPathInfo);
    if (!At<uint8_t>(self, field::kBuilt))
        Virtual<void>(self, slot::kBuild, 0u);
    Virtual<void>(self, slot::kUpdateCorners);
    void* resource = nullptr;
    struct Key {
        uint32_t type;
        uint32_t group;
        uint32_t instance;
    } key;
    key.instance = Virtual<uint32_t>(self, slot::kPieceId) | 0x06000000u;
    const uint32_t pathOverride = At<uint32_t>(self, field::kPathOverride);
    key.type = 0x296678F7u;
    key.group = 0xA966883Fu;
    if (pathOverride != 0)
        key.instance = pathOverride;
    void* const manager = Global<void*>(addr::cSC4NetworkOccupant_ResourceManager);
    Virtual<uint8_t>(manager, 0xC, &key, 0xA9667793u, &resource, 0u, 0u);
    if (resource) {
        Virtual<void>(resource, 0x44, 0u);
        void* const info = Virtual<void*>(resource, 0x64, self);
        Virtual<uint32_t>(resource, 0x8);
        if (info) {
            struct Iterator {
                char* node;
                char* table;
            } first;
            CallThis<void, Iterator*>(addr::SC4HashTable_Begin, Virtual<void*>(info, 0x78), &first);
            char* node = first.node;
            char* const table = first.table;
            while (node) {
                char* point = At<char*>(node, 0x8);
                At<uint32_t>(node, 0x14) = 0;
                const uint32_t start = Global<uint32_t>(addr::cSC4SubwayOccupant_PathStart);
                uint32_t before[3] = {start, start, start};
                if (point != At<char*>(node, 0xC)) {
                    do {
                        const double height =
                            Virtual<double>(self, slot::kGetAltitude, At<uint32_t>(point, 0), At<uint32_t>(point, 8));
                        At<float>(point, 4) = static_cast<float>(height);
                        if (point != At<char*>(node, 0x8)) {
                            const double dx = static_cast<double>(At<float>(point, 0)) -
                                              static_cast<double>(AsFloat(before[0]));
                            const double dy = static_cast<double>(At<float>(point, 4)) -
                                              static_cast<double>(AsFloat(before[1]));
                            const double dz = static_cast<double>(At<float>(point, 8)) -
                                              static_cast<double>(AsFloat(before[2]));
                            double squares = dz * dz + dy * dy;
                            squares = squares + dx * dx;
                            const double length = std::sqrt(squares) + static_cast<double>(At<float>(node, 0x14));
                            At<float>(node, 0x14) = static_cast<float>(length);
                        }
                        before[1] = At<uint32_t>(point, 4);
                        before[0] = At<uint32_t>(point, 0);
                        before[2] = At<uint32_t>(point, 8);
                        point += 0xC;
                    } while (point != At<char*>(node, 0xC));
                }
                char* next = At<char*>(node, 0x0);
                if (!next) {
                    const uint32_t buckets = BucketCount(table);
                    uint32_t index = At<uint32_t>(node, 0x4) % buckets;
                    const uint32_t size = BucketCount(table);
                    for (;;) {
                        ++index;
                        if (index >= size)
                            break;
                        next = At<char*>(At<char*>(table, 0x4), index * 4);
                        if (next)
                            break;
                    }
                }
                node = next;
            }
            At<void*>(self, field::kPathInfo) = info;
        }
    }
    if (void* const now = At<void*>(self, field::kPathInfo)) {
        Virtual<void>(now, 0xA0, self);
        for (std::size_t i = 0; i < 8; ++i)
            At<uint32_t>(self, field::kPathTable + 4 * i) = 0;
        Virtual<void>(At<void*>(self, field::kPathInfo), 0x6C, self);
    }
    return At<void*>(self, field::kPathInfo);
}

// ====================================================================================
// simulators.subwayocc.init: the statics, the two Inits and the save record
// ====================================================================================

// 0x00653640 (cdecl, none): set up once, marked by the byte at 0x00B46820.
// Five distances between the points of a table (0x00B0C960, 24-byte records
// whose x is at +0 and z at +8) picked by two index tables (0x00B0CB58 and
// 0x00B0CB80), sqrt(dz * dz + dx * dx) as the FPU makes it (53-bit
// precision, so double) and stored as floats in a vector of five (made and
// freed by the game's vector and allocator). Then the ratios of them in the
// statics (0x00B467F8 to 0x00B4681C: ones, and the ratios v1/(v1+v4),
// v2/(v3+v2), v3/(v2+v3) and v4/(v1+v4) twice each), the done byte, and,
// when the network occupant's statics (0x00B45250) are not done, the
// intermediate class's 0x0065B040, whose answer is the result. Otherwise 1.
// Already done: 0. The same code as the pipe's 0x00648A90 with other
// tables.
uint8_t __cdecl InitStatics()
{
    static bool logged = false;
    FirstCall("InitStatics", logged);
    const std::uintptr_t statics = addr::cSC4SubwayOccupant_Statics;
    if (Global<uint8_t>(statics + kStaticsDone))
        return 0;
    uint32_t vector[3];
    CallThis<void*, uint32_t>(addr::SC4DwordVector_ConstructZeroed, vector, 5u);
    float* const values = reinterpret_cast<float*>(vector[0]);
    for (uint32_t i = 0; i < 5; ++i) {
        const uint32_t from = Global<uint32_t>(addr::cSC4SubwayOccupant_FromPoints + 4 * i) * 24u;
        const uint32_t to = Global<uint32_t>(addr::cSC4SubwayOccupant_ToPoints + 4 * i) * 24u;
        const std::uintptr_t a = addr::cSC4SubwayOccupant_Points + from;
        const std::uintptr_t b = addr::cSC4SubwayOccupant_Points + to;
        const double dx = static_cast<double>(Global<float>(a)) - static_cast<double>(Global<float>(b));
        const double dz = static_cast<double>(Global<float>(a + 8)) - static_cast<double>(Global<float>(b + 8));
        values[i] = static_cast<float>(std::sqrt(dz * dz + dx * dx));
    }
    const float one = 1.0f;
    Global<float>(statics + 0x14) = one;
    Global<float>(statics + 0x10) = one;
    const uint8_t networkDone = Global<uint8_t>(addr::cSC4NetworkOccupant_StaticsDone);
    const double v1 = static_cast<double>(values[1]), v2 = static_cast<double>(values[2]);
    const double v3 = static_cast<double>(values[3]), v4 = static_cast<double>(values[4]);
    const float first = static_cast<float>(v1 / (v1 + v4));
    Global<float>(statics + 0x18) = first;
    Global<float>(statics + 0xC) = first;
    const float second = static_cast<float>(v2 / (v3 + v2));
    Global<float>(statics + 0x1C) = second;
    Global<float>(statics + 0x8) = second;
    const float third = static_cast<float>(v3 / (v2 + v3));
    Global<float>(statics + 0x20) = third;
    Global<float>(statics + 0x4) = third;
    Global<uint8_t>(statics + kStaticsDone) = 1;
    const float fourth = static_cast<float>(v4 / (v1 + v4));
    Global<float>(statics + 0x24) = fourth;
    Global<float>(statics + 0x0) = fourth;
    if (!networkDone) {
        const uint8_t result = CallCdecl<uint8_t>(addr::SC4PipeBase_InitStatics);
        CallCdecl<void, void*>(addr::SC4_Free, values);
        return result;
    }
    CallCdecl<void, void*>(addr::SC4_Free, values);
    return 1;
}

// The occupant part's slot 3 (0x00653770; `part` is the object + 4): the
// statics first (when not done), and an object that is not initialised yet
// goes through the intermediate class's occupant Init (on the part), then
// 0x0065AE80, which reads no register. AL is 1.
uint8_t __fastcall OccupantInit(char* part, void*)
{
    static bool logged = false;
    FirstCall("OccupantInit", logged);
    if (!Global<uint8_t>(addr::cSC4SubwayOccupant_Statics + kStaticsDone))
        CallCdecl<uint8_t>(addr::cSC4SubwayOccupant_InitStatics);
    if (!Virtual<uint8_t>(part, 0x14)) {
        CallThis<uint8_t>(addr::SC4PipeBase_OccupantInit, part);
        CallCdecl<uint8_t>(addr::SC4PipeBase_InitModels);
    }
    return 1;
}

// The interface's slot 3 (0x006537A0, one argument, handed on): the statics
// first (when not done); an object that is not initialised yet goes through
// the intermediate class's Init with the argument, the class ID and the
// descriptor. AL is 1.
uint8_t __fastcall Init(char* self, void*, void* description)
{
    static bool logged = false;
    FirstCall("Init", logged);
    if (!Global<uint8_t>(addr::cSC4SubwayOccupant_Statics + kStaticsDone))
        CallCdecl<uint8_t>(addr::cSC4SubwayOccupant_InitStatics);
    if (!Virtual<uint8_t>(self + field::kOccupant, 0x14))
        CallThis<uint8_t, void*, uint32_t, uint32_t>(addr::SC4PipeBase_Init, self, description, kClassId,
                                                     static_cast<uint32_t>(addr::cSC4SubwayOccupant_Descriptor));
    return 1;
}

// +0x8 slot 3 (0x00653010; `part` is the object + 8). Version 2, then the
// intermediate class's record (which holds the network occupant's; its
// answer is not looked at), and nothing of the subway's own. The answer is
// the stream's state. The layout is in tools/re/save_schemas/subwayocc.py.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    Virtual<uint8_t>(stream, 0x1C, 2u);
    CallThis<uint8_t, void*>(addr::SC4PipeBase_Write, part, stream);
    return NoError(stream);
}

// +0x8 slot 4 (0x00653040; `part` is the object + 8): version 2 only. The
// game reads the version into its argument's stack slot (the stream pointer
// until the stream writes two bytes of it), so it starts with that value
// here. Another version: AL cleared, the rest of EAX as the stream's read
// left it. The intermediate class's record, and when that is good the byte
// at +0x260 set to 1. The answer is the stream's state.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    uint32_t version = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
    const uint32_t read = Virtual<uint32_t>(stream, 0x1C, &version);
    if (static_cast<uint16_t>(version) != 2)
        return read & 0xFFFFFF00u;
    if (CallThis<uint8_t, void*>(addr::SC4PipeBase_Read, part, stream))
        At<uint8_t>(part, field::kDirty - field::kSerializable) = 1;
    return NoError(stream);
}

// ====================================================================================
// simulators.subwayocc.object: construction and destruction
// ====================================================================================

// simulators.subwayocc.own: with it the objects carry our copies of the
// class's seven vtables (107, 34, 6, 25, 5, 1 and 19 slots) instead of the
// game's tables. The copies are taken at the first use (BuildOwnVtables, with
// the installation: it needs the object group's detour list).
bool g_own = false;
occ_vtables::VtableCopy<107> g_vtable;
occ_vtables::VtableCopy<34> g_vtable4;
occ_vtables::VtableCopy<6> g_vtable8;
occ_vtables::VtableCopy<25> g_vtableC;
occ_vtables::VtableCopy<5> g_vtable20;
occ_vtables::VtableCopy<1> g_vtable38;
occ_vtables::VtableCopy<19> g_vbaseVtable;
void BuildOwnVtables();

// The vtables the constructor and the destructor store (the game's, or with
// simulators.subwayocc.own our copies of them): the five parts', then the
// virtual base's, found through the table at +0x3C as the game does (the table
// is read twice).
void SetVtables(char* self)
{
    if (g_own && !g_vtable.Built())
        BuildOwnVtables();
    At<std::uintptr_t>(self, 0x0) = g_own ? g_vtable.Address() : addr::cSC4SubwayOccupant_vftable;
    At<std::uintptr_t>(self, field::kOccupant) = g_own ? g_vtable4.Address() : addr::cSC4SubwayOccupant_vftable_4;
    At<std::uintptr_t>(self, field::kSerializable) = g_own ? g_vtable8.Address() : addr::cSC4SubwayOccupant_vftable_8;
    At<std::uintptr_t>(self, field::kHolder) = g_own ? g_vtableC.Address() : addr::cSC4SubwayOccupant_vftable_C;
    At<std::uintptr_t>(self, field::kExemplarPart) = g_own ? g_vtable20.Address() : addr::cSC4SubwayOccupant_vftable_20;
    At<std::uintptr_t>(self, field::kModelPart) = g_own ? g_vtable38.Address() : addr::cSC4SubwayOccupant_vftable_38;
    const uint32_t displacement = At<uint32_t>(At<char*>(self, field::kVbtable), 4);
    At<std::uintptr_t>(self, displacement + field::kVbtable) =
        g_own ? g_vbaseVtable.Address() : addr::cSC4SubwayOccupant_vbase_vftable;
    const uint32_t again = At<uint32_t>(At<char*>(self, field::kVbtable), 4);
    At<uint32_t>(self, again + field::kModelPart) = again - 0x298;
}

// The constructor (0x006530B0; one argument: non-zero for the most derived
// object, which sets the virtual base table at +0x3C and the abstract
// vtable at +0x2D4 itself). The intermediate class's constructor with 0 and
// the vtables; the four corner records are left as the pool's memory holds
// them. The game's EAX is the object.
void* __fastcall Construct(char* self, void*, uint32_t mostDerived)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    if (mostDerived) {
        At<std::uintptr_t>(self, field::kVbtable) = addr::cSC4SubwayOccupant_vbtable;
        At<std::uintptr_t>(self, field::kVbase) = addr::cSC4NetworkOccupant_vbase_abstract_vftable;
    }
    CallThis<void*, uint32_t>(addr::SC4PipeBase_cSC4PipeBase, self, 0u);
    SetVtables(self);
    return self;
}

// An item of the subway pool (0x00652FE0, and inline in the factory): the
// current page's free head, the pool refilled when the page has none
// (0x0090CF9A returns the new head); the page is read again after. The
// item's memory is not cleared.
void* PoolAllocate()
{
    char* const pool = reinterpret_cast<char*>(addr::cSC4SubwayOccupant_Pool);
    void* item = At<void*>(At<char*>(pool, 0x4), 0x8);
    if (!item)
        item = CallThis<void*>(addr::SC4EdgePool_Grow, pool);
    At<void*>(At<char*>(pool, 0x4), 0x8) = *static_cast<void**>(item);
    ++At<uint32_t>(At<char*>(pool, 0x4), 0xC);
    return item;
}

// An item of the pool back in its page (the page header is at the address
// with its low 16 bits clear): onto the page's free list, the used count
// down by one; at 0 the page is released, at 0x40000043 opened again.
void PoolReturn(void* pool, char* item)
{
    char* const page = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(item) & kPageMask);
    At<void*>(item, 0x0) = At<void*>(page, 0x8);
    const uint32_t used = At<uint32_t>(page, 0xC) - 1;
    At<void*>(page, 0x8) = item;
    At<uint32_t>(page, 0xC) = used;
    if (used == 0)
        CallThis<void, void*>(addr::SC4Pool_ReleaseBlock, pool, page);
    else if (used == kReopenCount)
        CallThis<void, void*>(addr::SC4Pool_ReopenBlock, pool, page);
}

// The deleting destructor (0x006535A0, one argument): the vtables as the
// constructor stores them, the intermediate class's destructor, and with
// flag 1 the memory back to the pool at 0x00B0CDB8 (no test for null). The
// result is the object.
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestructor", logged);
    SetVtables(self);
    CallThis<void>(addr::SC4PipeBase_Destroy, self);
    if (flags & 1)
        PoolReturn(reinterpret_cast<void*>(addr::cSC4SubwayOccupant_Pool), self);
    return self;
}

// The pool's free (0x00653520; slot 1 of the pool's tables at 0x00AAA578 and
// 0x00AAA590, ECX the pool, one argument: the item; null does nothing). The
// game's function ends in jumps to ReleaseBlock and ReopenBlock with the
// page in the argument's place; nothing in the exe calls it by address (the
// deleting destructor has its own copy of the code).
void __fastcall PoolFree(void* pool, void*, char* item)
{
    static bool logged = false;
    FirstCall("PoolFree", logged);
    if (!item)
        return;
    PoolReturn(pool, item);
}

// The pool's page set-up (0x00652E00; slot 2 of the same tables, called by
// the pool's Grow 0x0090CF9A with the pool in ECX, which is not read, and
// once at start-up by the static initialiser 0x00A73F10, before this
// detour is in place): a page of 64 KB (the allocator's order 0x10), 90 items of 0x2D8
// bytes from +0x10 chained upwards (each item's first word is the address of
// the next one, the last has 0), the page's free list at +8 the lowest item
// and its used count at +0xC zero. The result is the page.
char* __cdecl InitPoolPage()
{
    static bool logged = false;
    FirstCall("InitPoolPage", logged);
    char* const page = CallCdecl<char*, uint32_t>(addr::SC4_AllocatePoolPage, 0x10u);
    char* item = page + 0xFD28;
    std::uintptr_t previous = 0;
    char* lowest = nullptr;
    for (uint32_t i = 0; i < kPageItems; ++i) {
        At<std::uintptr_t>(item, 0) = previous;
        lowest = item;
        previous = reinterpret_cast<std::uintptr_t>(item);
        item -= kObjectSize;
    }
    At<void*>(page, 0x8) = lowest;
    At<uint32_t>(page, 0xC) = 0;
    return page;
}

// The pool allocation (0x00652FE0; cdecl, the size 0x2D8 pushed by the maker
// class is not read).
void* __cdecl Allocate(std::size_t)
{
    static bool logged = false;
    FirstCall("Allocate", logged);
    return PoolAllocate();
}

// The factory (0x00653560): an item of the pool, constructed as the most
// derived object by the constructor at its address. The result is the
// occupant part (the object + 4), or null if the constructor gave none (it
// never does).
void* __cdecl Create()
{
    static bool logged = false;
    FirstCall("Create", logged);
    char* const object =
        CallThis<char*, uint32_t>(addr::cSC4SubwayOccupant_cSC4SubwayOccupant, PoolAllocate(), 1u);
    return object ? object + field::kOccupant : nullptr;
}

// ====================================================================================
// Installation
// ====================================================================================

struct Detour {
    std::uintptr_t address;
    void* ours;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

// Hooks to leave out, for bisecting: OPENSC4_SUBWAYOCC_SKIP lists function
// addresses (hex, eight digits), as OPENSC4_NETOCC_SKIP does.
bool Skipped(std::uintptr_t address)
{
    char list[1024] = {};
    if (!GetEnvironmentVariableA("OPENSC4_SUBWAYOCC_SKIP", list, sizeof list))
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
            log::Write("subwayocc %s: no address entry for 0x%08X", what, static_cast<unsigned>(detour.address));
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
    log::Write("subwayocc %s: %u detours", what, installed);
    return true;
}

const Detour kSlots[] = {
    OPENSC4_DETOUR(addr::cSC4SubwayOccupant_QueryInterface, QueryInterface),
    OPENSC4_DETOUR(addr::cSC4SubwayOccupant_GetDescriptor, GetDescriptor),
    OPENSC4_DETOUR(addr::cSC4SubwayOccupant_GetDefaultFlags, GetDefaultFlags),
    OPENSC4_DETOUR(addr::cSC4SubwayOccupant_GetPieceKey, GetPieceKey),
    OPENSC4_DETOUR(addr::cSC4SubwayOccupant_GetAltitude, GetAltitude),
};

const Detour kPaths[] = {
    OPENSC4_DETOUR(addr::cSC4SubwayOccupant_UpdateCorners, UpdateCorners),
    OPENSC4_DETOUR(addr::cSC4SubwayOccupant_GetPathInfo, GetPathInfo),
};

const Detour kInit[] = {
    OPENSC4_DETOUR(addr::cSC4SubwayOccupant_InitStatics, InitStatics),
    OPENSC4_DETOUR(addr::cSC4SubwayOccupant_OccupantInit, OccupantInit),
    OPENSC4_DETOUR(addr::cSC4SubwayOccupant_Init, Init),
    OPENSC4_DETOUR(addr::cSC4SubwayOccupant_Write, Write),
    OPENSC4_DETOUR(addr::cSC4SubwayOccupant_Read, Read),
};

const Detour kObject[] = {
    OPENSC4_DETOUR(addr::cSC4SubwayOccupant_cSC4SubwayOccupant, Construct),
    OPENSC4_DETOUR(addr::cSC4SubwayOccupant_DeletingDestructor, DeletingDestructor),
    OPENSC4_DETOUR(addr::cSC4SubwayOccupant_PoolFree, PoolFree),
    OPENSC4_DETOUR(addr::cSC4SubwayOccupant_InitPoolPage, InitPoolPage),
    OPENSC4_DETOUR(addr::cSC4SubwayOccupant_Allocate, Allocate),
    OPENSC4_DETOUR(addr::cSC4SubwayOccupant_Create, Create),
};

bool InstallSlots(registry::Mode) { return InstallDetours(kSlots, "slots"); }
bool InstallPaths(registry::Mode) { return InstallDetours(kPaths, "paths"); }
bool InstallInit(registry::Mode) { return InstallDetours(kInit, "init"); }
bool InstallObject(registry::Mode) { return InstallDetours(kObject, "object"); }

// simulators.subwayocc.own: the copies, taken at the first construction or
// destruction that wants them, so after every PostAppInit replacement. Each is
// the game's table word for word. An entry that holds a function of the object
// group directly would become ours without the detour; none of this class's
// does (the deleting destructor is behind thunks only), so the count logged is
// 0. Every other entry stays the game's address or adjustor thunk, which the
// other groups' detours still decide.
void BuildOwnVtables()
{
    g_vtable.Build(reinterpret_cast<const void*>(addr::cSC4SubwayOccupant_vftable));
    g_vtable4.Build(reinterpret_cast<const void*>(addr::cSC4SubwayOccupant_vftable_4));
    g_vtable8.Build(reinterpret_cast<const void*>(addr::cSC4SubwayOccupant_vftable_8));
    g_vtableC.Build(reinterpret_cast<const void*>(addr::cSC4SubwayOccupant_vftable_C));
    g_vtable20.Build(reinterpret_cast<const void*>(addr::cSC4SubwayOccupant_vftable_20));
    g_vtable38.Build(reinterpret_cast<const void*>(addr::cSC4SubwayOccupant_vftable_38));
    g_vbaseVtable.Build(reinterpret_cast<const void*>(addr::cSC4SubwayOccupant_vbase_vftable));
    unsigned redirected = 0;
    for (const Detour& detour : kObject) {
        if (Skipped(detour.address))
            continue;
        const void* const game = reinterpret_cast<const void*>(detour.address);
        redirected += occ_vtables::RedirectAll(game, detour.ours, g_vtable, g_vtable4, g_vtable8, g_vtableC, g_vtable20,
                                               g_vtable38, g_vbaseVtable);
    }
    log::Write("subwayocc own: vtables copied (107, 34, 6, 25, 5, 1 and 19 slots), %u entries redirected", redirected);
}

// simulators.subwayocc.own installs nothing: it only says which tables the
// object group's constructor and destructor store.
bool InstallOwn(registry::Mode)
{
    g_own = true;
    return true;
}

const bool g_registered[] = {
    registry::Register({"simulators.subwayocc.slots", "simulators/subwayocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4SubwayOccupant_QueryInterface,
                        "Our subway occupant small slots: QueryInterface, the descriptor, the default flags, the "
                        "virtual base's piece key and the height at a point (5 detours; src/core/subwayocc_sim.cpp)",
                        false, InstallSlots}),
    registry::Register({"simulators.subwayocc.paths", "simulators/subwayocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4SubwayOccupant_GetPathInfo,
                        "Our subway occupant corner records (slot 106) and GetPathInfo (2 detours; "
                        "src/core/subwayocc_sim.cpp)",
                        false, InstallPaths}),
    registry::Register({"simulators.subwayocc.init", "simulators/subwayocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4SubwayOccupant_InitStatics,
                        "Our subway occupant statics (0x00653640), the two Inits and the save record: Write and "
                        "Read (version 2; 5 detours; src/core/subwayocc_sim.cpp)",
                        false, InstallInit}),
    registry::Register({"simulators.subwayocc.object", "simulators/subwayocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4SubwayOccupant_cSC4SubwayOccupant,
                        "Our subway occupant constructor, deleting destructor, the pool's free, page set-up and "
                        "allocation and the factory (6 detours; src/core/subwayocc_sim.cpp)",
                        false, InstallObject}),
    registry::Register({"simulators.subwayocc.own", "simulators/subwayocc", registry::Kind::Api,
                        registry::Phase::PostAppInit, addr::cSC4SubwayOccupant_vftable,
                        "Our own subway occupant: objects carry our copies of the class's seven vtables; only has an "
                        "effect with simulators.subwayocc.object (src/core/subwayocc_sim.cpp)",
                        false, InstallOwn}),
};

} // namespace
} // namespace opensc4::subwayocc_sim
