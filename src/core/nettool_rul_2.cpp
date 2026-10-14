// Our cSC4NetworkTool, part r2 (build 641; the class map is in
// src/core/nettool_sim.h, the rule layouts in src/core/nettool_rul.h): the
// type table rows' rule files, the records of the per-network RUL text
// files that LoadTypeInfo 0x00623310 hands to AddRuleFile 0x0063D360 (two a
// row, row 5 none). Two groups, each a list of detours at the game's
// addresses:
//   simulators.nettool.rulfile: the rule file object (constructor 0x00622A90,
//     deleting destructor 0x00622AE0, Load 0x00623020), its text reader
//     (Construct, Destruct, Open, ReadLine, NextToken) and the three parsers
//     (edges, tile, entry) (11 detours);
//   simulators.nettool.rulrec: what they build and transform: the multimap's
//     node code (construct, destruct, erase, create node, insert, insert
//     range), the check and tile vectors' copies, assignments and growth,
//     the edge, check, tile and entry turns and mirrors, the entry compare
//     and Transmogrify 0x00622B50 (24 detours).
// Two groups because the reader and parsers touch the resource files (each
// row's two RUL files are read at every city load), while the record code
// is pure memory work that Transmogrify and, during drags, the tool's slot
// 72 (0x006278A0 mirrors an edge word with 0x00621210) run: each can be
// tried and bisected alone.
//
// Every function was written from the disassembly. Calls between them go to
// the game's addresses (through the address entries), so each detour is
// independent of the others and OPENSC4_NETTOOL_SKIP bisects them. Nothing
// of the part's 35 functions stays the game's.
//
// What these records are (evidence in nettool_rul.h): each type row's
// vector +0x24 holds rule file objects (0x10 bytes, vtable 0x00AA8338: one
// slot, the deleting destructor), each an STLport multimap keyed by an
// entry's edge word; the tile solver reads them (0x006358C0 from the loop at
// 0x00635D70, just before the guarded RUL2 engine). They are not the RUL2
// override multimap 0x00B466D0 (its own nodes, eraser 0x00624CE0), not the
// RUL1 map 0x00B460DC and not the row's exemplar table +0x3C.
//
// NAM. The guarded RUL2 engine 0x00634D70 and line callback 0x0063D1E0 are
// not called here. NAM's DLL reads the type rows by layout and its
// Rul2Engine replaces the RUL2 pass after the rule file pass: the rule file
// objects, their multimaps, nodes and vectors stay in the game's memory with
// the game's layout, allocated through the game's allocators (SC4::
// stl_allocate, SC4::Free, operator delete) in the game's order, and built
// through the game's template helpers where the game calls them (vector
// copies 0x006143E0, 0x00621580, 0x00463DF0, growth 0x00621A40, the tree's
// rebalance 0x0040D5E0, the string code); nothing is kept across calls. No
// wrapper of src/core/game_*.cpp or *fix*.cpp detours or tests return
// addresses in 0x006210B0 to 0x00623260 (game_quiet.cpp names 0x006208D0,
// outside); sim_timers.cpp has nothing here.
//
// Floats: none. No x87 instruction in the part.
//
// Registers. The scan of every direct caller (ECX, EDX and EAX after the
// call, both branches) found no read of ECX or EDX after any of these
// functions (the flagged sites write CL first); the deleting destructor's
// only caller (the row's Clear, through slot 0) reloads both. So all are
// plain detours. EAX is the game's where callers read it: Open, Equals,
// ReadLine, the tile and entry parsers and Load (AL), the two
// UninitializedCopy/Fill (the end), the two AllocateCopy (the buffer),
// CreateNode (the node), NextToken (the token), the constructors (this), and
// also where the game returns something nobody reads (the copy constructors,
// assignments, Insert, the deleting destructor).
//
// Stack leftovers the game stores (ours start zeroed):
//   - Transmogrify's eight variant slots are zeroed but for their mask word
//     +0x1C, which the game sets to 1 for a kept variant and, when a variant
//     is a duplicate, to the mask of an "empty entry" on its stack that it
//     never wrote; its working entry's mask is never written on the mirror
//     path either. No code reads any of these masks (Rotate 0x00621F30 and
//     Equals 0x006214A0 do not; the inserted values get mask 1).
//   - The checks and tiles the parsers, Mirror and Rotate build on the stack
//     leave bytes +1 to +3 (checks) and +6, +7 (tiles) unwritten; every
//     copy of them (the in-place appends, 0x00621A40, 0x00621B40) copies the
//     byte, word, word or word, byte, byte fields only, so none reaches the
//     vectors. The vectors' own padding bytes are heap leftovers that the
//     word copies 0x006143E0 / 0x00463DF0 carry along (in game code, the
//     same in A and B runs for the same heap); nothing compares them.
// Other stores into the game's stack: Insert's result goes to InsertRange's
// first-argument slot, and the assignments overwrite their argument slot
// with the source's end (both never read again; ours use locals).
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the part's own, at the game's addresses: Open one (ret 4); Construct,
//     Destruct, NextToken, the map's Construct and Destruct none; ReadLine
//     cdecl two; the parsers one (ret 4); the copy constructors and
//     assignments one (ret 4); the two AllocateCopy three (ret 0xC);
//     UninitializedCopy and UninitializedFill cdecl four; the tile vector's
//     InsertOverflow five (ret 0x14), PushBack one (ret 4); Mirror (edges,
//     entry) one (ret 4); Rotate (edges, check, tile, entry) two (ret 8);
//     Equals one (ret 4); EraseSubtree one (ret 4, recursive); CreateNode one
//     (ret 4); Insert five (ret 0x14); InsertRange two (ret 8);
//     Transmogrify three (ret 0xC);
//   - SC4::OpenResourceRecord 0x005F9670 cdecl three (key, record out, 0),
//     SC4::operator_delete 0x005E5620 cdecl one; the record (cIGZFile): slot
//     0x8 Release, 0xC Open three (1, 2, 1), 0x10 IsOpen, 0x18 Position, 0x1C
//     Length none, 0x2C SeekToRelativePosition one (-1), 0x30
//     SeekToPosition one, 0x38 Read two (buffer, count); a cIGZString (vtable
//     0x00A80810): slot 0xC two (text, length; ret 8), slot 0x3C one (0; ret
//     4);
//   - the string code: SC4String::SplitOff 0x0090E98C two (ret 8: out, the
//     separator ","), SC4String::AssignRange 0x00408A70 three (ret 0xC:
//     first, last, tag), the append 0x0040EBC0 three (ret 0xC: first, last,
//     tag), the reserve 0x0040F1B0 one (ret 4), cRZBaseString::
//     AssignFromRange 0x0090FBE5 cdecl two (range, target);
//   - the C runtime: SC4::atol 0x009EF84A, SC4::isdigit 0x009EF61D,
//     SC4::TextToUint32 0x0090FE93, cdecl one;
//   - template code: SC4::stl_allocate 0x0090CF54 and SC4::Free 0x0090CF63
//     cdecl one; SC4Vector12::Copy 0x006143E0 cdecl five (first, last,
//     destination, tag, 0); 0x00621580 and SC4Vector8::Copy 0x00463DF0 cdecl
//     four; the check vector's InsertOverflow 0x00621A40 five (ret 0x14);
//     SC4Tree::RebalanceForInsert 0x0040D5E0 cdecl two (node, root*).
#include "core/nettool_rul.h"
#include "core/nettool_sim.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim {
namespace {

using namespace rul;

// The game's allocator and free (cdecl, one argument).
char* Allocate(uint32_t size)
{
    return CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, size);
}
void Free(void* block)
{
    CallCdecl<void, void*>(addr::SC4_Free, block);
}
// The pattern `if (p) Free(p)` the game inlines everywhere.
void FreeIfSet(void* block)
{
    if (block)
        Free(block);
}

// Data the part reads in the exe: the keywords (C strings in .rdata), the
// token separator, the flipped tiles' turn table (4 x 4 bytes) and the three
// position tables in .data (x and z of each position, and the position of
// each 5 * x - z).
inline constexpr std::uintptr_t kTransmogrify = 0x00AA834C; // "#Transmogrify", 14 bytes with its 0
inline constexpr std::uintptr_t kNoFlip = 0x00AA8344; // "noflip", 7
inline constexpr std::uintptr_t kNoTrans = 0x00AA833C; // "notrans", 8
inline constexpr std::uintptr_t kComma = 0x00A99F14; // ","
inline constexpr std::uintptr_t kFlippedTurns = 0x00AA8328;
inline constexpr std::uintptr_t kPositionX = 0x00B0B3F4, kPositionZ = 0x00B0B3D8, kPositionAt = 0x00B0B3C8;

// cIGZFile slots (the record SC4::OpenResourceRecord gives) and the two
// string slots used here.
namespace file_slot {
inline constexpr std::size_t kOpen = 0x0C, kIsOpen = 0x10, kPosition = 0x18, kLength = 0x1C;
inline constexpr std::size_t kSeekToRelativePosition = 0x2C, kSeekToPosition = 0x30, kRead = 0x38;
} // namespace file_slot
inline constexpr std::size_t kStringSlot3C = 0x3C;

// REPZ CMPSB over n bytes: equal when no byte differs (stops at the first
// difference, so it never reads past it).
bool SameBytes(const char* a, std::uintptr_t b, int n)
{
    const char* const other = reinterpret_cast<const char*>(b);
    for (int i = 0; i < n; ++i) {
        if (a[i] != other[i])
            return false;
    }
    return true;
}

// The signed remainder the game computes with AND 0x80000003 / 7 / 15, JNS,
// DEC, OR, INC (C's %).
int32_t SignedRemainder(uint32_t value, int32_t divisor)
{
    return static_cast<int32_t>(value) % divisor;
}

// The element copies the game inlines: a check (byte +0, words +4 and +8)
// and a tile (byte +4, word +0, byte +5), in its order.
void CopyCheck(char* to, const char* from)
{
    At<uint8_t>(to, check::kPosition) = At<uint8_t>(const_cast<char*>(from), check::kPosition);
    At<uint32_t>(to, check::kEdges) = At<uint32_t>(const_cast<char*>(from), check::kEdges);
    At<uint32_t>(to, check::kMask) = At<uint32_t>(const_cast<char*>(from), check::kMask);
}
void CopyTile(char* to, const char* from)
{
    At<uint8_t>(to, tile::kPosition) = At<uint8_t>(const_cast<char*>(from), tile::kPosition);
    At<uint32_t>(to, tile::kId) = At<uint32_t>(const_cast<char*>(from), tile::kId);
    At<uint8_t>(to, tile::kTurn) = At<uint8_t>(const_cast<char*>(from), tile::kTurn);
}

// The element counts: a check vector's by a signed division by 12 (the
// game's multiply by 0x2AAAAAAB), a tile vector's by an arithmetic shift.
int32_t CheckCount(const Vector& v)
{
    return static_cast<int32_t>(v.end - v.begin) / static_cast<int32_t>(check::kSize);
}
int32_t TileCount(const Vector& v)
{
    return static_cast<int32_t>(v.end - v.begin) >> 3;
}

// The appends the game inlines: in place (the writes skipped for a null
// end) when there is room, else the vector's InsertOverflow (where, value*,
// tag*, 1, 1) at the game's address.
void AppendCheck(Vector* v, char* value, void* tag)
{
    if (v->end != v->capacity) {
        if (v->end)
            CopyCheck(v->end, value);
        v->end += check::kSize;
        return;
    }
    CallThis<void, char*, char*, void*, uint32_t, uint32_t>(addr::SC4RuleCheckVector_InsertOverflow, v, v->end, value,
                                                            tag, 1u, 1u);
}
void AppendTile(Vector* v, char* value, void* tag)
{
    if (v->end != v->capacity) {
        if (v->end)
            CopyTile(v->end, value);
        v->end += tile::kSize;
        return;
    }
    CallThis<void, char*, char*, void*, uint32_t, uint32_t>(addr::SC4RuleTileVector_InsertOverflow, v, v->end, value,
                                                            tag, 1u, 1u);
}

// The next token of a reader (NextToken at its address).
const char* Next(char* reader)
{
    return CallThis<const char*>(addr::SC4RulReader_NextToken, reader);
}
int32_t Atol(const char* text)
{
    return CallCdecl<int32_t, const char*>(addr::SC4_atol, text);
}

// A multimap value (0x24 bytes) copied into a node's value as CreateNode and
// Insert's inlined copy do: the key, the edges, the two vectors by their
// copy constructors at the game's addresses, the mask.
void CopyValue(char* to, char* from)
{
    At<uint32_t>(to, rulevalue::kKey) = At<uint32_t>(from, rulevalue::kKey);
    At<uint32_t>(to, rulevalue::kEntry + entry::kEdges) = At<uint32_t>(from, rulevalue::kEntry + entry::kEdges);
    CallThis<char*, char*>(addr::SC4RuleCheckVector_CopyConstruct, to + rulevalue::kEntry + entry::kChecks,
                           from + rulevalue::kEntry + entry::kChecks);
    CallThis<char*, char*>(addr::SC4RuleTileVector_CopyConstruct, to + rulevalue::kEntry + entry::kTiles,
                           from + rulevalue::kEntry + entry::kTiles);
    At<uint32_t>(to, rulevalue::kEntry + entry::kMask) = At<uint32_t>(from, rulevalue::kEntry + entry::kMask);
}

// The multimap {head, count} emptied as the destructors inline it: when it
// has nodes, the root's subtree erased (EraseSubtree at its address), the
// head linked to itself and the count 0; then the head freed.
void DestroyMap(char* map)
{
    if (At<uint32_t>(map, tree::kCount)) {
        CallThis<void, char*>(addr::SC4RuleFileMap_EraseSubtree, map,
                              At<char*>(At<char*>(map, tree::kHead), tree::kParent));
        At<char*>(At<char*>(map, tree::kHead), tree::kLeft) = At<char*>(map, tree::kHead);
        At<char*>(At<char*>(map, tree::kHead), tree::kParent) = nullptr;
        At<char*>(At<char*>(map, tree::kHead), tree::kRight) = At<char*>(map, tree::kHead);
        At<uint32_t>(map, tree::kCount) = 0;
    }
    FreeIfSet(At<char*>(map, tree::kHead));
}

// The parent of a new node for insert_equal: from the head down, left when
// the key is below the node's (unsigned), else right. `x` gets the null
// child the walk ends on.
char* EqualParent(char* map, uint32_t key, char*& x)
{
    char* y = At<char*>(map, tree::kHead);
    x = At<char*>(y, tree::kParent);
    while (x) {
        y = x;
        const bool below = key < At<uint32_t>(x, tree::kValue + rulevalue::kKey);
        x = below ? At<char*>(x, tree::kLeft) : At<char*>(x, tree::kRight);
    }
    return y;
}

// The in-order successor (STLport's _M_increment, inlined by the game).
char* Successor(char* node)
{
    if (char* right = At<char*>(node, tree::kRight)) {
        while (char* const left = At<char*>(right, tree::kLeft))
            right = left;
        return right;
    }
    char* x = node;
    char* y = At<char*>(x, tree::kParent);
    while (x == At<char*>(y, tree::kRight)) {
        x = y;
        y = At<char*>(y, tree::kParent);
    }
    if (At<char*>(x, tree::kRight) != y)
        x = y;
    return x;
}

// A reader closed as the game inlines it (and Destruct starts): the open
// byte cleared; when there is a record, it is released, the record slot and
// the open answer cleared.
void CloseReader(char* reader)
{
    if (!At<uint8_t>(reader, reader::kOpen))
        return;
    void* const record = At<void*>(reader, reader::kRecord);
    At<uint8_t>(reader, reader::kOpen) = 0;
    if (!record)
        return;
    ReleaseOf(record);
    At<void*>(reader, reader::kRecord) = nullptr;
    At<uint8_t>(reader, reader::kOpened) = 0;
}

// ====================================================================================
// simulators.nettool.rulrec: the records' transforms
// ====================================================================================

// A position turned (0x00621180 and 0x00621680 inline it): 0 stays; 1 to 8
// move by 2 a turn in the ring of 8, 9 to 24 by 4 in the ring of 16 (signed
// remainders); 25 and up stay.
uint8_t TurnPosition(uint8_t position, uint32_t turns)
{
    if (position == 0)
        return 0;
    if (position < 9)
        return static_cast<uint8_t>(SignedRemainder(position + turns * 2 - 1, 8) + 1);
    if (position < 0x19)
        return static_cast<uint8_t>(SignedRemainder(position + turns * 4 - 9, 16) + 9);
    return position;
}

// 0x00621180 (ret 8: quarter turns, out*): out's position the turned one,
// its ID copied; the turn byte: not flipped, (low 7 bits + turns) % 4
// (signed); flipped, 0x80 | the table 0x00AA8328's byte at turns + 4 * low 7
// bits.
void __fastcall RotateTile(char* self, void*, uint32_t turns, char* out)
{
    static bool logged = false;
    FirstCall("RotateTile", logged);
    At<uint8_t>(out, tile::kPosition) = TurnPosition(At<uint8_t>(self, tile::kPosition), turns);
    At<uint32_t>(out, tile::kId) = At<uint32_t>(self, tile::kId);
    const uint8_t turn = At<uint8_t>(self, tile::kTurn);
    const uint32_t low = turn & 0x7F;
    const uint8_t flipped = turn & tile::kFlipped;
    uint8_t value;
    if (!flipped)
        value = static_cast<uint8_t>(SignedRemainder(low + turns, 4));
    else
        value = *reinterpret_cast<const uint8_t*>(kFlippedTurns + turns + low * 4);
    At<uint8_t>(out, tile::kTurn) = static_cast<uint8_t>(flipped | value);
}

// An edge byte mirrored: the low three bits 1 and 3 swapped; for 2, of the
// upper five bits only 0x8 kept and 0x10 and 0x20 swapped (0x40 and 0x80
// dropped); the bit 0x4 kept.
uint8_t MirrorEdgeByte(uint8_t b)
{
    uint8_t low = b & 7;
    const uint8_t four = b & 4;
    uint8_t high = b & 0xF8;
    if (low == 1) {
        low = 3;
    } else if (low == 3) {
        low = 1;
    } else if (low == 2) {
        high = static_cast<uint8_t>((high & 8) | ((high >> 1) & 0x10) | ((high & 0x10) << 1));
    }
    return static_cast<uint8_t>(high | low | four);
}

// 0x00621210 (ret 4: out*): out = 0, then byte by byte in the game's order
// (each read from this when the game reads it): byte 2 of out from byte 0,
// byte 1 from byte 1, byte 0 from byte 2, byte 3 from byte 3, each mirrored.
void __fastcall MirrorEdges(char* self, void*, uint32_t* out)
{
    static bool logged = false;
    FirstCall("MirrorEdges", logged);
    *out = 0;
    *out = static_cast<uint32_t>(MirrorEdgeByte(At<uint8_t>(self, 0))) << 16;
    *out = (*out & 0xFFFF00FFu) | (static_cast<uint32_t>(MirrorEdgeByte(At<uint8_t>(self, 1))) << 8);
    *out = (*out & 0xFFFFFF00u) | MirrorEdgeByte(At<uint8_t>(self, 2));
    *out = (*out & 0x00FFFFFFu) | (static_cast<uint32_t>(MirrorEdgeByte(At<uint8_t>(self, 3))) << 24);
}

// The shift of byte k after `turns` quarter turns: ((turns + k) % 4 signed)
// * 8, as SHL by CL masks it.
uint32_t TurnShift(uint32_t turns, uint32_t k)
{
    return (static_cast<uint32_t>(SignedRemainder(turns + k, 4)) << 3) & 31;
}

// 0x00621380 (ret 8: quarter turns, out*): out = 0; out = byte 0 << its
// shift; then bytes 1, 2, 3 put in their places (the byte there replaced).
void __fastcall RotateEdges(char* self, void*, uint32_t turns, uint32_t* out)
{
    static bool logged = false;
    FirstCall("RotateEdges", logged);
    *out = 0;
    *out = static_cast<uint32_t>(At<uint8_t>(self, 0)) << TurnShift(turns, 0);
    for (uint32_t k = 1; k < 4; ++k) {
        const uint32_t shift = TurnShift(turns, k);
        const uint32_t b = At<uint8_t>(self, k);
        *out = (~(0xFFu << shift) & *out) | (b << shift);
    }
}

// 0x006214A0 (ret 4: the other entry*; AL): the edges equal, every check of
// this has one in the other with the same position and edges, and every one
// of the other's has one in this (masks and tiles not compared).
uint8_t __fastcall EntryEquals(char* self, void*, char* other)
{
    static bool logged = false;
    FirstCall("EntryEquals", logged);
    if (At<uint32_t>(other, entry::kEdges) != At<uint32_t>(self, entry::kEdges))
        return 0;
    const Vector& mine = At<Vector>(self, entry::kChecks);
    const Vector& theirs = At<Vector>(other, entry::kChecks);
    const auto contains = [](const Vector& in, char* c) {
        for (char* e = in.begin; e != in.end; e += check::kSize) {
            if (At<uint8_t>(e, check::kPosition) == At<uint8_t>(c, check::kPosition) &&
                At<uint32_t>(c, check::kEdges) == At<uint32_t>(e, check::kEdges))
                return true;
        }
        return false;
    };
    for (char* c = mine.begin; c != mine.end; c += check::kSize) {
        if (!contains(theirs, c))
            return 0;
    }
    for (char* c = theirs.begin; c != theirs.end; c += check::kSize) {
        if (!contains(mine, c))
            return 0;
    }
    return 1;
}

// 0x00621680 (ret 8: quarter turns, out*): out's position the turned one,
// its edges and mask turned by 0x00621380 at its address.
void __fastcall RotateCheck(char* self, void*, uint32_t turns, char* out)
{
    static bool logged = false;
    FirstCall("RotateCheck", logged);
    At<uint8_t>(out, check::kPosition) = TurnPosition(At<uint8_t>(self, check::kPosition), turns);
    CallThis<void, uint32_t, char*>(addr::SC4RuleEdges_Rotate, self + check::kEdges, turns, out + check::kEdges);
    CallThis<void, uint32_t, char*>(addr::SC4RuleEdges_Rotate, self + check::kMask, turns, out + check::kMask);
}

// A position mirrored through the exe's tables: x and z (signed bytes) of
// the position, then the position at 5 * x - z (a signed index).
uint8_t MirrorPosition(uint8_t position)
{
    const int32_t x = *reinterpret_cast<const int8_t*>(kPositionX + position);
    const int32_t z = *reinterpret_cast<const int8_t*>(kPositionZ + position);
    const int32_t index = x * 5 - z;
    return *reinterpret_cast<const uint8_t*>(kPositionAt + static_cast<uint32_t>(index));
}

// 0x00621DF0 (ret 4: the out entry*): the edges mirrored into out's
// (0x00621210 at its address); each check appended to out's with its
// position mirrored and its edges and mask mirrored (0x00621210); each tile
// appended to out's with its position mirrored and its flip bit toggled.
void __fastcall MirrorEntry(char* self, void*, char* out)
{
    static bool logged = false;
    FirstCall("MirrorEntry", logged);
    CallThis<void, char*>(addr::SC4RuleEdges_Mirror, self + entry::kEdges, out + entry::kEdges);
    char* tag = out;
    {
        const Vector& checks = At<Vector>(self, entry::kChecks);
        char* const end = checks.end;
        for (char* c = checks.begin; c != end; c += check::kSize) {
            alignas(4) char value[check::kSize] = {};
            At<uint8_t>(value, check::kPosition) = MirrorPosition(At<uint8_t>(c, check::kPosition));
            CallThis<void, char*>(addr::SC4RuleEdges_Mirror, c + check::kEdges, value + check::kEdges);
            CallThis<void, char*>(addr::SC4RuleEdges_Mirror, c + check::kMask, value + check::kMask);
            AppendCheck(&At<Vector>(out, entry::kChecks), value, &tag);
        }
    }
    const Vector& tiles = At<Vector>(self, entry::kTiles);
    char* const end = tiles.end;
    for (char* t = tiles.begin; t != end; t += tile::kSize) {
        alignas(4) char value[tile::kSize] = {};
        const uint8_t position = MirrorPosition(At<uint8_t>(t, tile::kPosition));
        At<uint32_t>(value, tile::kId) = At<uint32_t>(t, tile::kId);
        At<uint8_t>(value, tile::kPosition) = position;
        At<uint8_t>(value, tile::kTurn) = static_cast<uint8_t>(At<uint8_t>(t, tile::kTurn) ^ tile::kFlipped);
        AppendTile(&At<Vector>(out, entry::kTiles), value, &tag);
    }
}

// 0x00621F30 (ret 8: quarter turns, the out entry*): the edges turned into
// out's (0x00621380 at its address); each check turned (0x00621680) into a
// zeroed one and appended to out's; each tile turned (0x00621180) into a
// zeroed one and appended to out's.
void __fastcall RotateEntry(char* self, void*, uint32_t turns, char* out)
{
    static bool logged = false;
    FirstCall("RotateEntry", logged);
    CallThis<void, uint32_t, char*>(addr::SC4RuleEdges_Rotate, self + entry::kEdges, turns, out + entry::kEdges);
    uint32_t tag = turns;
    {
        const Vector& checks = At<Vector>(self, entry::kChecks);
        char* const end = checks.end;
        for (char* c = checks.begin; c != end; c += check::kSize) {
            alignas(4) char value[check::kSize] = {};
            CallThis<void, uint32_t, char*>(addr::SC4RuleCheck_Rotate, c, turns, value);
            AppendCheck(&At<Vector>(out, entry::kChecks), value, &tag);
        }
    }
    const Vector& tiles = At<Vector>(self, entry::kTiles);
    char* const end = tiles.end;
    for (char* t = tiles.begin; t != end; t += tile::kSize) {
        alignas(4) char value[tile::kSize] = {};
        CallThis<void, uint32_t, char*>(addr::SC4RuleTile_Rotate, t, turns, value);
        AppendTile(&At<Vector>(out, entry::kTiles), value, &tag);
    }
}

// ====================================================================================
// simulators.nettool.rulrec: the check and tile vectors
// ====================================================================================

// 0x00621110 (cdecl: first, last, destination, tag; EAX the end).
char* __cdecl TileUninitializedCopy(const char* first, const char* last, char* destination, void*)
{
    static bool logged = false;
    FirstCall("TileUninitializedCopy", logged);
    for (; first != last; first += tile::kSize, destination += tile::kSize) {
        if (destination)
            CopyTile(destination, first);
    }
    return destination;
}

// 0x00621150 (cdecl: destination, count, value*, tag; EAX the end).
char* __cdecl TileUninitializedFill(char* destination, uint32_t count, const char* value, void*)
{
    static bool logged = false;
    FirstCall("TileUninitializedFill", logged);
    for (; count != 0; --count, destination += tile::kSize) {
        if (destination)
            CopyTile(destination, value);
    }
    return destination;
}

// 0x006215C0 (ret 0xC: count, first, last; EAX the buffer): count checks
// from the STL allocator (none for 0), the range copied into them.
char* __fastcall CheckAllocateCopy(void*, void*, uint32_t count, const char* first, const char* last)
{
    static bool logged = false;
    FirstCall("CheckAllocateCopy", logged);
    char* const buffer = count ? Allocate(count * check::kSize) : nullptr;
    char* to = buffer;
    for (; first != last; first += check::kSize, to += check::kSize) {
        if (to)
            CopyCheck(to, first);
    }
    return buffer;
}

// 0x00621620 (ret 0xC: count, first, last; EAX the buffer): the same for
// tiles.
char* __fastcall TileAllocateCopy(void*, void*, uint32_t count, const char* first, const char* last)
{
    static bool logged = false;
    FirstCall("TileAllocateCopy", logged);
    char* const buffer = count ? Allocate(count * tile::kSize) : nullptr;
    char* to = buffer;
    for (; first != last; first += tile::kSize, to += tile::kSize) {
        if (to)
            CopyTile(to, first);
    }
    return buffer;
}

// 0x00621700 (ret 4: the source; EAX this): this emptied, a buffer of the
// source's count of checks (none for 0), the checks copied.
char* __fastcall CheckCopyConstruct(char* self, void*, Vector* source)
{
    static bool logged = false;
    FirstCall("CheckCopyConstruct", logged);
    const int32_t count = CheckCount(*source);
    Vector& v = *reinterpret_cast<Vector*>(self);
    v.begin = v.end = v.capacity = nullptr;
    char* const buffer = count ? Allocate(static_cast<uint32_t>(count) * check::kSize) : nullptr;
    v.begin = buffer;
    v.end = buffer;
    v.capacity = buffer + static_cast<uint32_t>(count) * check::kSize;
    char* to = buffer;
    for (const char* from = source->begin; from != source->end; from += check::kSize, to += check::kSize) {
        if (to)
            CopyCheck(to, from);
    }
    v.end = to;
    return self;
}

// 0x00621790 (ret 4: the source; EAX this): the same for tiles.
char* __fastcall TileCopyConstruct(char* self, void*, Vector* source)
{
    static bool logged = false;
    FirstCall("TileCopyConstruct", logged);
    const int32_t count = TileCount(*source);
    Vector& v = *reinterpret_cast<Vector*>(self);
    v.begin = v.end = v.capacity = nullptr;
    char* const buffer = count ? Allocate(static_cast<uint32_t>(count) * tile::kSize) : nullptr;
    v.begin = buffer;
    v.end = buffer;
    v.capacity = buffer + static_cast<uint32_t>(count) * tile::kSize;
    char* to = buffer;
    for (const char* from = source->begin; from != source->end; from += tile::kSize, to += tile::kSize) {
        if (to)
            CopyTile(to, from);
    }
    v.end = to;
    return self;
}

// 0x00621850 (ret 4: the source; EAX this): vector operator=. Nothing for
// itself. More than this's capacity: a new buffer (0x006215C0), the old one
// freed, capacity and end the count. Else when this has at least as many:
// SC4Vector12::Copy over the start, the end moved; else the first ones
// copied over (SC4Vector12::Copy) and the rest copied after (0x00621580).
// Counts compared unsigned.
char* __fastcall CheckAssign(char* self, void*, Vector* source)
{
    static bool logged = false;
    FirstCall("CheckAssign", logged);
    Vector& v = *reinterpret_cast<Vector*>(self);
    if (source == &v)
        return self;
    char* tag = source->end; // the game keeps the source's end in its argument slot and passes that slot
    const uint32_t count = static_cast<uint32_t>(CheckCount(*source));
    char* const begin = v.begin;
    const uint32_t capacity = static_cast<uint32_t>(
        static_cast<int32_t>(v.capacity - v.begin) / static_cast<int32_t>(check::kSize));
    if (count > capacity) {
        char* const buffer = CallThis<char*, uint32_t, char*, char*>(addr::SC4RuleCheckVector_AllocateCopy, self,
                                                                     count, source->begin, tag);
        FreeIfSet(v.begin);
        v.capacity = buffer + count * check::kSize;
        v.begin = buffer;
        v.end = buffer + count * check::kSize;
        return self;
    }
    const uint32_t size = static_cast<uint32_t>(CheckCount(v));
    if (size >= count) {
        CallCdecl<char*, char*, char*, char*, char**, uint32_t>(addr::SC4Vector12_Copy, source->begin, tag, begin,
                                                                &tag, 0u);
        v.end = v.begin + count * check::kSize;
        return self;
    }
    char* const middle = source->begin + size * check::kSize;
    CallCdecl<char*, char*, char*, char*, char**, uint32_t>(addr::SC4Vector12_Copy, source->begin, middle, begin, &tag,
                                                            0u);
    const uint32_t now = static_cast<uint32_t>(CheckCount(v));
    CallCdecl<char*, char*, char*, char*, char**>(addr::SC4RuleCheckVector_UninitializedCopy,
                                                  source->begin + now * check::kSize, source->end, v.end, &tag);
    v.end = v.begin + count * check::kSize;
    return self;
}

// 0x00621970 (ret 4: the source; EAX this): the same for tiles (new buffer
// 0x00621620; copies SC4Vector8::Copy 0x00463DF0 and 0x00621110).
char* __fastcall TileAssign(char* self, void*, Vector* source)
{
    static bool logged = false;
    FirstCall("TileAssign", logged);
    Vector& v = *reinterpret_cast<Vector*>(self);
    if (source == &v)
        return self;
    char* tag = nullptr;
    char* const first = source->begin;
    char* const begin = v.begin;
    char* const last = source->end;
    const uint32_t count = static_cast<uint32_t>(static_cast<int32_t>(last - first) >> 3);
    const uint32_t capacity = static_cast<uint32_t>(static_cast<int32_t>(v.capacity - begin) >> 3);
    if (count > capacity) {
        char* const buffer = CallThis<char*, uint32_t, char*, char*>(addr::SC4RuleTileVector_AllocateCopy, self,
                                                                     count, first, last);
        FreeIfSet(v.begin);
        v.begin = buffer;
        v.capacity = buffer + count * tile::kSize;
        v.end = buffer + count * tile::kSize;
        return self;
    }
    const uint32_t size = static_cast<uint32_t>(TileCount(v));
    if (size >= count) {
        CallCdecl<void, char*, char*, char*, char**>(addr::SC4Vector8_Copy, first, last, begin, &tag);
        v.end = v.begin + count * tile::kSize;
        return self;
    }
    CallCdecl<void, char*, char*, char*, char**>(addr::SC4Vector8_Copy, first, first + size * tile::kSize, begin, &tag);
    const uint32_t now = static_cast<uint32_t>(TileCount(v));
    CallCdecl<char*, char*, char*, char*, char**>(addr::SC4RuleTileVector_UninitializedCopy,
                                                  source->begin + now * tile::kSize, source->end, v.end, &tag);
    v.end = v.begin + count * tile::kSize;
    return self;
}

// 0x00621B40 (ret 0x14: where, value*, tag*, count, a byte: inserting at
// the end): a new buffer of size + max(size, count) tiles (none for 0; the
// size by an arithmetic shift, compared unsigned); the tiles before `where`
// copied (inline), the count copies (inline for 1, else 0x00621150), the
// rest (0x00621110) unless at the end; the old buffer freed.
void __fastcall TileInsertOverflow(char* self, void*, char* where, const char* value, void*, uint32_t count,
                                   uint32_t atEnd)
{
    static bool logged = false;
    FirstCall("TileInsertOverflow", logged);
    Vector& v = *reinterpret_cast<Vector*>(self);
    const uint32_t size = static_cast<uint32_t>(TileCount(v));
    const uint32_t capacity = size + (size < count ? count : size);
    char* const buffer = capacity ? Allocate(capacity * tile::kSize) : nullptr;
    char* to = buffer;
    for (const char* from = v.begin; from != where; from += tile::kSize, to += tile::kSize) {
        if (to)
            CopyTile(to, from);
    }
    char* end;
    if (count == 1) {
        if (to)
            CopyTile(to, value);
        end = to + tile::kSize;
    } else {
        end = CallCdecl<char*, char*, uint32_t, const char*, uint32_t*>(addr::SC4RuleTileVector_UninitializedFill, to,
                                                                       count, value, &atEnd);
    }
    if (!(atEnd & 0xFF))
        end = CallCdecl<char*, char*, char*, char*, uint32_t*>(addr::SC4RuleTileVector_UninitializedCopy, where, v.end,
                                                               end, &atEnd);
    FreeIfSet(v.begin);
    v.begin = buffer;
    v.end = end;
    v.capacity = buffer + capacity * tile::kSize;
}

// 0x00621CF0 (ret 4: value*): a tile appended (in place, or 0x00621B40 at
// its address with the argument's slot as the tag).
void __fastcall TilePushBack(char* self, void*, char* value)
{
    static bool logged = false;
    FirstCall("TilePushBack", logged);
    AppendTile(reinterpret_cast<Vector*>(self), value, &value);
}

// ====================================================================================
// simulators.nettool.rulrec: the multimap
// ====================================================================================

// 0x00622190 (ret 4: a node): for each node down the left spine: its right
// subtree by this function at its address, its tile and check buffers freed,
// the node freed.
void __fastcall MapEraseSubtree(char* self, void*, char* node)
{
    static bool logged = false;
    FirstCall("MapEraseSubtree", logged);
    constexpr std::size_t kChecks = tree::kValue + rulevalue::kEntry + entry::kChecks;
    constexpr std::size_t kTiles = tree::kValue + rulevalue::kEntry + entry::kTiles;
    while (node) {
        CallThis<void, char*>(addr::SC4RuleFileMap_EraseSubtree, self, At<char*>(node, tree::kRight));
        char* const left = At<char*>(node, tree::kLeft);
        FreeIfSet(At<char*>(node, kTiles));
        FreeIfSet(At<char*>(node, kChecks));
        Free(node);
        node = left;
    }
}

// 0x006221F0 (ret 4: value*; EAX the node): a 0x34-byte node from the STL
// allocator holding a copy of the value; its links are not written.
char* __fastcall MapCreateNode(char*, void*, char* value)
{
    static bool logged = false;
    FirstCall("MapCreateNode", logged);
    char* const node = Allocate(rulevalue::kNodeSize);
    // The game tests node + 0x10 against null here (always true).
    CopyValue(node + tree::kValue, value);
    return node;
}

// 0x00622460 (ret 0x14: result*, x, y, value*, w; EAX result): STLport's
// _M_insert. Left of y when y is the head, or w is null and (x is not null
// or the value's key is below y's): a node made inline (as CreateNode), y's
// left; the head's root and rightmost when y is the head, else its leftmost
// when y was. Else right: CreateNode at its address, y's right, the head's
// rightmost when y was. Then the node's parent y, children null, the
// rebalance (0x0040D5E0 with the root's slot), the count + 1.
char** __fastcall MapInsert(char* self, void*, char** result, char* x, char* y, char* value, char* w)
{
    static bool logged = false;
    FirstCall("MapInsert", logged);
    char* node;
    const bool left = y == At<char*>(self, tree::kHead) ||
                      (!w && (x || At<uint32_t>(value, rulevalue::kKey) <
                                       At<uint32_t>(y, tree::kValue + rulevalue::kKey)));
    if (left) {
        node = Allocate(rulevalue::kNodeSize);
        // The game tests node + 0x10 against null here (always true).
        CopyValue(node + tree::kValue, value);
        At<char*>(y, tree::kLeft) = node;
        char* const head = At<char*>(self, tree::kHead);
        if (y == head) {
            At<char*>(head, tree::kParent) = node;
            At<char*>(At<char*>(self, tree::kHead), tree::kRight) = node;
        } else if (y == At<char*>(head, tree::kLeft)) {
            At<char*>(head, tree::kLeft) = node;
        }
    } else {
        node = CallThis<char*, char*>(addr::SC4RuleFileMap_CreateNode, self, value);
        At<char*>(y, tree::kRight) = node;
        char* const head = At<char*>(self, tree::kHead);
        if (y == At<char*>(head, tree::kRight))
            At<char*>(head, tree::kRight) = node;
    }
    At<char*>(node, tree::kParent) = y;
    At<char*>(node, tree::kLeft) = nullptr;
    At<char*>(node, tree::kRight) = nullptr;
    CallCdecl<void, char*, char**>(addr::SC4Tree_RebalanceForInsert, node,
                                   &At<char*>(At<char*>(self, tree::kHead), tree::kParent));
    ++At<uint32_t>(self, tree::kCount);
    *result = node;
    return result;
}

// 0x00622540 (ret 8: first, last node): for each node of the range (in
// order), its value inserted equal (the walk inline, Insert at its address
// with w null).
void __fastcall MapInsertRange(char* self, void*, char* first, char* last)
{
    static bool logged = false;
    FirstCall("MapInsertRange", logged);
    for (char* node = first; node != last; node = Successor(node)) {
        char* const value = node + tree::kValue;
        char* x = nullptr;
        char* const y = EqualParent(self, At<uint32_t>(value, rulevalue::kKey), x);
        char* result = nullptr;
        CallThis<char**, char**, char*, char*, char*, char*>(addr::SC4RuleFileMap_Insert, self, &result, x, y, value,
                                                             nullptr);
    }
}

// 0x006225D0 (no arguments): emptied and its head freed.
void __fastcall MapDestruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("MapDestruct", logged);
    DestroyMap(self);
}

// 0x00622750 (no arguments; EAX this): an empty multimap.
char* __fastcall MapConstruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("MapConstruct", logged);
    At<char*>(self, tree::kHead) = nullptr;
    char* const head = Allocate(rulevalue::kNodeSize);
    At<char*>(self, tree::kHead) = head;
    At<uint32_t>(self, tree::kCount) = 0;
    At<uint8_t>(head, 0) = 0;
    At<char*>(head, tree::kParent) = nullptr;
    At<char*>(head, tree::kLeft) = head;
    At<char*>(head, tree::kRight) = head;
    return self;
}

// ====================================================================================
// simulators.nettool.rulrec: Transmogrify
// ====================================================================================

// 0x00622B50 (ret 0xC: the out multimap*, the source multimap*, a word it
// never reads; this not read). For each source node in order: eight variant
// slots (entries) zeroed and a local multimap made (inline); for each i of
// 0 to 7 whose bit is set in the node's mask: a working entry zeroed, filled
// mirrored (0x00621DF0) when i & 4, else copied (the edges; the check and
// tile vectors by 0x00621850 / 0x00621970; the mask), turned by i & 3
// (0x00621F30) into the next slot; when no earlier slot is equal
// (0x006214A0), the slot is kept (its mask 1) and a value of it (the key and
// the edges its edges, its vectors copied, mask 1) inserted equal into the
// local multimap (0x00622460); else the slot emptied for reuse (edges 0, the
// vectors assigned from empty ones, the mask from an unwritten stack word:
// the top of the file); the working entry's buffers freed. Then the local
// multimap's nodes inserted into out (0x00622540), the local multimap
// destroyed (inline, 0x00622190), the slots' tile and check buffers freed
// from the last slot to the first.
void __fastcall Transmogrify(char*, void*, char* out, char* source, uint32_t)
{
    static bool logged = false;
    FirstCall("Transmogrify", logged);
    char* const sourceHead = At<char*>(source, tree::kHead);
    char* node = At<char*>(sourceHead, tree::kLeft);
    if (node == sourceHead)
        return;
    constexpr int kVariants = 8;
    do {
        alignas(4) char slots[kVariants][entry::kSize] = {};
        int32_t kept = 0;
        alignas(4) char local[8] = {};
        At<char*>(local, tree::kHead) = nullptr;
        char* const head = Allocate(rulevalue::kNodeSize);
        At<char*>(local, tree::kHead) = head;
        At<uint32_t>(local, tree::kCount) = 0;
        At<uint8_t>(head, 0) = 0;
        At<char*>(head, tree::kParent) = nullptr;
        At<char*>(head, tree::kLeft) = head;
        At<char*>(head, tree::kRight) = head;

        char* slot = slots[0];
        char* const from = node + tree::kValue + rulevalue::kEntry;
        for (uint32_t i = 0; i < static_cast<uint32_t>(kVariants); ++i) {
            if (!(At<uint32_t>(from, entry::kMask) & (1u << i)))
                continue;
            alignas(4) char working[entry::kSize] = {};
            if (i & 4) {
                CallThis<void, char*>(addr::SC4RuleEntry_Mirror, from, working);
            } else {
                At<uint32_t>(working, entry::kEdges) = At<uint32_t>(from, entry::kEdges);
                CallThis<char*, char*>(addr::SC4RuleCheckVector_Assign, working + entry::kChecks,
                                       from + entry::kChecks);
                CallThis<char*, char*>(addr::SC4RuleTileVector_Assign, working + entry::kTiles, from + entry::kTiles);
                At<uint32_t>(working, entry::kMask) = At<uint32_t>(from, entry::kMask);
            }
            CallThis<void, uint32_t, char*>(addr::SC4RuleEntry_Rotate, working, i & 3, slot);
            bool duplicate = false;
            for (int32_t j = 0; j < kept; ++j) {
                if (CallThis<uint8_t, char*>(addr::SC4RuleEntry_Equals, slots[j], slot)) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                ++kept;
                At<uint32_t>(slot, entry::kMask) = 1;
                alignas(4) char value[rulevalue::kSize] = {};
                At<uint32_t>(value, rulevalue::kKey) = At<uint32_t>(slot, entry::kEdges);
                At<uint32_t>(value, rulevalue::kEntry + entry::kEdges) = At<uint32_t>(slot, entry::kEdges);
                CallThis<char*, char*>(addr::SC4RuleCheckVector_CopyConstruct,
                                       value + rulevalue::kEntry + entry::kChecks, slot + entry::kChecks);
                CallThis<char*, char*>(addr::SC4RuleTileVector_CopyConstruct, value + rulevalue::kEntry + entry::kTiles,
                                       slot + entry::kTiles);
                char* x = nullptr;
                char* const y = EqualParent(local, At<uint32_t>(value, rulevalue::kKey), x);
                At<uint32_t>(value, rulevalue::kEntry + entry::kMask) = 1;
                char* result = nullptr;
                CallThis<char**, char**, char*, char*, char*, char*>(addr::SC4RuleFileMap_Insert, local, &result, x, y,
                                                                     value, nullptr);
                FreeIfSet(At<char*>(value, rulevalue::kEntry + entry::kTiles));
                FreeIfSet(At<char*>(value, rulevalue::kEntry + entry::kChecks));
                slot += entry::kSize;
            } else {
                // An empty entry; its mask word is not written by the game
                // (a stack leftover there, zero here).
                alignas(4) char empty[entry::kSize] = {};
                At<uint32_t>(slot, entry::kEdges) = 0;
                CallThis<char*, char*>(addr::SC4RuleCheckVector_Assign, slot + entry::kChecks, empty + entry::kChecks);
                CallThis<char*, char*>(addr::SC4RuleTileVector_Assign, slot + entry::kTiles, empty + entry::kTiles);
                At<uint32_t>(slot, entry::kMask) = At<uint32_t>(empty, entry::kMask);
            }
            FreeIfSet(At<char*>(working, entry::kTiles));
            FreeIfSet(At<char*>(working, entry::kChecks));
        }

        char* const localHead = At<char*>(local, tree::kHead);
        CallThis<void, char*, char*>(addr::SC4RuleFileMap_InsertRange, out, At<char*>(localHead, tree::kLeft),
                                     localHead);
        DestroyMap(local);
        for (int k = kVariants - 1; k >= 0; --k) {
            FreeIfSet(At<char*>(slots[k], entry::kTiles));
            FreeIfSet(At<char*>(slots[k], entry::kChecks));
        }
        node = Successor(node);
    } while (node != sourceHead);
}

// ====================================================================================
// simulators.nettool.rulfile: the reader
// ====================================================================================

// A reader string made as the constructor does: no buffer, then an 8-byte
// buffer from the STL allocator holding an empty text, the vtable, no
// references.
void MakeString(char* string)
{
    GameString& s = *reinterpret_cast<GameString*>(string);
    s.begin = s.end = s.capacity = nullptr;
    char* const text = Allocate(reader::kFirstBuffer);
    s.begin = text;
    s.end = text;
    s.capacity = text + reader::kFirstBuffer;
    At<uint8_t>(text, 0) = 0;
    s.vtable = addr::SC4String_Vtable;
    s.references = 0;
}

// 0x00621C30 (no arguments; EAX this): closed, the line string, then the
// token string (the record, the open answer and the two flags are left for
// Open to write).
char* __fastcall ReaderConstruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("ReaderConstruct", logged);
    At<uint8_t>(self, reader::kOpen) = 0;
    MakeString(self + reader::kLine);
    MakeString(self + reader::kToken);
    return self;
}

// 0x00621C90 (no arguments): closed (the record released), the token's and
// the line's vtables set back and their buffers freed.
void __fastcall ReaderDestruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("ReaderDestruct", logged);
    CloseReader(self);
    At<std::uintptr_t>(self, reader::kToken) = addr::SC4String_Vtable;
    FreeIfSet(At<char*>(self, reader::kTokenBegin));
    At<std::uintptr_t>(self, reader::kLine) = addr::SC4String_Vtable;
    FreeIfSet(At<char*>(self, reader::kLineBegin));
}

// 0x006210B0 (ret 4: a resource key*; AL): 0 when open already; else the
// record slot, the open answer and the two flags cleared, the record opened
// (SC4::OpenResourceRecord: key, the record slot, 0), the answer kept at
// +0x8; when it failed, operator delete on the record slot's pointer, the
// slot cleared and 0; else open and 1.
uint8_t __fastcall ReaderOpen(char* self, void*, const void* key)
{
    static bool logged = false;
    FirstCall("ReaderOpen", logged);
    if (At<uint8_t>(self, reader::kOpen))
        return 0;
    void** const record = &At<void*>(self, reader::kRecord);
    *record = nullptr;
    At<uint8_t>(self, reader::kOpened) = 0;
    At<uint8_t>(self, reader::kEnded) = 0;
    At<uint8_t>(self, reader::kPushedBack) = 0;
    const uint8_t opened =
        CallCdecl<uint8_t, const void*, void**, uint32_t>(addr::SC4_OpenResourceRecord, key, record, 0u);
    At<uint8_t>(self, reader::kOpened) = opened;
    if (!opened) {
        CallCdecl<void, void*>(addr::SC4_operator_delete, *record);
        *record = nullptr;
        At<uint8_t>(self, reader::kOpen) = 0;
        return At<uint8_t>(self, reader::kOpen);
    }
    At<uint8_t>(self, reader::kOpen) = 1;
    return At<uint8_t>(self, reader::kOpen);
}

// 0x00622240 (cdecl: a cIGZFile record, a cIGZString; AL whether anything
// was read). IsOpen asked, the string emptied (its slot 0x3C with 0); a
// closed record opened (1, 2, 1) or 0. Nothing when the position is not
// below the length (signed). Three buffers from the STL allocator in the
// game's order: an 8-byte range for the text, a 16-byte block holding a 0
// word (not otherwise used), a game string's 8 bytes. Then up to 128 bytes
// at a time are read until a CR or LF: the bytes before it appended to the
// range (0x0040EBC0); at the line end, the record moved after it and one more
// byte read: unless it is the other of CR and LF, the record moved back one
// (SeekToRelativePosition -1). Then the range given to the game string
// (0x0090FBE5), that to the caller's string (slot 0xC: text, length), the
// three buffers freed (string, block, range).
uint8_t __cdecl ReadLine(void* record, void* text)
{
    static bool logged = false;
    FirstCall("ReadLine", logged);
    uint8_t result = 0;
    const uint8_t isOpen = Virtual<uint8_t>(record, file_slot::kIsOpen);
    Virtual<void, uint32_t>(text, kStringSlot3C, 0u);
    if (!isOpen && !Virtual<uint8_t, uint32_t, uint32_t, uint32_t>(record, file_slot::kOpen, 1u, 2u, 1u))
        return result;
    uint32_t position = Virtual<uint32_t>(record, file_slot::kPosition);
    const uint32_t length = Virtual<uint32_t>(record, file_slot::kLength);
    if (static_cast<int32_t>(length) <= static_cast<int32_t>(position))
        return result;
    int32_t remaining = static_cast<int32_t>(length - position);
    uint8_t found = 0;

    Vector range{};
    {
        char* const p = Allocate(8);
        range.begin = p;
        range.end = p;
        range.capacity = p + 8;
        At<uint8_t>(p, 0) = 0;
    }
    char* const block = Allocate(0x10);
    At<uint16_t>(block, 0) = 0;
    GameString line{};
    {
        char* const p = Allocate(8);
        line.begin = p;
        line.end = p;
        line.capacity = p + 8;
        At<uint8_t>(p, 0) = 0;
        line.vtable = addr::SC4String_Vtable;
        line.references = 0;
    }

    alignas(4) char buffer[0x80];
    while (remaining != 0) {
        const int32_t chunk = remaining > 0x80 ? 0x80 : remaining;
        if (!Virtual<uint8_t, char*, uint32_t>(record, file_slot::kRead, buffer, static_cast<uint32_t>(chunk)))
            break;
        remaining -= chunk;
        int32_t i = 0;
        for (; i < chunk; ++i) {
            const uint32_t c = static_cast<uint8_t>(buffer[i]);
            if (c != '\r' && c != '\n')
                continue;
            Virtual<uint32_t, uint32_t>(record, file_slot::kSeekToPosition, position + static_cast<uint32_t>(i) + 1);
            found = 1;
            uint32_t after = 0;
            if (Virtual<uint8_t, uint32_t*, uint32_t>(record, file_slot::kRead, &after, 1u)) {
                const uint32_t next = after & 0xFF;
                if (!((next == '\r' || next == '\n') && next != c))
                    Virtual<uint32_t, int32_t>(record, file_slot::kSeekToRelativePosition, -1);
            }
            break;
        }
        CallThis<void, char*, char*, uint8_t*>(addr::cSC4Advisor_F40EBC0, &range, buffer, buffer + i, &found);
        result = 1;
        if (found)
            break;
        position = Virtual<uint32_t>(record, file_slot::kPosition);
    }

    CallCdecl<void, Vector*, GameString*>(addr::cRZBaseString_AssignFromRange, &range, &line);
    Virtual<void, char*, uint32_t>(text, string_slot::kFromCharLength, line.begin,
                                   static_cast<uint32_t>(line.end - line.begin));
    line.vtable = addr::SC4String_Vtable;
    FreeIfSet(line.begin);
    Free(block);
    FreeIfSet(range.begin);
    return result;
}

// 0x00622620 (no arguments; EAX the token's characters or null). A pushed
// back token is given again (the flag cleared); nothing once the record is
// used up. Else: while the line is empty, lines are read (0x00622240 at its
// address; the used-up flag = not read) until one is neither empty nor a ';'
// comment, which gets a ',' appended (grown by the reserve 0x0040F1B0 to
// size + max(size, 1) when full), or the record is used up. Then, unless
// used up, the text before the first ',' split off the line (0x0090E98C) and
// assigned to the token (0x00408A70); a non-empty token is the answer, an
// empty one reads on, unless used up.
char* __fastcall NextToken(char* self, void*)
{
    static bool logged = false;
    FirstCall("NextToken", logged);
    char* const token = self + reader::kTokenBegin;
    if (At<uint8_t>(self, reader::kPushedBack)) {
        At<uint8_t>(self, reader::kPushedBack) = 0;
        return At<char*>(token, 0);
    }
    if (At<uint8_t>(self, reader::kEnded))
        return nullptr;
    Vector& line = At<Vector>(self, reader::kLineBegin);
    for (;;) {
        if (line.begin == line.end) {
            bool ended = false;
            for (;;) {
                const uint8_t read = CallCdecl<uint8_t, void*, char*>(addr::SC4RulReader_ReadLine,
                                                                      At<void*>(self, reader::kRecord),
                                                                      self + reader::kLine);
                At<uint8_t>(self, reader::kEnded) = read ? 0 : 1;
                if (!read)
                    ended = true;
                if (line.begin != line.end && *line.begin != ';') {
                    if (line.end + 1 == line.capacity) {
                        const uint32_t size = static_cast<uint32_t>(line.end - line.begin);
                        CallThis<void, uint32_t>(addr::cSC4Advisor_F40F1B0, &line, size + (size < 1 ? 1u : size));
                    }
                    line.end[1] = 0;
                    line.end[0] = ',';
                    ++line.end;
                    break;
                }
                if (ended)
                    break;
            }
        }
        if (At<uint8_t>(self, reader::kEnded))
            return nullptr;
        alignas(4) char piece[sizeof(GameString)];
        uint8_t tag = 0;
        char* const split = CallThis<char*, char*, std::uintptr_t>(addr::SC4String_SplitOff, self + reader::kLine,
                                                                   piece, kComma);
        char* const range = split ? split + 4 : nullptr;
        if (range != token)
            CallThis<void, char*, char*, uint8_t*>(addr::SC4String_AssignRange, token, At<char*>(range, 0),
                                                   At<char*>(range, 4), &tag);
        At<std::uintptr_t>(piece, 0) = addr::SC4String_Vtable;
        FreeIfSet(At<char*>(piece, 4));
        if (At<char*>(token, 0) != At<char*>(token, 4))
            return At<char*>(token, 0);
        if (At<uint8_t>(self, reader::kEnded))
            return nullptr;
    }
}

// ====================================================================================
// simulators.nettool.rulfile: the parsers
// ====================================================================================

// An edge byte from a number n of the text (its low byte): ((n / 10) << 3,
// as a byte) | n % 10.
uint32_t EdgeByte(int32_t number)
{
    const uint32_t n = static_cast<uint8_t>(number);
    return static_cast<uint32_t>(static_cast<uint8_t>((n / 10) << 3)) | (n % 10);
}

// 0x00622790 (ret 4: the reader; AL 1): the position (atol), the ID
// (SC4::TextToUint32), the turn (atol), then a number whose being non-zero
// sets the flip bit; null tokens go to the C runtime as the game does.
uint8_t __fastcall TileParse(char* self, void*, char* reader)
{
    static bool logged = false;
    FirstCall("TileParse", logged);
    At<uint8_t>(self, tile::kPosition) = static_cast<uint8_t>(Atol(Next(reader)));
    At<uint32_t>(self, tile::kId) = CallCdecl<uint32_t, const char*>(addr::SC4_TextToUint32, Next(reader));
    At<uint8_t>(self, tile::kTurn) = static_cast<uint8_t>(Atol(Next(reader)));
    const int32_t flip = Atol(Next(reader));
    At<uint8_t>(self, tile::kTurn) = static_cast<uint8_t>(At<uint8_t>(self, tile::kTurn) | (flip ? tile::kFlipped : 0));
    return 1;
}

// 0x00622800 (ret 4: the reader; AL): the word cleared, then four tokens
// into its bytes 0 to 3 (EdgeByte); 0 at the first missing token (the bytes
// so far kept), else 1.
uint8_t __fastcall EdgesParse(char* self, void*, char* reader)
{
    static bool logged = false;
    FirstCall("EdgesParse", logged);
    uint32_t& edges = At<uint32_t>(self, 0);
    edges = 0;
    for (uint32_t shift = 0; shift < 32; shift += 8) {
        const char* const token = Next(reader);
        if (!token)
            return 0;
        const uint32_t value = EdgeByte(Atol(token));
        edges = (value << shift) | (~(0xFFu << shift) & edges);
    }
    return 1;
}

// 0x00622870 (ret 4: the reader; AL). The mask +0x1C 0xFF; "noflip" makes
// it 0xF, "notrans" 1 (then the next token); 0 for a missing token or a
// first number not 1. The edges (0x00622800, its answer not read). Then
// lines until the record ends: a token not starting with a digit (isdigit
// of the sign-extended byte), or a number other than 2 or 3, is pushed back
// (+0x35) and the answer is 1. 2: the position (atol) and four tokens ('?'
// clears the mask byte; else the edge byte and a mask byte 0xFF; a missing
// one answers 0), the check appended. 3: a tile zeroed, parsed
// (0x00622790), appended when it answers (0x00621CF0). At the end, whether
// there are tiles.
uint8_t __fastcall EntryParse(char* self, void*, char* reader)
{
    static bool logged = false;
    FirstCall("EntryParse", logged);
    const char* token = Next(reader);
    At<uint32_t>(self, entry::kMask) = entry::kAllVariants;
    if (!token)
        return 0;
    if (SameBytes(token, kNoFlip, 7)) {
        At<uint32_t>(self, entry::kMask) = entry::kNoFlip;
        token = Next(reader);
    } else if (SameBytes(token, kNoTrans, 8)) {
        At<uint32_t>(self, entry::kMask) = entry::kNoTrans;
        token = Next(reader);
    }
    if (!token)
        return 0;
    if (Atol(token) != 1)
        return 0;
    CallThis<uint8_t, char*>(addr::SC4RuleEdges_Parse, self + entry::kEdges, reader);
    char* tag = reader;
    for (token = Next(reader); token; token = Next(reader)) {
        if (!CallCdecl<int32_t, int32_t>(addr::SC4_isdigit, static_cast<int32_t>(static_cast<int8_t>(token[0])))) {
            At<uint8_t>(reader, reader::kPushedBack) = 1;
            return 1;
        }
        const int32_t kind = Atol(token);
        if (kind == 2) {
            alignas(4) char value[check::kSize] = {};
            At<uint8_t>(value, check::kPosition) = static_cast<uint8_t>(Atol(Next(reader)));
            uint32_t edges = 0, mask = 0;
            for (uint32_t shift = 0; shift < 32; shift += 8) {
                const char* const part = Next(reader);
                if (!part)
                    return 0;
                if (part[0] == '?') {
                    mask = ~(0xFFu << shift) & mask;
                } else {
                    edges = (EdgeByte(Atol(part)) << shift) | (~(0xFFu << shift) & edges);
                    mask = (~(0xFFu << shift) & mask) | (0xFFu << shift);
                }
            }
            At<uint32_t>(value, check::kEdges) = edges;
            At<uint32_t>(value, check::kMask) = mask;
            AppendCheck(&At<Vector>(self, entry::kChecks), value, &tag);
        } else if (kind == 3) {
            alignas(4) char value[tile::kSize] = {};
            if (CallThis<uint8_t, char*>(addr::SC4RuleTile_Parse, value, reader))
                CallThis<void, char*>(addr::SC4RuleTileVector_PushBack, self + entry::kTiles, value);
        } else {
            At<uint8_t>(reader, reader::kPushedBack) = 1;
            return 1;
        }
    }
    const Vector& tiles = At<Vector>(self, entry::kTiles);
    return tiles.begin != tiles.end ? 1 : 0;
}

// ====================================================================================
// simulators.nettool.rulfile: the rule file object
// ====================================================================================

// 0x00622A90 (no arguments; EAX this): the vtable 0x00AA8338 and an empty
// multimap (a 0x34-byte head).
char* __fastcall RuleFileConstruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("RuleFileConstruct", logged);
    At<std::uintptr_t>(self, rulefile::kVtable) = addr::SC4NetworkRuleFile_Vtable;
    At<char*>(self, rulefile::kMap) = nullptr;
    char* const head = Allocate(rulevalue::kNodeSize);
    At<char*>(self, rulefile::kMap) = head;
    At<uint32_t>(self, rulefile::kCount) = 0;
    At<uint8_t>(head, 0) = 0;
    At<char*>(head, tree::kParent) = nullptr;
    At<char*>(head, tree::kLeft) = head;
    At<char*>(head, tree::kRight) = head;
    return self;
}

// 0x00622AE0 (slot 0, ret 4: flags; EAX this): the vtable set, the
// multimap destroyed (inline: 0x00622190 at its address), the object given
// to operator delete when bit 0 of the flags is set.
char* __fastcall RuleFileDeletingDestructor(char* self, void*, uint32_t flags)
{
    static bool logged = false;
    FirstCall("RuleFileDeletingDestructor", logged);
    At<std::uintptr_t>(self, rulefile::kVtable) = addr::SC4NetworkRuleFile_Vtable;
    DestroyMap(self + rulefile::kMap);
    if (flags & 1)
        CallCdecl<void, void*>(addr::SC4_operator_delete, self);
    return self;
}

// 0x00623020 (ret 8: a resource key*, a word it never reads; AL). A reader
// made on the stack (0x00621C30) and opened (0x006210B0): when that fails,
// destroyed and 0. The first token "#Transmogrify" (14 bytes compared) sets
// the flag; another starting with '#' is skipped; anything else closes and
// destroys the reader and answers 0. Then entries (zeroed, 0x00622870) until
// one fails: each as a value (the key and the edges its edges, its vectors
// copied, its mask) inserted equal (the walk inline, 0x00622460), the
// value's and the entry's buffers freed (tiles, checks; tiles, checks); the
// failed one's buffers freed. With the flag: a new multimap (0x00622750)
// filled by Transmogrify from this one (0x00622B50 with this, it, the
// multimap and the second argument), the two exchanged, the old one
// destroyed (0x006225D0). The reader closed and destroyed; 1.
uint8_t __fastcall RuleFileLoad(char* self, void*, const void* key, uint32_t word)
{
    static bool logged = false;
    FirstCall("RuleFileLoad", logged);
    alignas(4) char reader[reader::kSize];
    CallThis<char*>(addr::SC4RulReader_Construct, reader);
    uint8_t transmogrify = 0;
    if (!CallThis<uint8_t, const void*>(addr::SC4RulReader_Open, reader, key)) {
        CallThis<void>(addr::SC4RulReader_Destruct, reader);
        return 0;
    }
    const char* const first = Next(reader);
    if (SameBytes(first, kTransmogrify, 14)) {
        transmogrify = 1;
    } else if (first[0] != '#') {
        CloseReader(reader);
        CallThis<void>(addr::SC4RulReader_Destruct, reader);
        return 0;
    }
    char* const map = self + rulefile::kMap;
    for (;;) {
        alignas(4) char parsed[entry::kSize] = {};
        if (!CallThis<uint8_t, char*>(addr::SC4RuleEntry_Parse, parsed, reader)) {
            FreeIfSet(At<char*>(parsed, entry::kTiles));
            FreeIfSet(At<char*>(parsed, entry::kChecks));
            break;
        }
        alignas(4) char value[rulevalue::kSize] = {};
        At<uint32_t>(value, rulevalue::kKey) = At<uint32_t>(parsed, entry::kEdges);
        At<uint32_t>(value, rulevalue::kEntry + entry::kEdges) = At<uint32_t>(parsed, entry::kEdges);
        CallThis<char*, char*>(addr::SC4RuleCheckVector_CopyConstruct, value + rulevalue::kEntry + entry::kChecks,
                               parsed + entry::kChecks);
        CallThis<char*, char*>(addr::SC4RuleTileVector_CopyConstruct, value + rulevalue::kEntry + entry::kTiles,
                               parsed + entry::kTiles);
        At<uint32_t>(value, rulevalue::kEntry + entry::kMask) = At<uint32_t>(parsed, entry::kMask);
        char* x = nullptr;
        char* const y = EqualParent(map, At<uint32_t>(value, rulevalue::kKey), x);
        char* result = nullptr;
        CallThis<char**, char**, char*, char*, char*, char*>(addr::SC4RuleFileMap_Insert, map, &result, x, y, value,
                                                             nullptr);
        FreeIfSet(At<char*>(value, rulevalue::kEntry + entry::kTiles));
        FreeIfSet(At<char*>(value, rulevalue::kEntry + entry::kChecks));
        FreeIfSet(At<char*>(parsed, entry::kTiles));
        FreeIfSet(At<char*>(parsed, entry::kChecks));
    }
    if (transmogrify) {
        alignas(4) char fresh[8];
        CallThis<char*>(addr::SC4RuleFileMap_Construct, fresh);
        CallThis<void, char*, char*, uint32_t>(addr::SC4NetworkRuleFile_Transmogrify, self, fresh, map, word);
        char* const oldHead = At<char*>(map, tree::kHead);
        const uint32_t oldCount = At<uint32_t>(map, tree::kCount);
        At<char*>(map, tree::kHead) = At<char*>(fresh, tree::kHead);
        At<char*>(fresh, tree::kHead) = oldHead;
        At<uint32_t>(map, tree::kCount) = At<uint32_t>(fresh, tree::kCount);
        At<uint32_t>(fresh, tree::kCount) = oldCount;
        CallThis<void>(addr::SC4RuleFileMap_Destruct, fresh);
    }
    CloseReader(reader);
    CallThis<void>(addr::SC4RulReader_Destruct, reader);
    return 1;
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kRulFile[] = {
    OPENSC4_DETOUR(addr::SC4RulReader_Open, ReaderOpen),
    OPENSC4_DETOUR(addr::SC4RulReader_Construct, ReaderConstruct),
    OPENSC4_DETOUR(addr::SC4RulReader_Destruct, ReaderDestruct),
    OPENSC4_DETOUR(addr::SC4RulReader_ReadLine, ReadLine),
    OPENSC4_DETOUR(addr::SC4RulReader_NextToken, NextToken),
    OPENSC4_DETOUR(addr::SC4RuleTile_Parse, TileParse),
    OPENSC4_DETOUR(addr::SC4RuleEdges_Parse, EdgesParse),
    OPENSC4_DETOUR(addr::SC4RuleEntry_Parse, EntryParse),
    OPENSC4_DETOUR(addr::SC4NetworkRuleFile_Construct, RuleFileConstruct),
    OPENSC4_DETOUR(addr::SC4NetworkRuleFile_DeletingDestructor, RuleFileDeletingDestructor),
    OPENSC4_DETOUR(addr::SC4NetworkRuleFile_Load, RuleFileLoad),
};

const Detour kRulRec[] = {
    OPENSC4_DETOUR(addr::SC4RuleTileVector_UninitializedCopy, TileUninitializedCopy),
    OPENSC4_DETOUR(addr::SC4RuleTileVector_UninitializedFill, TileUninitializedFill),
    OPENSC4_DETOUR(addr::SC4RuleTile_Rotate, RotateTile),
    OPENSC4_DETOUR(addr::SC4RuleEdges_Mirror, MirrorEdges),
    OPENSC4_DETOUR(addr::SC4RuleEdges_Rotate, RotateEdges),
    OPENSC4_DETOUR(addr::SC4RuleEntry_Equals, EntryEquals),
    OPENSC4_DETOUR(addr::SC4RuleCheckVector_AllocateCopy, CheckAllocateCopy),
    OPENSC4_DETOUR(addr::SC4RuleTileVector_AllocateCopy, TileAllocateCopy),
    OPENSC4_DETOUR(addr::SC4RuleCheck_Rotate, RotateCheck),
    OPENSC4_DETOUR(addr::SC4RuleCheckVector_CopyConstruct, CheckCopyConstruct),
    OPENSC4_DETOUR(addr::SC4RuleTileVector_CopyConstruct, TileCopyConstruct),
    OPENSC4_DETOUR(addr::SC4RuleCheckVector_Assign, CheckAssign),
    OPENSC4_DETOUR(addr::SC4RuleTileVector_Assign, TileAssign),
    OPENSC4_DETOUR(addr::SC4RuleTileVector_InsertOverflow, TileInsertOverflow),
    OPENSC4_DETOUR(addr::SC4RuleTileVector_PushBack, TilePushBack),
    OPENSC4_DETOUR(addr::SC4RuleEntry_Mirror, MirrorEntry),
    OPENSC4_DETOUR(addr::SC4RuleEntry_Rotate, RotateEntry),
    OPENSC4_DETOUR(addr::SC4RuleFileMap_EraseSubtree, MapEraseSubtree),
    OPENSC4_DETOUR(addr::SC4RuleFileMap_CreateNode, MapCreateNode),
    OPENSC4_DETOUR(addr::SC4RuleFileMap_Insert, MapInsert),
    OPENSC4_DETOUR(addr::SC4RuleFileMap_InsertRange, MapInsertRange),
    OPENSC4_DETOUR(addr::SC4RuleFileMap_Destruct, MapDestruct),
    OPENSC4_DETOUR(addr::SC4RuleFileMap_Construct, MapConstruct),
    OPENSC4_DETOUR(addr::SC4NetworkRuleFile_Transmogrify, Transmogrify),
};

#undef OPENSC4_DETOUR

bool InstallRulFile(registry::Mode) { return InstallDetours(kRulFile, "rulfile"); }
bool InstallRulRec(registry::Mode) { return InstallDetours(kRulRec, "rulrec"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.rulfile", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::SC4NetworkRuleFile_Load,
                        "Our network tool's type row rule files: the rule file object's constructor, deleting "
                        "destructor and load, its RUL text reader (open, line, token) and the edge, tile and entry "
                        "parsers (11 detours; src/core/nettool_rul_2.cpp)",
                        false, InstallRulFile}),
    registry::Register({"simulators.nettool.rulrec", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::SC4NetworkRuleFile_Transmogrify,
                        "Our network tool's rule file records: the multimap's node code, the check and tile vectors' "
                        "copies and growth, the edge, check, tile and entry turns and mirrors, the entry compare and "
                        "Transmogrify (24 detours; src/core/nettool_rul_2.cpp)",
                        false, InstallRulRec}),
};

} // namespace
} // namespace opensc4::nettool_sim
