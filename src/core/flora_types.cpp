// Our cSC4FloraSimulator (build 641), fourth part: the flora types and the
// tuning (docs/flora.md, "Types"). A type is a 0x178-byte record read from
// a flora exemplar (group 0x6534284A, type property 0x10 = 0xF), kept in the
// global type map, the load-order list and the family map. Containers grow
// through the game's own helpers, so the heap sees the same allocations.
#include "core/flora_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstdint>

namespace opensc4::flora_sim {
namespace {

constexpr uint32_t kExemplarType = 0x6534284A;
constexpr uint32_t kExemplarInterface = 0xA52160F5;
constexpr uint32_t kTuningKey[3] = {kExemplarType, 0x7A4A8458, 0x1A2FDB6A};

struct Vector {
    char* begin;
    char* end;
    char* capacity;
};

// A family map entry as the game inserts it: the family and its types.
struct FamilyPair {
    uint32_t family;
    Vector types;
};

using GetFloatFn = uint8_t(__cdecl*)(void* holder, uint32_t property, float* value);
using GetSint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, int32_t* value);
using GetUint32Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint32_t* value);
using GetBoolFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint8_t* value);
using GetVectorFn = uint8_t(__cdecl*)(void* holder, uint32_t property, Vector* value);
using GetSint64Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, int64_t* value);
using PtrInsertFn = void(__thiscall*)(void* vector, void* where, const void* value, void* allocator, uint32_t count,
                                      uint32_t atEnd);
using MapInsertFn = void*(__thiscall*)(std::uintptr_t map, void* result, const void* pair);
using VectorFn = void*(__thiscall*)(void* vector, const Vector* other);
using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using MessageCtorFn = void*(__thiscall*)(void* message);
using EraseFn = void(__thiscall*)(std::uintptr_t map, MapNode* node);
using DataFn = void(__stdcall*)(char* data);
using SelfFn = void(__thiscall*)(void* self);
using CallbackFn = uint32_t(__cdecl*)(const uint32_t* key, void* self);

const auto GetFloat = reinterpret_cast<GetFloatFn>(addr::SC4_GetPropertyFloat);
const auto GetSint32 = reinterpret_cast<GetSint32Fn>(addr::SC4_GetPropertySint32);
const auto GetUint32 = reinterpret_cast<GetUint32Fn>(addr::SC4_GetPropertyUint32);
const auto GetBool = reinterpret_cast<GetBoolFn>(addr::SC4_GetPropertyBool);
const auto GetVector = reinterpret_cast<GetVectorFn>(addr::SC4_GetPropertyUint32Vector);
const auto GetSint64 = reinterpret_cast<GetSint64Fn>(addr::SC4_GetPropertySint64);
const auto PtrInsert = reinterpret_cast<PtrInsertFn>(addr::SC4PtrVector_InsertN);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

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

// --- Records ------------------------------------------------------------------

// 0x006B0220: the defaults: sizes and heights, the 16x16 footprint (0x99 in
// the middle 8x8, 0 around it), everything else 0.
void __stdcall DefaultData(char* data)
{
    static bool logged = false;
    FirstCall("DefaultData", logged);
    At<uint32_t>(data, 0x10) = 0x44368000; // 730
    At<uint32_t>(data, 0x14) = 0x44368000;
    At<uint32_t>(data, 0x2C) = 0x3F000000; // 0.5
    At<uint32_t>(data, 0x34) = 0x3F000000;
    At<uint32_t>(data, 0x00) = 0;
    At<uint32_t>(data, 0x04) = 0;
    At<uint32_t>(data, 0x08) = 0;
    At<uint8_t>(data, 0x0C) = 0;
    At<uint32_t>(data, 0x18) = 0x43B68000; // 365
    At<uint32_t>(data, 0x1C) = 0x40000000; // 2
    At<uint32_t>(data, 0x20) = 0x43000000; // 128
    At<uint32_t>(data, 0x24) = 0x44B68000; // 1460
    At<uint32_t>(data, 0x28) = 0x44C30000; // 1560
    At<uint32_t>(data, 0x30) = 0x3E99999A; // 0.3
    At<uint32_t>(data, 0x38) = 0x3F800000; // 1
    At<uint32_t>(data, 0x3C) = 0x40800000; // 4
    At<uint32_t>(data, 0x40) = 0x40400000; // 3
    At<uint32_t>(data, 0x44) = 0x40400000;
    At<uint32_t>(data, 0x48) = 0;
    At<uint32_t>(data, 0x4C) = 0;
    for (int row = 0; row < 16; ++row) {
        for (int column = 0; column < 16; ++column) {
            const bool inside = row >= 5 && row <= 12 && column >= 5 && column <= 12;
            At<uint8_t>(data, 0x50 + row * 16 + column) = inside ? 0x99 : 0;
        }
    }
    At<uint32_t>(data, 0x150) = 0;
}

// Slot 6 (0x006B3570): a record into the maps unless its type is known: the
// type map, the load-order list, and each of its families. Then the
// record's +8 becomes its type, and every record whose +8 names this
// record's +4 points at this type instead.
uint8_t __fastcall AddStaticFloraDataRecord(void*, void*, char* data)
{
    static bool logged = false;
    FirstCall("AddStaticFloraData(record)", logged);
    uint32_t type = At<uint32_t>(data, 0);
    if (Find(addr::cSC4FloraSimulator_TypeMap, type) != TypeMapHead())
        return 0;
    struct {
        uint32_t key;
        char* value;
    } pair{type, data};
    char result[8];
    reinterpret_cast<MapInsertFn>(addr::SC4FloraMap_Insert)(addr::cSC4FloraSimulator_TypeMap, result, &pair);
    PushWord(&Global<Vector>(addr::cSC4FloraSimulator_TypeList), type);
    ++At<int32_t>(data, 0x170);

    const Vector& families = At<Vector>(data, 0x154);
    const int32_t count = static_cast<int32_t>((families.end - families.begin) >> 2);
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t family = reinterpret_cast<const uint32_t*>(At<Vector>(data, 0x154).begin)[i];
        MapNode* const head = FamilyMapHead();
        MapNode* node = Find(addr::cSC4FloraSimulator_FamilyMap, family);
        if (node == head) {
            const Vector empty{};
            FamilyPair entry;
            entry.family = family;
            reinterpret_cast<VectorFn>(addr::SC4Uint32Vector_Copy)(&entry.types, &empty);
            reinterpret_cast<MapInsertFn>(addr::SC4FamilyMap_Insert)(addr::cSC4FloraSimulator_FamilyMap, result,
                                                                     &entry);
            node = Find(addr::cSC4FloraSimulator_FamilyMap, family);
            if (entry.types.begin)
                Free(entry.types.begin);
        }
        PushWord(reinterpret_cast<Vector*>(&node->value), type);
    }

    At<uint32_t>(data, 0x8) = type;
    MapNode* const head = TypeMapHead();
    for (MapNode* node = head->left; node != head; node = Next(node)) {
        char* const other = node->value;
        if (At<uint32_t>(other, 0x4) == type)
            At<uint32_t>(data, 0x8) = At<uint32_t>(other, 0x4);
        if (At<uint32_t>(other, 0x8) == At<uint32_t>(data, 0x4))
            At<uint32_t>(other, 0x8) = type;
    }
    return 1;
}

// Slot 5 (0x006B12D0): a record from a flora exemplar (loaded by key when
// none is given), added through slot 6.
uint8_t __fastcall AddStaticFloraData(void* self, void*, const uint32_t* key, void* exemplar)
{
    static bool logged = false;
    FirstCall("AddStaticFloraData(exemplar)", logged);
    if (exemplar)
        Virtual<uint32_t>(exemplar, 0x4);
    else
        Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, kExemplarInterface, &exemplar, 0u, 0u);
    if (!exemplar)
        return 0;
    Vector families{};
    void* const holder = Virtual<void*>(exemplar, 0x1C);
    char* const data = NewData();
    void* const previous = At<void*>(data, 0x168);
    if (exemplar != previous) {
        Virtual<uint32_t>(exemplar, 0x4);
        At<void*>(data, 0x168) = exemplar;
        if (previous)
            Virtual<uint32_t>(previous, 0x8);
    }
    Virtual<uint8_t>(holder, 0x20, 0x21u, data);
    if (void* property = Virtual<void*>(holder, 0x24, 0x083DC35Bu)) {
        const uint32_t* const values = Virtual<uint32_t*>(Virtual<void*>(property, 0x14), 0x1BC);
        for (int i = 0; i < 14; ++i)
            At<uint32_t>(data, 0x10 + i * 4) = values[i];
    }
    if (Virtual<uint8_t>(holder, 0xC, 0xEA346888u))
        At<uint8_t>(data, 0xC) = 1;
    Virtual<uint8_t>(holder, 0x20, 0x2A0348BAu, data + 4);
    GetFloat(holder, 0x2A0348BB, &At<float>(data, 0x48));
    GetFloat(holder, 0x2A0348BC, &At<float>(data, 0x4C));
    if (void* property = Virtual<void*>(holder, 0x24, 0x083DC487u)) {
        const float* values = Virtual<float*>(Virtual<void*>(property, 0x14), 0x1BC);
        for (int i = 0; i < 256; ++i)
            At<uint8_t>(data, 0x50 + i) =
                static_cast<uint8_t>(static_cast<int64_t>(static_cast<double>(values[i]) * 255.0));
    }
    Virtual<uint8_t>(holder, 0x20, 0x083DE16Fu, data + 0x150);
    uint8_t& flags = At<uint8_t>(data, 0x16C);
    flags = static_cast<uint8_t>((flags & 0xFE) | 6);
    uint8_t value = 0;
    if (GetBool(holder, 0xE9D117C3, &value))
        flags = static_cast<uint8_t>((flags & 0xFE) | (value & 1));
    if (GetBool(holder, 0x6A37EBB6, &value))
        flags &= value ? 0xFB : 0xFD;
    GetVector(holder, 0xA8F149C5, &families);
    reinterpret_cast<VectorFn>(addr::SC4Uint32Vector_CopyAssign)(data + 0x154, &families);
    At<int64_t>(data, 0x160) = 0;
    GetSint64(holder, 0x49CAC341, &At<int64_t>(data, 0x160));
    const uint8_t added = Virtual<uint8_t>(self, 0x18, data);
    ReleaseData(data);
    if (families.begin)
        Free(families.begin);
    Virtual<uint32_t>(exemplar, 0x8);
    return added;
}

// --- Loading --------------------------------------------------------------------

// 0x006B03B0: each exemplar of the resource list; flora ones (property 0x10
// = 0xF) through slot 5. Returns what the game leaves in EAX.
uint32_t __cdecl LoadTypeCallback(const uint32_t* key, void* self)
{
    void* exemplar = nullptr;
    const uint32_t loaded =
        Virtual<uint32_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, kExemplarInterface, &exemplar, 0u, 0u);
    if (!static_cast<uint8_t>(loaded))
        return loaded;
    uint32_t type = 0;
    if (GetUint32(Virtual<void*>(exemplar, 0x1C), 0x10, &type) && type == 0xF)
        Virtual<uint8_t>(self, 0x14, key, exemplar);
    return Virtual<uint32_t>(exemplar, 0x8);
}

// 0x006B05E0: message 0x87A6277F (the simulator, the type count) after a
// load.
void __fastcall NotifyTypesLoaded(void* self, void*)
{
    void* const server = Global<void*>(addr::SC4_MessageServer);
    if (!server)
        return;
    void* message = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    if (message)
        message = reinterpret_cast<MessageCtorFn>(addr::cGZMessage_cGZMessage)(message);
    Virtual<uint32_t>(message, 0x4);
    Virtual<void*>(message, 0x14, 0x87A6277Fu);
    Virtual<void*>(message, 0x30, self);
    Virtual<void*>(message, 0x84, self);
    Virtual<void*>(message, 0x40, Global<uint32_t>(addr::cSC4FloraSimulator_TypeMap + 4));
    Virtual<uint8_t>(server, 0x10, message, 0u);
    Virtual<uint32_t>(message, 0x8);
}

// 0x006B0760: every exemplar through the resource manager's list (slot 18,
// callback 0x006B03B0), then the message.
void __fastcall LoadTypes(void* self, void*)
{
    static bool logged = false;
    FirstCall("LoadTypes", logged);
    if (void* manager = Global<void*>(addr::SC4_ResourceManager)) {
        void* list = nullptr;
        if (Virtual<uint32_t>(manager, 0x48, &list, kExemplarType)) {
            Virtual<void>(list, 0x1C, reinterpret_cast<CallbackFn>(addr::cSC4FloraSimulator_LoadTypeCallback), self);
            Virtual<uint32_t>(list, 0x8);
        }
    }
    reinterpret_cast<SelfFn>(addr::cSC4FloraSimulator_NotifyTypesLoaded)(self);
}

// 0x006B00F0: the tuning exemplar 6534284A-7A4A8458-1A2FDB6A: death rate
// (+0xB0 = 1 / value) and step, tracts a month (+0x44, then +0x48 = cells /
// tracts / 4), seeds a month (+0x4C, scaled by the city's size in meters and
// shifted down 20 bits) and the slope scale (+0xA0 = 16 / value).
void __fastcall LoadTuning(void* self, void*)
{
    static bool logged = false;
    FirstCall("LoadTuning", logged);
    void* exemplar = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, kTuningKey, kExemplarInterface, &exemplar,
                          0u, 0u))
        return;
    void* const holder = Virtual<void*>(exemplar, 0x1C);
    float value = 0;
    if (GetFloat(holder, 0xC9DBDD00, &value))
        At<float>(self, field::kDeathRate) = static_cast<float>(1.0 / static_cast<double>(value));
    GetFloat(holder, 0xC9DBDD01, &At<float>(self, field::kDeathsPerStep));
    GetSint32(holder, 0x8761ABD0, &At<int32_t>(self, 0x44));
    GetSint32(holder, 0x085489C7, &At<int32_t>(self, field::kSeedsPerMonth));
    if (GetFloat(holder, 0xC9DBDD02, &value))
        At<float>(self, field::kSlopeScale) = static_cast<float>(16.0 / static_cast<double>(value));
    const uint32_t cells = At<uint32_t>(self, field::kCellsZ) * At<uint32_t>(self, field::kCellsX);
    At<uint32_t>(self, field::kTractsPerMonth) = cells / At<uint32_t>(self, 0x44) >> 2;
    const int32_t seeds = At<int32_t>(self, field::kSeedsPerMonth);
    double scaled = static_cast<double>(seeds) + (seeds < 0 ? 4294967296.0 : 0.0);
    const int32_t byX = static_cast<int32_t>(
        static_cast<int64_t>(scaled * static_cast<double>(At<float>(self, field::kSizeX))));
    scaled = static_cast<double>(byX) + (byX < 0 ? 4294967296.0 : 0.0);
    const uint32_t byZ = static_cast<uint32_t>(
        static_cast<int64_t>(scaled * static_cast<double>(At<float>(self, field::kSizeZ))));
    At<uint32_t>(self, field::kSeedsPerMonth) = byZ >> 20;
    Virtual<uint32_t>(exemplar, 0x8);
}

// --- Freeing --------------------------------------------------------------------

// 0x006B04B0: a subtree of the type map, freed.
void __fastcall EraseTree(std::uintptr_t map, void*, MapNode* node)
{
    while (node) {
        reinterpret_cast<EraseFn>(addr::SC4FloraMap_EraseTree)(map, node->right);
        MapNode* const next = node->left;
        Free(node);
        node = next;
    }
}

// 0x006B08C0: the type map emptied (the records are not released).
void __fastcall MapClear(std::uintptr_t map, void*)
{
    MapNode*& head = *reinterpret_cast<MapNode**>(map);
    uint32_t& size = *reinterpret_cast<uint32_t*>(map + 4);
    if (!size)
        return;
    reinterpret_cast<EraseFn>(addr::SC4FloraMap_EraseTree)(map, head->parent);
    head->left = head;
    head->parent = nullptr;
    head->right = head;
    size = 0;
}

// 0x006B1070 (registered with the app on the first Init): every record
// released, the type map and the load-order list emptied. The family map
// is left as it is.
uint8_t __cdecl FreeTypes()
{
    static bool logged = false;
    FirstCall("FreeTypes", logged);
    uint8_t& loaded = Global<uint8_t>(addr::cSC4FloraSimulator_TypesLoaded);
    if (!loaded)
        return 1;
    loaded = 0;
    for (MapNode* node = TypeMapHead()->left; node != TypeMapHead(); node = Next(node))
        ReleaseData(node->value);
    if (Global<uint32_t>(addr::cSC4FloraSimulator_TypeMap + 4)) {
        reinterpret_cast<EraseFn>(addr::SC4FloraMap_EraseTree)(addr::cSC4FloraSimulator_TypeMap,
                                                               TypeMapHead()->parent);
        TypeMapHead()->left = TypeMapHead();
        TypeMapHead()->parent = nullptr;
        TypeMapHead()->right = TypeMapHead();
        Global<uint32_t>(addr::cSC4FloraSimulator_TypeMap + 4) = 0;
    }
    Vector& list = Global<Vector>(addr::cSC4FloraSimulator_TypeList);
    list.end = list.begin;
    return 1;
}

// --- Installation -----------------------------------------------------------

template <typename Fn>
bool Install(std::uintptr_t address, Fn ours, Fn* original)
{
    const addr::Entry* entry = addr_check::Find(address);
    return entry && hook::Install(*entry, ours, original);
}

using StdDataFn = void(__stdcall*)(char*);
using CdeclCallbackFn = uint32_t(__cdecl*)(const uint32_t*, void*);
using FastSelfFn = void(__fastcall*)(void*, void*);
using FastEraseFn = void(__fastcall*)(std::uintptr_t, void*, MapNode*);
using FastMapFn = void(__fastcall*)(std::uintptr_t, void*);
using FreeTypesFn = uint8_t(__cdecl*)();
StdDataFn g_originalDefault = nullptr;
CdeclCallbackFn g_originalCallback = nullptr;
FastSelfFn g_originalNotify = nullptr, g_originalLoadTypes = nullptr, g_originalTuning = nullptr;
FastEraseFn g_originalErase = nullptr;
FastMapFn g_originalClear = nullptr;
FreeTypesFn g_originalFree = nullptr;

bool InstallTypes(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(addr::cSC4FloraSimulator_vftable);
    if (vtable[5] != 0x006B12D0 || vtable[6] != 0x006B3570) {
        log::Write("flora types: slots 5/6 hold %08X/%08X; not installing", static_cast<unsigned>(vtable[5]),
                   static_cast<unsigned>(vtable[6]));
        return false;
    }
    if (!Install(addr::cSC4FloraSimulator_DefaultData, &DefaultData, &g_originalDefault) ||
        !Install(addr::cSC4FloraSimulator_LoadTypeCallback, &LoadTypeCallback, &g_originalCallback) ||
        !Install(addr::cSC4FloraSimulator_NotifyTypesLoaded, &NotifyTypesLoaded, &g_originalNotify) ||
        !Install(addr::cSC4FloraSimulator_LoadTypes, &LoadTypes, &g_originalLoadTypes) ||
        !Install(addr::cSC4FloraSimulator_LoadTuning, &LoadTuning, &g_originalTuning) ||
        !Install(addr::SC4FloraMap_EraseTree, &EraseTree, &g_originalErase) ||
        !Install(addr::SC4FloraMap_Clear, &MapClear, &g_originalClear) ||
        !Install(addr::cSC4FloraSimulator_FreeTypes, &FreeTypes, &g_originalFree))
        return false;
    DWORD old = 0;
    if (!VirtualProtect(vtable + 5, 2 * sizeof(*vtable), PAGE_READWRITE, &old))
        return false;
    vtable[5] = reinterpret_cast<std::uintptr_t>(&AddStaticFloraData);
    vtable[6] = reinterpret_cast<std::uintptr_t>(&AddStaticFloraDataRecord);
    VirtualProtect(vtable + 5, 2 * sizeof(*vtable), old, &old);
    return true;
}

const bool g_registeredTypes = registry::Register({
    "simulators.flora.types",
    "simulators/flora",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4FloraSimulator_LoadTypes,
    "Our flora types and tuning: records from the exemplars, the type and family maps (docs/flora.md)",
    false,
    InstallTypes,
});

} // namespace

// 0x00472C70: drops a reference to a type record; the last one releases the
// exemplar and frees the family list and the record.
void ReleaseData(char* data)
{
    int32_t& refs = At<int32_t>(data, 0x170);
    if (refs > 1) {
        --refs;
        return;
    }
    if (void* exemplar = At<void*>(data, 0x168)) {
        At<void*>(data, 0x168) = nullptr;
        Virtual<uint32_t>(exemplar, 0x8);
    }
    if (char* families = At<char*>(data, 0x154))
        Free(families);
    reinterpret_cast<FreeFn>(addr::SC4_operator_delete)(data);
}

// A new record as the game allocates it: the vectors and the exemplar
// cleared, one reference, then the defaults.
char* NewData()
{
    char* const data = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x178));
    if (data) {
        At<uint32_t>(data, 0x34) = 0;
        At<uint32_t>(data, 0x154) = 0;
        At<uint32_t>(data, 0x158) = 0;
        At<uint32_t>(data, 0x15C) = 0;
        At<uint32_t>(data, 0x168) = 0;
        At<uint32_t>(data, 0x170) = 0;
    }
    ++At<int32_t>(data, 0x170);
    reinterpret_cast<DataFn>(addr::cSC4FloraSimulator_DefaultData)(data);
    return data;
}

} // namespace opensc4::flora_sim
