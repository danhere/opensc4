// Our cSC4NetworkTool, part s1, second file (build 641): the vertex height
// constraint satisfier's solving, group simulators.nettool.solve (23
// detours at the game's addresses): Solve 0x00660B10, PropagateAll,
// Propagate, the two Apply dispatchers and the ten Apply functions, the
// constraint merges, the pending push, SettleVertex, the range arithmetic and
// the debug dump. The layouts and the shared helpers are in
// src/core/nettool_satisfier.h; what stays the game's (NextEntry 0x0065ED80,
// called at its address), NAM, the floats, the registers, the stack
// leftovers and the calls are at the top of src/core/nettool_satisfier_1.cpp.
#include "core/nettool_satisfier.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim::satisfier {
namespace {

// ====================================================================================
// simulators.nettool.solve: Solve and the range work under it
// ====================================================================================

// 0x00635FA0 (ret 4: a pair; this a range; RestrictVertex): Narrow
// without the changed flag. AL whether the range is not empty.
uint8_t __fastcall RangeIntersect(char* self, void*, char* pair)
{
    static bool logged = false;
    FirstCall("SatisfierRangeIntersect", logged);
    return Narrow(self, pair, nullptr) ? 1 : 0;
}

// 0x0065E6A0 (ret 8: a pair, changed out; this a range): changed cleared,
// then Narrow.
uint8_t __fastcall RangeIntersectChanged(char* self, void*, char* pair, uint8_t* changed)
{
    static bool logged = false;
    FirstCall("SatisfierRangeIntersectChanged", logged);
    *changed = 0;
    return Narrow(self, pair, changed) ? 1 : 0;
}

// 0x0065EC40 (ret 0xC: a vertex, a pair, changed out): AL 1 when the vertex
// has no entry (changed not written); else RangeIntersectChanged's answer.
uint8_t __fastcall IntersectVertexRange(char* self, void*, uint32_t vertex, char* pair, uint8_t* changed)
{
    static bool logged = false;
    FirstCall("SatisfierIntersectVertexRange", logged);
    char* const node = Find(self + sat::kEntries, vertex);
    if (!node)
        return 1;
    return CallThis<uint8_t, char*, uint8_t*>(addr::SC4VertexHeightSatisfier_RangeIntersectChanged, EntryOf(node),
                                              pair, changed)
               ? 1
               : 0;
}

// The merge of a new limit pair into an existing node's: the high limit
// lowered when the new one is below (fcomp; test ah, 5; jp), the low one
// raised when the new one is above (test ah, 0x41; jne), each by its bits;
// false when the node's hi is then below its lo or unordered.
bool MergeLimits(char* value, const uint32_t* words)
{
    if (Float(words[3]) < At<float>(value, 0xC))
        At<uint32_t>(value, 0xC) = words[3];
    if (Float(words[2]) > At<float>(value, 0x8))
        At<uint32_t>(value, 0x8) = words[2];
    return At<float>(value, 0xC) >= At<float>(value, 0x8);
}

// 0x0065E930 (ret 4: (a, b, lo, hi); the two curvature Apply functions):
// a - 2 b in [lo, hi] merged into the midpoint list: the node with the same
// a and b (that order) narrowed (MergeLimits; AL whether still not empty),
// else a new node at the back (AL 1).
uint8_t __fastcall MergeMidpoint(char* self, void*, const uint32_t* words)
{
    static bool logged = false;
    FirstCall("SatisfierMergeMidpoint", logged);
    char* const head = At<char*>(self, sat::kMidpoints);
    for (char* node = At<char*>(head, list::kNext); node != head; node = At<char*>(node, list::kNext))
        if (At<uint32_t>(node, list::kValue) == words[0] && At<uint32_t>(node, list::kValue + 4) == words[1])
            return MergeLimits(node + list::kValue, words) ? 1 : 0;
    char* const node = Allocate(sat::kQuadNodeSize);
    if (reinterpret_cast<std::uintptr_t>(node) + list::kValue != 0)
        for (std::size_t i = 0; i < 4; ++i)
            At<uint32_t>(node, list::kValue + 4 * i) = words[i];
    LinkBack(head, node);
    return 1;
}

// 0x0065E9E0 (ret 4: (a, b, lo, hi)): a + b in [lo, hi] merged into the sum
// list, the node matched in either order.
uint8_t __fastcall MergeSum(char* self, void*, const uint32_t* words)
{
    static bool logged = false;
    FirstCall("SatisfierMergeSum", logged);
    char* const head = At<char*>(self, sat::kSums);
    for (char* node = At<char*>(head, list::kNext); node != head; node = At<char*>(node, list::kNext)) {
        const uint32_t a = At<uint32_t>(node, list::kValue), b = At<uint32_t>(node, list::kValue + 4);
        if ((a == words[0] && b == words[1]) || (a == words[1] && b == words[0]))
            return MergeLimits(node + list::kValue, words) ? 1 : 0;
    }
    char* const node = Allocate(sat::kQuadNodeSize);
    if (reinterpret_cast<std::uintptr_t>(node) + list::kValue != 0)
        for (std::size_t i = 0; i < 4; ++i)
            At<uint32_t>(node, list::kValue + 4 * i) = words[i];
    LinkBack(head, node);
    return 1;
}

// 0x006600D0 (ret 8: a vertex, its height; Propagate): the pair pushed on
// the pending vector, in place while there is room (nothing written when the
// end is null), else through InsertN 0x007C61A0. AL 1.
uint8_t __fastcall PushPending(char* self, void*, uint32_t vertex, uint32_t height)
{
    static bool logged = false;
    FirstCall("SatisfierPushPending", logged);
    char* const vector = self + sat::kPending;
    uint32_t pair[2] = {vertex, height};
    char* const end = At<char*>(vector, 4);
    if (end != At<char*>(vector, 8)) {
        if (end) {
            At<uint32_t>(end, 0) = pair[0];
            At<uint32_t>(end, 4) = pair[1];
        }
        At<char*>(vector, 4) += 8;
        return 1;
    }
    CallThis<void, char*, uint32_t*, uint32_t*, uint32_t, uint32_t>(addr::SC4PointVector_InsertN, vector, end, pair,
                                                                     &vertex, 1u, 1u);
    return 1;
}

// 0x00660130 (ret 8: a vertex, its height; Solve): the height kept in the
// results (Lookup 0x004F8D30), the vertex's entry taken out (EraseAt
// 0x00489CF0) when it has one.
void __fastcall SettleVertex(char* self, void*, uint32_t vertex, uint32_t height)
{
    static bool logged = false;
    FirstCall("SatisfierSettleVertex", logged);
    *CallThis<uint32_t*, const uint32_t*>(addr::SC4Uint32HashMap_Lookup, self + sat::kResults, &vertex) = height;
    char* const entries = self + sat::kEntries;
    if (char* const node = Find(entries, vertex)) {
        Iterator it{node, entries};
        CallThis<void, Iterator*>(addr::SC4Uint32HashMap_EraseAt, entries, &it);
    }
}

// --- The Apply functions: one constraint kind applied from one vertex ---------
// "Fixed": the vertex has one height (Propagate's lo = hi case); the
// constraints of the vertex are taken out after use. "Range": the vertex has
// a range (lo, hi); the constraints stay. A neighbour whose range changed is
// inserted into `next` (the set of Propagate's next round).

// 0x0065F2C0 (ret 0xC: a vertex, its height, next): every other member of
// the vertex's group narrowed to (height, height); flag 8 cleared on every
// member's entry (the vertex's too); then the group taken out of the list
// and its set emptied, its head freed and the set deleted. AL 1 when there
// is no group; 0 when a member's range came out empty (the group stays).
uint8_t __fastcall ApplyGroupFixed(char* self, void*, uint32_t vertex, uint32_t height, char* next)
{
    static bool logged = false;
    FirstCall("SatisfierApplyGroupFixed", logged);
    char* const group = CallThis<char*, uint32_t>(addr::SC4VertexHeightSatisfier_FindGroup, self, vertex);
    if (!group)
        return 1;
    uint32_t pair[2] = {height, height};
    uint8_t changed = static_cast<uint8_t>(height); // the height's argument slot; always written before a read
    char* const head = At<char*>(group, set::kHead);
    for (char* node = At<char*>(head, tree::kLeft); node != head; node = Next(node)) {
        uint32_t member = At<uint32_t>(node, tree::kValue);
        char* const entryAt = EntryOf(Find(self + sat::kEntries, member));
        if (member != vertex) {
            if (!CallThis<uint8_t, uint32_t*, uint8_t*>(addr::SC4VertexHeightSatisfier_RangeIntersectChanged, entryAt,
                                                        pair, &changed))
                return 0;
            if (changed)
                Insert(next, &member);
        }
        At<uint32_t>(entryAt, entry::kFlags) &= ~entry::kInGroup;
    }
    char* const groups = At<char*>(self, sat::kGroups);
    for (char* node = At<char*>(groups, list::kNext); node != groups;) {
        char* const following = At<char*>(node, list::kNext);
        if (At<char*>(node, list::kValue) == group) {
            char* const prev = At<char*>(node, list::kPrev);
            At<char*>(prev, list::kNext) = following;
            At<char*>(following, list::kPrev) = prev;
            Free(node);
        }
        node = following;
    }
    if (At<uint32_t>(group, set::kCount))
        ClearWhole(group);
    if (void* const groupHead = At<void*>(group, set::kHead))
        Free(groupHead);
    Delete(group);
    return 1;
}

// 0x0065F440 (ret 0xC: a vertex, its range, next): every other member of
// the vertex's group narrowed to the range (IntersectVertexRange); the group
// stays. The changed byte is the vertex argument's low byte until written.
uint8_t __fastcall ApplyGroupRange(char* self, void*, uint32_t vertex, char* range, char* next)
{
    static bool logged = false;
    FirstCall("SatisfierApplyGroupRange", logged);
    char* const group = CallThis<char*, uint32_t>(addr::SC4VertexHeightSatisfier_FindGroup, self, vertex);
    if (!group)
        return 1;
    uint8_t changed = static_cast<uint8_t>(vertex);
    char* const head = At<char*>(group, set::kHead);
    for (char* node = At<char*>(head, tree::kLeft); node != head; node = Next(node)) {
        uint32_t member = At<uint32_t>(node, tree::kValue);
        if (member == vertex)
            continue;
        if (!CallThis<uint8_t, uint32_t, char*, uint8_t*>(addr::SC4VertexHeightSatisfier_IntersectVertexRange, self,
                                                          member, range, &changed))
            return 0;
        if (changed)
            Insert(next, &member);
    }
    return 1;
}

// The other vertex of a two-vertex constraint node (a, b at list::kValue):
// b when a is the vertex, a when b is; false when neither.
bool OtherOf(char* node, uint32_t vertex, uint32_t& other)
{
    if (At<uint32_t>(node, list::kValue) == vertex) {
        other = At<uint32_t>(node, list::kValue + 4);
        return true;
    }
    if (At<uint32_t>(node, list::kValue + 4) == vertex) {
        other = At<uint32_t>(node, list::kValue);
        return true;
    }
    return false;
}

// The loop of 0x0065F510 and 0x0065F720 (Fixed, over the sum or the slope
// list): for each node with the vertex and another vertex that is not
// negative, the other's range narrowed (Narrow, changed cleared first; AL 0
// on an empty range, the node kept), the other inserted into `next` when
// changed, the node taken out. `limits` gives the pair from the node's
// value. The changed byte starts as the height's low byte (the game keeps it
// in BL from the argument) and keeps its value where the other has no entry.
template <typename Limits>
uint8_t ApplyPairsFixed(char* self, std::size_t listAt, uint32_t vertex, uint32_t height, char* next, Limits limits)
{
    uint8_t changed = static_cast<uint8_t>(height);
    char* node = At<char*>(At<char*>(self, listAt), list::kNext);
    while (node != At<char*>(self, listAt)) {
        uint32_t other = 0;
        if (!OtherOf(node, vertex, other) || static_cast<int32_t>(other) < 0) {
            node = At<char*>(node, list::kNext);
            continue;
        }
        uint32_t pair[2];
        limits(node + list::kValue, Float(height), pair);
        if (char* const found = Find(self + sat::kEntries, other)) {
            changed = 0;
            if (!Narrow(EntryOf(found), reinterpret_cast<char*>(pair), &changed))
                return 0;
        }
        if (changed)
            Insert(next, &other);
        node = EraseListNode(node);
    }
    return 1;
}

// The loop of 0x0065F640 and 0x0065F850 (Range, the same lists): no node
// taken out; the other's range narrowed through RangeIntersectChanged; the
// changed byte starts as the range pointer's low byte (it lives in that
// argument's slot) and keeps its value where the other has no entry.
template <typename Limits>
uint8_t ApplyPairsRange(char* self, std::size_t listAt, uint32_t vertex, char* range, char* next, Limits limits)
{
    uint8_t changed = static_cast<uint8_t>(reinterpret_cast<std::uintptr_t>(range));
    char* const head = At<char*>(self, listAt);
    for (char* node = At<char*>(head, list::kNext); node != head; node = At<char*>(node, list::kNext)) {
        uint32_t other = 0;
        if (!OtherOf(node, vertex, other) || static_cast<int32_t>(other) < 0)
            continue;
        uint32_t pair[2];
        limits(node + list::kValue, range, pair);
        if (char* const found = Find(self + sat::kEntries, other))
            if (!CallThis<uint8_t, uint32_t*, uint8_t*>(addr::SC4VertexHeightSatisfier_RangeIntersectChanged,
                                                        EntryOf(found), pair, &changed))
                return 0;
        if (changed)
            Insert(next, &other);
    }
    return 1;
}

// 0x0065F510 (ret 0xC: a vertex, its height, next): a + b in [lo, hi] with
// the vertex at v: the other in [lo - v, hi - v].
uint8_t __fastcall ApplySumFixed(char* self, void*, uint32_t vertex, uint32_t height, char* next)
{
    static bool logged = false;
    FirstCall("SatisfierApplySumFixed", logged);
    return ApplyPairsFixed(self, sat::kSums, vertex, height, next, [](char* value, float v, uint32_t* pair) {
        pair[0] = Bits(static_cast<float>(static_cast<double>(At<float>(value, 8)) - static_cast<double>(v)));
        pair[1] = Bits(static_cast<float>(static_cast<double>(At<float>(value, 0xC)) - static_cast<double>(v)));
    });
}

// 0x0065F640 (ret 0xC: a vertex, its range, next): the other in
// [lo - range hi, hi - range lo].
uint8_t __fastcall ApplySumRange(char* self, void*, uint32_t vertex, char* range, char* next)
{
    static bool logged = false;
    FirstCall("SatisfierApplySumRange", logged);
    return ApplyPairsRange(self, sat::kSums, vertex, range, next, [](char* value, char* r, uint32_t* pair) {
        pair[0] = Bits(static_cast<float>(static_cast<double>(At<float>(value, 8)) -
                                          static_cast<double>(At<float>(r, entry::kHi))));
        pair[1] = Bits(static_cast<float>(static_cast<double>(At<float>(value, 0xC)) -
                                          static_cast<double>(At<float>(r, entry::kLo))));
    });
}

// 0x0065F720 (ret 0xC: a vertex, its height, next): |a - b| <= s with the
// vertex at v: the other in [v - s, v + s].
uint8_t __fastcall ApplySlopeFixed(char* self, void*, uint32_t vertex, uint32_t height, char* next)
{
    static bool logged = false;
    FirstCall("SatisfierApplySlopeFixed", logged);
    return ApplyPairsFixed(self, sat::kSlopes, vertex, height, next, [](char* value, float v, uint32_t* pair) {
        pair[0] = Bits(static_cast<float>(static_cast<double>(v) - static_cast<double>(At<float>(value, 8))));
        pair[1] = Bits(static_cast<float>(static_cast<double>(v) + static_cast<double>(At<float>(value, 8))));
    });
}

// 0x0065F850 (ret 0xC: a vertex, its range, next): the other in
// [range lo - s, range hi + s].
uint8_t __fastcall ApplySlopeRange(char* self, void*, uint32_t vertex, char* range, char* next)
{
    static bool logged = false;
    FirstCall("SatisfierApplySlopeRange", logged);
    return ApplyPairsRange(self, sat::kSlopes, vertex, range, next, [](char* value, char* r, uint32_t* pair) {
        pair[0] = Bits(static_cast<float>(static_cast<double>(At<float>(r, entry::kLo)) -
                                          static_cast<double>(At<float>(value, 8))));
        pair[1] = Bits(static_cast<float>(static_cast<double>(At<float>(r, entry::kHi)) +
                                          static_cast<double>(At<float>(value, 8))));
    });
}

// 0x0065EFA0 (ret 0xC: a vertex, its height, next): a - 2 b in [lo, hi].
// With a = v: b in [(hi - v) * -0.5, (lo - v) * -0.5], b's range narrowed
// inline (Narrow, changed cleared first); with b = v: a in [2v + lo, 2v + hi]
// through RangeIntersectChanged. AL 0 on an empty range (the node kept);
// else the node taken out. The changed byte is unset in the game until the
// first narrowing (top of file); ours starts at 0.
uint8_t __fastcall ApplyMidpointFixed(char* self, void*, uint32_t vertex, uint32_t height, char* next)
{
    static bool logged = false;
    FirstCall("SatisfierApplyMidpointFixed", logged);
    const double v = static_cast<double>(Float(height));
    uint8_t changed = 0;
    char* node = At<char*>(At<char*>(self, sat::kMidpoints), list::kNext);
    while (node != At<char*>(self, sat::kMidpoints)) {
        char* const value = node + list::kValue;
        const double lo = static_cast<double>(At<float>(value, 8)), hi = static_cast<double>(At<float>(value, 0xC));
        if (At<uint32_t>(value, 0) == vertex) {
            uint32_t pair[2];
            pair[1] = Bits(static_cast<float>((lo - v) * MinusHalf()));
            pair[0] = Bits(static_cast<float>((hi - v) * MinusHalf()));
            if (char* const found = Find(self + sat::kEntries, At<uint32_t>(value, 4))) {
                changed = 0;
                if (!Narrow(EntryOf(found), reinterpret_cast<char*>(pair), &changed))
                    return 0;
            }
            if (changed)
                Insert(next, &At<uint32_t>(value, 4));
            node = EraseListNode(node);
        } else if (At<uint32_t>(value, 4) == vertex) {
            const double twice = v + v;
            uint32_t pair[2] = {Bits(static_cast<float>(twice + lo)), Bits(static_cast<float>(twice + hi))};
            if (char* const found = Find(self + sat::kEntries, At<uint32_t>(value, 0)))
                if (!CallThis<uint8_t, uint32_t*, uint8_t*>(addr::SC4VertexHeightSatisfier_RangeIntersectChanged,
                                                            EntryOf(found), pair, &changed))
                    return 0;
            if (changed)
                Insert(next, &At<uint32_t>(value, 0));
            node = EraseListNode(node);
        } else {
            node = At<char*>(node, list::kNext);
        }
    }
    return 1;
}

// 0x0065F180 (ret 0xC: a vertex, its range, next): as ApplyMidpointFixed
// with the range: with a = the vertex, b in [(hi - range lo) * -0.5,
// (lo - range hi) * -0.5]; with b, a narrowed through IntersectVertexRange to
// [2 range lo + lo, 2 range hi + hi]. No node taken out. The changed byte as
// ApplyMidpointFixed's.
uint8_t __fastcall ApplyMidpointRange(char* self, void*, uint32_t vertex, char* range, char* next)
{
    static bool logged = false;
    FirstCall("SatisfierApplyMidpointRange", logged);
    uint8_t changed = 0;
    char* const head = At<char*>(self, sat::kMidpoints);
    for (char* node = At<char*>(head, list::kNext); node != head; node = At<char*>(node, list::kNext)) {
        char* const value = node + list::kValue;
        const double lo = static_cast<double>(At<float>(value, 8)), hi = static_cast<double>(At<float>(value, 0xC));
        const double rangeLo = static_cast<double>(At<float>(range, entry::kLo));
        const double rangeHi = static_cast<double>(At<float>(range, entry::kHi));
        if (At<uint32_t>(value, 0) == vertex) {
            uint32_t pair[2];
            pair[1] = Bits(static_cast<float>((lo - rangeHi) * MinusHalf()));
            pair[0] = Bits(static_cast<float>((hi - rangeLo) * MinusHalf()));
            if (char* const found = Find(self + sat::kEntries, At<uint32_t>(value, 4)))
                if (!CallThis<uint8_t, uint32_t*, uint8_t*>(addr::SC4VertexHeightSatisfier_RangeIntersectChanged,
                                                            EntryOf(found), pair, &changed))
                    return 0;
            if (changed)
                Insert(next, &At<uint32_t>(value, 4));
        } else if (At<uint32_t>(value, 4) == vertex) {
            uint32_t pair[2] = {Bits(static_cast<float>((rangeLo + rangeLo) + lo)),
                                Bits(static_cast<float>((rangeHi + rangeHi) + hi))};
            if (!CallThis<uint8_t, uint32_t, uint32_t*, uint8_t*>(addr::SC4VertexHeightSatisfier_IntersectVertexRange,
                                                                  self, At<uint32_t>(value, 0), pair, &changed))
                return 0;
            if (changed)
                Insert(next, &At<uint32_t>(value, 0));
        }
    }
    return 1;
}

// 0x0065F930 (ret 8: a vertex, its height): each curvature constraint
// (v1, v2, v3, k) with the vertex becomes a two-vertex one and is taken out:
// v1 = v: v3 - 2 v2 in [-k - v, k - v] (MergeMidpoint (v3, v2, ..));
// v2 = v: v1 + v3 in [2v - k, 2v + k] (MergeSum (v1, v3, ..)); v3 = v:
// v1 - 2 v2 in [-k - v, k - v] (MergeMidpoint (v1, v2, ..)). AL 0 when a
// merge leaves an empty pair (the node kept).
uint8_t __fastcall ApplyCurvatureFixed(char* self, void*, uint32_t vertex, uint32_t height)
{
    static bool logged = false;
    FirstCall("SatisfierApplyCurvatureFixed", logged);
    const double v = static_cast<double>(Float(height));
    char* node = At<char*>(At<char*>(self, sat::kCurvatures), list::kNext);
    while (node != At<char*>(self, sat::kCurvatures)) {
        char* const value = node + list::kValue;
        const uint32_t v1 = At<uint32_t>(value, 0), v2 = At<uint32_t>(value, 4), v3 = At<uint32_t>(value, 8);
        const double k = static_cast<double>(At<float>(value, 0xC));
        uint8_t ok = 0;
        if (v1 == vertex || (v2 != vertex && v3 == vertex)) {
            const uint32_t words[4] = {v1 == vertex ? v3 : v1, v2, Bits(static_cast<float>(-k - v)),
                                       Bits(static_cast<float>(k - v))};
            ok = CallThis<uint8_t, const uint32_t*>(addr::SC4VertexHeightSatisfier_MergeMidpoint, self, words);
        } else if (v2 == vertex) {
            const double twice = v + v;
            const uint32_t words[4] = {v1, v3, Bits(static_cast<float>(twice - k)),
                                       Bits(static_cast<float>(twice + k))};
            ok = CallThis<uint8_t, const uint32_t*>(addr::SC4VertexHeightSatisfier_MergeSum, self, words);
        } else {
            node = At<char*>(node, list::kNext);
            continue;
        }
        if (!ok)
            return 0;
        node = EraseListNode(node);
    }
    return 1;
}

// 0x0065FA10 (ret 8: a vertex, its range): as ApplyCurvatureFixed with the
// range, no node taken out: v1 or v3: [-k - range hi, k - range lo]; v2:
// [2 range lo - k, 2 range hi + k].
uint8_t __fastcall ApplyCurvatureRange(char* self, void*, uint32_t vertex, char* range)
{
    static bool logged = false;
    FirstCall("SatisfierApplyCurvatureRange", logged);
    char* const head = At<char*>(self, sat::kCurvatures);
    for (char* node = At<char*>(head, list::kNext); node != head; node = At<char*>(node, list::kNext)) {
        char* const value = node + list::kValue;
        const uint32_t v1 = At<uint32_t>(value, 0), v2 = At<uint32_t>(value, 4), v3 = At<uint32_t>(value, 8);
        const double k = static_cast<double>(At<float>(value, 0xC));
        const double rangeLo = static_cast<double>(At<float>(range, entry::kLo));
        const double rangeHi = static_cast<double>(At<float>(range, entry::kHi));
        uint8_t ok = 0;
        if (v1 == vertex || (v2 != vertex && v3 == vertex)) {
            const uint32_t words[4] = {v1 == vertex ? v3 : v1, v2, Bits(static_cast<float>(-k - rangeHi)),
                                       Bits(static_cast<float>(k - rangeLo))};
            ok = CallThis<uint8_t, const uint32_t*>(addr::SC4VertexHeightSatisfier_MergeMidpoint, self, words);
        } else if (v2 == vertex) {
            const uint32_t words[4] = {v1, v3, Bits(static_cast<float>((rangeLo + rangeLo) - k)),
                                       Bits(static_cast<float>((rangeHi + rangeHi) + k))};
            ok = CallThis<uint8_t, const uint32_t*>(addr::SC4VertexHeightSatisfier_MergeSum, self, words);
        } else {
            continue;
        }
        if (!ok)
            return 0;
    }
    return 1;
}

// 0x006601C0 (ret 0x10: a vertex, its height, its flags, next; Propagate):
// by the flags: 8 ApplyGroupFixed, 0x10 ApplySlopeFixed, 0x20
// ApplyCurvatureFixed, ApplySumFixed and ApplyMidpointFixed; AL 0 at the
// first that fails.
uint8_t __fastcall ApplyFixed(char* self, void*, uint32_t vertex, uint32_t height, uint32_t flags, char* next)
{
    static bool logged = false;
    FirstCall("SatisfierApplyFixed", logged);
    using Three = uint8_t (*)(std::uintptr_t, char*, uint32_t, uint32_t, char*);
    const Three call = [](std::uintptr_t at, char* object, uint32_t a, uint32_t b, char* c) {
        return CallThis<uint8_t, uint32_t, uint32_t, char*>(at, object, a, b, c);
    };
    if ((flags & entry::kInGroup) && !call(addr::SC4VertexHeightSatisfier_ApplyGroupFixed, self, vertex, height, next))
        return 0;
    if ((flags & entry::kInSlope) && !call(addr::SC4VertexHeightSatisfier_ApplySlopeFixed, self, vertex, height, next))
        return 0;
    if (!(flags & entry::kInCurvature))
        return 1;
    if (!CallThis<uint8_t, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_ApplyCurvatureFixed, self, vertex,
                                               height))
        return 0;
    if (!call(addr::SC4VertexHeightSatisfier_ApplySumFixed, self, vertex, height, next))
        return 0;
    return call(addr::SC4VertexHeightSatisfier_ApplyMidpointFixed, self, vertex, height, next) ? 1 : 0;
}

// 0x00660240 (ret 0x10: a vertex, its range, its flags, next; Propagate):
// the Range versions in the same order.
uint8_t __fastcall ApplyRange(char* self, void*, uint32_t vertex, char* range, uint32_t flags, char* next)
{
    static bool logged = false;
    FirstCall("SatisfierApplyRange", logged);
    using Three = uint8_t (*)(std::uintptr_t, char*, uint32_t, char*, char*);
    const Three call = [](std::uintptr_t at, char* object, uint32_t a, char* b, char* c) {
        return CallThis<uint8_t, uint32_t, char*, char*>(at, object, a, b, c);
    };
    if ((flags & entry::kInGroup) && !call(addr::SC4VertexHeightSatisfier_ApplyGroupRange, self, vertex, range, next))
        return 0;
    if ((flags & entry::kInSlope) && !call(addr::SC4VertexHeightSatisfier_ApplySlopeRange, self, vertex, range, next))
        return 0;
    if (!(flags & entry::kInCurvature))
        return 1;
    if (!CallThis<uint8_t, uint32_t, char*>(addr::SC4VertexHeightSatisfier_ApplyCurvatureRange, self, vertex, range))
        return 0;
    if (!call(addr::SC4VertexHeightSatisfier_ApplySumRange, self, vertex, range, next))
        return 0;
    return call(addr::SC4VertexHeightSatisfier_ApplyMidpointRange, self, vertex, range, next) ? 1 : 0;
}

// 0x00660390 (ret 4: a set of vertices; PropagateAll, Solve): rounds that
// alternate between the given set and a local one: each vertex of the
// current set applied (ApplyFixed with its lo when lo = hi, the entry then
// flagged 0x40 and pushed pending once; else ApplyRange), the neighbours
// that changed going into the other set; the current set emptied; until a
// round starts empty (AL 1). AL 0 when a vertex has no entry; also when its
// range is inverted or unordered or an Apply fails, then the vertex at +0x10.
// The local set is emptied and its head freed on every way out (the unrolled
// clear, but EraseSubtree on the root after a failed Apply).
uint8_t __fastcall Propagate(char* self, void*, char* given)
{
    static bool logged = false;
    FirstCall("SatisfierPropagate", logged);
    LocalSet local;
    ConstructLocal(local);
    char* const mine = reinterpret_cast<char*>(&local);
    for (uint32_t round = 0;; ++round) {
        char* const current = (round & 1) ? mine : given;
        char* const next = (round & 1) ? given : mine;
        if (!At<uint32_t>(current, set::kCount))
            break;
        char* const head = At<char*>(current, set::kHead);
        for (char* node = At<char*>(head, tree::kLeft); node != head; node = Next(node)) {
            const uint32_t vertex = At<uint32_t>(node, tree::kValue);
            char* const found = Find(self + sat::kEntries, vertex);
            if (!found) {
                if (local.count)
                    ClearUnrolled(&local);
                FreeHead(local);
                return 0;
            }
            char* const range = EntryOf(found);
            // fcomp; test ah, 1; jne: an inverted or unordered range fails.
            if (!(At<float>(range, entry::kHi) >= At<float>(range, entry::kLo))) {
                At<uint32_t>(self, sat::kFailed) = vertex;
                if (local.count)
                    ClearUnrolled(&local);
                FreeHead(local);
                return 0;
            }
            const uint32_t flags = At<uint32_t>(range, entry::kFlags);
            uint8_t ok;
            // fucompp; test ah, 0x44; jp: the fixed branch only on an
            // ordered equality.
            if (At<float>(range, entry::kLo) == At<float>(range, entry::kHi)) {
                ok = CallThis<uint8_t, uint32_t, uint32_t, uint32_t, char*>(
                    addr::SC4VertexHeightSatisfier_ApplyFixed, self, vertex, At<uint32_t>(range, entry::kLo), flags,
                    next);
                if (ok) {
                    const uint32_t now = At<uint32_t>(range, entry::kFlags);
                    if (!(now & entry::kPending)) {
                        At<uint32_t>(range, entry::kFlags) = now | entry::kPending;
                        CallThis<uint8_t, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_PushPending, self, vertex,
                                                              At<uint32_t>(range, entry::kLo));
                    }
                }
            } else {
                ok = CallThis<uint8_t, uint32_t, char*, uint32_t, char*>(addr::SC4VertexHeightSatisfier_ApplyRange,
                                                                         self, vertex, range, flags, next);
            }
            if (!ok) {
                At<uint32_t>(self, sat::kFailed) = vertex;
                if (local.count)
                    ClearWhole(&local);
                FreeHead(local);
                return 0;
            }
        }
        if (At<uint32_t>(current, set::kCount))
            ClearUnrolled(current);
    }
    if (local.count)
        ClearUnrolled(&local);
    FreeHead(local);
    return 1;
}

// 0x006606E0 (no arguments; Solve): every vertex of the entry table
// inserted into a local set (Begin, then the inlined increment), Propagate
// on it, the set emptied and its head freed. AL Propagate's answer.
uint8_t __fastcall PropagateAll(char* self, void*)
{
    static bool logged = false;
    FirstCall("SatisfierPropagateAll", logged);
    LocalSet local;
    ConstructLocal(local);
    Iterator it{};
    CallThis<Iterator*, Iterator*>(addr::SC4HashTable_Begin, self + sat::kEntries, &it);
    char* const entries = it.table;
    for (char* node = it.node; node;) {
        Insert(&local, &At<uint32_t>(node, table::kKey));
        char* next = At<char*>(node, table::kNext);
        if (!next) {
            const auto count = [entries] {
                return static_cast<uint32_t>(
                    static_cast<int32_t>(At<char*>(entries, table::kEnd) - At<char*>(entries, table::kBegin)) >> 2);
            };
            uint32_t index = At<uint32_t>(node, table::kKey) % count();
            for (;;) {
                ++index;
                if (index >= count())
                    break;
                next = At<char**>(entries, table::kBegin)[index];
                if (next)
                    break;
            }
        }
        node = next;
    }
    const uint8_t answer = CallThis<uint8_t, LocalSet*>(addr::SC4VertexHeightSatisfier_Propagate, self, &local);
    if (local.count)
        ClearUnrolled(&local);
    FreeHead(local);
    return answer;
}

// The game's string (cRZBaseString) as the dump keeps two on its stack.
struct DumpString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t references;
};

// The string's append 0x0040EBC0 (this the string's +4, ret 0xC: first,
// last, an unread tag).
void Append(DumpString& text, const char* first, const char* last)
{
    uint8_t tag = 0;
    CallThis<void*, const char*, const char*, uint8_t*>(addr::cSC4Advisor_F40EBC0, &text.begin, first, last, &tag);
}

// 0x006608C0 (no arguments; Solve when it fails): a debug dump whose text
// goes nowhere: for each group a string "<" with " %d,%d" per member (the
// terrain's slot 0x28 asked twice for the member's cell; the format filled
// through a temporary string's slot 0x6C) and " >", the first member looked
// up in the entries (the result unused), the string freed. Then the game
// walks the slope list and reads the other three lists' heads without
// effect: not repeated (reads only).
void __fastcall DumpGroups(char* self, void*)
{
    static bool logged = false;
    FirstCall("SatisfierDumpGroups", logged);
    char* const groups = At<char*>(self, sat::kGroups);
    for (char* group = At<char*>(groups, list::kNext); group != groups; group = At<char*>(group, list::kNext)) {
        char* const members = At<char*>(group, list::kValue);
        char* const head = At<char*>(members, set::kHead);
        DumpString text{};
        char* const buffer = Allocate(2);
        text.begin = buffer;
        text.capacity = buffer + 2;
        char* const copied = CallCdecl<char*, char*, const char*, uint32_t>(
            addr::SC4_memmove, buffer, reinterpret_cast<const char*>(addr::SC4VertexHeightSatisfier_DumpOpen), 1u);
        text.end = copied + 1;
        *text.end = 0;
        text.vtable = addr::SC4String_Vtable;
        text.references = 0;
        for (char* node = At<char*>(head, tree::kLeft); node != head; node = Next(node)) {
            DumpString cell{};
            char* const memory = Allocate(8);
            cell.capacity = memory + 8;
            cell.begin = memory;
            cell.end = memory;
            *memory = 0;
            cell.vtable = addr::SC4String_Vtable;
            cell.references = 0;
            const uint32_t vertex = At<uint32_t>(node, tree::kValue);
            int32_t x1 = 0, z1 = 0, x2 = 0, z2 = 0;
            Virtual<void>(Global<void*>(addr::SC4_Terrain), 0x28, vertex, &x1, &z1);
            Virtual<void>(Global<void*>(addr::SC4_Terrain), 0x28, vertex, &x2, &z2);
            using Format = void*(__cdecl*)(void*, const char*, ...);
            const auto format = reinterpret_cast<Format>(reinterpret_cast<void**>(cell.vtable)[0x6C / 4]);
            void* const formatted =
                format(&cell, reinterpret_cast<const char*>(addr::SC4VertexHeightSatisfier_DumpVertexFormat), x2, z1);
            const char* const chars = Virtual<const char*>(formatted, 0x14);
            const char* last = chars;
            while (*last)
                ++last;
            Append(text, chars, last);
            cell.vtable = addr::SC4String_Vtable;
            if (cell.begin)
                Free(cell.begin);
        }
        Append(text, reinterpret_cast<const char*>(addr::SC4VertexHeightSatisfier_DumpClose),
               reinterpret_cast<const char*>(addr::SC4VertexHeightSatisfier_DumpClose + 2));
        Find(self + sat::kEntries, At<uint32_t>(At<char*>(At<char*>(members, set::kHead), tree::kLeft), tree::kValue));
        text.vtable = addr::SC4String_Vtable;
        if (text.begin)
            Free(text.begin);
    }
}

// 0x00660B10 (no arguments; the tool's height pass 0x00637A80, slot 56
// 0x00644B20): PropagateAll; then while entries are left: NextEntry (the
// next vertex and its height), Propagate from it when its range changed,
// SettleVertex; the answer at +5 after each step, the loop ending on a 0 (or
// when the count read after the last settle is not positive). Then Clear,
// DumpGroups when the answer is 0, the local set emptied (EraseSubtree on
// its root) and its head freed. AL the answer.
uint8_t __fastcall Solve(char* self, void*)
{
    static bool logged = false;
    FirstCall("SatisfierSolve", logged);
    LocalSet local;
    ConstructLocal(local);
    const uint8_t first = CallThis<uint8_t>(addr::SC4VertexHeightSatisfier_PropagateAll, self);
    int32_t budget = At<int32_t>(self, sat::kEntryCount);
    At<uint8_t>(self, sat::kAnswer) = first;
    if (first) {
        while (At<uint32_t>(self, sat::kEntryCount)) {
            if (budget <= 0) {
                At<uint8_t>(self, sat::kAnswer) = 0;
                break;
            }
            uint32_t vertex = 0, height = 0;
            uint8_t changed = 0;
            const uint8_t ok = CallThis<uint8_t, uint32_t*, uint32_t*, uint8_t*>(
                addr::PuzzlePieceSolver_NextEntry, self, &vertex, &height, &changed);
            At<uint8_t>(self, sat::kAnswer) = ok;
            if (ok) {
                if (changed) {
                    Insert(&local, &vertex);
                    At<uint8_t>(self, sat::kAnswer) =
                        CallThis<uint8_t, LocalSet*>(addr::SC4VertexHeightSatisfier_Propagate, self, &local);
                    --budget;
                }
                if (!At<uint8_t>(self, sat::kAnswer))
                    break;
                CallThis<void, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_SettleVertex, self, vertex, height);
                budget = At<int32_t>(self, sat::kEntryCount);
            }
            if (!At<uint8_t>(self, sat::kAnswer))
                break;
        }
    }
    CallThis<void>(addr::SC4VertexHeightSatisfier_Clear, self);
    if (!At<uint8_t>(self, sat::kAnswer))
        CallThis<void>(addr::SC4VertexHeightSatisfier_DumpGroups, self);
    const uint8_t answer = At<uint8_t>(self, sat::kAnswer);
    if (local.count)
        ClearWhole(&local);
    FreeHead(local);
    return answer;
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kSolve[] = {
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_RangeIntersect, RangeIntersect),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_RangeIntersectChanged, RangeIntersectChanged),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_IntersectVertexRange, IntersectVertexRange),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_MergeMidpoint, MergeMidpoint),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_MergeSum, MergeSum),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_PushPending, PushPending),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_SettleVertex, SettleVertex),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_ApplyGroupFixed, ApplyGroupFixed),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_ApplyGroupRange, ApplyGroupRange),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_ApplySumFixed, ApplySumFixed),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_ApplySumRange, ApplySumRange),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_ApplySlopeFixed, ApplySlopeFixed),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_ApplySlopeRange, ApplySlopeRange),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_ApplyMidpointFixed, ApplyMidpointFixed),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_ApplyMidpointRange, ApplyMidpointRange),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_ApplyCurvatureFixed, ApplyCurvatureFixed),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_ApplyCurvatureRange, ApplyCurvatureRange),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_ApplyFixed, ApplyFixed),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_ApplyRange, ApplyRange),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_Propagate, Propagate),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_PropagateAll, PropagateAll),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_DumpGroups, DumpGroups),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_Solve, Solve),
};

#undef OPENSC4_DETOUR

bool InstallSolve(registry::Mode) { return InstallDetours(kSolve, "solve"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.solve", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::SC4VertexHeightSatisfier_Solve,
                        "Our network tool's vertex height solving but NextEntry: Solve, the propagation, the ten "
                        "Apply functions, the constraint merges and the range arithmetic (23 detours; "
                        "src/core/nettool_satisfier_2.cpp)",
                        false, InstallSolve}),
};

} // namespace
} // namespace opensc4::nettool_sim::satisfier
