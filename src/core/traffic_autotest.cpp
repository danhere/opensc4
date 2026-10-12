// The traffic simulator's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/zone_sim.h"

#include <cISC4City.h>
#include <cISC4TrafficSimulator.h>

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace opensc4::autotest {
namespace {

// What OPENSC4_TEST_TRAFFIC_QUERIES found, added to the fingerprint from
// that day on.
std::string g_queries;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t value)
    {
        for (int i = 0; i < 4; ++i, value >>= 8)
            h = (h ^ (value & 0xFF)) * 16777619u;
    }
    void MixFloat(double value)
    {
        const auto narrow = static_cast<float>(value);
        uint32_t bits;
        std::memcpy(&bits, &narrow, sizeof bits);
        Mix(bits);
    }
};

// A vector the game fills and grows with its own allocator.
struct GameVector {
    char* begin = nullptr;
    char* end = nullptr;
    char* capacity = nullptr;
    ~GameVector()
    {
        if (begin)
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(begin);
    }
};

// OPENSC4_TEST_TRAFFIC_QUERIES=<n>: on the n-th day, three queries of the
// traffic simulator that no run of a city makes by itself (build 641 slots;
// docs/traffic.md), through its interface, so the game's or ours:
//   - GetTravelTimeRatio (24) at every third cell both ways, for the nine
//     travel types;
//   - GetSubnetworksForLot (39) for every initialized lot;
//   - GetWaterRoute (33) between cells of the same body of water (the water
//     service's map, as the finder asks it), taken from every fourth cell
//     both ways: up to 24 pairs a body, each cell with the one 7 places on.
// The fingerprint gets a checksum of each and how many answers said
// something (a ratio other than 1, a lot with subnetworks, a route found).
void Queries(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_TRAFFIC_QUERIES") || !game::IsBuild641())
        return;
    void* const traffic = day.city->GetTrafficSimulator();
    if (!traffic)
        return;
    const auto width = static_cast<int32_t>(day.city->CellCountX());
    const auto depth = static_cast<int32_t>(day.city->CellCountZ());

    Hash ratios;
    uint32_t asked = 0, slowed = 0;
    for (int32_t z = 0; z < depth; z += 3) {
        for (int32_t x = 0; x < width; x += 3) {
            for (uint32_t type = 0; type < 9; ++type) {
                const double ratio = Virtual<double>(traffic, 0x60, x, z, type); // ST0
                ratios.MixFloat(ratio);
                ++asked;
                slowed += static_cast<float>(ratio) != 1.0f;
            }
        }
    }

    Hash subnetworks;
    uint32_t lots = 0, connected = 0;
    char* const zones = Virtual<char*>(day.city, 0x12C);
    if (zones && At<uint8_t>(zones, zone_sim::field::kInitialized)) {
        void** const head = At<void**>(zones, zone_sim::field::kLotList);
        uint32_t listed = 0;
        for (void** node = static_cast<void**>(head[0]); node != head && listed < 0x100000;
             node = static_cast<void**>(node[0]), ++listed) {
            void* const lot = node[2];
            if (!lot || !Virtual<uint8_t>(lot, 0x18)) // slot 6: IsInitialized
                continue;
            GameVector list;
            subnetworks.Mix(Virtual<uint32_t>(traffic, 0x9C, lot, &list) & 0xFF);
            const auto count = static_cast<uint32_t>((list.end - list.begin) / 4);
            subnetworks.Mix(count);
            for (uint32_t i = 0; i < count; ++i)
                subnetworks.Mix(At<uint32_t>(list.begin, 4 * i));
            ++lots;
            connected += count != 0;
        }
    }

    Hash routes;
    uint32_t pairs = 0, found = 0, steps = 0;
    void* const water = Global<void*>(addr::SC4_Service43CF0);
    void* const bodies = water ? Virtual<void*>(water, 0x24) : nullptr;
    if (bodies) {
        struct Cell {
            uint16_t body;
            int16_t x, z;
        };
        std::vector<Cell> cells;
        for (int32_t z = 0; z < depth; z += 4)
            for (int32_t x = 0; x < width; x += 4)
                if (const auto body = static_cast<uint16_t>(Virtual<uint32_t>(bodies, 0x1C, x, z)))
                    cells.push_back({body, static_cast<int16_t>(x), static_cast<int16_t>(z)});
        std::vector<uint16_t> done;
        for (const Cell& first : cells) {
            bool seen = false;
            for (const uint16_t body : done)
                seen = seen || body == first.body;
            if (seen)
                continue;
            done.push_back(first.body);
            std::vector<Cell> same;
            for (const Cell& cell : cells)
                if (cell.body == first.body)
                    same.push_back(cell);
            const std::size_t n = same.size();
            for (std::size_t i = 0; i < n && i < 24; ++i) {
                const Cell& from = same[i * (n / 24 ? n / 24 : 1) % n];
                const Cell& to = same[(i * (n / 24 ? n / 24 : 1) + 7) % n];
                GameVector route;
                const uint32_t result = Virtual<uint32_t>(traffic, 0x84, static_cast<int32_t>(from.x),
                                                          static_cast<int32_t>(from.z), static_cast<int32_t>(to.x),
                                                          static_cast<int32_t>(to.z), &route) & 0xFF;
                const auto bytes = static_cast<uint32_t>(route.end - route.begin);
                routes.Mix(result);
                routes.Mix(bytes);
                for (uint32_t b = 0; b < bytes; ++b)
                    routes.Mix(static_cast<uint8_t>(route.begin[b]));
                ++pairs;
                found += result != 0;
                steps += bytes;
            }
        }
    }

    char text[160];
    std::snprintf(text, sizeof text, " q %08X %u/%u %08X %u/%u %08X %u/%u/%u", ratios.h, slowed, asked, subnetworks.h,
                  connected, lots, routes.h, found, pairs, steps);
    g_queries = text;
    log::Write("autotest: traffic queries: travel time ratio %08X (%u of %u not 1), subnetworks %08X (%u of %u lots "
               "with any), water routes %08X (%u of %u found, %u bytes)",
               ratios.h, slowed, asked, subnetworks.h, connected, lots, routes.h, found, pairs, steps);
}

// Traffic (build 641 slots): trip scale and the one shown (14, 15),
// last month's income (18) and the freight scaling (27), as float bits.
void Fingerprint(cISC4City* city, std::string& line)
{
    cISC4TrafficSimulator* traffic = city->GetTrafficSimulator();
    char trafficText[96] = "";
    if (traffic && game::IsBuild641()) {
        auto slot = [traffic](int index) {
            using Fn = float(__thiscall*)(void*);
            const float value = reinterpret_cast<Fn>((*reinterpret_cast<void***>(traffic))[index])(traffic);
            uint32_t bits;
            std::memcpy(&bits, &value, sizeof bits);
            return bits;
        };
        std::snprintf(trafficText, sizeof trafficText, " | traffic %08X %08X %08X %08X", slot(14), slot(15),
                      slot(18), slot(27));
    }
    line += trafficText;
    line += g_queries;
}

const FingerprintPart kFingerprint{"traffic", 90, &Fingerprint};
const DayHook kQueries{"traffic.queries", 90, &Queries};

} // namespace
} // namespace opensc4::autotest
