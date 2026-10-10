// Our cSC4TractDeveloper::Grow (build 641; docs/tract.md) and the helpers
// only it uses: the tract developer's attempt to grow buildings on one tract
// for one growth developer (residential, commercial or industrial by
// wealth), called by the building development simulator through slot 5 of
// the primary vtable (0x0070D130).
//
// What one Grow call does:
//   1. Takes the growth profile (growth developer, tract x and z, growth
//      stage) and caches what the other parts read during the attempt: the
//      developer's construction info (its slot 9 into +0x90), the tract's
//      capacity (developer slot 14 grid), the demand (developer slot 18's
//      demand, at least 1) and from it +0xBC = demand * the tuning float
//      +0x40, rounded down.
//   2. Fills the attempt record at +0x13C (date, tract, population,
//      capacity, maximum capacity, desirability, demand, free construction
//      resources), empties its list of attempts (+0x16C) and makes its
//      outcome table (+0x170) 13 rows of 28 zeros (one count per outcome).
//   3. Repeats, at most tract size squared + 1 times, until a pass decides
//      it is done: empties the lots of the tract (+0xCC), the set +0xD8,
//      the candidates (+0xE4, back into the global pool) and the map +0x10C;
//      collects the tract's lots (0x00707FB0); then the lots part finds lots
//      to (re)occupy (0x0070A3D0): Success ends Grow at once. Otherwise the
//      buildings part checks the tract (0x00709650); if that succeeds, the
//      lots part gathers candidate lots (0x00708110, 0x007097E0 and, for
//      every purpose but 5, 0x00708C00; the best outcome is kept). Without
//      candidates the best outcome (NoLot instead of Success) is reported
//      and counted in row 0 of the table. With candidates they are scored
//      (0x00705750), sorted by score (std::sort, ascending float +0x34) and
//      tried in order by the buildings part (0x0070B640 for purpose 5,
//      0x0070B440 else); each outcome is appended to the caller's list. The
//      tries stop at InsufficientConstructionResources (done unless this is
//      the first pass), once as many grew as the demand (+0xC0 >= +0xB8,
//      done) or at a Success (done for purposes 5-8). Every outcome is followed by the
//      lots part's bookkeeping (0x0070AD20). A pass after the first that
//      did not end in Success is the last.
//   4. With the CheatDebugGrowth flag of the building development simulator,
//      keeps the attempt record per developer and tract (0x0070CFA0: the map
//      +0x17C, a success and a failure record per tract).
//   5. Writes the flag +0x108 to the caller's bool and returns the number of
//      buildings grown (+0xC0).
//
// Fields used here (offsets from the object's start; evidence in brackets):
//   0x1C  the building development simulator (Init: 0x00B43D68); slot 48
//         GetFreeConstructionResources, slot 57 CheatDebugGrowth [0x0070D296,
//         0x0070CFAB]
//   0x2C  the lot manager (Init: 0x00B43D08); slot 6 GetLot(x, z, false)
//         [0x007080D0]
//   0x40  float tuning (property 0x47E2C500, 1.5 by default); target factor
//         [0x0070D1C5]
//   0x80  cISC4GrowthDeveloper* of the profile; 0x84, 0x88 int32 tract x, z;
//         0x8C uint32 growth stage (profile byte +0xC)
//   0x90  the developer's construction info (its slot 9 fills it)
//   0xB4  uint32 capacity at the tract (developer slot 14 grid, uint16)
//   0xB8  int32 demand (developer slot 18's demand, its slot 12 float,
//         truncated, at least 1)
//   0xBC  int32 truncate(demand * float 0x40) (not read here)
//   0xC0  int32 grown so far (Grow's return; Grow zeroes it); 0xC4 int32
//         zeroed
//   0xC8  bool: not the first pass (set per pass); Grow's stop flag after
//         InsufficientConstructionResources
//   0xCC  set<cISC4Lot*> the lots of the tract (head, size; nodes 0x14)
//   0xD8  set (nodes 0x14; emptied per pass)
//   0xE4  vector<candidate*> (begin, end, capacity); candidates are pooled
//         (0x00B4AFCC); the score is the float at candidate +0x34
//   0xF0  int32 zeroed per call; 0xF4, 0xF8 int32 zeroed per pass; 0xFC,
//         0x100 int32 set to -1 per pass
//   0x104 the caller's outcome list (SC4List<int32>*); 0x108 uint8 flag
//         written to the caller's bool at the end (zeroed at the start)
//   0x10C map (nodes 0x18; emptied per pass)
//   0x13C the attempt record (0x40 bytes): 0x13C/0x140 a cRZDate (vtable
//         0x00A827D8, day from the simulator service slot 7's slot 26),
//         0x144 developer, 0x148 x, 0x14C z, 0x150 population (slot 15
//         grid, int16), 0x154 zero, 0x158 capacity, 0x15C maximum capacity
//         (slot 16 grid, uint16), 0x160 desirability (slot 13 grid, int16),
//         0x164 demand, 0x168 free construction resources (uint16), 0x16C
//         list of attempts (head; nodes 0x3C), 0x170 vector<vector<int32>>
//         (13 rows of 28 outcome counts)
//   0x17C map<int32 developer slot 6, map<pair<x, z>, record[2]>> (head,
//         size; outer nodes 0x20, inner nodes 0x98): the success (0) and
//         failure (1) record of each tract, kept with CheatDebugGrowth
// An attempt (list node value, 0x34 bytes at node +8): uint8; a set (nodes
// 0x14) at +4; a vector of owned candidate objects at +0x10 (destructor
// 0x00705D10, operator delete); six int32 at +0x1C.
#include "core/tract_sim.h"

#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>

namespace opensc4::tract_sim {
namespace {

namespace local {
constexpr std::size_t kDevelopment = 0x1C;
constexpr std::size_t kLotManager = 0x2C;
constexpr std::size_t kTargetFactor = 0x40;
constexpr std::size_t kDeveloper = 0x80;
constexpr std::size_t kTractX = 0x84;
constexpr std::size_t kTractZ = 0x88;
constexpr std::size_t kStage = 0x8C;
constexpr std::size_t kConstructionInfo = 0x90;
constexpr std::size_t kCapacity = 0xB4;
constexpr std::size_t kDemand = 0xB8;
constexpr std::size_t kTarget = 0xBC;
constexpr std::size_t kGrown = 0xC0;
constexpr std::size_t kC4 = 0xC4;
constexpr std::size_t kNotFirstPass = 0xC8;
constexpr std::size_t kLots = 0xCC;
constexpr std::size_t kSetD8 = 0xD8;
constexpr std::size_t kCandidates = 0xE4;
constexpr std::size_t kF0 = 0xF0;
constexpr std::size_t kF4 = 0xF4;
constexpr std::size_t kF8 = 0xF8;
constexpr std::size_t kFC = 0xFC;
constexpr std::size_t k100 = 0x100;
constexpr std::size_t kOutcomes = 0x104;
constexpr std::size_t kFlag = 0x108;
constexpr std::size_t kMap10C = 0x10C;
constexpr std::size_t kRecord = 0x13C;
constexpr std::size_t kStats = 0x17C;
} // namespace local

// The outcomes (cISC4TractDeveloper::Outcome in gzcom-dll).
constexpr int32_t kNoLot = 3;
constexpr int32_t kInsufficientResources = 20;
constexpr int32_t kSuccess = 27;

// --- Game calls ------------------------------------------------------------------

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
const auto Allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

template <typename R, typename... Args>
R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
R CallC(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

// The truncating float-to-int conversion (_ftol, 0x009EEF04).
int32_t Ftol(double value) { return static_cast<int32_t>(static_cast<int64_t>(value)); }

// --- The game's containers -------------------------------------------------------

// A red-black tree node (STLport layout; colour 0 is red, the head is red):
// the value follows at +0x10. Null children.
struct Node {
    uint8_t color;
    uint8_t pad[3];
    Node* parent;
    Node* left;
    Node* right;
};
static_assert(sizeof(Node) == 0x10);

// A map or set: the head (its parent is the root, left the leftmost and
// right the rightmost node) and the size.
struct Tree {
    Node* head;
    uint32_t size;
};

struct Vec {
    char* begin;
    char* end;
    char* cap;
};
static_assert(sizeof(Vec) == 12);

// A list node; the value follows at +8.
struct ListNode {
    ListNode* next;
    ListNode* prev;
};

// The attempt record (0x40 bytes; at +0x13C of the object and twice in each
// node of the map +0x17C).
struct Record {
    uint32_t vtable; // cRZDate
    uint32_t day;
    uint32_t data[10];
    ListNode* list; // attempts (nodes 0x3C)
    Vec rows;       // vector<vector<int32>>
};
static_assert(sizeof(Record) == 0x40);

// The map +0x17C: inner nodes (0x98) keyed by the tract, outer nodes (0x20)
// keyed by the developer.
struct InnerValue {
    int32_t x;
    int32_t z;
    Record records[2];
};
struct InnerNode {
    Node node;
    InnerValue value;
};
static_assert(sizeof(InnerNode) == 0x98);

struct OuterValue {
    int32_t key;
    Tree inner;
};
struct OuterNode {
    Node node;
    OuterValue value;
    uint32_t unused;
};
static_assert(sizeof(OuterNode) == 0x20);

struct InsertResult {
    Node* node;
    uint8_t inserted;
};

void* Value(Node* node) { return reinterpret_cast<char*>(node) + 0x10; }

Node* Increment(Node* node)
{
    reinterpret_cast<void(__thiscall*)(Node**)>(addr::SC4MapIterator_Next)(&node);
    return node;
}

Node* Decrement(Node* node)
{
    reinterpret_cast<void(__thiscall*)(Node**)>(addr::SC4TreeIterator_Decrement)(&node);
    return node;
}

void Rebalance(Node* node, Tree* tree)
{
    reinterpret_cast<void(__cdecl*)(Node*, Node**)>(addr::SC4Tree_RebalanceForInsert)(node, &tree->head->parent);
}

// Frees a subtree of nodes without anything to destroy (0x00704990,
// 0x007049D0, 0x00704A10 are three copies of this): the right subtree
// recursively, then the node, down the left spine.
void EraseNodes(Node* node)
{
    while (node) {
        EraseNodes(node->right);
        Node* const next = node->left;
        Free(node);
        node = next;
    }
}

// Empties a tree of plain nodes when it has any (the inline form in Grow and
// the destructors: the head is kept).
void ClearTree(Tree& tree)
{
    if (!tree.size)
        return;
    EraseNodes(tree.head->parent);
    tree.head->left = tree.head;
    tree.head->parent = nullptr;
    tree.head->right = tree.head;
    tree.size = 0;
}

// Appends to the caller's SC4List<int32> (nodes of 12 bytes: next, previous,
// value; the list has no size).
void PushOutcome(ListNode** list, int32_t outcome)
{
    ListNode* const head = *list;
    auto* const node = static_cast<ListNode*>(Allocate(0xC));
    At<int32_t>(node, 8) = outcome;
    ListNode* const tail = head->prev;
    node->prev = tail;
    node->next = head;
    tail->next = node;
    head->prev = node;
}

// --- The plain trees and the candidates ------------------------------------------

// 0x00704990, 0x007049D0, 0x00704A10 (thiscall; the tree is unused): the
// subtree erasers of the sets +0xCC (and the candidates' +0x18), +0xD8 (and
// the attempts') and the map +0x10C.
void __fastcall EraseLotNodes(void*, void*, Node* node)
{
    static bool logged = false;
    FirstCall("EraseLotNodes", logged);
    EraseNodes(node);
}

void __fastcall EraseSetNodes(void*, void*, Node* node)
{
    static bool logged = false;
    FirstCall("EraseSetNodes", logged);
    EraseNodes(node);
}

void __fastcall EraseMapNodes(void*, void*, Node* node)
{
    static bool logged = false;
    FirstCall("EraseMapNodes", logged);
    EraseNodes(node);
}

// 0x00705D10: the destructor of a lot candidate (also of the static one at
// 0x00B4AFF0, from 0x00A7D260): frees the vectors +0x94, +0x88, +0x7C,
// +0x70, then the set +0x18 (nodes, head).
void __fastcall DestroyCandidate(char* self, void*)
{
    static bool logged = false;
    FirstCall("DestroyCandidate", logged);
    for (std::size_t offset : {std::size_t{0x94}, std::size_t{0x88}, std::size_t{0x7C}, std::size_t{0x70}}) {
        if (void* const buffer = At<void*>(self, offset))
            Free(buffer);
    }
    Tree& set = At<Tree>(self, 0x18);
    ClearTree(set);
    if (set.head)
        Free(set.head);
}

// 0x00706B60 (thiscall; `this` unused): destroys and deletes the candidate
// objects an attempt owns (the vector at +0x10 of the attempt) and empties
// the vector.
void __fastcall DeleteAttemptObjects(void*, void*, char* attempt)
{
    static bool logged = false;
    FirstCall("DeleteAttemptObjects", logged);
    Vec& objects = At<Vec>(attempt, 0x10);
    for (char* p = objects.begin; p != objects.end; p += 4) {
        char* const object = *reinterpret_cast<char**>(p);
        if (object) {
            DestroyCandidate(object, nullptr);
            CallC<void>(addr::SC4_operator_delete, object);
        }
    }
    objects.end = objects.begin;
}

// 0x00707370 (cdecl): moves the candidates of a vector, last first, into
// the global pool (push_back; grows to size + max(size, 1)).
void __cdecl ReturnCandidates(Vec* candidates)
{
    static bool logged = false;
    FirstCall("ReturnCandidates", logged);
    Vec& pool = Global<Vec>(addr::cSC4TractDeveloper_CandidatePool);
    while (candidates->begin != candidates->end) {
        candidates->end -= 4;
        void* const candidate = *reinterpret_cast<void**>(candidates->end);
        if (pool.end != pool.cap) {
            *reinterpret_cast<void**>(pool.end) = candidate;
            pool.end += 4;
            continue;
        }
        const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(pool.end - pool.begin) >> 2);
        const uint32_t capacity = size + (size < 1 ? 1 : size);
        char* const buffer = capacity ? static_cast<char*>(Allocate(capacity * 4)) : nullptr;
        char* end = buffer;
        if (pool.end != pool.begin) {
            const std::size_t bytes = static_cast<std::size_t>(pool.end - pool.begin);
            std::memmove(buffer, pool.begin, bytes);
            end = buffer + bytes;
        }
        *reinterpret_cast<void**>(end) = candidate;
        if (pool.begin)
            Free(pool.begin);
        pool.begin = buffer;
        pool.end = end + 4;
        pool.cap = buffer + capacity * 4;
    }
}

// 0x00707FB0: collects the lots on the cells of the tract into the set +0xCC
// through the static cell iterator (set up on first use).
void CollectLots(char* self)
{
    static bool logged = false;
    FirstCall("CollectLots", logged);
    char* const cells = &Global<char>(addr::cSC4TractDeveloper_TractCells);
    uint32_t& guard = Global<uint32_t>(addr::cSC4TractDeveloper_TractCellsGuard);
    if (!(guard & 1)) {
        guard |= 1;
        cells[0] = 1;
        cells[1] = 1;
        cells[2] = 1;
        for (std::size_t offset = 4; offset <= 0x28; offset += 4)
            At<uint32_t>(cells, offset) = 0;
        At<uint32_t>(cells, 0x2C) = 1;
        CallC<int>(addr::SC4_atexit, addr::cSC4TractDeveloper_TractCellsAtExit);
    }
    void* const grid = Virtual<void*>(At<void*>(self, local::kDeveloper), 0x34);
    const uint32_t shift = Virtual<uint32_t>(grid, 0x2C) & 31; // shl by CL
    const uint32_t x = At<uint32_t>(self, local::kTractX);
    const uint32_t z = At<uint32_t>(self, local::kTractZ);
    const int32_t x0 = static_cast<int32_t>(x << shift);
    const int32_t z0 = static_cast<int32_t>(z << shift);
    const int32_t x1 = static_cast<int32_t>(((x + 1) << shift) - 1);
    const int32_t z1 = static_cast<int32_t>(((z + 1) << shift) - 1);
    At<int32_t>(cells, 0x4) = x0;
    At<int32_t>(cells, 0x8) = z0;
    At<int32_t>(cells, 0xC) = x1 < x0 ? x0 : x1;
    At<int32_t>(cells, 0x10) = z1 < z0 ? z0 : z1;
    cells[2] = 1;
    CallThis<void>(addr::TractIterator_Restart, cells, 1u);
    int32_t a = 0;
    int32_t b = 0;
    while (CallThis<bool>(addr::TractIterator_Next, cells, &a, &b)) {
        void* lot = Virtual<void*>(At<void*>(self, local::kLotManager), 0x18, a, b, 0u);
        if (lot) {
            uint32_t result[2];
            CallThis<void*>(addr::SC4PtrSet_Insert, self + local::kLots, static_cast<void*>(result), &lot);
        }
    }
}

// --- Sorting (STLport std::sort over pointers; the comparator is cdecl and
// returns bool in AL) ---------------------------------------------------------

using Pred = bool(__cdecl*)(void* a, void* b);
using Item = void*;

// 0x00704770 (cdecl): the candidates' order: score (+0x34) ascending.
bool __cdecl LessByScore(void* a, void* b) { return At<float>(a, 0x34) < At<float>(b, 0x34); }

// 0x00704790: the median of three (a pointer to it).
Item* Median(Item* first, Item* middle, Item* last, Pred pred)
{
    if (pred(*first, *middle)) {
        if (pred(*middle, *last))
            return middle;
        return pred(*first, *last) ? last : first;
    }
    if (pred(*first, *last))
        return first;
    return pred(*middle, *last) ? last : middle;
}

// 0x00704810: the unguarded linear insert.
void LinearInsert(Item* last, Item value, Pred pred)
{
    Item* next = last - 1;
    while (pred(value, *next)) {
        *last = *next;
        last = next;
        --next;
    }
    *last = value;
}

// 0x00704850: push_heap's hole walk.
void PushHeap(Item* first, int32_t hole, int32_t top, Item value, Pred pred)
{
    int32_t parent = (hole - 1) / 2;
    while (hole > top && pred(first[parent], value)) {
        first[hole] = first[parent];
        hole = parent;
        parent = (hole - 1) / 2;
    }
    first[hole] = value;
}

// 0x00704AF0: adjust_heap.
void AdjustHeap(Item* first, int32_t hole, int32_t length, Item value, Pred pred)
{
    const int32_t top = hole;
    int32_t second = 2 * hole + 2;
    while (second < length) {
        if (pred(first[second], first[second - 1]))
            --second;
        first[hole] = first[second];
        hole = second;
        second = 2 * second + 2;
    }
    if (second == length) {
        first[hole] = first[second - 1];
        hole = second - 1;
    }
    PushHeap(first, hole, top, value, pred);
}

// 0x00705550: make_heap.
void MakeHeap(Item* first, Item* last, Pred pred)
{
    const int32_t length = static_cast<int32_t>(last - first);
    if (length < 2)
        return;
    for (int32_t parent = (length - 2) / 2;; --parent) {
        AdjustHeap(first, parent, length, first[parent], pred);
        if (parent == 0)
            break;
    }
}

// 0x00706340: sort_heap.
void SortHeap(Item* first, Item* last, Pred pred)
{
    while (last - first > 1) {
        const Item value = last[-1];
        last[-1] = *first;
        AdjustHeap(first, 0, static_cast<int32_t>(last - 1 - first), value, pred);
        --last;
    }
}

// 0x00706F40: partial_sort.
void PartialSort(Item* first, Item* middle, Item* last, Pred pred)
{
    MakeHeap(first, middle, pred);
    for (Item* i = middle; i < last; ++i) {
        if (pred(*i, *first)) {
            const Item value = *i;
            *i = *first;
            AdjustHeap(first, 0, static_cast<int32_t>(middle - first), value, pred);
        }
    }
    SortHeap(first, middle, pred);
}

// 0x007054E0: the unguarded partition.
Item* Partition(Item* first, Item* last, Item pivot, Pred pred)
{
    for (;;) {
        while (pred(*first, pivot))
            ++first;
        --last;
        while (pred(pivot, *last))
            --last;
        if (!(first < last))
            return first;
        const Item swap = *first;
        *first = *last;
        *last = swap;
        ++first;
    }
}

// 0x00707F20 (cdecl): the introsort loop (more than 16 elements; recursion
// on the right part, partial_sort when the depth limit is used up).
void __cdecl IntroSortLoop(Item* first, Item* last, void*, int32_t depth, Pred pred)
{
    static bool logged = false;
    FirstCall("IntroSortLoop", logged);
    while (last - first > 16) {
        if (depth == 0) {
            PartialSort(first, last, last, pred);
            return;
        }
        --depth;
        const int32_t count = static_cast<int32_t>(last - first);
        const Item pivot = *Median(first, first + count / 2, last - 1, pred);
        Item* const cut = Partition(first, last, pivot, pred);
        IntroSortLoop(cut, last, nullptr, depth, pred);
        last = cut;
    }
}

// 0x007062D0 (cdecl): the insertion sort.
void __cdecl InsertionSort(Item* first, Item* last, Pred pred)
{
    static bool logged = false;
    FirstCall("InsertionSort", logged);
    if (first == last)
        return;
    for (Item* i = first + 1; i != last; ++i) {
        const Item value = *i;
        if (pred(value, *first)) {
            const std::size_t bytes = static_cast<std::size_t>(reinterpret_cast<char*>(i) -
                                                               reinterpret_cast<char*>(first));
            if (static_cast<int32_t>(bytes) > 0)
                std::memmove(first + 1, first, bytes);
            *first = value;
        } else {
            LinearInsert(i, value, pred);
        }
    }
}

// 0x00704A90 (cdecl): the unguarded insertion sort.
void __cdecl UnguardedInsertionSort(Item* first, Item* last, void*, Pred pred)
{
    static bool logged = false;
    FirstCall("UnguardedInsertionSort", logged);
    for (Item* i = first; i != last; ++i)
        LinearInsert(i, *i, pred);
}

// Grow's inline std::sort of the candidates (0x0070D5AD).
void SortCandidates(Item* first, Item* last)
{
    if (first == last)
        return;
    const int32_t count = static_cast<int32_t>(last - first);
    int32_t depth = 0;
    for (int32_t n = count; n != 1; n >>= 1)
        ++depth;
    IntroSortLoop(first, last, nullptr, depth * 2, &LessByScore);
    if (count > 16) {
        InsertionSort(first, first + 16, &LessByScore);
        UnguardedInsertionSort(first + 16, last, nullptr, &LessByScore);
    } else {
        InsertionSort(first, last, &LessByScore);
    }
}

// --- The attempts (list +0x30 of a record) ---------------------------------------

// Destroys an attempt's own parts and frees its node: the vector's buffer,
// the set's nodes and head.
void FreeAttemptNode(ListNode* node)
{
    if (void* const buffer = At<void*>(node, 0x18))
        Free(buffer);
    Tree& set = At<Tree>(node, 0xC);
    ClearTree(set);
    if (set.head)
        Free(set.head);
    Free(node);
}

// 0x007075C0 (thiscall on the list): frees every attempt node (the objects
// they point to stay; they belong to a kept record) and empties the list.
ListNode* __fastcall ClearAttempts(ListNode** list, void*)
{
    static bool logged = false;
    FirstCall("ClearAttempts", logged);
    ListNode* node = (*list)->next;
    while (node != *list) {
        ListNode* const next = node->next;
        FreeAttemptNode(node);
        node = next;
    }
    (*list)->next = *list;
    (*list)->prev = *list;
    return *list;
}

// 0x00709570 (thiscall, 4 arguments, the last unused): inserts copies of
// the attempts [first, last) before `where`. Returns last.
ListNode* __fastcall InsertAttempts(ListNode**, void*, ListNode* where, ListNode* first, ListNode* last, void*)
{
    static bool logged = false;
    FirstCall("InsertAttempts", logged);
    for (ListNode* source = first; source != last; source = source->next) {
        auto* const node = static_cast<ListNode*>(Allocate(0x3C));
        At<uint8_t>(node, 8) = At<uint8_t>(source, 8);
        CallThis<void*>(addr::cSC4TractDeveloper_Fn_00705AD0, reinterpret_cast<char*>(node) + 0xC,
                        reinterpret_cast<char*>(source) + 0xC);
        const Vec& from = At<Vec>(source, 0x18);
        Vec& to = At<Vec>(node, 0x18);
        const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(from.end - from.begin) >> 2);
        to = {nullptr, nullptr, nullptr};
        char* const buffer = count ? static_cast<char*>(Allocate(count * 4)) : nullptr;
        to.begin = buffer;
        to.end = buffer;
        to.cap = buffer + count * 4;
        char* end = buffer;
        if (from.end != from.begin) {
            const std::size_t bytes = static_cast<std::size_t>(from.end - from.begin);
            std::memmove(buffer, from.begin, bytes);
            end = buffer + bytes;
        }
        to.end = end;
        for (std::size_t offset = 0x24; offset <= 0x38; offset += 4)
            At<uint32_t>(node, offset) = At<uint32_t>(source, offset);
        ListNode* const before = where->prev;
        node->prev = before;
        node->next = where;
        before->next = node;
        where->prev = node;
    }
    return last;
}

// 0x00707E60 (thiscall, the list unused): erases the attempts [first, last);
// writes last to *result and returns result.
ListNode** __fastcall EraseAttempts(void*, void*, ListNode** result, ListNode* first, ListNode* last)
{
    static bool logged = false;
    FirstCall("EraseAttempts", logged);
    for (ListNode* node = first; node != last;) {
        ListNode* const next = node->next;
        node->prev->next = next;
        next->prev = node->prev;
        FreeAttemptNode(node);
        node = next;
    }
    *result = last;
    return result;
}

// 0x0070B130 (thiscall): list assignment: copies the source's attempts over
// the existing ones, then erases the rest or appends the remaining source.
ListNode** __fastcall AssignAttempts(ListNode** self, void*, ListNode** source)
{
    if (self == source)
        return self;
    ListNode* const head = *self;
    ListNode* const sourceHead = *source;
    ListNode* to = head->next;
    ListNode* from = sourceHead->next;
    while (to != head && from != sourceHead) {
        ListNode* const fromNext = from->next;
        ListNode* const toNext = to->next;
        At<uint8_t>(to, 8) = At<uint8_t>(from, 8);
        CallThis<void*>(addr::cSC4TractDeveloper_Fn_00705A10, reinterpret_cast<char*>(to) + 0xC,
                        reinterpret_cast<char*>(from) + 0xC);
        CallThis<void*>(addr::SC4PtrVector_Assign, reinterpret_cast<char*>(to) + 0x18,
                        reinterpret_cast<char*>(from) + 0x18);
        for (std::size_t offset = 0x24; offset <= 0x38; offset += 4)
            At<uint32_t>(to, offset) = At<uint32_t>(from, offset);
        from = fromNext;
        to = toNext;
    }
    if (from == sourceHead) {
        ListNode* result = nullptr;
        EraseAttempts(self, nullptr, &result, to, head);
    } else {
        InsertAttempts(self, nullptr, head, from, sourceHead, nullptr);
    }
    return self;
}

// --- The outcome tables (vector<vector<int32>> +0x34 of a record) ---------------

// 0x00706E10 (cdecl): copy-constructs [first, last) into raw storage at
// dest (skipping a null dest); returns the end.
Vec* CopyRowsInto(const Vec* first, const Vec* last, Vec* dest)
{
    for (; first != last; ++first, ++dest) {
        if (dest)
            CallThis<void*>(addr::SC4Uint32Vector_Copy, dest, first);
    }
    return dest;
}

// 0x00706E40 (thiscall, `this` unused): allocates count rows and
// copy-constructs [first, last) into them.
Vec* AllocateRows(uint32_t count, const Vec* first, const Vec* last)
{
    Vec* const buffer = count ? static_cast<Vec*>(Allocate(count * sizeof(Vec))) : nullptr;
    Vec* dest = buffer;
    for (; first != last; ++first, ++dest) {
        if (dest)
            CallThis<void*>(addr::SC4Uint32Vector_Copy, dest, first);
    }
    return buffer;
}

// 0x00706EF0 (cdecl): assigns [first, last) over the rows at dest; returns
// the end.
Vec* AssignRowRange(const Vec* first, const Vec* last, Vec* dest)
{
    int32_t count = static_cast<int32_t>(last - first);
    for (; count > 0; --count, ++first, ++dest)
        CallThis<void*>(addr::SC4Uint32Vector_CopyAssign, dest, first);
    return dest;
}

int32_t RowCount(const char* begin, const char* end) { return static_cast<int32_t>(end - begin) / 12; }

// 0x007093F0 (thiscall): vector<vector<int32>>::operator=.
Vec* AssignRows(Vec* self, const Vec* source)
{
    if (source == self)
        return self;
    const int32_t count = RowCount(source->begin, source->end);
    const auto* const sourceBegin = reinterpret_cast<const Vec*>(source->begin);
    const auto* const sourceEnd = reinterpret_cast<const Vec*>(source->end);
    if (static_cast<uint32_t>(count) > static_cast<uint32_t>(RowCount(self->begin, self->cap))) {
        Vec* const buffer = AllocateRows(static_cast<uint32_t>(count), sourceBegin, sourceEnd);
        CallThis<void>(addr::SC4ShortVectorVector_Destroy, self);
        self->cap = reinterpret_cast<char*>(buffer + count);
        self->begin = reinterpret_cast<char*>(buffer);
        self->end = reinterpret_cast<char*>(buffer + count);
        return self;
    }
    auto* const begin = reinterpret_cast<Vec*>(self->begin);
    const int32_t size = RowCount(self->begin, self->end);
    if (static_cast<uint32_t>(size) >= static_cast<uint32_t>(count)) {
        Vec* const end = AssignRowRange(sourceBegin, sourceEnd, begin);
        CallC<void>(addr::SC4IntVector_DestroyRange, end, reinterpret_cast<Vec*>(self->end));
    } else {
        AssignRowRange(sourceBegin, sourceBegin + size, begin);
        CopyRowsInto(sourceBegin + RowCount(self->begin, self->end), sourceEnd, reinterpret_cast<Vec*>(self->end));
    }
    self->end = reinterpret_cast<char*>(reinterpret_cast<Vec*>(self->begin) + count);
    return self;
}

// 0x00707540 (thiscall): vector<vector<int32>> copy constructor.
Vec* CopyRows(Vec* self, const Vec* source)
{
    const int32_t count = RowCount(source->begin, source->end);
    *self = {nullptr, nullptr, nullptr};
    Vec* const buffer = count ? static_cast<Vec*>(Allocate(static_cast<uint32_t>(count) * sizeof(Vec))) : nullptr;
    self->begin = reinterpret_cast<char*>(buffer);
    self->end = reinterpret_cast<char*>(buffer);
    self->cap = reinterpret_cast<char*>(buffer + count);
    Vec* dest = buffer;
    for (const Vec* row = reinterpret_cast<const Vec*>(source->begin); row != reinterpret_cast<const Vec*>(source->end);
         ++row, ++dest) {
        if (dest)
            CallThis<void*>(addr::SC4Uint32Vector_Copy, dest, row);
    }
    self->end = reinterpret_cast<char*>(dest);
    return self;
}

// --- The records ---------------------------------------------------------------

// 0x0070A370 (thiscall): constructs a node value's pair of records: the date
// with day 0, empty attempt lists (head allocated) and empty tables. The
// game leaves data[] as it was (stack garbage in its only caller, 0x0070C540);
// ours are zero. They are overwritten for the record kept right after.
void ConstructRecords(Record* records)
{
    for (int i = 0; i < 2; ++i) {
        Record& record = records[i];
        record.vtable = static_cast<uint32_t>(addr::cRZDate_vftable);
        record.day = 0;
        record.list = nullptr;
        auto* const head = static_cast<ListNode*>(Allocate(0x3C));
        head->next = head;
        head->prev = head;
        record.list = head;
        record.rows = {nullptr, nullptr, nullptr};
    }
}

// 0x0070BDA0 (thiscall): record copy constructor.
Record* CopyRecord(Record* self, const Record* source)
{
    self->vtable = static_cast<uint32_t>(addr::cRZDate_vftable); // 0x0091272C
    self->day = source->day;
    for (int i = 0; i < 10; ++i)
        self->data[i] = source->data[i];
    self->list = nullptr;
    auto* const head = static_cast<ListNode*>(Allocate(0x3C));
    head->next = head;
    head->prev = head;
    self->list = head;
    InsertAttempts(&self->list, nullptr, head, source->list->next, source->list, nullptr);
    CopyRows(&self->rows, &source->rows);
    return self;
}

// The record destructor as the game inlines it: the table, the attempts,
// the list head.
void DestroyRecord(Record* record)
{
    CallThis<void>(addr::SC4ShortVectorVector_Destroy, &record->rows);
    ClearAttempts(&record->list, nullptr);
    if (record->list)
        Free(record->list);
}

// 0x0070BD30 (thiscall): record assignment (the vtable stays).
Record* AssignRecord(Record* self, const Record* source)
{
    self->day = source->day;
    for (int i = 0; i < 10; ++i)
        self->data[i] = source->data[i];
    AssignAttempts(&self->list, nullptr, const_cast<ListNode**>(&source->list));
    AssignRows(&self->rows, &source->rows);
    return self;
}

// 0x0070AF20 (thiscall, `this` the developer, unused): before a kept record
// is overwritten: deletes the candidate objects of its attempts and erases
// the rows of its table.
void ResetRecord(Record* record)
{
    for (ListNode* node = record->list->next; node != record->list; node = node->next)
        DeleteAttemptObjects(nullptr, nullptr, reinterpret_cast<char*>(node) + 8);
    CallThis<void*>(addr::SC4IntVectorVector_Erase, &record->rows, record->rows.begin, record->rows.end);
}

// --- The map +0x17C -------------------------------------------------------------

// 0x0070BE40 (thiscall; the map unused): erases inner nodes: the right
// subtree recursively, then each record's table and attempts (second record
// first), then the node, down the left spine.
void __fastcall EraseInner(void* tree, void*, Node* node)
{
    static bool logged = false;
    FirstCall("EraseInner", logged);
    while (node) {
        EraseInner(tree, nullptr, node->right);
        Node* const next = node->left;
        InnerNode* const inner = reinterpret_cast<InnerNode*>(node);
        for (int i = 1; i >= 0; --i) {
            Record& record = inner->value.records[i];
            for (Vec* row = reinterpret_cast<Vec*>(record.rows.begin); row != reinterpret_cast<Vec*>(record.rows.end);
                 ++row) {
                if (row->begin)
                    Free(row->begin);
            }
            if (record.rows.begin)
                Free(record.rows.begin);
            ClearAttempts(&record.list, nullptr);
            if (record.list)
                Free(record.list);
        }
        Free(node);
        node = next;
    }
}

// 0x0070C3F0 (thiscall; the map unused): erases outer nodes, each with its
// inner map.
void __fastcall EraseOuter(void* tree, void*, Node* node)
{
    static bool logged = false;
    FirstCall("EraseOuter", logged);
    while (node) {
        EraseOuter(tree, nullptr, node->right);
        Node* const next = node->left;
        Tree& inner = reinterpret_cast<OuterNode*>(node)->value.inner;
        if (inner.size) {
            EraseInner(&inner, nullptr, inner.head->parent);
            inner.head->left = inner.head;
            inner.head->parent = nullptr;
            inner.head->right = inner.head;
            inner.size = 0;
        }
        if (inner.head)
            Free(inner.head);
        Free(node);
        node = next;
    }
}

// The two maps for the insertion templates: the key order and how a node is
// built from a value.
Record* CopyRecords(Record* to, const Record* from)
{
    CopyRecord(&to[0], &from[0]);
    CopyRecord(&to[1], &from[1]);
    return to;
}

struct InnerMap {
    // The pair order (x, then z; signed), as 0x006337B0.
    static bool Less(const void* a, const void* b)
    {
        const auto* const p = static_cast<const int32_t*>(a);
        const auto* const q = static_cast<const int32_t*>(b);
        return p[0] < q[0] || (p[0] == q[0] && p[1] < q[1]);
    }

    // 0x0070BF20 and the copy inlined in 0x0070BF70.
    static Node* Build(const void* value)
    {
        auto* const node = static_cast<InnerNode*>(Allocate(sizeof(InnerNode)));
        const auto* const from = static_cast<const InnerValue*>(value);
        node->value.x = from->x;
        node->value.z = from->z;
        CopyRecords(node->value.records, from->records);
        return &node->node;
    }
};

Tree* CopyInnerTree(Tree* self, const Tree* source);

struct OuterMap {
    static bool Less(const void* a, const void* b)
    {
        return *static_cast<const int32_t*>(a) < *static_cast<const int32_t*>(b);
    }

    // As inlined in 0x0070CAC0: the key, then the inner map's copy.
    static Node* Build(const void* value)
    {
        auto* const node = static_cast<OuterNode*>(Allocate(sizeof(OuterNode)));
        const auto* const from = static_cast<const OuterValue*>(value);
        node->value.key = from->key;
        CopyInnerTree(&node->value.inner, &from->inner);
        return &node->node;
    }
};

// 0x0070BF70 (inner) and 0x0070CAC0 (outer): links a new node under
// `parent`: on the right when forced or (x null and not value < parent),
// else on the left; rebalances.
template <typename Map>
Node** Link(Tree* tree, Node** result, Node* x, Node* parent, const void* value, uint32_t forceRight)
{
    Node* node;
    if (parent != tree->head && (forceRight || (!x && !Map::Less(value, Value(parent))))) {
        node = Map::Build(value);
        parent->right = node;
        if (parent == tree->head->right)
            tree->head->right = node;
    } else {
        node = Map::Build(value);
        parent->left = node;
        Node* const head = tree->head;
        if (parent == head) {
            head->parent = node;
            head->right = node;
        } else if (parent == head->left) {
            head->left = node;
        }
    }
    node->parent = parent;
    node->left = nullptr;
    node->right = nullptr;
    Rebalance(node, tree);
    ++tree->size;
    *result = node;
    return result;
}

// 0x0070C050 (inner) and 0x0070CB90 (outer): insert(value).
template <typename Map>
InsertResult* Insert(Tree* tree, InsertResult* result, const void* value)
{
    Node* parent = tree->head;
    Node* x = parent->parent;
    bool left = true;
    while (x) {
        parent = x;
        left = Map::Less(value, Value(x));
        x = left ? x->left : x->right;
    }
    Node* it = parent;
    if (left) {
        if (parent == tree->head->left) {
            Node* node = nullptr;
            Link<Map>(tree, &node, x, parent, value, 0);
            result->node = node;
            result->inserted = 1;
            return result;
        }
        it = Decrement(it);
    }
    if (Map::Less(Value(it), value)) {
        Node* node = nullptr;
        Link<Map>(tree, &node, x, parent, value, 0);
        result->node = node;
        result->inserted = 1;
        return result;
    }
    result->node = it;
    result->inserted = 0;
    return result;
}

// 0x0070C100 (inner) and 0x0070CC50 (outer): insert(hint, value).
template <typename Map>
Node** InsertHint(Tree* tree, Node** result, Node* hint, const void* value)
{
    auto plain = [&]() {
        InsertResult inserted{};
        Insert<Map>(tree, &inserted, value);
        *result = inserted.node;
        return result;
    };
    const auto force = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(hint));
    if (hint == tree->head->left) {
        if (tree->size == 0)
            return plain();
        if (Map::Less(value, Value(hint)))
            return Link<Map>(tree, result, hint, hint, value, 0);
        if (!Map::Less(Value(hint), value)) {
            *result = hint;
            return result;
        }
        Node* const next = Increment(hint);
        if (next == tree->head)
            return Link<Map>(tree, result, nullptr, hint, value, force);
        if (!Map::Less(value, Value(next)))
            return plain();
        if (!hint->right)
            return Link<Map>(tree, result, nullptr, hint, value, force);
        return Link<Map>(tree, result, next, next, value, 0);
    }
    if (hint == tree->head) {
        Node* const rightmost = tree->head->right;
        if (Map::Less(Value(rightmost), value))
            return Link<Map>(tree, result, nullptr, rightmost, value, force);
        return plain();
    }
    Node* const before = Decrement(hint);
    bool lessThanHint = false;
    if (Map::Less(value, Value(hint))) {
        if (Map::Less(Value(before), value)) {
            if (!before->right)
                return Link<Map>(tree, result, nullptr, before, value,
                                 static_cast<uint32_t>(reinterpret_cast<uintptr_t>(before)));
            return Link<Map>(tree, result, hint, hint, value, 0);
        }
        lessThanHint = true;
    }
    Node* const next = Increment(hint);
    if (lessThanHint)
        return plain();
    if (!Map::Less(Value(hint), value)) {
        *result = hint;
        return result;
    }
    if (next != tree->head && !Map::Less(value, Value(next)))
        return plain();
    if (!hint->right)
        return Link<Map>(tree, result, nullptr, hint, value, force);
    return Link<Map>(tree, result, next, next, value, 0);
}

// 0x0070C390 (thiscall): a copy of an inner node (colour kept, no
// children).
InnerNode* CloneInnerNode(const InnerNode* source)
{
    auto* const node = static_cast<InnerNode*>(Allocate(sizeof(InnerNode)));
    node->value.x = source->value.x;
    node->value.z = source->value.z;
    CopyRecords(node->value.records, source->value.records);
    node->node.color = source->node.color;
    node->node.left = nullptr;
    node->node.right = nullptr;
    return node;
}

// 0x0070C470 (thiscall): copies an inner subtree under `parent`: the top,
// its right subtree recursively, then down the left spine.
Node* CopyInnerSubtree(const Node* source, Node* parent)
{
    Node* const top = &CloneInnerNode(reinterpret_cast<const InnerNode*>(source))->node;
    top->parent = parent;
    if (source->right)
        top->right = CopyInnerSubtree(source->right, top);
    Node* above = top;
    for (const Node* from = source->left; from; from = from->left) {
        Node* const node = &CloneInnerNode(reinterpret_cast<const InnerNode*>(from))->node;
        above->left = node;
        node->parent = above;
        if (from->right)
            node->right = CopyInnerSubtree(from->right, node);
        above = node;
    }
    return top;
}

// 0x0070C6A0 (thiscall): inner map copy constructor.
Tree* CopyInnerTree(Tree* self, const Tree* source)
{
    self->head = nullptr;
    auto* const head = static_cast<Node*>(Allocate(sizeof(InnerNode)));
    self->head = head;
    self->size = 0;
    head->color = 0;
    if (!source->head->parent) {
        head->parent = nullptr;
        head->left = head;
        head->right = head;
        self->size = source->size;
        return self;
    }
    head->parent = CopyInnerSubtree(source->head->parent, head);
    Node* leftmost = head->parent;
    while (leftmost->left)
        leftmost = leftmost->left;
    head->left = leftmost;
    Node* rightmost = head->parent;
    while (rightmost->right)
        rightmost = rightmost->right;
    head->right = rightmost;
    self->size = source->size;
    return self;
}

// 0x0070C540 (thiscall): the records of a tract, inserted (from default
// records through a temporary pair) when missing.
Record* InnerIndex(Tree* tree, const int32_t* key)
{
    Node* const it = CallThis<Node*>(addr::SC4PairMap_LowerBound, tree, key);
    if (it != tree->head && !InnerMap::Less(key, Value(it)))
        return reinterpret_cast<InnerNode*>(it)->value.records;
    Record defaults[2];
    std::memset(defaults, 0, sizeof(defaults));
    ConstructRecords(defaults);
    InnerValue pair;
    pair.x = key[0];
    pair.z = key[1];
    CopyRecord(&pair.records[0], &defaults[0]);
    CopyRecord(&pair.records[1], &defaults[1]);
    Node* node = nullptr;
    InsertHint<InnerMap>(tree, &node, it, &pair);
    DestroyRecord(&pair.records[1]);
    DestroyRecord(&pair.records[0]);
    DestroyRecord(&defaults[1]);
    DestroyRecord(&defaults[0]);
    return reinterpret_cast<InnerNode*>(node)->value.records;
}

void DestroyInnerTree(Tree& tree)
{
    if (tree.size) {
        EraseInner(&tree, nullptr, tree.head->parent);
        tree.head->left = tree.head;
        tree.head->parent = nullptr;
        tree.head->right = tree.head;
        tree.size = 0;
    }
    if (tree.head)
        Free(tree.head);
}

// 0x0070CE60 (thiscall): the inner map of a developer, inserted (an empty
// map through two temporary copies) when missing.
Tree* OuterIndex(Tree* tree, const int32_t* key)
{
    Node* const head = tree->head;
    Node* found = head;
    for (Node* x = head->parent; x;) {
        if (!(*static_cast<const int32_t*>(Value(x)) < *key)) {
            found = x;
            x = x->left;
        } else {
            x = x->right;
        }
    }
    if (found != head && !(*key < *static_cast<const int32_t*>(Value(found))))
        return &reinterpret_cast<OuterNode*>(found)->value.inner;
    Tree empty{nullptr, 0};
    auto* const emptyHead = static_cast<Node*>(Allocate(sizeof(InnerNode)));
    empty.head = emptyHead;
    empty.size = 0;
    emptyHead->color = 0;
    emptyHead->parent = nullptr;
    emptyHead->left = emptyHead;
    emptyHead->right = emptyHead;
    OuterValue pair;
    pair.key = *key;
    CopyInnerTree(&pair.inner, &empty);
    Node* node = nullptr;
    InsertHint<OuterMap>(tree, &node, found, &pair);
    DestroyInnerTree(pair.inner);
    DestroyInnerTree(empty);
    return &reinterpret_cast<OuterNode*>(node)->value.inner;
}

// 0x0070CFA0: with CheatDebugGrowth, keeps the attempt record as the
// tract's success record (something grew) or failure record: deletes the
// objects of the record it replaces, then assigns.
void KeepRecord(char* self)
{
    static bool logged = false;
    FirstCall("KeepRecord", logged);
    if (!Virtual<bool>(At<void*>(self, local::kDevelopment), 0xE4))
        return;
    const int which = At<int32_t>(self, local::kGrown) > 0 ? 0 : 1;
    Tree* const stats = &At<Tree>(self, local::kStats);
    int32_t developer = Virtual<int32_t>(At<void*>(self, local::kDeveloper), 0x18);
    Node* outer = nullptr;
    CallThis<Node**>(addr::SC4Sint32Map_Find, stats, &outer, &developer);
    if (outer != stats->head) {
        Tree* const inner = &reinterpret_cast<OuterNode*>(outer)->value.inner;
        const int32_t tract[2] = {At<int32_t>(self, local::kTractX), At<int32_t>(self, local::kTractZ)};
        Node* const node = CallThis<Node*>(addr::SC4IntPairSet_Find, inner, tract);
        if (node != inner->head)
            ResetRecord(&reinterpret_cast<InnerNode*>(node)->value.records[which]);
    }
    developer = Virtual<int32_t>(At<void*>(self, local::kDeveloper), 0x18);
    const int32_t tract[2] = {At<int32_t>(self, local::kTractX), At<int32_t>(self, local::kTractZ)};
    Tree* const inner = OuterIndex(stats, &developer);
    Record* const records = InnerIndex(inner, tract);
    AssignRecord(&records[which], &At<Record>(self, local::kRecord));
}

// --- Grow ----------------------------------------------------------------------

// The growth profile (cISC4BuildingDevelopmentSimulator::GrowthProfile).
struct Profile {
    void* developer;
    int32_t x;
    int32_t z;
    uint8_t stage;
};

uint32_t CallPart(std::uintptr_t address, char* self) { return CallThis<uint32_t>(address, self); }

// Slot 5 (0x0070D130): Grow (see the top of the file).
uint32_t __fastcall Grow(char* self, void*, const Profile* profile, ListNode** outcomes, uint8_t* flag)
{
    static bool logged = false;
    FirstCall("Grow", logged);
    void* const developer = profile->developer;
    const int32_t x = profile->x;
    const int32_t z = profile->z;
    const int32_t purpose = Virtual<int32_t>(developer, 0x1C);
    At<void*>(self, local::kDeveloper) = developer;
    At<int32_t>(self, local::kTractX) = x;
    At<int32_t>(self, local::kTractZ) = z;
    At<uint32_t>(self, local::kStage) = profile->stage;
    Virtual<void*>(developer, 0x24, static_cast<void*>(self + local::kConstructionInfo));
    At<uint32_t>(self, local::kCapacity) = Virtual<uint16_t>(Virtual<void*>(developer, 0x38), 0x58, x, z);
    void* const demand = Virtual<void*>(developer, 0x48);
    int32_t& demandValue = At<int32_t>(self, local::kDemand);
    demandValue = Ftol(Virtual<double>(demand, 0x30)); // ST0, unrounded
    if (demandValue < 1)
        demandValue = 1;
    At<int32_t>(self, local::kTarget) =
        Ftol(static_cast<double>(demandValue) * static_cast<double>(At<float>(self, local::kTargetFactor)));
    At<int32_t>(self, local::kGrown) = 0;
    At<int32_t>(self, local::kC4) = 0;
    At<int32_t>(self, local::kF0) = 0;
    At<ListNode**>(self, local::kOutcomes) = outcomes;
    At<uint8_t>(self, local::kFlag) = 0;

    // The attempt record.
    Record& record = At<Record>(self, local::kRecord);
    void* const clock = Virtual<void*>(Global<void*>(addr::SC4_SimulatorService), 0x1C);
    record.day = Virtual<uint32_t>(clock, 0x68); // a temporary cRZDate (0x009126F3)
    record.data[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(developer));
    record.data[1] = static_cast<uint32_t>(x);
    record.data[2] = static_cast<uint32_t>(z);
    record.data[3] = static_cast<uint32_t>(
        static_cast<int32_t>(Virtual<int16_t>(Virtual<void*>(developer, 0x3C), 0x58, x, z)));
    record.data[5] = At<uint32_t>(self, local::kCapacity);
    record.data[4] = 0;
    record.data[6] = Virtual<uint16_t>(Virtual<void*>(developer, 0x40), 0x58, x, z);
    record.data[7] = static_cast<uint32_t>(
        static_cast<int32_t>(Virtual<int16_t>(Virtual<void*>(developer, 0x34), 0x58, x, z)));
    record.data[8] = static_cast<uint32_t>(demandValue);
    record.data[9] = Virtual<uint16_t>(Global<void*>(addr::SC4_Service43D68), 0xC0);
    ClearAttempts(&record.list, nullptr);
    Vec emptyRow{nullptr, nullptr, nullptr};
    CallThis<void>(addr::SC4IntVectorVector_Resize, &record.rows, 13u, &emptyRow);
    for (int row = 0; row < 13; ++row) {
        Vec& counts = reinterpret_cast<Vec*>(record.rows.begin)[row];
        const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(counts.end - counts.begin) >> 2);
        if (size > 28) {
            counts.end = counts.begin + 28 * 4;
        } else {
            const uint32_t zero = 0; // the empty row's begin
            CallThis<void>(addr::SC4Uint32Vector_InsertN, &counts, counts.end, 28 - size, &zero);
        }
        for (int i = 0; i < 28; ++i)
            reinterpret_cast<int32_t*>(counts.begin)[i] = 0;
    }

    uint8_t firstPass = 1;
    uint8_t done = 0;
    const int32_t tractSize = Virtual<int32_t>(Virtual<void*>(developer, 0x34), 0x28);
    int32_t passesLeft = tractSize * tractSize + 1;
    for (;;) {
        ClearTree(At<Tree>(self, local::kLots));
        ClearTree(At<Tree>(self, local::kSetD8));
        ReturnCandidates(&At<Vec>(self, local::kCandidates));
        At<uint8_t>(self, local::kNotFirstPass) = firstPass == 0 ? 1 : 0;
        At<int32_t>(self, local::kF8) = 0;
        At<int32_t>(self, local::kFC) = -1;
        At<int32_t>(self, local::k100) = -1;
        At<int32_t>(self, local::kF4) = 0;
        ClearTree(At<Tree>(self, local::kMap10C));
        int32_t best = 0;
        CollectLots(self);
        int32_t outcome = static_cast<int32_t>(CallPart(addr::cSC4TractDeveloper_Fn_0070A3D0, self));
        if (outcome > 0)
            best = outcome;
        if (outcome == kSuccess) {
            PushOutcome(outcomes, kSuccess);
            CallPart(addr::cSC4TractDeveloper_Fn_0070AD20, self);
            KeepRecord(self);
            *flag = At<uint8_t>(self, local::kFlag);
            return At<uint32_t>(self, local::kGrown);
        }
        if (static_cast<int32_t>(CallPart(addr::cSC4TractDeveloper_Fn_00709650, self)) == kSuccess) {
            outcome = static_cast<int32_t>(CallPart(addr::cSC4TractDeveloper_Fn_00708110, self));
            if (outcome > best)
                best = outcome;
            outcome = static_cast<int32_t>(CallPart(addr::cSC4TractDeveloper_Fn_007097E0, self));
            if (outcome > best)
                best = outcome;
            if (purpose != 5) {
                outcome = static_cast<int32_t>(CallPart(addr::cSC4TractDeveloper_Fn_00708C00, self));
                if (outcome > best)
                    best = outcome;
            }
            Vec& candidates = At<Vec>(self, local::kCandidates);
            if (candidates.begin == candidates.end) {
                if (best == kSuccess)
                    best = kNoLot;
                PushOutcome(outcomes, best);
                ++reinterpret_cast<int32_t*>(reinterpret_cast<Vec*>(record.rows.begin)[0].begin)[best];
                CallPart(addr::cSC4TractDeveloper_Fn_0070AD20, self);
            } else {
                CallPart(addr::cSC4TractDeveloper_Fn_00705750, self);
                if (candidates.begin != candidates.end)
                    SortCandidates(reinterpret_cast<Item*>(candidates.begin), reinterpret_cast<Item*>(candidates.end));
                // The game reads begin once and end on every step.
                for (auto* it = reinterpret_cast<Item*>(candidates.begin); it != reinterpret_cast<Item*>(candidates.end);
                     ++it) {
                    void* const candidate = *it;
                    if (Virtual<int32_t>(At<void*>(self, local::kDeveloper), 0x1C) == 5)
                        outcome = CallThis<int32_t>(addr::cSC4TractDeveloper_Fn_0070B640, self, candidate);
                    else
                        outcome = CallThis<int32_t>(addr::cSC4TractDeveloper_Fn_0070B440, self, candidate);
                    best = outcome;
                    PushOutcome(outcomes, outcome);
                    CallPart(addr::cSC4TractDeveloper_Fn_0070AD20, self);
                    if (outcome == kInsufficientResources) {
                        done = At<uint8_t>(self, local::kNotFirstPass);
                        break;
                    }
                    if (At<int32_t>(self, local::kGrown) >= At<int32_t>(self, local::kDemand)) {
                        done = 1;
                        break;
                    }
                    if (outcome == kSuccess) {
                        const int32_t now = Virtual<int32_t>(At<void*>(self, local::kDeveloper), 0x1C);
                        if (now >= 5 && now <= 8)
                            done = 1;
                        break;
                    }
                }
            }
        }
        if (!firstPass && best != kSuccess)
            done = 1;
        --passesLeft;
        firstPass = 0;
        if (passesLeft < 0 || done)
            break;
    }
    KeepRecord(self);
    *flag = At<uint8_t>(self, local::kFlag);
    return At<uint32_t>(self, local::kGrown);
}

// --- Installation ----------------------------------------------------------------

struct Detour {
    std::uintptr_t address;
    void* ours;
};

#define OPENSC4_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}

const Detour kDetours[] = {
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_00704990, EraseLotNodes),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_007049D0, EraseSetNodes),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_00704A10, EraseMapNodes),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_00704A90, UnguardedInsertionSort),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_00705D10, DestroyCandidate),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_00706B60, DeleteAttemptObjects),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_007062D0, InsertionSort),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_00707370, ReturnCandidates),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_007075C0, ClearAttempts),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_00707E60, EraseAttempts),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_00707F20, IntroSortLoop),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_00709570, InsertAttempts),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_0070BE40, EraseInner),
    OPENSC4_DETOUR(addr::cSC4TractDeveloper_Fn_0070C3F0, EraseOuter),
};

#undef OPENSC4_DETOUR

bool InstallGrow(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    auto* const slot = reinterpret_cast<std::uintptr_t*>(addr::cSC4TractDeveloper_vftable) + 5;
    if (*slot != addr::cSC4TractDeveloper_Grow) {
        log::Write("tract grow: slot 5 holds %08X, expected %08X; not installing", static_cast<unsigned>(*slot),
                   static_cast<unsigned>(addr::cSC4TractDeveloper_Grow));
        return false;
    }
    for (const Detour& detour : kDetours) {
        const addr::Entry* const entry = addr_check::Find(detour.address);
        void* original = nullptr;
        if (!entry || !hook::Install(*entry, detour.ours, &original))
            return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
        return false;
    *slot = reinterpret_cast<std::uintptr_t>(&Grow);
    VirtualProtect(slot, sizeof(*slot), old, &old);
    log::Write("tract grow: Grow and %u helpers are ours", static_cast<unsigned>(std::size(kDetours)));
    return true;
}

const bool g_registered = registry::Register({
    "simulators.tract.grow",
    "simulators/tract",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4TractDeveloper_vftable,
    "Our cSC4TractDeveloper::Grow (slot 5) with its helpers: the pass loop, the lots of a tract, the candidate "
    "sort and pool, the attempt record and the CheatDebugGrowth record map (docs/tract.md)",
    false,
    InstallGrow,
});

} // namespace
} // namespace opensc4::tract_sim
