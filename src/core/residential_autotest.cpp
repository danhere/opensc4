// The residential (education and health) simulator's part of the autotest
// (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"

#include <cISC4City.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::autotest {
namespace {

// A grid's cells (the game's SC4SimGrid classes: +0x38 holds one 12-byte
// vector per x, +0x20/+0x24 the counts), `size` bytes each.
uint32_t Cells(char* grid, uint32_t size, uint32_t h)
{
    if (!grid || !At<char*>(grid, 0x38))
        return h;
    const uint32_t nx = At<uint32_t>(grid, 0x20), nz = At<uint32_t>(grid, 0x24);
    for (uint32_t x = 0; x < nx; ++x) {
        const uint8_t* const row = At<const uint8_t*>(At<char*>(grid, 0x38), 12 * x);
        for (uint32_t i = 0; i < nz * size; ++i)
            h = (h ^ row[i]) * 16777619u;
    }
    return h;
}

// OPENSC4_TEST_RESIDENTIAL_QUERIES=1: the queries only the UI makes, each day,
// into a checksum: the ratings (slots 12, 13), building counts (46, 48),
// the largest EQ (50), the average EQ and HQ grids with their ranges (21,
// 22; each a new grid, released), the new residents' ages (42) and EQ and
// HQ by wealth at three cells (37, 38), the auto budgets (52, 54), and for
// the first school and hospital: coverage (45), current capacity (28),
// ideal funding (58) and auto budget (56).
uint32_t Queries(void* residential)
{
    uint32_t values[64] = {};
    uint32_t n = 0;
    const auto Add = [&](uint32_t v) {
        if (n < 64)
            values[n++] = v;
    };
    const auto Bits = [](float f) {
        uint32_t b = 0;
        std::memcpy(&b, &f, 4);
        return b;
    };
    float out = 0.0f;
    Virtual<float*>(residential, 0x30, &out);
    Add(Bits(out));
    Virtual<float*>(residential, 0x34, &out);
    Add(Bits(out));
    Add(Virtual<uint32_t>(residential, 0xB8));
    Add(Virtual<uint32_t>(residential, 0xC0));
    Add(Virtual<uint32_t>(residential, 0xC8));
    for (const std::size_t slot : {0x54u, 0x58u}) {
        void* grid = nullptr;
        float low = 0.0f, high = 0.0f;
        Add(Virtual<uint32_t>(residential, slot, &grid, &low, &high) & 0xFF);
        Add(Bits(low));
        Add(Bits(high));
        Add(Cells(static_cast<char*>(grid), 4, 2166136261u));
        if (grid)
            Virtual<uint32_t>(grid, 0x8);
    }
    for (uint32_t wealth = 1; wealth <= 3; ++wealth) {
        Add(Virtual<uint8_t>(residential, 0xA8, wealth));
        for (const uint32_t cell : {0u, 40u, 80u}) {
            Add(Bits(static_cast<float>(Virtual<double>(residential, 0x94, cell, cell, wealth))));
            Add(Bits(static_cast<float>(Virtual<double>(residential, 0x98, cell, cell, wealth))));
        }
    }
    Add(Virtual<uint8_t>(residential, 0xD0));
    Add(Virtual<uint8_t>(residential, 0xD8));
    for (const std::size_t lists : {0x1A8u, 0x1B4u}) {
        void* occupant = nullptr;
        char** const begin = At<char**>(residential, lists);
        char** const end = At<char**>(residential, lists + 4);
        for (char** list = begin; list != end && !occupant; ++list) {
            void** const head = reinterpret_cast<void**>(*list);
            if (head && head[0] != head)
                occupant = static_cast<void**>(head[0])[2];
        }
        if (!occupant) {
            Add(0xFFFFFFFFu);
            continue;
        }
        const float effectiveness = 1.0f;
        float rangeX = 0.0f, rangeZ = 0.0f, funding = 0.0f;
        uint32_t capacity = 0, units = 0;
        Add(Virtual<uint8_t>(residential, 0xB4, occupant, &effectiveness, &rangeX, &rangeZ));
        Add(Bits(rangeX));
        Add(Bits(rangeZ));
        Virtual<uint32_t>(residential, 0x70, occupant, &capacity, &units);
        Add(capacity);
        Add(units);
        Add(Virtual<uint8_t>(residential, 0xE8, occupant, &funding));
        Add(Bits(funding));
        Add(Virtual<uint8_t>(residential, 0xE0, occupant));
    }
    return BytesChecksum(values, sizeof values);
}

// Residential (the city's slot 68, build 641 offsets and slots;
// docs/residential.md): population (slot 16), the state +0x94 and its
// floats, the flag +0xA0, both strikes, global EQ and HQ (slots 32, 33),
// the step +0x348, the cell iterator +0x328, and checksums of the cells of
// the population (uint16), EQ and HQ (float) grids.
void Fingerprint(cISC4City* city, std::string& line)
{
    char residentialText[160] = " | res none";
    if (void* const residential = Virtual<void*>(city, 0x110); residential && game::IsBuild641()) {
        const float eq = static_cast<float>(Virtual<double>(residential, 0x80));
        const float hq = static_cast<float>(Virtual<double>(residential, 0x84));
        uint32_t eqBits = 0, hqBits = 0;
        std::memcpy(&eqBits, &eq, 4);
        std::memcpy(&hqBits, &hq, 4);
        uint32_t population = 2166136261u, quality = 2166136261u;
        for (int i = 0; i < 3; ++i) {
            population = Cells(At<char*>(residential, 0x48 + 4 * i), 2, population);
            quality = Cells(At<char*>(residential, 0x7C + 4 * i), 4, quality);
            quality = Cells(At<char*>(residential, 0x88 + 4 * i), 4, quality);
        }
        std::snprintf(residentialText, sizeof residentialText,
                      " | res %d st %u %08X %08X %u strike %u%u eq %08X hq %08X step %lld it %u %d %d grids %08X %08X",
                      Virtual<int32_t>(residential, 0x40), At<uint32_t>(residential, 0x94), At<uint32_t>(residential, 0x98),
                      At<uint32_t>(residential, 0x9C), At<uint8_t>(residential, 0xA0), At<uint8_t>(residential, 0x178),
                      At<uint8_t>(residential, 0x118), eqBits, hqBits,
                      static_cast<long long>(At<int64_t>(residential, 0x348)), At<uint8_t>(residential, 0x328),
                      At<int32_t>(residential, 0x32C), At<int32_t>(residential, 0x330), population, quality);
    }
    line += residentialText;
    if (EnvInt("OPENSC4_TEST_RESIDENTIAL_QUERIES", 0) == 1 && game::IsBuild641())
        if (void* const residential = Virtual<void*>(city, 0x110)) {
            char text[24];
            std::snprintf(text, sizeof text, " q %08X", Queries(residential));
            line += text;
        }
}

// OPENSC4_TEST_RESIDENTIAL_MONTH=<n>: every n days of the run, the month
// message (0x66956816) to the residential simulator, which restarts its
// cycle (it rests at state 10 between months), so a week's run covers it.
void Month(const Day& day)
{
    if (EveryRunDays(day, "OPENSC4_TEST_RESIDENTIAL_MONTH") && game::IsBuild641())
        if (void* const residential = Virtual<void*>(day.city, 0x110))
            Deliver(residential, 0x66956816, nullptr, 0, 0);
}

const FingerprintPart kFingerprint{"residential", 88, &Fingerprint};
const DayHook kMonth{"residential.month", 78, &Month};

} // namespace
} // namespace opensc4::autotest
