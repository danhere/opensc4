// Our cSC4NetworkTool, part s1, first file (build 641; the tool's class map
// and fields are in src/core/nettool_sim.h): the vertex height constraint
// satisfier
// (the class has no name in the exe; ours: SC4VertexHeightSatisfier). The
// tool keeps two, at +0x124 and +0x188; its height passes give it the
// vertices of a drag with a range of heights each and constraints between
// them, and Solve (0x00660B10) narrows the ranges until every vertex has a
// height or one range is empty. Two groups, each a list of detours at the
// game's addresses, one per file (the layouts and the shared helpers are in
// src/core/nettool_satisfier.h):
//   simulators.nettool.satisfier (this file): the object's life and
//     containers, the vertices and constraints the tool puts in, the results
//     it reads back (22 detours);
//   simulators.nettool.solve (nettool_satisfier_2.cpp): Solve and everything
//     under it, the range arithmetic (23 detours).
// The split: the first group moves words and compares at most one float
// (the two inserts keep the smaller limit), the second is the float work.
//
// Every function was written from the disassembly. Calls between them go to
// the game's addresses (through the address entries), so each detour stands
// alone and OPENSC4_NETTOOL_SKIP bisects them.
//
// Left as the game's:
//   - NextEntry 0x0065ED80 (the next vertex to settle and its height):
//     sc4fix patches inside it (0x0065EE3E, 0x0065EE66) and our
//     fixes.puzzle_piece detours its entry (src/core/puzzle_piece_fix.cpp).
//     Solve calls it at its address (addr::PuzzlePieceSolver_NextEntry).
//   - the template code under ours: the set's Insert 0x0046C0E0 and
//     EraseSubtree 0x0065E740 and Construct 0x0065E8C0, the hash tables'
//     Construct 0x00829BC0, Clear 0x008EBA60, Begin 0x004B0390, NextBucket
//     0x0053DCF0, the uint32 map's Lookup 0x004F8D30 and EraseAt 0x00489CF0,
//     the entry map's Index 0x0065FAD0, the list's Destroy 0x005650A0 and
//     remove 0x00809580, the pair vector's InsertN 0x007C61A0, the string's
//     append 0x0040EBC0, memmove 0x009EEB70.
//
// NAM. Its DLL calls InsertSlopeConstraint 0x0065FF60 at its address from
// compiled C++ (thiscall, three arguments, `ret 0xC`; NetworkSlopes), and its
// hook inside the guarded 0x00636690 reads the satisfier by layout; the
// guarded 0x00636690 itself calls 0x0065FDB0 and 0x0065FF60, and 0x00636030
// (which NAM calls) calls 0x00660010. Ours keep the conventions, keep every
// field, list, set and table in the game's memory with the game's layout
// (through the game's allocator and template code) and keep nothing across
// calls. Registers: the game's 0x0065FF60, 0x0065FDB0 and 0x00660010 leave
// EAX, ECX and EDX path-dependent (a node, a flag word, the list tail, the
// caller's own values on the early exits), so there is no fixed state to
// keep; the scan of every game caller (0x006368C1, 0x006368DF and the eight
// in 0x00644710 for 0x0065FF60; 0x00636861, 0x00636882, 0x006368A3, the
// eleven in 0x00644710 and 0x0065795E for 0x0065FDB0; 0x00636064, 0x006449B6,
// 0x00644A08 for 0x00660010; following both branches) found none that reads
// EAX, ECX or EDX before writing it, and NAM's compiled C++ cannot rely on
// ECX or EDX across a call. So all three are plain detours. The same scan
// over every other game caller of the part (0x0062A5C0, 0x0062B39D...,
// 0x00636xxx, 0x00637A80, 0x00644xxx, 0x00657xxx, 0x00658xxx) and over the
// part's own internal calls found none either; AL is the answer where the
// game's is a byte (callers test AL, or move AL), EAX the game's where it is
// a pointer (the constructor, the deleting destructors, FindGroup,
// AppendCurvature).
//
// Floats. The game runs the x87 at 53 bits, so every intermediate here is a
// double: the fields are floats (fld float is exact), the operations are
// fsub, fadd, fmul by -0.5 (0x00A949FC) and doubling (fadd st, st), each
// rounded to 53 bits as a double operation rounds, and each result is stored
// as a float (fstp dword) where the game stores it. The constants are read
// from the game's .rdata (-0.5 at 0x00A949FC, the gap 0.01 at 0x00AA8A74).
// Comparisons keep the game's NaN branches: `fcomp; test ah, 5; jp skip`
// skips when a >= b or unordered (we act on a < b), `test ah, 0x41; jne skip`
// skips unless a > b, `test ah, 1; jne fail` fails when a < b or unordered
// (we go on when a >= b), `fsub; fcomp gap; test ah, 0x41; jp fail` fails
// when the difference is above the gap or unordered (we go on when it is
// <= the gap), `fucompp; test ah, 0x44; jp` takes the equal branch only on
// an ordered equality. Values move as words (a limit is copied by its bits,
// as the game's `mov` does). Nothing is kept on the x87 stack across a call
// and there is no transcendental or integer conversion, so no naked helper.
//
// Stack leftovers (documented, not repeatable):
//   - AddVertex 0x0065FB60 writes the new entry's seven words from its
//     stack and never sets words 2 to 4 (+0x8 to +0x10 of the entry: the
//     preferred height and range); ours writes 0 there (what Index 0x0065FAD0
//     inserts). They are read only under flags 2 and 4 (NextEntry), which
//     SetPreferredHeight / SetPreferredRange set together with the words.
//     RestrictVertex 0x0065FED0 has the same insert on a path that cannot
//     run (the vertex was just found); ours writes 0 to words 2 to 5 there.
//   - the "changed" byte of ApplyMidpointFixed 0x0065EFA0 ([esp+0x13]) and
//     ApplyMidpointRange 0x0065F180 ([esp+0xF]) is read without a write when
//     a constraint's other vertex is no longer in the entry table; the game
//     then sees the byte of an earlier constraint of the same call, or before
//     the first one an unset stack byte. Ours keeps one byte across the call
//     (as the game) and starts it at 0. Every vertex of a constraint was in
//     the table when the constraint went in; a vertex leaves it only when
//     settled (SettleVertex 0x00660130), after its constraints were applied
//     and taken out, so this was not seen to happen.
//   The same byte in ApplyGroupRange 0x0065F440, ApplySumFixed 0x0065F510,
//   ApplySumRange 0x0065F640, ApplySlopeFixed 0x0065F720 and
//   ApplySlopeRange 0x0065F850 lives in an argument's slot (the vertex's,
//   the height's or the range pointer's low byte) and is repeated exactly.
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the part's own at their addresses: NextEntry 0x0065ED80 three (ret
//     0xC: vertex out, height out, changed out), RangeIntersect 0x00635FA0
//     one, RangeIntersectChanged 0x0065E6A0 two, FindGroup 0x0065E810 one,
//     AppendCurvature 0x0065E870 one (this the list +0x44), MergeMidpoint
//     0x0065E930 and MergeSum 0x0065E9E0 one, DeleteGroup 0x0065EAA0 one
//     (this a set), IntersectVertexRange 0x0065EC40 three, OrVertexFlags
//     0x0065ECA0 two, MergeGroups 0x0065ED00 two (`this` not read), the ten
//     Apply* three (ret 0xC) but the curvature ones two (ret 8), AddVertex
//     0x0065FB60 two, Clear 0x0065FBE0 none, RestrictVertex 0x0065FED0
//     three, PushPending 0x006600D0 two, SettleVertex 0x00660130 two,
//     ApplyFixed 0x006601C0 and ApplyRange 0x00660240 four (ret 0x10),
//     Destruct 0x006602C0 none, Propagate 0x00660390 one, PropagateAll
//     0x006606E0 none, DumpGroups 0x006608C0 none;
//   - the set (0xC bytes: head, count, a word never written; nodes 0x14:
//     colour, parent, left, right, an int32): Insert 0x0046C0E0 two (ret 8:
//     out pair, key address), EraseSubtree 0x0065E740 one (ret 4), Construct
//     0x0065E8C0 none;
//   - the hash tables: Construct 0x00829BC0 one (ret 4: 100), Clear
//     0x008EBA60 none, Begin 0x004B0390 one (ret 4: iterator out), NextBucket
//     0x0053DCF0 none (this the iterator), Lookup 0x004F8D30 one (ret 4: key
//     address; the value's address back), EraseAt 0x00489CF0 one (ret 4:
//     iterator address), Index 0x0065FAD0 one (ret 4: key address);
//   - the lists: Destroy 0x005650A0 none, remove 0x00809580 one (ret 4: the
//     value's address); the pending vector's InsertN 0x007C61A0 five (ret
//     0x14: where, the value's address, an unread tag's address, 1, 1);
//   - the dump: the terrain 0x00B43CF4 slot 0x28 three (ret 0xC: a vertex,
//     x out, z out), a string's slot 0x6C (cdecl, varargs: the string, the
//     format, two ints; `add esp, 0x10`) and slot 0x14 none, the string's
//     append 0x0040EBC0 three (ret 0xC: first, last, an unread tag's
//     address), memmove 0x009EEB70 cdecl three;
//   - the STL allocator 0x0090CF54 and free 0x0090CF63 cdecl one; operator
//     new 0x005E55E0 and delete 0x005E5620 cdecl one.
#include "core/nettool_satisfier.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>

namespace opensc4::nettool_sim::satisfier {
namespace {

// ====================================================================================
// simulators.nettool.satisfier: life, containers, input and results
// ====================================================================================

// 0x00660810 (no arguments; the network tool's constructor, twice): the
// vtable, +4 cleared, the two hash tables (their words +4 to +0x10 cleared,
// then 100 buckets), the five list heads (each field cleared, a head node of
// the list's node size linked to itself), the pending vector cleared. EAX
// this.
char* __fastcall Construct(char* self, void*)
{
    static bool logged = false;
    FirstCall("SatisfierConstruct", logged);
    At<std::uintptr_t>(self, 0) = addr::SC4VertexHeightSatisfier_vftable;
    At<uint8_t>(self, sat::kBegun) = 0;
    for (const std::size_t at : {sat::kResults, sat::kEntries}) {
        for (std::size_t word = 4; word <= 0x10; word += 4)
            At<uint32_t>(self, at + word) = 0;
        CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Construct, self + at, sat::kBuckets);
    }
    const std::pair<std::size_t, uint32_t> lists[] = {{sat::kGroups, sat::kGroupNodeSize},
                                                      {sat::kSlopes, sat::kSlopeNodeSize},
                                                      {sat::kCurvatures, sat::kQuadNodeSize},
                                                      {sat::kSums, sat::kQuadNodeSize},
                                                      {sat::kMidpoints, sat::kQuadNodeSize}};
    for (const auto& [at, size] : lists) {
        At<char*>(self, at) = nullptr;
        char* const head = Allocate(size);
        At<char*>(head, list::kNext) = head;
        At<char*>(head, list::kPrev) = head;
        At<char*>(self, at) = head;
    }
    for (std::size_t word = 0; word < 0xC; word += 4)
        At<uint32_t>(self, sat::kPending + word) = 0;
    return self;
}

// 0x006602C0 (no arguments; the network tool's destructor 0x0062CAF0 and
// the deleting destructor): the vtable written back; when begun, +4 cleared,
// Clear and the results' Clear; the pending vector's memory freed; the five
// lists destroyed (+0x4C first); each hash table cleared and its buckets
// freed, the entries first.
void __fastcall Destruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("SatisfierDestruct", logged);
    const uint8_t begun = At<uint8_t>(self, sat::kBegun);
    At<std::uintptr_t>(self, 0) = addr::SC4VertexHeightSatisfier_vftable;
    if (begun) {
        At<uint8_t>(self, sat::kBegun) = 0;
        CallThis<void>(addr::SC4VertexHeightSatisfier_Clear, self);
        CallThis<void>(addr::SC4Uint32HashSet_Clear, self + sat::kResults);
    }
    if (void* const pending = At<void*>(self, sat::kPending))
        Free(pending);
    for (const std::size_t at : {sat::kMidpoints, sat::kSums, sat::kCurvatures, sat::kSlopes, sat::kGroups})
        CallThis<void>(addr::SC4PairList_Destroy, self + at);
    for (const std::size_t at : {sat::kEntries, sat::kResults}) {
        CallThis<void>(addr::SC4Uint32HashSet_Clear, self + at);
        if (void* const buckets = At<void*>(self, at + table::kBegin))
            Free(buckets);
    }
}

// 0x006606C0 (slot 0 of the vtable 0x00AAAEF8, ret 4: flags): the
// destructor at its address, operator delete when bit 0 is set; EAX this.
char* __fastcall DeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("SatisfierDeletingDestructor", logged);
    CallThis<void>(addr::SC4VertexHeightSatisfier_Destruct, self);
    if (flags & 1)
        Delete(self);
    return self;
}

// 0x0065E710 (ret 8: the start range's lo and hi; the tool's height pass
// 0x00637A80, 0x0062A5C0, slot 56 0x00644B20): false when already begun;
// else the range kept, the begun flag and the answer set, true.
uint8_t __fastcall Begin(char* self, void*, uint32_t lo, uint32_t hi)
{
    static bool logged = false;
    FirstCall("SatisfierBegin", logged);
    if (At<uint8_t>(self, sat::kBegun))
        return 0;
    At<uint32_t>(self, sat::kStartLo) = lo;
    At<uint8_t>(self, sat::kBegun) = 1;
    At<uint8_t>(self, sat::kAnswer) = 1;
    At<uint32_t>(self, sat::kStartHi) = hi;
    return 1;
}

// 0x006601A0 (no arguments; the same three callers): when begun, the flag
// cleared, Clear and the results' Clear. AL 1.
uint8_t __fastcall Reset(char* self, void*)
{
    static bool logged = false;
    FirstCall("SatisfierReset", logged);
    if (At<uint8_t>(self, sat::kBegun)) {
        At<uint8_t>(self, sat::kBegun) = 0;
        CallThis<void>(addr::SC4VertexHeightSatisfier_Clear, self);
        CallThis<void>(addr::SC4Uint32HashSet_Clear, self + sat::kResults);
    }
    return 1;
}

// 0x0065FBE0 (no arguments; the destructor, Reset, Solve): the entries
// cleared; every group's set emptied (the unrolled clear when it has
// members), then emptied again when it still has some, its head freed and
// the set deleted; the five lists' nodes freed and their heads linked to
// themselves; the pending vector emptied (end = begin).
void __fastcall Clear(char* self, void*)
{
    static bool logged = false;
    FirstCall("SatisfierClear", logged);
    CallThis<void>(addr::SC4Uint32HashSet_Clear, self + sat::kEntries);
    char* const groups = At<char*>(self, sat::kGroups);
    for (char* node = At<char*>(groups, list::kNext); node != groups; node = At<char*>(node, list::kNext)) {
        char* const members = At<char*>(node, list::kValue);
        if (At<uint32_t>(members, set::kCount))
            ClearUnrolled(members);
        if (char* const again = At<char*>(node, list::kValue)) {
            if (At<uint32_t>(again, set::kCount))
                ClearWhole(again);
            if (void* const head = At<void*>(again, set::kHead))
                Free(head);
            Delete(again);
        }
    }
    for (const std::size_t at : {sat::kGroups, sat::kSlopes, sat::kCurvatures, sat::kSums, sat::kMidpoints}) {
        char* node = At<char*>(At<char*>(self, at), list::kNext);
        if (node != At<char*>(self, at)) {
            do {
                char* const freed = node;
                node = At<char*>(node, list::kNext);
                Free(freed);
            } while (node != At<char*>(self, at));
        }
        char* const head = At<char*>(self, at);
        At<char*>(head, list::kNext) = head;
        char* const again = At<char*>(self, at);
        At<char*>(again, list::kPrev) = again;
    }
    At<char*>(self, sat::kPending + 4) = At<char*>(self, sat::kPending);
}

// 0x0065FB60 (ret 8: a vertex, its wanted height; 0x006360C0, 0x00644710,
// 0x00644B20, 0x00658040, AddFixedVertex): when the vertex has no entry, one
// made through Index 0x0065FAD0 with the start range, the wanted height and
// flag 1. The game's words 2 to 4 are its stack's leftovers (top of file);
// ours 0.
void __fastcall AddVertex(char* self, void*, uint32_t vertex, uint32_t wanted)
{
    static bool logged = false;
    FirstCall("SatisfierAddVertex", logged);
    if (Find(self + sat::kEntries, vertex))
        return;
    const uint32_t words[entry::kWords] = {
        At<uint32_t>(self, sat::kStartLo), At<uint32_t>(self, sat::kStartHi), 0, 0, 0, wanted, entry::kRestricted};
    uint32_t* const value =
        CallThis<uint32_t*, const uint32_t*>(addr::SC4Uint32Map28_Index, self + sat::kEntries, &vertex);
    for (std::size_t i = 0; i < entry::kWords; ++i)
        value[i] = words[i];
}

// 0x0065FED0 (ret 0xC: a vertex, lo, hi; 0x006360C0, 0x00644B20, 0x00658040,
// AddFixedVertex): flag 1 ORed in (OrVertexFlags); nothing when the vertex
// has no entry; else its range narrowed by (lo, hi) (RangeIntersect). The
// insert after it cannot run (the vertex was just found); ours writes its
// leftover words as 0.
void __fastcall RestrictVertex(char* self, void*, uint32_t vertex, uint32_t lo, uint32_t hi)
{
    static bool logged = false;
    FirstCall("SatisfierRestrictVertex", logged);
    if (!CallThis<uint8_t, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_OrVertexFlags, self, vertex,
                                                 entry::kRestricted))
        return;
    uint32_t pair[2] = {lo, hi};
    if (char* const node = Find(self + sat::kEntries, vertex)) {
        CallThis<uint8_t, uint32_t*>(addr::SC4VertexHeightSatisfier_RangeIntersect, EntryOf(node), pair);
        return;
    }
    const uint32_t words[entry::kWords] = {lo, hi, 0, 0, 0, 0, entry::kRestricted};
    uint32_t* const value =
        CallThis<uint32_t*, const uint32_t*>(addr::SC4Uint32Map28_Index, self + sat::kEntries, &vertex);
    for (std::size_t i = 0; i < entry::kWords; ++i)
        value[i] = words[i];
}

// 0x00660360 (ret 8: a vertex, a height; 0x0062A5C0, 0x006360C0,
// 0x00644B20, 0x00658040): AddVertex (the height wanted), then
// RestrictVertex to (height, height).
void __fastcall AddFixedVertex(char* self, void*, uint32_t vertex, uint32_t height)
{
    static bool logged = false;
    FirstCall("SatisfierAddFixedVertex", logged);
    CallThis<void, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_AddVertex, self, vertex, height);
    CallThis<void, uint32_t, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_RestrictVertex, self, vertex, height,
                                                 height);
}

// 0x0065ECA0 (ret 8: a vertex, flags): the flags ORed into the vertex's
// entry; AL whether it has one.
uint8_t __fastcall OrVertexFlags(char* self, void*, uint32_t vertex, uint32_t flags)
{
    static bool logged = false;
    FirstCall("SatisfierOrVertexFlags", logged);
    char* const node = Find(self + sat::kEntries, vertex);
    if (!node)
        return 0;
    At<uint32_t>(EntryOf(node), entry::kFlags) |= flags;
    return 1;
}

// 0x0065EB90 (ret 8: a vertex, a height; 0x006360C0, 0x00644B20,
// 0x00658040): when the vertex has an entry, flag 2 and the preferred
// height.
void __fastcall SetPreferredHeight(char* self, void*, uint32_t vertex, uint32_t height)
{
    static bool logged = false;
    FirstCall("SatisfierSetPreferredHeight", logged);
    if (char* const node = Find(self + sat::kEntries, vertex)) {
        At<uint32_t>(EntryOf(node), entry::kFlags) |= 2;
        At<uint32_t>(EntryOf(node), entry::kPreferred) = height;
    }
}

// 0x0065EBE0 (ret 0xC: a vertex, lo, hi; 0x006360C0, 0x00658040): when the
// vertex has an entry, flag 4 and the preferred range.
void __fastcall SetPreferredRange(char* self, void*, uint32_t vertex, uint32_t lo, uint32_t hi)
{
    static bool logged = false;
    FirstCall("SatisfierSetPreferredRange", logged);
    if (char* const node = Find(self + sat::kEntries, vertex)) {
        At<uint32_t>(EntryOf(node), entry::kFlags) |= 4;
        At<uint32_t>(EntryOf(node), entry::kPreferred + 4) = lo;
        At<uint32_t>(EntryOf(node), entry::kPreferred + 8) = hi;
    }
}

// 0x0065E810 (ret 4: a vertex): the first group whose set has the vertex
// (lower bound by signed compare), or 0.
char* __fastcall FindGroup(char* self, void*, int32_t vertex)
{
    static bool logged = false;
    FirstCall("SatisfierFindGroup", logged);
    char* const groups = At<char*>(self, sat::kGroups);
    for (char* node = At<char*>(groups, list::kNext); node != groups; node = At<char*>(node, list::kNext)) {
        char* const members = At<char*>(node, list::kValue);
        char* const head = At<char*>(members, set::kHead);
        char* found = head;
        for (char* at = At<char*>(head, tree::kParent); at;) {
            if (At<int32_t>(at, tree::kValue) < vertex) {
                at = At<char*>(at, tree::kRight);
            } else {
                found = at;
                at = At<char*>(at, tree::kLeft);
            }
        }
        if (found == head || vertex < At<int32_t>(found, tree::kValue))
            found = head;
        if (found != head)
            return members;
    }
    return nullptr;
}

// 0x0065ED00 (ret 8: into, from; `this` not read; InsertEqualityConstraint):
// every member of `from` inserted into `into`, in order.
void __fastcall MergeGroups(char*, void*, char* into, char* from)
{
    static bool logged = false;
    FirstCall("SatisfierMergeGroups", logged);
    char* const head = At<char*>(from, set::kHead);
    for (char* node = At<char*>(head, tree::kLeft); node != head; node = Next(node))
        Insert(into, &At<uint32_t>(node, tree::kValue));
}

// 0x0065EAA0 (ret 4: flags; this a group's set; InsertEqualityConstraint):
// the set emptied (EraseSubtree on the root) when it has members, its head
// freed, the set deleted when bit 0 is set. EAX this.
char* __fastcall DeleteGroup(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("SatisfierDeleteGroup", logged);
    if (At<uint32_t>(self, set::kCount))
        ClearWhole(self);
    if (void* const head = At<void*>(self, set::kHead))
        Free(head);
    if (flags & 1)
        Delete(self);
    return self;
}

// 0x0065FDB0 (ret 8: two vertices; 0x00636690 (guarded), 0x00644710,
// 0x00657860): the two must take one height. Flag 8 ORed into both entries;
// then by their groups: none, a new set in a new group with both; one, the
// other vertex inserted into it; two different, the second merged into the
// first, its group taken out of the list (remove 0x00809580) and its set
// deleted (DeleteGroup).
void __fastcall InsertEqualityConstraint(char* self, void*, uint32_t first, uint32_t second)
{
    static bool logged = false;
    FirstCall("SatisfierInsertEqualityConstraint", logged);
    CallThis<uint8_t, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_OrVertexFlags, self, first, entry::kInGroup);
    CallThis<uint8_t, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_OrVertexFlags, self, second,
                                          entry::kInGroup);
    char* const group1 = CallThis<char*, uint32_t>(addr::SC4VertexHeightSatisfier_FindGroup, self, first);
    char* group2 = CallThis<char*, uint32_t>(addr::SC4VertexHeightSatisfier_FindGroup, self, second);
    if (!group1) {
        if (group2) {
            Insert(group2, &first);
            return;
        }
        void* const memory = New(sat::kSetSize);
        char* const members = memory ? CallThis<char*>(addr::SC4Uint32Set_Construct, memory) : nullptr;
        char* const head = At<char*>(self, sat::kGroups);
        char* const node = Allocate(sat::kGroupNodeSize);
        if (reinterpret_cast<std::uintptr_t>(node) + list::kValue != 0)
            At<char*>(node, list::kValue) = members;
        LinkBack(head, node);
        Insert(members, &first);
        Insert(members, &second);
        return;
    }
    if (!group2) {
        Insert(group1, &second);
        return;
    }
    if (group1 == group2)
        return;
    CallThis<void, char*, char*>(addr::SC4VertexHeightSatisfier_MergeGroups, self, group1, group2);
    CallThis<void, char**>(addr::SC4ListIterator_FreeList, self + sat::kGroups, &group2);
    CallThis<char*, uint32_t>(addr::SC4VertexHeightSatisfier_DeleteGroup, group2, 1u);
}

// 0x0065FF60 (ret 0xC: v1, v2, a slope; NAM's DLL calls it at this address,
// and the guarded 0x00636690 and 0x00644710 do): |v1 - v2| <= slope. Nothing
// when v1 = v2 or either has no entry (flag 0x10 ORed into both); an
// existing constraint of the two (either order) keeps the smaller slope;
// else a new node (v1, v2, slope) at the back.
void __fastcall InsertSlopeConstraint(char* self, void*, uint32_t first, uint32_t second, uint32_t slope)
{
    static bool logged = false;
    FirstCall("SatisfierInsertSlopeConstraint", logged);
    if (first == second)
        return;
    if (!CallThis<uint8_t, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_OrVertexFlags, self, first,
                                                 entry::kInSlope))
        return;
    if (!CallThis<uint8_t, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_OrVertexFlags, self, second,
                                                 entry::kInSlope))
        return;
    char* const head = At<char*>(self, sat::kSlopes);
    for (char* node = At<char*>(head, list::kNext); node != head; node = At<char*>(node, list::kNext)) {
        const uint32_t a = At<uint32_t>(node, list::kValue), b = At<uint32_t>(node, list::kValue + 4);
        if ((a == first && b == second) || (b == first && a == second)) {
            // fcomp; test ah, 5; jp: kept unless the new slope is below.
            if (Float(slope) < At<float>(node, list::kValue + 8))
                At<uint32_t>(node, list::kValue + 8) = slope;
            return;
        }
    }
    char* const node = Allocate(sat::kSlopeNodeSize);
    if (reinterpret_cast<std::uintptr_t>(node) + list::kValue != 0) {
        At<uint32_t>(node, list::kValue) = first;
        At<uint32_t>(node, list::kValue + 4) = second;
        At<uint32_t>(node, list::kValue + 8) = slope;
    }
    LinkBack(head, node);
}

// 0x0065E870 (ret 4: four words; this the list +0x44; InsertCurvature-
// Constraint): a node with the words linked at the back. EAX the node.
char* __fastcall AppendCurvature(char* self, void*, const uint32_t* words)
{
    static bool logged = false;
    FirstCall("SatisfierAppendCurvature", logged);
    char* const head = At<char*>(self, 0);
    char* const node = Allocate(sat::kQuadNodeSize);
    if (reinterpret_cast<std::uintptr_t>(node) + list::kValue != 0)
        for (std::size_t i = 0; i < 4; ++i)
            At<uint32_t>(node, list::kValue + 4 * i) = words[i];
    LinkBack(head, node);
    return node;
}

// 0x00660010 (ret 0x10: v1, v2, v3, k; 0x00636030, 0x00644710):
// |v1 - 2 v2 + v3| <= k. Nothing when two of the vertices are the same or
// one has no entry (flag 0x20 ORed in, in the order v1, v2, v3); an existing
// constraint with the same middle and the same ends (either order) keeps the
// smaller k; else (v1, v2, v3, k) appended (AppendCurvature).
void __fastcall InsertCurvatureConstraint(char* self, void*, uint32_t v1, uint32_t v2, uint32_t v3, uint32_t k)
{
    static bool logged = false;
    FirstCall("SatisfierInsertCurvatureConstraint", logged);
    if (v1 == v2 || v2 == v3 || v3 == v1)
        return;
    for (const uint32_t vertex : {v1, v2, v3})
        if (!CallThis<uint8_t, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_OrVertexFlags, self, vertex,
                                                     entry::kInCurvature))
            return;
    char* const head = At<char*>(self, sat::kCurvatures);
    for (char* node = At<char*>(head, list::kNext); node != head; node = At<char*>(node, list::kNext)) {
        if (At<uint32_t>(node, list::kValue + 4) != v2)
            continue;
        const uint32_t a = At<uint32_t>(node, list::kValue), c = At<uint32_t>(node, list::kValue + 8);
        if ((a == v1 && c == v3) || (a == v3 && c == v1)) {
            if (Float(k) < At<float>(node, list::kValue + 0xC))
                At<uint32_t>(node, list::kValue + 0xC) = k;
            return;
        }
    }
    const uint32_t words[4] = {v1, v2, v3, k};
    CallThis<char*, const uint32_t*>(addr::SC4VertexHeightSatisfier_AppendCurvature, self + sat::kCurvatures, words);
}

// 0x0065EB00 (ret 8: a vertex, where its height goes; 0x00629EB0, slot 73
// 0x006444F0, 0x00644B20, 0x00657790, 0x006588D0): the settled height from
// the results; AL whether there is one.
uint8_t __fastcall GetResult(char* self, void*, uint32_t vertex, uint32_t* out)
{
    static bool logged = false;
    FirstCall("SatisfierGetResult", logged);
    char* const node = Find(self + sat::kResults, vertex);
    if (!node)
        return 0;
    *out = At<uint32_t>(node, table::kValue);
    return 1;
}

// 0x0065EF70 (no arguments; slot 57 0x00644530): the results' iterator set
// to their first node (Begin 0x004B0390).
void __fastcall BeginResults(char* self, void*)
{
    static bool logged = false;
    FirstCall("SatisfierBeginResults", logged);
    Iterator it{};
    CallThis<Iterator*, Iterator*>(addr::SC4HashTable_Begin, self + sat::kResults, &it);
    At<char*>(self, sat::kCursor) = it.node;
    At<char*>(self, sat::kCursor + 4) = it.table;
}

// 0x0065EB50 (ret 8: vertex out, height out; slot 57): AL 0 at the end;
// else the iterator's vertex and height given and the iterator moved on (the
// next node of the chain, else NextBucket 0x0053DCF0).
uint8_t __fastcall NextResult(char* self, void*, uint32_t* vertex, uint32_t* height)
{
    static bool logged = false;
    FirstCall("SatisfierNextResult", logged);
    char* const cursor = self + sat::kCursor;
    if (!At<char*>(cursor, 0))
        return 0;
    *vertex = At<uint32_t>(At<char*>(cursor, 0), table::kKey);
    *height = At<uint32_t>(At<char*>(cursor, 0), table::kValue);
    char* next = At<char*>(At<char*>(cursor, 0), table::kNext);
    if (!next)
        next = CallThis<char*>(addr::SC4HashTable_NextBucket, cursor);
    At<char*>(cursor, 0) = next;
    return 1;
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kSatisfier[] = {
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_Construct, Construct),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_Destruct, Destruct),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_DeletingDestructor, DeletingDestructor),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_Begin, Begin),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_Reset, Reset),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_Clear, Clear),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_AddVertex, AddVertex),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_RestrictVertex, RestrictVertex),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_AddFixedVertex, AddFixedVertex),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_OrVertexFlags, OrVertexFlags),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_SetPreferredHeight, SetPreferredHeight),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_SetPreferredRange, SetPreferredRange),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_FindGroup, FindGroup),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_MergeGroups, MergeGroups),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_DeleteGroup, DeleteGroup),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_InsertEqualityConstraint, InsertEqualityConstraint),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_InsertSlopeConstraint, InsertSlopeConstraint),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_AppendCurvature, AppendCurvature),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_InsertCurvatureConstraint, InsertCurvatureConstraint),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_GetResult, GetResult),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_BeginResults, BeginResults),
    OPENSC4_DETOUR(addr::SC4VertexHeightSatisfier_NextResult, NextResult),
};

#undef OPENSC4_DETOUR

bool InstallSatisfier(registry::Mode) { return InstallDetours(kSatisfier, "satisfier"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.satisfier", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::SC4VertexHeightSatisfier_Construct,
                        "Our network tool's vertex height satisfier but its solving: its constructor, destructors, "
                        "Begin, Reset and Clear, the vertices, groups and constraints the tool puts in (NAM's "
                        "InsertSlopeConstraint among them) and the results it reads back (22 detours; "
                        "src/core/nettool_satisfier_1.cpp)",
                        false, InstallSatisfier}),
};

} // namespace
} // namespace opensc4::nettool_sim::satisfier
