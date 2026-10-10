// The landfill developer's part of the autotest (src/core/autotest_hooks.h).
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

struct Node {
    Node* next;
    Node* previous;
    void* value;
};

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

// A cell region as cSC4LandfillDeveloper::GetZoningCost reads one (x0, z0,
// x1, z1, +0x14 width, +0x18 height, +0x20 columns of bit rows): a square
// of 4 x 4 cells, all set. Plain data of ours; the game only reads it.
struct TestRegion {
    int32_t rect[4];
    uint32_t unused10;
    uint32_t width;
    uint32_t height;
    uint32_t unused1C;
    uint32_t** columns;
    uint32_t bits[4];
    uint32_t* rows[4];

    TestRegion(int32_t x, int32_t z)
        : rect{x, z, x + 3, z + 3}, unused10(0), width(4), height(4), unused1C(0), columns(rows),
          bits{0xF, 0xF, 0xF, 0xF}
    {
        for (int i = 0; i < 4; ++i)
            rows[i] = &bits[i];
    }
};

// The landfill developer (build 641; src/core/landfill_sim.cpp), from the
// city's slot 78: the count of zones, the capacity +0x40, the use +0x44,
// the upkeep +0x38 and a checksum over the zones: each one's rectangle,
// tile counts (+0x10, +0x14, +0x18), the empty and active flags (+0x28,
// +0x29), +0x34, the level +0x38 and per tile (0x20 bytes from +0x1C) its
// cell (+4, +8), flags (+0xC, +0xD), capacity (+0x14) and use (+0x18).
// OPENSC4_TEST_LANDFILL=1 adds a checksum of the queries no unattended run
// makes: the simulator type, the upkeep (slot 7), the usage (10), the tile
// count (11), the zone at nine cells (12; whether there is one), the zoning
// cost (9) of three squares of 4 x 4 cells and, once the department budget
// is there, the current budget (8) and the efficiency (5).
void Fingerprint(cISC4City* city, std::string& line)
{
    void* const developer = game::IsBuild641() ? Virtual<void*>(city, 0x138) : nullptr;
    if (!developer) {
        line += " | landfill none";
        return;
    }
    Node* const head = At<Node*>(developer, 0x30);
    Hash zones;
    uint32_t count = 0;
    for (Node* node = head->next; node != head && count < 4096; node = node->next, ++count) {
        void* const zone = node->value;
        if (void* const region = At<void*>(zone, 0xC))
            for (std::size_t offset = 0; offset < 0x10; offset += 4)
                zones.Mix(At<uint32_t>(region, offset));
        zones.Mix(At<uint32_t>(zone, 0x10));
        zones.Mix(At<uint32_t>(zone, 0x14));
        zones.Mix(At<uint32_t>(zone, 0x18));
        zones.Mix(At<uint8_t>(zone, 0x28));
        zones.Mix(At<uint8_t>(zone, 0x29));
        zones.Mix(At<uint32_t>(zone, 0x34));
        zones.Mix(At<uint32_t>(zone, 0x38));
        char* const end = At<char*>(zone, 0x20);
        int tiles = 0;
        for (char* tile = At<char*>(zone, 0x1C); tile != end && tiles < 65536; tile += 0x20, ++tiles) {
            zones.Mix(At<uint32_t>(tile, 0x4));
            zones.Mix(At<uint32_t>(tile, 0x8));
            zones.Mix(At<uint8_t>(tile, 0xC));
            zones.Mix(At<uint8_t>(tile, 0xD));
            zones.Mix(At<uint32_t>(tile, 0x14));
            zones.Mix(At<uint32_t>(tile, 0x18));
        }
    }
    char text[128];
    std::snprintf(text, sizeof text, " | landfill %u,%u,%u,%lld,%08X", count, At<uint32_t>(developer, 0x40),
                  At<uint32_t>(developer, 0x44), static_cast<long long>(At<int64_t>(developer, 0x38)), zones.h);
    line += text;
    if (EnvInt("OPENSC4_TEST_LANDFILL", 0) != 1)
        return;

    Hash queries;
    const auto Mix64 = [&](int64_t v) {
        queries.Mix(static_cast<uint32_t>(v));
        queries.Mix(static_cast<uint32_t>(static_cast<uint64_t>(v) >> 32));
    };
    queries.Mix(Virtual<uint32_t>(developer, 0x18));
    Mix64(Virtual<int64_t>(developer, 0x1C));
    uint32_t usage = 0, capacity = 0;
    Virtual<void>(developer, 0x28, &usage, &capacity);
    queries.Mix(usage);
    queries.Mix(capacity);
    queries.Mix(Virtual<uint32_t>(developer, 0x2C));
    const int32_t cellsX = static_cast<int32_t>(city->CellCountX());
    const int32_t cellsZ = static_cast<int32_t>(city->CellCountZ());
    for (int i = 1; i <= 3; ++i)
        for (int j = 1; j <= 3; ++j)
            queries.Mix(Virtual<void*>(developer, 0x30, cellsX * i / 4, cellsZ * j / 4) != nullptr);
    // A zone's own first cell is inside it by slot 12's test.
    for (Node* node = head->next; node != head; node = node->next)
        if (const int32_t* const rect = At<const int32_t*>(node->value, 0xC))
            queries.Mix(Virtual<void*>(developer, 0x30, rect[0], rect[1]) != nullptr);
    if (cellsX >= 16 && cellsZ >= 16) {
        for (int i = 1; i <= 3; ++i) {
            TestRegion region(cellsX * i / 4 - 2, cellsZ * i / 4 - 2);
            Mix64(Virtual<int64_t>(developer, 0x24, &region));
        }
        // Over the first zone, whose level the cost then uses.
        if (head->next != head)
            if (const int32_t* const rect = At<const int32_t*>(head->next->value, 0xC))
                if (rect[0] >= 0 && rect[1] >= 0 && rect[0] + 3 < cellsX && rect[1] + 3 < cellsZ) {
                    TestRegion region(rect[0], rect[1]);
                    Mix64(Virtual<int64_t>(developer, 0x24, &region));
                }
    }
    // Slots 8 and 5 use the department budget of message 0x26D31EC1
    // without a check.
    if (At<void*>(developer, 0x2C)) {
        Mix64(Virtual<int64_t>(developer, 0x20));
        float efficiency = 0.0f;
        Virtual<float*>(developer, 0x14, &efficiency);
        uint32_t bits = 0;
        std::memcpy(&bits, &efficiency, 4);
        queries.Mix(bits);
    }
    std::snprintf(text, sizeof text, ",q%08X", queries.h);
    line += text;
}

const FingerprintPart kFingerprint{"landfill", 95, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
