// Our cSC4DemandSimulator (build 641), fifth part: the demands and the
// tuning from the game's exemplars, Init and Shutdown (docs/demand.md,
// "Tuning"). Nothing here is fixed in code: every demand, its range and its
// effects on other demands come from the demand exemplars, so mods that
// change them (the Colossus Addon Mod among them) keep working.
#include "core/demand_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstdint>

namespace opensc4::demand_sim {
namespace {

constexpr uint32_t kExemplarType = 0x6534284A;  // the resource type of exemplars
constexpr uint32_t kExemplarInterface = 0xA52160F5;
constexpr uint32_t kDemandClass = 0x8990C12D;   // cSC4Demand
constexpr uint32_t kDemandIid = 0x0797A4E8;     // cISC4Demand

constexpr std::size_t kCellIterator = 0x18;
constexpr std::size_t kNeutralCurve = 0xB0;
constexpr std::size_t kTaxEffectCurve = 0xB4;
constexpr std::size_t kHistoryDays = 0xB8;
constexpr std::size_t kLogWeights = 0xBC; // vector<float> of 13

// The global counter that numbers the effects as they are read (0x00B49020).
constexpr std::uintptr_t kEffectCounter = 0x00B49020;

using GetUint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t id, uint32_t* out);
using GetSint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t id, int32_t* out);
using GetUint8Fn = uint8_t(__cdecl*)(void* holder, uint32_t id, uint8_t* out);
using GetFloatFn = uint8_t(__cdecl*)(void* holder, uint32_t id, float* out);
using GetVectorFn = uint8_t(__cdecl*)(void* holder, uint32_t id, void* vector);
using InsertEffectFn = void(__thiscall*)(void* map, void** result, TreeNode* x, TreeNode* y, const uint32_t* value,
                                         uint32_t flag);
using CurveFn = void(__thiscall*)(void* curve, void* property);
using CurveInitFn = void*(__thiscall*)(void* curve);
using OperatorNewFn2 = void*(__cdecl*)(std::size_t size);
using OperatorDeleteFn = void(__cdecl*)(void* p);
using FreeFn = void(__cdecl*)(void* p);
using EraseFn = void(__thiscall*)(void* tree, TreeNode* root);
using ListAtFn = void**(__thiscall*)(void* map, const uint32_t* key);
using ListAssignFn = void*(__thiscall*)(void* destination, const void* source);
using CellIteratorFn = void*(__thiscall*)(void* memory);

const auto GetUint32 = reinterpret_cast<GetUint32Fn>(addr::SC4_GetPropertyUint32);
const auto GetSint32 = reinterpret_cast<GetSint32Fn>(addr::SC4_GetPropertySint32);
const auto GetFloat = reinterpret_cast<GetFloatFn>(addr::SC4_GetPropertyFloat);

// A property's values as an array (property slot 5: the variant; its slot
// 103 the uint32 data, 111 the float data, 4 the count).
template <typename T>
const T* Values(void* holder, uint32_t id, std::size_t dataSlot, uint32_t* count)
{
    void* const property = Virtual<void*>(holder, 0x24, id);
    if (!property)
        return nullptr;
    void* const data = Virtual<void*>(property, 0x14);
    const T* const values = Virtual<const T*>(data, dataSlot);
    *count = Virtual<uint32_t>(data, 0x10);
    return values;
}

// One effect into the multimap at +0x5C (insert_equal after equal keys):
// the source ID, a sequence number, the source again, the target, the kind
// and the factor.
void AddEffect(void* self, uint32_t source, uint32_t sequence, uint32_t target, uint32_t kind, float factor)
{
    uint32_t value[6] = {source, sequence, source, target, kind, 0};
    static_assert(sizeof(float) == sizeof(uint32_t));
    *reinterpret_cast<float*>(&value[5]) = factor;
    void* const map = static_cast<char*>(self) + field::kEffects;
    TreeNode* const header = *static_cast<TreeNode**>(map);
    TreeNode* parent = header;
    TreeNode* node = header->parent;
    while (node) {
        parent = node;
        node = source < node->key ? node->left : node->right;
    }
    void* result = nullptr;
    reinterpret_cast<InsertEffectFn>(addr::SC4EffectMap_Insert)(map, &result, node, parent, value, 0);
}

// --- The demands ------------------------------------------------------------------

// 0x006A5210: a new cSC4Demand through the framework (class 0x8990C12D),
// Init, its ID; queried for `iid` into `out`; it replaces any demand with
// the ID in the map (that one shut down and released).
uint8_t __fastcall CreateDemand(void* self, void*, uint32_t id, uint32_t iid, void** out)
{
    void* demand = nullptr;
    void* const framework = reinterpret_cast<void*(__cdecl*)()>(addr::RZGetCOM)();
    if (!Virtual<uint8_t>(framework, 0xC, kDemandClass, kDemandIid, &demand))
        return 0;
    Virtual<uint8_t>(demand, 0xC);
    Virtual<uint8_t>(demand, 0x1C, id);
    if (!Virtual<uint8_t>(demand, 0x0, iid, out)) {
        Virtual<uint8_t>(demand, 0x10);
        Virtual<uint32_t>(demand, 0x8);
        return 0;
    }
    TreeNode* const demands = Header(self, field::kDemands);
    TreeNode* const found = Find(demands, id);
    if (found != demands) {
        void* const old = Value<void*>(found);
        Virtual<uint8_t>(old, 0x10);
        Virtual<uint32_t>(old, 0x8);
    }
    *reinterpret_cast<void**>(MapAt(static_cast<char*>(self) + field::kDemands, id)) = demand;
    return 1;
}

// Slot 13 (0x006A52D0): a demand object added under its ID, replacing (shut
// down, released) any other with that ID; referenced.
uint8_t __fastcall AddDemand(void* self, void*, void* demand)
{
    if (!demand)
        return 1;
    const uint32_t id = Virtual<uint32_t>(demand, 0x18);
    TreeNode* const demands = Header(self, field::kDemands);
    TreeNode* const found = Find(demands, id);
    if (found != demands) {
        void* const old = Value<void*>(found);
        Virtual<uint8_t>(old, 0x10);
        Virtual<uint32_t>(old, 0x8);
        Value<void*>(found) = demand;
        Virtual<uint32_t>(demand, 0x4);
        return 1;
    }
    *reinterpret_cast<void**>(MapAt(static_cast<char*>(self) + field::kDemands, id)) = demand;
    Virtual<uint32_t>(demand, 0x4);
    return 1;
}

// The effects of one property: pairs (target, factor in hundredths) or, for
// the cap and sum kinds, targets with factor 1.
void ReadEffects(void* self, void* holder, uint32_t source, uint32_t kind, uint32_t base, bool pairs)
{
    uint32_t count = 0;
    const uint32_t* const values = Values<uint32_t>(holder, kind, 0x19C, &count);
    if (!values)
        return;
    uint32_t& counter = Global<uint32_t>(kEffectCounter);
    if (pairs) {
        for (uint32_t i = 0; i < count; i += 2) {
            const float factor = static_cast<float>(Unsigned(values[i + 1]) * static_cast<double>(0.01f));
            AddEffect(self, source, counter + base, values[i], kind, factor);
            ++counter;
        }
    } else {
        for (uint32_t i = 0; i < count; ++i)
            AddEffect(self, source, counter + base, values[i], kind, 1.0f);
    }
}

// 0x006A5350: one demand exemplar (type 3): the demand with its ID
// (property 0x21, else the instance), created if new (class from 0x11);
// unless loaded from a save, its starting supply and demand (0x47BB3F20,
// 0x47BB3F21); its range (0x47BB3F10 minimum, 0x47BB3F11 maximum); its
// effects on other demands (0x47BB3F31, 30, 32 with factors, 41 and 50
// without); its region use (0x47BB3F60).
void __fastcall ConfigureDemand(void* self, void*, uint32_t instance, void* holder)
{
    uint32_t iid = 0;
    if (!GetUint32(holder, 0x11, &iid))
        iid = kDemandIid;
    uint32_t id = instance;
    GetUint32(holder, 0x21, &id);
    void* demand = nullptr;
    TreeNode* const demands = Header(self, field::kDemands);
    TreeNode* const found = Find(demands, id);
    bool loaded = At<uint8_t>(self, field::kLoaded) != 0;
    if (found != demands) {
        demand = Value<void*>(found);
        Virtual<uint8_t>(demand, 0xC);
    } else {
        CallThis<uint8_t>(addr::cSC4DemandSimulator_CreateDemand, self, id, iid, &demand);
        Virtual<uint32_t>(demand, 0x8);
        loaded = false;
    }
    if (!demand)
        return;
    int32_t value = 0;
    if (!loaded) {
        if (GetSint32(holder, 0x47BB3F20, &value))
            Virtual<uint8_t>(demand, 0x48, static_cast<float>(value));
        if (GetSint32(holder, 0x47BB3F21, &value))
            Virtual<uint8_t>(demand, 0x4C, static_cast<float>(value));
    }
    if (GetSint32(holder, 0x47BB3F10, &value))
        Virtual<void>(demand, 0x54, static_cast<float>(value));
    if (GetSint32(holder, 0x47BB3F11, &value))
        Virtual<void>(demand, 0x50, static_cast<float>(value));
    ReadEffects(self, holder, id, 0x47BB3F31, 10000, true);
    ReadEffects(self, holder, id, 0x47BB3F30, 20000, true);
    ReadEffects(self, holder, id, 0x47BB3F32, 30000, true);
    ReadEffects(self, holder, id, 0x47BB3F41, 40000, false);
    ReadEffects(self, holder, id, 0x47BB3F50, 50000, false);
    uint8_t use = 0;
    reinterpret_cast<GetUint8Fn>(addr::SC4_GetPropertyUint8)(holder, 0x47BB3F60, &use);
    Virtual<uint32_t>(demand, 0x74, static_cast<uint32_t>(use));
}

// 0x006A7840: the callback Init hands the resource manager for every
// exemplar: the demand exemplars (exemplar type 3, property 0x10) to
// ConfigureDemand.
void __cdecl ForEachExemplar(const uint32_t* key, void* self)
{
    void* resource = nullptr;
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, kExemplarInterface, &resource, 0u, 0u)) {
        void* const holder = Virtual<void*>(resource, 0x1C);
        uint32_t type = 0;
        if (GetUint32(holder, 0x10, &type) && type == 3)
            CallThis<void>(addr::cSC4DemandSimulator_ConfigureDemand, self, key[2], holder);
    }
    if (resource)
        Virtual<uint32_t>(resource, 0x8);
}

// --- The tuning ---------------------------------------------------------------------

// 0x006A8420: a type of occupant's demand effects from its exemplar: pairs
// (demand, amount) in 0x27812841 (demand factors; 0x27812843 floats replace
// the amounts) and 0x27812840 (supply factors; 0x27812842), for demands
// that exist, into the global map at 0x00B49008.
void __fastcall OccupantTuning(void* self, void*, uint32_t type, const uint32_t* key)
{
    void* resource = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, kExemplarInterface, &resource, 0u, 0u))
        return;
    auto** const list = static_cast<void**>(reinterpret_cast<OperatorNewFn2>(addr::SC4_stl_allocate)(0x14));
    list[0] = list;
    list[1] = list;
    auto append = [list](uint32_t id, float supply, float demandFactor) {
        auto** const node = static_cast<void**>(reinterpret_cast<OperatorNewFn2>(addr::SC4_stl_allocate)(0x14));
        auto* const values = reinterpret_cast<uint32_t*>(node + 2);
        values[0] = id;
        std::memcpy(&values[1], &supply, 4);
        std::memcpy(&values[2], &demandFactor, 4);
        void** const last = static_cast<void**>(list[1]);
        node[0] = list;
        node[1] = last;
        last[0] = node;
        list[1] = node;
    };
    TreeNode* const demands = Header(self, field::kDemands);
    for (int pass = 0; pass < 2; ++pass) {
        const uint32_t pairsId = pass == 0 ? 0x27812841 : 0x27812840;
        const uint32_t floatsId = pass == 0 ? 0x27812843 : 0x27812842;
        void* const holder = Virtual<void*>(resource, 0x1C);
        uint32_t count = 0;
        const uint32_t* const pairs = Values<uint32_t>(holder, pairsId, 0x19C, &count);
        if (!pairs)
            continue;
        uint32_t floatCount = 0;
        const float* const floats = Values<float>(Virtual<void*>(resource, 0x1C), floatsId, 0x1BC, &floatCount);
        for (uint32_t i = 0; i < count; i += 2) {
            const uint32_t id = pairs[i];
            float amount = static_cast<float>(Unsigned(pairs[i + 1]));
            if (floats)
                amount = floats[(i + 1) / 2];
            if (Find(demands, id) == demands)
                continue;
            if (pass == 0)
                append(id, 0.0f, amount);
            else
                append(id, amount, 0.0f);
        }
    }
    if (list[0] != list) {
        void** const slot = reinterpret_cast<ListAtFn>(addr::cSC4DemandSimulator_OccupantEffectsAt)(
            reinterpret_cast<void*>(addr::cSC4DemandSimulator_OccupantEffects), &type);
        reinterpret_cast<ListAssignFn>(addr::SC4DemandEffectList_Assign)(slot, &list);
    }
    Virtual<uint32_t>(resource, 0x8);
    for (void** node = static_cast<void**>(list[0]); node != list;) {
        void** const next = static_cast<void**>(node[0]);
        reinterpret_cast<FreeFn>(addr::SC4_Free)(node);
        node = next;
    }
    list[0] = list;
    list[1] = list;
    reinterpret_cast<FreeFn>(addr::SC4_Free)(list);
}

// A curve from a property, made when missing; freed when the property is
// missing.
void LoadCurve(void* self, void* holder, uint32_t id, std::size_t offset, std::size_t clearOffset)
{
    void* const property = Virtual<void*>(holder, 0x24, id);
    if (property) {
        if (!At<void*>(self, offset)) {
            void* const memory = reinterpret_cast<OperatorNewFn2>(addr::SC4_operator_new)(0x10);
            At<void*>(self, offset) =
                memory ? reinterpret_cast<CurveInitFn>(addr::SC4Curve_Init)(memory) : nullptr;
        }
        reinterpret_cast<CurveFn>(addr::SC4Curve_LoadFromProperty)(At<void*>(self, offset), property);
        return;
    }
    if (void** const curve = At<void**>(self, offset)) {
        if (curve[0])
            reinterpret_cast<FreeFn>(addr::SC4_Free)(curve[0]);
        reinterpret_cast<OperatorDeleteFn>(addr::SC4_operator_delete)(curve);
        At<void*>(self, clearOffset) = nullptr;
    }
}

struct FloatVector {
    float* begin;
    float* end;
    float* capacity;
};

// 0x006A8790 (Init, message 0xCA1B151C): every occupant type's demand
// effects (the service at +0x3C, slots 12 and 14), then the tuning exemplar
// 6534284A-E7E2C2DB-4A1F3CE6.
void __fastcall LoadTuning(void* self, void*)
{
    static bool logged = false;
    FirstCall("LoadTuning", logged);
    void* const types = Global<void*>(addr::SC4_Service43D68);
    uint32_t count = 0;
    const uint32_t size = Virtual<uint32_t>(types, 0x30, static_cast<uint32_t*>(nullptr), &count);
    auto* const ids = static_cast<uint32_t*>(reinterpret_cast<OperatorNewFn2>(addr::SC4_operator_new)(size * 4));
    Virtual<uint32_t>(types, 0x30, ids, &count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key[3] = {};
        if (Virtual<uint8_t>(types, 0x38, ids[i], key))
            CallThis<void>(addr::cSC4DemandSimulator_OccupantTuning, self, ids[i], static_cast<const uint32_t*>(key));
    }
    reinterpret_cast<OperatorDeleteFn>(addr::SC4_operator_delete)(ids);
    const uint32_t key[3] = {kExemplarType, 0xE7E2C2DB, 0x4A1F3CE6};
    void* resource = nullptr;
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, kExemplarInterface, &resource, 0u, 0u)) {
        void* const holder = Virtual<void*>(resource, 0x1C);
        LoadCurve(self, holder, 0x4A1F38B5, kNeutralCurve, kNeutralCurve);
        LoadCurve(self, holder, 0x4A1F38B6, kTaxEffectCurve, kTaxEffectCurve);
        At<uint32_t>(self, kHistoryDays) = 0x5A;
        GetUint32(holder, 0x4A1F38B7, &At<uint32_t>(self, kHistoryDays));
        FloatVector weights = {};
        float* const target = At<float*>(self, kLogWeights);
        if (reinterpret_cast<GetVectorFn>(addr::SC4_GetPropertyFloatVector)(holder, 0x4A1F38C5, &weights)) {
            for (int i = 0; i < 13; ++i)
                target[i] = weights.begin[i];
        } else {
            for (int i = 0; i < 13; ++i)
                target[i] = 0.0f;
        }
        const struct {
            std::size_t offset;
            uint32_t id;
            uint32_t bits;
        } floats[] = {{0xC8, 0x4A1F38C6, 0x3F800000}, {0xCC, 0x4A1F38C7, 0x40000000}, {0xD0, 0x4A1F38C8, 0x41A00000},
                      {0xD4, 0x4A1F38C9, 0x3DCCCCCD}, {0xD8, 0x4A1F38CA, 0x41A00000}};
        for (const auto& entry : floats) {
            At<uint32_t>(self, entry.offset) = entry.bits;
            GetFloat(holder, entry.id, &At<float>(self, entry.offset));
        }
        struct {
            uint32_t* begin;
            uint32_t* end;
            uint32_t* capacity;
        } weightsByWealth = {};
        if (reinterpret_cast<GetVectorFn>(addr::SC4_GetPropertyUint32Vector)(holder, 0x8A63F2A1, &weightsByWealth)) {
            for (int i = 0; i < 12; ++i)
                At<uint32_t>(self, field::kWeights + 4 * i) = weightsByWealth.begin[i];
        } else {
            for (int i = 0; i < 12; ++i)
                At<uint32_t>(self, field::kWeights + 4 * i) = 100;
        }
        if (weightsByWealth.begin)
            reinterpret_cast<FreeFn>(addr::SC4_Free)(weightsByWealth.begin);
        if (weights.begin)
            reinterpret_cast<FreeFn>(addr::SC4_Free)(weights.begin);
    }
    if (resource)
        Virtual<uint32_t>(resource, 0x8);
}

// --- Init and Shutdown -----------------------------------------------------------

void Reference(void* self, std::size_t offset, std::uintptr_t global)
{
    if (At<void*>(self, offset))
        return;
    void* const service = Global<void*>(global);
    At<void*>(self, offset) = service;
    Virtual<uint32_t>(service, 0x4);
}

void Release(void* self, std::size_t offset)
{
    if (void* const object = At<void*>(self, offset)) {
        Virtual<uint32_t>(object, 0x8);
        At<void*>(self, offset) = nullptr;
    }
}

constexpr uint32_t kMessages[] = {0xE8BDA2E7, 0x8A0D15DA, 0xEA0D163E, 0x26C63343, 0xCA1B151C};
constexpr uint32_t kActiveFigures[] = {0x2A67A399, 0xAA67A3A0, 0x8A67A3A4, 0x0A67A3A9, 0xAA67A3AD, 0xCA67A3B2,
                                       0xAA67A3B7, 0x2A67A3BC, 0xEA67A3BF, 0x0A67A3C4, 0xEA67A3C8, 0x8A67A3CC};

// Slot 3 (0x006A8C20): the global effect maps registered; every demand
// exemplar read (ForEachExemplar); the references; the cell walk over the
// city; unless loaded, the step at 0 and the saved floats and counts
// cleared; the tuning, the neighbours, the twelve active-demand figures
// declared, the messages, the simulator registration (as the game's: under
// "cSC4DemandSimulator" with 0, 4, 6 and 10), then the passes.
uint8_t __fastcall Init(void* self, void*)
{
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    static bool logged = false;
    FirstCall("Init", logged);
    reinterpret_cast<void(__cdecl*)(void*)>(addr::cSC4DemandSimulator_InitEffectMaps)(self);
    if (void* const resources = Global<void*>(addr::SC4_ResourceManager)) {
        void* list = nullptr;
        if (Virtual<void*>(resources, 0x48, &list, kExemplarType)) {
            Virtual<uint32_t>(list, 0x1C, reinterpret_cast<void*>(addr::cSC4DemandSimulator_ForEachExemplar), self);
            Virtual<uint32_t>(list, 0x8);
        }
    }
    Reference(self, field::kCity, addr::SC4_City);
    if (!At<void*>(self, kCellIterator)) {
        void* const memory = reinterpret_cast<OperatorNewFn2>(addr::SC4_operator_new)(0x20);
        void* const iterator =
            memory ? reinterpret_cast<CellIteratorFn>(addr::SC4CellIterator_Construct)(memory) : nullptr;
        At<void*>(self, kCellIterator) = iterator;
        void* const city = At<void*>(self, field::kCity);
        const int32_t zMax = Virtual<int32_t>(city, 0x178) - 1;
        const int32_t xMax = Virtual<int32_t>(city, 0x174) - 1;
        auto* const ints = static_cast<int32_t*>(At<void*>(self, kCellIterator));
        ints[4] = 0;
        ints[5] = 0;
        ints[6] = xMax;
        ints[7] = zMax;
        if (ints[6] < ints[4])
            ints[6] = ints[4];
        if (ints[7] < ints[5])
            ints[7] = ints[5];
    }
    const bool loaded = At<uint8_t>(self, field::kLoaded) != 0;
    At<uint32_t>(self, field::kState) = loaded ? 0u : 3u;
    if (!loaded) {
        for (std::size_t i = 0; i < 4; ++i)
            At<uint32_t>(self, field::kSaved + 4 * i) = 0;
        CallThis<void>(addr::cSC4DemandSimulator_ClearCounts, self, static_cast<char*>(self) + field::kCounted);
    }
    Reference(self, field::kService40, addr::SC4_LotManager);
    Reference(self, field::kService4C, addr::SC4_Service43DA4);
    Reference(self, field::kService3C, addr::SC4_Service43D68);
    Reference(self, field::kService44, addr::SC4_MonthEndService);
    Reference(self, field::kOrdinances, addr::SC4_Service43D90);
    CallThis<void>(addr::cSC4DemandSimulator_LoadTuning, self);
    CallThis<void>(addr::cSC4DemandSimulator_Neighbors, self, 1u);
    void* const figures = Global<void*>(addr::SC4_MonthlyFigures);
    for (const uint32_t figure : kActiveFigures)
        Virtual<uint8_t>(figures, 0x34, figure, 1u);
    if (!At<void*>(self, field::kMessageServer)) {
        Reference(self, field::kMessageServer, addr::SC4_MessageServer);
        void* const server = At<void*>(self, field::kMessageServer);
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(server, 0x14, static_cast<char*>(self) + 4, message);
    }
    void* const simulator = Global<void*>(addr::SC4_SimulatorService);
    struct {
        const void* vtable;
        char* begin;
        char* end;
        char* capacity;
        uint32_t zero;
    } name;
    CallThis<void>(addr::SC4String_FromText, &name, "cSC4DemandSimulator");
    for (const uint32_t kind : {0u, 4u, 6u, 10u})
        Virtual<uint8_t>(simulator, 0x4C, static_cast<char*>(self) + 4, kind, &name, 2u);
    At<uint8_t>(self, field::kInitialized) = 1;
    CallThis<void>(addr::cSC4DemandSimulator_Propagate, self);
    name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
    if (name.begin)
        reinterpret_cast<FreeFn>(addr::SC4_Free)(name.begin);
    return 1;
}

// Slot 4 (0x006A3A20): the registrations undone, the references released,
// the demands shut down and released, the cell walk, the curves (the game
// clears +0xB0 for the second curve, as here) and the counted values freed.
uint8_t __fastcall Shutdown(void* self, void*)
{
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    static bool logged = false;
    FirstCall("Shutdown", logged);
    At<uint8_t>(self, field::kInitialized) = 0;
    if (void* const registry = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC8))
        Virtual<uint8_t>(registry, 0x50, static_cast<char*>(self) + 4);
    if (void* const server = At<void*>(self, field::kMessageServer)) {
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(server, 0x18, static_cast<char*>(self) + 4, message);
        Virtual<uint32_t>(server, 0x8);
        At<void*>(self, field::kMessageServer) = nullptr;
    }
    Release(self, field::kCity);
    Release(self, field::kService3C);
    Release(self, field::kService4C);
    Release(self, field::kService40);
    Release(self, field::kService44);
    Release(self, field::kOrdinances);
    TreeNode* const demands = Header(self, field::kDemands);
    for (TreeNode* node = First(demands); node != demands; node = Next(node)) {
        void* const demand = Value<void*>(node);
        Virtual<uint8_t>(demand, 0x10);
        Virtual<uint32_t>(demand, 0x8);
    }
    auto clear = [self](std::size_t offset, std::uintptr_t erase) {
        if (!Size(self, offset))
            return;
        TreeNode* const header = Header(self, offset);
        reinterpret_cast<EraseFn>(erase)(static_cast<char*>(self) + offset, header->parent);
        header->left = header;
        header->parent = nullptr;
        header->right = header;
        At<uint32_t>(self, offset + 4) = 0;
    };
    clear(field::kDemands, addr::SC4DemandMap_EraseTree);
    if (void* const iterator = At<void*>(self, kCellIterator)) {
        reinterpret_cast<OperatorDeleteFn>(addr::SC4_operator_delete)(iterator);
        At<void*>(self, kCellIterator) = nullptr;
    }
    clear(field::kPending, addr::SC4FloatMap_EraseTree2);
    for (const std::size_t offset : {kNeutralCurve, kTaxEffectCurve}) {
        if (void** const curve = At<void**>(self, offset)) {
            if (curve[0])
                reinterpret_cast<FreeFn>(addr::SC4_Free)(curve[0]);
            reinterpret_cast<OperatorDeleteFn>(addr::SC4_operator_delete)(curve);
            At<void*>(self, kNeutralCurve) = nullptr;
        }
    }
    clear(field::kCounted, addr::SC4FloatMap_EraseTree2);
    return 1;
}

// --- Installation -----------------------------------------------------------

template <typename Fn>
bool Install(std::uintptr_t address, Fn ours, Fn* original)
{
    const addr::Entry* entry = addr_check::Find(address);
    return entry && hook::Install(*entry, ours, original);
}

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

using CreateFn = uint8_t(__fastcall*)(void*, void*, uint32_t, uint32_t, void**);
using ConfigureFn = void(__fastcall*)(void*, void*, uint32_t, void*);
using ExemplarFn = void(__cdecl*)(const uint32_t*, void*);
using OccupantFn = void(__fastcall*)(void*, void*, uint32_t, const uint32_t*);
using SelfFn = void(__fastcall*)(void*, void*);
using InitFn = uint8_t(__fastcall*)(void*, void*);

CreateFn g_originalCreate = nullptr;
ConfigureFn g_originalConfigure = nullptr;
ExemplarFn g_originalExemplar = nullptr;
OccupantFn g_originalOccupant = nullptr;
SelfFn g_originalTuning = nullptr;
InitFn g_originalInit = nullptr, g_originalShutdown = nullptr;

bool InstallTuning(registry::Mode)
{
    return game::IsBuild641() &&
           Install(addr::cSC4DemandSimulator_CreateDemand, &CreateDemand, &g_originalCreate) &&
           Install(addr::cSC4DemandSimulator_ConfigureDemand, &ConfigureDemand, &g_originalConfigure) &&
           Install(addr::cSC4DemandSimulator_ForEachExemplar, &ForEachExemplar, &g_originalExemplar) &&
           Install(addr::cSC4DemandSimulator_OccupantTuning, &OccupantTuning, &g_originalOccupant) &&
           Install(addr::cSC4DemandSimulator_LoadTuning, &LoadTuning, &g_originalTuning) &&
           SwapSlot(addr::cSC4DemandSimulator_vftable, 13, 0x006A52D0, reinterpret_cast<void*>(&AddDemand), "tuning");
}

bool InstallInit(registry::Mode)
{
    return game::IsBuild641() && Install(addr::cSC4DemandSimulator_Init, &Init, &g_originalInit);
}

bool InstallShutdown(registry::Mode)
{
    return game::IsBuild641() && Install(addr::cSC4DemandSimulator_Shutdown, &Shutdown, &g_originalShutdown);
}

const bool g_registeredTuning = registry::Register({
    "simulators.demand.tuning",
    "simulators/demand",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4DemandSimulator_LoadTuning,
    "Our demand tuning: the demands and their effects from the demand exemplars, the occupant effects, the "
    "simulator's tuning exemplar (docs/demand.md)",
    false,
    InstallTuning,
});

const bool g_registeredInit = registry::Register({
    "simulators.demand.init",
    "simulators/demand",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4DemandSimulator_Init,
    "Our demand Init (docs/demand.md)",
    false,
    InstallInit,
});

const bool g_registeredShutdown = registry::Register({
    "simulators.demand.shutdown",
    "simulators/demand",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4DemandSimulator_Shutdown,
    "Our demand Shutdown (docs/demand.md)",
    false,
    InstallShutdown,
});

} // namespace
} // namespace opensc4::demand_sim
