// Our cSC4DemandSimulator (build 641), second part: the step, the passes
// that move supply and demand between the demands, the published figures and
// the message handler (docs/demand.md, "The step"). Each helper is called
// through the game's address, so each replacement can be switched on alone.
#include "core/demand_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstdint>

namespace opensc4::demand_sim {
namespace {

// The kinds of a demand's effect on another (its exemplar's properties).
constexpr uint32_t kSupplyToDemand = 0x47BB3F30; // new supply x factor added to the other's demand
constexpr uint32_t kSupplyToSupply = 0x47BB3F31; // new supply x factor added to the other's supply
constexpr uint32_t kDemandToDemand = 0x47BB3F32; // new demand x factor added to the other's demand
constexpr uint32_t kCap = 0x47BB3F41;            // the other's cap from this one's demand over supply
constexpr uint32_t kSum = 0x47BB3F50;            // this one is the sum of the others

constexpr double kSettled = 0.01f;               // 0x00A823A4

// The stepping of the cell walk (state 1): an adaptive step and its once flag.
constexpr std::uintptr_t kWalkStep = 0x00B49024;
constexpr std::uintptr_t kWalkOnce = 0x00B4902C;
// The twelve demands averaged into figure 0x8A63F30C, with the weights at +0x68.
constexpr std::uintptr_t kWeightedDemands = 0x00B0D248;

struct Deadline {
    uint32_t start[2];
    uint32_t count;
    uint32_t step;
    uint8_t timedOut;
    uint8_t padding[3];
    uint32_t* stepGlobal;
    uint32_t spare[2];
};

void* Demand(void* self, uint32_t id) { return DemandAt(self, id); }

void Figure(uint32_t id, int32_t value)
{
    Virtual<uint8_t>(Global<void*>(addr::SC4_MonthlyFigures), 0x30, id, value);
}

// --- The passes ---------------------------------------------------------------

// 0x006A37A0: the 24 counted values of a map (+0x1C or +0x28) to 0.
void __fastcall ClearCounts(void*, void*, void* map)
{
    static const uint32_t kIds[] = {0x2111, 0x2112, 0x2113, 0x2114, 0x2121, 0x2122, 0x2123, 0x2124,
                                    0x2131, 0x2132, 0x2133, 0x2134, 0x1011, 0x1021, 0x1031, 0x3111,
                                    0x3121, 0x3131, 0x3321, 0x3331, 0x4101, 0x4201, 0x4301, 0x4401};
    for (const uint32_t id : kIds)
        *FloatMapAt(map, id) = 0.0f;
}

// 0x006A27E0 (message 0x66956810): every demand's SimulationBegin (slot 5).
void __fastcall BeginAll(void* self, void*)
{
    TreeNode* const header = Header(self, field::kDemands);
    for (TreeNode* node = First(header); node != header; node = Next(node))
        Virtual<uint8_t>(Value<void*>(node), 0x14);
}

// 0x006A4E70: each demand's effects on the others, then its EndOfCycle;
// repeated until a whole pass adds nothing above 0.01. A demand with sum
// effects (0x47BB3F50) becomes the sum of their supplies and demands.
void __fastcall Propagate(void* self, void*)
{
    static bool logged = false;
    FirstCall("Propagate", logged);
    TreeNode* const effects = Header(self, field::kEffects);
    bool settled;
    do {
        TreeNode* const demands = Header(self, field::kDemands);
        settled = true;
        if (First(demands) == demands)
            return;
        for (TreeNode* node = First(demands); node != demands; node = Next(node)) {
            const uint32_t key = node->key;
            void* const demand = Value<void*>(node);
            bool sums = false;
            float supplySum = 0.0f;
            float demandSum = 0.0f;
            // equal_range(key) in the effects.
            TreeNode* upper = effects;
            for (TreeNode* n = effects->parent; n;) {
                if (key < n->key) {
                    upper = n;
                    n = n->left;
                } else {
                    n = n->right;
                }
            }
            TreeNode* lower = effects;
            for (TreeNode* n = effects->parent; n;) {
                if (n->key < key) {
                    n = n->right;
                } else {
                    lower = n;
                    n = n->left;
                }
            }
            if (lower == upper) {
                Virtual<uint8_t>(demand, 0x78);
                continue;
            }
            for (TreeNode* effect = lower; effect != upper; effect = Next(effect)) {
                const char* const value = reinterpret_cast<const char*>(effect) + 0x1C;
                const uint32_t targetId = *reinterpret_cast<const uint32_t*>(value);
                void* const target = Demand(self, targetId);
                const uint32_t kind = *reinterpret_cast<const uint32_t*>(value + 4);
                const float factor = *reinterpret_cast<const float*>(value + 8);
                auto add = [&](std::size_t getter, std::size_t adder) {
                    const double amount = static_cast<double>(Virtual<float>(demand, getter)) * factor;
                    if (std::fabs(amount) > kSettled) {
                        Virtual<uint8_t>(target, adder, static_cast<float>(amount));
                        settled = false;
                    }
                };
                switch (kind) {
                case kSupplyToDemand:
                    add(0x28, 0x44);
                    break;
                case kSupplyToSupply:
                    add(0x28, 0x40);
                    break;
                case kDemandToDemand:
                    add(0x2C, 0x44);
                    break;
                case kCap: {
                    float supply = Virtual<float>(demand, 0x20);
                    double demandValue = Virtual<float>(demand, 0x24);
                    if (supply < static_cast<float>(kSettled))
                        supply = static_cast<float>(kSettled);
                    if (demandValue < 0.0)
                        demandValue = 0.0;
                    const float cap = demandValue < supply ? static_cast<float>(demandValue / supply) : 1.0f;
                    Virtual<void>(target, 0x6C, &cap);
                    break;
                }
                case kSum:
                    sums = true;
                    supplySum = static_cast<float>(static_cast<double>(Virtual<float>(target, 0x20)) + supplySum);
                    demandSum = static_cast<float>(static_cast<double>(Virtual<float>(target, 0x24)) + demandSum);
                    break;
                default:
                    break;
                }
            }
            Virtual<uint8_t>(demand, 0x78);
            if (sums) {
                const float supplyChange =
                    static_cast<float>(static_cast<double>(supplySum) - Virtual<float>(demand, 0x20));
                const float demandChange =
                    static_cast<float>(static_cast<double>(demandSum) - Virtual<float>(demand, 0x24));
                if (std::fabs(static_cast<double>(supplyChange)) > kSettled) {
                    Virtual<uint8_t>(demand, 0x40, supplyChange);
                    settled = false;
                }
                if (std::fabs(static_cast<double>(demandChange)) > kSettled) {
                    Virtual<uint8_t>(demand, 0x44, demandChange);
                    settled = false;
                }
            }
        }
    } while (!settled);
}

// 0x006A7600 (message 0x66956814, after the step): the passes, then the
// active demand of the twelve RCI groups published, truncated.
void __fastcall PublishActive(void* self, void*)
{
    CallThis<void>(addr::cSC4DemandSimulator_Propagate, self);
    static const uint32_t kGroups[][2] = {
        {0x21010, 0x2A67A399}, {0x21020, 0xAA67A3A0}, {0x21030, 0x8A67A3A4}, {0x23110, 0x0A67A3A9},
        {0x23120, 0xAA67A3AD}, {0x23130, 0xCA67A3B2}, {0x23320, 0xAA67A3B7}, {0x23330, 0x2A67A3BC},
        {0x24100, 0xEA67A3BF}, {0x24200, 0x0A67A3C4}, {0x24300, 0xEA67A3C8}, {0x24400, 0x8A67A3CC},
    };
    for (const auto& group : kGroups)
        Figure(group[1], Ftol(Virtual<float>(Demand(self, group[0]), 0x30)));
}

// State 2: the month's figures: population and jobs by wealth, the workers
// without jobs, and the weighted average of twelve demands (weights +0x68).
void PublishFigures(void* self)
{
    Figure(0x2A4E2003, Ftol(Virtual<float>(Demand(self, 0x1001), 0x20)));
    Figure(0x4A5E9485, Ftol(Virtual<float>(Demand(self, 0x1011), 0x20)));
    Figure(0xEA5E948A, Ftol(Virtual<float>(Demand(self, 0x1021), 0x20)));
    Figure(0x2A5E948E, Ftol(Virtual<float>(Demand(self, 0x1031), 0x20)));
    auto jobs = [self](uint32_t id) { return Virtual<int32_t>(self, 0x24, id); };
    const int32_t commerce1 = jobs(0x3111);
    Figure(0x8A5E950E, commerce1);
    const int32_t commerce2 = jobs(0x3121);
    Figure(0x0A5E9515, commerce2);
    const int32_t commerce3 = jobs(0x3131);
    Figure(0x8A5E954A, commerce3);
    const int32_t office2 = jobs(0x3321);
    Figure(0x8A5E9552, office2);
    const int32_t office3 = jobs(0x3331);
    Figure(0xEA5E9557, office3);
    const uint32_t commerce = static_cast<uint32_t>(office2) + static_cast<uint32_t>(office3) +
                              static_cast<uint32_t>(commerce3) + static_cast<uint32_t>(commerce2) +
                              static_cast<uint32_t>(commerce1);
    Figure(0x0A4E2056, static_cast<int32_t>(commerce));
    const int32_t agriculture = jobs(0x4101);
    Figure(0x2A5E95A1, agriculture);
    const int32_t dirty = jobs(0x4201);
    Figure(0x6A5E95A7, dirty);
    const int32_t manufacturing = jobs(0x4301);
    Figure(0x0A5E95AC, manufacturing);
    const int32_t hightech = jobs(0x4401);
    Figure(0xAA5E95B1, hightech);
    const uint32_t industry = static_cast<uint32_t>(hightech) + static_cast<uint32_t>(manufacturing) +
                              static_cast<uint32_t>(dirty) + static_cast<uint32_t>(agriculture);
    Figure(0x4A4E206B, static_cast<int32_t>(industry));
    const float workers = Virtual<float>(Demand(self, 0x2000), 0x24);
    Figure(0x6A931184, Ftol(workers));
    double idle = static_cast<double>(workers) - Unsigned(commerce) - Unsigned(industry);
    if (!(idle >= 0.0)) // below zero (or NaN): 0
        idle = 0.0;
    Figure(0x8A931172, Ftol(idle));
    uint64_t total = 0;
    uint64_t weighted = 0;
    const auto* const ids = reinterpret_cast<const uint32_t*>(kWeightedDemands);
    for (int i = 0; i < 12; ++i) {
        const int32_t supply = Ftol(Virtual<float>(Demand(self, ids[i]), 0x20));
        total += static_cast<uint64_t>(static_cast<int64_t>(supply));
        weighted += static_cast<uint32_t>(At<uint32_t>(self, field::kWeights + 4 * i) * static_cast<uint32_t>(supply));
    }
    Figure(0x8A63F30C, total ? static_cast<int32_t>(static_cast<uint32_t>(weighted / total)) : 0);
}

// The end of the cell walk: each counted value that moved since the last
// walk goes to its demand (region use 1: supply, 2: demand) and is kept.
// The two maps hold the same keys and are walked side by side.
void ApplyCounts(void* self)
{
    TreeNode* const pending = Header(self, field::kPending);
    TreeNode* counted = First(Header(self, field::kCounted));
    for (TreeNode* node = First(pending); node != pending; node = Next(node), counted = Next(counted)) {
        const uint32_t key = node->key;
        const float value = Value<float>(node);
        const float change = static_cast<float>(static_cast<double>(value) - Value<float>(counted));
        if (change == 0.0f)
            continue;
        TreeNode* const demands = Header(self, field::kDemands);
        TreeNode* const found = Find(demands, key);
        if (found == demands)
            continue;
        void* const demand = Value<void*>(found);
        const uint32_t use = Virtual<uint32_t>(demand, 0x70);
        if (use == 1)
            Virtual<uint8_t>(demand, 0x40, change);
        else if (use == 2)
            Virtual<uint8_t>(demand, 0x44, change);
        *FloatMapAt(static_cast<char*>(self) + field::kCounted, key) = value;
    }
}

// 0x006A7020 (messages 0x6695681A with a time budget, 0x66956814 with 5):
// one state a call.
//   0: the demands from the region, taxes and ordinances (0x006A6070), the
//      counts cleared, the cell walk restarted;
//   1: the cell walk (0x006A5A00 a cell) until the budget is spent, then the
//      counts applied;
//   2: the month's figures;
//   3: waits for MonthPassed.
void __fastcall Step(void* self, void*, uint32_t budget)
{
    static bool logged = false;
    FirstCall("Step", logged);
    uint32_t& state = At<uint32_t>(self, field::kState);
    if (state == 2) {
        PublishFigures(self);
        state = 3;
        return;
    }
    if (state == 0) {
        CallThis<void>(addr::cSC4DemandSimulator_RegionDemand, self);
        CallThis<void>(addr::cSC4DemandSimulator_ClearCounts, self, static_cast<char*>(self) + field::kPending);
        auto* const walk = At<uint8_t*>(self, field::kCellIterator);
        auto* const ints = reinterpret_cast<int32_t*>(walk);
        ints[1] = 0;
        ints[2] = 0;
        walk[0] = 0;
        if (ints[4] > 0 || ints[5] > 0 || ints[6] < 0 || ints[7] < 0)
            walk[0] = 2;
        state = 1;
    } else if (state != 1) {
        return;
    }
    uint32_t& once = Global<uint32_t>(kWalkOnce);
    if (!(once & 1)) {
        once |= 1;
        Global<uint32_t>(kWalkStep) = 0x400;
        Global<uint32_t>(kWalkStep + 4) = 0;
    }
    using StartFn = void(__thiscall*)(Deadline* deadline, uint32_t budget);
    using CheckFn = uint8_t(__thiscall*)(Deadline* deadline);
    using EndFn = void(__thiscall*)(Deadline* deadline);
    Deadline deadline{};
    reinterpret_cast<StartFn>(addr::SC4Deadline_Start)(&deadline, budget);
    deadline.stepGlobal = &Global<uint32_t>(kWalkStep);
    const uint32_t step = *deadline.stepGlobal;
    deadline.step = step;
    deadline.timedOut = 0;
    uint32_t count = 0;
    for (;;) {
        count += step;
        deadline.count = count;
        if (!(count & 0x3FF) && reinterpret_cast<CheckFn>(addr::SC4Deadline_Expired)(&deadline)) {
            deadline.timedOut = 1;
            break;
        }
        auto* const walk = At<uint8_t*>(self, field::kCellIterator);
        auto* const ints = reinterpret_cast<int32_t*>(walk);
        if (walk[0] == 2) {
            ApplyCounts(self);
            state = 2;
            break;
        }
        if (walk[0] == 0) {
            walk[0] = 1;
        } else {
            ints[1] += ints[3];
            if (ints[1] > ints[6]) {
                ints[1] = ints[4];
                ints[2] += ints[3];
                if (ints[2] > ints[7]) {
                    walk[0] = 2;
                    ApplyCounts(self);
                    state = 2;
                    break;
                }
            }
        }
        CallThis<void>(addr::cSC4DemandSimulator_CountCell, self, ints[1], ints[2]);
    }
    reinterpret_cast<EndFn>(addr::SC4Deadline_End)(&deadline);
}

// 0x006A9450 (MonthPassed, 0x66956816): the month's effects (0x006A3ED0)
// and the lots' share of jobs (0x006A8FF0); a waiting step starts again.
void __fastcall MonthPassed(void* self, void*)
{
    CallThis<void>(addr::cSC4DemandSimulator_MonthEffects, self);
    CallThis<void>(addr::cSC4DemandSimulator_MonthJobs, self);
    if (At<uint32_t>(self, field::kState) == 3)
        At<uint32_t>(self, field::kState) = 0;
}

// 0x006A5920: the demand effects of a type of occupant (the global map at
// 0x00B49008: a list of demand ID, supply and demand factors each), times
// the amount, added (or taken off).
void __fastcall OccupantDemand(void* self, void*, uint32_t type, uint8_t add, float amount)
{
    auto* const map = reinterpret_cast<void*>(addr::cSC4DemandSimulator_OccupantEffects);
    TreeNode* const header = *static_cast<TreeNode**>(map);
    TreeNode* const found = Find(header, type);
    if (found == header)
        return;
    void** const list = Value<void**>(found);
    for (void** entry = static_cast<void**>(list[0]); entry != list; entry = static_cast<void**>(entry[0])) {
        const auto id = reinterpret_cast<uint32_t>(entry[2]);
        void* const demand = Demand(self, id);
        const float* const factors = reinterpret_cast<const float*>(entry + 3);
        if (factors[0] != 0.0f) {
            const double change = static_cast<double>(amount) * factors[0];
            Virtual<uint8_t>(demand, 0x40, static_cast<float>(add ? change : -change));
        }
        if (factors[1] != 0.0f) {
            const double change = static_cast<double>(amount) * factors[1];
            Virtual<uint8_t>(demand, 0x44, static_cast<float>(add ? change : -change));
        }
    }
}

// The node for `key` in a map, or its header (lower bound, then the check).
TreeNode* FindIn(TreeNode* header, uint32_t key) { return Find(header, key); }

// 0x006A2C80: an occupant's jobs (the service at +0x3C: its slot 39 gives
// the type's record, slot 30 a key; the record's maps hold the jobs as
// uint16) added to (or taken off) that key's demand supply.
void __fastcall OccupantJobs(void* self, void*, uint32_t type, uint32_t value, uint8_t add)
{
    void* const service = At<void*>(self, field::kService3C);
    auto* const record = Virtual<uint32_t*>(service, 0x9C, type);
    if (!record[0])
        return;
    const uint32_t key = Virtual<uint32_t>(service, 0x78, record[0], value);
    int32_t jobs = 0;
    for (auto* element = reinterpret_cast<TreeNode**>(record[3]); element != reinterpret_cast<TreeNode**>(record[4]);
         element += 3) {
        TreeNode* const header = element[0];
        TreeNode* const node = FindIn(header, key);
        if (node != header)
            jobs += *reinterpret_cast<const uint16_t*>(reinterpret_cast<const char*>(node) + 0x14);
    }
    TreeNode* const demands = Header(self, field::kDemands);
    TreeNode* const found = FindIn(demands, key);
    if (found == demands)
        return;
    Virtual<uint8_t>(Value<void*>(found), 0x40, static_cast<float>(add ? jobs : -jobs));
}

// --- The message handler (cIGZMessageTarget2 at +4, slot 3: 0x006A9470) -----------

uint8_t __fastcall DoMessage(void* part, void*, void* message)
{
    void* const self = static_cast<char*>(part) - 4;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    switch (type) {
    case 0x66956810:
        CallThis<void>(addr::cSC4DemandSimulator_BeginAll, self);
        break;
    case 0x26C63343:
        CallThis<void>(addr::cSC4DemandSimulator_Propagate, self);
        CallThis<void>(addr::cSC4DemandSimulator_Propagate, self);
        break;
    case 0x66956814:
        CallThis<void>(addr::cSC4DemandSimulator_Step, self, 5u);
        CallThis<void>(addr::cSC4DemandSimulator_PublishActive, self);
        break;
    case 0x66956816:
        CallThis<void>(addr::cSC4DemandSimulator_MonthPassed, self);
        break;
    case 0x6695681A:
        CallThis<void>(addr::cSC4DemandSimulator_Step, self, Virtual<uint32_t>(message, 0x4C));
        break;
    case 0xCA1B151C:
        CallThis<void>(addr::cSC4DemandSimulator_LoadTuning, self);
        break;
    case 0x8A0D15DA:
    case 0xEA0D163E:
        Virtual<uint32_t>(message, 0x24);
        Virtual<uint32_t>(message, 0x38);
        Virtual<uint32_t>(message, 0x4C);
        CallThis<void>(addr::cSC4DemandSimulator_Neighbors, self, 0u);
        break;
    case 0xE8BDA2E7: {
        // An occupant's state changed: its demand effects and jobs follow
        // whether it is now functional (states 2 and 3), unless its exemplar
        // has 0x27812844 set.
        void* const occupant = Virtual<void*>(message, 0x80);
        const uint8_t before = static_cast<uint8_t>(Virtual<uint32_t>(message, 0x24));
        const uint8_t after = static_cast<uint8_t>(Virtual<uint32_t>(message, 0x38));
        uint32_t occupantType = Virtual<uint32_t>(message, 0x4C);
        const uint32_t value = Virtual<uint32_t>(message, 0x60);
        const bool on = after >= 2 && after < 4;
        const bool wasOn = before >= 2 && before < 4;
        if (on == wasOn)
            break;
        if (!occupantType) {
            occupantType = Virtual<uint32_t>(occupant, 0xB8, 1u);
            if (!occupantType)
                break;
        }
        uint8_t skip = 0;
        if (void* const building = Virtual<void*>(occupant, 0xC0)) {
            void* const exemplar = Virtual<void*>(building, 0xC);
            void* const holder = Virtual<void*>(exemplar, 0x18);
            reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, uint8_t*)>(addr::SC4_GetPropertyBool)(holder, 0x27812844,
                                                                                                      &skip);
        }
        if (skip)
            break;
        const uint8_t add = on ? 1 : 0;
        CallThis<void>(addr::cSC4DemandSimulator_OccupantDemand, self, occupantType, add,
                       Global<float>(addr::cSC4DemandSimulator_One));
        CallThis<void>(addr::cSC4DemandSimulator_OccupantJobs, self, occupantType, value, add);
        break;
    }
    default:
        break;
    }
    return 1;
}

// --- Installation -----------------------------------------------------------

template <typename Fn>
bool Install(std::uintptr_t address, Fn ours, Fn* original)
{
    const addr::Entry* entry = addr_check::Find(address);
    return entry && hook::Install(*entry, ours, original);
}

using SelfFn = void(__fastcall*)(void*, void*);
using StepFn = void(__fastcall*)(void*, void*, uint32_t);
using ClearFn = void(__fastcall*)(void*, void*, void*);
using OccupantDemandFn = void(__fastcall*)(void*, void*, uint32_t, uint8_t, float);
using OccupantJobsFn = void(__fastcall*)(void*, void*, uint32_t, uint32_t, uint8_t);

StepFn g_originalStep = nullptr;
SelfFn g_originalBeginAll = nullptr, g_originalPropagate = nullptr, g_originalPublish = nullptr,
       g_originalMonth = nullptr;
ClearFn g_originalClear = nullptr;
OccupantDemandFn g_originalOccupantDemand = nullptr;
OccupantJobsFn g_originalOccupantJobs = nullptr;

bool SwapSlot(std::uintptr_t vtableAddress, int index, std::uintptr_t original, void* ours, const char* what)
{
    auto* const slot = reinterpret_cast<std::uintptr_t*>(vtableAddress) + index;
    if (*slot != original) {
        log::Write("demand %s: slot %d holds %08X, expected %08X; not installing", what, index,
                   static_cast<unsigned>(*slot), static_cast<unsigned>(original));
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
        return false;
    *slot = reinterpret_cast<std::uintptr_t>(ours);
    VirtualProtect(slot, sizeof(*slot), old, &old);
    return true;
}

bool InstallStep(registry::Mode)
{
    return game::IsBuild641() && Install(addr::cSC4DemandSimulator_Step, &Step, &g_originalStep) &&
           Install(addr::cSC4DemandSimulator_ClearCounts, &ClearCounts, &g_originalClear);
}

bool InstallPasses(registry::Mode)
{
    return game::IsBuild641() && Install(addr::cSC4DemandSimulator_BeginAll, &BeginAll, &g_originalBeginAll) &&
           Install(addr::cSC4DemandSimulator_Propagate, &Propagate, &g_originalPropagate) &&
           Install(addr::cSC4DemandSimulator_PublishActive, &PublishActive, &g_originalPublish) &&
           Install(addr::cSC4DemandSimulator_MonthPassed, &MonthPassed, &g_originalMonth);
}

bool InstallMessages(registry::Mode)
{
    return game::IsBuild641() &&
           Install(addr::cSC4DemandSimulator_OccupantDemand, &OccupantDemand, &g_originalOccupantDemand) &&
           Install(addr::cSC4DemandSimulator_OccupantJobs, &OccupantJobs, &g_originalOccupantJobs) &&
           SwapSlot(addr::cSC4DemandSimulator_vftable_4, 3, 0x006A9470, reinterpret_cast<void*>(&DoMessage),
                    "messages");
}

const bool g_registeredStep = registry::Register({
    "simulators.demand.step",
    "simulators/demand",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4DemandSimulator_Step,
    "Our demand step: region demand, the cell walk, the month's figures (docs/demand.md)",
    false,
    InstallStep,
});

const bool g_registeredPasses = registry::Register({
    "simulators.demand.passes",
    "simulators/demand",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4DemandSimulator_Propagate,
    "Our demand passes: effects between demands, the published active demand, MonthPassed (docs/demand.md)",
    false,
    InstallPasses,
});

const bool g_registeredMessages = registry::Register({
    "simulators.demand.messages",
    "simulators/demand",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4DemandSimulator_OccupantDemand,
    "Our demand DoMessage and its occupant handlers (docs/demand.md)",
    false,
    InstallMessages,
});

} // namespace
} // namespace opensc4::demand_sim
