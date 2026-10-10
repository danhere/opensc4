// Shared between the parts of our cSC4ZoneManager (build 641): the class
// map, the vtables slot by slot, the fields and the containers it keeps.
// The manager keeps the zone type of every cell (the zone grid, an int8
// grid of its own), the count of cells per type, the tuning of the sixteen
// types (sizes, slopes, costs, textures, colours) and PlaceZone, which
// checks, prices and carries out every zoning and dezoning. Written in two
// files: zonemgr_sim.cpp (the queries, Init and Shutdown with the tuning,
// the message, the save record, our object) and zonemgr_place.cpp
// (PlaceZone).
//
// The class: class ID 0x298F9B2D, 0x380 bytes, constructor 0x004DCE50 (only
// reached by the jump from the factory 0x004DD3E0, registered at
// 0x004603F3), destructor 0x004DCC70, deleting destructor 0x004DCF40. Code
// 0x004DC1A0 to 0x004DE2E0 (0x004DBE40 and 0x004DC110 before it and
// 0x004DE2E0 on are other classes'). The city makes one per city and keeps
// it at +0x134; the city's slot 36 (+0x90) and the global 0x00B43D14 give
// it.
//
// QueryInterface (0x004DC1B0): IIDs 0x00000001 and 0x67652433
// (cISC4ZoneManager) the object; 0x452294AA (cIGZMessageTarget2) +0x4;
// 0xE4FDA3D4 (cIGZSerializable) +0x8.
//
// --- +0x0: cISC4ZoneManager, vtable 0x00A92E00, 22 slots -------------------
//    0 QueryInterface 0x004DC1B0        1 AddRef 0x004F7BA0 (shared code)
//    2 Release 0x004DC430               3 Init 0x004DE170
//    4 Shutdown 0x004DC230
//    5 GetZoneGrid 0x00698CF0 (shared code: +0x48)
//    6 GetZoneCount(type) 0x004DC2D0
//    7 GetUndevelopedTileCount(type) 0x004DC2F0
//    8 GetUndevelopedTileCount() 0x004DC2E0
//    9 GetDevelopmentFailureCount(type, reason) 0x004DE130
//   10 GetAbandonedTileCount 0x007C9FB0 (shared code: +0x114)
//   11 PlaceZone(region, type, place, skipFunds, customZoning, skipSize,
//      wholeLots, cost, error, lotSet) 0x004DD780 (ret 0x28)
//   12 GetZoneType(x, z) 0x004DC740     13 IsRCIZone(type) 0x004DC300
//   14 GetTextureForZone(type, key) 0x004DC340
//   15 GetZoneDragColor(type) 0x004DCD20
//   16 GetZoneDisplayAlpha 0x004DCD10
//   17 GetDefaultZoneDisplayAlpha 0x004DC370
//   18 SetZoneDisplayAlpha(alpha) 0x004DCF60
//   19 GetMinZoneSize(type) 0x004DCD30  20 GetMaxZoneSize(type) 0x004DCD40
//   21 the deleting destructor 0x004DCF40
// --- +0x4: cIGZMessageTarget2, vtable 0x00A92DF0, 4 slots -----------------
//    thunks 0x004DCD50, 0x0068D230, 0x004DCD70; 3 DoMessage 0x004DCA70
// --- +0x8: cIGZSerializable, vtable 0x00A92DD8, 6 slots -------------------
//    thunks 0x004DCD60, 0x0068D250, 0x004DCD80; 3 Write 0x004DC7A0;
//    4 Read 0x004DD400; 5 GetGZCLSID 0x004DC1A0
//
// Not virtual: LoadTuning 0x004DC460 (Init only). The class's own template
// code, each called only from inside it and written inline here: the
// failure maps' comparison 0x004DC380, lower bound 0x004DC760, node insert
// 0x004DCB40, insert_unique 0x004DCD90, hinted insert 0x004DD140,
// operator[] 0x004DE0C0 and subtree erase 0x004DC6D0; the cell vector's
// copies 0x004DC3B0, 0x004DC3F0 and growing insert 0x004DD050. The zoned
// message's destructors 0x004DCC10 and 0x004DCC30 (vtable 0x00A92D40) stay
// the game's, as does the list search 0x004DC710 that six other classes
// call (written inline in PlaceZone: it only compares).
//
// Fields (offsets from the object's start; DoMessage gets object + 4 and
// Write/Read object + 8):
//   +0x0C uint8  initialized (Init sets it last, Shutdown clears it first)
//   +0x10 uint32 reference count (unsigned compare in Release)
//   +0x14 uint8  read from a save (Read sets it; Init then keeps the counts)
//   +0x18 the message server (0x00B43CCC); registered for 0x99EF1142
//   +0x1C the city (0x00B43CEC), +0x20 the occupant manager (0x00B43D0C),
//         +0x24 the terrain (0x00B43CF4), +0x28 the pollution simulator
//         (0x00B43D9C, never called), +0x2C the budget simulator
//         (0x00B43D64): referenced by Init or read from the save, released
//         by Shutdown
//   +0x30 a random generator (0x18 bytes, seeded -1 by the constructor;
//         never drawn from)
//   +0x48 the zone grid (cSC4SimGridSint8, 0x44 bytes, made by Init or read
//         from the save; slot 5 gives it): one int8 per cell, the type
//   +0x4C  int32[16] cells zoned per type (type 0 = not zoned: the whole
//          city at first)
//   +0x8C  int32[16] and +0xCC int32[16]: set to the city's cell count by
//          Init and never changed here (slot 7 gives the first)
//   +0x10C int32 (slot 8), the cell count at Init; +0x110, +0x114 (slot
//          10), +0x118: zero from the constructor, saved, never changed
//   +0x11C int32[16] least and +0x15C int32[16] greatest side of a zoning
//          rectangle per type
//   +0x19C float[16] greatest slope per type (custom zoning only; not
//          saved)
//   +0x1E0 int64[16] cost of zoning a cell as a type, +0x260 int64[16] cost
//          of taking a cell's old type away
//   +0x2E0 uint32[16] texture instance IDs, +0x320 uint32[16] drag colours
//   +0x360 uint8 display alpha (0xFF from the constructor), +0x361 its
//          default (the tuning's value)
//   +0x364 std::map<{int8 type, int8 reason}, uint32> (head, size at
//          +0x368; 0x18-byte nodes, ordered by reason then type): the
//          development failure counts, grown by slot 9 on every new key
//   +0x370 a second such map (size at +0x374): saved and read only
//   +0x378, +0x37C never written
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::zonemgr_sim {

inline constexpr uint32_t kClassId = 0x298F9B2D;
inline constexpr uint32_t kIidUnknown = 0x00000001;
inline constexpr uint32_t kIidManager = 0x67652433;
inline constexpr uint32_t kIidMessageTarget = 0x452294AA;
inline constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
inline constexpr uint32_t kMessageOccupant = 0x99EF1142;
inline constexpr uint32_t kMessageZoned = 0x07699455;

inline constexpr std::size_t kObjectSize = 0x380;
inline constexpr std::size_t kMessagePart = 0x4;
inline constexpr std::size_t kSerializablePart = 0x8;
inline constexpr std::size_t kSlots = 22;
inline constexpr std::size_t kMessageSlots = 4;
inline constexpr std::size_t kSerializableSlots = 6;
inline constexpr uint32_t kTypes = 16;
inline constexpr int32_t kReasons = 19;

namespace field {
inline constexpr std::size_t kInitialized = 0x0C;
inline constexpr std::size_t kRefCount = 0x10;
inline constexpr std::size_t kLoaded = 0x14;
inline constexpr std::size_t kMessages = 0x18;
inline constexpr std::size_t kCity = 0x1C;
inline constexpr std::size_t kOccupants = 0x20;
inline constexpr std::size_t kTerrain = 0x24;
inline constexpr std::size_t kPollution = 0x28;
inline constexpr std::size_t kBudget = 0x2C;
inline constexpr std::size_t kRandom = 0x30;
inline constexpr std::size_t kGrid = 0x48;
inline constexpr std::size_t kZoned = 0x4C;
inline constexpr std::size_t kUndeveloped = 0x8C;
inline constexpr std::size_t kCountsCC = 0xCC;
inline constexpr std::size_t kUndevelopedTotal = 0x10C;
inline constexpr std::size_t kCount110 = 0x110;
inline constexpr std::size_t kAbandoned = 0x114;
inline constexpr std::size_t kCount118 = 0x118;
inline constexpr std::size_t kMinSize = 0x11C;
inline constexpr std::size_t kMaxSize = 0x15C;
inline constexpr std::size_t kMaxSlope = 0x19C;
inline constexpr std::size_t kZoneCost = 0x1E0;
inline constexpr std::size_t kDezoneCost = 0x260;
inline constexpr std::size_t kTextures = 0x2E0;
inline constexpr std::size_t kColors = 0x320;
inline constexpr std::size_t kAlpha = 0x360;
inline constexpr std::size_t kDefaultAlpha = 0x361;
inline constexpr std::size_t kFailures = 0x364;
inline constexpr std::size_t kMap370 = 0x370;
} // namespace field

// The manager's slots (byte offsets in the vtable 0x00A92E00).
namespace slot {
inline constexpr std::size_t kShutdown = 0x10;
inline constexpr std::size_t kGetZoneGrid = 0x14;
inline constexpr std::size_t kGetZoneCount = 0x18;
inline constexpr std::size_t kGetUndeveloped = 0x1C;
inline constexpr std::size_t kGetUndevelopedTotal = 0x20;
inline constexpr std::size_t kGetFailureCount = 0x24;
inline constexpr std::size_t kGetAbandoned = 0x28;
inline constexpr std::size_t kPlaceZone = 0x2C;
inline constexpr std::size_t kGetZoneType = 0x30;
inline constexpr std::size_t kIsRCIZone = 0x34;
inline constexpr std::size_t kGetTexture = 0x38;
inline constexpr std::size_t kGetDragColor = 0x3C;
inline constexpr std::size_t kGetAlpha = 0x40;
inline constexpr std::size_t kGetDefaultAlpha = 0x44;
inline constexpr std::size_t kSetAlpha = 0x48;
inline constexpr std::size_t kGetMinSize = 0x4C;
inline constexpr std::size_t kGetMaxSize = 0x50;
inline constexpr std::size_t kDeletingDestructor = 0x54;
} // namespace slot

// The game's red-black trees: {head, size}; the head node holds color,
// parent = root, left most, right most; nodes color, parent, left, right,
// then the value at +0x10.
struct Tree {
    char* head;
    uint32_t count;
};

namespace node {
inline constexpr std::size_t kParent = 0x4;
inline constexpr std::size_t kLeft = 0x8;
inline constexpr std::size_t kRight = 0xC;
inline constexpr std::size_t kType = 0x10;   // int8
inline constexpr std::size_t kReason = 0x11; // int8
inline constexpr std::size_t kValue = 0x14;  // uint32
inline constexpr std::size_t kSize = 0x18;
} // namespace node

// The in-order successor (inline in the game's Write).
inline char* TreeNext(char* at)
{
    if (char* right = At<char*>(at, node::kRight)) {
        while (At<char*>(right, node::kLeft))
            right = At<char*>(right, node::kLeft);
        return right;
    }
    char* parent = At<char*>(at, node::kParent);
    while (at == At<char*>(parent, node::kRight)) {
        at = parent;
        parent = At<char*>(parent, node::kParent);
    }
    return At<char*>(at, node::kRight) != parent ? parent : at;
}

// The zone grid's cell (as slot 12: the grid's slot 24 gives the address
// of its column vectors, 12 bytes each).
inline int8_t& GridCell(void* grid, int32_t x, int32_t z)
{
    char* const columns = *Virtual<char**>(grid, 0x60);
    return At<int8_t*>(columns, static_cast<std::size_t>(x) * 12)[z];
}

inline uint32_t AddRefOf(void* object) { return Virtual<uint32_t>(object, 0x4); }
inline uint32_t ReleaseOf(void* object) { return Virtual<uint32_t>(object, 0x8); }

// Logs "zonemgr: first <name>" once per flag.
void FirstCall(const char* name, bool& logged);

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

// Swaps the slots when every one still holds its original (build 641 only).
bool SwapSlots(const Slot* slots, std::size_t count, const char* what);

// A list of references ({next, previous, object} nodes around a head node)
// freed as the game's inline code does: every object released and its node
// freed in order, then the head (zonemgr_sim.cpp).
void FreeRefList(char* head);

} // namespace opensc4::zonemgr_sim
