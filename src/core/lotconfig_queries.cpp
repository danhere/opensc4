// Our cSC4LotConfigurationManager (build 641; the class map is in
// lotconfig_sim.h), the parts that only answer: the lookups by ID, family,
// size and filter, the registered filters, the footprint and object
// queries, and the base lot configuration filter that lives in the same
// code range.
//
// The developers pick from the sets these functions fill by position, and
// the sets are the game's STLport hash sets (iterated in bucket order, new
// nodes at a bucket's head): each function walks the map in the game's
// order and makes the game's own container calls (resize for one more, then
// insert), so a set ends up node for node as the game's.
//
// Every virtual call has its argument count from the callee's own `ret N`
// (see lotconfig_sim.cpp).
#include "core/lotconfig_sim.h"

#include "core/game.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::lotconfig_sim {
namespace {

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using CountFn = void(__thiscall*)(void* container, uint32_t count);
using InsertFn = void*(__thiscall*)(void* container, void* result, const void* value);
using ClearFn = uint32_t(__thiscall*)(void* table);
using FindFn = void(__thiscall*)(void* map, char** out, const uint32_t* key);
using RebalanceFn = void*(__cdecl*)(void* node, void* root, void* leftmost, void* rightmost);
using InsertNFn = void(__thiscall*)(void* vector, void* where, const void* value, void* tag, uint32_t count,
                                    uint32_t atEnd);
using CopyFn = void*(__thiscall*)(void* vector, const void* other);
using SortFn = void(__cdecl*)(void* first, void* last);
using FindValueFn = uint32_t*(__cdecl*)(uint32_t* first, uint32_t* last, const uint32_t* value, const void* tag);
using PushBackFn = void(__thiscall*)(void* vector, const uint32_t* value);
using InsertRangeFn = void(__thiscall*)(void* vector, uint32_t* where, const uint32_t* first, const uint32_t* last,
                                        void* tag);

const auto OperatorNew = reinterpret_cast<NewFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto ResizeIdSet = reinterpret_cast<CountFn>(addr::SC4Uint32HashSet_Resize);
const auto InsertId = reinterpret_cast<InsertFn>(addr::SC4Uint32HashSet_Insert);
const auto ResizePointerSet = reinterpret_cast<CountFn>(addr::SC4PtrHashSet_Resize);
const auto InsertPointer = reinterpret_cast<InsertFn>(addr::SC4PtrHashSet_Insert);
const auto ClearTable = reinterpret_cast<ClearFn>(addr::SC4Uint32HashSet_Clear);
const auto ConstructIdSet = reinterpret_cast<CountFn>(addr::SC4Uint32HashSet_Construct);
const auto FindInTree = reinterpret_cast<FindFn>(addr::SC4Uint32Map_Find);
const auto InsertN = reinterpret_cast<InsertNFn>(addr::SC4PtrVector_InsertN);
const auto FindValue = reinterpret_cast<FindValueFn>(addr::SC4_FindUint32);

uint32_t MapKey(const char* node) { return At<uint32_t>(const_cast<char*>(node), 0x4); }

// A configuration, referenced, into a caller's hash set of pointers. The
// reference is taken even when the set already holds it, as in the game.
void AddToSet(HashTable* set, void* configuration)
{
    InsertResult result{};
    AddRefOf(configuration);
    ResizePointerSet(set, set->count + 1);
    InsertPointer(set, &result, &configuration);
}

void AddIdToSet(HashTable* set, uint32_t id)
{
    InsertResult result{};
    ResizeIdSet(set, set->count + 1);
    InsertId(set, &result, &id);
}

// --- Lookups ----------------------------------------------------------------------

// Slot 8 (0x0048A320): no reference taken.
void* __fastcall GetLotConfiguration(char*, void*, uint32_t id)
{
    static bool logged = false;
    FirstCall("GetLotConfiguration", logged);
    char* const node = FindConfigurationNode(id);
    return node ? At<void*>(node, 0x8) : nullptr;
}

// Slot 10 (0x0048A7E0): the count.
uint32_t __fastcall GetLotConfigurationsByFamily(char*, void*, HashTable* set, uint32_t family)
{
    static bool logged = false;
    FirstCall("GetLotConfigurationsByFamily", logged);
    uint32_t count = 0;
    const HashTable* const map = Map();
    for (char* node = FirstNode(map); node; node = NextNode(map, node, MapKey(node))) {
        void* const configuration = At<void*>(node, 0x8);
        if (!Virtual<uint8_t>(configuration, config::kBelongsToFamily, family))
            continue;
        AddToSet(set, configuration);
        ++count;
    }
    return count;
}

// Slot 11 (0x0048A8B0): the count.
uint32_t __fastcall GetLotConfigurationsBySize(char*, void*, HashTable* set, uint32_t width, uint32_t depth)
{
    static bool logged = false;
    FirstCall("GetLotConfigurationsBySize", logged);
    uint32_t count = 0;
    const HashTable* const map = Map();
    for (char* node = FirstNode(map); node; node = NextNode(map, node, MapKey(node))) {
        void* const configuration = At<void*>(node, 0x8);
        uint32_t itsWidth = 0, itsDepth = 0;
        Virtual<void>(configuration, config::kGetSize, &itsWidth, &itsDepth);
        if (itsWidth != width || itsDepth != depth)
            continue;
        AddToSet(set, configuration);
        ++count;
    }
    return count;
}

// Slot 13 (0x0048A9A0): the IDs of the configurations the filter accepts
// (its slot 3), all for no filter; the count.
uint32_t __fastcall GetLotConfigurationIDsByFilter(char*, void*, HashTable* set, void* filter)
{
    static bool logged = false;
    FirstCall("GetLotConfigurationIDsByFilter", logged);
    uint32_t count = 0;
    const HashTable* const map = Map();
    for (char* node = FirstNode(map); node; node = NextNode(map, node, MapKey(node))) {
        void* const configuration = At<void*>(node, 0x8);
        if (filter && !Virtual<uint8_t>(filter, 0xC, configuration))
            continue;
        AddIdToSet(set, Virtual<uint32_t>(configuration, config::kGetId));
        ++count;
    }
    return count;
}

// Slot 14 (0x0048AA80).
uint32_t __fastcall GetLotConfigurationIDsByFamily(char*, void*, HashTable* set, uint32_t family)
{
    static bool logged = false;
    FirstCall("GetLotConfigurationIDsByFamily", logged);
    uint32_t count = 0;
    const HashTable* const map = Map();
    for (char* node = FirstNode(map); node; node = NextNode(map, node, MapKey(node))) {
        void* const configuration = At<void*>(node, 0x8);
        if (!Virtual<uint8_t>(configuration, config::kBelongsToFamily, family))
            continue;
        AddIdToSet(set, Virtual<uint32_t>(configuration, config::kGetId));
        ++count;
    }
    return count;
}

// Slot 15 (0x0048AB50).
uint32_t __fastcall GetLotConfigurationIDsBySize(char*, void*, HashTable* set, uint32_t width, uint32_t depth)
{
    static bool logged = false;
    FirstCall("GetLotConfigurationIDsBySize", logged);
    uint32_t count = 0;
    const HashTable* const map = Map();
    for (char* node = FirstNode(map); node; node = NextNode(map, node, MapKey(node))) {
        void* const configuration = At<void*>(node, 0x8);
        uint32_t itsWidth = 0, itsDepth = 0;
        Virtual<void>(configuration, config::kGetSize, &itsWidth, &itsDepth);
        if (itsWidth != width || itsDepth != depth)
            continue;
        AddIdToSet(set, Virtual<uint32_t>(configuration, config::kGetId));
        ++count;
    }
    return count;
}

// 0x0048A170: insert_unique(first, last) of a hash set of pointers.
void InsertConfigurationRange(HashTable* set, void** first, void** last)
{
    uint32_t count = static_cast<uint32_t>(last - first);
    ResizePointerSet(set, set->count + count);
    InsertResult result{};
    for (; count != 0; --count, ++first)
        InsertPointer(set, &result, first);
}

// Slot 9 (0x0048B1A0): the count. For a registered filter the accepted
// configurations are found once, sorted by address (the game's std::sort
// of the pointers, 0x0048ACF0) and kept in a vector at the filter's map
// node until a configuration is added or removed; every call references
// them and inserts them in the caller's set in that order. An unregistered
// filter (or none: every configuration) is asked for each configuration in
// the map's order.
uint32_t __fastcall GetLotConfigurationsByFilter(char* self, void*, HashTable* set, void* filter)
{
    static bool logged = false;
    FirstCall("GetLotConfigurationsByFilter", logged);
    const HashTable* const map = Map();
    if (filter) {
        char* const tree = self + field::kFilters;
        char* found = nullptr;
        const uint32_t key = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(filter));
        FindInTree(tree, &found, &key);
        if (found != At<char*>(tree, 0x0)) {
            if (!At<void*>(found, 0x14)) {
                PtrVector accepted{};
                for (char* node = FirstNode(map); node; node = NextNode(map, node, MapKey(node))) {
                    void* configuration = At<void*>(node, 0x8);
                    if (!Virtual<uint8_t>(filter, 0xC, configuration))
                        continue;
                    if (accepted.end != accepted.capacity) {
                        if (accepted.end)
                            *accepted.end = configuration;
                        ++accepted.end;
                    } else {
                        InsertN(&accepted, accepted.end, &configuration, &filter, 1u, 1u);
                    }
                }
                reinterpret_cast<SortFn>(addr::SC4_SortUint32)(accepted.begin, accepted.end);
                void* copy = OperatorNew(sizeof(PtrVector));
                if (copy)
                    copy = reinterpret_cast<CopyFn>(addr::SC4PtrVector_Copy)(copy, &accepted);
                At<void*>(found, 0x14) = copy;
                if (accepted.begin)
                    Free(accepted.begin);
            }
            PtrVector* const cached = At<PtrVector*>(found, 0x14);
            for (void** configuration = cached->begin; configuration != cached->end; ++configuration)
                AddRefOf(*configuration);
            InsertConfigurationRange(set, cached->begin, cached->end);
            return static_cast<uint32_t>(cached->end - cached->begin);
        }
    }
    uint32_t count = 0;
    for (char* node = FirstNode(map); node; node = NextNode(map, node, MapKey(node))) {
        void* const configuration = At<void*>(node, 0x8);
        if (filter && !Virtual<uint8_t>(filter, 0xC, configuration))
            continue;
        AddToSet(set, configuration);
        ++count;
    }
    return count;
}

// Slot 12 (0x0048A370): every configuration of the set released, the set
// emptied (its buckets stay). The game returns what the clear leaves in
// EAX.
uint32_t __fastcall ClearLotConfigurationSet(char*, void*, HashTable* set)
{
    static bool logged = false;
    FirstCall("ClearLotConfigurationSet", logged);
    for (char* node = FirstNode(set); node;) {
        ReleaseOf(At<void*>(node, 0x4));
        node = NextNode(set, node, At<uint32_t>(node, 0x4) >> 2);
    }
    return ClearTable(set);
}

// Slot 18 (0x0048A660): the filter into the map, with no vector yet (an
// insert of a filter already there changes nothing). The game returns the
// insert's result address; 1 here.
uint32_t __fastcall RegisterLotConfigurationFilter(char* self, void*, void* filter)
{
    static bool logged = false;
    FirstCall("RegisterLotConfigurationFilter", logged);
    struct {
        void* filter;
        void* cached;
    } pair{filter, nullptr};
    struct {
        void* node;
        uint32_t inserted;
    } result{};
    reinterpret_cast<InsertFn>(addr::SC4FloraMap_Insert)(self + field::kFilters, &result, &pair);
    return 1;
}

// Slot 19 (0x0048A3F0): the filter's node out of the map, its vector
// freed. The game returns nothing meaningful; 1 here.
uint32_t __fastcall UnregisterLotConfigurationFilter(char* self, void*, void* filter)
{
    static bool logged = false;
    FirstCall("UnregisterLotConfigurationFilter", logged);
    char* const tree = self + field::kFilters;
    char* found = nullptr;
    const uint32_t key = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(filter));
    FindInTree(tree, &found, &key);
    if (found == At<char*>(tree, 0x0))
        return 1;
    if (PtrVector* const cached = At<PtrVector*>(found, 0x14)) {
        if (cached->begin)
            Free(cached->begin);
        OperatorDelete(cached);
    }
    char* const head = At<char*>(tree, 0x0);
    if (void* const dead =
            reinterpret_cast<RebalanceFn>(addr::SC4Tree_RebalanceForErase)(found, head + 0x4, head + 0x8, head + 0xC))
        Free(dead);
    --At<uint32_t>(tree, 0x4);
    return 1;
}

// Slots 20 and 21 (0x00489720, 0x00489730).
void* __fastcall GetLotConfigurationMap(char*, void*) { return Map(); }
void* __fastcall GetLotConfigurationFamilyIDSet(char*, void*) { return FamilySet(); }

#define OPENSC4_SLOT(index, name, fn)                                                                                \
    {addr::cSC4LotConfigurationManager_vftable, index, addr::cSC4LotConfigurationManager_##name,                      \
     reinterpret_cast<void*>(&fn)}

const Slot kQuerySlots[] = {
    OPENSC4_SLOT(8, GetLotConfiguration, GetLotConfiguration),
    OPENSC4_SLOT(9, GetLotConfigurationsByFilter, GetLotConfigurationsByFilter),
    OPENSC4_SLOT(10, GetLotConfigurationsByFamily, GetLotConfigurationsByFamily),
    OPENSC4_SLOT(11, GetLotConfigurationsBySize, GetLotConfigurationsBySize),
    OPENSC4_SLOT(12, ClearLotConfigurationSet, ClearLotConfigurationSet),
    OPENSC4_SLOT(13, GetLotConfigurationIDsByFilter, GetLotConfigurationIDsByFilter),
    OPENSC4_SLOT(14, GetLotConfigurationIDsByFamily, GetLotConfigurationIDsByFamily),
    OPENSC4_SLOT(15, GetLotConfigurationIDsBySize, GetLotConfigurationIDsBySize),
    OPENSC4_SLOT(18, RegisterLotConfigurationFilter, RegisterLotConfigurationFilter),
    OPENSC4_SLOT(19, UnregisterLotConfigurationFilter, UnregisterLotConfigurationFilter),
    OPENSC4_SLOT(20, GetLotConfigurationMap, GetLotConfigurationMap),
    OPENSC4_SLOT(21, GetLotConfigurationFamilyIDSet, GetLotConfigurationFamilyIDSet),
};

// --- Footprints and objects ---------------------------------------------------------

// The rectangle grows to a width and depth (the game copies the float when
// the rectangle's is less).
void Grow(float* rect, float width, float depth)
{
    if (rect[0] < width)
        rect[0] = width;
    if (rect[1] < depth)
        rect[1] = depth;
}

// Slot 24 (0x00489E10): (4, 4, 0, 0), then the largest width and depth
// (+0x20, +0x28 of the development simulator's slot 40 record) of the IDs
// that are building types with a record (slot 15) and of the types of the
// others taken as families (slot 18). The game returns nothing meaningful;
// 1 here.
uint32_t __fastcall GetMaxFootprintForBuildingFamilies(char* self, void*, const Vector32* ids, float* rect)
{
    static bool logged = false;
    FirstCall("GetMaxFootprintForBuildingFamilies", logged);
    void* const development = At<void*>(self, field::kDevelopment);
    At<uint32_t>(rect, 0x8) = 0;
    At<uint32_t>(rect, 0xC) = 0;
    rect[0] = 4.0f;
    rect[1] = 4.0f;
    const int32_t count = static_cast<int32_t>(ids->end - ids->begin);
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t id = ids->begin[i];
        if (Virtual<uint8_t>(development, 0x3C, id)) {
            if (char* const profile = Virtual<char*>(development, 0xA0, id)) {
                Grow(rect, At<float>(profile, 0x20), At<float>(profile, 0x28));
                continue;
            }
        }
        Vector32 types{};
        if (Virtual<uint32_t>(development, 0x48, &types, id)) {
            uint32_t* const last = types.end;
            for (uint32_t* type = types.begin; type != last; ++type)
                if (char* const profile = Virtual<char*>(development, 0xA0, *type))
                    Grow(rect, At<float>(profile, 0x20), At<float>(profile, 0x28));
        }
        if (types.begin)
            Free(types.begin);
    }
    return 1;
}

// Slot 26 (0x00489F50): (2, 2, 0, 0), then the largest +0x4 and +0xC of
// the prop manager's (0x00B43D10) slot 14 records, for the IDs that have
// one and for the types of the others taken as families (slot 9).
uint32_t __fastcall GetMaxFootprintForPropFamilies(char*, void*, const Vector32* ids, float* rect)
{
    static bool logged = false;
    FirstCall("GetMaxFootprintForPropFamilies", logged);
    void* const props = Global<void*>(addr::SC4_Service43D10);
    At<uint32_t>(rect, 0x8) = 0;
    At<uint32_t>(rect, 0xC) = 0;
    rect[0] = 2.0f;
    rect[1] = 2.0f;
    const int32_t count = static_cast<int32_t>(ids->end - ids->begin);
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t id = ids->begin[i];
        if (char* const record = Virtual<char*>(props, 0x38, id)) {
            Grow(rect, At<float>(record, 0x4), At<float>(record, 0xC));
            continue;
        }
        Vector32 types{};
        if (Virtual<uint32_t>(props, 0x24, &types, id)) {
            uint32_t* const last = types.end;
            for (uint32_t* type = types.begin; type != last; ++type)
                if (char* const record = Virtual<char*>(props, 0x38, *type))
                    Grow(rect, At<float>(record, 0x4), At<float>(record, 0xC));
        }
        if (types.begin)
            Free(types.begin);
    }
    return 1;
}

// Twice a flora record's radius (+0x3C) into the width and the depth when
// greater (the game compares the sum on the x87 stack and stores it).
void GrowFlora(float* rect, const char* record)
{
    const double radius = At<float>(const_cast<char*>(record), 0x3C);
    const double across = radius + radius;
    if (across > static_cast<double>(rect[0]))
        rect[0] = static_cast<float>(across);
    if (across > static_cast<double>(rect[1]))
        rect[1] = static_cast<float>(across);
}

// Slot 25 (0x0048B860): (2, 2, 0, 0), then the flora simulator's
// (0x00B43D80) slot 7 records of the IDs that have one and of the types of
// the others taken as families (slot 14, into a hash set walked in its
// order).
uint32_t __fastcall GetMaxFootprintForFloraFamilies(char*, void*, const Vector32* ids, float* rect)
{
    static bool logged = false;
    FirstCall("GetMaxFootprintForFloraFamilies", logged);
    void* const flora = Global<void*>(addr::SC4_FloraSimulator43D80);
    At<uint32_t>(rect, 0x8) = 0;
    At<uint32_t>(rect, 0xC) = 0;
    rect[0] = 2.0f;
    rect[1] = 2.0f;
    const int32_t count = static_cast<int32_t>(ids->end - ids->begin);
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t id = ids->begin[i];
        if (const char* const record = Virtual<char*>(flora, 0x1C, id)) {
            GrowFlora(rect, record);
            continue;
        }
        HashTable types{};
        ConstructIdSet(&types, 100);
        if (Virtual<uint32_t>(flora, 0x38, &types, id)) {
            for (char* node = FirstNode(&types); node; node = NextNode(&types, node, MapKey(node)))
                if (const char* const record = Virtual<char*>(flora, 0x1C, MapKey(node)))
                    GrowFlora(rect, record);
        }
        ClearTable(&types);
        if (types.buckets)
            Free(types.buckets);
    }
    return 1;
}

// Slot 27 (0x0048BA10): the list emptied, then for each ID of the object
// (kind 0 building, 1 prop, 4 flora; nothing for other kinds) the ID itself
// when it is a type, else the types of the family, appended. The services
// come from the globals, not the object's fields.
uint32_t __fastcall GetObjectIDArray(char*, void*, char* object, Vector32* out)
{
    static bool logged = false;
    FirstCall("GetObjectIDArray", logged);
    out->end = out->begin;
    if (!object || At<uint32_t*>(object, 0x28) == At<uint32_t*>(object, 0x2C))
        return 1;
    const int32_t count = static_cast<int32_t>(At<uint32_t*>(object, 0x2C) - At<uint32_t*>(object, 0x28));
    void* const development = Global<void*>(addr::SC4_Service43D68);
    void* const props = Global<void*>(addr::SC4_Service43D10);
    void* const flora = Global<void*>(addr::SC4_FloraSimulator43D80);
    const auto PushBack = reinterpret_cast<PushBackFn>(addr::SC4PtrVector_PushBack);
    uint8_t tag = 0;
    for (int32_t i = 0; i < count; ++i) {
        uint32_t id = At<uint32_t*>(object, 0x28)[i];
        switch (At<uint8_t>(object, 0x0)) {
        case 0:
            if (Virtual<uint8_t>(development, 0x3C, id))
                PushBack(out, &id);
            else
                Virtual<void>(development, 0x48, out, id);
            break;
        case 1:
            if (Virtual<uint8_t>(props, 0x20, id)) {
                PushBack(out, &id);
            } else {
                Vector32 types{};
                Virtual<void>(props, 0x24, &types, id);
                reinterpret_cast<InsertRangeFn>(addr::SC4Uint32Vector_InsertRange)(out, out->end, types.begin,
                                                                                   types.end, &tag);
                if (types.begin)
                    Free(types.begin);
            }
            break;
        case 4:
            if (Virtual<uint8_t>(flora, 0x2C, id)) {
                PushBack(out, &id);
            } else {
                HashTable types{};
                ConstructIdSet(&types, 100);
                Virtual<void>(flora, 0x38, &types, id);
                for (char* node = FirstNode(&types); node; node = NextNode(&types, node, MapKey(node))) {
                    if (out->end != out->capacity) {
                        if (out->end)
                            *out->end = MapKey(node);
                        ++out->end;
                    } else {
                        InsertN(out, out->end, node + 0x4, &tag, 1u, 1u);
                    }
                }
                ClearTable(&types);
                if (types.buckets)
                    Free(types.buckets);
            }
            break;
        default:
            break;
        }
    }
    return 1;
}

// Slot 16 (0x00489AA0): the first building object (kind 0) whose ID list
// holds the ID; its address into `out` when given. The object count and
// the vector's start are read once.
uint8_t __fastcall LotConfigurationHasBuildingFamily(char*, void*, void* configuration, uint32_t family, char** out)
{
    static bool logged = false;
    FirstCall("LotConfigurationHasBuildingFamily", logged);
    if (out)
        *out = nullptr;
    if (!configuration || !family)
        return 0;
    const ObjectVector* const objects = Virtual<ObjectVector*>(configuration, config::kGetObjects);
    if (!objects)
        return 0;
    char* const begin = objects->begin;
    const uint32_t count = ObjectCount(objects);
    for (uint32_t i = 0; i < count; ++i) {
        char* const object = begin + static_cast<std::size_t>(i) * config::kObjectSize;
        if (At<uint8_t>(object, 0x0) != 0)
            continue;
        uint32_t* const last = At<uint32_t*>(object, 0x2C);
        if (FindValue(At<uint32_t*>(object, 0x28), last, &family, &family) != last) {
            if (out)
                *out = object;
            return 1;
        }
    }
    return 0;
}

// Slot 17 (0x00489B40): the first building object whose ID list holds the
// type, or a family the type belongs to (the development simulator's slot
// 20 with (type, ID)). The object count is read again after each object.
uint8_t __fastcall LotConfigurationHasBuildingType(char* self, void*, void* configuration, uint32_t type, char** out)
{
    static bool logged = false;
    FirstCall("LotConfigurationHasBuildingType", logged);
    if (out)
        *out = nullptr;
    if (!configuration || !type)
        return 0;
    const ObjectVector* const objects = Virtual<ObjectVector*>(configuration, config::kGetObjects);
    if (!objects || ObjectCount(objects) == 0)
        return 0;
    uint32_t index = 0;
    do {
        char* const object = objects->begin + static_cast<std::size_t>(index) * config::kObjectSize;
        if (At<uint8_t>(object, 0x0) == 0) {
            const Vector32* const ids = reinterpret_cast<const Vector32*>(object + 0x28);
            uint32_t* const last = ids->end;
            bool found = FindValue(ids->begin, last, &type, &type) != last;
            if (!found) {
                const int32_t count = static_cast<int32_t>(ids->end - ids->begin);
                for (int32_t i = 0; i < count && !found; ++i)
                    found = Virtual<uint8_t>(At<void*>(self, field::kDevelopment), 0x50, type, ids->begin[i]) != 0;
            }
            if (found) {
                if (out)
                    *out = objects->begin + static_cast<std::size_t>(index) * config::kObjectSize;
                return 1;
            }
        }
        ++index;
    } while (index < ObjectCount(objects));
    return 0;
}

const Slot kFootprintSlots[] = {
    OPENSC4_SLOT(16, LotConfigurationHasBuildingFamily, LotConfigurationHasBuildingFamily),
    OPENSC4_SLOT(17, LotConfigurationHasBuildingType, LotConfigurationHasBuildingType),
    OPENSC4_SLOT(24, GetMaxFootprintForBuildingFamilies, GetMaxFootprintForBuildingFamilies),
    OPENSC4_SLOT(25, GetMaxFootprintForFloraFamilies, GetMaxFootprintForFloraFamilies),
    OPENSC4_SLOT(26, GetMaxFootprintForPropFamilies, GetMaxFootprintForPropFamilies),
    OPENSC4_SLOT(27, GetObjectIDArray, GetObjectIDArray),
};

#undef OPENSC4_SLOT

// --- The base filter (vtable 0x00A8EC1C) --------------------------------------------

// Slot 0 (0x00489670).
uint8_t __fastcall FilterQueryInterface(char* self, void*, uint32_t iid, void** object)
{
    static bool logged = false;
    FirstCall("filter QueryInterface", logged);
    if (iid != kIidUnknown && iid != kIidFilter)
        return 0;
    *object = self;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// Slot 4 (0x004897A0).
void* __fastcall FilterDeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("filter destructor", logged);
    At<std::uintptr_t>(self, 0x0) = addr::cSC4LotConfigurationFilter_vftable;
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

const Slot kFilterSlots[] = {
    {addr::cSC4LotConfigurationFilter_vftable, 0, addr::cSC4LotConfigurationFilter_QueryInterface,
     reinterpret_cast<void*>(&FilterQueryInterface)},
    {addr::cSC4LotConfigurationFilter_vftable, 4, addr::cSC4LotConfigurationFilter_DeletingDestructor,
     reinterpret_cast<void*>(&FilterDeletingDestructor)},
};

bool InstallQueries(registry::Mode)
{
    return SwapSlots(kQuerySlots, sizeof(kQuerySlots) / sizeof(kQuerySlots[0]), "queries");
}

bool InstallFootprints(registry::Mode)
{
    return SwapSlots(kFootprintSlots, sizeof(kFootprintSlots) / sizeof(kFootprintSlots[0]), "footprints");
}

bool InstallFilter(registry::Mode)
{
    return SwapSlots(kFilterSlots, sizeof(kFilterSlots) / sizeof(kFilterSlots[0]), "filter");
}

const bool g_registered[] = {
    registry::Register({"simulators.lotconfig.queries", "simulators/lotconfig", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4LotConfigurationManager_vftable,
                        "Our lot configuration manager lookups by ID, family, size and filter, the registered "
                        "filters and the two container getters (src/core/lotconfig_queries.cpp)",
                        false, InstallQueries}),
    registry::Register({"simulators.lotconfig.footprints", "simulators/lotconfig", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4LotConfigurationManager_vftable,
                        "Our lot configuration manager footprint queries, GetObjectIDArray and the building "
                        "family and type tests (src/core/lotconfig_queries.cpp)",
                        false, InstallFootprints}),
    registry::Register({"simulators.lotconfig.filter", "simulators/lotconfig", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4LotConfigurationFilter_vftable,
                        "Our base lot configuration filter QueryInterface and deleting destructor, in the base "
                        "vtable only (src/core/lotconfig_queries.cpp)",
                        false, InstallFilter}),
};

} // namespace
} // namespace opensc4::lotconfig_sim
