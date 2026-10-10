// Shared between the parts of our cSC4LotConfigurationManager (build 641):
// the class map, the vtables slot by slot, the fields and the containers it
// keeps. The manager holds every lot configuration (cSC4LotConfiguration,
// the game's own class: 0xC0 bytes, vtable 0x00A8CEB8) by ID, loaded from
// the exemplars of group 0xA8FBD372, and answers the developers' queries by
// family, size and filter. Written in two files: lotconfig_sim.cpp (loading,
// messages, the save record, our object) and lotconfig_queries.cpp (the
// queries, the footprints, the base filter).
//
// The class: class ID 0x299062C0, 0x2C bytes, constructor 0x0048A1B0 (only
// reached by the jump from the factory 0x0048A520, registered at
// 0x00460400), destructor 0x00489D70, deleting destructor 0x0048A220. Code
// 0x00489650 to 0x0048C050 (0x004892B0 to 0x00489650 is the lot
// configuration's, 0x0048C060 on another class's); in between lie the base
// filter's four functions (0x00489660, 0x00489670, 0x004897A0, 0x00489A50)
// and generic sort code shared with other classes (0x00489740, 0x004899F0,
// 0x00489C90, 0x0048A100, 0x0048A460, 0x0048A4B0, 0x0048AC40, 0x0048ACF0).
// The city makes one per city (class object 0x299062C0 at 0x0045AC27 for a
// new city, a saved reference at 0x0045CDA5 for a loaded one) and keeps it
// at +0x148; the city's slot 37 (+0x94) and the global 0x00B43D18 give it.
// The configurations themselves are static (the map 0x00B216C8): loaded at
// the first Init of the game and again only when the option byte
// 0x00B43CD8+0xF16 has changed.
//
// QueryInterface (0x004896A0): IIDs 0x00000001 and 0xC8EDBEC0
// (cISC4LotConfigurationManager) the object; 0xE4FDA3D4 (cIGZSerializable)
// +0x8. The message target part at +0x4 has no IID.
//
// --- +0x0: cISC4LotConfigurationManager, vtable 0x00A8EC58, 29 slots ------
//    0 QueryInterface 0x004896A0        1 AddRef 0x004F7BA0 (shared code)
//    2 Release 0x004897C0               3 Init 0x0048A5E0
//    4 Shutdown 0x004897F0
//    5 AddLotConfiguration(key) 0x00489A60 (ret 4)
//    6 AddLotConfiguration(configuration, bool add) 0x0048A690 (ret 8)
//    7 RemoveLotConfiguration(id) 0x0048A2D0
//    8 GetLotConfiguration(id) 0x0048A320
//    9 GetLotConfigurationsByFilter(set, filter) 0x0048B1A0
//   10 GetLotConfigurationsByFamily(set, family) 0x0048A7E0
//   11 GetLotConfigurationsBySize(set, width, depth) 0x0048A8B0
//   12 ClearLotConfigurationSet(set) 0x0048A370
//   13 GetLotConfigurationIDsByFilter(set, filter) 0x0048A9A0
//   14 GetLotConfigurationIDsByFamily(set, family) 0x0048AA80
//   15 GetLotConfigurationIDsBySize(set, width, depth) 0x0048AB50
//   16 LotConfigurationHasBuildingFamily(configuration, family, object**)
//      0x00489AA0
//   17 LotConfigurationHasBuildingType(configuration, type, object**)
//      0x00489B40
//   18 RegisterLotConfigurationFilter(filter) 0x0048A660
//   19 UnregisterLotConfigurationFilter(filter) 0x0048A3F0
//   20 GetLotConfigurationMap 0x00489720 (the address 0x00B216C8)
//   21 GetLotConfigurationFamilyIDSet 0x00489730 (the address 0x00B216DC)
//   22 CreateGenericLotConfiguration(configuration**, building type)
//      0x0048BCF0
//   23 FixLotConfiguration(configuration) 0x0048B3B0 (a code 0 to 6 in EAX)
//   24 GetMaxFootprintForBuildingFamilies(ids, rect) 0x00489E10
//   25 GetMaxFootprintForFloraFamilies(ids, rect) 0x0048B860
//   26 GetMaxFootprintForPropFamilies(ids, rect) 0x00489F50
//   27 GetObjectIDArray(object, ids) 0x0048BA10
//   28 the deleting destructor 0x0048A220
// --- +0x4: cIGZMessageTarget2, vtable 0x00A8EC48, 4 slots -----------------
//    thunks 0x00489DD0, 0x0068D230, 0x00489DF0; 3 DoMessage 0x0048BC50
// --- +0x8: cIGZSerializable, vtable 0x00A8EC30, 6 slots -------------------
//    thunks 0x00489DE0, 0x0068D250, 0x00489E00; 3 Write 0x00489850;
//    4 Read 0x004898A0; 5 GetGZCLSID 0x00489650
//
// Not virtual: CreateLotConfigurationFromKey 0x00489910 and
// SaveLotConfiguration 0x00489700 (both also called by the lot
// configuration's code), LoadAll 0x0048A540 with its key callback
// 0x00489990 and cleanup 0x0048A240, UpdateConfiguration 0x0048AD40,
// InvalidateFilterCaches 0x0048A080.
//
// Fields (offsets from the object's start; DoMessage gets object + 4 and
// Write/Read object + 8):
//   +0x0C uint8  initialized (Init sets it first, Shutdown clears it last)
//   +0x10 uint32 reference count (unsigned compare in Release)
//   +0x14 the building development simulator (0x00B43D68), +0x18 the flora
//         simulator (0x00B43D80), +0x1C the prop manager (0x00B43D10):
//         referenced by Init or read from the save, released by Shutdown
//   +0x20 the registered filters: a map (head, size at +0x24; 0x18-byte
//         nodes) from the filter to a vector of the configurations it
//         accepts, sorted by address (null until first asked for; every
//         vector is freed when a configuration is added or removed)
//   +0x28 never written
//
// A configuration object (cSC4LotConfigurationObject, 0x34 bytes, in the
// configuration's vector at +0x90): +0x0 kind (0 building, 1 prop, 4
// flora), +0x2 orientation, +0x4/+0xC the centre x and z, +0x10/+0x14 the
// largest and +0x18/+0x1C the least x and z (floats, meters in the lot),
// +0x24 its ID, +0x28 a vector of type or family IDs.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::lotconfig_sim {

inline constexpr uint32_t kClassId = 0x299062C0;
inline constexpr uint32_t kIidUnknown = 0x00000001;
inline constexpr uint32_t kIidManager = 0xC8EDBEC0;
inline constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
inline constexpr uint32_t kIidFilter = 0x68F0113A;
inline constexpr uint32_t kMessageCityReady = 0x26D31EC1;

inline constexpr std::size_t kObjectSize = 0x2C;
inline constexpr std::size_t kMessagePart = 0x4;
inline constexpr std::size_t kSerializablePart = 0x8;
inline constexpr std::size_t kSlots = 29;
inline constexpr std::size_t kMessageSlots = 4;
inline constexpr std::size_t kSerializableSlots = 6;

namespace field {
inline constexpr std::size_t kInitialized = 0x0C;
inline constexpr std::size_t kRefCount = 0x10;
inline constexpr std::size_t kDevelopment = 0x14;
inline constexpr std::size_t kFlora = 0x18;
inline constexpr std::size_t kProps = 0x1C;
inline constexpr std::size_t kFilters = 0x20;
inline constexpr std::size_t kFilterCount = 0x24;
} // namespace field

// The manager's slots (byte offsets in the vtable 0x00A8EC58).
namespace slot {
inline constexpr std::size_t kAddByKey = 0x14;
inline constexpr std::size_t kAdd = 0x18;
inline constexpr std::size_t kGet = 0x20;
inline constexpr std::size_t kByFilter = 0x24;
inline constexpr std::size_t kByFamily = 0x28;
inline constexpr std::size_t kBySize = 0x2C;
inline constexpr std::size_t kClearSet = 0x30;
inline constexpr std::size_t kIdsByFilter = 0x34;
inline constexpr std::size_t kIdsByFamily = 0x38;
inline constexpr std::size_t kIdsBySize = 0x3C;
inline constexpr std::size_t kHasBuildingFamily = 0x40;
inline constexpr std::size_t kHasBuildingType = 0x44;
inline constexpr std::size_t kRegisterFilter = 0x48;
inline constexpr std::size_t kUnregisterFilter = 0x4C;
inline constexpr std::size_t kGetMap = 0x50;
inline constexpr std::size_t kGetFamilySet = 0x54;
inline constexpr std::size_t kBuildingFootprint = 0x60;
inline constexpr std::size_t kFloraFootprint = 0x64;
inline constexpr std::size_t kPropFootprint = 0x68;
inline constexpr std::size_t kGetObjectIds = 0x6C;
} // namespace slot

// cSC4LotConfiguration (vtable 0x00A8CEB8; argument counts from each
// slot's own `ret N`): byte offsets of the slots the manager calls and the
// fields it writes itself.
namespace config {
inline constexpr std::size_t kGetId = 0x0C;            // slot 3: +0x8
inline constexpr std::size_t kBelongsToFamily = 0x14;  // slot 5 (family); ret 4
inline constexpr std::size_t kGetFamilies = 0x18;      // slot 6: the vector at +0x24
inline constexpr std::size_t kGetSize = 0x1C;          // slot 7 (uint32* width, uint32* depth): bytes +0x20, +0x21
inline constexpr std::size_t kGetObjects = 0x5C;       // slot 23: the vector at +0x90
inline constexpr std::size_t kGetBuildingObject = 0x60; // slot 24: the first object of kind 0, or null
inline constexpr std::size_t kGetRetainingWalls = 0x70; // slot 28: the vector at +0x9C
inline constexpr std::size_t kLoad = 0x7C;             // slot 31 (key, option byte); ret 8
inline constexpr std::size_t kSave = 0x80;             // slot 32 (key, segment); ret 8
inline constexpr std::size_t kSize = 0xC0;
inline constexpr std::size_t kObjectSize = 0x34;
} // namespace config

// The game's STLport hash containers (0x14 bytes): nodes {next, key, ...}.
struct HashTable {
    void* unused;
    char** buckets;
    char** bucketsEnd;
    char** bucketsCapacity;
    uint32_t count;
};

// What the hash inserts write: {node, table, inserted} (9 bytes).
struct InsertResult {
    void* node;
    void* table;
    uint32_t inserted;
};

struct Vector32 {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};

struct PtrVector {
    void** begin;
    void** end;
    void** capacity;
};

// A vector of 0x34-byte configuration objects.
struct ObjectVector {
    char* begin;
    char* end;
    char* capacity;
};

inline HashTable* Map() { return reinterpret_cast<HashTable*>(addr::cSC4LotConfigurationManager_Map); }
inline HashTable* FamilySet() { return reinterpret_cast<HashTable*>(addr::cSC4LotConfigurationManager_FamilySet); }

// begin() of a hash container (as 0x004B0390): the first node of the first
// bucket that has one.
inline char* FirstNode(const HashTable* table)
{
    const uint32_t buckets = static_cast<uint32_t>(table->bucketsEnd - table->buckets);
    for (uint32_t i = 0; i < buckets; ++i)
        if (table->buckets[i])
            return table->buckets[i];
    return nullptr;
}

// The iterator's increment (inline in the game, or 0x0053DCF0): the next
// node of the bucket, else the first node of the next bucket that has one.
// `hash` is the node's key (the key shifted right by 2 in sets of pointers).
inline char* NextNode(const HashTable* table, char* node, uint32_t hash)
{
    if (char* const next = At<char*>(node, 0x0))
        return next;
    const uint32_t buckets = static_cast<uint32_t>(table->bucketsEnd - table->buckets);
    for (uint32_t i = hash % buckets + 1; i < buckets; ++i)
        if (table->buckets[i])
            return table->buckets[i];
    return nullptr;
}

// The map's node of an ID, or null (inline in the game's Add, Remove and
// GetLotConfiguration).
inline char* FindConfigurationNode(uint32_t id)
{
    const HashTable* const map = Map();
    char* node = map->buckets[id % static_cast<uint32_t>(map->bucketsEnd - map->buckets)];
    while (node && At<uint32_t>(node, 0x4) != id)
        node = At<char*>(node, 0x0);
    return node;
}

// The in-order successor in the game's red-black trees (head: color,
// parent = root, left most, right most; nodes: color, parent, left, right).
inline char* TreeNext(char* node)
{
    if (char* right = At<char*>(node, 0xC)) {
        while (At<char*>(right, 0x8))
            right = At<char*>(right, 0x8);
        return right;
    }
    char* parent = At<char*>(node, 0x4);
    while (node == At<char*>(parent, 0xC)) {
        node = parent;
        parent = At<char*>(parent, 0x4);
    }
    return At<char*>(node, 0xC) != parent ? parent : node;
}

// The objects of a configuration's vector (the game divides by 0x34 with a
// signed multiply and compares the count unsigned).
inline uint32_t ObjectCount(const ObjectVector* objects)
{
    return static_cast<uint32_t>(static_cast<int32_t>(objects->end - objects->begin) / 0x34);
}

inline uint32_t AddRefOf(void* object) { return Virtual<uint32_t>(object, 0x4); }
inline uint32_t ReleaseOf(void* object) { return Virtual<uint32_t>(object, 0x8); }

// Logs "lotconfig: first <name>" once per flag.
void FirstCall(const char* name, bool& logged);

struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};

// Swaps the slots when every one still holds its original (build 641 only).
bool SwapSlots(const Slot* slots, std::size_t count, const char* what);

// 0x0048A080 (lotconfig_sim.cpp): every registered filter's cached vector
// freed.
void InvalidateFilterCaches(char* self);

} // namespace opensc4::lotconfig_sim
