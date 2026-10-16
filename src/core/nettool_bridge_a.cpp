// Our cSC4NetworkTool, part t10 (build 641; the class map and the fields are
// in src/core/nettool_sim.h, the bridge layouts in nettool_bridge.h): the
// major bridges' cells and occupants. When a drag's solution has major
// bridges (the tool's +0xD8; part t11 finds and picks them), the code under
// the segment placer 0x0063B830 builds each bridge's cells
// (BuildMajorBridgeCells 0x00631300: a ramp piece at each end, pier pieces
// at the record's spacing, span pieces between, each with its piece, turn
// and heights), and PlaceAllSegments' placing pass makes a bridge occupant
// (class 0x49CC1BCD) for every cell (PlaceMajorBridgeOccupants 0x0062F610
// and PlaceBridgeCellOccupant 0x0062EEB0: the occupant through slot 54,
// its pre-built model set up with the deck offset, its piece, flags and
// passage ceiling, the cell cleared, the occupant into the occupant manager
// and the placement's list). Two groups, each a list of detours at the
// game's addresses:
//   simulators.nettool.bridges: PlaceMajorBridgeOccupants 0x0062F610,
//     PlaceBridgeCellOccupant 0x0062EEB0, BuildMajorBridgeCells 0x00631300
//     and the bridge cell vector's PushBack 0x006312C0, InsertOverflow
//     0x006310F0 and UninitializedCopy 0x0062F6B0 (6 detours);
//   simulators.nettool.bridgesort: the float range template code the cable
//     geometry (0x006307A0, the game's) sorts with and the occupant code's
//     lowest corner: MinElement 0x0062DB90, CopyBackward 0x0062DC30, the
//     heap's PushHeap 0x0062DBD0, AdjustHeap 0x0062EA20, MakeHeap
//     0x0062EC70, SortHeap 0x0062EE50, the sort's UnguardedPartition
//     0x0062E9E0, LinearInsert 0x0062EDF0, PartialSort 0x0062F6E0 and
//     IntrosortLoop 0x006306D0 (10 detours).
// The split: what builds and places bridges, and plain template code; each
// can be tried and bisected alone.
//
// What these are (the survey only inferred "bridges"): checked against
// the code. The 0x28-byte elements at +0xD8 are the major bridges (steps,
// record, deck altitude, cells); the 0x50-byte cells are the bridge's
// pieces; the type row's 0x178-byte records are the bridge kinds
// (nettool_bridge.h lists the fields this part reads). PlaceBridgeCell-
// Occupant's occupant is a bridge occupant (slot 54 with the kind
// 0x49CC1BCD, src/core/bridgeocc_sim.cpp), set up as the pre-built model
// occupant it derives from (SetUpPrebuiltOccupant 0x00624860).
//
// Every function was written from the disassembly. Calls between them go to
// the game's addresses (through the address entries; the tool's slots and
// the occupant's through their vtables, as the game calls them), so each
// detour stands alone and OPENSC4_NETTOOL_SKIP bisects them.
//
// Left as the game's (model geometry, not pure drawing): every function of
// the part that computes the vertices, UVs and index triples of model parts
// or calls the network occupant's AddPart (slot 31, 0x0061C800), and the
// functions whose only output feeds that geometry. AddPart appends 0x4C-byte
// records (model triangles) to the occupant's +0x118 vector and recomputes
// its box +0x124..+0x138 (slot 0x140, 0x0061DB50, for pre-built and bridge
// occupants); the network occupant's Write 0x0061A190 saves both, and the
// occupant manager places the occupant by that box (PlaceBridgeCellOccupant
// inserts it after its AddPart calls). So these build saved state. Corpus:
// all 48 bridge pieces in saves-networks Big City n0000 / n0090 carry wall
// records (keys 0xAFAFEA00 / 0xAFAFEE00); plain occupants carry the support
// quads of 0x643FB0 (keys 0x00BB0200 / 0x00BB0000). They stay the game's
// because they are model geometry (the AddPart family is fenced for the
// Renderer thread, docs/netocc.md) and 0x0062E690 uses FLDL2E, FRNDINT,
// F2XM1 and FSCALE (F2XM1's result is implementation-specific, not
// reproducible in C++). Ours call them at their addresses with the game's
// arguments, so parity holds. PlaceBridgeCellOccupant's own pier quads and
// cable triangles (ours) are saved state too, which is why they were checked
// bit for bit. The eleven:
//   - 0x0062DF30 AddBridgeCellWalls (the side walls of an arch or
//     suspension cell: corner positions, UVs 0.5 and 1.0, through
//     0x0062DCB0) with its helpers 0x0062DCB0 (four AddPart calls with the
//     index tables 0x00AA8900 to 0x00AA8924; it takes its occupant, vertex
//     and UV buffers in EDI, ESI and EBX besides four stack words) and
//     0x0062DB30 (a vertex's x and z from its index);
//   - 0x0062E690 RaiseWallsAlongCurve (the arch / suspension curve, an x87
//     exp() through FLDL2E, FRNDINT, F2XM1 and FSCALE: it raises the cells'
//     +0x2C to +0x38, which only 0x0062DF30 reads; part t11's 0x00632060
//     calls it too);
//   - 0x00630F70 AddCableParts and 0x006307A0 AddCableSegmentParts (the
//     cables split into per-cell 32-byte parts in the cells' +0x44 vectors,
//     which only PlaceBridgeCellOccupant's AddPart loop reads);
//   - 0x00643FB0 SetUpPieceSupports (slot 72's support walls of elevated
//     pieces) with 0x00643830, 0x006439C0, 0x00643B70 and 0x00643E10 (each
//     a set of AddPart quads: texture keys 0x00BB0200, 0x00BB0000,
//     0x08080000, 0x0912220E).
// BuildMajorBridgeCells and PlaceBridgeCellOccupant decide what is built
// (pieces, turns, heights, which occupants) and are ours; where they hand
// on to the geometry they call the functions above at their addresses with
// the same arguments, and PlaceBridgeCellOccupant's own inline AddPart loop
// (four triangles per cable part) and its pier wall quads are kept as the
// game computes them (their floats are plain copies, sums and products).
// Under the part, at their addresses: slot 54 CreateNetworkOccupant
// (through the vtable), SetUpPrebuiltOccupant 0x00624860, GetPieceExemplar
// 0x006244B0, ClearCellForOccupant 0x006272F0 (part t7), the cell entry
// 0x00633220 (t2), the bridge cell's copy constructor 0x006267D0 and the
// vector's Free 0x0062BF20 (part t11's), SC4::stl_allocate.
//
// NAM. None of these is guarded, none contains a NAM or sc4fix patch site
// (the NAM table of docs/nettool.md and every address entry checked for
// sites in 0x0062DB30 to 0x00632051 and 0x00643830 to 0x006444E8), none is
// called by NAM's DLL, and none calls a guarded function. NAM reads the
// tool and the occupants by layout: the bridges, their cells and records
// stay in the game's memory with the game's layout, the cell vectors grow
// through the game's allocator and the cells' copy constructor, nothing is
// cached across a call (the vectors' ends are read where the game reads
// them).
//
// Registers. The scan of every game caller after each call (both
// branches): MinElement 0x0062EFE5 (EAX, the pointer, read), PushHeap
// 0x0062EA78, CopyBackward 0x0062EE13, UnguardedPartition 0x0063075C (EAX
// read: the cut), AdjustHeap 0x0062EC9C, 0x0062ECB2, 0x0062EE90,
// 0x0062F734, MakeHeap 0x0062F6F7, LinearInsert 0x00630BC1, 0x00630C16,
// SortHeap 0x0062F74A, PartialSort 0x00630790, IntrosortLoop 0x0063076D,
// 0x00630B9C, PlaceBridgeCellOccupant 0x0062F678, PlaceMajorBridgeOccupants
// 0x0063BCB3 (ours, part t6), UninitializedCopy 0x0062C2CF, 0x006311CA (EAX
// read: the end), InsertOverflow 0x006312F2, 0x006321A7, PushBack
// 0x00631588, BuildMajorBridgeCells 0x006327C6, 0x00632A94 (part t11's).
// Where a caller returns right after the call the scan followed its own
// callers (the heap and sort chain ends in 0x006307A0, which writes ECX and
// EDX first). None reads ECX or EDX before writing it; EAX is read only
// where noted, and ours return the same pointer. So all 16 are plain
// detours. LinearInsert's EAX on its second path is the x87 status word
// (FNSTSW AX) in the game: no caller reads it; ours returns nothing.
//
// Wrappers: no file of src/core/game_*.cpp, *fix*.cpp, sim_timers.cpp,
// fixed_seed.* or crew_*.cpp detours or tests return addresses in
// 0x0062DB30 to 0x00632051 or 0x00643830 to 0x006444E8 (the crew's caller
// range is 0x00605490 to 0x00608C50; crew_life.cpp names 0x0062BC6C,
// 0x0062D3FA and 0x0063BD15, none here). The bridge occupants are made by
// slot 54 and their constructors at their own addresses (fixed_seed keys
// generator seeds by those). Nothing needed a mark.
//
// Floats. The game runs the x87 at 53 bits, so intermediates are doubles
// where every operation is +, -, *, / or a compare and every float load or
// store rounds as `float`; the order and association are the game's. None
// keeps a value on the x87 stack across a call (checked per call: the
// stack is empty at every call of PlaceBridgeCellOccupant and
// BuildMajorBridgeCells). The values:
//   - PlaceBridgeCellOccupant: the deck offset float(deck - y); the passage
//     ceiling float(lowest corner - record +0xD0); the pier top float(y -
//     depth); the lowest terrain altitude of the cell's four vertices (the
//     terrain's ST0, a double, compared unrounded with the running float,
//     stored rounded: `fcom` + `test ah, 5`: lower only when ordered); the
//     four wall corners float(x + offset), float(z + offset) (each stored
//     twice from one unrounded sum); the mean (((l3 + l2) + l1) + l0) *
//     0.25 compared with the water level (ST0 of terrain slot 0x78) and,
//     under water, the water line w - 0.01 (the double 0x00A92688): the
//     first three corners take float(w - 0.01) when it is below their top
//     (compared rounded, as the game reloads it), the fourth when the
//     UNROUNDED w - 0.01 is below (the game compares the stack value); the
//     tops' mean compared with the water level (`test ah, 0x41`: above
//     only when ordered); the cable quads float(y +- half height) (y first
//     for y - h, h first for h + y; a sum is the same either way). The pier
//     depth test `fcomp 0.0` + `test ah, 1`: below or a NaN skips.
//   - BuildMajorBridgeCells: the pier count fit n / ((f + f) + q) * f with
//     n and q integers (FILD, FIADD: exact), then the game's round by the
//     magic constant: float((x - 0.5) + 12582912.0) whose bits less
//     0x4B400000 are k, and k + 1 when x equals k + 1 exactly (FUCOMPP:
//     equal only when ordered); the deck float(alt + d4) and, for a pier,
//     float(unrounded(alt + d4) + c4) (the game stores the first and adds
//     to the stack value); the ramp's float((alt - drop) + d4) and corner
//     heights float(alt - r60[i]) (each stored twice); the cable points:
//     float((S + p) + 8.0), float(p + alt), float(p + 8.0) and float(t +
//     S) with S an integer times 16 (FILD), t a point stored as a float
//     before, in the game's order per call (the comments at each).
//   - the sort and heap code: float compares only (`test ah, 5` with JP /
//     JNP: less only when ordered; `test ah, 0x41` with JNE: greater only
//     when ordered), each written so that a NaN takes the game's branch.
//     Where the game moves a value through FLD / FSTP (PushHeap's value,
//     the partition's swap, SortHeap's last element) the bits are the same
//     unless the value is a signalling NaN, which the x87 would quieten;
//     ours copies the bits (as part t9's SetBridgeCellHeights does).
//
// Stack leftovers the game stores (ours are 0):
//   - BuildMajorBridgeCells' cell template: each cell is appended as a copy
//     of a 0x50-byte frame slot (+0x134) whose part vector (+0x44 to +0x4C)
//     alone is cleared; the game then writes the copy's kind, x, z, piece,
//     turn, the four heights, the deck, the pier depth and the flag byte
//     +0x40 on every path, but the four floats +0x2C to +0x38 and the float
//     +0x3C only when it sets flags (both pier markers unset, or the
//     record's +0xB0 or +0xB4 zero, write +0x40 = 0 and leave them). So a
//     cell with +0x40 = 0 carries the frame's leftovers there (and through
//     the copy constructor into every copy). Readers: 0x0062DF30 (the wall
//     geometry), reached only when +0x40 is not 0 (0x0062F382); 0x0062E690
//     (raises them), reached only when the record's +0xB0 and +0xB4 are set
//     and a pier was placed, in which case every cell got flags (the pier
//     marker is set from the first cell on); the copy constructor 0x006267D0
//     and the assignment 0x00626750 copy them. Verdict: never read as a
//     value. The bytes +0x11 to +0x13 and +0x41 to +0x43 are not copied by
//     the copy constructor (byte moves), so the template's leftovers do not
//     reach the vector there.
//   - BuildMajorBridgeCells keeps its ramp index (EDI) across cells: only
//     the ramp paths read it, right after they set it. Not a leftover.
//   - PlaceBridgeCellOccupant: slot 54's box out (+0xD4) is written whole by
//     slot 54; the occupant's position (GetPosition) whole; the pier quads
//     and the cable buffers are written before each use. None.
// Other stores into the game's stack, never read again as what they were:
// SortHeap keeps the value in its `last` argument slot, PartialSort in its
// tag's, IntrosortLoop the depth in its own; LinearInsert and PushBack hand
// an argument slot's address as an unread tag (ours a local's).
//
// Calls, each count from the asm's pushes for that call and the callee's own
// `ret N`:
//   - the part's own, at the game's addresses: PushHeap cdecl five (reads
//     four), AdjustHeap cdecl five, MakeHeap cdecl three (PartialSort
//     pushes five), SortHeap cdecl three, CopyBackward cdecl three
//     (LinearInsert pushes five), UnguardedPartition cdecl four (reads
//     three), PartialSort cdecl five, IntrosortLoop cdecl five, MinElement
//     cdecl two, UninitializedCopy cdecl four (reads three), InsertOverflow
//     five (ret 0x14), PushBack one (ret 4), PlaceBridgeCellOccupant four
//     (ret 0x10);
//   - left the game's (model geometry): AddBridgeCellWalls 0x0062DF30 four (ret
//     0x10), RaiseWallsAlongCurve 0x0062E690 cdecl five, AddCableParts
//     0x00630F70 cdecl ten, AddPierWalls 0x00643B70 cdecl four;
//   - the tool's: slot 54 (0xD8) four, SetUpPrebuiltOccupant 0x00624860 five
//     (ret 0x14), GetPieceExemplar 0x006244B0 two (ret 8; EAX),
//     ClearCellForOccupant 0x006272F0 three (ret 0xC), the cell entry
//     0x00633220 one (ret 4; EAX);
//   - part t11's: SC4BridgeCell::CopyConstruct 0x006267D0 one (ret 4),
//     SC4BridgeCellVector::Free 0x0062BF20 none;
//   - the network occupant (netocc_sim.h): slot 0x78 two (the piece and its
//     turn), ClearNetworkFlag (0x2C) / SetNetworkFlag (0x28) one,
//     GetNetworkFlag (0x30) none, SetImmovable (0x48) one, AddPart (0x7C)
//     four, AsOccupant (0xC0) none, SetPassageCeiling (0x110) one (a
//     float), Release (0x8); the occupant part: AddRef (0x4), GetPosition
//     (0x20) one; the exemplar's slot 0x1C none and its answer's slot 0x24
//     one (EAX tested whole), the exemplar's Release;
//   - the terrain (+0x44): slot 0x30 one (ST0), 0x78 two floats (ST0, the
//     water level); the occupant manager (+0x48) slot 0x50 two (AL);
//     SC4::stl_allocate cdecl one.
#include "core/nettool_bridge.h"
#include "core/nettool_occupants.h"
#include "core/nettool_sim.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <bit>
#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim {
namespace {

using occupants::ListNode;
namespace bcl = bridges::cell;
namespace bmj = bridges::major;
namespace bpt = bridges::part;
namespace brec = bridges::record;
namespace occ = occupants;

// ====================================================================================
// Helpers
// ====================================================================================

inline float F(const void* base, std::size_t offset)
{
    return *reinterpret_cast<const float*>(reinterpret_cast<std::uintptr_t>(base) + offset);
}
inline double D(const void* base, std::size_t offset) { return static_cast<double>(F(base, offset)); }
inline float Fb(uint32_t bits) { return std::bit_cast<float>(bits); }
inline uint32_t U(const void* base, std::size_t offset)
{
    return *reinterpret_cast<const uint32_t*>(reinterpret_cast<std::uintptr_t>(base) + offset);
}
// A field at a computed address (the game writes through the cell entry's
// answer without a test; so do ours).
template <typename T>
inline T& Put(void* base, std::size_t offset)
{
    return *reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(base) + offset);
}
inline int32_t Bytes(const void* first, const void* last)
{
    return static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(last) - reinterpret_cast<std::uintptr_t>(first));
}

// The game's float constants this part reads.
inline float Quarter() { return Global<float>(addr::cSC4NetworkOccupant_PositionScale); } // 0.25
inline float Eight() { return Global<float>(addr::SC4_FloatEight); }                      // 8.0
inline float Half() { return Global<float>(addr::SC4_FloatHalf); }                        // 0.5
inline float Zero() { return Global<float>(addr::SC4_FloatZero); }                        // 0.0
inline float RoundMagic() { return Global<float>(addr::cSC4NetworkTool_BridgeRoundMagic); } // 12582912.0
inline double WaterMargin() { return Global<double>(addr::cSC4NetworkTool_BridgeWaterMargin); } // 0.01
inline int32_t CornerByTurn(uint32_t index)
{
    return *reinterpret_cast<const int32_t*>(static_cast<uint32_t>(addr::cSC4NetworkTool_BridgeCornerByTurn + index * 4));
}

// The game's signed remainder by 4 (`and 0x80000003`, then the negative
// fix-up), C's %.
inline int32_t Mod4(int32_t value) { return value % 4; }

inline void CopyConstruct(char* to, const char* from)
{
    CallThis<char*, const char*>(addr::SC4BridgeCell_CopyConstruct, to, from);
}

// ====================================================================================
// The float sort and heap (simulators.nettool.bridgesort)
// ====================================================================================
// STLport's std::sort, heap and min_element on floats, as the cable code
// 0x006307A0 and PlaceBridgeCellOccupant use them. The values are handled
// as their bits (uint32) and compared as floats.

// 0x0062DB90 (cdecl: first, last): the first element no later one is below
// (`fcomp` + `test ah, 5` + JP: a later one replaces it only when below and
// ordered); EAX it, first for an empty range.
uint32_t* __cdecl MinElement(uint32_t* first, uint32_t* last)
{
    static bool logged;
    FirstCall("MinElement", logged);
    if (first == last)
        return first;
    uint32_t* best = first;
    for (uint32_t* p = first + 1; p != last; ++p)
        if (Fb(*p) < Fb(*best))
            best = p;
    return best;
}

// 0x0062DBD0 (cdecl: first, hole, top, value, an unread tag): the value
// moved up from the hole while its parent (hole - 1) / 2 (signed) is below
// it (`fcom` + `test ah, 0x41` + JNE: only when ordered and above) and the
// hole is above top (signed), then stored (the game's FSTP of its FLD).
void __cdecl PushHeap(uint32_t* first, int32_t hole, int32_t top, uint32_t value, uint32_t /*tag*/)
{
    static bool logged;
    FirstCall("PushHeap", logged);
    const float v = Fb(value);
    int32_t parent = (hole - 1) / 2;
    if (hole > top) {
        for (;;) {
            if (!(v > Fb(first[parent])))
                break;
            first[hole] = first[parent];
            hole = parent;
            parent = (parent - 1) / 2;
            if (!(hole > top))
                break;
        }
    }
    first[hole] = value;
}

// 0x0062DC30 (cdecl: first, last, the destination's end): the words copied
// backwards (the count (last - first) >> 2, signed; nothing when not above
// 0); EAX the destination's new begin.
uint32_t* __cdecl CopyBackward(uint32_t* first, uint32_t* last, uint32_t* destination)
{
    static bool logged;
    FirstCall("CopyBackward", logged);
    for (int32_t count = Bytes(first, last) >> 2; count > 0; --count)
        *--destination = *--last;
    return destination;
}

// 0x0062E9E0 (cdecl: first, last, the pivot, an unread tag): Hoare's
// partition without bounds: first moves on while its element is below the
// pivot, last moves back while the pivot is below its element (both only
// when ordered); when first is not below last (unsigned) it is the answer
// (EAX), else the two are swapped.
uint32_t* __cdecl UnguardedPartition(uint32_t* first, uint32_t* last, uint32_t pivot, uint32_t /*tag*/)
{
    static bool logged;
    FirstCall("UnguardedPartition", logged);
    const float p = Fb(pivot);
    for (;;) {
        while (Fb(*first) < p)
            ++first;
        do
            --last;
        while (p < Fb(*last));
        if (!(first < last))
            return first;
        const uint32_t kept = *first; // the game's FLD / FSTP
        *first = *last;
        *last = kept;
        ++first;
    }
}

// 0x0062EA20 (cdecl: first, hole, len, value, tag): the hole moved down to
// the larger child (the right one 2h + 2 unless it is below the left one,
// only when ordered) while that child is inside (signed), a last left
// child taken when the child index equals len, then PushHeap at its
// address with the old hole as top.
void __cdecl AdjustHeap(uint32_t* first, int32_t hole, int32_t len, uint32_t value, uint32_t tag)
{
    static bool logged;
    FirstCall("AdjustHeap", logged);
    const int32_t top = hole;
    int32_t child = 2 * hole + 2;
    while (child < len) {
        if (Fb(first[child]) < Fb(first[child - 1]))
            --child;
        first[hole] = first[child];
        hole = child;
        child = 2 * child + 2;
    }
    if (child == len) {
        first[hole] = first[child - 1];
        hole = child - 1;
    }
    CallCdecl<void, uint32_t*, int32_t, int32_t, uint32_t, uint32_t>(addr::SC4FloatHeap_PushHeap, first, hole, top,
                                                                      value, tag);
}

// 0x0062EC70 (cdecl: first, last, tag): nothing below two elements; else
// AdjustHeap at its address for every parent from (len - 2) / 2 down to 0.
void __cdecl MakeHeap(uint32_t* first, uint32_t* last, uint32_t tag)
{
    static bool logged;
    FirstCall("MakeHeap", logged);
    const int32_t len = Bytes(first, last) >> 2;
    if (len < 2)
        return;
    int32_t parent = (len - 2) / 2;
    CallCdecl<void, uint32_t*, int32_t, int32_t, uint32_t, uint32_t>(addr::SC4FloatHeap_AdjustHeap, first, parent, len,
                                                                      first[parent], tag);
    while (parent != 0) {
        const uint32_t value = first[parent - 1];
        --parent;
        CallCdecl<void, uint32_t*, int32_t, int32_t, uint32_t, uint32_t>(addr::SC4FloatHeap_AdjustHeap, first, parent,
                                                                          len, value, tag);
    }
}

// 0x0062EDF0 (cdecl: first, last, value, an unread tag): a value below the
// first element (only when ordered): the range moved up one through
// CopyBackward at its address (the game pushes five words: first, last,
// last + 1, its value slot's address, 0) and the value stored first; else
// the value moved down from last while it is below the element before.
void __cdecl LinearInsert(uint32_t* first, uint32_t* last, uint32_t value, uint32_t /*tag*/)
{
    static bool logged;
    FirstCall("LinearInsert", logged);
    if (Fb(value) < Fb(*first)) {
        uint32_t slot = value;
        CallCdecl<uint32_t*, uint32_t*, uint32_t*, uint32_t*, uint32_t*, uint32_t>(addr::SC4DwordRange_CopyBackward,
                                                                                   first, last, last + 1, &slot, 0u);
        *first = value;
        return;
    }
    uint32_t* hole = last;
    uint32_t* previous = last - 1;
    while (Fb(value) < Fb(*previous)) {
        *hole = *previous;
        hole = previous;
        --previous;
    }
    *hole = value;
}

// 0x0062EE50 (cdecl: first, last, tag): while more than one element is
// left ((bytes & ~3) above 4, signed), the last one taken out (the game's
// FLD / FSTP), the first moved there, AdjustHeap at its address on the
// rest with the taken value.
void __cdecl SortHeap(uint32_t* first, uint32_t* last, uint32_t tag)
{
    static bool logged;
    FirstCall("SortHeap", logged);
    int32_t bytes = Bytes(first, last);
    char* const base = reinterpret_cast<char*>(first);
    while ((bytes & ~3) > 4) {
        const uint32_t value = *reinterpret_cast<uint32_t*>(base + bytes - 4);
        *reinterpret_cast<uint32_t*>(base + bytes - 4) = *first;
        bytes -= 4;
        CallCdecl<void, uint32_t*, int32_t, int32_t, uint32_t, uint32_t>(addr::SC4FloatHeap_AdjustHeap, first, 0,
                                                                          bytes >> 2, value, tag);
    }
}

// 0x0062F6E0 (cdecl: first, middle, last, an unread word, tag): MakeHeap
// on [first, middle) (the game pushes five words: first, middle, tag, 0,
// 0), every later element below the heap's top (only when ordered) swapped
// in through AdjustHeap, then SortHeap on [first, middle); all at their
// addresses.
void __cdecl PartialSort(uint32_t* first, uint32_t* middle, uint32_t* last, uint32_t /*unread*/, uint32_t tag)
{
    static bool logged;
    FirstCall("PartialSort", logged);
    CallCdecl<void, uint32_t*, uint32_t*, uint32_t, uint32_t, uint32_t>(addr::SC4FloatHeap_MakeHeap, first, middle,
                                                                         tag, 0u, 0u);
    for (uint32_t* i = middle; i < last; ++i) {
        if (Fb(*i) < Fb(*first)) {
            const uint32_t value = *i;
            *i = *first;
            CallCdecl<void, uint32_t*, int32_t, int32_t, uint32_t, uint32_t>(addr::SC4FloatHeap_AdjustHeap, first, 0,
                                                                              Bytes(first, middle) >> 2, value, tag);
        }
    }
    CallCdecl<void, uint32_t*, uint32_t*, uint32_t>(addr::SC4FloatHeap_SortHeap, first, middle, tag);
}

// 0x006306D0 (cdecl: first, last, an unread word, the depth, tag): while
// more than 16 elements are left ((bytes & ~3) above 0x40, signed): with no
// depth left PartialSort(first, last, last, 0, tag) and done; else the
// depth less one, the pivot the median of the first, middle ((n / 2),
// signed) and last element (STLport's __median: a < b ? (b < c ? b : a < c
// ? c : a) : (a < c ? a : b < c ? c : b), each compare only when
// ordered), UnguardedPartition(first, last, pivot, tag), this function on
// [cut, last) with the depth left, and the loop on [first, cut); all at
// the game's addresses.
void __cdecl IntrosortLoop(uint32_t* first, uint32_t* last, uint32_t /*unread*/, uint32_t depth, uint32_t tag)
{
    static bool logged;
    FirstCall("IntrosortLoop", logged);
    while ((Bytes(first, last) & ~3) > 0x40) {
        if (depth == 0) {
            CallCdecl<void, uint32_t*, uint32_t*, uint32_t*, uint32_t, uint32_t>(addr::SC4FloatSort_PartialSort, first,
                                                                                   last, last, 0u, tag);
            return;
        }
        --depth;
        uint32_t* const middle = first + (Bytes(first, last) >> 2) / 2;
        uint32_t* const back = last - 1;
        const float a = Fb(*first), b = Fb(*middle), c = Fb(*back);
        const uint32_t* pick;
        if (a < b)
            pick = (b < c) ? middle : (a < c) ? back : first;
        else
            pick = (a < c) ? first : (b < c) ? back : middle;
        uint32_t* const cut = CallCdecl<uint32_t*, uint32_t*, uint32_t*, uint32_t, uint32_t>(
            addr::SC4FloatSort_UnguardedPartition, first, last, *pick, tag);
        CallCdecl<void, uint32_t*, uint32_t*, uint32_t, uint32_t, uint32_t>(addr::SC4FloatSort_IntrosortLoop, cut, last,
                                                                             0u, depth, tag);
        last = cut;
    }
}

// ====================================================================================
// The bridge cell vector (simulators.nettool.bridges)
// ====================================================================================

// 0x0062F6B0 (cdecl: first, last, destination, an unread tag): every
// 0x50-byte cell copy-constructed (0x006267D0) where the destination is
// not null; EAX the destination's end.
char* __cdecl BridgeCellUninitializedCopy(const char* first, const char* last, char* destination)
{
    static bool logged;
    FirstCall("BridgeCellUninitializedCopy", logged);
    for (; first != last; first += bcl::kSize, destination += bcl::kSize)
        if (destination)
            CopyConstruct(destination, first);
    return destination;
}

// 0x006310F0 (ret 0x14: the position, the value's address, an unread tag,
// a count, a byte "at the end"), this a vector of 0x50-byte cells: new
// memory for size + max(size, count) cells (the size by a signed division
// by 0x50, the max unsigned; none for 0), the cells before the position,
// count copies of the value and, unless the byte is set, the cells after
// it (UninitializedCopy at its address, handed the byte's slot as its tag)
// copy-constructed into it (where not null), the old cells freed (Free
// 0x0062BF20), the vector set.
void __fastcall BridgeCellInsertOverflow(char* self, void*, char* position, const char* value, void* /*tag*/, uint32_t count,
                               uint32_t atEnd)
{
    static bool logged;
    FirstCall("BridgeCellInsertOverflow", logged);
    Vector& v = *reinterpret_cast<Vector*>(self);
    const uint32_t size = static_cast<uint32_t>(Bytes(v.begin, v.end) / static_cast<int32_t>(bcl::kSize));
    const uint32_t grown = (size < count ? count : size) + size;
    char* const memory =
        grown ? CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, grown * static_cast<uint32_t>(bcl::kSize)) : nullptr;
    char* out = memory;
    for (char* from = v.begin; from != position; from += bcl::kSize, out += bcl::kSize)
        if (out)
            CopyConstruct(out, from);
    for (uint32_t k = count; k != 0; --k, out += bcl::kSize)
        if (out)
            CopyConstruct(out, value);
    if (!static_cast<uint8_t>(atEnd))
        out = CallCdecl<char*, char*, char*, char*, uint32_t*>(addr::SC4BridgeCell_UninitializedCopy, position, v.end,
                                                               out, &atEnd);
    CallThis<void>(addr::SC4BridgeCellVector_Free, self);
    v.end = out;
    v.begin = memory;
    v.capacity = memory + grown * bcl::kSize;
}

// 0x006312C0 (ret 4: the value's address), this a vector of 0x50-byte
// cells: with room the value copy-constructed at the end (not at a null
// end) and the end moved on; else InsertOverflow at its address (the end,
// the value, the address of the value's argument slot as the tag, 1, 1).
void __fastcall BridgeCellPushBack(char* self, void*, const char* value)
{
    static bool logged;
    FirstCall("BridgeCellPushBack", logged);
    Vector& v = *reinterpret_cast<Vector*>(self);
    if (v.end != v.capacity) {
        if (v.end)
            CopyConstruct(v.end, value);
        v.end += bcl::kSize;
        return;
    }
    const char* slot = value;
    CallThis<void, char*, const char*, const char**, uint32_t, uint32_t>(addr::SC4BridgeCellVector_InsertOverflow, self,
                                                                         v.end, value, &slot, 1u, 1u);
}

// ====================================================================================
// The bridges' occupants
// ====================================================================================

// 0x0062F610 (ret 4: the placement's list, as the placer's): the major
// bridges in order (their end read once) until one whose last step is not
// below the valid step count (signed; it ends the walk); for each, its
// record (the row's +0x30 + index * 0x178) and every cell (the cells' end
// read once per bridge): the cell's entry (0x00633220, key (z << 16) + x)
// marked +0x51 = 1 and PlaceBridgeCellOccupant at its address (the cell,
// the entry, the record, the list).
void __fastcall PlaceMajorBridgeOccupants(char* self, void*, ListNode** list)
{
    static bool logged;
    FirstCall("PlaceMajorBridgeOccupants", logged);
    char* mb = At<Vector>(self, field::kMajorBridges).begin;
    char* const bridgesEnd = At<Vector>(self, field::kMajorBridges).end;
    if (mb == bridgesEnd)
        return;
    do {
        if (At<int32_t>(mb, bmj::kLast) >= At<int32_t>(self, field::kValidSteps))
            return;
        char* const record = At<char*>(At<char*>(self, field::kRow), row::kBridges) +
                             At<uint32_t>(mb, bmj::kRecord) * static_cast<uint32_t>(bridge::kSize);
        char* const cellsEnd = At<char*>(mb, bmj::kCellsEnd);
        for (char* c = At<char*>(mb, bmj::kCells); c != cellsEnd; c += bcl::kSize) {
            const uint32_t key = (At<uint32_t>(c, bcl::kZ) << 16) + At<uint32_t>(c, bcl::kX);
            char* const info = CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, key);
            Put<uint8_t>(info, cell::kOccupied) = 1;
            CallThis<void, char*, char*, char*, ListNode**>(addr::cSC4NetworkTool_PlaceBridgeCellOccupant, self, c, info,
                                                            record, list);
        }
        mb += bmj::kSize;
    } while (mb != bridgesEnd);
}

inline constexpr std::size_t kDirection1 = 0x7C; // the drag's first direction (0..7)
inline constexpr uint32_t kLastSubdivisions = 10; // the last span's cable subdivisions

// What PlaceBridgeCellOccupant asks of the piece's exemplar: a property
// whose absence gives the occupant a passage ceiling.
inline constexpr uint32_t kNoCeilingProperty = 0xABE1B5A3;
inline constexpr uint32_t kFloatMaxBits = 0x7F7FFFFF;
// The network occupant's slots this part calls beyond nettool_occupants.h's.
inline constexpr std::size_t kSetImmovable = 0x48, kAddPart = 0x7C, kSetPassageCeiling = 0x110;
// The terrain's water level at a point (x, z; ST0).
inline constexpr std::size_t kTerrainWaterLevel = 0x78;
// The exemplar's property holder (slot 0x1C) and its property lookup (0x24).
inline constexpr std::size_t kExemplarHolder = 0x1C, kHolderProperty = 0x24;

// A pier wall quad as 0x00643B70 takes it: four corners (x, y, z).
struct Quad {
    float v[12];
};

// 0x0062EEB0 (ret 0x10: a bridge cell, its cell info, the bridge record,
// a list or null): the cell's bridge occupant:
//   - slot 54 (the info, the occupant out, a box out, 0x49CC1BCD); its
//     model set up (SetUpPrebuiltOccupant: the piece, the offset {0, deck -
//     y, 0}, the turn, group 0), slot 0x78 with the piece and the turn, the
//     flags 0x2000000 and 0x4000000 cleared, 0x8000000 set, 0x4000 cleared
//     when it has 0x10000000, else set;
//   - the piece's exemplar (0x006244B0 with the piece, 0): when its holder
//     (slot 0x1C) has no property 0xABE1B5A3 (slot 0x24 answers 0), the
//     passage ceiling (slot 0x110) is the lowest of the cell info's four
//     heights (MinElement at its address) less the record's +0xD0; the
//     exemplar released;
//   - a pier (a depth +0x28 not below 0): the occupant's top float(y -
//     depth) and the lowest terrain altitude at the cell's four vertices;
//     when the top is not below it, the pier's walls (AddPierWalls at its
//     address): the corners from the record's +0x8C (pier pieces) or +0x80
//     offsets for the turn (four (x, z) pairs per turn), from the top down
//     to that altitude, split at the water line when the walls' mean
//     bottom is under the water level (terrain slot 0x78): the part above
//     the line (only when the tops' mean is above the water) and below it;
//   - the side walls (AddBridgeCellWalls at its address) when +0x40 is set;
//   - each cable part of +0x44: four AddPart triangles of a quad (the two
//     points, each up and down by the part's half height; UVs (0, 1), (0,
//     0), (1, 0), (1, 1); index triples (0, 1, 2), (0, 2, 3), (0, 2, 1),
//     (0, 3, 2); the part's material);
//   - placing (+0x50): the cell cleared (ClearCellForOccupant with the
//     filter +0x364); SetImmovable(1); with an occupant manager, the
//     occupant inserted (slot 0x50, its occupant part and +0x208) and the
//     answer in the info's +0x54; with a list, the occupant part
//     referenced and the occupant appended; the occupant released.
void __fastcall PlaceBridgeCellOccupant(char* self, void*, char* c, char* info, char* record, ListNode** list)
{
    static bool logged;
    FirstCall("PlaceBridgeCellOccupant", logged);
    void* occupant = nullptr;
    float box[12] = {}; // slot 54 writes all twelve
    Virtual<void>(self, occ::tslot::kCreateNetworkOccupant, info, &occupant, box, occ::kKindBridge);
    float position[3] = {};
    Virtual<void>(Virtual<void*>(occupant, occ::oslot::kAsOccupant), occ::oslot::kGetPosition, position);

    float offset[3];
    offset[1] = static_cast<float>(D(c, bcl::kDeck) - static_cast<double>(position[1]));
    offset[2] = 0.0f;
    offset[0] = 0.0f;
    CallThis<void, void*, uint32_t, float*, uint32_t, uint32_t>(addr::cSC4NetworkTool_SetUpPrebuiltOccupant, self,
                                                                occupant, U(c, bcl::kPiece),
                                                                offset, At<uint8_t>(c, bcl::kTurn), 0u);
    Virtual<void>(occupant, occ::oslot::kSetPiece, U(c, bcl::kPiece), static_cast<uint32_t>(At<uint8_t>(c, bcl::kTurn)));
    occ::ClearFlag(occupant, occ::oflag::kFlag2000000);
    occ::ClearFlag(occupant, occ::oflag::kFlag4000000);
    occ::SetFlag(occupant, occ::oflag::kFlag8000000);
    if (Virtual<uint32_t>(occupant, occ::oslot::kGetFlags) & occ::oflag::kUsable)
        occ::ClearFlag(occupant, occ::oflag::kTranslucent);
    else
        occ::SetFlag(occupant, occ::oflag::kTranslucent);

    if (void* const exemplar =
            CallThis<void*, uint32_t, uint32_t>(addr::cSC4NetworkTool_GetPieceExemplar, self, U(c, bcl::kPiece), 0u)) {
        void* const holder = Virtual<void*>(exemplar, kExemplarHolder);
        if (Virtual<uint32_t>(holder, kHolderProperty, kNoCeilingProperty) == 0) {
            const float* const lowest = CallCdecl<float*, char*, char*>(
                addr::SC4FloatRange_MinElement, info + cell::kHeights, info + cell::kHeights + 0x10);
            const float ceiling = static_cast<float>(static_cast<double>(*lowest) - D(record, brec::kCeilingDrop));
            Virtual<void>(occupant, kSetPassageCeiling, ceiling);
        }
        ReleaseOf(exemplar);
    }

    // The pier: `fcomp 0.0` + `test ah, 1` + JNE: below 0 or a NaN skips.
    if (F(c, bcl::kPierDepth) >= Zero()) {
        float pos[3] = {};
        Virtual<void>(Virtual<void*>(occupant, occ::oslot::kAsOccupant), occ::oslot::kGetPosition, pos);
        const float top = static_cast<float>(static_cast<double>(pos[1]) - D(c, bcl::kPierDepth));
        const uint32_t turn8 = static_cast<uint32_t>(At<uint8_t>(c, bcl::kTurn)) * 8u;
        float lowest = Fb(kFloatMaxBits);
        for (std::size_t k = 0; k < 4; ++k) {
            const double t = Virtual<double>(At<void*>(self, field::kTerrain), terrain_slot::kVertexAltitude,
                                             U(info, cell::kVertices + 4 * k));
            if (t < static_cast<double>(lowest)) // `fcom` + `test ah, 5` + JP: lower only when ordered
                lowest = static_cast<float>(t);
        }
        // `fcomp` + `test ah, 1` + JNE: a top below the lowest, or a NaN, skips.
        if (top >= lowest) {
            const std::size_t table =
                U(c, bcl::kKind) == bcl::kKindPier ? brec::kPierWallOffsets : brec::kWallOffsets;
            Quad high{}, low{};
            for (std::size_t k = 0; k < 4; ++k) {
                const float* const offsets = At<float*>(record, table) + turn8 + 2 * k;
                const float x = static_cast<float>(static_cast<double>(pos[0]) + static_cast<double>(offsets[0]));
                const float z = static_cast<float>(static_cast<double>(pos[2]) + static_cast<double>(offsets[1]));
                high.v[3 * k] = x;
                high.v[3 * k + 1] = top;
                high.v[3 * k + 2] = z;
                low.v[3 * k] = x;
                low.v[3 * k + 1] = lowest;
                low.v[3 * k + 2] = z;
            }
            const double water =
                Virtual<double>(At<void*>(self, field::kTerrain), kTerrainWaterLevel, pos[0], pos[2]);
            // (((l3 + l2) + l1) + l0) * 0.25, the game's order.
            const double bottom = (((static_cast<double>(low.v[10]) + static_cast<double>(low.v[7])) +
                                    static_cast<double>(low.v[4])) +
                                   static_cast<double>(low.v[1])) *
                                  static_cast<double>(Quarter());
            if (bottom < water) { // `fcomp st(1)` + `test ah, 5` + JP: below only when ordered
                const double line = water - WaterMargin();
                const float lineFloat = static_cast<float>(line);
                Quad middle{};
                for (std::size_t k = 0; k < 4; ++k) {
                    middle.v[3 * k] = low.v[3 * k];
                    middle.v[3 * k + 2] = low.v[3 * k + 2];
                }
                // The first three compare the line as stored and reloaded
                // (`fst`, `fld`, `fcomp`), the fourth the unrounded line on
                // the x87 stack (`fst`, `fcomp`); each `test ah, 5` + JNP:
                // the line when it is below, else the top.
                for (std::size_t k = 0; k < 3; ++k)
                    middle.v[3 * k + 1] = (lineFloat < high.v[3 * k + 1]) ? lineFloat : high.v[3 * k + 1];
                middle.v[10] = (line < static_cast<double>(high.v[10])) ? lineFloat : high.v[10];
                const double above = (((static_cast<double>(high.v[10]) + static_cast<double>(high.v[7])) +
                                       static_cast<double>(high.v[4])) +
                                      static_cast<double>(high.v[1])) *
                                     static_cast<double>(Quarter());
                if (above > water) // `test ah, 0x41` + JNE: above only when ordered
                    CallCdecl<void, void*, Quad*, Quad*, uint32_t>(addr::SC4NetworkOccupantGeometry_AddPierWalls,
                                                                   occupant, &high, &middle, 0u);
                CallCdecl<void, void*, Quad*, Quad*, uint32_t>(addr::SC4NetworkOccupantGeometry_AddPierWalls, occupant,
                                                               &middle, &low, 0u);
            } else {
                CallCdecl<void, void*, Quad*, Quad*, uint32_t>(addr::SC4NetworkOccupantGeometry_AddPierWalls, occupant,
                                                               &high, &low, 0u);
            }
        }
    }

    if (At<uint8_t>(c, bcl::kWallFlags))
        CallThis<void, void*, char*, char*, char*>(addr::cSC4NetworkTool_AddBridgeCellWalls, self, occupant, c, info,
                                                   record);

    // The cable parts (the end read once): h + y and y - h, the game's
    // operand orders.
    char* p = At<char*>(c, bcl::kParts);
    char* const partsEnd = At<char*>(c, bcl::kPartsEnd);
    for (; p != partsEnd; p += bpt::kSize) {
        const double h = D(p, bpt::kHalfHeight);
        float vertices[12];
        vertices[0] = F(p, 0x0);
        vertices[1] = static_cast<float>(h + D(p, 0x4));
        vertices[2] = F(p, 0x8);
        vertices[3] = F(p, 0x0);
        vertices[4] = static_cast<float>(D(p, 0x4) - h);
        vertices[5] = F(p, 0x8);
        vertices[6] = F(p, 0xC);
        vertices[7] = static_cast<float>(D(p, 0x10) - h);
        vertices[8] = F(p, 0x14);
        vertices[9] = F(p, 0xC);
        vertices[10] = static_cast<float>(h + D(p, 0x10));
        vertices[11] = F(p, 0x14);
        const uint32_t uvs[8] = {0, 0x3F800000, 0, 0, 0x3F800000, 0, 0x3F800000, 0x3F800000};
        const uint32_t indices[12] = {0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2};
        const uint32_t material = U(p, bpt::kMaterial);
        for (std::size_t k = 0; k < 4; ++k)
            Virtual<void>(occupant, kAddPart, static_cast<const float*>(vertices), static_cast<const uint32_t*>(uvs),
                          static_cast<const uint32_t*>(indices + 3 * k), material);
    }

    if (At<uint8_t>(self, field::kReplacing))
        CallThis<void, uint32_t, uint32_t, void*>(addr::cSC4NetworkTool_ClearCellForOccupant, self, U(c, bcl::kX),
                                                  U(c, bcl::kZ), At<void*>(self, field::kFilter));
    Virtual<void>(occupant, kSetImmovable, 1u);
    if (void* const manager = At<void*>(self, field::kOccupants)) {
        const uint32_t word = At<uint32_t>(self, field::kWord208);
        void* const part = Virtual<void*>(occupant, occ::oslot::kAsOccupant);
        Put<uint8_t>(info, cell::kByte54) = Virtual<uint8_t>(manager, occ::xslot::kInsertOccupant, part, word);
    }
    if (list) {
        AddRefOf(Virtual<void*>(occupant, occ::oslot::kAsOccupant));
        occ::PushBack(*list, occupant);
    }
    if (occupant)
        ReleaseOf(occupant);
}

// ====================================================================================
// The bridge's cells
// ====================================================================================

// The record's 8-byte piece entries: {piece, turn byte}.
inline uint32_t EntryPiece(const char* record, std::size_t vector, uint32_t index)
{
    return U(At<char*>(const_cast<char*>(record), vector), index * brec::kPieceEntry);
}
inline uint8_t EntryTurn(const char* record, std::size_t vector, uint32_t index)
{
    return *reinterpret_cast<const uint8_t*>(At<char*>(const_cast<char*>(record), vector) +
                                             static_cast<uint32_t>(index * brec::kPieceEntry + brec::kPieceTurn));
}
inline uint32_t Word(const char* record, std::size_t vector, uint32_t index)
{
    return U(At<char*>(const_cast<char*>(record), vector), index * 4u);
}
inline double VectorFloat(const char* record, std::size_t vector, uint32_t index)
{
    return D(At<char*>(const_cast<char*>(record), vector), index * 4u);
}

// A cable point (x, y, z) as the game builds one on its stack.
struct Point {
    float x, y, z;
};

inline void AddCableParts(char* cells, int32_t rows, uint32_t across, Point* p0, Point* p1, Point* q0, Point* q1,
                          uint32_t subdivisions, uint32_t halfHeight, uint32_t material)
{
    CallCdecl<void, char*, int32_t, uint32_t, Point*, Point*, Point*, Point*, uint32_t, uint32_t, uint32_t>(
        addr::SC4BridgeCell_AddCableParts, cells, rows, across, p0, p1, q0, q1, subdivisions, halfHeight, material);
}

// 0x00631300 (ret 4: a major bridge): the bridge's cells, appended to its
// +0x1C through PushBack at its address (a zeroed template; the game's
// stack slot has only its part vector cleared), for every step from the
// first to the last and every dragged cell of the step (in the drag's
// order when the bridge's turn ((direction / 2 + 3) % 4, signed) is 0 or
// 1, else backwards):
//   - the pier run: start = first + r38 + r7c, end = last - r38 - r7c;
//     with a pier length (+0x40): the pier count q = span / r34 (unsigned)
//     kept within the fewest / most piers (+0xA8 / +0xAC: one less when
//     outside unless the limit is 1) and half the span (signed), the
//     spacing span / q (signed) or 100000 without a pier; once, the run is
//     narrowed by the round of n / (2 f + q) * f (f the record's +0x3C, n
//     the bridge's steps) less r7c + r38 when that is above 0, and the
//     count redone; the first pier at the run's start. A run that does not
//     fit (start above end) is the bridge's middle when the record wants
//     piers (+0xA8 not 0);
//   - a cell: kind 0 in the first and the last r10 steps (the ramp pieces
//     +0x14 by step and cell, the turn reversed at the far end, the depth
//     +0x20 by step); kind 1 on a pier (the pier pieces +0x44, depth +0x50;
//     reversed when the step is before the next pier; at the next pier the
//     pier begins and the next one is a spacing on, or none past the run's
//     end); kind 2 else (the span piece +0x2C, turn +0x30 + 2 i); the turn
//     plus the bridge's, all mod 4; heights: the deck altitude for the
//     corners, + d4 (+ c4 for a pier) for the deck; a ramp cell's from the
//     ramp drops +0x60 / +0x6C by its row; the wall flags and floats when a
//     pier was placed or is due and the record has +0xB0 and +0xB4;
//   - then, with +0xB0, +0xB4 and a pier: the walls raised along the curve
//     (0x0062E690: the start, each spacing between piers, the far end); or,
//     with cables (+0xD8): the cables' parts (0x00630F70: the first span,
//     each spacing to the last pier, the last span with 10 subdivisions).
void __fastcall BuildMajorBridgeCells(char* self, void*, char* mb)
{
    static bool logged;
    FirstCall("BuildMajorBridgeCells", logged);
    const char* const record = At<char*>(At<char*>(self, field::kRow), row::kBridges) +
                               At<uint32_t>(mb, bmj::kRecord) * static_cast<uint32_t>(bridge::kSize);
    const int32_t turn = Mod4(At<int32_t>(self, kDirection1) / 2 + 3);
    const uint8_t turnByte = static_cast<uint8_t>(turn);
    const bool forward = turnByte == 0 || turnByte == 1;
    const uint32_t turnWord = turnByte; // the game's movzx of the byte
    const uint32_t keep = U(record, brec::kKeepOff38) + U(record, brec::kKeepOff7C);
    const int32_t first = At<int32_t>(mb, bmj::kFirst);
    const int32_t last = At<int32_t>(mb, bmj::kLast);
    int32_t start = static_cast<int32_t>(static_cast<uint32_t>(first) + U(record, brec::kKeepOff38) +
                                         U(record, brec::kKeepOff7C));
    int32_t end = static_cast<int32_t>(static_cast<uint32_t>(last) - U(record, brec::kKeepOff38) -
                                       U(record, brec::kKeepOff7C));
    int32_t pierStart = -1; // the frame's +0x18: the step the current pier began at
    int32_t nextPier = -1;  // +0x14: the step of the next pier
    int32_t spacing = 100000;
    if (U(record, brec::kPierSteps) != 0) {
        if (start <= end) {
            int32_t span = end - start;
            bool narrowed = false;
            for (;;) {
                uint32_t count = static_cast<uint32_t>(span) / U(record, brec::kPierDivisor);
                const uint32_t most = U(record, brec::kMostPiers);
                if (count > most && most - 1u != 0)
                    count = most ? most - 1u : 0u;
                const uint32_t fewest = U(record, brec::kFewestPiers);
                if (count < fewest && fewest - 1u != 0)
                    count = fewest ? fewest - 1u : 0u;
                if (static_cast<int32_t>(count) > span / 2)
                    count = static_cast<uint32_t>(span / 2);
                if (static_cast<int32_t>(count) <= 0) {
                    spacing = 100000;
                    break;
                }
                spacing = span / static_cast<int32_t>(count);
                if (narrowed)
                    break;
                const int32_t steps = last - first + 1;
                narrowed = true;
                const double f = D(record, brec::kPierFit);
                // FILD n; FLD f; FADD st, st; FIADD q; FDIVP; FMUL f.
                const double fit = (static_cast<double>(steps) / ((f + f) + static_cast<double>(static_cast<int32_t>(count)))) * f;
                // The round by the magic constant: float((x - 0.5) + 12582912.0)'s
                // bits less 0x4B400000, one more when x is that exactly
                // (FUCOMPP + `test ah, 0x44`: equal only when ordered).
                const float shifted = static_cast<float>((fit - static_cast<double>(Half())) +
                                                         static_cast<double>(RoundMagic()));
                int32_t rounded = static_cast<int32_t>(std::bit_cast<uint32_t>(shifted) + 0xB4C00000u);
                if (fit == static_cast<double>(rounded + 1))
                    rounded = rounded + 1;
                const int32_t more = static_cast<int32_t>(static_cast<uint32_t>(rounded) - keep);
                if (more <= 0)
                    break;
                start += more;
                end -= more;
                span -= 2 * more;
            }
            nextPier = start;
        } else if (U(record, brec::kFewestPiers) != 0) {
            start = (first + last) / 2;
            end = start;
            spacing = 100000;
        }
    }

    const uint32_t across = U(record, brec::kAcross);
    const uint32_t rampSteps = U(record, brec::kRampSteps);
    uint32_t rampIndex = 0; // the game's EDI, set by the ramp paths before they read it
    if (first <= last) {
        for (int32_t step = first;; ++step) {
            const char* const stepRecord =
                occ::Steps(self) + static_cast<uint32_t>(step) * static_cast<uint32_t>(step::kSize);
            int32_t index, stop, direction;
            if (forward) {
                index = At<int32_t>(const_cast<char*>(stepRecord), 0);
                stop = At<int32_t>(const_cast<char*>(stepRecord), step::kLastCell) + 1;
                direction = 1;
            } else {
                index = At<int32_t>(const_cast<char*>(stepRecord), step::kLastCell);
                stop = At<int32_t>(const_cast<char*>(stepRecord), 0) - 1;
                direction = -1;
            }
            uint32_t i = 0;
            while (index != stop) {
                alignas(4) char templ[bcl::kSize] = {};
                CallThis<void, const char*>(addr::SC4BridgeCellVector_PushBack, mb + bmj::kCells, templ);
                char* const e = At<char*>(mb, bmj::kCellsEnd) - bcl::kSize;
                Put<uint32_t>(e, bcl::kPierDepth) = bcl::kNoPierBits;
                Put<uint32_t>(e, bcl::kX) = U(occ::Cells(self), static_cast<uint32_t>(index) * 8u);
                Put<uint32_t>(e, bcl::kZ) = U(occ::Cells(self), static_cast<uint32_t>(index) * 8u + 4u);
                bool reversed = false;
                const uint32_t ustep = static_cast<uint32_t>(step);
                if (ustep < static_cast<uint32_t>(first) + rampSteps) {
                    // The near ramp.
                    rampIndex = (ustep - static_cast<uint32_t>(first)) * across + i;
                    Put<uint32_t>(e, bcl::kKind) = bcl::kKindRamp;
                    Put<uint32_t>(e, bcl::kPiece) = EntryPiece(record, brec::kRampPieces, rampIndex);
                    Put<uint8_t>(e, bcl::kTurn) = EntryTurn(record, brec::kRampPieces, rampIndex);
                    Put<uint32_t>(e, bcl::kPierDepth) = Word(record, brec::kRampDepths, rampIndex / across);
                } else if (ustep > static_cast<uint32_t>(last) - rampSteps) {
                    // The far ramp: the pieces backwards, turned round.
                    rampIndex = (static_cast<uint32_t>(last) - ustep + 1u) * across - i;
                    Put<uint32_t>(e, bcl::kKind) = bcl::kKindRamp;
                    Put<uint32_t>(e, bcl::kPiece) = EntryPiece(record, brec::kRampPieces, rampIndex - 1u);
                    --rampIndex;
                    Put<uint8_t>(e, bcl::kTurn) = static_cast<uint8_t>(
                        Mod4(static_cast<int32_t>(EntryTurn(record, brec::kRampPieces, rampIndex)) + 2));
                    reversed = true;
                    Put<uint32_t>(e, bcl::kPierDepth) = Word(record, brec::kRampDepths, rampIndex / across);
                } else if (pierStart >= 0 &&
                           static_cast<uint32_t>(step - pierStart) < U(record, brec::kPierSteps)) {
                    // Inside the current pier.
                    const uint32_t d = static_cast<uint32_t>(step - pierStart);
                    const uint32_t k = across * d + i;
                    Put<uint32_t>(e, bcl::kKind) = bcl::kKindPier;
                    Put<uint32_t>(e, bcl::kPiece) = EntryPiece(record, brec::kPierPieces, k);
                    Put<uint8_t>(e, bcl::kTurn) = EntryTurn(record, brec::kPierPieces, k);
                    Put<uint32_t>(e, bcl::kPierDepth) = Word(record, brec::kPierDepths, d);
                } else {
                    bool span = true;
                    if (nextPier > 0) {
                        const int32_t gap = nextPier - step;
                        const uint32_t k = static_cast<uint32_t>(gap <= 0 ? 0 : gap) * across + i;
                        if (gap <= 0) {
                            // The next pier begins.
                            Put<uint32_t>(e, bcl::kKind) = bcl::kKindPier;
                            Put<uint32_t>(e, bcl::kPiece) = EntryPiece(record, brec::kPierPieces, k);
                            Put<uint8_t>(e, bcl::kTurn) = EntryTurn(record, brec::kPierPieces, k);
                            Put<uint32_t>(e, bcl::kPierDepth) = Word(record, brec::kPierDepths, 0);
                            pierStart = nextPier;
                            nextPier = nextPier + spacing;
                            if (nextPier > end)
                                nextPier = -1;
                            span = false;
                        } else if (static_cast<uint32_t>(gap) < U(record, brec::kPierSteps)) {
                            // Before the next pier: its pieces backwards.
                            Put<uint32_t>(e, bcl::kKind) = bcl::kKindPier;
                            Put<uint32_t>(e, bcl::kPiece) = EntryPiece(record, brec::kPierPieces, k);
                            Put<uint8_t>(e, bcl::kTurn) = static_cast<uint8_t>(
                                Mod4(static_cast<int32_t>(EntryTurn(record, brec::kPierPieces, k)) + 2));
                            Put<uint32_t>(e, bcl::kPierDepth) = Word(record, brec::kPierDepths, static_cast<uint32_t>(gap));
                            span = false;
                        }
                    }
                    if (span) {
                        Put<uint32_t>(e, bcl::kKind) = bcl::kKindSpan;
                        Put<uint32_t>(e, bcl::kPiece) = U(record, brec::kSpanPiece);
                        const uint8_t spanTurn = *reinterpret_cast<const uint8_t*>(record + brec::kSpanTurn);
                        Put<uint8_t>(e, bcl::kTurn) = spanTurn;
                        Put<uint8_t>(e, bcl::kTurn) =
                            static_cast<uint8_t>(Mod4(static_cast<int32_t>(spanTurn + 2u * i)));
                    }
                }
                Put<uint8_t>(e, bcl::kTurn) = static_cast<uint8_t>(
                    Mod4(static_cast<int32_t>(static_cast<uint32_t>(At<uint8_t>(e, bcl::kTurn)) + turnWord)));

                const uint32_t kind = U(e, bcl::kKind);
                if (kind != bcl::kKindRamp) {
                    // FLD alt, FADD d4, FST deck; a pier: FADD c4 on the
                    // unrounded sum, FSTP deck.
                    const double deck = D(mb, bmj::kAltitude) + D(record, brec::kDeckHeight);
                    Put<float>(e, bcl::kDeck) = static_cast<float>(deck);
                    if (kind == bcl::kKindPier)
                        Put<float>(e, bcl::kDeck) = static_cast<float>(deck + D(record, brec::kPierDeck));
                    for (std::size_t k = 0; k < 4; ++k)
                        Put<uint32_t>(e, bcl::kHeights + 4 * k) = U(mb, bmj::kAltitude);
                } else {
                    const uint32_t row = rampIndex / across;
                    const uint32_t which = reversed ? (turnWord - 2u) & 3u : turnWord;
                    const int32_t corner = CornerByTurn(which);
                    const double alt = D(mb, bmj::kAltitude);
                    Put<float>(e, bcl::kDeck) =
                        static_cast<float>((alt - VectorFloat(record, brec::kDeckDrops, row)) + D(record, brec::kDeckHeight));
                    const float h0 = static_cast<float>(alt - VectorFloat(record, brec::kRampDrops, row));
                    Put<float>(e, bcl::kHeights + 4 * static_cast<uint32_t>((corner + 1) & 3)) = h0;
                    Put<float>(e, bcl::kHeights + 4 * static_cast<uint32_t>(corner)) = h0;
                    const float h1 = static_cast<float>(alt - VectorFloat(record, brec::kRampDrops, row + 1u));
                    Put<float>(e, bcl::kHeights + 4 * static_cast<uint32_t>((corner - 1) & 3)) = h1;
                    Put<float>(e, bcl::kHeights + 4 * static_cast<uint32_t>((corner - 2) & 3)) = h1;
                }

                if ((pierStart == -1 && nextPier == -1) || U(record, brec::kWallsB0) == 0 ||
                    U(record, brec::kWallsB4) == 0) {
                    Put<uint8_t>(e, bcl::kWallFlags) = 0;
                } else {
                    const int32_t t = CornerByTurn(turnWord);
                    const uint32_t a = static_cast<uint32_t>((t - 2) & 3) + static_cast<uint32_t>((t + 1) & 3);
                    const uint32_t b = static_cast<uint32_t>((t - 1) & 3) + static_cast<uint32_t>(t);
                    uint8_t flags = static_cast<uint8_t>((b == a ? 1 : 0) + 1);
                    Put<uint8_t>(e, bcl::kWallFlags) = flags;
                    if (step == pierStart) {
                        flags |= 4;
                        Put<uint8_t>(e, bcl::kWallFlags) = flags;
                    }
                    for (std::size_t k = 0; k < 4; ++k)
                        Put<uint32_t>(e, bcl::kWallScales + 4 * k) = 0x3F800000; // 1.0f
                    Put<uint32_t>(e, bcl::kWallFloat) = U(record, brec::kWallFloat);
                    if (across > 1u) {
                        const uint32_t edge = (turnByte == 0 || turnByte == 3) ? 1u : 0u;
                        flags = At<uint8_t>(e, bcl::kWallFlags);
                        flags |= ((i == 0 ? 1u : 0u) == edge) ? 8 : 0x10;
                        Put<uint8_t>(e, bcl::kWallFlags) = flags;
                    }
                }
                index += direction;
                ++i;
            }
            if (step + 1 > last)
                break;
        }
    }

    const uint32_t across12 = across * static_cast<uint32_t>(bcl::kSize);
    if (U(record, brec::kWallsB0) != 0 && U(record, brec::kWallsB4) != 0 && pierStart != -1) {
        // The walls along the curve (cdecl: the cells, the rows, the cells
        // across, the record's float, the mode 0 start / 1 middle / 2 end).
        auto raiseWalls = [&](int32_t from, int32_t rows, uint32_t shape, uint32_t mode) {
            char* const cells = At<char*>(mb, bmj::kCells) + static_cast<uint32_t>(from - first) * across12;
            CallCdecl<void, char*, int32_t, uint32_t, uint32_t, uint32_t>(addr::SC4BridgeCell_RaiseWallsAlongCurve,
                                                                          cells, rows, across, shape, mode);
        };
        raiseWalls(first, start - first + 1, U(record, brec::kCurveA0), 0u);
        int32_t at = start;
        if (start < pierStart) {
            int32_t to;
            do {
                to = at + spacing;
                if (to > pierStart)
                    to = pierStart;
                raiseWalls(at, to - at + 1, U(record, brec::kCurve9C), 1u);
                at = to;
            } while (to < pierStart);
        }
        raiseWalls(pierStart, last - pierStart + 1, U(record, brec::kCurveA0), 2u);
        return;
    }
    if (!*reinterpret_cast<const uint8_t*>(record + brec::kCables))
        return;

    // The cables. The record's eight points: A (+0x114), B (+0x120), C
    // (+0x12C), D (+0x138) and four more at +0xE4, +0xF0, +0xFC, +0x108;
    // x shifted by the run's offset (an integer times 16) and 8, y by the
    // deck altitude, z by 8, every value as the game stores it.
    const double alt = D(mb, bmj::kAltitude);
    const double eight = static_cast<double>(Eight());
    auto r = [&](std::size_t offset) { return D(record, offset); };
    auto fl = [](double value) { return static_cast<float>(value); };
    const int32_t s = start - first;
    const int32_t q = pierStart - first;
    const int32_t rows = last - first + 1;
    char* const cells = At<char*>(mb, bmj::kCells);
    const uint32_t subdivisions = U(record, brec::kCableSubdivisions);
    const uint32_t halfHeight = U(record, brec::kCableHalfHeight);
    const uint32_t material = U(record, brec::kCableMaterial);
    // The frame's stored floats (+0x7C ... +0x68).
    const float t1x = fl(r(0xE4) + eight), t1y = fl(alt + r(0xE8)), t1z = fl(r(0xEC) + eight);
    const float t2x = fl(r(0xF0) + eight), t2y = fl(alt + r(0xF4)), t2z = fl(r(0xF8) + eight);
    const float t3x = fl(r(0xFC) + eight), t3y = fl(alt + r(0x100)), t3z = fl(r(0x104) + eight);
    const float t4x = fl(r(0x108) + eight), t4y = fl(alt + r(0x10C)), t4z = fl(r(0x110) + eight);
    // A point (x0 + p.x) + 8, p.y + alt, p.z + 8 of record point p at x0.
    auto at = [&](double x0, std::size_t p) {
        return Point{fl((x0 + r(p)) + eight), fl(r(p + 4) + alt), fl(r(p + 8) + eight)};
    };
    {
        const double x0 = static_cast<double>(static_cast<int32_t>(static_cast<uint32_t>(s) << 4));
        Point p0 = at(x0, 0x114);
        Point p1 = at(x0, 0x120);
        Point q0{fl(static_cast<double>(t1x) + x0), t1y, t1z};
        Point q1{fl(static_cast<double>(t2x) + x0), t2y, t2z};
        AddCableParts(cells, rows, across, &p0, &p1, &q0, &q1, subdivisions, halfHeight, material);
    }
    if (s < q) {
        int32_t from = s, to;
        do {
            to = from + spacing;
            if (to > q)
                to = q;
            const double x0 = static_cast<double>(static_cast<int32_t>(static_cast<uint32_t>(from) << 4));
            const double x1 = static_cast<double>(static_cast<int32_t>(static_cast<uint32_t>(to) << 4));
            const float x1f = fl(x1);
            Point c0 = at(x0, 0x12C);
            Point d0 = at(x0, 0x138);
            Point a1 = at(static_cast<double>(x1f), 0x114);
            Point b1 = at(static_cast<double>(x1f), 0x120);
            Point p3{fl(x0 + static_cast<double>(t3x)), t3y, t3z};
            Point p4{fl(static_cast<double>(t4x) + x0), t4y, t4z};
            AddCableParts(cells, rows, across, &p3, &p4, &c0, &d0, subdivisions, halfHeight, material);
            Point p1{fl(static_cast<double>(x1f) + static_cast<double>(t1x)), t1y, t1z};
            Point p2{fl(static_cast<double>(x1f) + static_cast<double>(t2x)), t2y, t2z};
            AddCableParts(cells, rows, across, &a1, &b1, &p1, &p2, subdivisions, halfHeight, material);
            from = to;
        } while (to < q);
    }
    {
        const double x0 = static_cast<double>(static_cast<int32_t>(static_cast<uint32_t>(q) << 4));
        Point d = at(x0, 0x138);
        Point c = at(x0, 0x12C);
        Point p3{fl(x0 + static_cast<double>(t3x)), t3y, t3z};
        Point p4{fl(static_cast<double>(t4x) + x0), t4y, t4z};
        AddCableParts(cells, rows, across, &p3, &p4, &c, &d, kLastSubdivisions, halfHeight, material);
    }
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kBridges[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_PlaceMajorBridgeOccupants, PlaceMajorBridgeOccupants),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_PlaceBridgeCellOccupant, PlaceBridgeCellOccupant),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_BuildMajorBridgeCells, BuildMajorBridgeCells),
    OPENSC4_DETOUR(addr::SC4BridgeCellVector_PushBack, BridgeCellPushBack),
    OPENSC4_DETOUR(addr::SC4BridgeCellVector_InsertOverflow, BridgeCellInsertOverflow),
    OPENSC4_DETOUR(addr::SC4BridgeCell_UninitializedCopy, BridgeCellUninitializedCopy),
};

const Detour kBridgeSort[] = {
    OPENSC4_DETOUR(addr::SC4FloatRange_MinElement, MinElement),
    OPENSC4_DETOUR(addr::SC4DwordRange_CopyBackward, CopyBackward),
    OPENSC4_DETOUR(addr::SC4FloatHeap_PushHeap, PushHeap),
    OPENSC4_DETOUR(addr::SC4FloatHeap_AdjustHeap, AdjustHeap),
    OPENSC4_DETOUR(addr::SC4FloatHeap_MakeHeap, MakeHeap),
    OPENSC4_DETOUR(addr::SC4FloatHeap_SortHeap, SortHeap),
    OPENSC4_DETOUR(addr::SC4FloatSort_UnguardedPartition, UnguardedPartition),
    OPENSC4_DETOUR(addr::SC4FloatSort_LinearInsert, LinearInsert),
    OPENSC4_DETOUR(addr::SC4FloatSort_PartialSort, PartialSort),
    OPENSC4_DETOUR(addr::SC4FloatSort_IntrosortLoop, IntrosortLoop),
};

#undef OPENSC4_DETOUR

bool InstallBridges(registry::Mode) { return InstallDetours(kBridges, "bridges"); }
bool InstallBridgeSort(registry::Mode) { return InstallDetours(kBridgeSort, "bridgesort"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.bridges", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_BuildMajorBridgeCells,
                        "Our network tool's major bridges: their cells (ramp, pier and span pieces), the bridge "
                        "occupants of the cells, and the cell vector's push, growth and copy (6 detours; "
                        "src/core/nettool_bridge_a.cpp)",
                        false, InstallBridges}),
    registry::Register({"simulators.nettool.bridgesort", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::SC4FloatSort_IntrosortLoop,
                        "Our float sort, heap and min element template code under the bridges' cable geometry and "
                        "occupants (10 detours; src/core/nettool_bridge_a.cpp)",
                        false, InstallBridgeSort}),
};

} // namespace
} // namespace opensc4::nettool_sim
