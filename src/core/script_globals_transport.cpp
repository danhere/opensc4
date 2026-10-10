// The advisors' script variables about transport: funding, station use,
// congestion per network with its worst place, airports and seaports,
// neighbour connections, and the counts of stations, tiles, airports and
// seaports. Replacement, off by default:
//
//   scripts.globals.transport: the two publishers (0x0041C1A0, which ends by
//     calling 0x0041B8D0).
//
// Neither function uses its `this`. The figures come from the traffic
// simulator's statistics block (slot 34), the airport and seaport developers
// and two unnamed services that list airports and seaports.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <utility>

namespace opensc4::script_globals {
namespace {

// A node of the game's lists; the head is a node whose links point to itself.
struct Node {
    Node* next;
    Node* previous;
    void* item;
};

struct NamedField {
    const char* name;
    std::size_t at;
};

void* System()
{
    return Global<void*>(addr::cSC4AdvisorSystem_Instance);
}

void* City()
{
    return Global<void*>(addr::SC4_City);
}

void Number(void* system, const char* name, double value)
{
    Virtual<uint8_t>(system, 0x34, name, value);
}

// A number with a place (or none).
void Located(void* system, const char* name, double value, void* point)
{
    Virtual<uint8_t>(system, 0x38, name, value, point);
}

int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

double Percent(char* stats, std::size_t at)
{
    return static_cast<double>(At<float>(stats, at)) * 100.0;
}

// A new point object in `point`, the one before released.
void Renew(void*& point)
{
    void* const com = reinterpret_cast<void*(__cdecl*)()>(addr::RZGetCOM)();
    if (void* const old = point) {
        point = nullptr;
        Virtual<uint32_t>(old, 0x8);
    }
    Virtual<uint8_t>(com, 0xC, uint32_t{0x6A26D049}, uint32_t{0x6A26D050}, &point);
}

void SetPoint(void* point, int32_t x, int32_t z)
{
    Virtual<void>(point, 0xC, static_cast<float>(x), static_cast<float>(z));
}

// The game's 0x0041B8D0.
void __fastcall PublishTransportCounts(char*, void*)
{
    void* const traffic = Global<void*>(addr::SC4_Service43DA8);
    void* const city = City();
    void* const system = System();
    char* const stats = Virtual<char*>(traffic, 0x88); // GetTrafficStats

    static const NamedField counts[] = {
        {"g_monorail_station_count", 0x188},
        {"g_elevated_station_count", 0x174},
        {"g_bus_station_count", 0x138},
        {"g_train_station_count", 0x14C},
        {"g_subway_station_count", 0x160},
        {"g_elevated_station_count", 0x174},
        {"g_train_freight_depot_count", 0x19C},
        {"g_num_rapidtransit_transitions", 0x1B0},
        {"g_pothole_count", 0x134},
        {"g_road_tile_count", 0x118},
        {"g_rail_tile_count", 0x11A},
        {"g_subway_tile_count", 0x126},
        {"g_highway_tile_count", 0x11C},
        {"g_street_tile_count", 0x11E},
        {"g_avenue_tile_count", 0x124},
        {"g_elevated_rail_tile_count", 0x128},
        {"g_monorail_tile_count", 0x12A},
        {"g_dirtroad_tile_count", 0x12E},
        {"g_groundhighway_tile_count", 0x130},
    };
    for (const NamedField& count : counts)
        Number(system, count.name, static_cast<double>(static_cast<int32_t>(At<uint16_t>(stats, count.at))));

    struct Query {
        const char* name;
        int32_t sizes;
        int32_t stages;
    };
    static const Query airports[] = {
        {"g_airport_count", 0xE, 0x1F},          {"g_medium_airport_count", 0x4, 0x1F},
        {"g_small_airport_count", 0x2, 0x1F},    {"g_large_airport_count", 0x8, 0x1F},
        {"g_medium_airport_s1_count", 0x4, 0x1}, {"g_small_airport_s1_count", 0x2, 0x1},
        {"g_large_airport_s1_count", 0x8, 0x1},  {"g_medium_airport_s2_count", 0x4, 0x2},
        {"g_small_airport_s2_count", 0x2, 0x2},  {"g_large_airport_s2_count", 0x8, 0x2},
        {"g_medium_airport_s3_count", 0x4, 0x4}, {"g_small_airport_s3_count", 0x2, 0x4},
        {"g_large_airport_s3_count", 0x8, 0x4},
    };
    static const Query seaports[] = {
        {"g_seaport_count", 0xE, 0x1F},
        {"g_seaport_s1_count", 0xE, 0x1},
        {"g_seaport_s2_count", 0xE, 0x2},
        {"g_seaport_s3_count", 0xE, 0x4},
    };
    void* counter = Virtual<void*>(Virtual<void*>(city, 0x134), 0x28); // the airport developer
    for (const Query& query : airports)
        Number(system, query.name, static_cast<double>(Virtual<int32_t>(counter, 0x24, query.sizes, query.stages)));
    counter = Virtual<void*>(Virtual<void*>(city, 0x130), 0x28); // the seaport developer
    for (const Query& query : seaports)
        Number(system, query.name, static_cast<double>(Virtual<int32_t>(counter, 0x24, query.sizes, query.stages)));

    const uint32_t volume = Virtual<uint32_t>(Virtual<void*>(city, 0x130), 0x34);
    Number(system, "g_seaport_volume", static_cast<double>(volume));
}

// A station kind's use in percent, cut to a whole number, at its busiest
// station.
void StationUse(void* system, char* stats, void*& point, const char* name, std::size_t at)
{
    Renew(point);
    const int32_t use = Ftol(Percent(stats, at + 0x8));
    SetPoint(point, At<int32_t>(stats, at), At<int32_t>(stats, at + 0x4));
    Located(system, name, static_cast<double>(use), point);
}

// A network's congestion in percent at its worst cell.
void WorstCongestion(void* system, char* stats, void*& point, const char* name, std::size_t at)
{
    Renew(point);
    SetPoint(point, At<int32_t>(stats, at), At<int32_t>(stats, at + 0x4));
    Located(system, name, Percent(stats, at + 0x8), point);
}

// The first of the service's ports (taken from the back) whose slot 0x70
// says no, published with 1; without one, 0 and no place.
void NoRoadAccess(void* system, void* service, void*& point, const char* name)
{
    struct {
        void** begin;
        void** end;
        void** capacity;
    } ports = {nullptr, nullptr, nullptr};
    bool found = false;
    Virtual<void>(service, 0x20, &ports);
    if (ports.begin != ports.end) {
        do {
            void* const port = ports.end[-1];
            if (!found && !Virtual<uint8_t>(port, 0x70)) {
                Renew(point);
                int32_t x = 0;
                int32_t z = 0;
                Virtual<void>(port, 0x54, &x, &z);
                SetPoint(point, x, z);
                found = true;
                Located(system, name, 1.0, point);
            }
            Virtual<uint32_t>(port, 0x8);
            --ports.end;
        } while (ports.begin != ports.end);
    }
    if (!found)
        Located(system, name, 0.0, static_cast<void*>(nullptr));
    if (ports.begin)
        reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(ports.begin);
}

// Empties the list the connection service fills for `kind`, from the back,
// and returns the highest congestion at a connection's cell; (x, z) change
// only when a cell is above zero and above the ones before.
int32_t ConnectionCongestion(void* connections, int32_t kind, Node*& list, void* congestion, int32_t& x, int32_t& z)
{
    int32_t worst = 0;
    Virtual<void>(connections, 0x44, kind, &list);
    while (list->next != list) {
        int32_t cellX = 0;
        int32_t cellZ = 0;
        if (Virtual<uint8_t>(list->previous->item, 0x1C, &cellX, &cellZ)) {
            const int32_t value = Virtual<uint8_t>(congestion, 0x1C, cellX, cellZ);
            if (value > worst) {
                x = cellX;
                z = cellZ;
                worst = value;
            }
        }
        Node* const node = list->previous;
        node->previous->next = node->next;
        node->next->previous = node->previous;
        if (node->item)
            Virtual<uint32_t>(node->item, 0x8);
        Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0x10, node);
    }
    return worst;
}

// The game's 0x0041C1A0.
void __fastcall PublishTransport(char* self, void*)
{
    void* const system = System();
    void* const traffic = Global<void*>(addr::SC4_Service43DA8);
    char* const stats = Virtual<char*>(traffic, 0x88); // GetTrafficStats

    Number(system, "g_road_funding_p", Percent(stats, 0x228));
    Number(system, "g_masstransit_funding_p", Percent(stats, 0x22C));
    Number(system, "g_transit_strike", static_cast<double>(int32_t{At<uint8_t>(stats, 0x230) != 0}));
    Number(system, "ga_freight_trip_length", static_cast<double>(static_cast<int32_t>(At<uint8_t>(stats, 0x238))));
    Number(system, "ga_rail_congestion", Percent(stats, 0x14));
    Number(system, "ga_monorail_station_utilization_p", Percent(stats, 0x18C));
    Number(system, "ga_elevated_station_utilization_p", Percent(stats, 0x178));
    Number(system, "ga_bus_station_utilization_p", Percent(stats, 0x13C));
    Number(system, "ga_train_station_utilization_p", Percent(stats, 0x150));
    Number(system, "ga_subway_station_utilization_p", Percent(stats, 0x164));
    Number(system, "ga_elevated_station_utilization_p", Percent(stats, 0x178));
    Number(system, "ga_monorail_station_utilization_p", Percent(stats, 0x18C));

    void* point = nullptr;
    StationUse(system, stats, point, "l_monorail_station_utilization_pl", 0x190);
    StationUse(system, stats, point, "l_elevated_station_utilization_pl", 0x17C);
    StationUse(system, stats, point, "l_bus_station_utilization_pl", 0x140);
    StationUse(system, stats, point, "l_train_station_utilization_pl", 0x154);
    StationUse(system, stats, point, "l_subway_station_utilization_pl", 0x168);
    StationUse(system, stats, point, "l_elevated_station_utilization_pl", 0x17C);
    StationUse(system, stats, point, "l_monorail_station_utilization_pl", 0x190);

    struct Network {
        const char* average;
        const char* tiles;
        std::size_t at;
    };
    static const Network networks[] = {
        {"ga_road_congestion", "g_road_congestion_tile_count", 0x0},
        {"ga_rail_congestion", "g_rail_congestion_tile_count", 0x14},
        {"ga_subway_congestion", "g_subway_congestion_tile_count", 0x8C},
        {"ga_elevated_congestion", "g_elevated_congestion_tile_count", 0xA0},
        {"ga_monorail_congestion", "g_monorail_congestion_tile_count", 0xB4},
        {"ga_street_congestion", "g_street_congestion_tile_count", 0x3C},
        {"ga_avenue_congestion", "g_avenue_congestion_tile_count", 0x78},
        {"ga_bridge_congestion", "g_bridge_congestion_tile_count", 0x104},
    };
    for (const Network& network : networks) {
        Number(system, network.average, Percent(stats, network.at));
        Number(system, network.tiles, static_cast<double>(At<uint32_t>(stats, network.at + 0x4)));
    }

    WorstCongestion(system, stats, point, "l_road_congestion_h", 0x8);
    WorstCongestion(system, stats, point, "l_rail_congestion_h", 0x1C);
    WorstCongestion(system, stats, point, "l_subway_congestion_h", 0x94);
    WorstCongestion(system, stats, point, "l_street_congestion_h", 0x44);
    WorstCongestion(system, stats, point, "l_avenue_congestion_h", 0x80);
    WorstCongestion(system, stats, point, "l_elevated_congestion_h", 0xA8);
    WorstCongestion(system, stats, point, "l_monorail_congestion_h", 0xBC);
    WorstCongestion(system, stats, point, "l_bridge_congestion_h", 0x10C);

    float efficiency;
    const float* result = Virtual<const float*>(Virtual<void*>(City(), 0x134), 0x14, &efficiency);
    Number(system, "ga_airport_efficiency", static_cast<double>(*result) * 100.0);
    result = Virtual<const float*>(Virtual<void*>(City(), 0x130), 0x14, &efficiency);
    Number(system, "ga_seaport_efficiency", static_cast<double>(*result) * 100.0);

    NoRoadAccess(system, Global<void*>(addr::SC4_Service43D2C), point, "l_airport_no_road_access");
    NoRoadAccess(system, Global<void*>(addr::SC4_Service43D30), point, "l_seaport_no_road_access");

    // Neighbour connections by kind (2 road, 8 avenue, 1 rail). The avenue
    // pass does not reset the place: with no congested avenue connection it
    // publishes the road pass's place.
    void* const connections = Global<void*>(addr::SC4_MonthEndService);
    Node* list = Virtual<Node*>(NodeAllocatorAt(addr::SC4List_NodeAllocator3), 0xC, int32_t{0xC});
    list->next = list;
    list->previous = list;
    void* const congestion = Virtual<void*>(traffic, 0x28); // the congestion grid
    int32_t x = 0;
    int32_t z = 0;
    int32_t worst = ConnectionCongestion(connections, 2, list, congestion, x, z);
    Renew(point);
    SetPoint(point, x, z);
    Located(system, "l_road_connection_congestion_h", static_cast<double>(worst), point);

    worst = ConnectionCongestion(connections, 8, list, congestion, x, z);
    Renew(point);
    SetPoint(point, x, z);
    Located(system, "l_avenue_connection_congestion_h", static_cast<double>(worst), point);

    Virtual<void>(connections, 0x44, int32_t{1}, &list);
    uint32_t count = 0;
    for (Node* node = list->next; node != list; node = node->next)
        ++count;
    Number(system, "g_rail_neighbor_connection_count", static_cast<double>(count));

    PublishTransportCounts(self, nullptr);

    reinterpret_cast<void(__thiscall*)(Node**)>(addr::SC4List_Destroy3)(&list);
    if (point)
        Virtual<uint32_t>(point, 0x8);
}

bool InstallTransport(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[2] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::ScriptGlobals_PublishTransportCounts, reinterpret_cast<void*>(&PublishTransportCounts)},
        {addr::ScriptGlobals_PublishTransport, reinterpret_cast<void*>(&PublishTransport)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_transportRegistered = registry::Register({
    "scripts.globals.transport",
    "scripts/globals",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::ScriptGlobals_PublishTransport,
    "Our publishers of the advisors' transport script variables",
    false,
    InstallTransport,
});

} // namespace
} // namespace opensc4::script_globals
