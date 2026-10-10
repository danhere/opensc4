// The airport developer's part of the autotest (src/core/autotest_hooks.h).
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

using FreeFn = void(__cdecl*)(void* block);

// The airport developer (build 641; src/core/airport_object.cpp), from the
// city's slot 77 (+0x134, which returns the city's +0x1B8; the save loader
// fills that with interface 0xEA20548D): the number of port types (+0x1C),
// the number of ports (the list +0x48), the sum of their usage, and a
// checksum over every port's stage, type index, cell, replacing flag,
// usage, efficiency bits and demand bits; then the first four ports as
// stage:type:x,z:usage. Read from memory only (no lot is fetched).
//
// OPENSC4_TEST_AIRPORT=1 adds a checksum of queries no unattended city run
// makes: GetSimulatorType, AsISC4PortDeveloper (as an offset),
// GetNumAirports for sizes 0 to 3, GetNumPorts for every size with stages
// (all), (first), (later), GetAvgEfficiency, GetNumAirportTrips, per port
// with a fetched lot GetAirportUsageValues and GetMaintenanceCost of that
// lot, GetAllPortLots (the lots released again, their count mixed in) and,
// with at most one port, GetLeastEfficientPort (with two or more the
// game's own never returns).
void Fingerprint(cISC4City* city, std::string& line)
{
    if (!game::IsBuild641()) {
        line += " | airport none";
        return;
    }
    char* const self = Virtual<char*>(city, 0x134);
    if (!self) {
        line += " | airport none";
        return;
    }
    uint32_t h = 2166136261u;
    const auto Mix = [&h](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    };
    char* const head = At<char*>(self, 0x48);
    uint32_t count = 0, usage = 0;
    std::string first;
    for (char* node = At<char*>(head, 0x0); node != head && count < 100000; node = At<char*>(node, 0x0), ++count) {
        char* const port = At<char*>(node, 0x8);
        char* const type = At<char*>(port, 0x8);
        const uint32_t typeIndex = type ? At<uint32_t>(type, 0x8) : 0xFFFFFFFFu;
        Mix(At<uint32_t>(port, 0x4));
        Mix(typeIndex);
        Mix(static_cast<uint32_t>(At<int16_t>(port, 0xE)));
        Mix(static_cast<uint32_t>(At<int16_t>(port, 0x10)));
        Mix(At<uint8_t>(port, 0x18));
        Mix(At<uint32_t>(port, 0x24));
        Mix(At<uint32_t>(port, 0x14));
        Mix(At<uint32_t>(port, 0x1C));
        usage += At<uint32_t>(port, 0x24);
        if (count < 4) {
            char text[64];
            std::snprintf(text, sizeof text, " %u:%u:%d,%d:%u", At<uint32_t>(port, 0x4), typeIndex,
                          At<int16_t>(port, 0xE), At<int16_t>(port, 0x10), At<uint32_t>(port, 0x24));
            first += text;
        }
    }
    char text[96];
    std::snprintf(text, sizeof text, " | airport %d,%u,%u,%08X", At<int32_t>(self, 0x1C), count, usage, h);
    line += text;
    line += first;
    if (EnvInt("OPENSC4_TEST_AIRPORT", 0) != 1)
        return;

    uint32_t q = 2166136261u;
    const auto MixQ = [&q](uint32_t v) {
        for (int i = 0; i < 4; ++i)
            q = (q ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    };
    MixQ(Virtual<uint32_t>(self, 0x18));
    MixQ(static_cast<uint32_t>(Virtual<char*>(self, 0x28) - self));
    for (uint32_t size = 0; size < 4; ++size)
        MixQ(Virtual<uint32_t>(self, 0x2C, size));
    for (const uint32_t stages : {0x1Fu, 0x1u, 0x1Eu}) {
        MixQ(Virtual<uint32_t>(self, 0x24, 0x1Fu, stages));
        for (const uint32_t sizes : {0x2u, 0x4u, 0x8u})
            MixQ(Virtual<uint32_t>(self, 0x24, sizes, stages));
    }
    float average = 0.0f;
    Virtual<float*>(self, 0x14, &average);
    uint32_t bits = 0;
    std::memcpy(&bits, &average, 4);
    MixQ(bits);
    MixQ(Virtual<uint32_t>(self, 0x34));
    bool resolved = true;
    for (char* node = At<char*>(head, 0x0); node != head; node = At<char*>(node, 0x0)) {
        char* const port = At<char*>(node, 0x8);
        void* const lot = At<void*>(port, 0x20);
        if (At<uint8_t>(port, 0xC) || !lot) {
            resolved = false;
            continue;
        }
        uint32_t portUsage = 0, capacity = 0;
        MixQ(Virtual<uint8_t>(self, 0x30, lot, &portUsage, &capacity));
        MixQ(portUsage);
        MixQ(capacity);
        const int64_t cost = Virtual<int64_t>(self, 0x38, lot);
        MixQ(static_cast<uint32_t>(cost));
        MixQ(static_cast<uint32_t>(cost >> 32));
    }
    // GetAllPortLots references every lot without a null test: only when
    // every port has its lot. A game vector (begin, end, capacity) the
    // developer appends the lots to.
    if (resolved) {
        void* lots[3] = {};
        Virtual<uint32_t>(self, 0x20, static_cast<void*>(lots));
        void** const begin = static_cast<void**>(lots[0]);
        void** const end = static_cast<void**>(lots[1]);
        MixQ(static_cast<uint32_t>(end - begin));
        for (void** lot = begin; lot != end; ++lot)
            if (*lot)
                Virtual<uint32_t>(*lot, 0x8);
        if (begin)
            reinterpret_cast<FreeFn>(addr::SC4_Free)(begin);
    }
    if (count == 0 || (count == 1 && resolved)) {
        float efficiency = 0.0f;
        int32_t cell[2] = {};
        MixQ(Virtual<uint8_t>(self, 0x1C, &efficiency, static_cast<int32_t*>(cell)));
        std::memcpy(&bits, &efficiency, 4);
        MixQ(bits);
        MixQ(static_cast<uint32_t>(cell[0]));
        MixQ(static_cast<uint32_t>(cell[1]));
    }
    char hash[16];
    std::snprintf(hash, sizeof hash, ",q%08X", q);
    line += hash;
}

const FingerprintPart kFingerprint{"airport", 93, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
