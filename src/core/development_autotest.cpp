// The building development simulator's part of the autotest
// (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cISC4City.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::autotest {
namespace {

// Elements of one of the game's lists ({next, prev, value} nodes).
uint32_t ListCount(void* head)
{
    if (!head)
        return 0;
    uint32_t n = 0;
    for (void** node = static_cast<void**>(static_cast<void**>(head)[0]); node != head && n < 1000000;
         node = static_cast<void**>(node[0]))
        ++n;
    return n;
}

// OPENSC4_TEST_DEVELOPMENT_QUERIES=1: queries the UI and other code make
// that a test city does not, each day, into a checksum: the building,
// family, foundation and retaining-wall type lists (slots 11, 13, 21, 27),
// type checks and family members for the first types (15, 16, 18, 20), the
// wealth of six developer types (31) and the stage population multiplier
// (53). Calls that take game objects or index tables by their arguments
// are left out: with made-up arguments they crash the original too.
uint32_t Queries(void* development)
{
    using FreeFn = void(__cdecl*)(void* block);
    uint32_t h = 2166136261u;
    const auto Mix = [&](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    };
    const auto Free = [](void** vector) {
        if (vector[0])
            reinterpret_cast<FreeFn>(addr::SC4_Free)(vector[0]);
    };
    uint32_t firstType = 0, firstFamily = 0;
    for (const std::size_t slot : {0x2Cu, 0x34u, 0x54u, 0x6Cu}) {
        void* vector[3] = {};
        Mix(Virtual<uint32_t>(development, slot, vector));
        const uint32_t* const begin = static_cast<uint32_t*>(vector[0]);
        const uint32_t* const end = static_cast<uint32_t*>(vector[1]);
        for (const uint32_t* v = begin; v && v != end; ++v)
            Mix(*v);
        if (slot == 0x2C && begin != end)
            firstType = begin[0];
        if (slot == 0x34 && begin != end)
            firstFamily = begin[0];
        Free(vector);
    }
    Mix(Virtual<uint8_t>(development, 0x3C, firstType));
    Mix(Virtual<uint8_t>(development, 0x40, firstFamily));
    Mix(Virtual<uint8_t>(development, 0x50, firstType, firstFamily));
    {
        void* vector[3] = {};
        Mix(Virtual<uint32_t>(development, 0x48, vector, firstFamily));
        for (const uint32_t* v = static_cast<uint32_t*>(vector[0]); v && v != vector[1]; ++v)
            Mix(*v);
        Free(vector);
    }
    for (const int32_t type : {0x1010, 0x1020, 0x1030, 0x3110, 0x3320, 0x4100})
        Mix(Virtual<uint32_t>(development, 0x7C, type));
    const float stage = static_cast<float>(Virtual<double>(development, 0xD4));
    uint32_t bits = 0;
    std::memcpy(&bits, &stage, 4);
    Mix(bits);
    return h;
}

// Building development (the city's slot 53, build 641 offsets and slots;
// docs/development.md): the queue state +0x74, construction resources
// free and total (slots 48, 49), the sizes of the occupant list +0x4C, the
// queues +0x7C and +0x80, the trees +0x84, +0x90, +0xB0, the developer
// flags +0x50 and the cell table +0x9C.
void Fingerprint(cISC4City* city, std::string& line)
{
    char text[160] = " | dev none";
    if (void* const development = Virtual<void*>(city, 0xD4); development && game::IsBuild641()) {
        std::snprintf(text, sizeof text, " | dev st %u res %d %d n %u %u %u %u %u %u %u %u",
                      At<uint32_t>(development, 0x74), Virtual<int16_t>(development, 0xC0),
                      Virtual<int16_t>(development, 0xC4), ListCount(At<void*>(development, 0x4C)),
                      ListCount(At<void*>(development, 0x7C)), ListCount(At<void*>(development, 0x80)),
                      At<uint32_t>(development, 0x88), At<uint32_t>(development, 0x94), At<uint32_t>(development, 0xB4),
                      At<uint32_t>(development, 0x54), At<uint32_t>(development, 0xAC));
    }
    line += text;
    if (EnvInt("OPENSC4_TEST_DEVELOPMENT_QUERIES", 0) == 1 && game::IsBuild641())
        if (void* const development = Virtual<void*>(city, 0xD4)) {
            char queries[24];
            std::snprintf(queries, sizeof queries, " q %08X", Queries(development));
            line += queries;
        }
}

const FingerprintPart kFingerprint{"development", 89, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
