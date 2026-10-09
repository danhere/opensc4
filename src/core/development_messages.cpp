// Our cSC4BuildingDevelopmentSimulator's messages and simulation (build 641;
// docs/development.md): DoMessage (cIGZMessageTarget2 slot 3, 0x0069CCE0)
// and everything it reaches in the class's own code: the daily step (the
// growth and decline queues, the random growth pass), the queue building
// state machine, the construction capacity, lots in a changed rectangle,
// occupant stage changes, the occupant list and the cheats. The primary
// slots it calls are called through the vtable, as the game does; the
// class's STL helpers (trees, the cell hash, the profile hash, the sets at
// 0x00B485E0) are written here again.
//
// Fields used (offsets from the object's start; names ours):
//   0x1C  the city (slot 91 +0x16C: the cell size)
//   0x28  the zone manager: slot 11 (+0x2C) dezones a cell region
//   0x2C  the lot manager: slot 7 (+0x1C) lists the lots in a rectangle
//   0x38  the terrain: slot 13 (+0x34) the height at (x, z)
//   0x40, 0x44  int32: the city's size in cells (x, z)
//   0x48  a byte grid: slot 24 (+0x60) its rows (vector of 12-byte rows)
//   0x4C  SC4List of occupants (referenced)
//   0x50  map<int32 developer type, uint8 stage> (0x006C0F30)
//   0x5C  map<uint32 developer type, developer*> (head; size +0x60)
//   0x68  a service: slot 5 (+0x14) grows a building, slot 6 (+0x18)
//         declines one
//   0x6C  a service: slot 30 (+0x78) a lot's height, slot 37 (+0x94)
//         takes a lot
//   0x70  the construction capacity (cSC4Demand): slot 18 (+0x48) sets it
//   0x74  uint32: the queue state (0 idle, 1-4)
//   0x78  the state machine's cursor in the developers map
//   0x7C, 0x80  the growth and decline queues (SC4List, 0x1C-byte nodes:
//         +8 day, +0xC x, +0x10 z, +0x14 developer, +0x18 byte)
//   0x9C  hash_map<(developer, x, z, byte), uint32 day> (buckets +0xA0,
//         count +0xAC)
//   0xB0  map<int32, int32>: the day of the last build by developer type
//   0xBC  uint32: the cool-down in days for a cell (the queue length)
//   0xC0, 0xC4  uint32: the construction capacity's bounds
//   0xC8  float: the capacity per resident
//   0xCC  int32: the weight added to every developer's growth
//   0xDC  uint8: growth through the queue (else the random pass)
//   0xDD, 0xDE, 0xE1  uint8: cheats
#include "core/development_sim.h"

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

namespace opensc4::development_sim {
namespace {

namespace local {
constexpr std::size_t kCity = 0x1C;
constexpr std::size_t kZones = 0x28;
constexpr std::size_t kLots = 0x2C;
constexpr std::size_t kTerrain = 0x38;
constexpr std::size_t kCellsX = 0x40;
constexpr std::size_t kCellsZ = 0x44;
constexpr std::size_t kGrid = 0x48;
constexpr std::size_t kOccupants = 0x4C;
constexpr std::size_t kStages = 0x50;
constexpr std::size_t kDevelopers = 0x5C;
constexpr std::size_t kGrower = 0x68;
constexpr std::size_t kLotTaker = 0x6C;
constexpr std::size_t kCapacity = 0x70;
constexpr std::size_t kState = 0x74;
constexpr std::size_t kCursor = 0x78;
constexpr std::size_t kGrowthQueue = 0x7C;
constexpr std::size_t kDeclineQueue = 0x80;
constexpr std::size_t kCells = 0x9C;
constexpr std::size_t kLastBuild = 0xB0;
constexpr std::size_t kCoolDown = 0xBC;
constexpr std::size_t kCapacityMin = 0xC0;
constexpr std::size_t kCapacityMax = 0xC4;
constexpr std::size_t kCapacityPerResident = 0xC8;
constexpr std::size_t kWeight = 0xCC;
constexpr std::size_t kQueueMode = 0xDC;
constexpr std::size_t kCheatDD = 0xDD;
constexpr std::size_t kCheatDE = 0xDE;
constexpr std::size_t kCheatCoffeeBar = 0xE1;
} // namespace local

// The cIGZMessageTarget2 part's offset in the object.
constexpr std::size_t kMessagePart = 0x8;

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* block);
using SelfFn = void(__thiscall*)(void* self);

void* Allocate(std::size_t size) { return reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate)(size); }

void Free(void* block) { reinterpret_cast<FreeFn>(addr::SC4_Free)(block); }

// __ftol (0x009EEF04): truncation; the low word of its int64.
int32_t Ftol(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

// FILD of a uint32 the game fixes up with + 2^32 when negative.
double Unsigned(uint32_t value) { return static_cast<double>(value); }

float FloatBits(uint32_t bits)
{
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// --- STLport containers ------------------------------------------------------

// A red-black tree node; the value follows at +0x10.
struct TreeNode {
    uint8_t color;
    uint8_t padding[3];
    TreeNode* parent;
    TreeNode* left;
    TreeNode* right;
};

// A tree (map or set): the head node (parent = root, left = leftmost,
// right = rightmost) and the size.
struct Tree {
    TreeNode* head;
    uint32_t count;
};

struct ListNode {
    ListNode* next;
    ListNode* prev;
};

template <typename T>
T& Value(void* node, std::size_t offset)
{
    return *reinterpret_cast<T*>(static_cast<char*>(node) + offset);
}

// The in-order successor as the game inlines it.
TreeNode* Next(TreeNode* x)
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
    if (x->right != y)
        x = y;
    return x;
}

void Increment(TreeNode** iterator)
{
    reinterpret_cast<void(__thiscall*)(TreeNode**)>(addr::SC4MapIterator_Next)(iterator);
}

void Decrement(TreeNode** iterator)
{
    reinterpret_cast<void(__thiscall*)(TreeNode**)>(addr::SC4TreeIterator_Decrement)(iterator);
}

void RebalanceForInsert(TreeNode* node, TreeNode* head)
{
    reinterpret_cast<void(__cdecl*)(TreeNode*, TreeNode**)>(addr::SC4Tree_RebalanceForInsert)(node, &head->parent);
}

// 0x00695CD0, 0x00695D90, 0x00695E10 (the same code for three node types,
// none of them owning anything): frees a subtree.
void EraseSubtree(TreeNode* x)
{
    while (x) {
        EraseSubtree(x->right);
        TreeNode* const left = x->left;
        Free(x);
        x = left;
    }
}

// A fresh head: red, no root, leftmost and rightmost the head itself.
TreeNode* NewHead()
{
    auto* const head = static_cast<TreeNode*>(Allocate(0x18));
    head->color = 0;
    head->parent = nullptr;
    head->left = head;
    head->right = head;
    return head;
}

// The game's inline clear() then the head freed (a local tree's
// destructor).
void DestroyTree(Tree& tree)
{
    if (tree.count) {
        EraseSubtree(tree.head->parent);
        tree.head->left = tree.head;
        tree.head->parent = nullptr;
        tree.head->right = tree.head;
        tree.count = 0;
    }
    if (tree.head)
        Free(tree.head);
}

// The inline clear() alone (the head kept).
void ClearTree(Tree& tree)
{
    if (!tree.count)
        return;
    EraseSubtree(tree.head->parent);
    tree.head->left = tree.head;
    tree.head->parent = nullptr;
    tree.head->right = tree.head;
    tree.count = 0;
}

// 0x00696F10, 0x00697740 (the same code): copies a subtree of 0x18-byte
// nodes (an 8-byte value at +0x10) under `parent`.
TreeNode* CopyNodes(const TreeNode* x, TreeNode* parent)
{
    auto* const top = static_cast<TreeNode*>(Allocate(0x18));
    Value<uint32_t>(top, 0x10) = Value<uint32_t>(const_cast<TreeNode*>(x), 0x10);
    Value<uint32_t>(top, 0x14) = Value<uint32_t>(const_cast<TreeNode*>(x), 0x14);
    top->color = x->color;
    top->left = nullptr;
    top->right = nullptr;
    top->parent = parent;
    if (x->right)
        top->right = CopyNodes(x->right, top);
    TreeNode* p = top;
    for (x = x->left; x; x = x->left) {
        auto* const y = static_cast<TreeNode*>(Allocate(0x18));
        Value<uint32_t>(y, 0x10) = Value<uint32_t>(const_cast<TreeNode*>(x), 0x10);
        Value<uint32_t>(y, 0x14) = Value<uint32_t>(const_cast<TreeNode*>(x), 0x14);
        y->color = x->color;
        y->left = nullptr;
        y->right = nullptr;
        p->left = y;
        y->parent = p;
        if (x->right)
            y->right = CopyNodes(x->right, y);
        p = y;
    }
    return top;
}

// 0x006989D0, 0x00698A80 (the same code): copy-constructs a tree of
// 0x18-byte nodes.
void CopyTree(Tree* to, const Tree* from)
{
    to->head = nullptr;
    TreeNode* const head = static_cast<TreeNode*>(Allocate(0x18));
    to->head = head;
    to->count = 0;
    head->color = 0;
    if (!from->head->parent) {
        head->parent = nullptr;
        head->left = head;
        head->right = head;
        to->count = from->count;
        return;
    }
    head->parent = CopyNodes(from->head->parent, head);
    TreeNode* x = head->parent;
    while (x->left)
        x = x->left;
    head->left = x;
    x = head->parent;
    while (x->right)
        x = x->right;
    head->right = x;
    to->count = from->count;
}

// 0x00696240: erase(first, last) on a map of 0x18-byte nodes.
void EraseRange(Tree* tree, TreeNode* first, TreeNode* last)
{
    TreeNode* const head = tree->head;
    if (first == head->left && last == head) {
        if (!tree->count)
            return;
        EraseSubtree(head->parent);
        tree->head->left = tree->head;
        tree->head->parent = nullptr;
        tree->head->right = tree->head;
        tree->count = 0;
        return;
    }
    using RebalanceFn = TreeNode*(__cdecl*)(TreeNode*, TreeNode**, TreeNode**, TreeNode**);
    while (first != last) {
        TreeNode* const node = first;
        first = Next(first);
        TreeNode* const h = tree->head;
        if (TreeNode* const freed =
                reinterpret_cast<RebalanceFn>(addr::SC4Tree_RebalanceForErase)(node, &h->parent, &h->left, &h->right))
            Free(freed);
        --tree->count;
    }
}

// --- The sets at 0x00B485E0 ----------------------------------------------------
//
// map<uint32 developer id, set<pair<int32, int32>>>: an outer node (0x20
// bytes) holds the key at +0x10 and its set at +0x14; a set node (0x18
// bytes) holds the pair at +0x10.

struct SetPair {
    uint32_t key;
    Tree set;
};

Tree* Sets() { return reinterpret_cast<Tree*>(addr::cSC4BuildingDevelopmentSimulator_Sets); }

// 0x00695F70 (also called from six places outside this class): find() on a
// set of int32 pairs; the head when missing.
TreeNode* __fastcall FindPair(void* set, void*, const int32_t* key)
{
    static bool logged = false;
    FirstCall("FindPair", logged);
    TreeNode* const head = static_cast<Tree*>(set)->head;
    TreeNode* y = head;
    for (TreeNode* x = head->parent; x;) {
        const int32_t first = Value<int32_t>(x, 0x10);
        if (key[0] > first || (key[0] == first && Value<int32_t>(x, 0x14) < key[1])) {
            x = x->right;
        } else {
            y = x;
            x = x->left;
        }
    }
    if (y == head)
        return head;
    const int32_t first = Value<int32_t>(y, 0x10);
    if (first > key[0])
        return head;
    if (first < key[0])
        return y;
    return key[1] >= Value<int32_t>(y, 0x14) ? y : head;
}

// 0x00696060: links a new set node for `value` under `y` (left when y is
// the head, x is set or the value is less; right when `right` is set or
// otherwise), then rebalances.
TreeNode* InsertPairAt(Tree* set, TreeNode* x, TreeNode* y, const int32_t* value, TreeNode* right)
{
    TreeNode* const head = set->head;
    TreeNode* z;
    bool left;
    if (y == head) {
        left = true;
    } else if (right) {
        left = false;
    } else if (x) {
        left = true;
    } else {
        const int32_t first = Value<int32_t>(y, 0x10);
        if (first > value[0])
            left = true;
        else if (first < value[0])
            left = false;
        else
            left = value[1] < Value<int32_t>(y, 0x14);
    }
    z = static_cast<TreeNode*>(Allocate(0x18));
    Value<int32_t>(z, 0x10) = value[0];
    Value<int32_t>(z, 0x14) = value[1];
    if (left) {
        y->left = z;
        if (y == set->head) {
            set->head->parent = z;
            set->head->right = z;
        } else if (y == set->head->left) {
            set->head->left = z;
        }
    } else {
        y->right = z;
        if (y == set->head->right)
            set->head->right = z;
    }
    z->parent = y;
    z->left = nullptr;
    z->right = nullptr;
    RebalanceForInsert(z, set->head);
    ++set->count;
    return z;
}

// 0x00696300: insert_unique on a set of int32 pairs (ordered by the first,
// then the second). Returns the node; *inserted whether it is new.
TreeNode* InsertPair(Tree* set, const int32_t* value, bool* inserted)
{
    TreeNode* const head = set->head;
    TreeNode* y = head;
    TreeNode* x = head->parent;
    bool less = true;
    while (x) {
        y = x;
        const int32_t first = Value<int32_t>(x, 0x10);
        less = first > value[0] || (first == value[0] && value[1] < Value<int32_t>(x, 0x14));
        x = less ? x->left : x->right;
    }
    TreeNode* j = y;
    if (less) {
        if (y == head->left) {
            *inserted = true;
            return InsertPairAt(set, x, y, value, nullptr);
        }
        Decrement(&j);
    }
    const int32_t first = Value<int32_t>(j, 0x10);
    if (value[0] > first || (value[0] == first && Value<int32_t>(j, 0x14) < value[1])) {
        *inserted = true;
        return InsertPairAt(set, x, y, value, nullptr);
    }
    *inserted = false;
    return j;
}

// 0x0069A610: links a new outer node (key and a copy of the set) under `y`.
TreeNode* InsertSetAt(Tree* map, TreeNode* x, TreeNode* y, const SetPair* value, TreeNode* right)
{
    auto make = [value]() {
        auto* const z = static_cast<TreeNode*>(Allocate(0x20));
        Value<uint32_t>(z, 0x10) = value->key;
        CopyTree(&Value<Tree>(z, 0x14), &value->set);
        return z;
    };
    TreeNode* z;
    bool left;
    if (y == map->head)
        left = true;
    else if (right)
        left = false;
    else if (x)
        left = true;
    else
        left = value->key < Value<uint32_t>(y, 0x10);
    if (left) {
        z = make();
        y->left = z;
        if (y == map->head) {
            map->head->parent = z;
            map->head->right = z;
        } else if (y == map->head->left) {
            map->head->left = z;
        }
    } else {
        z = make();
        y->right = z;
        if (y == map->head->right)
            map->head->right = z;
    }
    z->parent = y;
    z->left = nullptr;
    z->right = nullptr;
    RebalanceForInsert(z, map->head);
    ++map->count;
    return z;
}

// 0x0069A6E0: insert_unique on the map of sets (unsigned keys).
TreeNode* InsertSetUnique(Tree* map, const SetPair* value)
{
    TreeNode* const head = map->head;
    TreeNode* y = head;
    TreeNode* x = head->parent;
    bool less = true;
    while (x) {
        less = value->key < Value<uint32_t>(x, 0x10);
        y = x;
        x = less ? x->left : x->right;
    }
    TreeNode* j = y;
    if (less) {
        if (y == head->left)
            return InsertSetAt(map, x, y, value, nullptr);
        Decrement(&j);
    }
    if (Value<uint32_t>(j, 0x10) < value->key)
        return InsertSetAt(map, x, y, value, nullptr);
    return j;
}

// 0x0069A8F0: insert(hint, value) on the map of sets (STLport's
// insert_unique with a position).
TreeNode* InsertSetHint(Tree* map, TreeNode* hint, const SetPair* value)
{
    const uint32_t key = value->key;
    TreeNode* const head = map->head;
    if (hint == head->left) {
        if (!map->count)
            return InsertSetUnique(map, value);
        if (Value<uint32_t>(hint, 0x10) > key)
            return InsertSetAt(map, hint, hint, value, nullptr);
        if (!(Value<uint32_t>(hint, 0x10) < key))
            return hint;
        TreeNode* after = hint;
        Increment(&after);
        if (after == map->head)
            return InsertSetAt(map, nullptr, hint, value, hint);
        if (key < Value<uint32_t>(after, 0x10)) {
            if (!hint->right)
                return InsertSetAt(map, nullptr, hint, value, hint);
            return InsertSetAt(map, after, after, value, nullptr);
        }
        return InsertSetUnique(map, value);
    }
    if (hint == head) {
        TreeNode* const rightmost = head->right;
        if (Value<uint32_t>(rightmost, 0x10) < key)
            return InsertSetAt(map, nullptr, rightmost, value, hint);
        return InsertSetUnique(map, value);
    }
    TreeNode* before = hint;
    Decrement(&before);
    const bool beforeHint = key < Value<uint32_t>(hint, 0x10);
    if (beforeHint && Value<uint32_t>(before, 0x10) < key) {
        if (!before->right)
            return InsertSetAt(map, nullptr, before, value, before);
        return InsertSetAt(map, hint, hint, value, nullptr);
    }
    TreeNode* after = hint;
    Increment(&after);
    bool afterHint = !beforeHint;
    if (!beforeHint) {
        afterHint = Value<uint32_t>(hint, 0x10) < key;
        if (!afterHint)
            return hint;
        if (after == map->head || key < Value<uint32_t>(after, 0x10)) {
            if (!hint->right)
                return InsertSetAt(map, nullptr, hint, value, hint);
            return InsertSetAt(map, after, after, value, nullptr);
        }
    }
    if (beforeHint == afterHint)
        return hint;
    return InsertSetUnique(map, value);
}

// 0x0069AB40: the set of `*key` (operator[]): a new empty set inserted
// (copied, hinted at the lower bound) when missing.
Tree* SetsAt(const uint32_t* key)
{
    Tree* const map = Sets();
    TreeNode* const head = map->head;
    TreeNode* y = head;
    for (TreeNode* x = head->parent; x;) {
        if (Value<uint32_t>(x, 0x10) < *key) {
            x = x->right;
        } else {
            y = x;
            x = x->left;
        }
    }
    if (y != head && *key >= Value<uint32_t>(y, 0x10))
        return &Value<Tree>(y, 0x14);
    Tree empty{nullptr, 0};
    empty.head = NewHead();
    SetPair pair;
    pair.key = *key;
    CopyTree(&pair.set, &empty);
    TreeNode* const node = InsertSetHint(map, y, &pair);
    DestroyTree(pair.set);
    DestroyTree(empty);
    return &Value<Tree>(node, 0x14);
}

// 0x0069B070: the random pass's filter (through a pointer from 0x00695E50):
// true when the element's cell is not in the set of the developer id in
// 0x00B48544.
bool NotUsed(const int32_t* element)
{
    Tree* const set = SetsAt(&Global<uint32_t>(addr::cSC4BuildingDevelopmentSimulator_SetsKey));
    const int32_t pair[2] = {element[0], element[1]};
    return FindPair(set, nullptr, pair) == set->head;
}

// 0x00695E50: find_if over 12-byte elements with that filter (the game
// unrolls it by four; the calls come in the same order).
const char* FindUnused(const char* first, const char* last)
{
    for (; first != last; first += 12) {
        if (NotUsed(reinterpret_cast<const int32_t*>(first)))
            return first;
    }
    return last;
}

// --- The cell hash +0x9C -------------------------------------------------------
//
// A node: next, the key (developer, x, z, byte; 16 bytes), the uint32 value.

struct CellKey {
    void* developer;
    int32_t x;
    int32_t z;
    uint32_t byte; // only the low byte is set; see the notes in the callers
};
static_assert(sizeof(CellKey) == 16);

uint32_t CellHash(const uint32_t* key)
{
    uint32_t hash = (key[1] & 0x3F) | (key[0] << 6);
    hash = (hash << 6) | (key[2] & 0x3F);
    return (hash << 4) | (reinterpret_cast<const uint8_t*>(key)[0xC] & 0xF);
}

uint32_t BucketCount(const char* table)
{
    return static_cast<uint32_t>(static_cast<int32_t>(Value<char*>(const_cast<char*>(table), 8) -
                                                      Value<char*>(const_cast<char*>(table), 4)) >> 2);
}

// The bucket vector of a resize: the first prime >= `hint` (0xFFFFFFFB past
// the table's end); 0 when that is not more than `old`.
uint32_t NextPrime(uint32_t hint)
{
    using LowerBoundFn = const uint32_t*(__cdecl*)(const uint32_t* first, const uint32_t* last,
                                                   const uint32_t* value, uint32_t, uint32_t);
    const auto* const end = reinterpret_cast<const uint32_t*>(addr::SC4HashTable_PrimesEnd);
    const uint32_t* const pos = reinterpret_cast<LowerBoundFn>(addr::SC4_LowerBoundUint32)(
        reinterpret_cast<const uint32_t*>(addr::SC4HashTable_Primes), end, &hint, hint, 0u);
    return pos == end ? 0xFFFFFFFBu : *pos;
}

// The rehash of 0x00696940 and 0x00696540: every node moved to the front of
// its bucket in a new vector of `n` buckets, bucket by bucket.
template <typename HashFn>
void Rehash(char* table, uint32_t old, uint32_t n, HashFn hash)
{
    using ConstructFn = void*(__thiscall*)(void* vector, uint32_t count, void* const* value, const void* allocator);
    void* buckets[3];
    void* const null = nullptr;
    reinterpret_cast<ConstructFn>(addr::SC4PtrVector_Construct)(buckets, n, &null, &n);
    void** const fresh = static_cast<void**>(buckets[0]);
    for (uint32_t i = 0; i < old; ++i) {
        void* first = Value<void**>(table, 4)[i];
        while (first) {
            const uint32_t index = hash(first) % n;
            Value<void**>(table, 4)[i] = *static_cast<void**>(first);
            *static_cast<void**>(first) = fresh[index];
            fresh[index] = first;
            first = Value<void**>(table, 4)[i];
        }
    }
    void* const previous = Value<void*>(table, 4);
    Value<void*>(table, 4) = fresh;
    Value<void*>(table, 8) = buckets[1];
    Value<void*>(table, 0xC) = buckets[2];
    if (previous)
        Free(previous);
}

// 0x00696940: the cell hash's resize.
void ResizeCells(char* table, uint32_t hint)
{
    const uint32_t old = BucketCount(table);
    if (hint <= old)
        return;
    const uint32_t n = NextPrime(hint);
    if (n <= old)
        return;
    Rehash(table, old, n, [](void* node) { return CellHash(&Value<uint32_t>(node, 4)); });
}

// 0x00697470: the table resized for one more, then the entry (key and
// value, 5 dwords) put at the head of its bucket without looking for an
// equal key. Returns the node's entry (+4).
uint32_t* InsertCell(char* table, const uint32_t* entry)
{
    ResizeCells(table, Value<uint32_t>(table, 0x10) + 1);
    const uint32_t bucket = CellHash(entry) % BucketCount(table);
    void* const first = Value<void**>(table, 4)[bucket];
    auto* const node = static_cast<uint32_t*>(Allocate(0x18));
    node[0] = 0;
    for (int i = 0; i < 5; ++i)
        node[1 + i] = entry[i];
    node[0] = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(first));
    Value<void**>(table, 4)[bucket] = node;
    ++Value<uint32_t>(table, 0x10);
    return node + 1;
}

void* FindCell(char* table, const CellKey* key)
{
    using FindFn = void*(__thiscall*)(char* table, const CellKey* key);
    return reinterpret_cast<FindFn>(addr::SC4BuildingCellHash_Find)(table, key);
}

// 0x00698970: the value of a key (operator[]), 0 inserted when missing.
uint32_t* CellAt(char* table, const CellKey* key)
{
    if (void* const found = FindCell(table, key))
        return &Value<uint32_t>(found, 0x14);
    // The game copies the key's last dword whole; only its low byte is set
    // (see the callers), the rest is its stack.
    uint32_t entry[5];
    std::memcpy(entry, key, sizeof(CellKey));
    entry[4] = 0;
    return InsertCell(table, entry) + 4;
}

// --- The building profiles (0x00B48584) ---------------------------------------

using CopyProfileFn = void*(__thiscall*)(void* profile, const void* source);
using DestroyRangeFn = void(__cdecl*)(void* first, void* last, void* allocator);

// 0x00696540 (also called from 0x0080982B, outside this class): the resize
// of a hash table whose nodes hold a uint32 key at +8.
void __fastcall ResizeProfiles(void* table, void*, uint32_t hint)
{
    static bool logged = false;
    FirstCall("ResizeProfiles", logged);
    char* const t = static_cast<char*>(table);
    const uint32_t old = BucketCount(t);
    if (hint <= old)
        return;
    const uint32_t n = NextPrime(hint);
    if (n <= old)
        return;
    Rehash(t, old, n, [](void* node) { return Value<uint32_t>(node, 8); });
}

// A profile entry as the game builds it on its stack: the key, then the
// 0x40-byte profile at +8.
struct ProfilePair {
    uint32_t key;
    uint32_t padding;
    uint8_t profile[0x40];
};

// 0x0069B650: inserts a pair whose key is missing: resize for one more, a
// 0x50-byte node at the front of its bucket, the profile copied
// (0x004D1230). Returns the node's pair (+8).
char* InsertProfile(char* table, const ProfilePair* pair)
{
    ResizeProfiles(table, nullptr, Value<uint32_t>(table, 0x10) + 1);
    const uint32_t index = pair->key % BucketCount(table);
    void* const first = Value<void**>(table, 4)[index];
    char* const node = static_cast<char*>(Allocate(0x50));
    Value<void*>(node, 0) = nullptr;
    Value<uint32_t>(node, 8) = pair->key;
    reinterpret_cast<CopyProfileFn>(addr::SC4BuildingProfile_CopyConstruct)(node + 0x10, pair->profile);
    Value<void*>(node, 0) = first;
    Value<void**>(table, 4)[index] = node;
    ++Value<uint32_t>(table, 0x10);
    return node + 8;
}

// 0x0069B720: the profile of a building (operator[]): found by key (node
// +8), else a zeroed profile inserted through two copies, then both
// temporaries destroyed (their vectors of maps at +0xC). The game leaves
// the profile's padding (+0xB, +0x19-0x1B, +0x2C) unset; the copy skips it.
uint8_t* ProfileOf(uint32_t building)
{
    char* const table = reinterpret_cast<char*>(addr::cSC4BuildingDevelopmentSimulator_BuildingProfiles);
    for (void* node = Value<void**>(table, 4)[building % BucketCount(table)]; node;
         node = *static_cast<void**>(node)) {
        if (Value<uint32_t>(node, 8) == building)
            return static_cast<uint8_t*>(node) + 0x10;
    }
    uint8_t blank[0x40] = {};
    ProfilePair pair;
    pair.key = building;
    reinterpret_cast<CopyProfileFn>(addr::SC4BuildingProfile_CopyConstruct)(pair.profile, blank);
    uint8_t* const result = reinterpret_cast<uint8_t*>(InsertProfile(table, &pair)) + 8;
    uint8_t tag = 0;
    void* const copied = Value<void*>(pair.profile, 0xC);
    reinterpret_cast<DestroyRangeFn>(addr::SC4MapVector_DestroyRange)(copied, Value<void*>(pair.profile, 0x10), &tag);
    if (copied)
        Free(copied);
    // The blank's vector is null: the range is empty and the free skipped.
    void* const original = Value<void*>(blank, 0xC);
    reinterpret_cast<DestroyRangeFn>(addr::SC4MapVector_DestroyRange)(original, Value<void*>(blank, 0x10), &tag);
    if (original)
        Free(original);
    return result;
}

// --- Small helpers -------------------------------------------------------------

// A developer's growth: its slot 18's slot 12 (a float in ST0), truncated.
int32_t Growth(void* developer)
{
    return Ftol(Virtual<double>(Virtual<void*>(developer, 0x48), 0x30));
}

void* DeveloperOf(TreeNode* node) { return Value<void*>(node, 0x14); }

Tree* Developers(char* self) { return reinterpret_cast<Tree*>(self + local::kDevelopers); }

// Today: the simulator service's slot 7 (the clock), its slot 26.
uint32_t Today()
{
    return Virtual<uint32_t>(Virtual<void*>(Global<void*>(addr::SC4_SimulatorService), 0x1C), 0x68);
}

uint32_t Below(int32_t n)
{
    using BelowFn = uint32_t(__thiscall*)(void* random, uint32_t n);
    return reinterpret_cast<BelowFn>(addr::SC4Random_BelowLcg)(
        reinterpret_cast<void*>(addr::cSC4BuildingDevelopmentSimulator_Random), static_cast<uint32_t>(n));
}

uint32_t* Uint32At(Tree* map, const uint32_t* key)
{
    using AtFn = uint32_t*(__thiscall*)(Tree* map, const uint32_t* key);
    return reinterpret_cast<AtFn>(addr::SC4Uint32Map_At)(map, key);
}

int32_t* LastBuildAt(char* self, const uint32_t* key)
{
    using AtFn = int32_t*(__thiscall*)(void* map, const uint32_t* key);
    return reinterpret_cast<AtFn>(addr::SC4Sint32Map_At)(self + local::kLastBuild, key);
}

uint8_t* StageAt(char* self, const uint32_t* key)
{
    using AtFn = uint8_t*(__thiscall*)(void* map, const uint32_t* key);
    return reinterpret_cast<AtFn>(addr::SC4Sint32ByteMap_At)(self + local::kStages, key);
}

// A developer's vector of 12-byte cells (x, z, byte).
struct Cells {
    char* begin;
    char* end;
    char* capacity;
};

ListNode* Queue(char* self, std::size_t offset) { return Value<ListNode*>(self, offset); }

// The game's inline clear of a queue: every node freed, the head kept.
void ClearQueue(char* self, std::size_t offset)
{
    for (ListNode* node = Queue(self, offset)->next; node != Queue(self, offset);) {
        ListNode* const freed = node;
        node = node->next;
        Free(freed);
    }
    Queue(self, offset)->next = Queue(self, offset);
    Queue(self, offset)->prev = Queue(self, offset);
}

// A 0x1C-byte queue node appended: +8 its day, +0xC x, +0x10 z,
// +0x14 the developer, +0x18 a byte. The game writes the byte as a dword
// from its stack whose upper three bytes it never set; only the byte is
// written here.
void Enqueue(ListNode* head, uint32_t day, int32_t x, int32_t z, void* developer, uint8_t byte)
{
    auto* const node = static_cast<ListNode*>(Allocate(0x1C));
    Value<uint32_t>(node, 0x8) = day;
    Value<int32_t>(node, 0xC) = x;
    Value<int32_t>(node, 0x10) = z;
    Value<void*>(node, 0x14) = developer;
    Value<uint8_t>(node, 0x18) = byte;
    ListNode* const last = head->prev;
    node->prev = last;
    node->next = head;
    last->next = node;
    head->prev = node;
}

void Unlink(ListNode* node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
}

ListNode* NewSentinel()
{
    auto* const sentinel = static_cast<ListNode*>(Allocate(0xC));
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    return sentinel;
}

// The game's inline destruction of a local list: the nodes, then the
// sentinel.
void FreeList(ListNode* sentinel)
{
    for (ListNode* node = sentinel->next; node != sentinel;) {
        ListNode* const freed = node;
        node = node->next;
        Free(freed);
    }
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    Free(sentinel);
}

void DestroyPairList(ListNode** list) { reinterpret_cast<SelfFn>(addr::SC4PairList_Destroy)(list); }

// Adds day to every queued node's day (FILD of both as unsigned, the sum
// truncated).
void StampQueue(char* self, std::size_t offset, uint32_t day)
{
    ListNode* const head = Queue(self, offset);
    for (ListNode* node = head->next; node != Queue(self, offset); node = node->next)
        Value<uint32_t>(node, 8) =
            static_cast<uint32_t>(Ftol(Unsigned(Value<uint32_t>(node, 8)) + Unsigned(day)));
}

// --- The queues ------------------------------------------------------------------

// 0x00699440: the growth queue rebuilt: cells drawn from the developers
// with growth above 5 (weighted by growth + the weight +0xCC), each
// developer's cells in order, skipping cells that grew within the cool-down
// and duplicates; queued nodes get today + their index as their day.
void QueueGrowth(char* self)
{
    ClearQueue(self, local::kGrowthQueue);
    Tree next{nullptr, 0};
    next.head = NewHead();
    int32_t queued = 0;
    int32_t total = 0;
    int32_t cells = 0;
    Tree* const developers = Developers(self);
    for (TreeNode* node = developers->head->left; node != developers->head; node = Next(node)) {
        void* const developer = DeveloperOf(node);
        uint32_t key = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(developer));
        const int32_t growth = Growth(developer);
        if (growth > 5)
            total += At<int32_t>(self, local::kWeight) + growth;
        const Cells* range = Virtual<Cells*>(developer, 0x2C);
        cells += static_cast<int32_t>(range->end - range->begin) / 12;
        char* const begin = Virtual<Cells*>(developer, 0x2C)->begin;
        *Uint32At(&next, &key) = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(begin));
    }
    if (!At<uint32_t>(self, local::kCoolDown))
        At<int32_t>(self, local::kCoolDown) = cells;
    const uint32_t day = Today();
    for (;;) {
        int32_t pick = static_cast<int32_t>(Below(total));
        void* chosen = nullptr;
        for (TreeNode* node = developers->head->left; node != developers->head; node = Next(node)) {
            void* const developer = DeveloperOf(node);
            const int32_t growth = Growth(developer);
            if (growth <= 5)
                continue;
            const int32_t weight = At<int32_t>(self, local::kWeight);
            if (pick < weight + growth) {
                chosen = developer;
                break;
            }
            pick -= weight + growth;
        }
        if (chosen) {
            const Cells* range = Virtual<Cells*>(chosen, 0x2C);
            uint32_t key = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(chosen));
            uint32_t* const at = Uint32At(&next, &key);
            char* const cell = reinterpret_cast<char*>(static_cast<std::uintptr_t>(*at));
            if (cell != range->end) {
                const int32_t x = Value<int32_t>(cell, 0);
                const int32_t z = Value<int32_t>(cell, 4);
                const uint8_t byte = Value<uint8_t>(cell, 8);
                // The key's last dword: the byte, the rest the game's stack.
                const CellKey cellKey{chosen, x, z, byte};
                void* const found = FindCell(self + local::kCells, &cellKey);
                if (!found || day - Value<uint32_t>(found, 0x14) >= At<uint32_t>(self, local::kCoolDown)) {
                    bool duplicate = false;
                    for (ListNode* node = Queue(self, local::kGrowthQueue)->next;
                         node != Queue(self, local::kGrowthQueue); node = node->next) {
                        const uint32_t mine = Virtual<uint32_t>(chosen, 0x1C);
                        const uint32_t theirs = Virtual<uint32_t>(Value<void*>(node, 0x14), 0x1C);
                        if (mine == theirs && Value<int32_t>(node, 0xC) == x && Value<int32_t>(node, 0x10) == z) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (!duplicate) {
                        Enqueue(Queue(self, local::kGrowthQueue), static_cast<uint32_t>(queued), x, z, chosen, byte);
                        ++queued;
                    }
                }
                *at += 12;
            }
        }
        bool more = false;
        for (TreeNode* node = developers->head->left; node != developers->head; node = Next(node)) {
            void* const developer = DeveloperOf(node);
            uint32_t key = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(developer));
            if (Growth(developer) <= 5)
                continue;
            char* const end = Virtual<Cells*>(developer, 0x2C)->end;
            if (reinterpret_cast<char*>(static_cast<std::uintptr_t>(*Uint32At(&next, &key))) != end) {
                more = true;
                break;
            }
        }
        if (!more)
            break;
    }
    if (queued > 0 && Queue(self, local::kGrowthQueue)->next != Queue(self, local::kGrowthQueue))
        StampQueue(self, local::kGrowthQueue, day);
    At<int32_t>(self, local::kCoolDown) = queued;
    DestroyTree(next);
}

// 0x00699890: the decline queue rebuilt: cells drawn from the developers
// (not of type 5) with growth below -20 (weighted by the weight +0xCC less
// growth) from their slot 12's cells, in order; queued nodes get today +
// their index as their day.
void QueueDecline(char* self)
{
    ClearQueue(self, local::kDeclineQueue);
    Tree next{nullptr, 0};
    next.head = NewHead();
    int32_t queued = 0;
    int32_t total = 0;
    Tree* const developers = Developers(self);
    for (TreeNode* node = developers->head->left; node != developers->head; node = Next(node)) {
        void* const developer = DeveloperOf(node);
        uint32_t key = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(developer));
        if (Virtual<int32_t>(developer, 0x1C) == 5)
            continue;
        const int32_t growth = Growth(developer);
        if (growth < -20)
            total += At<int32_t>(self, local::kWeight) - growth;
        char* const begin = Virtual<Cells*>(developer, 0x30)->begin;
        *Uint32At(&next, &key) = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(begin));
    }
    const uint32_t day = Today();
    for (;;) {
        int32_t pick = static_cast<int32_t>(Below(total));
        void* chosen = nullptr;
        for (TreeNode* node = developers->head->left; node != developers->head; node = Next(node)) {
            void* const developer = DeveloperOf(node);
            if (Virtual<int32_t>(developer, 0x1C) == 5)
                continue;
            const int32_t growth = Growth(developer);
            if (growth >= -20)
                continue;
            const int32_t weight = At<int32_t>(self, local::kWeight);
            if (pick < weight - growth) {
                chosen = developer;
                break;
            }
            pick += growth - weight;
        }
        if (chosen) {
            const Cells* range = Virtual<Cells*>(chosen, 0x30);
            uint32_t key = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(chosen));
            uint32_t* const at = Uint32At(&next, &key);
            char* const cell = reinterpret_cast<char*>(static_cast<std::uintptr_t>(*at));
            if (cell != range->end) {
                Enqueue(Queue(self, local::kDeclineQueue), static_cast<uint32_t>(queued), Value<int32_t>(cell, 0),
                        Value<int32_t>(cell, 4), chosen, 0);
                ++queued;
                *at += 12;
            }
        }
        bool more = false;
        for (TreeNode* node = developers->head->left; node != developers->head; node = Next(node)) {
            void* const developer = DeveloperOf(node);
            uint32_t key = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(developer));
            if (Virtual<int32_t>(developer, 0x1C) == 5)
                continue;
            if (Growth(developer) >= -20)
                continue;
            char* const end = Virtual<Cells*>(developer, 0x30)->end;
            if (reinterpret_cast<char*>(static_cast<std::uintptr_t>(*Uint32At(&next, &key))) != end) {
                more = true;
                break;
            }
        }
        if (!more)
            break;
    }
    if (queued > 0 && Queue(self, local::kDeclineQueue)->next != Queue(self, local::kDeclineQueue))
        StampQueue(self, local::kDeclineQueue, day);
    DestroyTree(next);
}

// 0x00699EA0 (its argument unused): the queue state machine: 1 starts the
// walk over the developers, 2 waits until every developer's slot 28 says
// it is ready, 3 rebuilds the growth queue (in queue mode), 4 the decline
// queue and ends.
void Advance(char* self)
{
    TreeNode*& cursor = At<TreeNode*>(self, local::kCursor);
    switch (At<uint32_t>(self, local::kState)) {
    case 1:
        cursor = Developers(self)->head->left;
        At<uint32_t>(self, local::kState) = 2;
        [[fallthrough]];
    case 2:
        while (cursor != Developers(self)->head) {
            if (!Virtual<uint8_t>(DeveloperOf(cursor), 0x70))
                return;
            cursor = Next(cursor);
        }
        At<uint32_t>(self, local::kState) = 3;
        [[fallthrough]];
    case 3:
        if (At<uint8_t>(self, local::kQueueMode))
            QueueGrowth(self);
        At<uint32_t>(self, local::kState) = 4;
        return;
    case 4:
        QueueDecline(self);
        At<uint32_t>(self, local::kState) = 0;
        return;
    default:
        return;
    }
}

// 0x00696130: when idle, the construction capacity set from the region's
// three populations (0x00B43D74's slot 6 for 0x1001, 0x3001 and 0x4001,
// their slot 8) times +0xC8, bounded by +0xC0/+0xC4; then every
// developer's slot 27, and the state machine started.
void UpdateCapacity(char* self)
{
    if (At<uint32_t>(self, local::kState))
        return;
    void* const regions = Global<void*>(addr::SC4_Service43D74);
    const float residential = static_cast<float>(Virtual<double>(Virtual<void*>(regions, 0x18, 0x1001u, 0u), 0x20));
    const float commercial = static_cast<float>(Virtual<double>(Virtual<void*>(regions, 0x18, 0x3001u, 0u), 0x20));
    const double industrial = Virtual<double>(Virtual<void*>(regions, 0x18, 0x4001u, 0u), 0x20);
    uint32_t capacity = static_cast<uint32_t>(
        Ftol((industrial + commercial + residential) * static_cast<double>(At<float>(self, local::kCapacityPerResident))));
    if (capacity < At<uint32_t>(self, local::kCapacityMin))
        capacity = At<uint32_t>(self, local::kCapacityMin);
    if (capacity > At<uint32_t>(self, local::kCapacityMax))
        capacity = At<uint32_t>(self, local::kCapacityMax);
    Virtual<void>(At<void*>(self, local::kCapacity), 0x48, static_cast<float>(static_cast<int32_t>(capacity)));
    Tree* const developers = Developers(self);
    for (TreeNode* node = developers->head->left; node != developers->head; node = Next(node))
        Virtual<void>(DeveloperOf(node), 0x6C);
    At<uint32_t>(self, local::kState) = 1;
}

// 0x00699270: the growth queue's due nodes: those of developers whose
// growth fell to 5 or less dropped; others handed to the grower (+0x68
// slot 5). On success the developer's last build day is set and the pass
// ends; a result list ending in 0x14 ends it too; some results stamp the
// cell hash with today (unless the grower set its flag); the node is
// dropped.
void RunGrowthQueue(char* self)
{
    const uint32_t day = Today();
    ListNode* node = Queue(self, local::kGrowthQueue)->next;
    while (node != Queue(self, local::kGrowthQueue)) {
        if (day < Value<uint32_t>(node, 8)) {
            node = node->next;
            continue;
        }
        if (Growth(Value<void*>(node, 0x14)) <= 5) {
            ListNode* const after = node->next;
            Unlink(node);
            Free(node);
            node = after;
            continue;
        }
        // The key's last dword: the byte; the game never sets the upper
        // three bytes (its stack).
        const CellKey key{Value<void*>(node, 0x14), Value<int32_t>(node, 0xC), Value<int32_t>(node, 0x10),
                          Value<uint8_t>(node, 0x18)};
        ListNode* list = NewSentinel();
        uint8_t flag = 0;
        const int32_t result = Virtual<int32_t>(At<void*>(self, local::kGrower), 0x14, &key, &list, &flag);
        if (result > 0) {
            uint32_t type = Virtual<uint32_t>(Value<void*>(node, 0x14), 0x18);
            *LastBuildAt(self, &type) = static_cast<int32_t>(day);
            Unlink(node);
            Free(node);
            DestroyPairList(&list);
            return;
        }
        // With an empty list the game reads the sentinel's unset +8.
        const uint32_t last = Value<uint32_t>(list->prev, 8);
        if (last == 0x14) {
            DestroyPairList(&list);
            return;
        }
        switch (last) {
        case 10:
        case 11:
        case 12:
        case 14:
        case 15:
        case 16:
        case 17:
        case 18:
        case 22:
            if (!flag)
                *CellAt(self + local::kCells, &key) = day;
            break;
        default:
            break;
        }
        ListNode* const after = node->next;
        Unlink(node);
        Free(node);
        node = after;
        FreeList(list);
    }
}

// 0x00696A50: the decline queue's due nodes: those of developers whose
// growth rose above -20 dropped; others handed to the grower (+0x68 slot 6);
// on success the pass ends; the node is dropped.
void RunDeclineQueue(char* self)
{
    const uint32_t day = Today();
    ListNode* node = Queue(self, local::kDeclineQueue)->next;
    while (node != Queue(self, local::kDeclineQueue)) {
        if (day < Value<uint32_t>(node, 8)) {
            node = node->next;
            continue;
        }
        if (Growth(Value<void*>(node, 0x14)) > -20) {
            ListNode* const after = node->next;
            Unlink(node);
            Free(node);
            node = after;
            continue;
        }
        // As in RunGrowthQueue, the upper three bytes of the key's last
        // dword are the game's stack.
        const CellKey key{Value<void*>(node, 0x14), Value<int32_t>(node, 0xC), Value<int32_t>(node, 0x10),
                          Value<uint8_t>(node, 0x18)};
        // A list the game makes and destroys without handing it over.
        ListNode* list = NewSentinel();
        if (Virtual<int32_t>(At<void*>(self, local::kGrower), 0x18, &key) > 0) {
            Unlink(node);
            Free(node);
            DestroyPairList(&list);
            return;
        }
        ListNode* const after = node->next;
        Unlink(node);
        Free(node);
        node = after;
        FreeList(list);
    }
}

// The time budget of the random pass (0x0068BF50).
struct Deadline {
    uint32_t start[2];
    uint32_t count;
    uint32_t step;
    uint8_t timedOut;
    uint8_t padding[3];
    uint32_t* stepGlobal;
};

// 0x0069B0B0: the random growth pass (outside queue mode) on a copy of the
// developers map, within a time budget: a developer drawn by growth weight;
// if its slot 10 allows, its first cell not yet tried (the sets at
// 0x00B485E0; all cleared and the first cell retried when none is left) is
// marked tried and handed to the grower (+0x68 slot 5), success stamping
// the developer's last build day; the developer then leaves the copy. The
// loop runs until the copy is empty or the budget is spent; the budget's
// step adapts to how many rounds fit.
void GrowRandom(char* self)
{
    using StartFn = void(__thiscall*)(Deadline* deadline, uint32_t budget);
    using ExpiredFn = uint8_t(__thiscall*)(Deadline* deadline);
    uint32_t& once = Global<uint32_t>(addr::cSC4BuildingDevelopmentSimulator_GrowOnce);
    uint32_t& step = Global<uint32_t>(addr::cSC4BuildingDevelopmentSimulator_GrowStep);
    int32_t& trend = Global<int32_t>(addr::cSC4BuildingDevelopmentSimulator_GrowTrend);
    if (!(once & 1)) {
        once |= 1;
        step = 0x400;
        trend = 0;
    }
    Deadline deadline{};
    reinterpret_cast<StartFn>(addr::SC4Deadline_Start)(&deadline, 3);
    uint32_t count = 0;
    deadline.step = step;
    deadline.timedOut = 0;
    deadline.stepGlobal = &step;
    Tree copy;
    CopyTree(&copy, Developers(self));
    if (copy.count) {
        for (;;) {
            count += deadline.step;
            deadline.count = count;
            if (!(count & 0x3FF) && reinterpret_cast<ExpiredFn>(addr::SC4Deadline_Expired)(&deadline)) {
                ClearTree(copy);
                break;
            }
            int32_t total = 0;
            for (TreeNode* node = copy.head->left; node != copy.head; node = Next(node)) {
                const int32_t growth = Growth(DeveloperOf(node));
                if (growth > 5)
                    total += At<int32_t>(self, local::kWeight) + growth;
            }
            int32_t pick = static_cast<int32_t>(Below(total));
            void* chosen = nullptr;
            for (TreeNode* node = copy.head->left; node != copy.head; node = Next(node)) {
                void* const developer = DeveloperOf(node);
                const int32_t growth = Growth(developer);
                if (growth <= 5)
                    continue;
                const int32_t weight = At<int32_t>(self, local::kWeight);
                if (pick < weight + growth) {
                    chosen = developer;
                    break;
                }
                pick -= weight + growth;
            }
            if (chosen) {
                uint32_t type = Virtual<uint32_t>(chosen, 0x18);
                if (Virtual<uint8_t>(chosen, 0x28)) {
                    const Cells* const range = Virtual<Cells*>(chosen, 0x2C);
                    Global<uint32_t>(addr::cSC4BuildingDevelopmentSimulator_SetsKey) = type;
                    const char* cell = FindUnused(range->begin, range->end);
                    bool any = true;
                    if (cell == range->end) {
                        uint32_t key = type;
                        ClearTree(*SetsAt(&key));
                        cell = range->begin;
                        any = cell != range->end;
                    }
                    if (any) {
                        const int32_t x = Value<int32_t>(const_cast<char*>(cell), 0);
                        const int32_t z = Value<int32_t>(const_cast<char*>(cell), 4);
                        uint32_t key = type;
                        Tree* const set = SetsAt(&key);
                        const int32_t pair[2] = {x, z};
                        bool inserted = false;
                        InsertPair(set, pair, &inserted);
                        // The key's last dword: the cell's byte; the game
                        // never sets its upper three bytes (its stack).
                        const CellKey cellKey{chosen, Value<int32_t>(const_cast<char*>(cell), 0),
                                              Value<int32_t>(const_cast<char*>(cell), 4),
                                              Value<uint8_t>(const_cast<char*>(cell), 8)};
                        ListNode* list = NewSentinel();
                        uint8_t flag = 0;
                        if (Virtual<int32_t>(At<void*>(self, local::kGrower), 0x14, &cellKey, &list, &flag) > 0) {
                            const uint32_t day = Today();
                            *LastBuildAt(self, &type) = static_cast<int32_t>(day);
                        }
                        FreeList(list);
                    }
                }
                // erase(type) on the copy: the equal range by key.
                TreeNode* upper = copy.head;
                for (TreeNode* x = copy.head->parent; x;) {
                    if (type < Value<uint32_t>(x, 0x10)) {
                        upper = x;
                        x = x->left;
                    } else {
                        x = x->right;
                    }
                }
                TreeNode* lower = copy.head;
                for (TreeNode* x = copy.head->parent; x;) {
                    if (Value<uint32_t>(x, 0x10) < type) {
                        x = x->right;
                    } else {
                        lower = x;
                        x = x->left;
                    }
                }
                EraseRange(&copy, lower, upper);
            }
            if (!copy.count)
                break;
        }
    }
    if (copy.head)
        Free(copy.head);
    const uint32_t rounds = count >> 10;
    if (!rounds)
        return;
    if (static_cast<int32_t>(rounds) > 20) {
        if (++trend >= 16) {
            trend = 0;
            if (step > 1)
                step >>= 1;
        }
    } else if (static_cast<int32_t>(rounds) < 5) {
        if (--trend <= -4) {
            trend = 0;
            if (step < 0x400)
                step *= 2;
        }
    }
}

// 0x0069B6D0 (its argument unused): the daily step, under a stopwatch the
// game never reads: the growth queue or the random pass, then the decline
// queue.
void DailyStep(char* self)
{
    using StopwatchCtorFn = void*(__thiscall*)(void* stopwatch, uint32_t mode);
    using StopwatchFn = void(__thiscall*)(void* stopwatch);
    alignas(8) uint8_t stopwatch[0x18];
    reinterpret_cast<StopwatchCtorFn>(addr::SC4Stopwatch_Construct)(stopwatch, 4u);
    reinterpret_cast<StopwatchFn>(addr::SC4Stopwatch_Start)(stopwatch);
    if (At<uint8_t>(self, local::kQueueMode))
        RunGrowthQueue(self);
    else
        GrowRandom(self);
    RunDeclineQueue(self);
    reinterpret_cast<StopwatchFn>(addr::SC4Stopwatch_Destruct)(stopwatch);
}

// --- Lots in a changed rectangle ----------------------------------------------

// A cell region (0x24 bytes): the rectangle, then a part at +0x10 whose
// vtable 0x00A86364 the destructor sets before freeing its cells.
struct CellRegion {
    int32_t rect[4];
    uint8_t cells[0x14];
};

void ConstructRegion(CellRegion* region, const int32_t* rect, uint32_t flag)
{
    using CtorFn = void*(__thiscall*)(CellRegion* region, const int32_t* rect, uint32_t flag);
    reinterpret_cast<CtorFn>(addr::SC4CellRegion_Construct)(region, rect, flag);
}

void DestroyRegion(CellRegion* region)
{
    *reinterpret_cast<std::uintptr_t*>(region->cells) = addr::SC4CellRegion_CellsVtable;
    reinterpret_cast<SelfFn>(addr::SC4CellRegion_DestroyCells)(region->cells);
}

void ClearRefList(ListNode** list) { reinterpret_cast<SelfFn>(addr::SC4RefList_Clear)(list); }

// 0x00698610: the lots in a changed cell rectangle. Kinds 1-3: each lot
// (the lot manager's slot 7) whose height above the terrain is within
// [-1, 2] and whose slope (its slot 19 against +0x6C's slot 30, over the
// cell size) is at most 5 degrees goes to +0x6C's slot 37; others are
// demolished (those whose slot 39 says so, as a list through 0x00B43D00's
// slot 7, the cost charged to the budget for kinds 1 and 2) or dezoned.
// Other kinds: every lot goes to +0x6C's slot 37.
void LotsChanged(char* self, const int32_t* rect, uint32_t kind)
{
    if (rect[1] > rect[3] || rect[0] > rect[2])
        return;
    void* const lots = At<void*>(self, local::kLots);
    if (kind != 3 && kind != 1 && kind != 2) {
        using ListCtorFn = void*(__thiscall*)(ListNode** list, void* allocator);
        ListNode* list = nullptr;
        uint8_t tag = 0;
        reinterpret_cast<ListCtorFn>(addr::SC4RefList_Construct)(&list, &tag);
        Virtual<void>(lots, 0x1C, &list, rect[0], rect[1], rect[2], rect[3], 0u);
        for (ListNode* node = list->next; node != list; node = node->next)
            Virtual<void>(At<void*>(self, local::kLotTaker), 0x94, Value<void*>(node, 8), kind);
        ClearRefList(&list);
        if (list)
            Free(list);
        return;
    }
    // The game passes this as a dword whose upper three bytes it never set
    // (its stack); here they are zero.
    const uint8_t charge = kind == 1 || kind == 2 ? 1 : 0;
    ListNode* demolish = NewSentinel();
    ListNode* found = NewSentinel();
    Virtual<void>(lots, 0x1C, &found, rect[0], rect[1], rect[2], rect[3], 0u);
    CellRegion whole;
    ConstructRegion(&whole, rect, 0);
    for (ListNode* node = found->next; node != found; node = node->next) {
        void* const lot = Value<void*>(node, 8);
        float box[4];
        Virtual<void>(lot, 0x3C, box);
        const double ground = Virtual<double>(At<void*>(self, local::kTerrain), 0x34, box[0], box[2]);
        const float height = static_cast<float>(ground - static_cast<double>(box[1]));
        int32_t cells[4];
        Virtual<void>(lot, 0x38, cells);
        bool fits = false;
        if (!(height < -1.0f) && !(height > 2.0f)) {
            const float high = static_cast<float>(Virtual<double>(lot, 0x4C));
            const float low = static_cast<float>(Virtual<double>(At<void*>(self, local::kLotTaker), 0x78, lot, 0u));
            const double perCell = 1.0 / Virtual<double>(At<void*>(self, local::kCity), 0x16C);
            const double slope = std::atan(static_cast<double>(low) * perCell) -
                                 std::atan(perCell * static_cast<double>(high));
            fits = !(slope > static_cast<double>(FloatBits(0x3DB2B8C3))); // 5 degrees
        }
        if (fits) {
            Virtual<void>(At<void*>(self, local::kLotTaker), 0x94, lot, kind);
            continue;
        }
        if (Virtual<uint8_t>(lot, 0x9C)) {
            Virtual<uint32_t>(lot, 0x4);
            auto* const added = static_cast<ListNode*>(Allocate(0xC));
            Value<void*>(added, 8) = lot;
            Virtual<uint32_t>(lot, 0x4);
            ListNode* const last = demolish->prev;
            added->next = demolish;
            added->prev = last;
            last->next = added;
            demolish->prev = added;
            Virtual<uint32_t>(lot, 0x8);
        } else {
            int32_t lotCells[4];
            Virtual<void>(lot, 0x38, lotCells);
            CellRegion region;
            ConstructRegion(&region, lotCells, 1);
            Virtual<void>(At<void*>(self, local::kZones), 0x2C, &region, 0u, 1u, static_cast<uint32_t>(charge), 0u,
                          1u, 1u, 0u, 0u, 0u);
            DestroyRegion(&region);
        }
    }
    int64_t cost = 0;
    Virtual<void>(Global<void*>(addr::SC4_Service43D00), 0x1C, 1u, &demolish, 0u, 1u, 1u, 0u, &cost, 0u);
    if (charge && cost > 0) {
        void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
        const int64_t funds = Virtual<int64_t>(budget, 0x18);
        Virtual<void>(budget, 0x14, funds - cost);
    }
    DestroyRegion(&whole);
    ClearRefList(&found);
    if (found)
        Free(found);
    ClearRefList(&demolish);
    if (demolish)
        Free(demolish);
}

// --- Occupant stage changes ----------------------------------------------------

// 0x00695790: the developer type of a purpose and wealth (0 for none).
uint32_t DeveloperType(uint32_t purpose, uint32_t wealth)
{
    switch (purpose) {
    case 1:
        return wealth == 1 ? 0x1010 : wealth == 2 ? 0x1020 : wealth == 3 ? 0x1030 : 0;
    case 2:
        return wealth == 1 ? 0x3110 : wealth == 2 ? 0x3120 : wealth == 3 ? 0x3130 : 0;
    case 3:
        return wealth == 2 ? 0x3320 : wealth == 3 ? 0x3330 : 0;
    case 5:
        return 0x4100;
    case 6:
        return 0x4200;
    case 7:
        return 0x4300;
    case 8:
        return 0x4400;
    default:
        return 0;
    }
}

// 0x0069CA80: a building occupant entered or left a growth stage: its cells
// in the grid +0x48 get its profile's map key (purpose * 4 + wealth + 1) or
// 0; on entering at a stage above the highest so far for its developer
// type (+0x50), the new high is kept and a notification (class 0x6A26D049)
// at its position goes to the city's slot 81's view 0x17.
void OccupantStaged(char* self, void* occupant, uint32_t wealth, uint8_t staged)
{
    const uint32_t building = Virtual<uint32_t>(occupant, 0xB8, 0u);
    uint8_t* const profile = ProfileOf(building);
    int32_t rect[4];
    Virtual<void>(occupant, 0x38, rect);
    const uint8_t mark = staged ? static_cast<uint8_t>(profile[0] * 4 + static_cast<uint8_t>(wealth) + 1) : 0;
    char* const rows = *Virtual<char**>(At<void*>(self, local::kGrid), 0x60);
    for (int32_t x = rect[0]; x <= rect[2]; ++x) {
        for (int32_t z = rect[1]; z <= rect[3]; ++z)
            Value<uint8_t*>(rows, 12 * static_cast<std::size_t>(x))[z] = mark;
    }
    if (!staged || !Value<uint32_t>(profile, 0) || Value<uint32_t>(profile, 4) != wealth)
        return;
    void* const staging = Virtual<void*>(occupant, 0x24);
    void* const placed = Virtual<void*>(occupant, 0xC0);
    if (!staging || !placed)
        return;
    uint32_t type = DeveloperType(Value<uint32_t>(profile, 0), Value<uint32_t>(profile, 4));
    const uint8_t stage = Virtual<uint8_t>(staging, 0x2C);
    if (!(stage > *StageAt(self, &type)))
        return;
    *StageAt(self, &type) = stage;
    float position[3];
    Virtual<void>(Virtual<void*>(placed, 0xC), 0x20, position);
    void* const view = Virtual<void*>(Virtual<void*>(Global<void*>(addr::SC4_City), 0x144), 0x1C, 0x17u);
    void* city = Global<void*>(addr::SC4_City);
    void* notice = nullptr;
    void* const com = reinterpret_cast<void*(__cdecl*)()>(addr::RZGetCOM)();
    Virtual<uint8_t>(com, 0xC, 0x6A26D049u, 0x6A26D050u, &notice);
    const float z = static_cast<float>(static_cast<double>(position[2]) / Virtual<double>(city, 0x170));
    const float x = static_cast<float>(static_cast<double>(position[0]) / Virtual<double>(city, 0x16C));
    // The game does not check the creation.
    Virtual<void>(notice, 0xC, x, z);
    Virtual<void>(notice, 0x1C, occupant);
    Virtual<void>(notice, 0x28, (static_cast<uint32_t>(stage) << 24) | type);
    Virtual<void>(view, 0x48, 0x4A64A9CCu, 0u);
    if (notice)
        Virtual<uint32_t>(notice, 0x8);
}

// 0x00695260 (also called from 0x006A9633, outside this class): whether an
// occupant state (its low byte) is 2 or 3.
uint32_t __cdecl IsStaged(uint32_t state)
{
    static bool logged = false;
    FirstCall("IsStaged", logged);
    const uint8_t value = static_cast<uint8_t>(state);
    return value >= 2 && value < 4 ? 1 : 0;
}

// --- DoMessage --------------------------------------------------------------------

constexpr uint32_t kExcludedProperty = 0x029244DB;

// Whether a building occupant belongs on the list +0x4C: without property
// 0x029244DB on its holder (slot 6) and with purpose 0x150A or 0x150B
// (slot 22).
bool Listed(void* occupant)
{
    if (Virtual<uint8_t>(Virtual<void*>(occupant, 0x18), 0xC, kExcludedProperty))
        return false;
    return Virtual<uint8_t>(occupant, 0x58, 0x150Au) || Virtual<uint8_t>(occupant, 0x58, 0x150Bu);
}

// cIGZMessageTarget2 slot 3 (0x0069CCE0), on the part at +8.
uint8_t __fastcall DoMessage(void* part, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    char* const self = static_cast<char*>(part) - kMessagePart;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    switch (type) {
    case 0x230E27AC: {
        const uint32_t cheat = Virtual<uint32_t>(message, 0x24);
        if (cheat == 0x6AB6A2D7)
            At<uint8_t>(self, local::kCheatCoffeeBar) = At<uint8_t>(self, local::kCheatCoffeeBar) ? 0 : 1;
        else if (cheat == 0x491865E1)
            At<uint8_t>(self, local::kCheatDD) = 1;
        else if (cheat == 0x08DAD89A)
            At<uint8_t>(self, local::kCheatDE) = 1;
        return 1;
    }
    case 0x0B99AE81: {
        // A command: build a building (its type, then x and z) through
        // primary slot 7.
        void* const arguments = Virtual<void*>(message, 0x28);
        const uint32_t building = Virtual<uint32_t>(Virtual<void*>(arguments, 0x14, 0u), 0x60);
        float position[3];
        position[0] = static_cast<float>(Virtual<double>(Virtual<void*>(arguments, 0x14, 1u), 0x90));
        position[1] = static_cast<float>(Virtual<double>(Virtual<void*>(arguments, 0x14, 2u), 0x90));
        position[2] = -1.0f;
        Virtual<uint8_t>(self, 0x1C, building, position, 2);
        return 1;
    }
    case 0x278E6003: {
        // Cells changed: data 1 is (x1 + 1) << 16 | x2, data 2 the same for
        // z; the rectangle is bounded by the city.
        const uint32_t first = Virtual<uint32_t>(message, 0x24);
        const uint32_t second = Virtual<uint32_t>(message, 0x38);
        const uint32_t kind = Virtual<uint32_t>(message, 0x60);
        const int32_t x1 = static_cast<int32_t>(first >> 16) - 1;
        const int32_t z1 = static_cast<int32_t>(second >> 16) - 1;
        const int32_t x2 = static_cast<int32_t>(first & 0xFFFF);
        const int32_t z2 = static_cast<int32_t>(second & 0xFFFF);
        int32_t rect[4] = {x1, z1, x2, z2};
        if (x1 < 0)
            rect[0] = 0;
        if (x2 >= At<int32_t>(self, local::kCellsX))
            rect[2] = At<int32_t>(self, local::kCellsX) - 1;
        if (z1 < 0)
            rect[1] = 0;
        if (z2 >= At<int32_t>(self, local::kCellsZ))
            rect[3] = At<int32_t>(self, local::kCellsZ) - 1;
        LotsChanged(self, rect, kind);
        return 1;
    }
    case 0x66956810:
    case 0x66956816:
        UpdateCapacity(self);
        return 1;
    case 0x66956814: {
        Virtual<uint32_t>(message, 0x4C);
        Advance(self);
        DailyStep(self);
        return 1;
    }
    case 0x6695681A:
        Virtual<uint32_t>(message, 0x4C);
        Advance(self);
        return 1;
    case 0x99EF1142: {
        void* occupant = Virtual<void*>(message, 0x28);
        if (!Listed(occupant))
            return 1;
        reinterpret_cast<void(__thiscall*)(void*, void**)>(addr::SC4List_PushBack)(self + local::kOccupants,
                                                                                    &occupant);
        Virtual<uint32_t>(occupant, 0x4);
        return 1;
    }
    case 0x99EF1143: {
        void* occupant = Virtual<void*>(message, 0x28);
        if (!Listed(occupant))
            return 1;
        using FindFn = ListNode** (__cdecl*)(ListNode** out, ListNode* first, ListNode* last, void** value);
        using EraseFn = ListNode** (__thiscall*)(void* list, ListNode** out, ListNode* node);
        ListNode* const head = At<ListNode*>(self, local::kOccupants);
        ListNode* found = nullptr;
        reinterpret_cast<FindFn>(addr::SC4List_Find)(&found, head->next, head, &occupant);
        if (found == At<ListNode*>(self, local::kOccupants))
            return 1;
        void* const listed = Value<void*>(found, 8);
        ListNode* after = nullptr;
        reinterpret_cast<EraseFn>(addr::SC4List_Erase)(self + local::kOccupants, &after, found);
        Virtual<uint32_t>(listed, 0x8);
        return 1;
    }
    case 0xE8BDA2E7: {
        void* const occupant = Virtual<void*>(message, 0x80);
        if (!occupant || !Virtual<uint32_t>(occupant, 0xB8, 0u))
            return 1;
        const uint32_t before = Virtual<uint32_t>(message, 0x24);
        const uint32_t after = Virtual<uint32_t>(message, 0x38);
        const uint8_t staged = static_cast<uint8_t>(IsStaged(after));
        if (staged == static_cast<uint8_t>(IsStaged(before)))
            return 1;
        const uint32_t wealth = Virtual<uint32_t>(message, 0x60);
        // The game passes the flag in the message argument's slot with its
        // low byte replaced; only the byte is read.
        OccupantStaged(self, occupant, wealth, staged);
        return 1;
    }
    default:
        return 1;
    }
}

// --- Installation -----------------------------------------------------------------

template <typename Fn>
bool Install(std::uintptr_t address, Fn ours, Fn* original)
{
    const addr::Entry* entry = addr_check::Find(address);
    return entry && hook::Install(*entry, ours, original);
}

bool SwapSlot(std::uintptr_t vtableAddress, int index, std::uintptr_t original, void* ours)
{
    auto* const slot = reinterpret_cast<std::uintptr_t*>(vtableAddress) + index;
    if (*slot != original) {
        log::Write("development messages: slot %d holds %08X, expected %08X; not installing", index,
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

using IsStagedFn = uint32_t(__cdecl*)(uint32_t);
using FindPairFn = TreeNode*(__fastcall*)(void*, void*, const int32_t*);
using ResizeFn = void(__fastcall*)(void*, void*, uint32_t);
IsStagedFn g_originalIsStaged = nullptr;
FindPairFn g_originalFindPair = nullptr;
ResizeFn g_originalResize = nullptr;

bool InstallMessages(registry::Mode)
{
    return game::IsBuild641() &&
           Install(addr::cSC4BuildingDevelopmentSimulator_IsStaged, &IsStaged, &g_originalIsStaged) &&
           Install(addr::SC4IntPairSet_Find, &FindPair, &g_originalFindPair) &&
           Install(addr::SC4BuildingProfileHash_Resize, &ResizeProfiles, &g_originalResize) &&
           SwapSlot(addr::cSC4BuildingDevelopmentSimulator_vftable_4, 3, 0x0069CCE0,
                    reinterpret_cast<void*>(&DoMessage));
}

const bool g_registered = registry::Register({
    "simulators.development.messages",
    "simulators/development",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4BuildingDevelopmentSimulator_vftable_4,
    "Our building development DoMessage: the daily step (growth and decline queues, the random growth pass), "
    "the queue state machine, construction capacity, changed lots, occupant stages, cheats (docs/development.md)",
    false,
    InstallMessages,
});

} // namespace
} // namespace opensc4::development_sim
