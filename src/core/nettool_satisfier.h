// The vertex height constraint satisfier of the network tool (build 641;
// part s1): its layout, the layouts of its tables, lists and sets, and the
// helpers both of its files use (src/core/nettool_satisfier_1.cpp, the
// object's life, containers, input and results; nettool_satisfier_2.cpp,
// Solve and the range work under it). The top of nettool_satisfier_1.cpp
// says what stays the game's, NAM, floats, registers and the calls.
#pragma once

#include "core/nettool_sim.h"

#include <opensc4/addresses.h>

#include <bit>
#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim::satisfier {

// === The satisfier (0x64 bytes) =============================================
// Evidence: the constructor 0x00660810, the destructor 0x006602C0, Begin
// 0x0065E710, Clear 0x0065FBE0, Solve 0x00660B10 and the functions named.
//   +0x00 the vtable 0x00AAAEF8 (one slot: the deleting destructor 0x006606C0)
//   +0x04 uint8 begun (Begin sets it, Reset and the destructor clear it and
//         then empty the object)
//   +0x05 uint8 the last answer of Solve (NextEntry's, Propagate's)
//   +0x08, +0x0C floats: the range a new vertex starts with (Begin's two
//         arguments; the tool gives -FLT_MAX and FLT_MAX)
//   +0x10 the vertex whose range came out empty (Propagate, NextEntry)
//   +0x14 the results: a hash table vertex -> height (uint32 to uint32, 100
//         buckets; +0x18/+0x1C the buckets, +0x24 the count): SettleVertex
//         writes, GetResult and NextResult read
//   +0x28 the entries: a hash table vertex -> entry (nodes 0x24 bytes: next,
//         key, the entry's seven words; +0x2C/+0x30 the buckets, +0x38 the
//         count)
//   +0x3C the equality groups: a list of sets of vertices that take one
//         height (node 0xC: next, prev, the set; the set from operator new)
//   +0x40 the slope constraints |a - b| <= s (node 0x14: a, b, s)
//   +0x44 the curvature constraints |v1 - 2 v2 + v3| <= k (node 0x18: v1,
//         v2, v3, k)
//   +0x48 the sum constraints a + b in [lo, hi] (node 0x18: a, b, lo, hi)
//   +0x4C the midpoint constraints a - 2 b in [lo, hi] (node 0x18)
//   +0x50 the pending vector (begin, end, capacity) of (vertex, height)
//         pairs: fixed vertices NextEntry takes first
//   +0x5C, +0x60 the results' iterator (node, table) for NextResult
// The constructor does not write +0x05, +0x08 to +0x10 or +0x5C/+0x60. The
// list heads are allocated by the constructor (the sizes of their nodes).
//
// An entry (the node + 8; the puzzle fix's +0x8 lo / +0xC hi are node
// offsets): +0x00 lo, +0x04 hi (floats), +0x08 the preferred height (flag
// 2), +0x0C/+0x10 the preferred range (flag 4), +0x14 the wanted height
// (AddVertex's argument: NextEntry clamps it into the range), +0x18 flags:
// 1 restricted, 2 and 4 the preferences, 8 in an equality group, 0x10 in a
// slope constraint, 0x20 in a curvature constraint, 0x40 pushed as pending.
namespace sat {
inline constexpr std::size_t kBegun = 0x04, kAnswer = 0x05, kStartLo = 0x08, kStartHi = 0x0C, kFailed = 0x10;
inline constexpr std::size_t kResults = 0x14, kEntries = 0x28, kEntryCount = 0x38;
inline constexpr std::size_t kGroups = 0x3C, kSlopes = 0x40, kCurvatures = 0x44, kSums = 0x48, kMidpoints = 0x4C;
inline constexpr std::size_t kPending = 0x50, kCursor = 0x5C;
inline constexpr uint32_t kBuckets = 100;
inline constexpr uint32_t kGroupNodeSize = 0xC, kSlopeNodeSize = 0x14, kQuadNodeSize = 0x18;
inline constexpr uint32_t kSetSize = 0xC, kSetHeadSize = 0x14;
} // namespace sat
namespace entry {
inline constexpr std::size_t kLo = 0x0, kHi = 0x4, kPreferred = 0x8, kFlags = 0x18; // +0x14 the wanted height
inline constexpr std::size_t kWords = 7;
inline constexpr uint32_t kRestricted = 0x1, kInGroup = 0x8, kInSlope = 0x10, kInCurvature = 0x20, kPending = 0x40;
} // namespace entry
// A hash table (both of the object's): +0x4 the bucket vector, +0x10 the
// count; a node: +0x0 the next, +0x4 the key, +0x8 the value.
namespace table {
inline constexpr std::size_t kBegin = 0x4, kEnd = 0x8;
inline constexpr std::size_t kNext = 0x0, kKey = 0x4, kValue = 0x8;
} // namespace table
// A list (a pointer to its head node): node +0x0 next, +0x4 prev, +0x8 the
// value's words.
namespace list {
inline constexpr std::size_t kNext = 0x0, kPrev = 0x4, kValue = 0x8;
} // namespace list
// A set (0xC bytes: +0x0 the head node, +0x4 the count): nodes as the
// tool's trees (nettool_sim.h, tree::), the int32 vertex at +0x10.
namespace set {
inline constexpr std::size_t kHead = 0x0, kCount = 0x4;
} // namespace set

// A set on the stack as Propagate, PropagateAll and Solve keep theirs (the
// third word is never written).
struct LocalSet {
    char* head;
    uint32_t count;
    uint32_t unused;
};

// Insert's out pair (the node and whether it is new).
struct InsertResult {
    char* node;
    uint32_t inserted;
};

// A hash table iterator (Begin's out, EraseAt's argument).
struct Iterator {
    char* node;
    char* table;
};

// ====================================================================================
// Helpers
// ====================================================================================

inline char* Allocate(uint32_t size) { return CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, size); }
inline void Free(void* block) { CallCdecl<void, void*>(addr::SC4_Free, block); }
inline void* New(uint32_t size) { return CallCdecl<void*, uint32_t>(addr::SC4_operator_new, size); }
inline void Delete(void* object) { CallCdecl<void, void*>(addr::SC4_operator_delete, object); }

inline void Insert(void* set, const uint32_t* key)
{
    InsertResult out{};
    CallThis<InsertResult*, InsertResult*, const uint32_t*>(addr::SC4Uint32Set_Insert, set, &out, key);
}

inline void EraseSubtree(void* set, char* node) { CallThis<void, char*>(addr::SC4Uint32Set_EraseSubtree, set, node); }

inline float Float(uint32_t bits) { return std::bit_cast<float>(bits); }
inline uint32_t Bits(float value) { return std::bit_cast<uint32_t>(value); }

inline double Gap() { return static_cast<double>(Global<float>(addr::SC4VertexHeightSatisfier_Epsilon)); }
inline double MinusHalf() { return static_cast<double>(Global<float>(addr::SC4_FloatMinusHalf)); }

// The node of `key` in a hash table, or null: the bucket key % count (the
// count as (end - begin) >> 2, arithmetic; the division unsigned), then the
// chain.
inline char* Find(char* hashTable, uint32_t key)
{
    char* const begin = At<char*>(hashTable, table::kBegin);
    const uint32_t count =
        static_cast<uint32_t>(static_cast<int32_t>(At<char*>(hashTable, table::kEnd) - begin) >> 2);
    char* node = reinterpret_cast<char**>(begin)[key % count];
    while (node && At<uint32_t>(node, table::kKey) != key)
        node = At<char*>(node, table::kNext);
    return node;
}

// The entry of a node as the game computes it: node + 8, also for a null
// node (the game's callers then read through address 8, as ours do).
inline char* EntryOf(char* node)
{
    return reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(node) + table::kValue);
}

// The node after `node` in a set's order (the STL's increment, inlined by
// the game in every loop over a set).
inline char* Next(char* node)
{
    if (char* right = At<char*>(node, tree::kRight)) {
        while (char* left = At<char*>(right, tree::kLeft))
            right = left;
        return right;
    }
    char* parent = At<char*>(node, tree::kParent);
    while (node == At<char*>(parent, tree::kRight)) {
        node = parent;
        parent = At<char*>(parent, tree::kParent);
    }
    if (At<char*>(node, tree::kRight) != parent)
        node = parent;
    return node;
}

// A set's head linked to itself and the count cleared, in the game's order.
inline void ResetHead(void* object)
{
    char* const head = At<char*>(object, set::kHead);
    At<char*>(head, tree::kLeft) = head;
    At<char*>(At<char*>(object, set::kHead), tree::kParent) = nullptr;
    char* const again = At<char*>(object, set::kHead);
    At<char*>(again, tree::kRight) = again;
    At<uint32_t>(object, set::kCount) = 0;
}

// The game's inlined clear of a set whose count is not 0: down the root's
// left children, EraseSubtree on each one's right child and the node freed;
// then the head reset.
inline void ClearUnrolled(void* object)
{
    char* node = At<char*>(At<char*>(object, set::kHead), tree::kParent);
    while (node) {
        EraseSubtree(object, At<char*>(node, tree::kRight));
        char* const left = At<char*>(node, tree::kLeft);
        Free(node);
        node = left;
    }
    ResetHead(object);
}

// The other inlined clear: EraseSubtree on the root, then the head reset.
inline void ClearWhole(void* object)
{
    EraseSubtree(object, At<char*>(At<char*>(object, set::kHead), tree::kParent));
    ResetHead(object);
}

// A local set's start: the head pointer cleared, a 0x14-byte head node from
// the allocator, the count 0, the head's colour 0, no parent, its own
// leftmost and rightmost.
inline void ConstructLocal(LocalSet& local)
{
    local.head = nullptr;
    local.head = Allocate(sat::kSetHeadSize);
    local.count = 0;
    At<uint8_t>(local.head, 0) = 0;
    At<char*>(local.head, tree::kParent) = nullptr;
    At<char*>(local.head, tree::kLeft) = local.head;
    At<char*>(local.head, tree::kRight) = local.head;
}

inline void FreeHead(LocalSet& local)
{
    if (local.head)
        Free(local.head);
}

// A node taken out of a list and freed (the inlined erase).
inline char* EraseListNode(char* node)
{
    char* const next = At<char*>(node, list::kNext);
    char* const prev = At<char*>(node, list::kPrev);
    At<char*>(prev, list::kNext) = next;
    At<char*>(next, list::kPrev) = prev;
    Free(node);
    return next;
}

// A node linked in before the head (the inlined push_back).
inline void LinkBack(char* head, char* node)
{
    char* const prev = At<char*>(head, list::kPrev);
    At<char*>(node, list::kNext) = head;
    At<char*>(node, list::kPrev) = prev;
    At<char*>(prev, list::kNext) = node;
    At<char*>(head, list::kPrev) = node;
}

// The range check of 0x00635FA0 and 0x0065E6A0, inlined by the game in
// 0x0065EFA0, 0x0065F510 and 0x0065F720: the range's low limit raised to
// the pair's when it is below it, its high one lowered to the pair's when it
// is above it (each copied by its bits; `changed` set when not null); when
// the range is then inverted, false if lo - hi is above the gap or
// unordered, else hi = lo.
inline bool Narrow(char* range, char* pair, uint8_t* changed)
{
    if (At<float>(range, entry::kLo) < At<float>(pair, 0)) {
        At<uint32_t>(range, entry::kLo) = At<uint32_t>(pair, 0);
        if (changed)
            *changed = 1;
    }
    if (At<float>(range, entry::kHi) > At<float>(pair, 4)) {
        At<uint32_t>(range, entry::kHi) = At<uint32_t>(pair, 4);
        if (changed)
            *changed = 1;
    }
    if (!(At<float>(range, entry::kHi) < At<float>(range, entry::kLo)))
        return true;
    const double gap =
        static_cast<double>(At<float>(range, entry::kLo)) - static_cast<double>(At<float>(range, entry::kHi));
    if (!(gap <= Gap()))
        return false;
    At<uint32_t>(range, entry::kHi) = At<uint32_t>(range, entry::kLo);
    return true;
}

} // namespace opensc4::nettool_sim::satisfier
