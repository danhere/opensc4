// Our cSC4FloraSimulator (build 641), first part: its interface's queries
// and demolition calls, swapped into the original object's vtable
// (docs/flora.md). The flora types are global maps (0x00B49050 by type,
// 0x00B4905C by family) of 0x178-byte records from the flora exemplars.
#include "core/flora_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::flora_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("flora: first %s", name);
}

namespace {

constexpr uint32_t kFloraOccupant = 0x74758926;
constexpr uint32_t kFloraInterface = 0xC83DD057;

struct Vector {
    char* begin;
    char* end;
    char* capacity;
};

using PtrInsertFn = void(__thiscall*)(void* vector, void* where, const void* value, void* allocator, uint32_t count,
                                      uint32_t atEnd);
using ResizeFn = void(__thiscall*)(void* set, uint32_t count);
using InsertFn = void*(__thiscall*)(void* set, void* result, const uint32_t* key);
using ListFn = void(__thiscall*)(void* list);
using AllocateNodesFn = void**(__thiscall*)(void* list, uint32_t count, uint32_t zero);
using SlopeFn = int32_t(__thiscall*)(void* self, float x, float z, uint8_t uphill);

const auto PtrInsert = reinterpret_cast<PtrInsertFn>(addr::SC4PtrVector_InsertN);

void PushWord(Vector* vector, uint32_t value)
{
    if (vector->end != vector->capacity) {
        if (vector->end)
            *reinterpret_cast<uint32_t*>(vector->end) = value;
        vector->end += 4;
        return;
    }
    char allocator;
    PtrInsert(vector, vector->end, &value, &allocator, 1, 1);
}

void AddToSet(void* set, const uint32_t* key)
{
    reinterpret_cast<ResizeFn>(addr::SC4Uint32HashSet_Resize)(set, At<uint32_t>(set, 0x10) + 1);
    char result[8];
    reinterpret_cast<InsertFn>(addr::SC4Uint32HashSet_Insert)(set, result, key);
}

// A list of the game's node allocator (0x00B207E4), empty.
void* NewList()
{
    void** const sentinel = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    return sentinel;
}

// --- Queries ------------------------------------------------------------------

// 7 (0x006B1040).
char* __fastcall GetStaticFloraData(void*, void*, uint32_t type)
{
    return FloraData(type);
}

// 8 (0x006B0E90): every type, in load order.
void __fastcall GetAllFloraTypesVector(void*, void*, Vector* types)
{
    types->end = types->begin;
    const auto& all = Global<Vector>(addr::cSC4FloraSimulator_TypeList);
    for (const char* it = all.begin; it != all.end; it += 4)
        PushWord(types, *reinterpret_cast<const uint32_t*>(it));
}

// 9 (0x006B0420): into a buffer of up to *count, or the count alone.
uint32_t __fastcall GetAllFloraTypes(void*, void*, uint32_t* buffer, uint32_t* count)
{
    const auto& all = Global<Vector>(addr::cSC4FloraSimulator_TypeList);
    const uint32_t total = static_cast<uint32_t>((all.end - all.begin) >> 2);
    if (!buffer) {
        *count = total;
        return *count;
    }
    if (*count > total)
        *count = total;
    for (uint32_t i = 0; i < *count; ++i)
        buffer[i] = reinterpret_cast<const uint32_t*>(all.begin)[i];
    return *count;
}

// 10 (0x006B0F10): every family, in order.
void __fastcall GetAllFloraFamilyTypes(void*, void*, Vector* families)
{
    families->end = families->begin;
    MapNode* const head = FamilyMapHead();
    for (MapNode* node = head->left; node != head; node = Next(node))
        PushWord(families, node->key);
}

// 11 (0x006B0FC0).
uint32_t __fastcall IsFloraTypeValid(void*, void*, uint32_t type)
{
    return Find(addr::cSC4FloraSimulator_TypeMap, type) != TypeMapHead() ? 1u : 0u;
}

// 12 (0x006B0FF0): a family, or else a type.
uint32_t __fastcall IsFloraFamilyTypeValid(void*, void*, uint32_t type)
{
    if (Find(addr::cSC4FloraSimulator_FamilyMap, type) != FamilyMapHead())
        return 1;
    return Find(addr::cSC4FloraSimulator_TypeMap, type) != TypeMapHead() ? 1u : 0u;
}

// 13 (0x006B23A0): the types the filter (slot 3) accepts, into the set.
uint32_t __fastcall GetFloraTypesByFilter(void*, void*, void* set, void* filter)
{
    MapNode* const head = TypeMapHead();
    uint32_t count = 0;
    for (MapNode* node = head->left; node != head; node = Next(node)) {
        uint32_t key = node->key;
        if (filter && !Virtual<uint8_t>(filter, 0xC, key))
            continue;
        AddToSet(set, &key);
        ++count;
    }
    return count;
}

// 14 (0x006B2460): a family's types, into the set.
uint32_t __fastcall GetFloraTypesByFamily(void*, void*, void* set, uint32_t family)
{
    MapNode* const node = Find(addr::cSC4FloraSimulator_FamilyMap, family);
    if (node == FamilyMapHead())
        return 0;
    const auto* const types = reinterpret_cast<const Vector*>(&node->value);
    const int32_t count = static_cast<int32_t>((types->end - types->begin) >> 2);
    for (int32_t i = 0; i < count; ++i) {
        const char* const begin = reinterpret_cast<const Vector*>(&node->value)->begin;
        AddToSet(set, reinterpret_cast<const uint32_t*>(begin) + i);
    }
    return static_cast<uint32_t>(count);
}

// 0x006B02E0: the slope class at a position (0 to 15): the terrain's
// height there against sea level (terrain slots 29 and 13), up or down,
// times +0xA0, floored.
int32_t __fastcall SlopeClass(void* self, void*, float x, float z, uint8_t uphill)
{
    static bool logged = false;
    FirstCall("SlopeClass", logged);
    void* const terrain = At<void*>(self, field::kTerrain);
    const float sea = static_cast<float>(Virtual<double>(terrain, 0x74));
    const double height = Virtual<double>(terrain, 0x34, x, z);
    const double difference = uphill ? static_cast<double>(sea) - height : height - static_cast<double>(sea);
    const double value = difference * At<float>(self, field::kSlopeScale);
    const float rounded = static_cast<float>(value - Global<float>(addr::SC4_FloatHalf) + Global<float>(addr::SC4_FloatRoundMagic));
    uint32_t bits;
    std::memcpy(&bits, &rounded, sizeof bits);
    int32_t cell = static_cast<int32_t>(bits + 0xB4C00000u);
    if (static_cast<double>(cell + 1) == value)
        cell += 1;
    if (cell > 15)
        cell = 15;
    if (cell <= 0)
        cell = 0;
    return cell;
}

// 16 (0x006B07B0): the type with the best suitability (its table at +0x50,
// row by slope class, up or down by its +0xC, column by the cell service's
// slot 19), over 255; not types flagged +0x16C bit 0.
uint32_t __fastcall GetBestFloraTypeForLocation(void* self, void*, float x, float z, float* best)
{
    static bool logged = false;
    FirstCall("GetBestFloraTypeForLocation", logged);
    const int32_t column = Virtual<int32_t>(At<void*>(self, field::kCellService), 0x4C, x, z);
    const auto Slope = reinterpret_cast<SlopeFn>(addr::cSC4FloraSimulator_SlopeClass);
    const int32_t down = Slope(self, x, z, 0);
    const int32_t up = Slope(self, x, z, 1);
    float bestValue = 0.0f;
    uint32_t bestType = 0;
    MapNode* const head = TypeMapHead();
    for (MapNode* node = head->left; node != head; node = Next(node)) {
        const char* const data = node->value;
        if (data[0x16C] & 1)
            continue;
        const int32_t row = data[0xC] ? up : down;
        const double value = static_cast<double>(static_cast<uint8_t>(data[(row + 5) * 16 + column])) *
                             Global<float>(addr::SC4_FloatInv255);
        if (value > bestValue) {
            bestValue = static_cast<float>(value);
            bestType = node->key;
        }
    }
    *best = bestValue;
    return bestType;
}

// --- Occupants ------------------------------------------------------------------

// 18 (0x006AFC40): into the occupant manager (slot 20).
uint32_t __fastcall AddNewFloraOccupant(void* self, void*, void* occupant)
{
    void* const occupants = At<void*>(self, field::kOccupants);
    if (!occupants)
        return 0;
    return Virtual<uint8_t>(occupants, 0x50, occupant, 0u);
}

// 19 (0x006AFC60): out of the occupant manager (slot 21, 1, 0).
uint32_t __fastcall DemolishFloraOccupant(void* self, void*, void* occupant, int32_t)
{
    return Virtual<uint8_t>(At<void*>(self, field::kOccupants), 0x54, occupant, 1u, 0u);
}

// The flora occupants a query of the occupant manager returns (slot 41 for
// a cell rectangle, slot 37 for a box), each demolished (slot 19).
uint8_t DemolishAll(void* self, std::size_t slot, const void* first, const void* second)
{
    uint8_t ok = 1;
    void* head = NewList();
    if (Virtual<uint8_t>(At<void*>(self, field::kOccupants), slot, &head, first, second, kFloraOccupant, -1)) {
        void** const sentinel = static_cast<void**>(head);
        for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) {
            void* const occupant = node[2];
            if (!Virtual<uint8_t>(self, 0x4C, occupant, 0))
                ok = 0;
            Virtual<uint32_t>(occupant, 0x8);
        }
        reinterpret_cast<ListFn>(addr::SC4List_Clear2)(&head);
    }
    reinterpret_cast<ListFn>(addr::SC4List_Destroy2)(&head);
    return ok;
}

// 20 (0x006B24F0).
uint8_t __fastcall DemolishFloraInCellArea(void* self, void*, const int32_t* rect, int32_t)
{
    static bool logged = false;
    FirstCall("DemolishFloraInCellArea", logged);
    return DemolishAll(self, 0xA4, rect, rect + 2);
}

// 21 (0x006B2620).
uint8_t __fastcall DemolishFloraInBoundingBox(void* self, void*, const float* low, const float* high, int32_t)
{
    static bool logged = false;
    FirstCall("DemolishFloraInBoundingBox", logged);
    return DemolishAll(self, 0x94, low, high);
}

// 22 (0x006AFC80, shared): nothing.
int64_t __fastcall GetCostOfOccupantDemolition(void*, void*, void*, int32_t)
{
    return 0;
}

// 23 (0x006B1790): a flora occupant (interface 0xC83DD057) that slot 32
// allows is set dying (state 3, model 0x29CFF3CB, message 0x99EF1145) and
// joins the dying list (+0xA4), referenced.
uint8_t __fastcall KillFloraOccupant(void* self, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("KillFloraOccupant", logged);
    void* flora = nullptr;
    if (!Virtual<uint8_t>(occupant, 0x0, kFloraInterface, &flora) || !Virtual<uint8_t>(occupant, 0x80, 1u)) {
        if (flora)
            Virtual<uint32_t>(flora, 0x8);
        return 0;
    }
    if (Virtual<int32_t>(flora, 0x28) != 3) {
        Virtual<uint8_t>(flora, 0x2C, 3);
        Virtual<uint8_t>(flora, 0x14, 0x29CFF3CBu);
        void* const data = Virtual<void*>(self, 0x1C, 0x29CFF3CBu);
        Virtual<uint8_t>(flora, 0x38, data);
        Virtual<uint8_t>(occupant, 0x3C, 0x99EF1145u, 0u);
        Virtual<uint32_t>(occupant, 0x4);
        void** const list = At<void**>(self, field::kDying);
        void** const node = reinterpret_cast<AllocateNodesFn>(addr::SC4List_AllocateNodes)(
            static_cast<char*>(self) + field::kDying, 1, 0);
        if (node + 2)
            node[2] = occupant;
        void** const last = static_cast<void**>(list[1]);
        node[0] = list;
        node[1] = last;
        last[0] = node;
        list[1] = node;
    }
    if (flora)
        Virtual<uint32_t>(flora, 0x8);
    return 1;
}

// --- Installation -----------------------------------------------------------

struct Slot {
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(7, 0x006B1040, GetStaticFloraData),
    OPENSC4_SLOT(8, 0x006B0E90, GetAllFloraTypesVector),
    OPENSC4_SLOT(9, 0x006B0420, GetAllFloraTypes),
    OPENSC4_SLOT(10, 0x006B0F10, GetAllFloraFamilyTypes),
    OPENSC4_SLOT(11, 0x006B0FC0, IsFloraTypeValid),
    OPENSC4_SLOT(12, 0x006B0FF0, IsFloraFamilyTypeValid),
    OPENSC4_SLOT(13, 0x006B23A0, GetFloraTypesByFilter),
    OPENSC4_SLOT(14, 0x006B2460, GetFloraTypesByFamily),
    OPENSC4_SLOT(16, 0x006B07B0, GetBestFloraTypeForLocation),
    OPENSC4_SLOT(18, 0x006AFC40, AddNewFloraOccupant),
    OPENSC4_SLOT(19, 0x006AFC60, DemolishFloraOccupant),
    OPENSC4_SLOT(20, 0x006B24F0, DemolishFloraInCellArea),
    OPENSC4_SLOT(21, 0x006B2620, DemolishFloraInBoundingBox),
    OPENSC4_SLOT(22, 0x006AFC80, GetCostOfOccupantDemolition),
    OPENSC4_SLOT(23, 0x006B1790, KillFloraOccupant),
};

#undef OPENSC4_SLOT

constexpr int kVtableSlots = 26;

using SlopeOursFn = int32_t(__fastcall*)(void*, void*, float, float, uint8_t);
SlopeOursFn g_originalSlope = nullptr;

bool Install(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* slope = addr_check::Find(addr::cSC4FloraSimulator_SlopeClass);
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4FloraSimulator_vftable);
    for (const Slot& slot : kSlots) {
        if (vtable[slot.index] != slot.original) {
            log::Write("flora interface: slot %d holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(vtable[slot.index]), static_cast<unsigned>(slot.original));
            return false;
        }
    }
    if (!slope || !hook::Install(*slope, &SlopeClass, &g_originalSlope))
        return false;
    DWORD old = 0;
    if (!VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), PAGE_READWRITE, &old))
        return false;
    for (const Slot& slot : kSlots)
        vtable[slot.index] = reinterpret_cast<std::uintptr_t>(slot.ours);
    VirtualProtect(vtable, kVtableSlots * sizeof(*vtable), old, &old);
    log::Write("flora interface: %u of %d cISC4FloraSimulator slots are ours",
               static_cast<unsigned>(sizeof(kSlots) / sizeof(kSlots[0])), kVtableSlots);
    return true;
}

const bool g_registered = registry::Register({
    "simulators.flora.interface",
    "simulators/flora",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4FloraSimulator_vftable,
    "Our cISC4FloraSimulator queries and demolition on the original object (docs/flora.md)",
    false,
    Install,
});

} // namespace
} // namespace opensc4::flora_sim
