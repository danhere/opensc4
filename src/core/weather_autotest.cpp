// The weather simulator's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <cISC4City.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace opensc4::autotest {
namespace {

// Weather (cSC4WeatherSimulator, build 641 offsets; docs/weather.md):
// the moisture maps (+0x74, +0xDC, +0xE8, rows of bytes), the water
// cells (+0x80, rows of bits), the water bodies (+0x8C), the cells
// left this pass (+0xC8), the season and month, temperature and
// humidity, the wind (+0x118 to +0x144) as float bits.
void Fingerprint(cISC4City* city, std::string& line)
{
    void* const weather = city->GetWeatherSimulator();
    char weatherText[256] = " | weather none";
    if (weather && game::IsBuild641()) {
        auto rowsSum = [weather](std::size_t offset) {
            uint32_t h = 2166136261u;
            char* const* const begin = At<char* const*>(weather, offset);
            char* const* const end = At<char* const*>(weather, offset + 4);
            for (char* const* row = begin; row && row < end; row += 3)
                for (const char* b = row[0]; b < row[1]; ++b)
                    h = (h ^ static_cast<uint8_t>(*b)) * 16777619u;
            return h;
        };
        uint32_t bits = 2166136261u;
        {
            const int32_t countX = At<int32_t>(weather, 0x2C), countZ = At<int32_t>(weather, 0x30);
            const char* const rows = At<const char*>(weather, 0x80);
            const int32_t rowCount = rows ? static_cast<int32_t>((At<const char*>(weather, 0x84) - rows) / 20) : 0;
            for (int32_t x = 0; x < countX && x < rowCount; ++x) {
                const uint32_t* const data = *reinterpret_cast<uint32_t* const*>(rows + 20 * x);
                const int32_t start = *reinterpret_cast<const int32_t*>(rows + 20 * x + 4);
                for (int32_t z = 0; z < countZ; ++z) {
                    const int32_t bit = start + z;
                    const uint32_t set = (data[bit >> 5] >> (bit & 31)) & 1;
                    bits = (bits ^ set) * 16777619u;
                }
            }
        }
        auto f = [weather](std::size_t offset) { return At<uint32_t>(weather, offset); };
        const char* const bodies = At<const char*>(weather, 0x8C);
        std::snprintf(weatherText, sizeof weatherText,
                      " | weather %08X %08X %08X %08X bodies %d left %d season %d month %d temp %08X hum %08X "
                      "wind %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X",
                      rowsSum(0x74), rowsSum(0xDC), rowsSum(0xE8), bits,
                      bodies ? static_cast<int>((At<const char*>(weather, 0x90) - bodies) / 16) : 0,
                      At<int32_t>(weather, 0xC8), At<int32_t>(weather, 0x60), At<int32_t>(weather, 0x160), f(0x70),
                      f(0xF4), f(0x11C), f(0x120), f(0x124), f(0x128), f(0x12C), f(0x130), f(0x134), f(0x138),
                      f(0x13C), f(0x140), f(0x144));
    }
    line += weatherText;
}

// OPENSC4_TEST_WEATHER_YEAR=<n>: on the n-th day, run the weather
// simulator's month handler as for January (0x00729630 with 1, build
// 641; docs/weather.md): the water-body search runs then only.
void Year(const Day& day)
{
    if (OnRunDay(day, "OPENSC4_TEST_WEATHER_YEAR") && game::IsBuild641()) {
        if (void* const weather = day.city->GetWeatherSimulator()) {
            using MonthFn = void(__thiscall*)(void* self, int32_t month, int32_t unused);
            reinterpret_cast<MonthFn>(0x00729630)(weather, 1, 0);
            log::Write("autotest: weather January handler run on day %d", day.day);
        }
    }
}

const FingerprintPart kFingerprint{"weather", 60, &Fingerprint};
const DayHook kYear{"weather.year", 50, &Year};

} // namespace
} // namespace opensc4::autotest
