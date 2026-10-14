// Our cSC4NetworkTool, part t9, first file (build 641; the class map and the fields are
// in src/core/nettool_sim.h, the satisfier's layout in nettool_satisfier.h):
// the height pass and its constraints. After the tile solver has given every
// dragged cell a piece, the height pass FitHeights 0x00637A80 decides the
// height of every vertex the drag touches: it collects the cells to fit
// (the set +0xA4) and the border vertices around them (the set +0xB8), puts
// the bridges' and the +0xE4 records' heights into their cells, fits a plane
// over every regular stretch of the drag (slot 77), gives the first vertex
// height satisfier (+0x124) every cell's vertices with ranges and the
// equality, slope and curvature constraints the pieces ask for, and solves.
// Slot 56 CanTilesBeSupportedOnTerrain asks the second satisfier (+0x188)
// whether the terrain can carry that, slot 57 UpdateTerrainHeights writes
// the second satisfier's heights into the terrain, slot 73 reads a vertex's
// height back. Two groups, each a list of detours at the game's addresses:
//   simulators.nettool.heights: the height pass and its helpers (FitHeights,
//     AddIntersectionCells 0x006371E0, CollectHeightCells 0x00636AB0, the
//     12-byte vector's Resize 0x00637720, SetBridgeCellHeights 0x0062FB90,
//     SetRecordCellHeights 0x006274B0, InsertCellConstraints 0x00636910,
//     InsertIntersectionConstraints 0x00644710), slot 77 with
//     ComputeSegmentPlane 0x00637780 and IsVertexBlocked 0x006374B0, slots
//     56, 57 and 73 (14 detours);
//   simulators.nettool.heightcons: the per-cell constraint helpers next to
//     the guarded 0x00636690: InsertEqualityConstraint 0x00636000,
//     InsertSmoothnessConstraint 0x00636030 and GetCrossSections 0x00636360
//     (all three called by NAM at their addresses: naked thunks),
//     IsCellInHeightSet 0x00636070 and slot 76 InsertRangeConstraintsForCell
//     0x006360C0 (5 detours).
// The split: the second group is what NAM's NetworkSlopes code and the
// guarded function work with; it can be switched off on its own.
//
// Every function was written from the disassembly. Calls between them go to
// the game's addresses (through the address entries), so each detour stands
// alone and OPENSC4_NETTOOL_SKIP bisects them.
//
// Left as the game's:
//   - 0x00636690 InsertSlopeAndSmoothnessConstraintsForCell (NAM GUARD: its
//     NetworkSlopes jumps out at 0x006366C5 and replaces 0x006366C5 to
//     0x00636907): no detour, no translation; InsertCellConstraints calls it
//     at its address.
//   - nothing else of the part. Under it the game's (or the earlier parts')
//     code at its address: the cell entry 0x00633220, 0x00632EF0 (left the
//     game's by part t2: it writes stack leftovers into the cache), the
//     satisfier's functions, EdgeByte 0x00620DE0, FindPieceRecord 0x00625860,
//     PlaneFromPoints 0x005D67E0 (the plane's x87 work), the template code.
// Nothing here is drawing: UpdateTerrainHeights writes the terrain's vertex
// altitudes (simulation state) and asks the terrain to redo the rectangle
// (slots 0x15C, 0x110, 0x160), as the game does.
//
// NAM. Its NetworkSlopes code (the replacement of 0x006366C5-0x00636907)
// calls InsertEqualityConstraint, InsertSmoothnessConstraint and
// GetCrossSections at their addresses from compiled C++ (thiscall, `ret 8`,
// `ret 0x10`, `ret 0x1C`), and reads the tool (+0x124, +0x248..+0x28C), the
// cell infos and the type table by layout; all of that stays in the game's
// memory with the game's layout and nothing is kept across calls. The three
// detours keep the conventions and, through naked thunks, leave EAX, ECX,
// EDX and the flags as the game's do wherever those are in a known state:
//   - InsertEqualityConstraint: on its early exits (v1 == v2, v1 < 0, v2 <
//     0) the game leaves EAX = v1, EDX = v2, ECX = this and the flags of the
//     failing `cmp eax, edx` / `test eax, eax` / `test edx, edx`; ours the
//     same (the thunk repeats that instruction). Otherwise the game
//     tail-jumps to 0x0065FDB0 with ECX = this + 0x124, EAX = v1, EDX = v2
//     and the arguments as they came; so does ours, so what 0x0065FDB0
//     leaves comes back as in the game.
//   - InsertSmoothnessConstraint: on its early exits the game leaves ECX =
//     this and ESI restored; v1 < 0: EAX and EDX the caller's; v2 < 0: EDX =
//     v2, EAX the caller's; v3 < 0 and the three equalities: EDX = v2, EAX =
//     v3; the flags of the failing test or compare. Ours the same. Otherwise
//     the game calls 0x00660010 (v1, v2, v3, s; ECX = this + 0x124) and
//     returns what it leaves (only POPs after it); ours calls it last from
//     the thunk the same way.
//   - GetCrossSections: every exit of the game's ends with `add esp, 8;
//     ret 0x1C` from the same stack depth, so the flags are those of that
//     add; the thunk ends the same way from the same depth. AL is the answer;
//     EAX's upper bytes, ECX and EDX depend on the exit and ours computes
//     them as the game's leave them: kind-0 edge: EAX the shifted edge word
//     with AL 0, ECX side * 8, EDX the side; the exits after the "start"
//     pair is checked: EAX the start pointer with AL 1, ECX 1 or 0 (whether
//     start[1] is a corner of the cell), EDX the address of the corner
//     start[0] matched with DL 1, or info + 0xB8 with DL 0. When the
//     neighbour across is in the set and its cell is to be looked at, the
//     game's last call is the cell entry 0x00633220; the thunk makes that
//     call itself and then runs the game's last instructions (`mov cl,
//     [eax+0x56]`, the start pair + 100000, `mov al, 1`), so ECX and EDX
//     come back as the cell entry left them, as in the game.
// The scan of every game caller after each call (both branches): the
// guarded 0x00636690 after 0x00636000 (0x0063679F, 0x006367B8, 0x006367D1),
// 0x00636360 (0x00636807: `test al, al`) and 0x00636030 (0x006368F3); the
// height pass's callers 0x0062D2F2, 0x0062D31E, 0x0063BB5B, 0x00658CB8
// (`test al, al`); every call of the part's helpers inside the part; slot
// 56's callers test AL; none reads ECX or EDX before writing it, none reads
// more of EAX than AL where the answer is a byte. NAM's compiled C++ cannot
// rely on ECX, EDX or the flags across a call. So the thunks are fidelity
// beyond need; the other 16 functions are plain detours (slot 73 returns its
// height in ST0 as a double: the satisfier's float, or the terrain's ST0 as
// it left it).
//
// sc4fix and our wrappers: none detours or tests return addresses in
// 0x006274B0-0x00644F3C (src/core/game_*.cpp, *fix*.cpp checked); the
// puzzle-piece fix is in NextEntry 0x0065ED80, under Solve.
//
// Floats. The game runs the x87 at 53 bits, so intermediates are doubles
// where every operation is +, -, *, / or a compare and every float load or
// store rounds as `float`:
//   - slot 77: the plane height h = ((X * s * a) + (Z * s * c) + d) * inv
//     with X and Z the corner's cell coordinates as unsigned (FILD + 2^32
//     for a negative int), s the terrain scale +0x238, a, c, d the plane's
//     floats, inv = float(-1 / b); the low bound float(h - m) is stored,
//     the high one h + m stays unrounded on the x87 stack and is compared
//     unrounded (with the cell height, with the pair's high) and rounded
//     only where the game stores it; h itself is compared unrounded with
//     the cell height and stored rounded into the second pair. Every
//     compare's NaN branch is the game's (comments at each).
//   - ComputeSegmentPlane: coordinates float(n * s); the mean of two
//     heights float((a + b) * 0.5); the y of the far points rounded once
//     from the same unrounded value; the negation of the plane's d by its
//     sign bit (FCHS); PlaneFromPoints stays the game's.
//   - slot 56: lo = float(t - below), hi = float(t + above) compared
//     unrounded with the satisfier's height; the piece drop h - row +0x8C
//     compared unrounded with lo and stored rounded; the equality test of hi
//     and lo with FUCOMPP (unordered = not equal).
//   - slot 57: |h - t| with t the terrain's ST0 (a double at 53 bits)
//     against float 0.001, NaN not above.
//   - SetBridgeCellHeights: float(altitude - first) once, stored twice; the
//     bridge cells' four floats copied (FLD/FSTP of a float: the same bits
//     unless it is a signalling NaN, which the x87 would quieten; ours
//     copies the bits).
//   - FitHeights: the satisfier's upper bound float(top + 20.0).
//   - IsVertexBlocked: the terrain's ST0 stored as a float; the lot range
//     compared with 0 by FUCOMPP (equal only when ordered and equal).
//
// Stack leftovers the game stores (ours are 0):
//   - AddIntersectionCells' solved cell dword +0x4: the rotation byte (the
//     first record's turn + 0x80 for a flip) goes into the low byte of a
//     frame dword whose upper bytes are never written; the whole dword is
//     appended (in place, or copied into the grown vector), so the solved
//     cell's bytes +0x5 to +0x7 are the original's stack contents. Readers:
//     the same as for the tile solver's (src/core/nettool_solve_1.cpp's
//     top: slot 55, AddCellPreview, VerifyTiles, PatchTilePair, the cost,
//     the copies; every one takes the low byte). Verdict: harmless.
//   - none else: GetCrossSections writes all six words and the flag it
//     hands back before AL 1 (on AL 0 the guarded caller reads none);
//     IsVertexBlocked's two lot outputs are written by the lot manager
//     (slot 0x68 0x0072AE20 stores both first); slot 76's lot rectangle by
//     the lot's slot 0x38 (four words); slot 56's height by GetResult or
//     from the cell; the planes' twelve point words are all written.
// Other stores into the game's stack, never read again as what they were:
// GetCrossSections keeps the lot offset, the neighbour z and the side in
// three of its argument slots; IsVertexBlocked and slot 57 their occupant
// and vertex in theirs; ComputeSegmentPlane its first height and a vertex
// index; FitHeights' helpers the insert's key. Ours use locals.
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the part's own, at the game's addresses: AddIntersectionCells none,
//     CollectHeightCells one (ret 4), SC4Vector12::Resize two (ret 8),
//     SetBridgeCellHeights / SetRecordCellHeights one (ret 4),
//     InsertCellConstraints one (ret 4), InsertIntersectionConstraints one
//     (ret 4), ComputeSegmentPlane five (ret 0x14), IsVertexBlocked three
//     (ret 0xC), IsCellInHeightSet two (ret 8); the guarded 0x00636690 one
//     (ret 4); the tool's slots 75 (0x12C) none, 76 (0x130) one, 77 (0x134)
//     one, 78 (0x138) one;
//   - the tool's: the cell entry 0x00633220 one (ret 4), MakeTypeCellInfo
//     0x00632EF0 three (ret 0xC), EdgeByte 0x00620DE0 one (ret 4; this the
//     edge word), FindPieceRecord 0x00625860 one (ret 4);
//   - the satisfier's: Reset 0x006601A0 none, Begin 0x0065E710 two (ret 8),
//     Solve 0x00660B10 none, AddVertex 0x0065FB60 two (ret 8),
//     AddFixedVertex 0x00660360 two (ret 8), RestrictVertex 0x0065FED0 three
//     (ret 0xC), SetPreferredRange 0x0065EBE0 three (ret 0xC),
//     SetPreferredHeight 0x0065EB90 two (ret 8), GetResult 0x0065EB00 two
//     (ret 8), BeginResults 0x0065EF70 none, NextResult 0x0065EB50 two (ret
//     8), InsertEqualityConstraint 0x0065FDB0 two (ret 8),
//     InsertSlopeConstraint 0x0065FF60 three (ret 0xC),
//     InsertCurvatureConstraint 0x00660010 four (ret 0x10);
//   - the terrain (+0x44): slot 0x24 two (0x0074C5D0, ret 8: x, z; the
//     vertex index), 0x30 one (0x007413C0, ret 4; ST0), 0x60 none
//     (0x0074C6A0; ST0), 0xC8 two (0x00744E60, ret 8: vertex, height bits),
//     0x15C none, 0x110 four (0x00749850, ret 0x10: 1, 1, the rectangle, 4),
//     0x160 none; the city (0x00B43CEC) slots 0x174 and 0x178 none; the lot
//     manager (0x00B43D08) slot 0x18 three (0x0072B410: x, z, 0), the tool's
//     +0x4C (the city's lot manager) slot 0x68 four (0x0072AE20: x, z, two
//     floats out), a lot's slot 0x38 one (the rectangle out); the occupant
//     manager (+0x48) slot 0x80 four (out, x, z, filter; AL); an occupant's
//     QueryInterface (iid, out) and Release; the network occupant's slot
//     0x24 one (a mask; AL);
//   - template code: the hash sets' Clear 0x008EBA60 none, Resize 0x004F0940
//     one (ret 4), Insert 0x00624DD0 two (ret 8: a result out, the key's
//     address), SC4HashTable::Begin 0x004B0390 one (ret 4: the iterator
//     out); SC4Vector16::UninitializedCopy 0x00637E20 cdecl four;
//     SC4Vector12::Copy 0x006143E0 cdecl five; SC4Vector12::InsertN
//     0x00638D40 three (ret 0xC); SC4PtrVector::InsertN 0x0051CA60 five (ret
//     0x14); SC4::stl_allocate 0x0090CF54 and SC4::Free 0x0090CF63 cdecl
//     one; PlaneFromPoints 0x005D67E0 cdecl four; the list node allocator
//     0x00B207E4 (slots 0xC one, 0x10 one; fetched through the framework as
//     the game does), SC4List::Clear2 0x00413E60 and Destroy2 0x00413FE0
//     none.
#include "core/nettool_heights.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim {
namespace {

using namespace heights;

// ====================================================================================
// The height pass
// ====================================================================================

// 0x006274B0 (ret 4: the portions; FitHeights'): for each +0xE4 record while
// its last step is below the valid step count, the portion {first, last,
// first height, last height} appended, the first step's cells set to the
// first height and the last step's to the last.
void __fastcall SetRecordCellHeights(char* self, void*, Vector* portions)
{
    static bool logged = false;
    FirstCall("SetRecordCellHeights", logged);
    char* const end = At<char*>(self, hfield::kRecordsEnd);
    for (char* rec = At<char*>(self, field::kVectorE4); rec != end; rec += e4::kSize) {
        const uint32_t last = At<uint32_t>(rec, e4::kLast);
        if (static_cast<int32_t>(last) >= At<int32_t>(self, field::kValidSteps))
            return;
        const uint32_t record[4] = {At<uint32_t>(rec, e4::kFirst), last, At<uint32_t>(rec, e4::kFirstHeight),
                                    At<uint32_t>(rec, e4::kLastHeight)};
        PushPortion(*portions, record);
        SetStepHeights(self, At<uint32_t>(rec, e4::kFirst), At<uint32_t>(rec, e4::kFirstHeight));
        SetStepHeights(self, At<uint32_t>(rec, e4::kLast), At<uint32_t>(rec, e4::kLastHeight));
    }
}

// 0x0062FB90 (ret 4: the portions; FitHeights'): for each major bridge while
// its last step is below the valid step count, the portion {first, last, h,
// h} with h = float(altitude - the bridge record's first float), and each
// cell of its steps (counted over the bridge) given the bridge cell's four
// heights as heights and as both words of the first pairs.
void __fastcall SetBridgeCellHeights(char* self, void*, Vector* portions)
{
    static bool logged = false;
    FirstCall("SetBridgeCellHeights", logged);
    char* const end = At<char*>(self, hfield::kBridgesEnd);
    for (char* b = At<char*>(self, field::kMajorBridges); b != end; b += mbridge::kSize) {
        const uint32_t last = At<uint32_t>(b, mbridge::kLast);
        if (static_cast<int32_t>(last) >= At<int32_t>(self, field::kValidSteps))
            return;
        char* const records = At<char*>(At<char*>(self, field::kRow), row::kBridges);
        const float* const first = At<const float*>(
            records + At<uint32_t>(b, mbridge::kRecord) * static_cast<uint32_t>(bridge::kSize), mbridge::kRecordFloats);
        const float h =
            static_cast<float>(static_cast<double>(At<float>(b, mbridge::kAltitude)) - static_cast<double>(*first));
        const uint32_t record[4] = {At<uint32_t>(b, mbridge::kFirst), last, satisfier::Bits(h), satisfier::Bits(h)};
        PushPortion(*portions, record);
        uint32_t counted = 0;
        for (int32_t s = At<int32_t>(b, mbridge::kFirst); s <= At<int32_t>(b, mbridge::kLast); ++s) {
            char* const stepAt = Steps(self) + static_cast<uint32_t>(s) * step::kSize;
            for (uint32_t c = At<uint32_t>(stepAt, 0); c <= At<uint32_t>(Steps(self) + static_cast<uint32_t>(s) *
                                                                                            step::kSize,
                                                                        step::kLastCell);
                 ++c) {
                char* const info = Entry(self, PointKey(Cells(self) + c * kPointSize));
                char* const heights = At<char*>(b, mbridge::kCells) + counted * mbridge::kCellSize + mbridge::kCellHeights;
                ++counted;
                for (std::size_t k = 0; k < 4; ++k) {
                    const uint32_t value = At<uint32_t>(heights, 4 * k);
                    Field<uint32_t>(info, cell::kRanges + 8 * k + 4) = value;
                    Field<uint32_t>(info, cell::kRanges + 8 * k) = value;
                    Field<uint32_t>(info, cell::kHeights + 4 * k) = value;
                }
            }
        }
    }
}

// The RUL0 store's rule of an ID (the game's inlined search), or null.
char* StoreRule(uint32_t id)
{
    char* const node = satisfier::Find(reinterpret_cast<char*>(addr::cSC4NetworkTool_Rul0Store), id);
    return node ? node + rul::store::kRule : nullptr;
}

// 0x006371E0 (no arguments; FitHeights'): for each intersection whose rule
// is in the store (the cell entry of the intersection's own cell asked
// first, its answer unused), each checked point of the rule: a cell info of
// the tool's type at the point (MakeTypeCellInfo), marked +0x52; its flags
// set to the rule's +0xE4 when the point's cons cell has its "alone" byte
// (a point whose grid value has no cons cell makes the game read address 5:
// ours too); the cell appended to the solved cells as {0, the first
// record's turn + 0x80 for a flip, the cell's key}.
void __fastcall AddIntersectionCells(char* self, void*)
{
    static bool logged = false;
    FirstCall("AddIntersectionCells", logged);
    char* const end = At<char*>(self, hfield::kIntersectionsEnd);
    for (char* at = At<char*>(self, field::kIntersections); at != end; at += isect::kSize) {
        char* const rule = StoreRule(At<uint32_t>(at, isect::kRule));
        if (!rule)
            continue;
        Entry(self, (At<uint32_t>(at, isect::kZ) << 16) + At<uint32_t>(at, isect::kX));
        char* const records = At<char*>(rule, rul::rul0::kRecords);
        char* const pointsEnd = At<Vector>(rule, rul::rul0::kChecked).end;
        for (char* pt = At<Vector>(rule, rul::rul0::kChecked).begin; pt != pointsEnd; pt += kPointSize) {
            char* const info = CallThis<char*, uint32_t, uint32_t, uint32_t>(
                addr::cSC4NetworkTool_MakeTypeCellInfo, self, At<uint32_t>(at, isect::kX) + At<uint32_t>(pt, 0),
                At<uint32_t>(pt, 4) + At<uint32_t>(at, isect::kZ), At<uint32_t>(self, field::kType));
            Field<uint8_t>(info, cell::kHasFlag100000) = 1;
            // The cons cell of the point's grid value.
            char* value = nullptr;
            const uint32_t column = At<uint32_t>(pt, 0) - At<uint32_t>(rule, rul::rul0::kMinX);
            const uint32_t width = At<uint32_t>(rule, rul::rul0::kCellWidth);
            if (column < width) {
                const uint32_t index = (At<uint32_t>(pt, 4) - At<uint32_t>(rule, rul::rul0::kMinZ)) * width + column;
                const Vector& grid = At<Vector>(rule, rul::rul0::kCellGrid);
                if (index < static_cast<uint32_t>(grid.end - grid.begin)) {
                    const uint8_t key = At<uint8_t>(grid.begin, index);
                    char* const table = rule + rul::rul0::kConsCells;
                    char* node = reinterpret_cast<char**>(At<Vector>(table, rul::table::kBuckets).begin)
                        [static_cast<uint32_t>(key) % solve::BucketCount(table)];
                    while (node && At<uint8_t>(node, rul::table::kKey) != key)
                        node = At<char*>(node, rul::table::kNext);
                    if (node)
                        value = node + 8;
                }
            }
            const uint8_t alone =
                *reinterpret_cast<volatile const uint8_t*>(reinterpret_cast<std::uintptr_t>(value) + 5);
            if (alone)
                Field<uint32_t>(info, cell::kFlags) = At<uint32_t>(rule, rul::rul0::kWordE4);
            const uint32_t key = (Field<uint32_t>(info, cell::kZ) << 16) + Field<uint32_t>(info, cell::kX);
            const uint8_t rotation = static_cast<uint8_t>((At<uint8_t>(records, rul::record::kFlip) ? 0x80 : 0) +
                                                          At<uint8_t>(records, rul::record::kTurn));
            // The game's inlined push_back of {0, rotation, key}.
            Vector& solved = At<Vector>(self, field::kSolved);
            if (solved.end != solved.capacity) {
                if (solved.end) {
                    At<uint32_t>(solved.end, 0) = 0;
                    At<uint32_t>(solved.end, 4) = rotation;
                    At<uint32_t>(solved.end, 8) = key;
                }
                solved.end += solve::kSolvedSize;
                continue;
            }
            const uint32_t size =
                static_cast<uint32_t>(static_cast<int32_t>(solved.end - solved.begin) / static_cast<int32_t>(12));
            const uint32_t grown = (size < 1 ? 1u : size) + size;
            char* const buffer = grown ? solve::Allocate(grown * 12) : nullptr;
            char* to = buffer;
            for (char* from = solved.begin; from != solved.end; from += 12, to += 12)
                if (to)
                    for (std::size_t word = 0; word < 12; word += 4)
                        At<uint32_t>(to, word) = At<uint32_t>(from, word);
            if (to) {
                At<uint32_t>(to, 0) = 0;
                At<uint32_t>(to, 4) = rotation;
                At<uint32_t>(to, 8) = key;
            }
            solve::FreeIfSet(solved.begin);
            solved.begin = buffer;
            solved.end = to + 12;
            solved.capacity = buffer + grown * 12;
        }
    }
}

// A cell key into one of the tool's sets (Resize to the count + 1, Insert).
inline void InsertKey(char* self, std::size_t set, uint32_t key) { solve::Insert(self + set, &key); }

// 0x00636AB0 (ret 4: a radius; FitHeights'): the sets +0xA4 and +0xB8
// cleared. When the step count +0x70 equals +0x74 and the solved count: the
// dragged cells' bounding rectangle grown by the radius (clamped), every
// cell in it whose cell info has not +0x55 into +0xA4 and every vertex of
// its border (not on the city's edge) into +0xB8. Else (solved cells
// present): per vertex row the x span (0x00B460E8, int16 pairs) around
// every solved cell; +0xA4 gets the cells inside the spans, +0xB8 the
// spans' ends and the steps between neighbouring rows.
void __fastcall CollectHeightCells(char* self, void*, int32_t radius)
{
    static bool logged = false;
    FirstCall("CollectHeightCells", logged);
    CallThis<void>(addr::SC4Uint32HashSet_Clear, self + hfield::kSetA4);
    CallThis<void>(addr::SC4Uint32HashSet_Clear, self + hfield::kSetB8);
    const uint32_t verticesX = At<uint32_t>(self, field::kVertexStride);
    const uint32_t verticesZ = At<uint32_t>(self, hfield::kVerticesZ);
    const int32_t stepCount = At<int32_t>(self, field::kStepCount);
    if (stepCount == At<int32_t>(self, hfield::kWord74) && solve::SolvedCount(self) == stepCount) {
        int32_t minX = 10000, minZ = 10000, maxX = -1, maxZ = -1;
        const int32_t n = At<int32_t>(self, field::kValidSteps);
        for (int32_t s = 0; s < n; ++s) {
            char* const stepAt = Steps(self) + static_cast<uint32_t>(s) * step::kSize;
            const uint32_t first = At<uint32_t>(stepAt, 0), last = At<uint32_t>(stepAt, step::kLastCell);
            if (first > last)
                continue;
            uint32_t c = first, left = last - first + 1;
            do { // the game's count-down loop (`dec; jne`)
                const int32_t x = At<int32_t>(Cells(self) + c * kPointSize, 0);
                const int32_t z = At<int32_t>(Cells(self) + c * kPointSize, 4);
                if (x < minX)
                    minX = x;
                if (x > maxX)
                    maxX = x;
                if (z < minZ)
                    minZ = z;
                if (z > maxZ)
                    maxZ = z;
                ++c;
            } while (--left);
        }
        minX = static_cast<int32_t>(static_cast<uint32_t>(minX) - static_cast<uint32_t>(radius));
        minZ = static_cast<int32_t>(static_cast<uint32_t>(minZ) - static_cast<uint32_t>(radius));
        maxX = static_cast<int32_t>(static_cast<uint32_t>(maxX) + static_cast<uint32_t>(radius) + 1);
        maxZ = static_cast<int32_t>(static_cast<uint32_t>(maxZ) + static_cast<uint32_t>(radius) + 1);
        if (minX < 0)
            minX = 0;
        if (minZ < 0)
            minZ = 0;
        if (static_cast<uint32_t>(maxX) > At<uint32_t>(self, field::kCellsX))
            maxX = static_cast<int32_t>(verticesX - 1);
        if (static_cast<uint32_t>(maxZ) > At<uint32_t>(self, field::kCellsZ))
            maxZ = static_cast<int32_t>(verticesZ - 1);
        uint32_t rowKey = (static_cast<uint32_t>(minZ) << 16) + static_cast<uint32_t>(minX);
        for (int32_t z = minZ; z <= maxZ; ++z, rowKey += 0x10000) {
            uint32_t key = rowKey;
            for (int32_t x = minX; x <= maxX; ++x, ++key) {
                char* const info = Entry(self, key);
                if (info && !At<uint8_t>(info, cell::kKind3))
                    InsertKey(self, hfield::kSetA4, key);
                if ((x == minX || x == maxX || z == minZ || z == maxZ) && x != 0 &&
                    static_cast<uint32_t>(x) != verticesX - 1 && z != 0 && static_cast<uint32_t>(z) != verticesZ - 1)
                    InsertKey(self, hfield::kSetB8,
                              VertexIndex(self, static_cast<uint32_t>(x), static_cast<uint32_t>(z)));
            }
        }
        return;
    }
    if (solve::SolvedCount(self) == 0)
        return;
    auto lo = [](uint32_t z) -> int16_t& { return *reinterpret_cast<int16_t*>(addr::cSC4NetworkTool_RowSpans + z * 4); };
    auto hi = [](uint32_t z) -> int16_t& {
        return *reinterpret_cast<int16_t*>(addr::cSC4NetworkTool_RowSpans + z * 4 + 2);
    };
    for (uint32_t z = 0; z < At<uint32_t>(self, hfield::kVerticesZ); ++z) {
        lo(z) = 10000;
        hi(z) = -1;
    }
    int32_t minZ = 10000, maxZ = -1;
    const Vector& solved = At<Vector>(self, field::kSolved);
    for (char* s = solved.begin; s != At<Vector>(self, field::kSolved).end; s += solve::kSolvedSize) {
        char* const info = Entry(self, At<uint32_t>(s, solve::kKey));
        if (!info)
            continue;
        const uint32_t cz = At<uint32_t>(info, cell::kZ), cx = At<uint32_t>(info, cell::kX);
        // The sign of the difference (`jns`), the ends against the vertex
        // counts unsigned.
        int32_t zlo = static_cast<int32_t>(cz - static_cast<uint32_t>(radius));
        if (zlo < 0)
            zlo = 0;
        uint32_t zhi = static_cast<uint32_t>(radius) + cz + 1;
        if (zhi >= At<uint32_t>(self, hfield::kVerticesZ))
            zhi = At<uint32_t>(self, hfield::kVerticesZ) - 1;
        if (minZ > zlo)
            minZ = zlo;
        if (maxZ < static_cast<int32_t>(zhi))
            maxZ = static_cast<int32_t>(zhi);
        int32_t xlo = static_cast<int32_t>(cx - static_cast<uint32_t>(radius));
        if (xlo < 0)
            xlo = 0;
        uint32_t xhi = static_cast<uint32_t>(radius) + cx + 1;
        if (xhi >= At<uint32_t>(self, field::kVertexStride))
            xhi = At<uint32_t>(self, field::kVertexStride) - 1;
        for (int32_t z = zlo; z <= static_cast<int32_t>(zhi); ++z) {
            if (lo(static_cast<uint32_t>(z)) > xlo)
                lo(static_cast<uint32_t>(z)) = static_cast<int16_t>(xlo);
            if (hi(static_cast<uint32_t>(z)) < static_cast<int32_t>(xhi))
                hi(static_cast<uint32_t>(z)) = static_cast<int16_t>(xhi);
        }
    }
    // The cells inside the spans (x from the low end up to before the high).
    for (int32_t z = minZ; z < maxZ; ++z) {
        const int32_t high = hi(static_cast<uint32_t>(z)), low = lo(static_cast<uint32_t>(z));
        if (low >= high)
            continue;
        uint32_t key = (static_cast<uint32_t>(z) << 16) + static_cast<uint32_t>(low);
        for (int32_t left = high - low; left; --left, ++key) {
            char* const info = Entry(self, key);
            if (info && !At<uint8_t>(info, cell::kKind3))
                InsertKey(self, hfield::kSetA4, key);
        }
    }
    auto row = [&](int32_t z) {
        for (int32_t x = lo(static_cast<uint32_t>(z)); x <= hi(static_cast<uint32_t>(z)); ++x)
            if (x != 0 && static_cast<uint32_t>(x) != At<uint32_t>(self, field::kVertexStride) - 1)
                InsertKey(self, hfield::kSetB8, VertexIndex(self, static_cast<uint32_t>(x), static_cast<uint32_t>(z)));
    };
    // The first and the last row's spans whole.
    if (minZ != 0)
        row(minZ);
    if (static_cast<uint32_t>(maxZ) != At<uint32_t>(self, hfield::kVerticesZ) - 1)
        row(maxZ);
    // The rows between: the span's two ends, and the steps to the
    // neighbouring rows' ends that lie inside it.
    const int32_t lastRow = maxZ - 1;
    for (int32_t z = minZ + 1; z <= lastRow; ++z) {
        const uint32_t uz = static_cast<uint32_t>(z);
        if (hi(uz) < 0)
            continue;
        const int32_t low = lo(uz), high = hi(uz);
        const int32_t around[4] = {lo(uz - 1), hi(uz - 1), lo(uz + 1), hi(uz + 1)};
        if (around[1] < 0 || around[3] < 0) {
            // A neighbouring row without a span: the whole span but its
            // first vertex when that is 0, up to before the last column.
            for (int32_t x = low == 0 ? 1 : low; x <= high; ++x) {
                if (static_cast<uint32_t>(x) >= At<uint32_t>(self, field::kVertexStride) - 1)
                    break;
                InsertKey(self, hfield::kSetB8, VertexIndex(self, static_cast<uint32_t>(x), uz));
            }
            continue;
        }
        if (low != 0)
            InsertKey(self, hfield::kSetB8, VertexIndex(self, static_cast<uint32_t>(low), uz));
        if (static_cast<uint32_t>(high) != At<uint32_t>(self, field::kVertexStride) - 1)
            InsertKey(self, hfield::kSetB8, VertexIndex(self, static_cast<uint32_t>(high), uz));
        for (uint32_t k = 0; k < 4; ++k) {
            const int32_t c = around[k];
            if (c <= low || c >= high)
                continue;
            if (k & 1) {
                for (int32_t x = c; x < high; ++x)
                    InsertKey(self, hfield::kSetB8, VertexIndex(self, static_cast<uint32_t>(x), uz));
            } else {
                for (int32_t x = low + 1; x <= c; ++x)
                    InsertKey(self, hfield::kSetB8, VertexIndex(self, static_cast<uint32_t>(x), uz));
            }
        }
    }
}

// 0x00637720 (ret 8: a count, the value's address; this a vector of 12-byte
// records; FitHeights'): below the count the value appended (InsertN at the
// end), above it the tail erased (Copy of the empty range [end, end) to the
// count's place, as the game's erase; its fourth argument the count's
// address, its fifth 0).
void __fastcall ResizeVector12(Vector* self, void*, uint32_t count, const uint32_t* value)
{
    static bool logged = false;
    FirstCall("ResizeVector12", logged);
    char* const begin = self->begin;
    char* const end = self->end;
    const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(end - begin) / static_cast<int32_t>(12));
    if (count < size) {
        self->end = CallCdecl<char*, char*, char*, char*, uint32_t*, uint32_t>(addr::SC4Vector12_Copy, end, end,
                                                                               begin + count * 12, &count, 0u);
        return;
    }
    CallThis<void, char*, uint32_t, const uint32_t*>(addr::SC4Vector12_InsertN, self, end, count - size, value);
}

// 0x00636910 (ret 4: a hash set of cell keys; FitHeights' with +0xA4): over
// the whole set slot 76 for each key, then the guarded 0x00636690 at its
// address, then slot 78. The first pass starts from SC4HashTable::Begin, the
// other two from the game's inlined first-bucket scan.
void __fastcall InsertCellConstraints(char* self, void*, char* set)
{
    static bool logged = false;
    FirstCall("InsertCellConstraints", logged);
    for (char* node = solve::Begin(set); node; node = solve::NextNode(node, set))
        Virtual<void>(self, 0x130, At<uint32_t>(node, rul::table::kKey));
    for (char* node = FirstNode(set); node; node = solve::NextNode(node, set))
        CallThis<void, uint32_t>(addr::cSC4NetworkTool_InsertSlopeAndSmoothnessConstraintsForCell, self,
                                 At<uint32_t>(node, rul::table::kKey));
    for (char* node = FirstNode(set); node; node = solve::NextNode(node, set))
        Virtual<void>(self, 0x138, At<uint32_t>(node, rul::table::kKey));
}

// 0x00644710 (ret 4: a satisfier; FitHeights' with +0x124, slot 56's with
// +0x188): for each intersection whose rule is in the store (the cell
// entry of its cell asked first), each checked point: its cons grid piece
// (0 outside the grid), the cell info at the point (none: next point), its
// four vertices added at their heights, then by piece (v0..v3 the corners
// NW, SW, SE, NE; s the type row's +0x100, d its +0x104):
//   1 '-': v0 = v1, v2 = v3, |v0 - v3| <= s, |v1 - v2| <= s
//   2 '|': v0 = v3, v1 = v2, |v0 - v1| <= s, |v3 - v2| <= s
//   3 '/': v0 = v2, |v1 - v3| <= d
//   4 '\': v1 = v3, |v0 - v2| <= d
//   5 'F': v0 = v2, |v1 - v3| <= d, curvature (v1, v0, v3) 0
//   6 'R': v1 = v3, |v0 - v2| <= d, curvature (v0, v1, v2) 0
//   7 'X': v0 = v1, v0 = v2, v0 = v3
//   8 '#': |v0 - v1|, |v0 - v3|, |v1 - v2|, |v3 - v2| <= s
// (the game's jump table 0x00644AF8; the row's word read before each call).
void __fastcall InsertIntersectionConstraints(char* self, void*, char* sat)
{
    static bool logged = false;
    FirstCall("InsertIntersectionConstraints", logged);
    char* const end = At<char*>(self, hfield::kIntersectionsEnd);
    for (char* at = At<char*>(self, field::kIntersections); at != end; at += isect::kSize) {
        char* const rule = StoreRule(At<uint32_t>(at, isect::kRule));
        if (!rule)
            continue;
        Entry(self, (At<uint32_t>(at, isect::kZ) << 16) + At<uint32_t>(at, isect::kX));
        char* const pointsEnd = At<Vector>(rule, rul::rul0::kChecked).end;
        for (char* pt = At<Vector>(rule, rul::rul0::kChecked).begin; pt != pointsEnd; pt += kPointSize) {
            const uint32_t px = At<uint32_t>(pt, 0), pz = At<uint32_t>(pt, 4);
            const uint32_t index = (At<uint32_t>(rule, rul::rul0::kConsAnchorZ) + pz) *
                                       At<uint32_t>(rule, rul::rul0::kConsWidth) +
                                   px + At<uint32_t>(rule, rul::rul0::kConsAnchorX);
            const Vector& grid = At<Vector>(rule, rul::rul0::kConsGrid);
            const uint32_t piece =
                index < static_cast<uint32_t>(grid.end - grid.begin) ? At<uint8_t>(grid.begin, index) : 0u;
            char* const info = Entry(self, ((At<uint32_t>(at, isect::kZ) + pz) << 16) + At<uint32_t>(at, isect::kX) + px);
            if (!info)
                continue;
            for (std::size_t k = 0; k < 4; ++k)
                AddVertex(sat, At<uint32_t>(info, cell::kVertices + 4 * k), At<uint32_t>(info, cell::kHeights + 4 * k));
            auto v = [info](std::size_t k) { return At<uint32_t>(info, cell::kVertices + 4 * k); };
            auto straight = [self] { return At<uint32_t>(At<char*>(self, field::kRow), hrow::kSlopeStraight); };
            auto diagonal = [self] { return At<uint32_t>(At<char*>(self, field::kRow), hrow::kSlopeDiagonal); };
            switch (piece) {
            case 1:
                Equality(sat, v(0), v(1));
                Equality(sat, v(2), v(3));
                Slope(sat, v(0), v(3), straight());
                Slope(sat, v(1), v(2), straight());
                break;
            case 2:
                Equality(sat, v(0), v(3));
                Equality(sat, v(1), v(2));
                Slope(sat, v(0), v(1), straight());
                Slope(sat, v(3), v(2), straight());
                break;
            case 3:
                Equality(sat, v(0), v(2));
                Slope(sat, v(1), v(3), diagonal());
                break;
            case 4:
                Equality(sat, v(1), v(3));
                Slope(sat, v(0), v(2), diagonal());
                break;
            case 5:
                Equality(sat, v(0), v(2));
                Slope(sat, v(1), v(3), diagonal());
                Curvature(sat, v(1), v(0), v(3), 0);
                break;
            case 6:
                Equality(sat, v(1), v(3));
                Slope(sat, v(0), v(2), diagonal());
                Curvature(sat, v(0), v(1), v(2), 0);
                break;
            case 7:
                for (std::size_t k = 1; k < 4; ++k)
                    Equality(sat, v(0), v(k));
                break;
            case 8:
                Slope(sat, v(0), v(1), straight());
                Slope(sat, v(0), v(3), straight());
                Slope(sat, v(1), v(2), straight());
                Slope(sat, v(3), v(2), straight());
                break;
            default:
                break;
            }
        }
    }
}

// 0x006374B0 (ret 0xC: a vertex, a height out, a flag out; this the tool;
// ComputeSegmentPlane's): the flag cleared, the terrain's height at the
// vertex stored as a float; the lot manager's adjustment range of the vertex
// (+0x4C slot 0x68: x, z, two floats out): the flag set when the second is
// 0. Else for the vertex's four cells inside the city and not in +0xA4: the
// first occupant (occupant manager slot 0x80 with the filter +0x364) asked
// for the network occupant interface; the flag set when that is of the
// class mask +0x360 (slot 0x24). An empty list from the list node allocator
// lives over the walk, as the game's (never filled).
void __fastcall IsVertexBlocked(char* self, void*, uint32_t vertex, float* height, uint8_t* blocked)
{
    static bool logged = false;
    FirstCall("IsVertexBlocked", logged);
    const uint32_t verticesX = At<uint32_t>(self, field::kVertexStride);
    const uint32_t z = vertex / verticesX, x = vertex % verticesX;
    *blocked = 0;
    *height = static_cast<float>(TerrainHeight(self, vertex));
    float rangeX = 0.0f, rangeZ = 0.0f;
    Virtual<uint8_t>(At<void*>(self, field::kCityObject4C), 0x68, x, z, &rangeX, &rangeZ);
    if (rangeZ == 0.0f) { // FUCOMPP, `test ah, 0x44; jp`: equal and ordered only
        *blocked = 1;
        return;
    }
    char* const head = Virtual<char*>(ListNodeAllocator(), 0xC, uint32_t{0xC});
    At<char*>(head, 0) = head;
    At<char*>(head, 4) = head;
    char* list = head;
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t cx = static_cast<uint32_t>(Global<int32_t>(addr::cSC4NetworkTool_CornerCellDx + i * 4)) + x;
        const uint32_t cz = static_cast<uint32_t>(Global<int32_t>(addr::cSC4NetworkTool_CornerCellDz + i * 4)) + z;
        if (cx >= At<uint32_t>(self, field::kCellsX) || cz >= At<uint32_t>(self, field::kCellsZ))
            continue;
        if (satisfier::Find(self + hfield::kSetA4, (cz << 16) + cx))
            continue;
        void* occupant = nullptr;
        if (Virtual<uint8_t>(At<void*>(self, field::kOccupants), occupants_slot::kGetFirstByCell, &occupant, cx, cz,
                             At<uint32_t>(self, field::kFilter))) {
            void* network = nullptr;
            Virtual<uint8_t>(occupant, 0x0, kIidNetworkOccupant, &network);
            if (Virtual<uint8_t>(network, 0x24, At<uint32_t>(self, field::kClassMask))) {
                *blocked = 1;
                if (network)
                    ReleaseOf(network);
                if (occupant)
                    ReleaseOf(occupant);
                CallThis<void>(addr::SC4List_Destroy2, &list);
                return;
            }
            if (network)
                ReleaseOf(network);
        }
        if (occupant)
            ReleaseOf(occupant);
    }
    CallThis<void>(addr::SC4List_Clear2, &list);
    Virtual<void>(ListNodeAllocator(), 0x10, static_cast<void*>(head));
}

// The terrain coordinate of a cell coordinate: float(n * scale).
inline float Scaled(int32_t n, double scale) { return static_cast<float>(static_cast<double>(n) * scale); }

// A first or last height: kept when above 0 (or a NaN), else from the
// vertices a and b (IsVertexBlocked at the game's address): the first one's
// terrain height when it is blocked, else the second's when that is, else
// their mean.
uint32_t ResolveHeight(char* self, uint32_t bits, uint32_t a, uint32_t b)
{
    const float given = satisfier::Float(bits);
    if (!(given <= 0.0f)) // `test ah, 0x41; jp`: above 0 or unordered keeps it
        return bits;
    float first = 0.0f, second = 0.0f;
    uint8_t blocked = 0;
    CallThis<void, uint32_t, float*, uint8_t*>(addr::cSC4NetworkTool_IsVertexBlocked, self, a, &first, &blocked);
    if (blocked)
        return satisfier::Bits(first);
    CallThis<void, uint32_t, float*, uint8_t*>(addr::cSC4NetworkTool_IsVertexBlocked, self, b, &second, &blocked);
    if (blocked)
        return satisfier::Bits(second);
    return satisfier::Bits(static_cast<float>((static_cast<double>(second) + static_cast<double>(first)) *
                                              static_cast<double>(Global<float>(addr::SC4_FloatHalf))));
}

// 0x00637780 (ret 0x14: the plane out, the previous portion's last step,
// the next one's first step, their heights (float bits); slot 77's): the
// two steps' first cells, the drag direction c (+0x7C when +0x74 is above 0,
// else +0x80) and e = (c + 4) % 8; four corners P1/P2 = cell1 + the
// direction's offsets (0x00AA8AE0 / 0x00AA8AA0), P3/P4 = cell2 + the
// opposite's; the heights resolved (P1, P2 for the first; P3, P4 for the
// second); for a single step (the two equal and not below +0x74) the plane
// (0, 1, 0, -first), else PlaneFromPoints through (P1, first), (P2, first),
// (P3, second).
void __fastcall ComputeSegmentPlane(char* self, void*, float* out, uint32_t prevLast, uint32_t nextFirst,
                                    uint32_t prevHeight, uint32_t nextHeight)
{
    static bool logged = false;
    FirstCall("ComputeSegmentPlane", logged);
    const uint32_t cell1 = At<uint32_t>(Steps(self) + prevLast * step::kSize, 0);
    const uint32_t cell2 = At<uint32_t>(Steps(self) + nextFirst * step::kSize, 0);
    const uint32_t c = At<int32_t>(self, hfield::kWord74) > 0 ? At<uint32_t>(self, hfield::kDirection7C)
                                                               : At<uint32_t>(self, hfield::kDirection80);
    const uint32_t e = static_cast<uint32_t>(static_cast<int32_t>(c + 4) % 8);
    const int32_t x1 = At<int32_t>(Cells(self) + cell1 * kPointSize, 0);
    const int32_t z1 = At<int32_t>(Cells(self) + cell1 * kPointSize, 4);
    const int32_t x2 = At<int32_t>(Cells(self) + cell2 * kPointSize, 0);
    const int32_t z2 = At<int32_t>(Cells(self) + cell2 * kPointSize, 4);
    auto word = [](std::uintptr_t base, uint32_t dir, uint32_t half) {
        return TableWord(base, dir * 8 + half * 4);
    };
    const int32_t p1x = word(addr::cSC4NetworkTool_PlaneFrontOffsets, c, 0) + x1;
    const int32_t p1z = word(addr::cSC4NetworkTool_PlaneFrontOffsets, c, 1) + z1;
    const int32_t p2x = word(addr::cSC4NetworkTool_PlaneSideOffsets, c, 0) + x1;
    const int32_t p2z = word(addr::cSC4NetworkTool_PlaneSideOffsets, c, 1) + z1;
    const int32_t p3x = word(addr::cSC4NetworkTool_PlaneFrontOffsets, e, 0) + x2;
    const int32_t p3z = word(addr::cSC4NetworkTool_PlaneFrontOffsets, e, 1) + z2;
    const int32_t p4x = word(addr::cSC4NetworkTool_PlaneSideOffsets, e, 0) + x2;
    const int32_t p4z = word(addr::cSC4NetworkTool_PlaneSideOffsets, e, 1) + z2;
    const double scale = At<float>(self, life::kTerrainScale);
    float a[3] = {Scaled(p1x, scale), 0.0f, Scaled(p1z, scale)};
    float b[3] = {Scaled(p2x, scale), 0.0f, Scaled(p2z, scale)};
    float d3[3] = {Scaled(p3x, scale), 0.0f, Scaled(p3z, scale)};
    const uint32_t vx = At<uint32_t>(self, field::kVertexStride);
    auto vertex = [vx](int32_t x, int32_t z) {
        return vx * static_cast<uint32_t>(z) + static_cast<uint32_t>(x);
    };
    prevHeight = ResolveHeight(self, prevHeight, vertex(p1x, p1z), vertex(p2x, p2z));
    // The second height: the same, with its value rounded once into both far
    // points (the game keeps it on the x87 stack).
    nextHeight = ResolveHeight(self, nextHeight, vertex(p3x, p3z), vertex(p4x, p4z));
    d3[1] = satisfier::Float(nextHeight);
    a[1] = satisfier::Float(prevHeight);
    b[1] = satisfier::Float(prevHeight);
    if (prevLast == nextFirst && static_cast<int32_t>(prevLast) >= At<int32_t>(self, hfield::kWord74)) {
        At<uint32_t>(out, 0xC) = prevHeight ^ 0x80000000u; // FCHS
        At<uint32_t>(out, 0x8) = 0;
        At<uint32_t>(out, 0x0) = 0;
        At<uint32_t>(out, 0x4) = kOneBits;
        return;
    }
    CallCdecl<void, float*, float*, float*, float*>(addr::SC4_PlaneFromPoints, a, b, d3, out);
}

// 0x00637B90 slot 77 ApplyPlaneConstraintToRegularCells (ret 4: the
// portions; FitHeights'): for each regular step (+0x54 word +8 is 0) of the
// valid ones, a plane through the portions around it (recomputed when the
// step passes the next portion's first step, or before the first): the
// latest portion ending before the step and the earliest starting after it
// (else the step itself with height 0 and the last valid step with height
// 0); a plane with b = 0 ends the whole function. Each cell of the step,
// each corner k: the plane's height h at the corner, then
//   lo = float(h - m) and hi = h + m (m = +0x2B0), the cell height H: lo =
//   H when H < lo, else hi = H when hi < H; the first pair's low raised to
//   lo, its high lowered to hi (stored as a float), an inverted pair
//   collapsed (hi = lo) when lo - hi <= 0.01; the second pair (min(h, H),
//   max(h, H)).
void __fastcall ApplyPlaneConstraintToRegularCells(char* self, void*, Vector* portions)
{
    static bool logged = false;
    FirstCall("ApplyPlaneConstraintToRegularCells", logged);
    int32_t n = At<int32_t>(self, field::kValidSteps);
    if (n <= 0)
        return;
    int32_t nextFirst = -1;
    uint8_t havePlane = 0;
    uint32_t prevHeight = 0, nextHeight = 0;
    float plane[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    double inverse = 0.0;
    for (int32_t i = 0; i < n; ++i, n = At<int32_t>(self, field::kValidSteps)) {
        char* const stepAt = Steps(self) + static_cast<uint32_t>(i) * step::kSize;
        if (At<uint32_t>(stepAt, step::kIrregular) != 0)
            continue;
        if (i > nextFirst || !havePlane) {
            int32_t prevLast = -1;
            nextFirst = 0x7FFFFFFF;
            for (char* r = portions->begin; r != portions->end; r += portion::kSize) {
                const int32_t last = At<int32_t>(r, portion::kLast);
                if (last < i && prevLast < last) {
                    prevLast = last;
                    prevHeight = At<uint32_t>(r, portion::kLastHeight);
                }
                const int32_t first = At<int32_t>(r, portion::kFirst);
                if (first > i && nextFirst > first) {
                    nextFirst = first;
                    nextHeight = At<uint32_t>(r, portion::kFirstHeight);
                }
            }
            if (prevLast == -1) {
                prevLast = i;
                prevHeight = 0;
            }
            if (nextFirst == 0x7FFFFFFF) {
                nextFirst = n - 1;
                nextHeight = 0;
            }
            CallThis<void, float*, int32_t, int32_t, uint32_t, uint32_t>(addr::cSC4NetworkTool_ComputeSegmentPlane,
                                                                        self, plane, prevLast, nextFirst, prevHeight,
                                                                        nextHeight);
            if (plane[1] == 0.0f) // FUCOMPP, `jnp`: equal and ordered
                return;
            havePlane = 1;
            inverse = static_cast<float>(static_cast<double>(Global<float>(addr::SC4_FloatMinusOne)) /
                                         static_cast<double>(plane[1]));
        }
        const double scale = At<float>(self, life::kTerrainScale);
        const double margin = At<float>(self, hfield::kMargin);
        for (uint32_t c = At<uint32_t>(Steps(self) + static_cast<uint32_t>(i) * step::kSize, 0);
             c <= At<uint32_t>(Steps(self) + static_cast<uint32_t>(i) * step::kSize, step::kLastCell); ++c) {
            char* const info = Entry(self, PointKey(Cells(self) + c * kPointSize));
            for (uint32_t k = 0; k < 4; ++k) {
                // FILD of the int, + 2^32 when negative: the unsigned value.
                const uint32_t ux =
                    static_cast<uint32_t>(Global<int32_t>(addr::cSC4NetworkTool_CornerVertexDx + k * 4)) +
                    Field<uint32_t>(info, cell::kX);
                const uint32_t uz =
                    static_cast<uint32_t>(Global<int32_t>(addr::cSC4NetworkTool_CornerVertexDz + k * 4)) +
                    Field<uint32_t>(info, cell::kZ);
                const double px = static_cast<double>(ux) * scale * static_cast<double>(plane[0]);
                const double pz = static_cast<double>(uz) * scale * static_cast<double>(plane[2]);
                const double h = (px + pz + static_cast<double>(plane[3])) * inverse;
                float lo = static_cast<float>(h - margin);
                double hi = h + margin;
                const float height = Field<float>(info, cell::kHeights + 4 * k);
                char* const pair = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(info) + cell::kRanges + 8 * k);
                // `test ah, 5; jp`: the jump is taken when not below or
                // unordered.
                if (height < lo)
                    lo = height;
                else if (hi < static_cast<double>(height))
                    hi = height;
                if (At<float>(pair, 0) < lo)
                    At<float>(pair, 0) = lo;
                if (hi < static_cast<double>(At<float>(pair, 4)))
                    At<float>(pair, 4) = static_cast<float>(hi);
                if (At<float>(pair, 4) < At<float>(pair, 0)) {
                    const double gap = static_cast<double>(At<float>(pair, 0)) - static_cast<double>(At<float>(pair, 4));
                    if (gap <= satisfier::Gap()) // `test ah, 0x41; jp`: below or equal, ordered
                        At<uint32_t>(pair, 4) = At<uint32_t>(pair, 0);
                }
                // `test ah, 0x41; jne`: h not above H (or unordered) gives
                // (h, H), else (H, h).
                if (!(h > static_cast<double>(height))) {
                    At<float>(pair, 0x20) = static_cast<float>(h);
                    At<float>(pair, 0x24) = height;
                } else {
                    At<float>(pair, 0x24) = static_cast<float>(h);
                    At<float>(pair, 0x20) = height;
                }
            }
        }
    }
}

// 0x00637A80 (ret 4: a radius; the guarded 0x0062CD50, 0x0063B830,
// 0x006588D0): the height pass. The solved count taken first; the
// intersections' cells added (0x006371E0), slot 75, the cells to fit
// collected (0x00636AB0), the solved cells cut back to the count (Resize
// with a zero record), the bridges' and +0xE4 records' portions and heights
// (0x0062FB90, 0x006274B0), slot 77 with them; the first satisfier reset
// and begun with (0, float(the terrain's slot 0x60 + 20)); every cell's
// constraints (0x00636910 with +0xA4), the intersections' (0x00644710);
// Solve; the portions freed. AL Solve's answer.
uint8_t __fastcall FitHeights(char* self, void*, int32_t radius)
{
    static bool logged = false;
    FirstCall("FitHeights", logged);
    const int32_t count = solve::SolvedCount(self);
    CallThis<void>(addr::cSC4NetworkTool_AddIntersectionCells, self);
    Virtual<void>(self, 0x12C);
    CallThis<void, int32_t>(addr::cSC4NetworkTool_CollectHeightCells, self, radius);
    const uint32_t zero[3] = {0, 0, 0};
    CallThis<void, uint32_t, const uint32_t*>(addr::SC4Vector12_Resize, self + field::kSolved,
                                              static_cast<uint32_t>(count), zero);
    Vector portions{nullptr, nullptr, nullptr};
    CallThis<void, Vector*>(addr::cSC4NetworkTool_SetBridgeCellHeights, self, &portions);
    CallThis<void, Vector*>(addr::cSC4NetworkTool_SetRecordCellHeights, self, &portions);
    Virtual<void>(self, 0x134, &portions);
    char* const sat = Sat1(self);
    CallThis<uint8_t>(addr::SC4VertexHeightSatisfier_Reset, sat);
    const float top = static_cast<float>(Virtual<double>(Terrain(self), 0x60) +
                                         Global<double>(addr::cSC4NetworkTool_HeightPassHeadroom));
    CallThis<uint8_t, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_Begin, sat, 0u, satisfier::Bits(top));
    CallThis<void, char*>(addr::cSC4NetworkTool_InsertCellConstraints, self, self + hfield::kSetA4);
    CallThis<void, char*>(addr::cSC4NetworkTool_InsertIntersectionConstraints, self, sat);
    const uint8_t answer = CallThis<uint8_t>(addr::SC4VertexHeightSatisfier_Solve, sat);
    solve::FreeIfSet(portions.begin);
    return answer;
}

// ====================================================================================
// Slots 56, 57 and 73
// ====================================================================================

// 0x006444F0 slot 73 GetNetworkVertexHt (ret 4: a vertex): the first
// satisfier's result, else the terrain's height (its ST0 as it is).
double __fastcall GetNetworkVertexHt(char* self, void*, uint32_t vertex)
{
    static bool logged = false;
    FirstCall("GetNetworkVertexHt", logged);
    uint32_t result = 0;
    if (GetResult(Sat1(self), vertex, &result))
        return satisfier::Float(result);
    return TerrainHeight(self, vertex);
}

// 0x00644530 slot 57 UpdateTerrainHeights (ret 4: a uint32 vector out): the
// vector emptied (the game's erase of [begin, end): its memmove branch is
// never taken); every (vertex, height) of the second satisfier's results
// whose height is more than 0.001 off the terrain's: the terrain's slot
// 0xC8 (vertex, height), the vertex appended, the rectangle of the changed
// vertices (x = v % per row, z = v / per column, as the game divides)
// grown by one (within 0 and the city's slots 0x174 / 0x178); when it is
// not empty the terrain's slots 0x15C, 0x110 (1, 1, the rectangle, 4) and
// 0x160.
void __fastcall UpdateTerrainHeights(char* self, void*, Vector* changed)
{
    static bool logged = false;
    FirstCall("UpdateTerrainHeights", logged);
    int32_t rect[4] = {At<int32_t>(self, field::kVertexStride), At<int32_t>(self, hfield::kVerticesZ), -1, -1};
    changed->end = changed->begin;
    char* const sat = Sat2(self);
    CallThis<void>(addr::SC4VertexHeightSatisfier_BeginResults, sat);
    uint32_t vertex = 0, height = 0;
    while (CallThis<uint8_t, uint32_t*, uint32_t*>(addr::SC4VertexHeightSatisfier_NextResult, sat, &vertex, &height)) {
        const double terrain = TerrainHeight(self, vertex);
        const double off = std::fabs(static_cast<double>(satisfier::Float(height)) - terrain);
        if (!(off > static_cast<double>(Global<float>(addr::FloraTuning_HeightEpsilon))))
            continue;
        Virtual<void>(Terrain(self), 0xC8, vertex, height);
        uint32_t copy = vertex;
        if (changed->end != changed->capacity) {
            if (changed->end)
                At<uint32_t>(changed->end, 0) = vertex;
            changed->end += 4;
        } else {
            CallThis<void, char*, uint32_t*, uint32_t*, uint32_t, uint32_t>(addr::SC4PtrVector_InsertN, changed,
                                                                            changed->end, &copy, &vertex, 1u, 1u);
        }
        const int32_t x = static_cast<int32_t>(vertex % At<uint32_t>(self, field::kVertexStride));
        const int32_t z = static_cast<int32_t>(vertex / At<uint32_t>(self, hfield::kVerticesZ));
        if (x < rect[0])
            rect[0] = x;
        if (x > rect[2])
            rect[2] = x;
        if (z < rect[1])
            rect[1] = z;
        if (z > rect[3])
            rect[3] = z;
    }
    if (rect[1] != 0)
        --rect[1];
    if (rect[0] != 0)
        --rect[0];
    void* const city = Global<void*>(addr::SC4_City);
    if (rect[2] < Virtual<int32_t>(city, 0x174))
        ++rect[2];
    if (rect[3] < Virtual<int32_t>(city, 0x178))
        ++rect[3];
    if (rect[2] >= rect[0] && rect[3] >= rect[1]) {
        Virtual<void>(Terrain(self), 0x15C);
        Virtual<void>(Terrain(self), 0x110, 1u, 1u, rect, 4u);
        Virtual<void>(Terrain(self), 0x160);
    }
}

// 0x00644B20 slot 56 CanTilesBeSupportedOnTerrain (ret 4: a network type):
// the second satisfier reset and begun with (-FLT_MAX, FLT_MAX). For every
// cell of +0xA4 (from SC4HashTable::Begin) whose info is not a network lot,
// each corner vertex v: H = the first satisfier's result (else the cell's
// height), t = float(the terrain's height); v added at t unless it has an
// entry; fixed at t when it is in +0xB8 or the cell is immovable and from
// an occupant; else lo = float(t - row +0x60), hi = float(t + row +0x5C)
// (hi = H when the unrounded sum is above H); with a piece record for the
// cell's piece (its solved piece, else its occupant's slot 0x5C; none
// without either) lo raised to float(H - row +0x8C) and AL 0 when lo then
// lies above hi; without one, AL 0 when the cell is movable and H lies
// above hi, else (movable) lo = hi = H; then v fixed at hi when lo == hi,
// else restricted to (lo, hi) with the preferred height t (immovable) or H.
// Then the intersections' constraints (0x00644710), every vertex of +0xB8
// without an entry added and fixed at float(the terrain's height), Solve:
// AL its answer.
uint8_t __fastcall CanTilesBeSupportedOnTerrain(char* self, void*, uint32_t type)
{
    static bool logged = false;
    FirstCall("CanTilesBeSupportedOnTerrain", logged);
    char* const row = solve::TypeRow(type);
    char* const sat = Sat2(self);
    CallThis<uint8_t>(addr::SC4VertexHeightSatisfier_Reset, sat);
    CallThis<uint8_t, uint32_t, uint32_t>(addr::SC4VertexHeightSatisfier_Begin, sat, cell::kLowestBits,
                                          cell::kHighestBits);
    char* const cellSet = self + hfield::kSetA4;
    for (char* node = solve::Begin(cellSet); node; node = solve::NextNode(node, cellSet)) {
        char* const info = Entry(self, At<uint32_t>(node, rul::table::kKey));
        if (!info || At<uint8_t>(info, cell::kNetworkLot))
            continue;
        for (uint32_t k = 0; k < 4; ++k) {
            const uint32_t v = At<uint32_t>(info, cell::kVertices + 4 * k);
            uint32_t settled = 0;
            if (!GetResult(Sat1(self), v, &settled))
                settled = At<uint32_t>(info, cell::kHeights + 4 * k);
            const float terrain = static_cast<float>(TerrainHeight(self, v));
            const uint32_t t = satisfier::Bits(terrain);
            if (!satisfier::Find(sat + satisfier::sat::kEntries, v))
                AddVertex(sat, v, t);
            if (satisfier::Find(self + hfield::kSetB8, v) ||
                (At<uint8_t>(info, cell::kImmovable) && At<uint8_t>(info, cell::kOccupied))) {
                AddFixed(sat, v, t);
                continue;
            }
            const double H = satisfier::Float(settled);
            float lo = static_cast<float>(static_cast<double>(terrain) - static_cast<double>(At<float>(row, hrow::kBelow)));
            const double sum = static_cast<double>(terrain) + static_cast<double>(At<float>(row, hrow::kAbove));
            float hi = static_cast<float>(sum);
            if (sum > H) // `test ah, 0x41; jne`: only above, ordered
                hi = satisfier::Float(settled);
            uint32_t piece = 0;
            bool havePiece = true;
            const int32_t index = At<int32_t>(info, cell::kSolvedIndex);
            if (index >= 0) {
                piece = At<uint32_t>(At<Vector>(self, field::kSolved).begin + static_cast<uint32_t>(index) * 12, 0);
            } else if (void* const occupant = At<void*>(info, cell::kOccupant)) {
                piece = Virtual<uint32_t>(occupant, 0x5C);
            } else {
                havePiece = false;
            }
            const bool record =
                havePiece && CallThis<char*, uint32_t>(addr::cSC4NetworkTool_FindPieceRecord, self, piece) != nullptr;
            if (record) {
                const double drop = H - static_cast<double>(At<float>(row, hrow::kPieceDrop));
                if (static_cast<double>(lo) < drop) // `test ah, 5; jp`: below and ordered stores
                    lo = static_cast<float>(drop);
                if (lo > hi) // `test ah, 0x41; jne`: only above, ordered, gives AL 0
                    return 0;
            } else if (!At<uint8_t>(info, cell::kImmovable)) {
                if (H > static_cast<double>(hi)) // `test ah, 0x41; je`: above, ordered
                    return 0;
                hi = satisfier::Float(settled);
                lo = satisfier::Float(settled);
            }
            if (hi == lo) { // FUCOMPP, `test ah, 0x44; jp`: equal and ordered
                AddFixed(sat, v, satisfier::Bits(hi));
                continue;
            }
            Restrict(sat, v, satisfier::Bits(lo), satisfier::Bits(hi));
            PreferHeight(sat, v, At<uint8_t>(info, cell::kImmovable) ? t : settled);
        }
    }
    CallThis<void, char*>(addr::cSC4NetworkTool_InsertIntersectionConstraints, self, sat);
    char* const border = self + hfield::kSetB8;
    for (char* node = FirstNode(border); node; node = solve::NextNode(node, border)) {
        const uint32_t v = At<uint32_t>(node, rul::table::kKey);
        if (satisfier::Find(sat + satisfier::sat::kEntries, v))
            continue;
        const uint32_t t = satisfier::Bits(static_cast<float>(TerrainHeight(self, v)));
        AddVertex(sat, v, t);
        AddFixed(sat, v, t);
    }
    return CallThis<uint8_t>(addr::SC4VertexHeightSatisfier_Solve, sat);
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kHeights[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_FitHeights, FitHeights),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_AddIntersectionCells, AddIntersectionCells),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_CollectHeightCells, CollectHeightCells),
    OPENSC4_DETOUR(addr::SC4Vector12_Resize, ResizeVector12),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_SetBridgeCellHeights, SetBridgeCellHeights),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_SetRecordCellHeights, SetRecordCellHeights),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_InsertCellConstraints, InsertCellConstraints),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_InsertIntersectionConstraints, InsertIntersectionConstraints),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_ApplyPlaneConstraintToRegularCells, ApplyPlaneConstraintToRegularCells),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_ComputeSegmentPlane, ComputeSegmentPlane),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_IsVertexBlocked, IsVertexBlocked),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_CanTilesBeSupportedOnTerrain, CanTilesBeSupportedOnTerrain),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_UpdateTerrainHeights, UpdateTerrainHeights),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_GetNetworkVertexHt, GetNetworkVertexHt),
};

#undef OPENSC4_DETOUR

bool InstallHeights(registry::Mode) { return InstallDetours(kHeights, "heights"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.heights", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_FitHeights,
                        "Our network tool's height pass (FitHeights and its helpers: the intersections' cells, the "
                        "cells to fit, the bridges' and records' heights, every cell's and intersection's "
                        "constraints), slot 77's plane fit with its plane and vertex test, slot 56 "
                        "CanTilesBeSupportedOnTerrain, slot 57 UpdateTerrainHeights and slot 73 GetNetworkVertexHt "
                        "(14 detours; src/core/nettool_heights_1.cpp)",
                        false, InstallHeights}),
};

} // namespace
} // namespace opensc4::nettool_sim
