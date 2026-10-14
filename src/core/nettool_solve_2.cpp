// Our cSC4NetworkTool, part t8, second file (build 641; the first,
// src/core/nettool_solve_1.cpp, has the solver and, at its top, the state,
// NAM, wrapper, register and stack leftover analysis and the call list of
// the whole part; the shared layouts and helpers are in
// src/core/nettool_solve.h). Two groups of detours at the game's addresses:
//   simulators.nettool.tilefix: FixCellEdges 0x006359B0, FixEdgesKind1
//     0x00633A60, FixEdgesKind2 0x00633C10, MarkDiagonalRuns 0x006335E0,
//     IsDiagonalEdge 0x00623C90, SplitSidesByKind 0x00620DF0,
//     SetSideHighBits 0x00621070, SetSideKind 0x006335B0 and PatchTilePair
//     0x006337E0 (called by the NAM-guarded RUL2 pass 0x00634D70) (9 detours);
//   simulators.nettool.dragcells: CollectDraggedCells 0x006355C0 (called by
//     the NAM-guarded 0x0062CD50 and by 0x0063B830), SpreadConnected
//     0x006345B0, CollectIntersectionCells 0x0063B1A0, EraseRecordE4Cells
//     0x00638BB0 and EraseBridgeCells 0x00638B40 (5 detours).
// No floats; plain detours (the callers' registers are listed in the first
// file); the stack leftovers these functions pass on are explained there.
#include "core/nettool_solve.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim {
namespace {

using namespace solve;
namespace table = rul::table;

// ====================================================================================
// Edge words
// ====================================================================================

// 0x00620DF0 (ret 0x10: the "2" sides out, the odd sides out, their counts
// out; this an edge word; called by 0x00623C90 and 0x00633A60): the counts
// from 0, then each side (0 to 3) whose low three bits are 2 into the first
// array, 1 or 3 into the second. EAX not read by the callers.
void __fastcall SplitSidesByKind(const uint8_t* self, void*, int32_t* twos, int32_t* odds, int32_t* twoCount,
                                 int32_t* oddCount)
{
    static bool logged = false;
    FirstCall("SplitSidesByKind", logged);
    *twoCount = 0;
    *oddCount = 0;
    for (int32_t side = 0; side < static_cast<int32_t>(solve::kSides); ++side) {
        const uint8_t kind = static_cast<uint8_t>(self[side] & 7);
        if (kind == 2)
            twos[(*twoCount)++] = side;
        else if (kind == 1 || kind == 3)
            odds[(*oddCount)++] = side;
    }
}

// 0x00621070 (ret 8: a side, a value; this an edge word; 0x00633A60's): the
// side's byte (the shift is side * 8, masked as the CPU does) becomes its
// low three bits | (value << 3) (eight bits). EAX the new byte shifted into
// place (no caller reads it).
uint32_t __fastcall SetSideHighBits(uint32_t* self, void*, uint32_t side, uint32_t value)
{
    static bool logged = false;
    FirstCall("SetSideHighBits", logged);
    const uint32_t shift = (side << 3) & 31;
    const uint8_t byte = static_cast<uint8_t>(((*self >> shift) & 7) | static_cast<uint8_t>(value << 3));
    const uint32_t placed = static_cast<uint32_t>(byte) << shift;
    *self = (*self & ~(0xFFu << shift)) | placed;
    return placed;
}

// 0x006335B0 (ret 8: a side, a kind; this an edge word; 0x00633A60's): the
// side's byte with its low three bits replaced by the kind's byte, through
// SetSideByte 0x00620FC0 (which takes the low byte of the word the game
// builds from the shifted edge word). EAX SetSideByte's (this).
void* __fastcall SetSideKind(uint32_t* self, void*, uint32_t side, uint32_t kind)
{
    static bool logged = false;
    FirstCall("SetSideKind", logged);
    const uint32_t shifted = *self >> ((side * 8) & 31);
    const uint32_t value = (shifted & ~0xFFu) | (((shifted & 0xF8u) | (kind & 0xFFu)) & 0xFFu);
    return CallThis<void*, uint32_t, uint32_t>(addr::cSC4NetworkTool_SetSideByte, self, side, value);
}

// 0x00623C90 (cdecl: a cell info, a network type; called by 0x006335E0 and
// 0x00633A60): 0x00620DF0 on the type's edge word; 1 when no side is of
// kind 2 and one or two are of kind 1 or 3, else 0 (all of EAX).
uint32_t __cdecl IsDiagonalEdge(char* info, uint32_t type)
{
    static bool logged = false;
    FirstCall("IsDiagonalEdge", logged);
    int32_t twos[4] = {0, 0, 0, 0}, odds[4] = {0, 0, 0, 0};
    int32_t twoCount = 0, oddCount = 0;
    CallThis<void, int32_t*, int32_t*, int32_t*, int32_t*>(addr::cSC4NetworkTool_SplitSidesByKind,
                                                           EdgeWord(info, type), twos, odds, &twoCount, &oddCount);
    if (twoCount != 0)
        return 0;
    return (oddCount == 2 || oddCount == 1) ? 1u : 0u;
}

// 0x006335E0 (ret 0xC: a cell info, four bytes out (cleared as one word), a
// network type; this the tool; 0x00633A60's): for each side not yet marked
// whose kind is 1 (the other side is the next one) or 3 (the previous one)
// and whose other side's kind adds up to 4 with it: from the cell, a walk
// along each of the two sides' directions in turn (the side's step, the cell
// entry 0x00633220; none ends it as a failure; a diagonal cell
// (0x00623C90) ends it as a success; else the side turns (up for 3, down
// otherwise) and the new side's kind must add up to 4 with the current one,
// which it becomes); both walks succeeding mark the two sides.
void __fastcall MarkDiagonalRuns(char* self, void*, char* info, uint8_t* marks, uint32_t type)
{
    static bool logged = false;
    FirstCall("MarkDiagonalRuns", logged);
    At<uint32_t>(marks, 0) = 0;
    for (uint32_t side = 0; side < solve::kSides; ++side) {
        if (marks[side])
            continue;
        const uint32_t word = *EdgeWord(info, type);
        const uint32_t kind = (word >> ((side * 8) & 31)) & 3;
        uint32_t other = 0;
        if (kind == 1)
            other = (side + 1) & 3;
        else if (kind == 3)
            other = (side - 1) & 3;
        else
            continue;
        const uint32_t otherKind = (word >> ((other * 8) & 31)) & 3;
        if (kind + otherKind != 4)
            continue;
        bool ok = true;
        for (int pass = 0; pass < 2 && ok; ++pass) {
            uint32_t at = pass == 0 ? side : other;
            uint32_t direction = pass == 0 ? kind : otherKind;
            uint32_t x = At<uint32_t>(info, cell::kX), z = At<uint32_t>(info, cell::kZ);
            for (;;) {
                z += SideDz(at);
                x += SideDx(at);
                char* const entry = Entry(self, Key(x, z));
                if (!entry) {
                    ok = false;
                    break;
                }
                if (static_cast<uint8_t>(
                        CallCdecl<uint32_t, char*, uint32_t>(addr::cSC4NetworkTool_IsDiagonalEdge, entry, type)))
                    break;
                if (direction == 3)
                    ++at;
                else
                    --at;
                at &= 3;
                const uint32_t next = (*EdgeWord(entry, type) >> ((at * 8) & 31)) & 3;
                if (next + direction != 4) {
                    ok = false;
                    break;
                }
                direction = next;
            }
        }
        if (ok) {
            marks[side] = 1;
            marks[other] = 1;
        }
    }
}

// 0x00633A60 (ret 0x10: a cell info, a network type, a cell set, an apply
// byte; this the tool; FixCellEdges' for type rows of kind 1): AL 1 at once
// unless the type's edge word has sides of kind 2 and of kinds 1 or 3. The
// diagonal runs marked (0x006335E0); then for each odd side not marked: its
// neighbour's cell entry (none: next side), whether it is diagonal
// (0x00623C90); with the apply byte, AL 0 when the cell is immovable and the
// neighbour diagonal, else the side's high bits set to that (0x00621070),
// its kind to 2 when not diagonal (0x006335B0), the edges combined; then,
// when the neighbour is in the set (SC4Uint32HashTable::Find), the same on
// the neighbour's opposite side, else AL 0 when it is diagonal and
// immovable. AL 1 at the end.
uint8_t __fastcall FixEdgesKind1(char* self, void*, char* info, uint32_t type, char* set, uint32_t apply)
{
    static bool logged = false;
    FirstCall("FixEdgesKind1", logged);
    int32_t twos[4] = {0, 0, 0, 0}, odds[4] = {0, 0, 0, 0};
    int32_t twoCount = 0, oddCount = 0;
    uint32_t* const word = EdgeWord(info, type);
    CallThis<void, int32_t*, int32_t*, int32_t*, int32_t*>(addr::cSC4NetworkTool_SplitSidesByKind, word, twos, odds,
                                                           &twoCount, &oddCount);
    if (twoCount == 0 || oddCount == 0)
        return 1;
    alignas(4) uint8_t marks[4] = {0, 0, 0, 0};
    CallThis<void, char*, uint8_t*, uint32_t>(addr::cSC4NetworkTool_MarkDiagonalRuns, self, info, marks, type);
    for (int32_t n = 0; n < oddCount; ++n) {
        const uint32_t side = static_cast<uint32_t>(odds[n]);
        if (marks[side])
            continue;
        uint32_t key =
            ((At<uint32_t>(info, cell::kZ) + SideDz(side)) << 16) + SideDx(side) + At<uint32_t>(info, cell::kX);
        char* const neighbor = Entry(self, key);
        if (!neighbor)
            continue;
        const uint8_t diagonal =
            static_cast<uint8_t>(CallCdecl<uint32_t, char*, uint32_t>(addr::cSC4NetworkTool_IsDiagonalEdge, neighbor,
                                                                      type)) != 0
                ? 1
                : 0;
        if (static_cast<uint8_t>(apply)) {
            if (At<uint8_t>(info, cell::kImmovable) && diagonal)
                return 0;
            CallThis<uint32_t, uint32_t, uint32_t>(addr::cSC4NetworkTool_SetSideHighBits, word, side,
                                                   static_cast<uint32_t>(diagonal));
            if (!diagonal)
                CallThis<void*, uint32_t, uint32_t>(addr::cSC4NetworkTool_SetSideKind, word, side, 2u);
            Combine(info);
        }
        std::uintptr_t found[2] = {0, 0};
        CallThis<void*, std::uintptr_t*, uint32_t*>(addr::SC4Uint32HashTable_Find, set, found, &key);
        if (found[0]) {
            const uint32_t opposite = (side - 2) & 3;
            uint32_t* const theirs = EdgeWord(neighbor, type);
            CallThis<uint32_t, uint32_t, uint32_t>(addr::cSC4NetworkTool_SetSideHighBits, theirs, opposite,
                                                   static_cast<uint32_t>(diagonal));
            if (!diagonal)
                CallThis<void*, uint32_t, uint32_t>(addr::cSC4NetworkTool_SetSideKind, theirs, opposite, 2u);
            Combine(neighbor);
        } else if (diagonal && At<uint8_t>(neighbor, cell::kImmovable)) {
            return 0;
        }
    }
    return 1;
}

// 0x00633C10 (ret 0x10: a cell info, a network type, a cell set, an apply
// byte; this the tool; FixCellEdges' for type rows of kind 2): for each side
// with a kind: an odd one counts when the opposite side has a kind that is
// not the same or the previous side's kind is not its complement, at level
// 2 when the next (kind 1) or previous (kind 3) side is straight or its
// complement, else 1; a straight one gets the level of the opposite side
// having a kind (1), the previous side being 1 (2) and the next being 3 (4),
// and counts unless that is 0 or 1. The side's byte becomes (level << 3) |
// kind: with the apply byte (AL 0 when the cell is immovable) on the cell
// (SetSideByte, edges combined), and on the neighbour's opposite side when
// the neighbour is in the set (its cell entry; none: next side); a
// neighbour not in the set ends with AL 0 when it is immovable. AL 1 at the
// end.
uint8_t __fastcall FixEdgesKind2(char* self, void*, char* info, uint32_t type, char* set, uint32_t apply)
{
    static bool logged = false;
    FirstCall("FixEdgesKind2", logged);
    uint32_t* const word = EdgeWord(info, type);
    for (uint32_t side = 0; side < solve::kSides; ++side) {
        const uint32_t previous = (side - 1) & 3, opposite = (side - 2) & 3, next = (side + 1) & 3;
        const uint32_t bits = *word;
        auto kindOf = [bits](uint32_t at) { return (bits >> ((at * 8) & 31)) & 3; };
        const uint32_t kind = kindOf(side);
        if (kind == 0)
            continue;
        uint32_t level = 0;
        if (kind & 1) {
            const uint32_t oppositeKind = kindOf(opposite);
            bool counts = oppositeKind != 0;
            if (counts && ((oppositeKind ^ kind) & 3) == 0 && kind == 4 - kindOf(previous))
                counts = false;
            const uint32_t besideKind = kindOf(kind == 1 ? next : previous);
            const bool high = besideKind == 2 || besideKind + kind == 4;
            if (!counts)
                continue;
            level = (high ? 1u : 0u) + 1;
        } else {
            if (kind != 2)
                continue;
            level = kindOf(opposite) ? 1u : 0u;
            if (kindOf(previous) == 1)
                level |= 2;
            if (kindOf(next) == 3)
                level |= 4;
            if (level == 1 || level == 0)
                continue;
        }
        const uint32_t key = ((SideDz(side) + At<uint32_t>(info, cell::kZ)) << 16) + SideDx(side) +
                             At<uint32_t>(info, cell::kX);
        const uint32_t byte = static_cast<uint8_t>((level << 3) | kind);
        if (static_cast<uint8_t>(apply)) {
            if (At<uint8_t>(info, cell::kImmovable))
                return 0;
            CallThis<void*, uint32_t, uint32_t>(addr::cSC4NetworkTool_SetSideByte, word, side, byte);
            Combine(info);
        }
        if (FindNode(set, key)) {
            char* const neighbor = Entry(self, key);
            if (!neighbor)
                continue;
            CallThis<void*, uint32_t, uint32_t>(addr::cSC4NetworkTool_SetSideByte, EdgeWord(neighbor, type), opposite,
                                                byte);
            Combine(neighbor);
        } else {
            char* const neighbor = Entry(self, key);
            if (neighbor && At<uint8_t>(neighbor, cell::kImmovable))
                return 0;
        }
    }
    return 1;
}

// 0x006337E0 PatchTilePair (ret 0x10: a range {first, last} of the RUL2
// multimap 0x00B466D0, two solved cells, a direction; this not read; the
// guarded 0x00634D70's): each cell's rotation byte turned into the pair's
// frame (by 8 - (direction + 2), backwards when flipped; & 0x83); for each
// node of the range whose second piece (+0x1C) is the second cell's: the
// rotations flipped (^ 0x82 both) when the node's first rotation differs
// from the first cell's in the flip bit; a match is the first rotation
// equal and either the second cell's piece 0 or the second rotation equal.
// On a match: EAX 2 when its third piece (+0x24) is 0; else both cells get
// the node's third and fourth pieces and rotations (^ 0x82 when flipped),
// turned back by direction + 2; EAX 1. No match: EAX 0.
uint32_t __fastcall PatchTilePair(void*, void*, char** range, char* first, char* second, uint32_t direction)
{
    static bool logged = false;
    FirstCall("PatchTilePair", logged);
    const uint8_t turn = static_cast<uint8_t>(static_cast<uint8_t>(direction) + 2);
    const uint8_t back = static_cast<uint8_t>(8 - turn);
    auto into = [back](uint8_t rotation) {
        return static_cast<uint8_t>(((rotation & 0x80) ? rotation - back + 8 : back + rotation) & 0x83);
    };
    const uint8_t mine = into(At<uint8_t>(first, solve::kRotation));
    const uint8_t theirs = into(At<uint8_t>(second, solve::kRotation));
    char* node = range[0];
    char* const last = range[1];
    while (node != last) {
        if (At<uint32_t>(second, solve::kPiece) == At<uint32_t>(node, solve::kPair2Piece)) {
            bool flipped = false;
            uint8_t a = mine, b = theirs;
            if ((At<uint8_t>(node, solve::kPair1Rotation) ^ mine) & 0x80) {
                a = static_cast<uint8_t>(mine ^ 0x82);
                b = static_cast<uint8_t>(theirs ^ 0x82);
                flipped = true;
            }
            if (a == At<uint8_t>(node, solve::kPair1Rotation) &&
                (At<uint32_t>(second, solve::kPiece) == 0 || b == At<uint8_t>(node, solve::kPair2Rotation))) {
                const uint32_t piece = At<uint32_t>(node, solve::kPair3Piece);
                if (!piece)
                    return 2;
                At<uint32_t>(first, solve::kPiece) = piece;
                At<uint32_t>(second, solve::kPiece) = At<uint32_t>(node, solve::kPair4Piece);
                At<uint8_t>(first, solve::kRotation) = At<uint8_t>(node, solve::kPair3Rotation);
                At<uint8_t>(second, solve::kRotation) = At<uint8_t>(node, solve::kPair4Rotation);
                if (flipped) {
                    At<uint8_t>(first, solve::kRotation) ^= 0x82;
                    At<uint8_t>(second, solve::kRotation) ^= 0x82;
                }
                auto out = [turn](uint8_t rotation) {
                    return static_cast<uint8_t>(((rotation & 0x80) ? rotation - turn + 8 : rotation + turn) & 0x83);
                };
                At<uint8_t>(first, solve::kRotation) = out(At<uint8_t>(first, solve::kRotation));
                At<uint8_t>(second, solve::kRotation) = out(At<uint8_t>(second, solve::kRotation));
                return 1;
            }
        }
        // The tree's increment (inlined by the game).
        if (char* right = At<char*>(node, tree::kRight)) {
            while (char* left = At<char*>(right, tree::kLeft))
                right = left;
            node = right;
        } else {
            char* parent = At<char*>(node, tree::kParent);
            while (node == At<char*>(parent, tree::kRight)) {
                node = parent;
                parent = At<char*>(parent, tree::kParent);
            }
            if (At<char*>(node, tree::kRight) != parent)
                node = parent;
        }
    }
    return 0;
}

// 0x006359B0 FixCellEdges (ret 0xC: the cells, the
// failed cells out, a byte; this the tool; the solver's with 1): the cells
// grown by one into a local set (0x00634480). With the byte: for each cell
// of the grown set with at most one flag, its type (the first of the 13
// whose flag it has; when none, the value the game's third argument slot
// holds: the dword as pushed, or the last type found); when the cell is in
// the cells, that type's side bytes reduced to their kinds (0x00621440) and
// the edges combined. Then for each cell of the grown set (walked from its
// first bucket): whether it is in the cells (the low byte of the game's
// third argument slot); with at most one flag and a type found (the game's
// first argument slot keeps it), its type row's word +0x0 picks
// FixEdgesKind1 (1) or FixEdgesKind2 (2) with that slot as the apply byte;
// a refusal erases the cell from the cells and inserts it into the failed.
void __fastcall FixCellEdges(char* self, void*, char* cells, char* failed, uint32_t check)
{
    static bool logged = false;
    FirstCall("FixCellEdges", logged);
    alignas(4) char grown[table::kSize] = {};
    Construct(grown);
    CallThis<void, char*, char*, uint32_t>(addr::cSC4NetworkTool_GrowCellSet, self, cells, grown, 1u);
    // The game's argument slots it writes and reads back.
    uint32_t checkSlot = check;
    uint32_t cellsSlot = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(cells));
    if (static_cast<uint8_t>(check)) {
        for (char* node = Begin(grown); node; node = NextNode(node, grown)) {
            const bool inCells = FindNode(cells, At<uint32_t>(node, table::kKey)) != nullptr;
            char* const entry = Entry(self, At<uint32_t>(node, table::kKey));
            const uint32_t flags = At<uint32_t>(entry, cell::kFlags);
            if (flags & (flags - 1))
                continue;
            for (uint32_t type = 0; type < kTypes; ++type)
                if (TypeFlag(type) & flags) {
                    checkSlot = type;
                    break;
                }
            if (inCells) {
                CallThis<void>(addr::cSC4NetworkTool_ClearSideHighBits, EdgeWord(entry, checkSlot));
                Combine(entry);
            }
        }
    }
    char* node = nullptr;
    {
        const uint32_t count = BucketCount(grown);
        for (uint32_t i = 0; i < count; ++i)
            if (char* const head = reinterpret_cast<char**>(At<Vector>(grown, table::kBuckets).begin)[i]) {
                node = head;
                break;
            }
    }
    for (; node; node = NextNode(node, grown)) {
        const bool inCells = FindNode(cells, At<uint32_t>(node, table::kKey)) != nullptr;
        checkSlot = (checkSlot & ~0xFFu) | (inCells ? 1u : 0u);
        uint32_t* const key = &At<uint32_t>(node, table::kKey);
        char* const entry = Entry(self, *key);
        const uint32_t flags = At<uint32_t>(entry, cell::kFlags);
        if (flags & (flags - 1))
            continue;
        uint32_t kind = 0;
        for (uint32_t type = 0; type < kTypes; ++type)
            if (TypeFlag(type) & flags) {
                kind = At<uint32_t>(TypeRow(type), rul::typerow::kWord0);
                cellsSlot = type;
                break;
            }
        uint8_t ok = 1;
        if (kind == 1)
            ok = CallThis<uint8_t, char*, uint32_t, char*, uint32_t>(addr::cSC4NetworkTool_FixEdgesKind1, self, entry,
                                                                     cellsSlot, cells, checkSlot);
        else if (kind == 2)
            ok = CallThis<uint8_t, char*, uint32_t, char*, uint32_t>(addr::cSC4NetworkTool_FixEdgesKind2, self, entry,
                                                                     cellsSlot, cells, checkSlot);
        else
            continue;
        if (!ok) {
            Erase(cells, key);
            Insert(failed, key);
        }
    }
    Destroy(grown);
}

// ====================================================================================
// The drag's cells
// ====================================================================================

// 0x006345B0 (ret 8: a uint32 vector of cell keys, a pass count; this the
// tool; 0x006355C0's with 4). Per pass: the vector's contents taken as the
// pass's list and the vector given the previous list's memory, empty (the
// first pass: none); each listed key appended again, and, when its cell
// entry exists, each side its combined edges connect (EdgeConnects
// 0x00620EC0) to a neighbour inside the city that is not in +0x84 and whose
// cell entry is neither immovable nor of step kind 3: the neighbour inserted
// into +0x84, marked (+0x57) and appended (in place or InsertN 0x0051CA60).
// The last pass's list memory freed at the end.
void __fastcall SpreadConnected(char* self, void*, Vector* frontier, int32_t passes)
{
    static bool logged = false;
    FirstCall("SpreadConnected", logged);
    if (passes <= 0)
        return;
    char* spareBegin = nullptr;
    char* spareCapacity = nullptr;
    Vector work{nullptr, nullptr, nullptr};
    for (int32_t left = passes;;) {
        work = *frontier;
        frontier->begin = spareBegin;
        frontier->end = spareBegin;
        frontier->capacity = spareCapacity;
        for (char* at = work.begin; at != work.end; at += 4) {
            const uint32_t key = At<uint32_t>(at, 0);
            PushWord(*frontier, key);
            char* const entry = Entry(self, key);
            if (!entry)
                continue;
            for (uint32_t side = 0; side < solve::kSides; ++side) {
                if (!CallThis<uint8_t, uint32_t>(addr::cSC4NetworkTool_EdgeConnects, entry + cell::kCombined, side))
                    continue;
                const uint32_t x = SideDx(side) + At<uint32_t>(entry, cell::kX);
                const uint32_t z = SideDz(side) + At<uint32_t>(entry, cell::kZ);
                if (x >= At<uint32_t>(self, field::kCellsX) || z >= At<uint32_t>(self, field::kCellsZ))
                    continue;
                uint32_t neighborKey = Key(x, z);
                if (FindNode(self + field::kSet84, neighborKey))
                    continue;
                char* const neighbor = Entry(self, neighborKey);
                if (!neighbor || At<uint8_t>(neighbor, cell::kImmovable) || At<uint8_t>(neighbor, cell::kKind3))
                    continue;
                Insert(self + field::kSet84, &neighborKey);
                At<uint8_t>(neighbor, solve::kMarked) = 1;
                if (frontier->end != frontier->capacity) {
                    if (frontier->end)
                        At<uint32_t>(frontier->end, 0) = neighborKey;
                    frontier->end += 4;
                } else {
                    uint32_t tag = 0;
                    CallThis<void, char*, uint32_t*, uint32_t*, uint32_t, uint32_t>(
                        addr::SC4PtrVector_InsertN, frontier, frontier->end, &neighborKey, &tag, 1u, 1u);
                }
            }
        }
        spareBegin = work.begin;
        spareCapacity = work.capacity;
        if (--left == 0)
            break;
    }
    FreeIfSet(work.begin);
}

// 0x0063B1A0 (ret 4: a cell set; this the tool; 0x006355C0's): for each
// intersection (+0xCC) until one of a step not below +0x78: its RUL0 rule
// (the store's FindOrInsert 0x0062C2F0 on its ID); each checked point of the
// rule (+0x4C) placed at the intersection: erased from the set when there,
// and appended to the intersection's key vector +0x1C.
void __fastcall CollectIntersectionCells(char* self, void*, char* set)
{
    static bool logged = false;
    FirstCall("CollectIntersectionCells", logged);
    char* const end = At<char*>(self, solve::kIntersectionEnd);
    for (char* at = At<char*>(self, field::kIntersections); at != end; at += solve::kIntersectionSize) {
        if (At<int32_t>(at, solve::kIntersectionStep) >= At<int32_t>(self, field::kValidSteps))
            return;
        const uint32_t id = At<uint32_t>(at, solve::kIntersectionRule);
        void* const store = reinterpret_cast<void*>(addr::cSC4NetworkTool_Rul0Store);
        char* const rule = CallThis<char*, const uint32_t*>(addr::SC4Rul0Store_FindOrInsert, store, &id);
        char* const pointsEnd = At<char*>(rule, solve::kRuleCheckedEnd);
        for (char* point = At<char*>(rule, solve::kRuleChecked); point != pointsEnd; point += solve::kRulePointSize) {
            uint32_t key = ((At<uint32_t>(point, 4) + At<uint32_t>(at, solve::kIntersectionZ)) << 16) +
                           At<uint32_t>(at, solve::kIntersectionX) + At<uint32_t>(point, 0);
            if (FindNode(set, key))
                Erase(set, &key);
            PushWord(At<Vector>(at, solve::kIntersectionCells), key);
        }
    }
}

// 0x00638BB0 (ret 4: a cell set; this the tool; 0x006355C0's): for each
// +0xE4 record until one whose last step is not below +0x78: the dragged
// cells (+0x60) of each step from its first step to its last (signed) erased
// from the set.
void __fastcall EraseRecordE4Cells(char* self, void*, char* set)
{
    static bool logged = false;
    FirstCall("EraseRecordE4Cells", logged);
    char* const end = At<char*>(self, solve::kRecordE4End);
    for (char* record = At<char*>(self, solve::kRecordsE4); record != end; record += solve::kRecordE4Size) {
        if (At<int32_t>(record, solve::kRecordLastStep) >= At<int32_t>(self, field::kValidSteps))
            return;
        for (int32_t s = At<int32_t>(record, solve::kRecordFirstStep); s <= At<int32_t>(record, solve::kRecordLastStep);
             ++s) {
            const std::size_t offset = static_cast<std::size_t>(s) * step::kSize;
            uint32_t i = At<uint32_t>(At<Vector>(self, field::kSteps).begin + offset, solve::kStepFirst);
            if (i > At<uint32_t>(At<Vector>(self, field::kSteps).begin + offset, solve::kStepLast))
                continue;
            do {
                char* const point = At<Vector>(self, field::kCells).begin + i * kPointSize;
                const uint32_t key = Key(At<uint32_t>(point, 0), At<uint32_t>(point, 4));
                Erase(set, &key);
                ++i;
            } while (i <= At<uint32_t>(At<Vector>(self, field::kSteps).begin + offset, solve::kStepLast));
        }
    }
}

// 0x00638B40 (ret 4: a cell set; this the tool; 0x006355C0's): for each
// major bridge (+0xD8) until one whose step is not below +0x78: each of its
// cells' keys erased from the set.
void __fastcall EraseBridgeCells(char* self, void*, char* set)
{
    static bool logged = false;
    FirstCall("EraseBridgeCells", logged);
    char* const end = At<char*>(self, solve::kBridgeEnd);
    for (char* bridge = At<char*>(self, field::kMajorBridges); bridge != end; bridge += solve::kBridgeSize) {
        if (At<int32_t>(bridge, solve::kBridgeStep) >= At<int32_t>(self, field::kValidSteps))
            return;
        char* const cellsEnd = At<char*>(bridge, solve::kBridgeCells + 4);
        for (char* at = At<char*>(bridge, solve::kBridgeCells); at != cellsEnd; at += solve::kBridgeCellSize) {
            const uint32_t key = Key(At<uint32_t>(at, solve::kBridgeCellX), At<uint32_t>(at, solve::kBridgeCellZ));
            Erase(set, &key);
        }
    }
}

// 0x006355C0 (no arguments; this the tool; called by the guarded 0x0062CD50
// and by 0x0063B830): a local vector of keys. Each dragged cell of each
// valid step (+0x54, +0x60; first to last, unsigned) whose cell entry (not
// checked for null) is not immovable: inserted into +0x84, marked (+0x57)
// and appended. Each rule cell record (+0xF0) inside the city: its cell
// entry marked, the key appended, then inserted into +0x84. Then
// SpreadConnected (the vector, 4), CollectIntersectionCells,
// EraseRecordE4Cells and EraseBridgeCells on +0x84; the vector's memory
// freed.
void __fastcall CollectDraggedCells(char* self, void*)
{
    static bool logged = false;
    FirstCall("CollectDraggedCells", logged);
    Vector keys{nullptr, nullptr, nullptr};
    char* const set = self + field::kSet84;
    for (int32_t s = 0; s < At<int32_t>(self, field::kValidSteps); ++s) {
        const std::size_t offset = static_cast<std::size_t>(s) * step::kSize;
        uint32_t i = At<uint32_t>(At<Vector>(self, field::kSteps).begin + offset, solve::kStepFirst);
        if (i > At<uint32_t>(At<Vector>(self, field::kSteps).begin + offset, solve::kStepLast))
            continue;
        do {
            char* const point = At<Vector>(self, field::kCells).begin + i * kPointSize;
            uint32_t key = Key(At<uint32_t>(point, 0), At<uint32_t>(point, 4));
            char* const entry = Entry(self, key);
            if (!At<uint8_t>(entry, cell::kImmovable)) {
                Insert(set, &key);
                At<uint8_t>(entry, solve::kMarked) = 1;
                PushWord(keys, key);
            }
            ++i;
        } while (i <= At<uint32_t>(At<Vector>(self, field::kSteps).begin + offset, solve::kStepLast));
    }
    char* const recordsEnd = At<char*>(self, solve::kRuleCellsEnd);
    for (char* record = At<char*>(self, field::kRuleCellRecords); record != recordsEnd;
         record += solve::kRuleCellSize) {
        const uint32_t x = At<uint32_t>(record, 0);
        if (x >= At<uint32_t>(self, field::kCellsX))
            continue;
        const uint32_t z = At<uint32_t>(record, 4);
        if (z >= At<uint32_t>(self, field::kCellsZ))
            continue;
        uint32_t key = Key(x, z);
        At<uint8_t>(Entry(self, key), solve::kMarked) = 1;
        PushWord(keys, key);
        Insert(set, &key);
    }
    CallThis<void, Vector*, int32_t>(addr::cSC4NetworkTool_SpreadConnected, self, &keys, 4);
    CallThis<void, char*>(addr::cSC4NetworkTool_CollectIntersectionCells, self, set);
    CallThis<void, char*>(addr::cSC4NetworkTool_EraseRecordE4Cells, self, set);
    CallThis<void, char*>(addr::cSC4NetworkTool_EraseBridgeCells, self, set);
    FreeIfSet(keys.begin);
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kTileFix[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_FixCellEdges, FixCellEdges),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_FixEdgesKind1, FixEdgesKind1),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_FixEdgesKind2, FixEdgesKind2),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_MarkDiagonalRuns, MarkDiagonalRuns),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_IsDiagonalEdge, IsDiagonalEdge),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_SplitSidesByKind, SplitSidesByKind),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_SetSideHighBits, SetSideHighBits),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_SetSideKind, SetSideKind),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_PatchTilePair, PatchTilePair),
};

const Detour kDragCells[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_CollectDraggedCells, CollectDraggedCells),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_SpreadConnected, SpreadConnected),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_CollectIntersectionCells, CollectIntersectionCells),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_EraseRecordE4Cells, EraseRecordE4Cells),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_EraseBridgeCells, EraseBridgeCells),
};

#undef OPENSC4_DETOUR

bool InstallTileFix(registry::Mode) { return InstallDetours(kTileFix, "tilefix"); }
bool InstallDragCells(registry::Mode) { return InstallDetours(kDragCells, "dragcells"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.tilefix", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_FixCellEdges,
                        "Our network tool's edge fix-ups before the rule files (FixCellEdges, the two kinds' "
                        "fix-ups, the diagonal runs and tests, the side byte helpers) and the RUL2 pair patch "
                        "PatchTilePair the guarded pass calls (9 detours; src/core/nettool_solve_2.cpp)",
                        false, InstallTileFix}),
    registry::Register({"simulators.nettool.dragcells", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_CollectDraggedCells,
                        "Our network tool's collection of a drag's cells for the solver: CollectDraggedCells, "
                        "SpreadConnected, and the intersections', +0xE4 records' and major bridges' cells (5 "
                        "detours; src/core/nettool_solve_2.cpp)",
                        false, InstallDragCells}),
};

} // namespace
} // namespace opensc4::nettool_sim
