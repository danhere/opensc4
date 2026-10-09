// Our cSC4DemandSimulator (build 641), first part: its interface, swapped
// into the original object's vtable, and its save record (docs/demand.md).
// The demands live in a map at +0x50 (ID to cISC4Demand*); the getters read
// them through their own slots, as the game's do.
#include "core/demand_sim.h"

#include "core/game.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstdint>

namespace opensc4::demand_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("demand: first %s", name);
}

namespace {

constexpr uint32_t kVersion = 2;
constexpr uint32_t kDemandIid = 0x0797A4E8;        // cISC4Demand
constexpr uint32_t kExemplarInterface = 0xA52160F5;
constexpr uint32_t kJobsProperty = 0x47BB3F30;     // the demand exemplar's (ID, amount) pairs

using ReadReferenceFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void* object, uint32_t flags);
using WriteReferenceFn = uint8_t(__cdecl*)(void* stream, void* object);
using OccupantDemandFn = void(__thiscall*)(void* self, uint32_t occupant, uint8_t add, float amount);
using TreeEraseFn = void(__thiscall*)(void* map, TreeNode* root);

// The supply (slot 8) of the demand with this ID; inserted (null) when
// missing, as the game's operator[] does.
float Supply(void* self, uint32_t id)
{
    return Virtual<float>(DemandAt(self, id), 0x20);
}

// --- The getters -------------------------------------------------------------

// 5 (0x006A2390).
uint32_t __fastcall GetSimulatorType(void*, void*) { return 0x279681D7; }

// 6 (0x006A2960): the demand with the ID plus the index, or null; Init first
// if not yet initialised.
void* __fastcall GetDemand(void* self, void*, uint32_t id, uint32_t index)
{
    if (!At<uint8_t>(self, field::kInitialized))
        Virtual<uint8_t>(self, 0xC);
    TreeNode* const header = Header(self, field::kDemands);
    TreeNode* const node = Find(header, id + index);
    return node != header ? Value<void*>(node) : nullptr;
}

// 7 (0x006A6C90): when the two percentages differ, the occupant's demand
// effects taken off at the first and put back at the second (0x006A5920).
void __fastcall UpdateOccupantEffects(void* self, void*, uint32_t occupant, const float* before, const float* after)
{
    if (*after == *before)
        return;
    const auto apply = reinterpret_cast<OccupantDemandFn>(addr::cSC4DemandSimulator_OccupantDemand);
    apply(self, occupant, 0, *before);
    apply(self, occupant, 1, *after);
}

// 8 (0x006A23A0): the jobs a unit of the demand makes, by wealth, from the
// demand exemplar 6534284A-C7BB4816-(ID + index), property 0x47BB3F30: pairs
// (ID, amount in hundredths) for the IDs index + 0x2010, 0x2020, 0x2030.
// Written to out[1] to out[3], as the game's.
void __fastcall CalculateJobsPerUnitOfDemand(void*, void*, float* out, uint32_t id, uint32_t index)
{
    out[1] = 0.0f;
    out[2] = 0.0f;
    out[3] = 0.0f;
    const uint32_t key[3] = {0x6534284A, 0xC7BB4816, id + index};
    void* resource = nullptr;
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, kExemplarInterface, &resource, 0u, 0u)) {
        if (void* const holder = Virtual<void*>(resource, 0x1C)) {
            if (void* const property = Virtual<void*>(holder, 0x24, kJobsProperty)) {
                void* const data = Virtual<void*>(property, 0x14);
                const uint32_t* const values = Virtual<const uint32_t*>(data, 0x19C);
                const uint32_t count = Virtual<uint32_t>(data, 0x10);
                for (uint32_t i = 0; i < count; i += 2) {
                    const uint32_t which = values[i];
                    const double amount = Unsigned(values[i + 1]) * static_cast<double>(0.01f);
                    if (which == index + 0x2010)
                        out[1] = static_cast<float>(amount);
                    else if (which == index + 0x2020)
                        out[2] = static_cast<float>(amount);
                    else if (which == index + 0x2030)
                        out[3] = static_cast<float>(amount);
                }
            }
        }
    }
    if (resource)
        Virtual<uint32_t>(resource, 0x8);
}

// 9 (0x006A3E60): the jobs of a census group: the demand's supply times its
// jobs per unit summed over the wealth levels, truncated. 0 for the three
// population groups 0x1011, 0x1021, 0x1031.
int32_t __fastcall GetJobsBySensus(void* self, void*, uint32_t type)
{
    if (type == 0x1011 || type == 0x1021 || type == 0x1031)
        return 0;
    float jobs[4];
    Virtual<void>(self, 0x20, jobs, type, 0u);
    const float perUnit = static_cast<float>(static_cast<double>(jobs[3]) + jobs[2] + jobs[1]);
    return Ftol(static_cast<double>(Supply(self, type)) * perUnit);
}

// 10 (0x006A3DD0): the tax curve (+0xB0) at the supply of 0x1001, 0x3001
// and 0x4001 summed. The curve's result is returned unrounded (ST0), as the
// game's.
double __fastcall GetNeutralTaxRate(void* self, void*)
{
    const float residential = Supply(self, 0x1001);
    const float commercial = Supply(self, 0x3001);
    const double industrial = Supply(self, 0x4001);
    const float total = static_cast<float>(industrial + commercial + residential);
    return EvaluateCurve(At<void*>(self, field::kTaxCurve), total);
}

// 11 (0x006A5F80): the caller's map cleared, then for each demand of region
// use 1 its supply and of use 2 its demand, truncated, under its ID.
void __fastcall GetLocalPopulationSummary(void* self, void*, void* map)
{
    if (At<uint32_t>(map, 4)) {
        TreeNode* const header = At<TreeNode*>(map, 0);
        reinterpret_cast<TreeEraseFn>(addr::SC4Uint32IntMap_EraseTree)(map, header->parent);
        header->left = header;
        header->parent = nullptr;
        header->right = header;
        At<uint32_t>(map, 4) = 0;
    }
    TreeNode* const header = Header(self, field::kDemands);
    for (TreeNode* node = First(header); node != header; node = Next(node)) {
        void* const demand = Value<void*>(node);
        const uint32_t use = Virtual<uint32_t>(demand, 0x70);
        if (use != 1 && use != 2)
            continue;
        uint32_t* const slot = MapAt(map, node->key);
        *slot = static_cast<uint32_t>(Ftol(Virtual<float>(demand, use == 1 ? 0x20 : 0x24)));
    }
}

// --- The save record (version 2; cIGZSerializable at +8) ------------------------

void* Self(void* part) { return static_cast<char*>(part) - 8; }

// Slot 3 (0x006A2680): four floats (+0xA0 to +0xAC), the demands (ID and a
// reference each), the counted values (+0x28: ID and float each).
uint8_t __fastcall Write(void* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    if (Virtual<uint8_t>(stream, 0x48))
        return 0;
    void* const self = Self(part);
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(kVersion));
    for (std::size_t i = 0; i < 4; ++i)
        Virtual<uint8_t>(stream, 0x30, At<float>(self, field::kSaved + 4 * i));
    const auto WriteReference = reinterpret_cast<WriteReferenceFn>(addr::SC4Stream_WriteReference);
    Virtual<uint8_t>(stream, 0x24, Size(self, field::kDemands));
    TreeNode* const demands = Header(self, field::kDemands);
    for (TreeNode* node = First(demands); node != demands; node = Next(node)) {
        void* const demand = Value<void*>(node);
        Virtual<uint8_t>(stream, 0x24, node->key);
        WriteReference(stream, demand);
    }
    Virtual<uint8_t>(stream, 0x24, Size(self, field::kCounted));
    TreeNode* const counted = Header(self, field::kCounted);
    for (TreeNode* node = First(counted); node != counted; node = Next(node)) {
        const float value = Value<float>(node);
        Virtual<uint8_t>(stream, 0x24, node->key);
        Virtual<uint8_t>(stream, 0x30, value);
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// Slot 4 (0x006A3C50): versions 0 to 2 (before 2, four more words first,
// ignored). Sets +0xC (loaded).
uint8_t __fastcall Read(void* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    if (Virtual<uint8_t>(stream, 0x48))
        return 0;
    void* const self = Self(part);
    uint16_t version = 0;
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version > kVersion)
        return 0;
    At<uint8_t>(self, field::kLoaded) = 1;
    if (version < 2) {
        uint32_t ignored = 0;
        for (int i = 0; i < 4; ++i)
            Virtual<uint8_t>(stream, 0x24, &ignored);
    }
    for (std::size_t i = 0; i < 4; ++i)
        Virtual<uint8_t>(stream, 0x30, &At<float>(self, field::kSaved + 4 * i));
    const auto ReadReference = reinterpret_cast<ReadReferenceFn>(addr::SC4Stream_ReadReference);
    uint32_t count = 0;
    Virtual<uint8_t>(stream, 0x24, &count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t id = 0;
        Virtual<uint8_t>(stream, 0x24, &id);
        void* demand = nullptr;
        ReadReference(stream, kDemandIid, &demand, 0);
        *reinterpret_cast<void**>(MapAt(static_cast<char*>(self) + field::kDemands, id)) = demand;
    }
    Virtual<uint8_t>(stream, 0x24, &count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t id = 0;
        float value = 0.0f;
        Virtual<uint8_t>(stream, 0x24, &id);
        Virtual<uint8_t>(stream, 0x30, &value);
        *FloatMapAt(static_cast<char*>(self) + field::kCounted, id) = value;
    }
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// --- Installation -----------------------------------------------------------

struct Slot {
    int index;
    std::uintptr_t original; // the game's function, expected in the slot
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(5, 0x006A2390, GetSimulatorType),
    OPENSC4_SLOT(6, 0x006A2960, GetDemand),
    OPENSC4_SLOT(7, 0x006A6C90, UpdateOccupantEffects),
    OPENSC4_SLOT(8, 0x006A23A0, CalculateJobsPerUnitOfDemand),
    OPENSC4_SLOT(9, 0x006A3E60, GetJobsBySensus),
    OPENSC4_SLOT(10, 0x006A3DD0, GetNeutralTaxRate),
    OPENSC4_SLOT(11, 0x006A5F80, GetLocalPopulationSummary),
};

#undef OPENSC4_SLOT

constexpr int kVtableSlots = 14;

// Swaps a slot of a game vtable if it still holds the game's function.
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

// Swaps every slot or none: each must still hold the game's function.
bool InstallInterface(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4DemandSimulator_vftable);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("demand interface: slot %d holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    DWORD old = 0;
    if (!VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), PAGE_READWRITE, &old)) {
        log::Write("demand interface: VirtualProtect failed (%lu)", GetLastError());
        return false;
    }
    for (const Slot& slot : kSlots)
        vtable[slot.index] = reinterpret_cast<std::uintptr_t>(slot.ours);
    VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), old, &old);
    log::Write("demand interface: %u of %d cISC4DemandSimulator slots are ours",
               static_cast<unsigned>(sizeof(kSlots) / sizeof(kSlots[0])), kVtableSlots);
    return true;
}

bool InstallSave(registry::Mode)
{
    return game::IsBuild641() &&
           SwapSlot(addr::cSC4DemandSimulator_vftable_8, 3, 0x006A2680, reinterpret_cast<void*>(&Write), "save");
}

bool InstallLoad(registry::Mode)
{
    return game::IsBuild641() &&
           SwapSlot(addr::cSC4DemandSimulator_vftable_8, 4, 0x006A3C50, reinterpret_cast<void*>(&Read), "load");
}

const bool g_registeredInterface = registry::Register({
    "simulators.demand.interface",
    "simulators/demand",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4DemandSimulator_vftable,
    "Our cISC4DemandSimulator getters on the original object (docs/demand.md)",
    false,
    InstallInterface,
});

const bool g_registeredSave = registry::Register({
    "simulators.demand.save",
    "simulators/demand",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4DemandSimulator_vftable_8,
    "Our demand simulator's Write: the save record, version 2 (docs/demand.md)",
    false,
    InstallSave,
});

const bool g_registeredLoad = registry::Register({
    "simulators.demand.load",
    "simulators/demand",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4DemandSimulator_vftable_8,
    "Our demand simulator's Read: the save record, version 2 (docs/demand.md)",
    false,
    InstallLoad,
});

} // namespace
} // namespace opensc4::demand_sim
