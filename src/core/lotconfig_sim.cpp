// Our cSC4LotConfigurationManager (build 641; the class map is in
// lotconfig_sim.h), the parts that change it: Init and Shutdown, the
// loading of every lot configuration from the exemplars, Add and Remove,
// FixLotConfiguration, CreateGenericLotConfiguration, the message that
// completes the configurations once the city is ready, the save record and
// our own object (constructor, destructor, identity). The queries are in
// lotconfig_queries.cpp.
//
// Every virtual call below has its argument count from the callee's own
// `ret N` (the configuration 0x00A8CEB8, the development simulator
// 0x00AB0AF8, the flora simulator 0x00AB13C0, the prop manager 0x00A8F928,
// the resource manager 0x00AD89D0 and its key list 0x00AADA64, the message
// server, the streams, the service at 0x00B43C94 = vtable 0x00A86AF0); only
// the COM object's slot 7 (CreateGuid) is taken from gzcom-dll and from the
// stack offsets the game uses after the call.
#include "core/lotconfig_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::lotconfig_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("lotconfig: first %s", name);
}

bool SwapSlots(const Slot* slots, std::size_t count, const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (std::size_t i = 0; i < count; ++i) {
        const Slot& slot = slots[i];
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("lotconfig %s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (std::size_t i = 0; i < count; ++i) {
        const Slot& slot = slots[i];
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("lotconfig %s: %u slots are ours", what, static_cast<unsigned>(count));
    return true;
}

namespace {

constexpr uint32_t kIidDevelopment = 0x07813EAE;
constexpr uint32_t kIidFlora = 0x06C66177;
constexpr uint32_t kIidProps = 0x28326F3E;
constexpr uint32_t kExemplarType = 0x6534284A;
constexpr uint32_t kExemplarIid = 0xA52160F5;
constexpr uint32_t kConfigurationGroup = 0xA8FBD372;

using NewFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using MoveFn = void*(__cdecl*)(void* to, const void* from, std::size_t bytes);
using SelfFn = void*(__thiscall*)(void* self);
using CountFn = void(__thiscall*)(void* container, uint32_t count);
using InsertFn = void*(__thiscall*)(void* container, void* result, const void* value);
using EraseAtFn = void(__thiscall*)(void* map, void* iterator);
using ClearFn = uint32_t(__thiscall*)(void* table);
using NodeFn = void(__thiscall*)(void* tree, void* node);
using RefFn = uint8_t(__cdecl*)(void* stream, void* object);
using ReadRefFn = uint8_t(__cdecl*)(void* stream, uint32_t iid, void* out, uint32_t zero);
using InsertNFn = void(__thiscall*)(void* vector, void* where, const void* value, void* tag, uint32_t count,
                                    uint32_t atEnd);
using AssignFn = void*(__thiscall*)(void* vector, const void* other);
using LoadListFn = void(__cdecl*)(const void* key, void* vector);
using PropertyBoolFn = uint8_t(__cdecl*)(void* holder, uint32_t property, uint8_t* value);
using GetFn = void*(__cdecl*)();
using EraseBitsFn = void*(__thiscall*)(void* vector, void* result, uint32_t* firstWord, uint32_t firstBit,
                                       uint32_t* lastWord, uint32_t lastBit);
using EraseRangeFn = void*(__thiscall*)(void* vector, void* first, void* last);
using CopyObjectFn = void*(__thiscall*)(void* object, const void* other);
using CopyObjectsFn = void*(__cdecl*)(const void* first, const void* last, void* out, void* tag, uint32_t zero);

const auto OperatorNew = reinterpret_cast<NewFn>(addr::SC4_operator_new);
const auto OperatorDelete = reinterpret_cast<FreeFn>(addr::SC4_operator_delete);
const auto Allocate = reinterpret_cast<NewFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto Move = reinterpret_cast<MoveFn>(addr::SC4_memmove);
const auto ConstructConfiguration = reinterpret_cast<SelfFn>(addr::cSC4LotConfiguration_cSC4LotConfiguration);
const auto ResizeIdTable = reinterpret_cast<CountFn>(addr::SC4Uint32HashSet_Resize);
const auto InsertId = reinterpret_cast<InsertFn>(addr::SC4Uint32HashSet_Insert);
const auto InsertPair = reinterpret_cast<InsertFn>(addr::SC4Uint32HashMap_Insert);
const auto ClearTable = reinterpret_cast<ClearFn>(addr::SC4Uint32HashSet_Clear);
const auto WriteRef = reinterpret_cast<RefFn>(addr::SC4Stream_WriteReference);
const auto ReadRef = reinterpret_cast<ReadRefFn>(addr::SC4Stream_ReadReference);

char* Target(char* self) { return self + kMessagePart; }

// --- The filter caches ------------------------------------------------------------

} // namespace

// 0x0048A080: the cached vector of every registered filter freed, in the
// map's order.
void InvalidateFilterCaches(char* self)
{
    char* const head = At<char*>(self, field::kFilters);
    for (char* node = At<char*>(head, 0x8); node != head; node = TreeNext(node)) {
        PtrVector* const cached = At<PtrVector*>(node, 0x14);
        if (!cached)
            continue;
        if (cached->begin)
            Free(cached->begin);
        OperatorDelete(cached);
        At<PtrVector*>(node, 0x14) = nullptr;
    }
}

namespace {

// --- Loading ----------------------------------------------------------------------

// 0x00489910 (not virtual; also called by the lot configuration's code at
// 0x00483CAC, so detoured): a new configuration with one reference, loaded
// from the exemplar at `key`. When the load fails the configuration is
// released and its address stays in `*out`, as in the game. The game tests
// neither the allocation nor the configuration for null.
uint8_t __fastcall CreateLotConfigurationFromKey(char*, void*, const void* key, void** out)
{
    static bool logged = false;
    FirstCall("CreateLotConfigurationFromKey", logged);
    *out = nullptr;
    void* configuration = OperatorNew(config::kSize);
    if (configuration)
        configuration = ConstructConfiguration(configuration);
    *out = configuration;
    AddRefOf(configuration);
    // The option byte: the configuration's Load reads its second argument as
    // a byte (0x00479D13).
    const uint32_t option = At<uint8_t>(Global<void*>(addr::SC4_Options43CD8), 0xF16);
    const uint8_t loaded = Virtual<uint8_t>(*out, config::kLoad, key, option);
    // On success the game calls an empty stub (0x00648F00) on it.
    if (!loaded)
        ReleaseOf(*out);
    return loaded;
}

// 0x00489700 (not virtual; only called by the lot configuration's code at
// 0x0047AB91, so detoured): the configuration saved to a segment under a
// key (its slot 32).
uint8_t __fastcall SaveLotConfiguration(char*, void*, const void* key, void* configuration, void* segment)
{
    static bool logged = false;
    FirstCall("SaveLotConfiguration", logged);
    if (!configuration)
        return 0;
    return Virtual<uint8_t>(configuration, config::kSave, key, segment);
}

// Slot 5 (0x00489A60).
uint8_t __fastcall AddLotConfigurationByKey(char* self, void*, const void* key)
{
    static bool logged = false;
    FirstCall("AddLotConfiguration (key)", logged);
    void* configuration = nullptr;
    uint8_t added = 0;
    if (CreateLotConfigurationFromKey(self, nullptr, key, &configuration)) {
        added = Virtual<uint8_t>(self, slot::kAdd, configuration, 1u);
        ReleaseOf(configuration);
    }
    return added;
}

// Slot 6 (0x0048A690). Always true. A configuration whose ID is not in the
// map is added only with `add` (referenced; its families go into the family
// set, the filters' caches are freed). One whose ID is in the map replaces
// the old one with `add` (the caches stay) and is taken out without it: the
// pair is erased, the old configuration not released and the family set
// left as it is, as in the game.
uint8_t __fastcall AddLotConfiguration(char* self, void*, void* configuration, uint8_t add)
{
    static bool logged = false;
    FirstCall("AddLotConfiguration", logged);
    const uint32_t id = Virtual<uint32_t>(configuration, config::kGetId);
    HashTable* const map = Map();
    char* const node = FindConfigurationNode(id);
    InsertResult result{};
    if (!node) {
        if (!add)
            return 1;
        AddRefOf(configuration);
        if (const Vector32* const families = Virtual<Vector32*>(configuration, config::kGetFamilies)) {
            const int32_t count = static_cast<int32_t>(families->end - families->begin);
            HashTable* const set = FamilySet();
            for (int32_t i = 0; i < count; ++i) {
                ResizeIdTable(set, set->count + 1);
                InsertId(set, &result, families->begin + i);
            }
        }
        InvalidateFilterCaches(self);
        struct {
            uint32_t id;
            void* configuration;
        } pair{id, configuration};
        ResizeIdTable(map, map->count + 1);
        InsertPair(map, &result, &pair);
        return 1;
    }
    if (!add) {
        InvalidateFilterCaches(self);
        struct {
            char* node;
            HashTable* map;
        } iterator{node, map};
        reinterpret_cast<EraseAtFn>(addr::SC4Uint32HashMap_EraseAt)(map, &iterator);
        return 1;
    }
    ReleaseOf(At<void*>(node, 0x8));
    AddRefOf(configuration);
    At<void*>(node, 0x8) = configuration;
    return 1;
}

// Slot 7 (0x0048A2D0): false for an ID not in the map.
uint8_t __fastcall RemoveLotConfiguration(char* self, void*, uint32_t id)
{
    static bool logged = false;
    FirstCall("RemoveLotConfiguration", logged);
    char* const node = FindConfigurationNode(id);
    if (!node)
        return 0;
    return Virtual<uint8_t>(self, slot::kAdd, At<void*>(node, 0x8), 0u);
}

// 0x0048A240: the cleanup LoadAll registers under the game's address (the
// service compares the address to unregister it), so the game's function
// is detoured.
uint8_t __cdecl FreeConfigurations(void*)
{
    static bool logged = false;
    FirstCall("FreeConfigurations", logged);
    uint8_t& loaded = Global<uint8_t>(addr::cSC4LotConfigurationManager_Loaded);
    if (!loaded)
        return 1;
    void* const service = Global<void*>(addr::SC4_Service43C94);
    loaded = 0;
    Virtual<uint8_t>(service, 0x118, reinterpret_cast<void*>(addr::cSC4LotConfigurationManager_FreeConfigurations), 0u);
    HashTable* const map = Map();
    if (map->count) {
        for (char* node = FirstNode(map); node; node = NextNode(map, node, At<uint32_t>(node, 0x4)))
            ReleaseOf(At<void*>(node, 0x8));
        ClearTable(map);
    }
    return 1;
}

// 0x00489990: one exemplar key of the walk.
void __cdecl OnResourceKey(const uint32_t* key, void* manager)
{
    static bool logged = false;
    FirstCall("OnResourceKey", logged);
    if (key[1] == kConfigurationGroup)
        Virtual<uint8_t>(manager, slot::kAddByKey, key);
}

// 0x0048A540: the static configurations loaded once per option byte.
uint8_t LoadAll(char* self)
{
    static bool logged = false;
    FirstCall("LoadAll", logged);
    const uint32_t option = At<uint8_t>(Global<void*>(addr::SC4_Options43CD8), 0xF16);
    uint8_t& loaded = Global<uint8_t>(addr::cSC4LotConfigurationManager_Loaded);
    if (loaded) {
        if (Global<uint32_t>(addr::cSC4LotConfigurationManager_LoadedOption) == option)
            return 1;
        FreeConfigurations(nullptr);
    }
    void* const service = Global<void*>(addr::SC4_Service43C94);
    loaded = 1;
    Virtual<uint8_t>(service, 0x114, reinterpret_cast<void*>(addr::cSC4LotConfigurationManager_FreeConfigurations), 0u);
    if (void* const resources = Global<void*>(addr::SC4_ResourceManager)) {
        void* keys = nullptr;
        if (Virtual<uint32_t>(resources, 0x48, &keys, kExemplarType)) {
            Virtual<void>(keys, 0x1C, reinterpret_cast<void*>(&OnResourceKey), static_cast<void*>(self));
            ReleaseOf(keys);
        }
    }
    Global<uint32_t>(addr::cSC4LotConfigurationManager_LoadedOption) = option;
    return 1;
}

// Slot 3 (0x0048A5E0). Always true. A service the save gave (Read) is
// kept; the others come from the globals, referenced without a null test.
uint8_t __fastcall Init(char* self, void*)
{
    static bool logged = false;
    FirstCall("Init", logged);
    if (At<uint8_t>(self, field::kInitialized))
        return 1;
    At<uint8_t>(self, field::kInitialized) = 1;
    LoadAll(self);
    const struct {
        std::size_t offset;
        std::uintptr_t global;
    } services[] = {{field::kDevelopment, addr::SC4_Service43D68},
                    {field::kFlora, addr::SC4_FloraSimulator43D80},
                    {field::kProps, addr::SC4_Service43D10}};
    for (const auto& service : services) {
        if (At<void*>(self, service.offset))
            continue;
        void* const object = Global<void*>(service.global);
        At<void*>(self, service.offset) = object;
        AddRefOf(object);
    }
    if (void* const messages = Global<void*>(addr::SC4_MessageServer))
        Virtual<uint8_t>(messages, 0x14, Target(self), kMessageCityReady);
    return 1;
}

// Slot 4 (0x004897F0). Always true.
uint8_t __fastcall Shutdown(char* self, void*)
{
    static bool logged = false;
    FirstCall("Shutdown", logged);
    if (!At<uint8_t>(self, field::kInitialized))
        return 1;
    if (void* const messages = Global<void*>(addr::SC4_MessageServer))
        Virtual<uint8_t>(messages, 0x18, Target(self), kMessageCityReady);
    for (const std::size_t offset : {field::kDevelopment, field::kFlora, field::kProps}) {
        if (void* const object = At<void*>(self, offset)) {
            ReleaseOf(object);
            At<void*>(self, offset) = nullptr;
        }
    }
    At<uint8_t>(self, field::kInitialized) = 0;
    return 1;
}

// --- FixLotConfiguration ----------------------------------------------------------

// Slot 23 (0x0048B3B0). The kind of the first fix made, or 0:
//   1 an ID of an object that is no building (the development simulator's
//     slot 16), prop (the prop manager's slot 11) or flora (the flora
//     simulator's slot 12) type or family was removed;
//   2 an object left without IDs was removed;
//   3 an object's rectangle was resized to its footprint;
//   4 an object reaches outside the lot (only reported);
//   5 an object's centre was outside its rectangle and was moved;
//   6 a retaining wall type the development simulator has no key for was
//     removed (only the first, then the function returns).
// The x87 code keeps its sums at 53 bits before storing floats: doubles.
uint32_t __fastcall FixLotConfiguration(char* self, void*, void* configuration)
{
    static bool logged = false;
    FirstCall("FixLotConfiguration", logged);
    uint32_t code = 0;
    uint32_t width = 0, depth = 0;
    Virtual<void>(configuration, config::kGetSize, &width, &depth);
    const float lotWidth = static_cast<float>(width << 4);
    const float lotDepth = static_cast<float>(depth << 4);
    const float half = Global<float>(addr::SC4_FloatHalf);
    ObjectVector* const objects = Virtual<ObjectVector*>(configuration, config::kGetObjects);
    if (ObjectCount(objects) != 0) {
        uint32_t index = 0;
        std::ptrdiff_t offset = 0;
        do {
            char* const object = objects->begin + offset;
            const uint8_t kind = At<uint8_t>(object, 0x0);
            if (kind == 0 || kind == 4 || kind == 1) {
                Vector32* const ids = reinterpret_cast<Vector32*>(object + 0x28);
                if (static_cast<uint32_t>(ids->end - ids->begin) != 0) {
                    uint32_t i = 0;
                    do {
                        const uint32_t id = ids->begin[i];
                        uint8_t known = 1;
                        switch (At<uint8_t>(object, 0x0)) {
                        case 0: known = Virtual<uint8_t>(At<void*>(self, field::kDevelopment), 0x40, id); break;
                        case 1: known = Virtual<uint8_t>(At<void*>(self, field::kProps), 0x2C, id); break;
                        case 4: known = Virtual<uint8_t>(At<void*>(self, field::kFlora), 0x30, id); break;
                        default: break;
                        }
                        if (!known) {
                            uint32_t* const at = ids->begin + i;
                            if (ids->end != at + 1)
                                Move(at, at + 1,
                                     static_cast<std::size_t>(reinterpret_cast<char*>(ids->end) -
                                                              reinterpret_cast<char*>(at + 1)));
                            --ids->end;
                            --i;
                            if (!code)
                                code = 1;
                        }
                        ++i;
                    } while (i < static_cast<uint32_t>(ids->end - ids->begin));
                }
                if (ids->begin == ids->end) {
                    // The object goes: the later ones move down by
                    // assignment, the last one's ID list is freed.
                    char* const at = objects->begin + offset;
                    char* const next = at + config::kObjectSize;
                    uint8_t tag = 0;
                    if (next != objects->end)
                        reinterpret_cast<CopyObjectsFn>(addr::cSC4LotConfiguration_CopyObjects)(next, objects->end, at,
                                                                                                 &tag, 0u);
                    objects->end -= config::kObjectSize;
                    if (void* const list = At<void*>(objects->end, 0x28))
                        Free(list);
                    --index;
                    offset -= static_cast<std::ptrdiff_t>(config::kObjectSize);
                    if (!code)
                        code = 2;
                } else {
                    float rect[4] = {1.0f, 1.0f, 0.0f, 0.0f};
                    const float negMinX = -At<float>(object, 0x18);
                    const float objectWidth =
                        static_cast<float>(static_cast<double>(negMinX) + static_cast<double>(At<float>(object, 0x10)));
                    const float objectDepth = static_cast<float>(-static_cast<double>(At<float>(object, 0x1C)) +
                                                                 static_cast<double>(At<float>(object, 0x14)));
                    switch (At<uint8_t>(object, 0x0)) {
                    case 0: Virtual<void>(self, slot::kBuildingFootprint, ids, static_cast<float*>(rect)); break;
                    case 1: Virtual<void>(self, slot::kPropFootprint, ids, static_cast<float*>(rect)); break;
                    case 4: Virtual<void>(self, slot::kFloraFootprint, ids, static_cast<float*>(rect)); break;
                    default: break;
                    }
                    const uint8_t orientation = At<uint8_t>(object, 0x2);
                    if (orientation == 1 || orientation == 3) {
                        const float swapped = rect[0];
                        rect[0] = rect[1];
                        rect[1] = swapped;
                    }
                    const double offX = std::fabs(static_cast<double>(objectWidth) - static_cast<double>(rect[0]));
                    const float offZ =
                        static_cast<float>(std::fabs(static_cast<double>(objectDepth) - static_cast<double>(rect[1])));
                    const float zero = Global<float>(addr::SC4_FloatZero);
                    if (offX > static_cast<double>(zero) || offZ > zero) {
                        const float halfZ = static_cast<float>(static_cast<double>(rect[1]) * half);
                        const float halfX = static_cast<float>(static_cast<double>(rect[0]) * half);
                        const double x = At<float>(object, 0x4), z = At<float>(object, 0xC);
                        const float maxZ = static_cast<float>(static_cast<double>(halfZ) + z);
                        const float maxX = static_cast<float>(static_cast<double>(halfX) + x);
                        const float minZ = static_cast<float>(z - static_cast<double>(halfZ));
                        const float minX = static_cast<float>(x - static_cast<double>(halfX));
                        At<float>(object, 0x18) = minX;
                        At<float>(object, 0x1C) = minZ;
                        At<float>(object, 0x10) = maxX;
                        At<float>(object, 0x14) = maxZ;
                        if (!code)
                            code = 3;
                    }
                    const double margin = Global<float>(addr::cSC4LotConfigurationManager_FloatMargin);
                    const float limitX = static_cast<float>(static_cast<double>(lotWidth) - margin);
                    const float limitZ = static_cast<float>(static_cast<double>(lotDepth) - margin);
                    const float minEdge = Global<float>(addr::cSC4LotConfigurationManager_FloatMinEdge);
                    const double epsilon = Global<float>(addr::FloraTuning_HeightEpsilon);
                    if (At<float>(object, 0x18) < minEdge || At<float>(object, 0x1C) < minEdge ||
                        static_cast<double>(limitX) + epsilon < static_cast<double>(At<float>(object, 0x10)) ||
                        static_cast<double>(limitZ) + epsilon < static_cast<double>(At<float>(object, 0x14))) {
                        if (!code)
                            code = 4;
                    }
                    if (At<float>(object, 0x4) < At<float>(object, 0x18) ||
                        At<float>(object, 0x4) > At<float>(object, 0x10) ||
                        At<float>(object, 0xC) < At<float>(object, 0x1C) ||
                        At<float>(object, 0xC) > At<float>(object, 0x14)) {
                        At<float>(object, 0x4) = static_cast<float>(
                            (static_cast<double>(At<float>(object, 0x10)) + At<float>(object, 0x18)) * half);
                        At<float>(object, 0xC) = static_cast<float>(
                            (static_cast<double>(At<float>(object, 0x1C)) + At<float>(object, 0x14)) * half);
                        if (!code)
                            code = 5;
                    }
                }
            }
            offset += static_cast<std::ptrdiff_t>(config::kObjectSize);
            ++index;
        } while (index < ObjectCount(objects));
    }
    if (Vector32* const walls = Virtual<Vector32*>(configuration, config::kGetRetainingWalls)) {
        uint32_t key[3] = {0, 0, 0};
        if (static_cast<uint32_t>(walls->end - walls->begin) != 0) {
            uint32_t i = 0;
            do {
                if (!Virtual<uint8_t>(At<void*>(self, field::kDevelopment), 0x70, walls->begin[i],
                                      static_cast<uint32_t*>(key))) {
                    uint32_t* const at = walls->begin + i;
                    if (walls->end != at + 1)
                        Move(at, at + 1,
                             static_cast<std::size_t>(reinterpret_cast<char*>(walls->end) -
                                                      reinterpret_cast<char*>(at + 1)));
                    --walls->end;
                    return code ? code : 6;
                }
                ++i;
            } while (i < static_cast<uint32_t>(walls->end - walls->begin));
        }
    }
    return code;
}

// --- CreateGenericLotConfiguration --------------------------------------------------

// Slot 22 (0x0048BCF0). False (and null) for an ID the development
// simulator does not know (its slot 16). The configuration: as many cells
// as the building's footprint needs plus one each way, slopes 90 / 0 / 5,
// growth stage 1, no zone, wealth or purpose bits, one building object in
// the middle with the type as its only ID. It is not added to the map. Its
// ID is a new GUID from the framework's COM object (slot 7).
uint8_t __fastcall CreateGenericLotConfiguration(char* self, void*, void** out, uint32_t type)
{
    static bool logged = false;
    FirstCall("CreateGenericLotConfiguration", logged);
    *out = nullptr;
    if (!Virtual<uint8_t>(At<void*>(self, field::kDevelopment), 0x40, type))
        return 0;
    float rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    Vector32 types{};
    const auto InsertN = reinterpret_cast<InsertNFn>(addr::SC4PtrVector_InsertN);
    InsertN(&types, nullptr, &type, &type, 1u, 1u);
    Virtual<void>(self, slot::kBuildingFootprint, &types, static_cast<float*>(rect));
    // _ftol2 (0x009EEF04) truncates; the products are exact (-1/16).
    const double perCell = Global<float>(addr::cSC4LotConfigurationManager_FloatMinusSixteenth);
    const int32_t cellsX = 1 - static_cast<int32_t>(static_cast<double>(rect[0]) * perCell);
    const int32_t cellsZ = 1 - static_cast<int32_t>(static_cast<double>(rect[1]) * perCell);

    void* made = OperatorNew(config::kSize);
    if (made)
        made = ConstructConfiguration(made);
    char* const configuration = static_cast<char*>(made);
    *out = configuration;
    AddRefOf(configuration);
    // The game's stack word is unset before the call; 0 here.
    uint32_t guid = 0;
    void* const com = reinterpret_cast<GetFn>(addr::RZGetCOM)();
    Virtual<uint8_t>(com, 0x1C, &guid);
    At<uint32_t>(configuration, 0x8) = guid;
    At<uint8_t>(configuration, 0x20) = static_cast<uint8_t>(cellsX);
    At<uint8_t>(configuration, 0x21) = static_cast<uint8_t>(cellsZ);
    At<void*>(configuration, 0x28) = At<void*>(configuration, 0x24); // no families
    At<float>(configuration, 0x38) = 5.0f;
    At<float>(configuration, 0x30) = 90.0f;
    At<uint32_t>(configuration, 0x34) = 0;
    At<uint8_t>(configuration, 0x3C) = 1;
    At<uint8_t>(configuration, 0x42) = 0;
    // The three bit vectors (zone, wealth and purpose types) emptied.
    const auto EraseBits = reinterpret_cast<EraseBitsFn>(addr::SC4BitVector_Erase);
    for (const std::size_t bits : {std::size_t{0x44}, std::size_t{0x58}, std::size_t{0x6C}}) {
        uint32_t result[2] = {};
        EraseBits(configuration + bits, result, At<uint32_t*>(configuration, bits),
                  At<uint32_t>(configuration, bits + 4), At<uint32_t*>(configuration, bits + 8),
                  At<uint32_t>(configuration, bits + 0xC));
    }
    char* const objects = configuration + 0x90;
    reinterpret_cast<EraseRangeFn>(addr::cSC4LotConfiguration_EraseObjects)(objects, At<void*>(objects, 0x0),
                                                                              At<void*>(objects, 0x4));
    At<void*>(configuration, 0xA0) = At<void*>(configuration, 0x9C);
    At<void*>(configuration, 0xAC) = At<void*>(configuration, 0xA8);
    At<void*>(configuration, 0xB8) = At<void*>(configuration, 0xB4);

    // The building object. The game leaves its +0x24 (the object's ID) as
    // the stack had it; 0 here.
    alignas(4) char object[config::kObjectSize] = {};
    const double half = Global<float>(addr::SC4_FloatHalf);
    const int32_t metersX = static_cast<int32_t>(static_cast<uint32_t>(cellsX) << 4);
    const int32_t metersZ = static_cast<int32_t>(static_cast<uint32_t>(cellsZ) << 4);
    const float centreX = static_cast<float>(static_cast<double>(metersX) * half);
    const float centreZ = static_cast<float>(static_cast<double>(metersZ) * half);
    const double halfX = static_cast<double>(rect[0]) * half;
    const double halfZ = static_cast<double>(rect[1]) * half;
    At<float>(object, 0x4) = centreX;
    At<float>(object, 0x8) = 0.0f;
    At<float>(object, 0xC) = centreZ;
    At<float>(object, 0x10) = static_cast<float>(halfX + static_cast<double>(centreX));
    At<float>(object, 0x14) = static_cast<float>(halfZ + static_cast<double>(centreZ));
    At<float>(object, 0x18) = static_cast<float>(static_cast<double>(centreX) - halfX);
    At<float>(object, 0x1C) = static_cast<float>(static_cast<double>(centreZ) - halfZ);
    reinterpret_cast<AssignFn>(addr::SC4Uint32Vector_CopyAssign)(object + 0x28, &types);
    char* const end = At<char*>(objects, 0x4);
    if (end != At<char*>(objects, 0x8)) {
        if (end)
            reinterpret_cast<CopyObjectFn>(addr::cSC4LotConfiguration_CopyObject)(end, object);
        At<char*>(objects, 0x4) += config::kObjectSize;
    } else {
        using InsertObjectsFn = void(__thiscall*)(void*, void*, const void*, void*, uint32_t, uint32_t);
        reinterpret_cast<InsertObjectsFn>(addr::cSC4LotConfiguration_InsertObjects)(objects, end, object, &type, 1u,
                                                                                      1u);
    }
    if (void* const list = At<void*>(object, 0x28))
        Free(list);
    if (types.begin)
        Free(types.begin);
    return 1;
}

const Slot kLoadSlots[] = {
    {addr::cSC4LotConfigurationManager_vftable, 3, addr::cSC4LotConfigurationManager_Init,
     reinterpret_cast<void*>(&Init)},
    {addr::cSC4LotConfigurationManager_vftable, 4, addr::cSC4LotConfigurationManager_Shutdown,
     reinterpret_cast<void*>(&Shutdown)},
    {addr::cSC4LotConfigurationManager_vftable, 5, addr::cSC4LotConfigurationManager_AddLotConfigurationByKey,
     reinterpret_cast<void*>(&AddLotConfigurationByKey)},
    {addr::cSC4LotConfigurationManager_vftable, 6, addr::cSC4LotConfigurationManager_AddLotConfiguration,
     reinterpret_cast<void*>(&AddLotConfiguration)},
    {addr::cSC4LotConfigurationManager_vftable, 7, addr::cSC4LotConfigurationManager_RemoveLotConfiguration,
     reinterpret_cast<void*>(&RemoveLotConfiguration)},
    {addr::cSC4LotConfigurationManager_vftable, 22, addr::cSC4LotConfigurationManager_CreateGenericLotConfiguration,
     reinterpret_cast<void*>(&CreateGenericLotConfiguration)},
    {addr::cSC4LotConfigurationManager_vftable, 23, addr::cSC4LotConfigurationManager_FixLotConfiguration,
     reinterpret_cast<void*>(&FixLotConfiguration)},
};

struct Detour {
    std::uintptr_t address;
    void* ours;
    void* original;
};

// The three functions game code calls directly. Their callers (0x00483CAC,
// 0x0047AB91, the service's callback call and the manager itself) pass
// their arguments by the plain conventions and keep nothing in ECX, EDX or
// EAX across the calls: plain detours.
Detour g_loadDetours[] = {
    {addr::cSC4LotConfigurationManager_CreateLotConfigurationFromKey,
     reinterpret_cast<void*>(&CreateLotConfigurationFromKey), nullptr},
    {addr::cSC4LotConfigurationManager_SaveLotConfiguration, reinterpret_cast<void*>(&SaveLotConfiguration), nullptr},
    {addr::cSC4LotConfigurationManager_FreeConfigurations, reinterpret_cast<void*>(&FreeConfigurations), nullptr},
};

bool InstallLoad(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : kLoadSlots) {
        if (reinterpret_cast<const std::uintptr_t*>(slot.vtable)[slot.index] != slot.original) {
            log::Write("lotconfig load: slot %d holds %08X, expected %08X; not installing", slot.index,
                       static_cast<unsigned>(reinterpret_cast<const std::uintptr_t*>(slot.vtable)[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (Detour& detour : g_loadDetours) {
        const addr::Entry* const entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, detour.ours, &detour.original)) {
            log::Write("lotconfig load: no detour at %08X", static_cast<unsigned>(detour.address));
            return false;
        }
    }
    return SwapSlots(kLoadSlots, sizeof(kLoadSlots) / sizeof(kLoadSlots[0]), "load");
}

// --- The message ------------------------------------------------------------------

struct Tree {
    char* head;
    uint32_t count;
};

// 0x0048AD40, for message 0x26D31EC1 (the city is ready, so the development
// simulator knows its buildings): what a configuration cannot know from its
// own exemplar. Over the building types its building object stands for (its
// IDs through the development simulator's slot 19: a type itself, a
// family's types): the least and greatest capacity (the profile's +0x8;
// both 0 when there is no building object, no ID or no type) into +0x3E and
// +0x40, the sorted union of the uint32 lists SC4::LoadUint32List reads
// from the buildings' exemplars into the vector +0x80 (what the
// configuration's slot 18 searches), and into +0x8C whether some building's
// exemplar is missing or lacks a true property 0x2781283F.
void UpdateConfiguration(char* self, char* configuration)
{
    static bool logged = false;
    FirstCall("UpdateConfiguration", logged);
    uint32_t least = 0xFFFF, most = 0;
    Tree values{};
    values.head = static_cast<char*>(Allocate(0x14));
    values.count = 0;
    At<uint8_t>(values.head, 0x0) = 0;
    At<char*>(values.head, 0x4) = nullptr;
    At<char*>(values.head, 0x8) = values.head;
    At<char*>(values.head, 0xC) = values.head;
    Vector32 list{};
    uint8_t unflagged = 0;
    bool ranged = false;
    char* const building = Virtual<char*>(configuration, config::kGetBuildingObject);
    if (building && At<uint32_t*>(building, 0x28) != At<uint32_t*>(building, 0x2C)) {
        uint32_t* cursor = At<uint32_t*>(building, 0x28);
        do {
            const uint32_t id = *cursor;
            HashTable types{};
            reinterpret_cast<CountFn>(addr::SC4Uint32HashSet_Construct)(&types, 100);
            Virtual<void>(At<void*>(self, field::kDevelopment), 0x4C, &types, id);
            for (char* node = FirstNode(&types); node; node = NextNode(&types, node, At<uint32_t>(node, 0x4))) {
                const uint32_t type = At<uint32_t>(node, 0x4);
                uint32_t key[3] = {0, 0, 0};
                Virtual<void>(At<void*>(self, field::kDevelopment), 0x38, type, static_cast<uint32_t*>(key));
                reinterpret_cast<LoadListFn>(addr::SC4_LoadUint32List)(key, &list);
                uint32_t* const last = list.end;
                for (uint32_t* value = list.begin; value != last; ++value) {
                    void* inserted[2] = {};
                    reinterpret_cast<InsertFn>(addr::SC4PtrSet_Insert)(&values, inserted, value);
                }
                uint8_t flag = 0;
                void* exemplar = nullptr;
                if (Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, static_cast<uint32_t*>(key),
                                     kExemplarIid, &exemplar, 0u, 0u)) {
                    // The exemplar's property holder (its slot 7), asked for twice.
                    Virtual<void*>(exemplar, 0x1C);
                    reinterpret_cast<PropertyBoolFn>(addr::SC4_GetPropertyBool)(Virtual<void*>(exemplar, 0x1C),
                                                                                 0x2781283Fu, &flag);
                }
                if (!flag)
                    unflagged = 1;
                const uint32_t capacity =
                    At<uint16_t>(Virtual<char*>(At<void*>(self, field::kDevelopment), 0x9C, type), 0x8);
                if (static_cast<uint16_t>(capacity) < static_cast<uint16_t>(least))
                    least = capacity;
                if (static_cast<uint16_t>(capacity) > static_cast<uint16_t>(most))
                    most = capacity;
                if (exemplar)
                    ReleaseOf(exemplar);
            }
            ClearTable(&types);
            if (types.buckets)
                Free(types.buckets);
            ++cursor;
        } while (cursor != At<uint32_t*>(building, 0x2C));
        ranged = static_cast<uint16_t>(least) <= static_cast<uint16_t>(most);
    }
    if (!ranged) {
        most = 0;
        least = 0;
    }
    At<uint16_t>(configuration, 0x3E) = static_cast<uint16_t>(least);
    At<uint16_t>(configuration, 0x40) = static_cast<uint16_t>(most);
    Vector32* const out = reinterpret_cast<Vector32*>(configuration + 0x80);
    out->end = out->begin;
    for (char* node = At<char*>(values.head, 0x8); node != values.head; node = TreeNext(node)) {
        const uint32_t value = At<uint32_t>(node, 0x10);
        if (out->end != out->capacity) {
            if (out->end)
                *out->end = value;
            ++out->end;
            continue;
        }
        // The vector grows as the game's inline code: twice the size (1 for
        // an empty one) from the STL allocator.
        const uint32_t size = static_cast<uint32_t>(out->end - out->begin);
        const uint32_t capacity = size + (size < 1 ? 1 : size);
        uint32_t* const grown = capacity ? static_cast<uint32_t*>(Allocate(capacity * 4)) : nullptr;
        uint32_t* at = grown;
        if (out->end != out->begin) {
            const std::size_t bytes =
                static_cast<std::size_t>(reinterpret_cast<char*>(out->end) - reinterpret_cast<char*>(out->begin));
            at = reinterpret_cast<uint32_t*>(static_cast<char*>(Move(grown, out->begin, bytes)) + bytes);
        }
        *at = value;
        if (out->begin)
            Free(out->begin);
        out->begin = grown;
        out->end = at + 1;
        out->capacity = grown + capacity;
    }
    At<uint8_t>(configuration, 0x8C) = unflagged;
    if (list.begin)
        Free(list.begin);
    if (values.count) {
        for (char* node = At<char*>(values.head, 0x4); node;) {
            reinterpret_cast<NodeFn>(addr::SC4Uint32Set_EraseNodes)(&values, At<void*>(node, 0xC));
            char* const left = At<char*>(node, 0x8);
            Free(node);
            node = left;
        }
        At<char*>(values.head, 0x8) = values.head;
        At<char*>(values.head, 0x4) = nullptr;
        At<char*>(values.head, 0xC) = values.head;
        values.count = 0;
    }
    Free(values.head);
}

// cIGZMessageTarget2 slot 3 (0x0048BC50), on the part at +0x4. Always true.
uint8_t __fastcall DoMessage(char* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    char* const self = part - kMessagePart;
    if (Virtual<uint32_t>(message, 0x10) != kMessageCityReady)
        return 1;
    const HashTable* const map = Map();
    for (char* node = FirstNode(map); node; node = NextNode(map, node, At<uint32_t>(node, 0x4)))
        UpdateConfiguration(self, At<char*>(node, 0x8));
    return 1;
}

const Slot kMessageSwaps[] = {
    {addr::cSC4LotConfigurationManager_vftable_4, 3, addr::cSC4LotConfigurationManager_DoMessage,
     reinterpret_cast<void*>(&DoMessage)},
};

bool InstallMessages(registry::Mode) { return SwapSlots(kMessageSwaps, 1, "messages"); }

// --- Save -------------------------------------------------------------------------

// cIGZSerializable slot 3 (0x00489850), on the part at +0x8: version 1 and
// the references of the three services. False when the stream is in error
// after.
uint8_t __fastcall Write(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Write", logged);
    char* const self = part - kSerializablePart;
    Virtual<uint8_t>(stream, 0x1C, static_cast<uint16_t>(1));
    WriteRef(stream, At<void*>(self, field::kDevelopment));
    WriteRef(stream, At<void*>(self, field::kFlora));
    WriteRef(stream, At<void*>(self, field::kProps));
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

// cIGZSerializable slot 4 (0x004898A0): version 1 only (false otherwise).
// The game reads the version into the stack slot of its argument, so a
// read that leaves it unset compares the stream address's low word.
uint8_t __fastcall Read(char* part, void*, void* stream)
{
    static bool logged = false;
    FirstCall("Read", logged);
    char* const self = part - kSerializablePart;
    uint16_t version = static_cast<uint16_t>(reinterpret_cast<std::uintptr_t>(stream));
    Virtual<uint8_t>(stream, 0x1C, &version);
    if (version != 1)
        return 0;
    ReadRef(stream, kIidDevelopment, self + field::kDevelopment, 0u);
    ReadRef(stream, kIidFlora, self + field::kFlora, 0u);
    ReadRef(stream, kIidProps, self + field::kProps, 0u);
    return Virtual<uint8_t>(stream, 0x48) ? 0 : 1;
}

const Slot kSaveSlots[] = {
    {addr::cSC4LotConfigurationManager_vftable_8, 3, addr::cSC4LotConfigurationManager_Write,
     reinterpret_cast<void*>(&Write)},
    {addr::cSC4LotConfigurationManager_vftable_8, 4, addr::cSC4LotConfigurationManager_Read,
     reinterpret_cast<void*>(&Read)},
};

bool InstallSave(registry::Mode) { return SwapSlots(kSaveSlots, 2, "save"); }

const bool g_registered[] = {
    registry::Register({"simulators.lotconfig.load", "simulators/lotconfig", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4LotConfigurationManager_vftable,
                        "Our lot configuration manager Init, Shutdown, the loading of the configurations, Add, Remove, "
                        "FixLotConfiguration and CreateGenericLotConfiguration, with detours of the three functions "
                        "called directly (src/core/lotconfig_sim.cpp)",
                        false, InstallLoad}),
    registry::Register({"simulators.lotconfig.messages", "simulators/lotconfig", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4LotConfigurationManager_vftable_4,
                        "Our lot configuration manager DoMessage: capacities and lists of every configuration when "
                        "the city is ready (src/core/lotconfig_sim.cpp)",
                        false, InstallMessages}),
    registry::Register({"simulators.lotconfig.save", "simulators/lotconfig", registry::Kind::Vtable,
                        registry::Phase::PostAppInit, addr::cSC4LotConfigurationManager_vftable_8,
                        "Our lot configuration manager Write and Read: the three services, version 1 "
                        "(src/core/lotconfig_sim.cpp)",
                        false, InstallSave}),
};

// --- Our object -------------------------------------------------------------------

// Copies of the game's 0x00A8EC58 (29 slots, deleting destructor last),
// 0x00A8EC48 (at +4) and 0x00A8EC30 (at +8), taken at the first
// construction: a city is made long after PostAppInit, so the other
// groups' slot swaps are in them.
void* g_vtable[kSlots];
void* g_vtable4[kMessageSlots];
void* g_vtable8[kSerializableSlots];

// As the game's (0x004F7BA0, shared with other classes).
uint32_t __fastcall AddRef(char* self, void*) { return ++At<uint32_t>(self, field::kRefCount); }

// As the game's (0x004896A0). The game's null test of `this` for the part
// cannot be met.
uint8_t __fastcall QueryInterface(char* self, void*, uint32_t iid, void** object)
{
    std::size_t offset;
    switch (iid) {
    case kIidUnknown:
    case kIidManager: offset = 0; break;
    case kIidSerializable: offset = kSerializablePart; break;
    default: return 0;
    }
    *object = self + offset;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// As the game's (0x004897C0): Shutdown and the deleting destructor (slot
// 28) at the last reference.
uint32_t __fastcall ReleaseObject(char* self, void*)
{
    uint32_t& refs = At<uint32_t>(self, field::kRefCount);
    if (refs > 1)
        return --refs;
    Virtual<uint8_t>(self, 0x10);
    Virtual<void*>(self, 0x70, 1u);
    return 0;
}

// 0x004899B0: the filter map's nodes below a node freed (the cached
// vectors are not, as in the game).
void EraseFilterNodes(char* node)
{
    while (node) {
        EraseFilterNodes(At<char*>(node, 0xC));
        char* const left = At<char*>(node, 0x8);
        Free(node);
        node = left;
    }
}

// The destructor (0x00489D70): Shutdown (called directly by the game),
// then the filter map.
void Destroy(char* self)
{
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, kMessagePart) = g_vtable4;
    At<void**>(self, kSerializablePart) = g_vtable8;
    Shutdown(self, nullptr);
    char* const tree = self + field::kFilters;
    if (At<uint32_t>(tree, 0x4)) {
        char* const head = At<char*>(tree, 0x0);
        EraseFilterNodes(At<char*>(head, 0x4));
        At<char*>(head, 0x8) = head;
        At<char*>(head, 0x4) = nullptr;
        At<char*>(head, 0xC) = head;
        At<uint32_t>(tree, 0x4) = 0;
    }
    if (void* const head = At<void*>(tree, 0x0))
        Free(head);
}

// Slot 28, as the game's (0x0048A220).
void* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static int destroyed = 0;
    if (destroyed++ < 3)
        log::Write("lotconfig object: destroying %p", static_cast<void*>(self));
    Destroy(self);
    if (flags & 1)
        OperatorDelete(self);
    return self;
}

// As the game's (0x00489650).
uint32_t __fastcall GetGZCLSID(void*, void*) { return kClassId; }

template <std::size_t Offset>
uint8_t __fastcall QueryInterfaceAt(char* part, void*, uint32_t iid, void** object)
{
    return QueryInterface(part - Offset, nullptr, iid, object);
}

template <std::size_t Offset>
uint32_t __fastcall AddRefAt(char* part, void*)
{
    return AddRef(part - Offset, nullptr);
}

template <std::size_t Offset>
uint32_t __fastcall ReleaseAt(char* part, void*)
{
    return ReleaseObject(part - Offset, nullptr);
}

void BuildVtables()
{
    std::memcpy(g_vtable, reinterpret_cast<const void*>(addr::cSC4LotConfigurationManager_vftable), sizeof(g_vtable));
    std::memcpy(g_vtable4, reinterpret_cast<const void*>(addr::cSC4LotConfigurationManager_vftable_4),
                sizeof(g_vtable4));
    std::memcpy(g_vtable8, reinterpret_cast<const void*>(addr::cSC4LotConfigurationManager_vftable_8),
                sizeof(g_vtable8));
    g_vtable[0] = reinterpret_cast<void*>(&QueryInterface);
    g_vtable[1] = reinterpret_cast<void*>(&AddRef);
    g_vtable[2] = reinterpret_cast<void*>(&ReleaseObject);
    g_vtable[28] = reinterpret_cast<void*>(&DeletingDestructor);
    g_vtable4[0] = reinterpret_cast<void*>(&QueryInterfaceAt<kMessagePart>);
    g_vtable4[1] = reinterpret_cast<void*>(&AddRefAt<kMessagePart>);
    g_vtable4[2] = reinterpret_cast<void*>(&ReleaseAt<kMessagePart>);
    g_vtable8[0] = reinterpret_cast<void*>(&QueryInterfaceAt<kSerializablePart>);
    g_vtable8[1] = reinterpret_cast<void*>(&AddRefAt<kSerializablePart>);
    g_vtable8[2] = reinterpret_cast<void*>(&ReleaseAt<kSerializablePart>);
    g_vtable8[5] = reinterpret_cast<void*>(&GetGZCLSID);
    log::Write("lotconfig object: vtables copied (slots 3/6/8/24 %p %p %p %p, +4 slot 3 %p, +8 slots 3/4 %p %p)",
               g_vtable[3], g_vtable[6], g_vtable[8], g_vtable[24], g_vtable4[3], g_vtable8[3], g_vtable8[4]);
}

// The constructor (detour of 0x0048A1B0, reached only from the factory
// 0x0048A520): the fields the game's sets; +0x28 stays unset.
void* __fastcall Construct(char* self, void*)
{
    static bool built = false;
    if (!built) {
        built = true;
        BuildVtables();
    }
    At<void**>(self, 0x0) = g_vtable;
    At<void**>(self, kMessagePart) = g_vtable4;
    At<void**>(self, kSerializablePart) = g_vtable8;
    At<uint8_t>(self, field::kInitialized) = 0;
    At<uint32_t>(self, field::kRefCount) = 0;
    At<void*>(self, field::kDevelopment) = nullptr;
    At<void*>(self, field::kFlora) = nullptr;
    At<void*>(self, field::kProps) = nullptr;
    At<char*>(self, field::kFilters) = nullptr;
    char* const head = static_cast<char*>(Allocate(0x18));
    At<char*>(self, field::kFilters) = head;
    At<uint32_t>(self, field::kFilterCount) = 0;
    At<uint8_t>(head, 0x0) = 0;
    At<char*>(head, 0x4) = nullptr;
    At<char*>(head, 0x8) = head;
    At<char*>(head, 0xC) = head;
    static int created = 0;
    if (created++ < 3)
        log::Write("lotconfig object: constructed %p", static_cast<void*>(self));
    return self;
}

using ConstructFn = void*(__fastcall*)(char*, void*);
ConstructFn g_originalConstruct = nullptr;

bool ApplyObject(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const addr::Entry* const entry = addr_check::Find(addr::cSC4LotConfigurationManager_cSC4LotConfigurationManager);
    return entry && hook::Install(*entry, &Construct, &g_originalConstruct);
}

const bool g_registeredObject = registry::Register({
    "simulators.lotconfig.object",
    "simulators/lotconfig",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4LotConfigurationManager_cSC4LotConfigurationManager,
    "Our cSC4LotConfigurationManager object: constructor (detour), destructor, identity; methods from copies of "
    "the (patched) game vtables (src/core/lotconfig_sim.cpp)",
    false,
    ApplyObject,
});

} // namespace
} // namespace opensc4::lotconfig_sim
