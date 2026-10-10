// The lot configuration manager's part of the autotest
// (src/core/autotest_hooks.h).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/lotconfig_sim.h"

#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace opensc4::autotest {
namespace {

using lotconfig_sim::FirstNode;
using lotconfig_sim::HashTable;
using lotconfig_sim::NextNode;
using lotconfig_sim::ObjectVector;
using lotconfig_sim::Vector32;
namespace field = lotconfig_sim::field;
namespace slot = lotconfig_sim::slot;
namespace config = lotconfig_sim::config;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
    void MixVector(const Vector32* vector)
    {
        Mix(static_cast<uint32_t>(vector->end - vector->begin));
        for (const uint32_t* value = vector->begin; value != vector->end; ++value)
            Mix(*value);
    }
};

using FreeFn = void(__cdecl*)(void* block);
using CountFn = void(__thiscall*)(void* container, uint32_t count);
using ClearFn = uint32_t(__thiscall*)(void* table);
using SelfFn = void*(__thiscall*)(void* self);

void FreeBlock(void* block) { reinterpret_cast<FreeFn>(addr::SC4_Free)(block); }

// One of the configuration's bit vectors (std::vector<bool>: the first and
// the last bit as (word, bit) pairs): the bit count and the bits.
void MixBits(Hash& hash, char* configuration, std::size_t offset)
{
    const uint32_t* const firstWord = At<uint32_t*>(configuration, offset);
    const uint32_t firstBit = At<uint32_t>(configuration, offset + 4);
    const uint32_t* const lastWord = At<uint32_t*>(configuration, offset + 8);
    const uint32_t lastBit = At<uint32_t>(configuration, offset + 0xC);
    uint32_t count = 0;
    if (firstWord && lastWord)
        count = static_cast<uint32_t>(lastWord - firstWord) * 32 + lastBit - firstBit;
    hash.Mix(count);
    if (count > 1024)
        return;
    uint32_t packed = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t bit = firstBit + i;
        packed = (packed << 1) | ((firstWord[bit / 32] >> (bit % 32)) & 1);
        if (i % 32 == 31 || i + 1 == count) {
            hash.Mix(packed);
            packed = 0;
        }
    }
}

// One configuration's observable state: its ID and size through the
// getters, the fields of the other getters (slopes, growth stage, the
// capacities and lists the manager's message fills, the flags), its family,
// retaining wall and fence lists, the three bit vectors and every object
// (kind, flags, the seven floats, +0x20, the ID list). Not an object's ID
// (+0x24): CreateGenericLotConfiguration leaves it unset in the game.
uint32_t ConfigurationHash(char* configuration)
{
    Hash hash;
    hash.Mix(Virtual<uint32_t>(configuration, config::kGetId));
    uint32_t width = 0, depth = 0;
    Virtual<void>(configuration, config::kGetSize, &width, &depth);
    hash.Mix(width | (depth << 16));
    for (const std::size_t offset : {std::size_t{0x30}, std::size_t{0x34}, std::size_t{0x38}})
        hash.Mix(At<uint32_t>(configuration, offset));
    hash.Mix(At<uint8_t>(configuration, 0x3C) | (uint32_t{At<uint8_t>(configuration, 0x42)} << 8) |
             (uint32_t{At<uint8_t>(configuration, 0x8C)} << 16) | (uint32_t{At<uint8_t>(configuration, 0x8D)} << 24));
    hash.Mix(At<uint16_t>(configuration, 0x3E) | (uint32_t{At<uint16_t>(configuration, 0x40)} << 16));
    for (const std::size_t offset : {std::size_t{0x24}, std::size_t{0x80}, std::size_t{0x9C}, std::size_t{0xA8}})
        hash.MixVector(reinterpret_cast<const Vector32*>(configuration + offset));
    const uint8_t* const fenceBegin = At<uint8_t*>(configuration, 0xB4);
    const uint8_t* const fenceEnd = At<uint8_t*>(configuration, 0xB8);
    hash.Mix(static_cast<uint32_t>(fenceEnd - fenceBegin));
    for (const uint8_t* value = fenceBegin; value != fenceEnd; ++value)
        hash.Mix(*value);
    for (const std::size_t offset : {std::size_t{0x44}, std::size_t{0x58}, std::size_t{0x6C}})
        MixBits(hash, configuration, offset);
    const ObjectVector* const objects = reinterpret_cast<const ObjectVector*>(configuration + 0x90);
    const uint32_t count = lotconfig_sim::ObjectCount(objects);
    hash.Mix(count);
    for (uint32_t i = 0; i < count; ++i) {
        char* const object = objects->begin + static_cast<std::size_t>(i) * config::kObjectSize;
        for (std::size_t offset = 0x0; offset <= 0x20; offset += 4)
            hash.Mix(At<uint32_t>(object, offset));
        hash.MixVector(reinterpret_cast<const Vector32*>(object + 0x28));
    }
    return hash.h;
}

// A hash set for the queries, made and freed as the game's callers do.
struct Set {
    HashTable table{};
    Set() { reinterpret_cast<CountFn>(addr::SC4Uint32HashSet_Construct)(&table, 100); }
    ~Set()
    {
        reinterpret_cast<ClearFn>(addr::SC4Uint32HashSet_Clear)(&table);
        if (table.buckets)
            FreeBlock(table.buckets);
    }
    Set(const Set&) = delete;
    Set& operator=(const Set&) = delete;
};

// A set of IDs in its own order (bucket order: the same in two runs).
void MixIdSet(Hash& hash, const HashTable* set, uint32_t returned)
{
    hash.Mix(returned);
    hash.Mix(set->count);
    for (char* node = FirstNode(set); node; node = NextNode(set, node, At<uint32_t>(node, 0x4)))
        hash.Mix(At<uint32_t>(node, 0x4));
}

// A set of configurations: their IDs without their order (the set is
// keyed by the configurations' addresses), then released through the
// manager's slot 12.
void MixConfigurationSet(Hash& hash, void* manager, HashTable* set, uint32_t returned)
{
    uint32_t sum = 0, mixed = 0;
    for (char* node = FirstNode(set); node; node = NextNode(set, node, At<uint32_t>(node, 0x4) >> 2)) {
        Hash one;
        one.Mix(Virtual<uint32_t>(At<void*>(node, 0x4), config::kGetId));
        sum += one.h;
        mixed ^= one.h;
    }
    hash.Mix(returned);
    hash.Mix(set->count);
    hash.Mix(sum);
    hash.Mix(mixed);
    Virtual<uint32_t>(manager, slot::kClearSet, set);
    hash.Mix(set->count);
}

// OPENSC4_TEST_LOTCONFIG=1: the query slots, each day, with their results
// in a checksum. Pure queries: the sets and lists are the test's own.
//   - the two container getters; GetLotConfiguration for every 7th ID of
//     the map and for 0xFFFFFFFF;
//   - by family, for up to 16 families spread over the family set: the IDs
//     (slot 14) and the configurations (slot 10);
//   - by size, 1 to 4 cells each way: the IDs (slot 15), and for the
//     square sizes the configurations (slot 11);
//   - by filter (slots 13 and 9): no filter, and the game's base filter
//     (vtable 0x00A8EC1C: accepts everything) built on the stack by the
//     game's constructor, as the zone developer builds its own;
//   - for every 13th configuration, up to 16 of its building, prop and
//     flora objects with IDs: the footprint (slots 24, 26, 25) and
//     GetObjectIDArray (slot 27); for the buildings also the family and
//     type tests (slots 16, 17) with the object's first ID and with the
//     first type GetObjectIDArray gave.
// =2 adds the registered-filter path: the base filter (a static one)
// registered (slot 18), the configurations by filter twice (slot 9: the
// cached vector is built, then reused), then unregistered (slot 19). This
// allocates in the manager's map, alike in both runs.
uint32_t Queries(char* manager, int level)
{
    Hash hash;
    const HashTable* const map = lotconfig_sim::Map();
    const HashTable* const families = lotconfig_sim::FamilySet();
    hash.Mix(Virtual<void*>(manager, slot::kGetMap) == map);
    hash.Mix(Virtual<void*>(manager, slot::kGetFamilySet) == families);
    uint32_t index = 0;
    for (char* node = FirstNode(map); node; node = NextNode(map, node, At<uint32_t>(node, 0x4)), ++index) {
        if (index % 7 != 0)
            continue;
        void* const found = Virtual<void*>(manager, slot::kGet, At<uint32_t>(node, 0x4));
        hash.Mix(found == At<void*>(node, 0x8));
    }
    hash.Mix(Virtual<void*>(manager, slot::kGet, 0xFFFFFFFFu) != nullptr);

    const uint32_t step = families->count > 16 ? families->count / 16 : 1;
    index = 0;
    uint32_t asked = 0;
    for (char* node = FirstNode(families); node && asked < 16;
         node = NextNode(families, node, At<uint32_t>(node, 0x4)), ++index) {
        if (index % step != 0)
            continue;
        ++asked;
        const uint32_t family = At<uint32_t>(node, 0x4);
        hash.Mix(family);
        {
            Set ids;
            MixIdSet(hash, &ids.table, Virtual<uint32_t>(manager, slot::kIdsByFamily, &ids.table, family));
        }
        {
            Set configurations;
            MixConfigurationSet(hash, manager, &configurations.table,
                                Virtual<uint32_t>(manager, slot::kByFamily, &configurations.table, family));
        }
    }

    for (uint32_t width = 1; width <= 4; ++width)
        for (uint32_t depth = 1; depth <= 4; ++depth) {
            {
                Set ids;
                MixIdSet(hash, &ids.table, Virtual<uint32_t>(manager, slot::kIdsBySize, &ids.table, width, depth));
            }
            if (width == depth) {
                Set configurations;
                MixConfigurationSet(hash, manager, &configurations.table,
                                    Virtual<uint32_t>(manager, slot::kBySize, &configurations.table, width, depth));
            }
        }

    // The base filter: 8 bytes (vtable, reference count 0); its destructor
    // only sets the vtable again.
    void* stackFilter[2] = {};
    reinterpret_cast<SelfFn>(addr::cSC4LotConfigurationFilter_Construct)(stackFilter);
    for (void* const filter : {static_cast<void*>(nullptr), static_cast<void*>(stackFilter)}) {
        {
            Set ids;
            MixIdSet(hash, &ids.table, Virtual<uint32_t>(manager, slot::kIdsByFilter, &ids.table, filter));
        }
        {
            Set configurations;
            MixConfigurationSet(hash, manager, &configurations.table,
                                Virtual<uint32_t>(manager, slot::kByFilter, &configurations.table, filter));
        }
    }

    index = 0;
    for (char* node = FirstNode(map); node; node = NextNode(map, node, At<uint32_t>(node, 0x4)), ++index) {
        if (index % 13 != 0)
            continue;
        char* const configuration = At<char*>(node, 0x8);
        const ObjectVector* const objects = Virtual<ObjectVector*>(configuration, config::kGetObjects);
        if (!objects)
            continue;
        const uint32_t count = lotconfig_sim::ObjectCount(objects);
        uint32_t done = 0;
        for (uint32_t i = 0; i < count && done < 16; ++i) {
            char* const object = objects->begin + static_cast<std::size_t>(i) * config::kObjectSize;
            const uint8_t kind = At<uint8_t>(object, 0x0);
            Vector32* const ids = reinterpret_cast<Vector32*>(object + 0x28);
            if ((kind != 0 && kind != 1 && kind != 4) || ids->begin == ids->end)
                continue;
            ++done;
            uint32_t rect[4] = {};
            Virtual<uint32_t>(manager,
                              kind == 0 ? slot::kBuildingFootprint
                                        : kind == 1 ? slot::kPropFootprint : slot::kFloraFootprint,
                              ids, static_cast<uint32_t*>(rect));
            for (const uint32_t bits : rect)
                hash.Mix(bits);
            Vector32 types{};
            Virtual<uint32_t>(manager, slot::kGetObjectIds, object, &types);
            hash.MixVector(&types);
            if (kind == 0) {
                for (const uint32_t id : {ids->begin[0], types.begin != types.end ? types.begin[0] : 0u}) {
                    char* family = nullptr;
                    hash.Mix(Virtual<uint8_t>(manager, slot::kHasBuildingFamily, configuration, id, &family));
                    hash.Mix(family ? static_cast<uint32_t>(family - objects->begin) : 0xFFFFFFFFu);
                    char* type = nullptr;
                    hash.Mix(Virtual<uint8_t>(manager, slot::kHasBuildingType, configuration, id, &type));
                    hash.Mix(type ? static_cast<uint32_t>(type - objects->begin) : 0xFFFFFFFFu);
                }
            }
            if (types.begin)
                FreeBlock(types.begin);
        }
    }

    if (level >= 2) {
        static void* registered[2];
        reinterpret_cast<SelfFn>(addr::cSC4LotConfigurationFilter_Construct)(registered);
        const uint32_t before = At<uint32_t>(manager, field::kFilterCount);
        Virtual<uint32_t>(manager, slot::kRegisterFilter, static_cast<void*>(registered));
        hash.Mix(At<uint32_t>(manager, field::kFilterCount) - before);
        for (int pass = 0; pass < 2; ++pass) {
            Set configurations;
            MixConfigurationSet(hash, manager, &configurations.table,
                                Virtual<uint32_t>(manager, slot::kByFilter, &configurations.table,
                                                  static_cast<void*>(registered)));
        }
        Virtual<uint32_t>(manager, slot::kUnregisterFilter, static_cast<void*>(registered));
        hash.Mix(At<uint32_t>(manager, field::kFilterCount) - before);
    }
    return hash.h;
}

// The lot configuration manager (build 641; src/core/lotconfig_sim.h),
// found through the city's slot 37 (0x0045xxxx getter of +0x148; the same
// object as the global 0x00B43D18): its reference count and which of its
// three services it holds, the number of configurations, family IDs and
// registered filters, a checksum of the map as the game walks it (bucket
// order: each ID with its configuration's state, ConfigurationHash above),
// a checksum of the family set in its order, and for the registered
// filters the number of cached vectors with the number and (unordered) IDs
// of the configurations in them. No addresses. The whole table is hashed
// every day (a few thousand configurations).
void Fingerprint(cISC4City* city, std::string& line)
{
    if (!game::IsBuild641()) {
        line += " | lotconfig none";
        return;
    }
    char* const manager = Virtual<char*>(city, 0x94);
    if (!manager || !At<uint8_t>(manager, field::kInitialized)) {
        line += " | lotconfig none";
        return;
    }
    const HashTable* const map = lotconfig_sim::Map();
    const HashTable* const families = lotconfig_sim::FamilySet();
    Hash configurations;
    uint32_t walked = 0;
    for (char* node = FirstNode(map); node && walked < 0x100000;
         node = NextNode(map, node, At<uint32_t>(node, 0x4)), ++walked) {
        configurations.Mix(At<uint32_t>(node, 0x4));
        configurations.Mix(ConfigurationHash(At<char*>(node, 0x8)));
    }
    Hash familyIds;
    walked = 0;
    for (char* node = FirstNode(families); node && walked < 0x100000;
         node = NextNode(families, node, At<uint32_t>(node, 0x4)), ++walked)
        familyIds.Mix(At<uint32_t>(node, 0x4));
    uint32_t cachedVectors = 0, cached = 0, cachedSum = 0;
    char* const head = At<char*>(manager, field::kFilters);
    walked = 0;
    for (char* node = At<char*>(head, 0x8); node != head && walked < 4096;
         node = lotconfig_sim::TreeNext(node), ++walked) {
        const lotconfig_sim::PtrVector* const vector = At<lotconfig_sim::PtrVector*>(node, 0x14);
        if (!vector)
            continue;
        ++cachedVectors;
        for (void** configuration = vector->begin; configuration != vector->end; ++configuration) {
            Hash one;
            one.Mix(Virtual<uint32_t>(*configuration, config::kGetId));
            cachedSum += one.h;
            ++cached;
        }
    }
    const uint32_t services = (At<void*>(manager, field::kDevelopment) ? 1u : 0u) |
                              (At<void*>(manager, field::kFlora) ? 2u : 0u) |
                              (At<void*>(manager, field::kProps) ? 4u : 0u);
    char text[200];
    std::snprintf(text, sizeof text, " | lotconfig %u,%u,%u,%u,%u,%08X,%08X,%u/%u/%08X",
                  At<uint32_t>(manager, field::kRefCount), services, map->count, families->count,
                  At<uint32_t>(manager, field::kFilterCount), configurations.h, familyIds.h, cachedVectors, cached,
                  cachedSum);
    line += text;
    const int level = EnvInt("OPENSC4_TEST_LOTCONFIG", 0);
    if (level >= 1) {
        char hash[16];
        std::snprintf(hash, sizeof hash, ",q%08X", Queries(manager, level));
        line += hash;
    }
}

const FingerprintPart kFingerprint{"lotconfig", 101, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
