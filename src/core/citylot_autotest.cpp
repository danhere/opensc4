// The lots' part of the autotest (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/citylot_sim.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/zone_sim.h"

#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace opensc4::autotest {
namespace {

namespace field = citylot_sim::field;
namespace slot = citylot_sim::slot;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
    void MixFloat(double v)
    {
        const float f = static_cast<float>(v);
        uint32_t bits = 0;
        std::memcpy(&bits, &f, 4);
        Mix(bits);
    }
};

// A map uint32 -> uint16 of the lot (head, size), in key order.
void MixMap(Hash& hash, char* map)
{
    char* const head = At<char*>(map, 0x0);
    hash.Mix(At<uint32_t>(map, 0x4));
    if (!head)
        return;
    uint32_t n = 0;
    for (char* node = At<char*>(head, 0x8); node != head && n < 4096; node = citylot_sim::TreeNext(node), ++n) {
        hash.Mix(At<uint32_t>(node, 0x10));
        hash.Mix(At<uint16_t>(node, 0x14));
    }
}

uint32_t Cell(void* lot)
{
    if (!lot)
        return 0xFFFFFFFF;
    return At<uint8_t>(lot, field::kCellX) | (static_cast<uint32_t>(At<uint8_t>(lot, field::kCellZ)) << 16);
}

// One lot's observable state from its fields: no addresses (the
// configuration by its ID, the unit by presence, the travel designate by
// its cell).
uint32_t LotHash(char* lot, uint32_t& population)
{
    Hash hash;
    hash.Mix(At<uint32_t>(lot, field::kMinX));
    for (const std::size_t offset : {field::kAltitude, field::kSlope, field::kBuiltSlope, field::kConfigId,
                                     field::kStateDay, field::kBuildingType, field::kTravelJobs,
                                     field::kTravelJobs + 4, field::kTravelJobs + 8})
        hash.Mix(At<uint32_t>(lot, offset));
    // Not the vacate reason +0x83: the cell constructor leaves it unset (pool
    // memory) until a state change writes it.
    for (std::size_t offset = field::kInitialized; offset <= field::kWealth; ++offset)
        hash.Mix(At<uint8_t>(lot, offset));
    hash.Mix(At<uint8_t>(lot, field::kRandom));
    void* const config = At<void*>(lot, field::kConfig);
    hash.Mix(config ? Virtual<uint32_t>(config, 0xC) : 0u);
    hash.Mix(At<void*>(lot, field::kUnit) != nullptr);
    hash.Mix(Cell(At<void*>(lot, field::kDesignate)));
    hash.Mix(At<uint32_t>(lot, field::kMap40 + 4));
    char* const begin = At<char*>(lot, field::kPopulations);
    char* const end = At<char*>(lot, field::kPopulations + 4);
    hash.Mix(static_cast<uint32_t>((end - begin) / 12));
    for (char* map = begin; map != end; map += 12) {
        MixMap(hash, map);
        char* const head = At<char*>(map, 0x0);
        uint32_t n = 0;
        for (char* node = At<char*>(head, 0x8); node != head && n < 4096; node = citylot_sim::TreeNext(node), ++n)
            population += At<uint16_t>(node, 0x14);
    }
    MixMap(hash, lot + field::kTravel);
    uint32_t trips = 0;
    void** const head = At<void**>(lot, field::kTrips);
    for (void** node = static_cast<void**>(head[0]); node != head && trips < 0x10000; node = static_cast<void**>(node[0]))
        ++trips;
    hash.Mix(trips);
    return hash.h;
}

// The pure query slots of a lot (OPENSC4_TEST_CITYLOT=1, every 7th lot of
// the list): the rectangle, centre, location, size, facing, zone, state and
// condition getters, the flag getters, the travel jobs for wealth 0 to 4,
// the days since the last state change, the land value, the building type
// both ways, whether there is a building, the populations and travel
// populations of the developer types the lot holds and of 0x1010, the
// capacity of those types (slot 50, both ways), job access, and for lots
// with a building type whose profile has no more maps than the lot: the
// capacity by wealth 1 to 3 (slot 51), the available capacity (slot 52, both
// ways) and the jobs (slot 64). Not the occupant list (it allocates), the
// configuration object (it needs an occupant) nor the vacate reason (unset
// memory in new lots). Asking the development simulator for a building
// type's profile (its slot 39) adds an empty one for a type it has none
// for, as the game's own queries do, on both sides alike.
uint32_t Queries(char* lot)
{
    Hash hash;
    int32_t rect[4] = {};
    Virtual<void>(lot, slot::kGetBoundingRect, static_cast<int32_t*>(rect));
    for (const int32_t v : rect)
        hash.Mix(static_cast<uint32_t>(v));
    float center[3] = {};
    Virtual<void*>(lot, slot::kGetCenter, static_cast<float*>(center));
    for (const float v : center)
        hash.MixFloat(v);
    int32_t x = 0, z = 0;
    Virtual<void>(lot, slot::kGetLocation, &x, &z);
    uint8_t width = 0, depth = 0;
    Virtual<void>(lot, slot::kGetSize, &width, &depth);
    hash.Mix(static_cast<uint32_t>(x) | (static_cast<uint32_t>(z) << 8) | (uint32_t{width} << 16) | (uint32_t{depth} << 24));
    for (const std::size_t getter : {slot::kGetFlags, slot::kGetFacing, slot::kGetZoneType, slot::kGetState,
                                     slot::kGetCondition, slot::kGetOccupantWealth, slot::kIsInLivableState, slot::kHasCapacity, slot::kIsInDemolishableState,
                                     slot::kGetDaysSinceLastStateChange})
        hash.Mix(Virtual<uint32_t>(lot, getter));
    for (const std::size_t getter : {slot::kIsInitialized, slot::kGetNeedsPower, slot::kGetHasPower, slot::kIsHistorical,
                                     slot::kGetVerticesAreLocked, slot::kHasJobAccess})
        hash.Mix(Virtual<uint8_t>(lot, getter));
    for (const std::size_t getter : {slot::kGetAltitude, slot::kGetSlope, slot::kGetBuiltSlope})
        hash.MixFloat(Virtual<double>(lot, getter));
    for (uint32_t wealth = 0; wealth < 5; ++wealth)
        hash.MixFloat(Virtual<double>(lot, slot::kGetTravelJobs, wealth));
    const int64_t value = Virtual<int64_t>(lot, slot::kGetLandValue);
    hash.Mix(static_cast<uint32_t>(value));
    hash.Mix(static_cast<uint32_t>(value >> 32));
    hash.Mix(Virtual<uint32_t>(lot, slot::kGetBuildingType, 0u));
    const uint32_t building = Virtual<uint32_t>(lot, slot::kGetBuildingType, 1u);
    hash.Mix(building);
    hash.Mix(Virtual<void*>(lot, slot::kGetBuilding) != nullptr);
    hash.Mix(Virtual<void*>(lot, slot::kGetLotConfiguration) != nullptr);
    hash.Mix(Virtual<void*>(lot, slot::kGetUnit) != nullptr);
    hash.Mix(Cell(Virtual<void*>(lot, slot::kGetTravelDesignate)));
    char* const begin = At<char*>(lot, field::kPopulations);
    char* const end = At<char*>(lot, field::kPopulations + 4);
    const uint32_t maps = static_cast<uint32_t>((end - begin) / 12);
    uint32_t types[8] = {0x1010};
    uint32_t count = 1;
    for (char* map = begin; map != end; map += 12) {
        char* const head = At<char*>(map, 0x0);
        for (char* node = At<char*>(head, 0x8); node != head && count < 8; node = citylot_sim::TreeNext(node))
            types[count++] = At<uint32_t>(node, 0x10);
    }
    const void* const development = Global<void*>(addr::SC4_Service43D68);
    bool profiled = false;
    if (building && development) {
        char* const profile = Virtual<char*>(Global<void*>(addr::SC4_Service43D68), 0x9C, building);
        profiled = profile && At<uint32_t>(profile, 0x0) &&
                   static_cast<uint32_t>((At<char*>(profile, 0x10) - At<char*>(profile, 0xC)) / 12) <= maps;
    }
    for (uint32_t i = 0; i < count; ++i) {
        hash.Mix(Virtual<uint16_t>(lot, slot::kGetPopulation, types[i]));
        hash.Mix(Virtual<uint16_t>(lot, slot::kGetTravelPopulation, types[i]));
        if (development) {
            hash.Mix(Virtual<uint16_t>(lot, slot::kGetCapacityByType, types[i], 0u));
            hash.Mix(Virtual<uint16_t>(lot, slot::kGetCapacityByType, types[i], 1u));
        }
        if (profiled) {
            hash.Mix(Virtual<uint16_t>(lot, slot::kGetAvailableCapacity, types[i], 0u));
            hash.Mix(Virtual<uint16_t>(lot, slot::kGetAvailableCapacity, types[i], 1u));
        }
    }
    if (profiled) {
        for (uint32_t wealth = 1; wealth < 4; ++wealth)
            hash.Mix(Virtual<uint16_t>(lot, slot::kGetCapacityByWealth, wealth));
        float jobs[4] = {};
        Virtual<uint32_t>(lot, slot::kGetJobs, static_cast<float*>(jobs));
        for (const float v : jobs)
            hash.MixFloat(v);
    }
    return hash.h;
}

// The city's lots (build 641; src/core/citylot_sim.h), walked through the
// lot manager's list (the zone developer of the city's slot 75, list at
// +0x60): the count, how many are initialized, the count per state 0 to 6
// (and others), the population held in all maps, and two order-independent
// checksums over every lot's fields (the sum and the exclusive or of the
// lots' hashes). OPENSC4_TEST_CITYLOT=1 adds a checksum of the pure query
// slots called on every 7th lot (Queries above), in list order.
void Fingerprint(cISC4City* city, std::string& line)
{
    if (!game::IsBuild641()) {
        line += " | citylot none";
        return;
    }
    char* const zones = Virtual<char*>(city, 0x12C);
    if (!zones || !At<uint8_t>(zones, zone_sim::field::kInitialized)) {
        line += " | citylot none";
        return;
    }
    const bool queries = EnvInt("OPENSC4_TEST_CITYLOT", 0) == 1;
    // OPENSC4_TEST_CITYLOT_DUMP=<n>: the n-th fingerprint of the run also
    // logs every lot's fields, to find which lot two runs differ in.
    static int calls = 0;
    const bool dump = ++calls == EnvInt("OPENSC4_TEST_CITYLOT_DUMP", 0);
    uint32_t lots = 0, initialized = 0, population = 0, sum = 0, mixed = 0, listed = 0;
    uint32_t states[8] = {};
    Hash asked;
    void** const head = At<void**>(zones, zone_sim::field::kLotList);
    for (void** node = static_cast<void**>(head[0]); node != head && listed < 0x100000;
         node = static_cast<void**>(node[0]), ++listed) {
        char* const lot = static_cast<char*>(node[2]);
        if (!lot)
            continue;
        ++lots;
        initialized += At<uint8_t>(lot, field::kInitialized) != 0;
        const uint8_t state = At<uint8_t>(lot, field::kState);
        ++states[state < 7 ? state : 7];
        const uint32_t hash = LotHash(lot, population);
        if (dump) {
            uint32_t trips = 0;
            void** const tripHead = At<void**>(lot, field::kTrips);
            for (void** trip = static_cast<void**>(tripHead[0]); trip != tripHead && trips < 0x10000;
                 trip = static_cast<void**>(trip[0]))
                ++trips;
            log::Write("citylot dump: %u,%u-%u,%u hash %08X bytes %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X "
                       "random %u floats %08X %08X %08X day %u type %08X jobs %08X %08X %08X unit %d designate %08X "
                       "access %u maps %u travel %u trips %u",
                       At<uint8_t>(lot, 0x20), At<uint8_t>(lot, 0x21), At<uint8_t>(lot, 0x22), At<uint8_t>(lot, 0x23),
                       hash, At<uint8_t>(lot, 0x78), At<uint8_t>(lot, 0x79), At<uint8_t>(lot, 0x7A),
                       At<uint8_t>(lot, 0x7B), At<uint8_t>(lot, 0x7C), At<uint8_t>(lot, 0x7D), At<uint8_t>(lot, 0x7E),
                       At<uint8_t>(lot, 0x7F), At<uint8_t>(lot, 0x80), At<uint8_t>(lot, 0x81), At<uint8_t>(lot, 0x82),
                       At<uint8_t>(lot, field::kRandom), At<uint32_t>(lot, field::kAltitude),
                       At<uint32_t>(lot, field::kSlope), At<uint32_t>(lot, field::kBuiltSlope),
                       At<uint32_t>(lot, field::kStateDay), At<uint32_t>(lot, field::kBuildingType),
                       At<uint32_t>(lot, field::kTravelJobs), At<uint32_t>(lot, field::kTravelJobs + 4),
                       At<uint32_t>(lot, field::kTravelJobs + 8), At<void*>(lot, field::kUnit) != nullptr,
                       Cell(At<void*>(lot, field::kDesignate)), At<uint32_t>(lot, field::kMap40 + 4),
                       static_cast<uint32_t>((At<char*>(lot, field::kPopulations + 4) -
                                              At<char*>(lot, field::kPopulations)) / 12),
                       At<uint32_t>(lot + field::kTravel, 0x4), trips);
        }
        sum += hash;
        mixed ^= hash;
        if (queries && listed % 7 == 0 && At<uint8_t>(lot, field::kInitialized))
            asked.Mix(Queries(lot));
    }
    char text[200];
    std::snprintf(text, sizeof text, " | citylot %u,%u,%u/%u/%u/%u/%u/%u/%u/%u,%u,%08X,%08X", lots, initialized, states[0],
                  states[1], states[2], states[3], states[4], states[5], states[6], states[7], population, sum, mixed);
    line += text;
    if (queries) {
        char hash[16];
        std::snprintf(hash, sizeof hash, ",q%08X", asked.h);
        line += hash;
    }
}

const FingerprintPart kFingerprint{"citylot", 99, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
