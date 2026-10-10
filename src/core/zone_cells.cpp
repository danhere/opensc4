// Our cSC4ZoneDeveloper: cells (build 641; code 0x0072C200 to 0x0072E520):
// the lot manager's rectangle queries and the cell-level workers of the
// parcellization.
//
//   Lot manager slots (`this` is object + 4):
//     7  0x0072E080 the lots of a cell rectangle, as a list of references
//     10 0x0072E2B0 the lots in the ring of cells around a rectangle
//     20 0x0072C2D0 CanCreateLot: the lowest verdict of the rectangle's
//        cells (0x0072C200: 0 outside, 1 water, 2 wrong zone, 3 network,
//        7 taken, 8 free)
//     25 0x0072CAE0 deletes the lots of a cell rectangle
//   Workers the other parts call (detoured):
//     0x0072CB90 the cells of a region that can be zoned (lots that do not
//       fit the zone are deleted on the way)
//     0x0072CDA0 the connected piece of a region at a cell, taken out of
//       the region (a flood fill over the four neighbours)
//     0x0072D200 one lot on a rectangle, facing the first side of a list
//       that has a street (a random start when the list is empty)
//     0x0072D440 a rectangle filled with lots one cell wide facing a side
//     0x0072DB60 the parcel outlines of HighlightParcels: one overlay
//       texture per cell of the planned parcels (map +0xD8) and per cell of
//       the region under another zone's lot
//     0x0072C8E0 a cheat: one default lot of every lot configuration
//   Container code of the class (detoured): 0x0072C370 the cell maps'
//   insert with a hint, 0x0072C810 an insertion through a cursor,
//   0x0072C860 and 0x0072C890 fill and copy_backward over lot grid columns.
//   0x0072C600 (the hinted insert of a map keyed by int32) is shared with
//   another class (0x0075ED94 calls it) and stays the game's.
//
// Fields used here (offsets from the object's start; names in zone_sim.h):
//   0x04 the lot manager part: slot 6 GetLot(x, z, 0), slot 7, slot 22
//        (x, z, width, depth, facing, lot**, 0/1, 0, configuration: makes a
//        lot; 0x0072D200 and the cheat call it), slot 24 DeleteLot
//   0x20 kDevelopment     its slot 36 gives the lot developer, whose slot 7
//                         (lot, configuration, 0, 0) builds the cheat's lots
//   0x28, 0x2C kCellsX/Z  int32: every bounds check
//   0x38 kTerrainPicker   slot 4 (cell*, key*, rotation, flip, colour, 3, 0;
//                         0x007526E0 of the vtable 0x00AB4410, ret 0x1C)
//                         places an outline texture (0x0072DB60)
//   0x40 kZones           the zone manager: slot 15 (zone type) is the
//                         outline's colour
//   0x54 kLotGrid         columns of 12 bytes, [x].begin[z] = cISC4Lot* (the
//                         lot manager slots read it at this + 0x50)
//   0x99 kPlaceStreets    uint8: set = a real operation (the cell's zone
//                         must match, 0x0072A040); clear = HighlightParcels
//                         (the planned cells of the maps +0xD8/+0xE4 count)
//   0xA0 kZoneType        int32: 7 to 9 are the zones parcelled cell by
//                         cell (0x0072D440) and outlined without edges
//   0xA4 kRegion          the operation's region or null: cells outside it
//                         are no cells (0x0072C200, 0x0072D440)
//   0xD4 kValueD4         int32: the first instance ID of the outline
//                         textures (type 0x7AB50E44, group 0x0986135E)
//   0xD8 kCellsD8         map from a cell to {+0x18, +0x1C the parcel the
//                         cell is planned for (compared as a pair), +0x20
//                         the parcel's facing}; 0x0072DB60 walks it
//   0xF0 kMapF0           read through the object part's 0x0072C180: side
//                         values to {texture offset, rotation, flip}
//   0x00B4B398            the class's random generator (0x0072D200)
// Sides and facings: 0 north (z - 1), 1 east (x + 1), 2 south, 3 west.
//
// Virtual calls: every argument count below was taken from the callee (the
// `ret N` of the vtable slot): lot manager 0x00AB37C0 slots 6 (3), 7 (6),
// 22 (9), 24 (1); primary 0x00AB3830 slot 8 (3), slot 9 (a tail jump to the
// terrain 0x00AB3D18 slot 34, 2); cISC4Lot 0x00AB19B0 slots 1, 2, 9, 23,
// 31 (0), 14 (1), 21 (2); lot configuration 0x00A8CEB8 slots 7 (2), 14
// (0), 15 (1); zone manager 0x00A92E00 slot 15 (1); lot configuration
// manager 0x00A8EC58 slot 20 (0); building development simulator
// 0x00AB0AF8 slot 36 (0); lot developer 0x00AB1B98 slot 7 (4); the +0x38
// object 0x00AB4410 slot 4 (7).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/zone_sim.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <iterator>

namespace opensc4::zone_sim {
namespace {

// --- The game's helpers --------------------------------------------------------

// A node of the game's STL lists: the value follows (4 or 8 bytes).
struct Node {
    Node* next;
    Node* previous;
};

template <typename T>
T& Value(Node* node, std::size_t index = 0)
{
    return reinterpret_cast<T*>(node + 1)[index];
}

Node* StlNode(std::size_t size)
{
    using Fn = void*(__cdecl*)(std::size_t);
    return static_cast<Node*>(reinterpret_cast<Fn>(addr::SC4_stl_allocate)(size));
}

void Free(void* memory)
{
    using Fn = void(__cdecl*)(void*);
    reinterpret_cast<Fn>(addr::SC4_Free)(memory);
}

// A new empty list: its head node.
Node* NewList(std::size_t nodeSize)
{
    Node* const head = StlNode(nodeSize);
    head->next = head;
    head->previous = head;
    return head;
}

// push_back as the game inlines it (the node is linked after its value is
// set).
void LinkBefore(Node* node, Node* where)
{
    Node* const before = where->previous;
    node->next = where;
    node->previous = before;
    before->next = node;
    where->previous = node;
}

void* LotManagerOf(void* self) { return static_cast<char*>(self) + kLotManagerPart; }

void* GetLot(void* self, int32_t x, int32_t z)
{
    return Virtual<void*>(LotManagerOf(self), 0x18, x, z, 0u);
}

// The lot of a cell in the grid (no checks, as the game).
void* GridLot(void* self, int32_t x, int32_t z)
{
    void** const column = *reinterpret_cast<void***>(At<char*>(self, field::kLotGrid) + x * 12);
    return column[z];
}

bool ExistsNetwork(void* self, int32_t x, int32_t z, uint32_t mask)
{
    return Virtual<uint8_t>(self, 0x20, x, z, mask) != 0;
}

bool IsCellWater(void* self, int32_t x, int32_t z) { return Virtual<uint8_t>(self, 0x24, x, z) != 0; }

using CellTestFn = uint8_t(__thiscall*)(void* self, int32_t x, int32_t z);

// 0x0072A040 (the object part's): the cell's zone is the operation's.
bool ZoneMatches(void* self, int32_t x, int32_t z)
{
    return reinterpret_cast<CellTestFn>(addr::cSC4ZoneDeveloper_Fn_0072A040)(self, x, z) != 0;
}

// 0x0072C050 (the object part's): a lot or a planned parcel has the cell.
bool CellTaken(void* self, int32_t x, int32_t z)
{
    return reinterpret_cast<CellTestFn>(addr::cSC4ZoneDeveloper_Fn_0072C050)(self, x, z) != 0;
}

bool RegionTest(void* cells, int32_t x, int32_t z)
{
    using Fn = uint8_t(__thiscall*)(const void*, int32_t, int32_t);
    return reinterpret_cast<Fn>(addr::SC4CellRegion_Test)(cells, x, z) != 0;
}

// The bit of a region's cell by column and row (no checks).
uint32_t& RegionWord(void* cells, uint32_t column, uint32_t row)
{
    return At<uint32_t**>(cells, region::kRows)[column][row >> 5];
}

void SetRegionBit(void* cells, uint32_t column, uint32_t row, bool value)
{
    const uint32_t bit = 1u << (row & 31);
    uint32_t& word = RegionWord(cells, column, row);
    word = value ? (word | bit) : (word & ~bit);
}

// A new region of `source`'s rectangle with every cell `fill` (operator new
// 0x24 bytes; the game goes on with a null pointer when that fails, as
// this does).
void* NewRegionLike(void* source, uint8_t fill)
{
    using NewFn = void*(__cdecl*)(std::size_t);
    using ConstructFn = void*(__thiscall*)(void* cells, uint32_t width, uint32_t depth, const uint8_t* fill);
    void* const made = reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x24);
    if (made) {
        for (std::size_t offset = 0; offset < 0x10; offset += 4)
            At<int32_t>(made, offset) = At<int32_t>(source, offset);
        const int32_t depth = At<int32_t>(source, region::kZ1) - At<int32_t>(source, region::kZ0) + 1;
        const int32_t width = At<int32_t>(source, region::kX1) - At<int32_t>(source, region::kX0) + 1;
        reinterpret_cast<ConstructFn>(addr::SC4CellRegion_ConstructCells)(
            static_cast<char*>(made) + region::kCellsVtable, static_cast<uint32_t>(width),
            static_cast<uint32_t>(depth), &fill);
    }
    return made;
}

// The cell map's find (0x0072AC60, the object part's): the node or the head.
void* FindCell(void* map, const int32_t* cell)
{
    using Fn = void*(__thiscall*)(void*, const int32_t*);
    return reinterpret_cast<Fn>(addr::cSC4ZoneDeveloper_Fn_0072AC60)(map, cell);
}

// --- CanCreateLot --------------------------------------------------------------

// 0x0072C200: the verdict on one cell.
int32_t CellVerdict(void* self, int32_t x, int32_t z, uint8_t ignoreZone, uint8_t ignoreWater)
{
    if (x < 0 || x >= At<int32_t>(self, field::kCellsX))
        return 0;
    if (z < 0 || z >= At<int32_t>(self, field::kCellsZ))
        return 0;
    if (void* const mask = At<void*>(self, field::kRegion)) {
        if (!RegionTest(mask, x, z))
            return 0;
    }
    if (!ignoreWater && IsCellWater(self, x, z))
        return 1;
    if (At<uint8_t>(self, field::kPlaceStreets) && !ignoreZone && !ZoneMatches(self, x, z))
        return 2;
    if (reinterpret_cast<CellTestFn>(addr::cSC4ZoneDeveloper_Fn_0072A010)(self, x, z))
        return 3;
    return CellTaken(self, x, z) ? 7 : 8;
}

// Lot manager slot 20 (0x0072C2D0): the lowest verdict over the rectangle;
// 8 when it is empty. Water is ignored when asked, else when the lot
// configuration (may be null) says so (its slot 14).
int32_t __fastcall CanCreateLot(void* lots, void*, int32_t x, int32_t z, int32_t width, int32_t depth,
                                uint8_t ignoreZone, uint8_t ignoreWater, void* configuration)
{
    static bool logged = false;
    FirstCall("CanCreateLot", logged);
    void* const self = static_cast<char*>(lots) - kLotManagerPart;
    int32_t lowest = 8;
    if (ignoreWater)
        ignoreWater = 1;
    else if (configuration)
        ignoreWater = Virtual<uint8_t>(configuration, 0x38);
    else
        ignoreWater = 0;
    const int32_t xEnd = x + width;
    const int32_t zEnd = z + depth;
    for (int32_t cx = x; cx < xEnd; ++cx) {
        for (int32_t cz = z; cz < zEnd; ++cz) {
            const int32_t verdict = CellVerdict(self, cx, cz, ignoreZone, ignoreWater);
            if (verdict < lowest)
                lowest = verdict;
        }
    }
    return lowest;
}

// --- Container code ------------------------------------------------------------

// 0x0072C370: insert_unique with a hint (STLport) of the maps keyed by a
// cell pair (+0xD8, +0xE4; nodes: parent +4, left +8, right +0xC, key
// +0x10). The head's left is the first node, its right the last; the size
// is at +4. The node insertion (0x0072BAE0) and the plain insert_unique
// (0x0072BBD0) are the object part's. Returns `out` with the node.
void** __fastcall InsertCellWithHint(void* map, void*, void** out, void* position, const int32_t* value)
{
    static bool logged = false;
    FirstCall("InsertCellWithHint", logged);
    using InsertFn = void(__thiscall*)(void* map, void** out, void* x, void* y, const int32_t* value, void* w);
    using InsertUniqueFn = void**(__thiscall*)(void* map, void** out, const int32_t* value);
    using StepFn = void(__thiscall*)(void** iterator);
    const auto less = [](const int32_t* a, const int32_t* b) { return a[0] == b[0] ? a[1] < b[1] : a[0] < b[0]; };
    const auto key = [](void* node) { return &At<int32_t>(node, 0x10); };
    const auto insert = [&](void* x, void* y, void* w) {
        reinterpret_cast<InsertFn>(addr::cSC4ZoneDeveloper_Fn_0072BAE0)(map, out, x, y, value, w);
        return out;
    };
    const auto insertUnique = [&] {
        void* pair[2]; // {node, bool}
        *out = *reinterpret_cast<InsertUniqueFn>(addr::cSC4ZoneDeveloper_Fn_0072BBD0)(map, pair, value);
        return out;
    };
    const auto keep = [&] {
        *out = position;
        return out;
    };
    void* const head = At<void*>(map, 0);
    if (position == At<void*>(head, 0x8)) {
        if (At<uint32_t>(map, 0x4) == 0)
            return insertUnique();
        if (less(value, key(position)))
            return insert(position, position, nullptr);
        if (!less(key(position), value))
            return keep();
        void* after = position;
        reinterpret_cast<StepFn>(addr::SC4MapIterator_Next)(&after);
        if (after == At<void*>(map, 0))
            return insert(nullptr, position, position);
        if (less(value, key(after))) {
            if (!At<void*>(position, 0xC))
                return insert(nullptr, position, position);
            return insert(after, after, nullptr);
        }
        return insertUnique();
    }
    if (position == head) {
        void* const last = At<void*>(head, 0xC);
        if (less(key(last), value))
            return insert(nullptr, last, position);
        return insertUnique();
    }
    void* before = position;
    reinterpret_cast<StepFn>(addr::SC4TreeIterator_Decrement)(&before);
    const bool valueFirst = less(value, key(position));
    if (valueFirst && less(key(before), value)) {
        if (!At<void*>(before, 0xC))
            return insert(nullptr, before, before);
        return insert(position, position, nullptr);
    }
    void* after = position;
    reinterpret_cast<StepFn>(addr::SC4MapIterator_Next)(&after);
    bool positionFirst = !valueFirst;
    if (!valueFirst) {
        positionFirst = less(key(position), value);
        if (!positionFirst)
            return keep();
        if (after == At<void*>(map, 0) || less(value, key(after))) {
            if (!At<void*>(position, 0xC))
                return insert(nullptr, position, position);
            return insert(after, after, nullptr);
        }
    }
    if (valueFirst == positionFirst)
        return keep();
    return insertUnique();
}

// 0x0072C810: inserts a dword through a cursor {node, list*} (the list:
// head node, +8 a count): at the end when the cursor is at the head, else
// after the cursor's node, the cursor stepping back one node. Nothing
// without a list. 0x0072E62E (zone_parcels.cpp) calls it on what the traffic
// simulator's slot 22 returns. EAX: the new node (0 without a list).
void* __fastcall InsertAtCursor(void* cursor, void*, const uint32_t* value)
{
    static bool logged = false;
    FirstCall("InsertAtCursor", logged);
    void* const list = At<void*>(cursor, 0x4);
    if (!list)
        return nullptr;
    Node* where = At<Node*>(list, 0);
    Node* const at = At<Node*>(cursor, 0);
    if (at != where) {
        At<Node*>(cursor, 0) = at->previous;
        where = at->next;
    }
    Node* const node = StlNode(0xC);
    Value<uint32_t>(node) = *value;
    LinkBefore(node, where);
    ++At<uint32_t>(At<void*>(cursor, 0x4), 0x8);
    return node;
}

using AssignFn = void(__thiscall*)(void* vector, const void* source);

// 0x0072C860: std::fill over lot grid columns (12-byte vectors of lots).
void __cdecl FillColumns(void* first, void* last, const void* value)
{
    static bool logged = false;
    FirstCall("FillColumns", logged);
    for (char* column = static_cast<char*>(first); column != last; column += 12)
        reinterpret_cast<AssignFn>(addr::SC4PtrVector_Assign)(column, value);
}

// 0x0072C890: std::copy_backward over lot grid columns; the destination's
// start. (The game's caller pushes two more arguments nothing reads.)
void* __cdecl CopyColumnsBackward(void* first, void* last, void* destinationEnd)
{
    static bool logged = false;
    FirstCall("CopyColumnsBackward", logged);
    const int32_t count = static_cast<int32_t>(static_cast<char*>(last) - static_cast<char*>(first)) / 12;
    char* source = static_cast<char*>(last);
    char* destination = static_cast<char*>(destinationEnd);
    for (int32_t left = count; left > 0; --left) {
        source -= 12;
        destination -= 12;
        reinterpret_cast<AssignFn>(addr::SC4PtrVector_Assign)(destination, source);
    }
    return destination;
}

// --- The cheat -------------------------------------------------------------------

// 0x0072C8E0 (DoMessage's command 0x8929E68E): walks the lot configuration
// manager's map (0x00B43D18 slot 20: an STLport hash map, nodes {next, ID,
// configuration}) and makes a default lot of each configuration: the lot
// manager's slot 22 at the next cell of a walk over the city (rows of z,
// x first) until one succeeds, the lot developer's slot 7 to build it, then
// width + 1 cells skipped. After every 16th lot 0x005F9140(1000, 1).
void __fastcall PlaceEveryConfiguration(void* self, void*)
{
    static bool logged = false;
    FirstCall("PlaceEveryConfiguration", logged);
    using BeginFn = void*(__thiscall*)(void* table, void* out);
    using PauseFn = void(__cdecl*)(uint32_t, uint8_t);
    void* const table = Virtual<void*>(Global<void*>(addr::SC4_Service43D18), 0x50);
    int32_t xLast = At<int32_t>(self, field::kCellsX) - 1;
    int32_t zLast = At<int32_t>(self, field::kCellsZ) - 1;
    if (xLast < 0)
        xLast = 0;
    if (zLast < 0)
        zLast = 0;
    int32_t x = 0, z = 0, made = 0;
    uint8_t state = 0; // 0 before the first cell, 1 walking, 2 past the end
    // The next cell; false past the end.
    const auto step = [&] {
        if (state == 2)
            return false;
        if (state == 0) {
            state = 1;
            return true;
        }
        if (++x <= xLast)
            return true;
        x = 0;
        return ++z <= zLast;
    };
    struct {
        void* node;
        void* table;
    } it{};
    reinterpret_cast<BeginFn>(addr::SC4HashTable_Begin)(table, &it);
    while (it.node) {
        void* const configuration = At<void*>(it.node, 0x8);
        int32_t width = 0, depth = 0;
        Virtual<void>(configuration, 0x1C, &width, &depth);
        if (!step())
            return;
        void* const lots = LotManagerOf(self);
        void* lot = nullptr;
        for (;;) {
            lot = nullptr;
            if (Virtual<uint8_t>(lots, 0x58, x, z, width, depth, 0u, &lot, 1u, 0u, configuration))
                break;
            if (!step())
                return;
        }
        void* const developer = Virtual<void*>(At<void*>(self, field::kDevelopment), 0x90);
        Virtual<void>(developer, 0x1C, lot, configuration, 0u, 0u);
        Virtual<uint32_t>(lot, 0x8);
        for (int32_t skip = width + 1; skip != 0; --skip) {
            if (state == 2)
                continue;
            if (state == 0) {
                state = 1;
            } else if (++x > xLast) {
                x = 0;
                if (++z > zLast)
                    state = 2;
            }
        }
        if (++made % 16 == 0)
            reinterpret_cast<PauseFn>(addr::SC4_Fn_005F9140)(1000, 1);
        // The hash map iterator's ++.
        void* next = At<void*>(it.node, 0);
        if (!next) {
            const auto buckets = [&] {
                return static_cast<uint32_t>((At<char*>(it.table, 0x8) - At<char*>(it.table, 0x4)) >> 2);
            };
            uint32_t index = At<uint32_t>(it.node, 0x4) % buckets();
            const uint32_t count = buckets();
            while (++index < count) {
                next = At<void**>(it.table, 0x4)[index];
                if (next)
                    break;
            }
        }
        it.node = next;
    }
}

// --- Lots of a rectangle ---------------------------------------------------------

// Lot manager slot 25 (0x0072CAE0): deletes the lots of a cell rectangle:
// slot 7 into a list of its own, slot 24 (DeleteLot) for each, then the
// list's references released and its nodes freed.
void __fastcall DeleteLotsInRect(void* lots, void*, int32_t x1, int32_t z1, int32_t x2, int32_t z2)
{
    static bool logged = false;
    FirstCall("DeleteLotsInRect", logged);
    Node* list = NewList(0xC);
    if (Virtual<int32_t>(lots, 0x1C, &list, x1, z1, x2, z2, 0u) != 0) {
        Node* const head = list;
        for (Node* node = head->next; node != head; node = node->next)
            Virtual<void>(lots, 0x60, Value<void*>(node));
    }
    for (Node* node = list->next; node != list;) {
        Node* const current = node;
        void* const lot = Value<void*>(current);
        node = node->next;
        if (lot)
            Virtual<uint32_t>(lot, 0x8);
        Free(current);
    }
    list->next = list;
    list->previous = list;
    Free(list);
}

// Lot manager slot 7 (0x0072E080): empties the list (a list of references,
// its head pointer at +0) and appends the lots of the rectangle, clipped to
// the city; the number appended. With byAnchor a lot counts at its own cell
// (lot slot 21) only; without, a cell's lot counts unless it is the lot of
// the cell before it in the column (even a null one resets that) or of the
// same cell of the column before: a lot wider and deeper than one cell can
// come more than once.
int32_t __fastcall GetLotsInRect(void* lots, void*, Node** list, int32_t x1, int32_t z1, int32_t x2, int32_t z2,
                                 uint8_t byAnchor)
{
    static bool logged = false;
    FirstCall("GetLotsInRect", logged);
    using ClearFn = void(__thiscall*)(void* list);
    using ConstructFn = void*(__thiscall*)(void* iterator);
    using SetRectFn = void(__thiscall*)(void* iterator, const int32_t* rect);
    using PushBackFn = void(__thiscall*)(void* list, void* const* object);
    void* const self = static_cast<char*>(lots) - kLotManagerPart;
    int32_t count = 0;
    reinterpret_cast<ClearFn>(addr::SC4RefList_Clear)(list);
    if (x1 < 0)
        x1 = 0;
    if (z1 < 0)
        z1 = 0;
    if (x2 >= At<int32_t>(self, field::kCellsX))
        x2 = At<int32_t>(self, field::kCellsX) - 1;
    if (z2 >= At<int32_t>(self, field::kCellsZ))
        z2 = At<int32_t>(self, field::kCellsZ) - 1;
    if (x1 > x2 || z1 > z2)
        return 0;
    if (byAnchor) {
        const int32_t rect[4] = {x1, z1, x2, z2};
        alignas(4) uint8_t iterator[0x20] = {};
        reinterpret_cast<ConstructFn>(addr::SC4CellIterator_Construct)(iterator);
        reinterpret_cast<SetRectFn>(addr::SC4CellIterator_SetRect)(iterator, rect);
        // The game steps the iterator's fields itself: the step at +0xC,
        // the rectangle at +0x10.
        const auto field = [&](std::size_t offset) { return At<int32_t>(iterator, offset); };
        uint8_t state = 0;
        int32_t x = x1, z = z1;
        if (x1 < field(0x10) || z1 < field(0x14) || x1 > field(0x18) || z1 > field(0x1C))
            state = 2;
        for (;;) {
            if (state == 2)
                return count;
            if (state == 0) {
                state = 1;
            } else {
                x += field(0xC);
                if (x > field(0x18)) {
                    x = field(0x10);
                    z += field(0xC);
                    if (z > field(0x1C))
                        return count;
                }
            }
            void* const lot = GridLot(self, x, z);
            if (!lot)
                continue;
            int32_t lotX = 0, lotZ = 0;
            Virtual<void>(lot, 0x54, &lotX, &lotZ);
            if (lotX != x || lotZ != z)
                continue;
            void* reference = lot;
            Virtual<uint32_t>(lot, 0x4);
            reinterpret_cast<PushBackFn>(addr::SC4RefList_PushBack2)(list, &reference);
            Virtual<uint32_t>(lot, 0x8);
            ++count;
        }
    }
    void*** previousColumn = nullptr;
    int32_t offset = x1 * 12;
    int32_t columns = x2 - x1 + 1;
    do {
        void*** const column = reinterpret_cast<void***>(At<char*>(self, field::kLotGrid) + offset);
        void* previous = nullptr;
        int32_t z = z1;
        do {
            void* const lot = (*column)[z];
            if (lot && lot != previous && !(previousColumn && lot == (*previousColumn)[z])) {
                Virtual<uint32_t>(lot, 0x4);
                Node* const head = *list;
                Node* const node = StlNode(0xC);
                Value<void*>(node) = lot;
                Virtual<uint32_t>(lot, 0x4);
                LinkBefore(node, head);
                Virtual<uint32_t>(lot, 0x8);
                ++count;
            }
            ++z;
            previous = lot;
        } while (z <= z2);
        previousColumn = column;
        offset += 12;
    } while (--columns != 0);
    return count;
}

// Lot manager slot 10 (0x0072E2B0): appends to the list (not emptied) the
// lots in the ring of cells one cell around the rectangle (corners
// included) that do not overlap the rectangle (lot slot 14) and are not in
// the list yet; the number appended. `sides` (1 north, 2 east, 4 south, 8
// west) picks the edges of the ring unless it is 15; an edge outside the
// city is dropped from it.
int32_t __fastcall GetLotsAroundRect(void* lots, void*, const int32_t* rect, Node** list, uint32_t sides)
{
    static bool logged = false;
    FirstCall("GetLotsAroundRect", logged);
    using NextFn = uint8_t(__thiscall*)(void* iterator, int32_t* x, int32_t* z);
    void* const self = static_cast<char*>(lots) - kLotManagerPart;
    const int32_t outer[4] = {rect[0] - 1, rect[1] - 1, rect[2] + 1, rect[3] + 1};
    int32_t clipped[4] = {outer[0], outer[1], outer[2], outer[3]};
    int32_t count = 0;
    if (outer[0] < 0) {
        clipped[0] = 0;
        sides &= ~8u;
    }
    if (clipped[1] < 0) {
        clipped[1] = 0;
        sides &= ~1u;
    }
    if (clipped[2] >= At<int32_t>(self, field::kCellsX)) {
        clipped[2] = At<int32_t>(self, field::kCellsX) - 1;
        sides &= ~2u;
    }
    if (outer[3] >= At<int32_t>(self, field::kCellsZ)) {
        clipped[3] = At<int32_t>(self, field::kCellsZ) - 1;
        sides &= ~4u;
    }
    // A cell iterator over the whole ring's rectangle, built in place as
    // the game does (state bytes +0 and +1, the cell +4/+8, the step +0xC,
    // the rectangle +0x10; bytes +2 and +3 are not written by the game).
    alignas(4) uint8_t iterator[0x20] = {};
    At<int32_t>(iterator, 0x4) = outer[0];
    At<int32_t>(iterator, 0x8) = outer[1];
    At<int32_t>(iterator, 0xC) = 1;
    At<int32_t>(iterator, 0x10) = outer[0];
    At<int32_t>(iterator, 0x14) = outer[1];
    At<int32_t>(iterator, 0x18) = outer[2] < outer[0] ? outer[0] : outer[2];
    At<int32_t>(iterator, 0x1C) = outer[3] < outer[1] ? outer[1] : outer[3];
    int32_t x = 0, z = 0;
    while (reinterpret_cast<NextFn>(addr::SC4RectIterator_Next)(iterator, &x, &z)) {
        if (x < clipped[0] || z < clipped[1] || x > clipped[2] || z > clipped[3])
            continue;
        void* const lot = GridLot(self, x, z);
        if (!lot)
            continue;
        int32_t bounds[4] = {};
        Virtual<void>(lot, 0x38, bounds);
        if (bounds[2] >= rect[0] && bounds[0] <= rect[2] && bounds[3] >= rect[1] && bounds[1] <= rect[3])
            continue;
        if (sides != 0xF) {
            if (z == outer[1] && !(sides & 1))
                continue;
            if (x == outer[2] && !(sides & 2))
                continue;
            if (z == outer[3] && !(sides & 4))
                continue;
            if (x == outer[0] && !(sides & 8))
                continue;
        }
        Node* const head = *list;
        Virtual<uint32_t>(lot, 0x4);
        Node* found = (*list)->next;
        while (found != *list && Value<void*>(found) != lot)
            found = found->next;
        const bool isNew = found == head;
        Virtual<uint32_t>(lot, 0x8);
        if (!isNew)
            continue;
        Virtual<uint32_t>(lot, 0x4);
        Node* const end = *list;
        Node* const node = StlNode(0xC);
        Value<void*>(node) = lot;
        Virtual<uint32_t>(lot, 0x4);
        LinkBefore(node, end);
        Virtual<uint32_t>(lot, 0x8);
        ++count;
    }
    return count;
}

// --- Regions -----------------------------------------------------------------------

// 0x0072CB90: a new region of the source's rectangle holding the source's
// cells that can be zoned: not a network cell (mask 0x1F4F; only with a
// zone type set), not water, and when the cell is taken (always looked at
// in a real operation) not under a lot that fits: one whose configuration
// (lot slot 9) accepts the zone type (configuration slot 15), or without a
// configuration one of zone type 1 to 6 while zoning 1 to 6. Another lot
// there is deleted (lot manager slot 24).
void* __fastcall ZonableCells(void* self, void*, void* source)
{
    static bool logged = false;
    FirstCall("ZonableCells", logged);
    void* const made = NewRegionLike(source, 0);
    for (int32_t column = 0; column <= At<int32_t>(made, region::kX1) - At<int32_t>(made, region::kX0); ++column) {
        const int32_t x = At<int32_t>(made, region::kX0) + column;
        for (int32_t row = 0; row <= At<int32_t>(made, region::kZ1) - At<int32_t>(made, region::kZ0); ++row) {
            const int32_t z = At<int32_t>(made, region::kZ0) + row;
            const uint32_t bit = 1u << (row & 31);
            if (!(RegionWord(source, static_cast<uint32_t>(column), static_cast<uint32_t>(row)) & bit))
                continue;
            if (At<int32_t>(self, field::kZoneType) != 0 && ExistsNetwork(self, x, z, 0x1F4F))
                continue;
            if (IsCellWater(self, x, z))
                continue;
            bool keep = true;
            if (At<uint8_t>(self, field::kPlaceStreets) || CellTaken(self, x, z)) {
                void* const lots = LotManagerOf(self);
                if (void* const lot = Virtual<void*>(lots, 0x18, x, z, 0u)) {
                    bool remove = true;
                    if (void* const configuration = Virtual<void*>(lot, 0x24)) {
                        if (Virtual<uint8_t>(configuration, 0x3C, At<int32_t>(self, field::kZoneType))) {
                            keep = false;
                            remove = false;
                        }
                    } else {
                        const int32_t zoning = At<int32_t>(self, field::kZoneType);
                        if (zoning > 0 && zoning <= 6) {
                            const int32_t zoned = Virtual<int32_t>(lot, 0x7C);
                            if (zoned > 0 && zoned <= 6) {
                                keep = false;
                                remove = false;
                            }
                        }
                    }
                    if (remove)
                        Virtual<void>(lots, 0x60, lot);
                }
            }
            SetRegionBit(made, static_cast<uint32_t>(column), static_cast<uint32_t>(row), keep);
        }
    }
    return made;
}

// 0x0072CDA0: takes the connected piece at (column, row) out of the region
// and returns it as a new region: the cells joined to the start through
// the four neighbours that have the start's value V get !V in the source
// and V in the new region, whose other cells are !V. (V is set for the
// game's callers.) The queue is a list of 0x10-byte nodes; the neighbours
// go in the order column + 1, row + 1, column - 1, row - 1. `this` is not
// read.
void* __fastcall TakeConnectedCells(void*, void*, void* source, uint32_t column, uint32_t row)
{
    static bool logged = false;
    FirstCall("TakeConnectedCells", logged);
    using DestroyFn = void(__thiscall*)(void* list);
    const auto test = [&](uint32_t c, uint32_t r) { return (RegionWord(source, c, r) & (1u << (r & 31))) != 0; };
    const bool value = test(column, row);
    SetRegionBit(source, column, row, !value);
    void* const made = NewRegionLike(source, value ? 0 : 1);
    SetRegionBit(made, column, row, value);
    Node* queue = NewList(0x10);
    const auto push = [&](uint32_t c, uint32_t r) {
        Node* const node = StlNode(0x10);
        Value<uint32_t>(node, 0) = c;
        Value<uint32_t>(node, 1) = r;
        LinkBefore(node, queue);
    };
    const auto visit = [&](uint32_t c, uint32_t r) {
        if (c >= At<uint32_t>(source, region::kWidth) || r >= At<uint32_t>(source, region::kDepth))
            return;
        if (test(c, r) != value)
            return;
        SetRegionBit(source, c, r, !value);
        SetRegionBit(made, c, r, value);
        push(c, r);
    };
    push(column, row);
    while (queue->next != queue) {
        Node* const node = queue->next;
        const uint32_t c = Value<uint32_t>(node, 0);
        const uint32_t r = Value<uint32_t>(node, 1);
        node->previous->next = node->next;
        node->next->previous = node->previous;
        Free(node);
        visit(c + 1, r);
        visit(c, r + 1);
        visit(c - 1, r);
        visit(c, r - 1);
    }
    reinterpret_cast<DestroyFn>(addr::SC4PairList_Destroy)(&queue);
    return made;
}

// --- Lots on a rectangle -------------------------------------------------------------

// 0x0072D200: makes one lot on the rectangle (lot manager slot 22), facing
// the first side of the list (a list of dwords, its head pointer at +0)
// along which a cell just outside the rectangle has a network of the mask
// 0xC49; the list's first side when none has. An empty list is filled
// first with the four sides in order from a random one (the class's
// generator). AL: slot 22's answer; *lot is the lot.
uint8_t __fastcall CreateLotFacingStreet(void* self, void*, int32_t x, int32_t z, int32_t width, int32_t depth,
                                         Node** sides, void** lot)
{
    static bool logged = false;
    FirstCall("CreateLotFacingStreet", logged);
    using BelowFn = uint32_t(__thiscall*)(void* random, uint32_t count);
    if ((*sides)->next == *sides) {
        uint32_t side = reinterpret_cast<BelowFn>(addr::SC4Random_BelowLcg)(
            reinterpret_cast<void*>(addr::cSC4ZoneDeveloper_Random), 4);
        for (int left = 4; left != 0; --left) {
            Node* const head = *sides;
            Node* const node = StlNode(0xC);
            Value<uint32_t>(node) = side;
            LinkBefore(node, head);
            if (static_cast<int32_t>(++side) > 3)
                side = 0;
        }
    }
    const auto create = [&](uint32_t facing) {
        return Virtual<uint8_t>(LotManagerOf(self), 0x58, x, z, width, depth, facing, lot, 0u, 0u, 0u);
    };
    const int32_t zLast = z + depth - 1;
    for (Node* node = (*sides)->next; node != *sides; node = node->next) {
        switch (Value<uint32_t>(node)) {
        case 0:
            if (z > 0) {
                for (int32_t cx = x; cx <= width + x - 1; ++cx) {
                    if (ExistsNetwork(self, cx, z - 1, 0xC49))
                        return create(0);
                }
            }
            break;
        case 1: {
            const int32_t xLast = width + x - 1;
            if (xLast < At<int32_t>(self, field::kCellsX) - 1) {
                for (int32_t cz = z; cz <= zLast; ++cz) {
                    if (ExistsNetwork(self, xLast + 1, cz, 0xC49))
                        return create(1);
                }
            }
            break;
        }
        case 2:
            if (zLast < At<int32_t>(self, field::kCellsZ) - 1) {
                for (int32_t cx = x; cx <= width + x - 1; ++cx) {
                    if (ExistsNetwork(self, cx, zLast + 1, 0xC49))
                        return create(2);
                }
            }
            break;
        case 3:
            if (x > 0) {
                for (int32_t cz = z; cz <= zLast; ++cz) {
                    if (ExistsNetwork(self, x - 1, cz, 0xC49))
                        return create(3);
                }
            }
            break;
        default:
            break;
        }
    }
    return create(Value<uint32_t>((*sides)->next));
}

// 0x0072D440: fills the rectangle with lots one cell wide that face `side`
// (the dwords 0 to 3 at 0x00AB36C4): for sides 0 and 2 column by column,
// for 1 and 3 row by row, each maximal run of usable cells becomes one lot
// (0x0072D200 with a list of the one side; the run is walked towards lower
// coordinates for sides 1 and 2). A cell is usable when it is in the city
// and the operation's region, not water, of the operation's zone (real
// operations only), without a network (mask 0x1F4F) and not taken. For
// zone types 7 to 9 every cell gets a 1 x 1 lot with a random facing
// instead, whatever its state, and the answer is 1. AL: the last lot's
// answer (0 when no lot was tried or the side is not 0 to 3).
uint8_t __fastcall FillWithLots(void* self, void*, int32_t x, int32_t z, int32_t width, int32_t depth, uint32_t side)
{
    static bool logged = false;
    FirstCall("FillWithLots", logged);
    using PushBackFn = void(__thiscall*)(void* list, const uint32_t* value);
    using DestroyFn = void(__thiscall*)(void* list);
    const int32_t xLast = x + width - 1;
    const int32_t zLast = z + depth - 1;
    const int32_t zoning = At<int32_t>(self, field::kZoneType);
    if (zoning >= 7 && zoning <= 9) {
        int32_t cz = z;
        for (int32_t rows = depth; rows > 0; --rows, ++cz) {
            int32_t cx = x;
            for (int32_t columns = width; columns > 0; --columns, ++cx) {
                void* lot = nullptr;
                Node* list = NewList(0xC);
                CreateLotFacingStreet(self, nullptr, cx, cz, 1, 1, &list, &lot);
                if (lot)
                    Virtual<uint32_t>(lot, 0x8);
                for (Node* node = list->next; node != list;) {
                    Node* const current = node;
                    node = node->next;
                    Free(current);
                }
                list->next = list;
                list->previous = list;
                Free(list);
            }
        }
        return 1;
    }
    if (side > 3)
        return 0;
    const auto usable = [&](int32_t cx, int32_t cz) {
        if (cx < 0 || cx >= At<int32_t>(self, field::kCellsX) || cz < 0 || cz >= At<int32_t>(self, field::kCellsZ))
            return false;
        if (void* const mask = At<void*>(self, field::kRegion)) {
            if (!RegionTest(mask, cx, cz))
                return false;
        }
        if (IsCellWater(self, cx, cz))
            return false;
        if (At<uint8_t>(self, field::kPlaceStreets) && !ZoneMatches(self, cx, cz))
            return false;
        if (ExistsNetwork(self, cx, cz, 0x1F4F))
            return false;
        return !CellTaken(self, cx, cz);
    };
    uint8_t result = 0;
    Node* list = NewList(0xC);
    reinterpret_cast<PushBackFn>(addr::SC4StlList_PushBack)(&list, &side);
    const auto place = [&](int32_t px, int32_t pz, int32_t w, int32_t d) {
        void* lot = nullptr;
        result = CreateLotFacingStreet(self, nullptr, px, pz, w, d, &list, &lot);
        if (lot)
            Virtual<uint32_t>(lot, 0x8);
    };
    int32_t run = 0;
    switch (side) {
    case 3: {
        int32_t cz = z;
        for (int32_t rows = depth; rows > 0; --rows, ++cz) {
            int32_t start = x;
            int32_t cx = x;
            for (int32_t columns = width; columns > 0; --columns, ++cx) {
                if (usable(cx, cz)) {
                    ++run;
                } else if (run > 0) {
                    place(start, cz, run, 1);
                    start = cx + 1;
                    run = 0;
                } else {
                    ++start;
                }
            }
            if (run > 0) {
                place(start, cz, run, 1);
                run = 0;
            }
        }
        break;
    }
    case 1: {
        int32_t cz = z;
        for (int32_t rows = depth; rows > 0; --rows, ++cz) {
            int32_t end = xLast;
            int32_t cx = xLast;
            for (int32_t columns = width; columns > 0; --columns, --cx) {
                if (usable(cx, cz)) {
                    ++run;
                } else if (run > 0) {
                    place(end - run + 1, cz, run, 1);
                    end = cx - 1;
                    run = 0;
                } else {
                    --end;
                }
            }
            if (run > 0) {
                place(end - run + 1, cz, run, 1);
                run = 0;
            }
        }
        break;
    }
    case 0: {
        int32_t cx = x;
        for (int32_t columns = width; columns > 0; --columns, ++cx) {
            int32_t start = z;
            int32_t cz = z;
            for (int32_t rows = depth; rows > 0; --rows, ++cz) {
                if (usable(cx, cz)) {
                    ++run;
                } else if (run > 0) {
                    place(cx, start, 1, run);
                    start = cz + 1;
                    run = 0;
                } else {
                    ++start;
                }
            }
            if (run > 0) {
                place(cx, start, 1, run);
                run = 0;
            }
        }
        break;
    }
    default: { // 2
        int32_t cx = x;
        for (int32_t columns = width; columns > 0; --columns, ++cx) {
            int32_t end = zLast;
            int32_t cz = zLast;
            for (int32_t rows = depth; rows > 0; --rows, --cz) {
                if (usable(cx, cz)) {
                    ++run;
                } else if (run > 0) {
                    place(cx, end - run + 1, 1, run);
                    end = cz - 1;
                    run = 0;
                } else {
                    --end;
                }
            }
            if (run > 0) {
                place(cx, end - run + 1, 1, run);
                run = 0;
            }
        }
        break;
    }
    }
    reinterpret_cast<DestroyFn>(addr::SC4PairList_Destroy)(&list);
    return result;
}

// --- The parcel outlines ---------------------------------------------------------------

// 0x0072DB60 (HighlightParcels' last step): one overlay texture per cell.
// First every cell of the planned parcels (the map +0xD8, in key order),
// then every cell of the region that has no network (mask 0x1F4F), is
// under a lot of another zone type and is not in the map. A cell's four
// side values (north, east, south, west) are 0 where the neighbour belongs
// to the same parcel or lot, else 2 on the side the parcel or lot faces
// and 1 on the others; all 0 for zone types 7 to 9. 0x0072C180 turns them
// into a texture offset (added to +0xD4 for the instance), a rotation and
// a flip, and the +0x38 object's slot 4 places the texture in the zone
// type's drag colour (zone manager slot 15). The game pushes the rotation
// and the flip as dwords of which it only wrote the low byte (the rest is
// stale stack; the callee 0x00751A70 reads the bytes): zeros here.
void __fastcall OutlineParcels(void* self, void*, void* cells)
{
    static bool logged = false;
    FirstCall("OutlineParcels", logged);
    using PieceFn = void(__thiscall*)(void* self, const int32_t* sides, int32_t* offset, uint8_t* rotation,
                                      uint8_t* flip);
    uint32_t key[3] = {0x7AB50E44, 0x0986135E, 0};
    const uint32_t colour =
        Virtual<uint32_t>(At<void*>(self, field::kZones), 0x3C, At<int32_t>(self, field::kZoneType));
    const int32_t zoning = At<int32_t>(self, field::kZoneType);
    const bool edges = !(zoning >= 7 && zoning <= 9);
    const auto draw = [&](int32_t* cell, const int32_t* sides) {
        int32_t offset = 0;
        uint8_t rotation = 0, flip = 0;
        reinterpret_cast<PieceFn>(addr::cSC4ZoneDeveloper_Fn_0072C180)(self, sides, &offset, &rotation, &flip);
        key[2] = static_cast<uint32_t>(At<int32_t>(self, field::kValueD4) + offset);
        Virtual<void>(At<void*>(self, field::kTerrainPicker), 0x10, cell, static_cast<void*>(key),
                      static_cast<uint32_t>(rotation), static_cast<uint32_t>(flip), colour, 3u, 0u);
    };

    void* const map = static_cast<char*>(self) + field::kCellsD8;
    void* head = At<void*>(map, 0);
    for (void* node = At<void*>(head, 0x8); node != head; head = At<void*>(map, 0)) {
        int32_t cell[2] = {At<int32_t>(node, 0x10), At<int32_t>(node, 0x14)};
        int32_t sides[4] = {1, 1, 1, 1};
        if (edges) {
            const auto side = [&](int index, int32_t facing) {
                void* const other = FindCell(map, cell);
                if (other != head && At<int32_t>(other, 0x18) == At<int32_t>(node, 0x18) &&
                    At<int32_t>(other, 0x1C) == At<int32_t>(node, 0x1C))
                    sides[index] = 0;
                else if (At<int32_t>(node, 0x20) == facing)
                    sides[index] = 2;
            };
            cell[1] -= 1;
            side(0, 0);
            cell[1] += 2;
            side(2, 2);
            cell[1] -= 1;
            cell[0] -= 1;
            side(3, 3);
            cell[0] += 2;
            side(1, 1);
            cell[0] -= 1;
        } else {
            sides[0] = sides[1] = sides[2] = sides[3] = 0;
        }
        draw(cell, sides);
        // The next node in key order.
        if (void* right = At<void*>(node, 0xC)) {
            node = right;
            while (void* left = At<void*>(node, 0x8))
                node = left;
        } else {
            void* parent = At<void*>(node, 0x4);
            while (node == At<void*>(parent, 0xC)) {
                node = parent;
                parent = At<void*>(parent, 0x4);
            }
            if (At<void*>(node, 0xC) != parent)
                node = parent;
        }
    }

    const int32_t x0 = At<int32_t>(cells, region::kX0);
    const int32_t z0 = At<int32_t>(cells, region::kZ0);
    int32_t x1 = At<int32_t>(cells, region::kX1);
    int32_t z1 = At<int32_t>(cells, region::kZ1);
    if (x1 < x0)
        x1 = x0;
    if (z1 < z0)
        z1 = z0;
    for (int32_t z = z0; z <= z1; ++z) {
        for (int32_t x = x0; x <= x1; ++x) {
            const uint32_t column = static_cast<uint32_t>(x - At<int32_t>(cells, region::kX0));
            const uint32_t row = static_cast<uint32_t>(z - At<int32_t>(cells, region::kZ0));
            if (column >= At<uint32_t>(cells, region::kWidth) || row >= At<uint32_t>(cells, region::kDepth))
                continue;
            if (!(RegionWord(cells, column, row) & (1u << (row & 31))))
                continue;
            if (ExistsNetwork(self, x, z, 0x1F4F))
                continue;
            void* const lot = GetLot(self, x, z);
            if (!lot)
                continue;
            if (Virtual<int32_t>(lot, 0x7C) == At<int32_t>(self, field::kZoneType))
                continue;
            int32_t cell[2] = {x, z};
            if (FindCell(map, cell) != At<void*>(map, 0))
                continue;
            int32_t sides[4] = {1, 1, 1, 1};
            if (edges) {
                const auto side = [&](int index, int32_t nx, int32_t nz, int32_t facing) {
                    if (GetLot(self, nx, nz) == lot)
                        sides[index] = 0;
                    else if (Virtual<int32_t>(lot, 0x5C) == facing)
                        sides[index] = 2;
                };
                side(0, x, z - 1, 0);
                side(2, x, z + 1, 2);
                side(3, x - 1, z, 3);
                side(1, x + 1, z, 1);
            } else {
                sides[0] = sides[1] = sides[2] = sides[3] = 0;
            }
            draw(cell, sides);
        }
    }
}

// --- Installation ------------------------------------------------------------------------

struct Slot {
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(7, addr::cSC4ZoneDeveloper_Fn_0072E080, GetLotsInRect),
    OPENSC4_SLOT(10, addr::cSC4ZoneDeveloper_Fn_0072E2B0, GetLotsAroundRect),
    OPENSC4_SLOT(20, addr::cSC4ZoneDeveloper_Fn_0072C2D0, CanCreateLot),
    OPENSC4_SLOT(25, addr::cSC4ZoneDeveloper_Fn_0072CAE0, DeleteLotsInRect),
};

#undef OPENSC4_SLOT

// The functions the other parts (and the game's code of this class) call
// directly. Their call sites (0x0072F835; 0x0072E62E; 0x007334E8, 0x007334F6,
// 0x0073354D; 0x00732A2E; 0x0072EC6E, 0x00733940; 0x0072ECD9, 0x00733E5B;
// 0x0072EB71, 0x00730AFC, 0x0073387E; 0x0072E886, 0x0072F41A, 0x00732B76;
// 0x00733F77) keep nothing in EAX, ECX or EDX across the call and pass
// nothing in registers but `this`, so plain detours do.
struct Detour {
    std::uintptr_t address;
    void* ours;
    void* original;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn), nullptr}

Detour g_detours[] = {
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072C370, InsertCellWithHint),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072C810, InsertAtCursor),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072C860, FillColumns),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072C890, CopyColumnsBackward),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072C8E0, PlaceEveryConfiguration),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072CB90, ZonableCells),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072CDA0, TakeConnectedCells),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072D200, CreateLotFacingStreet),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072D440, FillWithLots),
    OPENSC4_DETOUR(addr::cSC4ZoneDeveloper_Fn_0072DB60, OutlineParcels),
};

#undef OPENSC4_DETOUR

// Checks that every slot still holds the original, installs the detours,
// then swaps the slots.
bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4ZoneDeveloper_vftable_4);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("zone cells: slot %d of %08X holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(addr::cSC4ZoneDeveloper_vftable_4),
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (Detour& detour : g_detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, detour.ours, &detour.original)) {
            log::Write("zone cells: no detour at %08X", static_cast<unsigned>(detour.address));
            return false;
        }
    }
    for (const Slot& slot : kSlots) {
        std::uintptr_t* const entry = vtable + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("zone cells: %u slots and %u detours are ours", static_cast<unsigned>(std::size(kSlots)),
               static_cast<unsigned>(std::size(g_detours)));
    return true;
}

const bool g_registered = registry::Register(
    {"simulators.zone.cells", "simulators/zone", registry::Kind::Vtable, registry::Phase::PostAppInit,
     addr::cSC4ZoneDeveloper_vftable_4,
     "Our cSC4ZoneDeveloper cell workers: CanCreateLot, the lots in and around a rectangle and their deletion, "
     "the zonable cells and connected pieces of a region, lots facing a street, strips of lots, the parcel "
     "outlines, the lot-of-every-configuration cheat and the class's container code",
     false, Install});

} // namespace
} // namespace opensc4::zone_sim
