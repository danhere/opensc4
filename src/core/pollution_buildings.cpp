// Our cSC4PollutionSimulator, third part: the helpers that keep the garbage
// and water treatment buildings up to date after the tract walk
// (docs/pollution.md, "Building lists"). Integer code only.
#include "core/pollution_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstdint>

namespace opensc4::pollution_sim {
namespace {

// --- SpreadUsedCapacity (0x006E3FE0) -----------------------------------------

// Shares out `*used` (at most `total`) over the list's buildings, an equal
// part each, as much as each has room for (current capacity minus the used
// property), and adds it to their used property. When a building has less
// room than its part, the part is recomputed from what is left over the
// buildings still to come (before that building's share is taken off).
void __fastcall SpreadUsedCapacity(void* self, void*, void* list, uint32_t currentProperty, uint32_t usedProperty,
                                   uint32_t total, uint32_t* used, uint32_t count)
{
    if (total == 0 || count == 0)
        return;
    uint32_t part = (total < *used ? total : *used) / count;
    void* const sentinel = *static_cast<void**>(list);
    if (*used == 0)
        return;
    uint32_t remaining = count;
    for (void* node = NextNode(sentinel); node != sentinel; node = NextNode(node)) {
        void* const occupant = NodeObject(node);
        void* const holder = PropertyHolder(occupant);
        --remaining;
        uint32_t current;
        if (Virtual<uint8_t>(self, kSlotGetCurrentCapacity, currentProperty, occupant, &current)) {
            if (void* const property = Virtual<void*>(holder, 0x24, usedProperty)) { // cISCPropertyHolder::GetProperty
                void* const value = Virtual<void*>(property, 0x14);                 // cISCProperty::GetPropertyValue
                const uint32_t usedNow = *Virtual<uint32_t*>(value, 0x12C);
                const uint32_t room = current > usedNow ? current - usedNow : 0;
                uint32_t share = part;
                if (static_cast<int32_t>(part) > static_cast<int32_t>(room)) {
                    share = room;
                    if (remaining != 0)
                        part = *used / remaining;
                }
                *used -= share;
                Virtual<uint32_t>(value, 0x74, share + usedNow); // cIGZVariant::SetValUint32
            }
        }
        if (*used == 0)
            break;
    }
}

// --- UpdateMaintenanceCosts (0x006E4100) -------------------------------------

// cISC4BudgetSimulator::BudgetItem (gzcom-dll's layout, which the game's
// code matches).
struct BudgetItem {
    uint32_t departmentId;
    uint32_t lineItem;
    uint32_t purpose;
    uint32_t padding;
    int64_t cost;
    uint32_t index;
    float localFundingPercentage;
};
static_assert(sizeof(BudgetItem) == 0x20);

constexpr uint32_t kPurposeMaintenance = 0xCA58EDC7;

// Brings each building's maintenance line item in the budget up to date
// with its maintenance cost (CalcOccupantMaintenanceCost, slot 65).
void __fastcall UpdateMaintenanceCosts(void* self, void*, void* list)
{
    void* const sentinel = *static_cast<void**>(list);
    for (void* node = NextNode(sentinel); node != sentinel; node = NextNode(node)) {
        void* const occupant = NodeObject(node);
        void* const holder = PropertyHolder(occupant);
        int64_t cost = 0;
        if (!Virtual<uint8_t>(self, 0x104, occupant, &cost))
            continue;
        void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
        BudgetItem item{}; // the game zeroes only localFundingPercentage; the call fills the rest
        if (!Virtual<uint8_t>(budget, 0xCC, holder, kPurposeMaintenance, &item)) // GetBudgetItemForPurpose
            continue;
        if (cost == item.cost)
            continue;
        void* const lineItem = Virtual<void*>(budget, 0xE4, &item); // GetLineItemFromBudgetItem
        if (!lineItem)
            continue;
        Virtual<uint32_t>(lineItem, 0x34, static_cast<int64_t>(static_cast<uint64_t>(cost) - static_cast<uint64_t>(item.cost)));
        Virtual<uint32_t>(budget, 0xD4, holder, item.index, cost); // ChangeBudgetItemCost; the game passes +0x18
    }
}

// --- Installation -----------------------------------------------------------

using SpreadFn = void(__fastcall*)(void*, void*, void*, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
using ListFn = void(__fastcall*)(void*, void*, void*);
SpreadFn g_originalSpread = nullptr;
ListFn g_originalList = nullptr;

bool Install(registry::Mode)
{
    const addr::Entry* spread = addr_check::Find(addr::cSC4PollutionSimulator_SpreadUsedCapacity);
    const addr::Entry* list = addr_check::Find(addr::cSC4PollutionSimulator_UpdateMaintenanceCosts);
    if (!game::IsBuild641() || !spread || !list)
        return false;
    return hook::Install(*spread, &SpreadUsedCapacity, &g_originalSpread) &&
           hook::Install(*list, &UpdateMaintenanceCosts, &g_originalList);
}

const bool g_registered = registry::Register({
    "simulators.pollution.buildings",
    "simulators/pollution",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PollutionSimulator_SpreadUsedCapacity,
    "Our building-list helpers: used capacity, maintenance line items (docs/pollution.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::pollution_sim
