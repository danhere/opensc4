// The lot developer's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"

#include <cISC4City.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace opensc4::autotest {
namespace {

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
    void MixFloat(float v)
    {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        Mix(bits);
    }
};

// A deque iterator of the game's STL: {cur, first, last, node}.
struct DequeIterator {
    char* cur;
    char* first;
    char* last;
    char** node;
};

// The distance between two iterators of a deque of pointers (32 to a block).
int32_t Distance(const DequeIterator& from, const DequeIterator& to)
{
    return ((static_cast<int32_t>(reinterpret_cast<char*>(to.node) - reinterpret_cast<char*>(from.node)) >> 2) - 1) *
               32 +
           (static_cast<int32_t>(to.cur - to.first) >> 2) + (static_cast<int32_t>(from.last - from.cur) >> 2);
}

// The lot developer (build 641; src/core/lot_sim.h, lot_object.cpp), from
// the city's slot 79: the initialized flag (+0xC), the force-empty-textures
// flag (+0x5C), the two flags the lot update takes (+0x5D, +0x5E), the
// demand flag (+0x5F), the date +0x60, the number of buildings in the deque
// +0x64, the state of the lot update (+0xAC), its count (+0xB4) and its
// copies of the flags (+0xB8 to +0xBA), the size of the tree +0xFC and the
// terrain size Init read (+0x40, +0x44, +0x48). The lot developer's work
// itself (foundations, base textures, construction, demolition) shows in
// the occupant and lot counts every fingerprint has.
// The night step (+0x8C, +0x9C, +0xA4) follows the timer, not game days,
// and is left out.
// OPENSC4_TEST_LOT=1 adds a checksum of queries: GetSimulatorType (slot 5),
// GetForceEmptyLotBaseTextures (46), GetTerrainAltitudeByVertex (15) at 25
// vertices with and without water, and, for the lots the lot manager finds
// at 16 cells, whether each has a lot foundation (51) and the position slot
// 40 gives for its building. OPENSC4_TEST_LOT=2 adds the night step: its
// state, the index of the next building and the milliseconds counter.
void Fingerprint(cISC4City* city, std::string& line)
{
    char* const self = game::IsBuild641() ? Virtual<char*>(city, 0x13C) : nullptr;
    if (!self) {
        line += " | lot none";
        return;
    }
    const DequeIterator& begin = At<DequeIterator>(self, 0x64);
    const DequeIterator& end = At<DequeIterator>(self, 0x74);
    const bool hasDeque = At<void*>(self, 0x84) != nullptr;
    Hash size;
    size.Mix(At<uint32_t>(self, 0x40));
    size.Mix(At<uint32_t>(self, 0x44));
    size.Mix(At<uint32_t>(self, 0x48));
    char text[200];
    std::snprintf(text, sizeof text, " | lot %u f%u%u%u%u d%u b%d u%u,%d,%u%u%u t%u s%08X", At<uint8_t>(self, 0xC),
                  At<uint8_t>(self, 0x5C), At<uint8_t>(self, 0x5D), At<uint8_t>(self, 0x5E), At<uint8_t>(self, 0x5F),
                  At<uint32_t>(self, 0x60), hasDeque ? Distance(begin, end) : -1, At<uint32_t>(self, 0xAC),
                  At<int32_t>(self, 0xB4), At<uint8_t>(self, 0xB8), At<uint8_t>(self, 0xB9), At<uint8_t>(self, 0xBA),
                  At<uint32_t>(self, 0x100), size.h);
    line += text;
    const int test = EnvInt("OPENSC4_TEST_LOT", 0);
    if (test < 1 || !At<uint8_t>(self, 0xC))
        return;
    Hash q;
    q.Mix(Virtual<uint32_t>(self, 0x14));
    q.Mix(Virtual<uint8_t>(self, 0xB8));
    const int32_t vertices = static_cast<int32_t>(At<uint32_t>(self, 0x40));
    if (vertices > 1 && At<void*>(self, 0x3C)) {
        for (int32_t i = 0; i < 5; ++i)
            for (int32_t j = 0; j < 5; ++j) {
                const int32_t x = (vertices - 1) * i / 4, z = (vertices - 1) * j / 4;
                q.MixFloat(static_cast<float>(Virtual<double>(self, 0x3C, x, z, 0u)));
                q.MixFloat(static_cast<float>(Virtual<double>(self, 0x3C, x, z, 1u)));
            }
    }
    const int32_t cellsX = At<int32_t>(self, 0x50), cellsZ = At<int32_t>(self, 0x4C);
    if (void* const lots = At<void*>(self, 0x24)) {
        for (int32_t i = 0; i < 16 && cellsX > 16 && cellsZ > 16; ++i) {
            const int32_t x = (cellsX - 1) * (1 + i % 4) / 5, z = (cellsZ - 1) * (1 + i / 4) / 5;
            void* const lot = Virtual<void*>(lots, 0x18, x, z, 0u);
            q.Mix(lot ? 1u : 0u);
            if (!lot)
                continue;
            q.Mix(Virtual<void*>(self, 0xCC, lot) ? 1u : 0u);
            float position[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            int32_t facing[2] = {0, 0};
            q.Mix(Virtual<void*>(self, 0xA0, lot, static_cast<float*>(position), static_cast<int32_t*>(facing)) ? 1u
                                                                                                               : 0u);
            for (const float value : position)
                q.MixFloat(value);
            q.Mix(static_cast<uint32_t>(facing[0]));
        }
    }
    std::snprintf(text, sizeof text, " q%08X", q.h);
    line += text;
    if (test < 2)
        return;
    std::snprintf(text, sizeof text, " n%u,%d,%u", At<uint32_t>(self, 0x9C),
                  hasDeque ? Distance(begin, At<DequeIterator>(self, 0x8C)) : -1, At<uint32_t>(self, 0xA4));
    line += text;
}

const FingerprintPart kFingerprint{"lot", 97, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
