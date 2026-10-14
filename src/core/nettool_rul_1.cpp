// Our cSC4NetworkTool, part r1 (build 641; the class map is in
// src/core/nettool_sim.h, the rule layouts in src/core/nettool_rul.h): the
// RUL engine's statics and containers. Two groups, each a list of detours at
// the game's addresses:
//   simulators.nettool.rulload: the rule load LoadRules 0x006253D0 (once per
//     resource load: the network ini, RUL 0x10000001, 0x10000002 and
//     0x10000000), the per-network ini LoadTypeInfo 0x00623310, the reset
//     callback 0x006232F0, the power line tool's LoadRulesOnce 0x00626990,
//     the erasers of the RUL2 multimap 0x00624CE0 and of the RUL1 map
//     0x00635C60, and the type table row's static constructor 0x00641F70 and
//     destructor 0x00643510 (8 detours);
//   simulators.nettool.rul0: the RUL0 rule and its store: the rule's
//     constructor, copy constructor and destructor with the copies under
//     them, the store's Clear, InsertNode and FindOrInsert, the two layout
//     parsers, the transforms (Translate, Transpose, Rotate) and their grid,
//     point and edge helpers, and the pair table's Find 0x00633970 (21
//     detours).
// Two groups because the load runs once per city load and touches the type
// table, while the RUL0 code also runs during drags (FindOrInsert from the
// tool's rule lookups) and is what NAM's FlexPieces edit: each can be tried
// and bisected alone.
//
// Every function was written from the disassembly. Calls between them go to
// the game's addresses (through the address entries), so each detour is
// independent of the others and OPENSC4_NETTOOL_SKIP bisects them.
//
// Left as the game's (not in this part): the RUL text parsers (parts r3 to
// r5, 0x0063BF30 to 0x006437A0) that LoadRules calls at their addresses
// (LoadRul1 0x00641D30, ReadRul1TypeIds 0x0063D630, LoadRul2 0x0063D3E0,
// LoadRul0 0x00641B60), the type row's loaders (Clear 0x006421D0,
// AddRuleFile 0x0063D360, LoadBridges 0x006437A0, LoadExemplars 0x0063F780,
// LoadParams 0x0063D460, SetLoaded 0x0063BF90, LoadTileVerifiers 0x0063FAF0)
// and the shared template code. Nothing of the part's 29 functions stays the
// game's.
//
// NAM. The guarded RUL2 engine 0x00634D70 and the RUL2 line callback
// 0x0063D1E0 stay the game's; neither is called here (LoadRul2 0x0063D3E0
// pushes 0x0063D1E0 itself). NAM's DLL reads, and its FlexPieces edit in
// place, the RUL0 rules, the store 0x00B452B0, the multimap 0x00B466D0 and the
// type table rows: every container, rule, node and row here stays in the
// game's memory with the game's layout, allocated through the game's
// allocator (SC4::stl_allocate, SC4::Free) and built through the game's
// template helpers where the game calls them; nothing is kept across calls.
// No wrapper of src/core/game_*.cpp or *fix*.cpp detours or tests return
// addresses in this range. sim_timers.cpp names 0x00608C50 only as the
// (exclusive) end of the construction crew's caller range 0x00605490 to
// 0x00608C50: our detour of 0x00608C50 is past it and does not touch timers.
//
// Floats: three functions touch floats, all on the RUL0 records' (x, z)
// pairs. Translate 0x00608D20: FILD the int offset, FADD the float, FSTP
// float; with the x87 at 53 bits that is the int and the float added as
// doubles and the sum rounded to float (exact in C++: an int32 and a float
// are exact in double and the double rounding is the same). Transpose
// 0x006091F0 moves one float with FLD + FSTP (and the other as an integer
// word) and Rotate 0x006094B0 moves and negates them with FLD, FCHS, FSTP:
// the same instructions (X87Move, X87Negate), because an x87 load and store
// of a signalling NaN quiets it, which a C++ float copy does not. Nothing is
// kept on the x87 stack across a call; no __ftol2.
//
// Registers. The scan of every direct caller (call sites in all.dis, ECX,
// EDX and EAX after the call) found nothing that reads ECX or EDX after any
// of these functions (the flagged sites write CL/DL first), so they are plain
// detours. EAX is the game's where callers read it: MirrorEdgeWord (the
// word), CopyRecords (the destination's end), CopyRecordVector,
// CopyConstruct, Construct and the row constructor (this), InsertNode (the
// node + 4), FindOrInsert (the rule), Find (the node or null),
// LoadRulesOnce (AL 1). The reset callback 0x006232F0 is called only
// indirectly (the app's reset callbacks), by a caller nobody scanned: it is a
// thunk that keeps EAX's upper bytes, ECX and EDX as the game's (which only
// writes AL).
//
// Stack leftovers the game stores and we cannot repeat (documented, not
// hashed by the test):
//   - FindOrInsert's empty rule is made by the constructor 0x0062A480 on
//     FindOrInsert's stack, and the constructor leaves +0x0, +0x18 to +0x24,
//     +0x40, +0x44, +0x7C and +0xC4 unwritten: a new rule inherits the
//     game's stack leftovers there (the RUL0 loader then writes +0x0; the
//     layout parser +0x18 to +0x24 and +0xC4 when the rule has a cell
//     layout). Ours starts its temporaries zeroed, so a B run stores zeros.
//   - ParseCellLayout stores a cell's key as one byte of a stack word and
//     copies the whole word: the cell's three bytes after the key are the
//     game's stack leftovers. Ours are zero.
// What reads them (checked against the exe): +0x40/+0x44 only behind the
// byte +0x48 (0x00609A00), which the constructor clears; the bounds and
// +0xC4 of a rule without a cell grid only behind a grid size test
// (0x00625D24, 0x006372B2); a cell's three bytes are copied, never
// compared; +0x0 is written by the loader right after FindOrInsert (a drag
// that inserts an absent ID keeps a leftover there, which its callers do not
// read). +0x7C (PlaceQueryID) has one reader: the intersection placement
// tool's text at 0x00661778, an LTEXT of group 0x2A592FD1 at that instance.
// A probe of the original on Rush Hour t0000 found address-like leftovers
// on 28 of 249 rules; the group holds no instance near either those or 0
// (SimCityLocale.DAT: 0x0C369C73, 0x8AADD2B5, 0xA0000000 to 0xA0000004,
// 0xF00000nn), so both miss and show the same "missing" text.
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the part's own, at the game's addresses: MirrorEdgeWord cdecl one;
//     Translate two (ret 8); RotatePoints and RotateCells cdecl two;
//     TransposeGrid cdecl four; RotateGrid cdecl five; CopyRecords cdecl four;
//     CopyRecordVector, CopyCheckTypes, CopyConsCells, CopyConstruct,
//     InsertNode one (ret 4); Destruct and Construct none; the erasers one
//     (ret 4, recursive); LoadTypeInfo cdecl one; LoadRules cdecl none;
//   - the loaders (parts r3 to r5): LoadRul1 cdecl one, ReadRul1TypeIds cdecl
//     two, LoadRul2 cdecl two, LoadRul0 cdecl two, LoadTileVerifiers cdecl
//     two; the type row's Clear none, AddRuleFile three (ret 0xC),
//     LoadBridges four (ret 0x10), LoadExemplars one (ret 4), LoadParams one
//     (ret 4), SetLoaded none; the row's DestroyPlacementParams 0x00624F70
//     none, DestroyTable3C 0x0063D760 none, a bridge record's destructor
//     0x00640230 none;
//   - the ini file: SC4::OpenResourceRecord 0x005F9670 cdecl three (key,
//     record out, 0), cRZIniFile::ConstructFromRecord 0x0091CF46 one (ret
//     4), cRZIniFile::Destruct 0x0091CDE3 none; the record's Release (slot
//     2) none; the app 0x00B43C94 slot 0x114 two (the callback, 0);
//   - template code: SC4::stl_allocate 0x0090CF54 and SC4::Free 0x0090CF63
//     cdecl one; SC4::isalnum 0x009EF70A cdecl one; SC4HashTable::Begin
//     0x004B0390 one (ret 4: the iterator out); SC4HashTable::Resize
//     0x0051CE70 and SC4Uint32HashSet::Resize 0x004F0940 one; the hash
//     table construct 0x00829BC0 one (100) and clear 0x008EBA60 none;
//     SC4ByteVector::ConstructZeroed 0x00608EC0 one, InsertN 0x004618E0
//     three (ret 0xC: where, count, value*), CopyConstruct 0x00459E80 one;
//     SC4PointVector::InsertN 0x007C61A0 and SC4Vector12::InsertOverflow
//     0x00626240 five (ret 0x14: where, value*, tag*, 1, 1), the point and
//     12-byte vectors' CopyConstruct 0x007CED60 / 0x00624D60 one;
//     PtrVector::Reserve 0x0045D4F0 one, SC4PtrVector::FillInsert 0x004FBA20
//     three (where, count, value*); SC4String::CopyBuffer 0x004089B0 four
//     (ret 0x10: the source buffer, 0, -1, tag*).
#include "core/nettool_rul.h"
#include "core/nettool_sim.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

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

// A hash table's buckets and their number as the game computes it ((end -
// begin) >> 2, an arithmetic shift, used unsigned).
char** Buckets(char* table)
{
    return At<char**>(table, table::kBuckets);
}
uint32_t BucketCount(char* table)
{
    return static_cast<uint32_t>((At<char*>(table, table::kBucketsEnd) - At<char*>(table, table::kBuckets)) >> 2);
}

// The first node of a table (the scan the game inlines in Transpose): the
// first non-empty bucket's head, or null.
char* FirstNode(char* table)
{
    for (uint32_t i = 0; i < BucketCount(table); ++i) {
        if (char* const head = Buckets(table)[i])
            return head;
    }
    return nullptr;
}

// The node after `node` (the STLport iterator's increment, inlined by the
// game): its next, else the head of the next non-empty bucket after the one
// its byte key (+0x4) hashes to, else null.
char* NextNode(char* table, char* node)
{
    if (char* const next = At<char*>(node, table::kNext))
        return next;
    const uint32_t count = BucketCount(table);
    uint32_t bucket = static_cast<uint32_t>(At<uint8_t>(node, table::kKey)) % count;
    char* found = nullptr;
    while (++bucket < count) {
        found = Buckets(table)[bucket];
        if (found)
            break;
    }
    return found;
}

// The node of a byte-keyed table with that key, or null (the chain walk the
// layout parsers inline).
char* FindByte(char* table, uint8_t key)
{
    char* node = Buckets(table)[static_cast<uint32_t>(key) % BucketCount(table)];
    while (node && At<uint8_t>(node, table::kKey) != key)
        node = At<char*>(node, table::kNext);
    return node;
}

// Frees a pointer of the object when it is set (the destructors' pattern).
void FreeAt(char* object, std::size_t offset)
{
    if (void* const block = At<void*>(object, offset))
        Free(block);
}

// FLD dword [from]; FSTP dword [to]: the records' float moves.
__declspec(naked) void __cdecl X87Move(const float* /*from*/, float* /*to*/)
{
    __asm mov eax, [esp + 4]
    __asm fld dword ptr [eax]
    __asm mov eax, [esp + 8]
    __asm fstp dword ptr [eax]
    __asm ret
}

// FLD dword [p]; FCHS; FSTP dword [p]: the records' negations.
__declspec(naked) void __cdecl X87Negate(float* /*p*/)
{
    __asm mov eax, [esp + 4]
    __asm fld dword ptr [eax]
    __asm fchs
    __asm fstp dword ptr [eax]
    __asm ret
}

// The rule's vectors walked element by element.
template <typename Fn>
void ForEach(char* self, std::size_t vector, std::size_t stride, Fn fn)
{
    Vector& v = At<Vector>(self, vector);
    for (char* element = v.begin; element != v.end; element += stride)
        fn(element);
}

// ====================================================================================
// simulators.nettool.rul0: the transforms
// ====================================================================================

// 0x00608C50 (cdecl, one word; EAX the answer): a check type's edge word
// mirrored: each byte 1, 2 or 3 becomes 4 - itself (0 and 4 or more stay),
// then the bytes reordered: the answer's bytes are the word's 1, 0, 3, 2.
uint32_t __cdecl MirrorEdgeWord(uint32_t word)
{
    static bool logged = false;
    FirstCall("MirrorEdgeWord", logged);
    uint32_t b0 = word & 0xFF, b1 = (word >> 8) & 0xFF, b2 = (word >> 16) & 0xFF, b3 = word >> 24;
    for (uint32_t* const b : {&b0, &b1, &b2, &b3}) {
        if (*b > 0 && *b < 4)
            *b = 4 - *b;
    }
    return (((((b2 << 8) + b3) << 8) + b0) << 8) + b1;
}

// 0x00608D20 (ret 8: dx, dz): the rule moved: its '+' points (+0x28), its
// checked points (+0x4C) and its cells (+0x58) by (dx, dz); the records'
// floats (+0x4) by the same as FILD + FADD + FSTP float; the offset +0x94,
// +0x98 by (dx, dz), the cons anchor +0xAC, +0xB0 by (-dx, -dz); the bounds
// by (dx, dz).
void __fastcall Translate(char* self, void*, int32_t dx, int32_t dz)
{
    static bool logged = false;
    FirstCall("Translate", logged);
    const auto move = [&](char* p) {
        At<int32_t>(p, point::kX) += dx;
        At<int32_t>(p, point::kZ) += dz;
    };
    ForEach(self, rul0::kPoints, point::kSize, move);
    ForEach(self, rul0::kChecked, point::kSize, move);
    ForEach(self, rul0::kCells, cellrec::kSize, move);
    ForEach(self, rul0::kRecords, record::kSize, [&](char* r) {
        // x87 at 53 bits: the int and the float added as doubles, rounded to
        // float by the store.
        float& x = At<float>(r, record::kX);
        x = static_cast<float>(static_cast<double>(dx) + static_cast<double>(x));
        float& z = At<float>(r, record::kZ);
        z = static_cast<float>(static_cast<double>(dz) + static_cast<double>(z));
    });
    At<int32_t>(self, rul0::kOffsetX) += dx;
    At<int32_t>(self, rul0::kOffsetZ) += dz;
    At<int32_t>(self, rul0::kConsAnchorX) -= dx;
    At<int32_t>(self, rul0::kConsAnchorZ) -= dz;
    At<int32_t>(self, rul0::kMinX) += dx;
    At<int32_t>(self, rul0::kMaxX) += dx;
    At<int32_t>(self, rul0::kMinZ) += dz;
    At<int32_t>(self, rul0::kMaxZ) += dz;
}

// The points' turn of 0x00608E00 / 0x00608E60: 1 (x, z) -> (-z, x), 2 (-x,
// -z), 3 (z, -x); anything else leaves the point.
void TurnPoint(char* p, uint32_t turns)
{
    int32_t& x = At<int32_t>(p, point::kX);
    int32_t& z = At<int32_t>(p, point::kZ);
    const int32_t oldX = x, oldZ = z;
    switch (turns) {
    case 1:
        x = static_cast<int32_t>(0u - static_cast<uint32_t>(oldZ));
        z = oldX;
        break;
    case 2:
        x = static_cast<int32_t>(0u - static_cast<uint32_t>(oldX));
        z = static_cast<int32_t>(0u - static_cast<uint32_t>(oldZ));
        break;
    case 3:
        x = oldZ;
        z = static_cast<int32_t>(0u - static_cast<uint32_t>(oldX));
        break;
    default:
        break;
    }
}

void TurnAll(Vector* v, uint32_t turns, std::size_t stride)
{
    if (!turns || v->begin == v->end)
        return;
    for (char* p = v->begin; p != v->end; p += stride)
        TurnPoint(p, turns);
}

// 0x00608E00 (cdecl: a vector of 8-byte points, the quarter turns).
void __cdecl RotatePoints(Vector* points, uint32_t turns)
{
    static bool logged = false;
    FirstCall("RotatePoints", logged);
    TurnAll(points, turns, point::kSize);
}

// 0x00608E60 (cdecl: a vector of 12-byte cells, the quarter turns).
void __cdecl RotateCells(Vector* cells, uint32_t turns)
{
    static bool logged = false;
    FirstCall("RotateCells", logged);
    TurnAll(cells, turns, cellrec::kSize);
}

// A grid value swapped as TransposeGrid's flag asks: 1 <-> 2.
uint8_t SwapOneTwo(uint8_t value)
{
    return value == 1 ? 2 : value == 2 ? 1 : value;
}

// 0x00608F20 (cdecl: a byte grid vector, its width*, a pair of words*, a flag
// byte): the pair's two words swapped; when the width is not 0, the grid
// transposed into a new zeroed vector (0x00608EC0) of the old size (height =
// size / width, unsigned): the new row j is the old column j, values 1 and 2
// swapped when the flag is set; the width becomes the height and the old
// buffer is freed.
void __cdecl TransposeGrid(Vector* grid, int32_t* width, int32_t* pair, uint32_t flag)
{
    static bool logged = false;
    FirstCall("TransposeGrid", logged);
    const int32_t first = pair[0];
    pair[0] = pair[1];
    pair[1] = first;
    if (*width == 0)
        return;
    Vector fresh;
    CallThis<void*, uint32_t>(addr::SC4ByteVector_ConstructZeroed, &fresh,
                              static_cast<uint32_t>(grid->end - grid->begin));
    const uint32_t columns = static_cast<uint32_t>(*width);
    const int32_t rows = static_cast<int32_t>(static_cast<uint32_t>(grid->end - grid->begin) / columns);
    char* out = fresh.begin;
    for (int32_t j = 0; j < static_cast<int32_t>(columns); ++j) {
        uint32_t offset = 0;
        for (int32_t i = 0; i < rows; ++i, offset += columns) {
            const uint8_t value = At<uint8_t>(grid->begin, offset + static_cast<uint32_t>(j));
            At<uint8_t>(out, static_cast<std::size_t>(i)) = (flag & 0xFF) ? SwapOneTwo(value) : value;
        }
        out += rows;
    }
    *width = rows;
    char* const old = grid->begin;
    *grid = fresh;
    if (old)
        Free(old);
}

// RotateGrid's flag map (the jump table 0x006091D0): 1 <-> 2, 3 <-> 4,
// 5 <-> 6, anything else stays.
uint8_t SwapPairs(uint8_t value)
{
    switch (value) {
    case 1:
        return 2;
    case 2:
        return 1;
    case 3:
        return 4;
    case 4:
        return 3;
    case 5:
        return 6;
    case 6:
        return 5;
    default:
        return value;
    }
}

// 0x00609040 (cdecl: a byte grid vector, its width*, an anchor pair*, the
// quarter turns, a flag byte). Nothing when the width is 0. Height = size /
// width (signed). Bit 1: the anchor mirrored (width - x - 1, height - z - 1)
// and the bytes reversed in place. Bit 0: a new zeroed grid (0x00608EC0) whose
// row j is the old column j read bottom up (values paired by SwapPairs when
// the flag is set), the width becomes the height, the anchor (x, z) becomes
// (height - z - 1, x), the old buffer freed. The width used is the one read
// on entry.
void __cdecl RotateGrid(Vector* grid, int32_t* width, int32_t* pair, uint32_t turns, uint32_t flag)
{
    static bool logged = false;
    FirstCall("RotateGrid", logged);
    const int32_t columns = *width;
    if (columns == 0)
        return;
    const int32_t size = static_cast<int32_t>(grid->end - grid->begin);
    const int32_t rows = size / columns;
    if (turns & 2) {
        pair[0] = static_cast<int32_t>(static_cast<uint32_t>(columns) - static_cast<uint32_t>(pair[0]) - 1u);
        pair[1] = static_cast<int32_t>(static_cast<uint32_t>(rows) - static_cast<uint32_t>(pair[1]) - 1u);
        for (int32_t i = 0, k = size - 1; i < k; ++i, --k) {
            const uint8_t low = At<uint8_t>(grid->begin, static_cast<std::size_t>(i));
            At<uint8_t>(grid->begin, static_cast<std::size_t>(i)) =
                At<uint8_t>(grid->begin, static_cast<std::size_t>(k));
            At<uint8_t>(grid->begin, static_cast<std::size_t>(k)) = low;
        }
    }
    if (!(turns & 1))
        return;
    Vector fresh;
    CallThis<void*, uint32_t>(addr::SC4ByteVector_ConstructZeroed, &fresh, static_cast<uint32_t>(size));
    char* out = fresh.begin;
    for (int32_t j = 0; j < columns; ++j) {
        if (rows > 0) {
            uint32_t offset = static_cast<uint32_t>(rows - 1) * static_cast<uint32_t>(columns);
            for (int32_t i = 0; i < rows; ++i, offset -= static_cast<uint32_t>(columns)) {
                const uint8_t value = At<uint8_t>(grid->begin, offset + static_cast<uint32_t>(j));
                At<uint8_t>(out, static_cast<std::size_t>(i)) = (flag & 0xFF) ? SwapPairs(value) : value;
            }
        }
        out += rows;
    }
    *width = rows;
    const int32_t oldX = pair[0];
    pair[0] = pair[1];
    pair[1] = oldX;
    pair[0] = static_cast<int32_t>(static_cast<uint32_t>(rows) - static_cast<uint32_t>(pair[0]) - 1u);
    char* const old = grid->begin;
    *grid = fresh;
    if (old)
        Free(old);
}

// 0x006091F0 (no arguments): the rule mirrored on its diagonal. x and z
// swapped in the '+' points, the checked points and the cells; in each check
// type (walked as the game walks its table) whose side's network type byte is
// not negative, the side's edge word mirrored by 0x00608C50 (at its address)
// and its mask's bytes reordered the same way without the value change; in
// each record, the floats swapped (z moved as a word, x through FLD/FSTP),
// the flip byte inverted and the turn stepped (+1 when it was 0, else -1, &
// 3); each cons cell's byte b: negative (b - 1) & 3, else (b - 0x7F) & 0x83;
// the offset swapped; the cons grid transposed with its anchor (flag 1), the
// cell grid with a throwaway pair (-minX, -minZ) (flag 0); the bounds
// swapped; the rotation (0xFE - it) & 7.
void __fastcall Transpose(char* self, void*)
{
    static bool logged = false;
    FirstCall("Transpose", logged);
    const auto swap = [](char* p) {
        const int32_t x = At<int32_t>(p, point::kX);
        At<int32_t>(p, point::kX) = At<int32_t>(p, point::kZ);
        At<int32_t>(p, point::kZ) = x;
    };
    ForEach(self, rul0::kPoints, point::kSize, swap);
    ForEach(self, rul0::kChecked, point::kSize, swap);
    ForEach(self, rul0::kCells, cellrec::kSize, swap);

    char* const types = self + rul0::kCheckTypes;
    if (BucketCount(types)) {
        for (char* node = FirstNode(types); node; node = NextNode(types, node)) {
            for (std::size_t side = 0; side < 2; ++side) {
                if (At<int8_t>(node, checktype::kTypes + side) < 0)
                    continue;
                uint32_t& edges = At<uint32_t>(node, checktype::kEdges + side * checktype::kSide);
                edges = CallCdecl<uint32_t, uint32_t>(addr::SC4Rul0Rule_MirrorEdgeWord, edges);
                uint32_t& mask = At<uint32_t>(node, checktype::kMasks + side * checktype::kSide);
                const uint32_t w = mask;
                mask = (((((w >> 16) & 0xFF) << 8) + (w >> 24)) << 8 | (w & 0xFF)) << 8 | ((w >> 8) & 0xFF);
            }
        }
    }

    ForEach(self, rul0::kRecords, record::kSize, [](char* r) {
        const uint32_t z = At<uint32_t>(r, record::kZ);
        X87Move(&At<float>(r, record::kX), &At<float>(r, record::kZ));
        At<uint32_t>(r, record::kX) = z;
        const uint8_t flipped = At<uint8_t>(r, record::kFlip) == 0 ? 1 : 0;
        At<uint8_t>(r, record::kFlip) = flipped;
        uint8_t turn = At<uint8_t>(r, record::kTurn);
        turn = flipped ? static_cast<uint8_t>(turn + 1) : static_cast<uint8_t>(turn - 1);
        At<uint8_t>(r, record::kTurn) = turn & 3;
    });

    char* const cons = self + rul0::kConsCells;
    if (BucketCount(cons)) {
        for (char* node = FirstNode(cons); node; node = NextNode(cons, node)) {
            uint8_t b = At<uint8_t>(node, conscell::kTurn);
            b = static_cast<int8_t>(b) < 0 ? static_cast<uint8_t>((b - 1) & 3)
                                           : static_cast<uint8_t>((b - 0x7F) & 0x83);
            At<uint8_t>(node, conscell::kTurn) = b;
        }
    }

    const int32_t offsetX = At<int32_t>(self, rul0::kOffsetX);
    At<int32_t>(self, rul0::kOffsetX) = At<int32_t>(self, rul0::kOffsetZ);
    At<int32_t>(self, rul0::kOffsetZ) = offsetX;
    CallCdecl<void, char*, char*, char*, uint32_t>(addr::SC4Rul0Rule_TransposeGrid, self + rul0::kConsGrid,
                                                   self + rul0::kConsWidth, self + rul0::kConsAnchorX, 1u);
    int32_t throwaway[2] = {static_cast<int32_t>(0u - At<uint32_t>(self, rul0::kMinX)),
                            static_cast<int32_t>(0u - At<uint32_t>(self, rul0::kMinZ))};
    CallCdecl<void, char*, char*, int32_t*, uint32_t>(addr::SC4Rul0Rule_TransposeGrid, self + rul0::kCellGrid,
                                                      self + rul0::kCellWidth, throwaway, 0u);
    const int32_t minX = At<int32_t>(self, rul0::kMinX);
    At<int32_t>(self, rul0::kMinX) = At<int32_t>(self, rul0::kMinZ);
    At<int32_t>(self, rul0::kMinZ) = minX;
    const int32_t maxX = At<int32_t>(self, rul0::kMaxX);
    At<int32_t>(self, rul0::kMaxX) = At<int32_t>(self, rul0::kMaxZ);
    At<int32_t>(self, rul0::kMaxZ) = maxX;
    At<uint8_t>(self, rul0::kRotation) = static_cast<uint8_t>((0xFE - At<uint8_t>(self, rul0::kRotation)) & 7);
}

// 0x006094B0 (ret 4: quarter turns; only their low two bits): nothing for 0.
// The '+' points and checked points (0x00608E00) and the cells (0x00608E60)
// turned; each check type (from SC4HashTable::Begin at its address, then
// walked as the game walks) whose side's network type is not negative has
// its edge word and mask rotated left by 8 bits a turn; each record's turn
// byte += turns (-= when flipped) & 3 and its floats turned like the points
// (moves as words or FLD/FSTP, negations FLD/FCHS/FSTP, in the game's
// order); each cons cell's byte b: negative ((b - turns) & 3) | 0x80, else
// (b + turns) & 3; the offset turned like a point; the cons grid turned with
// its anchor (flag 1), the cell grid with a throwaway pair (-minX, -minZ)
// (flag 0); the bounds turned (bit 1 then bit 0); the rotation += 2 * turns,
// & 7.
void __fastcall Rotate(char* self, void*, uint32_t argument)
{
    static bool logged = false;
    FirstCall("Rotate", logged);
    const uint32_t turns = argument & 3;
    if (!turns)
        return;
    CallCdecl<void, char*, uint32_t>(addr::SC4Rul0Rule_RotatePoints, self + rul0::kPoints, turns);
    CallCdecl<void, char*, uint32_t>(addr::SC4Rul0Rule_RotatePoints, self + rul0::kChecked, turns);
    CallCdecl<void, char*, uint32_t>(addr::SC4Rul0Rule_RotateCells, self + rul0::kCells, turns);

    struct Iterator {
        char* node;
        char* table;
    } it{};
    CallThis<void*, Iterator*>(addr::SC4HashTable_Begin, self + rul0::kCheckTypes, &it);
    const int shift = static_cast<int>(turns * 8);
    for (char* node = it.node; node; node = NextNode(it.table, node)) {
        for (std::size_t side = 0; side < 2; ++side) {
            if (At<int8_t>(node, checktype::kTypes + side) < 0)
                continue;
            uint32_t& edges = At<uint32_t>(node, checktype::kEdges + side * checktype::kSide);
            edges = std::rotl(edges, shift);
            uint32_t& mask = At<uint32_t>(node, checktype::kMasks + side * checktype::kSide);
            mask = std::rotl(mask, shift);
        }
    }

    ForEach(self, rul0::kRecords, record::kSize, [turns](char* r) {
        const uint32_t step = At<uint8_t>(r, record::kFlip) ? 0u - turns : turns;
        At<uint8_t>(r, record::kTurn) = static_cast<uint8_t>((At<uint8_t>(r, record::kTurn) + step) & 3);
        float* const x = &At<float>(r, record::kX);
        float* const z = &At<float>(r, record::kZ);
        switch (turns) {
        case 1: { // (x, z) -> (-z, x)
            const uint32_t oldZ = At<uint32_t>(r, record::kZ);
            X87Move(x, z);
            At<uint32_t>(r, record::kX) = oldZ;
            X87Negate(x);
            break;
        }
        case 2: // (-x, -z)
            X87Negate(x);
            X87Negate(z);
            break;
        case 3: { // (x, z) -> (z, -x)
            const uint32_t oldZ = At<uint32_t>(r, record::kZ);
            X87Move(x, z);
            At<uint32_t>(r, record::kX) = oldZ;
            X87Negate(z);
            break;
        }
        default:
            break;
        }
    });

    char* const cons = self + rul0::kConsCells;
    if (BucketCount(cons)) {
        for (char* node = FirstNode(cons); node; node = NextNode(cons, node)) {
            uint8_t b = At<uint8_t>(node, conscell::kTurn);
            b = static_cast<int8_t>(b) < 0 ? static_cast<uint8_t>(((b - turns) & 3) | 0x80)
                                           : static_cast<uint8_t>((b + turns) & 3);
            At<uint8_t>(node, conscell::kTurn) = b;
        }
    }

    const uint32_t offsetX = At<uint32_t>(self, rul0::kOffsetX), offsetZ = At<uint32_t>(self, rul0::kOffsetZ);
    uint32_t newX = offsetX, newZ = offsetZ;
    switch (turns) {
    case 1:
        newX = 0u - offsetZ;
        newZ = offsetX;
        break;
    case 2:
        newX = 0u - offsetX;
        newZ = 0u - offsetZ;
        break;
    case 3:
        newX = offsetZ;
        newZ = 0u - offsetX;
        break;
    default:
        break;
    }
    At<uint32_t>(self, rul0::kOffsetZ) = newZ;
    At<uint32_t>(self, rul0::kOffsetX) = newX;
    CallCdecl<void, char*, char*, char*, uint32_t, uint32_t>(addr::SC4Rul0Rule_RotateGrid, self + rul0::kConsGrid,
                                                             self + rul0::kConsWidth, self + rul0::kConsAnchorX,
                                                             turns, 1u);
    int32_t throwaway[2] = {static_cast<int32_t>(0u - At<uint32_t>(self, rul0::kMinX)),
                            static_cast<int32_t>(0u - At<uint32_t>(self, rul0::kMinZ))};
    CallCdecl<void, char*, char*, int32_t*, uint32_t, uint32_t>(addr::SC4Rul0Rule_RotateGrid, self + rul0::kCellGrid,
                                                                self + rul0::kCellWidth, throwaway, turns, 0u);
    uint32_t& minX = At<uint32_t>(self, rul0::kMinX);
    uint32_t& minZ = At<uint32_t>(self, rul0::kMinZ);
    uint32_t& maxX = At<uint32_t>(self, rul0::kMaxX);
    uint32_t& maxZ = At<uint32_t>(self, rul0::kMaxZ);
    if (turns & 2) {
        const uint32_t a = minX, b = minZ;
        minX = maxX;
        maxX = a;
        minZ = maxZ;
        maxZ = b;
        minX = 0u - minX;
        minZ = 0u - minZ;
        maxX = 0u - maxX;
        maxZ = 0u - maxZ;
    }
    if (turns & 1) {
        const uint32_t oldMinX = minX, oldMaxZ = maxZ, oldMaxX = maxX;
        minZ = oldMinX;
        minX = 0u - oldMaxZ;
        maxZ = oldMaxX;
        maxX = 0u - oldMinX;
    }
    At<uint8_t>(self, rul0::kRotation) = static_cast<uint8_t>((At<uint8_t>(self, rul0::kRotation) + turns * 2) & 7);
}

// ====================================================================================
// simulators.nettool.rul0: the layout parsers
// ====================================================================================

// A point appended to one of the rule's point vectors as the game's inlined
// push_back does: in place when there is room (the store skipped for a null
// end), else SC4PointVector::InsertN (where, value*, tag*, 1, 1).
void PushPoint(char* self, std::size_t vector, int32_t x, int32_t z)
{
    Vector& v = At<Vector>(self, vector);
    int32_t value[2] = {x, z};
    if (v.end != v.capacity) {
        if (v.end) {
            At<int32_t>(v.end, point::kX) = x;
            At<int32_t>(v.end, point::kZ) = z;
        }
        v.end += point::kSize;
        return;
    }
    uint8_t tag = 0;
    CallThis<void, char*, int32_t*, uint8_t*, uint32_t, uint32_t>(addr::SC4PointVector_InsertN, &v, v.end, value,
                                                                  &tag, 1u, 1u);
}

// The bounds widened to (x, z): ParseCellLayout's four compares.
void Widen(char* self, int32_t x, int32_t z)
{
    if (x < At<int32_t>(self, rul0::kMinX))
        At<int32_t>(self, rul0::kMinX) = x;
    if (At<int32_t>(self, rul0::kMaxX) < x)
        At<int32_t>(self, rul0::kMaxX) = x;
    if (z < At<int32_t>(self, rul0::kMinZ))
        At<int32_t>(self, rul0::kMinZ) = z;
    if (At<int32_t>(self, rul0::kMaxZ) < z)
        At<int32_t>(self, rul0::kMaxZ) = z;
}

// A vector<uint8> made `count` bytes long the way the parsers inline it: cut
// when shorter (end = begin + count), else grown by InsertN with a zero.
void ResizeBytes(Vector* bytes, uint32_t count)
{
    const uint32_t size = static_cast<uint32_t>(bytes->end - bytes->begin);
    uint8_t fill = 0;
    if (count < size)
        bytes->end = bytes->begin + count;
    else
        CallThis<void, char*, uint32_t, uint8_t*>(addr::SC4ByteVector_InsertN, bytes, bytes->end, count - size, &fill);
}

// Every byte of a vector<uint8> zeroed (the parsers' REP STOS).
void ZeroBytes(Vector* bytes)
{
    if (bytes->begin != bytes->end)
        std::memset(bytes->begin, 0, static_cast<std::size_t>(bytes->end - bytes->begin));
}

// The list of strings the parsers get: a pointer to the list's head node;
// a node is +0x0 the next node, then a game string at +0x8 whose characters'
// begin and end are at +0xC and +0x10.
inline constexpr std::size_t kLineBegin = 0xC, kLineEnd = 0x10;

// 0x00609FC0 (ret 4: the CellLayout lines). The bounds start at (1000, 1000,
// -1000, -1000), the anchor at (-1, -1). Row z is the line, column x the
// character (jump tables 0x0060A50C / 0x0060A520): ' ' and '.' nothing; '+'
// a '+' point and a checked point, bounds widened; '<' the anchor's row is
// z; '^' its column is x; anything else, when isalnum (at its address, with
// the character sign-extended) and a check type of that character exists, a
// cell (x, z, character) appended (in place or SC4Vector12::InsertOverflow)
// and, when the type's byte +0x1D is set, the bounds widened and a checked
// point appended. Then everything is moved by -anchor; the cell grid +0xB8
// resized to (maxZ - minZ + 1) * (maxX - minX + 1) and zeroed, +0xC4 its
// width; for each checked point: the grid's byte (z' * width + x') =
// ((z' << 4) + x' + 1) & 0xFF, that value's cons cell found or made (the
// table grown by SC4HashTable::Resize for count + 1, a 16-byte node from the
// STL allocator put first in its bucket), its word ((x' << 4) + z') << 4, its
// byte +0xC 0 and +0xD 1, or 0 when a cell has the point's coordinates.
void __fastcall ParseCellLayout(char* self, void*, char** lines)
{
    static bool logged = false;
    FirstCall("ParseCellLayout", logged);
    char* const head = *lines;
    int32_t anchorZ = -1, anchorX = -1;
    At<int32_t>(self, rul0::kMinX) = rul0::kStartMin;
    At<int32_t>(self, rul0::kMinZ) = rul0::kStartMin;
    At<int32_t>(self, rul0::kMaxX) = rul0::kStartMax;
    At<int32_t>(self, rul0::kMaxZ) = rul0::kStartMax;
    int32_t z = 0;
    for (char* line = At<char*>(head, 0); line != head; line = At<char*>(line, 0), ++z) {
        const char* const end = At<char*>(line, kLineEnd);
        int32_t x = 0;
        for (const char* p = At<char*>(line, kLineBegin); p != end; ++p, ++x) {
            const int8_t c = static_cast<int8_t>(*p);
            switch (c) {
            case ' ':
            case '.':
                break;
            case '+':
                PushPoint(self, rul0::kPoints, x, z);
                PushPoint(self, rul0::kChecked, x, z);
                Widen(self, x, z);
                break;
            case '<':
                anchorZ = z;
                break;
            case '^':
                anchorX = x;
                break;
            default: {
                if (!CallCdecl<int32_t, int32_t>(addr::SC4_isalnum, static_cast<int32_t>(c)))
                    break;
                char* const type = FindByte(self + rul0::kCheckTypes, static_cast<uint8_t>(c));
                if (!type)
                    break;
                // The key as the game's stack word: one byte written, the
                // three after it the game's leftovers (ours zero).
                uint32_t cell[3] = {static_cast<uint32_t>(x), static_cast<uint32_t>(z), static_cast<uint8_t>(c)};
                Vector& cells = At<Vector>(self, rul0::kCells);
                if (cells.end != cells.capacity) {
                    if (cells.end)
                        std::memcpy(cells.end, cell, sizeof cell);
                    cells.end += cellrec::kSize;
                } else {
                    uint8_t tag = 0;
                    CallThis<void, char*, uint32_t*, uint8_t*, uint32_t, uint32_t>(
                        addr::SC4Vector12_InsertOverflow, &cells, cells.end, cell, &tag, 1u, 1u);
                }
                if (At<uint8_t>(type, checktype::kChecked)) {
                    Widen(self, x, z);
                    PushPoint(self, rul0::kChecked, x, z);
                }
                break;
            }
            }
        }
    }

    At<int32_t>(self, rul0::kMinX) -= anchorX;
    At<int32_t>(self, rul0::kMinZ) -= anchorZ;
    At<int32_t>(self, rul0::kMaxX) -= anchorX;
    At<int32_t>(self, rul0::kMaxZ) -= anchorZ;
    const auto move = [&](char* p) {
        At<int32_t>(p, point::kX) -= anchorX;
        At<int32_t>(p, point::kZ) -= anchorZ;
    };
    ForEach(self, rul0::kChecked, point::kSize, move);
    ForEach(self, rul0::kPoints, point::kSize, move);
    ForEach(self, rul0::kCells, cellrec::kSize, move);

    const uint32_t height = static_cast<uint32_t>(At<int32_t>(self, rul0::kMaxZ) - At<int32_t>(self, rul0::kMinZ)) + 1u;
    const uint32_t widthNow =
        static_cast<uint32_t>(At<int32_t>(self, rul0::kMaxX) - At<int32_t>(self, rul0::kMinX)) + 1u;
    Vector* const grid = &At<Vector>(self, rul0::kCellGrid);
    ResizeBytes(grid, height * widthNow);
    At<int32_t>(self, rul0::kCellWidth) = At<int32_t>(self, rul0::kMaxX) - At<int32_t>(self, rul0::kMinX) + 1;
    ZeroBytes(grid);

    Vector& checked = At<Vector>(self, rul0::kChecked);
    char* const checkedEnd = checked.end;
    for (char* p = checked.begin; p != checkedEnd; p += point::kSize) {
        const int32_t px = At<int32_t>(p, point::kX), pz = At<int32_t>(p, point::kZ);
        const uint32_t xr = static_cast<uint32_t>(px - At<int32_t>(self, rul0::kMinX));
        const uint32_t zr = static_cast<uint32_t>(pz - At<int32_t>(self, rul0::kMinZ));
        const uint32_t index = static_cast<uint32_t>(At<int32_t>(self, rul0::kCellWidth)) * zr + xr;
        const uint8_t key = static_cast<uint8_t>(static_cast<uint8_t>(zr << 4) + static_cast<uint8_t>(xr) + 1);
        At<uint8_t>(grid->begin, index) = key;

        char* const table = self + rul0::kConsCells;
        char* node = FindByte(table, key);
        if (!node) {
            CallThis<void, uint32_t>(addr::SC4HashTable_Resize, table, At<uint32_t>(table, table::kCount) + 1);
            const uint32_t bucket = static_cast<uint32_t>(key) % BucketCount(table);
            char* const next = Buckets(table)[bucket];
            node = Allocate(conscell::kNodeSize);
            At<char*>(node, table::kNext) = nullptr;
            // The game tests node + 4 against null here (always true).
            At<uint8_t>(node, table::kKey) = key;
            At<uint32_t>(node, conscell::kValue) = 0;
            At<uint32_t>(node, conscell::kTurn) = 0;
            At<char*>(node, table::kNext) = next;
            Buckets(table)[bucket] = node;
            ++At<uint32_t>(table, table::kCount);
        }
        char* const value = node + conscell::kValue;
        At<uint8_t>(value, conscell::kTurn - conscell::kValue) = 0;
        At<uint32_t>(value, 0) = ((xr << 4) + zr) << 4;
        At<uint8_t>(value, conscell::kAlone - conscell::kValue) = 1;
        Vector& cells = At<Vector>(self, rul0::kCells);
        for (char* c = cells.begin; c != cells.end; c += cellrec::kSize) {
            if (At<int32_t>(c, cellrec::kX) == px && At<int32_t>(c, cellrec::kZ) == pz) {
                At<uint8_t>(value, conscell::kAlone - conscell::kValue) = 0;
                break;
            }
        }
    }
}

// 0x0060A560 (ret 4: the ConsLayout lines): the anchor +0xAC/+0xB0 set to
// -1; the width +0xA8 the longest line (signed, from 0); the cons grid +0x9C
// resized to lines * width and zeroed; then each character at (x, z) (jump
// tables 0x0060A738 / 0x0060A764): '-' 1, '|' 2, '/' 3, '\' 4, 'F' 5, 'R' 6,
// '#' 7, 'X' 8 into the grid at z * width + x; '^' the anchor's column x, '<'
// its row z; anything else nothing.
void __fastcall ParseConsLayout(char* self, void*, char** lines)
{
    static bool logged = false;
    FirstCall("ParseConsLayout", logged);
    char* const head = *lines;
    uint32_t count = 0;
    for (char* line = At<char*>(head, 0); line != head; line = At<char*>(line, 0))
        ++count;
    At<int32_t>(self, rul0::kConsAnchorX) = -1;
    At<int32_t>(self, rul0::kConsAnchorZ) = -1;
    int32_t width = 0;
    for (char* line = At<char*>(head, 0); line != head; line = At<char*>(line, 0)) {
        const int32_t length = static_cast<int32_t>(At<char*>(line, kLineEnd) - At<char*>(line, kLineBegin));
        if (width < length)
            width = length;
    }
    Vector* const grid = &At<Vector>(self, rul0::kConsGrid);
    ResizeBytes(grid, count * static_cast<uint32_t>(width));
    At<int32_t>(self, rul0::kConsWidth) = width;
    ZeroBytes(grid);
    int32_t z = 0;
    uint32_t offset = 0;
    for (char* line = At<char*>(head, 0); line != head;
         line = At<char*>(line, 0), ++z, offset += static_cast<uint32_t>(width)) {
        const char* const end = At<char*>(line, kLineEnd);
        int32_t x = 0;
        for (const char* p = At<char*>(line, kLineBegin); p != end; ++p, ++x) {
            uint8_t value = 0;
            switch (static_cast<int8_t>(*p)) {
            case '-':
                value = 1;
                break;
            case '|':
                value = 2;
                break;
            case '/':
                value = 3;
                break;
            case '\\':
                value = 4;
                break;
            case 'F':
                value = 5;
                break;
            case 'R':
                value = 6;
                break;
            case '#':
                value = 7;
                break;
            case 'X':
                value = 8;
                break;
            case '<':
                At<int32_t>(self, rul0::kConsAnchorZ) = z;
                continue;
            case '^':
                At<int32_t>(self, rul0::kConsAnchorX) = x;
                continue;
            default:
                continue;
            }
            At<uint8_t>(grid->begin, offset + static_cast<uint32_t>(x)) = value;
        }
    }
}

// ====================================================================================
// simulators.nettool.rul0: the rule's life and copies
// ====================================================================================

// 0x00624E60 (cdecl: first, last, destination, tag; EAX the destination's
// end): 16-byte records copied (word +0, bytes +4 and +5, words +8 and +0xC;
// the bytes +6 and +7 not); a null destination is only advanced.
char* __cdecl CopyRecords(const char* first, const char* last, char* destination, void*)
{
    static bool logged = false;
    FirstCall("CopyRecords", logged);
    for (; first != last; first += record::kSize, destination += record::kSize) {
        if (!destination)
            continue;
        At<uint32_t>(destination, record::kWord) = At<uint32_t>(const_cast<char*>(first), record::kWord);
        At<uint8_t>(destination, record::kTurn) = At<uint8_t>(const_cast<char*>(first), record::kTurn);
        At<uint8_t>(destination, record::kFlip) = At<uint8_t>(const_cast<char*>(first), record::kFlip);
        At<uint32_t>(destination, record::kX) = At<uint32_t>(const_cast<char*>(first), record::kX);
        At<uint32_t>(destination, record::kZ) = At<uint32_t>(const_cast<char*>(first), record::kZ);
    }
    return destination;
}

// 0x00625190 (ret 4: the source vector): this emptied, a buffer of the
// source's count (signed >> 4) of records from the STL allocator (none for
// 0), the records copied by 0x00624E60 at its address. EAX this.
char* __fastcall CopyRecordVector(char* self, void*, Vector* source)
{
    static bool logged = false;
    FirstCall("CopyRecordVector", logged);
    const int32_t count = static_cast<int32_t>((source->end - source->begin) >> 4);
    Vector& v = *reinterpret_cast<Vector*>(self);
    v.begin = v.end = v.capacity = nullptr;
    char* const buffer = count ? Allocate(static_cast<uint32_t>(count) << 4) : nullptr;
    v.begin = buffer;
    v.end = buffer;
    v.capacity = buffer + (static_cast<uint32_t>(count) << 4);
    v.end = CallCdecl<char*, char*, char*, char*, Vector**>(addr::SC4Rul0Rule_CopyRecords, source->begin,
                                                            source->end, buffer, &source);
    return self;
}

// 0x00626160 (no arguments): the rule's buffers freed in the game's order:
// the cons cells (cleared by 0x008EBA60, then their buckets), the two grids,
// the string (its vtable set back to 0x00A80810, its characters), the check
// types (cleared, buckets), the cells, the checked points, the vector +0x34,
// the '+' points and the records.
void __fastcall Destruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("Destruct", logged);
    CallThis<void>(addr::SC4Uint32HashSet_Clear, self + rul0::kConsCells);
    FreeAt(self, rul0::kConsCells + table::kBuckets);
    FreeAt(self, rul0::kCellGrid);
    FreeAt(self, rul0::kConsGrid);
    At<std::uintptr_t>(self, rul0::kText) = addr::SC4String_Vtable;
    FreeAt(self, rul0::kTextBegin);
    CallThis<void>(addr::SC4Uint32HashSet_Clear, self + rul0::kCheckTypes);
    FreeAt(self, rul0::kCheckTypes + table::kBuckets);
    FreeAt(self, rul0::kCells);
    FreeAt(self, rul0::kChecked);
    FreeAt(self, rul0::kPoints34);
    FreeAt(self, rul0::kPoints);
    FreeAt(self, rul0::kRecords);
}

// The two table copies (0x00626430, 0x006265B0): this's buckets emptied (end
// = begin), reserved and filled with as many nulls as the source has buckets
// (PtrVector::Reserve, SC4PtrVector::FillInsert with a zero), then each
// source bucket's chain copied in order into nodes of `nodeSize` bytes from
// the STL allocator (next null, the key byte, then `words` words from +0x8),
// the count copied.
void CopyTable(char* self, char* source, uint32_t nodeSize, std::size_t words)
{
    Vector& buckets = At<Vector>(self, table::kBuckets);
    buckets.end = buckets.begin;
    CallThis<void, uint32_t>(addr::PtrVector_Reserve, &buckets, BucketCount(source));
    uint32_t zero = 0;
    CallThis<void, char*, uint32_t, uint32_t*>(addr::SC4PtrVector_FillInsert, &buckets, buckets.end,
                                               BucketCount(source), &zero);
    const auto copy = [&](char* from) {
        char* const node = Allocate(nodeSize);
        At<char*>(node, table::kNext) = nullptr;
        // The game tests node + 4 against null here (always true).
        At<uint8_t>(node, table::kKey) = At<uint8_t>(from, table::kKey);
        for (std::size_t i = 0; i < words; ++i)
            At<uint32_t>(node, 0x8 + 4 * i) = At<uint32_t>(from, 0x8 + 4 * i);
        return node;
    };
    for (uint32_t i = 0; i < BucketCount(source); ++i) {
        char* from = Buckets(source)[i];
        if (!from)
            continue;
        char* last = copy(from);
        Buckets(self)[i] = last;
        for (from = At<char*>(from, table::kNext); from; from = At<char*>(from, table::kNext)) {
            char* const node = copy(from);
            At<char*>(last, table::kNext) = node;
            last = node;
        }
    }
    At<uint32_t>(self, table::kCount) = At<uint32_t>(source, table::kCount);
}

// 0x00626430 (ret 4: the source table): the check types (32-byte nodes, six
// words after the key).
void __fastcall CopyCheckTypes(char* self, void*, char* source)
{
    static bool logged = false;
    FirstCall("CopyCheckTypes", logged);
    CopyTable(self, source, checktype::kNodeSize, checktype::kCopiedWords);
}

// 0x006265B0 (ret 4: the source table): the cons cells (16-byte nodes, two
// words after the key).
void __fastcall CopyConsCells(char* self, void*, char* source)
{
    static bool logged = false;
    FirstCall("CopyConsCells", logged);
    CopyTable(self, source, conscell::kNodeSize, 2);
}

// The words of a range copied from the source rule.
void CopyWords(char* self, const char* source, std::size_t from, std::size_t to)
{
    for (std::size_t at = from; at < to; at += 4)
        At<uint32_t>(self, at) = At<uint32_t>(const_cast<char*>(source), at);
}

// 0x0062A1D0 (ret 4: the source rule): every field in the game's order:
// +0x0; the records (0x00625190); +0x10 to +0x24; the '+' points and +0x34
// (SC4PointVector::CopyConstruct); +0x40, +0x44, the byte +0x48; the checked
// points; the cells (SC4Vector12::CopyConstruct); the check types (their
// buckets and count cleared, 0x00626430); +0x78, +0x7C; the string
// (SC4String::CopyBuffer of the source's buffer, or null when the source's
// string were at null, 0, -1; then the vtable and a reference count of 0);
// +0x94, +0x98; the cons grid (SC4ByteVector::CopyConstruct); +0xA8 to
// +0xB4; the cell grid; +0xC4 to +0xCC; the cons cells (cleared, 0x006265B0);
// +0xE4, the bytes +0xE8, +0xE9, +0xEC to +0xF8. EAX this.
char* __fastcall CopyConstruct(char* self, void*, char* source)
{
    static bool logged = false;
    FirstCall("CopyConstruct", logged);
    At<uint32_t>(self, rul0::kId) = At<uint32_t>(source, rul0::kId);
    CallThis<void, char*>(addr::SC4Rul0Rule_CopyRecordVector, self + rul0::kRecords, source + rul0::kRecords);
    CopyWords(self, source, rul0::kWord10, rul0::kPoints);
    CallThis<void, char*>(addr::SC4PointVector_CopyConstruct, self + rul0::kPoints, source + rul0::kPoints);
    CallThis<void, char*>(addr::SC4PointVector_CopyConstruct, self + rul0::kPoints34, source + rul0::kPoints34);
    CopyWords(self, source, rul0::kWord40, rul0::kByte48);
    At<uint8_t>(self, rul0::kByte48) = At<uint8_t>(source, rul0::kByte48);
    CallThis<void, char*>(addr::SC4PointVector_CopyConstruct, self + rul0::kChecked, source + rul0::kChecked);
    CallThis<void, char*>(addr::SC4Vector12_CopyConstruct, self + rul0::kCells, source + rul0::kCells);
    for (std::size_t at = table::kBuckets; at <= table::kCount; at += 4)
        At<uint32_t>(self, rul0::kCheckTypes + at) = 0;
    CallThis<void, char*>(addr::SC4Rul0Rule_CopyCheckTypes, self + rul0::kCheckTypes, source + rul0::kCheckTypes);
    CopyWords(self, source, rul0::kWord78, rul0::kText);
    // The game tests the source string's address against null (never true).
    char* const textObject = source + rul0::kText;
    char* const text = textObject ? source + rul0::kTextBegin : nullptr;
    char* tag = source;
    CallThis<void, char*, uint32_t, uint32_t, char**>(addr::SC4String_CopyBuffer, self + rul0::kTextBegin, text, 0u,
                                                      0xFFFFFFFFu, &tag);
    At<std::uintptr_t>(self, rul0::kText) = addr::SC4String_Vtable;
    At<uint32_t>(self, rul0::kTextReferences) = 0;
    CopyWords(self, source, rul0::kOffsetX, rul0::kConsGrid);
    CallThis<void, char*>(addr::SC4ByteVector_CopyConstruct, self + rul0::kConsGrid, source + rul0::kConsGrid);
    CopyWords(self, source, rul0::kConsWidth, rul0::kCellGrid);
    CallThis<void, char*>(addr::SC4ByteVector_CopyConstruct, self + rul0::kCellGrid, source + rul0::kCellGrid);
    CopyWords(self, source, rul0::kCellWidth, rul0::kConsCells);
    for (std::size_t at = table::kBuckets; at <= table::kCount; at += 4)
        At<uint32_t>(self, rul0::kConsCells + at) = 0;
    CallThis<void, char*>(addr::SC4Rul0Rule_CopyConsCells, self + rul0::kConsCells, source + rul0::kConsCells);
    At<uint32_t>(self, rul0::kWordE4) = At<uint32_t>(source, rul0::kWordE4);
    At<uint8_t>(self, rul0::kRotation) = At<uint8_t>(source, rul0::kRotation);
    At<uint8_t>(self, rul0::kByteE9) = At<uint8_t>(source, rul0::kByteE9);
    CopyWords(self, source, rul0::kWordsEC, rul0::kSize);
    return self;
}

// 0x0062A480 (no arguments): an empty rule. The words and vectors of the
// game's list cleared (+0x4 to +0x14, +0x28 to +0x3C, +0x4C to +0x60, +0x78,
// +0x94 to +0xC0, +0xC8, +0xCC, +0xE4), the byte +0x48 and the bytes +0xE8,
// +0xE9; the two tables' buckets and counts cleared and constructed with 100
// buckets (0x00829BC0); the string: an 8-byte buffer from the STL allocator
// holding an empty text, the vtable 0x00A80810, no references; +0xEC to
// +0xF8 -1. +0x0, +0x18 to +0x24, +0x40, +0x44, +0x7C and +0xC4 are not
// written. EAX this.
char* __fastcall Construct(char* self, void*)
{
    static bool logged = false;
    FirstCall("Construct", logged);
    for (const std::size_t at : {std::size_t{0x04}, std::size_t{0x08}, std::size_t{0x0C}, std::size_t{0x10},
                                 std::size_t{0x14}, std::size_t{0x28}, std::size_t{0x2C}, std::size_t{0x30},
                                 std::size_t{0x34}, std::size_t{0x38}, std::size_t{0x3C}})
        At<uint32_t>(self, at) = 0;
    At<uint8_t>(self, rul0::kByte48) = 0;
    for (std::size_t at = rul0::kChecked; at < rul0::kCheckTypes; at += 4)
        At<uint32_t>(self, at) = 0;
    for (std::size_t at = table::kBuckets; at <= table::kCount; at += 4)
        At<uint32_t>(self, rul0::kCheckTypes + at) = 0;
    CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Construct, self + rul0::kCheckTypes, table::kFirstBuckets);
    At<uint32_t>(self, rul0::kWord78) = 0;
    At<char*>(self, rul0::kTextBegin) = nullptr;
    At<char*>(self, rul0::kTextEnd) = nullptr;
    At<char*>(self, rul0::kTextCapacity) = nullptr;
    char* const text = Allocate(rul0::kFirstTextBuffer);
    At<char*>(self, rul0::kTextBegin) = text;
    At<char*>(self, rul0::kTextEnd) = text;
    At<char*>(self, rul0::kTextCapacity) = text + rul0::kFirstTextBuffer;
    At<uint8_t>(text, 0) = 0;
    At<std::uintptr_t>(self, rul0::kText) = addr::SC4String_Vtable;
    At<uint32_t>(self, rul0::kTextReferences) = 0;
    for (std::size_t at = rul0::kOffsetX; at <= 0xC0; at += 4)
        At<uint32_t>(self, at) = 0;
    At<uint32_t>(self, rul0::kWordC8) = 0;
    At<uint32_t>(self, rul0::kWordCC) = 0;
    for (std::size_t at = table::kBuckets; at <= table::kCount; at += 4)
        At<uint32_t>(self, rul0::kConsCells + at) = 0;
    CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Construct, self + rul0::kConsCells, table::kFirstBuckets);
    for (std::size_t at = rul0::kWordsEC; at < rul0::kSize; at += 4)
        At<int32_t>(self, at) = -1;
    At<uint32_t>(self, rul0::kWordE4) = 0;
    At<uint8_t>(self, rul0::kRotation) = 0;
    At<uint8_t>(self, rul0::kByteE9) = 0;
    return self;
}

// ====================================================================================
// simulators.nettool.rul0: the store
// ====================================================================================

// 0x0062A3C0 (no arguments; the store 0x00B452B0): each bucket's nodes'
// rules destroyed (0x00626160) and the nodes freed, the bucket nulled; the
// count 0 (also when there were no buckets).
void __fastcall StoreClear(char* self, void*)
{
    static bool logged = false;
    FirstCall("StoreClear", logged);
    for (uint32_t i = 0; i < BucketCount(self); ++i) {
        char* node = Buckets(self)[i];
        while (node) {
            char* const next = At<char*>(node, table::kNext);
            CallThis<void>(addr::SC4Rul0Rule_Destruct, node + store::kRule);
            Free(node);
            node = next;
        }
        Buckets(self)[i] = nullptr;
    }
    At<uint32_t>(self, table::kCount) = 0;
}

// 0x0062C160 (ret 4: a pair, the key and a rule): the table grown for one
// more (SC4Uint32HashSet::Resize), the key's bucket (key % buckets after the
// growth), a 0x104-byte node from the STL allocator: the key and a copy of
// the rule (0x0062A1D0), put first in its bucket; the count + 1. EAX node +
// 4.
char* __fastcall InsertNode(char* self, void*, char* pair)
{
    static bool logged = false;
    FirstCall("InsertNode", logged);
    CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Resize, self, At<uint32_t>(self, table::kCount) + 1);
    const uint32_t bucket = At<uint32_t>(pair, 0) % BucketCount(self);
    char* const next = Buckets(self)[bucket];
    char* const node = Allocate(store::kNodeSize);
    At<char*>(node, table::kNext) = nullptr;
    // The game tests node + 4 against null here (always true).
    At<uint32_t>(node, table::kKey) = At<uint32_t>(pair, 0);
    CallThis<void, char*>(addr::SC4Rul0Rule_CopyConstruct, node + store::kRule, pair + store::kPairRule);
    At<char*>(node, table::kNext) = next;
    Buckets(self)[bucket] = node;
    At<uint32_t>(self, table::kCount) = At<uint32_t>(self, table::kCount) + 1;
    return node + table::kKey;
}

// 0x0062C2F0 (ret 4: the key*; EAX the rule): the key's node (bucket key %
// buckets), else an empty rule made (0x0062A480) and copied (0x0062A1D0) into
// a pair with the key, the pair inserted (0x0062C160), then the pair's rule
// and the empty one destroyed (0x00626160), in that order. Both temporaries
// start zeroed here (the game's start as its stack leftovers: the top of the
// file).
char* __fastcall FindOrInsert(char* self, void*, const uint32_t* key)
{
    static bool logged = false;
    FirstCall("FindOrInsert", logged);
    const uint32_t wanted = *key;
    for (char* node = Buckets(self)[wanted % BucketCount(self)]; node; node = At<char*>(node, table::kNext)) {
        if (At<uint32_t>(node, table::kKey) == wanted)
            return node + store::kRule;
    }
    alignas(4) char pair[store::kPairSize] = {};
    alignas(4) char empty[rul0::kSize] = {};
    char* const made = CallThis<char*>(addr::SC4Rul0Rule_Construct, empty);
    At<uint32_t>(pair, 0) = *key;
    CallThis<char*, char*>(addr::SC4Rul0Rule_CopyConstruct, pair + store::kPairRule, made);
    char* const inserted = CallThis<char*, char*>(addr::SC4Rul0Store_InsertNode, self, pair);
    CallThis<void>(addr::SC4Rul0Rule_Destruct, pair + store::kPairRule);
    CallThis<void>(addr::SC4Rul0Rule_Destruct, empty);
    return inserted + 4;
}

// 0x00633970 (ret 4: a pair of words*; EAX the node or null): the node of a
// table keyed by two words: bucket (second + 2 * first) % buckets, the chain
// walked for both words equal.
char* __fastcall PairFind(char* self, void*, const uint32_t* key)
{
    static bool logged = false;
    FirstCall("PairFind", logged);
    const uint32_t first = key[0], second = key[1];
    char* node = Buckets(self)[(second + first * 2) % BucketCount(self)];
    while (node && !(At<uint32_t>(node, 0x4) == first && At<uint32_t>(node, 0x8) == second))
        node = At<char*>(node, table::kNext);
    return node;
}

// ====================================================================================
// simulators.nettool.rulload: the load, the per-network ini and the statics
// ====================================================================================

// The resources LoadRules reads: the network ini (type 0, group, instance;
// built on its stack) and the three RUL files' keys in the game's data
// (0x0A5BCF4B, 0xAA5BCF57, instance 0x10000001 / 0x10000002 / 0x10000000).
inline constexpr uint32_t kIniGroup = 0x8A5971C5, kIniInstance = 0x8A5993B9;
inline constexpr std::uintptr_t kRul1Key = 0x00B0B544, kRul2Key = 0x00B0B550, kRul0Key = 0x00B0B538;
// The section names (C strings in .rdata).
inline constexpr std::uintptr_t kRuleOverrides = 0x00AA86F0, kTileVerifiers = 0x00AA85B4;
inline constexpr std::size_t kIniFileSize = 0x3C;

// The type table's rows.
char* TypeRow(uint32_t type)
{
    return reinterpret_cast<char*>(addr::cSC4NetworkConstructionCrew_NetworkToolTuning + type * kRowSize);
}

// Whether OpenResourceRecord found the key; the record goes to the one slot
// LoadRules reuses for every file.
bool Open(const void* key, void** record)
{
    return CallCdecl<uint8_t, const void*, void**, uint32_t>(addr::SC4_OpenResourceRecord, key, record, 0u) != 0;
}

// 0x006253D0 (cdecl, no arguments; once per resource load, from Init
// 0x006269C0 and LoadRulesOnce 0x00626990). One record slot for the four
// files (released once at the end, as the game does); each found file read
// through an ini file object on the stack (0x3C bytes):
//   - the network ini (type 0, 0x8A5971C5, 0x8A5993B9): LoadTypeInfo;
//   - RUL 0x10000001: LoadRul1, then ReadRul1TypeIds into 13 zeroed words,
//     each copied to its type row's +0xFC;
//   - RUL 0x10000002: the RUL2 multimap emptied when it has entries (its
//     eraser on the root, the head linked to itself, the count 0), then
//     LoadRul2 with 'RuleOverrides';
//   - RUL 0x10000000: LoadRul0 into the store 0x00B452B0.
void __cdecl LoadRules()
{
    static bool logged = false;
    FirstCall("LoadRules", logged);
    void* record = nullptr;
    const uint32_t iniKey[3] = {0u, kIniGroup, kIniInstance};
    alignas(4) uint8_t reader[kIniFileSize];
    if (Open(iniKey, &record)) {
        CallThis<void*, void*>(addr::cRZIniFile_ConstructFromRecord, reader, record);
        CallCdecl<void, void*>(addr::cSC4NetworkTool_LoadTypeInfo, reader);
        CallThis<void>(addr::cRZIniFile_Destruct, reader);
    }
    if (Open(reinterpret_cast<const void*>(kRul1Key), &record)) {
        CallThis<void*, void*>(addr::cRZIniFile_ConstructFromRecord, reader, record);
        CallCdecl<void, void*>(addr::cSC4NetworkTool_LoadRul1, reader);
        uint32_t ids[kTypes] = {};
        CallCdecl<void, void*, uint32_t*>(addr::cSC4NetworkTool_ReadRul1TypeIds, reader, ids);
        for (uint32_t type = 0; type < kTypes; ++type)
            At<uint32_t>(TypeRow(type), typerow::kRul1Id) = ids[type];
        CallThis<void>(addr::cRZIniFile_Destruct, reader);
    }
    if (Open(reinterpret_cast<const void*>(kRul2Key), &record)) {
        CallThis<void*, void*>(addr::cRZIniFile_ConstructFromRecord, reader, record);
        char* const map = reinterpret_cast<char*>(addr::cSC4NetworkTool_Rul2Overrides);
        if (At<uint32_t>(map, rul2::kCount) != 0) {
            CallThis<void, char*>(addr::SC4Rul2Map_EraseSubtree, map, At<char*>(At<char*>(map, 0), tree::kParent));
            At<char*>(At<char*>(map, 0), tree::kLeft) = At<char*>(map, 0);
            At<char*>(At<char*>(map, 0), tree::kParent) = nullptr;
            At<char*>(At<char*>(map, 0), tree::kRight) = At<char*>(map, 0);
            At<uint32_t>(map, rul2::kCount) = 0;
        }
        CallCdecl<void, void*, std::uintptr_t>(addr::cSC4NetworkTool_LoadRul2, reader, kRuleOverrides);
        CallThis<void>(addr::cRZIniFile_Destruct, reader);
    }
    if (Open(reinterpret_cast<const void*>(kRul0Key), &record)) {
        CallThis<void*, void*>(addr::cRZIniFile_ConstructFromRecord, reader, record);
        CallCdecl<void, void*, std::uintptr_t>(addr::cSC4NetworkTool_LoadRul0, reader, addr::cSC4NetworkTool_Rul0Store);
        CallThis<void>(addr::cRZIniFile_Destruct, reader);
    }
    if (record)
        ReleaseOf(record);
}

// What LoadTypeInfo writes and loads per row, in the game's row order.
struct TypeSetup {
    uint32_t type;
    uint32_t ids[typerow::kIdCount]; // +0x08 to +0x20
    uint32_t word0;
    bool loads; // the files, bridges, exemplars and parameters (all but row 5)
    std::uintptr_t files[2]; // resource keys in the game's data
    std::uintptr_t bridges; // the ini section's name
    uint32_t bridgeId, bridgeFlag;
    bool exemplars; // LoadExemplars(0x8974F80F)
    uint32_t params; // LoadParams' exemplar ID
    bool diagonal; // the byte +0x75 set (row 3)
};

inline constexpr uint32_t kTypeExemplars = 0x8974F80F;

// The table of 0x00623310's unrolled code, row by row.
const TypeSetup kTypeSetups[] = {
    {0, {0xA766C101, 0x2821ED93, 0, 0xE17BB266, 0x49E13260, 1, 1}, 1, true, {0x00B0B424, 0x00B0B418},
     0x00AA86D4, 0x1000, 1, true, 0x0781214F, false},
    {10, {0x2BE08438, 0xCBE084CB, 0, 0xABE091A8, 0, 1, 1}, 1, true, {0x00B0B4E4, 0x00B0B4D8},
     0x00AA86B8, 0x100A, 1, true, 0x8BDA0C2B, false},
    {11, {0x4BE08AFB, 0x6BE08658, 0, 0x4BE091B2, 0, 1, 1}, 1, true, {0x00B0B4FC, 0x00B0B4F0},
     0x00AA86A0, 0x100B, 1, true, 0xEBE08CDA, false},
    {1, {0xC8433ECB, 0xE8347989, 0, 0x217BB308, 0, 1, 1}, 2, true, {0x00B0B454, 0x00B0B448},
     0x00AA868C, 0x1001, 1, true, 0x0814E612, false},
    {8, {0x0B79E2E1, 0x2B79DFFB, 0, 0xAB79E761, 0, 1, 1}, 2, true, {0x00B0B4CC, 0x00B0B4C0},
     0x00AA8670, 0x1008, 1, true, 0xCB79E089, false},
    {9, {0xABE08433, 0xEBE084C2, 0, 0xEBE091B6, 0, 1, 1}, 2, true, {0x00B0B514, 0x00B0B508},
     0x00AA8658, 0x1009, 1, true, 0xEBE08882, false},
    {3, {0x092A047D, 0xA92A02EA, 0, 0x2A920BF9, 0, 1, 1}, 1, true, {0x00B0B484, 0x00B0B478},
     0x00AA8640, 0x1003, 1, true, 0x492A03A3, true},
    {2, {0x08433F47, 0xA8434037, 0, 0x8BF30A8B, 0, 2, 2}, 0, true, {0x00B0B46C, 0x00B0B460},
     0x00AA8628, 0x1002, 2, false, 0xA8433F6B, false},
    {12, {0x6BE0843B, 0xEBE084D1, 0, 0x6BE091B4, 0, 2, 2}, 0, true, {0x00B0B52C, 0x00B0B520},
     0x00AA8608, 0x100C, 2, false, 0x0BE08C87, false},
    {6, {0x0B730A26, 0xCB730FAC, 0, 0x6B730B83, 0, 2, 2}, 0, true, {0x00B0B4B4, 0x00B0B4A8},
     0x00AA85F0, 0x1006, 2, false, 0x2B730A17, false},
    {4, {0x09511847, 0xA951171C, 0xF0, 0x82C2FF49, 0, 1, 1}, 1, true, {0x00B0B43C, 0x00B0B430},
     0x00AA85DC, 0x1004, 1, false, 0x095117DB, false},
    {7, {0x8A15EE13, 0x8A15F3F2, 0xF1, 0xC17BB329, 0, 1, 1}, 1, true, {0x00B0B49C, 0x00B0B490},
     0x00AA85C4, 0x1007, 1, false, 0x0A367CA7, false},
    {5, {0xA88E1592, 0x088E1962, 0, 0xA16F1463, 0x69E13770, 1, 2}, 0, false, {0, 0}, 0, 0, 0, false, 0, false},
};

// 0x00623310 (cdecl: the network ini): each row of the table above, in its
// order: the row cleared (0x006421D0), the IDs +0x08 to +0x20 and the word
// +0x0 written; unless it is row 5: two rule files (0x0063D360 with a key,
// 0, 0), the bridges (0x006437A0: the ini, the section, the ID, the flag),
// the exemplars (0x0063F780, rows 0, 1, 3, 8, 9, 10, 11), the parameters
// (0x0063D460), row 3's diagonal byte +0x75; the row marked loaded
// (0x0063BF90). Then the tile verifiers (0x0063FAF0 with 'TileVerifiers').
void __cdecl LoadTypeInfo(void* ini)
{
    static bool logged = false;
    FirstCall("LoadTypeInfo", logged);
    for (const TypeSetup& setup : kTypeSetups) {
        char* const row = TypeRow(setup.type);
        CallThis<void>(addr::SC4NetworkTypeInfo_Clear, row);
        for (std::size_t i = 0; i < typerow::kIdCount; ++i)
            At<uint32_t>(row, typerow::kIds + 4 * i) = setup.ids[i];
        At<uint32_t>(row, typerow::kWord0) = setup.word0;
        if (setup.loads) {
            for (const std::uintptr_t file : setup.files)
                CallThis<void, std::uintptr_t, uint32_t, uint32_t>(addr::SC4NetworkTypeInfo_AddRuleFile, row, file, 0u,
                                                                   0u);
            CallThis<void, void*, std::uintptr_t, uint32_t, uint32_t>(addr::SC4NetworkTypeInfo_LoadBridges, row, ini,
                                                                      setup.bridges, setup.bridgeId, setup.bridgeFlag);
            if (setup.exemplars)
                CallThis<void, uint32_t>(addr::SC4NetworkTypeInfo_LoadExemplars, row, kTypeExemplars);
            CallThis<void, uint32_t>(addr::SC4NetworkTypeInfo_LoadParams, row, setup.params);
            if (setup.diagonal)
                At<uint8_t>(row, typerow::kDiagonal) = 1;
        }
        CallThis<void>(addr::SC4NetworkTypeInfo_SetLoaded, row);
    }
    CallCdecl<void, void*, std::uintptr_t>(addr::cSC4NetworkTool_LoadTileVerifiers, ini, kTileVerifiers);
}

// 0x006232F0 (the reset callback the app keeps, called indirectly): the flag
// 0x00B452A8 cleared when set; AL 1. The thunk keeps EAX's upper bytes, ECX
// and EDX as the game's (which writes only AL).
void __cdecl RulesResetCallbackBody()
{
    static bool logged = false;
    FirstCall("RulesResetCallback", logged);
    if (Global<uint8_t>(addr::cSC4NetworkTool_RulesLoaded))
        Global<uint8_t>(addr::cSC4NetworkTool_RulesLoaded) = 0;
}

__declspec(naked) void RulesResetCallbackThunk()
{
    __asm {
        push eax
        push ecx
        push edx
        call RulesResetCallbackBody
        pop edx
        pop ecx
        pop eax
        mov al, 1
        ret
    }
}

// 0x00626990 (no arguments; the power line tool's Init 0x00650630; AL 1):
// Init's once-load: when the flag 0x00B452A8 is clear, it is set, the app
// (0x00B43C94) gets the reset callback 0x006232F0 by its slot 0x114
// (callback, 0) and LoadRules runs at its address.
uint8_t __cdecl LoadRulesOnce()
{
    static bool logged = false;
    FirstCall("LoadRulesOnce", logged);
    if (!Global<uint8_t>(addr::cSC4NetworkTool_RulesLoaded)) {
        void* const app = Global<void*>(addr::SC4_Service43C94);
        Global<uint8_t>(addr::cSC4NetworkTool_RulesLoaded) = 1;
        Virtual<void>(app, 0x114, addr::cSC4NetworkTool_RulesResetCallback, 0u);
        CallCdecl<void>(addr::cSC4NetworkTool_LoadRules);
    }
    return 1;
}

// 0x00624CE0 (ret 4: a node; this the multimap 0x00B466D0): the subtree
// freed: for each node down the left spine, its right subtree by this
// function at its address, then the node.
void __fastcall Rul2EraseSubtree(char* self, void*, char* node)
{
    static bool logged = false;
    FirstCall("Rul2EraseSubtree", logged);
    while (node) {
        CallThis<void, char*>(addr::SC4Rul2Map_EraseSubtree, self, At<char*>(node, tree::kRight));
        char* const left = At<char*>(node, tree::kLeft);
        Free(node);
        node = left;
    }
}

// 0x00635C60 (ret 4: a node; this the RUL1 map 0x00B460DC): as above, each
// node's hash set at +0x18 cleared (0x008EBA60) and its buckets freed before
// the node.
void __fastcall Rul1EraseSubtree(char* self, void*, char* node)
{
    static bool logged = false;
    FirstCall("Rul1EraseSubtree", logged);
    constexpr std::size_t kSet = 0x18;
    while (node) {
        CallThis<void, char*>(addr::SC4Rul1Map_EraseSubtree, self, At<char*>(node, tree::kRight));
        char* const left = At<char*>(node, tree::kLeft);
        CallThis<void>(addr::SC4Uint32HashSet_Clear, node + kSet);
        FreeAt(node, kSet + table::kBuckets);
        Free(node);
        node = left;
    }
}

// 0x00641F70 (no arguments; only the CRT's static initialiser 0x00A73D10,
// once per row, before our groups install: it never runs as ours): the row
// emptied: the byte +0x4, the vectors +0x24 and +0x30, the table +0x3C
// (cleared, 100 buckets), the parameters' vectors (+0x78, +0xA4 to +0xC4,
// +0xF0 to +0xF8) and the five floats +0x100 to +0x110 (10, 7.0711, 10,
// 7.0711, 20 as the game's constant words). EAX this.
char* __fastcall TypeRowConstruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("TypeRowConstruct", logged);
    At<uint8_t>(self, typerow::kLoaded) = 0;
    for (std::size_t at = typerow::kFiles; at < typerow::kTable; at += 4)
        At<uint32_t>(self, at) = 0;
    for (std::size_t at = table::kBuckets; at <= table::kCount; at += 4)
        At<uint32_t>(self, typerow::kTable + at) = 0;
    CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Construct, self + typerow::kTable, table::kFirstBuckets);
    for (const std::size_t at : {std::size_t{0x78}, std::size_t{0x7C}, std::size_t{0x80}})
        At<uint32_t>(self, at) = 0;
    for (std::size_t at = 0xA4; at <= 0xC4; at += 4)
        At<uint32_t>(self, at) = 0;
    for (const std::size_t at : {std::size_t{0xF0}, std::size_t{0xF4}, std::size_t{0xF8}})
        At<uint32_t>(self, at) = 0;
    constexpr uint32_t kTen = 0x41200000, kSevenish = 0x40E24630, kTwenty = 0x41A00000;
    At<uint32_t>(self, 0x104) = kSevenish;
    At<uint32_t>(self, 0x10C) = kSevenish;
    At<uint32_t>(self, 0x100) = kTen;
    At<uint32_t>(self, 0x108) = kTen;
    At<uint32_t>(self, 0x110) = kTwenty;
    return self;
}

// 0x00643510 (no arguments; only the CRT's static destructor 0x00A7C140, at
// exit): the row cleared (0x006421D0), its parameters destroyed (0x00624F70),
// the table +0x3C's nodes (0x0063D760) and buckets freed, each bridge record
// destroyed (0x00640230; the end read once) and the records' and the rule
// files' buffers freed.
void __fastcall TypeRowDestruct(char* self, void*)
{
    static bool logged = false;
    FirstCall("TypeRowDestruct", logged);
    CallThis<void>(addr::SC4NetworkTypeInfo_Clear, self);
    CallThis<void>(addr::cSC4NetworkTool_DestroyPlacementParams, self + typerow::kParams);
    CallThis<void>(addr::SC4NetworkTypeInfo_DestroyTable3C, self + typerow::kTable);
    FreeAt(self, typerow::kTable + table::kBuckets);
    char* const end = At<char*>(self, typerow::kBridges + 4);
    for (char* bridge = At<char*>(self, typerow::kBridges); bridge != end; bridge += typerow::kBridgeSize)
        CallThis<void>(addr::SC4NetworkBridgeRecord_Destruct, bridge);
    FreeAt(self, typerow::kBridges);
    FreeAt(self, typerow::kFiles);
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kRulLoad[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_LoadRules, LoadRules),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_LoadTypeInfo, LoadTypeInfo),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_RulesResetCallback, RulesResetCallbackThunk),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_LoadRulesOnce, LoadRulesOnce),
    OPENSC4_DETOUR(addr::SC4Rul2Map_EraseSubtree, Rul2EraseSubtree),
    OPENSC4_DETOUR(addr::SC4Rul1Map_EraseSubtree, Rul1EraseSubtree),
    OPENSC4_DETOUR(addr::SC4NetworkTypeInfo_Construct, TypeRowConstruct),
    OPENSC4_DETOUR(addr::SC4NetworkTypeInfo_Destruct, TypeRowDestruct),
};

const Detour kRul0[] = {
    OPENSC4_DETOUR(addr::SC4Rul0Rule_MirrorEdgeWord, MirrorEdgeWord),
    OPENSC4_DETOUR(addr::SC4Rul0Rule_Translate, Translate),
    OPENSC4_DETOUR(addr::SC4Rul0Rule_RotatePoints, RotatePoints),
    OPENSC4_DETOUR(addr::SC4Rul0Rule_RotateCells, RotateCells),
    OPENSC4_DETOUR(addr::SC4Rul0Rule_TransposeGrid, TransposeGrid),
    OPENSC4_DETOUR(addr::SC4Rul0Rule_RotateGrid, RotateGrid),
    OPENSC4_DETOUR(addr::SC4Rul0Rule_Transpose, Transpose),
    OPENSC4_DETOUR(addr::SC4Rul0Rule_Rotate, Rotate),
    OPENSC4_DETOUR(addr::SC4Rul0Rule_ParseCellLayout, ParseCellLayout),
    OPENSC4_DETOUR(addr::SC4Rul0Rule_ParseConsLayout, ParseConsLayout),
    OPENSC4_DETOUR(addr::SC4Rul0Rule_CopyRecords, CopyRecords),
    OPENSC4_DETOUR(addr::SC4Rul0Rule_CopyRecordVector, CopyRecordVector),
    OPENSC4_DETOUR(addr::SC4Rul0Rule_Destruct, Destruct),
    OPENSC4_DETOUR(addr::SC4Rul0Rule_CopyCheckTypes, CopyCheckTypes),
    OPENSC4_DETOUR(addr::SC4Rul0Rule_CopyConsCells, CopyConsCells),
    OPENSC4_DETOUR(addr::SC4Rul0Rule_CopyConstruct, CopyConstruct),
    OPENSC4_DETOUR(addr::SC4Rul0Rule_Construct, Construct),
    OPENSC4_DETOUR(addr::SC4Rul0Store_Clear, StoreClear),
    OPENSC4_DETOUR(addr::SC4Rul0Store_InsertNode, InsertNode),
    OPENSC4_DETOUR(addr::SC4Rul0Store_FindOrInsert, FindOrInsert),
    OPENSC4_DETOUR(addr::SC4PairHashTable_Find, PairFind),
};

#undef OPENSC4_DETOUR

bool InstallRulLoad(registry::Mode) { return InstallDetours(kRulLoad, "rulload"); }
bool InstallRul0(registry::Mode) { return InstallDetours(kRul0, "rul0"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.rulload", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_LoadRules,
                        "Our network tool's rule load: LoadRules, the per-network ini (the type table rows), the "
                        "reset callback, the power line tool's once-load, the RUL1 and RUL2 maps' erasers and the "
                        "type row's static constructor and destructor (8 detours; src/core/nettool_rul_1.cpp)",
                        false, InstallRulLoad}),
    registry::Register({"simulators.nettool.rul0", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::SC4Rul0Store_FindOrInsert,
                        "Our network tool's RUL0 rules: the rule's constructors, destructor and copies, the store's "
                        "clear, insert and lookup, the CellLayout and ConsLayout parsers, the translate, transpose "
                        "and rotate transforms with their helpers, and the pair table's find (21 detours; "
                        "src/core/nettool_rul_1.cpp)",
                        false, InstallRul0}),
};

} // namespace
} // namespace opensc4::nettool_sim
