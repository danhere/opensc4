// Shared between the parts of our cSC4OccupantManager (build 641): the class
// map, the vtables slot by slot, the fields and the containers it keeps.
// The manager is the city's index of every occupant (buildings, props,
// flora, network pieces, lot textures...) by place: a fixed grid of 192 x
// 192 "world cells" of 64 m, the city's own cells being 64..127 in it (the
// margin is for occupants beyond the edges), each cell with a list of the
// occupants whose bounding box touches it. Written in three files:
// occmgr_sim.cpp (the conversions, Init and Shutdown, the save record, our
// object), occmgr_edit.cpp (insert, remove, move, find) and
// occmgr_queries.cpp (the first-occupant, get and iterate families).
//
// The class: class ID 0x098F964D, 0x9C bytes, constructor 0x004A0540 (only
// reached by the jump from the factory 0x004A18A0), destructor 0x004A0620,
// deleting destructor 0x004A18C0. Code 0x0049D8B0 to 0x004A1D20 (0x0049CC30
// to 0x0049D8A6 before it and 0x004A1D20 on are other classes'). The city
// makes one per city and keeps it at +0x12C; the city's slot 34 (+0x88) and
// the global 0x00B43D0C (SC4::NetworkService in our address files, an old
// name) give it. No function of the class is called directly from outside
// it: everything goes through the vtables.
//
// QueryInterface (0x0049D910): IIDs 0x00000001 and 0xC6BB8334
// (cISC4OccupantManager) the object; 0xE4FDA3D4 (cIGZSerializable) +0x4;
// 0x452294AA (cIGZMessageTarget2) +0x8.
//
// --- +0x0: cISC4OccupantManager, vtable 0x00A8F7F0, 50 slots ---------------
// (gzcom-dll's header has the overloaded pairs in the other order: the
// first of each pair takes a filter, the second a type; 46/47 are type then
// filter.)
//    0 QueryInterface 0x0049D910        1 AddRef 0x0049DF80
//    2 Release 0x0049DFB0               3 Init 0x004A18E0
//    4 Shutdown 0x0049E000              5 SetCitySize(x, z) 0x0049E050
//    6 GetOccupantManagerCellSizes 0x0049D990 (64, 64)
//    7 GetWorldCellCount 0x0049D9B0 (192, 192)
//    8 GetOccupantManagerCellCount 0x0049D9D0 (+0x24, +0x28)
//    9 GetOccupantManagerCellBounds 0x0049DB50 (64, 64, +0x24 + 63, ...)
//   10 WorldCellToOccupantManagerCell 0x0049D9F0 (- 64)
//   11 OccupantManagerCellToWorldCell 0x0049DA10 (+ 64)
//   12 WorldCellToStandardCityCell 0x0049DA30 (a bug: see the code)
//   13 StandardCityCellToWorldCell 0x0049DA50 (/ 4 + 64)
//   14 WorldCellToPosition 0x0049DA80   15 PositionToWorldCell 0x0049E0E0
//   16 OccupantManagerCellToPosition 0x0049DAF0
//   17 PositionToOccupantManagerCell 0x0049E150
//   18 StandardCityCellToPosition 0x0049DB20
//   19 PositionToStandardCityCell 0x0049E1B0
//   20 InsertOccupant(occupant, data) 0x0049EF60
//   21 RemoveOccupant(occupant, shutdown, data) 0x0049F240
//   22 RemoveOccupants(filter, shutdown, data) 0x0049FCD0
//   23 RemoveOccupants(type, shutdown, data) 0x0049DB80
//   24 MoveOccupant(occupant, notify) 0x0049F3C0
//   25 IsCellEmpty(x, z) 0x0049E490
//   26 GetBoundingCells(x, z, cells, count) 0x0049DBA0
//   27 FindOccupant(occupant, searchAll, x, z) 0x0049FB30
//   28/29 GetFirstOccupantByPosition(out, x, z, filter / type) 0x0049E4D0,
//         0x0049DC80
//   30/31 GetFirstOccupant(out, cellX, cellZ, filter / type) 0x0049E5E0,
//         0x0049DCB0
//   32/33 GetFirstOccupantByStandardCityCell(out, x, z, filter / type)
//         0x0049E690, 0x0049DCE0
//   34/35 GetFirstOccupantByStandardCityCells(out, low, high, filter /
//         type) 0x0049E800, 0x0049DD10
//   36/37 GetOccupantsByBBox(list, low, high, filter / type, limit)
//         0x004A0670, 0x0049DD40
//   38/39 GetOccupantsByOccupantManagerCells(list, low, high, filter /
//         type, limit) 0x004A0A70, 0x0049DD80 (the cells are WORLD cells)
//   40/41 GetOccupantsByStandardCityCells(list, low, high, filter / type,
//         limit) 0x0049E250, 0x0049DDC0
//   42/43 IterateOccupantsByBBox(callback, context, low, high, filter /
//         type) 0x004A0D70, 0x0049DE00
//   44/45 IterateOccupants(callback, context, low, high, filter / type)
//         0x004A1360, 0x0049DE40 (world cells as (x, -, z) triples)
//   46/47 IterateOccupantsByStandardCityCell(callback, context, low, high,
//         type / filter) 0x0049DE80, 0x0049E370
//   48 ReleaseOccupantList(list) 0x0049EF30
//   49 the deleting destructor 0x004A18C0
// --- +0x4: cIGZSerializable, vtable 0x00A8F7D4, 6 slots -------------------
//    thunks 0x004A05C0, 0x004A05E0, 0x004A05F0; 3 Write 0x0049ECF0;
//    4 Read 0x004A1A20; 5 GetGZCLSID 0x004A05B0
// --- +0x8: cIGZMessageTarget2, vtable 0x00A8F7C4, 4 slots -----------------
//    thunks 0x004A05D0, 0x004A0600, 0x004A0610; 3 DoMessage 0x006B8AA0
//    (shared code: asks the message's type and returns true)
//
// Not virtual, each called only from inside the class and written inline
// here: the box test 0x0049D8B0 (arguments in ECX, EDX, ESI, EDI), the
// load's callback 0x0049D900, the position to cell bytes 0x0049DAC0, the
// clamps 0x0049DEC0 and 0x0049DF00, the cell getter 0x0049EE80 and the
// first-cell finder 0x0049F7C0.
//
// Container code kept the game's and called at its address: the visited
// set (constructor 0x0049FA80, insert 0x004A03B0 with 0x004A01C0, 0x004A0290
// and 0x0049FA10 below it, deque destructor 0x0049F8A0, its vector helpers
// 0x0049EAD0, 0x0049EB30, 0x0049EC50, 0x0049ECC0, 0x0049F940: template code
// only this class instantiates; kept so its allocations are the game's);
// and code other classes share: 0x0049EAA0 (list erase), 0x0049EB90 (deque
// map), 0x0049FB10, 0x0049FC30 and 0x0049FC70 (vectors), 0x004A0350.
//
// Fields (Write and Read get object + 4):
//   +0x0C uint8  initialized (Init sets it first, Shutdown clears it last)
//   +0x0D uint8  read from a save (Read sets it; nothing reads it)
//   +0x10 uint32 reference count (unsigned compare in Release)
//   +0x14 the lock (cRZCriticalSection, 8 bytes: slot 1 enters, slot 2
//         leaves; it nests)
//   +0x1C float  the city's size in x, +0x20 in z (SetCitySize; saved)
//   +0x24 int32  the size in 64 m cells, x and +0x28 z
//   +0x2C int32  the size in 16 m cells, x and +0x30 z
//   +0x34 the manager's own filter by type (0x10 bytes, made by Init: the
//         type at its +0xC, set by every by-type slot before use)
//   +0x38 the grid: a vector (begin, end, capacity) of 192 column vectors
//         of 192 cell pointers each. A cell is null or 4 bytes (operator
//         new) holding the head node of a list (STL allocator, 0xC-byte
//         nodes: next, previous, value). The value is the occupant with
//         bit 0 set when it lies in this one cell only. An occupant whose
//         box spans several cells has an entry in each (bit 0 clear) and
//         keeps the rectangle itself (its +0x28: min x, min z, max x, max
//         z as bytes; slots 25 and 26 of the occupant). New entries go to
//         the FRONT of a list; a loaded city has the saved order.
//   +0x44 the visited set (0x58 bytes), emptied after every use: the
//         occupants of several cells already reported by a query
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::occmgr_sim {

inline constexpr uint32_t kClassId = 0x098F964D;
inline constexpr uint32_t kIidUnknown = 0x00000001;
inline constexpr uint32_t kIidManager = 0xC6BB8334;
inline constexpr uint32_t kIidMessageTarget = 0x452294AA;
inline constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
inline constexpr uint32_t kIidOccupant = 0x26BB767E;
inline constexpr uint32_t kMessageInserted = 0x99EF1142;
inline constexpr uint32_t kMessageRemoved = 0x99EF1143;
inline constexpr uint32_t kMessageMoved = 0x99EF1146;

inline constexpr std::size_t kObjectSize = 0x9C;
inline constexpr std::size_t kSerializablePart = 0x4;
inline constexpr std::size_t kMessagePart = 0x8;
inline constexpr std::size_t kSlots = 50;
inline constexpr std::size_t kSerializableSlots = 6;
inline constexpr std::size_t kMessageSlots = 4;
inline constexpr std::size_t kSetSize = 0x58;

// The constants that fix the grid: 192 world cells a side, the city
// starting at cell 64, 64 m a cell (4096 m of margin), 16 m a standard city
// cell.
inline constexpr int32_t kWorldCells = 0xC0;
inline constexpr int32_t kCityOrigin = 0x40;

// An improvement beyond the original (docs/city-size.md, "The 8 km spike"):
// with [improvements] big_cities = on, every simulators.occmgr group ours
// (the game's own slots know only 192) and simulators.weather.bodies ours
// (the game's scratch is 256 by 256 on the stack), a new city's grid is sized from the
// city, with the same 64-cell margin on both sides, at most 255 a side: an
// occupant keeps its cells as bytes and a first byte of 0xFF means "not in
// the manager". Tiles are square, so one count serves both axes. Off (the
// default): 192, as the game. Decided on first use, after the replacements
// are applied.
bool BigCities();
// The cells a side Init gives an empty grid: 192, or with big cities enough
// for the city's size (SetCitySize comes first).
int32_t NewGridCells(char* self);

namespace field {
inline constexpr std::size_t kInitialized = 0x0C;
inline constexpr std::size_t kLoaded = 0x0D;
inline constexpr std::size_t kRefCount = 0x10;
inline constexpr std::size_t kLock = 0x14;
inline constexpr std::size_t kSizeX = 0x1C;
inline constexpr std::size_t kSizeZ = 0x20;
inline constexpr std::size_t kCellsX = 0x24;
inline constexpr std::size_t kCellsZ = 0x28;
inline constexpr std::size_t kCityCellsX = 0x2C;
inline constexpr std::size_t kCityCellsZ = 0x30;
inline constexpr std::size_t kFilter = 0x34;
inline constexpr std::size_t kGrid = 0x38;
inline constexpr std::size_t kVisited = 0x44;
} // namespace field

// The manager's slots (byte offsets in the vtable 0x00A8F7F0).
namespace slot {
inline constexpr std::size_t kInit = 0x0C;
inline constexpr std::size_t kShutdown = 0x10;
inline constexpr std::size_t kWorldCellToOccupantManagerCell = 0x28;
inline constexpr std::size_t kOccupantManagerCellToWorldCell = 0x2C;
inline constexpr std::size_t kWorldCellToStandardCityCell = 0x30;
inline constexpr std::size_t kStandardCityCellToWorldCell = 0x34;
inline constexpr std::size_t kWorldCellToPosition = 0x38;
inline constexpr std::size_t kPositionToWorldCell = 0x3C;
inline constexpr std::size_t kOccupantManagerCellToPosition = 0x40;
inline constexpr std::size_t kPositionToOccupantManagerCell = 0x44;
inline constexpr std::size_t kStandardCityCellToPosition = 0x48;
inline constexpr std::size_t kPositionToStandardCityCell = 0x4C;
inline constexpr std::size_t kRemoveOccupants = 0x58;
inline constexpr std::size_t kIsCellEmpty = 0x64;
inline constexpr std::size_t kGetBoundingCells = 0x68;
inline constexpr std::size_t kFindOccupant = 0x6C;
inline constexpr std::size_t kFirstByPosition = 0x70;
inline constexpr std::size_t kFirst = 0x78;
inline constexpr std::size_t kFirstByCityCell = 0x80;
inline constexpr std::size_t kFirstByCityCells = 0x88;
inline constexpr std::size_t kGetByBBox = 0x90;
inline constexpr std::size_t kGetByCells = 0x98;
inline constexpr std::size_t kGetByCityCells = 0xA0;
inline constexpr std::size_t kIterateByBBox = 0xA8;
inline constexpr std::size_t kIterate = 0xB0;
inline constexpr std::size_t kIterateByCityCellsByType = 0xB8;
inline constexpr std::size_t kIterateByCityCells = 0xBC;
inline constexpr std::size_t kReleaseOccupantList = 0xC0;
inline constexpr std::size_t kDeletingDestructor = 0xC4;
} // namespace slot

// The occupant's slots used here (cISC4Occupant; one implementation each
// for 0x18 and 0x64 to 0x80 in all 57 occupant vtables).
namespace occupant {
inline constexpr std::size_t kAddRef = 0x04;
inline constexpr std::size_t kRelease = 0x08;
inline constexpr std::size_t kInit = 0x0C;
inline constexpr std::size_t kShutdown = 0x10;
inline constexpr std::size_t kPropertyHolder = 0x18;
inline constexpr std::size_t kGetType = 0x1C;
inline constexpr std::size_t kGetPosition = 0x20;
inline constexpr std::size_t kGetBoundingBox = 0x28; // (float min[3], float max[3])
inline constexpr std::size_t kPostMessage = 0x3C;    // (message, data)
inline constexpr std::size_t kGetCells = 0x64;       // (uint8_t rect[4]); false when unset
inline constexpr std::size_t kSetCells = 0x68;       // (const uint8_t rect[4])
inline constexpr std::size_t kSetFlag = 0x74;
inline constexpr std::size_t kClearFlag = 0x7C;
inline constexpr std::size_t kIsFlagSet = 0x80;
} // namespace occupant

struct Vector {
    char* begin;
    char* end;
    char* capacity;
};

struct ListNode {
    ListNode* next;
    ListNode* previous;
    std::uintptr_t value;
};

// A cell: the list object (one pointer, to its head node).
struct Cell {
    ListNode* head;
};

inline Vector* Grid(char* self) { return reinterpret_cast<Vector*>(self + field::kGrid); }

// The grid's entry for a world cell (no range test, as the game's inline
// reads).
inline Cell*& CellAt(char* self, int32_t x, int32_t z)
{
    Vector* const column = reinterpret_cast<Vector*>(Grid(self)->begin + static_cast<std::ptrdiff_t>(x) * 12);
    return reinterpret_cast<Cell**>(column->begin)[z];
}

inline void* OccupantOf(const ListNode* node)
{
    return reinterpret_cast<void*>(node->value & ~static_cast<std::uintptr_t>(1));
}

// The node of an occupant in a cell's list, or the head node.
inline ListNode* FindNode(const Cell* cell, const void* occupant)
{
    ListNode* const head = cell->head;
    ListNode* node = head->next;
    while (node != head && OccupantOf(node) != occupant)
        node = node->next;
    return node;
}

inline void LockOf(char* self) { Virtual<void>(self + field::kLock, 0x4); }
inline void UnlockOf(char* self) { Virtual<void>(self + field::kLock, 0x8); }
inline uint32_t AddRefOf(void* object) { return Virtual<uint32_t>(object, 0x4); }
inline uint32_t ReleaseOf(void* object) { return Virtual<uint32_t>(object, 0x8); }

// The game's "a < b" on the x87 flags (fcomp, C0 tested alone): also true
// when either is not a number.
inline bool Below(float a, float b) { return !(a >= b); }

// An occupant's box and what the edits do to it before taking its cells:
// an axis wider than 1 is pulled in by 0.1 at both ends (in doubles, stored
// as floats).
inline constexpr float kMargin = 0.1f; // 0x00A8F768
inline void ShrinkAxis(float& low, float& high)
{
    if (static_cast<double>(high) - static_cast<double>(low) > 1.0) {
        low = static_cast<float>(static_cast<double>(low) + static_cast<double>(kMargin));
        high = static_cast<float>(static_cast<double>(high) - static_cast<double>(kMargin));
    }
}

// The grid's cells a side: its own columns, so a saved grid of any size is
// walked whole whatever the setting; 192 before Init (the game's constant).
inline int32_t WorldCells(char* self)
{
    const Vector* const grid = Grid(self);
    const int32_t columns = static_cast<int32_t>((grid->end - grid->begin) / 12);
    return columns ? columns : kWorldCells;
}

inline int32_t ClampCell(char* self, int32_t value)
{
    const int32_t cells = WorldCells(self);
    if (value < 0)
        value = 0;
    if (value >= cells)
        value = cells - 1;
    return value;
}

// Logs "occmgr: first <name>" once per flag.
void FirstCall(const char* name, bool& logged);

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

// Swaps the slots when every one still holds its original (build 641 only).
bool SwapSlots(const Slot* slots, std::size_t count, const char* what);

// A new cell with its empty list (operator new, then the STL allocator);
// null when operator new gives none (occmgr_sim.cpp).
Cell* NewCell();

// A new empty list of the node allocator at 0x00B207E4 (its head node), and
// an occupant appended to one (occmgr_sim.cpp).
ListNode* NewNodeList();
void AppendNode(ListNode* head, void* occupant);

// The visited set at +0x44 emptied as the game's inline code does after a
// query (occmgr_sim.cpp).
void ResetVisited(char* self);

} // namespace opensc4::occmgr_sim
