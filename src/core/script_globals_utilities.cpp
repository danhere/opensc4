// The advisors' script variables for the utilities (docs/lua.md): power,
// water, garbage and pollution figures, the utility buildings' counts and
// the neighbour deals, read from the simulators and stored in the advisor
// system under names like `g_power_consumed`. Replacement, off by default:
//
//   scripts.globals.utilities: the three publishers (0x004220A0 and the two
//     it calls last, 0x00421030 and 0x00421AB0).
//
// The lists' own helpers (Clear, Destroy) stay the game's.
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

constexpr std::size_t kDirty = 0xBC; // the publisher's flags of figures to refresh

// The data object the advisor system keeps with a number (a history or a
// percentage with its source).
constexpr uint32_t kDataClass = 0x6A26D049;
constexpr uint32_t kDataInterface = 0x6A26D050;

void* GetCOM() { return reinterpret_cast<void*(__cdecl*)()>(addr::RZGetCOM)(); }

void ReleaseAndClear(void*& ref)
{
    if (void* const held = ref) {
        ref = nullptr;
        Virtual<uint32_t>(held, 0x8);
    }
}

// Makes a new data object in `data` for `source` (when there is one) and
// hands it the source's interface 1.
void MakeData(void*& data, void* source)
{
    ReleaseAndClear(data);
    if (!source)
        return;
    void* const com = GetCOM();
    ReleaseAndClear(data);
    Virtual<uint8_t>(com, 0xC, kDataClass, kDataInterface, &data);
    void* inner = nullptr;
    Virtual<uint8_t>(source, 0x0, uint32_t{1}, &inner);
    Virtual<uint32_t>(data, 0x1C, inner);
    if (inner)
        Virtual<uint32_t>(inner, 0x8);
}

// The game's 0x00421030: the power plants, the water buildings and the
// garbage buildings by number, with the worst funded and the oldest.
void __fastcall PublishUtilityBuildings(char* self, void*)
{
    void* const pollution = Global<void*>(addr::SC4_PollutionSimulator);
    void* const water = Global<void*>(addr::SC4_Service43D94);
    void* const system = Global<void*>(addr::cSC4AdvisorSystem_Instance);
    void* const power = Global<void*>(addr::SC4_Service43DA0);
    void* data = nullptr;

    Virtual<uint8_t>(system, 0x34, "g_power_plant_count", static_cast<double>(Virtual<int32_t>(power, 0x60)));
    Virtual<uint8_t>(system, 0x34, "g_power_plant_count_coal",
                     static_cast<double>(Virtual<int32_t>(power, 0x5C, int32_t{1})));
    Virtual<uint8_t>(system, 0x34, "g_power_plant_count_gas",
                     static_cast<double>(Virtual<int32_t>(power, 0x5C, int32_t{3})));
    Virtual<uint8_t>(system, 0x34, "g_power_plant_count_oil",
                     static_cast<double>(Virtual<int32_t>(power, 0x5C, int32_t{6})));
    Virtual<uint8_t>(system, 0x34, "g_power_plant_count_wind",
                     static_cast<double>(Virtual<int32_t>(power, 0x5C, int32_t{9})));
    Virtual<uint8_t>(system, 0x34, "g_power_plant_count_solar",
                     static_cast<double>(Virtual<int32_t>(power, 0x5C, int32_t{7})));
    Virtual<uint8_t>(system, 0x34, "g_power_plant_count_nuclear",
                     static_cast<double>(Virtual<int32_t>(power, 0x5C, int32_t{5})));
    Virtual<uint8_t>(system, 0x34, "g_power_plant_count_waste",
                     static_cast<double>(Virtual<int32_t>(power, 0x5C, int32_t{8})));
    Virtual<uint8_t>(system, 0x34, "g_power_plant_count_fusion",
                     static_cast<double>(Virtual<int32_t>(power, 0x5C, int32_t{2})));
    Virtual<uint8_t>(system, 0x34, "g_aged_power_plant_count", static_cast<double>(Virtual<int32_t>(power, 0x70)));
    Virtual<uint8_t>(system, 0x34, "g_power_pole_count", static_cast<double>(Virtual<int32_t>(power, 0x68)));

    // The worst funded plant and the oldest one.
    float funding = Global<float>(addr::SC4_FloatOneB07D3C);
    void* leastFunded = nullptr;
    void* oldest = nullptr;
    int32_t age = 0;
    Virtual<uint32_t>(power, 0x78, &leastFunded, &funding, &oldest, &age);
    MakeData(data, leastFunded);
    Virtual<uint8_t>(system, 0x38, "l_power_plant_funding_pl", static_cast<double>(funding) * 100.0, data);
    MakeData(data, oldest);
    Virtual<uint8_t>(system, 0x38, "l_power_plant_age_h", static_cast<double>(age), data);

    Virtual<uint8_t>(system, 0x34, "g_water_source_count", static_cast<double>(Virtual<int32_t>(water, 0x30)));
    Virtual<uint8_t>(system, 0x34, "g_water_pipe_count", static_cast<double>(Virtual<int32_t>(water, 0x48)));
    Virtual<uint8_t>(system, 0x34, "g_water_distressed_pipe_count",
                     static_cast<double>(Virtual<int32_t>(water, 0x4C)));
    Virtual<uint8_t>(system, 0x34, "g_water_burst_pipe_count", static_cast<double>(Virtual<int32_t>(water, 0x50)));
    Virtual<uint8_t>(system, 0x34, "g_watered_building_count", static_cast<double>(Virtual<int32_t>(water, 0x54)));
    Virtual<uint8_t>(system, 0x34, "g_unwatered_building_count",
                     static_cast<double>(Virtual<int32_t>(water, 0x58)));
    Virtual<uint8_t>(system, 0x34, "g_water_pollution_pump_shutdown_count",
                     static_cast<double>(Virtual<int32_t>(water, 0x34)));

    // The same pair for the water buildings.
    funding = Global<float>(addr::SC4_FloatOneB07D3C);
    age = 0;
    ReleaseAndClear(oldest);
    ReleaseAndClear(leastFunded);
    Virtual<uint32_t>(water, 0x6C, &leastFunded, &funding, &oldest, &age);
    MakeData(data, leastFunded);
    Virtual<uint8_t>(system, 0x38, "l_water_building_funding_pl", static_cast<double>(funding) * 100.0, data);
    MakeData(data, oldest);
    Virtual<uint8_t>(system, 0x38, "l_water_building_age_h", static_cast<double>(age), data);

    Virtual<uint8_t>(system, 0x34, "g_incinerator_count", static_cast<double>(Virtual<int32_t>(pollution, 0xA8)));
    Virtual<uint8_t>(system, 0x34, "g_waste_to_energy_building_count",
                     static_cast<double>(Virtual<int32_t>(pollution, 0xAC)));
    Virtual<uint8_t>(system, 0x34, "g_water_treatment_plant_count",
                     static_cast<double>(Virtual<int32_t>(pollution, 0xB4)));
    Virtual<uint8_t>(system, 0x34, "g_recycling_center_count",
                     static_cast<double>(Virtual<int32_t>(pollution, 0xB0)));

    At<uint32_t>(self, kDirty) &= 0xFFFFFFFCu;
    if (leastFunded)
        Virtual<uint32_t>(leastFunded, 0x8);
    if (oldest)
        Virtual<uint32_t>(oldest, 0x8);
    if (data)
        Virtual<uint32_t>(data, 0x8);
}

// A new empty list head (12 bytes, linked to itself) from the allocator
// cached at `global`.
char* NewListHead(std::uintptr_t global)
{
    char* const head = Virtual<char*>(NodeAllocatorAt(global), 0xC, uint32_t{0xC});
    At<char*>(head, 0x0) = head;
    At<char*>(head, 0x4) = head;
    return head;
}

uint32_t ListCount(char* head)
{
    uint32_t count = 0;
    for (char* node = At<char*>(head, 0x0); node != head; node = At<char*>(node, 0x0))
        ++count;
    return count;
}

// Unlinks the list's last node, releases its object and frees the node.
void PopBack(char* head, std::uintptr_t global)
{
    char* const node = At<char*>(head, 0x4);
    char* const prev = At<char*>(node, 0x4);
    char* const next = At<char*>(node, 0x0);
    At<char*>(prev, 0x0) = next;
    At<char*>(next, 0x4) = prev;
    if (void* const object = At<void*>(node, 0x8))
        Virtual<uint32_t>(object, 0x8);
    Virtual<uint32_t>(NodeAllocatorAt(global), 0x10, node);
}

void ClearList(char** list) { reinterpret_cast<void(__thiscall*)(char**)>(addr::SC4List_Clear)(list); }

// The game's 0x00421AB0: the neighbour deals by kind, the kinds of
// connection present and the deals on offer.
void __fastcall PublishNeighborDeals(char* self, void*)
{
    void* const system = Global<void*>(addr::cSC4AdvisorSystem_Instance);
    void* const app = Global<void*>(addr::SC4_Service43C94);
    void* const deals = Global<void*>(addr::SC4_MonthEndService);
    Virtual<void*>(app, 0x8C);

    char* list = nullptr;
    list = NewListHead(addr::SC4List_NodeAllocator);

    static const std::pair<uint32_t, const char*> kCounts[] = {
        {0x4773FFE2, "g_nd_count_buy_water"},      {0x4773FFE3, "g_nd_count_sell_water"},
        {0x4773FFE0, "g_nd_count_buy_power"},      {0x4773FFE1, "g_nd_count_sell_power"},
        {0x4773FFE4, "g_nd_count_export_garbage"}, {0x4773FFE5, "g_nd_count_import_garbage"},
    };
    for (const auto& [kind, name] : kCounts) {
        ClearList(&list);
        Virtual<uint32_t>(deals, 0x78, kind, &list);
        Virtual<uint8_t>(system, 0x34, name, static_cast<double>(ListCount(list)));
    }

    // The neighbour connections: which utilities have one.
    char* connections = nullptr;
    connections = NewListHead(addr::SC4List_NodeAllocator3);
    Virtual<uint32_t>(deals, 0x40, &connections);
    uint32_t present = 0;
    while (At<char*>(connections, 0x0) != connections && present != 7) {
        void* const connection = At<void*>(At<char*>(connections, 0x4), 0x8);
        switch (Virtual<int32_t>(connection, 0x14)) {
        case 1:
        case 2:
            present |= 4;
            break;
        case 4:
            present |= 2;
            break;
        case 5:
            present |= 1;
            break;
        default:
            break;
        }
        PopBack(connections, addr::SC4List_NodeAllocator3);
    }
    Virtual<uint8_t>(system, 0x34, "g_nd_connection_present_water",
                     static_cast<double>(static_cast<int32_t>(present & 1)));
    Virtual<uint8_t>(system, 0x34, "g_nd_connection_present_power",
                     static_cast<double>(static_cast<int32_t>((present >> 1) & 1)));
    Virtual<uint8_t>(system, 0x34, "g_nd_connection_present_garbage",
                     static_cast<double>(static_cast<int32_t>((present >> 2) & 1)));

    // The deals on offer: which the city can take.
    uint32_t sell = 0;
    uint32_t buy = 0;
    ClearList(&list);
    Virtual<uint32_t>(deals, 0x64, &list, uint32_t{0});
    while (At<char*>(list, 0x0) != list && buy != 7 && sell != 7) {
        void* const deal = At<void*>(At<char*>(list, 0x4), 0x8);
        switch (Virtual<uint32_t>(deal, 0x20)) {
        case 0x4773FFE0:
            buy |= 2;
            break;
        case 0x4773FFE1:
            sell |= 2;
            break;
        case 0x4773FFE2:
            buy |= 1;
            break;
        case 0x4773FFE3:
            sell |= 1;
            break;
        case 0x4773FFE4:
            buy |= 4;
            break;
        case 0x4773FFE5:
            sell |= 4;
            break;
        default:
            break;
        }
        PopBack(list, addr::SC4List_NodeAllocator);
    }
    ClearList(&list);
    Virtual<uint8_t>(system, 0x34, "g_nd_can_buy_water", static_cast<double>(static_cast<int32_t>(buy & 1)));
    Virtual<uint8_t>(system, 0x34, "g_nd_can_buy_power", static_cast<double>(static_cast<int32_t>((buy >> 1) & 1)));
    Virtual<uint8_t>(system, 0x34, "g_nd_can_export_garbage",
                     static_cast<double>(static_cast<int32_t>((buy >> 2) & 1)));
    Virtual<uint8_t>(system, 0x34, "g_nd_can_sell_water", static_cast<double>(static_cast<int32_t>(sell & 1)));
    Virtual<uint8_t>(system, 0x34, "g_nd_can_sell_power",
                     static_cast<double>(static_cast<int32_t>((sell >> 1) & 1)));
    Virtual<uint8_t>(system, 0x34, "g_nd_can_import_garbage",
                     static_cast<double>(static_cast<int32_t>((sell >> 2) & 1)));

    At<uint32_t>(self, kDirty) &= 0xFFFFFFFBu;
    reinterpret_cast<void(__thiscall*)(char**)>(addr::SC4List_Destroy3)(&connections);
    reinterpret_cast<void(__thiscall*)(char**)>(addr::SC4List_Destroy)(&list);
}

// A pollution average as a number: never below zero.
double NotNegative(int32_t value)
{
    const double number = static_cast<double>(value);
    return number < 0.0 ? 0.0 : number;
}

// One of the pollution histories (slot at `offset` gives two figures for
// the data object and the value), stored under `name`.
void PublishPollutionHistory(void* system, void* pollution, void*& data, std::size_t offset, const char* name)
{
    void* const com = GetCOM();
    ReleaseAndClear(data);
    Virtual<uint8_t>(com, 0xC, kDataClass, kDataInterface, &data);
    // The original leaves these three unset before the call.
    int32_t first = 0;
    int32_t second = 0;
    int32_t value = 0;
    Virtual<uint32_t>(pollution, offset, &first, &second, &value);
    Virtual<uint32_t>(data, 0xC, static_cast<float>(first), static_cast<float>(second));
    if (value < 0)
        value = 0;
    Virtual<uint8_t>(system, 0x38, name, static_cast<double>(value), data);
}

// The game's 0x004220A0: power, water, garbage and pollution, then the two
// publishers above.
void __fastcall PublishUtilities(char* self, void*)
{
    void* water = Global<void*>(addr::SC4_Service43D94);
    void* const pollution = Global<void*>(addr::SC4_PollutionSimulator);
    void* const system = Global<void*>(addr::cSC4AdvisorSystem_Instance);
    void* const city = Global<void*>(addr::SC4_City);
    void* const power = Global<void*>(addr::SC4_Service43DA0);
    Virtual<void*>(city, 0x80); // GetHistoryWarehouse
    void* const app = Global<void*>(addr::SC4_Service43C94);
    void* data = nullptr;
    void* const region = Virtual<void*>(app, 0x90);

    Virtual<uint8_t>(system, 0x34, "g_power_funding_p",
                     static_cast<double>(*Virtual<float*>(power, 0xC0)) * 100.0);
    Virtual<uint8_t>(system, 0x34, "g_power_production_capacity",
                     static_cast<double>(Virtual<int32_t>(power, 0x30)));
    Virtual<uint8_t>(system, 0x34, "g_power_consumed", static_cast<double>(Virtual<int32_t>(power, 0x44)));
    Virtual<uint8_t>(system, 0x34, "g_power_imported", Virtual<double>(region, 0xE0, int32_t{1}));
    Virtual<uint8_t>(system, 0x34, "g_power_exported", Virtual<double>(region, 0xD8, int32_t{1}));
    Virtual<uint8_t>(system, 0x34, "g_overworked_power_plant_count",
                     static_cast<double>(Virtual<int32_t>(power, 0x6C)));
    Virtual<uint8_t>(system, 0x34, "g_unpowered_building_count",
                     static_cast<double>(Virtual<int32_t>(power, 0x54)));
    Virtual<uint8_t>(system, 0x34, "g_power_strike",
                     static_cast<double>(Virtual<uint8_t>(power, 0x9C) != 0 ? int32_t{1} : int32_t{0}));
    float chance = 0.0f; // unset in the original; the callee returns its address
    Virtual<uint8_t>(system, 0x34, "g_power_strike_chance",
                     static_cast<double>(*Virtual<float*>(power, 0xA0, &chance)) * 100.0);

    void* budget = Global<void*>(addr::SC4_BudgetSimulator);
    void* department = Virtual<void*>(budget, 0x70, uint32_t{0xAA4014B4});
    Virtual<uint8_t>(system, 0x34, "g_water_funding_p",
                     static_cast<double>(*Virtual<float*>(department, 0x28)) * 100.0);
    Virtual<uint8_t>(system, 0x34, "g_water_production_capacity",
                     static_cast<double>(Virtual<int32_t>(water, 0x38)));
    Virtual<uint8_t>(system, 0x34, "g_water_consumed", static_cast<double>(Virtual<int32_t>(water, 0x3C)));
    Virtual<uint8_t>(system, 0x34, "g_water_imported", Virtual<double>(region, 0xE0, int32_t{2}));
    Virtual<uint8_t>(system, 0x34, "g_water_exported", Virtual<double>(region, 0xD8, int32_t{2}));

    budget = Global<void*>(addr::SC4_BudgetSimulator);
    department = Virtual<void*>(budget, 0x70, uint32_t{0x6A42A8F5});
    Virtual<uint8_t>(system, 0x34, "g_pollution_funding_p",
                     static_cast<double>(*Virtual<float*>(department, 0x28)) * 100.0);
    Virtual<uint8_t>(system, 0x34, "g_incinerator_capacity_daily",
                     static_cast<double>(static_cast<uint32_t>(Virtual<uint32_t>(pollution, 0x7C) * 30u)));
    Virtual<uint8_t>(system, 0x34, "g_waste_to_energy_capacity_daily",
                     static_cast<double>(static_cast<uint32_t>(Virtual<uint32_t>(pollution, 0x80) * 30u)));
    Virtual<uint8_t>(system, 0x34, "g_recycling_center_effect_p",
                     static_cast<double>(*Virtual<float*>(pollution, 0x84)) * 100.0);
    Virtual<uint8_t>(system, 0x34, "ga_air_pollution", NotNegative(Virtual<int32_t>(pollution, 0x28)));
    Virtual<uint8_t>(system, 0x34, "ga_water_pollution", NotNegative(Virtual<int32_t>(pollution, 0x2C)));
    Virtual<uint8_t>(system, 0x34, "ga_garbage_pollution", NotNegative(Virtual<int32_t>(pollution, 0x30)));
    Virtual<uint8_t>(system, 0x34, "g_uncollected_garbage", static_cast<double>(Virtual<int32_t>(pollution, 0x60)));

    void* const landfills = Global<void*>(addr::SC4_Value43D28);
    uint32_t used = 0;
    uint32_t capacity = 0;
    Virtual<uint32_t>(landfills, 0x28, &used, &capacity);
    Virtual<uint8_t>(system, 0x34, "g_landfill_capacity", static_cast<double>(capacity));
    Virtual<uint8_t>(system, 0x34, "g_available_landfill_capacity", static_cast<double>(capacity - used));

    Virtual<uint8_t>(system, 0x34, "g_garbage_produced", static_cast<double>(Virtual<int64_t>(pollution, 0x8C)));
    Virtual<uint8_t>(system, 0x34, "g_garbage_imported", Virtual<double>(region, 0xD8, int32_t{3}));
    Virtual<uint8_t>(system, 0x34, "g_garbage_exported", Virtual<double>(region, 0xE0, int32_t{3}));
    Virtual<uint8_t>(system, 0x34, "g_garbage_recycled", static_cast<double>(Virtual<int64_t>(pollution, 0x98)));
    Virtual<uint8_t>(system, 0x34, "g_garbage_to_energy", static_cast<double>(Virtual<int64_t>(pollution, 0xA0)));
    Virtual<uint8_t>(system, 0x34, "g_garbage_to_landfill", static_cast<double>(Virtual<int64_t>(pollution, 0xA4)));

    PublishPollutionHistory(system, pollution, data, 0x44, "l_air_pollution_h");
    PublishPollutionHistory(system, pollution, data, 0x48, "l_water_pollution_h");
    PublishPollutionHistory(system, pollution, data, 0x4C, "l_uncollected_garbage_h");

    PublishUtilityBuildings(self, nullptr);
    PublishNeighborDeals(self, nullptr);
    if (data)
        Virtual<uint32_t>(data, 0x8);
}

bool InstallUtilities(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[4] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::ScriptGlobals_PublishUtilityBuildings, reinterpret_cast<void*>(&PublishUtilityBuildings)},
        {addr::ScriptGlobals_PublishNeighborDeals, reinterpret_cast<void*>(&PublishNeighborDeals)},
        {addr::ScriptGlobals_PublishUtilities, reinterpret_cast<void*>(&PublishUtilities)},
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

const bool g_utilitiesRegistered = registry::Register({
    "scripts.globals.utilities",
    "scripts/globals",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::ScriptGlobals_PublishUtilities,
    "Our publishers of the utilities' script variables (docs/lua.md)",
    false,
    InstallUtilities,
});

} // namespace
} // namespace opensc4::script_globals
