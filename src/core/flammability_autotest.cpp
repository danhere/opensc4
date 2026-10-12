// The flammability simulator's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <cISC4City.h>
#include <cISC4SimGrid.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::autotest {
namespace {

// Flammability (build 641 layout; docs/flammability.md): both grids
// (+0x58, +0x5C), the average (slot 8), the hotspot (slot 7), the
// multiplier (+0x64), the walk's sum and count (+0x68, +0x6C), the month's
// hot cells (+0x70) and the clusters (+0x7C).
void Fingerprint(cISC4City* city, std::string& line)
{
    void* const sim = city->GetFlammabilitySimulator();
    if (!sim || !game::IsBuild641())
        return;
    auto* const self = static_cast<char*>(sim);
    using GridFn = cISC4SimGrid<uint8_t>*;
    uint8_t value = 0;
    int32_t x = -1;
    int32_t z = -1;
    const uint8_t hotspot = Virtual<uint8_t>(sim, 0x1C, &value, &x, &z);
    uint32_t multiplier;
    std::memcpy(&multiplier, self + 0x64, 4);
    const auto* const hot = *reinterpret_cast<char* const*>(self + 0x70);
    const auto* const hotEnd = *reinterpret_cast<char* const*>(self + 0x74);
    char* const head = *reinterpret_cast<char**>(self + 0x7C);
    unsigned clusters = 0;
    for (char* node = *reinterpret_cast<char**>(head); node != head; node = *reinterpret_cast<char**>(node))
        ++clusters;
    char text[160];
    std::snprintf(text, sizeof text, " | flam %08X %08X avg %u hot %u %u,%d,%d mul %08X sum %d/%d cells %d cl %u",
                  GridChecksum(*reinterpret_cast<GridFn*>(self + 0x58)),
                  GridChecksum(*reinterpret_cast<GridFn*>(self + 0x5C)), Virtual<uint8_t>(sim, 0x20), hotspot, value,
                  x, z, multiplier, *reinterpret_cast<int32_t*>(self + 0x68), *reinterpret_cast<int32_t*>(self + 0x6C),
                  static_cast<int>((hotEnd - hot) / 12), clusters);
    line += text;
    // OPENSC4_TEST_FLAM_DUMP=<n>: the n-th fingerprint of the run also logs
    // every cell with a raw value (its raw and grid values and whether the
    // water simulator says it has water), to find where two runs differ.
    static int calls = 0;
    if (++calls == EnvInt("OPENSC4_TEST_FLAM_DUMP", 0)) {
        const int32_t width = *reinterpret_cast<int32_t*>(self + 0x38);
        const int32_t depth = *reinterpret_cast<int32_t*>(self + 0x3C);
        void* const grid = Virtual<void*>(*reinterpret_cast<void**>(self + 0x58), 0x60);
        void* const raw = Virtual<void*>(*reinterpret_cast<void**>(self + 0x5C), 0x60);
        void* const water = *reinterpret_cast<void**>(self + 0x48);
        auto cell = [](void* rows, int32_t cx, int32_t cz) {
            return reinterpret_cast<uint8_t*>(static_cast<char***>(rows)[0][3 * cx])[cz];
        };
        for (int32_t cz = 0; cz < depth; ++cz)
            for (int32_t cx = 0; cx < width; ++cx)
                if (const uint8_t r = cell(raw, cx, cz))
                    log::Write("flam dump: %d,%d raw %u grid %u water %u", cx, cz, r, cell(grid, cx, cz),
                               Virtual<uint8_t>(water, 0x2C, cx, cz));
    }
}

// OPENSC4_TEST_FLAMMABILITY: every cell counts as hot (+0x9C 0), clusters
// of one cell are kept (+0xA0 1) and a fire starts at a random lot on
// every walk's end (+0xA4 100), with the whole city walked each step
// (+0x94), so the walk's end, clusters and fires run in short runs
// (docs/flammability.md).
void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_FLAMMABILITY") || !game::IsBuild641())
        return;
    auto* const self = static_cast<char*>(static_cast<void*>(day.city->GetFlammabilitySimulator()));
    if (!self)
        return;
    *reinterpret_cast<int32_t*>(self + 0x9C) = 0;
    *reinterpret_cast<int32_t*>(self + 0xA0) = 1;
    *reinterpret_cast<float*>(self + 0xA4) = 100.0f;
    // The whole city in one step, so the walk ends every step.
    *reinterpret_cast<int32_t*>(self + 0x94) =
        *reinterpret_cast<int32_t*>(self + 0x38) * *reinterpret_cast<int32_t*>(self + 0x3C);
    log::Write("autotest: flammability driven (all cells hot, clusters of one, fires)");
}

const FingerprintPart kFingerprint{"flammability", 115, &Fingerprint};
const DayHook kDrive{"flammability.drive", 115, &Drive};

} // namespace
} // namespace opensc4::autotest
