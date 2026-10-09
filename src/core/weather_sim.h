// Our cSC4WeatherSimulator (build 641): what the parts share (docs/weather.md).
#pragma once

#include "core/game_access.h"

#include <cstddef>
#include <cstdint>

namespace opensc4::weather_sim {

// Fields of cSC4WeatherSimulator (offsets from the object's start; 0x168
// bytes). The interfaces: +0x4 (a listener, IID 0xE98B2F57), +0x8
// cIGZMessageTarget2, +0xC cIGZSerializable.
namespace field {
constexpr std::size_t kInitialised = 0x10;   // uint8
constexpr std::size_t kRefs = 0x14;
constexpr std::size_t kLoaded = 0x18;        // uint8: Read ran (the maps came from the save)
constexpr std::size_t kSettled = 0x19;       // uint8: DoPostCityInit ran its first full pass
constexpr std::size_t kCity = 0x1C;
constexpr std::size_t kSimulators = 0x20;
constexpr std::size_t kTerrain = 0x28;       // service 0x00B43CF4
constexpr std::size_t kCellsX = 0x2C;        // the weather grid (one cell per 16 m)
constexpr std::size_t kCellsZ = 0x30;
constexpr std::size_t kCellWidthX = 0x34;    // float, 16
constexpr std::size_t kCellWidthZ = 0x38;
constexpr std::size_t kInverseWidthX = 0x3C; // float, 1/16
constexpr std::size_t kInverseWidthZ = 0x40;
constexpr std::size_t kSizeX = 0x44;         // float, the city's size in metres
constexpr std::size_t kSizeZ = 0x48;
constexpr std::size_t kCityCellsX = 0x4C;    // city cells per weather cell
constexpr std::size_t kCityCellsZ = 0x50;
constexpr std::size_t kTickDivisor = 0x54;   // cells per tick = cells / this
constexpr std::size_t kSpreadRadius = 0x5C;
constexpr std::size_t kSeason = 0x60;
constexpr std::size_t kBaseTemperature = 0x64; // float
constexpr std::size_t kTemperatureSwing = 0x68;
constexpr std::size_t kTemperatureScale = 0x6C;
constexpr std::size_t kTemperature = 0x70;   // float, this month's
constexpr std::size_t kCellMoisture = 0x74;  // rows of bytes: each cell's own moisture
constexpr std::size_t kWaterBits = 0x80;     // rows of bits (20-byte rows): water body cells
constexpr std::size_t kWaterBodies = 0x8C;   // vector of {x0, z0, x1, z1}
constexpr std::size_t kTractIterator = 0x98; // the cell walk of the tick (rect at +0x9C)
constexpr std::size_t kCellsPerTick = 0xC8;
constexpr std::size_t kBodyMargin = 0xCC;
constexpr std::size_t kWaterFactor = 0xD0;   // float
constexpr std::size_t kHeightFactor = 0xD4;
constexpr std::size_t kWindFactor = 0xD8;
constexpr std::size_t kMoisture = 0xDC;      // rows of bytes: the moisture map (slots 17-19)
constexpr std::size_t kManual = 0xE8;        // rows of bytes: manual additions (slot 20)
constexpr std::size_t kHumidity = 0xF4;      // float, this month's
constexpr std::size_t kHumidityBase = 0xF8;
constexpr std::size_t kHumiditySwing = 0xFC;
constexpr std::size_t kHumidityScale = 0x100;
constexpr std::size_t kSlopeScale = 0x104;
constexpr std::size_t kRainFactor = 0x108;
constexpr std::size_t kSlopeLimit = 0x10C;
constexpr std::size_t kSeasonWind = 0x118;   // float[3]: speed, x, z for a new season
constexpr std::size_t kWindSpeed = 0x124;    // float: today's wind (slot 22)
constexpr std::size_t kWindX = 0x128;        // its direction
constexpr std::size_t kWindZ = 0x12C;
constexpr std::size_t kMonthStartSpeed = 0x130; // the month's start and target winds
constexpr std::size_t kMonthStartX = 0x134;
constexpr std::size_t kMonthStartZ = 0x138;
constexpr std::size_t kMonthEndSpeed = 0x13C;
constexpr std::size_t kMonthEndX = 0x140;
constexpr std::size_t kMonthEndZ = 0x144;
constexpr std::size_t kRandom = 0x148;
constexpr std::size_t kMonth = 0x160;
constexpr std::size_t kWindSpread = 0x164;   // float
} // namespace field

// __ftol2: truncation toward zero, the low 32 bits.
inline int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// The bit of cell (x, z) in a row-of-bits vector (20-byte rows: words, bit
// offset), as the game addresses it (floor division for negatives).
inline uint32_t* BitWord(void* rows, int32_t x, int32_t z, uint32_t& mask)
{
    char* const row = static_cast<char*>(rows) + 20 * x;
    uint32_t* const words = *reinterpret_cast<uint32_t**>(row);
    const int32_t bit = *reinterpret_cast<int32_t*>(row + 4) + z;
    int32_t word = bit >> 5;
    int32_t shift = bit & 31;
    mask = 1u << shift;
    return words + word;
}

void FirstCall(const char* name, bool& logged);

} // namespace opensc4::weather_sim
