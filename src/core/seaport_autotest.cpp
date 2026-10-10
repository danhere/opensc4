// The seaport developer's part of the autotest (src/core/autotest_hooks.h).
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

// The seaport developer (build 641; src/core/seaport_sim.cpp), from the
// city's slot 76: whether the city has sea (+0x66), the ports-changed and
// map-stale flags (+0x64, +0x65), the sea access map's size (+0x74, +0x78)
// and a checksum of its bytes (+0x68), the full and current amounts of the
// budget department's line item 1 (+0x34), the number of ports (+0x48) and a
// checksum of each port's stage, type index, cell, efficiency and the fields
// +0x18 to +0x1C. OPENSC4_TEST_SEAPORT=1 adds a checksum of the queries no
// test city without a seaport user makes: the simulator type (slot 6), the
// average efficiency (5), GetNumSeaports (11), GetNumSeaportTrips (13),
// GetSeaportUsageValues and GetMaintenanceCost of no lot (12, 14), CityHasSea
// (15), GetCellSeaAccess (16) over a lattice of cells and QueryInterface for
// the seaport interface.
void Fingerprint(cISC4City* city, std::string& line)
{
    char* const seaport = game::IsBuild641() ? Virtual<char*>(city, 0x130) : nullptr;
    if (!seaport) {
        line += " | seaport none";
        return;
    }
    const uint32_t width = At<uint32_t>(seaport, 0x74), height = At<uint32_t>(seaport, 0x78);
    const uint8_t* const map = At<uint8_t*>(seaport, 0x68);
    const std::size_t mapSize = static_cast<std::size_t>(At<uint8_t*>(seaport, 0x6C) - map);
    const bool mapValid = map && mapSize == static_cast<std::size_t>(width) * height;
    long long full = -1, current = -1;
    if (void* const department = At<void*>(seaport, 0x34))
        if (void* const item = Virtual<void*>(department, 0x50, 1u)) {
            full = Virtual<int64_t>(item, 0x28);
            current = Virtual<int64_t>(item, 0x2C);
        }
    uint32_t h = 2166136261u;
    const auto Mix = [&h](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    };
    uint32_t ports = 0;
    if (void** const head = At<void**>(seaport, 0x48))
        for (void** node = static_cast<void**>(head[0]); node != head && ports < 4096;
             node = static_cast<void**>(node[0]), ++ports) {
            char* const port = static_cast<char*>(node[2]);
            Mix(At<uint32_t>(port, 0x4));
            Mix(At<uint16_t>(At<char*>(port, 0x8), 0x8));
            Mix(static_cast<uint32_t>(At<int16_t>(port, 0xE)));
            Mix(static_cast<uint32_t>(At<int16_t>(port, 0x10)));
            Mix(At<uint32_t>(port, 0x14));
            Mix(At<uint8_t>(port, 0x18));
            Mix(At<uint8_t>(port, 0x19));
            Mix(At<uint32_t>(port, 0x1C));
        }
    char text[200];
    std::snprintf(text, sizeof text, " | seaport %u,%u,%u %ux%u,%08X %lld,%lld %u,%08X", At<uint8_t>(seaport, 0x66),
                  At<uint8_t>(seaport, 0x64), At<uint8_t>(seaport, 0x65), width, height,
                  mapValid ? BytesChecksum(map, mapSize) : 0u, full, current, ports, h);
    line += text;
    if (EnvInt("OPENSC4_TEST_SEAPORT", 0) != 1)
        return;
    h = 2166136261u;
    Mix(Virtual<uint32_t>(seaport, 0x18));
    float efficiency = 0.0f;
    Virtual<float*>(seaport, 0x14, &efficiency);
    uint32_t bits = 0;
    std::memcpy(&bits, &efficiency, 4);
    Mix(bits);
    Mix(Virtual<uint32_t>(seaport, 0x2C));
    Mix(Virtual<uint32_t>(seaport, 0x34));
    uint32_t usage = 0, capacity = 0;
    Mix(Virtual<uint8_t>(seaport, 0x30, static_cast<void*>(nullptr), &usage, &capacity));
    Mix(usage);
    Mix(capacity);
    const int64_t cost = Virtual<int64_t>(seaport, 0x38, static_cast<void*>(nullptr));
    Mix(static_cast<uint32_t>(cost));
    Mix(static_cast<uint32_t>(cost >> 32));
    Mix(Virtual<uint8_t>(seaport, 0x3C));
    if (mapValid)
        for (uint32_t z = 0; z < height; z += 7)
            for (uint32_t x = 0; x < width; x += 5)
                Mix(Virtual<uint8_t>(seaport, 0x40, x, z));
    void* same = nullptr;
    if (Virtual<uint8_t>(seaport, 0x0, 0xCA22FE3Eu, &same) && same) {
        Mix(same == static_cast<void*>(seaport) ? 1u : 2u);
        Virtual<uint32_t>(same, 0x8);
    }
    std::snprintf(text, sizeof text, ",q%08X", h);
    line += text;
}

const FingerprintPart kFingerprint{"seaport", 94, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
