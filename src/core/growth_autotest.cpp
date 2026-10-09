// The growth developers' part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"

#include <cISC4City.h>
#include <cISC4SimGrid.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace opensc4::autotest {
namespace {

// The in-order successor in one of the game's trees (node: +0x4 parent,
// +0x8 left, +0xC right).
char* Next(char* node)
{
    if (char* x = At<char*>(node, 0xC)) {
        while (At<char*>(x, 0x8))
            x = At<char*>(x, 0x8);
        return x;
    }
    char* y = At<char*>(node, 0x4);
    while (node == At<char*>(y, 0xC)) {
        node = y;
        y = At<char*>(y, 0x4);
    }
    if (At<char*>(node, 0xC) != y)
        node = y;
    return node;
}

// Growth developers (build 641; docs/growth.md), from the building
// development simulator's map of developer type to developer (+0x5C): per
// developer its type, the map update state +0xD4, the total capacity (slot
// 17), the growth and decline list sizes (slots 11, 12; 12-byte items) and
// checksums of the desirability, capacity, population and maximum capacity
// grids (slots 13 to 16). OPENSC4_TEST_GROWTH_QUERIES=1 adds, per
// developer, a checksum of queries no test city makes: EstimateDesirability
// (slot 29) at five cells, GetSubjectiveThresholds (30) for indexes -1 to 5,
// GetAbandonmentThreshold (24) for densities 0 to 2 (the game's map lookup
// inserts a missing density, on both sides alike) and the desirability
// threshold (23).
void Fingerprint(cISC4City* city, std::string& line)
{
    void* const development = Virtual<void*>(city, 0xD4);
    if (!development || !game::IsBuild641()) {
        line += " | growth none";
        return;
    }
    line += " | growth";
    const bool queries = EnvInt("OPENSC4_TEST_GROWTH_QUERIES", 0) == 1;
    char* const head = At<char*>(development, 0x5C);
    int n = 0;
    for (char* node = At<char*>(head, 0x8); node != head && n < 64; node = Next(node), ++n) {
        void* const developer = At<void*>(node, 0x14);
        if (!developer)
            continue;
        const auto ListSize = [&](std::size_t slot) {
            void** const vector = Virtual<void**>(developer, slot);
            return static_cast<uint32_t>((static_cast<char*>(vector[1]) - static_cast<char*>(vector[0])) / 12);
        };
        char text[160];
        std::snprintf(text, sizeof text, " %X:%u,%u,%u,%u,%08X,%08X,%08X,%08X", At<uint32_t>(node, 0x10),
                      At<uint32_t>(developer, 0xD4), Virtual<uint32_t>(developer, 0x44), ListSize(0x2C),
                      ListSize(0x30), GridChecksum(Virtual<cISC4SimGrid<int16_t>*>(developer, 0x34)),
                      GridChecksum(Virtual<cISC4SimGrid<uint16_t>*>(developer, 0x38)),
                      GridChecksum(Virtual<cISC4SimGrid<int16_t>*>(developer, 0x3C)),
                      GridChecksum(Virtual<cISC4SimGrid<uint16_t>*>(developer, 0x40)));
        line += text;
        if (queries) {
            uint32_t h = 2166136261u;
            const auto Mix = [&](uint32_t v) {
                for (int i = 0; i < 4; ++i)
                    h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
            };
            for (const int32_t cell : {8, 16, 32, 64, 100})
                Mix(Virtual<uint32_t>(developer, 0x74, cell, cell + 3) & 0xFFFF);
            for (int32_t i = -1; i < 6; ++i) {
                int32_t low = 0, high = 0;
                Virtual<void>(developer, 0x78, i, &low, &high);
                Mix(static_cast<uint32_t>(low));
                Mix(static_cast<uint32_t>(high));
            }
            for (int32_t density = 0; density < 3; ++density) {
                const float threshold = static_cast<float>(Virtual<double>(developer, 0x60, density));
                uint32_t bits = 0;
                std::memcpy(&bits, &threshold, 4);
                Mix(bits);
            }
            Mix(Virtual<uint32_t>(developer, 0x5C));
            char hash[16];
            std::snprintf(hash, sizeof hash, ",q%08X", h);
            line += hash;
        }
    }
}

const FingerprintPart kFingerprint{"growth", 91, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
