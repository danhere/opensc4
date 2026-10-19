// OPENSC4_TEST_PANELS=1: right after the load, log in plain numbers what the
// budget, ordinance, RCI, power and water windows read from the game's
// simulators, to check tools/sc4report.py against them (docs/sc4report.md).
// OPENSC4_TEST_PANELS_BUDGET=1 also opens the budget window (budget slot 24)
// for a screenshot run. Build 641 slots and offsets, as the fingerprints.
#include "core/autotest_hooks.h"
#include "core/demand_sim.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <cISC4City.h>
#include <cISC4Ordinance.h>
#include <cISC4OrdinanceSimulator.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::autotest {
namespace {

char* MapNext(char* node)
{
    if (char* right = At<char*>(node, 0xC)) {
        while (char* left = At<char*>(right, 0x8))
            right = left;
        return right;
    }
    char* parent = At<char*>(node, 0x4);
    while (node == At<char*>(parent, 0xC)) {
        node = parent;
        parent = At<char*>(parent, 0x4);
    }
    if (At<char*>(node, 0xC) != parent)
        node = parent;
    return node;
}

template <typename R>
R Slot(void* object, int index)
{
    using Fn = R(__thiscall*)(void*);
    return reinterpret_cast<Fn>((*static_cast<void***>(object))[index])(object);
}

void Budget(cISC4City* city)
{
    void* const budget = city->GetBudgetSimulator();
    if (!budget)
        return;
    // Slots 6 (funds), 14 and 16 (this month's expense and income), 18
    // (a group's tax income), 36 (a group's rate); +0x670 the tax income.
    log::Write("panels: budget funds %lld expense %lld income %lld tax %lld",
               static_cast<long long>(Slot<int64_t>(budget, 6)), static_cast<long long>(Virtual<int64_t>(budget, 0x38)),
               static_cast<long long>(Virtual<int64_t>(budget, 0x40)),
               static_cast<long long>(At<int64_t>(budget, 0x670)));
    for (int32_t group = 0; group < 12; ++group)
        log::Write("panels: tax %d rate %.2f income %lld", group,
                   static_cast<double>(Virtual<float>(budget, 0x90, group)),
                   static_cast<long long>(Virtual<int64_t>(budget, 0x48, group)));
    // Departments (map +0x54): ID, expense and income (slots 15, 16), the
    // funding (slot 10, a percentage).
    char* const header = At<char*>(budget, 0x54);
    for (char* node = At<char*>(header, 0x8); node != header; node = MapNext(node)) {
        void* const department = At<void*>(node, 0x14);
        log::Write("panels: department %08X expense %lld income %lld funding %.4f", At<uint32_t>(node, 0x10),
                   static_cast<long long>(Virtual<int64_t>(department, 0x3C)),
                   static_cast<long long>(Virtual<int64_t>(department, 0x40)),
                   static_cast<double>(*Virtual<const float*>(department, 0x28)));
    }
    if (Env("OPENSC4_TEST_PANELS_BUDGET") == "1")
        Virtual<uint8_t>(budget, 0x60);
}

void Ordinances(cISC4City* city)
{
    cISC4OrdinanceSimulator* const ordinances = city->GetOrdinanceSimulator();
    if (!ordinances)
        return;
    log::Write("panels: ordinances expense %lld income %lld available %u on %u",
               static_cast<long long>(ordinances->GetOrdinanceMonthlyExpense()),
               static_cast<long long>(ordinances->GetOrdinanceMonthlyIncome()),
               ordinances->GetOrdinanceAvailableCount(), ordinances->GetOrdinanceOnCount());
    uint32_t ids[128];
    uint32_t count = 128;
    ordinances->GetOrdinanceIDArray(ids, count);
    for (uint32_t i = 0; i < count; ++i)
        if (void* const ordinance = ordinances->GetOrdinanceByID(ids[i]); ordinance && ordinances->IsOrdinanceOn(ids[i]))
            log::Write("panels: ordinance %08X on, this month %lld", ids[i],
                       static_cast<long long>(Virtual<int64_t>(ordinance, 0x50)));
}

void Demand(cISC4City* city)
{
    void* const demandSim = city->GetDemandSimulator();
    if (!demandSim)
        return;
    using namespace demand_sim;
    TreeNode* const demands = Header(demandSim, field::kDemands);
    // The twelve the RCI bars read (PublishActive, 0x006A7600): active demand
    // (slot 12) and the cap.
    for (const uint32_t id : {0x21010u, 0x21020u, 0x21030u, 0x23110u, 0x23120u, 0x23130u, 0x23320u, 0x23330u,
                              0x24100u, 0x24200u, 0x24300u, 0x24400u}) {
        TreeNode* const node = Find(demands, id);
        if (node == demands)
            continue;
        void* const demand = Value<void*>(node);
        log::Write("panels: demand %05X active %.1f cap %.4f", id, static_cast<double>(Virtual<float>(demand, 0x30)),
                   static_cast<double>(At<float>(demand, demand::kCap)));
    }
}

void Utilities(cISC4City* city)
{
    if (void* const power = city->GetPowerSimulator())
        log::Write("panels: power produced %d demand %d used %d imported %d exported %d plants %d",
                   Slot<int32_t>(power, 12), Slot<int32_t>(power, 15), Slot<int32_t>(power, 17),
                   Slot<int32_t>(power, 13), Slot<int32_t>(power, 14), Slot<int32_t>(power, 24));
    if (void* const water = city->GetPlumbingSimulator())
        log::Write("panels: water capacity %d used %d imported %d exported %d demand %d sources %d",
                   Slot<int32_t>(water, 14), Slot<int32_t>(water, 15), Slot<int32_t>(water, 16),
                   Slot<int32_t>(water, 17), Slot<int32_t>(water, 23), Slot<int32_t>(water, 12));
}

void Panels(cISC4City* city)
{
    if (Env("OPENSC4_TEST_PANELS") != "1" || !game::IsBuild641())
        return;
    Ordinances(city);
    Demand(city);
    Utilities(city);
    Budget(city);
}

const LoadHook kPanels{"panels", 990, &Panels};

} // namespace
} // namespace opensc4::autotest
