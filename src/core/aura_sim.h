// Our cSC4AuraSimulator (build 641): what the parts share (docs/aura.md).
#pragma once

#include "core/game_access.h"

#include <cstddef>
#include <cstdint>

namespace opensc4::aura_sim {

// Fields of cSC4AuraSimulator (offsets from the object's start; 0x160 bytes).
namespace field {
constexpr std::size_t kRefs = 0x10;
constexpr std::size_t kInitialised = 0x0C;      // uint8
constexpr std::size_t kRandom = 0x18;           // SC4Random (the riot draw)
constexpr std::size_t kCity = 0x30;
constexpr std::size_t kMessageServer = 0x34;
constexpr std::size_t kService43D68 = 0x38;
constexpr std::size_t kGridSize = 0x3C;         // passed to the grids' slot 9
constexpr std::size_t kAuraGrid = 0x40;         // int8: the aura (slots 5, 12)
constexpr std::size_t kEffectGrid = 0x44;       // int8: the month's sum of effects, cell size
constexpr std::size_t kSmoothedGrid = 0x48;     // int8: +0x44 averaged and smoothed
constexpr std::size_t kTransientGrid = 0x4C;    // int8: transient effects (slots 6, 7, 13)
constexpr std::size_t kLandmarkSources = 0x50;  // int8: property 0xCA5B9305 spread
constexpr std::size_t kParkSources = 0x54;      // int16: property 0x27812850 spread
constexpr std::size_t kParkMap = 0x58;          // int16: +0x54 times park funding (slot 14)
constexpr std::size_t kDepartmentGrids = 0x5C;  // map<uint32_t department, int16 grid*>: property 0x2781284F
constexpr std::size_t kDepartmentGridCount = 0x60;
constexpr std::size_t kLandmarkMap = 0x68;      // int16: copy of +0x6C at the end of state 3 (slot 15)
constexpr std::size_t kDepartmentSum = 0x6C;    // int16: the department grids times their funding
constexpr std::size_t kState = 0x70;
constexpr std::size_t kPassStart = 0x74;        // the date state 1 ran
constexpr std::size_t kLotIterator = 0x78;      // list iterator over the lots (state 5)
constexpr std::size_t kDepartmentKey = 0x7C;    // state 3's position in the map; -1 = start
constexpr std::size_t kPassFlag = 0x80;
constexpr std::size_t kMayorRating = 0x84;
constexpr std::size_t kRatingOffset = 0x88;     // service 0x00B43D90, value 0xAA5B8407
constexpr std::size_t kFundingBase = 0x94;      // float[12], -1 until the first pass
constexpr std::size_t kLowTracts = 0xC4;        // vector of {x, z, 1}: begin, end, capacity
constexpr std::size_t kClusters = 0xD0;         // list of {next, prev, x, z, count}
constexpr std::size_t kStrikeFlag = 0xD4;       // uint8
constexpr std::size_t kParkCurve = 0xD8;        // curves (SC4Curve*), from the tuning
constexpr std::size_t kPollutionCurves = 0xDC;  // vector of curves, one per pollution grid
constexpr std::size_t kCoverageCurve1 = 0xE8;
constexpr std::size_t kCoverageCurve2 = 0xEC;
constexpr std::size_t kService43D70Curve = 0xF0;
constexpr std::size_t kService43DA8Curve1 = 0xF4;
constexpr std::size_t kService43DA8Curve2 = 0xF8;
constexpr std::size_t kFundingCurve = 0xFC;
constexpr std::size_t kHeightCurve = 0x100;
constexpr std::size_t kSmoothing = 0x104;       // float, 0.6
constexpr std::size_t kTransientDecay = 0x108;  // float, 0.4
constexpr std::size_t kTuningBase = 0x10C;      // int32[3]: the effect table's bases
constexpr std::size_t kTransientValue = 0x118;  // float[4]
constexpr std::size_t kTransientRadius = 0x128; // float[4]
constexpr std::size_t kClusterDistance = 0x138;
constexpr std::size_t kLowThreshold = 0x13C;
constexpr std::size_t kClusterMinimum = 0x140;
constexpr std::size_t kRiotCurve = 0x144;
constexpr std::size_t kBestX = 0x148;           // best tract: x, z (int32), value (int16)
constexpr std::size_t kBestZ = 0x14C;
constexpr std::size_t kBestValue = 0x150;
constexpr std::size_t kWorstX = 0x154;
constexpr std::size_t kWorstZ = 0x158;
constexpr std::size_t kWorstValue = 0x15C;
} // namespace field

// One entry of the effect table at 0x00B0D150 (a zero id ends it).
struct EffectEntry {
    uint32_t id;
    int32_t tuningIndex;
    int32_t department;
    int32_t base;
    int32_t value;
};

inline EffectEntry* EffectTable()
{
    return &Global<EffectEntry>(addr::SC4_AuraEffectTable);
}

// A grid's cells by row (slot 24, or 25 for the const view): a vector of
// row vectors, each begin, end, capacity, so row x starts at [3 * x].
template <typename T>
T* const* Rows(void* grid, std::size_t slot = 0x60)
{
    return *Virtual<T* const* const*>(grid, slot);
}

// __ftol2: truncation toward zero, the low 32 bits.
inline int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// Clamped to an aura value: -127 to 127 (-128 means "none").
inline int8_t ClampAura(int32_t value)
{
    if (value > 127)
        return 127;
    if (value < -127)
        return -127;
    return static_cast<int8_t>(value);
}

void FirstCall(const char* name, bool& logged);

} // namespace opensc4::aura_sim
