// Script variables the advisors publish for their scripts: three advisor
// functions that read figures from the simulators and store them in the
// advisor system under names like `g_city_r_population`. Replacement, off
// by default:
//
//   scripts.globals.city: the city figures (mayor rating, population,
//     demand, zones, neighbours; 0x00412530, slot 9 of the advisor vtable
//     0x00A81514), that advisor's park figures (0x00411BC0) and the budget
//     figures (0x00413A10, slot 9 of the advisor vtable 0x00A81D4C).
//
// The lists' helpers and the services stay the game's.
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

constexpr uint32_t kCity = 0;
constexpr uint32_t kRegion = 0x20000;

using ListFn = void(__thiscall*)(void** list);

// Sets a number in the advisor system (slot 13).
void Number(void* system, const char* name, double value)
{
    Virtual<uint8_t>(system, 0x34, name, value);
}

// A demand simulator's object for a group in the city or the region (slot 6).
void* Demand(void* demand, uint32_t group, uint32_t area)
{
    return Virtual<void*>(demand, 0x18, group, area);
}

// Sets a mayor rating with the cell it is found at: the cell goes into the
// object kept at `point` (class 0x6A26D049, made anew each time).
void RatingAt(void* system, const char* name, int8_t rating, const int32_t* cell, void*& point)
{
    void* const com = reinterpret_cast<void*(__cdecl*)()>(addr::RZGetCOM)();
    if (void* const old = point) {
        point = nullptr;
        Virtual<uint32_t>(old, 0x8);
    }
    Virtual<uint8_t>(com, 0xC, uint32_t{0x6A26D049}, uint32_t{0x6A26D050}, &point);
    Virtual<void>(point, 0xC, static_cast<float>(cell[0]), static_cast<float>(cell[1]));
    Virtual<uint8_t>(system, 0x38, name, static_cast<double>(rating), point);
}

void* NewList(std::uintptr_t allocator)
{
    void* const head = Virtual<void*>(NodeAllocatorAt(allocator), 0xC, uint32_t{0xC});
    At<void*>(head, 0x0) = head;
    At<void*>(head, 0x4) = head;
    return head;
}

uint32_t Count(void* head)
{
    uint32_t count = 0;
    for (void* node = At<void*>(head, 0x0); node != head; node = At<void*>(node, 0x0))
        ++count;
    return count;
}

// Slot 17 less slot 9 of the extrapolation object for a group.
int32_t Extrapolated(void* object, uint32_t group)
{
    const uint32_t ahead = Virtual<uint32_t>(object, 0x44, group);
    const uint32_t now = Virtual<uint32_t>(object, 0x24, group);
    return static_cast<int32_t>(ahead - now);
}

int32_t Sum(int32_t a, int32_t b)
{
    return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

// A funding percentage of a department budget: its slot 10 gives a float.
double FundingPercent(uint32_t department)
{
    void* const budget = Virtual<void*>(Global<void*>(addr::SC4_BudgetSimulator), 0x70, department);
    const float* const share = Virtual<const float*>(budget, 0x28);
    return static_cast<double>(*share) * 100.0;
}

// The game's 0x00411BC0.
void __fastcall PublishParks(char* self, void*)
{
    void* const system = Global<void*>(addr::cSC4AdvisorSystem_Instance);
    Number(system, "g_parks_funding_p", FundingPercent(0x8A4260A6));
    Number(system, "g_landmarks_funding_p", FundingPercent(0x8A416368));
    Number(system, "g_num_buildings", static_cast<double>(At<int32_t>(self, 0xC4)));
    Number(system, "g_num_parks", static_cast<double>(At<int32_t>(self, 0xC8)));
    Number(system, "g_num_recreation", static_cast<double>(At<int32_t>(self, 0xCC)));
}

// The game's 0x00412530.
void __fastcall PublishCity(char* self, void*)
{
    void* const city = Global<void*>(addr::SC4_City);
    void* const zones = Global<void*>(addr::SC4_ZoneManager);
    void* const system = Global<void*>(addr::cSC4AdvisorSystem_Instance);
    void* const demand = Global<void*>(addr::SC4_Service43D74);
    void* point = nullptr;
    int32_t cell[2]; // left as it is when the service does not fill it, as in the game

    // The mayor rating: the city's, then the lowest and the highest with their cells.
    const int8_t rating = Virtual<int8_t>(Global<void*>(addr::SC4_Service43D60), 0x20);
    Number(system, "ga_mayor_rating", static_cast<double>(rating));
    const int8_t lowest = Virtual<int8_t>(Global<void*>(addr::SC4_Service43D60), 0x28, &cell[0]);
    RatingAt(system, "l_mayor_rating_l", lowest, cell, point);
    const int8_t highest = Virtual<int8_t>(Global<void*>(addr::SC4_Service43D60), 0x24, &cell[0]);
    RatingAt(system, "l_mayor_rating_h", highest, cell, point);

    // Population of the city.
    double r = Virtual<double>(Demand(demand, 0x1001, kCity), 0x20);
    double c = Virtual<double>(Demand(demand, 0x3001, kCity), 0x20);
    double i = Virtual<double>(Demand(demand, 0x4001, kCity), 0x20);
    Number(system, "g_city_r_population", r);
    Number(system, "g_city_c_population", c);
    Number(system, "g_city_i_population", i);
    Number(system, "g_city_rci_population", i + c + r);
    Number(system, "g_city_r1_population", Virtual<double>(Demand(demand, 0x1011, kCity), 0x20));
    Number(system, "g_city_r2_population", Virtual<double>(Demand(demand, 0x1021, kCity), 0x20));
    Number(system, "g_city_r3_population", Virtual<double>(Demand(demand, 0x1031, kCity), 0x20));
    Number(system, "g_city_ir_population", static_cast<double>(Virtual<uint32_t>(demand, 0x24, uint32_t{0x4101})));
    Number(system, "g_city_id_population", static_cast<double>(Virtual<uint32_t>(demand, 0x24, uint32_t{0x4201})));
    Number(system, "g_city_im_population", static_cast<double>(Virtual<uint32_t>(demand, 0x24, uint32_t{0x4301})));
    Number(system, "g_city_iht_population", static_cast<double>(Virtual<uint32_t>(demand, 0x24, uint32_t{0x4401})));
    Number(system, "g_city_co3_population", static_cast<double>(Virtual<uint32_t>(demand, 0x24, uint32_t{0x3331})));
    Number(system, "g_city_co2_population", static_cast<double>(Virtual<uint32_t>(demand, 0x24, uint32_t{0x3321})));
    Number(system, "g_city_cs1_population", static_cast<double>(Virtual<uint32_t>(demand, 0x24, uint32_t{0x3111})));
    Number(system, "g_city_cs2_population", static_cast<double>(Virtual<uint32_t>(demand, 0x24, uint32_t{0x3121})));
    Number(system, "g_city_cs3_population", static_cast<double>(Virtual<uint32_t>(demand, 0x24, uint32_t{0x3131})));

    // Population of the region.
    r = Virtual<double>(Demand(demand, 0x1001, kRegion), 0x20);
    c = Virtual<double>(Demand(demand, 0x3001, kRegion), 0x20);
    i = Virtual<double>(Demand(demand, 0x4001, kRegion), 0x20);
    Number(system, "g_region_r_population", r);
    Number(system, "g_region_c_population", c);
    Number(system, "g_region_i_population", i);
    Number(system, "g_region_rci_population", i + c + r);
    Number(system, "g_city_workforce_population", Virtual<double>(Demand(demand, 0x2100, kCity), 0x20));
    Number(system, "g_region_workforce_population", Virtual<double>(Demand(demand, 0x2100, kRegion), 0x20));

    // Demand (slot 9), active demand (slot 12) and the caps (slot 26, a float by pointer).
    Number(system, "g_r1_demand", Virtual<double>(Demand(demand, 0x1010, kRegion), 0x24));
    Number(system, "g_r2_demand", Virtual<double>(Demand(demand, 0x1020, kRegion), 0x24));
    Number(system, "g_r3_demand", Virtual<double>(Demand(demand, 0x1030, kRegion), 0x24));
    Number(system, "g_co2_demand", Virtual<double>(Demand(demand, 0x3320, kRegion), 0x24));
    Number(system, "g_co3_demand", Virtual<double>(Demand(demand, 0x3330, kRegion), 0x24));
    Number(system, "g_r1_active_demand", Virtual<double>(Demand(demand, 0x1010, kRegion), 0x30));
    Number(system, "g_r2_active_demand", Virtual<double>(Demand(demand, 0x1020, kRegion), 0x30));
    Number(system, "g_r3_active_demand", Virtual<double>(Demand(demand, 0x1030, kRegion), 0x30));
    Number(system, "g_co2_active_demand", Virtual<double>(Demand(demand, 0x3320, kRegion), 0x30));
    Number(system, "g_co3_active_demand", Virtual<double>(Demand(demand, 0x3330, kRegion), 0x30));
    Number(system, "g_id_active_demand", Virtual<double>(Demand(demand, 0x4200, kRegion), 0x30));
    Number(system, "g_im_active_demand", Virtual<double>(Demand(demand, 0x4300, kRegion), 0x30));
    Number(system, "g_iht_active_demand", Virtual<double>(Demand(demand, 0x4400, kRegion), 0x30));
    Number(system, "g_workforce_demand", Virtual<double>(Demand(demand, 0x2000, kRegion), 0x24));
    Number(system, "g_workforce_active_demand", Virtual<double>(Demand(demand, 0x2000, kRegion), 0x30));
    const auto cap = [demand](uint32_t group) {
        const float* const value = Virtual<const float*>(Demand(demand, group, kRegion), 0x68);
        return static_cast<double>(*value) * 100.0;
    };
    Number(system, "g_current_id_cap", cap(0x4200));
    Number(system, "g_current_im_cap", cap(0x4300));
    Number(system, "g_current_iht_cap", cap(0x4400));
    Number(system, "g_current_co2_cap", cap(0x3320));
    Number(system, "g_current_co3_cap", cap(0x3330));
    Number(system, "g_current_r1_cap", cap(0x1010));
    Number(system, "g_current_r2_cap", cap(0x1020));
    Number(system, "g_current_r3_cap", cap(0x1030));
    Number(system, "g_tax_rate_neutral", Virtual<double>(demand, 0x28));

    // Zoned tiles by zone type.
    Number(system, "g_num_rzone_ld_tiles", static_cast<double>(Virtual<int32_t>(zones, 0x18, int32_t{1})));
    Number(system, "g_num_rzone_md_tiles", static_cast<double>(Virtual<int32_t>(zones, 0x18, int32_t{2})));
    Number(system, "g_num_rzone_hd_tiles", static_cast<double>(Virtual<int32_t>(zones, 0x18, int32_t{3})));
    Number(system, "g_num_czone_ld_tiles", static_cast<double>(Virtual<int32_t>(zones, 0x18, int32_t{4})));
    Number(system, "g_num_czone_md_tiles", static_cast<double>(Virtual<int32_t>(zones, 0x18, int32_t{5})));
    Number(system, "g_num_czone_hd_tiles", static_cast<double>(Virtual<int32_t>(zones, 0x18, int32_t{6})));
    Number(system, "g_num_izone_r_tiles", static_cast<double>(Virtual<int32_t>(zones, 0x18, int32_t{7})));
    Number(system, "g_num_izone_l_tiles", static_cast<double>(Virtual<int32_t>(zones, 0x18, int32_t{8})));
    Number(system, "g_num_izone_h_tiles", static_cast<double>(Virtual<int32_t>(zones, 0x18, int32_t{9})));

    // Neighbour connections by kind, then neighbouring cities.
    const auto clear3 = reinterpret_cast<ListFn>(addr::SC4List_ClearReleasing);
    const auto clear4 = reinterpret_cast<ListFn>(addr::SC4List_ClearReleasing4);
    void* connections = NewList(addr::SC4List_NodeAllocator3);
    Virtual<void>(Global<void*>(addr::SC4_MonthEndService), 0x44, int32_t{1}, &connections);
    Number(system, "g_num_rail_neighbors", static_cast<double>(Count(connections)));
    clear3(&connections);
    Virtual<void>(Global<void*>(addr::SC4_MonthEndService), 0x44, int32_t{2}, &connections);
    Number(system, "g_num_road_neighbors", static_cast<double>(Count(connections)));
    clear3(&connections);
    Virtual<void>(Global<void*>(addr::SC4_MonthEndService), 0x44, int32_t{8}, &connections);
    Number(system, "g_num_avenue_neighbors", static_cast<double>(Count(connections)));

    void* cities = NewList(addr::SC4List_NodeAllocator4);
    clear4(&cities);
    Virtual<void>(Global<void*>(addr::SC4_MonthEndService), 0x54, &cities, int32_t{0});
    Number(system, "g_num_cities_connected", static_cast<double>(Count(cities)));
    clear4(&cities);
    Virtual<void>(Global<void*>(addr::SC4_MonthEndService), 0x50, &cities, int32_t{0});
    Number(system, "g_num_cities_adjacent", static_cast<double>(Count(cities)));
    clear4(&cities);
    Virtual<void>(Global<void*>(addr::SC4_MonthEndService), 0x58, &cities, int32_t{0});
    Number(system, "g_num_cities_connected_indirectly", static_cast<double>(Count(cities)));

    void* const coast = Virtual<void*>(city, 0x130);
    Number(system, "g_city_has_sea", Virtual<uint8_t>(coast, 0x3C) != 0 ? 1.0 : 0.0);

    // Extrapolated demand.
    void* const extrapolation = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x90);
    const int32_t r1 = Extrapolated(extrapolation, 0x1010);
    Number(system, "g_r1_demand_extrap", static_cast<double>(r1));
    const int32_t r2 = Extrapolated(extrapolation, 0x1020);
    Number(system, "g_r2_demand_extrap", static_cast<double>(r2));
    const int32_t r3 = Extrapolated(extrapolation, 0x1030);
    Number(system, "g_r3_demand_extrap", static_cast<double>(r3));
    Number(system, "g_r_demand_extrap", static_cast<double>(Sum(Sum(r3, r2), r1)));
    const int32_t cs1 = Extrapolated(extrapolation, 0x3110);
    Number(system, "g_cs1_demand_extrap", static_cast<double>(cs1));
    const int32_t cs2 = Extrapolated(extrapolation, 0x3120);
    Number(system, "g_cs2_demand_extrap", static_cast<double>(cs2));
    const int32_t cs3 = Extrapolated(extrapolation, 0x3130);
    Number(system, "g_cs3_demand_extrap", static_cast<double>(cs3));
    const int32_t co2 = Extrapolated(extrapolation, 0x3320);
    Number(system, "g_co2_demand_extrap", static_cast<double>(co2));
    const int32_t co3 = Extrapolated(extrapolation, 0x3330);
    Number(system, "g_co3_demand_extrap", static_cast<double>(co3));
    Number(system, "g_c_demand_extrap", static_cast<double>(Sum(Sum(Sum(Sum(co3, co2), cs3), cs2), cs1)));
    const int32_t ir = Extrapolated(extrapolation, 0x4100);
    Number(system, "g_ir_demand_extrap", static_cast<double>(ir));
    const int32_t id = Extrapolated(extrapolation, 0x4200);
    Number(system, "g_id_demand_extrap", static_cast<double>(id));
    const int32_t im = Extrapolated(extrapolation, 0x4300);
    Number(system, "g_im_demand_extrap", static_cast<double>(im));
    const int32_t ih = Extrapolated(extrapolation, 0x4400);
    Number(system, "g_ih_demand_extrap", static_cast<double>(ih));
    Number(system, "g_i_demand_extrap", static_cast<double>(Sum(Sum(Sum(ih, im), id), ir)));

    // The city's slot 90 result goes through a float, then times slot 89's.
    const float side = static_cast<float>(Virtual<double>(city, 0x168));
    Number(system, "g_city_tile_size", Virtual<double>(city, 0x164) * static_cast<double>(side));
    const int32_t water = Virtual<int32_t>(Global<void*>(addr::SC4_Terrain), 0xD8);
    Number(system, "g_water_tile_count", static_cast<double>(water));

    PublishParks(self, nullptr);

    reinterpret_cast<ListFn>(addr::SC4List_Destroy4)(&cities);
    reinterpret_cast<ListFn>(addr::SC4List_Destroy3)(&connections);
    if (point)
        Virtual<uint32_t>(point, 0x8);
}

// The game's 0x00413A10.
void __fastcall PublishBudget(char*, void*)
{
    void* const system = Global<void*>(addr::cSC4AdvisorSystem_Instance);
    void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
    Number(system, "g_funds", static_cast<double>(Virtual<int64_t>(budget, 0x18)));
    Number(system, "g_income_monthly", static_cast<double>(Virtual<int64_t>(budget, 0x40)));
    Number(system, "g_expense_monthly", static_cast<double>(Virtual<int64_t>(budget, 0x38)));
    Number(system, "g_tax_rate_r_low", Virtual<double>(budget, 0x90, int32_t{0}));
    Number(system, "g_tax_rate_r_med", Virtual<double>(budget, 0x90, int32_t{1}));
    Number(system, "g_tax_rate_r_high", Virtual<double>(budget, 0x90, int32_t{2}));
    Number(system, "g_tax_rate_cs_low", Virtual<double>(budget, 0x90, int32_t{3}));
    Number(system, "g_tax_rate_cs_med", Virtual<double>(budget, 0x90, int32_t{4}));
    Number(system, "g_tax_rate_cs_high", Virtual<double>(budget, 0x90, int32_t{5}));
    Number(system, "g_tax_rate_co_med", Virtual<double>(budget, 0x90, int32_t{6}));
    Number(system, "g_tax_rate_co_high", Virtual<double>(budget, 0x90, int32_t{7}));
    Number(system, "g_tax_rate_i_resource", Virtual<double>(budget, 0x90, int32_t{8}));
    Number(system, "g_tax_rate_i_dirty", Virtual<double>(budget, 0x90, int32_t{9}));
    Number(system, "g_tax_rate_i_manufacturing", Virtual<double>(budget, 0x90, int32_t{10}));
    Number(system, "g_tax_rate_i_hightech", Virtual<double>(budget, 0x90, int32_t{11}));
    Number(system, "g_tax_income", static_cast<double>(Virtual<int64_t>(budget, 0x4C)));
    Number(system, "g_borrowed", static_cast<double>(Virtual<int64_t>(budget, 0xA4)));
    Number(system, "g_borrowing_limit", static_cast<double>(Virtual<int64_t>(budget, 0xA8)));
    Number(system, "g_bond_payments_monthly", static_cast<double>(Virtual<int64_t>(budget, 0xB4)));
    void* const neighbors = Global<void*>(addr::SC4_MonthEndService);
    Number(system, "g_nd_income", static_cast<double>(Virtual<int64_t>(neighbors, 0x80)));
    Number(system, "g_nd_expense", static_cast<double>(Virtual<int64_t>(neighbors, 0x84)));
}

bool InstallCity(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[3] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::ScriptGlobals_PublishParkGlobals, reinterpret_cast<void*>(&PublishParks)},
        {addr::ScriptGlobals_PublishCityGlobals, reinterpret_cast<void*>(&PublishCity)},
        {addr::ScriptGlobals_PublishBudgetGlobals, reinterpret_cast<void*>(&PublishBudget)},
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

const bool g_cityRegistered = registry::Register({
    "scripts.globals.city",
    "scripts/globals",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::ScriptGlobals_PublishCityGlobals,
    "The advisors' city, park and budget script variables",
    false,
    InstallCity,
});

} // namespace
} // namespace opensc4::script_globals
