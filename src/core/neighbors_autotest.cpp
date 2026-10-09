// The neighbors simulator's part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cISC4City.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace opensc4::autotest {
namespace {

// Neighbors: every connection through slot 16 (count, and a
// checksum of each one's type, cells, visited and active flags,
// whether it has a city across, and what slots 10-13 answer for
// it); the counts slots 17 and 20 give (road connections; adjacent
// cities with and without unestablished ones); the deals (slot 28)
// and monthly income and expense (32, 33) (docs/neighbors.md).
void Fingerprint(cISC4City* city, std::string& line)
{
    char neighborsText[128] = " | nb none";
    if (void* const neighbors = city->GetNeighborsSimulator(); neighbors && game::IsBuild641()) {
        using ClearFn = void(__thiscall*)(void*);
        const auto newList = [](std::uintptr_t allocator) {
            void** const sentinel = static_cast<void**>(Virtual<void*>(NodeAllocatorAt(allocator), 0xC, 0xCu));
            sentinel[0] = sentinel;
            sentinel[1] = sentinel;
            return static_cast<void*>(sentinel);
        };
        const auto count = [](void* head) {
            uint32_t n = 0;
            for (void** node = static_cast<void**>(static_cast<void**>(head)[0]); node != head;
                 node = static_cast<void**>(node[0]))
                ++n;
            return n;
        };
        void* connections = newList(addr::SC4List_NodeAllocator3);
        Virtual<void>(neighbors, 0x40, &connections);
        uint32_t sum = 0;
        for (void** node = static_cast<void**>(static_cast<void**>(connections)[0]); node != connections;
             node = static_cast<void**>(node[0])) {
            void* const connection = node[2];
            int32_t cell[6] = {};
            Virtual<void>(connection, 0x18, &cell[0], &cell[1]);
            Virtual<uint8_t>(connection, 0x1C, &cell[2], &cell[3]);
            Virtual<void>(connection, 0x20, &cell[4], &cell[5]);
            const uint32_t type = Virtual<uint32_t>(connection, 0x14);
            const uint32_t values[8] = {
                type,
                Virtual<uint8_t>(connection, 0x2C),
                Virtual<uint8_t>(connection, 0x34),
                Virtual<void*>(connection, 0x24) != nullptr,
                Virtual<uint32_t>(neighbors, 0x28, type, cell[0], cell[1]),
                Virtual<void*>(neighbors, 0x2C, type, cell[0], cell[1]) == connection,
                Virtual<void*>(neighbors, 0x30, cell[0], cell[1]) == Virtual<void*>(connection, 0x24),
                Virtual<void*>(neighbors, 0x34, connection) != nullptr};
            sum = sum * 31 + BytesChecksum(values, sizeof values) * 7 + BytesChecksum(cell, sizeof cell);
        }
        const uint32_t total = count(connections);
        void* roads = newList(addr::SC4List_NodeAllocator3);
        Virtual<void>(neighbors, 0x44, 2u, &roads);
        const uint32_t roadCount = count(roads);
        reinterpret_cast<ClearFn>(addr::SC4List_ClearReleasing)(&roads);
        Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0x10, roads);
        reinterpret_cast<ClearFn>(addr::SC4List_ClearReleasing)(&connections);
        Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0x10, connections);
        uint32_t adjacent[2] = {};
        for (uint32_t all = 0; all < 2; ++all) {
            void* cities = newList(addr::SC4List_NodeAllocator4);
            Virtual<void>(neighbors, 0x50, &cities, static_cast<uint8_t>(all));
            adjacent[all] = count(cities);
            reinterpret_cast<ClearFn>(addr::SC4List_ClearReleasing4)(&cities);
            Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator4), 0x10, cities);
        }
        void* deals = newList(addr::SC4List_NodeAllocator);
        Virtual<void>(neighbors, 0x70, &deals);
        const uint32_t dealCount = count(deals);
        reinterpret_cast<ClearFn>(addr::SC4List_Destroy)(&deals);
        const int64_t income = Virtual<int64_t>(neighbors, 0x80);
        const int64_t expense = Virtual<int64_t>(neighbors, 0x84);
        const auto* const flags = static_cast<const uint8_t*>(neighbors);
        std::snprintf(neighborsText, sizeof neighborsText,
                      " | nb %u %08X roads %u adj %u %u deals %u %lld %lld fl %02X%02X%02X%02X", total, sum,
                      roadCount, adjacent[0], adjacent[1], dealCount, static_cast<long long>(income),
                      static_cast<long long>(expense), flags[0xC], flags[0x3C], flags[0x3D], flags[0x3F]);
    }
    line += neighborsText;
}

// OPENSC4_TEST_NEIGHBORS_MONTH=<n>: every n days of the run, the
// neighbors simulator's monthly pass over its deals (0x006D0E50,
// build 641), as MonthPassed does (docs/neighbors.md).
void Month(const Day& day)
{
    if (EveryRunDays(day, "OPENSC4_TEST_NEIGHBORS_MONTH") && game::IsBuild641())
        if (void* const neighbors = day.city->GetNeighborsSimulator()) {
            reinterpret_cast<void(__thiscall*)(void*)>(addr::cSC4NeighborsSimulator_Monthly)(neighbors);
            log::Write("autotest: neighbors month on day %d", day.day);
        }
}

const FingerprintPart kFingerprint{"neighbors", 80, &Fingerprint};
const DayHook kMonth{"neighbors.month", 70, &Month};

} // namespace
} // namespace opensc4::autotest
