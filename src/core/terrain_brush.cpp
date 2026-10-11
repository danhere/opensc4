// Our cSTETerrain shaping machinery (build 641), piece 3 of the city's
// terrain object (vtable 0x00AB3D18, the service 0x00B43CF4): slot 57
// (ApplyBrush) and the internal functions the two whole-map shapers of
// terrain_edit.cpp (CreateFlatAreas, CreateShoreCliffs) are built from:
//
//   0x00749660  FlattenAround      gathers an area of like altitudes, levels it
//   0x00743210  SpreadMarks        marks the surroundings of marked vertices
//   0x00740A20  FlaggedBounds      the bounding rectangle of marked vertices
//   0x00745020  SmoothRect         limits the slopes between neighbours
//   0x007409B0  ClearFlagsInRect   clears flags in a rectangle
//   0x00741DE0  FlagVerticesWhere  sets flags where a callback answers
//   0x00740F70  UpdateMinMaxHeight the lowest and highest altitude
//   0x0074AD20  ShoreCollect1      follows a shore line one way (push_back)
//   0x0074AB60  ShoreCollect2      and the other way (push_front)
//   0x00749FA0  ShoreApply         cuts the line into runs and shapes them
//   0x00741680  WaterNeighbour     (only ShoreApply calls it)
//   0x00744220  ShoreCliffRun      (only ShoreApply calls it; the random draws)
//   0x00740530, 0x00740550, 0x00740570, 0x007405A0, 0x00741B80: the five
//   callbacks (thiscall, one index, AL) the shapers hand to the walkers.
//
// All from the disassembly, statement by statement. What is NOT rewritten
// and stays the game's: the body of ApplyBrush (0x0074D730 and everything
// under it: the brush walkers 0x00742180 / 0x00742440 with FSIN / FCOS, the
// per-vertex routines of the eleven layer kinds, the erosion step
// 0x007479B0 -> 0x00746AD0, 0x00748040, 0x0074B5E0), erosion (0x00747B20,
// 0x00746AD0, 0x00746F40), the flood (slot 56, 0x00747720 -> 0x007467F0),
// the brush loader, and DoMessage (0x0074A470). ApplyBrush here is the
// slot's own code around the call of 0x0074D730.
//
// How the group reaches every caller. Slot 57 is swapped in the vtable. The
// internal functions are called directly by game code (the originals of
// slots 43 and 44 when simulators.terrain.edit is off, slot 68, slot 90,
// 0x007428E0, 0x00743B60, 0x00748130, 0x00748C90, 0x00749AF0, 0x0074B570)
// and at their game addresses by terrain_edit.cpp, so each gets a detour at
// its entry. Two of them need a thunk that keeps ECX, because callers go on
// using it as the terrain after the call (the originals never write ECX):
// 0x007409B0 (0x0074215E reads the vtable through it, 0x00742908 calls
// 0x00741DE0 with it) and 0x00740A20 (0x0074C315: 0x00745020 is called next
// without loading ECX). The others are plain thiscall at every call site
// (the sites are listed at the installation).
//
// Floating point. As in terrain_queries.cpp and terrain_edit.cpp: what the
// game keeps on the x87 stack (53-bit precision) is a double here, and a
// float exactly where the game stores one. Clamps compare the value still on
// the stack. No transcendental instruction is in the functions rewritten
// here. Constants are read from the game's memory at each use.
//
// Random numbers. Only ShoreCliffRun draws: from the terrain's generator
// (0x00B4BC78), one uniform draw per run, then per record one Gaussian draw
// and five uniform draws, through the game's functions and in its order.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/terrain_sim.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <iterator>

namespace opensc4::terrain_sim {

// --- Additions for terrain_sim.h (to be merged there) --------------------------

namespace field {
// int32_t[8]: what to add to a vertex index to reach each of its eight
// neighbours, from the west clockwise through the north (even entries are
// the straight ones). Read by 0x00741B80 and 0x00745020.
constexpr std::size_t kNeighbourOffsets = 0x84;
} // namespace field

namespace flag {
constexpr uint16_t kShoreFlag = 0x0010; // as flag::kShore of terrain_edit.cpp
} // namespace flag

namespace slot {
constexpr std::size_t kSetAltitudeIndex = 0xC8; // slot 50 (index, altitude)
constexpr std::size_t kRedisplay = 0x110;       // slot 68 (bool, bool, rect, uint32_t)
} // namespace slot

// tBrushParameters (0x38 bytes; gzcom-dll cISTETerrain.h) as slot 57 reads it.
namespace parameters {
constexpr std::size_t kRedisplay = 0x14;   // uint8: redisplay the applied rectangle afterwards
constexpr std::size_t kMessageData = 0x20; // dword: the last argument of slot 68
constexpr std::size_t kApplied = 0x24;     // int32[4]: the rectangle of vertices the brush touched
} // namespace parameters

// A node of the brush map (addr::cSTETerrain_Brushes): the record starts
// behind the key.
namespace brush_node {
constexpr std::size_t kRecord = 0x14;
} // namespace brush_node

namespace {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("terrain: first %s", name);
}

template <typename R, typename... Args>
inline R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
inline R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

inline float F(uint32_t address) { return AtAddress<float>(address); }

inline uint32_t U32(void* self, std::size_t offset) { return At<uint32_t>(self, offset); }

inline float Sea(char* self) { return At<float>(self, field::kSeaLevel); }

// A record's offset in the vertex and water arrays (index * 9 * 4, 32-bit,
// wrapping). Nothing here tests an index's range unless the game does.
inline uint32_t Offset(uint32_t index) { return index * 9u * 4u; }

inline uint16_t& Flags(uint32_t record) { return AtAddress<uint16_t>(record + vertex::kFlags); }

inline bool Locked(uint32_t record) { return (AtAddress<uint8_t>(record + 0x23) & 0x40) != 0; }

inline int32_t Table(std::uintptr_t table, uint32_t entry)
{
    return AtAddress<int32_t>(static_cast<uint32_t>(table) + 4u * entry);
}

// The callbacks' type: thiscall on (the terrain + an adjustment), one vertex
// index; the walkers test AL.
using Predicate = uint32_t(__thiscall*)(void*, uint32_t);

inline bool Ask(uint32_t function, char* self, uint32_t adjust, uint32_t index)
{
    return (reinterpret_cast<Predicate>(static_cast<std::uintptr_t>(function))(self + adjust, index) & 0xFFu) != 0;
}

// --- The callbacks -----------------------------------------------------------

// 0x00740530: the mark 1 of a vertex.
uint32_t __fastcall IsMarked1(char* self, void*, uint32_t index)
{
    return AtAddress<uint8_t>(VertexBase(self) + Offset(index) + vertex::kFlags) & 1u;
}

// 0x00740550: the mark 4.
uint32_t __fastcall IsMarked4(char* self, void*, uint32_t index)
{
    return (AtAddress<uint8_t>(VertexBase(self) + Offset(index) + vertex::kFlags) >> 2) & 1u;
}

// 0x00740570: without the mark 1 and above the sea level (not a NaN).
uint32_t __fastcall IsLandUnmarked1(char* self, void*, uint32_t index)
{
    const uint32_t record = VertexBase(self) + Offset(index);
    if (AtAddress<uint8_t>(record + vertex::kFlags) & 1u)
        return 0;
    return F(record) > Sea(self) ? 1u : 0u;
}

// 0x007405A0: the same for the mark 4.
uint32_t __fastcall IsLandUnmarked4(char* self, void*, uint32_t index)
{
    const uint32_t record = VertexBase(self) + Offset(index);
    if (AtAddress<uint8_t>(record + vertex::kFlags) & 4u)
        return 0;
    return F(record) > Sea(self) ? 1u : 0u;
}

// The neighbours a vertex at (x, z) has inside the grid, one bit each in the
// order of the +0x84 table (inlined in 0x00741B80 and 0x00745020): x and z
// below 1 are tested signed, the far edges unsigned against the cell counts.
inline uint8_t NeighbourBits(char* self, uint32_t x, uint32_t z)
{
    uint8_t bits = 0xFF;
    if (static_cast<int32_t>(x) < 1)
        bits = 0x7C;
    if (x >= U32(self, field::kCellsX))
        bits &= 0xC7;
    if (static_cast<int32_t>(z) < 1)
        bits &= 0xF1;
    if (z >= U32(self, field::kCellsZ))
        bits &= 0x1F;
    return bits;
}

// 0x00741B80: a vertex above the sea level (or a NaN: the game's test lets
// it through) with one of its four straight neighbours below it.
uint32_t __fastcall IsShoreVertex(char* self, void*, uint32_t index)
{
    const uint32_t base = VertexBase(self);
    const float altitude = F(base + Offset(index));
    const float sea = Sea(self);
    if (altitude < sea || altitude == sea)
        return 0;
    const uint32_t stride = U32(self, field::kVertsX);
    const uint8_t bits = NeighbourBits(self, index % stride, index / stride);
    for (uint32_t k = 0; k < 8; k += 2) {
        if (!(bits & (1u << k)))
            continue;
        const uint32_t other = static_cast<uint32_t>(At<int32_t>(self, field::kNeighbourOffsets + 4 * k)) + index;
        if (F(base + Offset(other)) < Sea(self))
            return 1;
    }
    return 0;
}

// --- The small walkers -------------------------------------------------------

// 0x007409B0 (rect, mask): clears the mask's bits in the flags word of every
// vertex of a rectangle (inclusive, signed; the far column is read again
// after each vertex, the far row after each row); no range test. The game's
// leaves ECX alone and callers count on it: reached through the thunk below.
void __fastcall ClearFlagsInRect(char* self, void*, const int32_t* rect, uint32_t mask)
{
    static bool logged = false;
    FirstCall("ClearFlagsInRect", logged);
    const uint16_t keep = static_cast<uint16_t>(~mask);
    int32_t z = rect[1];
    if (z > rect[3])
        return;
    int32_t last = rect[2];
    do {
        int32_t x = rect[0];
        uint32_t offset = Offset(U32(self, field::kVertsX) * static_cast<uint32_t>(z) + static_cast<uint32_t>(x));
        if (x <= last) {
            do {
                Flags(VertexBase(self) + offset) &= keep;
                last = rect[2];
                ++x;
                offset += vertex::kSize;
            } while (x <= last);
        }
        ++z;
    } while (z <= rect[3]);
}

__declspec(naked) void ClearFlagsInRectThunk()
{
    __asm push ecx
    __asm push dword ptr [esp + 12]
    __asm push dword ptr [esp + 12]
    __asm call ClearFlagsInRect
    __asm pop ecx
    __asm ret 8
}

// 0x00740A20 (rect, mask): the bounding rectangle (vertex columns and rows)
// of the vertices with any of the mask's flags; the empty rectangle
// (0x7FFFFFFF twice, 0x80000000 twice) for none. The game's leaves ECX alone
// and 0x0074C315 counts on it: reached through the thunk below.
void __fastcall FlaggedBounds(char* self, void*, int32_t* rect, uint32_t mask)
{
    static bool logged = false;
    FirstCall("FlaggedBounds", logged);
    rect[1] = 0x7FFFFFFF;
    rect[0] = 0x7FFFFFFF;
    rect[3] = INT32_MIN;
    rect[2] = INT32_MIN;
    if (U32(self, field::kVertsZ) == 0)
        return;
    int32_t row = 0;
    do {
        const uint32_t stride = U32(self, field::kVertsX);
        uint32_t offset = Offset(stride * static_cast<uint32_t>(row));
        int32_t column = 0;
        if (stride != 0) {
            do {
                if ((Flags(VertexBase(self) + offset) & mask & 0xFFFFu) != 0) {
                    if (column < rect[0])
                        rect[0] = column;
                    if (column > rect[2])
                        rect[2] = column;
                    if (row < rect[1])
                        rect[1] = row;
                    if (row > rect[3])
                        rect[3] = row;
                }
                ++column;
                offset += vertex::kSize;
            } while (static_cast<uint32_t>(column) < U32(self, field::kVertsX));
        }
        ++row;
    } while (static_cast<uint32_t>(row) < U32(self, field::kVertsZ));
}

__declspec(naked) void FlaggedBoundsThunk()
{
    __asm push ecx
    __asm push dword ptr [esp + 12]
    __asm push dword ptr [esp + 12]
    __asm call FlaggedBounds
    __asm pop ecx
    __asm ret 8
}

// 0x00740F70: the lowest (+0x70) and highest (+0x74) altitude of all
// vertices; nothing without the array. A vertex below the lowest so far
// replaces it; else one above the highest so far replaces that (a NaN
// neither). The array's address is taken once, the count read each time.
void __fastcall UpdateMinMaxHeight(char* self, void*)
{
    static bool logged = false;
    FirstCall("UpdateMinMaxHeight", logged);
    const uint32_t base = VertexBase(self);
    if (!base)
        return;
    At<float>(self, field::kMinHeight) = F(base);
    At<float>(self, field::kMaxHeight) = F(base);
    uint32_t record = base + vertex::kSize;
    for (uint32_t index = 1; index < U32(self, field::kVertexCount); ++index, record += vertex::kSize) {
        const float altitude = F(record);
        if (altitude < At<float>(self, field::kMinHeight))
            At<float>(self, field::kMinHeight) = altitude;
        else if (altitude > At<float>(self, field::kMaxHeight))
            At<float>(self, field::kMaxHeight) = altitude;
    }
}

// 0x00741DE0 (callback, adjustment, mask): sets the mask's flags on every
// vertex the callback answers for, in index order.
void __fastcall FlagVerticesWhere(char* self, void*, uint32_t callback, uint32_t adjust, uint32_t mask)
{
    static bool logged = false;
    FirstCall("FlagVerticesWhere", logged);
    if (U32(self, field::kVertexCount) == 0)
        return;
    uint32_t index = 0;
    uint32_t offset = 0;
    do {
        if (Ask(callback, self, adjust, index))
            Flags(VertexBase(self) + offset) |= static_cast<uint16_t>(mask);
        ++index;
        offset += vertex::kSize;
    } while (index < U32(self, field::kVertexCount));
}

// 0x00743210 (distance, first callback, its adjustment, second callback, its
// adjustment, mask): for every vertex the first callback answers for (rows
// first), every vertex of the square of that distance around it (cut to the
// grid: columns 0 to cells x, rows 0 to cells z) that the second callback
// answers for gets the mask's flags. The flags set on the way are seen by
// the callbacks of later vertices, as in the game.
void __fastcall SpreadMarks(char* self, void*, int32_t distance, uint32_t first, uint32_t firstAdjust, uint32_t second,
                            uint32_t secondAdjust, uint32_t mask)
{
    static bool logged = false;
    FirstCall("SpreadMarks", logged);
    if (U32(self, field::kVertsZ) == 0)
        return;
    int32_t row = 0;
    int32_t rowLow = -distance;
    do {
        const uint32_t stride = U32(self, field::kVertsX);
        uint32_t index = stride * static_cast<uint32_t>(row);
        if (stride != 0) {
            int32_t columnLow = -distance;
            int32_t columnHigh = distance;
            do {
                if (Ask(first, self, firstAdjust, index)) {
                    int32_t z0 = rowLow;
                    int32_t x0 = columnLow;
                    int32_t x1 = columnHigh;
                    int32_t z1 = distance + row;
                    if (z0 < 0)
                        z0 = 0;
                    if (x0 < 0)
                        x0 = 0;
                    if (x1 > static_cast<int32_t>(U32(self, field::kCellsX)))
                        x1 = static_cast<int32_t>(U32(self, field::kCellsX));
                    if (z1 > static_cast<int32_t>(U32(self, field::kCellsZ)))
                        z1 = static_cast<int32_t>(U32(self, field::kCellsZ));
                    for (int32_t z = z0; z <= z1; ++z) {
                        uint32_t other = U32(self, field::kVertsX) * static_cast<uint32_t>(z) + static_cast<uint32_t>(x0);
                        if (x0 > x1)
                            continue;
                        uint32_t offset = Offset(other);
                        // x1 - x0 + 1 vertices (the count is taken before the walk).
                        uint32_t left = static_cast<uint32_t>(x1 - x0) + 1u;
                        do {
                            if (Ask(second, self, secondAdjust, other))
                                Flags(VertexBase(self) + offset) |= static_cast<uint16_t>(mask);
                            ++other;
                            offset += vertex::kSize;
                        } while (--left != 0);
                    }
                }
                ++columnLow;
                ++columnHigh;
                ++index;
            } while (static_cast<uint32_t>(columnLow + distance) < U32(self, field::kVertsX));
        }
        ++row;
        ++rowLow;
    } while (static_cast<uint32_t>(row) < U32(self, field::kVertsZ));
}

// --- Smoothing ---------------------------------------------------------------

// The water record of a vertex gets sea level - altitude (also a negative
// one), as after every altitude change here.
inline void WaterFromSea(char* self, uint32_t wet, uint32_t record)
{
    AtAddress<float>(wet) = static_cast<float>(static_cast<double>(Sea(self)) - static_cast<double>(F(record)));
}

// A vertex lowered by `amount`, not below the minimum allowed height (the
// clamp tests the difference before it is rounded); nothing when locked.
inline void SmoothDown(char* self, uint32_t base, uint32_t offset, float amount)
{
    const uint32_t record = base + offset;
    if (Locked(record))
        return;
    const double lowered = static_cast<double>(F(record)) - static_cast<double>(amount);
    const uint32_t wet = WaterBase(self) + offset;
    AtAddress<float>(record) = static_cast<float>(lowered);
    if (lowered < static_cast<double>(Global<float>(addr::cSTETerrain_MinAllowedHeightValue)))
        AtAddress<float>(record) = Global<float>(addr::cSTETerrain_MinAllowedHeightValue);
    WaterFromSea(self, wet, record);
}

// A vertex raised by `amount`, not above the maximum allowed height.
inline void SmoothUp(char* self, uint32_t base, uint32_t offset, float amount)
{
    const uint32_t record = base + offset;
    if (Locked(record))
        return;
    const double raised = static_cast<double>(amount) + static_cast<double>(F(record));
    const uint32_t wet = WaterBase(self) + offset;
    AtAddress<float>(record) = static_cast<float>(raised);
    if (raised > static_cast<double>(Global<float>(addr::cSTETerrain_MaxAllowedHeightValue)))
        AtAddress<uint32_t>(record) = Global<uint32_t>(addr::cSTETerrain_MaxAllowedHeightValue);
    WaterFromSea(self, wet, record);
}

// 0x00745020 (rect, passes, mask): `passes` times over the rectangle of
// vertices (rows first; the far ends are read again each time): each vertex
// against each of its neighbours inside the grid (the +0x84 table's order),
// when either of the two has a flag of the mask. Where the vertex is higher
// than the neighbour by more than the limit (0x00B4BCF4 for a straight
// neighbour, 0x00B4BCF0 for a diagonal one), the excess times the factor
// 0x00B4B40C (rounded to float) is taken from the vertex if IT is flagged
// and added to the neighbour if THAT is flagged; where it is lower by more
// than the limit, the other way round. Locked vertices stay; the clamps and
// the water depths as everywhere. The changed altitudes are used at once.
void __fastcall SmoothRect(char* self, void*, const int32_t* rect, int32_t passes, uint32_t mask)
{
    static bool logged = false;
    FirstCall("SmoothRect", logged);
    if (passes <= 0)
        return;
    do {
        int32_t z = rect[1];
        if (z <= rect[3]) {
            do {
                int32_t x = rect[0];
                uint32_t index = U32(self, field::kVertsX) * static_cast<uint32_t>(z) + static_cast<uint32_t>(x);
                if (x <= rect[2]) {
                    uint32_t offset = Offset(index);
                    do {
                        const bool here = (Flags(VertexBase(self) + offset) & mask & 0xFFFFu) != 0;
                        const uint8_t bits = NeighbourBits(self, static_cast<uint32_t>(x), static_cast<uint32_t>(z));
                        for (uint32_t k = 0; k < 8; ++k) {
                            if (!(bits & (1u << k)))
                                continue;
                            const uint32_t other =
                                static_cast<uint32_t>(At<int32_t>(self, field::kNeighbourOffsets + 4 * k)) + index;
                            const uint32_t otherOffset = Offset(other);
                            const uint32_t base = VertexBase(self);
                            const bool there = (Flags(base + otherOffset) & mask & 0xFFFFu) != 0;
                            if (!here && !there)
                                continue;
                            const double difference =
                                static_cast<double>(F(base + offset)) - static_cast<double>(F(base + otherOffset));
                            const double limit = (k & 1u) ? Global<float>(addr::cSTETerrain_SmoothDiagonalLimit)
                                                          : Global<float>(addr::cSTETerrain_SmoothStraightLimit);
                            if (difference > limit) {
                                const float amount = static_cast<float>(
                                    (difference - limit) * static_cast<double>(Global<float>(addr::cSTETerrain_SmoothFactor)));
                                if (here)
                                    SmoothDown(self, base, offset, amount);
                                if (there)
                                    SmoothUp(self, VertexBase(self), otherOffset, amount);
                            } else if (difference < -limit) {
                                const float amount = static_cast<float>(
                                    (-difference - limit) * static_cast<double>(Global<float>(addr::cSTETerrain_SmoothFactor)));
                                if (here)
                                    SmoothUp(self, base, offset, amount);
                                if (there)
                                    SmoothDown(self, VertexBase(self), otherOffset, amount);
                            }
                        }
                        ++x;
                        ++index;
                        offset += vertex::kSize;
                    } while (x <= rect[2]);
                }
                ++z;
            } while (z <= rect[3]);
        }
    } while (--passes != 0);
}

// --- Flat areas --------------------------------------------------------------

// A std::vector of the game (begin, end, capacity).
struct GameVector {
    uint32_t start, finish, capacity;
};

// 0x00749660 (index, tolerance, mask): gathers the vertices connected to
// `index` through straight neighbours that have none of the mask's flags,
// are above the sea level and differ from the altitude of `index` by less
// than the tolerance; each gathered vertex gets the mask's flags. The list
// starts with `index` itself (unflagged, so it is gathered again from its
// first neighbour) and is a vector of the game's (growing through
// 0x0051CA60, so the allocations are the game's). With more gathered than
// 0x00B0D5F0 (unsigned) every one of them is set to the altitude of `index`
// through slot 50 and the answer is 1; else the flags are cleared again and
// the answer is 0.
uint8_t __fastcall FlattenAround(char* self, void*, uint32_t index, float tolerance, uint32_t mask)
{
    static bool logged = false;
    FirstCall("FlattenAround", logged);
    const float level = F(VertexBase(self) + Offset(index));
    uint32_t value = index;
    uint32_t tag = 1; // the game hands the address of its counter as the tag
    GameVector list{0, 0, 0};
    This<void>(addr::SC4PtrVector_InsertN, &list, 0u, &value, &tag, 1u, 1u);
    int32_t count = 1;
    int32_t next = 0;
    do {
        const uint32_t current = AtAddress<uint32_t>(list.start + 4u * static_cast<uint32_t>(next));
        const uint32_t x = current % U32(self, field::kVertsX);
        const uint32_t z = current / U32(self, field::kVertsX);
        ++next;
        for (uint32_t k = 0; k < 4; ++k) {
            const int32_t nx = Table(addr::cSTETerrain_StraightDX, k) + static_cast<int32_t>(x);
            if (nx < 0)
                continue;
            const uint32_t stride = U32(self, field::kVertsX);
            if (static_cast<uint32_t>(nx) >= stride)
                continue;
            const int32_t nz = Table(addr::cSTETerrain_StraightDZ, k) + static_cast<int32_t>(z);
            if (nz < 0)
                continue;
            if (static_cast<uint32_t>(nz) >= U32(self, field::kVertsZ))
                continue;
            const uint32_t other = stride * static_cast<uint32_t>(nz) + static_cast<uint32_t>(nx);
            const uint32_t offset = Offset(other);
            const uint32_t record = VertexBase(self) + offset;
            if ((Flags(record) & mask & 0xFFFFu) != 0)
                continue;
            if (!(F(record) > Sea(self)))
                continue;
            double distance = static_cast<double>(F(record)) - static_cast<double>(level);
            if (distance < 0.0)
                distance = -distance; // FABS (a NaN fails the test below either way)
            if (!(distance < static_cast<double>(tolerance)))
                continue;
            Flags(VertexBase(self) + offset) |= static_cast<uint16_t>(mask);
            value = other;
            if (list.finish != list.capacity) {
                if (list.finish)
                    AtAddress<uint32_t>(list.finish) = other;
                list.finish += 4u;
            } else {
                This<void>(addr::SC4PtrVector_InsertN, &list, list.finish, &value, &tag, 1u, 1u);
            }
            ++count;
        }
    } while (next < count);
    if (static_cast<uint32_t>(count) > Global<uint32_t>(addr::cSTETerrain_FlatMinCount)) {
        for (int32_t i = 0; i < count; ++i)
            Virtual<void>(self, slot::kSetAltitudeIndex, AtAddress<uint32_t>(list.start + 4u * static_cast<uint32_t>(i)),
                          level);
        if (list.start)
            Cdecl<void>(addr::SC4_Free, list.start);
        return 1;
    }
    const uint16_t keep = static_cast<uint16_t>(~mask);
    for (int32_t i = 0; i < count; ++i)
        Flags(VertexBase(self) + Offset(AtAddress<uint32_t>(list.start + 4u * static_cast<uint32_t>(i)))) &= keep;
    if (list.start)
        Cdecl<void>(addr::SC4_Free, list.start);
    return 0;
}

// --- Shore cliffs ------------------------------------------------------------

// A std::deque<uint32_t> of the game (STLport; blocks of 0x80 bytes), as
// CreateShoreCliffs keeps one on its stack.
struct IndexDeque {
    uint32_t startCurrent, startFirst, startLast, startNode;
    uint32_t finishCurrent, finishFirst, finishLast, finishNode;
    uint32_t map, mapSize;
};

// push_back as inlined at 0x0074AE48.
void PushBack(IndexDeque* queue, uint32_t index)
{
    const uint32_t current = queue->finishCurrent;
    if (current != queue->finishLast - 4u) {
        if (current)
            AtAddress<uint32_t>(current) = index;
        queue->finishCurrent = queue->finishCurrent + 4u;
        return;
    }
    const uint32_t used = static_cast<uint32_t>(static_cast<int32_t>(queue->finishNode - queue->map) >> 2);
    if (queue->mapSize - used < 2u)
        This<void>(addr::SC4CellDeque_ReserveMapAtBack, queue, 1u, 0u);
    const uint32_t block = Cdecl<uint32_t>(addr::SC4_stl_allocate, 0x80u);
    AtAddress<uint32_t>(queue->finishNode + 4u) = block;
    if (queue->finishCurrent)
        AtAddress<uint32_t>(queue->finishCurrent) = index;
    queue->finishNode += 4u;
    queue->finishFirst = AtAddress<uint32_t>(queue->finishNode);
    queue->finishLast = queue->finishFirst + 0x80u;
    queue->finishCurrent = queue->finishFirst;
}

// push_front as inlined at 0x0074AC7C.
void PushFront(IndexDeque* queue, uint32_t index)
{
    const uint32_t current = queue->startCurrent;
    if (current != queue->startFirst) {
        if (current - 4u)
            AtAddress<uint32_t>(current - 4u) = index;
        queue->startCurrent -= 4u;
        return;
    }
    if (static_cast<uint32_t>(static_cast<int32_t>(queue->startNode - queue->map) >> 2) < 1u)
        This<void>(addr::SC4CellDeque_ReserveMapAtBack, queue, 1u, 1u);
    const uint32_t block = Cdecl<uint32_t>(addr::SC4_stl_allocate, 0x80u);
    AtAddress<uint32_t>(queue->startNode - 4u) = block;
    queue->startNode -= 4u;
    queue->startFirst = AtAddress<uint32_t>(queue->startNode);
    queue->startLast = queue->startFirst + 0x80u;
    queue->startCurrent = queue->startLast - 4u;
    if (queue->startCurrent)
        AtAddress<uint32_t>(queue->startCurrent) = index;
}

// What both collectors do with the neighbour in direction `direction` of
// (x, z): 2 when it is a shore vertex (flag 0x10) without the mask's flags
// (then `taken` is its index and nx, nz its place), 1 when the walk ends
// there (inside the grid, not such a vertex, above the sea level), 0 to go on
// to the next direction (outside the grid, or at or below the sea level, or
// a NaN).
inline int LookAround(char* self, uint32_t stride, uint32_t x, uint32_t z, uint32_t direction, uint32_t mask,
                      uint32_t& nx, uint32_t& nz, uint32_t& taken)
{
    nx = static_cast<uint32_t>(Table(addr::cSTETerrain_AroundDX, direction)) + x;
    nz = static_cast<uint32_t>(Table(addr::cSTETerrain_AroundDZ, direction)) + z;
    if (nx >= stride || nz >= U32(self, field::kVertsZ))
        return 0;
    const uint32_t other = stride * nz + nx;
    const uint32_t record = VertexBase(self) + Offset(other);
    const uint32_t flags = Flags(record);
    if ((flags & flag::kShoreFlag) && (flags & mask & 0xFFFFu) == 0) {
        taken = other;
        return 2;
    }
    return F(record) > Sea(self) ? 1 : 0;
}

// Whether the neighbour in a direction is inside the grid and below the sea
// level (the first loop of both collectors).
inline bool WaterAround(char* self, uint32_t stride, uint32_t x, uint32_t z, uint32_t direction)
{
    const uint32_t nx = static_cast<uint32_t>(Table(addr::cSTETerrain_AroundDX, direction)) + x;
    const uint32_t nz = static_cast<uint32_t>(Table(addr::cSTETerrain_AroundDZ, direction)) + z;
    if (nx >= stride || nz >= U32(self, field::kVertsZ))
        return false;
    return F(VertexBase(self) + Offset(stride * nz + nx)) < Sea(self);
}

// 0x0074AD20 (index, queue, mask): follows the shore line from a vertex with
// the water on one hand: finds the first of the eight directions (from the
// west, clockwise) with water, then turns on from there until a shore vertex
// (flag 0x10) without the mask's flags comes: that one gets the flags, goes
// to the back of the queue and becomes the place to go on from, starting
// behind the direction it was come from. Ends when a turn meets land that is
// not such a vertex, or comes round to its start.
void __fastcall ShoreCollect1(char* self, void*, uint32_t index, IndexDeque* queue, uint32_t mask)
{
    static bool logged = false;
    FirstCall("ShoreCollect1", logged);
    uint32_t stride = U32(self, field::kVertsX);
    uint32_t x = index % stride;
    uint32_t z = index / stride;
    uint32_t direction = 0;
    while (!WaterAround(self, stride, x, z, direction))
        if (++direction >= 8)
            return;
    uint32_t start = direction;
    direction = (direction + 1u) & 7u;
    for (;;) {
        bool found = false;
        if (direction != start) {
            stride = U32(self, field::kVertsX);
            for (;;) {
                uint32_t nx = 0, nz = 0, taken = 0;
                const int what = LookAround(self, stride, x, z, direction, mask, nx, nz, taken);
                if (what == 2) {
                    Flags(VertexBase(self) + Offset(taken)) |= static_cast<uint16_t>(mask);
                    PushBack(queue, taken);
                    x = nx;
                    z = nz;
                    found = true;
                    break;
                }
                if (what == 1)
                    break;
                direction = (direction + 1u) & 7u;
                if (direction == start)
                    break;
            }
        }
        direction = (direction - 4u) & 7u;
        start = direction;
        direction = (direction + 1u) & 7u;
        if (!found)
            return;
    }
}

// 0x0074AB60 (index, queue, mask): the same the other way round (directions
// counted down from 7), the vertices going to the FRONT of the queue. The
// row length is read again at every look.
void __fastcall ShoreCollect2(char* self, void*, uint32_t index, IndexDeque* queue, uint32_t mask)
{
    static bool logged = false;
    FirstCall("ShoreCollect2", logged);
    const uint32_t stride = U32(self, field::kVertsX);
    uint32_t x = index % stride;
    uint32_t z = index / stride;
    int32_t direction = 7;
    while (!WaterAround(self, stride, x, z, static_cast<uint32_t>(direction)))
        if (--direction < 0)
            return;
    int32_t start = direction;
    if (--direction < 0)
        direction = 7;
    for (;;) {
        bool found = false;
        if (direction != start) {
            for (;;) {
                uint32_t nx = 0, nz = 0, taken = 0;
                const int what = LookAround(self, U32(self, field::kVertsX), x, z, static_cast<uint32_t>(direction), mask,
                                            nx, nz, taken);
                if (what == 2) {
                    Flags(VertexBase(self) + Offset(taken)) |= static_cast<uint16_t>(mask);
                    PushFront(queue, taken);
                    x = nx;
                    z = nz;
                    found = true;
                    break;
                }
                if (what == 1)
                    break;
                if (--direction < 0)
                    direction = 7;
                if (direction == start)
                    break;
            }
        }
        direction = static_cast<int32_t>((static_cast<uint32_t>(direction) - 4u) & 7u);
        start = direction;
        if (--direction < 0)
            direction = 7;
        if (!found)
            return;
    }
}

// 0x00741680: the first straight neighbour (west, north, east, south) inside
// the grid that is below the sea level; -1 for none. The row is the index
// divided by the ROW COUNT (+0x3C), not the row length: the game's (the same
// on a square map), kept.
uint32_t WaterNeighbour(char* self, uint32_t index)
{
    const uint32_t stride = U32(self, field::kVertsX);
    const uint32_t x = index % stride;
    const uint32_t z = index / U32(self, field::kVertsZ);
    for (uint32_t direction = 0; direction < 8; direction += 2) {
        const uint32_t nx = static_cast<uint32_t>(Table(addr::cSTETerrain_AroundDX, direction)) + x;
        const uint32_t nz = static_cast<uint32_t>(Table(addr::cSTETerrain_AroundDZ, direction)) + z;
        if (nx >= stride || nz >= U32(self, field::kVertsZ))
            continue;
        const uint32_t other = stride * nz + nx;
        if (F(VertexBase(self) + Offset(other)) < Sea(self))
            return other;
    }
    return 0xFFFFFFFFu;
}

// A shore record: the water neighbour, the shore vertex, and five vertices
// inland in a straight line.
constexpr uint32_t kRecordSize = 0x1C;

// 0x00744220 (records): shapes one run of shore records into a cliff. The
// level: sea level + a uniform draw between the floats 0x00B0D618 and
// 0x00B0D61C, rounded to float (drawn before the count is looked at). Then,
// for at most 0x00B0D610 records: the water neighbour and the shore vertex
// get the mark 4; a Gaussian draw between 0 and 2 is added to the level when
// the shore vertex is above it, else taken from it, and the vertex set to
// that (rounded to float) through slot 50; the five inland vertices are set
// to that + a uniform draw between -8 and 8 each. A record without a water
// neighbour (-1) makes the game write the mark in front of the vertex array;
// so does this.
void ShoreCliffRun(char* self, GameVector* records)
{
    void* const random = reinterpret_cast<void*>(addr::cSTETerrain_Random);
    const float level = static_cast<float>(
        This<double>(addr::cRZRandom_Uniform, random, static_cast<double>(Global<float>(addr::cSTETerrain_ShoreCliffLow)),
                     static_cast<double>(Global<float>(addr::cSTETerrain_ShoreCliffHigh))) +
        static_cast<double>(Sea(self)));
    uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(records->finish - records->start) / 28);
    if (count > Global<uint32_t>(addr::cSTETerrain_ShoreMaxRun))
        count = Global<uint32_t>(addr::cSTETerrain_ShoreMaxRun);
    if (static_cast<int32_t>(count) <= 0)
        return;
    uint32_t at = 0;
    do {
        const uint32_t record = records->start + at;
        const double high = Global<double>(addr::SC4_DoubleTwo);
        AtAddress<uint8_t>(VertexBase(self) + Offset(AtAddress<uint32_t>(record)) + vertex::kFlags) |= 4;
        AtAddress<uint8_t>(VertexBase(self) + Offset(AtAddress<uint32_t>(record + 4u)) + vertex::kFlags) |= 4;
        const double step = This<double>(addr::cRZRandom_GaussianBetween, random, Global<double>(addr::SC4_DoubleZero), high);
        const uint32_t shore = AtAddress<uint32_t>(record + 4u);
        float height;
        if (F(VertexBase(self) + Offset(shore)) > level)
            height = static_cast<float>(step + static_cast<double>(level));
        else
            height = static_cast<float>(static_cast<double>(level) - step);
        Virtual<void>(self, slot::kSetAltitudeIndex, shore, height);
        for (uint32_t k = 2; k < 7; ++k) {
            const float inland = static_cast<float>(
                This<double>(addr::cRZRandom_Uniform, random, Global<double>(addr::cSTETerrain_ShoreInlandLow),
                             Global<double>(addr::cSTETerrain_ShoreInlandHigh)) +
                static_cast<double>(height));
            Virtual<void>(self, slot::kSetAltitudeIndex, AtAddress<uint32_t>(record + 4u * k), inland);
        }
        at += kRecordSize;
    } while (--count != 0);
}

// The i-th element of a deque from its start (what the game gets by copying
// the start iterator and advancing it, 0x006C6010: blocks of 32).
inline uint32_t DequeAt(const IndexDeque* queue, int32_t i)
{
    const int32_t position = (static_cast<int32_t>(queue->startCurrent - queue->startFirst) >> 2) + i;
    if (position >= 0 && position < 0x20)
        return AtAddress<uint32_t>(queue->startCurrent + 4u * static_cast<uint32_t>(i));
    const int32_t block = position > 0 ? position / 32 : (position + 1) / 32 - 1;
    const uint32_t first = AtAddress<uint32_t>(queue->startNode + 4u * static_cast<uint32_t>(block));
    return AtAddress<uint32_t>(first + 4u * static_cast<uint32_t>(position - block * 32));
}

// The end of a run of records in ShoreApply: a run longer than 0x00B0D60C
// (unsigned) is shaped.
inline bool ShapeRun(char* self, GameVector* records)
{
    const int32_t count = static_cast<int32_t>(records->finish - records->start) / 28;
    if (static_cast<uint32_t>(count) <= Global<uint32_t>(addr::cSTETerrain_ShoreMinRun))
        return false;
    ShoreCliffRun(self, records);
    return true;
}

// 0x00749FA0 (queue): 0 for a shore line of fewer vertices than 0x00B0D60C.
// Else, for each vertex of the line in order: its water neighbour
// (0x00741680) gives the direction inland (vertex index - neighbour index);
// the five vertices that many indices on, each time, must lie in a row
// inside the grid and not below the sea level, and the heights above the sea
// of the six together must be more than 0x00B0D614. Such a vertex adds a
// record to the run (a vector of the game's, growing through 0x00748F20);
// any other ends the run, which is shaped (0x00744220) when it is longer
// than 0x00B0D60C; so is the run left at the end. Answers whether any run was
// shaped. A vertex without a water neighbour is not treated apart (the
// neighbour index is -1), as in the game.
uint8_t __fastcall ShoreApply(char* self, void*, IndexDeque* queue)
{
    static bool logged = false;
    FirstCall("ShoreApply", logged);
    const int32_t size = (static_cast<int32_t>(queue->finishNode - queue->startNode) >> 2) * 32 +
                         (static_cast<int32_t>(queue->finishCurrent - queue->finishFirst) >> 2) +
                         (static_cast<int32_t>(queue->startLast - queue->startCurrent) >> 2) - 0x20;
    if (static_cast<uint32_t>(size) < Global<uint32_t>(addr::cSTETerrain_ShoreMinRun))
        return 0;
    GameVector records{0, 0, 0};
    uint8_t shaped = 0;
    uint32_t tag = 0;
    for (int32_t i = 0; i < size; ++i) {
        const uint32_t index = DequeAt(queue, i);
        uint32_t record[7];
        record[0] = WaterNeighbour(self, index);
        record[1] = index;
        const uint32_t step = index - record[0];
        double sum = static_cast<double>(F(VertexBase(self) + Offset(index))) - static_cast<double>(Sea(self));
        uint32_t along = step;
        bool good = true;
        for (uint32_t k = 0; k < 5; ++k) {
            const uint32_t inland = along + index;
            const uint32_t row = inland / U32(self, field::kVertsX);
            record[2 + k] = inland;
            if (row >= U32(self, field::kVertsZ)) {
                good = false;
                break;
            }
            const uint32_t place = VertexBase(self) + Offset(inland);
            if (F(place) < Sea(self)) {
                good = false;
                break;
            }
            sum = sum + (static_cast<double>(F(place)) - static_cast<double>(Sea(self)));
            along += step;
        }
        if (good && sum > static_cast<double>(Global<float>(addr::cSTETerrain_ShoreMinHeightSum))) {
            if (records.finish != records.capacity) {
                if (records.finish)
                    for (uint32_t k = 0; k < 7; ++k)
                        AtAddress<uint32_t>(records.finish + 4u * k) = record[k];
                records.finish += kRecordSize;
            } else {
                This<void>(addr::SC4Record28Vector_InsertN, &records, records.finish, static_cast<void*>(record), &tag, 1u,
                           1u);
            }
            continue;
        }
        if (ShapeRun(self, &records))
            shaped = 1;
        records.finish = records.start;
    }
    if (ShapeRun(self, &records))
        shaped = 1;
    if (records.start)
        Cdecl<void>(addr::SC4_Free, records.start);
    return shaped;
}

// --- Slot 57 -----------------------------------------------------------------

// Slot 57 (0x0074F0E0), ApplyBrush(brush, parameters): the applied rectangle
// of the parameters starts empty (0x00B0D698); 0 for an exemplar ID the
// brush map does not have. Else the brush's layers are run (0x0074D730, the
// game's, with the record and the parameters), and when the parameters ask
// for it and the applied rectangle is not empty, that rectangle is
// redisplayed (slot 68 with 1, 1, the rectangle, the parameters' dword
// +0x20). Answers 1.
uint8_t __fastcall ApplyBrush(char* self, void*, uint32_t id, char* params)
{
    static bool logged = false;
    FirstCall("ApplyBrush", logged);
    int32_t* const applied = &At<int32_t>(params, parameters::kApplied);
    const int32_t* const empty = &Global<int32_t>(addr::cSTETerrain_EmptyRect);
    applied[0] = empty[0];
    applied[1] = empty[1];
    applied[2] = empty[2];
    applied[3] = empty[3];
    char* node = nullptr;
    This<void>(addr::SC4Uint32Map_Find, reinterpret_cast<void*>(addr::cSTETerrain_Brushes), &node, &id);
    if (node == Global<char*>(addr::cSTETerrain_Brushes))
        return 0;
    This<void>(addr::cSTETerrain_ApplyBrushLayers, self, static_cast<void*>(node + brush_node::kRecord),
               static_cast<void*>(params));
    if (At<uint8_t>(params, parameters::kRedisplay) && applied[0] <= applied[2] && applied[1] <= applied[3])
        Virtual<void>(self, slot::kRedisplay, 1u, 1u, static_cast<void*>(applied),
                      At<uint32_t>(params, parameters::kMessageData));
    return 1;
}

// --- Installation ------------------------------------------------------------

struct Detour {
    std::uintptr_t address;
    void* ours;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

// Every direct call site of each (scan of all direct calls in the exe),
// checked for what it uses after the call:
// 0x007409B0: 0x00742159 (slot 90; reads [ECX] next), 0x00742908 (calls
//   0x00741DE0 with ECX next), 0x00743B9B, 0x00748E69, 0x00749B82,
//   0x0074C11A: the thunk keeps ECX.
// 0x00740A20: 0x0074A321, 0x0074C315 (calls 0x00745020 with ECX next): the
//   thunk keeps ECX.
// 0x00740F70: 0x00748287, 0x00749896, 0x0074A22B. 0x00741DE0: 0x00742918,
//   0x0074C12C. 0x00743210: 0x0074A313, 0x0074C307. 0x00745020: 0x00748E5B,
//   0x0074A336, 0x0074C323. 0x00749660: 0x0074A2C0. 0x0074AD20: 0x0074B5C2,
//   0x0074C24F. 0x0074AB60: 0x0074B5CC, 0x0074C25E. 0x00749FA0: 0x0074C26A.
//   All load what they use anew after the call (registers ESI, EDI, EBX, EBP
//   or the stack); arguments on the stack only.
// The five callbacks are reached through pointers only (the walkers above
// and their originals), which test AL and keep nothing in ECX or EDX.
const Detour kDetours[] = {
    OPENSC4_DETOUR(addr::cSTETerrain_ClearFlagsInRect, ClearFlagsInRectThunk),
    OPENSC4_DETOUR(addr::cSTETerrain_FlaggedBounds, FlaggedBoundsThunk),
    OPENSC4_DETOUR(addr::cSTETerrain_UpdateMinMaxHeight, UpdateMinMaxHeight),
    OPENSC4_DETOUR(addr::cSTETerrain_FlagVerticesWhere, FlagVerticesWhere),
    OPENSC4_DETOUR(addr::cSTETerrain_SpreadMarks, SpreadMarks),
    OPENSC4_DETOUR(addr::cSTETerrain_SmoothRect, SmoothRect),
    OPENSC4_DETOUR(addr::cSTETerrain_FlattenAround, FlattenAround),
    OPENSC4_DETOUR(addr::cSTETerrain_ShoreCollect1, ShoreCollect1),
    OPENSC4_DETOUR(addr::cSTETerrain_ShoreCollect2, ShoreCollect2),
    OPENSC4_DETOUR(addr::cSTETerrain_ShoreApply, ShoreApply),
    OPENSC4_DETOUR(addr::cSTETerrain_IsMarked1, IsMarked1),
    OPENSC4_DETOUR(addr::cSTETerrain_IsMarked4, IsMarked4),
    OPENSC4_DETOUR(addr::cSTETerrain_IsLandUnmarked1, IsLandUnmarked1),
    OPENSC4_DETOUR(addr::cSTETerrain_IsLandUnmarked4, IsLandUnmarked4),
    OPENSC4_DETOUR(addr::cSTETerrain_IsShoreVertex, IsShoreVertex),
};

#undef OPENSC4_DETOUR

void* g_originals[std::size(kDetours)];

constexpr int kApplyBrushSlot = 57;

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSTETerrain_vftable);
    if (vtable[kApplyBrushSlot] != addr::cSTETerrain_ApplyBrush) {
        log::Write("terrain brush: slot %d of %08X holds %08X, expected %08X; not installing", kApplyBrushSlot,
                   static_cast<unsigned>(addr::cSTETerrain_vftable), static_cast<unsigned>(vtable[kApplyBrushSlot]),
                   static_cast<unsigned>(addr::cSTETerrain_ApplyBrush));
        return false;
    }
    for (const Detour& detour : kDetours) {
        if (!addr_check::Find(detour.address)) {
            log::Write("terrain brush: no address entry for %08X; not installing",
                       static_cast<unsigned>(detour.address));
            return false;
        }
    }
    for (std::size_t i = 0; i < std::size(kDetours); ++i) {
        if (!hook::Install(*addr_check::Find(kDetours[i].address), kDetours[i].ours, &g_originals[i])) {
            log::Write("terrain brush: detour %u of %u failed (%08X); the earlier ones stay", static_cast<unsigned>(i),
                       static_cast<unsigned>(std::size(kDetours)), static_cast<unsigned>(kDetours[i].address));
            return false;
        }
    }
    std::uintptr_t* const entry = vtable + kApplyBrushSlot;
    DWORD old = 0;
    if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
        return false;
    *entry = reinterpret_cast<std::uintptr_t>(&ApplyBrush);
    VirtualProtect(entry, sizeof(*entry), old, &old);
    log::Write("terrain brush: slot 57 of %08X and %u functions (detours) are ours",
               static_cast<unsigned>(addr::cSTETerrain_vftable), static_cast<unsigned>(std::size(kDetours)));
    return true;
}

const bool g_registered = registry::Register(
    {"simulators.terrain.brush", "simulators/terrain", registry::Kind::Detour, registry::Phase::PostAppInit,
     addr::cSTETerrain_ApplyBrush,
     "Our cSTETerrain shaping machinery: slot 57 (ApplyBrush, around the game's layer worker) and the helpers of "
     "CreateFlatAreas and CreateShoreCliffs (flatten, spread, bounds, smooth, shore line collectors and cliffs) with "
     "their callbacks, by detours",
     false, Install});

} // namespace
} // namespace opensc4::terrain_sim
