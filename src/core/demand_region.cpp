// Our cSC4DemandSimulator (build 641), third part: where demand comes from
// (docs/demand.md, "Sources"): the taxes and ordinances of each RCI group
// (state 0 of the step), the cell walk that counts people and jobs (state 1)
// and the neighbouring cities' share.
#include "core/demand_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>

namespace opensc4::demand_sim {
namespace {

// The twelve RCI groups: the 90-day figure of their tax rate, their demand,
// their ordinance factor (the service at +0x48, slot 25), their type for the
// economy (the service from 0x00B43C94 slot 34, slot 16), and the saved
// float added to the economy (offices +0xA0, industry +0xA4).
struct Group {
    uint32_t figure;
    uint32_t demand;
    uint32_t ordinance;
    uint32_t type;
    std::size_t extra; // 0 for none
};

constexpr Group kGroups[] = {
    {0xAABAAA9F, 0x21010, 0x2A651010, 0x1010, 0},     {0xAABAAAA0, 0x21020, 0x2A651020, 0x1020, 0},
    {0xAABAAAA1, 0x21030, 0x2A651030, 0x1030, 0},     {0xAABAAAA2, 0x23110, 0x2A653110, 0x3110, 0},
    {0xAABAAAA3, 0x23120, 0x2A653120, 0x3120, 0},     {0xAABAAAA4, 0x23130, 0x2A653130, 0x3130, 0},
    {0xAABAAAA5, 0x23320, 0x2A653320, 0x3320, 0xA0},  {0xAABAAAA6, 0x23330, 0x2A653330, 0x3330, 0xA0},
    {0xAABAAAA7, 0x24100, 0x2A654100, 0x4100, 0xA4},  {0xAABAAAA8, 0x24200, 0x2A654200, 0x4200, 0xA4},
    {0xAABAAAA9, 0x24300, 0x2A654300, 0x4300, 0xA4},  {0xAABAAAAA, 0x24400, 0x2A654400, 0x4400, 0xA4},
};

constexpr std::size_t kTaxEffectCurve = 0xB4; // SC4Curve*, tuning 0x4A1F38B6
constexpr std::size_t kHistoryDays = 0xB8;     // tuning 0x4A1F38B7, 90 by default
constexpr std::size_t kNeighbors = 0x98;       // SC4List of the neighbouring cities
constexpr std::size_t kConnections = 0x9C;     // SC4List filled by the service at +0x44, slot 21

// 0x006A6070 (state 0): each RCI group's tax modifier, from its tax rate
// over the last 90 days (figure history, slot 7) against the neutral rate
// (slot 10, at least 0.1) through curve +0xB4, plus its ordinance factor
// less 1; and its economy modifier.
void __fastcall RegionDemand(void* self, void*)
{
    static bool logged = false;
    FirstCall("RegionDemand", logged);
    void* const economy = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x88);
    void* const figures = Global<void*>(addr::SC4_MonthlyFigures);
    const double neutralRate = Virtual<double>(self, 0x28);
    float neutral = static_cast<float>(neutralRate);
    if (neutralRate < static_cast<double>(0.1f))
        neutral = 0.1f;
    void* const simulator = Virtual<void*>(Global<void*>(addr::SC4_SimulatorService), 0x1C);
    const int32_t from = Virtual<int32_t>(simulator, 0x68) - At<int32_t>(self, kHistoryDays);
    uint32_t date[4];
    CallThis<void>(addr::cRZDate_cRZDate, date, from);
    float inverse = 0.0f;
    for (std::size_t i = 0; i < sizeof kGroups / sizeof kGroups[0]; ++i) {
        const Group& group = kGroups[i];
        float average = 0.0f;
        uint32_t ignored[2] = {};
        Virtual<uint8_t>(figures, 0x1C, group.figure, &date[2], simulator, &ignored[0], &average, &ignored[1]);
        const double scaled = static_cast<double>(average) * static_cast<double>(0.1f);
        double x;
        if (i == 0) {
            // The first group uses the inverse before it is stored as a float.
            const double unrounded = 1.0 / static_cast<double>(neutral);
            inverse = static_cast<float>(unrounded);
            x = unrounded * scaled;
        } else {
            x = static_cast<double>(inverse) * scaled;
        }
        const float effect = static_cast<float>(EvaluateCurve(At<void*>(self, kTaxEffectCurve), static_cast<float>(x)));
        void* const demand = DemandAt(self, group.demand);
        const double ordinance = Virtual<double>(At<void*>(self, field::kOrdinances), 0x64, group.ordinance, 0u);
        Virtual<uint32_t>(demand, 0x64, static_cast<float>(ordinance - 1.0 + effect));
        double value = Virtual<double>(economy, 0x40, group.type);
        if (group.extra)
            value += At<float>(self, group.extra);
        Virtual<uint32_t>(demand, 0x5C, static_cast<float>(value));
    }
    CallThis<void>(addr::cRZDate_Destroy, date);
}

// A zoned lot's share of its kind at a cell (the service at +0x3C, slot 32:
// its object's slot 13 grid, slot 7 (x, z) in hundredths), in [0, 1].
float Share(void* service, uint32_t key, int32_t x, int32_t z)
{
    void* const record = Virtual<void*>(service, 0x80, key);
    void* const grid = Virtual<void*>(record, 0x34);
    const double value = static_cast<double>(Virtual<int16_t>(grid, 0x1C, x, z)) * static_cast<double>(0.01f);
    float share = static_cast<float>(value);
    if (value < 0.0)
        share = 0.0f;
    else if (share > 1.0f)
        share = 1.0f;
    return share;
}

// 0x006A5A00 (the cell walk): the lot at a cell, if functional (slot 38)
// and zoned (slot 46): residential (kind 1) people by wealth into +0x1C
// under 0x1001 + 16 x wealth, and weighted by the cell's value (the service
// at +0x4C, slot 30) under 0x2110 + 16 x wealth + a level of 1 to 4 (from
// its slot 35); commercial and industrial (kinds 2, 3, 5 to 8) jobs under
// their key + 1.
void __fastcall CountCell(void* self, void*, int32_t x, int32_t z)
{
    void* const lot = Virtual<void*>(At<void*>(self, field::kService40), 0x18, x, z, 1u);
    if (!lot || !Virtual<uint8_t>(lot, 0x98))
        return;
    const uint32_t kind = Virtual<uint32_t>(lot, 0xB8, 1u);
    if (!kind)
        return;
    void* const service = At<void*>(self, field::kService3C);
    const uint32_t* const info = Virtual<const uint32_t*>(service, 0x9C, kind);
    void* const pending = static_cast<char*>(self) + field::kPending;
    switch (info[0]) {
    case 1: {
        const uint32_t wealth = Virtual<uint32_t>(lot, 0xA4);
        const uint32_t key = Virtual<uint32_t>(service, 0x78, info[0], wealth);
        const float share = Share(service, key, x, z);
        const uint16_t count = Virtual<uint16_t>(lot, 0xC8, key, 0u);
        const float people = static_cast<float>(static_cast<double>(count) * share);
        void* const values = At<void*>(self, field::kService4C);
        float scratch = 0.0f;
        const float* const factor = Virtual<const float*>(values, 0x78, &scratch, x, z);
        const float weighted = static_cast<float>(static_cast<double>(people) * *factor);
        const uint8_t level = static_cast<uint8_t>(Ftol(Virtual<double>(values, 0x8C, x, z)));
        int32_t step = Ftol(static_cast<double>(level) * static_cast<double>(0.02f) + 1.0);
        if (step < 1)
            step = 1;
        else if (step > 4)
            step = 4;
        float* const byLevel = FloatMapAt(pending, (wealth + 0x210) * 16 + static_cast<uint32_t>(step));
        *byLevel = static_cast<float>(static_cast<double>(weighted) + *byLevel);
        float* const byWealth = FloatMapAt(pending, wealth * 16 + 0x1001);
        *byWealth = static_cast<float>(static_cast<double>(people) + *byWealth);
        break;
    }
    case 2:
    case 3:
    case 5:
    case 6:
    case 7:
    case 8: {
        const uint32_t wealth = Virtual<uint32_t>(lot, 0xA4);
        const uint32_t key = Virtual<uint32_t>(service, 0x78, info[0], wealth);
        const float share = Share(service, key, x, z);
        const uint16_t count = Virtual<uint16_t>(lot, 0xC8, key, 0u);
        const float jobs = static_cast<float>(static_cast<double>(count) * share);
        float* const total = FloatMapAt(pending, key + 1);
        *total = static_cast<float>(static_cast<double>(jobs) + *total);
        break;
    }
    default:
        break;
    }
}

// 0x006A5CB0 (Init with true, messages 0x8A0D15DA and 0xEA0D163E with
// false): the neighbouring cities (the service at +0x44, slot 22) replace
// the list at +0x98; for each demand of region use 1 or 2, the change in the
// neighbours' figure for it (slot 17 of each city but this one) goes to the
// demand ID + 0x10000 (supply for use 1, demand for use 2). With `reset`,
// that demand is set to the neighbours' figure instead.
void __fastcall Neighbors(void* self, void*, uint8_t reset)
{
    static bool logged = false;
    FirstCall("Neighbors", logged);
    void* const allocator = NodeAllocatorAt(addr::SC4List_NodeAllocator4);
    void** const fresh = Virtual<void**>(allocator, 0xC, 0xCu);
    fresh[0] = fresh;
    fresh[1] = fresh;
    void* list = fresh;
    void* const regions = At<void*>(self, field::kService44);
    Virtual<uint32_t>(regions, 0x58, &list, 0u);
    Virtual<uint32_t>(regions, 0x54, static_cast<char*>(self) + kConnections, 0u);
    void* const city = Virtual<void*>(Global<void*>(addr::SC4_Service43C94), 0x90);
    TreeNode* const demands = Header(self, field::kDemands);
    for (TreeNode* node = First(demands); node != demands; node = Next(node)) {
        void* const demand = Value<void*>(node);
        const uint32_t key = node->key;
        const int32_t use = Virtual<int32_t>(demand, 0x70);
        if (use < 1 || use > 2)
            continue;
        const uint32_t regional = Virtual<uint32_t>(demand, 0x18) + 0x10000;
        TreeNode* const found = Find(demands, regional);
        if (found == demands)
            continue;
        void* const target = Value<void*>(found);
        float before = 0.0f;
        float after = 0.0f;
        auto** const old = At<void**>(self, kNeighbors);
        for (auto** entry = static_cast<void**>(old[0]); entry != old; entry = static_cast<void**>(entry[0])) {
            void* const neighbour = entry[2];
            if (neighbour != city)
                before = static_cast<float>(static_cast<double>(Virtual<int32_t>(neighbour, 0x44, key)) + before);
        }
        auto** const now = static_cast<void**>(list);
        for (auto** entry = static_cast<void**>(now[0]); entry != now; entry = static_cast<void**>(entry[0])) {
            void* const neighbour = entry[2];
            if (neighbour != city)
                after = static_cast<float>(static_cast<double>(Virtual<int32_t>(neighbour, 0x44, key)) + after);
        }
        float change = static_cast<float>(static_cast<double>(after) - before);
        if (use == 1) {
            if (reset) {
                const float added = Virtual<float>(target, 0x28);
                change = static_cast<float>(after - (static_cast<double>(Virtual<float>(target, 0x20)) + added));
            }
            Virtual<uint8_t>(target, 0x40, change);
        } else {
            if (reset) {
                const float added = Virtual<float>(target, 0x2C);
                change = static_cast<float>(after - (static_cast<double>(Virtual<float>(target, 0x24)) + added));
            }
            Virtual<uint8_t>(target, 0x44, change);
        }
    }
    void* previous = At<void*>(self, kNeighbors);
    At<void*>(self, kNeighbors) = list;
    CallThis<void>(addr::SC4List_ClearReleasing4, &previous);
    Virtual<void>(NodeAllocatorAt(addr::SC4List_NodeAllocator4), 0x10, previous);
}

// --- Installation -----------------------------------------------------------

template <typename Fn>
bool Install(std::uintptr_t address, Fn ours, Fn* original)
{
    const addr::Entry* entry = addr_check::Find(address);
    return entry && hook::Install(*entry, ours, original);
}

using SelfFn = void(__fastcall*)(void*, void*);
using CellFn = void(__fastcall*)(void*, void*, int32_t, int32_t);
using NeighborsFn = void(__fastcall*)(void*, void*, uint8_t);

SelfFn g_originalRegion = nullptr;
CellFn g_originalCell = nullptr;
NeighborsFn g_originalNeighbors = nullptr;

bool InstallSources(registry::Mode)
{
    return game::IsBuild641() && Install(addr::cSC4DemandSimulator_RegionDemand, &RegionDemand, &g_originalRegion) &&
           Install(addr::cSC4DemandSimulator_CountCell, &CountCell, &g_originalCell) &&
           Install(addr::cSC4DemandSimulator_Neighbors, &Neighbors, &g_originalNeighbors);
}

const bool g_registered = registry::Register({
    "simulators.demand.sources",
    "simulators/demand",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4DemandSimulator_RegionDemand,
    "Our demand sources: taxes and ordinances by RCI group, the cell walk's counts, the neighbours (docs/demand.md)",
    false,
    InstallSources,
});

} // namespace
} // namespace opensc4::demand_sim
