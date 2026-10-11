// Our cSC4OccupantManager, the queries (build 641; the class map is in
// occmgr_sim.h): IsCellEmpty, the first-occupant family, the get family
// (into a list), the iterate family (a callback) and ReleaseOccupantList,
// each with its by-type twin.
//
// Every virtual call below has its argument count from the callee's own
// `ret N`: the occupants (57 vtables; slots 1, 2 and 10: 0, 0 and 2
// arguments in every one), the filter's slot 3 (1 argument in the
// manager's own 0x00A8F79C; a caller's filter is the caller's class), the
// manager's slots 15, 18, 28, 30, 32, 34, 36, 38, 40, 42, 44 and 47, the
// lock and the allocator service.
//
// Order is game state and is the grid's: cells x then z (ascending), a
// cell's list front to back (newest first; saved order after a load). An
// occupant lying in several cells is reported at the first of them the
// walk meets (the visited set; entries with bit 0 set lie in one cell and
// skip it). The first-occupant slots do not use the set: they stop at the
// first match.
#include "core/occmgr_sim.h"

#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::occmgr_sim {
namespace {

using SelfFn = void(__thiscall*)(void* self);
using ClearFn = uint32_t(__thiscall*)(void* list);
using ConstructPartFn = void*(__thiscall*)(void* self);
using FreeFn = void(__cdecl*)(void* block);
using PushBackFn = void(__thiscall*)(void* list, void* const* value);
using SetInsertFn = uint8_t(__thiscall*)(void* set, void* key);
using CallbackFn = uint8_t(__cdecl*)(void* occupant, void* context);

const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto PushBack = reinterpret_cast<PushBackFn>(addr::SC4List_PushBack);
const auto SetInsert = reinterpret_cast<SetInsertFn>(addr::SC4VisitedSet_Insert);
const auto ClearNodeList = reinterpret_cast<SelfFn>(addr::SC4List_Clear2);
const auto DestroyNodeList = reinterpret_cast<SelfFn>(addr::SC4List_Destroy2);

constexpr float kLowest = -3.40282347e+38f; // 0xFF7FFFFF
constexpr float kHighest = 3.40282347e+38f; // 0x7F7FFFFF
constexpr double kMarginD = static_cast<double>(kMargin);

bool Included(void* filter, void* occupant) { return !filter || Virtual<uint8_t>(filter, 0xC, occupant); }

void BoxOf(void* occupant, float* low, float* high)
{
    Virtual<void*>(occupant, occupant::kGetBoundingBox, low, high);
}

// 0x0049D8B0 and its inline copies: the occupant's box and the query's
// overlap strictly in x, y and z.
bool Overlaps(const float* low, const float* high, const float* occupantLow, const float* occupantHigh)
{
    return Below(occupantLow[0], high[0]) && Below(low[0], occupantHigh[0]) && Below(occupantLow[1], high[1]) &&
           Below(low[1], occupantHigh[1]) && Below(occupantLow[2], high[2]) && Below(low[2], occupantHigh[2]);
}

// The same in x and z only, against a rectangle.
bool OverlapsFlat(float lowX, float lowZ, float highX, float highZ, const float* occupantLow,
                  const float* occupantHigh)
{
    return Below(occupantLow[0], highX) && Below(lowX, occupantHigh[0]) && Below(occupantLow[2], highZ) &&
           Below(lowZ, occupantHigh[2]);
}

// A city cell coordinate clamped to 0 .. size - 1 (in that order: a size
// of 0 gives -1).
int32_t ClampCity(int32_t value, int32_t size)
{
    if (value < 0)
        value = 0;
    if (value >= size)
        value = size - 1;
    return value;
}

float PlusMargin(float value) { return static_cast<float>(static_cast<double>(value) + kMarginD); }
float MinusMargin(float value) { return static_cast<float>(static_cast<double>(value) - kMarginD); }
double CellEdge(int32_t cell) { return static_cast<double>(static_cast<int32_t>(static_cast<uint32_t>(cell) << 4)); }

// 25 (0x0049E490): no initialized test.
uint8_t __fastcall IsCellEmpty(char* self, void*, int32_t x, int32_t z)
{
    static bool logged = false;
    FirstCall("IsCellEmpty", logged);
    if (x < 0 || x >= kWorldCells || z < 0 || z >= kWorldCells)
        return 1;
    const Cell* const cell = CellAt(self, x, z);
    return !cell || cell->head->next == cell->head ? 1 : 0;
}

// 28 (0x0049E4D0): the world cell of the position (slot 15; NOT range
// tested: a position outside the 192 x 192 grid reads outside it), its
// list front to back: the first occupant the filter takes whose box has
// min <= p < max in x and z, referenced.
uint8_t __fastcall GetFirstOccupantByPosition(char* self, void*, void** out, float x, float z, void* filter)
{
    static bool logged = false;
    FirstCall("GetFirstOccupantByPosition", logged);
    LockOf(self);
    uint8_t found = 0;
    if (At<uint8_t>(self, field::kInitialized)) {
        int32_t cellX = 0, cellZ = 0;
        Virtual<uint32_t>(self, slot::kPositionToWorldCell, x, z, &cellX, &cellZ);
        if (const Cell* const cell = CellAt(self, cellX, cellZ)) {
            ListNode* const head = cell->head;
            for (ListNode* node = head->next; node != head; node = node->next) {
                void* const occupant = OccupantOf(node);
                if (!Included(filter, occupant))
                    continue;
                float low[3] = {}, high[3] = {};
                BoxOf(occupant, low, high);
                if (x >= low[0] && x < high[0] && z >= low[2] && z < high[2]) {
                    *out = occupant;
                    AddRefOf(occupant);
                    found = 1;
                    break;
                }
            }
        }
    }
    UnlockOf(self);
    return found;
}

// 30 (0x0049E5E0): a world cell (false outside 0..191): the first occupant
// of its list the filter takes, referenced.
uint8_t __fastcall GetFirstOccupant(char* self, void*, void** out, int32_t x, int32_t z, void* filter)
{
    static bool logged = false;
    FirstCall("GetFirstOccupant", logged);
    LockOf(self);
    uint8_t found = 0;
    if (At<uint8_t>(self, field::kInitialized) && x >= 0 && x < kWorldCells && z >= 0 && z < kWorldCells) {
        if (const Cell* const cell = CellAt(self, x, z)) {
            ListNode* const head = cell->head;
            for (ListNode* node = head->next; node != head; node = node->next) {
                void* const occupant = OccupantOf(node);
                if (!Included(filter, occupant))
                    continue;
                *out = occupant;
                AddRefOf(occupant);
                found = 1;
                break;
            }
        }
    }
    UnlockOf(self);
    return found;
}

// 32 (0x0049E690): a standard city cell: the rectangle from its low corner
// + 0.1 to its high corner - 0.1 (16 m), the world cell of the low corner
// (slot 15; not range tested), and the first occupant of that list the
// filter takes whose box overlaps the rectangle in x and z; referenced.
uint8_t __fastcall GetFirstOccupantByStandardCityCell(char* self, void*, void** out, int32_t x, int32_t z,
                                                      void* filter)
{
    static bool logged = false;
    FirstCall("GetFirstOccupantByStandardCityCell", logged);
    LockOf(self);
    uint8_t found = 0;
    if (At<uint8_t>(self, field::kInitialized)) {
        const double edgeX = CellEdge(x), edgeZ = CellEdge(z);
        float highX = static_cast<float>(edgeX + 16.0);
        float highZ = static_cast<float>(16.0 + edgeZ);
        const float lowX = static_cast<float>(edgeX + kMarginD);
        const float lowZ = static_cast<float>(edgeZ + kMarginD);
        highX = MinusMargin(highX);
        highZ = MinusMargin(highZ);
        int32_t cellX = 0, cellZ = 0;
        Virtual<uint32_t>(self, slot::kPositionToWorldCell, lowX, lowZ, &cellX, &cellZ);
        if (const Cell* const cell = CellAt(self, cellX, cellZ)) {
            ListNode* const head = cell->head;
            for (ListNode* node = head->next; node != head; node = node->next) {
                void* const occupant = OccupantOf(node);
                if (!Included(filter, occupant))
                    continue;
                float low[3] = {}, high[3] = {};
                BoxOf(occupant, low, high);
                if (OverlapsFlat(lowX, lowZ, highX, highZ, low, high)) {
                    *out = occupant;
                    AddRefOf(occupant);
                    found = 1;
                    break;
                }
            }
        }
    }
    UnlockOf(self);
    return found;
}

// 34 (0x0049E800): a rectangle of standard city cells, each corner clamped
// to the city (+0x2C/+0x30; none given: 0, 0 and the sizes themselves, one
// past the last cell). The rectangle from the low corner + 0.1 to the cell
// after the high corner - 0.1; the world cells of its two corners (slot
// 15) walked x then z; the first occupant the filter takes whose box
// overlaps the rectangle in x and z; referenced. No visited set.
uint8_t __fastcall GetFirstOccupantByStandardCityCells(char* self, void*, void** out, const int32_t* low,
                                                       const int32_t* high, void* filter)
{
    static bool logged = false;
    FirstCall("GetFirstOccupantByStandardCityCells", logged);
    LockOf(self);
    if (!At<uint8_t>(self, field::kInitialized)) {
        UnlockOf(self);
        return 0;
    }
    int32_t lowCellX = 0, lowCellZ = 0;
    if (low) {
        lowCellX = ClampCity(low[0], At<int32_t>(self, field::kCityCellsX));
        lowCellZ = ClampCity(low[1], At<int32_t>(self, field::kCityCellsZ));
    }
    int32_t highCellX = At<int32_t>(self, field::kCityCellsX), highCellZ = At<int32_t>(self, field::kCityCellsZ);
    if (high) {
        highCellX = ClampCity(high[0], At<int32_t>(self, field::kCityCellsX));
        highCellZ = ClampCity(high[1], At<int32_t>(self, field::kCityCellsZ));
    }
    const float lowX = static_cast<float>(CellEdge(lowCellX) + kMarginD);
    const float lowZ = static_cast<float>(CellEdge(lowCellZ) + kMarginD);
    const float highX =
        MinusMargin(static_cast<float>(CellEdge(static_cast<int32_t>(static_cast<uint32_t>(highCellX) + 1u))));
    const float highZ =
        MinusMargin(static_cast<float>(CellEdge(static_cast<int32_t>(static_cast<uint32_t>(highCellZ) + 1u))));
    int32_t x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    Virtual<uint32_t>(self, slot::kPositionToWorldCell, lowX, lowZ, &x0, &z0);
    Virtual<uint32_t>(self, slot::kPositionToWorldCell, highX, highZ, &x1, &z1);
    for (int32_t x = x0; x <= x1; ++x)
        for (int32_t z = z0; z <= z1; ++z) {
            const Cell* const cell = CellAt(self, x, z);
            if (!cell)
                continue;
            ListNode* const head = cell->head;
            for (ListNode* node = head->next; node != head; node = node->next) {
                void* const occupant = OccupantOf(node);
                if (!Included(filter, occupant))
                    continue;
                float occupantLow[3] = {}, occupantHigh[3] = {};
                BoxOf(occupant, occupantLow, occupantHigh);
                if (OverlapsFlat(lowX, lowZ, highX, highZ, occupantLow, occupantHigh)) {
                    *out = occupant;
                    AddRefOf(occupant);
                    UnlockOf(self);
                    return 1;
                }
            }
        }
    UnlockOf(self);
    return 0;
}

// The corners of a get or iterate by box as world cells, clamped to the
// grid.
struct CellRange {
    int32_t x0, z0, x1, z1;
};

CellRange RangeOfBox(char* self, const float* low, const float* high)
{
    CellRange range{};
    Virtual<uint32_t>(self, slot::kPositionToWorldCell, low[0], low[2], &range.x0, &range.z0);
    Virtual<uint32_t>(self, slot::kPositionToWorldCell, high[0], high[2], &range.x1, &range.z1);
    range.x0 = ClampCell(range.x0);
    range.z0 = ClampCell(range.z0);
    range.x1 = ClampCell(range.x1);
    range.z1 = ClampCell(range.z1);
    return range;
}

// 36 (0x004A0670): false before Init and for a limit of 0. The box (none
// given: from 0, the lowest height, 0 to the city's size and the highest
// height) gives a rectangle of world cells. One cell: its list front to
// back; each occupant the filter takes whose box overlaps the query's is
// referenced and appended to the caller's list (0x00467B50). Several
// cells: x then z, an entry without the one-cell bit only when the visited
// set did not have it (asked BEFORE the filter and the box test, so an
// occupant rejected in one cell is not looked at again in the next); the
// append is the game's inline one. Stops at `limit` occupants. The set is
// emptied after the several-cells walk.
uint8_t __fastcall GetOccupantsByBBox(char* self, void*, ListNode** list, const float* low, const float* high,
                                      void* filter, uint32_t limit)
{
    static bool logged = false;
    FirstCall("GetOccupantsByBBox", logged);
    LockOf(self);
    if (!At<uint8_t>(self, field::kInitialized) || !limit) {
        UnlockOf(self);
        return 0;
    }
    float queryLow[3] = {0.0f, kLowest, 0.0f};
    float queryHigh[3] = {0.0f, kHighest, 0.0f};
    if (low) {
        std::memcpy(queryLow, low, sizeof queryLow);
    }
    if (high) {
        std::memcpy(queryHigh, high, sizeof queryHigh);
    } else {
        std::memcpy(&queryHigh[0], self + field::kSizeX, 4);
        std::memcpy(&queryHigh[2], self + field::kSizeZ, 4);
    }
    const CellRange range = RangeOfBox(self, queryLow, queryHigh);
    uint32_t count = 0;
    if (range.x0 == range.x1 && range.z0 == range.z1) {
        if (const Cell* const cell = CellAt(self, range.x0, range.z0)) {
            ListNode* const head = cell->head;
            for (ListNode* node = head->next; node != head; node = node->next) {
                void* occupant = OccupantOf(node);
                if (!Included(filter, occupant))
                    continue;
                float occupantLow[3] = {}, occupantHigh[3] = {};
                BoxOf(occupant, occupantLow, occupantHigh);
                if (!Overlaps(queryLow, queryHigh, occupantLow, occupantHigh))
                    continue;
                AddRefOf(occupant);
                PushBack(list, &occupant);
                if (++count >= limit)
                    break;
            }
        }
        UnlockOf(self);
        return 1;
    }
    bool full = false;
    for (int32_t x = range.x0; !full && x <= range.x1; ++x)
        for (int32_t z = range.z0; !full && z <= range.z1; ++z) {
            const Cell* const cell = CellAt(self, x, z);
            if (!cell)
                continue;
            ListNode* const head = cell->head;
            for (ListNode* node = head->next; node != head; node = node->next) {
                void* const occupant = OccupantOf(node);
                if (!(node->value & 1) && !SetInsert(self + field::kVisited, occupant))
                    continue;
                if (!Included(filter, occupant))
                    continue;
                float occupantLow[3] = {}, occupantHigh[3] = {};
                BoxOf(occupant, occupantLow, occupantHigh);
                if (!Overlaps(queryLow, queryHigh, occupantLow, occupantHigh))
                    continue;
                AddRefOf(occupant);
                AppendNode(*list, occupant);
                if (++count >= limit) {
                    full = true;
                    break;
                }
            }
        }
    ResetVisited(self);
    UnlockOf(self);
    return 1;
}

// 38 (0x004A0A70): as slot 36 for a rectangle of WORLD cells (each corner
// clamped to the grid; none given: the city, 64 to +0x24 + 63 and +0x28 +
// 63, not clamped), without a box test.
uint8_t __fastcall GetOccupantsByOccupantManagerCells(char* self, void*, ListNode** list, const int32_t* low,
                                                      const int32_t* high, void* filter, uint32_t limit)
{
    static bool logged = false;
    FirstCall("GetOccupantsByOccupantManagerCells", logged);
    LockOf(self);
    if (!At<uint8_t>(self, field::kInitialized) || !limit) {
        UnlockOf(self);
        return 0;
    }
    int32_t x0 = kCityOrigin, z0 = kCityOrigin;
    if (low) {
        x0 = ClampCell(low[0]);
        z0 = ClampCell(low[1]);
    }
    int32_t x1, z1;
    if (high) {
        x1 = ClampCell(high[0]);
        z1 = ClampCell(high[1]);
    } else {
        x1 = At<int32_t>(self, field::kCellsX) + 0x3F;
        z1 = At<int32_t>(self, field::kCellsZ) + 0x3F;
    }
    uint32_t count = 0;
    if (x0 == x1 && z0 == z1) {
        if (const Cell* const cell = CellAt(self, x0, z0)) {
            ListNode* const head = cell->head;
            for (ListNode* node = head->next; node != head; node = node->next) {
                void* occupant = OccupantOf(node);
                if (!Included(filter, occupant))
                    continue;
                AddRefOf(occupant);
                PushBack(list, &occupant);
                if (++count >= limit)
                    break;
            }
        }
        UnlockOf(self);
        return 1;
    }
    bool full = false;
    for (int32_t x = x0; !full && x <= x1; ++x)
        for (int32_t z = z0; !full && z <= z1; ++z) {
            const Cell* const cell = CellAt(self, x, z);
            if (!cell)
                continue;
            ListNode* const head = cell->head;
            for (ListNode* node = head->next; node != head; node = node->next) {
                void* const occupant = OccupantOf(node);
                if (!(node->value & 1) && !SetInsert(self + field::kVisited, occupant))
                    continue;
                if (!Included(filter, occupant))
                    continue;
                AddRefOf(occupant);
                AppendNode(*list, occupant);
                if (++count >= limit) {
                    full = true;
                    break;
                }
            }
        }
    ResetVisited(self);
    UnlockOf(self);
    return 1;
}

// What slots 40 and 47 make of a rectangle of standard city cells: the low
// corner clamped to the city (none: 0, 0), the high corner too (none: the
// sizes +0x2C/+0x30 themselves), the positions of the low cell and of the
// cell after the high one (slot 18), pulled in by 0.1, with every height.
void BoxOfCityCells(char* self, const int32_t* low, const int32_t* high, float* boxLow, float* boxHigh)
{
    int32_t lowX = 0, lowZ = 0;
    if (low) {
        lowX = ClampCity(low[0], At<int32_t>(self, field::kCityCellsX));
        lowZ = ClampCity(low[1], At<int32_t>(self, field::kCityCellsZ));
    }
    int32_t highX = At<int32_t>(self, field::kCityCellsX), highZ = At<int32_t>(self, field::kCityCellsZ);
    if (high) {
        highX = ClampCity(high[0], At<int32_t>(self, field::kCityCellsX));
        highZ = ClampCity(high[1], At<int32_t>(self, field::kCityCellsZ));
    }
    float fromX = 0.0f, fromZ = 0.0f, toX = 0.0f, toZ = 0.0f;
    Virtual<uint32_t>(self, slot::kStandardCityCellToPosition, lowX, lowZ, &fromX, &fromZ);
    Virtual<uint32_t>(self, slot::kStandardCityCellToPosition, static_cast<int32_t>(static_cast<uint32_t>(highX) + 1u),
                      static_cast<int32_t>(static_cast<uint32_t>(highZ) + 1u), &toX, &toZ);
    boxLow[0] = PlusMargin(fromX);
    boxLow[1] = kLowest;
    boxLow[2] = PlusMargin(fromZ);
    boxHigh[0] = MinusMargin(toX);
    boxHigh[1] = kHighest;
    boxHigh[2] = MinusMargin(toZ);
}

// 40 (0x0049E250): false before Init; else slot 36 with the cells' box. No
// lock of its own.
uint32_t __fastcall GetOccupantsByStandardCityCells(char* self, void*, ListNode** list, const int32_t* low,
                                                    const int32_t* high, void* filter, uint32_t limit)
{
    static bool logged = false;
    FirstCall("GetOccupantsByStandardCityCells", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 0;
    float boxLow[3], boxHigh[3];
    BoxOfCityCells(self, low, high, boxLow, boxHigh);
    return Virtual<uint32_t>(self, slot::kGetByBBox, list, static_cast<const float*>(boxLow),
                             static_cast<const float*>(boxHigh), filter, limit);
}

// The callbacks for a collected list: each occupant in order until one
// callback returns false; every one is released either way.
void CallBack(ListNode* found, CallbackFn callback, void* context, bool& go)
{
    for (ListNode* node = found->next; node != found; node = node->next) {
        void* const occupant = reinterpret_cast<void*>(node->value);
        if (go && !callback(occupant, context))
            go = false;
        ReleaseOf(occupant);
    }
}

// A local visited set (the iterate slots do not use the manager's), as the
// game makes and destroys it.
struct LocalSet {
    alignas(4) char bytes[kSetSize];
    LocalSet() { reinterpret_cast<ConstructPartFn>(addr::SC4VisitedSet_Construct)(bytes); }
    void Destroy()
    {
        reinterpret_cast<SelfFn>(addr::SC4VisitedSet_DestroyDeque)(bytes + 0x20);
        if (void* const buffer = At<void*>(bytes, 0x14))
            Free(buffer);
        if (void* const buffer = At<void*>(bytes, 0x8))
            Free(buffer);
    }
};

// The walk the two iterate slots share. One cell: its matches are
// collected (referenced) under the lock, the callbacks made after it.
// Several cells: cell by cell, x then z, with a local visited set; the
// walk ends at the first cell in which a callback returned false. The next
// node is read before the filter is asked, as the game does.
void IterateCells(char* self, const CellRange& range, CallbackFn callback, void* context, void* filter,
                  const float* low, const float* high)
{
    if (range.x0 == range.x1 && range.z0 == range.z1) {
        ListNode* found = NewNodeList();
        bool go = true;
        LockOf(self);
        if (const Cell* const cell = CellAt(self, range.x0, range.z0)) {
            ListNode* const head = cell->head;
            for (ListNode* node = head->next; node != head;) {
                ListNode* const next = node->next;
                void* const occupant = OccupantOf(node);
                node = next;
                if (!Included(filter, occupant))
                    continue;
                if (low) {
                    float occupantLow[3] = {}, occupantHigh[3] = {};
                    BoxOf(occupant, occupantLow, occupantHigh);
                    if (!Overlaps(low, high, occupantLow, occupantHigh))
                        continue;
                }
                AddRefOf(occupant);
                AppendNode(found, occupant);
            }
        }
        UnlockOf(self);
        CallBack(found, callback, context, go);
        DestroyNodeList(&found);
        return;
    }
    LocalSet visited;
    ListNode* found = NewNodeList();
    bool go = true;
    for (int32_t x = range.x0; go && x <= range.x1; ++x)
        for (int32_t z = range.z0; go && z <= range.z1; ++z) {
            LockOf(self);
            if (const Cell* const cell = CellAt(self, x, z)) {
                ListNode* const head = cell->head;
                for (ListNode* node = head->next; node != head;) {
                    const std::uintptr_t value = node->value;
                    node = node->next;
                    void* const occupant = reinterpret_cast<void*>(value & ~static_cast<std::uintptr_t>(1));
                    if (!(value & 1) && !SetInsert(visited.bytes, occupant))
                        continue;
                    if (!Included(filter, occupant))
                        continue;
                    if (low) {
                        float occupantLow[3] = {}, occupantHigh[3] = {};
                        BoxOf(occupant, occupantLow, occupantHigh);
                        if (!Overlaps(low, high, occupantLow, occupantHigh))
                            continue;
                    }
                    AddRefOf(occupant);
                    AppendNode(found, occupant);
                }
            }
            UnlockOf(self);
            CallBack(found, callback, context, go);
            ClearNodeList(&found);
        }
    ClearNodeList(&found);
    Virtual<void>(ListNodeAllocator(), 0x10, static_cast<void*>(found));
    visited.Destroy();
}

// 42 (0x004A0D70): true, also before Init (nothing done then). The box
// must be given (no null test). The cells of its corners, clamped; then
// the walk above with the box test. The lock is held only while a cell is
// read, not across the callbacks.
uint8_t __fastcall IterateOccupantsByBBox(char* self, void*, CallbackFn callback, void* context, const float* low,
                                          const float* high, void* filter)
{
    static bool logged = false;
    FirstCall("IterateOccupantsByBBox", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    float queryLow[3], queryHigh[3];
    std::memcpy(queryLow, low, sizeof queryLow);
    std::memcpy(queryHigh, high, sizeof queryHigh);
    const CellRange range = RangeOfBox(self, queryLow, queryHigh);
    IterateCells(self, range, callback, context, filter, queryLow, queryHigh);
    return 1;
}

// 44 (0x004A1360): true. A rectangle of WORLD cells given as two (x, -, z)
// triples, each clamped to the grid (none given: the city, as slot 38),
// walked as above without a box test. The lock is taken for the whole call
// (and again inside, cell by cell): the callbacks run under it.
uint8_t __fastcall IterateOccupants(char* self, void*, CallbackFn callback, void* context, const int32_t* low,
                                    const int32_t* high, void* filter)
{
    static bool logged = false;
    FirstCall("IterateOccupants", logged);
    LockOf(self);
    if (At<uint8_t>(self, field::kInitialized)) {
        CellRange range{kCityOrigin, kCityOrigin, 0, 0};
        if (low) {
            range.x0 = ClampCell(low[0]);
            range.z0 = ClampCell(low[2]);
        }
        if (high) {
            range.x1 = ClampCell(high[0]);
            range.z1 = ClampCell(high[2]);
        } else {
            range.x1 = At<int32_t>(self, field::kCellsX) + 0x3F;
            range.z1 = At<int32_t>(self, field::kCellsZ) + 0x3F;
        }
        IterateCells(self, range, callback, context, filter, nullptr, nullptr);
    }
    UnlockOf(self);
    return 1;
}

// 47 (0x0049E370): false before Init; else slot 42 with the cells' box (as
// slot 40).
uint32_t __fastcall IterateOccupantsByStandardCityCell(char* self, void*, CallbackFn callback, void* context,
                                                       const int32_t* low, const int32_t* high, void* filter)
{
    static bool logged = false;
    FirstCall("IterateOccupantsByStandardCityCell", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 0;
    float boxLow[3], boxHigh[3];
    BoxOfCityCells(self, low, high, boxLow, boxHigh);
    return Virtual<uint32_t>(self, slot::kIterateByBBox, callback, context, static_cast<const float*>(boxLow),
                             static_cast<const float*>(boxHigh), filter);
}

// 48 (0x0049EF30): every occupant of a list released, front to back, then
// the list emptied (0x00413E60, whose EAX is returned as the game does).
uint32_t __fastcall ReleaseOccupantList(char*, void*, ListNode** list)
{
    static bool logged = false;
    FirstCall("ReleaseOccupantList", logged);
    ListNode* const head = *list;
    for (ListNode* node = head->next; node != head; node = node->next)
        ReleaseOf(reinterpret_cast<void*>(node->value));
    return reinterpret_cast<ClearFn>(addr::SC4List_Clear2)(list);
}

// --- The by-type twins --------------------------------------------------------------
// Each puts the type in the manager's own filter (+0x34, +0xC of it) and
// goes on to the by-filter slot with that filter (tail jumps in the game:
// the callee's EAX is returned). All are false before Init.

void* FilterFor(char* self, uint32_t type)
{
    void* const filter = At<void*>(self, field::kFilter);
    At<uint32_t>(filter, 0xC) = type;
    return At<void*>(self, field::kFilter);
}

// 29 (0x0049DC80), 31 (0x0049DCB0), 33 (0x0049DCE0), 35 (0x0049DD10): the
// filter is used whatever the type (type 0 matches nothing with a type).
uint32_t __fastcall GetFirstOccupantByPositionByType(char* self, void*, void** out, float x, float z, uint32_t type)
{
    static bool logged = false;
    FirstCall("GetFirstOccupantByPositionByType", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 0;
    return Virtual<uint32_t>(self, slot::kFirstByPosition, out, x, z, FilterFor(self, type));
}

uint32_t __fastcall GetFirstOccupantByType(char* self, void*, void** out, int32_t x, int32_t z, uint32_t type)
{
    static bool logged = false;
    FirstCall("GetFirstOccupantByType", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 0;
    return Virtual<uint32_t>(self, slot::kFirst, out, x, z, FilterFor(self, type));
}

uint32_t __fastcall GetFirstOccupantByStandardCityCellByType(char* self, void*, void** out, int32_t x, int32_t z,
                                                             uint32_t type)
{
    static bool logged = false;
    FirstCall("GetFirstOccupantByStandardCityCellByType", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 0;
    return Virtual<uint32_t>(self, slot::kFirstByCityCell, out, x, z, FilterFor(self, type));
}

uint32_t __fastcall GetFirstOccupantByStandardCityCellsByType(char* self, void*, void** out, const int32_t* low,
                                                              const int32_t* high, uint32_t type)
{
    static bool logged = false;
    FirstCall("GetFirstOccupantByStandardCityCellsByType", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 0;
    return Virtual<uint32_t>(self, slot::kFirstByCityCells, out, low, high, FilterFor(self, type));
}

// 37 (0x0049DD40), 39 (0x0049DD80), 41 (0x0049DDC0), 43 (0x0049DE00), 45
// (0x0049DE40), 46 (0x0049DE80): type 0 means no filter (the manager's is
// left as it was).
uint32_t __fastcall GetOccupantsByBBoxByType(char* self, void*, ListNode** list, const float* low, const float* high,
                                             uint32_t type, uint32_t limit)
{
    static bool logged = false;
    FirstCall("GetOccupantsByBBoxByType", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 0;
    return Virtual<uint32_t>(self, slot::kGetByBBox, list, low, high, type ? FilterFor(self, type) : nullptr, limit);
}

uint32_t __fastcall GetOccupantsByOccupantManagerCellsByType(char* self, void*, ListNode** list, const int32_t* low,
                                                             const int32_t* high, uint32_t type, uint32_t limit)
{
    static bool logged = false;
    FirstCall("GetOccupantsByOccupantManagerCellsByType", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 0;
    return Virtual<uint32_t>(self, slot::kGetByCells, list, low, high, type ? FilterFor(self, type) : nullptr, limit);
}

uint32_t __fastcall GetOccupantsByStandardCityCellsByType(char* self, void*, ListNode** list, const int32_t* low,
                                                          const int32_t* high, uint32_t type, uint32_t limit)
{
    static bool logged = false;
    FirstCall("GetOccupantsByStandardCityCellsByType", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 0;
    return Virtual<uint32_t>(self, slot::kGetByCityCells, list, low, high, type ? FilterFor(self, type) : nullptr,
                             limit);
}

uint32_t __fastcall IterateOccupantsByBBoxByType(char* self, void*, CallbackFn callback, void* context,
                                                 const float* low, const float* high, uint32_t type)
{
    static bool logged = false;
    FirstCall("IterateOccupantsByBBoxByType", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 0;
    return Virtual<uint32_t>(self, slot::kIterateByBBox, callback, context, low, high,
                             type ? FilterFor(self, type) : nullptr);
}

uint32_t __fastcall IterateOccupantsByType(char* self, void*, CallbackFn callback, void* context, const int32_t* low,
                                           const int32_t* high, uint32_t type)
{
    static bool logged = false;
    FirstCall("IterateOccupantsByType", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 0;
    return Virtual<uint32_t>(self, slot::kIterate, callback, context, low, high,
                             type ? FilterFor(self, type) : nullptr);
}

uint32_t __fastcall IterateOccupantsByStandardCityCellByType(char* self, void*, CallbackFn callback, void* context,
                                                             const int32_t* low, const int32_t* high, uint32_t type)
{
    static bool logged = false;
    FirstCall("IterateOccupantsByStandardCityCellByType", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 0;
    return Virtual<uint32_t>(self, slot::kIterateByCityCells, callback, context, low, high,
                             type ? FilterFor(self, type) : nullptr);
}

#define OCCMGR_SLOT(index, name)                                                                                     \
    {                                                                                                                \
        addr::cSC4OccupantManager_vftable, index, addr::cSC4OccupantManager_##name, reinterpret_cast<void*>(&name)  \
    }

const Slot kQuerySlots[] = {
    OCCMGR_SLOT(25, IsCellEmpty),
    OCCMGR_SLOT(28, GetFirstOccupantByPosition),
    OCCMGR_SLOT(29, GetFirstOccupantByPositionByType),
    OCCMGR_SLOT(30, GetFirstOccupant),
    OCCMGR_SLOT(31, GetFirstOccupantByType),
    OCCMGR_SLOT(32, GetFirstOccupantByStandardCityCell),
    OCCMGR_SLOT(33, GetFirstOccupantByStandardCityCellByType),
    OCCMGR_SLOT(34, GetFirstOccupantByStandardCityCells),
    OCCMGR_SLOT(35, GetFirstOccupantByStandardCityCellsByType),
    OCCMGR_SLOT(36, GetOccupantsByBBox),
    OCCMGR_SLOT(37, GetOccupantsByBBoxByType),
    OCCMGR_SLOT(38, GetOccupantsByOccupantManagerCells),
    OCCMGR_SLOT(39, GetOccupantsByOccupantManagerCellsByType),
    OCCMGR_SLOT(40, GetOccupantsByStandardCityCells),
    OCCMGR_SLOT(41, GetOccupantsByStandardCityCellsByType),
    OCCMGR_SLOT(42, IterateOccupantsByBBox),
    OCCMGR_SLOT(43, IterateOccupantsByBBoxByType),
    OCCMGR_SLOT(44, IterateOccupants),
    OCCMGR_SLOT(45, IterateOccupantsByType),
    OCCMGR_SLOT(46, IterateOccupantsByStandardCityCellByType),
    OCCMGR_SLOT(47, IterateOccupantsByStandardCityCell),
    OCCMGR_SLOT(48, ReleaseOccupantList),
};

bool InstallQueries(registry::Mode)
{
    return SwapSlots(kQuerySlots, sizeof kQuerySlots / sizeof kQuerySlots[0], "queries");
}

const bool g_registered = registry::Register(
    {"simulators.occmgr.queries", "simulators/occmgr", registry::Kind::Vtable, registry::Phase::PostAppInit,
     addr::cSC4OccupantManager_GetOccupantsByBBox,
     "Our occupant manager queries: IsCellEmpty, the first-occupant, get and iterate families with their by-type "
     "twins, ReleaseOccupantList (src/core/occmgr_queries.cpp)",
     false, InstallQueries});

} // namespace
} // namespace opensc4::occmgr_sim
