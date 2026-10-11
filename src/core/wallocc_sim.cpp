// Our cSC4LotRetainingWallOccupant (build 641; the class map, the vtables
// slot by slot and the fields are in wallocc_sim.h): the class's own slots
// in its five vtables, the save record's Write and Read, the free functions
// of its file that make and remove a lot's wall and hash its sides, and our
// own object (the constructor, the factory and the pool allocation by
// detour, the deleting destructor and identity from copies of the vtables).
//
// The occupant base (cSC4Occupant: its constructor, destructor,
// QueryInterface, AddRef, Release, Init, Shutdown, Write, Read) is every
// occupant class's (src/core/occbase_sim.cpp): it is called at its address.
// The class's getters and setters of one instruction (the lot, the edge
// flags, the wall type, the position's getter) are code the linker folded
// with other classes' vtables; they stay the game's. So do the pool's own
// page maker and item free, and the code that draws: the walls' drawable
// builder 0x005E3400 and its lookup 0x005E2FC0.
//
// Every virtual call below has its argument count from the callee's own
// `ret N`:
//   - the streams' vtables 0x00AA2310 (out; 0x005D1E30, 0x005D1E70,
//     0x005D1EB0) and 0x00AA2280 (in; 0x005D19C0, 0x005D19F0, 0x005D1A20):
//     slots 0x1C, 0x20, 0x24 one argument, 0x48 (0x005D16B0) none;
//   - the lot (0x00AB19B0): AddRef 0x4 and Release 0x8 none, 0x24
//     (0x00A6CAB0) none, GetFlags 0x30 (0x006C2030) none, SetFlags 0x34
//     (0x006BF220) one, GetBoundingRect 0x38 (0x006BF320) one (16 bytes
//     written), GetAltitude 0x40 (0x006C2060) none (a float in ST0),
//     GetLocation 0x54 (0x006BF2E0) two (a dword each), GetState 0x84
//     (0x006C2100) none;
//   - the lot configuration (0x00A8CEB8): 0x70 (0x004778A0) none;
//   - the lot manager (0x00AB37C0): GetLot 0x18 (0x0072B410) three, 0x2C
//     (0x0072A790) and 0x30 (0x0072A820) three each (16 bytes written);
//   - the terrain (0x00AB3D18): 0x38 (0x007413E0) two, a float in ST0;
//   - the occupant manager (0x00A8F7F0): InsertOccupant 0x50 (0x0049EF60)
//     two, RemoveOccupant 0x54 (0x0049F240) three, 0x84 (0x0049DCE0) four;
//   - the building development simulator (0x00AB0AF8): 0x74 (0x006983C0)
//     two;
//   - the wall itself: AddRef 0x4 and Release 0x8 none, AsOccupant 0xC none,
//     SetSize 0x10, SetLot 0x18, SetEdgeFlags 0x20 (0x007B62D0),
//     SetEdgeHashArray 0x28 and SetRetainingWallType 0x30 (0x006CE240) one
//     each; of its occupant part Init 0xC (0x005DEA50) none, IsInitialized
//     0x14 (0x00458A40) none, SetPosition 0x24 one.
#include "core/wallocc_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::wallocc_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("wallocc: first %s", name);
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

// --- The primary interface (`self` is the object) ---------------------------------

// Slot 0 (0x005E28C0): the wall interface here (the object, counted
// through its slot 1), everything else from the occupant base (a jump, with
// the object + 4). The class ID is not answered here.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("QueryInterface", logged);
    if (iid == kIidWall) {
        *object = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    return CallThis<uint8_t, uint32_t, void**>(addr::cSC4Occupant_QueryInterface, self + field::kOccupant, iid, object);
}

// The other parts' slot 0 (0x005E29A0, 0x005E29D0, 0x005E2A00, 0x005E2A40).
template <std::size_t Offset>
uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return QueryInterface(part - Offset, nullptr, iid, object);
}

// Slot 4 (0x005E2C90): the three floats copied as they are. The game
// returns AL = 1; the rest of EAX is the third float's.
uint32_t __fastcall SetSize(char* self, void*, const uint32_t* size)
{
    static bool logged = false;
    FirstCall("SetSize", logged);
    At<uint32_t>(self, field::kSize) = size[0];
    At<uint32_t>(self, field::kSize + 4) = size[1];
    const uint32_t last = size[2];
    At<uint32_t>(self, field::kSize + 8) = last;
    return (last & 0xFFFFFF00u) | 1u;
}

// Slot 6 (0x005E2CB0): another lot than the one held is referenced (its
// slot 1) and stored, then the old one released (its slot 2); the same lot
// again changes nothing. The game sets no result.
void __fastcall SetLot(char* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("SetLot", logged);
    void* const old = At<void*>(self, field::kLot);
    if (lot == old)
        return;
    if (lot)
        Virtual<uint32_t>(lot, 0x4);
    At<void*>(self, field::kLot) = lot;
    if (old)
        Virtual<uint32_t>(old, 0x8);
}

// Slot 9 (0x005E2910): the four hashes copied out. EAX is the buffer.
uint32_t* __fastcall GetEdgeHashArray(char* self, void*, uint32_t* hashes)
{
    static bool logged = false;
    FirstCall("GetEdgeHashArray", logged);
    for (int i = 0; i < 4; ++i)
        hashes[i] = At<uint32_t>(self, field::kEdgeHashes + 4 * i);
    return hashes;
}

// Slot 10 (0x005E2930): the four hashes copied in. EAX is the last.
uint32_t __fastcall SetEdgeHashArray(char* self, void*, const uint32_t* hashes)
{
    static bool logged = false;
    FirstCall("SetEdgeHashArray", logged);
    for (int i = 0; i < 3; ++i)
        At<uint32_t>(self, field::kEdgeHashes + 4 * i) = hashes[i];
    const uint32_t last = hashes[3];
    At<uint32_t>(self, field::kEdgeHashes + 12) = last;
    return last;
}

// --- The occupant part (`part` is the object + 4) ---------------------------------

// Slot 4 (0x005E2A80): when the occupant is initialized (its slot 5) the
// lot is cleared and released, then the base's Shutdown. AL is 1 either
// way; the rest of EAX is what the last call left.
uint32_t __fastcall Shutdown(char* part, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    char* const self = part - field::kOccupant;
    uint32_t last = Virtual<uint32_t>(part, occupant::kIsInitialized);
    if (last & 0xFFu) {
        if (void* const lot = At<void*>(self, field::kLot)) {
            At<void*>(self, field::kLot) = nullptr;
            Virtual<uint32_t>(lot, 0x8);
        }
        last = CallThis<uint32_t>(addr::cSC4Occupant_Shutdown, part);
    }
    return (last & 0xFFFFFF00u) | 1u;
}

// Slot 9 (0x005E2C10): the three floats copied as they are. EAX is the
// last.
uint32_t __fastcall SetPosition(char* part, void*, const uint32_t* position)
{
    static bool logged = false;
    FirstCall("SetPosition", logged);
    char* const self = part - field::kOccupant;
    At<uint32_t>(self, field::kPosition) = position[0];
    At<uint32_t>(self, field::kPosition + 4) = position[1];
    const uint32_t last = position[2];
    At<uint32_t>(self, field::kPosition + 8) = last;
    return last;
}

// Slot 10 (0x005E2C30): the box of the walls under the position. In x and
// z the position less and plus half the size; in y from the position less
// the whole height up to the position (copied as it is). The products and
// sums are made on the FPU and stored as floats, in the game's order: low
// x, high x, low y, high y, low z, high z. EAX is left holding the first
// pointer.
float* __fastcall GetBoundingBox(char* part, void*, float* low, float* high)
{
    static bool logged = false;
    FirstCall("GetBoundingBox", logged);
    char* const self = part - field::kOccupant;
    const double half = static_cast<double>(Global<float>(addr::SC4_FloatHalf));
    low[0] = static_cast<float>(static_cast<double>(At<float>(self, field::kPosition)) -
                                static_cast<double>(At<float>(self, field::kSize)) * half);
    high[0] = static_cast<float>(static_cast<double>(At<float>(self, field::kSize)) * half +
                                 static_cast<double>(At<float>(self, field::kPosition)));
    low[1] = static_cast<float>(static_cast<double>(At<float>(self, field::kPosition + 4)) -
                                static_cast<double>(At<float>(self, field::kSize + 4)));
    std::memcpy(&high[1], self + field::kPosition + 4, 4);
    low[2] = static_cast<float>(static_cast<double>(At<float>(self, field::kPosition + 8)) -
                                static_cast<double>(At<float>(self, field::kSize + 8)) * half);
    high[2] = static_cast<float>(static_cast<double>(At<float>(self, field::kSize + 8)) * half +
                                 static_cast<double>(At<float>(self, field::kPosition + 8)));
    return low;
}

// --- The other parts ----------------------------------------------------------------

// +0x8 slot 5 (0x005E2990).
uint32_t __fastcall GetGZCLSID(void*, void*)
{
    static bool logged = false;
    FirstCall("GetGZCLSID", logged);
    return kClassId;
}

// --- Save ---------------------------------------------------------------------------

// +0x8 slot 3 (0x005E2AB0; `part` = object + 8): version 2, the occupant
// base's record, then the wall type (slot 0x24), the edge flags and the
// four edge hashes (slot 0x20 each), the position, the size and a reference
// to the lot. The layout is in tools/re/save_schemas/wallocc.py. When the
// base fails the class's own fields are left out, and the result is in both
// cases whether the stream is free of error after.
uint32_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - field::kSerializable;
    Virtual<uint8_t>(stream, 0x1C, 2u);
    if (CallThis<uint8_t, void*>(addr::cSC4Occupant_Write, part, stream)) {
        Virtual<uint8_t>(stream, 0x24, At<uint32_t>(self, field::kWallType));
        Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kEdgeFlags));
        for (int i = 0; i < 4; ++i)
            Virtual<uint8_t>(stream, 0x20, At<uint32_t>(self, field::kEdgeHashes + 4 * i));
        CallCdecl<void*, void*, void*>(addr::SC4Stream_WriteVector3, stream, self + field::kPosition);
        CallCdecl<void*, void*, void*>(addr::SC4Stream_WriteVector3, stream, self + field::kSize);
        CallCdecl<uint8_t, void*, void*>(addr::SC4Stream_WriteReference, stream, At<void*>(self, field::kLot));
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// +0x8 slot 4 (0x005E2B40; `part` = object + 8): version 2 only. The game
// reads the version into its argument's stack slot (the stream pointer
// until the stream writes two bytes of it), so it starts with that value
// here. Another version: AL cleared, the rest of EAX as the stream's read
// left it. As in Write, a failing base skips the fields and the result is
// the stream's state. After each hash is read, a hash that is not zero sets
// its side's bit in the edge flags just read. The lot held (none in an
// object the factory just made) is cleared and released before the
// reference is read, as a cISC4Lot, into the field.
uint32_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - field::kSerializable;
    uint32_t version = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(stream));
    const uint32_t read = Virtual<uint32_t>(stream, 0x1C, &version);
    if (static_cast<uint16_t>(version) != 2)
        return read & 0xFFFFFF00u;
    if (CallThis<uint8_t, void*>(addr::cSC4Occupant_Read, part, stream)) {
        Virtual<uint8_t>(stream, 0x24, self + field::kWallType);
        Virtual<uint8_t>(stream, 0x20, self + field::kEdgeFlags);
        for (int i = 0; i < 4; ++i) {
            Virtual<uint8_t>(stream, 0x20, self + field::kEdgeHashes + 4 * i);
            if (At<uint32_t>(self, field::kEdgeHashes + 4 * i) != 0)
                At<uint32_t>(self, field::kEdgeFlags) |= 1u << i;
        }
        CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadVector3, stream, self + field::kPosition);
        CallCdecl<void*, void*, void*>(addr::SC4Stream_ReadVector3, stream, self + field::kSize);
        if (void* const old = At<void*>(self, field::kLot)) {
            At<void*>(self, field::kLot) = nullptr;
            Virtual<uint32_t>(old, 0x8);
        }
        CallCdecl<uint8_t, void*, uint32_t, void**, uint32_t>(addr::SC4Stream_ReadReference, stream, kIidLot,
                                                               &At<void*>(self, field::kLot), 0u);
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// The holder part's slots 23 and 24 (0x005E2F20, 0x005E2F30): this - 4.
uint32_t __fastcall WriteThunk(char* holder, void*, void* stream) { return Write(holder - 0x4, nullptr, stream); }
uint32_t __fastcall ReadThunk(char* holder, void*, void* stream) { return Read(holder - 0x4, nullptr, stream); }

// --- The free functions of the class's file -----------------------------------------

// 0x005E2CE0 (cdecl): the hash of each of a lot's four sides and the
// height the walls must reach.
//
// *highest starts as the lot's altitude plus 0.1 (0x00A8C950; the sum made
// on the FPU, stored as a float). Then per side n (mask 1 << n), the hash
// zeroed first:
//   - the cells along the side outside the lot (lot manager slot 12; a
//     false answer leaves the side without a wall). Each of them is looked
//     up (GetLot): a cell without a lot, or with a lot whose flags lack bit
//     0, asks for a wall; a lot whose state is not 6 sets the bit of a
//     cursor that starts at 0x80000000 and moves down one bit per cell
//     (wrapping) in the side's bits;
//   - when no cell asked for a wall the hash stays zero; else it starts as
//     those bits, and every vertex along the side (lot manager slot 11; its
//     answer is not looked at) adds (bits of float(height + 12582912.0) +
//     0xB4C00000) * z * x + 1, the terrain's height (slot 14, in ST0)
//     raising *highest when it is above it (an unordered compare does not).
// The result is whether any vertex was visited. The lot itself is not used.
//
// The game's two rectangles are stack slots it does not clear, kept across
// the sides: zero here before the first side (the lot manager writes all
// four values whenever it answers true).
uint32_t __cdecl ComputeEdgeHashes(void*, float altitude, const int32_t* cells, uint32_t* hashes, float* highest)
{
    static bool logged = false;
    FirstCall("ComputeEdgeHashes", logged);
    *highest = static_cast<float>(static_cast<double>(altitude) +
                                  static_cast<double>(Global<float>(addr::cSC4LotRetainingWallOccupant_HeightMargin)));
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    void* const lots = Global<void*>(addr::SC4_LotManager);
    uint32_t visited = 0;
    int32_t neighbours[4] = {};
    int32_t vertices[4] = {};
    for (uint32_t side = 0; side < 4; ++side) {
        const uint32_t mask = 1u << side;
        hashes[side] = 0;
        uint32_t occupied = 0;
        uint32_t cursor = 0x80000000u;
        if (!Virtual<uint8_t>(lots, 0x30, cells, mask, static_cast<int32_t*>(neighbours)))
            continue;
        for (int32_t x = neighbours[0]; x <= neighbours[2]; ++x) {
            for (int32_t z = neighbours[1]; z <= neighbours[3]; ++z) {
                if (void* const lot = Virtual<void*>(lots, 0x18, x, z, 0u)) {
                    if (!(Virtual<uint32_t>(lot, 0x30) & 1u))
                        hashes[side] = 1;
                    if (Virtual<uint32_t>(lot, 0x84) != 6)
                        occupied |= cursor;
                } else {
                    hashes[side] = 1;
                }
                cursor >>= 1;
                if (cursor == 0)
                    cursor = 0x80000000u;
            }
        }
        if (hashes[side] == 0)
            continue;
        hashes[side] = occupied;
        Virtual<uint8_t>(lots, 0x2C, cells, mask, static_cast<int32_t*>(vertices));
        for (int32_t x = vertices[0]; x <= vertices[2]; ++x) {
            for (int32_t z = vertices[1]; z <= vertices[3]; ++z) {
                visited = 1;
                const double height = Virtual<double>(terrain, 0x38, x, z);
                if (height > static_cast<double>(*highest))
                    *highest = static_cast<float>(height);
                const float biased = static_cast<float>(
                    height + static_cast<double>(Global<float>(addr::cSC4LotRetainingWallOccupant_HashBias)));
                uint32_t bits;
                std::memcpy(&bits, &biased, 4);
                hashes[side] += (bits + 0xB4C00000u) * static_cast<uint32_t>(z) * static_cast<uint32_t>(x) + 1u;
            }
        }
    }
    return visited;
}

// 0x005E2ED0 (cdecl; only 0x005E3070 calls it): one of the retaining wall
// types of the lot's configuration (the lot's slot 9, then its slot 28: a
// vector of IDs), drawn with the file's generator; zero without a
// configuration, a vector or types. The count is a signed shift of the
// byte difference, as in the game.
uint32_t PickWallType(void* lot)
{
    static bool logged = false;
    FirstCall("PickWallType", logged);
    void* const configuration = Virtual<void*>(lot, 0x24);
    if (!configuration)
        return 0;
    const std::uintptr_t* const types = Virtual<const std::uintptr_t*>(configuration, 0x70);
    if (!types)
        return 0;
    const int32_t count = static_cast<int32_t>(types[1] - types[0]) >> 2;
    if (count == 0)
        return 0;
    const uint32_t index =
        CallThis<uint32_t, uint32_t>(addr::cRZRandom_Below,
                                     reinterpret_cast<void*>(addr::cSC4LotRetainingWallOccupant_Random),
                                     static_cast<uint32_t>(count));
    return reinterpret_cast<const uint32_t*>(types[0])[index];
}

// 0x005E3070 (cdecl; lot, make): a lot's retaining wall made or removed.
//
// Make (only for a lot with flag 1): the lot's cells and altitude, the
// sides' hashes (nothing more when no vertex was visited), then a new wall
// (the allocator and the constructor at their addresses, as the game calls
// them) referenced once; a wall type drawn for the lot and, when there is
// one, its exemplar loaded by the building development simulator (slot 29;
// a failure ends here, the wall released). Then the edge flags (bit n for
// a hash on side n), the position (the middle of the cells in x and z:
// (first + last + 1) * 16 * 0.5 from the integer; y the altitude's float
// as it is), the size (the cells' extent * 16 in x and z; in y the highest
// value less the altitude, on the FPU), the lot, the hashes, the type,
// Init, the occupant manager's InsertOccupant (occupant, 0) and bit 4 set
// in the lot's flags; the reference released. The result byte is zero on
// every path of this branch (the game's result variable is never set in
// it).
//
// Remove: the wall occupant (type 0x895D1169) at the lot's cell; when the
// manager answers true with an occupant it is removed (occupant, 1, 0),
// released and the result is 1. Bit 4 of the lot's flags is cleared either
// way. The game leaves the cell's two values unset before GetLocation
// (which writes both): zero here.
uint32_t __cdecl UpdateLotWall(void* lot, uint8_t make)
{
    static bool logged = false;
    FirstCall("UpdateLotWall", logged);
    if (!make) {
        void* const occupants = Global<void*>(addr::SC4_NetworkService);
        void* found = nullptr;
        int32_t x = 0, z = 0;
        Virtual<void>(lot, 0x54, &x, &z);
        uint32_t removed = 0;
        if (Virtual<uint8_t>(occupants, 0x84, &found, x, z, kOccupantType) && found) {
            Virtual<uint8_t>(occupants, 0x54, found, 1u, 0u);
            Virtual<uint32_t>(found, 0x8);
            removed = 1;
        }
        const uint32_t flags = Virtual<uint32_t>(lot, 0x30);
        Virtual<void>(lot, 0x34, flags & 0xFFFFFFFBu);
        return removed;
    }
    if (!(Virtual<uint32_t>(lot, 0x30) & 1u))
        return 0;
    int32_t cells[4] = {};
    Virtual<void>(lot, 0x38, static_cast<int32_t*>(cells));
    float highest = 0.0f;
    const float altitude = Virtual<float>(lot, 0x40);
    uint32_t hashes[4] = {};
    if (!(ComputeEdgeHashes(lot, altitude, cells, hashes, &highest) & 0xFFu))
        return 0;
    void* const occupants = Global<void*>(addr::SC4_NetworkService);
    char* wall = CallCdecl<char*, uint32_t>(addr::cSC4LotRetainingWallOccupant_Allocate, 0x6Cu);
    if (wall)
        wall = CallThis<char*>(addr::cSC4LotRetainingWallOccupant_cSC4LotRetainingWallOccupant, wall);
    Virtual<uint32_t>(wall, 0x4);
    const uint32_t type = PickWallType(lot);
    void* const development = Global<void*>(addr::SC4_Service43D68);
    if (type == 0 || Virtual<uint8_t>(development, 0x74, wall, type)) {
        const uint32_t sides = (hashes[0] ? 1u : 0u) | (hashes[1] ? 2u : 0u) | (hashes[2] ? 4u : 0u) |
                               (hashes[3] ? 8u : 0u);
        Virtual<void>(wall, slot::kSetEdgeFlags, sides);
        const double half = static_cast<double>(Global<float>(addr::SC4_FloatHalf));
        const auto Times16 = [](int32_t value) { return static_cast<int32_t>(static_cast<uint32_t>(value) << 4); };
        float position[3];
        position[0] = static_cast<float>(static_cast<double>(Times16(cells[2] + cells[0] + 1)) * half);
        position[1] = altitude;
        position[2] = static_cast<float>(static_cast<double>(Times16(cells[3] + cells[1] + 1)) * half);
        float size[3];
        size[0] = static_cast<float>(Times16(cells[2] - cells[0] + 1));
        size[1] = static_cast<float>(static_cast<double>(highest) - static_cast<double>(altitude));
        size[2] = static_cast<float>(Times16(cells[3] - cells[1] + 1));
        char* const part = wall + field::kOccupant;
        Virtual<void>(part, occupant::kSetPosition, static_cast<float*>(position));
        Virtual<void>(wall, slot::kSetSize, static_cast<float*>(size));
        Virtual<void>(wall, slot::kSetLot, lot);
        Virtual<void>(wall, slot::kSetEdgeHashArray, static_cast<uint32_t*>(hashes));
        Virtual<void>(wall, slot::kSetRetainingWallType, type);
        Virtual<void>(part, occupant::kInit);
        void* const asOccupant = Virtual<void*>(wall, slot::kAsOccupant);
        Virtual<uint8_t>(occupants, 0x50, asOccupant, 0u);
        const uint32_t flags = Virtual<uint32_t>(lot, 0x30);
        Virtual<void>(lot, 0x34, flags | 4u);
    }
    Virtual<uint32_t>(wall, 0x8);
    return 0;
}

// --- Installation -------------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(addr::cSC4LotRetainingWallOccupant_vftable, 0, addr::cSC4LotRetainingWallOccupant_QueryInterface,
                 QueryInterface),
    OPENSC4_SLOT(addr::cSC4LotRetainingWallOccupant_vftable, 4, addr::cSC4LotRetainingWallOccupant_SetSize, SetSize),
    OPENSC4_SLOT(addr::cSC4LotRetainingWallOccupant_vftable, 6, addr::cSC4LotRetainingWallOccupant_SetLot, SetLot),
    OPENSC4_SLOT(addr::cSC4LotRetainingWallOccupant_vftable, 9, addr::cSC4LotRetainingWallOccupant_GetEdgeHashArray,
                 GetEdgeHashArray),
    OPENSC4_SLOT(addr::cSC4LotRetainingWallOccupant_vftable, 10, addr::cSC4LotRetainingWallOccupant_SetEdgeHashArray,
                 SetEdgeHashArray),
    OPENSC4_SLOT(addr::cSC4LotRetainingWallOccupant_vftable_4, 0,
                 addr::cSC4LotRetainingWallOccupant_QueryInterfaceThunk4, QueryInterfaceAt<0x4>),
    OPENSC4_SLOT(addr::cSC4LotRetainingWallOccupant_vftable_4, 4, addr::cSC4LotRetainingWallOccupant_Shutdown,
                 Shutdown),
    OPENSC4_SLOT(addr::cSC4LotRetainingWallOccupant_vftable_4, 9, addr::cSC4LotRetainingWallOccupant_SetPosition,
                 SetPosition),
    OPENSC4_SLOT(addr::cSC4LotRetainingWallOccupant_vftable_4, 10, addr::cSC4LotRetainingWallOccupant_GetBoundingBox,
                 GetBoundingBox),
    OPENSC4_SLOT(addr::cSC4LotRetainingWallOccupant_vftable_8, 0,
                 addr::cSC4LotRetainingWallOccupant_QueryInterfaceThunk8, QueryInterfaceAt<0x8>),
    OPENSC4_SLOT(addr::cSC4LotRetainingWallOccupant_vftable_8, 5, addr::cSC4LotRetainingWallOccupant_GetGZCLSID,
                 GetGZCLSID),
    OPENSC4_SLOT(addr::cSC4LotRetainingWallOccupant_vftable_C, 0,
                 addr::cSC4LotRetainingWallOccupant_QueryInterfaceThunkC, QueryInterfaceAt<0xC>),
    OPENSC4_SLOT(addr::cSC4LotRetainingWallOccupant_vftable_20, 0,
                 addr::cSC4LotRetainingWallOccupant_QueryInterfaceThunk20, QueryInterfaceAt<0x20>),
};
const Slot kSaveSlots[] = {
    OPENSC4_SLOT(addr::cSC4LotRetainingWallOccupant_vftable_8, 3, addr::cSC4LotRetainingWallOccupant_Write, Write),
    OPENSC4_SLOT(addr::cSC4LotRetainingWallOccupant_vftable_8, 4, addr::cSC4LotRetainingWallOccupant_Read, Read),
    OPENSC4_SLOT(addr::cSC4LotRetainingWallOccupant_vftable_C, 23, addr::cSC4LotRetainingWallOccupant_WriteThunk,
                 WriteThunk),
    OPENSC4_SLOT(addr::cSC4LotRetainingWallOccupant_vftable_C, 24, addr::cSC4LotRetainingWallOccupant_ReadThunk,
                 ReadThunk),
};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SlotsHoldOriginals(const Slot (&slots)[N], const char* what)
{
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("wallocc %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    return true;
}

template <std::size_t N>
bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    if (!game::IsBuild641() || !SlotsHoldOriginals(slots, what))
        return false;
    for (const Slot& slot : slots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("wallocc %s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

// None of the slots is called directly from outside the class (the
// QueryInterface thunks jump to 0x005E28C0 and the save thunks to Write and
// Read inside it; the class registration's call of GetGZCLSID at 0x00460820
// runs before any replacement is installed) and no other class's vtable
// holds one, so they are all slot swaps: no detour, no thunk.
bool InstallSlots(registry::Mode) { return SwapSlots(kSlots, "slots"); }

bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, "save"); }

using ComputeEdgeHashesFn = uint32_t(__cdecl*)(void*, float, const int32_t*, uint32_t*, float*);
using UpdateLotWallFn = uint32_t(__cdecl*)(void*, uint8_t);
ComputeEdgeHashesFn g_originalComputeEdgeHashes = nullptr;
UpdateLotWallFn g_originalUpdateLotWall = nullptr;

// The two free functions code outside the class calls directly, by detour.
// Every call site was read for what it keeps in registers:
//   - 0x005E2CE0: 0x005E30CB (inside 0x005E3070: add esp, test al) and
//     0x005E38AA in the lot's wall update 0x005E37F0 (add esp, then CL and
//     ESI cleared; the result is not read);
//   - 0x005E3070: 0x005E392E and 0x005E3947 in the wall update (ECX
//     reloaded from the stack, or nothing, after), 0x006C8A3D and
//     0x006C8C23 in the lot developer (ECX and EDX reloaded from the stack,
//     or ESI pushed, after; ours in src/core/lot_build.cpp calls the
//     address).
// None reads EAX, ECX or EDX after the call: plain cdecl detours, no thunk.
// 0x005E2ED0 has no caller but 0x005E3070, so it needs none.
bool InstallHelpers(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const hashes = addr_check::Find(addr::cSC4LotRetainingWallOccupant_ComputeEdgeHashes);
    const addr::Entry* const update = addr_check::Find(addr::SC4_LotHelper_005E3070);
    return hashes && update && hook::Install(*hashes, &ComputeEdgeHashes, &g_originalComputeEdgeHashes) &&
           hook::Install(*update, &UpdateLotWall, &g_originalUpdateLotWall);
}

const bool g_registered[] = {
    registry::Register({"simulators.wallocc.slots", "simulators/wallocc", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4LotRetainingWallOccupant_vftable,
                        "Our cSC4LotRetainingWallOccupant slots: QueryInterface and the parts' thunks, SetSize, "
                        "SetLot, GetEdgeHashArray, SetEdgeHashArray, Shutdown, SetPosition, GetBoundingBox, "
                        "GetGZCLSID (src/core/wallocc_sim.h)",
                        false, InstallSlots}),
    registry::Register({"simulators.wallocc.save", "simulators/wallocc", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4LotRetainingWallOccupant_vftable_8,
                        "Our cSC4LotRetainingWallOccupant Write and Read (the save record, version 2; "
                        "src/core/wallocc_sim.h)",
                        false, InstallSave}),
    registry::Register({"simulators.wallocc.helpers", "simulators/wallocc", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::SC4_LotHelper_005E3070,
                        "Our retaining wall helpers: a lot's wall made or removed (0x005E3070, with the wall "
                        "type's draw 0x005E2ED0) and the sides' hashes (0x005E2CE0), by detour "
                        "(src/core/wallocc_sim.h)",
                        false, InstallHelpers}),
};

// --- Our object ---------------------------------------------------------------------

// Copies of the game's five vtables, taken at the first construction, after
// every PostAppInit slot swap of the other groups.
constexpr int kSlots0 = 13, kSlots4 = 34, kSlots8 = 6, kSlotsC = 25, kSlots20 = 5;
void* g_vtable[kSlots0];
void* g_vtable4[kSlots4];
void* g_vtable8[kSlots8];
void* g_vtableC[kSlotsC];
void* g_vtable20[kSlots20];

void SetVtables(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, field::kOccupant) = g_vtable4;
    At<void**>(self, field::kSerializable) = g_vtable8;
    At<void**>(self, field::kHolder) = g_vtableC;
    At<void**>(self, field::kExemplarPart) = g_vtable20;
}

// An item of the wall pool (0x005E2A50, and inline in the factory): the
// current page's free head, the pool refilled when the page has none
// (0x0090CF9A returns the new head); the page is read again after. The
// item's memory is not cleared.
void* PoolAllocate()
{
    char* const pool = reinterpret_cast<char*>(addr::cSC4LotRetainingWallOccupant_Pool);
    void* item = At<void*>(At<char*>(pool, 0x4), 0x8);
    if (!item)
        item = CallThis<void*>(addr::SC4EdgePool_Grow, pool);
    At<void*>(At<char*>(pool, 0x4), 0x8) = *static_cast<void**>(item);
    ++At<uint32_t>(At<char*>(pool, 0x4), 0xC);
    return item;
}

// The deleting destructor (0x005E3370): the class's vtables, the lot
// released (the field is left as it is), the occupant base destroyed, and
// with flag 1 the memory back to the pool (the page header is at the
// address with its low 13 bits clear).
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("DeletingDestructor", logged);
    SetVtables(self);
    if (void* const lot = At<void*>(self, field::kLot))
        Virtual<uint32_t>(lot, 0x8);
    CallThis<void>(addr::cSC4Occupant_Destroy, self + field::kOccupant);
    if (flags & 1) {
        char* const page = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(self) & 0xFFFFE000u);
        At<void*>(self, 0x0) = At<void*>(page, 0x8);
        const uint32_t used = At<uint32_t>(page, 0xC) - 1;
        At<void*>(page, 0x8) = self;
        At<uint32_t>(page, 0xC) = used;
        void* const pool = reinterpret_cast<void*>(addr::cSC4LotRetainingWallOccupant_Pool);
        if (used == 0)
            CallThis<void, void*>(addr::SC4Pool_ReleaseBlock, pool, page);
        else if (used == 0x40000038)
            CallThis<void, void*>(addr::SC4Pool_ReopenBlock, pool, page);
    }
    return self;
}

// The holder part's slot 22 (0x005E2A30).
void* __fastcall DeletingDestructorAtHolder(char* part, void*, uint32_t flags)
{
    return DeletingDestructor(part - field::kHolder, nullptr, flags);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4LotRetainingWallOccupant_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4LotRetainingWallOccupant_vftable_4),
                sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4LotRetainingWallOccupant_vftable_8),
                sizeof(g_vtable8));
    std::memcpy(g_vtableC, reinterpret_cast<const void*>(addr::cSC4LotRetainingWallOccupant_vftable_C),
                sizeof(g_vtableC));
    std::memcpy(g_vtable20, reinterpret_cast<const void*>(addr::cSC4LotRetainingWallOccupant_vftable_20),
                sizeof(g_vtable20));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x4>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x8>);
    g_vtable8[5] = reinterpret_cast<void*>(&GetGZCLSID);
    g_vtableC[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0xC>);
    g_vtableC[22] = reinterpret_cast<void*>(&DeletingDestructorAtHolder);
    g_vtable20[0] = reinterpret_cast<void*>(&QueryInterfaceAt<0x20>);
    log::Write("wallocc object: vtables copied (slots 4/6/9/10 %p %p %p %p, +4 slots 4/9/10 %p %p %p, +8 slots 3/4 "
               "%p %p)",
               g_vtable[4], g_vtable[6], g_vtable[9], g_vtable[10], g_vtable4[4], g_vtable4[9], g_vtable4[10],
               g_vtable8[3], g_vtable8[4]);
}

// The constructor (detour of 0x005E2F40): the occupant base constructed at
// its address on the object + 4, our vtables, the wall type, the lot, the
// edge flags and the position zeroed, the size's three floats each a copy
// of the float at 0x00B230C0 (read as the game does, not assumed zero), the
// occupant type, and last the four edge hashes zeroed. Every byte from
// +0x38 to the end is set: nothing is left as the pool's memory.
void* __fastcall Construct(char* self, void*)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    CallThis<void*>(addr::cSC4Occupant_cSC4Occupant, self + field::kOccupant);
    SetVtables(self);
    At<uint32_t>(self, field::kWallType) = 0;
    At<void*>(self, field::kLot) = nullptr;
    At<uint32_t>(self, field::kEdgeFlags) = 0;
    for (std::size_t offset = field::kPosition; offset < field::kSize; offset += 4)
        At<uint32_t>(self, offset) = 0;
    for (std::size_t offset = field::kSize; offset < kObjectSize; offset += 4)
        At<uint32_t>(self, offset) = Global<uint32_t>(addr::cSC4LotRetainingWallOccupant_DefaultSize);
    At<uint32_t>(self, field::kType) = kOccupantType;
    for (std::size_t offset = field::kEdgeHashes; offset < field::kPosition; offset += 4)
        At<uint32_t>(self, offset) = 0;
    static int created = 0;
    if (created++ < 3)
        log::Write("wallocc object: constructed %p", static_cast<void*>(self));
    return self;
}

// The pool allocator (detour of 0x005E2A50; the size is not used).
void* __cdecl Allocate(std::size_t)
{
    static bool logged = false;
    FirstCall("Allocate", logged);
    return PoolAllocate();
}

// The factory (detour of 0x005E3330): an item of the pool, constructed (the
// game jumps to the constructor); the result is the object itself, not its
// occupant part.
void* __cdecl Create()
{
    static bool logged = false;
    FirstCall("Create", logged);
    return Construct(static_cast<char*>(PoolAllocate()), nullptr);
}

using ConstructFn = void*(__fastcall*)(char*, void*);
using AllocateFn = void*(__cdecl*)(std::size_t);
using CreateFn = void*(__cdecl*)();
ConstructFn g_originalConstruct = nullptr;
AllocateFn g_originalAllocate = nullptr;
CreateFn g_originalCreate = nullptr;

// Every construction path: the factory (called through the class
// registration's pointer when a save is read; it jumps to the constructor,
// so its caller sees the constructor's result) and the helper 0x005E3070,
// at 0x005E30E7 (the allocator; add esp, cmp eax with EBX after) and
// 0x005E30F5 (the constructor; mov esi, eax after), which keeps its values
// in EBX and EDI and on its stack across both; our own helper above calls
// both at their addresses. None keeps anything in ECX or EDX across the
// calls. The deleting destructor is reached through the holder part's slot
// 22 only (0x005E2A30 jumps to it), so the vtable copy replaces it without
// a detour; walls made before this group is installed (there are none at
// PostAppInit) would keep the game's.
bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const construct =
        addr_check::Find(addr::cSC4LotRetainingWallOccupant_cSC4LotRetainingWallOccupant);
    const addr::Entry* const allocate = addr_check::Find(addr::cSC4LotRetainingWallOccupant_Allocate);
    const addr::Entry* const create = addr_check::Find(addr::cSC4LotRetainingWallOccupant_Create);
    return construct && allocate && create && hook::Install(*construct, &Construct, &g_originalConstruct) &&
           hook::Install(*allocate, &Allocate, &g_originalAllocate) &&
           hook::Install(*create, &Create, &g_originalCreate);
}

const bool g_registeredObject = registry::Register({
    "simulators.wallocc.object",
    "simulators/wallocc",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4LotRetainingWallOccupant_cSC4LotRetainingWallOccupant,
    "Our cSC4LotRetainingWallOccupant object: the constructor, the factory and the pool allocation (detours), the "
    "deleting destructor, identity; methods from copies of the (patched) game vtables taken once "
    "(src/core/wallocc_sim.h)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::wallocc_sim
