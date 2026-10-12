// Our cSTETerrain erosion, flood and water distance map (build 641), piece 5
// of the city's terrain object (vtable 0x00AB3D18, the service 0x00B43CF4).
// Group simulators.terrain.erode.
//
//   0x00747B20  Erode                 the erosion message's worker (kind, strong)
//   0x00746AD0  ErodePass             rain, flow and sediment over a rectangle
//   0x00746F40  SmoothAll             the slope limits over the whole map
//   0x00744760  LowestNeighbour       where the water of a vertex runs to
//   slot 56, 0x00747720  StartFlood   500 m of water on a fixed window
//   0x007467F0  FloodStep             one step of the flood, or its end
//   0x00744DE0  SaveWaterDepths       (only StartFlood calls it)
//   0x00744D60  RestoreWaterDepths    (only FloodStep calls it)
//   0x007440C0  UpdateWaterDistances  the byte rows +0x190 and the count +0xE4
//
// All from the disassembly, statement by statement.
//
// Erosion (ErodePass, rect and iterations). The water line is the sea level
// less 15 (0x00AB3CC8). First every vertex of the rectangle gets the water
// depth line - altitude when it is below the line, else 0, and no sediment.
// Then, `iterations` times: (rain) each vertex above the line gets
// 0.01 (0x00B0D5A8) x its ALTITUDE more water; (flow) each vertex with
// water, rows first, the changes seen at once: below the line the depth is
// set to line - altitude and that is all; else the water runs to the
// neighbour LowestNeighbour names (on the rectangle's border: nowhere, it
// leaves the map): half the difference of the two surfaces, at most the
// depth. The flow can carry 5 (0x00B0D5AC) x its amount. With more sediment
// in the water than that, the capacity's worth is carried on and the rest
// stays; else an unlocked vertex is eroded by rate x (capacity - sediment)
// and the water's sediment is carried on with it, the rate going from 0.02
// to 0.06 (0x00B0D5B8, 0x00B0D5BC) as the altitude step to the neighbour
// (divided by the square root of 2 for a diagonal one) goes from 5.128 to
// 38.14 (0x00B0D5C0, 0x00B0D5C4). The water and what is carried arrive at
// the neighbour only when that is above the line. A vertex left without
// water loses its sediment; one left with water and sediment, unlocked, is
// raised by 0.2 (0x00B0D5B0) of the sediment, which the water loses.
// At the end the water object resets the depths (sea level - altitude) and
// the sediment of the rectangle grown by one (0x0073D180, the game's): the
// water and the sediment are scratch values of the pass.
// Erode: kind 0 runs ErodePass over the whole map `runs` times and then
// SmoothAll; the three numbers come from 0x00B0D5A0, 0x00B0D5A4 and
// 0x00B0D5EC (1 run of 35 iterations, 1 smoothing pass), or with the strong
// byte from the three dwords at 0x00B4E1C0 that the region screen's tuning
// fills. Kind 1 runs SmoothAll alone, 5 passes (0x00B0D594). No altitude
// clamp in ErodePass; SmoothAll clamps as SmoothRect does.
//
// The flood (slot 56). StartFlood copies every water depth to the array
// +0xE0, sets the window +0x19C to the vertices 20 to 40 in x and z (fixed
// numbers, whatever the map's size; the far ends are not included), adds
// 500 (0x00AB3CCC) to the depth of each vertex of it, sets +0xD4 and runs
// the first step. A step (the timer message runs one while +0xD4 is set):
// over the window, rows first: from the 352nd step on (counter above 350) the depth loses 0.01
// (0x00AB3CC0) of what it has above the saved depth; then the water runs to
// the lowest neighbour as in the erosion (on the map's edge it leaves), and
// arrives only on a neighbour above the SEA LEVEL. Then the window grows by
// one on every side (cut to the cells), the counter +0x1AC goes up, message
// 0x678E6E3A goes out with the window and the window is redisplayed (slot
// 68 with 0, 1, the window, 0). The step that finds the counter above 500
// (the 502nd) ends the flood instead: +0xD4 cleared, every water depth put
// back from +0xE0, the same message and redisplay. Altitudes never change.
//
// The water distance map (UpdateWaterDistances, rect): the rectangle is
// joined into +0xAC; nothing more while that covers less than 100 (width x
// height). Else +0xA8 and +0xAC are reset, each vertex's byte in the rows
// +0x190 becomes 0 on water (depth above 0; counted in +0xE4) and 6
// (the byte 0x00B0D58C + 2) elsewhere, and with any water 0x005E5180 (the
// game's, outside the class) spreads the distances 1 to 5 from it.
//
// How the group reaches every caller. Slot 56 is swapped in the vtable. The
// others are called directly by game code and by our other terrain files at
// their addresses, so they get detours: Erode (DoMessage 0x0074A4CD, the
// new-terrain set-up 0x0074826A), ErodePass (the brushes' erosion layer
// 0x00747A20), SmoothAll (only Erode calls it), FloodStep (the timer's work
// 0x0074A3F7; the game's slot 56 jumps to it), UpdateWaterDistances (the
// refresh 0x007490B9, the timer's work 0x0074A45F). Every one of those
// sites is a plain thiscall with the arguments on the stack and loads what
// it uses anew after the call (listed at the installation): no thunks.
// LowestNeighbour, SaveWaterDepths and RestoreWaterDepths are called only
// by functions replaced here and are plain functions (the game's
// SaveWaterDepths leaves ECX alone and its StartFlood counts on that: of no
// account once the slot is ours).
//
// Floating point. As in the other terrain files: what the game keeps on the
// x87 stack (53-bit precision) is a double here, and a float exactly where
// the game stores one; comparisons are made on the value still on the stack.
// The only x87 instruction beyond the four operations is FSQRT (the square
// root of 2, twice, each kept by the game in a static with a guard bit;
// ours fill the same statics under the same guards): correctly rounded, as
// std::sqrt is. No transcendental instruction. Constants are read from the
// game's memory at each use.
//
// Random numbers: none of these functions draws.
//
// Still the game's and called at their addresses: the water object's reset
// 0x0073D180, the message poster 0x00743380, the distance spreader
// 0x005E5180, slot 68. The normals of a rectangle (0x00743B60) are in
// terrain_erode_2.cpp, a group of their own. Not rewritten: 0x00748130 (a
// new city's terrain; it calls Erode at its address).
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/terrain_sim.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace opensc4::terrain_sim {

// --- Additions for terrain_sim.h (to be merged there) --------------------------

namespace field {
constexpr std::size_t kNeighbourOffsets = 0x84; // int32[8]: as in terrain_brush.cpp
constexpr std::size_t kElapsed = 0xA8;          // uint32: as in terrain_messages.cpp; UpdateWaterDistances resets it
constexpr std::size_t kChanged = 0xAC;          // int32[4]: the rectangle UpdateWaterDistances gathers; empty (0x00B0D698) after a refill
constexpr std::size_t kFlooding = 0xD4;         // uint8: StartFlood sets it, the FloodStep that ends the flood clears it
constexpr std::size_t kSavedDepths = 0xE0;      // float[vertex count] (Init allocates it): the water depths as StartFlood found them
constexpr std::size_t kRows = 0x190;            // vector of 12-byte vectors of bytes: the distance from water per vertex (0 to 6)
constexpr std::size_t kFloodRect = 0x19C;       // int32[4]: the flood's window, x0, z0, x1, z1 (vertices; the far ends not included)
constexpr std::size_t kFloodSteps = 0x1AC;      // int32: the flood's steps so far; above 500 the next step ends it
} // namespace field

namespace slot {
constexpr std::size_t kRedisplay = 0x110; // slot 68 (bool, bool, rect, uint32_t)
} // namespace slot

namespace message {
constexpr uint32_t kFloodWindow = 0x678E6E3A; // data 1 = (x0 << 16) + x1, data 2 = (z0 << 16) + z1, 0, 0
} // namespace message

// The flood's fixed numbers (immediates in the code).
namespace flood {
constexpr int32_t kWindowLow = 20;   // 0x0074772B
constexpr int32_t kWindowHigh = 40;  // 0x00747730
constexpr int32_t kLastStep = 500;   // 0x00746843
} // namespace flood

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

inline float& F(uint32_t address) { return AtAddress<float>(address); }

// A float constant of the game, as a value on the x87 stack.
inline double C(std::uintptr_t address) { return static_cast<double>(Global<float>(address)); }

inline uint32_t U32(void* self, std::size_t offset) { return At<uint32_t>(self, offset); }

inline float Sea(char* self) { return At<float>(self, field::kSeaLevel); }

// A record's offset in the vertex and water arrays (index * 9 * 4, 32-bit,
// wrapping). Nothing here tests an index's range unless the game does.
inline uint32_t Offset(uint32_t index) { return index * 9u * 4u; }

inline bool Locked(uint32_t record) { return (AtAddress<uint8_t>(record + 0x23) & 0x40) != 0; }

inline int32_t NeighbourStep(char* self, uint32_t direction)
{
    return At<int32_t>(self, field::kNeighbourOffsets + 4u * direction);
}

// The neighbours a vertex at (x, z) has inside the grid, one bit each in the
// order of the +0x84 table (inlined in 0x00744760 and 0x00746F40, as in
// 0x00745020): x and z below 1 are tested signed, the far edges unsigned
// against the cell counts.
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

// --- Where the water runs ----------------------------------------------------

// 0x00744760 (index): the direction (0 to 7, the +0x84 table's order) of the
// neighbour the water of a vertex runs to, -1 for none. The surface is
// altitude + water depth, rounded to float for the vertex itself. First the
// four straight neighbours (0, 2, 4, 6): the one the vertex stands highest
// above, by more than 0 (a later one must beat the float-rounded best so
// far). Then the four diagonal ones (1, 3, 5, 7): one wins when the vertex
// stands above it by more than the square root of 2 (as a float) times that
// best drop, rounded to float; a later one must beat the float-rounded drop
// of the last winner. The array addresses are taken once.
int32_t LowestNeighbour(char* self, uint32_t index)
{
    const uint32_t stride = U32(self, field::kVertsX);
    const uint8_t bits = NeighbourBits(self, index % stride, index / stride);
    const uint32_t wet = WaterBase(self);
    const uint32_t base = VertexBase(self);
    const uint32_t offset = Offset(index);
    double best = C(addr::SC4_FloatZero);
    int32_t result = -1;
    const float here = static_cast<float>(static_cast<double>(F(base + offset)) + static_cast<double>(F(wet + offset)));
    // The game's static (guard bit 0 of 0x00B4BD78): FSQRT of the double 2.0.
    if (!(Global<uint8_t>(addr::cSTETerrain_SqrtTwoGuard) & 1u)) {
        Global<uint32_t>(addr::cSTETerrain_SqrtTwoGuard) |= 1u;
        Global<float>(addr::cSTETerrain_SqrtTwo) = static_cast<float>(std::sqrt(Global<double>(addr::SC4_DoubleTwo)));
    }
    for (uint32_t k = 0; k < 8; k += 2) {
        if (!(bits & (1u << k)))
            continue;
        const uint32_t other = Offset(static_cast<uint32_t>(NeighbourStep(self, k)) + index);
        const double drop =
            static_cast<double>(here) - (static_cast<double>(F(base + other)) + static_cast<double>(F(wet + other)));
        const float stored = static_cast<float>(drop);
        if (drop > best) {
            result = static_cast<int32_t>(k);
            best = static_cast<double>(stored);
        }
    }
    float limit = static_cast<float>(C(addr::cSTETerrain_SqrtTwo) * best);
    for (uint32_t k = 1; k < 8; k += 2) {
        if (!(bits & (1u << k)))
            continue;
        const uint32_t other = Offset(static_cast<uint32_t>(NeighbourStep(self, k)) + index);
        const double drop =
            static_cast<double>(here) - (static_cast<double>(F(base + other)) + static_cast<double>(F(wet + other)));
        if (drop > static_cast<double>(limit)) {
            limit = static_cast<float>(drop);
            result = static_cast<int32_t>(k);
        }
    }
    return result;
}

// --- Erosion -----------------------------------------------------------------

// One vertex of ErodePass's flow loop (0x00746C70 to 0x00746E91). `line` is
// the water line, `ratio` the rate's slope; x, z, index and offset the
// vertex.
inline void FlowVertex(char* self, const int32_t* rect, int32_t x, int32_t z, uint32_t index, uint32_t offset, float line,
                       float ratio)
{
    const uint32_t wet = WaterBase(self) + offset;
    uint32_t base = VertexBase(self);
    const uint32_t record = base + offset;
    const uint32_t sediment = wet + water::kSediment;
    // Without water (0 or less) nothing; a NaN goes on.
    if (F(wet) < Global<float>(addr::SC4_FloatZero) || F(wet) == Global<float>(addr::SC4_FloatZero))
        return;
    if (F(record) < line) {
        F(wet) = static_cast<float>(static_cast<double>(line) - static_cast<double>(F(record)));
        return;
    }
    int32_t other = -1;    // the neighbour's index; -1 on the rectangle's border
    int32_t direction = 0; // read only with a neighbour
    double flow;
    if (z == rect[1] || z == rect[3] || x == rect[0] || x == rect[2]) {
        flow = static_cast<double>(F(wet));
    } else {
        direction = LowestNeighbour(self, index);
        if (direction < 0)
            return;
        other = NeighbourStep(self, static_cast<uint32_t>(direction)) + static_cast<int32_t>(index);
        const double surface = static_cast<double>(F(record)) + static_cast<double>(F(wet));
        base = VertexBase(self);
        const uint32_t wetBase = WaterBase(self);
        const uint32_t otherOffset = Offset(static_cast<uint32_t>(other));
        const double otherSurface =
            static_cast<double>(F(wetBase + otherOffset)) + static_cast<double>(F(base + otherOffset));
        flow = (surface - otherSurface) * C(addr::SC4_FloatHalf);
        if (flow > static_cast<double>(F(wet)))
            flow = static_cast<double>(F(wet));
    }
    if (!(flow > C(addr::SC4_FloatZero)))
        return;
    float carried = 0.0f; // the sediment that goes with the water
    const double capacity = C(addr::cSTETerrain_ErodeCapacity) * flow;
    if (capacity < static_cast<double>(F(sediment))) {
        carried = static_cast<float>(capacity);
        F(sediment) = static_cast<float>(static_cast<double>(F(sediment)) - capacity);
    } else {
        double slope;
        if (other >= 0) {
            slope = static_cast<double>(F(record)) - static_cast<double>(F(base + Offset(static_cast<uint32_t>(other))));
            if (direction & 1)
                slope = slope * C(addr::cSTETerrain_InverseSqrtTwo);
        } else {
            slope = C(addr::SC4_FloatZero);
        }
        double rate;
        if (slope <= C(addr::cSTETerrain_ErodeSlopeLow))
            rate = C(addr::cSTETerrain_ErodeRateLow);
        else if (slope >= C(addr::cSTETerrain_ErodeSlopeHigh)) // not a NaN: the game tests "below" and interpolates that
            rate = C(addr::cSTETerrain_ErodeRateHigh);
        else
            rate = (slope - C(addr::cSTETerrain_ErodeSlopeLow)) * static_cast<double>(ratio) +
                   C(addr::cSTETerrain_ErodeRateLow);
        if (!Locked(base + offset)) {
            const double eroded = rate * (capacity - static_cast<double>(F(sediment)));
            F(record) = static_cast<float>(static_cast<double>(F(record)) - eroded);
            carried = static_cast<float>(eroded + static_cast<double>(F(sediment)));
            AtAddress<uint32_t>(sediment) = 0;
        }
    }
    F(wet) = static_cast<float>(static_cast<double>(F(wet)) - flow);
    if (other >= 0) {
        const uint32_t otherOffset = Offset(static_cast<uint32_t>(other));
        if (F(VertexBase(self) + otherOffset) > line) {
            const uint32_t otherWet = WaterBase(self) + otherOffset;
            F(otherWet) = static_cast<float>(flow + static_cast<double>(F(otherWet)));
            const uint32_t otherSediment = WaterBase(self) + otherOffset + water::kSediment;
            F(otherSediment) = static_cast<float>(static_cast<double>(carried) + static_cast<double>(F(otherSediment)));
        }
    }
    // No water left (0 or less): no sediment either. A NaN goes on.
    if (F(wet) < Global<float>(addr::SC4_FloatZero) || F(wet) == Global<float>(addr::SC4_FloatZero)) {
        AtAddress<uint32_t>(sediment) = 0;
        return;
    }
    if (!(F(sediment) > Global<float>(addr::SC4_FloatZero)))
        return;
    if (Locked(VertexBase(self) + offset))
        return;
    const double dropped = C(addr::cSTETerrain_ErodeDeposit) * static_cast<double>(F(sediment));
    F(record) = static_cast<float>(dropped + static_cast<double>(F(record)));
    F(sediment) = static_cast<float>(static_cast<double>(F(sediment)) - dropped);
}

// 0x00746AD0 (rect, iterations): see the top of the file. The rectangle is
// of vertices, both ends included, signed, read again at every step as the
// game does; no range test. The locals are computed in the game's order: the
// water line, the static 1 / square root of 2 (guard bit 0 of 0x00B4BD88),
// the rate's slope.
void __fastcall ErodePass(char* self, void*, const int32_t* rect, int32_t iterations)
{
    static bool logged = false;
    FirstCall("ErodePass", logged);
    const float line = static_cast<float>(static_cast<double>(Sea(self)) - C(addr::cSTETerrain_ErodeSeaMargin));
    if (!(Global<uint8_t>(addr::cSTETerrain_InverseSqrtTwoGuard) & 1u)) {
        Global<uint32_t>(addr::cSTETerrain_InverseSqrtTwoGuard) |= 1u;
        Global<float>(addr::cSTETerrain_InverseSqrtTwo) =
            static_cast<float>(Global<double>(addr::SC4_DoubleOne) / std::sqrt(Global<double>(addr::SC4_DoubleTwo)));
    }
    const float ratio = static_cast<float>((C(addr::cSTETerrain_ErodeRateHigh) - C(addr::cSTETerrain_ErodeRateLow)) /
                                           (C(addr::cSTETerrain_ErodeSlopeHigh) - C(addr::cSTETerrain_ErodeSlopeLow)));

    // The water under the line, nothing above it; no sediment.
    int32_t z = rect[1];
    if (z <= rect[3]) {
        do {
            int32_t x = rect[0];
            uint32_t offset = Offset(U32(self, field::kVertsX) * static_cast<uint32_t>(z) + static_cast<uint32_t>(x));
            if (x <= rect[2]) {
                do {
                    const uint32_t record = VertexBase(self) + offset;
                    const uint32_t wet = WaterBase(self) + offset;
                    if (F(record) < line)
                        F(wet) = static_cast<float>(static_cast<double>(line) - static_cast<double>(F(record)));
                    else
                        AtAddress<uint32_t>(wet) = 0;
                    AtAddress<uint32_t>(WaterBase(self) + offset + water::kSediment) = 0;
                    ++x;
                    offset += vertex::kSize;
                } while (x <= rect[2]);
            }
            ++z;
        } while (z <= rect[3]);
    }

    if (iterations > 0) {
        do {
            // Rain: by the altitude, above the line only.
            z = rect[1];
            if (z <= rect[3]) {
                do {
                    int32_t x = rect[0];
                    uint32_t offset =
                        Offset(U32(self, field::kVertsX) * static_cast<uint32_t>(z) + static_cast<uint32_t>(x));
                    if (x <= rect[2]) {
                        do {
                            const uint32_t record = VertexBase(self) + offset;
                            if (F(record) > line) {
                                const uint32_t wet = WaterBase(self) + offset;
                                F(wet) = static_cast<float>(C(addr::cSTETerrain_ErodeRain) * static_cast<double>(F(record)) +
                                                            static_cast<double>(F(wet)));
                            }
                            ++x;
                            offset += vertex::kSize;
                        } while (x <= rect[2]);
                    }
                    ++z;
                } while (z <= rect[3]);
            }
            // Flow.
            z = rect[1];
            if (z <= rect[3]) {
                do {
                    int32_t x = rect[0];
                    uint32_t index = U32(self, field::kVertsX) * static_cast<uint32_t>(z) + static_cast<uint32_t>(x);
                    if (x <= rect[2]) {
                        uint32_t offset = Offset(index);
                        do {
                            FlowVertex(self, rect, x, z, index, offset, line, ratio);
                            ++x;
                            ++index;
                            offset += vertex::kSize;
                        } while (x <= rect[2]);
                    }
                    ++z;
                } while (z <= rect[3]);
            }
        } while (--iterations != 0);
    }

    // The rectangle grown by one, cut to the cells (each bound is tested
    // before it is cut), given to the water object: depth = sea level -
    // altitude and no sediment.
    int32_t grown[4] = {rect[0] - 1, rect[1] - 1, rect[2] + 1, rect[3] + 1};
    if (grown[1] < 0)
        grown[1] = 0;
    if (grown[0] < 0)
        grown[0] = 0;
    if (grown[2] > static_cast<int32_t>(U32(self, field::kCellsX)))
        grown[2] = static_cast<int32_t>(U32(self, field::kCellsX));
    if (grown[3] > static_cast<int32_t>(U32(self, field::kCellsZ)))
        grown[3] = static_cast<int32_t>(U32(self, field::kCellsZ));
    This<void>(addr::STE_WaterResetDepths, At<void*>(self, field::kWater), static_cast<void*>(grown));
}

// One vertex against one neighbour in SmoothAll (the eight blocks from
// 0x00746F9C on): where the vertex is higher than the neighbour by more than
// the limit (0x00B4BCF4 straight, 0x00B4BCF0 diagonal), the excess times the
// factor 0x00B4B40C (kept unrounded, unlike 0x00745020) is taken from the
// vertex and added to the neighbour, each unless locked, with the clamps and
// the water depths as everywhere.
inline void SmoothPair(char* self, uint32_t index, uint32_t offset, uint32_t k)
{
    const uint32_t base = VertexBase(self);
    const uint32_t otherOffset = Offset(static_cast<uint32_t>(NeighbourStep(self, k)) + index);
    const double limit = (k & 1u) ? C(addr::cSTETerrain_SmoothDiagonalLimit) : C(addr::cSTETerrain_SmoothStraightLimit);
    const double excess = (static_cast<double>(F(base + offset)) - static_cast<double>(F(base + otherOffset))) - limit;
    if (!(excess > C(addr::SC4_FloatZero)))
        return;
    const double amount = excess * C(addr::cSTETerrain_SmoothFactor);
    const uint32_t record = base + offset;
    if (!Locked(record)) {
        const double lowered = static_cast<double>(F(record)) - amount;
        const uint32_t wet = WaterBase(self) + offset;
        F(record) = static_cast<float>(lowered);
        if (lowered < C(addr::cSTETerrain_MinAllowedHeightValue))
            AtAddress<uint32_t>(record) = Global<uint32_t>(addr::cSTETerrain_MinAllowedHeightValue);
        F(wet) = static_cast<float>(static_cast<double>(Sea(self)) - static_cast<double>(F(record)));
    }
    const uint32_t otherRecord = VertexBase(self) + otherOffset;
    if (!Locked(otherRecord)) {
        const uint32_t wet = WaterBase(self) + otherOffset;
        const double raised = amount + static_cast<double>(F(otherRecord));
        F(otherRecord) = static_cast<float>(raised);
        if (raised > C(addr::cSTETerrain_MaxAllowedHeightValue))
            AtAddress<uint32_t>(otherRecord) = Global<uint32_t>(addr::cSTETerrain_MaxAllowedHeightValue);
        F(wet) = static_cast<float>(static_cast<double>(Sea(self)) - static_cast<double>(F(otherRecord)));
    }
}

// 0x00746F40 (passes): `passes` times over every vertex in index order, each
// against its neighbours inside the grid in the +0x84 table's order; the
// changed altitudes are used at once. Nothing for passes below 1.
void __fastcall SmoothAll(char* self, void*, int32_t passes)
{
    static bool logged = false;
    FirstCall("SmoothAll", logged);
    if (passes <= 0)
        return;
    do {
        if (U32(self, field::kVertexCount) != 0) {
            uint32_t index = 0;
            uint32_t offset = 0;
            do {
                const uint32_t stride = U32(self, field::kVertsX);
                const uint8_t bits = NeighbourBits(self, index % stride, index / stride);
                for (uint32_t k = 0; k < 8; ++k)
                    if (bits & (1u << k))
                        SmoothPair(self, index, offset, k);
                ++index;
                offset += vertex::kSize;
            } while (index < U32(self, field::kVertexCount));
        }
    } while (--passes != 0);
}

// 0x00747B20 (kind, strong): kind 0: ErodePass over the whole map (+0x40)
// `runs` times with `iterations` each, then SmoothAll; the numbers from
// 0x00B0D5A0, 0x00B0D5A4 and 0x00B0D5EC, or with the strong byte from
// 0x00B4E1C8, 0x00B4E1C4 and 0x00B4E1C0. Kind 1: SmoothAll with 0x00B0D594.
// Any other kind: nothing. The numbers are read once, before the work.
void __fastcall Erode(char* self, void*, uint32_t kind, uint32_t strong)
{
    static bool logged = false;
    FirstCall("Erode", logged);
    if (kind == 0) {
        uint32_t smooth, runs, iterations;
        if ((strong & 0xFFu) != 0) {
            smooth = Global<uint32_t>(addr::SC4RegionScreen_Tuning4E1C0);
            runs = Global<uint32_t>(addr::SC4RegionScreen_Tuning4E1C0 + 8);
            iterations = Global<uint32_t>(addr::SC4RegionScreen_Tuning4E1C0 + 4);
        } else {
            smooth = Global<uint32_t>(addr::cSTETerrain_ErodeSmoothPasses);
            runs = Global<uint32_t>(addr::cSTETerrain_ErodeRuns);
            iterations = Global<uint32_t>(addr::cSTETerrain_ErodeIterations);
        }
        for (; runs != 0; --runs)
            ErodePass(self, nullptr, &At<int32_t>(self, field::kCellRect), static_cast<int32_t>(iterations));
        SmoothAll(self, nullptr, static_cast<int32_t>(smooth));
    } else if (kind == 1) {
        SmoothAll(self, nullptr, static_cast<int32_t>(Global<uint32_t>(addr::cSTETerrain_SmoothOnlyPasses)));
    }
}

// --- The flood ---------------------------------------------------------------

// 0x00744DE0 (rect): the water depth of every vertex of a rectangle (both
// ends included) copied to the array +0xE0, by index. No range test.
void SaveWaterDepths(char* self, const int32_t* rect)
{
    for (int32_t z = rect[1]; z <= rect[3]; ++z) {
        int32_t x = rect[0];
        uint32_t index = U32(self, field::kVertsX) * static_cast<uint32_t>(z) + static_cast<uint32_t>(x);
        for (; x <= rect[2]; ++x, ++index)
            AtAddress<uint32_t>(U32(self, field::kSavedDepths) + 4u * index) =
                AtAddress<uint32_t>(WaterBase(self) + Offset(index));
    }
}

// 0x00744D60 (rect): the other way.
void RestoreWaterDepths(char* self, const int32_t* rect)
{
    for (int32_t z = rect[1]; z <= rect[3]; ++z) {
        int32_t x = rect[0];
        uint32_t index = U32(self, field::kVertsX) * static_cast<uint32_t>(z) + static_cast<uint32_t>(x);
        for (; x <= rect[2]; ++x, ++index)
            AtAddress<uint32_t>(WaterBase(self) + Offset(index)) =
                AtAddress<uint32_t>(U32(self, field::kSavedDepths) + 4u * index);
    }
}

// The flood's message with the window, and the window redisplayed (the end
// of both branches of FloodStep).
inline void ShowFloodWindow(char* self)
{
    int32_t* const window = &At<int32_t>(self, field::kFloodRect);
    const uint32_t columns = (static_cast<uint32_t>(window[0]) << 16) + static_cast<uint32_t>(window[2]);
    const uint32_t rows = (static_cast<uint32_t>(window[1]) << 16) + static_cast<uint32_t>(window[3]);
    Cdecl<void>(addr::cSTETerrain_PostMessage, message::kFloodWindow, columns, rows, 0u, 0u);
    Virtual<void>(self, slot::kRedisplay, 0u, 1u, static_cast<void*>(window), 0u);
}

// One vertex of FloodStep's loop (0x00746900 to 0x007469F4).
inline void FloodVertex(char* self, uint32_t index, uint32_t offset)
{
    if (At<int32_t>(self, field::kFloodSteps) > Global<int32_t>(addr::cSTETerrain_FloodDecayStart)) {
        const uint32_t wet = WaterBase(self) + offset;
        const double part = (static_cast<double>(F(wet)) - static_cast<double>(F(U32(self, field::kSavedDepths) + 4u * index))) *
                            C(addr::cSTETerrain_FloodDecay);
        F(wet) = static_cast<float>(static_cast<double>(F(wet)) - part);
    }
    const uint32_t stride = U32(self, field::kVertsX);
    const uint32_t row = index / stride;
    const uint32_t column = index - stride * row;
    int32_t other = -1; // -1 on the map's edge: the water leaves
    double flow;
    if (column == 0 || column == U32(self, field::kCellsX) || row == 0 || row == U32(self, field::kCellsZ)) {
        flow = static_cast<double>(F(WaterBase(self) + offset));
    } else {
        const int32_t direction = LowestNeighbour(self, index);
        if (direction < 0)
            return;
        other = NeighbourStep(self, static_cast<uint32_t>(direction)) + static_cast<int32_t>(index);
        const uint32_t wetBase = WaterBase(self);
        const uint32_t base = VertexBase(self);
        const uint32_t otherOffset = Offset(static_cast<uint32_t>(other));
        const double surface = static_cast<double>(F(base + offset)) + static_cast<double>(F(wetBase + offset));
        const double otherSurface =
            static_cast<double>(F(base + otherOffset)) + static_cast<double>(F(wetBase + otherOffset));
        flow = (surface - otherSurface) * C(addr::SC4_FloatHalf);
        if (flow > static_cast<double>(F(wetBase + offset)))
            flow = static_cast<double>(F(WaterBase(self) + offset));
    }
    if (!(flow > C(addr::SC4_FloatZero)))
        return;
    if (other >= 0) {
        const uint32_t otherOffset = Offset(static_cast<uint32_t>(other));
        if (F(VertexBase(self) + otherOffset) > Sea(self)) {
            const uint32_t otherWet = WaterBase(self) + otherOffset;
            F(otherWet) = static_cast<float>(flow + static_cast<double>(F(otherWet)));
        }
    }
    const uint32_t wet = WaterBase(self) + offset;
    F(wet) = static_cast<float>(static_cast<double>(F(wet)) - flow);
}

// 0x007467F0: see the top of the file. The step from which the water sinks
// is a static of the game's (0x00B4BD7C, guard bit 0 of 0x00B4BD80): the
// floor of the float 0x00AB3CC4 (350.0) as the compiler folded it: 350, or
// 351 should the float equal 351.
void __fastcall FloodStep(char* self, void*)
{
    static bool logged = false;
    FirstCall("FloodStep", logged);
    if (!(Global<uint8_t>(addr::cSTETerrain_FloodDecayStartGuard) & 1u)) {
        Global<uint32_t>(addr::cSTETerrain_FloodDecayStartGuard) |= 1u;
        int32_t start = 350;
        if (C(addr::cSTETerrain_FloodDecayStartFloat) == 351.0)
            start = 351;
        Global<int32_t>(addr::cSTETerrain_FloodDecayStart) = start;
    }
    if (At<int32_t>(self, field::kFloodSteps) > flood::kLastStep) {
        At<uint8_t>(self, field::kFlooding) = 0;
        RestoreWaterDepths(self, &At<int32_t>(self, field::kCellRect));
        ShowFloodWindow(self);
        return;
    }
    int32_t* const window = &At<int32_t>(self, field::kFloodRect);
    int32_t z = window[1];
    if (z < window[3]) {
        do {
            int32_t x = window[0];
            uint32_t index = U32(self, field::kVertsX) * static_cast<uint32_t>(z) + static_cast<uint32_t>(x);
            if (x < window[2]) {
                uint32_t offset = Offset(index);
                do {
                    FloodVertex(self, index, offset);
                    ++x;
                    ++index;
                    offset += vertex::kSize;
                } while (x < window[2]);
            }
            ++z;
        } while (z < window[3]);
    }
    // The window grown by one, cut to the cells (each bound is tested before
    // it is cut).
    const int32_t z0 = window[1] - 1, x0 = window[0] - 1, x1 = window[2] + 1, z1 = window[3] + 1;
    window[1] = z0;
    window[0] = x0;
    window[2] = x1;
    window[3] = z1;
    if (z0 < 0)
        window[1] = 0;
    if (x0 < 0)
        window[0] = 0;
    if (x1 > static_cast<int32_t>(U32(self, field::kCellsX)))
        window[2] = static_cast<int32_t>(U32(self, field::kCellsX));
    if (z1 > static_cast<int32_t>(U32(self, field::kCellsZ)))
        window[3] = static_cast<int32_t>(U32(self, field::kCellsZ));
    ++At<int32_t>(self, field::kFloodSteps);
    ShowFloodWindow(self);
}

// Slot 56 (0x00747720), StartFlood(): every water depth saved; the counter
// 0 and the window 20 to 40 both ways; 500 (0x00AB3CCC) added to the depth
// of each vertex of the window (the far ends not included; no test against
// the map's size); +0xD4 set; the first step (the game jumps to it).
void __fastcall StartFlood(char* self, void*)
{
    static bool logged = false;
    FirstCall("StartFlood", logged);
    SaveWaterDepths(self, &At<int32_t>(self, field::kCellRect));
    int32_t* const window = &At<int32_t>(self, field::kFloodRect);
    At<int32_t>(self, field::kFloodSteps) = 0;
    window[1] = flood::kWindowLow;
    window[0] = flood::kWindowLow;
    window[2] = flood::kWindowHigh;
    window[3] = flood::kWindowHigh;
    int32_t z = flood::kWindowLow;
    do {
        int32_t x = window[0];
        uint32_t offset = Offset(U32(self, field::kVertsX) * static_cast<uint32_t>(z) + static_cast<uint32_t>(x));
        if (x < window[2]) {
            do {
                const uint32_t wet = WaterBase(self) + offset;
                F(wet) = static_cast<float>(static_cast<double>(F(wet)) + C(addr::cSTETerrain_FloodStartDepth));
                ++x;
                offset += vertex::kSize;
            } while (x < window[2]);
        }
        ++z;
    } while (z < window[3]);
    At<uint8_t>(self, field::kFlooding) = 1;
    FloodStep(self, nullptr);
}

// --- The water distance map --------------------------------------------------

// 0x007440C0 (rect): see the top of the file. The area is (z1 - z0) x
// (x1 - x0) of the joined rectangle, 32-bit and signed. The spreader gets
// the rows, the byte to start from (0 with any water; with none it is 0xFF
// and nothing is spread) and how many steps (the far byte - that - 1); the
// game pushes dwords whose upper bytes are whatever was there, of which
// 0x005E5180 reads the low bytes only.
void __fastcall UpdateWaterDistances(char* self, void*, const int32_t* rect)
{
    static bool logged = false;
    FirstCall("UpdateWaterDistances", logged);
    int32_t* const changed = &At<int32_t>(self, field::kChanged);
    changed[0] = changed[0] < rect[0] ? changed[0] : rect[0];
    changed[1] = changed[1] < rect[1] ? changed[1] : rect[1];
    changed[2] = changed[2] > rect[2] ? changed[2] : rect[2];
    changed[3] = changed[3] <= rect[3] ? rect[3] : changed[3];
    const int32_t area = static_cast<int32_t>(static_cast<uint32_t>(changed[3] - changed[1]) *
                                              static_cast<uint32_t>(changed[2] - changed[0]));
    if (area < 100)
        return;
    At<uint32_t>(self, field::kElapsed) = 0;
    const int32_t* const empty = &Global<int32_t>(addr::cSTETerrain_EmptyRect);
    changed[0] = empty[0];
    changed[1] = empty[1];
    changed[2] = empty[2];
    changed[3] = empty[3];
    const uint8_t farthest = static_cast<uint8_t>(Global<uint8_t>(addr::cSTETerrain_WaterDistanceLimit) + 2u);
    uint8_t nearest = 0xFF;
    At<uint32_t>(self, field::kWaterVertices) = 0;
    if (U32(self, field::kVertsZ) != 0) {
        uint32_t row = 0;
        uint32_t rowOffset = 0;
        do {
            const uint32_t stride = U32(self, field::kVertsX);
            uint32_t offset = Offset(stride * row);
            uint32_t column = 0;
            if (stride != 0) {
                do {
                    const float depth = F(WaterBase(self) + offset);
                    const uint32_t cells = AtAddress<uint32_t>(U32(self, field::kRows) + rowOffset);
                    if (depth > Global<float>(addr::SC4_FloatZero)) {
                        AtAddress<uint8_t>(cells + column) = 0;
                        ++At<uint32_t>(self, field::kWaterVertices);
                    } else {
                        AtAddress<uint8_t>(cells + column) = farthest;
                    }
                    ++column;
                    offset += vertex::kSize;
                } while (column < U32(self, field::kVertsX));
            }
            ++row;
            rowOffset += 0xCu;
        } while (row < U32(self, field::kVertsZ));
    }
    if (At<int32_t>(self, field::kWaterVertices) > 0)
        nearest = 0;
    if (nearest >= farthest)
        return;
    Cdecl<void>(addr::SC4DistanceRows_Spread, static_cast<void*>(self + field::kRows), static_cast<uint32_t>(nearest),
                static_cast<uint32_t>(static_cast<uint8_t>(farthest - nearest - 1u)));
}

// --- Installation ------------------------------------------------------------

struct Detour {
    std::uintptr_t address;
    void* ours;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

// Every direct call site of each (scan of all direct calls in the exe),
// checked for what it uses after the call:
// 0x00747B20: 0x0074826A (0x00748130: pushes the strong byte and the kind,
//   loads ECX from ESI next), 0x0074A4CD (DoMessage: pushes 0 and data 1,
//   loads ECX from EDI next).
// 0x00746AD0: 0x00747A20 (the brushes' erosion layer 0x007479B0: a loop that
//   loads the iteration count, pushes it and the rectangle and sets ECX from
//   EBX each time round), 0x00747B74 (Erode, ours).
// 0x00746F40: 0x00747B84, 0x00747B9C (Erode, ours; the game's leaves 0 or
//   the terrain in ECX, which nothing uses).
// 0x007467F0: 0x0074A3F7 (the timer's work: pushes EDI and loads its
//   arguments from the stack next), 0x007477B5 (the jump from slot 56).
// 0x007440C0: 0x007490B9 (loads EAX from [ESI + 0xBC] next), 0x0074A45F
//   (returns next).
// All are thiscall with the arguments on the stack; none keeps a value in
// EAX, ECX or EDX across the call.
const Detour kDetours[] = {
    OPENSC4_DETOUR(addr::cSTETerrain_Erode, Erode),
    OPENSC4_DETOUR(addr::cSTETerrain_ErodePass, ErodePass),
    OPENSC4_DETOUR(addr::cSTETerrain_SmoothAll, SmoothAll),
    OPENSC4_DETOUR(addr::cSTETerrain_FloodStep, FloodStep),
    OPENSC4_DETOUR(addr::cSTETerrain_UpdateWaterDistances, UpdateWaterDistances),
};

#undef OPENSC4_DETOUR

void* g_originals[std::size(kDetours)];

constexpr int kStartFloodSlot = 56;

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSTETerrain_vftable);
    if (vtable[kStartFloodSlot] != addr::cSTETerrain_StartFlood) {
        log::Write("terrain erode: slot %d of %08X holds %08X, expected %08X; not installing", kStartFloodSlot,
                   static_cast<unsigned>(addr::cSTETerrain_vftable), static_cast<unsigned>(vtable[kStartFloodSlot]),
                   static_cast<unsigned>(addr::cSTETerrain_StartFlood));
        return false;
    }
    for (const Detour& detour : kDetours) {
        if (!addr_check::Find(detour.address)) {
            log::Write("terrain erode: no address entry for %08X; not installing",
                       static_cast<unsigned>(detour.address));
            return false;
        }
    }
    for (std::size_t i = 0; i < std::size(kDetours); ++i) {
        if (!hook::Install(*addr_check::Find(kDetours[i].address), kDetours[i].ours, &g_originals[i])) {
            log::Write("terrain erode: detour %u of %u failed (%08X); the earlier ones stay", static_cast<unsigned>(i),
                       static_cast<unsigned>(std::size(kDetours)), static_cast<unsigned>(kDetours[i].address));
            return false;
        }
    }
    std::uintptr_t* const entry = vtable + kStartFloodSlot;
    DWORD old = 0;
    if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
        return false;
    *entry = reinterpret_cast<std::uintptr_t>(&StartFlood);
    VirtualProtect(entry, sizeof(*entry), old, &old);
    log::Write("terrain erode: slot 56 of %08X and %u functions (detours) are ours",
               static_cast<unsigned>(addr::cSTETerrain_vftable), static_cast<unsigned>(std::size(kDetours)));
    return true;
}

const bool g_registered = registry::Register(
    {"simulators.terrain.erode", "simulators/terrain", registry::Kind::Detour, registry::Phase::PostAppInit,
     addr::cSTETerrain_Erode,
     "Our cSTETerrain erosion (the erosion message's worker, the rain and flow pass, the whole-map smoothing), flood "
     "(slot 56 StartFlood and the step) and water distance map, by detours",
     false, Install});

} // namespace
} // namespace opensc4::terrain_sim
