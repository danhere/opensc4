// Our cSC4GrowthDeveloper's messages and map update (build 641;
// docs/growth.md): DoMessage (cIGZMessageTarget2 slot 3, 0x006B7030) and
// everything it reaches in the class's own code: the map update state
// machine (0x006B5F20) with its grid mappers and the STLport sort of the
// growth and decline lists, the capacity update of a changed rectangle
// (0x006B4B90) and an occupant's stage change (0x006B51C0). The uint16
// grid mapper 0x006B4710, a sibling the developer never calls itself, is
// detoured so its outside caller (0x007A2080) gets ours too. Still the
// game's: the int16 mapper 0x0068C270 (outside the class), SC4Curve, the
// cell iterator, deadline, stopwatch, random generator, list and vector
// allocation helpers (shared with other classes).
//
// The map update's states (+0xD4; StartMapUpdate, slot 27, sets 1 when 0;
// IsMapUpdateFinished is +0xD4 == 0; GrowthAndDeclineListsAreValid is
// +0xD4 < 0x11). One state runs per call (message 0x66956814 with a budget
// of 5 ms, 0x6695681A with data 3), except 15 and 18 which may stay:
//   0      idle
//   1      waits for Init (+0xC), stamps the start day (+0xD8), then 2
//   2      the work grid +0x110 filled with the base desirability (+0x90)
//   3-14   curves of other simulators' grids added to it (+0x48 ... +0x88)
//   15     the developer's occupants' own effects spread into it, one
//          occupant per call (list +0xE4, cursor +0xE8)
//   16     the last curve (+0x8C)
//   17     +0x114 = +0x110; the growth list +0xEC = the cells of +0x10C
//   18     +0x118 = +0x11C capacity scaled by desirability, within a time
//          budget (cell iterator +0xDC)
//   19     the demand (+0x34 slot 17) told the change of the weighted
//          capacity (last +0x104)
//   20     the growth list sorted by desirability, the decline list +0xF8
//          made (shuffled for agriculture and industry), the growth list
//          thinned and its cells given a curve index (+0xA0); then 0
//
// Fields used (offsets from the object's start; names ours; for
// growth_sim.h):
//   0x0C  uint8: Init ran (state 1 waits for it)
//   0x14  the city: slot 95 (+0x17C) a position to a cell
//   0x1C  the development simulator: slot 35 (+0x8C) an object whose slot
//         7 takes (developer, x, z); slot 44 (+0xB0) its occupant list;
//         slot 45 (+0xB4) a cell's value for (zone type, +0x38, +0x3C);
//         slot 53 (+0xD4) a float scale
//   0x2C  the zone manager: slot 12 (+0x30) the zone type at (x, z)
//   0x34  the demand: slot 17 (+0x44) adds a float
//   0x38  uint32 purpose: compared with *0x00B43D68 slot 39 (occupant)
//         and 5..8 (agriculture and industry) by the map update
//   0x3C  uint32 wealth: DoMessage compares message data 4 with it (it is
//         the part's +0x38: growth_sim.h's kType 0x38 is the primary's)
//   0x47  uint8: state 3 takes 0x00B43D88 slot 7 instead of 8
//   0x48, 0x4C, 0x5C, 0x60, 0x64, 0x68, 0x6C, 0x70, 0x74, 0x78, 0x7C,
//   0x80, 0x84, 0x88, 0x8C  SC4Curve* (null: state skipped)
//   0x50  SC4Curve** (4, one per pollution grid 0-3): a pointer, read as
//         [[+0x50] + 4i]
//   0x90  int32: the base desirability (low word)
//   0x94  int32: the desirability threshold (states 19, 20)
//   0x98  int32: the decline threshold (state 20)
//   0x9C  uint8: state 19 sums the weighted capacity
//   0xA0, 0xA4  vector of 16-byte SC4Curves (state 20)
//   0xCC  uint32 subtype: occupants' property 0x4A5F7C4B lists it
//   0xD0  uint32 zone kind 1-3 (state 20)
//   0xD4  uint32 the map update's state; 0xD8 its start day
//   0xDC  the cell iterator of state 18 (+0 state byte, +4 x, +8 z, +0x10
//         x1, +0x14 z1, +0x18 x2, +0x1C z2)
//   0xE4  SC4RefList of occupants; 0xE8 its cursor node
//   0xEC  vector<Cell> growth list (end +0xF0, capacity +0xF4)
//   0xF8  vector<Cell> decline list (end +0xFC, capacity +0x100)
//   0x104 int32: the weighted capacity last told the demand
//   0x108 int32: the total capacity (occupant stage changes)
//   0x10C uint8 grid: per cell, how many sub-cells fit (0x006B4B90)
//   0x110 int16 grid: the work desirability; 0x114 its copy
//   0x118 grid: capacity times desirability (state 18)
//   0x11C uint16 grid: capacity (occupant stage changes)
//   0x120 grid: summed cell values (0x006B4B90)
#include "core/growth_sim.h"

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

namespace opensc4::growth_sim {
namespace {

namespace local {
constexpr std::size_t kInitialized = 0x0C;
constexpr std::size_t kCity = 0x14;
constexpr std::size_t kDevelopment = 0x1C;
constexpr std::size_t kZones = 0x2C;
constexpr std::size_t kDemand = 0x34;
constexpr std::size_t kPurpose = 0x38;
constexpr std::size_t kWealth = 0x3C;
constexpr std::size_t kUseDesirability2 = 0x47;
constexpr std::size_t kCurve48 = 0x48;
constexpr std::size_t kCurve4C = 0x4C;
constexpr std::size_t kPollutionCurves = 0x50;
constexpr std::size_t kCurve5C = 0x5C;
constexpr std::size_t kCurve60 = 0x60;
constexpr std::size_t kCurve64 = 0x64;
constexpr std::size_t kCurve68 = 0x68;
constexpr std::size_t kCurve6C = 0x6C;
constexpr std::size_t kCurve70 = 0x70;
constexpr std::size_t kCurve74 = 0x74;
constexpr std::size_t kCurve78 = 0x78;
constexpr std::size_t kCurve7C = 0x7C;
constexpr std::size_t kCurve80 = 0x80;
constexpr std::size_t kCurve84 = 0x84;
constexpr std::size_t kCurve88 = 0x88;
constexpr std::size_t kCurve8C = 0x8C;
constexpr std::size_t kDesirabilityBase = 0x90;
constexpr std::size_t kDesirabilityThreshold = 0x94;
constexpr std::size_t kDeclineThreshold = 0x98;
constexpr std::size_t kSumCapacity = 0x9C;
constexpr std::size_t kCurves = 0xA0; // begin; end +0xA4
constexpr std::size_t kSubType = 0xCC;
constexpr std::size_t kZoneKind = 0xD0;
constexpr std::size_t kMapState = 0xD4;
constexpr std::size_t kStartDay = 0xD8;
constexpr std::size_t kCellIterator = 0xDC;
constexpr std::size_t kOccupants = 0xE4;
constexpr std::size_t kCursor = 0xE8;
constexpr std::size_t kGrowthList = 0xEC;
constexpr std::size_t kDeclineList = 0xF8;
constexpr std::size_t kToldCapacity = 0x104;
constexpr std::size_t kTotalCapacity = 0x108;
constexpr std::size_t kFits = 0x10C;
constexpr std::size_t kWork = 0x110;
constexpr std::size_t kWorkCopy = 0x114;
constexpr std::size_t kWeighted = 0x118;
constexpr std::size_t kCapacity = 0x11C;
constexpr std::size_t kSums = 0x120;
} // namespace local

// The cIGZMessageTarget2 part's offset in the object.
constexpr std::size_t kMessagePart = 0x4;

using SelfFn = void(__thiscall*)(void* self);
using EvaluateFn = double(__thiscall*)(void* curve, float x); // ST0, unrounded

// __ftol (0x009EEF04): truncation; the low word of its int64.
int32_t Ftol(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

uint32_t Bits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

double Evaluate(void* curve, float x) { return reinterpret_cast<EvaluateFn>(addr::SC4Curve_Evaluate)(curve, x); }

void* Service(std::uintptr_t global) { return Global<void*>(global); }

// The word `index` of a grid's rows (slot 24 or 25: a vector of 12-byte
// rows whose first word points to the row's cells).
template <typename T>
T* Row(void* rows, int32_t index)
{
    char* const begin = *static_cast<char**>(rows);
    return *reinterpret_cast<T**>(begin + index * 12);
}

// *cell += value as the game's 16-bit add.
void AddTo(int16_t* cell, int32_t value)
{
    *cell = static_cast<int16_t>(static_cast<uint16_t>(*cell) + static_cast<uint16_t>(value));
}

// --- The grid mappers ----------------------------------------------------------
//
// 0x006B3E40 (uint8 source), 0x006B4430 (int8), 0x006B4710 (uint16),
// 0x0068C270 (int16, the game's) and 0x006B4120 (float): `add` is called
// for every cell of the int16 target grid with the source's value. A
// coarser target (larger shift, slot 11) gets the sum of the source cells
// it covers, shifted down by the difference twice (the mean); a finer one
// gets each source value in every cell it covers.

using IntCallback = void(__cdecl*)(int16_t* cell, uint32_t value, void* data);
using FloatCallback = void(__cdecl*)(int16_t* cell, float value, void* data);

struct Shape {
    int32_t shift;
    int32_t rows;
    int32_t columns;
    void* cells;
};

// Slots 11, 12, 13 then 24 (the target) or 25 (the source), in that order.
Shape ShapeOf(void* grid, std::size_t cellsSlot)
{
    Shape shape;
    shape.shift = Virtual<int32_t>(grid, 0x2C);
    shape.rows = Virtual<int32_t>(grid, 0x30);
    shape.columns = Virtual<int32_t>(grid, 0x34);
    shape.cells = Virtual<void*>(grid, cellsSlot);
    return shape;
}

template <typename T>
void MapInts(void* target, void* source, IntCallback add, void* data)
{
    const Shape to = ShapeOf(target, 0x60);
    const Shape from = ShapeOf(source, 0x64);
    if (to.shift == from.shift) {
        for (int32_t r = 0; r < to.rows; ++r)
            for (int32_t c = 0; c < to.columns; ++c)
                add(Row<int16_t>(to.cells, r) + c, static_cast<uint32_t>(static_cast<int32_t>(Row<T>(from.cells, r)[c])),
                    data);
        return;
    }
    if (to.shift > from.shift) {
        const int32_t k = to.shift - from.shift;
        for (int32_t r = 0; r < to.rows; ++r) {
            for (int32_t c = 0; c < to.columns; ++c) {
                int32_t sum = 0;
                for (int32_t sr = r << k; sr < (r + 1) << k; ++sr)
                    for (int32_t sc = c << k; sc < (c + 1) << k; ++sc)
                        sum += Row<T>(from.cells, sr)[sc];
                add(Row<int16_t>(to.cells, r) + c, static_cast<uint32_t>((sum >> k) >> k), data);
            }
        }
        return;
    }
    const int32_t k = from.shift - to.shift;
    for (int32_t sr = 0; sr < from.rows; ++sr) {
        for (int32_t sc = 0; sc < from.columns; ++sc) {
            const int32_t value = Row<T>(from.cells, sr)[sc];
            for (int32_t r = sr << k; r < (sr + 1) << k; ++r)
                for (int32_t c = sc << k; c < (sc + 1) << k; ++c)
                    add(Row<int16_t>(to.cells, r) + c, static_cast<uint32_t>(value), data);
        }
    }
}

// 0x006B4120: the float source. The coarse sum is kept as an int, each
// cell added on the FPU and truncated; a finer target gets the truncated
// value.
void MapFloats(void* target, void* source, FloatCallback add, void* data)
{
    const Shape to = ShapeOf(target, 0x60);
    const Shape from = ShapeOf(source, 0x64);
    if (to.shift == from.shift) {
        for (int32_t r = 0; r < to.rows; ++r)
            for (int32_t c = 0; c < to.columns; ++c)
                add(Row<int16_t>(to.cells, r) + c, Row<float>(from.cells, r)[c], data);
        return;
    }
    if (to.shift > from.shift) {
        const int32_t k = to.shift - from.shift;
        for (int32_t r = 0; r < to.rows; ++r) {
            for (int32_t c = 0; c < to.columns; ++c) {
                int32_t sum = 0;
                for (int32_t sr = r << k; sr < (r + 1) << k; ++sr)
                    for (int32_t sc = c << k; sc < (c + 1) << k; ++sc)
                        sum = Ftol(static_cast<double>(sum) + static_cast<double>(Row<float>(from.cells, sr)[sc]));
                add(Row<int16_t>(to.cells, r) + c, static_cast<float>((sum >> k) >> k), data);
            }
        }
        return;
    }
    const int32_t k = from.shift - to.shift;
    for (int32_t sr = 0; sr < from.rows; ++sr) {
        for (int32_t sc = 0; sc < from.columns; ++sc) {
            const int32_t value = Ftol(static_cast<double>(Row<float>(from.cells, sr)[sc]));
            for (int32_t r = sr << k; r < (sr + 1) << k; ++r)
                for (int32_t c = sc << k; c < (sc + 1) << k; ++c)
                    add(Row<int16_t>(to.cells, r) + c, static_cast<float>(value), data);
        }
    }
}

void MapUint8(void* target, void* source, IntCallback add, void* data) { MapInts<uint8_t>(target, source, add, data); }

void MapSint8(void* target, void* source, IntCallback add, void* data) { MapInts<int8_t>(target, source, add, data); }

void MapSint16(void* target, void* source, IntCallback add, void* data)
{
    reinterpret_cast<void(__cdecl*)(void*, void*, IntCallback, void*)>(addr::SC4GridMapper_FromSint16)(target, source,
                                                                                                    add, data);
}

// 0x006B4710 (detoured: its only caller is 0x007A213D, outside the class).
void __cdecl MapUint16(void* target, void* source, IntCallback add, void* data)
{
    static bool logged = false;
    FirstCall("MapUint16Grid", logged);
    MapInts<uint16_t>(target, source, add, data);
}

// The mappers' callbacks: the curve `data` at the value, truncated, added
// to the cell. 0x006B3AB0 (uint8), 0x006B3AE0 (float), 0x006B3B00 (int16),
// 0x006B3B30 (int8).
void __cdecl AddCurveOfUint8(int16_t* cell, uint32_t value, void* curve)
{
    AddTo(cell, Ftol(Evaluate(curve, static_cast<float>(static_cast<uint8_t>(value)))));
}

void __cdecl AddCurveOfFloat(int16_t* cell, float value, void* curve) { AddTo(cell, Ftol(Evaluate(curve, value))); }

void __cdecl AddCurveOfSint16(int16_t* cell, uint32_t value, void* curve)
{
    AddTo(cell, Ftol(Evaluate(curve, static_cast<float>(static_cast<int16_t>(value)))));
}

void __cdecl AddCurveOfSint8(int16_t* cell, uint32_t value, void* curve)
{
    AddTo(cell, Ftol(Evaluate(curve, static_cast<float>(static_cast<int8_t>(value)))));
}

// --- The cell lists ----------------------------------------------------------------

// An element of the growth and decline lists: the work grid's row and
// column, and a value (1 when listed; state 20 stores a curve index).
struct Cell {
    int32_t x;
    int32_t z;
    int32_t value;
};

struct CellVector {
    Cell* begin;
    Cell* end;
    Cell* capacity;
};

CellVector& ListAt(char* self, std::size_t offset) { return At<CellVector>(self, offset); }

// 0x006B56F0: push_back; the game's _M_insert_overflow when full.
void PushBack(CellVector& vector, const Cell* value)
{
    if (vector.end != vector.capacity) {
        if (vector.end)
            *vector.end = *value;
        ++vector.end;
        return;
    }
    using OverflowFn = void(__thiscall*)(CellVector*, Cell*, const Cell*, const void*, uint32_t, uint32_t);
    const uint32_t tag = 0; // a type tag passed by reference, never read
    reinterpret_cast<OverflowFn>(addr::SC4Vector12_InsertOverflow)(&vector, vector.end, value, &tag, 1, 1);
}

// 0x00626130 (inline here): clear() = end set to begin.
void Clear(CellVector& vector) { vector.end = vector.begin; }

// --- STLport sort on 12-byte cells (0x006B5EC0 and the functions it reaches) ---

using Compare = bool (*)(const Cell* a, const Cell* b);

// 0x006B3C70: __push_heap.
void PushHeap(Cell* first, int32_t hole, int32_t top, Cell value, Compare less)
{
    int32_t parent = (hole - 1) / 2;
    while (hole > top && less(first + parent, &value)) {
        first[hole] = first[parent];
        hole = parent;
        parent = (hole - 1) / 2;
    }
    first[hole] = value;
}

// 0x006B4A90: __adjust_heap.
void AdjustHeap(Cell* first, int32_t hole, int32_t length, Cell value, Compare less)
{
    const int32_t top = hole;
    int32_t second = 2 * hole + 2;
    while (second < length) {
        if (less(first + second, first + second - 1))
            --second;
        first[hole] = first[second];
        hole = second;
        second = 2 * second + 2;
    }
    if (second == length) {
        first[hole] = first[second - 1];
        hole = second - 1;
    }
    PushHeap(first, hole, top, value, less);
}

// 0x006B5530: make_heap.
void MakeHeap(Cell* first, Cell* last, Compare less)
{
    const int32_t length = static_cast<int32_t>(last - first);
    if (length < 2)
        return;
    int32_t parent = (length - 2) / 2;
    for (;;) {
        AdjustHeap(first, parent, length, first[parent], less);
        if (parent == 0)
            return;
        --parent;
    }
}

// 0x006B5650: sort_heap.
void SortHeap(Cell* first, Cell* last, Compare less)
{
    while (last - first > 1) {
        --last;
        const Cell value = *last;
        *last = *first;
        AdjustHeap(first, 0, static_cast<int32_t>(last - first), value, less);
    }
}

// 0x006B5780: partial_sort.
void PartialSort(Cell* first, Cell* middle, Cell* last, Compare less)
{
    MakeHeap(first, middle, less);
    for (Cell* i = middle; i < last; ++i) {
        if (less(i, first)) {
            const Cell value = *i;
            *i = *first;
            AdjustHeap(first, 0, static_cast<int32_t>(middle - first), value, less);
        }
    }
    SortHeap(first, middle, less);
}

// 0x006B5480: __unguarded_partition.
Cell* UnguardedPartition(Cell* first, Cell* last, Cell pivot, Compare less)
{
    for (;;) {
        while (less(first, &pivot))
            ++first;
        --last;
        while (less(&pivot, last))
            --last;
        if (!(first < last))
            return first;
        const Cell swap = *first;
        *first = *last;
        *last = swap;
        ++first;
    }
}

// The median of three as 0x006B5DA0 picks it.
const Cell* Median(const Cell* a, const Cell* b, const Cell* c, Compare less)
{
    if (less(a, b)) {
        if (less(b, c))
            return b;
        if (less(a, c))
            return c;
        return a;
    }
    if (less(a, c))
        return a;
    if (less(b, c))
        return c;
    return b;
}

// 0x006B5DA0: __introsort_loop.
void IntrosortLoop(Cell* first, Cell* last, int32_t depth, Compare less)
{
    while (last - first > 16) {
        if (depth == 0) {
            PartialSort(first, last, last, less);
            return;
        }
        --depth;
        const Cell pivot = *Median(first, first + (last - first) / 2, last - 1, less);
        Cell* const cut = UnguardedPartition(first, last, pivot, less);
        IntrosortLoop(cut, last, depth, less);
        last = cut;
    }
}

// 0x006B3C10: __unguarded_linear_insert.
void UnguardedLinearInsert(Cell* last, Cell value, Compare less)
{
    Cell* next = last - 1;
    while (less(&value, next)) {
        *last = *next;
        last = next;
        --next;
    }
    *last = value;
}

// 0x006B55D0: __linear_insert (copy_backward 0x00614430 inline).
void LinearInsert(Cell* first, Cell* last, Cell value, Compare less)
{
    if (less(&value, first)) {
        for (Cell* from = last; from != first;) {
            --from;
            from[1] = *from;
        }
        *first = value;
    } else {
        UnguardedLinearInsert(last, value, less);
    }
}

// 0x006B5730: __insertion_sort.
void InsertionSort(Cell* first, Cell* last, Compare less)
{
    if (first == last)
        return;
    for (Cell* i = first + 1; i != last; ++i)
        LinearInsert(first, i, *i, less);
}

// 0x006B5940: __final_insertion_sort (0x006B4A50 the unguarded part).
void FinalInsertionSort(Cell* first, Cell* last, Compare less)
{
    if (last - first > 16) {
        InsertionSort(first, first + 16, less);
        for (Cell* i = first + 16; i != last; ++i)
            UnguardedLinearInsert(i, *i, less);
    } else {
        InsertionSort(first, last, less);
    }
}

// 0x006B5EC0: sort.
void Sort(Cell* first, Cell* last, Compare less)
{
    if (first == last)
        return;
    int32_t depth = 0;
    for (int32_t n = static_cast<int32_t>(last - first); n != 1; n >>= 1)
        ++depth;
    IntrosortLoop(first, last, depth * 2, less);
    FinalInsertionSort(first, last, less);
}

// 0x006B4B50: higher work desirability first (the rows in SortRows).
bool MoreDesirable(const Cell* a, const Cell* b)
{
    void* const rows = Global<void*>(addr::cSC4GrowthDeveloper_SortRows);
    return Row<int16_t>(rows, a->x)[a->z] > Row<int16_t>(rows, b->x)[b->z];
}

// A cell's draw for the shuffle: a generator seeded with
// ((z << 16) + x) << 4 ^ ShuffleSeed, one step.
uint32_t ShuffleDraw(const Cell* cell)
{
    using ConstructFn = void*(__thiscall*)(void* random, uint32_t seed);
    using AdvanceFn = uint64_t(__fastcall*)(void* random);
    alignas(8) uint8_t random[0x18] = {};
    const uint32_t seed = ((static_cast<uint32_t>(cell->z) << 16) + static_cast<uint32_t>(cell->x)) << 4;
    reinterpret_cast<ConstructFn>(addr::SC4Random_Construct)(random,
                                                             seed ^ Global<uint32_t>(addr::cSC4GrowthDeveloper_ShuffleSeed));
    return static_cast<uint32_t>(reinterpret_cast<AdvanceFn>(addr::cRZRandom_Advance)(random));
}

// 0x006B3820: the shuffle order (a's draw above b's).
bool Shuffled(const Cell* a, const Cell* b)
{
    const uint32_t first = ShuffleDraw(a);
    return ShuffleDraw(b) < first;
}

// --- The map update (0x006B5F20) -----------------------------------------------

struct Deadline {
    uint32_t start[2];
    uint32_t count;
    uint32_t step;
    uint8_t timedOut;
    uint8_t padding[3];
    uint32_t* stepGlobal;
};

struct ListNode {
    ListNode* next;
    ListNode* prev;
};

void* Curve(char* self, std::size_t offset) { return At<void*>(self, offset); }

void* Work(char* self) { return At<void*>(self, local::kWork); }

// A curve applied to the work grid through a mapper.
void MapCurve(char* self, std::size_t curve, void (*map)(void*, void*, IntCallback, void*), void* source,
              IntCallback add)
{
    map(Work(self), source, add, Curve(self, curve));
}

// State 2: the work grid filled with the base desirability.
void FillBase(char* self)
{
    void* const work = Work(self);
    void* const rows = Virtual<void*>(work, 0x60);
    const int32_t count = Virtual<int32_t>(work, 0x30);
    const int32_t columns = Virtual<int32_t>(work, 0x34);
    const auto base = At<int16_t>(self, local::kDesirabilityBase);
    for (int32_t r = 0; r < count; ++r)
        for (int32_t c = 0; c < columns; ++c)
            Row<int16_t>(rows, r)[c] = base;
}

// State 12: per work cell, the lowest nonzero value of the n x n cells it
// covers in 0x00B43DA8 slot 13's object (slot 7 (x, z), a byte; 255 if
// none) through the curve +0x80.
void AddLowestCover(char* self)
{
    void* const curve = Curve(self, local::kCurve80);
    void* const work = Work(self);
    void* const rows = Virtual<void*>(work, 0x60);
    const int32_t count = Virtual<int32_t>(work, 0x30);
    const int32_t columns = Virtual<int32_t>(work, 0x34);
    const int32_t n = Virtual<int32_t>(work, 0x28);
    void* const cover = Virtual<void*>(Service(addr::SC4_Service43DA8), 0x34);
    for (int32_t r = 0; r < count; ++r) {
        for (int32_t c = 0; c < columns; ++c) {
            int32_t lowest = 0xFF;
            for (int32_t i = 0; i < n; ++i) {
                for (int32_t j = 0; j < n; ++j) {
                    const uint8_t value = Virtual<uint8_t>(cover, 0x1C, r * n + i, c * n + j);
                    if (value && value < lowest)
                        lowest = value;
                }
            }
            const double added = Evaluate(curve, static_cast<float>(lowest));
            int16_t* const cell = Row<int16_t>(rows, r) + c;
            *cell = static_cast<int16_t>(Ftol(static_cast<double>(*cell) + added));
        }
    }
}

// Entering state 15: the development simulator's occupants (slot 44)
// copied into the list +0xE4, the cursor at its first.
void CopyOccupants(char* self)
{
    using PushFn = void(__thiscall*)(void* list, void* const* object);
    void* const source = Virtual<void*>(At<void*>(self, local::kDevelopment), 0xB0);
    char* const list = self + local::kOccupants;
    reinterpret_cast<SelfFn>(addr::SC4RefList_Clear)(list);
    for (ListNode* node = (*static_cast<ListNode**>(source))->next; node != *static_cast<ListNode**>(source);
         node = node->next) {
        void* object = *reinterpret_cast<void**>(node + 1);
        if (object)
            Virtual<uint32_t>(object, 0x4);
        reinterpret_cast<PushFn>(addr::SC4RefList_PushBack2)(list, &object);
        if (object)
            Virtual<uint32_t>(object, 0x8);
    }
    At<ListNode*>(self, local::kCursor) = At<ListNode*>(self, local::kOccupants)->next;
}

// State 15: from the cursor, the first occupant whose property 0x4A5F7C4B
// lists the subtype +0xCC spreads the matching triple of property
// 0x4A5F7315 (radius in work cells, value, edge) into the work grid around
// its cell; the cursor moves past it. 16 when the list is done.
void SpreadOccupants(char* self)
{
    constexpr uint32_t kSubTypes = 0x4A5F7C4B;
    constexpr uint32_t kEffects = 0x4A5F7315;
    using SpreadFn = void(__cdecl*)(void* grid, int32_t rows, int32_t columns, int32_t x, int32_t z, float radius,
                                    float value, float edge);
    bool spread = false;
    for (;;) {
        ListNode* const node = At<ListNode*>(self, local::kCursor);
        if (node == At<ListNode*>(self, local::kOccupants))
            break;
        void* const occupant = *reinterpret_cast<void**>(node + 1);
        if (void* const property = Virtual<void*>(Virtual<void*>(occupant, 0x18), 0x24, kSubTypes)) {
            void* const variant = Virtual<void*>(property, 0x14);
            const uint32_t* const types = Virtual<const uint32_t*>(variant, 0x19C);
            const uint32_t count = Virtual<uint32_t>(variant, 0x10);
            const uint32_t subType = At<uint32_t>(self, local::kSubType);
            uint32_t i = 0;
            while (i < count && types[i] != subType)
                ++i;
            if (i < count) {
                if (void* const effects = Virtual<void*>(Virtual<void*>(occupant, 0x18), 0x24, kEffects)) {
                    void* const values = Virtual<void*>(effects, 0x14);
                    const float* const triples = Virtual<const float*>(values, 0x1BC);
                    Virtual<uint32_t>(values, 0x10);
                    void* const work = Work(self);
                    const int32_t n = Virtual<int32_t>(work, 0x28);
                    const float radius = static_cast<float>(static_cast<double>(triples[3 * i]) / static_cast<double>(n));
                    const float value = triples[3 * i + 1];
                    const float edge = triples[3 * i + 2];
                    // Unwritten if the calls fail: the game's stack there.
                    float position[3] = {0.0f, 0.0f, 0.0f};
                    Virtual<uint8_t>(occupant, 0x20, position);
                    int32_t x = 0;
                    int32_t z = 0;
                    Virtual<uint8_t>(At<void*>(self, local::kCity), 0x17C, position[0], position[2], &x, &z);
                    const int32_t shift = Virtual<int32_t>(work, 0x2C);
                    x >>= shift;
                    z >>= shift;
                    const int32_t columns = Virtual<int32_t>(work, 0x34);
                    const int32_t rows = Virtual<int32_t>(work, 0x30);
                    void* const cells = Virtual<void*>(work, 0x60);
                    reinterpret_cast<SpreadFn>(addr::SC4SimGrid_SpreadAround)(cells, rows, columns, x, z, radius, value,
                                                                              edge);
                    spread = true;
                }
            }
        }
        At<ListNode*>(self, local::kCursor) = At<ListNode*>(self, local::kCursor)->next;
        if (spread)
            break;
    }
    if (At<ListNode*>(self, local::kCursor) == At<ListNode*>(self, local::kOccupants))
        At<uint32_t>(self, local::kMapState) = 16;
}

// State 17: the copy +0x114 of the work grid, and the growth list made of
// the cells +0x10C marks.
void ListCells(char* self)
{
    void* const fits = Virtual<void*>(At<void*>(self, local::kFits), 0x60);
    void* const copy = Virtual<void*>(At<void*>(self, local::kWorkCopy), 0x60);
    void* const work = Work(self);
    void* const rows = Virtual<void*>(work, 0x60);
    const int32_t count = Virtual<int32_t>(work, 0x30);
    const int32_t columns = Virtual<int32_t>(work, 0x34);
    CellVector& list = ListAt(self, local::kGrowthList);
    Clear(list);
    for (int32_t r = 0; r < count; ++r) {
        for (int32_t c = 0; c < columns; ++c) {
            Row<int16_t>(copy, r)[c] = Row<int16_t>(rows, r)[c];
            if (Row<uint8_t>(fits, r)[c]) {
                const Cell cell{r, c, 1};
                PushBack(list, &cell);
            }
        }
    }
    At<uint32_t>(self, local::kMapState) = 18;
    // The cell iterator restarted inline: at (0, 0), done unless its
    // rectangle holds (0, 0).
    char* const iterator = At<char*>(self, local::kCellIterator);
    At<int32_t>(iterator, 0x4) = 0;
    At<int32_t>(iterator, 0x8) = 0;
    At<uint8_t>(iterator, 0x0) = 0;
    if (At<int32_t>(iterator, 0x10) > 0 || At<int32_t>(iterator, 0x14) > 0 || At<int32_t>(iterator, 0x18) < 0 ||
        At<int32_t>(iterator, 0x1C) < 0)
        At<uint8_t>(iterator, 0x0) = 2;
}

// State 18: per cell of the iterator, +0x118 = +0x11C capacity times the
// work desirability / 100 clamped to [0, 1] (rounded by the float trick),
// and the development simulator's slot 35 object told (slot 7). Within the
// time budget; 19 when the iterator is done.
void WeighCapacity(char* self, uint32_t budget)
{
    using StartWithFn = void(__thiscall*)(Deadline* deadline, uint32_t budget, uint32_t* step);
    using DeadlineFn = uint8_t(__thiscall*)(Deadline* deadline);
    using NextFn = uint8_t(__thiscall*)(void* iterator, int32_t* x, int32_t* z);
    uint32_t& once = Global<uint32_t>(addr::cSC4GrowthDeveloper_LimitOnce);
    uint32_t* const step = &Global<uint32_t>(addr::cSC4GrowthDeveloper_LimitStep);
    if (!(once & 1)) {
        once |= 1;
        step[0] = 0x400;
        step[1] = 0;
    }
    Deadline deadline{};
    reinterpret_cast<StartWithFn>(addr::SC4Deadline_StartWith)(&deadline, budget, step);
    struct {
        char* self;
        int32_t x;
        int32_t z;
    } told{self, 0, 0};
    void* const listener = Virtual<void*>(At<void*>(self, local::kDevelopment), 0x8C);
    void* const capacity = Virtual<void*>(At<void*>(self, local::kCapacity), 0x60);
    void* const weighted = Virtual<void*>(At<void*>(self, local::kWeighted), 0x60);
    void* const work = Virtual<void*>(Work(self), 0x60);
    // Written by the iterator before use (the game's slots hold the budget
    // and garbage).
    int32_t x = 0;
    int32_t z = 0;
    for (;;) {
        deadline.count += deadline.step;
        if (!(deadline.count & 0x3FF) && reinterpret_cast<DeadlineFn>(addr::SC4Deadline_Expired)(&deadline)) {
            deadline.timedOut = 1;
            break;
        }
        if (deadline.timedOut)
            break;
        if (!reinterpret_cast<NextFn>(addr::SC4CellIterator_Next)(At<void*>(self, local::kCellIterator), &x, &z)) {
            At<uint32_t>(self, local::kMapState) = 19;
            break;
        }
        double factor = static_cast<double>(Row<int16_t>(work, x)[z]) * static_cast<double>(0.01f);
        if (factor < 0.0)
            factor = 0.0;
        else if (factor > 1.0)
            factor = 1.0;
        const float rounded =
            static_cast<float>(static_cast<double>(Row<uint16_t>(capacity, x)[z]) * factor + 12582912.0);
        Row<uint16_t>(weighted, x)[z] = static_cast<uint16_t>(Bits(rounded));
        told.x = x;
        told.z = z;
        Virtual<uint32_t>(listener, 0x1C, &told);
    }
    reinterpret_cast<SelfFn>(addr::SC4Deadline_End)(&deadline);
}

// State 19: the growth list's capacity weighted by how far each cell is
// below the desirability threshold (/ 100, at most 1); a change from the
// last told is added to the demand.
void TellCapacity(char* self)
{
    int32_t sum = 0;
    if (At<uint8_t>(self, local::kSumCapacity)) {
        void* const work = Virtual<void*>(Work(self), 0x60);
        void* const weighted = Virtual<void*>(At<void*>(self, local::kWeighted), 0x60);
        const CellVector& list = ListAt(self, local::kGrowthList);
        for (const Cell* cell = list.begin; cell != list.end; ++cell) {
            const int32_t below =
                At<int32_t>(self, local::kDesirabilityThreshold) - Row<int16_t>(work, cell->x)[cell->z];
            if (below <= 0)
                continue;
            double factor = static_cast<double>(below) * static_cast<double>(0.01f);
            if (factor > 1.0)
                factor = 1.0;
            sum += Ftol(static_cast<double>(Row<int16_t>(weighted, cell->x)[cell->z]) * factor);
        }
    }
    const int32_t change = sum - At<int32_t>(self, local::kToldCapacity);
    if (change) {
        Virtual<uint32_t>(At<void*>(self, local::kDemand), 0x44, static_cast<float>(change));
        At<int32_t>(self, local::kToldCapacity) = sum;
    }
    At<uint32_t>(self, local::kMapState) = 20;
}

// One reading of 0x00B43D74's slot 6 (id, flags) object (slot 8, ST0).
double Statistic(uint32_t id, uint32_t flags)
{
    void* const object = Virtual<void*>(Service(addr::SC4_Service43D74), 0x18, id, flags);
    return Virtual<double>(object, 0x20);
}

// The month-end service's lists of slots 21 and 22: their sizes, in that
// order, the lists alive until the caller is done (destroyed second first).
struct MonthLists {
    void* first = nullptr;
    void* second = nullptr;
    int32_t firstSize = 0;
    int32_t secondSize = 0;
};

int32_t ListSize(void* list)
{
    ListNode* const head = *static_cast<ListNode**>(list);
    int32_t count = 0;
    for (ListNode* node = head->next; node != head; node = node->next)
        ++count;
    return count;
}

void FillMonthLists(MonthLists& lists)
{
    reinterpret_cast<SelfFn>(addr::SC4List_Construct4)(&lists.first);
    Virtual<uint32_t>(Service(addr::SC4_MonthEndService), 0x54, &lists.first, 0u);
    lists.firstSize = ListSize(&lists.first);
    reinterpret_cast<SelfFn>(addr::SC4List_Construct4)(&lists.second);
    Virtual<uint32_t>(Service(addr::SC4_MonthEndService), 0x58, &lists.second, 0u);
    lists.secondSize = ListSize(&lists.second);
}

void DestroyMonthLists(MonthLists& lists)
{
    reinterpret_cast<SelfFn>(addr::SC4List_Destroy4)(&lists.second);
    reinterpret_cast<SelfFn>(addr::SC4List_Destroy4)(&lists.first);
}

// State 20's target by zone kind (+0xD0) before the development
// simulator's scale.
float GrowthTarget(char* self)
{
    switch (At<uint32_t>(self, local::kZoneKind)) {
    case 1: {
        MonthLists lists;
        FillMonthLists(lists);
        const float base = static_cast<float>(Statistic(0x3001, 0));
        float share = 0.0f;
        if (lists.secondSize > 0)
            share = static_cast<float>(Statistic(0x3301, 0x10000) * (static_cast<double>(lists.firstSize) /
                                                                       static_cast<double>(lists.secondSize)));
        const double other = Statistic(0x1001, 0);
        const float otherRounded = static_cast<float>(other);
        const float mixed = static_cast<float>(static_cast<double>(share) * 0.5 + static_cast<double>(base));
        const float result = other < static_cast<double>(mixed) ? mixed : otherRounded;
        DestroyMonthLists(lists);
        return result;
    }
    case 2: {
        MonthLists lists;
        FillMonthLists(lists);
        const float base = static_cast<float>(Statistic(0x3001, 0));
        double share = 0.0;
        if (lists.secondSize > 0)
            share = Statistic(0x3301, 0x10000) *
                    (static_cast<double>(lists.firstSize) / static_cast<double>(lists.secondSize));
        const float result = static_cast<float>(share * 0.5 + static_cast<double>(base));
        DestroyMonthLists(lists);
        return result;
    }
    case 3:
        return static_cast<float>(Statistic(0x4001, 0x20000));
    default:
        return 0.0f;
    }
}

// State 20: the growth list sorted by desirability; the decline list made
// from its cells below +0x98 (last first; shuffled for purposes 5-8); the
// growth list's cells below +0x94 removed; each left cell given the index
// of the curve (+0xA0, from the last back) its rank reaches; state 0.
void FinishLists(char* self)
{
    void* const work = Work(self);
    CellVector& growth = ListAt(self, local::kGrowthList);
    CellVector& decline = ListAt(self, local::kDeclineList);
    Global<void*>(addr::cSC4GrowthDeveloper_SortRows) = Virtual<void*>(work, 0x60);
    Sort(growth.begin, growth.end, MoreDesirable);
    Clear(decline);
    for (Cell* cell = growth.end; cell != growth.begin;) {
        --cell;
        const auto value = static_cast<int16_t>(Virtual<uint32_t>(work, 0x58, cell->x, cell->z));
        if (value < At<int32_t>(self, local::kDeclineThreshold))
            PushBack(decline, cell);
    }
    const int32_t purpose = At<int32_t>(self, local::kPurpose);
    if (purpose >= 5 && purpose <= 8) {
        using AdvanceFn = uint64_t(__fastcall*)(void* random);
        Global<uint32_t>(addr::cSC4GrowthDeveloper_ShuffleSeed) = static_cast<uint32_t>(
            reinterpret_cast<AdvanceFn>(addr::cRZRandom_Advance)(&Global<uint8_t>(addr::cSC4GrowthDeveloper_ShuffleRandom)));
        Sort(decline.begin, decline.end, Shuffled);
    }
    Cell* cell = growth.begin;
    while (cell != growth.end) {
        const auto value = static_cast<int16_t>(Virtual<uint32_t>(work, 0x58, cell->x, cell->z));
        if (value < At<int32_t>(self, local::kDesirabilityThreshold)) {
            // erase (copy 0x006143E0 inline)
            for (Cell* from = cell + 1; from != growth.end; ++from)
                from[-1] = *from;
            --growth.end;
        } else {
            ++cell;
        }
    }

    const float target = GrowthTarget(self);
    const double scale = Virtual<double>(At<void*>(self, local::kDevelopment), 0xD4);
    const float total = static_cast<float>(scale * static_cast<double>(target));
    const auto count = static_cast<uint32_t>(growth.end - growth.begin);
    const auto countRounded = static_cast<float>(static_cast<double>(count));
    char* const curves = At<char*>(self, local::kCurves);
    int32_t index = static_cast<int32_t>(At<char*>(self, local::kCurves + 4) - curves) >> 4;
    if (index > 0) {
        float reached = static_cast<float>(Evaluate(At<char*>(self, local::kCurves) + (index - 1) * 16, total));
        if (growth.begin != growth.end) {
            const auto step = static_cast<float>(1.0 / static_cast<double>(countRounded));
            int32_t rank = 0;
            for (Cell* listed = growth.begin; listed != growth.end; ++listed) {
                const double share = static_cast<double>(rank) * static_cast<double>(step);
                const auto shareRounded = static_cast<float>(share);
                if (!(share < static_cast<double>(reached))) {
                    while (index > 1) {
                        --index;
                        reached = static_cast<float>(
                            Evaluate(At<char*>(self, local::kCurves) + (index - 1) * 16, total) +
                            static_cast<double>(reached));
                        if (shareRounded < reached)
                            break;
                    }
                }
                listed->value = index;
                rank += 100;
            }
        }
    }
    At<uint32_t>(self, local::kMapState) = 0;
}

// 0x006B5F20: one step of the map update (budget: the time limit of state
// 18 in ms). Under a stopwatch the game never reads.
void UpdateMap(char* self, uint32_t budget)
{
    using StopwatchCtorFn = void*(__thiscall*)(void* stopwatch, uint32_t mode);
    alignas(8) uint8_t stopwatch[0x18];
    reinterpret_cast<StopwatchCtorFn>(addr::SC4Stopwatch_Construct)(stopwatch, 4u);
    reinterpret_cast<SelfFn>(addr::SC4Stopwatch_Start)(stopwatch);
    uint32_t& state = At<uint32_t>(self, local::kMapState);
    switch (state) {
    case 1:
        if (At<uint8_t>(self, local::kInitialized)) {
            At<uint32_t>(self, local::kStartDay) = Virtual<uint32_t>(Service(addr::SC4_SimulatorService), 0x20);
            state = 2;
        }
        break;
    case 2:
        FillBase(self);
        state = 3;
        break;
    case 3:
        if (Curve(self, local::kCurve48)) {
            void* const desirability = Service(addr::SC4_Service43D88);
            void* const grid = At<uint8_t>(self, local::kUseDesirability2) ? Virtual<void*>(desirability, 0x1C)
                                                                             : Virtual<void*>(desirability, 0x20);
            MapCurve(self, local::kCurve48, MapUint8, grid, AddCurveOfUint8);
        }
        state = 4;
        break;
    case 4:
        if (Curve(self, local::kCurve4C))
            MapFloats(Work(self), Virtual<void*>(Service(addr::SC4_Service43D88), 0x24), AddCurveOfFloat,
                      Curve(self, local::kCurve4C));
        state = 5;
        break;
    case 5: {
        void* const service = Service(addr::SC4_Service43DA4);
        if (Curve(self, local::kCurve60))
            MapCurve(self, local::kCurve60, MapUint8, Virtual<void*>(service, 0x14, 1u), AddCurveOfUint8);
        if (Curve(self, local::kCurve64))
            MapCurve(self, local::kCurve64, MapUint8, Virtual<void*>(service, 0x14, 2u), AddCurveOfUint8);
        if (Curve(self, local::kCurve68))
            MapCurve(self, local::kCurve68, MapUint8, Virtual<void*>(service, 0x14, 3u), AddCurveOfUint8);
        state = 6;
        break;
    }
    case 6: {
        void* const service = Service(addr::SC4_Service43D6C);
        if (Curve(self, local::kCurve6C))
            MapCurve(self, local::kCurve6C, MapUint8, Virtual<void*>(service, 0x14, 0x3320u), AddCurveOfUint8);
        if (Curve(self, local::kCurve70))
            MapCurve(self, local::kCurve70, MapUint8, Virtual<void*>(service, 0x14, 0x3330u), AddCurveOfUint8);
        state = 7;
        break;
    }
    case 7:
        if (Curve(self, local::kCurve5C))
            MapCurve(self, local::kCurve5C, MapSint16, Virtual<void*>(Service(addr::SC4_Service43D60), 0x38),
                     AddCurveOfSint16);
        state = 8;
        break;
    case 8: {
        void* const pollution = Service(addr::SC4_PollutionSimulator);
        for (uint32_t i = 0; i < 4; ++i) {
            if (!At<void**>(self, local::kPollutionCurves)[i])
                continue;
            void* const grid = Virtual<void*>(pollution, 0x14, i);
            MapSint16(Work(self), grid, AddCurveOfSint16, At<void**>(self, local::kPollutionCurves)[i]);
        }
        state = 9;
        break;
    }
    case 9:
        if (Curve(self, local::kCurve74))
            MapFloats(Work(self), Virtual<void*>(Service(addr::SC4_Service43DA4), 0x9C), AddCurveOfFloat,
                      Curve(self, local::kCurve74));
        state = 10;
        break;
    case 10:
        if (Curve(self, local::kCurve78))
            MapFloats(Work(self), Virtual<void*>(Service(addr::SC4_Service43DA4), 0xA0), AddCurveOfFloat,
                      Curve(self, local::kCurve78));
        state = 11;
        break;
    case 11:
        if (Curve(self, local::kCurve7C))
            MapCurve(self, local::kCurve7C, MapUint8, Virtual<void*>(Service(addr::SC4_Service43D70), 0x14),
                     AddCurveOfUint8);
        state = 12;
        break;
    case 12:
        if (Curve(self, local::kCurve80))
            AddLowestCover(self);
        state = 13;
        break;
    case 13:
        if (Curve(self, local::kCurve84))
            MapCurve(self, local::kCurve84, MapUint8, Virtual<void*>(Service(addr::SC4_Service43DA8), 0x24),
                     AddCurveOfUint8);
        state = 14;
        break;
    case 14:
        if (Curve(self, local::kCurve88))
            MapCurve(self, local::kCurve88, MapSint16, Virtual<void*>(Service(addr::SC4_Service43D60), 0x3C),
                     AddCurveOfSint16);
        state = 15;
        CopyOccupants(self);
        break;
    case 15:
        SpreadOccupants(self);
        break;
    case 16:
        if (Curve(self, local::kCurve8C))
            MapCurve(self, local::kCurve8C, MapSint8, Virtual<void*>(Service(addr::SC4_Service43D60), 0x34),
                     AddCurveOfSint8);
        state = 17;
        break;
    case 17:
        ListCells(self);
        break;
    case 18:
        WeighCapacity(self, budget);
        break;
    case 19:
        TellCapacity(self);
        break;
    case 20:
        FinishLists(self);
        break;
    default:
        break;
    }
    reinterpret_cast<SelfFn>(addr::SC4Stopwatch_Destruct)(stopwatch);
}

// --- Message 0x07699455: cells changed (0x006B4B90) ------------------------------
//
// The rectangle (x1, z1, x2, z2 in city cells) in +0x120's cells (bounded
// above by its size): per cell, the city cells it covers whose zone type
// (the zone manager's slot 12) fits the construction type
// (SC4ConstructionType::FitsZone) are counted into +0x10C and their values
// (the development simulator's slot 45) summed into +0x120 (slot 26 each;
// the sum rounded by the float trick).
void UpdateRectangle(char* self, const int32_t* rect)
{
    void* const sums = At<void*>(self, local::kSums);
    const int32_t shift = Virtual<int32_t>(sums, 0x2C);
    const int32_t x1 = rect[0] >> shift;
    const int32_t z1 = rect[1] >> shift;
    int32_t x2 = rect[2] >> shift;
    int32_t z2 = rect[3] >> shift;
    if (x2 > Virtual<int32_t>(sums, 0x30) - 1)
        x2 = Virtual<int32_t>(sums, 0x30) - 1;
    if (z2 > Virtual<int32_t>(sums, 0x34) - 1)
        z2 = Virtual<int32_t>(sums, 0x34) - 1;
    uint32_t info[5] = {}; // GetConstructionInfo writes 0x12 bytes
    Virtual<uint32_t>(self, 0x24, info);
    if (x2 < x1)
        x2 = x1;
    if (z2 < z1)
        z2 = z1;
    using FitsFn = uint8_t(__cdecl*)(uint32_t type, int32_t zone);
    for (int32_t z = z1; z <= z2; ++z) {
        for (int32_t x = x1; x <= x2; ++x) {
            const int32_t cellX1 = x << shift;
            const int32_t cellZ1 = z << shift;
            int32_t cellX2 = ((x + 1) << shift) - 1;
            int32_t cellZ2 = ((z + 1) << shift) - 1;
            if (cellX2 < cellX1)
                cellX2 = cellX1;
            if (cellZ2 < cellZ1)
                cellZ2 = cellZ1;
            float sum = 0.0f;
            uint32_t count = 0;
            for (int32_t cellZ = cellZ1; cellZ <= cellZ2; ++cellZ) {
                for (int32_t cellX = cellX1; cellX <= cellX2; ++cellX) {
                    const auto zone = static_cast<int32_t>(
                        static_cast<int8_t>(Virtual<uint8_t>(At<void*>(self, local::kZones), 0x30, cellX, cellZ)));
                    if (!reinterpret_cast<FitsFn>(addr::SC4ConstructionType_FitsZone)(info[0], zone))
                        continue;
                    ++count;
                    const double value = Virtual<double>(At<void*>(self, local::kDevelopment), 0xB4, zone,
                                                         At<uint32_t>(self, local::kPurpose),
                                                         At<uint32_t>(self, local::kWealth));
                    sum = static_cast<float>(value + static_cast<double>(sum));
                }
            }
            Virtual<uint32_t>(At<void*>(self, local::kFits), 0x68, x, z, count);
            const float rounded = static_cast<float>(static_cast<double>(sum) + 12582912.0);
            Virtual<uint32_t>(sums, 0x68, x, z, Bits(rounded));
        }
    }
}

// --- Message 0xE8BDA2E7: an occupant's stage changed (0x006B51C0) ------------------

// A capacity cell changed by `change`; below zero it is cleared and the
// occupant's slot 9 object told (its slot 3).
void ChangeCapacity(void* occupant, uint16_t* cell, int32_t change)
{
    if (static_cast<int32_t>(*cell) + change < 0) {
        if (void* const object = Virtual<void*>(occupant, 0x24))
            Virtual<uint32_t>(object, 0xC);
        *cell = 0;
        return;
    }
    *cell = static_cast<uint16_t>(*cell + static_cast<uint16_t>(change));
}

// For an occupant of this developer's purpose (0x00B43D68 slot 39, asked
// with data 3, not the occupant: 0x006B51C3 pushes the second argument): its
// capacity for the subtype (slot 50) added (staged) or removed, spread
// evenly over the cells of its rectangle (slot 14) in +0x11C, the
// remainder on its anchor cell (slot 21); the total +0x108 changed.
// `data3` is the message's data 3: the game passes it in the stack slot
// slot 22 writes its first byte to.
void OccupantStaged(char* self, void* occupant, uint32_t data3, uint8_t staged)
{
    const auto* const purpose = Virtual<const uint32_t*>(Service(addr::SC4_Service43D68), 0x9C, data3);
    if (At<uint32_t>(self, local::kPurpose) != *purpose)
        return;
    const auto capacity =
        static_cast<int32_t>(static_cast<uint16_t>(Virtual<uint32_t>(occupant, 0xC8, At<uint32_t>(self, local::kSubType), 1u)));
    const int32_t change = staged ? capacity : -capacity;
    uint32_t width = data3;
    uint32_t depth = 0; // never written by the game when slot 22 does not
    Virtual<uint8_t>(occupant, 0x58, &width, &depth);
    const int32_t cells = static_cast<int32_t>(static_cast<uint8_t>(width)) * static_cast<int32_t>(static_cast<uint8_t>(depth));
    const int32_t each = change / cells;
    const int32_t rest = change - each * cells;
    int32_t rect[4] = {}; // unwritten if slot 14 fails: the game's stack
    Virtual<uint8_t>(occupant, 0x38, rect);
    void* const grid = At<void*>(self, local::kCapacity);
    void* const rows = Virtual<void*>(grid, 0x60);
    const int32_t shift = Virtual<int32_t>(grid, 0x2C);
    const int32_t x1 = rect[0];
    const int32_t z1 = rect[1];
    const int32_t x2 = rect[2] < x1 ? x1 : rect[2];
    const int32_t z2 = rect[3] < z1 ? z1 : rect[3];
    for (int32_t z = z1; z <= z2; ++z)
        for (int32_t x = x1; x <= x2; ++x)
            ChangeCapacity(occupant, Row<uint16_t>(rows, x >> shift) + (z >> shift), each);
    int32_t anchorX = 0; // unwritten if slot 21 fails: the game's stack
    int32_t anchorZ = 0;
    Virtual<uint8_t>(occupant, 0x54, &anchorX, &anchorZ);
    ChangeCapacity(occupant, Row<uint16_t>(rows, anchorX >> shift) + (anchorZ >> shift), rest);
    At<int32_t>(self, local::kTotalCapacity) += change;
}

// --- DoMessage --------------------------------------------------------------------

bool IsStaged(uint8_t state) { return state >= 2 && state < 4; }

// cIGZMessageTarget2 slot 3 (0x006B7030), on the part at +4.
uint8_t __fastcall DoMessage(void* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    char* const self = static_cast<char*>(part) - kMessagePart;
    switch (Virtual<uint32_t>(message, 0x10)) {
    case 0x66956814: {
        UpdateMap(self, 5);
        const uint32_t state = At<uint32_t>(self, local::kMapState);
        if (!state)
            return 1;
        const uint32_t elapsed =
            Virtual<uint32_t>(Service(addr::SC4_SimulatorService), 0x20) - At<uint32_t>(self, local::kStartDay);
        // The state (not a day count) against the days since the update
        // started, as the game compares them.
        return state < elapsed ? 0 : 1;
    }
    case 0x6695681A:
        UpdateMap(self, Virtual<uint32_t>(message, 0x4C));
        return 1;
    case 0x07699455:
        UpdateRectangle(self, Virtual<const int32_t*>(message, 0x3C));
        return 1;
    case 0xE8BDA2E7: {
        void* const occupant = Virtual<void*>(message, 0x80);
        const uint32_t before = Virtual<uint32_t>(message, 0x24);
        const uint32_t after = Virtual<uint32_t>(message, 0x38);
        const uint32_t data3 = Virtual<uint32_t>(message, 0x4C);
        if (Virtual<uint32_t>(message, 0x60) != At<uint32_t>(self, local::kWealth))
            return 1;
        const bool staged = IsStaged(static_cast<uint8_t>(after));
        if (staged == IsStaged(static_cast<uint8_t>(before)))
            return 1;
        if (!data3) {
            Virtual<uint32_t>(occupant, 0xB8, 1u);
            return 1;
        }
        OccupantStaged(self, occupant, data3, staged ? 1 : 0);
        return 1;
    }
    default:
        return 1;
    }
}

// --- Installation -----------------------------------------------------------------

template <typename Fn>
bool Install(std::uintptr_t address, Fn ours, Fn* original)
{
    const addr::Entry* entry = addr_check::Find(address);
    return entry && hook::Install(*entry, ours, original);
}

bool SwapSlot(std::uintptr_t vtableAddress, int index, std::uintptr_t original, void* ours)
{
    auto* const slot = reinterpret_cast<std::uintptr_t*>(vtableAddress) + index;
    if (*slot != original) {
        log::Write("growth messages: slot %d holds %08X, expected %08X; not installing", index,
                   static_cast<unsigned>(*slot), static_cast<unsigned>(original));
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
        return false;
    *slot = reinterpret_cast<std::uintptr_t>(ours);
    VirtualProtect(slot, sizeof(*slot), old, &old);
    return true;
}

using MapFn = void(__cdecl*)(void*, void*, IntCallback, void*);
MapFn g_originalMapUint16 = nullptr;

bool InstallMessages(registry::Mode)
{
    return game::IsBuild641() &&
           Install(addr::cSC4GrowthDeveloper_MapUint16Grid, &MapUint16, &g_originalMapUint16) &&
           SwapSlot(addr::cSC4GrowthDeveloper_vftable_4, 3, 0x006B7030, reinterpret_cast<void*>(&DoMessage));
}

const bool g_registered = registry::Register({
    "simulators.growth.messages",
    "simulators/growth",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4GrowthDeveloper_vftable_4,
    "Our growth developer DoMessage: the map update state machine (desirability curves, occupant effects, "
    "growth and decline lists), changed cells' capacity, occupant stage changes (docs/growth.md)",
    false,
    InstallMessages,
});

} // namespace
} // namespace opensc4::growth_sim
