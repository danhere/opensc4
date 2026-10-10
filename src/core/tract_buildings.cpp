// Our cSC4TractDeveloper's building choice (build 641; docs/tract.md): which
// lot configurations and building types can grow on a lot candidate, and the
// development of the chosen one. Grow (tract_grow.cpp) reaches this part
// through three entries, detoured here: 0x00709650, 0x0070B440 and
// 0x0070B640. Everything else in this file is only called from these.
//
// The top-level functions:
//   GatherLotConfigurations (0x00709650): asks the lot configuration service
//     (+0x24) for every lot configuration of the developer's wealth and
//     purpose (through a filter object kept per (wealth, purpose) in the map
//     +0x130), checks each (CheckLotConfiguration: purpose, wealth, growth
//     stage, building style, size) and keeps those that pass in the set
//     +0xD8. Returns the best outcome (Success 27 when any passed, 5 when the
//     service had none).
//   Develop (0x0070B440): ChooseBuilding for a lot candidate (the lots part's
//     record), then checks the construction resources for the building's
//     capacity and develops by the candidate's kind: 2 the candidate's lot is
//     cleared and rebuilt, 3 its lots are deleted and one lot is made over
//     their rectangle (ReplaceLots), 4 its lot is deleted and a row of lots
//     is made, each developed (Subdivide). The lot gets the chosen
//     configuration and building type through the service at +0x28.
//   DevelopFarm (0x0070B640): agriculture. Floods out from the candidate's
//     rectangle over neighbouring lots that can become fields (zone, recent
//     growth, land value, lot state, the building on it, roads; each lot's
//     outcome is cached in the map +0x10C), and with at least 15 such lots
//     and the resources for 30 more, develops the farm (Develop) and turns
//     every field lot into the farm's field type (a random entry of the
//     farm exemplar's property 0x29B55F73).
//   ChooseBuilding (0x00709BA0): the building style first (0x3000 when the
//     candidate's rectangle asks for it, else a draw over the developer's
//     style percentages), then a random lot configuration among those of
//     +0xD8 that have the style and fit the candidate (the lots part's
//     0x00706500) into +0xF0, the capacity range into +0xFC/+0x100, and a
//     random building type of the configuration's families that passes
//     CheckBuildingType (purpose, wealth, style, capacity range, monopoly)
//     into +0xF4. Monopoly pieces (outcome 2) are the fallback set when the
//     developer's monopoly use is 1.
//
// Fields this file uses (offsets from the object's start), to merge into
// tract_sim.h:
//   +0x1C  the building development simulator (0x00B43D68): slot 0x38 a
//          building type's exemplar key, 0x4C the building types of a family
//          into a hash set, 0x90 an object whose slot 0x90 tells whether a
//          rectangle wants style 0x3000, 0x9C a building type's info
//          (int32 purpose +0, int32 wealth +4, uint16 capacity +8), 0xC8 the
//          construction resources for a capacity are there, 0xCC take them
//   +0x20  the land value service (0x00B43D88): slot 0x18 the value at a cell
//   +0x24  the lot configuration service (0x00B43D18): slot 0x20 a
//          configuration by building type, 0x24 the configurations a filter
//          passes into a hash set, 0x48 register a filter
//   +0x28  the lot development service (0x00B43D20): slot 0x18 (lot,
//          configuration, building type, flag) develops a lot
//   +0x2C  the lot manager (0x00B43D08): 0x24 lots around a lot, 0x28 lots
//          around a rectangle, 0x50 can a lot be made, 0x58 make a lot,
//          0x60 delete a lot
//   +0x38  the service 0x00B43DA8 (slot 0x34: road access object, its slot
//          0x1C (x, z)); +0x3C the zone manager (slot 0x30 the zone at a cell)
//   +0x6C  uint8  tuning bool 0x47E2C540: a farm may take a lot of another
//          wealth (DevelopFarm's wealth test)
//   +0x80  the growth developer of this Grow (profile word 0; slots 0x1C
//          purpose, 0x20 wealth, 0x24 construction info, 0x64 monopoly
//          use, 0x68 the style percentages map)
//   +0x8C  int32  the profile's growth stage (byte +0xC of the profile)
//   +0x98, +0x9C  int32 wealth and purpose of the construction info Grow
//          fills at +0x90 (developer slot 0x24)
//   +0xB4  int32  (uint16 from Grow) added to +0xBC for the largest size
//   +0xB8  int32  the capacity Grow wants (at least 1)
//   +0xBC  int32  the capacity Grow may add; +0xC0 int32 the capacity added
//          (Develop adds, ReplaceLots/Subdivide subtract what they remove);
//          +0xC4 int32 the capacity Subdivide's row added
//   +0xC8  uint8  set by Grow on its first pass: no minimum size test
//   +0xD8  set<cISC4LotConfiguration*> (0x14-byte nodes, size +0xDC):
//          the configurations GatherLotConfigurations passed
//   +0xF0  the chosen lot configuration; +0xF4 uint32 the chosen building
//          type; +0xF8 int32 the land value of the last lot that failed it
//   +0xFC, +0x100 int32 the smallest and largest capacity ChooseBuilding
//          takes
//   +0x10C map<cISC4Lot*, int32 outcome> (0x18-byte nodes, value +0x14;
//          size +0x110; unsigned keys): DevelopFarm's cached lot outcomes
//   +0x118 vector<uint32> active styles (end +0x11C), +0x124 the current
//          style's index, +0x12C one style at a time
//   +0x130 map<pair<int32 wealth, int32 purpose>, filter*> (0x1C-byte
//          nodes, key +0x10/+0x14, value +0x18; size +0x134; ours: At,
//          InsertHint, InsertUnique, InsertNode)
//   +0x170 vector<vector<int32>>: 13 rows of 28 outcome counters (rows are
//          12-byte vectors; Grow sizes each to 28 int32 and clears them).
//          Rows used here: 2 GatherLotConfigurations, 6 Develop, 7
//          CheckLotConfiguration, 8 DevelopFarm, 10 ChooseBuilding's style
//          misses, 12 CheckBuildingType.
//
// The lot candidate (the lots part's record; offsets used here): +0 kind
// (2, 3, 4), +4 cISC4Lot*, +8/+0xC the first cell (x, z), +0x10 facing
// (0-3), +0x14/+0x15 uint8 a new lot's sides in cells, +0x18 the header of a
// set<cISC4Lot*> (value +0x10), +0x24 int32 the capacity it replaces, +0x38
// rectangle (x0, z0, x1, z1), +0x6C uint8 the number of lots in a row,
// +0xA0 uint8 passed on to the lots part's 0x00706500.
#include "core/tract_sim.h"

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

namespace opensc4::tract_sim {
namespace {

namespace local {
constexpr std::size_t kBuildings = 0x1C;
constexpr std::size_t kLandValue = 0x20;
constexpr std::size_t kConfigurations = 0x24;
constexpr std::size_t kLotDevelopment = 0x28;
constexpr std::size_t kLots = 0x2C;
constexpr std::size_t kService38 = 0x38;
constexpr std::size_t kZones = 0x3C;
constexpr std::size_t kFarmAnyWealth = 0x6C;
constexpr std::size_t kDeveloper = 0x80;
constexpr std::size_t kStage = 0x8C;
constexpr std::size_t kInfoWealth = 0x98;
constexpr std::size_t kInfoPurpose = 0x9C;
constexpr std::size_t kSizeB4 = 0xB4;
constexpr std::size_t kWanted = 0xB8;
constexpr std::size_t kAllowed = 0xBC;
constexpr std::size_t kAdded = 0xC0;
constexpr std::size_t kRowAdded = 0xC4;
constexpr std::size_t kFirstPass = 0xC8;
constexpr std::size_t kPassed = 0xD8;
constexpr std::size_t kConfiguration = 0xF0;
constexpr std::size_t kBuildingType = 0xF4;
constexpr std::size_t kLastLandValue = 0xF8;
constexpr std::size_t kMinCapacity = 0xFC;
constexpr std::size_t kMaxCapacity = 0x100;
constexpr std::size_t kLotOutcomes = 0x10C;
constexpr std::size_t kStyles = 0x118;
constexpr std::size_t kStylesEnd = 0x11C;
constexpr std::size_t kStyleIndex = 0x124;
constexpr std::size_t kOneAtATime = 0x12C;
constexpr std::size_t kFilters = 0x130;
constexpr std::size_t kStats = 0x170;
} // namespace local

// The lot candidate's fields.
namespace cand {
constexpr std::size_t kKind = 0x00;
constexpr std::size_t kLot = 0x04;
constexpr std::size_t kX = 0x08;
constexpr std::size_t kZ = 0x0C;
constexpr std::size_t kFacing = 0x10;
constexpr std::size_t kSide1 = 0x14;
constexpr std::size_t kSide2 = 0x15;
constexpr std::size_t kLotSet = 0x18;
constexpr std::size_t kCapacity = 0x24;
constexpr std::size_t kRect = 0x38;
constexpr std::size_t kRowCount = 0x6C;
constexpr std::size_t kFlagA0 = 0xA0;
} // namespace cand

constexpr int32_t kSuccess = 0x1B;
constexpr uint32_t kIidPropertyHolderResource = 0xA52160F5;
constexpr uint32_t kPropertyMonopolyPiece = 0x2781283F;
constexpr uint32_t kPropertyFieldTypes = 0x29B55F73;

// A red-black tree node of the game's STL (STLport): colour, parent, left,
// right, then the value at +0x10.
struct TreeNode {
    uint8_t color;
    uint8_t pad[3];
    TreeNode* parent;
    TreeNode* left;
    TreeNode* right;
};

// A tree object: the header node, the count (and an empty comparator).
struct Tree {
    TreeNode* header;
    uint32_t count;
    uint32_t comparator;
};

// The game's hash_set (STLport hashtable): an empty functor word, the bucket
// vector, the count. Nodes are (next, value).
struct HashNode {
    HashNode* next;
    uint32_t value;
};

struct HashTable {
    uint32_t functors;
    HashNode** buckets;
    HashNode** bucketsEnd;
    HashNode** bucketsCapacity;
    uint32_t count;
};

// A doubly linked list node of the game's lists (next, prev, value) and the
// list object (its head pointer).
struct ListNode {
    ListNode* next;
    ListNode* prev;
    void* value;
};

struct List {
    ListNode* head;
};

using AllocFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using RebalanceInsertFn = void(__cdecl*)(TreeNode* node, TreeNode** root);
using RebalanceEraseFn = TreeNode*(__cdecl*)(TreeNode* node, TreeNode** root, TreeNode** leftmost,
                                             TreeNode** rightmost);
using IteratorFn = void(__thiscall*)(TreeNode** iterator);
using LowerBoundFn = TreeNode*(__thiscall*)(void* tree, const int32_t* key);
using HashConstructFn = void(__thiscall*)(HashTable* table, uint32_t hint);
using HashSelfFn = void(__thiscall*)(HashTable* table);
using HashResizeFn = void(__thiscall*)(HashTable* table, uint32_t count);
using HashInsertFn = void(__thiscall*)(HashTable* table, void* result, const uint32_t* value);
using HashAssignFn = void*(__thiscall*)(HashTable* table, const HashTable* other);
using SetInsertFn = void(__thiscall*)(void* set, void* result, void* const* key);
using MapFindFn = void(__thiscall*)(void* map, TreeNode** out, void* const* key);
using MapAtFn = int32_t*(__thiscall*)(void* map, void* const* key);
using MultimapEraseFn = void(__thiscall*)(void* map, TreeNode* node);
using ListPushFn = void(__thiscall*)(List* list, void* const* object);
using ListClearFn = void(__thiscall*)(List* list);
using FitsZoneFn = bool(__cdecl*)(uint32_t constructionType, int32_t zoneType);
using HasStyleFn = bool(__cdecl*)(const uint32_t* key, uint32_t style);
using GetBoolFn = bool(__cdecl*)(void* holder, uint32_t property, uint8_t* value);
using BelowFn = uint32_t(__thiscall*)(void* random, uint32_t n);
// Other parts' functions, called at the game's address.
using PrepareFn = uint32_t(__thiscall*)(void* self, void* candidate);                  // lots, 0x007055B0
using FitsFn = int32_t(__thiscall*)(void* self, void* configuration, void* candidate, uint32_t flag); // lots, 0x00706500
using EraseSubtreeFn = void(__thiscall*)(void* tree, TreeNode* node);                  // grow, 0x007049D0

void* Alloc(std::size_t size) { return reinterpret_cast<AllocFn>(addr::SC4_stl_allocate)(size); }
void Free(void* block) { reinterpret_cast<FreeFn>(addr::SC4_Free)(block); }

uint32_t Below(uint32_t n)
{
    return reinterpret_cast<BelowFn>(addr::SC4Random_BelowLcg)(reinterpret_cast<void*>(addr::cSC4TractDeveloper_Random),
                                                               n);
}

void* Service(char* self, std::size_t field) { return At<void*>(self, field); }
void* Developer(char* self) { return At<void*>(self, local::kDeveloper); }
int32_t Purpose(char* self) { return Virtual<int32_t>(Developer(self), 0x1C); }
int32_t Wealth(char* self) { return Virtual<int32_t>(Developer(self), 0x20); }

// One of the 13 rows of outcome counters at +0x170.
void Count(char* self, int stage, int32_t outcome)
{
    char* const rows = At<char*>(self, local::kStats);
    int32_t* const row = *reinterpret_cast<int32_t**>(rows + 12 * stage);
    ++row[outcome];
}

// A building type's info (building development simulator slot 39).
const char* BuildingInfo(char* self, uint32_t type)
{
    return Virtual<const char*>(Service(self, local::kBuildings), 0x9C, type);
}

uint32_t Capacity(char* self, uint32_t type)
{
    return *reinterpret_cast<const uint16_t*>(BuildingInfo(self, type) + 8);
}

TreeNode* TreeNext(TreeNode* x)
{
    if (x->right) {
        x = x->right;
        while (x->left)
            x = x->left;
        return x;
    }
    TreeNode* y = x->parent;
    while (x == y->right) {
        x = y;
        y = y->parent;
    }
    return x->right != y ? y : x;
}

template <typename T>
T& Value(TreeNode* node, std::size_t offset = 0x10)
{
    return *reinterpret_cast<T*>(reinterpret_cast<char*>(node) + offset);
}

// A new, empty list head (0x0C bytes, linked to itself).
List NewList()
{
    auto* const head = static_cast<ListNode*>(Alloc(0xC));
    head->next = head;
    head->prev = head;
    return List{head};
}

// Empties a list of references and frees its head, as the game's inline code
// (each value released, each node freed, the head relinked, then freed).
void DestroyRefList(List& list)
{
    ListNode* const head = list.head;
    for (ListNode* node = head->next; node != head;) {
        ListNode* const next = node->next;
        if (node->value)
            Virtual<uint32_t>(node->value, 0x8);
        Free(node);
        node = next;
    }
    head->next = head;
    head->prev = head;
    Free(head);
}

// push_back of a reference, inline as the game's: a temporary reference, the
// node (its own reference), linked at the back, the temporary released.
void PushRef(List& list, void* object)
{
    Virtual<uint32_t>(object, 0x4);
    auto* const node = static_cast<ListNode*>(Alloc(0xC));
    node->value = object;
    Virtual<uint32_t>(object, 0x4);
    ListNode* const head = list.head;
    ListNode* const prev = head->prev;
    node->next = head;
    node->prev = prev;
    prev->next = node;
    head->prev = node;
    Virtual<uint32_t>(object, 0x8);
}

// Demolishes what is on the lots of a list (service 0x00B43D00 slot 7 with
// the game's flags 0x800).
void Demolish(List& list)
{
    Virtual<uint32_t>(Global<void*>(addr::SC4_Service43D00), 0x1C, 1u, &list, 0u, 0x800u, 0u, 0u, 0u, 0u);
}

// Removes a lot from the outcome map +0x10C (the game's inline lower_bound
// with unsigned keys, then _Rb_global::_Rebalance_for_erase).
void EraseNode(char* self, TreeNode* node)
{
    TreeNode* const header = At<TreeNode*>(self, local::kLotOutcomes);
    TreeNode* const freed = reinterpret_cast<RebalanceEraseFn>(addr::SC4Tree_RebalanceForErase)(
        node, &header->parent, &header->left, &header->right);
    if (freed)
        Free(freed);
    --At<int32_t>(self, local::kLotOutcomes + 4);
}

void FindAndEraseLot(char* self, void* lot)
{
    TreeNode* const header = At<TreeNode*>(self, local::kLotOutcomes);
    TreeNode* y = header;
    for (TreeNode* x = header->parent; x;) {
        if (Value<uintptr_t>(x) < reinterpret_cast<uintptr_t>(lot)) {
            x = x->right;
        } else {
            y = x;
            x = x->left;
        }
    }
    if (y == header || reinterpret_cast<uintptr_t>(lot) < Value<uintptr_t>(y))
        y = header;
    if (y != At<TreeNode*>(self, local::kLotOutcomes))
        EraseNode(self, y);
}

void EraseFoundLot(char* self, void* const* lot)
{
    TreeNode* found = nullptr;
    reinterpret_cast<MapFindFn>(addr::SC4Uint32Map_Find)(self + local::kLotOutcomes, &found, lot);
    if (found != At<TreeNode*>(self, local::kLotOutcomes))
        EraseNode(self, found);
}

// --- Hash sets ------------------------------------------------------------------------

// An empty table for 100 entries. The game's 0x00709BA0 inlines this
// (_M_initialize_buckets: the prime above 100, reserve, fill with nulls,
// count 0); 0x00709650 calls it. Same calls, same allocation.
void Construct(HashTable& table)
{
    table.buckets = nullptr;
    table.bucketsEnd = nullptr;
    table.bucketsCapacity = nullptr;
    table.count = 0;
    reinterpret_cast<HashConstructFn>(addr::SC4Uint32HashSet_Construct)(&table, 100);
}

void Destroy(HashTable& table)
{
    reinterpret_cast<HashSelfFn>(addr::SC4Uint32HashSet_Clear)(&table);
    if (table.buckets)
        Free(table.buckets);
}

uint32_t Buckets(const HashTable& table) { return static_cast<uint32_t>(table.bucketsEnd - table.buckets); }

HashNode* First(const HashTable& table)
{
    for (uint32_t i = 0; i < Buckets(table); ++i)
        if (table.buckets[i])
            return table.buckets[i];
    return nullptr;
}

// The iterator's ++: the chain, then the next non-empty bucket. Pointer
// tables hash by value >> 2 (0x0047C1A0), uint32 tables by value.
HashNode* Next(const HashTable& table, const HashNode* node, int shift)
{
    if (node->next)
        return node->next;
    const uint32_t n = Buckets(table);
    for (uint32_t i = (node->value >> shift) % n + 1; i < n; ++i)
        if (table.buckets[i])
            return table.buckets[i];
    return nullptr;
}

// --- The filter map +0x130 (map<pair<int32, int32>, filter*>) --------------------

bool Less(const int32_t* a, const int32_t* b) { return a[0] < b[0] || (a[0] == b[0] && a[1] < b[1]); }

const int32_t* Key(TreeNode* node) { return &Value<int32_t>(node); }

TreeNode* Header(char* tree) { return At<TreeNode*>(tree, 0); }

// 0x00705B80: links a new node (0x1C bytes: the 12-byte value copied) under
// y; left when y is the header, else right when w is set, else left when x
// is set, else by the key. Returns result (ret 0x14).
TreeNode** InsertNode(char* tree, TreeNode** result, TreeNode* x, TreeNode* y, const int32_t* value, TreeNode* w)
{
    static bool logged = false;
    FirstCall("tract filter map insert node", logged);
    TreeNode* const header = Header(tree);
    const bool left = y == header || (!w && (x || Less(value, Key(y))));
    auto* const z = static_cast<TreeNode*>(Alloc(0x1C));
    std::memcpy(reinterpret_cast<char*>(z) + 0x10, value, 12);
    if (left) {
        y->left = z;
        if (y == header) {
            header->parent = z;
            header->right = z;
        } else if (y == header->left) {
            header->left = z;
        }
    } else {
        y->right = z;
        if (y == header->right)
            header->right = z;
    }
    z->parent = y;
    z->left = nullptr;
    z->right = nullptr;
    reinterpret_cast<RebalanceInsertFn>(addr::SC4Tree_RebalanceForInsert)(z, &header->parent);
    ++At<uint32_t>(tree, 4);
    *result = z;
    return result;
}

// 0x00705C60: insert_unique(value): the node and whether it was inserted.
// (The game passes the loop's x, null by then, as InsertNode's x.)
TreeNode* InsertUnique(char* tree, const int32_t* value, bool* inserted)
{
    static bool logged = false;
    FirstCall("tract filter map insert unique", logged);
    TreeNode* const header = Header(tree);
    TreeNode* x = header->parent;
    TreeNode* y = header;
    bool comp = true;
    while (x) {
        y = x;
        comp = Less(value, Key(x));
        x = comp ? x->left : x->right;
    }
    TreeNode* j = y;
    TreeNode* node = nullptr;
    if (comp) {
        if (y == header->left) {
            InsertNode(tree, &node, x, y, value, nullptr);
            *inserted = true;
            return node;
        }
        reinterpret_cast<IteratorFn>(addr::SC4TreeIterator_Decrement)(&j);
    }
    if (Less(Key(j), value)) {
        InsertNode(tree, &node, x, y, value, nullptr);
        *inserted = true;
        return node;
    }
    *inserted = false;
    return j;
}

TreeNode* Unique(char* tree, const int32_t* value)
{
    bool inserted = false;
    return InsertUnique(tree, value, &inserted);
}

// 0x00706040: insert_unique(hint, value) (STLport's, the comparisons in the
// game's order).
TreeNode* InsertHint(char* tree, TreeNode* pos, const int32_t* v)
{
    static bool logged = false;
    FirstCall("tract filter map insert hint", logged);
    TreeNode* const header = Header(tree);
    TreeNode* node = nullptr;
    if (pos == header->left) {
        if (At<uint32_t>(tree, 4) == 0)
            return Unique(tree, v);
        if (Less(v, Key(pos)))
            return *InsertNode(tree, &node, pos, pos, v, nullptr);
        if (!Less(Key(pos), v))
            return pos;
        TreeNode* after = pos;
        reinterpret_cast<IteratorFn>(addr::SC4MapIterator_Next)(&after);
        if (after == header)
            return *InsertNode(tree, &node, nullptr, pos, v, pos);
        if (!Less(v, Key(after)))
            return Unique(tree, v);
        if (!pos->right)
            return *InsertNode(tree, &node, nullptr, pos, v, pos);
        return *InsertNode(tree, &node, after, after, v, nullptr);
    }
    if (pos == header) {
        TreeNode* const last = header->right;
        if (Less(Key(last), v))
            return *InsertNode(tree, &node, nullptr, last, v, pos);
        return Unique(tree, v);
    }
    TreeNode* before = pos;
    reinterpret_cast<IteratorFn>(addr::SC4TreeIterator_Decrement)(&before);
    const bool compVP = Less(v, Key(pos));
    if (compVP && Less(Key(before), v)) {
        if (!before->right)
            return *InsertNode(tree, &node, nullptr, before, v, before);
        return *InsertNode(tree, &node, pos, pos, v, nullptr);
    }
    TreeNode* after = pos;
    reinterpret_cast<IteratorFn>(addr::SC4MapIterator_Next)(&after);
    bool compPV = !compVP;
    if (!compVP) {
        compPV = Less(Key(pos), v);
        if (!compPV)
            return pos;
        if (after == header || Less(v, Key(after))) {
            if (!pos->right)
                return *InsertNode(tree, &node, nullptr, pos, v, pos);
            return *InsertNode(tree, &node, after, after, v, nullptr);
        }
    }
    if (compVP == compPV)
        return pos;
    return Unique(tree, v);
}

// 0x007074D0: operator[](key): the value's address, a null value inserted
// before the lower bound when missing.
void** FilterAt(char* tree, const int32_t* key)
{
    static bool logged = false;
    FirstCall("tract filter map at", logged);
    TreeNode* node = reinterpret_cast<LowerBoundFn>(addr::SC4PairMap_LowerBound)(tree, key);
    if (node == Header(tree) || Less(key, Key(node))) {
        const int32_t value[3] = {key[0], key[1], 0};
        node = InsertHint(tree, node, value);
    }
    return &Value<void*>(node, 0x18);
}

// --- Lot configurations ---------------------------------------------------------------

// 0x00704950: the filter's slot 3: the configuration takes the filter's
// purpose (+8) and wealth (+4).
uint32_t __fastcall FilterMatches(char* filter, void*, void* configuration)
{
    static bool logged = false;
    FirstCall("tract configuration filter", logged);
    if (Virtual<uint8_t>(configuration, 0x44, At<int32_t>(filter, 8)) &&
        Virtual<uint8_t>(configuration, 0x40, At<int32_t>(filter, 4)))
        return 1;
    return 0;
}

// 0x00704C00: checks a lot configuration (counted in row 7): purpose 7,
// wealth 8, growth stage above the profile's 14, style 9 (residential and
// commercial purposes 1-3 with active styles: the current style, or any
// active style when not one at a time), no buildings 0, too small for the
// wanted capacity at a stage below the profile's 11 (not on the first pass),
// too big 13, else success.
int32_t CheckLotConfiguration(char* self, void* configuration)
{
    static bool logged = false;
    FirstCall("CheckLotConfiguration", logged);
    const int32_t purpose = Purpose(self);
    if (!Virtual<uint8_t>(configuration, 0x44, purpose)) {
        Count(self, 7, 7);
        return 7;
    }
    if (!Virtual<uint8_t>(configuration, 0x40, Wealth(self))) {
        Count(self, 7, 8);
        return 8;
    }
    const uint8_t stage = Virtual<uint8_t>(configuration, 0x2C);
    if (static_cast<int32_t>(stage) > At<int32_t>(self, local::kStage)) {
        Count(self, 7, 0xE);
        return 0xE;
    }
    const auto* const styles = At<const uint32_t*>(self, local::kStyles);
    const auto* const stylesEnd = At<const uint32_t*>(self, local::kStylesEnd);
    if (styles != stylesEnd && purpose > 0 && purpose <= 3) {
        bool ok = false;
        if (At<uint8_t>(self, local::kOneAtATime)) {
            ok = Virtual<uint8_t>(configuration, 0x48, styles[At<int32_t>(self, local::kStyleIndex)]) != 0;
        } else {
            for (const uint32_t* style = styles; style != At<const uint32_t*>(self, local::kStylesEnd); ++style)
                if (Virtual<uint8_t>(configuration, 0x48, *style)) {
                    ok = true;
                    break;
                }
        }
        if (!ok) {
            Count(self, 7, 9);
            return 9;
        }
    }
    if (!Virtual<int32_t>(configuration, 0x60)) {
        Count(self, 7, 0);
        return 0;
    }
    const uint32_t smallest = stage == 1 ? 0u : (Virtual<uint32_t>(configuration, 0x30) & 0xFFFF);
    const uint32_t largest = Virtual<uint32_t>(configuration, 0x34) & 0xFFFF;
    if (!At<uint8_t>(self, local::kFirstPass) && static_cast<int32_t>(largest) < At<int32_t>(self, local::kWanted) &&
        static_cast<int32_t>(Virtual<uint8_t>(configuration, 0x2C)) < At<int32_t>(self, local::kStage)) {
        Count(self, 7, 0xB);
        return 0xB;
    }
    if (static_cast<int32_t>(smallest) > At<int32_t>(self, local::kAllowed) + At<int32_t>(self, local::kSizeB4)) {
        Count(self, 7, 0xD);
        return 0xD;
    }
    Count(self, 7, kSuccess);
    return kSuccess;
}

// 0x00709650: fills +0xD8 with the lot configurations that pass (see the file
// header). The filter for (wealth, purpose) is made on first use: 12 bytes
// from SC4::operator_new, the game's vtable, registered with the service.
int32_t __fastcall GatherLotConfigurations(char* self, void*)
{
    static bool logged = false;
    FirstCall("GatherLotConfigurations", logged);
    HashTable configurations{};
    Construct(configurations);
    int32_t key[2];
    key[0] = Wealth(self);
    key[1] = Purpose(self);
    void** const filter = FilterAt(self + local::kFilters, key);
    if (!*filter) {
        auto* const made = static_cast<uint32_t*>(reinterpret_cast<AllocFn>(addr::SC4_operator_new)(0xC));
        if (made) {
            void* const developer = Developer(self);
            made[0] = static_cast<uint32_t>(addr::cSC4TractDeveloper_ConfigFilter_vftable);
            made[1] = Virtual<uint32_t>(developer, 0x20);
            made[2] = Virtual<uint32_t>(developer, 0x1C);
        }
        *filter = made;
        Virtual<uint32_t>(Service(self, local::kConfigurations), 0x48, made);
    }
    if (!Virtual<uint32_t>(Service(self, local::kConfigurations), 0x24, &configurations, *filter)) {
        Destroy(configurations);
        return 5;
    }
    int32_t best = 0;
    for (HashNode* node = First(configurations); node;) {
        void* configuration = reinterpret_cast<void*>(node->value);
        const int32_t outcome = CheckLotConfiguration(self, configuration);
        Count(self, 2, outcome);
        if (outcome > best)
            best = outcome;
        if (outcome == kSuccess) {
            uint32_t result[2];
            reinterpret_cast<SetInsertFn>(addr::SC4PtrSet_Insert)(self + local::kPassed, result, &configuration);
        }
        Virtual<uint32_t>(configuration, 0x8);
        node = Next(configurations, node, 2);
    }
    Destroy(configurations);
    return best;
}

// --- Building types -------------------------------------------------------------------

// The exemplar of a building type has the style (0x005E8F70 on the type's
// key from the building development simulator's slot 14).
bool TypeHasStyle(char* self, uint32_t type, uint32_t style)
{
    uint32_t key[3] = {0, 0, 0};
    Virtual<uint32_t>(Service(self, local::kBuildings), 0x38, type, key);
    return reinterpret_cast<HasStyleFn>(addr::SC4Exemplar_HasBuildingStyle)(key, style);
}

// 0x00704E00: checks a building type (counted in row 12): purpose 7, wealth
// 8, the current style (purposes 1-3 with active styles) 9, capacity below
// +0xFC 11 or above +0x100 13, monopoly 2 (a monopoly piece when the
// developer's monopoly use is 0 or 1, or not one when it is 2), else
// success. The second argument (the candidate) is not read (ret 8).
int32_t CheckBuildingType(char* self, uint32_t type, void*)
{
    static bool logged = false;
    FirstCall("CheckBuildingType", logged);
    const auto* const info = reinterpret_cast<const int32_t*>(BuildingInfo(self, type));
    if (info[0] != Purpose(self)) {
        Count(self, 12, 7);
        return 7;
    }
    if (info[1] != Wealth(self)) {
        Count(self, 12, 8);
        return 8;
    }
    const auto* const styles = At<const uint32_t*>(self, local::kStyles);
    if (styles != At<const uint32_t*>(self, local::kStylesEnd) && info[0] > 0 && info[0] <= 3) {
        const uint32_t style = styles[At<int32_t>(self, local::kStyleIndex)];
        if (!TypeHasStyle(self, type, style)) {
            Count(self, 12, 9);
            return 9;
        }
    }
    const int32_t capacity = *reinterpret_cast<const uint16_t*>(reinterpret_cast<const char*>(info) + 8);
    if (capacity < At<int32_t>(self, local::kMinCapacity)) {
        Count(self, 12, 0xB);
        return 0xB;
    }
    if (capacity > At<int32_t>(self, local::kMaxCapacity)) {
        Count(self, 12, 0xD);
        return 0xD;
    }
    uint8_t monopoly = 0;
    uint32_t key[3] = {0, 0, 0};
    Virtual<uint32_t>(Service(self, local::kBuildings), 0x38, type, key);
    void* holder = nullptr;
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, kIidPropertyHolderResource, &holder, 0u,
                         0u)) {
        void* const properties = Virtual<void*>(holder, 0x1C);
        reinterpret_cast<GetBoolFn>(addr::SC4_GetPropertyBool)(properties, kPropertyMonopolyPiece, &monopoly);
        Virtual<uint32_t>(holder, 0x8);
    }
    const int32_t use = Virtual<int32_t>(Developer(self), 0x64);
    if (use >= 0) {
        if (use <= 1) {
            if (monopoly) {
                Count(self, 12, 2);
                return 2;
            }
        } else if (use == 2 && !monopoly) {
            Count(self, 12, 2);
            return 2;
        }
    }
    Count(self, 12, kSuccess);
    return kSuccess;
}

// Inserts a building type into a hash set, grown first for one more.
void AddType(HashTable& table, const uint32_t* type)
{
    reinterpret_cast<HashResizeFn>(addr::SC4Uint32HashSet_Resize)(&table, table.count + 1);
    uint32_t result[3]; // pair<iterator {node, table}, bool>: 9 bytes
    reinterpret_cast<HashInsertFn>(addr::SC4Uint32HashSet_Insert)(&table, result, type);
}

// Empties the local configuration set of ChooseBuilding and frees its header,
// as the game's inline destructor (the right subtrees through grow's
// 0x007049D0, the left spine here).
void DestroySet(Tree& set)
{
    if (set.count) {
        for (TreeNode* node = set.header->parent; node;) {
            reinterpret_cast<EraseSubtreeFn>(addr::cSC4TractDeveloper_Fn_007049D0)(&set, node->right);
            TreeNode* const left = node->left;
            Free(node);
            node = left;
        }
        set.header->left = set.header;
        set.header->parent = nullptr;
        set.header->right = set.header;
        set.count = 0;
    }
    if (set.header)
        Free(set.header);
}

// 0x00709BA0: chooses the lot configuration (+0xF0) and building type
// (+0xF4) for a candidate (see the file header). Returns success, 0 without
// a configuration, else the last building type's outcome.
int32_t ChooseBuilding(char* self, char* candidate)
{
    static bool logged = false;
    FirstCall("ChooseBuilding", logged);
    int32_t last = 0;
    reinterpret_cast<PrepareFn>(addr::cSC4TractDeveloper_Fn_007055B0)(self, candidate);
    uint32_t style = 0;
    const int32_t purpose = Purpose(self);
    void* const zoning = Virtual<void*>(Global<void*>(addr::SC4_Service43D68), 0x90);
    if (Virtual<uint8_t>(zoning, 0x90, candidate + cand::kRect, purpose)) {
        style = 0x3000;
    } else {
        char* const percentages = Virtual<char*>(Developer(self), 0x68);
        if (At<uint32_t>(percentages, 4)) {
            uint32_t draw = Below(100);
            TreeNode* const header = At<TreeNode*>(percentages, 0);
            for (TreeNode* node = header->left; node != header; node = TreeNext(node)) {
                // fld/fmul 100.0/fadd 12582912.0/fstp float: the float's
                // low bits are the percentage rounded to nearest.
                double scaled = static_cast<double>(Value<float>(node, 0x14)) * 100.0;
                scaled = scaled + 12582912.0;
                const float stored = static_cast<float>(scaled);
                uint32_t bits = 0;
                std::memcpy(&bits, &stored, sizeof bits);
                const uint32_t percent = bits + 0xB4C00000u;
                if (draw < percent) {
                    style = Value<uint32_t>(node);
                    break;
                }
                draw -= percent;
            }
        }
    }

    Tree fitting{};
    fitting.header = static_cast<TreeNode*>(Alloc(0x14));
    fitting.count = 0;
    fitting.header->color = 0;
    fitting.header->parent = nullptr;
    fitting.header->left = fitting.header;
    fitting.header->right = fitting.header;
    TreeNode* const passed = At<TreeNode*>(self, local::kPassed);
    for (TreeNode* node = passed->left; node != At<TreeNode*>(self, local::kPassed); node = TreeNext(node)) {
        void* configuration = Value<void*>(node);
        if (style && !Virtual<uint8_t>(configuration, 0x48, style)) {
            Count(self, 10, 9);
            continue;
        }
        const int32_t fits = reinterpret_cast<FitsFn>(addr::cSC4TractDeveloper_Fn_00706500)(
            self, configuration, candidate, At<uint8_t>(candidate, cand::kFlagA0));
        if (fits == kSuccess) {
            uint32_t result[2];
            reinterpret_cast<SetInsertFn>(addr::SC4PtrSet_Insert)(&fitting, result, &configuration);
        }
    }
    if (!fitting.count) {
        if (fitting.header)
            Free(fitting.header);
        return 0;
    }

    uint32_t pick = Below(fitting.count);
    At<void*>(self, local::kConfiguration) = nullptr;
    for (TreeNode* node = fitting.header->left; node != fitting.header; node = TreeNext(node)) {
        if (!pick) {
            At<void*>(self, local::kConfiguration) = Value<void*>(node);
            break;
        }
        --pick;
    }

    const int32_t replaced = At<int32_t>(candidate, cand::kCapacity);
    // Wraps like the game's add (signed overflow would be undefined in C++).
    int32_t smallest = static_cast<int32_t>(static_cast<uint32_t>(replaced) + 1u);
    if (smallest < replaced)
        smallest = replaced;
    if (At<int32_t>(candidate, cand::kKind) == 4)
        smallest /= static_cast<int32_t>(At<uint8_t>(candidate, cand::kRowCount));
    int32_t largest = At<int32_t>(self, local::kAllowed) - At<int32_t>(self, local::kAdded) + replaced;
    if (largest < smallest + 1)
        largest = smallest + 1;
    At<int32_t>(self, local::kMaxCapacity) = largest;
    At<int32_t>(self, local::kMinCapacity) = smallest;
    char* const families = Virtual<char*>(At<void*>(self, local::kConfiguration), 0x60);

    HashTable chosen{};
    Construct(chosen);
    HashTable monopolies{};
    Construct(monopolies);
    for (auto* family = At<uint32_t*>(families, 0x28); family != At<uint32_t*>(families, 0x2C); ++family) {
        HashTable types{};
        Construct(types);
        Virtual<uint32_t>(Service(self, local::kBuildings), 0x4C, &types, *family);
        for (HashNode* node = First(types); node; node = Next(types, node, 0)) {
            const uint32_t type = node->value;
            if (style && !TypeHasStyle(self, type, style)) {
                Count(self, 10, 9);
                continue;
            }
            last = CheckBuildingType(self, type, candidate);
            if (last == kSuccess)
                AddType(chosen, &type);
            else if (last == 2 && Virtual<int32_t>(Developer(self), 0x64) == 1)
                AddType(monopolies, &type);
        }
        Destroy(types);
    }
    if (!chosen.count) {
        if (!monopolies.count) {
            Destroy(monopolies);
            Destroy(chosen);
            DestroySet(fitting);
            return last;
        }
        reinterpret_cast<HashSelfFn>(addr::SC4Uint32HashSet_Clear)(&chosen);
        reinterpret_cast<HashAssignFn>(addr::SC4Uint32HashSet_Assign)(&chosen, &monopolies);
    }
    pick = Below(chosen.count);
    At<uint32_t>(self, local::kBuildingType) = 0;
    for (HashNode* node = First(chosen); node; node = Next(chosen, node, 0)) {
        if (!pick) {
            At<uint32_t>(self, local::kBuildingType) = node->value;
            break;
        }
        --pick;
    }
    Destroy(monopolies);
    Destroy(chosen);
    DestroySet(fitting);
    return kSuccess;
}

// --- Development ----------------------------------------------------------------------

// 0x007068E0: kind 3: every lot of the candidate's set is taken out of the
// outcome map, demolished and deleted, the capacity it had is subtracted,
// and one lot is made over the candidate's rectangle (facing -1 for a
// square industrial or farm configuration). Returns whether the lot was
// made (*made).
bool ReplaceLots(char* self, char* candidate, void** made)
{
    static bool logged = false;
    FirstCall("ReplaceLots", logged);
    TreeNode* const lots = At<TreeNode*>(candidate, cand::kLotSet);
    for (TreeNode* node = lots->left; node != At<TreeNode*>(candidate, cand::kLotSet); node = TreeNext(node)) {
        void* const lot = Value<void*>(node);
        Virtual<uint32_t>(lot, 0x4);
        FindAndEraseLot(self, lot);
        List list = NewList();
        PushRef(list, lot);
        Demolish(list);
        Virtual<uint8_t>(Service(self, local::kLots), 0x60, lot);
        Virtual<uint32_t>(lot, 0x8);
        DestroyRefList(list);
    }
    At<int32_t>(self, local::kAdded) -= At<int32_t>(candidate, cand::kCapacity);
    *made = nullptr;
    int32_t facing = At<int32_t>(candidate, cand::kFacing);
    const int32_t purpose = Purpose(self);
    if (purpose >= 5 && purpose <= 8) {
        // Out arguments in the game's argument slots: they hold the
        // candidate and `made` until the slot writes them.
        uint32_t a = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(candidate));
        uint32_t b = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(made));
        Virtual<uint32_t>(At<void*>(self, local::kConfiguration), 0x1C, &a, &b);
        if (a == b)
            facing = -1;
    }
    const int32_t* const rect = &At<int32_t>(candidate, cand::kRect);
    Virtual<uint8_t>(Service(self, local::kLots), 0x58, rect[0], rect[1], rect[2] - rect[0] + 1,
                     rect[3] - rect[1] + 1, facing, made, 0u, 0u, 0u);
    if (void* const terrain = Global<void*>(addr::SC4_Terrain)) {
        int32_t area[4] = {rect[0], rect[1], rect[2], rect[3]};
        Virtual<uint32_t>(terrain, 0x110, 0u, 0u, area, 0u);
    }
    return *made != nullptr;
}

// 0x0070A9B0: kind 4: the candidate's lot is taken out of the outcome map,
// demolished and deleted, and a row of the candidate's count of lots is
// made from its first cell, each side by side along the facing; while the
// capacity added stays below what Grow allows, each lot is developed with
// the chosen configuration and a building type chosen again for every lot
// after the first.
void Subdivide(char* self, char* candidate)
{
    static bool logged = false;
    FirstCall("Subdivide", logged);
    // The lot's slot 22 with two out arguments the game never reads (a byte
    // local and the argument slot, which holds the candidate).
    uint8_t unusedByte = 0;
    uint32_t unusedWord = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(candidate));
    Virtual<uint32_t>(At<void*>(candidate, cand::kLot), 0x58, &unusedByte, &unusedWord);
    int32_t x = At<int32_t>(candidate, cand::kX);
    int32_t z = At<int32_t>(candidate, cand::kZ);
    int32_t dx = 0;
    int32_t dz = 0;
    // A facing above 3 leaves the sides as the stack had them; zero here.
    int32_t width = 0;
    int32_t depth = 0;
    const int32_t side1 = At<uint8_t>(candidate, cand::kSide1);
    const int32_t side2 = At<uint8_t>(candidate, cand::kSide2);
    switch (At<uint32_t>(candidate, cand::kFacing)) {
    case 0:
        dx = -side1;
        x += 1 - side1;
        width = side1;
        depth = side2;
        break;
    case 1:
        dz = -side1;
        x += 1 - side2;
        z += 1 - side1;
        width = side2;
        depth = side1;
        break;
    case 2:
        z += 1 - side2;
        dx = side1;
        width = side1;
        depth = side2;
        break;
    case 3:
        dz = side1;
        width = side2;
        depth = side1;
        break;
    default:
        break;
    }
    EraseFoundLot(self, &At<void*>(candidate, cand::kLot));
    List list = NewList();
    void* const old = At<void*>(candidate, cand::kLot);
    if (old)
        Virtual<uint32_t>(old, 0x4);
    {
        auto* const node = static_cast<ListNode*>(Alloc(0xC));
        node->value = old;
        if (old)
            Virtual<uint32_t>(old, 0x4);
        ListNode* const head = list.head;
        ListNode* const prev = head->prev;
        node->next = head;
        node->prev = prev;
        prev->next = node;
        head->prev = node;
    }
    if (old)
        Virtual<uint32_t>(old, 0x8);
    Demolish(list);
    At<int32_t>(self, local::kAdded) -= At<int32_t>(candidate, cand::kCapacity);
    Virtual<uint8_t>(Service(self, local::kLots), 0x60, At<void*>(candidate, cand::kLot));
    int32_t limit = At<int32_t>(self, local::kAllowed) - At<int32_t>(self, local::kAdded) +
                    At<int32_t>(candidate, cand::kCapacity);
    if (limit == 0)
        limit = 1;
    At<int32_t>(self, local::kRowAdded) = 0;
    for (int32_t i = 0; i < static_cast<int32_t>(At<uint8_t>(candidate, cand::kRowCount)); ++i) {
        void* lot = nullptr;
        Virtual<uint8_t>(Service(self, local::kLots), 0x58, x, z, width, depth, At<int32_t>(candidate, cand::kFacing),
                         &lot, 0u, 0u, 0u);
        if (lot) {
            if (At<int32_t>(self, local::kAdded) < limit) {
                bool ok = true;
                if (i > 0)
                    ok = ChooseBuilding(self, candidate) == kSuccess;
                const uint32_t capacity = Capacity(self, At<uint32_t>(self, local::kBuildingType));
                if (ok && Virtual<uint8_t>(Service(self, local::kBuildings), 0xC8, capacity)) {
                    Virtual<uint32_t>(Service(self, local::kLotDevelopment), 0x18, lot,
                                      At<void*>(self, local::kConfiguration), At<uint32_t>(self, local::kBuildingType),
                                      1u);
                    At<int32_t>(self, local::kAdded) += static_cast<int32_t>(capacity);
                    At<int32_t>(self, local::kRowAdded) += static_cast<int32_t>(capacity);
                }
            }
            Virtual<uint32_t>(lot, 0x8);
        }
        x += dx;
        z += dz;
    }
    DestroyRefList(list);
}

// 0x0070B440: chooses a building for the candidate and develops it (see the
// file header). Returns the outcome (ret 4).
int32_t __fastcall Develop(char* self, void*, char* candidate)
{
    static bool logged = false;
    FirstCall("Develop", logged);
    void* lot = nullptr;
    const int32_t result = ChooseBuilding(self, candidate);
    if (result != kSuccess)
        return result;
    const uint32_t capacity = Capacity(self, At<uint32_t>(self, local::kBuildingType));
    if (!Virtual<uint8_t>(Service(self, local::kBuildings), 0xC8, capacity)) {
        Count(self, 6, 0x14);
        return 0x14;
    }
    switch (At<int32_t>(candidate, cand::kKind)) {
    case 4:
        Virtual<uint32_t>(Service(self, local::kBuildings), 0xCC, capacity);
        Subdivide(self, candidate);
        return kSuccess;
    case 3:
        ReplaceLots(self, candidate, &lot);
        break;
    case 2: {
        lot = At<void*>(candidate, cand::kLot);
        Virtual<uint32_t>(lot, 0x4);
        TreeNode* found = nullptr;
        reinterpret_cast<MapFindFn>(addr::SC4Uint32Map_Find)(self + local::kLotOutcomes, &found, &lot);
        if (found != At<TreeNode*>(self, local::kLotOutcomes))
            reinterpret_cast<MultimapEraseFn>(addr::SC4LotMultimap_Erase)(self + local::kLotOutcomes, found);
        List list = NewList();
        void* const temporary = lot;
        if (temporary)
            Virtual<uint32_t>(temporary, 0x4);
        reinterpret_cast<ListPushFn>(addr::SC4RefList_PushBack2)(&list, &temporary);
        if (temporary)
            Virtual<uint32_t>(temporary, 0x8);
        Demolish(list);
        At<int32_t>(self, local::kAdded) -= At<int32_t>(candidate, cand::kCapacity);
        reinterpret_cast<ListClearFn>(addr::SC4RefList_Clear)(&list);
        if (list.head)
            Free(list.head);
        break;
    }
    default:
        break;
    }
    if (lot) {
        Virtual<uint32_t>(Service(self, local::kLotDevelopment), 0x18, lot, At<void*>(self, local::kConfiguration),
                          At<uint32_t>(self, local::kBuildingType), 1u);
        At<int32_t>(self, local::kAdded) += static_cast<int32_t>(capacity);
        Virtual<uint32_t>(lot, 0x8);
        return result;
    }
    Count(self, 6, 0);
    return 0;
}

// DevelopFarm's test of one lot around the farm (counted in row 8): the
// outcome, cached by the caller.
int32_t CheckFieldLot(char* self, void* lot)
{
    int32_t x = 0;
    int32_t z = 0;
    Virtual<uint32_t>(lot, 0x54, &x, &z);
    uint8_t info[0x14] = {};
    Virtual<void*>(Developer(self), 0x24, info);
    const auto zone = static_cast<int8_t>(Virtual<uint8_t>(Service(self, local::kZones), 0x30, x, z));
    uint32_t construction = 0;
    std::memcpy(&construction, info, sizeof construction);
    if (!reinterpret_cast<FitsZoneFn>(addr::SC4ConstructionType_FitsZone)(construction, zone))
        return 6;
    if (Virtual<uint8_t>(lot, 0x94) && Virtual<uint32_t>(lot, 0xA0) < 0xB4)
        return 0x13;
    const uint8_t low = info[0x10];
    const uint8_t high = info[0x11];
    if (low || high) {
        const int32_t value = Virtual<uint8_t>(Service(self, local::kLandValue), 0x18, x, z);
        if (value < low || value > high) {
            At<int32_t>(self, local::kLastLandValue) = value;
            return 0x16;
        }
    }
    const int32_t state = Virtual<int32_t>(lot, 0x84);
    if (state == 2 || state >= 5)
        return 4;
    int32_t bounds[4] = {};
    Virtual<uint32_t>(lot, 0x38, bounds);
    const int32_t made = Virtual<int32_t>(Service(self, local::kLots), 0x50, bounds[0], bounds[1],
                                          bounds[2] - bounds[0] + 1, bounds[3] - bounds[1] + 1, 0u, 0u, 0u);
    if (made != 8 && made != 7) {
        switch (made) {
        case 1:
            return 4;
        case 2:
            return 6;
        case 3:
            return 3;
        default:
            return 0;
        }
    }
    if (Virtual<uint8_t>(lot, 0x98)) {
        const uint32_t type = Virtual<uint32_t>(lot, 0xB8, 0u);
        if (!type)
            return 4;
        const auto* const theirs = reinterpret_cast<const int32_t*>(BuildingInfo(self, type));
        const int32_t purpose = theirs[0];
        if (purpose == 5 || (purpose == 7 && At<int32_t>(self, local::kInfoPurpose) == 6))
            return 0xF;
        const int32_t ours = At<int32_t>(self, local::kInfoWealth);
        if (!At<uint8_t>(self, local::kFarmAnyWealth) && ours != theirs[1])
            return 0x10;
        if (ours < theirs[1])
            return 0x10;
        if (purpose != At<int32_t>(self, local::kInfoPurpose) && ours <= theirs[1])
            return 0x10;
        if (Virtual<uint8_t>(lot, 0xAC))
            return 0x12;
        if (void* const building = Virtual<void*>(lot, 0x24)) {
            const int32_t stage = Virtual<uint8_t>(building, 0x2C);
            const bool bigger = theirs[1] == At<int32_t>(self, local::kInfoWealth)
                                    ? At<int32_t>(self, local::kStage) > stage
                                    : At<int32_t>(self, local::kStage) >= stage;
            if (!bigger)
                return 0x11;
        }
    }
    const int32_t purpose = Purpose(self);
    bool roads = false;
    if (purpose == 1)
        roads = Virtual<uint8_t>(Virtual<void*>(Service(self, local::kService38), 0x34), 0x1C, x, z) != 0;
    else if (purpose > 5 && purpose <= 8)
        roads = Virtual<uint8_t>(lot, 0x74, purpose) != 0;
    else
        roads = Virtual<uint8_t>(lot, 0x70) != 0;
    return roads ? kSuccess : 0x15;
}

bool Contains(const List& list, void* value)
{
    for (ListNode* node = list.head->next; node != list.head; node = node->next)
        if (node->value == value)
            return true;
    return false;
}

// 0x0070B640: agriculture (see the file header). Returns the farm's outcome
// from Develop, or 0 (ret 4).
int32_t __fastcall DevelopFarm(char* self, void*, char* candidate)
{
    static bool logged = false;
    FirstCall("DevelopFarm", logged);
    int32_t result = 0;
    List fields = NewList(); // lots that can become fields (no references)
    List around = NewList(); // lots met by the flood (references)
    const int32_t* const rect = &At<int32_t>(candidate, cand::kRect);
    void* const lots = Service(self, local::kLots);
    for (uint32_t flag : {1u, 2u, 4u, 8u})
        Virtual<uint8_t>(lots, 0x28, rect, &around, flag);
    for (ListNode* it = around.head->next; it != around.head; it = it->next) {
        void* lot = it->value;
        int32_t bounds[4];
        Virtual<uint32_t>(lot, 0x38, bounds);
        if (!(bounds[2] < rect[0] || bounds[0] > rect[2] || bounds[3] < rect[1] || bounds[1] > rect[3]))
            continue; // inside the farm's rectangle
        TreeNode* found = nullptr;
        reinterpret_cast<MapFindFn>(addr::SC4Uint32Map_Find)(self + local::kLotOutcomes, &found, &lot);
        int32_t outcome;
        if (found != At<TreeNode*>(self, local::kLotOutcomes)) {
            outcome = Value<int32_t>(found, 0x14);
        } else {
            outcome = CheckFieldLot(self, lot);
            *reinterpret_cast<MapAtFn>(addr::SC4Uint32Map_At)(self + local::kLotOutcomes, &lot) = outcome;
            Count(self, 8, outcome);
        }
        if (outcome != kSuccess && outcome != 0x15)
            continue;
        if (Contains(fields, lot))
            continue;
        auto* const node = static_cast<ListNode*>(Alloc(0xC));
        node->value = lot;
        ListNode* const prev = fields.head->prev;
        node->next = fields.head;
        node->prev = prev;
        prev->next = node;
        fields.head->prev = node;
        for (uint32_t flag : {1u, 2u, 4u, 8u})
            Virtual<uint8_t>(lots, 0x24, lot, &around, flag);
    }

    int32_t count = 0;
    for (ListNode* node = fields.head->next; node != fields.head; node = node->next)
        ++count;
    void* const buildings = Service(self, local::kBuildings);
    if (count >= 15 && Virtual<uint8_t>(buildings, 0xC8, static_cast<uint32_t>(count + 30))) {
        Virtual<uint32_t>(buildings, 0xCC, 0x1Eu);
        result = Develop(self, nullptr, candidate);
        if (result != kSuccess) {
            Virtual<uint32_t>(buildings, 0xCC, static_cast<uint32_t>(count));
        } else {
            uint32_t key[3] = {0, 0, 0};
            Virtual<uint32_t>(buildings, 0x38, At<uint32_t>(self, local::kBuildingType), key);
            void* holder = nullptr;
            void* property = nullptr;
            if (Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, key, kIidPropertyHolderResource,
                                 &holder, 0u, 0u))
                property = Virtual<void*>(Virtual<void*>(holder, 0x1C), 0x24, kPropertyFieldTypes);
            uint32_t fieldType = 0;
            if (property) {
                void* const variant = Virtual<void*>(property, 0x14);
                const auto* const types = Virtual<const uint32_t*>(variant, 0x19C);
                fieldType = types[Below(Virtual<uint32_t>(variant, 0x10))];
            }
            const uint32_t capacity = Capacity(self, fieldType);
            void* const configuration = Virtual<void*>(Service(self, local::kConfigurations), 0x20, fieldType);
            if (!configuration) {
                Virtual<uint32_t>(buildings, 0xCC, static_cast<uint32_t>(count));
            } else {
                for (ListNode* node = fields.head->next; node != fields.head; node = node->next) {
                    void* lot = node->value;
                    Virtual<uint32_t>(lot, 0x60, At<uint32_t>(candidate, cand::kFacing));
                    Virtual<uint32_t>(Service(self, local::kLotDevelopment), 0x18, lot, configuration, fieldType, 0u);
                    EraseFoundLot(self, &lot);
                    Virtual<uint32_t>(lot, 0x88, 3u, 0u, 0u);
                    Virtual<uint32_t>(lot, 0xDC, 0x4100u, capacity);
                }
            }
            if (holder)
                Virtual<uint32_t>(holder, 0x8);
        }
    }

    DestroyRefList(around);
    for (ListNode* node = fields.head->next; node != fields.head;) {
        ListNode* const next = node->next;
        Free(node);
        node = next;
    }
    fields.head->next = fields.head;
    fields.head->prev = fields.head;
    Free(fields.head);
    return result;
}

// --- Installation -----------------------------------------------------------------

template <typename Fn>
bool Install(std::uintptr_t address, Fn ours)
{
    const addr::Entry* entry = addr_check::Find(address);
    Fn original = nullptr;
    return entry && hook::Install(*entry, ours, &original);
}

bool SwapFilterSlot()
{
    auto* const slot = reinterpret_cast<std::uintptr_t*>(addr::cSC4TractDeveloper_ConfigFilter_vftable) + 3;
    if (*slot != addr::cSC4TractDeveloper_Fn_00704950) {
        log::Write("tract buildings: filter slot 3 holds %08X, expected %08X; not installing",
                   static_cast<unsigned>(*slot), static_cast<unsigned>(addr::cSC4TractDeveloper_Fn_00704950));
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
        return false;
    *slot = reinterpret_cast<std::uintptr_t>(&FilterMatches);
    VirtualProtect(slot, sizeof(*slot), old, &old);
    return true;
}

bool InstallBuildings(registry::Mode)
{
    return game::IsBuild641() && SwapFilterSlot() &&
           Install(addr::cSC4TractDeveloper_Fn_00709650, &GatherLotConfigurations) &&
           Install(addr::cSC4TractDeveloper_Fn_0070B440, &Develop) &&
           Install(addr::cSC4TractDeveloper_Fn_0070B640, &DevelopFarm);
}

const bool g_registered = registry::Register({
    "simulators.tract.buildings",
    "simulators/tract",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4TractDeveloper_Fn_0070B440,
    "Our cSC4TractDeveloper building choice: lot configurations, building styles and types, developing the chosen "
    "lot (new, replaced or subdivided lots, farms and their fields) (docs/tract.md)",
    false,
    InstallBuildings,
});

} // namespace
} // namespace opensc4::tract_sim
