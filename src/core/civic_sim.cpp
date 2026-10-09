// Our cSC4CivicBuildingSimulator (build 641), first part: the interface
// (slots 5-8), DoMessage, the month and the building handlers, each a
// replacement on the original object (docs/civic.md).
#include "core/civic_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::civic_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("civic: first %s", name);
}

namespace {

using FreeFn = void(__cdecl*)(void* p);
using NodesFn = void**(__thiscall*)(void* list, uint32_t count, uint32_t unused);
using EraseFn = void(__thiscall*)(void* list, void** next, void** node);
using PropertyFn = uint8_t(__cdecl*)(void* holder, uint32_t property, void* value);

constexpr uint32_t kBuildingInterface = 0x87DDA3A9; // cISC4BuildingOccupant
constexpr uint32_t kLotInterface = 0xE818FCED;      // cISC4Lot
constexpr uint32_t kZooFlag = 0x1917;               // the occupant flag of the ape escape candidates
constexpr uint32_t kLandmarkProperty = 0xAA59670C;  // the budget department of a building
constexpr uint32_t kTicketProperty = 0x4A5A495E;    // a second department line (the building's item)

void* Service(std::uintptr_t global) { return Global<void*>(global); }

// A list (SC4List: head node, nodes next, previous, value): the node of a
// value, or the head.
void** FindNode(void** head, void* value)
{
    void** node = static_cast<void**>(head[0]);
    while (node != head && node[2] != value)
        node = static_cast<void**>(node[0]);
    return node;
}

void Append(void* list, void* value)
{
    void** const head = *static_cast<void***>(list);
    void** const node = reinterpret_cast<NodesFn>(addr::SC4List_AllocateNodes)(list, 1, 0);
    node[2] = value;
    void** const tail = static_cast<void**>(head[1]);
    node[0] = head;
    node[1] = tail;
    tail[0] = node;
    head[1] = node;
}

void Erase(void* list, void** node)
{
    void* next = nullptr;
    reinterpret_cast<EraseFn>(addr::SC4List_Erase)(list, &next, node);
}

// --- The interface (vtable 0x00AB0C7C) -------------------------------------------

// 5 (0x0069E5B0): the key in the set of excluded families (+0x18).
uint8_t __fastcall IsBuildingExcluded(char* self, void*, uint32_t family)
{
    return CallThis<void*>(addr::SC4Uint32HashSet_Find, self + field::kExcluded, &family) != nullptr ? 1 : 0;
}

// 6 (0x0069D8D0): the status of a conditional building, or null.
void* __fastcall GetConditionalBuildingStatus(char* self, void*, uint32_t instance)
{
    char* const statuses = self + field::kStatuses;
    void** const buckets = At<void**>(statuses, table::kBuckets);
    const auto count = static_cast<uint32_t>(At<void**>(statuses, table::kBucketsEnd) - buckets);
    auto* node = static_cast<uint32_t*>(buckets[instance % count]);
    while (node && node[1] != instance)
        node = reinterpret_cast<uint32_t*>(node[0]);
    return node ? node + 2 : nullptr;
}

// 7 (0x0069E9F0): a conditional building's status set (made when new).
// A status: a flag (uint8), a count (uint16 at +2), a string at +4 (vtable,
// begin, end, capacity, a word).
void __fastcall UpdateConditionalBuildingStatus(char* self, void*, uint32_t instance, const char* status)
{
    using AssignFn = void(__thiscall*)(void* buffer, const char* begin, const char* end, void* tag);
    auto* const value =
        CallThis<char*>(addr::cSC4CivicBuildingSimulator_StatusAt, self + field::kStatuses, &instance);
    At<uint8_t>(value, 0) = At<uint8_t>(const_cast<char*>(status), 0);
    At<uint16_t>(value, 2) = At<uint16_t>(const_cast<char*>(status), 2);
    const char* const from = status + 8;
    if (from != value + 8)
        reinterpret_cast<AssignFn>(addr::SC4String_AssignRange)(value + 8, At<const char*>(const_cast<char*>(from), 0),
                                                                At<const char*>(const_cast<char*>(from), 4), &instance);
}

// 8 (0x0069E5D0): the landmarks (+0x44).
uint32_t __fastcall GetLandmarkCount(char* self, void*)
{
    void** const head = At<void**>(self, field::kLandmarks);
    uint32_t count = 0;
    for (void** node = static_cast<void**>(head[0]); node != head; node = static_cast<void**>(node[0]))
        ++count;
    return count;
}

struct Slot {
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(5, 0x0069E5B0, IsBuildingExcluded),
    OPENSC4_SLOT(6, 0x0069D8D0, GetConditionalBuildingStatus),
    OPENSC4_SLOT(7, 0x0069E9F0, UpdateConditionalBuildingStatus),
    OPENSC4_SLOT(8, 0x0069E5D0, GetLandmarkCount),
};

#undef OPENSC4_SLOT

constexpr int kVtableSlots = 10;

bool InstallInterface(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4CivicBuildingSimulator_vftable);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("civic interface: slot %d holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    DWORD old = 0;
    if (!VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), PAGE_READWRITE, &old))
        return false;
    for (const Slot& slot : kSlots)
        vtable[slot.index] = reinterpret_cast<std::uintptr_t>(slot.ours);
    VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), old, &old);
    log::Write("civic interface: %u of %d cISC4CivicBuildingSimulator slots are ours",
               static_cast<unsigned>(sizeof(kSlots) / sizeof(kSlots[0])), kVtableSlots);
    return true;
}

const bool g_registeredInterface = registry::Register({
    "simulators.civic.interface",
    "simulators/civic",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4CivicBuildingSimulator_vftable,
    "Our cISC4CivicBuildingSimulator slots 5-8 on the original object (docs/civic.md)",
    false,
    InstallInterface,
});

// --- The month (0x0069E010) -------------------------------------------------------

// One chance in a hundred: a random building of +0x40 gets an ape escape
// (city slot 47, then its slot 10). Then each landmark's lot is abandoned
// (state 2) while its department's funding is below half, and back (0)
// when it is not.
void __fastcall OnMonth(char* self, void*)
{
    static bool logged = false;
    FirstCall("OnMonth", logged);
    using StringFn = void*(__thiscall*)(void* string, const char* text);
    using NextFloatFn = double(__thiscall*)(void* random);
    using BelowFn = uint32_t(__thiscall*)(void* random, uint32_t n);
    using RandomFn = void*(__thiscall*)(void* random, int32_t seed);
    void** const zoo = At<void**>(self, field::kZooBuildings);
    if (static_cast<void**>(zoo[0]) != zoo) {
        alignas(8) char random[0x20];
        reinterpret_cast<RandomFn>(addr::SC4Random_Construct)(random, -1);
        const double draw = reinterpret_cast<NextFloatFn>(addr::cRZRandom_NextFloat)(random);
        if (draw <= static_cast<double>(0.01f)) {
            uint32_t count = 0;
            for (void** node = static_cast<void**>(zoo[0]); node != zoo; node = static_cast<void**>(node[0]))
                ++count;
            uint32_t pick = reinterpret_cast<BelowFn>(addr::SC4Random_BelowLcg)(random, count);
            void** node = static_cast<void**>(zoo[0]);
            if (node != zoo) {
                while (pick) {
                    node = static_cast<void**>(node[0]);
                    --pick;
                    if (node == zoo)
                        break;
                }
                if (node != zoo) {
                    void* const building = node[2];
                    if (void* const layer = Virtual<void*>(Service(addr::SC4_City), 0xBC)) {
                        struct {
                            const void* vtable;
                            char* begin;
                            char* end;
                            char* capacity;
                            uint32_t zero;
                        } name;
                        reinterpret_cast<StringFn>(addr::SC4String_FromText)(&name, Global<const char*>(0x00B0D1E8));
                        Virtual<uint32_t>(layer, 0x28, &name, building, 0u);
                        name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
                        if (name.begin)
                            reinterpret_cast<FreeFn>(addr::SC4_Free)(name.begin);
                    }
                }
            }
        }
    }
    void** const head = At<void**>(self, field::kLandmarks);
    if (static_cast<void**>(head[0]) == head)
        return;
    void* const budget = Service(addr::SC4_BudgetSimulator);
    uint32_t info[8];
    for (void** node = static_cast<void**>(head[0]); node != head; node = static_cast<void**>(node[0])) {
        void* const occupant = node[2];
        info[7] = 0;
        void* const holder = Virtual<void*>(occupant, 0x18);
        if (!Virtual<uint8_t>(budget, 0xCC, holder, kLandmarkProperty, info))
            continue;
        void* const department = Virtual<void*>(budget, 0x70, info[0]);
        if (!department)
            continue;
        void* const lot = Virtual<void*>(Service(addr::SC4_LotManager), 0x38, occupant);
        if (!lot)
            continue;
        const float funding = *Virtual<const float*>(department, 0x28);
        if (!(funding < 0.5f)) {
            if (Virtual<int32_t>(lot, 0x8C) == 2)
                Virtual<uint32_t>(lot, 0x90, 0u);
        } else if (Virtual<int32_t>(lot, 0x8C) == 0) {
            Virtual<uint32_t>(lot, 0x90, 2u);
        }
    }
}

using MonthFn = void(__fastcall*)(char*, void*);
MonthFn g_originalMonth = nullptr;

bool InstallMonth(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4CivicBuildingSimulator_OnMonth);
    return game::IsBuild641() && entry && hook::Install(*entry, &OnMonth, &g_originalMonth);
}

const bool g_registeredMonth = registry::Register({
    "simulators.civic.month",
    "simulators/civic",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4CivicBuildingSimulator_OnMonth,
    "Our civic MonthPassed: the ape escape draw and the landmarks' lots (docs/civic.md)",
    false,
    InstallMonth,
});

// --- The buildings (0x0069DE20, 0x0069DAA0) ------------------------------------------

// A lot's building: its department line (budget slot 51, then the
// department's item for the building), the landmark list, and the ape
// escape list when it has the flag. `info` is the budget simulator's
// answer (slot 51): the department (0), the item key (1), an int64 cost
// (4, 5), a word (6).
void __fastcall OnLotDeveloped(char* self, void*, void* lot)
{
    static bool logged = false;
    FirstCall("OnLotDeveloped", logged);
    void* const building = Virtual<void*>(lot, 0xC0);
    if (!building)
        return;
    void* const holder = Virtual<void*>(Virtual<void*>(building, 0xC), 0x18);
    void* const budget = Service(addr::SC4_BudgetSimulator);
    uint32_t info[8];
    info[7] = 0;
    if (Virtual<uint8_t>(budget, 0xCC, holder, kLandmarkProperty, info)) {
        if (info[1] == 0) {
            const uint32_t key = Virtual<uint32_t>(building, 0x10);
            info[1] = key;
            Virtual<uint32_t>(budget, 0xD0, holder, info[6], key);
        }
        if (void* const department = Virtual<void*>(budget, 0x70, info[0])) {
            void* item = Virtual<void*>(department, 0x50, info[1]);
            if (!item)
                item = Virtual<void*>(department, 0x48, info[1], 0u);
            if (item)
                Virtual<uint32_t>(item, 0x34, info[4], info[5]);
        }
        Virtual<uint32_t>(building, 0x4);
        Append(self + field::kLandmarks, Virtual<void*>(building, 0xC));
    }
    if (Virtual<uint8_t>(budget, 0xCC, holder, kTicketProperty, info)) {
        if (void* const department = Virtual<void*>(budget, 0x70, info[0])) {
            const uint32_t key = Virtual<uint32_t>(building, 0x10);
            void* item = Virtual<void*>(department, 0x50, key);
            if (!item) {
                item = Virtual<void*>(department, 0x48, key, 0u);
                Virtual<uint32_t>(item, 0x14, 1u);
            }
            Virtual<uint32_t>(item, 0x40, info[4], info[5]);
        }
    }
    if (!Virtual<uint8_t>(Virtual<void*>(building, 0xC), 0x58, kZooFlag))
        return;
    void** const zoo = At<void**>(self, field::kZooBuildings);
    if (FindNode(zoo, Virtual<void*>(building, 0xC)) != zoo)
        return;
    Append(self + field::kZooBuildings, Virtual<void*>(building, 0xC));
    Virtual<uint32_t>(building, 0x4);
}

// A building leaving: the reverse of the above.
void __fastcall OnBuildingRemoved(char* self, void*, void* building)
{
    static bool logged = false;
    FirstCall("OnBuildingRemoved", logged);
    void* const holder = Virtual<void*>(Virtual<void*>(building, 0xC), 0x18);
    void* const budget = Service(addr::SC4_BudgetSimulator);
    uint32_t info[8];
    info[7] = 0;
    if (Virtual<uint8_t>(budget, 0xCC, holder, kLandmarkProperty, info)) {
        Virtual<uint32_t>(budget, 0xE0, holder, info[6]);
        void* const occupant = Virtual<void*>(building, 0xC);
        void** const landmarks = At<void**>(self, field::kLandmarks);
        void** const node = FindNode(landmarks, occupant);
        if (node != landmarks) {
            Virtual<uint32_t>(node[2], 0x8);
            Erase(self + field::kLandmarks, node);
        }
    }
    if (Virtual<uint8_t>(budget, 0xCC, holder, kTicketProperty, info)) {
        if (void* const department = Virtual<void*>(budget, 0x70, info[0])) {
            const uint32_t key = Virtual<uint32_t>(building, 0x10);
            if (void* const item = Virtual<void*>(department, 0x50, key)) {
                const int64_t cost = static_cast<int64_t>(static_cast<uint64_t>(info[5]) << 32 | info[4]);
                Virtual<uint32_t>(item, 0x3C, Virtual<int64_t>(item, 0x38) - cost);
                if (Virtual<int64_t>(item, 0x38) < 1)
                    Virtual<uint32_t>(department, 0x4C, key);
            }
        }
    }
    if (!Virtual<uint8_t>(Virtual<void*>(building, 0xC), 0x58, kZooFlag))
        return;
    void** const zoo = At<void**>(self, field::kZooBuildings);
    void** const node = FindNode(zoo, Virtual<void*>(building, 0xC));
    if (node == zoo)
        return;
    Erase(self + field::kZooBuildings, node);
    Virtual<uint32_t>(building, 0x8);
}

using BuildingFn = void(__fastcall*)(char*, void*, void*);
BuildingFn g_originalLotDeveloped = nullptr;
BuildingFn g_originalBuildingRemoved = nullptr;

bool InstallBuildings(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* developed = addr_check::Find(addr::cSC4CivicBuildingSimulator_OnLotDeveloped);
    const addr::Entry* removed = addr_check::Find(addr::cSC4CivicBuildingSimulator_OnBuildingRemoved);
    return developed && removed && hook::Install(*developed, &OnLotDeveloped, &g_originalLotDeveloped) &&
           hook::Install(*removed, &OnBuildingRemoved, &g_originalBuildingRemoved);
}

const bool g_registeredBuildings = registry::Register({
    "simulators.civic.buildings",
    "simulators/civic",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4CivicBuildingSimulator_OnLotDeveloped,
    "Our civic building handlers: a lot developed, a building removed (docs/civic.md)",
    false,
    InstallBuildings,
});

// --- DoMessage (+0x4 vtable 0x00AB0C6C, slot 3) --------------------------------------

uint8_t __fastcall DoMessage(char* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    char* const self = part - 0x4;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == 0x66956816u) { // MonthPassed
        CallThis<void>(addr::cSC4CivicBuildingSimulator_OnMonth, self);
        return 1;
    }
    if (type == 0xE8BDA2E7u) {
        void* lot = nullptr;
        if (Virtual<uint8_t>(Virtual<void*>(message, 0x80), 0x0, kLotInterface, &lot)) {
            if (Virtual<int32_t>(message, 0x38) == 3)
                CallThis<void>(addr::cSC4CivicBuildingSimulator_OnLotDeveloped, self, lot);
            Virtual<uint32_t>(lot, 0x8);
        }
        return 1;
    }
    if (type != 0x99EF1142u && type != 0x99EF1143u) // occupant inserted, removed
        return 1;
    void* const occupant = Virtual<void*>(message, 0x28);
    if (Virtual<uint32_t>(occupant, 0x1C) != 0x278128A0u) // a building
        return 1;
    void* const holder = Virtual<void*>(occupant, 0x18);
    uint8_t conditional = 0;
    reinterpret_cast<PropertyFn>(addr::SC4_GetPropertyBool)(holder, 0xEA3209F8u, &conditional);
    if (conditional) {
        uint32_t key[3] = {};
        void* building = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, kBuildingInterface, &building)) {
            void* const exemplar = Virtual<void*>(building, 0x10, key);
            if (Virtual<uint8_t>(Service(addr::SC4_Service43D68), 0x38, exemplar)) {
                if (auto* const status = Virtual<char*>(self, 0x18, key[2])) {
                    if (type == 0x99EF1142u)
                        ++At<uint16_t>(status, 2);
                    else
                        --At<uint16_t>(status, 2);
                }
            }
            Virtual<uint32_t>(building, 0x8);
        }
    }
    uint32_t family = 0;
    if (reinterpret_cast<PropertyFn>(addr::SC4_GetPropertyUint32)(holder, 0xEA2E078Bu, &family)) {
        if (type == 0x99EF1142u) {
            char result[12];
            CallThis<void>(addr::SC4Uint32HashSet_InsertUnique, self + field::kExcluded, result, &family);
            return 1;
        }
        CallThis<uint32_t>(addr::SC4Uint32HashSet_Erase, self + field::kExcluded, &family);
    }
    if (type == 0x99EF1143u) {
        void* building = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, kBuildingInterface, &building)) {
            CallThis<void>(addr::cSC4CivicBuildingSimulator_OnBuildingRemoved, self, building);
            Virtual<uint32_t>(building, 0x8);
        }
    }
    return 1;
}

bool InstallMessages(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4CivicBuildingSimulator_vftable_4);
    if (vtable[3] != addr::cSC4CivicBuildingSimulator_DoMessage) {
        log::Write("civic messages: slot 3 holds %08X; not installing", static_cast<unsigned>(vtable[3]));
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(&vtable[3], sizeof(*vtable), PAGE_READWRITE, &old))
        return false;
    vtable[3] = reinterpret_cast<std::uintptr_t>(&DoMessage);
    VirtualProtect(&vtable[3], sizeof(*vtable), old, &old);
    return true;
}

const bool g_registeredMessages = registry::Register({
    "simulators.civic.messages",
    "simulators/civic",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4CivicBuildingSimulator_vftable_4,
    "Our civic DoMessage (docs/civic.md)",
    false,
    InstallMessages,
});

} // namespace
} // namespace opensc4::civic_sim
