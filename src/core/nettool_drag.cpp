// Our cSC4NetworkTool, part t5 (build 641; the class map and the fields are
// in src/core/nettool_sim.h): the drag. A player's drag goes through slot 18
// DrawNetworkLine 0x0063AF40 (NAM-guarded, the game's), which classifies the
// drag (0x00637EB0, NAM-guarded, the game's: the step count +0x70, the
// regular step count +0x74, the directions +0x7C / +0x80 and two words) and
// then calls BuildDragPath 0x00639790 (ours) to lay the steps (+0x54) and
// the dragged cells (+0x60). The segment placer 0x0063B830 (part t6) then
// asks slots 66 to 69 whether the drag may start, is not reserved and is on
// good terrain, and for a one-way road runs OneWayRoadPass 0x0063A640. Two
// groups, each a list of detours at the game's addresses:
//   simulators.nettool.drag: BuildDragPath 0x00639790, OneWayRoadPass
//     0x0063A640, the 12-byte vector's InsertN 0x00638D40 and Insert
//     0x00639070, the hash set's InsertRange 0x00638FA0 and the hash
//     iterator distance 0x00638AE0 (6 detours);
//   simulators.nettool.dragchecks: slots 65 TestForAutoConnectOccupant
//     0x00638680, 66 IdentifyStartingConditions 0x00638710, 67
//     CheckForReservedTilesAt 0x006392F0, 68 CheckForReservedTiles
//     0x00639170, 69 CheckForGoodTerrain 0x00638990, and the auto-connect
//     filter those make: IsOccupantIncluded 0x006385D0 (slot 3), the
//     placed network occupant test 0x00638600, IsPropertyHolderIncluded
//     0x00637EA0 (slot 5, a naked thunk) and the deleting destructor
//     0x00638650 (slot 6) (9 detours).
// The split: the path's building and its template code, and the checks the
// tool family shares through its vtables (slots 66 and 67 are also the
// underground, pipe and subway tools'; 65, 68 and 69 the power line tool's)
// with the filter they build; each group can be tried and bisected alone
// (OPENSC4_NETTOOL_SKIP too).
//
// What these are (from the code; the survey had slots 67 and 68 swapped):
// slot 67 0x006392F0 takes a cell and a type and says whether an occupant
// there reserves it (one of type 0x6A11BD2B, or an immovable network
// occupant that is not a network lot or not of the type); slot 68 0x00639170
// takes the step count by address, the type and a skip byte and runs slot
// 67 on every dragged cell of the steps not of kind 3 (the count becomes
// the first step with a reserved cell) and on the rule cell records (+0xF0;
// the count 0). Slot 66 0x00638710 and slot 69 0x00638990 test water (the
// tool's slot 63 IsStoppedByWater, the terrain's slot 0x88, the cell info's
// network lot byte +0x56). Slot 65 0x00638680 asks the occupant manager for
// the first occupant of a cell through an auto-connect filter (0x10 bytes:
// the filter base 0x005EA080, vtables 0x00AA8BBC / 0x00AA8BA4, the tool's
// filter +0x364 at +0xC): one the tool's filter includes that is a placed
// network occupant.
//
// BuildDragPath (the drag's line from start to end): one point at a time
// toward the end, the axis with the larger distance first (ties alternate:
// x after a z move, z after an x move); with the classifier's x word set an x move continues the step
// (and with its z word a z move), else the step ends; per step the range of
// the coordinate the global byte 0x00B464F0 picks (x when the x word is
// set). Wide pens (the row's +0x1C / +0x20 above 1) widen the ranges by the
// pen less 1 toward the side the direction +0x80 picks; the steps vector is
// sized to the step count (InsertN with zeros); each step becomes a run of
// dragged cells. Wide pens then look at the networks at the line's two ends
// (the occupant manager's slot 0x80 through an auto-connect filter, the
// network occupant's edge word slot 0x98) and may shift the whole line, the
// caller's start and end points included, onto an existing network's
// alignment. Pen-1 types with more than two steps (or two regular ones)
// extend the line at each end by one step when the tool auto-connects (slot
// 64, false for the general tool) and slot 65 finds an occupant there.
// OneWayRoadPass (type 10 only, the segment placer's): the cells of the
// first and the last step flood their one-way bits (cell info +0x50, two
// bits per side) into the placement's cells (+0x10) along the network's
// edges, adding each cell it marks (+0x57) to the cells to solve (+0x84),
// then resolves conflicting directions cell by cell over that set until no
// cell changes.
//
// Every function was written from the disassembly. Calls between part
// functions go to the game's addresses (addr::...), so each detour stands
// alone; slots are called through the vtable as the game does.
//
// Left as the game's: DrawNetworkLine 0x0063AF40 and the classifier
// 0x00637EB0 (NAM's diagonal streets patch makes the JE at 0x0063AFF2 and
// at 0x00637F80, each a test of the tool's +0x2B5, a short JMP). Nothing of
// this part draws: DrawNetworkLine only lays steps and cells (the preview
// drawing is part t4's and the Renderer thread's). Called at their
// addresses: the cell entry 0x00633220 and the world cache's GetCell
// 0x00647A20 (t2), the 12-byte vector's InsertOverflow 0x00626240,
// CopyBackward 0x00614430, UninitializedCopy 0x00623FA0, UninitializedFill
// 0x00623FE0 and Fill 0x00637E60, the point vector's Resize 0x0066D1E0 and
// Insert 0x00665DB0, the hash set's Resize 0x004F0940, Insert 0x00624DD0,
// Assign 0x0050DA70 and Clear 0x008EBA60, the bucket vector's Reserve
// 0x0045D4F0 and FillInsert 0x004FBA20, the list's Clear2 0x00413E60 and the
// list node allocator (game_access.h, as the game inlines it), the filter
// base's constructor 0x005EA080 and destructor 0x005E95C0, operator new and
// delete, SC4::stl_allocate and SC4::Free, the tunnel occupant's +0x1D0
// getter 0x00647570.
//
// NAM. The guarded DrawNetworkLine calls BuildDragPath (thiscall, `ret
// 0x14`, kept) and, on both sides of its patched JE, reads after it only
// callee-saved registers (EDI the row, ESI the tool, EBP +0x70) and memory:
// EAX, ECX and EDX are written before any read. The guarded 0x0062CD50
// calls slot 68 through the vtable and reads only the count it passed by
// address. NAM's DLL calls nothing of this part. NAM reads the tool by
// layout (+0x1C, +0x54, +0x60, +0x98, +0xCC, +0x124, +0x188, +0x240, +0x244,
// +0x2B5): this part writes +0x54, +0x60, +0x70, +0x74 and the cell infos'
// bytes +0x10, +0x50 and +0x57 in place, in the game's layout, with the
// game's allocator; nothing is cached across a call.
//
// Registers. Every direct game caller was scanned after the call (scratch
// regscan.py / eaxscan.py, both branches): none reads ECX or EDX before
// writing it (the one flag, 0x0063A3EB after Insert, is a `sar edx` of an
// `imul` result). EAX is read after 0x00638600 (AL) and the distance 0x00638AE0
// (all of it); ours give those. The slots and the filter's slots are called
// only through vtables (by the segment placer, the guarded 0x0062CD50, the
// network manager and the occupant manager's searches), whose callers
// cannot rely on a register a virtual callee leaves but EAX; the answers are
// AL (slots 65 to 69, as the game's: the game's EAX above AL is what its
// last call left) and EAX 0 or 1 (filter slot 3). IsPropertyHolderIncluded
// 0x00637EA0 is the game's tail jump to the inner filter's slot 5 (`mov
// ecx, [ecx + 0xC]; mov eax, [ecx]; jmp [eax + 0x14]`): a naked thunk keeps
// it (ECX, EDX and the stack as the game's; EAX the inner vtable); only the
// flags differ (its first-call note runs before; no callee reads flags).
//
// Wrappers: no file of src/core/game_*.cpp or *fix*.cpp detours a function
// of this part or tests a return address in 0x00637EA0 to 0x0063B130
// (game_quiet.cpp calls DrawNetworkLine through the interface in its own
// OPENSC4_TEST_QUIET test, which then runs ours under it); sim_timers.cpp's
// crew range 0x00605490 to 0x00608C50 is elsewhere and nothing here reads a
// stopwatch; fixed_seed.cpp: nothing here makes or seeds a generator. So
// nothing needed a mark.
//
// Floats: none. Every value of the part is an integer (cells, step counts,
// edge words, bytes).
//
// Stack leftovers: the original reads none. BuildDragPath's two occupant
// locals are cleared before use, its range vector is made by Resize, its
// zero record and point locals are written whole; OneWayRoadPass's hash sets
// on its stack are made with zeroed bucket words and counts, and their +0x0
// word (the hash functor, never written by the game) is never read by
// Assign, Insert, Resize or Clear (ours is 0); the tunnel end's cell (slot
// 0xEC's two outputs) is read only when the tunnel test wrote it. Unread
// tags the game passes as the address of an argument slot are our locals'
// addresses. The game reuses its argument slots for locals (BuildDragPath:
// the classifier words, the start and end pointers and the type after their
// last read; slot 67: x and the list's head); ours use locals.
//
// Calls, each count from the asm's pushes for that call and the callee's
// own `ret N` (slots as byte offsets):
//   - the part's own, at the game's addresses: InsertN three (ret 0xC),
//     Insert two (ret 8), InsertRange five (ret 0x14), the distance cdecl
//     three pushed (two read), the placed test cdecl one;
//   - the game's: the cell entry one (ret 4), GetCell one (ret 4),
//     InsertOverflow five (ret 0x14), CopyBackward cdecl five, the uninitialized
//     copy cdecl four, the uninitialized fill and Fill cdecl three, the point
//     vector's Resize one (ret 4) and Insert two (ret 8), the hash set's
//     Resize one (ret 4), Insert two (ret 8), Assign one (ret 4) and Clear
//     none, Reserve one (ret 4), FillInsert three (ret 0xC), Clear2 none, the
//     filter base's constructor and destructor none, operator new and delete,
//     SC4::stl_allocate and SC4::Free cdecl one, 0x00647570 none;
//   - the tool's slots 0xFC (63) none, 0x100 (64) none, 0x104 (65) two,
//     0x10C (67) three, all AL;
//   - the occupant manager (+0x48): 0x80 four (out, x, z, filter; AL), 0xA0
//     five (the list, two cells, a filter, -1; AL); the terrain (+0x44):
//     0x88 two (AL); an occupant: 0x0 QueryInterface two, 0x8 Release, 0x1C
//     GetType, 0x20 HasFlag one; a network occupant: 0x44 IsImmovable, 0x54
//     IsPlaced, 0x58 IsOfType one, 0x98 the edge word one, 0xEC the occupied
//     cell two; a filter: 0x4 AddRef, 0x8 Release, 0xC IsOccupantIncluded
//     one; the list node allocator: 0xC one, 0x10 one.
#include "core/nettool_occupants.h"
#include "core/nettool_sim.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim {
namespace {

using occupants::Entry;
using occupants::ListNode;

// ====================================================================================
// Layouts
// ====================================================================================

// The tool's fields this part reads beyond nettool_sim.h's.
namespace dfield {
inline constexpr std::size_t kRegularSteps = 0x74; // the regular step count (the classifier's)
inline constexpr std::size_t kDirection = 0x80;    // the drag's direction (0..7, the classifier's)
inline constexpr std::size_t kRuleCellsEnd = 0xF4;
} // namespace dfield

// A step (12 bytes): the first and last dragged cell, the kind (0 regular).
namespace dstep {
inline constexpr std::size_t kSize = 0xC, kFirst = 0x0, kLast = 0x4, kKind = 0x8;
} // namespace dstep

// A rule cell record (+0xF0, 0x14 bytes): x, z, an edge word, a type.
namespace drule {
inline constexpr std::size_t kSize = 0x14, kX = 0x0, kZ = 0x4, kType = 0xC;
} // namespace drule

// The auto-connect filter (0x10 bytes): the filter base's words, the inner
// filter at +0xC.
namespace afilter {
inline constexpr uint32_t kSize = 0x10;
inline constexpr std::size_t kSecond = 0x4, kInner = 0xC;
} // namespace afilter

// A hash set (`rul::table`): the bucket vector and the count; its node.
namespace hset = rul::table;

// Slots.
namespace dslot {
inline constexpr std::size_t kIsStoppedByWater = 0xFC, kShouldAutoConnect = 0x100;
inline constexpr std::size_t kTestForAutoConnect = 0x104, kCheckReservedAt = 0x10C; // the tool's
inline constexpr std::size_t kFirstByCell = 0x80, kByCityCells = 0xA0;               // the occupant manager's
inline constexpr std::size_t kUnderWater = 0x88;                                       // the terrain's
inline constexpr std::size_t kQueryInterface = 0x0, kGetType = 0x1C, kHasFlag = 0x20;  // an occupant's
inline constexpr std::size_t kIsImmovable = 0x44, kIsPlaced = 0x54, kIsOfType = 0x58;  // a network occupant's
inline constexpr std::size_t kEdges = 0x98, kOccupiedCell = 0xEC;
inline constexpr std::size_t kIsOccupantIncluded = 0xC; // a filter's (slot 5 at 0x14: the thunk's jump)
inline constexpr std::size_t kAllocate = 0xC, kDeallocate = 0x10;                      // the list node allocator's
} // namespace dslot

// The occupant types slot 67 tells apart, the flags and interfaces.
inline constexpr uint32_t kTypeIgnored = 0x278128A0, kTypeReserved = 0x6A11BD2B, kTypeNetwork = 0xC772BF98;
inline constexpr uint32_t kFlagNetworkLot = 0x200000, kFlagTunnel = 0x80000;
inline constexpr uint32_t kIidTunnel = 0x8A4BD52B;
// BuildDragPath's empty range and its edge masks.
inline constexpr int32_t kRangeLow = 100000, kRangeHigh = -100000;
inline constexpr uint32_t kEdgesStraight = 0x4000400, kEdgesAny = 0x4040404, kEdgeBit17 = 0x20000;
inline constexpr uint32_t kPrimeWanted = 100, kPrimeCount = 0x1C, kNoPrime = 0xFFFFFFFBu;

// ====================================================================================
// Helpers
// ====================================================================================

inline Vector& VectorAt(char* self, std::size_t offset) { return At<Vector>(self, offset); }

// Element addresses with the game's 32-bit index arithmetic.
inline char* Offset(char* base, uint32_t index, uint32_t size)
{
    return reinterpret_cast<char*>(static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(base)) + index * size);
}
inline char* StepAt(char* self, uint32_t s) { return Offset(VectorAt(self, field::kSteps).begin, s, dstep::kSize); }
inline char* CellAt(char* self, uint32_t c) { return Offset(VectorAt(self, field::kCells).begin, c, kPointSize); }
inline uint32_t& Word(char* base, std::size_t offset) { return At<uint32_t>(base, offset); }
inline int32_t& SWord(char* base, std::size_t offset) { return At<int32_t>(base, offset); }

// The signed count of 12-byte records (the game's divide by 12).
inline int32_t Count12(const Vector& v) { return static_cast<int32_t>(v.end - v.begin) / 12; }

inline int32_t CdqAbsValue(int32_t v)
{
    const uint32_t sign = static_cast<uint32_t>(v >> 31);
    return static_cast<int32_t>((static_cast<uint32_t>(v) ^ sign) - sign);
}

inline char* Allocate(uint32_t bytes) { return CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, bytes); }
inline void Free(void* block) { CallCdecl<void, void*>(addr::SC4_Free, block); }

inline uint8_t UnderWater(char* self, uint32_t x, uint32_t z)
{
    return Virtual<uint8_t>(At<void*>(self, field::kTerrain), dslot::kUnderWater, x, z);
}
inline uint8_t FirstByCell(char* self, void** out, uint32_t x, uint32_t z, void* filter)
{
    return Virtual<uint8_t>(At<void*>(self, field::kOccupants), dslot::kFirstByCell, out, x, z, filter);
}
// Released and cleared, as the game's `if (p) { p = 0; p->Release(); }`.
inline void Drop(void*& object)
{
    if (void* const held = object) {
        object = nullptr;
        ReleaseOf(held);
    }
}
inline uint8_t AsNetworkOccupant(void* occupant, void** out)
{
    return Virtual<uint8_t>(occupant, dslot::kQueryInterface, kIidNetworkOccupant, out);
}

// The auto-connect filter as the game inlines its making (0x00638680,
// 0x00639D74, 0x0063A1C9): operator new 0x10 (null on failure), the base's
// constructor, the two vtables, the tool's filter at +0xC referenced, the
// filter itself referenced.
char* MakeAutoConnectFilter(char* self)
{
    char* const filter = CallCdecl<char*, uint32_t>(addr::SC4_operator_new, afilter::kSize);
    if (!filter)
        return nullptr;
    void* const inner = At<void*>(self, field::kFilter);
    CallThis<void*>(addr::SC4_OccupantFilterBase, filter);
    At<std::uintptr_t>(filter, 0) = addr::SC4AutoConnectFilter_vftable;
    At<std::uintptr_t>(filter, afilter::kSecond) = addr::SC4AutoConnectFilter_vftable2;
    At<void*>(filter, afilter::kInner) = inner;
    if (inner)
        AddRefOf(inner);
    AddRefOf(filter);
    return filter;
}

// A point appended to the dragged cells as BuildDragPath inlines push_back:
// in place (nothing written at a null end), else size + max(size, 1) points
// (unsigned), the old points copied one by one (skipped at a null
// destination), the new one, the old memory freed.
void PushPoint(char* self, uint32_t x, uint32_t z)
{
    Vector& v = VectorAt(self, field::kCells);
    char* const end = v.end;
    if (end != v.capacity) {
        if (end) {
            Word(end, 0) = x;
            Word(end, 4) = z;
        }
        v.end = end + kPointSize;
        return;
    }
    const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(end - v.begin) >> 3);
    const uint32_t grown = (size < 1 ? 1u : size) + size;
    char* const memory = grown != 0 ? Allocate(grown * 8) : nullptr;
    char* at = memory;
    for (char* from = v.begin; from != end; from += 8, at += 8)
        if (at) {
            Word(at, 0) = Word(from, 0);
            Word(at, 4) = Word(from, 4);
        }
    if (at) {
        Word(at, 0) = x;
        Word(at, 4) = z;
    }
    if (v.begin)
        Free(v.begin);
    v.capacity = memory + grown * 8;
    v.begin = memory;
    v.end = at + 8;
}

// A 12-byte record appended to a local vector as OneWayRoadPass inlines
// push_back (the same growth, copies of three words).
void PushRecord(Vector& v, uint32_t a, uint32_t b, uint32_t c)
{
    char* const end = v.end;
    if (end != v.capacity) {
        if (end) {
            Word(end, 0) = a;
            Word(end, 4) = b;
            Word(end, 8) = c;
        }
        v.end = end + 12;
        return;
    }
    const uint32_t size = static_cast<uint32_t>(Count12(v));
    const uint32_t grown = (size < 1 ? 1u : size) + size;
    char* const memory = grown != 0 ? Allocate(grown * 12) : nullptr;
    char* at = memory;
    for (char* from = v.begin; from != end; from += 12, at += 12)
        if (at) {
            Word(at, 0) = Word(from, 0);
            Word(at, 4) = Word(from, 4);
            Word(at, 8) = Word(from, 8);
        }
    if (at) {
        Word(at, 0) = a;
        Word(at, 4) = b;
        Word(at, 8) = c;
    }
    if (v.begin)
        Free(v.begin);
    v.begin = memory;
    v.capacity = memory + grown * 12;
    v.end = at + 12;
}

// The hash set's inlined walks (STLport: the bucket by key % count, the
// count from the bucket vector's length, signed, the division unsigned).
inline uint32_t BucketCount(char* set)
{
    const Vector& buckets = At<Vector>(set, hset::kBuckets);
    return static_cast<uint32_t>(static_cast<int32_t>(buckets.end - buckets.begin) >> 2);
}
inline char* FirstNode(char* set)
{
    const uint32_t count = BucketCount(set);
    char** const buckets = reinterpret_cast<char**>(At<Vector>(set, hset::kBuckets).begin);
    for (uint32_t i = 0; i < count; ++i)
        if (buckets[i])
            return buckets[i];
    return nullptr;
}
inline char* NextNode(char* node, char* set)
{
    if (char* const next = At<char*>(node, hset::kNext))
        return next;
    const uint32_t count = BucketCount(set);
    uint32_t index = At<uint32_t>(node, hset::kKey) % count;
    char** const buckets = reinterpret_cast<char**>(At<Vector>(set, hset::kBuckets).begin);
    while (++index < count)
        if (buckets[index])
            return buckets[index];
    return nullptr;
}
// The set grown for one more and the key inserted (the result is the
// insert's 9-byte pair: a 12-byte buffer).
inline void InsertKey(char* set, const uint32_t* key)
{
    CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Resize, set, At<uint32_t>(set, hset::kCount) + 1);
    uint32_t result[3] = {0, 0, 0};
    CallThis<void*, uint32_t*, const uint32_t*>(addr::SC4Uint32HashSet_Insert, set, result, key);
}

// A new empty list of the game's (the head from the list node allocator).
inline ListNode* NewList()
{
    ListNode* const head =
        Virtual<ListNode*>(ListNodeAllocator(), dslot::kAllocate, static_cast<uint32_t>(sizeof(ListNode)));
    head->next = head;
    head->previous = head;
    return head;
}

// ====================================================================================
// The 12-byte vector and the hash set (template code only this part calls)
// ====================================================================================

// 0x00638D40 (ret 0xC: where, a count, the value's address; this a vector
// of 12-byte records; called by the 12-byte vector's resize 0x00637720 and
// BuildDragPath): nothing for 0; InsertOverflow (where, the value, an unread
// tag, the count, 0) when the room left is less than the count; else the
// value copied first, and either the last `count` records moved to the end
// (uninitialized copy), the rest moved back (copy backward) and the hole
// filled, or the missing copies made at the end (uninitialized fill), the
// records after `where` moved past them and the old part filled.
void __fastcall Vector12InsertN(char* self, void*, char* where, uint32_t count, const uint32_t* value)
{
    static bool logged = false;
    FirstCall("Vector12InsertN", logged);
    if (count == 0)
        return;
    Vector& v = At<Vector>(self, 0);
    uint32_t tag = 0; // the game passes its third argument's slot: never read
    const uint32_t room = static_cast<uint32_t>(static_cast<int32_t>(v.capacity - v.end) / 12);
    if (room < count) {
        CallThis<void, char*, const uint32_t*, void*, uint32_t, uint32_t>(addr::SC4Vector12_InsertOverflow, self, where,
                                                                          value, &tag, count, 0u);
        return;
    }
    const uint32_t copy[3] = {value[0], value[1], value[2]};
    char* const end = v.end;
    const uint32_t after = static_cast<uint32_t>(static_cast<int32_t>(end - where) / 12);
    if (after > count) {
        const uint32_t bytes = count * 12;
        CallCdecl<char*, char*, char*, char*, void*>(addr::SC4Vector12_UninitializedCopy, end - bytes, end, end, &tag);
        v.end = end + bytes;
        CallCdecl<char*, char*, char*, char*, void*, uint32_t>(addr::SC4Vector12_CopyBackward, where, end - bytes, end,
                                                               &tag, 0u);
        CallCdecl<char*, char*, char*, const uint32_t*>(addr::SC4Vector12_Fill, where, where + bytes, copy);
        return;
    }
    CallCdecl<char*, char*, uint32_t, const uint32_t*>(addr::SC4Vector12_UninitializedFill, end, count - after, copy);
    char* const moved = v.end + (count - after) * 12;
    v.end = moved;
    CallCdecl<char*, char*, char*, char*, void*>(addr::SC4Vector12_UninitializedCopy, where, end, moved, &tag);
    v.end = v.end + after * 12;
    CallCdecl<char*, char*, char*, const uint32_t*>(addr::SC4Vector12_Fill, where, end, copy);
}

// 0x00639070 (ret 8: where, the value's address; this a vector of 12-byte
// records; called by BuildDragPath): when full, InsertOverflow (where, the
// value, an unread tag, 1, 0); at the end, the value copied in place (not at
// a null end); else the last record copied one further, the rest moved back
// (copy backward) and the value (read before the move) stored at `where`.
// EAX the inserted record (the begin, re-read, plus the old index).
char* __fastcall Vector12Insert(char* self, void*, char* where, const uint32_t* value)
{
    static bool logged = false;
    FirstCall("Vector12Insert", logged);
    Vector& v = At<Vector>(self, 0);
    const int32_t index = static_cast<int32_t>(where - v.begin) / 12;
    char* const end = v.end;
    uint32_t tag = 0;
    if (end == v.capacity) {
        CallThis<void, char*, const uint32_t*, void*, uint32_t, uint32_t>(addr::SC4Vector12_InsertOverflow, self, where,
                                                                          value, &tag, 1u, 0u);
    } else if (where == end) {
        if (end) {
            Word(end, 0) = value[0];
            Word(end, 4) = value[1];
            Word(end, 8) = value[2];
        }
        v.end = end + 12;
    } else {
        if (end) {
            Word(end, 0) = Word(end - 12, 0);
            Word(end, 4) = Word(end - 12, 4);
            Word(end, 8) = Word(end - 12, 8);
        }
        v.end = end + 12;
        const uint32_t a = value[0], b = value[1], c = value[2];
        CallCdecl<char*, char*, char*, char*, void*, uint32_t>(addr::SC4Vector12_CopyBackward, where, end - 12, end, &tag,
                                                               0u);
        Word(where, 0) = a;
        Word(where, 4) = b;
        Word(where, 8) = c;
    }
    return Offset(v.begin, static_cast<uint32_t>(index), 12);
}

// 0x00638AE0 (cdecl: the first and the last iterator's addresses, each a
// node and its hash set; a third pushed, not read; called by InsertRange):
// the increments from first to last (the next node, else the next bucket's
// head of the first iterator's set). EAX the count.
uint32_t __cdecl HashDistance(const std::uintptr_t* first, const std::uintptr_t* last)
{
    static bool logged = false;
    FirstCall("HashDistance", logged);
    char* node = reinterpret_cast<char*>(first[0]);
    char* const set = reinterpret_cast<char*>(first[1]);
    char* const stop = reinterpret_cast<char*>(last[0]);
    uint32_t count = 0;
    while (node != stop) {
        node = NextNode(node, set);
        ++count;
    }
    return count;
}

// 0x00638FA0 (ret 0x14: the first iterator's node and set, the last
// iterator's node and set, an unread tag; this a hash set of uint32 keys;
// called by OneWayRoadPass): the distance (0x00638AE0), the set grown for
// its count plus that many, then each key of the range inserted, the
// iterator moved through the first iterator's set.
void __fastcall HashSetInsertRange(char* self, void*, char* firstNode, char* firstSet, char* lastNode, char* lastSet,
                                   void*)
{
    static bool logged = false;
    FirstCall("HashSetInsertRange", logged);
    std::uintptr_t first[2] = {reinterpret_cast<std::uintptr_t>(firstNode), reinterpret_cast<std::uintptr_t>(firstSet)};
    std::uintptr_t last[2] = {reinterpret_cast<std::uintptr_t>(lastNode), reinterpret_cast<std::uintptr_t>(lastSet)};
    const uint32_t count = CallCdecl<uint32_t, std::uintptr_t*, std::uintptr_t*, std::uintptr_t*>(
        addr::SC4HashIterator_Distance, first, last, first);
    CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Resize, self, At<uint32_t>(self, hset::kCount) + count);
    if (count == 0)
        return;
    char* node = firstNode;
    for (uint32_t left = count; left != 0; --left) {
        uint32_t result[3] = {0, 0, 0};
        CallThis<void*, uint32_t*, const uint32_t*>(addr::SC4Uint32HashSet_Insert, self, result,
                                                    &At<uint32_t>(node, hset::kKey));
        node = NextNode(node, firstSet);
    }
}

// ====================================================================================
// The auto-connect filter
// ====================================================================================

// 0x00638600 (cdecl: an occupant; called by the filter's slot 3): a null
// occupant is not; else asked for the network occupant interface, its slot
// 0x54 (IsPlaced); the interface released. AL.
uint8_t __cdecl IsPlacedNetworkOccupant(void* occupant)
{
    static bool logged = false;
    FirstCall("IsPlacedNetworkOccupant", logged);
    void* network = nullptr;
    uint8_t placed = 0;
    if (occupant && AsNetworkOccupant(occupant, &network) && Virtual<uint8_t>(network, dslot::kIsPlaced))
        placed = 1;
    if (network)
        ReleaseOf(network);
    return placed;
}

// 0x006385D0 (the filter's slot 3, ret 4: an occupant): the inner filter
// (+0xC) includes it and 0x00638600 says it is placed. EAX 1 or 0.
uint32_t __fastcall AutoConnectIsOccupantIncluded(char* self, void*, void* occupant)
{
    static bool logged = false;
    FirstCall("AutoConnectIsOccupantIncluded", logged);
    if (!Virtual<uint8_t>(At<void*>(self, afilter::kInner), dslot::kIsOccupantIncluded, occupant))
        return 0;
    if (!CallCdecl<uint8_t, void*>(addr::SC4AutoConnectFilter_IsPlacedNetworkOccupant, occupant))
        return 0;
    return 1;
}

// 0x00637EA0 (the filter's slot 5): a tail jump to the inner filter's slot 5
// with its own arguments. A naked thunk: the first-call note, then the
// game's three instructions.
void __cdecl NotePropertyHolderIncluded()
{
    static bool logged = false;
    FirstCall("AutoConnectIsPropertyHolderIncluded", logged);
}

__declspec(naked) void AutoConnectIsPropertyHolderIncludedThunk()
{
    __asm {
        push ecx
        push edx
        call NotePropertyHolderIncluded
        pop edx
        pop ecx
        mov ecx, [ecx + 0xC]
        mov eax, [ecx]
        jmp dword ptr [eax + 0x14]
    }
}

// 0x00638650 (the filter's slot 6, ret 4: flags): the inner filter released
// (the word kept), the base's destructor 0x005E95C0, operator delete when
// bit 0 is set. EAX this.
char* __fastcall AutoConnectDeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("AutoConnectDeletingDestructor", logged);
    if (void* const inner = At<void*>(self, afilter::kInner))
        ReleaseOf(inner);
    CallThis<void>(addr::SC4OccupantFilterBase_Destroy, self);
    if (flags & 1)
        CallCdecl<void, void*>(addr::SC4_operator_delete, self);
    return self;
}

// ====================================================================================
// Slots 65 to 69
// ====================================================================================

// 0x00638680 (slot 65, ret 8: x, z): an auto-connect filter (none when the
// allocation fails: the search then gets a null filter, as the game's), the
// occupant manager's first occupant of the cell through it (slot 0x80), the
// occupant and the filter released. AL the search's answer.
uint8_t __fastcall TestForAutoConnectOccupant(char* self, void*, uint32_t x, uint32_t z)
{
    static bool logged = false;
    FirstCall("TestForAutoConnectOccupant", logged);
    char* const filter = MakeAutoConnectFilter(self);
    void* found = nullptr;
    const uint8_t answer = FirstByCell(self, &found, x, z, filter);
    if (found)
        ReleaseOf(found);
    if (filter)
        ReleaseOf(filter);
    return answer;
}

// 0x00638710 (slot 66, ret 4: the step count out): when the tool stops at
// water (slot 63) and the first dragged cell is under water (terrain 0x88)
// and its cell info (0x00633220) is missing or not a network lot (+0x56):
// the count 0, AL 0. Else AL 1.
uint8_t __fastcall IdentifyStartingConditions(char* self, void*, int32_t* count)
{
    static bool logged = false;
    FirstCall("IdentifyStartingConditions", logged);
    if (!Virtual<uint8_t>(self, dslot::kIsStoppedByWater))
        return 1;
    char* const first = VectorAt(self, field::kCells).begin;
    const uint32_t x = Word(first, 0), z = Word(first, 4);
    if (!UnderWater(self, x, z))
        return 1;
    char* const info = Entry(self, (z << 16) + x);
    if (info && At<uint8_t>(info, cell::kNetworkLot))
        return 1;
    *count = 0;
    return 0;
}

// 0x00638990 (slot 69, ret 4: the step count out): when the tool stops at
// water: every regular step below the count (re-read), each of its cells
// (the last re-read): under water and its cell info missing or not a network
// lot: the count becomes the step, AL 0. Then every rule cell record (the
// end read once): not under water, or under water with a network lot cell
// info: the count 0, AL 0. Else AL 1.
uint8_t __fastcall CheckForGoodTerrain(char* self, void*, int32_t* count)
{
    static bool logged = false;
    FirstCall("CheckForGoodTerrain", logged);
    if (!Virtual<uint8_t>(self, dslot::kIsStoppedByWater))
        return 1;
    if (*count > 0) {
        int32_t s = 0;
        do {
            char* const step = StepAt(self, static_cast<uint32_t>(s));
            if (Word(step, dstep::kKind) == 0) {
                uint32_t c = Word(step, dstep::kFirst);
                if (c <= Word(step, dstep::kLast)) {
                    do {
                        char* const point = CellAt(self, c);
                        const uint32_t x = Word(point, 0), z = Word(point, 4);
                        if (UnderWater(self, x, z)) {
                            char* const info = Entry(self, (z << 16) + x);
                            if (!info || !At<uint8_t>(info, cell::kNetworkLot)) {
                                *count = s;
                                return 0;
                            }
                        }
                        ++c;
                    } while (c <= Word(StepAt(self, static_cast<uint32_t>(s)), dstep::kLast));
                }
            }
            ++s;
        } while (s < *count);
    }
    char* const last = At<char*>(self, dfield::kRuleCellsEnd);
    for (char* record = At<char*>(self, field::kRuleCellRecords); record != last; record += drule::kSize) {
        const uint32_t x = Word(record, drule::kX), z = Word(record, drule::kZ);
        if (!UnderWater(self, x, z)) {
            *count = 0;
            return 0;
        }
        char* const info = Entry(self, (z << 16) + x);
        if (info && At<uint8_t>(info, cell::kNetworkLot)) {
            *count = 0;
            return 0;
        }
    }
    return 1;
}

// 0x006392F0 (slot 67, ret 0xC: x, z, a network type): a new list, the
// cell's occupants (occupant manager slot 0xA0: the list, the cell twice, no
// filter, -1); for each: type 0x278128A0 nothing, 0x6A11BD2B reserves, a
// network occupant (0xC772BF98) reserves when it is immovable (0x44) and not
// a network lot (0x20 with 0x200000) or not of the type (0x58); each
// released. The list cleared (Clear2) and its head given back. AL.
uint8_t __fastcall CheckForReservedTilesAt(char* self, void*, uint32_t x, uint32_t z, uint32_t type)
{
    static bool logged = false;
    FirstCall("CheckForReservedTilesAt", logged);
    ListNode* list = NewList();
    uint32_t cellAt[2] = {x, z};
    uint8_t reserved = 0;
    Virtual<uint8_t>(At<void*>(self, field::kOccupants), dslot::kByCityCells, &list, cellAt, cellAt, 0u, 0xFFFFFFFFu);
    ListNode* const head = list;
    for (ListNode* node = head->next; node != head; node = node->next) {
        void* const occupant = node->value;
        const uint32_t kind = Virtual<uint32_t>(occupant, dslot::kGetType);
        if (kind == kTypeIgnored) {
        } else if (kind == kTypeReserved) {
            reserved = 1;
        } else if (kind == kTypeNetwork) {
            void* network = nullptr;
            if (AsNetworkOccupant(occupant, &network) && Virtual<uint8_t>(network, dslot::kIsImmovable)) {
                if (!Virtual<uint8_t>(network, dslot::kHasFlag, kFlagNetworkLot) ||
                    !Virtual<uint8_t>(network, dslot::kIsOfType, type))
                    reserved = 1;
            }
            if (network)
                ReleaseOf(network);
        }
        ReleaseOf(occupant);
    }
    CallThis<void>(addr::SC4List_Clear2, &list);
    ListNode* const sentinel = list;
    Virtual<void>(ListNodeAllocator(), dslot::kDeallocate, static_cast<void*>(sentinel));
    return reserved;
}

// 0x00639170 (slot 68, ret 0xC: the step count out, a network type, a skip
// byte): a new list (never used: made, cleared and given back, as the
// game's). Unless skipped: every step below the count (re-read) not of kind
// 3, each of its cells (the last re-read): slot 67 (x, z, type); a reserved
// cell makes the count the step and ends it. Then (when no step ended it)
// every rule cell record (the end read once): slot 67 (x, z, its type); a
// reserved one makes the count 0. The game's EAX is what the allocator's
// slot 0x10 leaves (no caller reads it).
void __fastcall CheckForReservedTiles(char* self, void*, int32_t* count, uint32_t type, uint32_t skip)
{
    static bool logged = false;
    FirstCall("CheckForReservedTiles", logged);
    ListNode* list = NewList();
    ListNode* const kept = list;
    bool done = false;
    if (static_cast<uint8_t>(skip) == 0 && *count > 0) {
        for (int32_t s = 0; s < *count && !done; ++s) {
            char* const step = StepAt(self, static_cast<uint32_t>(s));
            if (Word(step, dstep::kKind) == 3)
                continue;
            uint32_t c = Word(step, dstep::kFirst);
            if (c > Word(step, dstep::kLast))
                continue;
            do {
                char* const point = CellAt(self, c);
                if (Virtual<uint8_t>(self, dslot::kCheckReservedAt, Word(point, 0), Word(point, 4), type)) {
                    *count = s;
                    done = true;
                    break;
                }
                ++c;
            } while (c <= Word(StepAt(self, static_cast<uint32_t>(s)), dstep::kLast));
        }
    }
    if (!done) {
        char* const last = At<char*>(self, dfield::kRuleCellsEnd);
        for (char* record = At<char*>(self, field::kRuleCellRecords); record != last; record += drule::kSize)
            if (Virtual<uint8_t>(self, dslot::kCheckReservedAt, Word(record, drule::kX), Word(record, drule::kZ),
                                 Word(record, drule::kType))) {
                *count = 0;
                break;
            }
    }
    CallThis<void>(addr::SC4List_Clear2, &list);
    Virtual<void>(ListNodeAllocator(), dslot::kDeallocate, static_cast<void*>(kept));
}

// ====================================================================================
// BuildDragPath
// ====================================================================================

// A range of the local range vector (8 bytes: low, high), 32-bit indexing.
inline int32_t* RangeAt(char* ranges, uint32_t i)
{
    return reinterpret_cast<int32_t*>(Offset(ranges, i, 8));
}
inline void Widen(int32_t* range, int32_t by)
{
    if (by < 0)
        range[0] += by;
    else
        range[1] += by;
}

// How far a wide pen widens a range: 1 - pen toward the direction's side
// (1 or 3 with the x word 0, 1 or 7 with it), else pen - 1.
inline int32_t PenOffset(char* self, uint32_t xWord, uint32_t pen)
{
    const uint32_t direction = Word(self, dfield::kDirection);
    const bool back = xWord == 0 ? (direction == 1 || direction == 3) : (direction == 1 || direction == 7);
    return back ? static_cast<int32_t>(1u - pen) : static_cast<int32_t>(pen - 1u);
}

// 0x00639790 (ret 0x14: the start and end points' addresses, the
// classifier's x word, its z word, a network type; called only by the
// NAM-guarded DrawNetworkLine 0x0063AF40). The parts, in the game's order:
//   1. the ranges: a local vector of (low, high) pairs, step count + 1 of
//      them (0x0066D1E0); the global byte 0x00B464F0 = x word != 0; walking
//      from the start to the end one cell at a time (the larger distance's
//      axis; ties alternate: x after a z move, z after an x move; the
//      last axis x to start), each step
//      starting at (100000, -100000) and taking the tracked coordinate's
//      min and max; a move continues the step when its axis' word is set;
//      the walk stops at the end or when the step index reaches the count;
//   2. wide pens (+0x1C above 1: the first `regular` ranges; +0x20 above 1:
//      the irregular steps, the first and last only when they are one cell
//      wide, the first always when the regular count is above 0) widened;
//   3. the steps sized to the step count (a cut, or InsertN with zeros);
//   4. each step's first and last cell index and kind 0, its cells pushed:
//      x fixed (start x plus the step's offset) and z over the range with
//      the x word 0, else z fixed and x over the range; the offset moves by
//      1 or -1 by the start's and end's order on the fixed axis (unsigned);
//   5. pen x above 1: the networks at the line's two ends (an auto-connect
//      filter, the occupant manager's slot 0x80, the edge word slot 0x98)
//      may shift every cell (when the shifted last cell is in the city) and
//      the caller's start and end points by the offset to the network found;
//   6. pen x of 1 with more than two steps (or two regular ones): at each
//      end, a cell one step out (in the city, slot 64 and slot 65 on it)
//      makes a new step there, or replaces a one-cell end step's cell when
//      there are no regular steps;
//   7. the range vector freed.
void __fastcall BuildDragPath(char* self, void*, int32_t* start, int32_t* end, uint32_t xWord, uint32_t zWord,
                              int32_t type)
{
    static bool logged = false;
    FirstCall("BuildDragPath", logged);
    char* const row = reinterpret_cast<char*>(static_cast<std::uintptr_t>(static_cast<uint32_t>(
        addr::cSC4NetworkConstructionCrew_NetworkToolTuning + static_cast<uint32_t>(type) * kRowSize)));
    int32_t curX = start[0], curZ = start[1];
    uint32_t s = 0;
    Vector rangeVector{};
    CallThis<void, uint32_t>(addr::SC4PointVector_Resize, &rangeVector,
                             static_cast<uint32_t>(At<int32_t>(self, field::kStepCount)) + 1u);
    char* const ranges = rangeVector.begin;
    uint8_t& tracksX = Global<uint8_t>(addr::cSC4NetworkTool_DragTracksX);
    tracksX = xWord != 0 ? 1 : 0;

    // 1. The ranges.
    uint8_t lastWasX = 1;
    for (int32_t* range = RangeAt(ranges, 0);; range += 2) {
        range[0] = kRangeLow;
        range[1] = kRangeHigh;
        bool finished = false;
        for (;;) {
            const int32_t tracked = tracksX ? curX : curZ;
            if (tracked < range[0])
                range[0] = tracked;
            if (tracked > range[1])
                range[1] = tracked;
            if ((curX == end[0] && curZ == end[1]) || static_cast<int32_t>(s) == At<int32_t>(self, field::kStepCount)) {
                finished = true;
                break;
            }
            const int32_t dx = static_cast<int32_t>(static_cast<uint32_t>(end[0]) - static_cast<uint32_t>(curX));
            const int32_t dz = static_cast<int32_t>(static_cast<uint32_t>(end[1]) - static_cast<uint32_t>(curZ));
            const uint32_t ax = static_cast<uint32_t>(CdqAbsValue(dx)), az = static_cast<uint32_t>(CdqAbsValue(dz));
            const bool moveX = ax > az || (ax == az && lastWasX == 0); // 0x006398B9
            uint32_t keep;
            if (moveX) {
                curX = static_cast<int32_t>(static_cast<uint32_t>(curX) + (dx > 0 ? 1u : 0xFFFFFFFFu));
                keep = xWord;
                lastWasX = 1;
            } else {
                curZ = static_cast<int32_t>(static_cast<uint32_t>(curZ) + (dz > 0 ? 1u : 0xFFFFFFFFu));
                keep = zWord;
                lastWasX = 0;
            }
            if (keep == 0)
                break;
        }
        if (finished)
            break;
        ++s;
    }

    // 2. Wide pens.
    const uint32_t penX = Word(row, row::kPenWidthX);
    if (penX > 1) {
        const int32_t by = PenOffset(self, xWord, penX);
        for (uint32_t i = 0; i < Word(self, dfield::kRegularSteps); ++i)
            Widen(RangeAt(ranges, i), by);
    }
    const uint32_t penZ = Word(row, row::kPenWidthZ);
    if (penZ > 1) {
        const int32_t by = PenOffset(self, xWord, penZ);
        const int32_t regular = SWord(self, dfield::kRegularSteps);
        if (SWord(self, field::kStepCount) > regular) {
            int32_t* const first = RangeAt(ranges, static_cast<uint32_t>(regular));
            if (regular > 0 || first[0] == first[1])
                Widen(first, by);
            const uint32_t lastIndex = Word(self, field::kStepCount) - 1u;
            for (uint32_t i = static_cast<uint32_t>(regular) + 1u; i < lastIndex; ++i)
                Widen(RangeAt(ranges, i), by);
            int32_t* const last = RangeAt(ranges, lastIndex);
            if (last[0] == last[1])
                Widen(last, by);
        }
    }

    // 3. The steps sized to the step count.
    {
        Vector& steps = VectorAt(self, field::kSteps);
        const uint32_t wanted = Word(self, field::kStepCount);
        const uint32_t size = static_cast<uint32_t>(Count12(steps));
        if (wanted < size) {
            steps.end = Offset(steps.begin, wanted, 12); // the erase of [begin + n, end): nothing after it to move
        } else {
            const uint32_t zero[3] = {0, 0, 0};
            CallThis<void, char*, uint32_t, const uint32_t*>(addr::SC4Vector12_InsertN, &steps, steps.end,
                                                              wanted - size, zero);
        }
    }

    // 4. The steps' cells.
    const int32_t sign = xWord == 0 ? (static_cast<uint32_t>(start[0]) < static_cast<uint32_t>(end[0]) ? 1 : -1)
                                    : (static_cast<uint32_t>(start[1]) < static_cast<uint32_t>(end[1]) ? 1 : -1);
    {
        uint32_t cellIndex = 0;
        int32_t along = 0;
        for (uint32_t k = 0; k < Word(self, field::kStepCount); ++k) {
            int32_t* const range = RangeAt(ranges, k);
            char* const step = StepAt(self, k);
            const int32_t count = range[1] - range[0] + 1;
            Word(step, dstep::kFirst) = cellIndex;
            Word(step, dstep::kLast) = static_cast<uint32_t>(count) + cellIndex - 1u;
            Word(step, dstep::kKind) = 0;
            if (xWord == 0) {
                const uint32_t x = static_cast<uint32_t>(start[0] + along);
                for (int32_t i = 0; i < count; ++i)
                    PushPoint(self, x, static_cast<uint32_t>(range[0] + i));
            } else {
                const uint32_t z = static_cast<uint32_t>(start[1] + along);
                for (int32_t i = 0; i < count; ++i)
                    PushPoint(self, static_cast<uint32_t>(range[0] + i), z);
            }
            cellIndex += static_cast<uint32_t>(count);
            along += sign;
        }
    }

    // 5. Wide pens: the networks at the ends.
    if (Word(row, row::kPenWidthX) > 1) {
        void* found = nullptr;   // the game's +0x14 (cleared before the filter is made)
        void* network = nullptr; // +0x18
        char* const filter = MakeAutoConnectFilter(self);
        char* const step0 = StepAt(self, 0);
        char* const firstCell = CellAt(self, Word(step0, dstep::kFirst));
        char* const lastCell = CellAt(self, Word(step0, dstep::kLast));
        int32_t firstX = SWord(firstCell, 0), firstZ = SWord(firstCell, 4);
        int32_t lastX = SWord(lastCell, 0), lastZ = SWord(lastCell, 4);
        uint8_t straight = 0, any = 0, bit17 = 0;
        Drop(found);
        uint8_t atFirst = FirstByCell(self, &found, static_cast<uint32_t>(firstX), static_cast<uint32_t>(firstZ), filter);
        if (found) {
            void* const occupant = found;
            Drop(network);
            if (AsNetworkOccupant(occupant, &network)) {
                const uint32_t edges = Virtual<uint32_t>(network, dslot::kEdges, type);
                straight = (edges & kEdgesStraight) != 0;
                bit17 = static_cast<uint8_t>((edges >> 17) & 1);
                any = (edges & kEdgesAny) != 0;
            }
            Drop(found);
        }
        uint8_t atLast = FirstByCell(self, &found, static_cast<uint32_t>(lastX), static_cast<uint32_t>(lastZ), filter);
        bool connects = any != 0;
        if (found) {
            void* const occupant = found;
            Drop(network);
            if (AsNetworkOccupant(occupant, &network)) { // the game keeps `found` here: released at the end
                const uint32_t edges = Virtual<uint32_t>(network, dslot::kEdges, type);
                if (!straight)
                    straight = (edges & kEdgesStraight) != 0;
                if (bit17 || (edges & kEdgeBit17))
                    bit17 = 1;
                connects = any != 0 || (edges & kEdgesAny) != 0;
            }
        }
        bool go = connects || SWord(self, field::kStepCount) <= SWord(self, dfield::kRegularSteps);
        if (go && atFirst == atLast)
            go = false;
        if (go) {
            if (SWord(self, field::kStepCount) > SWord(self, dfield::kRegularSteps) && straight) {
                if (bit17) {
                    char* const a = CellAt(self, Word(StepAt(self, 0), dstep::kLast));
                    char* const b = CellAt(self, Word(StepAt(self, 1), dstep::kLast));
                    firstX = SWord(a, 0);
                    firstZ = SWord(a, 4);
                    lastX = SWord(b, 0);
                    lastZ = SWord(b, 4);
                    Drop(found);
                    atFirst = FirstByCell(self, &found, static_cast<uint32_t>(firstX), static_cast<uint32_t>(firstZ),
                                          filter);
                    Drop(found);
                    atLast = FirstByCell(self, &found, static_cast<uint32_t>(lastX), static_cast<uint32_t>(lastZ),
                                         filter);
                } else {
                    char* const b = CellAt(self, Word(StepAt(self, 1), dstep::kFirst));
                    lastX = SWord(b, 0);
                    lastZ = SWord(b, 4);
                    Drop(found);
                    atLast = FirstByCell(self, &found, static_cast<uint32_t>(lastX), static_cast<uint32_t>(lastZ),
                                         filter);
                }
            }
            if (atFirst != atLast) {
                int32_t baseX, baseZ, dX, dZ;
                if (atFirst) {
                    dX = static_cast<int32_t>(static_cast<uint32_t>(firstX) - static_cast<uint32_t>(lastX));
                    dZ = static_cast<int32_t>(static_cast<uint32_t>(firstZ) - static_cast<uint32_t>(lastZ));
                    baseX = firstX;
                    baseZ = firstZ;
                } else {
                    dX = static_cast<int32_t>(static_cast<uint32_t>(lastX) - static_cast<uint32_t>(firstX));
                    dZ = static_cast<int32_t>(static_cast<uint32_t>(lastZ) - static_cast<uint32_t>(firstZ));
                    baseX = lastX;
                    baseZ = lastZ;
                }
                Drop(found);
                if (FirstByCell(self, &found, static_cast<uint32_t>(baseX) + static_cast<uint32_t>(dX),
                                static_cast<uint32_t>(baseZ) + static_cast<uint32_t>(dZ), filter) &&
                    (dX != 0 || dZ != 0)) {
                    const Vector& cells = VectorAt(self, field::kCells);
                    const int32_t count = static_cast<int32_t>(cells.end - cells.begin) >> 3;
                    char* const last = Offset(cells.begin, static_cast<uint32_t>(count), 8) - 8;
                    const int32_t nx = static_cast<int32_t>(Word(last, 0) + static_cast<uint32_t>(dX));
                    const int32_t nz = static_cast<int32_t>(Word(last, 4) + static_cast<uint32_t>(dZ));
                    if (nx >= 0 && static_cast<uint32_t>(nx) < Word(self, field::kCellsX) && nz >= 0 &&
                        static_cast<uint32_t>(nz) < Word(self, field::kCellsZ)) {
                        for (int32_t i = 0; i < count; ++i) {
                            char* const point = CellAt(self, static_cast<uint32_t>(i));
                            Word(point, 0) += static_cast<uint32_t>(dX);
                            Word(point, 4) += static_cast<uint32_t>(dZ);
                        }
                    }
                    start[0] += dX;
                    start[1] += dZ;
                    end[0] += dX;
                    end[1] += dZ;
                }
            }
        }
        if (filter)
            ReleaseOf(filter);
        if (network)
            ReleaseOf(network);
        if (found)
            ReleaseOf(found);
    }

    // 6. Pen 1: one more step at each end where the tool auto-connects.
    if (Word(row, row::kPenWidthX) == 1) {
        const int32_t steps = SWord(self, field::kStepCount);
        if (steps > 2 || (steps == SWord(self, dfield::kRegularSteps) && steps > 1)) {
            char* const filter = MakeAutoConnectFilter(self);
            Vector& cellVector = VectorAt(self, field::kCells);
            Vector& stepVector = VectorAt(self, field::kSteps);
            const uint32_t cellsX = Word(self, field::kCellsX), cellsZ = Word(self, field::kCellsZ);
            // The start.
            {
                char* a;
                char* b;
                if (SWord(self, field::kStepCount) == SWord(self, dfield::kRegularSteps)) {
                    a = CellAt(self, Word(StepAt(self, 0), dstep::kFirst));
                    b = CellAt(self, Word(StepAt(self, 1), dstep::kFirst));
                } else {
                    a = CellAt(self, Word(StepAt(self, 1), dstep::kFirst));
                    b = CellAt(self, Word(StepAt(self, 2), dstep::kFirst));
                }
                const uint32_t dX = Word(a, 0) - Word(b, 0), dZ = Word(a, 4) - Word(b, 4);
                char* const c0 = CellAt(self, Word(StepAt(self, 0), dstep::kFirst));
                const uint32_t nx = Word(c0, 0) + dX, nz = Word(c0, 4) + dZ;
                if (nx < cellsX && nz < cellsZ && Virtual<uint8_t>(self, dslot::kShouldAutoConnect) &&
                    Virtual<uint8_t>(self, dslot::kTestForAutoConnect, nx, nz)) {
                    uint8_t noRegular = 0;
                    uint32_t merged = 0;
                    if (Word(self, dfield::kRegularSteps) == 0) {
                        noRegular = 1;
                        char* const step = StepAt(self, 0);
                        if (Word(step, dstep::kFirst) == Word(step, dstep::kLast)) {
                            char* const point = CellAt(self, Word(StepAt(self, 0), dstep::kFirst));
                            Word(point, 0) = nx;
                            Word(point, 4) = nz;
                            merged = 1;
                        }
                    }
                    char* const stepJ = StepAt(self, merged);
                    const uint32_t lastIndex = Word(stepJ, dstep::kLast);
                    const int32_t n = static_cast<int32_t>(Word(stepJ, dstep::kLast) - Word(stepJ, dstep::kFirst) + 1u);
                    if (n > 0)
                        for (int32_t left = n; left != 0; --left) {
                            char* const from = CellAt(self, lastIndex);
                            const uint32_t point[2] = {Word(from, 0) + dX, Word(from, 4) + dZ};
                            CallThis<char*, char*, const uint32_t*>(addr::SC4PointVector_Insert, &cellVector,
                                                                    Offset(cellVector.begin, merged, 8), point);
                        }
                    for (int32_t i = static_cast<int32_t>(merged); i < SWord(self, field::kStepCount); ++i) {
                        char* const step = StepAt(self, static_cast<uint32_t>(i));
                        Word(step, dstep::kFirst) += static_cast<uint32_t>(n);
                        Word(step, dstep::kLast) += static_cast<uint32_t>(n);
                    }
                    const uint32_t record[3] = {merged, merged + static_cast<uint32_t>(n) - 1u,
                                                Word(StepAt(self, 0), dstep::kKind)};
                    CallThis<char*, char*, const uint32_t*>(addr::SC4Vector12_Insert, &stepVector,
                                                            Offset(stepVector.begin, merged, 12), record);
                    if (!noRegular)
                        ++Word(self, dfield::kRegularSteps);
                    ++Word(self, field::kStepCount);
                }
            }
            // The end.
            {
                const uint32_t lastStep = static_cast<uint32_t>(Count12(stepVector) - 1);
                uint32_t dX, dZ;
                if (SWord(self, field::kStepCount) == SWord(self, dfield::kRegularSteps)) {
                    char* const a = CellAt(self, Word(StepAt(self, lastStep - 1u), dstep::kFirst));
                    char* const b = CellAt(self, Word(StepAt(self, lastStep), dstep::kFirst));
                    dX = Word(b, 0) - Word(a, 0);
                    dZ = Word(b, 4) - Word(a, 4);
                } else {
                    char* const a = CellAt(self, Word(StepAt(self, lastStep - 1u), dstep::kFirst));
                    char* const b = CellAt(self, Word(StepAt(self, lastStep - 2u), dstep::kFirst));
                    dX = Word(a, 0) - Word(b, 0);
                    dZ = Word(a, 4) - Word(b, 4);
                }
                char* const base = CellAt(self, Word(StepAt(self, lastStep), dstep::kFirst));
                const uint32_t nx = Word(base, 0) + dX, nz = Word(base, 4) + dZ;
                if (nx < cellsX && nz < cellsZ && Virtual<uint8_t>(self, dslot::kShouldAutoConnect) &&
                    Virtual<uint8_t>(self, dslot::kTestForAutoConnect, nx, nz)) {
                    uint8_t noRegular = 0;
                    uint32_t merged = 0;
                    if (Word(self, dfield::kRegularSteps) == 0) {
                        noRegular = 1;
                        char* const step = stepVector.end - 12;
                        if (Word(step, dstep::kFirst) == Word(step, dstep::kLast)) {
                            char* const point = CellAt(self, Word(stepVector.end - 12, dstep::kFirst));
                            Word(point, 0) = nx;
                            Word(point, 4) = nz;
                            merged = 1;
                        }
                    }
                    char* const stepT = StepAt(self, Word(self, field::kStepCount) - merged - 1u);
                    const uint32_t firstIndex = Word(stepT, dstep::kFirst);
                    const int32_t n = static_cast<int32_t>(Word(stepT, dstep::kLast) - firstIndex + 1u);
                    if (n > 0) {
                        uint32_t offset = firstIndex << 3;
                        for (int32_t left = n; left != 0; --left) {
                            char* const from = Offset(cellVector.begin, offset, 1);
                            const uint32_t point[2] = {Word(from, 0) + dX, Word(from, 4) + dZ};
                            CallThis<char*, char*, const uint32_t*>(addr::SC4PointVector_Insert, &cellVector,
                                                                    cellVector.end - merged * 8, point);
                            offset += 8;
                        }
                    }
                    for (int32_t i = Count12(stepVector) - static_cast<int32_t>(merged);
                         i < SWord(self, field::kStepCount); ++i) {
                        char* const step = StepAt(self, static_cast<uint32_t>(i));
                        Word(step, dstep::kFirst) += static_cast<uint32_t>(n);
                        Word(step, dstep::kLast) += static_cast<uint32_t>(n);
                    }
                    const int32_t cellCount = static_cast<int32_t>(cellVector.end - cellVector.begin) >> 3;
                    const uint32_t record[3] = {static_cast<uint32_t>(cellCount - n) - merged,
                                                static_cast<uint32_t>(cellCount) - merged - 1u,
                                                Word(StepAt(self, 0), dstep::kKind)};
                    CallThis<char*, char*, const uint32_t*>(addr::SC4Vector12_Insert, &stepVector,
                                                            stepVector.end - merged * 12, record);
                    if (!noRegular)
                        ++Word(self, dfield::kRegularSteps);
                    ++Word(self, field::kStepCount);
                }
            }
            if (filter)
                ReleaseOf(filter);
        }
    }

    // 7.
    Free(rangeVector.begin);
}

// ====================================================================================
// OneWayRoadPass
// ====================================================================================

// 0x0063A640 (ret 4: a network type; called by the segment placer 0x0063B830
// for the one-way road only). In the game's order:
//   1. a local list A of {x, z, -1} for every cell of step 0 and then of the
//      last step (step count - 1; the same step once when they are one);
//   2. while A has records: each record's cell info (GetCell 0x00647A20)
//      gets +0x10 set, and +0x57 with its key added to the cells to solve
//      (+0x84) when +0x57 was 0; a cell with one or two sides in its edge
//      word for the type: its tunnel's other end (a network occupant with
//      the flag 0x80000 asked for the tunnel interface: the side towards the
//      other end's cell, slot 0xEC), then over the four sides with one-way
//      bits (+0x50): the neighbour (or the tunnel's other end) in the city,
//      its cell info not flagged +0x52: in the placement it fixes the side
//      (the record's third word, else the first such side), else it is the
//      side to set; with both, the side's bits made the fixed side's (turned
//      for the opposite side, XOR 3), and the neighbour's opposite side the
//      same, the neighbour recorded {x, z, opposite side} in list B; then
//      A takes B's records and B A's memory, emptied;
//   3. the conflicts: a hash set H = the cells to solve (Assign 0x0050DA70),
//      a hash set G with the next prime bucket count from 100 (the prime
//      table 0x00A829B0), empty; while H has keys: per cell, per side with
//      one-way bits, the edge word's direction for the side (+ the side, & 3)
//      collects the side's bits (turned by XOR 3 when the direction and the
//      side agree in bit 1); two different bits for one direction clear
//      the direction's bits and the neighbour's opposite side's, and add the
//      cell's key (not the neighbour's) to G when the neighbour's +0x57 was
//      0 (setting it); G's keys added to the cells to solve (InsertRange
//      0x00638FA0), G and H swapped, G cleared;
//   4. G and H cleared and their buckets freed, B's and A's memory freed.
void __fastcall OneWayRoadPass(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("OneWayRoadPass", logged);
    Vector a{}, b{};
    // 1.
    for (int32_t s = 0;;) {
        char* const step = StepAt(self, static_cast<uint32_t>(s));
        uint32_t c = Word(step, dstep::kFirst);
        if (c <= Word(step, dstep::kLast)) {
            do {
                char* const point = CellAt(self, c);
                PushRecord(a, Word(point, 0), Word(point, 4), 0xFFFFFFFFu);
                ++c;
            } while (c <= Word(StepAt(self, static_cast<uint32_t>(s)), dstep::kLast));
        }
        const int32_t last = SWord(self, field::kStepCount) - 1;
        if (s == last)
            break;
        s = last;
    }
    char* const set84 = self + field::kSet84;
    const uint32_t cellsX = Word(self, field::kCellsX), cellsZ = Word(self, field::kCellsZ);
    // 2.
    if (a.begin != a.end) {
        for (;;) {
            for (char* record = a.begin; record != a.end; record += 12) {
                const uint32_t rx = Word(record, 0), rz = Word(record, 4);
                char* const info = CallThis<char*, uint32_t>(addr::cSC4NetworkWorldCache_GetCell,
                                                             self + field::kWorldCache, (rz << 16) + rx);
                const uint8_t marked = At<uint8_t>(info, cell::kByte57);
                At<uint8_t>(info, cell::kInPlacement) = 1;
                if (!marked) {
                    At<uint8_t>(info, cell::kByte57) = 1;
                    const uint32_t key = (Word(record, 4) << 16) + Word(record, 0);
                    InsertKey(set84, &key);
                }
                uint32_t edges = Word(info, cell::kByType + type * 4);
                edges = (((edges & 0x07070707u) + 0x07070707u) & 0x08080808u) * 0x01010101u >> 27;
                if (static_cast<int32_t>(edges) < 1 || static_cast<int32_t>(edges) > 2)
                    continue;
                int32_t tunnelSide = -1;
                uint32_t otherX = 0, otherZ = 0; // the game's: what an earlier record's tunnel left (read only for a side set below)
                if (void* const occupant = At<void*>(info, cell::kOccupant)) {
                    if (Virtual<uint8_t>(occupant, dslot::kHasFlag, kFlagTunnel)) {
                        void* tunnel = nullptr;
                        if (Virtual<uint8_t>(occupant, dslot::kQueryInterface, kIidTunnel, &tunnel)) {
                            void* const other = CallThis<void*>(addr::SC4TunnelOccupant_OtherEnd, tunnel);
                            uint32_t gx = 0, gz = 0;
                            Virtual<void>(other, dslot::kOccupiedCell, &gx, &gz);
                            otherX = gx;
                            otherZ = gz;
                            if (static_cast<int32_t>(gx) < static_cast<int32_t>(rx))
                                tunnelSide = 0;
                            else if (static_cast<int32_t>(gz) < static_cast<int32_t>(rz))
                                tunnelSide = 1;
                            else if (static_cast<int32_t>(gx) > static_cast<int32_t>(rx))
                                tunnelSide = 2;
                            else if (static_cast<int32_t>(gz) > static_cast<int32_t>(rz))
                                tunnelSide = 3;
                        }
                        if (tunnel)
                            ReleaseOf(tunnel);
                    }
                }
                int32_t fixedSide = SWord(record, 8);
                int32_t setSide = -1;
                char* neighbor = nullptr;
                for (uint32_t i = 0; i < 4; ++i) {
                    if (((At<uint8_t>(info, cell::kOneWay) >> (2 * i)) & 3) == 0)
                        continue;
                    uint32_t nx = Global<uint32_t>(addr::cSC4NetworkTool_StepSideDx + i * 4) + rx;
                    uint32_t nz = Global<uint32_t>(addr::cSC4NetworkTool_StepSideDz + i * 4) + rz;
                    if (static_cast<int32_t>(i) == tunnelSide) {
                        nx = otherX;
                        nz = otherZ;
                    }
                    if (nx >= cellsX || nz >= cellsZ)
                        continue;
                    char* const e = Entry(self, (nz << 16) + nx);
                    if (!e || At<uint8_t>(e, cell::kHasFlag100000))
                        continue;
                    if (At<uint8_t>(e, cell::kInPlacement) && (fixedSide < 0 || static_cast<int32_t>(i) == fixedSide)) {
                        fixedSide = static_cast<int32_t>(i);
                        continue;
                    }
                    setSide = static_cast<int32_t>(i);
                    neighbor = e;
                }
                if (fixedSide < 0 || setSide < 0)
                    continue;
                const uint8_t oneWay = At<uint8_t>(info, cell::kOneWay);
                uint8_t bits = static_cast<uint8_t>((oneWay >> (fixedSide * 2)) & 3);
                const uint8_t current = static_cast<uint8_t>((oneWay >> (setSide * 2)) & 3);
                if (setSide == (fixedSide ^ 1))
                    bits ^= 3;
                if (current == bits)
                    continue;
                At<uint8_t>(info, cell::kOneWay) = static_cast<uint8_t>(
                    (oneWay & static_cast<uint8_t>(~(3u << (setSide * 2)))) + static_cast<uint8_t>(bits << (setSide * 2)));
                if (!neighbor)
                    continue;
                const uint32_t opposite = static_cast<uint32_t>(setSide - 2) & 3;
                const uint8_t theirs = At<uint8_t>(neighbor, cell::kOneWay);
                At<uint8_t>(neighbor, cell::kOneWay) = static_cast<uint8_t>(
                    (theirs & static_cast<uint8_t>(~(3u << (opposite * 2)))) + static_cast<uint8_t>(bits << (opposite * 2)));
                PushRecord(b, Word(neighbor, cell::kX), Word(neighbor, cell::kZ), opposite);
            }
            char* const oldBegin = a.begin;
            char* const oldCapacity = a.capacity;
            a = b;
            b.begin = oldBegin;
            b.end = oldBegin;
            b.capacity = oldCapacity;
            if (a.begin == a.end)
                break;
        }
    }
    // 3.
    alignas(4) char h[hset::kSize] = {};
    alignas(4) char g[hset::kSize] = {};
    CallThis<void, char*>(addr::SC4Uint32HashSet_Assign, h, set84);
    {
        const uint32_t* prime = reinterpret_cast<const uint32_t*>(addr::SC4HashTable_Primes);
        int32_t length = static_cast<int32_t>(kPrimeCount);
        while (length > 0) {
            const int32_t half = length >> 1;
            if (prime[half] < kPrimeWanted) {
                prime += half + 1;
                length = length - half - 1;
            } else {
                length = half;
            }
        }
        const uint32_t buckets =
            reinterpret_cast<std::uintptr_t>(prime) == addr::SC4HashTable_PrimesEnd ? kNoPrime : *prime;
        CallThis<void, uint32_t>(addr::PtrVector_Reserve, g + hset::kBuckets, buckets);
        const uint32_t zero = 0;
        CallThis<void, char*, uint32_t, const uint32_t*>(addr::SC4PtrVector_FillInsert, g + hset::kBuckets,
                                                          At<char*>(g, hset::kBucketsEnd), buckets, &zero);
        At<uint32_t>(g, hset::kCount) = 0;
    }
    uint8_t tag = 0;
    const uint32_t edgeOffset = static_cast<uint32_t>(cell::kByType) + type * 4;
    while (At<uint32_t>(h, hset::kCount) != 0) {
        for (char* node = FirstNode(h); node; node = NextNode(node, h)) {
            const uint32_t* const key = &At<uint32_t>(node, hset::kKey);
            char* const info = Entry(self, *key);
            uint32_t bySide[4] = {0, 0, 0, 0};
            for (uint32_t i = 0; i < 4; ++i) {
                const uint32_t word = *reinterpret_cast<uint32_t*>(Offset(info, edgeOffset, 1));
                const uint32_t direction = ((word >> (8 * i)) + i) & 3;
                uint32_t bits = (static_cast<uint32_t>(At<uint8_t>(info, cell::kOneWay)) >> (2 * i)) & 3;
                if (bits == 0)
                    continue;
                if (((direction ^ i) & 2) == 0)
                    bits ^= 3;
                uint32_t& held = bySide[direction];
                if (held != 0 && held != bits) {
                    At<uint8_t>(info, cell::kOneWay) &= static_cast<uint8_t>(~(3u << (direction * 2)));
                    const uint32_t nx =
                        (*key & 0xFFFF) + Global<uint32_t>(addr::cSC4NetworkTool_StepSideDx + direction * 4);
                    const uint32_t nz = (*key >> 16) + Global<uint32_t>(addr::cSC4NetworkTool_StepSideDz + direction * 4);
                    if (nx < cellsX && nz < cellsZ) {
                        if (char* const e = Entry(self, (nz << 16) + nx)) {
                            const uint32_t opposite = (direction - 2) & 3;
                            At<uint8_t>(e, cell::kOneWay) &= static_cast<uint8_t>(~(3u << (opposite * 2)));
                            if (!At<uint8_t>(e, cell::kByte57)) {
                                At<uint8_t>(e, cell::kByte57) = 1;
                                InsertKey(g, key);
                            }
                        }
                    }
                }
                held = bits;
            }
        }
        CallThis<void, char*, char*, char*, char*, void*>(addr::SC4Uint32HashSet_InsertRange, set84, FirstNode(g), g,
                                                           nullptr, g, &tag);
        for (std::size_t at = hset::kBuckets; at < hset::kSize; at += 4) {
            const uint32_t held = At<uint32_t>(h, at);
            At<uint32_t>(h, at) = At<uint32_t>(g, at);
            At<uint32_t>(g, at) = held;
        }
        CallThis<void>(addr::SC4Uint32HashSet_Clear, g);
    }
    // 4.
    CallThis<void>(addr::SC4Uint32HashSet_Clear, g);
    if (char* const memory = At<char*>(g, hset::kBuckets))
        Free(memory);
    CallThis<void>(addr::SC4Uint32HashSet_Clear, h);
    if (char* const memory = At<char*>(h, hset::kBuckets))
        Free(memory);
    if (b.begin)
        Free(b.begin);
    if (a.begin)
        Free(a.begin);
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kDrag[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_BuildDragPath, BuildDragPath),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_OneWayRoadPass, OneWayRoadPass),
    OPENSC4_DETOUR(addr::SC4Vector12_InsertN, Vector12InsertN),
    OPENSC4_DETOUR(addr::SC4Vector12_Insert, Vector12Insert),
    OPENSC4_DETOUR(addr::SC4Uint32HashSet_InsertRange, HashSetInsertRange),
    OPENSC4_DETOUR(addr::SC4HashIterator_Distance, HashDistance),
};

const Detour kDragChecks[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_TestForAutoConnectOccupant, TestForAutoConnectOccupant),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_IdentifyStartingConditions, IdentifyStartingConditions),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_CheckForReservedTilesAt, CheckForReservedTilesAt),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_CheckForReservedTiles, CheckForReservedTiles),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_CheckForGoodTerrain, CheckForGoodTerrain),
    OPENSC4_DETOUR(addr::SC4AutoConnectFilter_IsOccupantIncluded, AutoConnectIsOccupantIncluded),
    OPENSC4_DETOUR(addr::SC4AutoConnectFilter_IsPlacedNetworkOccupant, IsPlacedNetworkOccupant),
    OPENSC4_DETOUR(addr::SC4AutoConnectFilter_IsPropertyHolderIncluded, AutoConnectIsPropertyHolderIncludedThunk),
    OPENSC4_DETOUR(addr::SC4AutoConnectFilter_DeletingDestructor, AutoConnectDeletingDestructor),
};

#undef OPENSC4_DETOUR

bool InstallDrag(registry::Mode) { return InstallDetours(kDrag, "drag"); }
bool InstallDragChecks(registry::Mode) { return InstallDetours(kDragChecks, "dragchecks"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.drag", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_BuildDragPath,
                        "Our network tool's drag: BuildDragPath (the steps and dragged cells DrawNetworkLine lays), "
                        "OneWayRoadPass, the 12-byte vector's InsertN and Insert, the hash set's InsertRange and the "
                        "hash iterator distance (6 detours; src/core/nettool_drag.cpp)",
                        false, InstallDrag}),
    registry::Register({"simulators.nettool.dragchecks", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_CheckForReservedTiles,
                        "Our network tool's drag checks: slots 65 to 69 (TestForAutoConnectOccupant, "
                        "IdentifyStartingConditions, CheckForReservedTiles by cell and by step, CheckForGoodTerrain) "
                        "and the auto-connect filter's slots and test (9 detours; src/core/nettool_drag.cpp)",
                        false, InstallDragChecks}),
};

} // namespace
} // namespace opensc4::nettool_sim
