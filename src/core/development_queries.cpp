// Our cSC4BuildingDevelopmentSimulator's queries (build 641;
// docs/development.md): the building, family, foundation and retaining wall
// tables, building profiles and names, developers and their types, the
// maximum capacities, construction resources, the cheats. Every function is
// a replacement on the original object (slots of the primary vtable
// 0x00AB0AF8); the helpers they call inside the class are plain functions
// here.
//
// Fields used here (offsets from the object's start), to merge into the
// shared header:
//   kCity1C = 0x1C         the city's service: slot 95 (+0x17C) turns a
//                          position into a cell, slot 24 (+0x60) is the
//                          index into the stage multipliers
//   kService20 = 0x20      the occupant service: slot 33 (+0x84) finds the
//                          occupant of a type at a cell, slot 21 (+0x54) the
//                          cost of an occupant
//   kBuildingMap = 0x48    returned by slot 41
//   kDevelopers = 0x5C     std::map<uint32 developer type, developer*>
//                          (value at node +0x14); slot 34 returns its address
//   kLotDeveloper = 0x6C   returned by slot 36
//   kResources = 0x70      the construction resources: slot 8 (+0x20) the
//                          total, slot 9 (+0x24) the claimed, slot 17
//                          (+0x44) adds a float, slot 30 (+0x78) updates
//   kLastBuildDays = 0xB0  std::map<int32 developer type, int32 day> (value
//                          at node +0x14)
//   kStageMultipliers = 0xD0  float array, by the city's stage index
//   kCheatIgnorePower = 0xDE, kCheatIgnoreCaps = 0xDF,
//   kCheatCoffeeBar = 0xE1 (uint8)
//
// The class's tables are globals (STLport hash tables: +4 bucket begin, +8
// bucket end, +0x10 element count; nodes: +0 next, key at +4 unless noted):
//   0x00B48548 building types: key, then the resource key (3 dwords) at +8
//   0x00B4855C building names, 0x00B48570 exemplar names (reached only
//     through the game's map operator[] 0x004862F0)
//   0x00B48584 building profiles: key at +8, the 0x40-byte profile at +0x10
//   0x00B48598 families: key, then a vector of building types at +8
//   0x00B485AC maximum capacities: std::map<{int32 zone, uint8 map key},
//     float> (key at node +0x10/+0x14, value at +0x18)
//   0x00B485B8 foundation types, 0x00B485CC retaining wall types (key, then
//     the resource key at +8)
#include "core/development_sim.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::development_sim {
namespace {

namespace local {
constexpr std::size_t kCity1C = 0x1C;
constexpr std::size_t kService20 = 0x20;
constexpr std::size_t kBuildingMap = 0x48;
constexpr std::size_t kDevelopers = 0x5C;
constexpr std::size_t kLotDeveloper = 0x6C;
constexpr std::size_t kResources = 0x70;
constexpr std::size_t kLastBuildDays = 0xB0;
constexpr std::size_t kStageMultipliers = 0xD0;
constexpr std::size_t kCheatIgnorePower = 0xDE;
constexpr std::size_t kCheatIgnoreCaps = 0xDF;
constexpr std::size_t kCheatCoffeeBar = 0xE1;
} // namespace local

constexpr uint32_t kCostProperty = 0x099AFACD;
constexpr uint32_t kOccupantTypeAtCell = 0x278128A0;

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using InsertNFn = void(__thiscall*)(void* vector, uint32_t* where, const void* value, const void* allocator,
                                    uint32_t count, uint32_t atEnd);

void Free(void* block) { reinterpret_cast<FreeFn>(addr::SC4_Free)(block); }

// The truncating float-to-int conversion (0x009EEF04).
int32_t Ftol(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

// --- Hash tables -------------------------------------------------------------

char* Table(std::uintptr_t address) { return reinterpret_cast<char*>(address); }

void** Buckets(char* table) { return At<void**>(table, 4); }

// The bucket count: (end - begin) >> 2 (SAR), divided by unsigned.
uint32_t BucketCount(char* table)
{
    return static_cast<uint32_t>(
        static_cast<int32_t>(At<uintptr_t>(table, 8) - At<uintptr_t>(table, 4)) >> 2);
}

// The node of `key` (at `keyOffset` in the node), or null.
void* FindNode(char* table, uint32_t key, std::size_t keyOffset)
{
    void* node = Buckets(table)[key % BucketCount(table)];
    while (node && At<uint32_t>(node, keyOffset) != key)
        node = *static_cast<void**>(node);
    return node;
}

// An iterator as the game's begin() (0x004B0390) fills it.
struct Iterator {
    void* node;
    char* table;
};

void Begin(std::uintptr_t table, Iterator* out)
{
    using BeginFn = Iterator*(__thiscall*)(void* table, Iterator* out);
    reinterpret_cast<BeginFn>(addr::SC4HashTable_Begin)(Table(table), out);
}

// The iterator's ++ (inlined by the game; key at +4): the next node in the
// bucket, else the first node of a later bucket, else null. *count gets the
// bucket count the game leaves in EAX when it searches the buckets.
void* Next(void* node, char* table, uint32_t* count)
{
    if (void* next = *static_cast<void**>(node))
        return next;
    uint32_t index = At<uint32_t>(node, 4) % BucketCount(table);
    const uint32_t n = BucketCount(table);
    *count = n;
    void* found = nullptr;
    for (++index; index < n; ++index) {
        found = Buckets(table)[index];
        if (found)
            break;
    }
    return found;
}

// The game's inline push_back on a vector of uint32 (growing through
// 0x0051CA60, with `allocator` as its unused allocator argument).
void PushBack(void** vector, uint32_t* value, const void* allocator)
{
    auto* const end = static_cast<uint32_t*>(vector[1]);
    if (end == vector[2]) {
        reinterpret_cast<InsertNFn>(addr::SC4PtrVector_InsertN)(vector, end, value, allocator, 1u, 1u);
        return;
    }
    if (end)
        *end = *value;
    vector[1] = end + 1;
}

// 0x00697C50, 0x00697D10, 0x00698050, 0x00698180 (inline): empties the
// vector (end = begin; the game's erase(end, end)) and appends every key of
// the table. Returns what the game leaves in EAX: the bucket count after the
// last node; for an empty table the game returns the address of its own
// iterator on its stack, which cannot be matched: ours returns 0.
uint32_t ListKeys(void** vector, std::uintptr_t table)
{
    vector[1] = vector[0];
    Iterator it;
    Begin(table, &it);
    void* node = it.node;
    if (!node)
        return 0;
    char* const t = it.table;
    uint32_t count = 0;
    do {
        uint32_t value = At<uint32_t>(node, 4);
        PushBack(vector, &value, &value);
        node = Next(node, t, &count);
    } while (node);
    return count;
}

// Copies the resource key (3 dwords at node +8) of `type` in a table into
// *key; false without it.
uint8_t KeyOf(std::uintptr_t table, uint32_t type, uint32_t* key)
{
    void* const node = FindNode(Table(table), type, 4);
    if (!node)
        return 0;
    key[0] = At<uint32_t>(node, 0x8);
    key[1] = At<uint32_t>(node, 0xC);
    key[2] = At<uint32_t>(node, 0x10);
    return 1;
}

// --- Building profiles -------------------------------------------------------

// 0x00696540: the hash table's resize for nodes keyed at +8 (also called
// from 0x0080982B, outside this class): grows the buckets to the first
// prime >= `hint` (0x00A829B0-0x00A82A20; 0xFFFFFFFB past the end) when
// that is more than now, moving every node to the front of its new bucket.
void Resize(char* table, uint32_t hint)
{
    using LowerBoundFn = const uint32_t*(__cdecl*)(const uint32_t* first, const uint32_t* last,
                                                   const uint32_t* value, uint32_t, uint32_t);
    using ConstructFn = void*(__thiscall*)(void* vector, uint32_t count, void* const* value, const void* allocator);
    const uint32_t old = BucketCount(table);
    if (hint <= old)
        return;
    uint32_t n = hint;
    const auto* const primesEnd = reinterpret_cast<const uint32_t*>(addr::SC4HashTable_PrimesEnd);
    const uint32_t* const pos = reinterpret_cast<LowerBoundFn>(addr::SC4_LowerBoundUint32)(
        reinterpret_cast<const uint32_t*>(addr::SC4HashTable_Primes), primesEnd, &n, hint, 0u);
    n = pos == primesEnd ? 0xFFFFFFFBu : *pos;
    if (n <= old)
        return;
    void* buckets[3];
    void* const null = nullptr;
    reinterpret_cast<ConstructFn>(addr::SC4PtrVector_Construct)(buckets, n, &null, &n);
    void** const fresh = static_cast<void**>(buckets[0]);
    for (uint32_t bucket = 0; bucket < old; ++bucket) {
        void* first = Buckets(table)[bucket];
        while (first) {
            const uint32_t index = At<uint32_t>(first, 8) % n;
            Buckets(table)[bucket] = *static_cast<void**>(first);
            *static_cast<void**>(first) = fresh[index];
            fresh[index] = first;
            first = Buckets(table)[bucket];
        }
    }
    void* const previous = At<void*>(table, 4);
    At<void*>(table, 4) = fresh;
    At<void*>(table, 8) = buckets[1];
    At<void*>(table, 0xC) = buckets[2];
    if (previous)
        Free(previous);
}

// A profile map entry as the game builds it on its stack: the key, then the
// 0x40-byte profile at +8.
struct ProfilePair {
    uint32_t key;
    uint32_t pad;
    uint8_t profile[0x40];
};

using CopyProfileFn = void*(__thiscall*)(void* profile, const void* source);

// 0x0069B650: inserts a pair whose key is not in the table: resize for one
// more, a 0x50-byte node (STL allocator) at the front of its bucket, the
// profile copied (0x004D1230). Returns the node's pair (+8).
char* InsertProfile(char* table, ProfilePair* pair)
{
    Resize(table, At<uint32_t>(table, 0x10) + 1);
    const uint32_t index = pair->key % BucketCount(table);
    void* const head = Buckets(table)[index];
    char* const node = static_cast<char*>(reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(0x50));
    At<void*>(node, 0) = nullptr;
    At<uint32_t>(node, 8) = pair->key;
    reinterpret_cast<CopyProfileFn>(addr::SC4BuildingProfile_CopyConstruct)(node + 0x10, pair->profile);
    At<void*>(node, 0) = head;
    Buckets(table)[index] = node;
    ++At<uint32_t>(table, 0x10);
    return node + 8;
}

// Destroys the vector of maps at +0xC of a profile (0x00459170) and frees
// its storage.
void DestroyProfileVector(uint8_t* profile, void* allocator, bool release)
{
    using DestroyFn = void(__cdecl*)(void* first, void* last, void* allocator);
    void* const first = At<void*>(profile, 0xC);
    reinterpret_cast<DestroyFn>(addr::SC4MapVector_DestroyRange)(first, At<void*>(profile, 0x10), allocator);
    if (release && first)
        Free(first);
}

// 0x0069B720: the profile of a building (operator[] of the profile table):
// found by key (node +8), else a zeroed profile inserted. The game leaves
// the profile's padding bytes unset; they are not copied.
uint8_t* ProfileOf(uint32_t building)
{
    char* const table = Table(addr::cSC4BuildingDevelopmentSimulator_BuildingProfiles);
    if (void* const node = FindNode(table, building, 8))
        return static_cast<uint8_t*>(node) + 0x10;
    uint8_t blank[0x40] = {};
    ProfilePair pair;
    pair.key = building;
    reinterpret_cast<CopyProfileFn>(addr::SC4BuildingProfile_CopyConstruct)(pair.profile, blank);
    uint8_t* const result = reinterpret_cast<uint8_t*>(InsertProfile(table, &pair)) + 8;
    uint8_t tag = 0;
    DestroyProfileVector(pair.profile, &tag, true);
    // The blank's vector is null: the game calls the range destructor and
    // skips the free.
    DestroyProfileVector(blank, &tag, true);
    return result;
}

// --- The interface -----------------------------------------------------------

// Slot 5 (0x00695590): whether a building can go at a position: its type is
// valid (slot 15) and the occupant service has no occupant of type
// 0x278128A0 at the position's cell (the found occupant is released).
uint8_t __fastcall LocationIsOKForNewBuilding(void* self, void*, uint32_t building, float x, float z)
{
    static bool logged = false;
    FirstCall("LocationIsOKForNewBuilding", logged);
    if (!Virtual<uint8_t>(self, 0x3C, building))
        return 0;
    // The game reuses its argument slots: the cell over x and z, the
    // occupant over the building.
    int32_t cellX;
    int32_t cellZ;
    std::memcpy(&cellX, &x, sizeof(cellX));
    std::memcpy(&cellZ, &z, sizeof(cellZ));
    void* occupant = nullptr;
    Virtual<uint8_t>(At<void*>(self, local::kCity1C), 0x17C, x, z, &cellX, &cellZ);
    Virtual<uint8_t>(At<void*>(self, local::kService20), 0x84, &occupant, cellX, cellZ, kOccupantTypeAtCell);
    const bool found = occupant != nullptr;
    if (occupant)
        Virtual<uint32_t>(occupant, 0x8);
    return !found;
}

// Slot 10 (0x00695700): an occupant's demolition cost: the int64 property
// 0x099AFACD of its slot 3's property holder (slot 6), 0 without it.
int64_t __fastcall GetOccupantDemolitionCost(void*, void*, void* occupant, uint32_t)
{
    using GetSint64Fn = uint8_t(__cdecl*)(void* holder, uint32_t property, int64_t* value);
    static bool logged = false;
    FirstCall("GetOccupantDemolitionCost", logged);
    int64_t cost = 0;
    void* const object = Virtual<void*>(occupant, 0xC);
    void* const holder = Virtual<void*>(object, 0x18);
    reinterpret_cast<GetSint64Fn>(addr::SC4_GetPropertySint64)(holder, kCostProperty, &cost);
    return cost;
}

// Slot 11 (0x00697C50): every building type into a vector.
uint32_t __fastcall GetAllBuildingTypes(void*, void*, void** vector)
{
    static bool logged = false;
    FirstCall("GetAllBuildingTypes", logged);
    return ListKeys(vector, addr::cSC4BuildingDevelopmentSimulator_BuildingTypes);
}

// Slot 12 (0x00697B80): up to *count building types into an array; *count
// is capped at the number of types first. Returns the number of types.
uint32_t __fastcall GetAllBuildingTypesArray(void*, void*, uint32_t* array, uint32_t* count)
{
    static bool logged = false;
    FirstCall("GetAllBuildingTypesArray", logged);
    const std::uintptr_t types = addr::cSC4BuildingDevelopmentSimulator_BuildingTypes;
    const uint32_t size = At<uint32_t>(Table(types), 0x10);
    if (*count > size)
        *count = size;
    if (!array || *count == 0)
        return size;
    Iterator it;
    Begin(types, &it);
    if (*count == 0)
        return size;
    void* node = it.node;
    char* const table = it.table;
    uint32_t i = 0;
    uint32_t unused = 0;
    do {
        if (!node)
            break;
        array[i] = At<uint32_t>(node, 4);
        node = Next(node, table, &unused);
        ++i;
    } while (i < *count);
    return size;
}

// Slot 13 (0x00697D10): every family into a vector.
uint32_t __fastcall GetAllBuildingFamilyTypes(void*, void*, void** vector)
{
    static bool logged = false;
    FirstCall("GetAllBuildingFamilyTypes", logged);
    return ListKeys(vector, addr::cSC4BuildingDevelopmentSimulator_BuildingFamilies);
}

// Slot 14 (0x00697DD0): a building type's resource key; false without it.
uint8_t __fastcall GetBuildingKeyFromType(void*, void*, uint32_t type, uint32_t* key)
{
    static bool logged = false;
    FirstCall("GetBuildingKeyFromType", logged);
    return KeyOf(addr::cSC4BuildingDevelopmentSimulator_BuildingTypes, type, key);
}

// Slot 15 (0x00697E30): whether a building type exists.
uint8_t __fastcall IsBuildingTypeValid(void*, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("IsBuildingTypeValid", logged);
    return FindNode(Table(addr::cSC4BuildingDevelopmentSimulator_BuildingTypes), type, 4) != nullptr;
}

// Slot 16 (0x00697E70): whether an ID is a family or a building type.
uint8_t __fastcall IsBuildingOrFamilyTypeValid(void*, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("IsBuildingOrFamilyTypeValid", logged);
    if (FindNode(Table(addr::cSC4BuildingDevelopmentSimulator_BuildingFamilies), type, 4))
        return 1;
    return FindNode(Table(addr::cSC4BuildingDevelopmentSimulator_BuildingTypes), type, 4) != nullptr;
}

using SetResizeFn = void(__thiscall*)(void* set, uint32_t count);
using SetInsertFn = void*(__thiscall*)(void* set, void* result, const uint32_t* value);

// The game's inline set insert: resize for one more (+0x10 is the count),
// then insert_unique (the pair<iterator, bool> result is unused).
void SetInsert(void* set, const uint32_t* value, void* result)
{
    reinterpret_cast<SetResizeFn>(addr::SC4Uint32HashSet_Resize)(set, At<uint32_t>(set, 0x10) + 1);
    reinterpret_cast<SetInsertFn>(addr::SC4Uint32HashSet_Insert)(set, result, value);
}

// Slot 17 (0x00699C50): every building type the filter accepts (its slot 3;
// all without a filter) into a hash set. Returns the number inserted
// (counted even when already in the set).
uint32_t __fastcall GetBuildingTypesByFilter(void*, void*, void* set, void* filter)
{
    static bool logged = false;
    FirstCall("GetBuildingTypesByFilter", logged);
    Iterator it;
    Begin(addr::cSC4BuildingDevelopmentSimulator_BuildingTypes, &it);
    uint32_t inserted = 0;
    void* node = it.node;
    if (!node)
        return 0;
    char* const table = it.table;
    uint32_t unused = 0;
    do {
        uint32_t type = At<uint32_t>(node, 4);
        if (!filter || Virtual<uint8_t>(filter, 0xC, type)) {
            uint32_t result[3];
            SetInsert(set, &type, result);
            ++inserted;
        }
        node = Next(node, table, &unused);
    } while (node);
    return inserted;
}

// Slot 18 (0x00697EF0): the building types of a family into a vector (a
// copy; returns their number), or, for a building type, the type alone
// (returns 0); 0 and the vector untouched for neither.
uint32_t __fastcall GetBuildingTypesByFamily(void*, void*, void** vector, uint32_t id)
{
    using CopyFn = void*(__thiscall*)(void* vector, const void* other);
    static bool logged = false;
    FirstCall("GetBuildingTypesByFamily", logged);
    if (char* const family =
            static_cast<char*>(FindNode(Table(addr::cSC4BuildingDevelopmentSimulator_BuildingFamilies), id, 4))) {
        const uint32_t count = static_cast<uint32_t>(
            static_cast<int32_t>(At<uintptr_t>(family, 0xC) - At<uintptr_t>(family, 8)) >> 2);
        reinterpret_cast<CopyFn>(addr::SC4Uint32Vector_CopyAssign)(vector, family + 8);
        return count;
    }
    if (!FindNode(Table(addr::cSC4BuildingDevelopmentSimulator_BuildingTypes), id, 4))
        return 0;
    vector[1] = vector[0];
    void* allocator = vector;
    PushBack(vector, &id, &allocator);
    return 0;
}

// Slot 19 (0x00699D20): the building types of a family into a hash set
// (returns their number), or, for a building type, the type alone (returns
// 0); 0 for neither.
uint32_t __fastcall GetBuildingTypesByFamilySet(void*, void*, void* set, uint32_t id)
{
    static bool logged = false;
    FirstCall("GetBuildingTypesByFamilySet", logged);
    uint32_t result[3];
    if (char* const family =
            static_cast<char*>(FindNode(Table(addr::cSC4BuildingDevelopmentSimulator_BuildingFamilies), id, 4))) {
        const int32_t count = static_cast<int32_t>(At<uintptr_t>(family, 0xC) - At<uintptr_t>(family, 8)) >> 2;
        if (count <= 0)
            return static_cast<uint32_t>(count);
        for (int32_t i = 0; i < count; ++i) {
            const uint32_t* const types = At<uint32_t*>(family, 8);
            SetInsert(set, types + i, result);
        }
        return static_cast<uint32_t>(count);
    }
    if (FindNode(Table(addr::cSC4BuildingDevelopmentSimulator_BuildingTypes), id, 4))
        SetInsert(set, &id, result);
    return 0;
}

// Slot 20 (0x00697FE0): whether a building is in a family (or is it): a
// find (0x004696C0) over the family's types.
uint8_t __fastcall IsBuildingInFamily(void*, void*, uint32_t building, uint32_t family)
{
    using FindFn = uint32_t*(__cdecl*)(uint32_t* first, uint32_t* last, const uint32_t* value, const void* tag);
    static bool logged = false;
    FirstCall("IsBuildingInFamily", logged);
    if (building == family)
        return 1;
    char* const node =
        static_cast<char*>(FindNode(Table(addr::cSC4BuildingDevelopmentSimulator_BuildingFamilies), family, 4));
    if (!node)
        return 0;
    uint32_t* const last = At<uint32_t*>(node, 0xC);
    uint32_t* const found =
        reinterpret_cast<FindFn>(addr::SC4_FindUint32)(At<uint32_t*>(node, 8), last, &building, &building);
    return found != last;
}

// Slot 21 (0x00698050): every foundation type into a vector; returns their
// number.
uint32_t __fastcall GetAllFoundationTypes(void*, void*, void** vector)
{
    static bool logged = false;
    FirstCall("GetAllFoundationTypes", logged);
    ListKeys(vector, addr::cSC4BuildingDevelopmentSimulator_FoundationTypes);
    return At<uint32_t>(Table(addr::cSC4BuildingDevelopmentSimulator_FoundationTypes), 0x10);
}

// Slot 25 (0x00695770): the demolition cost of an occupant's slot 3 by the
// occupant service (its slot 21, with 1 and 0), passed through.
uint64_t __fastcall GetCostOfDemolition(void* self, void*, void* occupant, uint32_t)
{
    static bool logged = false;
    FirstCall("GetCostOfDemolition", logged);
    void* const object = Virtual<void*>(occupant, 0xC);
    return Virtual<uint64_t>(At<void*>(self, local::kService20), 0x54, object, 1u, 0u);
}

// Slot 27 (0x00698180): every retaining wall type into a vector; returns
// their number.
uint32_t __fastcall GetAllLotRetainingWallTypes(void*, void*, void** vector)
{
    static bool logged = false;
    FirstCall("GetAllLotRetainingWallTypes", logged);
    ListKeys(vector, addr::cSC4BuildingDevelopmentSimulator_RetainingWallTypes);
    return At<uint32_t>(Table(addr::cSC4BuildingDevelopmentSimulator_RetainingWallTypes), 0x10);
}

// Slot 28 (0x00698250): a retaining wall type's resource key.
uint8_t __fastcall GetLotRetainingWallKeyFromType(void*, void*, uint32_t type, uint32_t* key)
{
    static bool logged = false;
    FirstCall("GetLotRetainingWallKeyFromType", logged);
    return KeyOf(addr::cSC4BuildingDevelopmentSimulator_RetainingWallTypes, type, key);
}

// 0x00695790: the developer type of a purpose and wealth: residential
// 0x1010-0x1030, commercial services 0x3110-0x3130, commercial offices
// 0x3320/0x3330, industry 0x4100-0x4400 (purposes 5-8); 0 otherwise.
uint32_t DeveloperType(uint32_t purpose, uint32_t wealth)
{
    switch (purpose) {
    case 1:
        return wealth == 1 ? 0x1010u : wealth == 2 ? 0x1020u : wealth == 3 ? 0x1030u : 0u;
    case 2:
        return wealth == 1 ? 0x3110u : wealth == 2 ? 0x3120u : wealth == 3 ? 0x3130u : 0u;
    case 3:
        return wealth == 2 ? 0x3320u : wealth == 3 ? 0x3330u : 0u;
    case 5: return 0x4100;
    case 6: return 0x4200;
    case 7: return 0x4300;
    case 8: return 0x4400;
    default: return 0;
    }
}

// 0x00695840: the wealth of a developer type (1-3; industry: 0x4100 1,
// 0x4200 and 0x4300 2, 0x4400 3); 0 otherwise.
uint32_t WealthOf(int32_t type)
{
    switch (type) {
    case 0x1010:
    case 0x3110:
    case 0x4100:
        return 1;
    case 0x1020:
    case 0x3120:
    case 0x3320:
    case 0x4200:
    case 0x4300:
        return 2;
    case 0x1030:
    case 0x3130:
    case 0x3330:
    case 0x4400:
        return 3;
    default:
        return 0;
    }
}

// Slot 30 (0x00698D60): the developer type of a purpose and wealth.
uint32_t __fastcall GetDeveloperType(void*, void*, uint32_t purpose, uint32_t wealth)
{
    static bool logged = false;
    FirstCall("GetDeveloperType", logged);
    return DeveloperType(purpose, wealth);
}

// Slot 31 (0x00698D80): the wealth of a developer type.
uint32_t __fastcall GetWealth(void*, void*, int32_t type)
{
    static bool logged = false;
    FirstCall("GetWealth", logged);
    return WealthOf(type);
}

// Slot 32 (0x00696C80): the developer of a type, null without one.
void* __fastcall GetDeveloperByType(void* self, void*, uint32_t type)
{
    using FindFn = void(__thiscall*)(void* map, void** out, const uint32_t* key);
    static bool logged = false;
    FirstCall("GetDeveloperByType", logged);
    void* const map = static_cast<char*>(self) + local::kDevelopers;
    void* node = nullptr;
    reinterpret_cast<FindFn>(addr::SC4Uint32Map_Find)(map, &node, &type);
    if (node == *static_cast<void**>(map))
        return nullptr;
    return At<void*>(node, 0x14);
}

// Slot 33 (0x006958B0): the developer of a purpose and wealth (slots 30 and
// 32).
void* __fastcall GetDeveloper(void* self, void*, uint32_t purpose, uint32_t wealth)
{
    static bool logged = false;
    FirstCall("GetDeveloper", logged);
    const uint32_t type = Virtual<uint32_t>(self, 0x78, purpose, wealth);
    return Virtual<void*>(self, 0x80, type);
}

// Slot 34 (0x00698CD0): the developer map.
void* __fastcall GetDevelopers(void* self, void*)
{
    static bool logged = false;
    FirstCall("GetDevelopers", logged);
    return static_cast<char*>(self) + local::kDevelopers;
}

// Slot 36 (0x00698CE0): the lot developer.
void* __fastcall GetLotDeveloper(void* self, void*)
{
    static bool logged = false;
    FirstCall("GetLotDeveloper", logged);
    return At<void*>(self, local::kLotDeveloper);
}

using NameIndexFn = void*(__thiscall*)(void* map, const uint32_t* key);

// Slot 37 (0x0069AB20): an exemplar's name (the name map's operator[]).
void* __fastcall GetExemplarName(void*, void*, uint32_t exemplar)
{
    static bool logged = false;
    FirstCall("GetExemplarName", logged);
    return reinterpret_cast<NameIndexFn>(addr::SC4NameHashMap_Index)(
        Table(addr::cSC4BuildingDevelopmentSimulator_ExemplarNames), &exemplar);
}

// Slot 38 (0x0069AB00): a building's name.
void* __fastcall GetBuildingName(void*, void*, uint32_t building)
{
    static bool logged = false;
    FirstCall("GetBuildingName", logged);
    return reinterpret_cast<NameIndexFn>(addr::SC4NameHashMap_Index)(
        Table(addr::cSC4BuildingDevelopmentSimulator_BuildingNames), &building);
}

// Slot 39 (0x0069CA60): a building's profile, made blank when missing.
void* __fastcall GetBuildingProfile(void*, void*, uint32_t building)
{
    static bool logged = false;
    FirstCall("GetBuildingProfile", logged);
    return ProfileOf(building);
}

// Slot 40 (0x006985C0): a building's profile, null when missing.
void* __fastcall GetBuildingProfilePtr(void*, void*, uint32_t building)
{
    static bool logged = false;
    FirstCall("GetBuildingProfilePtr", logged);
    void* const node = FindNode(Table(addr::cSC4BuildingDevelopmentSimulator_BuildingProfiles), building, 8);
    return node ? static_cast<char*>(node) + 0x10 : nullptr;
}

// Slot 41 (0x00698CF0): the building map.
void* __fastcall GetBuildingMap(void* self, void*)
{
    static bool logged = false;
    FirstCall("GetBuildingMap", logged);
    return At<void*>(self, local::kBuildingMap);
}

// Slot 42 (0x00698D90): the building map key of a purpose and wealth:
// purpose * 4 + wealth + 1, as a byte (the game leaves the rest of EAX as
// its caller had it).
uint8_t __fastcall GetBuildingMapKey(void*, void*, uint32_t purpose, uint32_t wealth)
{
    static bool logged = false;
    FirstCall("GetBuildingMapKey", logged);
    return static_cast<uint8_t>(static_cast<uint8_t>(purpose) * 4 + static_cast<uint8_t>(wealth) + 1);
}

// Slot 43 (0x0069CCB0): the building map key of a building, from its
// profile (slot 39's operator[]: purpose at +0, wealth at +4). The game's
// EAX keeps the profile pointer's upper 24 bits.
uint32_t __fastcall GetBuildingMapKeyOfBuilding(void*, void*, uint32_t building)
{
    static bool logged = false;
    FirstCall("GetBuildingMapKeyOfBuilding", logged);
    uint8_t* const profile = ProfileOf(building);
    const auto key = static_cast<uint8_t>(profile[0] * 4 + profile[4] + 1);
    return (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(profile)) & 0xFFFFFF00u) | key;
}

// 0x00695F10: find in the capacity map (std::map keyed by {int32 zone,
// uint8 map key}); the header for none.
void* FindCapacity(int32_t zone, uint8_t key)
{
    void* const header = Global<void*>(addr::cSC4BuildingDevelopmentSimulator_MaxCapacities);
    void* found = header;
    void* node = At<void*>(header, 4);
    while (node) {
        const int32_t nodeZone = At<int32_t>(node, 0x10);
        if (zone > nodeZone) {
            node = At<void*>(node, 0xC);
        } else if (zone < nodeZone) {
            found = node;
            node = At<void*>(node, 8);
        } else if (At<uint8_t>(node, 0x14) < key) {
            node = At<void*>(node, 0xC);
        } else {
            found = node;
            node = At<void*>(node, 8);
        }
    }
    if (found == header)
        return header;
    const int32_t foundZone = At<int32_t>(found, 0x10);
    if (foundZone > zone)
        return header;
    if (foundZone < zone)
        return found;
    if (key < At<uint8_t>(found, 0x14))
        return header;
    return found;
}

// Slot 45 (0x00696C20): the maximum capacity of a zone, purpose and wealth
// (key by slot 42), truncated to an integer; 0 without an entry. Returned in
// ST0.
double __fastcall GetMaxCapacity(void* self, void*, int32_t zone, uint32_t purpose, uint32_t wealth)
{
    static bool logged = false;
    FirstCall("GetMaxCapacity", logged);
    int32_t capacity = 0;
    const uint8_t key = Virtual<uint8_t>(self, 0xA8, purpose, wealth);
    void* const node = FindCapacity(zone, key);
    if (node != Global<void*>(addr::cSC4BuildingDevelopmentSimulator_MaxCapacities))
        capacity = Ftol(At<float>(node, 0x18));
    return static_cast<double>(capacity);
}

// Slot 48 (0x006958E0): the free construction resources: total (stored as
// a float) minus claimed, truncated, at least 0.
int32_t __fastcall GetFreeConstructionResources(void* self, void*)
{
    static bool logged = false;
    FirstCall("GetFreeConstructionResources", logged);
    void* const resources = At<void*>(self, local::kResources);
    const float total = static_cast<float>(Virtual<double>(resources, 0x20));
    const double claimed = Virtual<double>(resources, 0x24);
    const int32_t available = Ftol(static_cast<double>(total) - claimed);
    return available < 0 ? 0 : available;
}

// Slot 49 (0x00695910): the total construction resources, truncated.
int32_t __fastcall GetTotalConstructionResources(void* self, void*)
{
    static bool logged = false;
    FirstCall("GetTotalConstructionResources", logged);
    return Ftol(Virtual<double>(At<void*>(self, local::kResources), 0x20));
}

// Slot 50 (0x00695920): claims resources when claimed + amount (as a
// float) does not exceed the total (false otherwise, also for NaN): adds
// the amount (slot 17), then updates (slot 30).
uint8_t __fastcall ClaimConstructionResources(void* self, void*, uint32_t amount)
{
    static bool logged = false;
    FirstCall("ClaimConstructionResources", logged);
    const float value = static_cast<float>(static_cast<int32_t>(static_cast<uint16_t>(amount)));
    void* const resources = At<void*>(self, local::kResources);
    const float wanted = static_cast<float>(Virtual<double>(resources, 0x24) + static_cast<double>(value));
    const double total = Virtual<double>(resources, 0x20);
    if (!(total >= static_cast<double>(wanted)))
        return 0;
    Virtual<void>(At<void*>(self, local::kResources), 0x44, value);
    Virtual<void>(At<void*>(self, local::kResources), 0x78);
    return 1;
}

// Slot 51 (0x00695980): releases resources: adds minus the amount (slot
// 17), then updates (slot 30, whose result is returned).
uint64_t __fastcall ReleaseConstructionResources(void* self, void*, uint32_t amount)
{
    static bool logged = false;
    FirstCall("ReleaseConstructionResources", logged);
    const int32_t negative = -static_cast<int32_t>(static_cast<uint16_t>(amount));
    Virtual<void>(At<void*>(self, local::kResources), 0x44, static_cast<float>(negative));
    return Virtual<uint64_t>(At<void*>(self, local::kResources), 0x78);
}

// Slot 52 (0x00696CC0): the day of a developer type's last successful
// build, 0 without one.
int32_t __fastcall GetDayOfLastSuccessfulBuild(void* self, void*, int32_t type)
{
    using FindFn = void(__thiscall*)(void* map, void** out, const int32_t* key);
    static bool logged = false;
    FirstCall("GetDayOfLastSuccessfulBuild", logged);
    void* const map = static_cast<char*>(self) + local::kLastBuildDays;
    void* node = nullptr;
    reinterpret_cast<FindFn>(addr::SC4Sint32Map_Find)(map, &node, &type);
    if (node == *static_cast<void**>(map))
        return 0;
    return At<int32_t>(node, 0x14);
}

// Slot 53 (0x00696040): the population multiplier of the city's stage (its
// slot 24 indexes the multipliers). Returned in ST0.
double __fastcall GetStagePopulationMultiplier(void* self, void*)
{
    static bool logged = false;
    FirstCall("GetStagePopulationMultiplier", logged);
    const int32_t stage = Virtual<int32_t>(At<void*>(self, local::kCity1C), 0x60);
    return At<float*>(self, local::kStageMultipliers)[stage];
}

// Slot 55 (0x00698D00): the ignore-power cheat.
uint8_t __fastcall CheatIgnorePower(void* self, void*)
{
    static bool logged = false;
    FirstCall("CheatIgnorePower", logged);
    return At<uint8_t>(self, local::kCheatIgnorePower);
}

// Slot 56 (0x00698D10): the ignore-caps cheat.
uint8_t __fastcall CheatIgnoreCaps(void* self, void*)
{
    static bool logged = false;
    FirstCall("CheatIgnoreCaps", logged);
    return At<uint8_t>(self, local::kCheatIgnoreCaps);
}

// Slot 58 (0x00698D20): the coffee bar cheat.
uint8_t __fastcall CheatCoffeeBar(void* self, void*)
{
    static bool logged = false;
    FirstCall("CheatCoffeeBar", logged);
    return At<uint8_t>(self, local::kCheatCoffeeBar);
}

// --- Installation ------------------------------------------------------------

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

#define OPENSC4_SLOT(index, original, fn)                                                                    \
    {addr::cSC4BuildingDevelopmentSimulator_vftable, index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(5, 0x00695590, LocationIsOKForNewBuilding),
    OPENSC4_SLOT(10, 0x00695700, GetOccupantDemolitionCost),
    OPENSC4_SLOT(11, 0x00697C50, GetAllBuildingTypes),
    OPENSC4_SLOT(12, 0x00697B80, GetAllBuildingTypesArray),
    OPENSC4_SLOT(13, 0x00697D10, GetAllBuildingFamilyTypes),
    OPENSC4_SLOT(14, 0x00697DD0, GetBuildingKeyFromType),
    OPENSC4_SLOT(15, 0x00697E30, IsBuildingTypeValid),
    OPENSC4_SLOT(16, 0x00697E70, IsBuildingOrFamilyTypeValid),
    OPENSC4_SLOT(17, 0x00699C50, GetBuildingTypesByFilter),
    OPENSC4_SLOT(18, 0x00697EF0, GetBuildingTypesByFamily),
    OPENSC4_SLOT(19, 0x00699D20, GetBuildingTypesByFamilySet),
    OPENSC4_SLOT(20, 0x00697FE0, IsBuildingInFamily),
    OPENSC4_SLOT(21, 0x00698050, GetAllFoundationTypes),
    OPENSC4_SLOT(25, 0x00695770, GetCostOfDemolition),
    OPENSC4_SLOT(27, 0x00698180, GetAllLotRetainingWallTypes),
    OPENSC4_SLOT(28, 0x00698250, GetLotRetainingWallKeyFromType),
    OPENSC4_SLOT(30, 0x00698D60, GetDeveloperType),
    OPENSC4_SLOT(31, 0x00698D80, GetWealth),
    OPENSC4_SLOT(32, 0x00696C80, GetDeveloperByType),
    OPENSC4_SLOT(33, 0x006958B0, GetDeveloper),
    OPENSC4_SLOT(34, 0x00698CD0, GetDevelopers),
    OPENSC4_SLOT(36, 0x00698CE0, GetLotDeveloper),
    OPENSC4_SLOT(37, 0x0069AB20, GetExemplarName),
    OPENSC4_SLOT(38, 0x0069AB00, GetBuildingName),
    OPENSC4_SLOT(39, 0x0069CA60, GetBuildingProfile),
    OPENSC4_SLOT(40, 0x006985C0, GetBuildingProfilePtr),
    OPENSC4_SLOT(41, 0x00698CF0, GetBuildingMap),
    OPENSC4_SLOT(42, 0x00698D90, GetBuildingMapKey),
    OPENSC4_SLOT(43, 0x0069CCB0, GetBuildingMapKeyOfBuilding),
    OPENSC4_SLOT(45, 0x00696C20, GetMaxCapacity),
    OPENSC4_SLOT(48, 0x006958E0, GetFreeConstructionResources),
    OPENSC4_SLOT(49, 0x00695910, GetTotalConstructionResources),
    OPENSC4_SLOT(50, 0x00695920, ClaimConstructionResources),
    OPENSC4_SLOT(51, 0x00695980, ReleaseConstructionResources),
    OPENSC4_SLOT(52, 0x00696CC0, GetDayOfLastSuccessfulBuild),
    OPENSC4_SLOT(53, 0x00696040, GetStagePopulationMultiplier),
    OPENSC4_SLOT(55, 0x00698D00, CheatIgnorePower),
    OPENSC4_SLOT(56, 0x00698D10, CheatIgnoreCaps),
    OPENSC4_SLOT(58, 0x00698D20, CheatCoffeeBar),
};

#undef OPENSC4_SLOT

template <std::size_t N>
bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (const Slot& slot : slots) {
        const auto* const vtable = reinterpret_cast<const std::uintptr_t*>(slot.vtable);
        if (vtable[slot.index] != slot.original) {
            log::Write("%s: slot %d of %08X holds %08X, expected %08X; not installing", what, slot.index,
                       static_cast<unsigned>(slot.vtable), static_cast<unsigned>(vtable[slot.index]),
                       static_cast<unsigned>(slot.original));
            return false;
        }
    }
    for (const Slot& slot : slots) {
        auto* const entry = reinterpret_cast<std::uintptr_t*>(slot.vtable) + slot.index;
        DWORD old = 0;
        if (!VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old))
            return false;
        *entry = reinterpret_cast<std::uintptr_t>(slot.ours);
        VirtualProtect(entry, sizeof(*entry), old, &old);
    }
    log::Write("%s: %u slots are ours", what, static_cast<unsigned>(N));
    return true;
}

bool Install(registry::Mode) { return SwapSlots(kSlots, "development sim queries"); }

const bool g_registered = registry::Register(
    {"simulators.development.queries", "simulators/development", registry::Kind::Vtable,
     registry::Phase::PostAppInit, addr::cSC4BuildingDevelopmentSimulator_vftable,
     "Our cSC4BuildingDevelopmentSimulator queries: building, family, foundation and wall tables, profiles, "
     "names, developers, capacities, construction resources, cheats (docs/development.md)",
     false, Install});

} // namespace
} // namespace opensc4::development_sim
