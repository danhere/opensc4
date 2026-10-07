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

#include <windows.h>

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
                Virtual<uint32_t>(value, 0x74, share + usedNow); // cIGZVariant::SetValSint32
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

// --- OnOccupantInsertedOrRemoved (0x006E4680) ---------------------------------

struct Vector3 {
    float x, y, z;
};

using GetPropertyBoolFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint8_t* value);
using SetPropertyFn = void(__cdecl*)(void* holder, uint32_t property, uint32_t value);
using SetProperty64Fn = void(__cdecl*)(void* holder, uint32_t property, int64_t value);
using SpreadFn2 = void(__cdecl*)(void* grid, int32_t tractsX, int32_t tractsZ, int32_t tractX, int32_t tractZ,
                                 float radius, float value, float edge);
using UpdateListFn = uint8_t(__thiscall*)(void* self, void* list, void* occupant, uint8_t insert);

const auto GetPropertyBool = reinterpret_cast<GetPropertyBoolFn>(addr::SC4_GetPropertyBool);
const auto SetPropertySint32 = reinterpret_cast<SetPropertyFn>(addr::SC4_SetPropertySint32);
const auto SetPropertyUint32 = reinterpret_cast<SetPropertyFn>(addr::SC4_SetPropertyUint32);
const auto SetPropertySint64 = reinterpret_cast<SetProperty64Fn>(addr::SC4_SetPropertySint64);
const auto SpreadAround = reinterpret_cast<SpreadFn2>(addr::SC4SimGrid_SpreadAround);
const auto CallUpdateBuildingList = reinterpret_cast<UpdateListFn>(addr::cSC4PollutionSimulator_UpdateBuildingList);

// cISCPropertyHolder::GetProperty, then the value's array (variant slot
// 105 for int32 arrays, 111 for float arrays), or null.
void* PropertyArray(void* holder, uint32_t property, std::size_t slot)
{
    void* const found = Virtual<void*>(holder, 0x24, property);
    if (!found)
        return nullptr;
    return Virtual<void*>(Virtual<void*>(found, 0x14), slot);
}

// cISCPropertyHolder slot 10 with the arguments the game passes.
void AddProperty(void* holder, uint32_t property)
{
    Virtual<uint32_t>(holder, 0x28, property, 0u, 1u, 0u);
}

int32_t Clamp(int32_t value, int32_t last)
{
    return value < 0 ? 0 : (value > last ? last : value);
}

// A building appears in or leaves the city: its pollution footprint in the
// source grids, its place in the building lists and its properties and
// budget item.
void __fastcall OnOccupantInsertedOrRemoved(void* self, void*, void* occupant, uint8_t inserted)
{
    static bool loggedInsert = false;
    static bool loggedRemove = false;
    bool& logged = inserted ? loggedInsert : loggedRemove;
    if (!logged) {
        logged = true;
        log::Write("pollution buildings: first occupant %s", inserted ? "inserted" : "removed");
    }
    void* const holder = PropertyHolder(occupant);
    uint8_t ignored = 0;
    if (GetPropertyBool(holder, 0x029244DB, &ignored) && ignored)
        return;

    if (auto* const amounts = static_cast<int32_t*>(PropertyArray(holder, 0x27812851, 0x1A4))) {
        auto* edges = reinterpret_cast<int32_t*>(addr::PollutionTuning_DefaultSpreadEdge);
        if (void* const own = PropertyArray(holder, 0xAA5832F3, 0x1A4))
            edges = static_cast<int32_t*>(own);

        // Garbage: added to every cell of the building's footprint.
        Vector3 low;
        Vector3 high;
        Virtual<uint32_t>(occupant, 0x28, &low, &high); // cISC4Occupant::GetBoundingBox
        int32_t x0, z0, x1, z1;
        void* const city = At<void*>(self, 0x18);
        Virtual<uint32_t>(city, 0x17C, low.x, low.z, &x0, &z0); // position to cell
        Virtual<uint32_t>(city, 0x17C, high.x, high.z, &x1, &z1);
        void* const sizes = Global<void*>(addr::SC4_City);
        const int32_t sizeX = Virtual<int32_t>(sizes, 0x174);
        const int32_t sizeZ = Virtual<int32_t>(sizes, 0x178);
        x0 = Clamp(x0, sizeX - 1);
        x1 = Clamp(x1, sizeX - 1);
        z0 = Clamp(z0, sizeZ - 1);
        z1 = Clamp(z1, sizeZ - 1);
        const int32_t garbage = inserted ? amounts[2] : -amounts[2];
        void* const garbageGrid = Grid(self, field::kValueGrids + 8);
        const uint32_t shift = GetTractShift(garbageGrid) & 31;
        for (int32_t x = x0; x <= x1; ++x) {
            for (int32_t z = z0; z <= z1; ++z) {
                const int32_t value = GetCellValue(garbageGrid, static_cast<uint32_t>(x), static_cast<uint32_t>(z)) + garbage;
                SetTractValue(garbageGrid, static_cast<uint32_t>(x >> shift), static_cast<uint32_t>(z >> shift),
                              static_cast<int16_t>(value));
            }
        }
        At<uint8_t>(self, field::kGarbageChanged) = 1;

        // All four: spread around the building's tract within a radius.
        Vector3 center;
        Virtual<uint32_t>(occupant, 0x20, &center); // cISC4Occupant::GetPosition
        float defaultRadii[4] = {2.5f, 1.5f, 0.5f, 4.0f};
        float* radii = defaultRadii;
        if (void* const own = PropertyArray(holder, 0x68EE9764, 0x1BC))
            radii = static_cast<float*>(own);
        for (int type = 0; type < 4; ++type) {
            if (!(radii[type] > 0.0f))
                continue;
            void* const grid = Grid(self, field::kValueGrids + 4 * type);
            const int32_t tractsX = Virtual<int32_t>(grid, 0x30);
            const int32_t tractsZ = Virtual<int32_t>(grid, 0x34);
            int32_t tractX, tractZ;
            Virtual<uint32_t>(grid, 0x4C, center.x, center.z, &tractX, &tractZ); // PositionToTract
            Virtual<uint32_t>(grid, 0x28);
            const int32_t edge = inserted ? edges[type] : -edges[type];
            const int32_t amount = inserted ? amounts[type] : -amounts[type];
            SpreadAround(Virtual<void*>(grid, 0x60), tractsX, tractsZ, tractX, tractZ, radii[type],
                         static_cast<float>(amount), static_cast<float>(edge));
        }
    }

    // The building lists, and the capacity properties of a new building.
    if (Virtual<void*>(holder, 0x24, 0xC8EBD131u)) {
        CallUpdateBuildingList(self, &At<char>(self, field::kWaterTreatmentList), occupant, inserted);
        if (inserted) {
            AddProperty(holder, 0x28ECF510);
            SetPropertySint32(holder, 0x28ECF510, 0);
            AddProperty(holder, 0x08FCDB69);
            SetPropertyUint32(holder, 0x08FCDB69, 0);
        }
    }
    if (Virtual<void*>(holder, 0x24, 0xA8EBCF0Cu)) {
        CallUpdateBuildingList(self, &At<char>(self, field::kIncineratorList), occupant, inserted);
        if (inserted) {
            AddProperty(holder, 0xE8ECF641);
            SetPropertySint32(holder, 0xE8ECF641, 0);
            AddProperty(holder, 0xA8FCDBA8);
            SetPropertyUint32(holder, 0xA8FCDBA8, 0);
        }
    }
    if (void* const found = Virtual<void*>(holder, 0x24, 0x48EBD0B1u)) {
        const int32_t capacity = *Virtual<int32_t*>(Virtual<void*>(found, 0x14), 0x124);
        CallUpdateBuildingList(self, &At<char>(self, field::kWasteToEnergyList), occupant, inserted);
        int32_t& total = At<int32_t>(self, field::kMonthlyWasteToEnergyCapacity);
        if (inserted) {
            AddProperty(holder, 0x88ECF76C);
            SetPropertySint32(holder, 0x88ECF76C, 0);
            AddProperty(holder, 0x28FCDBB0);
            SetPropertyUint32(holder, 0x28FCDBB0, 0);
            uint32_t carried;
            if (static_cast<uint8_t>(GetPropertyUint32(holder, 0x27812852, &carried))) {
                AddProperty(holder, 0x08FD1613);
                SetPropertyUint32(holder, 0x08FD1613, carried);
                Virtual<uint32_t>(holder, 0x34, 0x27812852u, 0u, 0u);
            }
            total += capacity;
        } else {
            total -= capacity;
        }
        if (total < 0)
            total = 0;
    }
    if (Virtual<void*>(holder, 0x24, 0x88ED1059u)) {
        CallUpdateBuildingList(self, &At<char>(self, field::kRecyclingList), occupant, inserted);
        At<uint32_t>(self, 0xFC) = ListCount(self, field::kRecyclingList);
    }

    // Maintenance line items of type 10 follow the building.
    void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
    BudgetItem item{};
    if (Virtual<uint8_t>(budget, 0xCC, holder, kPurposeMaintenance, &item) && item.lineItem == 10) {
        Virtual<uint32_t>(budget, inserted ? 0xDC : 0xE0, holder, item.index);
        SetPropertySint64(holder, 0x2A58D350, item.cost);
    }
}

// --- Monthly helpers and the building lists ---------------------------------

// The value pointer of a property (cIGZVariant slot 75), as the game reads
// uint32 properties in place.
uint32_t* ValuePointer(void* value)
{
    return Virtual<uint32_t*>(value, 0x12C);
}

// RollUsedCapacity (0x006E3C80): each building's used capacity becomes its
// last month's, and the used capacity starts again at 0.
void __fastcall RollUsedCapacity(void*, void*, void* list, uint32_t usedProperty, uint32_t lastMonthProperty)
{
    void* const sentinel = *static_cast<void**>(list);
    for (void* node = NextNode(sentinel); node != sentinel; node = NextNode(node)) {
        void* const holder = PropertyHolder(NodeObject(node));
        void* const used = Virtual<void*>(holder, 0x24, usedProperty);
        if (!used)
            continue;
        void* const usedValue = Virtual<void*>(used, 0x14);
        if (void* const last = Virtual<void*>(holder, 0x24, lastMonthProperty)) {
            void* const lastValue = Virtual<void*>(last, 0x14);
            Virtual<uint32_t>(lastValue, 0x68, *ValuePointer(usedValue)); // SetValUint32
        }
        Virtual<uint32_t>(usedValue, 0x74, 0); // SetValSint32
    }
}

// UpdateWasteToEnergyOutput (0x006E3F50): each plant's property 0x27812852
// = used * 0x08FD1613 / capacity, in 32-bit unsigned arithmetic as the game
// does (a capacity of 0 divides by zero there too).
void __fastcall UpdateWasteToEnergyOutput(void* self, void*)
{
    void* const sentinel = At<void*>(self, field::kWasteToEnergyList);
    for (void* node = NextNode(sentinel); node != sentinel; node = NextNode(node)) {
        void* const holder = PropertyHolder(NodeObject(node));
        uint32_t factor, capacity, used;
        if (static_cast<uint8_t>(GetPropertyUint32(holder, 0x08FD1613, &factor)) &&
            static_cast<uint8_t>(GetPropertyUint32(holder, 0x48EBD0B1, &capacity)) &&
            static_cast<uint8_t>(GetPropertyUint32(holder, 0x88ECF76C, &used)))
            Virtual<uint32_t>(holder, 0x34, 0x27812852u, used * factor / capacity, 0u);
    }
}

// The building lists' node allocator, fetched from the framework on first
// use as the game's list helpers (0x00413DD0, 0x00413F70) do.
void* ListNodeAllocator()
{
    using GetFrameworkFn = void*(__cdecl*)();
    using ReleaseRefFn = void(__thiscall*)(void** ref);
    void*& allocator = Global<void*>(addr::SC4List_NodeAllocator2);
    if (!allocator) {
        void* fetched = nullptr;
        if (void* framework = reinterpret_cast<GetFrameworkFn>(addr::RZGetFramework)())
            Virtual<uint8_t>(framework, 0x14, 0x3AE4BEA3u, 0x3AE4BEABu, &fetched);
        allocator = fetched;
        reinterpret_cast<ReleaseRefFn>(addr::SC4_ReleaseRef)(&fetched);
    }
    return allocator;
}

// UpdateBuildingList (0x006E45F0): puts the building at the front of the
// list, taking a reference, or removes its node and releases it. Returns
// whether it did. (Front, not back: the lists are walked newest first.)
uint8_t __fastcall UpdateBuildingList(void*, void*, void* list, void* occupant, uint8_t insert)
{
    static int calls = 0;
    if (calls++ < 3)
        log::Write("pollution buildings: building list %s", insert ? "insert" : "remove");
    void* const sentinel = *static_cast<void**>(list);
    if (insert) {
        Virtual<uint32_t>(occupant, 0x4); // AddRef
        auto** const node = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 12u));
        node[2] = occupant;
        void** const first = static_cast<void**>(NextNode(sentinel));
        void** const before = static_cast<void**>(first[1]);
        node[0] = first;
        node[1] = before;
        before[0] = node;
        first[1] = node;
        return 1;
    }
    void* node = NextNode(sentinel);
    while (node != sentinel && NodeObject(node) != occupant)
        node = NextNode(node);
    if (node == sentinel)
        return 0;
    void** const unlinked = static_cast<void**>(node);
    static_cast<void**>(unlinked[1])[0] = unlinked[0];
    static_cast<void**>(unlinked[0])[1] = unlinked[1];
    Virtual<uint32_t>(ListNodeAllocator(), 0x10, node);
    Virtual<uint32_t>(occupant, 0x8); // Release
    return 1;
}

// --- Installation -----------------------------------------------------------

using SpreadFn = void(__fastcall*)(void*, void*, void*, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
using ListFn = void(__fastcall*)(void*, void*, void*);
SpreadFn g_originalSpread = nullptr;
ListFn g_originalList = nullptr;

using RollFn = void(__fastcall*)(void*, void*, void*, uint32_t, uint32_t);
using OutputFn = void(__fastcall*)(void*, void*);
using ListUpdateFn = uint8_t(__fastcall*)(void*, void*, void*, void*, uint8_t);
RollFn g_originalRoll = nullptr;
OutputFn g_originalOutput = nullptr;
ListUpdateFn g_originalListUpdate = nullptr;

// UpdateBuildingList runs only when a garbage or water building is built or
// demolished, which the parity corpora do not do. With
// OPENSC4_POLLUTION_SELFCHECK set, the replacement checks itself against the
// game's function when it is installed: the same inserts and removals on two
// scratch lists of fake occupants (it only calls their AddRef and Release),
// then the same order, results and reference counts. Off by default: its
// allocations change the heap the game sees.
struct FakeOccupant {
    void* const* vtable;
    int32_t refs = 0;
};

uint32_t __fastcall FakeAddRef(FakeOccupant* self, void*) { return static_cast<uint32_t>(++self->refs); }
uint32_t __fastcall FakeRelease(FakeOccupant* self, void*) { return static_cast<uint32_t>(--self->refs); }
void* const kFakeVtable[3] = {nullptr, reinterpret_cast<void*>(&FakeAddRef), reinterpret_cast<void*>(&FakeRelease)};

bool CheckUpdateBuildingList()
{
    FakeOccupant fakes[2][3] = {{{kFakeVtable}, {kFakeVtable}, {kFakeVtable}}, {{kFakeVtable}, {kFakeVtable}, {kFakeVtable}}};
    FakeOccupant missing[2] = {{kFakeVtable}, {kFakeVtable}};
    void* lists[2];
    for (void*& list : lists) {
        list = Virtual<void*>(ListNodeAllocator(), 0xC, 12u);
        static_cast<void**>(list)[0] = list;
        static_cast<void**>(list)[1] = list;
    }
    auto run = [&](int side, int which, bool insert) {
        void* const occupant = which < 0 ? static_cast<void*>(&missing[side]) : static_cast<void*>(&fakes[side][which]);
        return side == 0 ? g_originalListUpdate(nullptr, nullptr, &lists[0], occupant, insert)
                         : UpdateBuildingList(nullptr, nullptr, &lists[1], occupant, insert);
    };
    const struct {
        int which;
        bool insert;
    } steps[] = {{0, true}, {1, true}, {2, true}, {1, false}, {-1, false}, {1, true}, {0, false}};
    bool same = true;
    for (const auto& step : steps)
        same &= run(0, step.which, step.insert) == run(1, step.which, step.insert);
    auto order = [&](int side) {
        int sequence = 0;
        for (void* node = NextNode(lists[side]); node != lists[side]; node = NextNode(node))
            sequence = sequence * 10 + 1 + static_cast<int>(static_cast<FakeOccupant*>(NodeObject(node)) - fakes[side]);
        return sequence;
    };
    same &= order(0) == order(1);
    for (int i = 0; i < 3; ++i)
        same &= fakes[0][i].refs == fakes[1][i].refs;
    log::Write("pollution helpers: UpdateBuildingList self-check against the game's: %s (order %d vs %d)",
               same ? "same" : "DIFFERENT", order(0), order(1));
    for (int side = 0; side < 2; ++side) {
        for (int i = 0; i < 3; ++i)
            while (run(side, i, false)) {
            }
        Virtual<uint32_t>(ListNodeAllocator(), 0x10, lists[side]);
    }
    return same;
}

bool InstallHelpers(registry::Mode)
{
    const addr::Entry* roll = addr_check::Find(addr::cSC4PollutionSimulator_RollUsedCapacity);
    const addr::Entry* output = addr_check::Find(addr::cSC4PollutionSimulator_UpdateWasteToEnergyOutput);
    const addr::Entry* list = addr_check::Find(addr::cSC4PollutionSimulator_UpdateBuildingList);
    if (!game::IsBuild641() || !roll || !output || !list)
        return false;
    return hook::Install(*roll, &RollUsedCapacity, &g_originalRoll) &&
           hook::Install(*output, &UpdateWasteToEnergyOutput, &g_originalOutput) &&
           hook::Install(*list, &UpdateBuildingList, &g_originalListUpdate) &&
           (GetEnvironmentVariableA("OPENSC4_POLLUTION_SELFCHECK", nullptr, 0) == 0 || CheckUpdateBuildingList());
}

const bool g_registeredHelpers = registry::Register({
    "simulators.pollution.helpers",
    "simulators/pollution",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PollutionSimulator_UpdateBuildingList,
    "Our building-list and monthly helpers: list insert/remove, used capacity roll-over, waste-to-energy output (docs/pollution.md)",
    false,
    InstallHelpers,
});

using OccupantFn = void(__fastcall*)(void*, void*, void*, uint8_t);
OccupantFn g_originalOccupant = nullptr;

bool InstallOccupants(registry::Mode)
{
    const addr::Entry* entry = addr_check::Find(addr::cSC4PollutionSimulator_OnOccupantInsertedOrRemoved);
    if (!game::IsBuild641() || !entry)
        return false;
    return hook::Install(*entry, &OnOccupantInsertedOrRemoved, &g_originalOccupant);
}

const bool g_registeredOccupants = registry::Register({
    "simulators.pollution.occupants",
    "simulators/pollution",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4PollutionSimulator_OnOccupantInsertedOrRemoved,
    "Our handler for buildings added and removed: pollution footprints, building lists (docs/pollution.md)",
    false,
    InstallOccupants,
});

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
