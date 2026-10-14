// Our cSC4NetworkTool, part t8 (build 641; the class map and the fields are
// in src/core/nettool_sim.h, the RUL layouts in src/core/nettool_rul.h): the
// tile solver, which turns the cells of a drag (or of a resolve) into solved
// cells (+0x98: a piece ID, a rotation and flip byte, the cell key) from the
// RUL1 pair overrides and the type rows' rule files, then hands them to the
// NAM-guarded RUL2 pass and the tile verifiers. In two files (this one has
// the first group; src/core/nettool_solve_2.cpp the other two; the layouts
// and the shared helpers are in src/core/nettool_solve.h), three groups, each
// a list of detours at the game's addresses:
//   simulators.nettool.tiles: the solver SolveTiles 0x00635CB0 and its steps:
//     ApplyRuleFile 0x006358C0, MatchRuleFile 0x006340D0, ResolveCellTypes
//     0x00634820, VerifyTiles 0x00635290, the tile verifier table's
//     EqualRange 0x006339B0, the set helpers SetSubtract 0x00633E50,
//     SetEraseKeys 0x00633ED0, SetUnion 0x00633F10, GrowCellSet 0x00634480
//     and CellRectAround 0x00623C20 (11 detours);
//   simulators.nettool.tilefix: the edge fix-ups before the rule files:
//     FixCellEdges 0x006359B0, FixEdgesKind1 0x00633A60, FixEdgesKind2
//     0x00633C10, MarkDiagonalRuns 0x006335E0, IsDiagonalEdge 0x00623C90,
//     SplitSidesByKind 0x00620DF0, SetSideHighBits 0x00621070, SetSideKind
//     0x006335B0, and the RUL2 pair patch PatchTilePair 0x006337E0 the
//     guarded pass calls (9 detours);
//   simulators.nettool.dragcells: the cells a drag hands the solver:
//     CollectDraggedCells 0x006355C0 (called by the guarded 0x0062CD50 and by
//     the segment placer 0x0063B830), SpreadConnected 0x006345B0,
//     CollectIntersectionCells 0x0063B1A0, EraseRecordE4Cells 0x00638BB0 and
//     EraseBridgeCells 0x00638B40 (5 detours).
// Three groups so each can be tried and bisected alone: the solver's sets
// and rule matching, the edge rewriting of the cell infos (the code nearest
// to NAM's, since PatchTilePair is called by NAM's replacement too), and the
// collection of the drag's cells.
//
// Every function was written from the disassembly. Calls between them go to
// the game's addresses (through the address entries), so each detour is
// independent of the others and OPENSC4_NETTOOL_SKIP bisects them.
//
// What the state is (evidence in the functions below):
//   - the tool's +0x84 is the set of cells to solve (an STLport hash set of
//     uint32 cell keys (z << 16) + x, 0x14 bytes, `rul::table`), +0x98 the
//     solved cells (vector of 12 bytes: the piece ID, a byte of rotation in
//     bits 0-1 and flip in bit 7 (three bytes after it, below), the cell
//     key), +0x08 a vector of 12-byte records {x, z, the cell's flags} that
//     ResolveCellTypes appends for each RUL1 override it applies (the life
//     code's "pointer the destructor frees"), +0xCC the intersections (0x28
//     bytes: +0x0 a RUL0 rule ID, +0x4 x, +0x8 z, +0x14 a step, +0x1C a
//     uint32 vector of the cells it covers, which CollectIntersectionCells
//     appends to), +0xD8 the major bridges (0x28: +0x8 a step, +0x1C a vector
//     of 0x50-byte cells with x +0x4 and z +0x8), +0xE4 records of 0x10 bytes
//     (+0x8 the first and +0xC the last step), +0xF0 the rule cell records
//     (0x14: x, z);
//   - a cell info's edge word by type (+0x18 + 4 * type) has a byte a side
//     (W, N, E, S by the side steps 0x00AA89F8 / 0x00AA8A08): low three bits
//     its kind (0 none, 2 straight, 1 and 3 the two diagonal halves), the
//     bits above a level the fix-ups write; +0x50 the one-way byte, +0x57 a
//     mark the drag code sets, +0xB8 the index of the cell's solved cell;
//   - the solver's three locals: a uint32 vector of resolved keys and two
//     hash sets (100 buckets each, SC4Uint32HashSet::Construct): the cells
//     around (+0x84 grown by two) and the cells that failed.
//
// Left as the game's: the RUL2 pass AdjustTileSubsets 0x00634D70 (NAM's
// Rul2Engine jumps out at 0x00634D79 and replaces 0x00634D79 to 0x00635282:
// no detour, no translation; SolveTiles calls it at its address). Nothing of
// this part computes geometry, UVs or textures or calls render, texture or
// model services, so nothing is left to the Renderer thread. The small game
// helpers the part calls stay the game's and are called at their addresses
// (SetSideByte 0x00620FC0, ClearSideHighBits 0x00621440, EdgeConnects
// 0x00620EC0, EdgeWordsAgree 0x00729EC0 and the ones listed below).
//
// NAM. AdjustTileSubsets (guarded) calls PatchTilePair 0x006337E0 twice
// (0x00634F28, 0x00634FF5) and NAM's source names PatchTilePair as a function
// its engine may call: after the call the game compares all of EAX with 2
// and 1, sets CL itself and reads only its own frame and callee-saved
// registers (0x00634F2D to 0x00635012, the branches at 0x006351C7, 0x00635017,
// 0x0063527D and 0x00634E64 scanned). PatchTilePair does not read `this`
// (the game pushes ECX for its frame only). The guarded 0x0062CD50 calls
// CollectDraggedCells (0x0062D2B9; then push EBP, EBX, ECX written) and
// SolveTiles (0x0062D2C2; EAX compared with 1): ours answer what the game's
// do. So all are plain detours with the game's conventions. NAM reads the
// tool's +0x84, +0x98, +0xCC and the cell infos by layout: everything stays
// in the game's memory with the game's layout, through the game's
// allocators (SC4::stl_allocate / SC4::Free, the hash sets' Construct,
// Resize, Insert, Erase, EraseAt, Clear) and template helpers (the 12-byte
// vector's InsertOverflow 0x00626240, the pointer vector's InsertN
// 0x0051CA60, SC4::memmove); no copy is kept across a call.
// Wrappers: no file of src/core/game_*.cpp or *fix*.cpp detours or tests
// return addresses in 0x00620DF0 to 0x0063B340 (game_quiet.cpp only names
// 0x006208D0 in a comment); sim_timers.cpp has nothing here.
//
// Floats: none (no x87 instruction in any of the 25 functions).
//
// Registers. Every game caller was scanned (EAX, ECX, EDX after the call,
// both branches): 0x00623CB3 and 0x00633A91 (SplitSidesByKind: EAX
// rewritten at once), 0x00633B57 / 0x00633BA9 (SetSideHighBits),
// 0x006344C2 (CellRectAround), 0x006336FF / 0x00633B1F (IsDiagonalEdge: AL),
// 0x00633B6D / 0x00633BB7 (SetSideKind), 0x00633AAF (MarkDiagonalRuns),
// 0x006352DB (EqualRange), 0x00635B9F / 0x00635BB2 (the two fix-ups: AL),
// 0x00635D2B / 0x00635D55 / 0x00635D64 / 0x00635DDE (the set helpers),
// 0x00635906 (MatchRuleFile: then EBX and EDI, callee-saved), 0x00635D0D /
// 0x006359E8 (GrowCellSet), 0x00635883 (SpreadConnected), 0x00635D40
// (ResolveCellTypes), 0x00635E67 (VerifyTiles: AL), 0x00635DB1
// (ApplyRuleFile), 0x00635D20 (FixCellEdges), 0x0062B783 / 0x0062D2C2 /
// 0x0063BAFE (SolveTiles: all of EAX), 0x0062D2B9 / 0x0063BAE1
// (CollectDraggedCells), 0x00635891 / 0x00635899 / 0x006358A1 (the three
// erasers), the two PatchTilePair sites above. None reads ECX or EDX before
// writing it; where EAX is read ours gives the same: so all are plain
// detours.
//
// Stack leftovers the game stores (ours are 0):
//   - the solved cell's dword +0x4. MatchRuleFile and ResolveCellTypes put
//     the rotation byte (a rule tile's +0x5, a RUL1 pair's +0x10) into the
//     low byte of a dword of their frame whose three upper bytes they never
//     write, and append the whole dword (in place, or by value through
//     InsertOverflow), so the solved cell's bytes +0x5 to +0x7 are the
//     original's stack contents. Readers of +0x98's elements in the game:
//     slot 55 0x006258C0 (`xor eax, eax; mov al, [edi+4]`), AddCellPreview
//     0x006246A0 (`mov al, [ecx+4]`), VerifyTiles (`movzx eax, byte
//     [eax+4]`), PatchTilePair (`mov dl, [ebp+4]`, byte writes), the cost
//     0x00625640 (the key +0x8 only), and the copies (InsertOverflow,
//     0x00629EB0, VerifyTiles' shift, the life code's vector moves) that
//     carry the dword along. Every reader takes the low byte; NAM's DLL
//     reads the solved cells by layout (its source treats the rotation as
//     a byte; not checked against its binary). Verdict: harmless; ours
//     writes 0 above the byte, and the test hashes the low byte only.
//   - FixEdgesKind1 pushes a dword whose low byte is its "neighbour is
//     diagonal" flag and whose upper bytes are never written; SetSideHighBits
//     reads `mov cl, [esp+0xC]` (the byte only). FixEdgesKind2 pushes the
//     new side byte the same way to SetSideByte, which reads `movzx esi,
//     byte [esp+0xC]`. FixCellEdges passes its third argument slot (the
//     solver's 1 with its low byte overwritten by the "in the set" flag, or
//     a type number) as both fix-ups' fourth argument, which they read as a
//     byte. No other reader. Verdict: harmless; ours pass the same low byte
//     with zeros above.
// Other stores into the game's stack, never read again as what they were:
// IsDiagonalEdge's counts go into its own argument slots; PatchTilePair keeps
// its rotations and flag in the low bytes of its four argument slots;
// SetEraseKeys, GrowCellSet, ResolveCellTypes, SpreadConnected,
// FixCellEdges, EraseBridgeCells and SolveTiles reuse their argument slots
// for keys, types and counters. Ours use locals (FixCellEdges keeps the
// values of its two reused slots in locals because it reads them back).
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the part's own, at the game's addresses: SplitSidesByKind four (ret
//     0x10), SetSideHighBits two (ret 8), CellRectAround four (ret 0x10),
//     IsDiagonalEdge cdecl two, SetSideKind two (ret 8), MarkDiagonalRuns
//     three (ret 0xC), EqualRange two (ret 8), FixEdgesKind1 / 2 four (ret
//     0x10), the set helpers cdecl two, MatchRuleFile six (ret 0x18),
//     GrowCellSet three (ret 0xC), SpreadConnected two (ret 8),
//     ResolveCellTypes three (ret 0xC), VerifyTiles one (ret 4),
//     ApplyRuleFile five (ret 0x14), FixCellEdges three (ret 0xC), the three
//     erasers one each (ret 4);
//   - the guarded AdjustTileSubsets 0x00634D70 two (ret 8: +0x98, the
//     solver's second argument as pushed);
//   - the tool's: the cell entry 0x00633220 one (ret 4); the world cache's
//     GetCell 0x00647A20 one (ret 4); the cell info's CombineEdges 0x00647830
//     none; DirectionStep 0x00620DA0 four (ret 0x10); DirectionOffsets
//     0x00620D80 cdecl three; the walker's constructor 0x00627080 five (ret
//     0x14); the rule file's match 0x006220D0 two (ret 8);
//     GetFirstNetworkTypeFromFlags 0x00623E60 cdecl one; EdgeConnects
//     0x00620EC0 one (ret 4); SetSideByte 0x00620FC0 two (ret 8);
//     ClearSideHighBits 0x00621440 none; EdgeWordsAgree 0x00729EC0 cdecl two;
//   - the network occupant (a cell info's +0xC): slot 0x30 (flags) none,
//     slot 0x34 two (ret 8: two words out; AL), slot 0x5C (the piece ID)
//     none, slot 0x68 (the rotation byte; AL) none;
//   - the RUL0 store's FindOrInsert 0x0062C2F0 one (ret 4: the ID's
//     address; EAX the rule); the RUL1 map's SC4IntPairSet::Find 0x00695F70
//     one (ret 4: the pair's address; EAX the node, the head when missing);
//   - the hash sets: SC4Uint32HashSet::Construct 0x00829BC0 one (ret 4:
//     100), Resize 0x004F0940 one (ret 4), Insert 0x00624DD0 two (ret 8: the
//     result pair out, the key's address), Erase 0x0045D460 one (ret 4: the
//     key's address), Clear 0x008EBA60 none; SC4HashTable::Begin 0x004B0390
//     one (ret 4: the iterator out); SC4Uint32HashMap::EraseAt 0x00489CF0
//     one (ret 4: an iterator's address); SC4Uint32HashTable::Find
//     0x006F1530 two (ret 8: the iterator out, the key's address; EAX the
//     out);
//   - template code: SC4::stl_allocate 0x0090CF54 and SC4::Free 0x0090CF63
//     cdecl one; SC4::memmove 0x009EEB70 cdecl three; SC4Vector12::
//     InsertOverflow 0x00626240 five (ret 0x14: where, the value's address,
//     an unread tag, 1, 1); SC4PtrVector::InsertN 0x0051CA60 five (ret 0x14:
//     the same).
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
// The sets
// ====================================================================================

// 0x00623C20 (ret 0x10: x, z, the rectangle out (x1, z1, x2, z2), a radius;
// this the tool; 0x00634480's): x and z minus the radius (0 when not above
// it), plus it (the cells in x / z - 1 at most), unsigned. EAX the out.
uint32_t* __fastcall CellRectAround(char* self, void*, uint32_t x, uint32_t z, uint32_t* rect, uint32_t radius)
{
    static bool logged = false;
    FirstCall("CellRectAround", logged);
    rect[0] = x > radius ? x - radius : 0;
    const uint32_t right = x + radius;
    rect[2] = right;
    const uint32_t cellsX = At<uint32_t>(self, field::kCellsX);
    if (right >= cellsX)
        rect[2] = cellsX - 1;
    rect[1] = z > radius ? z - radius : 0;
    const uint32_t bottom = radius + z;
    rect[3] = bottom;
    const uint32_t cellsZ = At<uint32_t>(self, field::kCellsZ);
    if (bottom >= cellsZ)
        rect[3] = cellsZ - 1;
    return rect;
}

// 0x00634480 (ret 0xC: a cell set, a set out, a radius; this the tool;
// called by 0x006359B0 and the solver): for each cell of the first set, every
// cell of the rectangle around it (0x00623C20; inclusive, unsigned) whose
// cell entry exists and is not immovable, or is the cell itself, inserted
// into the second set.
void __fastcall GrowCellSet(char* self, void*, char* from, char* into, uint32_t radius)
{
    static bool logged = false;
    FirstCall("GrowCellSet", logged);
    for (char* node = Begin(from); node; node = NextNode(node, from)) {
        const uint32_t key = At<uint32_t>(node, table::kKey);
        uint32_t rect[4] = {0, 0, 0, 0};
        CallThis<uint32_t*, uint32_t, uint32_t, uint32_t*, uint32_t>(addr::cSC4NetworkTool_CellRectAround, self,
                                                                     key & 0xFFFF, key >> 16, rect, radius);
        for (uint32_t z = rect[1]; z <= rect[3]; ++z) {
            for (uint32_t x = rect[0]; x <= rect[2]; ++x) {
                uint32_t cellKey = Key(x, z);
                char* const entry = Entry(self, cellKey);
                if (entry && (!At<uint8_t>(entry, cell::kImmovable) || At<uint32_t>(node, table::kKey) == cellKey))
                    Insert(into, &cellKey);
            }
        }
    }
}

// 0x00633E50 (cdecl: a cell set, another; the solver's): every key of the
// second erased from the first (the second's node's key by address).
void __cdecl SetSubtract(char* set, char* other)
{
    static bool logged = false;
    FirstCall("SetSubtract", logged);
    for (char* node = Begin(other); node; node = NextNode(node, other))
        Erase(set, &At<uint32_t>(node, table::kKey));
}

// 0x00633ED0 (cdecl: a cell set, a uint32 vector*; the solver's): every key
// of the vector erased from the set (the game copies each into its second
// argument slot first).
void __cdecl SetEraseKeys(char* set, const Vector* keys)
{
    static bool logged = false;
    FirstCall("SetEraseKeys", logged);
    for (const char* at = keys->begin; at != keys->end; at += 4) {
        const uint32_t key = At<uint32_t>(const_cast<char*>(at), 0);
        Erase(set, &key);
    }
}

// 0x00633F10 (cdecl: a cell set, another; the solver's): every key of the
// second inserted into the first.
void __cdecl SetUnion(char* set, char* other)
{
    static bool logged = false;
    FirstCall("SetUnion", logged);
    for (char* node = Begin(other); node; node = NextNode(node, other))
        Insert(set, &At<uint32_t>(node, table::kKey));
}

// ====================================================================================
// The rule files and the overrides
// ====================================================================================

// 0x006340D0 (ret 0x18: the cells around, the cells to solve, the unmatched
// out, a rule file, the occupant manager (not read), a network type; this
// the tool; 0x006358C0's). Nothing when there are no cells to solve; then
// while the cells around are not empty: their first cell (Begin); its cell
// entry; when there is one, a walker of it (0x00627080 with the byte 0) and,
// when the entry's flags & 0x1FFF are the type's flag, the file's match
// (0x006220D0), unless the entry is a network lot whose occupant's flags &
// 0x1FFF are not the entry's flags, or whose occupant's slot 0x34 answers
// two edge words the type's edges (no bits 0xF8F8F8F8 allowed) do not agree
// with both ways (0x00729EC0); the walker's memo freed. No match (or no
// entry): the cell into the unmatched and out of the cells around (EraseAt).
// A match: each of the matching entry's tiles steps from the cell
// (DirectionStep 0x00620DA0); the tile's cell taken out of the cells around
// or, not there, out of the unmatched (neither: next tile), then out of the
// cells to solve (not there: next tile), and its piece solved (the cell
// entry's +0xB8 the solved count, the piece, the tile's rotation byte, the
// key appended to +0x98). When no tile stepped onto the cell itself, the
// cell is erased from the cells around and inserted into the unmatched.
// The loop ends when the cells to solve are empty.
void __fastcall MatchRuleFile(char* self, void*, char* around, char* cells, char* unmatched, char* file, void*,
                              uint32_t type)
{
    static bool logged = false;
    FirstCall("MatchRuleFile", logged);
    if (!Count(cells))
        return;
    do {
        if (!Count(around))
            return;
        char* const node = Begin(around);
        uint32_t key = At<uint32_t>(node, table::kKey);
        const uint32_t x = key & 0xFFFF, z = key >> 16;
        bool matched = false;
        char* tiles = nullptr;
        if (char* const entry = Entry(self, key)) {
            alignas(4) char walkerObject[walker::kSize] = {};
            CallThis<char*, int32_t, int32_t, char*, uint32_t, uint32_t>(addr::cSC4NetworkTool_NeighborConstruct,
                                                                         walkerObject, static_cast<int32_t>(x),
                                                                         static_cast<int32_t>(z), self, type, 0u);
            if ((At<uint32_t>(entry, cell::kFlags) & cell::kOccupantFlagMask) == TypeFlag(type)) {
                bool tryMatch = true;
                if (At<uint8_t>(entry, cell::kNetworkLot)) {
                    void* const occupant = At<void*>(entry, cell::kOccupant);
                    const uint32_t flags = Virtual<uint32_t>(occupant, solve::kOccupantFlags);
                    if ((flags & cell::kOccupantFlagMask) != At<uint32_t>(entry, cell::kFlags)) {
                        tryMatch = false;
                    } else {
                        uint32_t first = 0, second = 0;
                        if (Virtual<uint8_t>(occupant, solve::kOccupantLotEdges, &first, &second)) {
                            const uint32_t edges = *EdgeWord(entry, type);
                            if (edges & solve::kHighEdgeBits)
                                tryMatch = false;
                            else if (!static_cast<uint8_t>(CallCdecl<uint32_t, uint32_t, uint32_t>(
                                         addr::SC4_EdgeWordsAgree, first, edges)))
                                tryMatch = false;
                            else if (!static_cast<uint8_t>(CallCdecl<uint32_t, uint32_t, uint32_t>(
                                         addr::SC4_EdgeWordsAgree, edges, second)))
                                tryMatch = false;
                        }
                    }
                }
                if (tryMatch)
                    matched =
                        CallThis<uint8_t, char*, char**>(addr::SC4RuleFile_Match, file, walkerObject, &tiles) != 0;
            }
            FreeIfSet(At<Vector>(walkerObject, 0x8).begin);
        }
        if (!matched) {
            Insert(unmatched, &key);
            EraseAt(around, node);
            continue;
        }
        bool onCell = false;
        char* const end = At<char*>(tiles, 4);
        for (char* tile = At<char*>(tiles, 0); tile != end; tile += solve::kTileSize) {
            int32_t tx = 0, tz = 0;
            CallThis<int32_t, int32_t, int32_t, int32_t*, int32_t*>(addr::cSC4NetworkTool_DirectionStep, tile,
                                                                    static_cast<int32_t>(x), static_cast<int32_t>(z),
                                                                    &tx, &tz);
            if (static_cast<int32_t>(x) == tx && static_cast<int32_t>(z) == tz)
                onCell = true;
            const uint32_t tileKey = Key(static_cast<uint32_t>(tx), static_cast<uint32_t>(tz));
            if (char* const found = FindNode(around, tileKey)) {
                EraseAt(around, found);
            } else if (char* const other = FindNode(unmatched, tileKey)) {
                EraseAt(unmatched, other);
            } else {
                continue;
            }
            char* const solving = FindNode(cells, tileKey);
            if (!solving)
                continue;
            EraseAt(cells, solving);
            const uint32_t piece = At<uint32_t>(tile, solve::kTilePiece);
            const uint8_t rotation = At<uint8_t>(tile, solve::kTileRotation);
            AddSolved(self, Entry(self, tileKey), piece, rotation, tileKey);
        }
        if (!onCell) {
            Erase(around, &key);
            Insert(unmatched, &key);
        }
    } while (Count(cells));
}

// 0x006358C0 (ret 0x14: the cells around, the cells to solve, a rule file,
// the occupant manager, a network type; this the tool; the solver's for each
// rule file of each type row): a local set of unmatched cells (100 buckets),
// 0x006340D0 into it; while cells remain to solve, the unmatched go back
// into the cells around; the local set destroyed.
void __fastcall ApplyRuleFile(char* self, void*, char* around, char* cells, char* file, void* occupants,
                              uint32_t type)
{
    static bool logged = false;
    FirstCall("ApplyRuleFile", logged);
    alignas(4) char unmatched[table::kSize] = {};
    Construct(unmatched);
    CallThis<void, char*, char*, char*, char*, void*, uint32_t>(addr::cSC4NetworkTool_MatchRuleFile, self, around,
                                                                cells, unmatched, file, occupants, type);
    if (Count(cells))
        for (char* node = Begin(unmatched); node; node = NextNode(node, unmatched))
            Insert(around, &At<uint32_t>(node, table::kKey));
    Destroy(unmatched);
}

// The used bits of an edge word for the cover test: per side, both kind bits
// when either is set.
uint32_t UsedSides(uint32_t edges)
{
    return (((edges >> 1) | edges) & 0x01010101u) * 3u;
}

// The one-way byte's mask of an edge word: a bit per side whose kind is 1
// (the game's multiply gathers the four side bits into the top byte).
uint8_t OneWaySides(uint32_t edges)
{
    return static_cast<uint8_t>(((((edges & 0x03030303u) + 0x03030303u) & 0x04040404u) * 0xC30C30u) >> 24);
}

// 0x00634820 (ret 0xC: the cells to solve, a uint32 vector out, a preferred
// network type; this the tool; the solver's). For each cell (its cell entry,
// not checked for null) with two or more network flags:
//   - three or more: the lowest three types; the first type (in that order)
//     that has a higher one (priority 0x00AA8A18, the preferred type winning
//     a tie) whose edges equal its own on every side it uses and the same
//     layered-ness (flags 0x304) loses its flag and edges (the edges
//     combined; the one-way byte cleared for type 10), and the cell is looked
//     at again; when none does, the cell is left;
//   - two (not a network lot): A the higher (as above), B the other. When B's
//     edges equal A's on every side B uses: when A's type row allows B
//     (+0xFC bit B), B's edges cleared, the edges combined, the flags A's
//     only, the one-way byte cleared for B 10; the cell is left. Else, when
//     both are layered or neither is, the sides of B equal to A's cleared,
//     written back, combined, the one-way byte masked for B 10; then the
//     one-way byte loses the sides any one-way source type (flags 0x849)
//     has as kind 1; the RUL1 map 0x00B460DC's node of the type pair (lower
//     type first); its pair table's node of the two edge words (lower type's
//     first): the cell gets that piece and rotation (solved: +0xB8, +0x98),
//     its key goes into the vector and {x, z, the flags} into +0x08.
void __fastcall ResolveCellTypes(char* self, void*, char* cells, Vector* keys, uint32_t preferred)
{
    static bool logged = false;
    FirstCall("ResolveCellTypes", logged);
    auto higher = [preferred](uint32_t a, uint32_t b) {
        // Whether a loses to b.
        const int32_t pa = Priority(a), pb = Priority(b);
        return pa < pb || (pa == pb && a != preferred && b == preferred);
    };
    auto unlayered = [](uint32_t flag) { return (flag & solve::kLayered) ? 0u : 1u; };
    auto getFirst = [](uint32_t flags) {
        return CallCdecl<uint32_t, uint32_t>(addr::cSC4NetworkTool_GetFirstNetworkTypeFromFlags, flags);
    };
    for (char* node = Begin(cells); node; node = NextNode(node, cells)) {
        uint32_t key = At<uint32_t>(node, table::kKey);
        char* const entry = Entry(self, key);
        uint32_t flags = At<uint32_t>(entry, cell::kFlags);
        uint32_t rest = (flags - 1) & flags;
        uint32_t lowest = rest ^ flags;
        while (rest) {
            const uint32_t t1 = getFirst(lowest);
            const uint32_t more = (rest - 1) & rest;
            const uint32_t second = more ? (rest ^ more) : rest;
            const uint32_t t2 = getFirst(second);
            if (more) {
                const uint32_t t3 = getFirst(more);
                const uint32_t types[3] = {t1, t2, t3};
                const uint32_t edges[3] = {*EdgeWord(entry, t1), *EdgeWord(entry, t2), *EdgeWord(entry, t3)};
                int32_t removed = -1;
                for (int i = 0; i < 3 && removed < 0; ++i)
                    for (int j = 0; j < 3; ++j) {
                        if (!higher(types[i], types[j]))
                            continue;
                        if ((edges[j] ^ edges[i]) & UsedSides(edges[i]))
                            continue;
                        if (unlayered(TypeFlag(types[i])) != unlayered(TypeFlag(types[j])))
                            continue;
                        removed = i;
                        break;
                    }
                if (removed < 0)
                    break;
                const uint32_t type = types[removed];
                At<uint32_t>(entry, cell::kFlags) &= ~TypeFlag(type);
                *EdgeWord(entry, type) = 0;
                Combine(entry);
                if (type == solve::kOneWayType)
                    At<uint8_t>(entry, cell::kOneWay) = 0;
                flags = At<uint32_t>(entry, cell::kFlags);
                rest = (flags - 1) & flags;
                lowest = rest ^ flags;
                continue;
            }
            // Two types.
            if (At<uint8_t>(entry, cell::kNetworkLot))
                break;
            uint32_t a = t1, b = t2, aBit = lowest, bBit = second;
            if (higher(t1, t2)) {
                a = t2;
                b = t1;
                aBit = second;
                bBit = lowest;
            }
            const uint32_t edgesA = *EdgeWord(entry, a);
            uint32_t edgesB = *EdgeWord(entry, b);
            if (((edgesB ^ edgesA) & UsedSides(edgesB)) == 0) {
                if (!(At<uint32_t>(TypeRow(a), rul::typerow::kRul1Id) & (1u << (b & 31))))
                    break;
                *EdgeWord(entry, b) = 0;
                Combine(entry);
                At<uint32_t>(entry, cell::kFlags) = aBit;
                if (b == solve::kOneWayType)
                    At<uint8_t>(entry, cell::kOneWay) = 0;
                break;
            }
            if (unlayered(aBit) == unlayered(bBit)) {
                for (uint32_t side = 0; side < solve::kSides; ++side) {
                    const uint32_t mask = 3u << (side * 8);
                    if ((edgesA & mask) == (edgesB & mask) && (edgesB & mask))
                        edgesB &= ~(0xFFu << (side * 8));
                }
                *EdgeWord(entry, b) = edgesB;
                Combine(entry);
                if (b == solve::kOneWayType)
                    At<uint8_t>(entry, cell::kOneWay) &= OneWaySides(edgesB);
            }
            uint32_t sources = 0;
            for (uint32_t type = 0; type < kTypes; ++type)
                if (TypeFlag(type) & solve::kOneWaySources)
                    sources |= *EdgeWord(entry, type);
            At<uint8_t>(entry, cell::kOneWay) &= static_cast<uint8_t>(~OneWaySides(sources));
            int32_t pair[2] = {0, 0};
            if (static_cast<int32_t>(a) < static_cast<int32_t>(b)) {
                pair[0] = static_cast<int32_t>(a);
                pair[1] = static_cast<int32_t>(b);
            } else {
                pair[0] = static_cast<int32_t>(b);
                pair[1] = static_cast<int32_t>(a);
            }
            char* const found = CallThis<char*, const int32_t*>(addr::SC4IntPairSet_Find,
                                                                reinterpret_cast<void*>(addr::cSC4NetworkTool_Rul1Map),
                                                                pair);
            if (found == Global<char*>(addr::cSC4NetworkTool_Rul1Map))
                break;
            uint32_t low = edgesA, high = edgesB;
            if (static_cast<int32_t>(a) >= static_cast<int32_t>(b)) {
                low = edgesB;
                high = edgesA;
            }
            char* const pairs = found + rul::rul1map::kTable;
            char* hit = reinterpret_cast<char**>(At<Vector>(pairs, table::kBuckets).begin)[(high + low * 2) %
                                                                                           BucketCount(pairs)];
            while (hit && !(At<uint32_t>(hit, solve::kPairLow) == low && At<uint32_t>(hit, solve::kPairHigh) == high))
                hit = At<char*>(hit, table::kNext);
            if (!hit)
                break;
            const uint8_t rotation = At<uint8_t>(hit, solve::kPairRotation);
            const uint32_t piece = At<uint32_t>(hit, solve::kPairPiece);
            AddSolved(self, entry, piece, rotation, key);
            if (keys->end != keys->capacity) {
                if (keys->end)
                    At<uint32_t>(keys->end, 0) = key;
                keys->end += 4;
            } else {
                uint32_t tag = 0;
                CallThis<void, char*, uint32_t*, uint32_t*, uint32_t, uint32_t>(addr::SC4PtrVector_InsertN, keys,
                                                                                keys->end, &key, &tag, 1u, 1u);
            }
            const uint32_t record[3] = {key & 0xFFFF, key >> 16, flags};
            PushRecord(At<Vector>(self, solve::kOverrides), record);
            break;
        }
    }
}

// 0x006339B0 (ret 8: the range out {first, table, after, table}, the key's
// address; this the tile verifier table 0x00B466DC; 0x00635290's): the
// key's bucket searched for its first node (none: {0, table, 0, table});
// "after" the first following node with another key, or the head of the
// first non-empty later bucket, or 0. EAX the out.
uint32_t* __fastcall VerifierEqualRange(char* self, void*, uint32_t* out, const uint32_t* key)
{
    static bool logged = false;
    FirstCall("VerifierEqualRange", logged);
    const uint32_t k = *key;
    char** const buckets = reinterpret_cast<char**>(At<Vector>(self, table::kBuckets).begin);
    const uint32_t index = k % BucketCount(self);
    char* node = buckets[index];
    while (node && At<uint32_t>(node, table::kKey) != k)
        node = At<char*>(node, table::kNext);
    const uint32_t me = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(self));
    if (!node) {
        out[0] = 0;
        out[2] = 0;
        out[1] = me;
        out[3] = me;
        return out;
    }
    char* after = At<char*>(node, table::kNext);
    while (after && At<uint32_t>(after, table::kKey) == k)
        after = At<char*>(after, table::kNext);
    if (!after) {
        for (uint32_t i = index + 1; i < BucketCount(self); ++i)
            if (buckets[i]) {
                after = buckets[i];
                break;
            }
    }
    out[2] = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(after));
    out[0] = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(node));
    out[1] = me;
    out[3] = me;
    return out;
}

// 0x00635290 (ret 4: the solved cells; this the tool; the solver's): AL 1
// at once when the tile verifier table 0x00B466DC is empty. For each solved
// cell: its verifier nodes (0x006339B0 on its piece); none: next cell. Each
// node in turn: an empty entry vector passes; else every entry must hold:
// the entry's direction (DirectionOffsets 0x00620D80) turned by the cell's
// rotation (bit 0: (x, z) to (-z, x); bit 1: both negated; flip: x negated)
// from the cell gives a cell inside the city whose world cache cell info
// (GetCell 0x00647A20) exists and whose piece (its solved cell by +0xB8, or
// its occupant's slots 0x5C and 0x68) is the entry's piece with the
// entry's rotation relative to the cell's. A node that passes keeps the
// cell. When none does, the cell's key goes back into +0x84, the later
// cells' info indices +0xB8 are lowered, the cell is taken out of the
// vector (the rest moved down) and the same place is looked at again. AL 1
// when no cell was taken out, else 0 (all of EAX).
uint32_t __fastcall VerifyTiles(char* self, void*, Vector* solved)
{
    static bool logged = false;
    FirstCall("VerifyTiles", logged);
    char* const verifiers = reinterpret_cast<char*>(addr::cSC4NetworkTool_TileVerifiers);
    if (!Count(verifiers))
        return 1;
    bool changed = false;
    char* cursor = solved->begin;
    while (cursor != solved->end) {
        uint32_t range[4] = {0, 0, 0, 0};
        CallThis<uint32_t*, uint32_t*, const uint32_t*>(addr::SC4TileVerifierTable_EqualRange, verifiers, range,
                                                        reinterpret_cast<uint32_t*>(cursor));
        char* node = reinterpret_cast<char*>(static_cast<std::uintptr_t>(range[0]));
        char* const after = reinterpret_cast<char*>(static_cast<std::uintptr_t>(range[2]));
        if (node == after) {
            cursor += solve::kSolvedSize;
            continue;
        }
        char* const rangeTable = reinterpret_cast<char*>(static_cast<std::uintptr_t>(range[1]));
        const uint32_t key = At<uint32_t>(cursor, solve::kKey);
        const uint32_t x = key & 0xFFFF, z = key >> 16;
        const uint8_t rotation = At<uint8_t>(cursor, solve::kRotation);
        bool passed = false;
        for (;;) {
            char* entry = At<char*>(node, solve::kVerifierEntries);
            char* const entriesEnd = At<char*>(node, solve::kVerifierEntriesEnd);
            if (entry == entriesEnd) {
                passed = true;
                break;
            }
            bool failed = false;
            for (; entry != entriesEnd; entry += solve::kVerifierEntrySize) {
                uint8_t step[2] = {0, 0}; // x, z
                CallCdecl<uint32_t, uint32_t, uint8_t*, uint8_t*>(
                    addr::cSC4NetworkTool_DirectionOffsets,
                    static_cast<uint32_t>(At<uint8_t>(entry, solve::kEntryDirection)), &step[0], &step[1]);
                uint8_t dx = step[0], dz = step[1];
                if (rotation & 1) {
                    const uint8_t was = dz;
                    dz = dx;
                    dx = static_cast<uint8_t>(0 - was);
                }
                if (rotation & 2) {
                    dx = static_cast<uint8_t>(0 - dx);
                    dz = static_cast<uint8_t>(0 - dz);
                }
                if (rotation & 0x80)
                    dx = static_cast<uint8_t>(0 - dx);
                const uint32_t cx = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(dx))) + x;
                const uint32_t cz = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(dz))) + z;
                if (cx >= At<uint32_t>(self, field::kCellsX) || cz >= At<uint32_t>(self, field::kCellsZ)) {
                    failed = true;
                    break;
                }
                char* const info = CallThis<char*, uint32_t>(addr::cSC4NetworkWorldCache_GetCell,
                                                             self + field::kWorldCache, Key(cx, cz));
                if (!info) {
                    failed = true;
                    break;
                }
                uint32_t piece = 0, theirs = 0;
                const int32_t index = At<int32_t>(info, cell::kSolvedIndex);
                if (index >= 0) {
                    char* const other = solved->begin + index * static_cast<int32_t>(solve::kSolvedSize);
                    piece = At<uint32_t>(other, solve::kPiece);
                    theirs = At<uint8_t>(other, solve::kRotation);
                } else if (void* const occupant = At<void*>(info, cell::kOccupant)) {
                    piece = Virtual<uint32_t>(occupant, solve::kOccupantPiece);
                    theirs = Virtual<uint8_t>(occupant, solve::kOccupantRotation);
                }
                const uint32_t mine = rotation;
                uint32_t relative = ((mine ^ theirs) & 0x80) ? theirs + mine : theirs - mine + 8;
                relative &= 0x83;
                if (piece != At<uint32_t>(entry, solve::kEntryPiece) ||
                    relative != At<uint8_t>(entry, solve::kEntryRotation)) {
                    failed = true;
                    break;
                }
            }
            if (!failed) {
                passed = true;
                break;
            }
            node = NextNode(node, rangeTable);
            if (node == after)
                break;
        }
        if (passed) {
            cursor += solve::kSolvedSize;
            continue;
        }
        Insert(self + field::kSet84, &At<uint32_t>(cursor, solve::kKey));
        char* const next = cursor + solve::kSolvedSize;
        char* const end = solved->end;
        for (char* later = next; later != end; later += solve::kSolvedSize) {
            char* const info = CallThis<char*, uint32_t>(addr::cSC4NetworkWorldCache_GetCell,
                                                         self + field::kWorldCache, At<uint32_t>(later, solve::kKey));
            if (info)
                --At<int32_t>(info, cell::kSolvedIndex);
        }
        if (next != solved->end) {
            int32_t moves = static_cast<int32_t>(solved->end - next) / static_cast<int32_t>(solve::kSolvedSize);
            for (char* to = cursor; moves > 0; --moves, to += solve::kSolvedSize) {
                At<uint32_t>(to, 0) = At<uint32_t>(to, 0xC);
                At<uint32_t>(to, 4) = At<uint32_t>(to, 0x10);
                At<uint32_t>(to, 8) = At<uint32_t>(to, 0x14);
            }
        }
        solved->end -= solve::kSolvedSize;
        changed = true;
    }
    return changed ? 0u : 1u;
}

// 0x00635CB0 SolveTiles (ret 8: a preferred network type (or -1), a byte;
// this the tool; called by 0x0062A5C0 with (-1, 1), the guarded 0x0062CD50
// and 0x0063B830). Locals: a vector of resolved keys and two sets (100
// buckets): the cells around and the failed. The cells of +0x84 grown by two
// into the cells around (0x00634480); the edges fixed (0x006359B0 with 1),
// the failed taken out of +0x84 (0x00633E50); multi-type cells resolved
// (0x00634820 with the preferred type), their keys erased from +0x84 and the
// cells around (0x00633ED0); for each of the 13 type rows, each of its rule
// files until a null one, 0x006358C0 (the occupant manager +0x48, the
// type); the failed back into +0x84 (0x00633F10). Then, unless the byte is
// 0 and +0x84 is not empty: the guarded RUL2 pass 0x00634D70 (+0x98, the
// dword as pushed) and the verifiers 0x00635290; either refusing answers the
// valid step count + 0x78 - 1. Else the answer is the smallest step +0x14 of
// the cells left in +0x84 whose cell infos are in the current placement
// (+0x10), starting from +0x78; a cell without either makes it +0x78 - 1
// when it is still +0x78. The locals are destroyed (the failed, the cells
// around, the vector's memory) before every answer.
int32_t __fastcall SolveTiles(char* self, void*, uint32_t preferred, uint32_t flag)
{
    static bool logged = false;
    FirstCall("SolveTiles", logged);
    Vector keys{nullptr, nullptr, nullptr};
    alignas(4) char around[table::kSize] = {};
    alignas(4) char failed[table::kSize] = {};
    Construct(around);
    Construct(failed);
    char* const cells = self + field::kSet84;
    CallThis<void, char*, char*, uint32_t>(addr::cSC4NetworkTool_GrowCellSet, self, cells, around, 2u);
    CallThis<void, char*, char*, uint32_t>(addr::cSC4NetworkTool_FixCellEdges, self, around, failed, 1u);
    CallCdecl<void, char*, char*>(addr::cSC4NetworkTool_SetSubtract, cells, failed);
    CallThis<void, char*, Vector*, uint32_t>(addr::cSC4NetworkTool_ResolveCellTypes, self, cells, &keys, preferred);
    if (keys.begin != keys.end) {
        CallCdecl<void, char*, Vector*>(addr::cSC4NetworkTool_SetEraseKeys, cells, &keys);
        CallCdecl<void, char*, Vector*>(addr::cSC4NetworkTool_SetEraseKeys, around, &keys);
    }
    for (uint32_t type = 0; type < kTypes; ++type) {
        const Vector& files = At<Vector>(TypeRow(type), rul::typerow::kFiles);
        const int32_t count = static_cast<int32_t>(files.end - files.begin) >> 2;
        for (int32_t i = 0; i < count; ++i) {
            char* const file = reinterpret_cast<char**>(At<Vector>(TypeRow(type), rul::typerow::kFiles).begin)[i];
            if (!file)
                break;
            CallThis<void, char*, char*, char*, void*, uint32_t>(addr::cSC4NetworkTool_ApplyRuleFile, self, around,
                                                                 cells, file, At<void*>(self, field::kOccupants), type);
        }
    }
    CallCdecl<void, char*, char*>(addr::cSC4NetworkTool_SetUnion, cells, failed);
    int32_t answer = 0;
    bool refused = false;
    if (static_cast<uint8_t>(flag) || Count(cells) == 0) {
        if (!static_cast<uint8_t>(CallThis<uint32_t, char*, uint32_t>(addr::cSC4NetworkTool_AdjustTileSubsets, self,
                                                                      self + field::kSolved, flag)) ||
            !static_cast<uint8_t>(
                CallThis<uint32_t, char*>(addr::cSC4NetworkTool_VerifyTiles, self, self + field::kSolved))) {
            answer = At<int32_t>(self, field::kValidSteps) - 1;
            refused = true;
        }
    }
    if (!refused) {
        answer = At<int32_t>(self, field::kValidSteps);
        for (char* node = Begin(cells); node; node = NextNode(node, cells)) {
            char* const entry = Entry(self, At<uint32_t>(node, table::kKey));
            if (entry && At<uint8_t>(entry, cell::kInPlacement)) {
                const int32_t step = At<int32_t>(entry, cell::kStep);
                if (step < answer)
                    answer = step;
            } else if (answer == At<int32_t>(self, field::kValidSteps)) {
                answer = At<int32_t>(self, field::kValidSteps) - 1;
            }
        }
    }
    Destroy(failed);
    Destroy(around);
    FreeIfSet(keys.begin);
    return answer;
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kTiles[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_SolveTiles, SolveTiles),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_ApplyRuleFile, ApplyRuleFile),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_MatchRuleFile, MatchRuleFile),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_ResolveCellTypes, ResolveCellTypes),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_VerifyTiles, VerifyTiles),
    OPENSC4_DETOUR(addr::SC4TileVerifierTable_EqualRange, VerifierEqualRange),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_SetSubtract, SetSubtract),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_SetEraseKeys, SetEraseKeys),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_SetUnion, SetUnion),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GrowCellSet, GrowCellSet),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_CellRectAround, CellRectAround),
};

#undef OPENSC4_DETOUR

bool InstallTiles(registry::Mode) { return InstallDetours(kTiles, "tiles"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.tiles", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_SolveTiles,
                        "Our network tool's tile solver: SolveTiles, the rule file application and match, the "
                        "multi-type resolution (RUL1 overrides), the tile verifiers and their table's EqualRange, "
                        "the cell set helpers, GrowCellSet and CellRectAround; the NAM-guarded RUL2 pass stays the "
                        "game's (11 detours; src/core/nettool_solve_1.cpp)",
                        false, InstallTiles}),
};

} // namespace
} // namespace opensc4::nettool_sim
