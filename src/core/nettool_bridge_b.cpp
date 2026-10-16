// Our cSC4NetworkTool, part t11 (build 641; the class map and the fields are
// in src/core/nettool_sim.h): the bridges the segment placer 0x0063B830 asks
// for. A drag across water or across a dip becomes a bridge here: the
// placer calls FindMajorBridges 0x00632830 (over water: the type row's bridge
// records whose byte +0x99 is set, picked by the water's depth and the
// span's length) and FitMinorBridges 0x00632AD0 (over a dip in the terrain:
// the records whose byte +0x98 is set, the depth 0). Both append a major
// bridge (0x28 bytes) to the tool's +0xD8 and lay its cells (0x50 bytes
// each, the bridge's +0x1C) from the record's layout for that length
// (LayBridgeCells 0x00632060) or, when the record has no layout for it,
// through part t10's 0x00631300. Two groups, each a list of detours at the
// game's addresses:
//   simulators.nettool.bridgepick: FindMajorBridges 0x00632830,
//     FitMinorBridges 0x00632AD0, AddMinorBridge 0x00632580, FindBridgeSpan
//     0x00630570, FitBridgeSpan 0x0062FED0, PickBridgeRecord 0x0062F760,
//     ExtendBridgeSpan 0x0062ECD0, StepCellsFitBridge 0x0062EB80,
//     CellsAreFree 0x0062FDB0 (a naked thunk: ESI in), LowestCellCorner
//     0x0062E630 (a naked thunk: ECX in, the caller cleans) and
//     RaiseMajorBridgeAltitude 0x0062EA90 (11 detours);
//   simulators.nettool.bridgeplace: LayBridgeCells 0x00632060 and the
//     containers under the bridges: the 32-byte element vector's copy
//     constructor 0x00624EA0, the bridge cell's copy constructor 0x006267D0
//     and the cell vector's Free 0x0062BF20 and copy constructor 0x0062FB00,
//     the major bridges' uninitialized copy 0x00631200 and fill 0x00631260,
//     InsertOverflow 0x006323E0 and PushBack 0x00632510 (9 detours).
// The split: the decisions (where a bridge goes, which record, how high)
// and the cells' making with the template code under it; each group can be
// tried and bisected alone (OPENSC4_NETTOOL_SKIP too).
//
// What the names mean (found from the code, the survey only said
// "bridges"): a step of the drag (+0x54) is a run of dragged cells (+0x60).
// The tool's +0x33C lists the records usable over water (byte +0x99), +0x348
// the ones over land (byte +0x98) (ListBridgeKinds 0x0062F9D0). A record's
// +0x4 / +0x8 are its shortest and longest span in steps, +0xC its cells per
// step, +0xC8 / +0xCC the water depth it allows, +0x5C / +0x78 the deck's
// height above water / above land, +0x9C, +0xA0, +0xA4 and +0xD4 words and a
// float the cells get, +0x16C a map from the span's length to a layout (a
// vector of 20-byte pieces: a position, a piece word, a turn byte, a float,
// a kind word 0 / 1 pylon / 2 end; and a float vector of heights per step,
// part r4's bridge parser). A major bridge (+0xD8, 0x28 bytes): +0x0 the
// network type, +0x4 / +0x8 the first and last step, +0xC the record,
// +0x10 the deck's altitude, +0x14 the height FitBridgeSpan found (a minor
// bridge: its height in both), the byte +0x18 (1 for a minor bridge), +0x1C its cells.
// A bridge cell (0x50 bytes, the bridge's +0x1C): +0x0 0, +0x4 / +0x8 x and
// z, +0xC the piece, the byte +0x10 its turn, +0x14 to +0x20 the four corner
// heights, +0x24 the deck height, +0x28 the piece's float, +0x2C to +0x38
// four floats (1.0 to start; part t10's 0x0062E690 lowers them), +0x3C the
// record's +0xA4, the byte +0x40 the pier flags (1 or 2, 4 for a pylon, 8 or
// 0x10 for the side), +0x44 a vector of 32-byte elements.
//
// Every function was written from the disassembly. Calls between part
// functions (ours or part t10's) go to the game's addresses, so each detour
// stands alone. Part t10's functions are called through its address
// entries (BuildMajorBridgeCells 0x00631300, SC4BridgeCellVector::
// InsertOverflow 0x006310F0, SC4BridgeCell::RaiseWallsAlongCurve
// 0x0062E690).
//
// Left as the game's: nothing of the part. Called at their addresses: part
// t10's 0x00631300, 0x006310F0 and 0x0062E690; the cell entry 0x00633220
// (t2); DestroyMajorBridges 0x0062C590 (t3); the 32-byte element copy
// 0x0062E970 (template code); SC4PtrVector::InsertN, SC4Uint32Vector::Clear,
// SC4Sint32Map::Find, SC4List::Clear2 and Destroy2, SC4::stl_allocate and
// SC4::Free; the list node allocator (game_access.h's NodeAllocatorAt, as
// the game inlines it). Nothing here draws: the cells hold heights and piece
// IDs the occupant code (part t10's 0x0062F610) turns into bridge occupants.
//
// NAM. None of these is guarded, no patched byte is in their ranges (docs
// and data/addresses checked), and no guarded function calls them (their
// only callers are listed per function; the segment placer 0x0063B830 is
// part t6's, ours). NAM's DLL reads the tool by layout (+0x1C, +0x54, +0x60,
// +0x98, +0xCC, +0x124, +0x188, +0x240, +0x244, +0x2B5): this part reads the
// steps and cells there and writes only +0x54's step words (+0x8, marked
// irregular as the game does), +0xD8 and +0x1F4, all in place with the
// game's layout and allocator.
//
// Registers. Every game caller of each function was scanned after the call
// (both branches; scratch regscan.py / eaxscan.py): none reads ECX or EDX
// before writing it (the scan's "ret" verdicts return into callers that
// write both). EAX is read after StepCellsFitBridge (AL), ExtendBridgeSpan
// (0x0062F86A: AL; 0x0062F96C reads nothing), PickBridgeRecord (all of it,
// the index), CellsAreFree (AL), FindBridgeSpan (AL), the uninitialized copy
// and fill (the end pointer), LowestCellCorner (only ST0; AX is then
// overwritten by FNSTSW); ours give those. The copy constructors return
// `this` (nobody reads it). The game's EAX above AL on the byte answers is
// not kept (callers test AL only). Two functions have conventions C++
// cannot declare and go through naked thunks: CellsAreFree takes the
// occupant manager in ESI (an LTCG convention; its four callers load ESI
// from the tool's +0x48 before the call and never read it after it as
// anything else; ESI is kept), and LowestCellCorner takes the terrain in
// ECX with two stack arguments the caller pops (`ret` with no count).
//
// Wrappers: no file of src/core/game_*.cpp or *fix*.cpp detours a function
// of this part or tests return addresses in 0x00624EA0 to 0x00632D44
// (game_quiet.cpp names 0x006208D0 in a comment only); sim_timers.cpp's
// crew range 0x00605490 to 0x00608C50 is elsewhere and nothing here reads a
// stopwatch; fixed_seed.cpp: nothing here makes or seeds a generator. So
// nothing needed a mark.
//
// Floats. The game runs the x87 at 53 bits, so intermediates are doubles
// where every operation is +, -, *, fabs or a compare and every float load
// or store rounds as `float`. No function keeps an x87 value across a call
// (each callee's ST0 is stored, combined or compared at once; FitMinorBridges
// keeps the second vertex altitude in ST0 only through compares and stores,
// no call); there is no transcendental and no float-to-integer conversion.
// The values:
//   - LowestCellCorner: the terrain's four corner heights (slot 0x44), the
//     least (each compare `test ah, 5; jnp`: a < b ordered picks a, so a NaN
//     picks the second operand), loaded with FLD (a signalling NaN comes
//     back quiet, as our float return through ST0 does);
//   - RaiseMajorBridgeAltitude: per step, the last cell's (slot 0x48 height
//     + the record's +0x5C over water, else +0x78), kept unrounded for its
//     compare and stored as float(h) when above the bridge's +0x10 (`test
//     ah, 0x41; jne`); the game loads its own argument slot (the bridge's
//     ADDRESS) as the first float: only an empty step before any cell would
//     compare it, and ours loads the same bits (the same pointer);
//   - FitBridgeSpan: the limits float(params+0 * 14.0) and float(params+0 *
//     4.0) (exact products rounded once), per step the highest of the cells'
//     least corners (moved as bits: the game copies the selected float with
//     MOV in the inline form and stores the call's ST0 with FSTP in the
//     other, as ours does) and the highest neighbouring cell height (FSTP),
//     their difference unrounded against the limits; the bridge height the
//     min / max of four candidates by bits, the altitude override when not
//     below 0.0 (`test ah, 1; jne`: a NaN is not taken); the span grown
//     while the step's highest cell is not at or above the height (`test
//     ah, 1; je`: a NaN grows it); the height replaced by the first cell's
//     when below 0.01 (a double, ordered); the depth = the largest float(h -
//     cell height) over the first water cell of each step;
//   - PickBridgeRecord: +0xC8 <= depth (`test ah, 0x41; jp`: ordered) and
//     +0xCC >= depth (`test ah, 1; jne`: ordered);
//   - LayBridgeCells: the deck height float((bridge+0x10 + piece position)
//     + record+0xD4) and the corners float(layout float + bridge+0x10),
//     each FST/FSTP of the same unrounded sum;
//   - FitMinorBridges: the step's two vertex altitudes (slot 0x30): the first
//     stored as float and compared, the second compared unrounded and stored
//     rounded; the span's tests bridgeTop - minLow > params+0x3C and
//     |bridgeTop - max| <= params+0x40 unrounded; every compare's NaN branch
//     as the game's (commented where not obvious).
//
// Stack leftovers the game stores (ours are 0):
//   - LayBridgeCells builds each cell from a 0x50-byte template on its stack
//     whose words +0x0 to +0x40 it never writes (only the vector +0x44 is
//     cleared); the copy (0x006267D0, or the InsertOverflow 0x006310F0)
//     carries them into the new cell. It then writes +0x0 to +0x28 and the
//     byte +0x40 of every cell, and +0x2C to +0x3C only for a cell whose
//     piece kind (+0x10 of its layout piece) is not 0 or while a pier group
//     is pending. So a plain cell outside a group keeps the original's stack
//     words in +0x2C to +0x3C. Readers: part t10's 0x0062E690 (called here
//     for a group of steps; it takes min(its value, the old word) of +0x2C
//     to +0x38 for every cell of the group, the group's first step included,
//     whose plain cells were laid before the group began: there the
//     original mixes its leftovers in), the pier code 0x0062DF30 (part t10,
//     reached from 0x0062EEB0 only when the cell's byte +0x40 is not 0,
//     which LayBridgeCells sets together with +0x2C to +0x3C: never for a
//     plain cell), the cell copies (word by word), and no other code of the
//     tool's range (the heights pass reads +0x14 to +0x20, the solver +0x4 and
//     +0x8, the cost the bridge's own words). Verdict: harmless (a leftover
//     can reach +0x2C to +0x38 of a cell whose byte +0x40 is 0, which nothing
//     reads); the padding +0x11 to +0x13 and +0x41 to +0x43 is never copied.
//   - LayBridgeCells' previous group kind (its entry ESP - 0x58) is read only
//     when a group is pending, which it sets together: no leftover read.
//   - AddMinorBridge and FindMajorBridges append a major bridge from a
//     0x28-byte local whose words +0x0 to +0x18 they never write (the cell
//     vector +0x1C is cleared); the copy carries them in, and both overwrite
//     every word and the byte at once (FindMajorBridges leaves +0x10, which
//     RaiseMajorBridgeAltitude writes first thing, at its 0x0062EABE). The
//     bytes +0x19 to +0x1B are never copied. Harmless.
//   - FitMinorBridges' dip top and first highest (its entry ESP - 0x24 and
//     - 0xC) are read only after a dip's start writes them (the in-dip byte
//     gates). Ours start at 0.
//   - FitBridgeSpan's after-span corner pass (0x006301A0 to 0x006301A8):
//     terrain slot 0x44 (0x007415F0) writes nothing for a cell outside the
//     city, so the game would compare its stack's leftover corners where
//     ours compares zeros. Harmless: those cells passed CellsAreFree and the
//     fit check, so they are inside the city and the slot writes all four.
//   - LowestCellCorner 0x0062E630: its 16-byte corner buffer on the stack is
//     not written by terrain slot 0x44 (0x007415F0) when x >= [terrain+0x28]
//     or z >= [terrain+0x2C] (unsigned); the game then returns the previous
//     call's corners (likely the previous cell's), ours 0.0. Reader:
//     FitBridgeSpan (the bridge height choice, not drawing; 0x0062FFEF).
//     Gate: an off-map cell in a drag step, which cannot occur. Harmless.
// Other stores into the game's stack, never read again as what they were:
// ExtendBridgeSpan keeps its "grow backwards" byte in its third argument's
// slot; PickBridgeRecord keeps the kind list in its third argument's slot,
// the best index in its fourth's and the best difference in its third's;
// FitBridgeSpan reuses its fifth argument's slot for a height and its first
// for a step offset; FindBridgeSpan passes its sixth argument's slot as the
// depth out; AddMinorBridge keeps a cell pointer and the list in its
// argument slots; LayBridgeCells passes its forward byte's address as the
// unread tag. Ours use locals (the unread tags are locals' addresses).
//
// Calls, each count from the asm's pushes for that call and the callee's
// own `ret N` (slots as byte offsets):
//   - the part's own, at the game's addresses: FindBridgeSpan six (ret
//     0x18, AL), FitBridgeSpan five (ret 0x14), PickBridgeRecord five (ret
//     0x14, EAX), ExtendBridgeSpan four (ret 0x10, AL), StepCellsFitBridge
//     two (ret 8, AL), CellsAreFree cdecl five with ESI (AL),
//     LowestCellCorner two with ECX, caller-popped (ST0),
//     RaiseMajorBridgeAltitude one (ret 4), AddMinorBridge four (ret 0x10),
//     LayBridgeCells two (ret 8), PushBack one (ret 4), InsertOverflow five
//     (ret 0x14), the uninitialized copy and fill cdecl four, the cell
//     vector's copy constructor one (ret 4) and Free none, the cell's copy
//     constructor one (ret 4), the 32-byte vector's copy constructor one (ret
//     4);
//   - part t10's: 0x00631300 one (ret 4), 0x006310F0 five (ret 0x14),
//     0x0062E690 cdecl five;
//   - the game's: the cell entry 0x00633220 one (ret 4), DestroyMajorBridges
//     cdecl three, 0x0062E970 cdecl four (first, last, destination, tag),
//     SC4PtrVector::InsertN five (ret 0x14), SC4Uint32Vector::Clear none,
//     SC4Sint32Map::Find two (ret 8), SC4List::Clear2 and Destroy2 none,
//     SC4::stl_allocate and SC4::Free cdecl one;
//   - the tool's slot 0xF4 (61 AllowBridges) none (AL);
//   - the terrain (+0x44, vtable 0x00AB3D18): 0x30 one (ST0), 0x44 three,
//     0x48 two (ST0), 0x88 two (AL); the occupant manager (+0x48): 0xA0 five
//     (AL: the list, two corners, a filter, -1), 0xC0 one; the list node
//     allocator: 0xC one (EAX), 0x10 one.
#include "core/nettool_occupants.h"
#include "core/nettool_sim.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim {
namespace {

using occupants::Bits;
using occupants::Cells;
using occupants::Entry;
using occupants::Float;
using occupants::ListNode;
using occupants::Row;
using occupants::Steps;
using occupants::Terrain;
using occupants::ToolRow;

// ====================================================================================
// Layouts
// ====================================================================================

// Part t10's functions this part calls.
constexpr std::uintptr_t kMakeBridgeCellsT10 = addr::cSC4NetworkTool_BuildMajorBridgeCells; // ret 4
constexpr std::uintptr_t kInsertBridgeCellsT10 = addr::SC4BridgeCellVector_InsertOverflow;  // ret 0x14
constexpr std::uintptr_t kShapePiersT10 = addr::SC4BridgeCell_RaiseWallsAlongCurve;         // cdecl five

// The tool's fields this part reads beyond nettool_sim.h's.
namespace bfield {
inline constexpr std::size_t kRegularSteps = 0x74;    // the regular step count
inline constexpr std::size_t kDirection = 0x7C;       // the drag's direction (0..7)
inline constexpr std::size_t kBridgesEnd = 0xDC;      // the major bridges' end
inline constexpr std::size_t kVertexOffsetsA = 0x250; // by direction & 3 (part t6's)
inline constexpr std::size_t kVertexOffsetsB = 0x270;
inline constexpr std::size_t kCellSize = 0x290;       // params +0x0: a float (the cell's size)
inline constexpr std::size_t kMinDip = 0x2CC;         // params +0x3C: a float
inline constexpr std::size_t kMaxDeckOffset = 0x2D0;  // params +0x40: a float
inline constexpr std::size_t kByKind99 = 0x33C, kByKind98 = 0x348; // ListBridgeKinds' (uint32 vectors)
} // namespace bfield

// A bridge record (0x178 bytes, the type row's +0x30) as this part reads it.
namespace brec {
inline constexpr std::size_t kSize = 0x178;
inline constexpr std::size_t kMinLength = 0x4, kMaxLength = 0x8, kCellsPerStep = 0xC;
inline constexpr std::size_t kDeckOverWater = 0x5C, kDeckOverLand = 0x78;
inline constexpr std::size_t kWord9C = 0x9C, kWordA0 = 0xA0, kWordA4 = 0xA4;
inline constexpr std::size_t kMinDepth = 0xC8, kMaxDepth = 0xCC, kDeckOffset = 0xD4;
inline constexpr std::size_t kLayouts = 0x16C; // the int map's head (its +0x4 the root)
} // namespace brec

// A major bridge (+0xD8, 0x28 bytes).
namespace major {
inline constexpr std::size_t kSize = 0x28, kType = 0x0, kFirst = 0x4, kLast = 0x8, kRecord = 0xC;
inline constexpr std::size_t kAltitude = 0x10, kHeight = 0x14, kMinor = 0x18, kCells = 0x1C;
inline constexpr std::size_t kCellsEnd = 0x20;
} // namespace major

// A bridge cell (0x50 bytes, the bridge's +0x1C).
namespace bcell {
inline constexpr std::size_t kSize = 0x50, kWord0 = 0x0, kX = 0x4, kZ = 0x8, kPiece = 0xC, kTurn = 0x10;
inline constexpr std::size_t kCorners = 0x14, kDeck = 0x24, kFloat28 = 0x28, kPiers = 0x2C, kWord3C = 0x3C;
inline constexpr std::size_t kFlags = 0x40, kVector = 0x44;
} // namespace bcell

// A layout's piece (20 bytes, the bridge parser's element) and the layout
// (the int map node's +0x14: the piece vector, then the float vector).
namespace piece {
inline constexpr std::size_t kSize = 0x14, kPosition = 0x0, kId = 0x4, kTurn = 0x8, kFloat = 0xC, kKind = 0x10;
} // namespace piece
namespace layout {
inline constexpr std::size_t kPieces = 0x0, kHeights = 0xC;
inline constexpr std::size_t kInNode = 0x14; // the value's part a layout pointer starts at
} // namespace layout

// The int map's node (the record's +0x16C; part r3's intmap).
namespace imap {
inline constexpr std::size_t kRoot = 0x4, kLeft = 0x8, kRight = 0xC, kKey = 0x10;
} // namespace imap

// Slots.
namespace bslot {
inline constexpr std::size_t kAllowBridges = 0xF4;                         // the tool's slot 61
inline constexpr std::size_t kVertexAltitude = 0x30, kCornerAltitudes = 0x44; // the terrain's
inline constexpr std::size_t kCellAltitude = 0x48, kUnderWater = 0x88;
inline constexpr std::size_t kGetByCityCells = 0xA0, kReleaseOccupantList = 0xC0; // the occupant manager's
inline constexpr std::size_t kAllocate = 0xC, kDeallocate = 0x10;           // the list node allocator's
} // namespace bslot

constexpr uint32_t kOneBits = 0x3F800000;
constexpr uint32_t kLowestBits = 0xFF7FFFFF, kHighestBits = 0x7F7FFFFF;
constexpr int32_t kNoBest = 0x7FFFFFFF;
constexpr uint32_t kFarBehind = 100000; // ExtendBridgeSpan's penalty for a span too short

// ====================================================================================
// Helpers
// ====================================================================================

// A step (12 bytes) by index, 32-bit arithmetic as the game's `lea`.
inline char* StepAt(char* self, int32_t s)
{
    return reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(Steps(self)) +
                                   static_cast<uint32_t>(s) * static_cast<uint32_t>(step::kSize));
}
inline uint32_t StepFirst(char* self, int32_t s) { return At<uint32_t>(StepAt(self, s), 0); }
inline uint32_t StepLast(char* self, int32_t s) { return At<uint32_t>(StepAt(self, s), step::kLastCell); }
inline int32_t StepIrregular(char* self, int32_t s) { return At<int32_t>(StepAt(self, s), step::kIrregular); }
inline char* CellAt(char* self, uint32_t c) { return Cells(self) + c * static_cast<uint32_t>(kPointSize); }
inline uint32_t CellX(char* self, uint32_t c) { return At<uint32_t>(CellAt(self, c), 0); }
inline uint32_t CellZ(char* self, uint32_t c) { return At<uint32_t>(CellAt(self, c), 4); }

inline char* RecordAt(char* records, uint32_t index)
{
    return reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(records) +
                                   index * static_cast<uint32_t>(brec::kSize));
}

inline double CellAltitude(char* self, uint32_t x, uint32_t z)
{
    return Virtual<double>(Terrain(self), bslot::kCellAltitude, x, z);
}
inline uint8_t UnderWater(char* self, uint32_t x, uint32_t z)
{
    return Virtual<uint8_t>(Terrain(self), bslot::kUnderWater, x, z);
}
inline double VertexAltitude(char* self, uint32_t vertex)
{
    return Virtual<double>(Terrain(self), bslot::kVertexAltitude, vertex);
}

inline float F(uint32_t bits) { return std::bit_cast<float>(bits); }
inline double D(uint32_t bits) { return static_cast<double>(std::bit_cast<float>(bits)); }
// The x87's `fld a; fcomp b; test ah, 5; jnp`: a < b, ordered.
inline bool Below(uint32_t a, uint32_t b) { return F(a) < F(b); }

// The CDQ / XOR / SUB absolute value (INT_MIN stays itself).
inline int32_t CdqAbs(int32_t v)
{
    const uint32_t sign = static_cast<uint32_t>(v >> 31);
    return static_cast<int32_t>((static_cast<uint32_t>(v) ^ sign) - sign);
}

// A new empty list of the game's (the head from the list node allocator, its
// next and previous itself), as the game inlines it.
inline ListNode* NewList()
{
    ListNode* const head = Virtual<ListNode*>(ListNodeAllocator(), bslot::kAllocate, uint32_t{sizeof(ListNode)});
    head->next = head;
    head->previous = head;
    return head;
}

// Calls a game function that takes ESI and five cdecl arguments (CellsAreFree
// 0x0062FDB0); AL its answer.
__declspec(naked) uint32_t __cdecl CallWithEsi5(std::uintptr_t /*fn*/, void* /*esi*/, uint32_t /*a*/, uint32_t /*b*/,
                                                uint32_t /*c*/, uint32_t /*d*/, uint32_t /*e*/)
{
    __asm {
        push esi
        mov esi, [esp + 12]
        push dword ptr [esp + 32]
        push dword ptr [esp + 32]
        push dword ptr [esp + 32]
        push dword ptr [esp + 32]
        push dword ptr [esp + 32]
        call dword ptr [esp + 28]
        add esp, 20
        pop esi
        ret
    }
}

// Calls a game function that takes ECX and two stack arguments it leaves to
// the caller, and gives ST0 (LowestCellCorner 0x0062E630).
__declspec(naked) float __cdecl CallEcxPop2(std::uintptr_t /*fn*/, void* /*ecx*/, uint32_t /*a*/, uint32_t /*b*/)
{
    __asm {
        mov ecx, [esp + 8]
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        call dword ptr [esp + 12]
        add esp, 8
        ret
    }
}

// CellsAreFree for a step: the occupant manager in ESI, the tool's filter
// (+0x364's pointer), the step's first and last cells.
uint8_t StepCellsAreFree(char* self, int32_t s)
{
    const uint32_t first = StepFirst(self, s), last = StepLast(self, s);
    return static_cast<uint8_t>(CallWithEsi5(addr::cSC4NetworkTool_CellsAreFree, At<void*>(self, field::kOccupants),
                                             At<uint32_t>(self, field::kFilter), CellX(self, first),
                                             CellZ(self, first), CellX(self, last), CellZ(self, last)));
}

inline uint8_t StepFits(char* self, int32_t s, uint32_t type)
{
    return CallThis<uint8_t, int32_t, uint32_t>(addr::cSC4NetworkTool_StepCellsFitBridge, self, s, type);
}

// A major bridge's words and byte copied (the game's inline copy of 0x28
// elements: six words, the byte +0x18, then the cell vector's copy
// constructor 0x0062FB00).
inline void CopyMajorBridge(char* to, const char* from)
{
    for (std::size_t at = 0; at < major::kMinor; at += 4)
        At<uint32_t>(to, at) = At<uint32_t>(const_cast<char*>(from), at);
    At<uint8_t>(to, major::kMinor) = At<uint8_t>(const_cast<char*>(from), major::kMinor);
    CallThis<char*, const char*>(addr::SC4BridgeCellVector_CopyConstruct, to + major::kCells, from + major::kCells);
}

// ====================================================================================
// The containers
// ====================================================================================

// 0x00624EA0 (ret 4: the source vector; this a new vector of 32-byte
// elements; called by the bridge cell's copy constructor): memory for the
// source's count (none for 0), begin, end and capacity, then the range
// copied (0x0062E970: first, last, the memory, the address of the argument
// slot as an unread tag) and the end its answer. EAX this.
char* __fastcall Vector32CopyConstruct(char* self, void*, char* source)
{
    static bool logged = false;
    FirstCall("Vector32CopyConstruct", logged);
    Vector& v = At<Vector>(self, 0);
    const int32_t count = static_cast<int32_t>(At<Vector>(source, 0).end - At<Vector>(source, 0).begin) >> 5;
    v.begin = v.end = v.capacity = nullptr;
    char* const memory =
        count != 0 ? CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, static_cast<uint32_t>(count) << 5) : nullptr;
    v.begin = memory;
    v.end = memory;
    v.capacity = memory + (static_cast<uint32_t>(count) << 5);
    char* const tag = reinterpret_cast<char*>(&source);
    v.end = CallCdecl<char*, char*, char*, char*, char*>(addr::SC4Vector32_UninitializedCopy,
                                                         At<Vector>(source, 0).begin, At<Vector>(source, 0).end,
                                                         memory, tag);
    return self;
}

// 0x006267D0 (ret 4: the source; this a new bridge cell; called by
// 0x0062A420, 0x0062F6B0, 0x0062FB00, 0x006310F0, 0x006312C0 and
// LayBridgeCells): the words +0x0 to +0xC, the byte +0x10, the words +0x14 to
// +0x3C, the byte +0x40 (the padding not copied), the vector +0x44 copy
// constructed (0x00624EA0). EAX this.
char* __fastcall BridgeCellCopyConstruct(char* self, void*, char* source)
{
    static bool logged = false;
    FirstCall("BridgeCellCopyConstruct", logged);
    for (std::size_t at = 0; at < bcell::kTurn; at += 4)
        At<uint32_t>(self, at) = At<uint32_t>(source, at);
    At<uint8_t>(self, bcell::kTurn) = At<uint8_t>(source, bcell::kTurn);
    for (std::size_t at = bcell::kCorners; at < bcell::kFlags; at += 4)
        At<uint32_t>(self, at) = At<uint32_t>(source, at);
    At<uint8_t>(self, bcell::kFlags) = At<uint8_t>(source, bcell::kFlags);
    CallThis<char*, char*>(addr::SC4Vector32_CopyConstruct, self + bcell::kVector, source + bcell::kVector);
    return self;
}

// 0x0062BF20 (no arguments; this a vector of bridge cells; called by
// 0x0062C1E0, 0x006310F0 and FindMajorBridges): each cell's vector +0x44
// freed when it has memory, then the vector's memory.
void __fastcall BridgeCellVectorFree(char* self)
{
    static bool logged = false;
    FirstCall("BridgeCellVectorFree", logged);
    char* const end = At<Vector>(self, 0).end;
    for (char* at = At<Vector>(self, 0).begin; at != end; at += bcell::kSize)
        if (void* const memory = At<void*>(at, bcell::kVector))
            CallCdecl<void, void*>(addr::SC4_Free, memory);
    if (char* const memory = At<Vector>(self, 0).begin)
        CallCdecl<void, void*>(addr::SC4_Free, memory);
}

// 0x0062FB00 (ret 4: the source vector; this a new vector of bridge cells;
// called by the major bridges' copies 0x00631200, 0x00631260, InsertOverflow
// and PushBack): memory for the source's count (the size / 0x50, signed;
// none for 0), each cell copy constructed (0x006267D0) where the memory is
// not null, the end after the last. EAX this.
char* __fastcall BridgeCellsCopyConstruct(char* self, void*, char* source)
{
    static bool logged = false;
    FirstCall("BridgeCellsCopyConstruct", logged);
    Vector& v = At<Vector>(self, 0);
    const int32_t count = static_cast<int32_t>(At<Vector>(source, 0).end - At<Vector>(source, 0).begin) /
                          static_cast<int32_t>(bcell::kSize);
    v.begin = v.end = v.capacity = nullptr;
    const uint32_t bytes = static_cast<uint32_t>(count) * static_cast<uint32_t>(bcell::kSize);
    char* const memory = count != 0 ? CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, bytes) : nullptr;
    v.begin = memory;
    v.end = memory;
    v.capacity = memory + bytes;
    char* const last = At<Vector>(source, 0).end;
    char* to = memory;
    for (char* from = At<Vector>(source, 0).begin; from != last; from += bcell::kSize, to += bcell::kSize)
        if (to)
            CallThis<char*, char*>(addr::SC4BridgeCell_CopyConstruct, to, from);
    v.end = to;
    return self;
}

// 0x00631200 (cdecl: first, last, the destination, an unread tag; called by
// InsertOverflow): the major bridges of [first, last) copied into
// uninitialized memory (skipped at a null destination); EAX the end.
char* __cdecl MajorBridgeUninitializedCopy(char* first, char* last, char* destination)
{
    static bool logged = false;
    FirstCall("MajorBridgeUninitializedCopy", logged);
    for (; first != last; first += major::kSize, destination += major::kSize)
        if (destination)
            CopyMajorBridge(destination, first);
    return destination;
}

// 0x00631260 (cdecl: the destination, a count, the value, an unread tag;
// called by InsertOverflow): the count copies of the value (skipped at a
// null destination); EAX the end.
char* __cdecl MajorBridgeUninitializedFill(char* destination, uint32_t count, const char* value)
{
    static bool logged = false;
    FirstCall("MajorBridgeUninitializedFill", logged);
    if (count == 0)
        return destination;
    do {
        if (destination)
            CopyMajorBridge(destination, value);
        destination += major::kSize;
    } while (--count);
    return destination;
}

// 0x006323E0 (ret 0x14: where, the value, an unread tag, a count, an "at
// the end" byte; this the major bridges' vector; called by PushBack): the
// STL insert that outgrows the capacity: size + max(size, count) elements
// (unsigned; none for 0), [begin, where) copied, the value once (inline) or
// the count times (0x00631260), [where, end) copied unless at the end, the
// old elements destroyed (0x0062C590) and freed. The tag every call gets is
// the address of the fifth argument's slot. EAX the new capacity's end.
char* __fastcall MajorBridgesInsertOverflow(char* self, void*, char* where, const char* value, void*, uint32_t count,
                                            uint32_t atEnd)
{
    static bool logged = false;
    FirstCall("MajorBridgesInsertOverflow", logged);
    Vector& v = At<Vector>(self, 0);
    const uint32_t size =
        static_cast<uint32_t>(static_cast<int32_t>(v.end - v.begin) / static_cast<int32_t>(major::kSize));
    const uint32_t capacity = size + (size < count ? count : size);
    char* const memory =
        capacity != 0
            ? CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, capacity * static_cast<uint32_t>(major::kSize))
            : nullptr;
    void* const tag = &atEnd;
    char* to = CallCdecl<char*, char*, char*, char*, void*>(addr::SC4MajorBridge_UninitializedCopy, v.begin, where,
                                                             memory, tag);
    if (count == 1) {
        if (to)
            CopyMajorBridge(to, value);
        to += major::kSize;
    } else {
        to = CallCdecl<char*, char*, uint32_t, const char*, void*>(addr::SC4MajorBridge_UninitializedFill, to, count,
                                                                   value, tag);
    }
    if (!static_cast<uint8_t>(atEnd))
        to = CallCdecl<char*, char*, char*, char*, void*>(addr::SC4MajorBridge_UninitializedCopy, where, v.end, to,
                                                          tag);
    CallCdecl<void, char*, char*, void*>(addr::cSC4NetworkTool_DestroyMajorBridges, v.begin, v.end, tag);
    if (v.begin)
        CallCdecl<void, void*>(addr::SC4_Free, v.begin);
    v.begin = memory;
    v.end = to;
    v.capacity = memory + capacity * static_cast<uint32_t>(major::kSize);
    return v.capacity;
}

// 0x00632510 (ret 4: the value; this the major bridges' vector; called by
// AddMinorBridge and FindMajorBridges): in place when there is room
// (nothing written at a null end, the end moved on all the same; EAX the
// old end), else InsertOverflow (the end, the value, the argument slot's
// address as the tag, 1, 1; EAX its answer).
char* __fastcall MajorBridgesPushBack(char* self, void*, const char* value)
{
    static bool logged = false;
    FirstCall("MajorBridgesPushBack", logged);
    Vector& v = At<Vector>(self, 0);
    char* const end = v.end;
    if (end != v.capacity) {
        if (end)
            CopyMajorBridge(end, value);
        v.end += major::kSize;
        return end;
    }
    return CallThis<char*, char*, const char*, void*, uint32_t, uint32_t>(
        addr::SC4MajorBridgeVector_InsertOverflow, self, end, value, &value, 1u, 1u);
}

// ====================================================================================
// The tests a bridge's steps must pass
// ====================================================================================

// 0x0062E630 (the thunk below: ECX the terrain, two stack arguments x and z
// the caller pops; called by FitBridgeSpan): the least of the cell's four
// corner heights (terrain slot 0x44), in ST0.
float __cdecl LowestCellCorner(void* terrain, uint32_t x, uint32_t z)
{
    static bool logged = false;
    FirstCall("LowestCellCorner", logged);
    uint32_t corners[4] = {};
    Virtual<void>(terrain, bslot::kCornerAltitudes, x, z, reinterpret_cast<float*>(corners));
    // `test ah, 5; jnp`: the first when it is below the second (ordered),
    // else the second.
    const uint32_t* const high = Below(corners[3], corners[2]) ? &corners[3] : &corners[2];
    const uint32_t* const low = Below(corners[1], corners[0]) ? &corners[1] : &corners[0];
    return F(Below(*high, *low) ? *high : *low);
}

__declspec(naked) void LowestCellCornerThunk()
{
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push ecx
        call LowestCellCorner
        add esp, 12
        ret
    }
}

// 0x0062FDB0 (the thunk below: ESI the occupant manager; cdecl five: a
// network occupant filter, x1, z1, x2, z2; called by FitBridgeSpan,
// FindBridgeSpan and FindMajorBridges): AL 1 when the occupant manager's
// slot 0xA0 finds no occupant through the filter in the rectangle around
// the two cells one cell wider on every side (signed min and max), or when
// the query fails. The list comes from the list node allocator, is handed
// back through slot 0xC0 when the query ran, and destroyed.
uint8_t __cdecl CellsAreFree(void* occupants, uint32_t filter, int32_t x1, int32_t z1, int32_t x2, int32_t z2)
{
    static bool logged = false;
    FirstCall("CellsAreFree", logged);
    const int32_t low[2] = {(x2 < x1 ? x2 : x1) - 1, (z2 < z1 ? z2 : z1) - 1};
    const int32_t high[2] = {(x1 < x2 ? x2 : x1) + 1, (z1 < z2 ? z2 : z1) + 1};
    ListNode* list = NewList();
    if (Virtual<uint8_t>(occupants, bslot::kGetByCityCells, &list, low, high, filter, 0xFFFFFFFFu)) {
        const bool any = list->next != list;
        Virtual<void>(occupants, bslot::kReleaseOccupantList, &list);
        CallThis<void>(addr::SC4List_Destroy2, &list);
        return any ? 0 : 1;
    }
    CallThis<void>(addr::SC4List_Destroy2, &list);
    return 1;
}

__declspec(naked) void CellsAreFreeThunk()
{
    __asm {
        push dword ptr [esp + 20]
        push dword ptr [esp + 20]
        push dword ptr [esp + 20]
        push dword ptr [esp + 20]
        push dword ptr [esp + 20]
        push esi
        call CellsAreFree
        add esp, 24
        ret
    }
}

// 0x0062EB80 (ret 8: a step, a network type; called by ExtendBridgeSpan,
// FitBridgeSpan, FindBridgeSpan, FindMajorBridges and FitMinorBridges): AL 1
// when every cell of the step either has no cell info (0x00633220) or one
// whose network flags are only the type's (0x00AA88B8) and whose edge word
// for the type stays within the step's mask: the drag's direction (+0x7C bit
// 1: diagonal) and the cell's place (a one-cell step, its first cell, its
// last, or a middle one) pick it. The step is read through a pointer taken
// once, the cells' vector and the step's last cell each time.
uint8_t __fastcall StepCellsFitBridge(char* self, void*, int32_t s, uint32_t type)
{
    static bool logged = false;
    FirstCall("StepCellsFitBridge", logged);
    char* const at = StepAt(self, s);
    uint32_t c = At<uint32_t>(at, 0);
    if (c > At<uint32_t>(at, step::kLastCell))
        return 1;
    do {
        char* const info = Entry(self, (CellZ(self, c) << 16) + CellX(self, c));
        if (info) {
            const uint32_t flag =
                Global<uint32_t>(static_cast<uint32_t>(addr::cSC4NetworkTool_StepTypeFlags + type * 4));
            if ((~flag & At<uint32_t>(info, cell::kFlags)) & cell::kOccupantFlagMask)
                return 0;
            const uint32_t first = At<uint32_t>(at, 0), last = At<uint32_t>(at, step::kLastCell);
            uint32_t mask;
            if (At<uint8_t>(self, bfield::kDirection) & 2) {
                if (last == first)
                    mask = 0x2000200;
                else if (c == first)
                    mask = 0x2040200;
                else
                    mask = c != last ? 0x2040204u : 0x02000204u;
            } else {
                if (last == first)
                    mask = 0x20002;
                else if (c == first)
                    mask = 0x4020002;
                else
                    mask = c != last ? 0x4020402u : 0x00020402u;
            }
            if (~mask & At<uint32_t>(info, cell::kByType + type * 4))
                return 0;
        }
        ++c;
    } while (c <= At<uint32_t>(at, step::kLastCell));
    return 1;
}

// 0x0062ECD0 (ret 0x10: a wanted length in steps, the first step's address,
// the last step's address, a network type; called by PickBridgeRecord): the
// span [*first, *last] grown to the length: backwards while *first - 1 is
// above 1 and that step and the one before it are regular and fit
// (StepCellsFitBridge), forwards while *last + 1 is below the valid step
// count and that step is regular and fits; a direction that fails stops.
// AL 1 when the span already had the length; else AL (EAX) 1 when the grown
// span's last step is below the regular step count (+0x74) and the span has
// the length, else 0.
uint8_t __fastcall ExtendBridgeSpan(char* self, void*, int32_t wanted, int32_t* first, int32_t* last, uint32_t type)
{
    static bool logged = false;
    FirstCall("ExtendBridgeSpan", logged);
    const int32_t length = *last - *first + 1;
    if (length >= wanted)
        return 1;
    uint8_t backwards = *first > 1 ? 1 : 0; // the game's: the low byte of its third argument's slot
    uint8_t forwards = 1;
    int32_t need = wanted - length;
    while (need > 0) {
        if (backwards) {
            const int32_t at = --*first;
            if (at > 1 && StepIrregular(self, at) == 0 && StepIrregular(self, at - 1) == 0 &&
                CallThis<uint8_t, int32_t, uint32_t>(addr::cSC4NetworkTool_StepCellsFitBridge, self, at, type)) {
                --need;
            } else {
                ++*first;
                backwards = 0;
            }
        } else if (!forwards) {
            return 0;
        }
        if (need <= 0)
            break;
        if (forwards) {
            const int32_t at = ++*last;
            if (at >= At<int32_t>(self, field::kValidSteps) || StepIrregular(self, at) != 0 ||
                !CallThis<uint8_t, int32_t, uint32_t>(addr::cSC4NetworkTool_StepCellsFitBridge, self, at, type)) {
                --*last;
                forwards = 0;
            } else {
                --need;
            }
        }
    }
    const int32_t end = *last;
    if (end >= At<int32_t>(self, bfield::kRegularSteps))
        return 0;
    return end - *first + 1 < wanted ? 0 : 1;
}

// 0x0062EA90 (ret 4: a major bridge; called by FindMajorBridges): its
// altitude (+0x10) set to the tool's altitude override (+0x204), then for
// each of its steps [first, last) raised to the last cell's height (terrain
// slot 0x48) plus the record's deck height over water (+0x5C, terrain slot
// 0x88 says the cell is under water) or over land (+0x78) when that is
// above it.
void __fastcall RaiseMajorBridgeAltitude(char* self, void*, char* bridge)
{
    static bool logged = false;
    FirstCall("RaiseMajorBridgeAltitude", logged);
    char* const record =
        RecordAt(At<char*>(ToolRow(self), row::kBridges), At<uint32_t>(bridge, major::kRecord));
    int32_t s = At<int32_t>(bridge, major::kFirst);
    At<uint32_t>(bridge, major::kAltitude) = At<uint32_t>(self, field::kAltitudeOverride);
    if (s >= At<int32_t>(bridge, major::kLast))
        return;
    // The game's FLD of its own argument slot: the bridge's address as a
    // float, compared only after a step without cells.
    double value = D(static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(bridge)));
    for (; s < At<int32_t>(bridge, major::kLast); ++s) {
        uint32_t c = StepFirst(self, s);
        if (c <= StepLast(self, s)) {
            do {
                const uint8_t water = UnderWater(self, CellX(self, c), CellZ(self, c));
                const double h = CellAltitude(self, CellX(self, c), CellZ(self, c));
                value = h + static_cast<double>(At<float>(record, water ? brec::kDeckOverWater : brec::kDeckOverLand));
                ++c;
            } while (c <= StepLast(self, s));
        }
        // `fcom; test ah, 0x41; jne`: stored only when above (ordered).
        if (value > static_cast<double>(At<float>(bridge, major::kAltitude)))
            At<float>(bridge, major::kAltitude) = static_cast<float>(value);
    }
}

// ====================================================================================
// Picking a bridge
// ====================================================================================

// 0x0062F760 (ret 0x14: the first step's address, the last step's address,
// a byte (1: the water records +0x33C, 0: the land records +0x348), a depth
// (float), a network type; called by FindBridgeSpan and AddMinorBridge):
// the records of that list (only the preferred bridge +0x200 when it is not
// negative) whose depth range (+0xC8, +0xCC) holds the depth and whose
// longest span (+0x8) is not below the span's length, and whose shortest
// (+0x4) the span has or can be grown to (ExtendBridgeSpan on copies of the
// two steps) are collected; none: -1. Else the one whose shortest span is
// nearest the length (a shortest longer than the span counts 100000 more
// steps away, the first of equals kept), the span grown to it when it is
// longer (ExtendBridgeSpan on the caller's steps), the collected indices
// become the possible bridges (+0x1F4, the old memory freed). EAX the index.
int32_t __fastcall PickBridgeRecord(char* self, void*, int32_t* first, int32_t* last, uint32_t water,
                                    uint32_t depthBits, uint32_t type)
{
    static bool logged = false;
    FirstCall("PickBridgeRecord", logged);
    Vector* const records = &At<Vector>(ToolRow(self), row::kBridges);
    char* const kinds = self + (static_cast<uint8_t>(water) ? bfield::kByKind99 : bfield::kByKind98);
    const int32_t length = static_cast<int32_t>(static_cast<uint32_t>(*last) - static_cast<uint32_t>(*first) + 1u);
    const int32_t count = static_cast<int32_t>(At<Vector>(kinds, 0).end - At<Vector>(kinds, 0).begin) >> 2;
    const float depth = F(depthBits);
    Vector found{nullptr, nullptr, nullptr};
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t index = reinterpret_cast<uint32_t*>(At<Vector>(kinds, 0).begin)[i];
        const int32_t preferred = At<int32_t>(self, field::kPreferredBridge);
        if (preferred >= 0 && index != static_cast<uint32_t>(preferred))
            continue;
        char* const record = RecordAt(records->begin, index);
        if (!(At<float>(record, brec::kMinDepth) <= depth)) // `test ah, 0x41; jp`: ordered <= only
            continue;
        if (!(At<float>(record, brec::kMaxDepth) >= depth)) // `test ah, 1; jne`: below or unordered skips
            continue;
        int32_t a = *first, b = *last;
        if (At<uint32_t>(record, brec::kMaxLength) < static_cast<uint32_t>(length))
            continue;
        if (At<uint32_t>(record, brec::kMinLength) > static_cast<uint32_t>(length) &&
            !CallThis<uint8_t, uint32_t, int32_t*, int32_t*, uint32_t>(addr::cSC4NetworkTool_ExtendBridgeSpan, self,
                                                                       At<uint32_t>(record, brec::kMinLength), &a, &b,
                                                                       type))
            continue;
        if (found.end != found.capacity) {
            if (found.end)
                *reinterpret_cast<uint32_t*>(found.end) = index;
            found.end += 4;
        } else {
            uint32_t value = index;
            uint32_t tag = 0;
            CallThis<void, char*, const uint32_t*, void*, uint32_t, uint32_t>(addr::SC4PtrVector_InsertN, &found,
                                                                              found.end, &value, &tag, 1u, 1u);
        }
    }
    if (found.begin == found.end) {
        if (found.begin)
            CallCdecl<void, void*>(addr::SC4_Free, found.begin);
        return -1;
    }
    const int32_t n = static_cast<int32_t>(found.end - found.begin) >> 2;
    char* const recordsBegin = records->begin;
    int32_t best = kNoBest;
    int32_t bestIndex = -1;
    for (int32_t j = 0; j < n; ++j) {
        const uint32_t index = reinterpret_cast<uint32_t*>(found.begin)[j];
        int32_t difference = static_cast<int32_t>(static_cast<uint32_t>(length) -
                                                  At<uint32_t>(RecordAt(recordsBegin, index), brec::kMinLength));
        if (difference < 0)
            difference = static_cast<int32_t>(static_cast<uint32_t>(difference) - kFarBehind);
        if (CdqAbs(difference) < CdqAbs(best)) {
            best = difference;
            bestIndex = static_cast<int32_t>(index);
        }
    }
    if (best < 0)
        CallThis<uint8_t, uint32_t, int32_t*, int32_t*, uint32_t>(
            addr::cSC4NetworkTool_ExtendBridgeSpan, self,
            At<uint32_t>(RecordAt(recordsBegin, static_cast<uint32_t>(bestIndex)), brec::kMinLength), first, last,
            type);
    Vector& possible = At<Vector>(self, field::kPossibleBridges);
    char* const old = possible.begin;
    possible = found;
    if (old)
        CallCdecl<void, void*>(addr::SC4_Free, old);
    return bestIndex;
}

// The highest of the step's cells' least corners: the inline form (terrain
// slot 0x44 and the three compares, bits moved), as FitBridgeSpan's second
// pass does it.
uint32_t HighestLowCornerInline(char* self, int32_t s)
{
    uint32_t top = 0;
    for (uint32_t c = StepFirst(self, s); c <= StepLast(self, s); ++c) {
        uint32_t corners[4] = {};
        Virtual<void>(Terrain(self), bslot::kCornerAltitudes, CellX(self, c), CellZ(self, c),
                      reinterpret_cast<float*>(corners));
        const uint32_t* const high = Below(corners[3], corners[2]) ? &corners[3] : &corners[2];
        const uint32_t* const low = Below(corners[1], corners[0]) ? &corners[1] : &corners[0];
        const uint32_t least = Below(*high, *low) ? *high : *low;
        top = Below(top, least) ? least : top;
    }
    return top;
}

// The highest cell height (terrain slot 0x48, stored as float) of a step.
uint32_t HighestCell(char* self, int32_t s)
{
    uint32_t top = 0;
    for (uint32_t c = StepFirst(self, s); c <= StepLast(self, s); ++c) {
        const uint32_t h = Bits(static_cast<float>(CellAltitude(self, CellX(self, c), CellZ(self, c))));
        top = Below(top, h) ? h : top;
    }
    return top;
}

// 0x0062FED0 (ret 0x14: the first step's address, the last step's address,
// the depth's address, the height's address, a network type; called by
// FindBridgeSpan): the span's deck height and the water's depth under it.
// Before the first step (down to step 2) and after the last (up to the
// valid count less 2), while the steps are regular, free (CellsAreFree) and
// fit: the highest of each step's cells' least corners against the highest
// cell of the step beyond it; a rise above 14 cells' size and above 4 cells'
// size each gives a candidate (after the span the second stops measuring
// when not above). The height: the lesser of the two low candidates and the
// greater of the two high ones, the lesser of those; the altitude override
// instead when it is not below 0. The span then grows backwards to the step
// where measuring stopped, and forwards to where it stopped, while the next
// step's highest cell is not at or above the height; a height below 0.01
// becomes the first cell's. The depth: the largest (height - cell height)
// of the first water cell of each step of the span. AL 1.
uint8_t __fastcall FitBridgeSpan(char* self, void*, int32_t* first, int32_t* last, float* depth, float* height,
                                 uint32_t type)
{
    static bool logged = false;
    FirstCall("FitBridgeSpan", logged);
    const double size = static_cast<double>(At<float>(self, bfield::kCellSize));
    const float limitHigh =
        static_cast<float>(size * static_cast<double>(Global<float>(addr::cSC4NetworkTool_BridgeRiseFactor)));
    const float limitLow = static_cast<float>(size * static_cast<double>(Global<float>(addr::SC4_FloatFour)));

    // Before the span.
    uint32_t beforeHigh = 0, beforeLow = 0;
    int32_t lowest = 1;
    int32_t s = *first;
    if (s > 1) {
        for (;;) {
            if (StepIrregular(self, s) || StepIrregular(self, s - 1) || StepIrregular(self, s - 2) ||
                !StepCellsAreFree(self, s) || !StepFits(self, s, type)) {
                lowest = s;
                break;
            }
            uint32_t top = 0;
            for (uint32_t c = StepFirst(self, s); c <= StepLast(self, s); ++c) {
                const uint32_t v = Bits(CallEcxPop2(addr::cSC4NetworkTool_LowestCellCorner, Terrain(self),
                                                    CellX(self, c), CellZ(self, c)));
                top = Below(top, v) ? v : top;
            }
            const uint32_t ground = HighestCell(self, s - 1);
            const double rise = D(ground) - D(top);
            if (rise > static_cast<double>(limitHigh)) // `test ah, 0x41; jne`: above, ordered
                beforeHigh = ground;
            if (rise > static_cast<double>(limitLow))
                beforeLow = ground;
            --s;
            if (s <= 1)
                break;
        }
    }

    // After the span.
    int32_t limit = At<int32_t>(self, field::kValidSteps) - 2;
    uint32_t afterHigh = 0, afterLow = 0;
    uint8_t stop = 0;
    int32_t e = *last;
    if (e < limit) {
        for (;;) {
            if (StepIrregular(self, e) || StepIrregular(self, e + 1) || StepIrregular(self, e + 2) ||
                !StepCellsAreFree(self, e) || !StepFits(self, e, type)) {
                limit = e;
                break;
            }
            if (!stop) {
                const uint32_t top = HighestLowCornerInline(self, e);
                const uint32_t ground = HighestCell(self, e + 1);
                const double rise = D(ground) - D(top);
                if (rise > static_cast<double>(limitHigh))
                    afterHigh = ground;
                if (rise > static_cast<double>(limitLow))
                    afterLow = ground;
                else
                    stop = 1;
            }
            ++e;
            if (e >= limit)
                break;
        }
    }

    // The height.
    const uint32_t lowCap = Below(afterLow, beforeLow) ? afterLow : beforeLow;
    const uint32_t highCap = Below(beforeHigh, afterHigh) ? afterHigh : beforeHigh;
    At<uint32_t>(height, 0) = Below(lowCap, highCap) ? lowCap : highCap;
    // `test ah, 1; jne`: the override is taken when not below 0 (ordered).
    if (At<float>(self, field::kAltitudeOverride) >= Global<float>(addr::SC4_FloatZero))
        At<uint32_t>(height, 0) = At<uint32_t>(self, field::kAltitudeOverride);

    // Grown backwards, then forwards, while the step stays below the height
    // (`test ah, 1; je`: a NaN grows it).
    if (*first > lowest) {
        do {
            if (F(HighestCell(self, *first)) >= *height)
                break;
            --*first;
        } while (*first > lowest);
    }
    if (*last < limit) {
        do {
            if (F(HighestCell(self, *last)) >= *height)
                break;
            ++*last;
        } while (*last < limit);
    }

    // `test ah, 5; jp`: below 0.01 (a double), ordered.
    if (static_cast<double>(*height) < Global<double>(addr::cSC4NetworkTool_BridgeWaterMargin)) {
        const uint32_t c = StepFirst(self, *first);
        *height = static_cast<float>(CellAltitude(self, CellX(self, c), CellZ(self, c)));
    }

    // The depth.
    At<uint32_t>(depth, 0) = 0;
    for (int32_t k = *first; k < *last; ++k) {
        for (uint32_t c = StepFirst(self, k); c <= StepLast(self, k); ++c) {
            const uint32_t x = CellX(self, c), z = CellZ(self, c);
            if (!UnderWater(self, x, z))
                continue;
            const uint32_t d =
                Bits(static_cast<float>(static_cast<double>(*height) - CellAltitude(self, x, z)));
            At<uint32_t>(depth, 0) = Below(At<uint32_t>(depth, 0), d) ? d : At<uint32_t>(depth, 0);
            break;
        }
    }
    return 1;
}

// 0x00630570 (ret 0x18: a step, the first step's address, the last step's
// address, the record index's address, the height's address, a network
// type; called by FindMajorBridges): the span around the step: the step
// before it (regular, fitting) is the first; the last moves on while its
// step is regular, free and fitting and has a water cell (stopping at the
// valid count less 1: AL 0); the first dry step ends it, and must be below
// the regular count. Then FitBridgeSpan (the depth into the sixth
// argument's slot in the game, a local in ours) and PickBridgeRecord (the
// water records, that depth); the index stored. AL 1 when it is not
// negative.
uint8_t __fastcall FindBridgeSpan(char* self, void*, int32_t n, int32_t* first, int32_t* last, int32_t* index,
                                  float* height, uint32_t type)
{
    static bool logged = false;
    FirstCall("FindBridgeSpan", logged);
    const int32_t before = n - 1;
    *first = before;
    if (before < 0 || StepIrregular(self, before) != 0 || !StepFits(self, before, type))
        return 0;
    *last = n + 1;
    if (n + 1 >= At<int32_t>(self, field::kValidSteps) - 1)
        return 0;
    for (;;) {
        const int32_t e = *last;
        if (StepIrregular(self, e) != 0 || !StepCellsAreFree(self, e) || !StepFits(self, e, type))
            return 0;
        bool water = false;
        for (uint32_t c = StepFirst(self, *last); c <= StepLast(self, *last); ++c)
            if (UnderWater(self, CellX(self, c), CellZ(self, c))) {
                water = true;
                break;
            }
        if (!water)
            break;
        const int32_t next = *last + 1;
        *last = next;
        if (next >= At<int32_t>(self, field::kValidSteps) - 1)
            return 0;
    }
    if (*last >= At<int32_t>(self, bfield::kRegularSteps))
        return 0;
    uint32_t depthBits = 0;
    CallThis<uint8_t, int32_t*, int32_t*, uint32_t*, float*, uint32_t>(addr::cSC4NetworkTool_FitBridgeSpan, self,
                                                                       first, last, &depthBits, height, type);
    const int32_t picked = CallThis<int32_t, int32_t*, int32_t*, uint32_t, uint32_t, uint32_t>(
        addr::cSC4NetworkTool_PickBridgeRecord, self, first, last, 1u, depthBits, type);
    *index = picked;
    return picked >= 0 ? 1 : 0;
}

// ====================================================================================
// Laying a bridge's cells
// ====================================================================================

// 0x00632060 (ret 8: a major bridge, a layout (the record's int map node +
// 0x14); called by AddMinorBridge and FindMajorBridges): for each step of
// the bridge, each of its cells in the drag's order (backwards for
// directions whose (direction / 2 + 3) % 4 is 2 or 3) gets a bridge cell
// appended to the bridge's +0x1C (copied from a template, the game's with
// its stack's leftovers, ours zero): the cell, the layout piece's ID, turn
// (+ the bridge's), float and deck height (bridge altitude + position +
// record +0xD4), the four corners from the layout's heights of this step
// and the next (+ the altitude); a pylon or end piece (kind 1 or 2), and
// every cell while a group is pending, gets its pier floats 1.0, +0x3C the
// record's +0xA4 and its pier flags. A step with a kind (the latest
// non-zero kind of any of its cells' pieces) ends a pending group (part
// t10's 0x0062E690 over the group's cells, by the kinds: 1 then 2 with
// +0xA0 and 2; 1 then not 2 with +0x9C and 1; else not 2 with +0xA0 and 0)
// and starts the next unless its kind is 2 after a group.
void __fastcall LayBridgeCells(char* self, void*, char* bridge, char* layout)
{
    static bool logged = false;
    FirstCall("LayBridgeCells", logged);
    char* const record = RecordAt(At<char*>(ToolRow(self), row::kBridges), At<uint32_t>(bridge, major::kRecord));
    const int32_t direction = At<int32_t>(self, bfield::kDirection);
    const uint8_t turn = static_cast<uint8_t>((direction / 2 + 3) % 4); // CDQ/SUB/SAR, AND 0x80000003 and the fix
    uint8_t forward = (turn == 0 || turn == 1) ? 1 : 0;
    const uint32_t wordA0 = At<uint32_t>(record, brec::kWordA0);
    const uint32_t word9C = At<uint32_t>(record, brec::kWord9C);
    int32_t stepNo = 0;
    int32_t pieceNo = 0;
    int32_t pending = -1;
    uint32_t pendingKind = 0; // the game's entry ESP - 0x58, never written before a group starts (ours 0)
    for (int32_t s = At<int32_t>(bridge, major::kFirst); s <= At<int32_t>(bridge, major::kLast); ++s, ++stepNo) {
        uint32_t kind = 0;
        uint32_t c, end;
        int32_t way;
        if (forward) {
            c = StepFirst(self, s);
            end = StepLast(self, s) + 1;
            way = 1;
        } else {
            c = StepLast(self, s);
            end = StepFirst(self, s) - 1;
            way = -1;
        }
        if (c == end)
            continue;
        const uint32_t startCell = c;
        do {
            // The template: the game's has its stack's words in +0x0 to +0x40.
            alignas(4) char local[bcell::kSize] = {};
            Vector& cells = At<Vector>(bridge, major::kCells);
            if (cells.end != cells.capacity) {
                if (cells.end)
                    CallThis<char*, char*>(addr::SC4BridgeCell_CopyConstruct, cells.end, local);
                cells.end += bcell::kSize;
            } else {
                CallThis<void, char*, char*, uint8_t*, uint32_t, uint32_t>(kInsertBridgeCellsT10, &cells, cells.end,
                                                                           local, &forward, 1u, 1u);
            }
            char* const cell = At<char*>(bridge, major::kCellsEnd) - bcell::kSize;
            char* const p = At<char*>(layout, layout::kPieces) +
                            static_cast<uint32_t>(pieceNo) * static_cast<uint32_t>(piece::kSize);
            At<uint32_t>(cell, bcell::kFloat28) = At<uint32_t>(p, piece::kFloat);
            At<uint32_t>(cell, bcell::kX) = CellX(self, c);
            At<uint32_t>(cell, bcell::kZ) = CellZ(self, c);
            At<uint32_t>(cell, bcell::kWord0) = 0;
            At<uint32_t>(cell, bcell::kPiece) = At<uint32_t>(p, piece::kId);
            At<uint8_t>(cell, bcell::kTurn) = static_cast<uint8_t>((At<uint8_t>(p, piece::kTurn) + turn) & 3);
            const double altitude = static_cast<double>(At<float>(bridge, major::kAltitude));
            At<float>(cell, bcell::kDeck) =
                static_cast<float>((altitude + static_cast<double>(At<float>(p, piece::kPosition))) +
                                   static_cast<double>(At<float>(record, brec::kDeckOffset)));
            At<uint8_t>(cell, bcell::kFlags) = 0;
            const float* const heights = At<float*>(layout, layout::kHeights);
            const uint32_t corner =
                Global<uint32_t>(static_cast<uint32_t>(addr::cSC4NetworkTool_LaidCellCornerByTurn + turn * 4u));
            const double here = static_cast<double>(heights[stepNo]) + altitude;
            const double next = static_cast<double>(heights[stepNo + 1]) + altitude;
            At<float>(cell, bcell::kCorners + corner * 4) = static_cast<float>(here);
            At<float>(cell, bcell::kCorners + ((corner + 1) & 3) * 4) = static_cast<float>(here);
            At<float>(cell, bcell::kCorners + ((corner - 2) & 3) * 4) = static_cast<float>(next);
            At<float>(cell, bcell::kCorners + ((corner - 1) & 3) * 4) = static_cast<float>(next);
            if (At<uint32_t>(p, piece::kKind) != 0 || pending >= 0) {
                for (std::size_t at = bcell::kPiers; at < bcell::kWord3C; at += 4)
                    At<uint32_t>(cell, at) = kOneBits;
                At<uint32_t>(cell, bcell::kWord3C) = At<uint32_t>(record, brec::kWordA4);
                uint8_t flags = (turn == 1 || turn == 3) ? 2 : 1;
                At<uint8_t>(cell, bcell::kFlags) = flags;
                if (At<uint32_t>(p, piece::kKind) == 1) {
                    flags |= 4;
                    At<uint8_t>(cell, bcell::kFlags) = flags;
                }
                if (At<uint32_t>(record, brec::kCellsPerStep) > 1) {
                    const uint32_t want = (turn != 0 && turn != 3) ? 0u : 1u;
                    const uint32_t atStart = c == startCell ? 1u : 0u;
                    At<uint8_t>(cell, bcell::kFlags) =
                        static_cast<uint8_t>(At<uint8_t>(cell, bcell::kFlags) | (atStart == want ? 8 : 0x10));
                }
                if (const uint32_t k = At<uint32_t>(p, piece::kKind))
                    kind = k;
            }
            c += static_cast<uint32_t>(way);
            ++pieceNo;
        } while (c != end);
        if (kind == 0)
            continue;
        bool start = true;
        if (pending >= 0) {
            const uint32_t perStep = At<uint32_t>(record, brec::kCellsPerStep);
            const int32_t count = stepNo - pending + 1;
            char* const group = At<char*>(bridge, major::kCells) +
                                perStep * static_cast<uint32_t>(pending) * static_cast<uint32_t>(bcell::kSize);
            if (pendingKind == 1) {
                if (kind == 2) {
                    CallCdecl<void, char*, int32_t, uint32_t, uint32_t, uint32_t>(kShapePiersT10, group, count,
                                                                                  perStep, wordA0, 2u);
                    pending = -1; // 0x00632384 with a group pending
                    start = false;
                } else {
                    CallCdecl<void, char*, int32_t, uint32_t, uint32_t, uint32_t>(kShapePiersT10, group, count,
                                                                                  perStep, word9C, 1u);
                }
            } else if (kind == 2) {
                pending = -1;
                start = false;
            } else {
                CallCdecl<void, char*, int32_t, uint32_t, uint32_t, uint32_t>(kShapePiersT10, group, count, perStep,
                                                                              wordA0, 0u);
            }
        }
        if (start) {
            // 0x0063237F / 0x00632384 without a group: a new one at this step.
            pending = stepNo;
            pendingKind = kind;
        }
    }
}

// ====================================================================================
// The bridge passes
// ====================================================================================

// 0x00632580 (ret 0x10: the first step, the last step, a height (float), a
// network type; called by FitMinorBridges): a land record for the span
// (PickBridgeRecord with depth 0, the steps possibly grown); none: AL 0.
// The rectangle around the span's end cells (unsigned min and max, one cell
// wider) asked of the occupant manager (slot 0xA0, no filter): any occupant
// empties the possible bridges (+0x1F4) and gives AL 0. Else the span's
// steps marked irregular, a major bridge appended (the tool's type, the
// steps, the record, the height twice, the byte 1) and its cells laid
// (LayBridgeCells with the record's layout for the span's length, found in
// its int map by the inlined lower bound, else part t10's 0x00631300); the
// list emptied and its head freed. AL 1.
uint8_t __fastcall AddMinorBridge(char* self, void*, int32_t first, int32_t last, uint32_t heightBits, uint32_t type)
{
    static bool logged = false;
    FirstCall("AddMinorBridge", logged);
    int32_t a = first, b = last;
    const int32_t index = CallThis<int32_t, int32_t*, int32_t*, uint32_t, uint32_t, uint32_t>(
        addr::cSC4NetworkTool_PickBridgeRecord, self, &a, &b, 0u, 0u, type);
    if (index < 0)
        return 0;
    char* const record = RecordAt(At<char*>(ToolRow(self), row::kBridges), static_cast<uint32_t>(index));
    char* const from = CellAt(self, StepFirst(self, a));
    char* const to = CellAt(self, StepLast(self, b));
    const uint32_t fromX = At<uint32_t>(from, 0), fromZ = At<uint32_t>(from, 4);
    const uint32_t toX = At<uint32_t>(to, 0), toZ = At<uint32_t>(to, 4);
    // Unsigned compares (`jb` / `jae`), each picking the address it reads.
    const uint32_t low[2] = {(toX < fromX ? toX : fromX) - 1, (toZ < fromZ ? toZ : fromZ) - 1};
    const uint32_t high[2] = {(fromX < toX ? toX : fromX) + 1, (fromZ >= toZ ? fromZ : toZ) + 1};
    ListNode* list = NewList();
    void* const occupants = At<void*>(self, field::kOccupants);
    if (Virtual<uint8_t>(occupants, bslot::kGetByCityCells, &list, low, high, 0u, 0xFFFFFFFFu)) {
        const bool any = list->next != list;
        Virtual<void>(At<void*>(self, field::kOccupants), bslot::kReleaseOccupantList, &list);
        if (any) {
            CallThis<void>(addr::SC4Uint32Vector_Clear, self + field::kPossibleBridges);
            CallThis<void>(addr::SC4List_Destroy2, &list);
            return 0;
        }
    }
    for (int32_t k = a; k <= b; ++k)
        At<int32_t>(StepAt(self, k), step::kIrregular) = 1;
    // The game's local: words +0x0 to +0x18 its stack's, the cell vector 0.
    alignas(4) char local[major::kSize] = {};
    CallThis<char*, const char*>(addr::SC4MajorBridgeVector_PushBack, self + field::kMajorBridges, local);
    char* const bridge = At<char*>(self, bfield::kBridgesEnd) - major::kSize;
    At<uint32_t>(bridge, major::kType) = At<uint32_t>(self, field::kType);
    At<int32_t>(bridge, major::kRecord) = index;
    At<int32_t>(bridge, major::kLast) = b;
    At<int32_t>(bridge, major::kFirst) = a;
    At<uint32_t>(bridge, major::kAltitude) = heightBits;
    At<uint32_t>(bridge, major::kHeight) = heightBits;
    At<uint8_t>(bridge, major::kMinor) = 1;
    // The record's layout for the span's length: the lower bound of the int
    // map (signed keys), the head when none or when the key found is above.
    const int32_t length = b - a + 1;
    char* const head = At<char*>(record, brec::kLayouts);
    char* found = head;
    for (char* node = At<char*>(head, imap::kRoot); node;) {
        if (At<int32_t>(node, imap::kKey) >= length) {
            found = node;
            node = At<char*>(node, imap::kLeft);
        } else {
            node = At<char*>(node, imap::kRight);
        }
    }
    if (found == head || length < At<int32_t>(found, imap::kKey))
        found = head;
    if (found != At<char*>(record, brec::kLayouts))
        CallThis<void, char*, char*>(addr::cSC4NetworkTool_LayBridgeCells, self, bridge, found + layout::kInNode);
    else
        CallThis<void, char*>(kMakeBridgeCellsT10, self, bridge);
    CallThis<void>(addr::SC4List_Clear2, &list);
    ListNode* const sentinel = list;
    Virtual<void>(ListNodeAllocator(), bslot::kDeallocate, static_cast<void*>(sentinel));
    return 1;
}

// 0x00632830 (ret 8: the step count's address, a network type; called by
// the segment placer 0x0063B830 at 0x0063B9E5): nothing without bridges
// (slot 61), with fewer than 4 steps, more than the regular count or a type
// row without bridge records. Else from step 2 on: a regular step whose two
// before it are regular too, free (CellsAreFree), with a water cell and
// fitting starts a span (FindBridgeSpan); a failed one or no record ends
// the pass. A span of at least 4 steps and the record's shortest gets its
// steps marked irregular, a major bridge appended (the type, the span, the
// record, the height it fitted, the byte 0), its altitude raised
// (RaiseMajorBridgeAltitude) and its cells laid (LayBridgeCells with the
// record's layout for the length, SC4Sint32Map::Find, else part t10's
// 0x00631300). The count becomes the step where the pass stopped.
void __fastcall FindMajorBridges(char* self, void*, int32_t* count, uint32_t type)
{
    static bool logged = false;
    FirstCall("FindMajorBridges", logged);
    if (!Virtual<uint8_t>(self, bslot::kAllowBridges))
        return;
    const int32_t n = *count;
    if (n < 4 || n > At<int32_t>(self, bfield::kRegularSteps))
        return;
    const Vector& records = At<Vector>(Row(type), row::kBridges);
    if (records.begin == records.end)
        return;
    int32_t s = 2;
    if (n > 2) {
        for (;;) {
            if (StepIrregular(self, s) || StepIrregular(self, s - 1) || StepIrregular(self, s - 2) ||
                !StepCellsAreFree(self, s))
                goto next;
            {
                uint8_t water = 0;
                for (uint32_t c = StepFirst(self, s); c <= StepLast(self, s); ++c)
                    if (UnderWater(self, CellX(self, c), CellZ(self, c)))
                        water = 1;
                if (!StepFits(self, s, type) || !water)
                    goto next;
                int32_t first = 0, last = 0, index = -1;
                uint32_t heightBits = 0;
                if (!CallThis<uint8_t, int32_t, int32_t*, int32_t*, int32_t*, uint32_t*, uint32_t>(
                        addr::cSC4NetworkTool_FindBridgeSpan, self, s, &first, &last, &index, &heightBits, type))
                    break;
                if (index < 0)
                    break;
                char* const record =
                    RecordAt(At<char*>(ToolRow(self), row::kBridges), static_cast<uint32_t>(index));
                if (last - first < 4)
                    goto next;
                const uint32_t length = static_cast<uint32_t>(last - first + 1);
                if (length < At<uint32_t>(record, brec::kMinLength))
                    goto next;
                for (int32_t k = first; k <= last; ++k)
                    At<int32_t>(StepAt(self, k), step::kIrregular) = 1;
                // The game's local: words +0x0 to +0x18 its stack's, the cell
                // vector 0 (freed again after the copy).
                alignas(4) char local[major::kSize] = {};
                CallThis<char*, const char*>(addr::SC4MajorBridgeVector_PushBack, self + field::kMajorBridges,
                                             local);
                CallThis<void>(addr::SC4BridgeCellVector_Free, local + major::kCells);
                char* const bridge = At<char*>(self, bfield::kBridgesEnd) - major::kSize;
                At<uint32_t>(bridge, major::kType) = type;
                At<uint32_t>(bridge, major::kHeight) = heightBits;
                At<int32_t>(bridge, major::kFirst) = first;
                At<int32_t>(bridge, major::kLast) = last;
                At<int32_t>(bridge, major::kRecord) = index;
                At<uint8_t>(bridge, major::kMinor) = 0;
                CallThis<void, char*>(addr::cSC4NetworkTool_RaiseMajorBridgeAltitude, self, bridge);
                int32_t key = static_cast<int32_t>(length);
                char* node = nullptr;
                CallThis<void, char**, int32_t*>(addr::SC4Sint32Map_Find, record + brec::kLayouts, &node, &key);
                if (node == At<char*>(record, brec::kLayouts))
                    CallThis<void, char*>(kMakeBridgeCellsT10, self, bridge);
                else
                    CallThis<void, char*, char*>(addr::cSC4NetworkTool_LayBridgeCells, self, bridge,
                                                 node + layout::kInNode);
            }
        next:
            ++s;
            if (s >= *count)
                break;
        }
    }
    *count = s;
}

// 0x00632AD0 (ret 8: the valid step count, a network type; called by the
// segment placer 0x0063B830 at 0x0063BA36 when the type has a bridge
// record): nothing without bridges (slot 61) or with fewer than 5 steps
// (the lesser of the count and the regular count, less 1). Each regular,
// fitting step from 1 on is measured at its first cell's two vertices for
// the drag's direction (terrain slot 0x30: the highest and the lowest of
// the two); a step whose highest is below the previous step's lowest starts
// a dip (its top the previous step's highest); inside it the lowest is
// tracked while the steps stay below the top and below the dip's first
// highest, and the start moves on one step (the top lowered to that
// highest) when a step is below the top again; a step at or above the top
// ends it: when the dip has at least 4 steps, is deeper than params +0x3C
// and its end is within params +0x40 of the top, AddMinorBridge(start, the
// step, the top, the type).
void __fastcall FitMinorBridges(char* self, void*, int32_t valid, uint32_t type)
{
    static bool logged = false;
    FirstCall("FitMinorBridges", logged);
    if (!Virtual<uint8_t>(self, bslot::kAllowBridges))
        return;
    int32_t m = At<int32_t>(self, bfield::kRegularSteps);
    if (valid <= m)
        m = valid;
    --m;
    if (m < 4)
        return;
    const uint32_t way = At<uint32_t>(self, bfield::kDirection) & 3;
    int32_t start = kNoBest;
    uint8_t inDip = 0;      // the game's: the low byte of its first argument's slot
    uint32_t prevLow = 0;   // the game's word at its entry ESP - 8
    uint32_t prevHigh = 0;  // at entry ESP - 0x10
    uint32_t dipLow = kHighestBits;
    uint32_t top = 0;       // entry ESP - 0x24, never written before a dip's start (ours 0)
    uint32_t firstHigh = 0; // entry ESP - 0xC, the same
    for (int32_t s = 1; s < m; ++s) {
        if (StepIrregular(self, s) != 0 || !StepFits(self, s, type)) {
            inDip = 0;
            continue;
        }
        const uint32_t c = StepFirst(self, s);
        const uint32_t vertex = CellZ(self, c) * At<uint32_t>(self, field::kVertexStride) + CellX(self, c);
        const uint32_t vb = At<uint32_t>(self, bfield::kVertexOffsetsB + way * 4) + vertex;
        const uint32_t va = At<uint32_t>(self, bfield::kVertexOffsetsA + way * 4) + vertex;
        uint32_t high = kLowestBits, low = kHighestBits;
        const uint32_t a = Bits(static_cast<float>(VertexAltitude(self, va)));
        const double b = VertexAltitude(self, vb);
        if (D(a) > static_cast<double>(Global<float>(addr::cSC4NetworkTool_LowestFloat))) // `test ah, 0x41; jne`
            high = a;
        if (b > D(high))
            high = Bits(static_cast<float>(b));
        if (D(a) < static_cast<double>(Global<float>(addr::cSC4NetworkTool_HighestFloat))) // `test ah, 5; jp`
            low = a;
        if (b < D(low))
            low = Bits(static_cast<float>(b));
        if (!inDip) {
            if (Below(high, prevLow)) {
                inDip = 1;
                start = s - 1;
                top = prevHigh;
                firstHigh = high;
                dipLow = kHighestBits;
            } else {
                prevLow = low;
                prevHigh = high;
            }
            continue;
        }
        // `test ah, 1; je`: at or above the top (ordered) ends the dip; a
        // NaN does not.
        if (!(F(high) >= F(top))) {
            // `test ah, 1; jne` twice: below (or unordered) the first highest
            // keeps the dip going.
            if (!(F(prevHigh) >= F(firstHigh)) || !(F(high) >= F(firstHigh))) {
                if (Below(low, dipLow))
                    dipLow = low;
                continue;
            }
            if (Below(high, top)) {
                ++start;
                top = firstHigh;
            }
        }
        const int32_t length = static_cast<int32_t>(static_cast<uint32_t>(s) - static_cast<uint32_t>(start) + 1u);
        if (length >= 4 &&
            D(top) - D(dipLow) > static_cast<double>(At<float>(self, bfield::kMinDip)) && // `test ah, 0x41; jne`
            std::fabs(D(top) - D(high)) <= static_cast<double>(At<float>(self, bfield::kMaxDeckOffset))) // `jp`
            CallThis<uint8_t, int32_t, int32_t, uint32_t, uint32_t>(addr::cSC4NetworkTool_AddMinorBridge, self, start,
                                                                    s, top, type);
        inDip = 0;
    }
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kBridgePick[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_FindMajorBridges, FindMajorBridges),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_FitMinorBridges, FitMinorBridges),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_AddMinorBridge, AddMinorBridge),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_FindBridgeSpan, FindBridgeSpan),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_FitBridgeSpan, FitBridgeSpan),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_PickBridgeRecord, PickBridgeRecord),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_ExtendBridgeSpan, ExtendBridgeSpan),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_StepCellsFitBridge, StepCellsFitBridge),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_CellsAreFree, CellsAreFreeThunk),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_LowestCellCorner, LowestCellCornerThunk),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_RaiseMajorBridgeAltitude, RaiseMajorBridgeAltitude),
};

const Detour kBridgePlace[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_LayBridgeCells, LayBridgeCells),
    OPENSC4_DETOUR(addr::SC4Vector32_CopyConstruct, Vector32CopyConstruct),
    OPENSC4_DETOUR(addr::SC4BridgeCell_CopyConstruct, BridgeCellCopyConstruct),
    OPENSC4_DETOUR(addr::SC4BridgeCellVector_Free, BridgeCellVectorFree),
    OPENSC4_DETOUR(addr::SC4BridgeCellVector_CopyConstruct, BridgeCellsCopyConstruct),
    OPENSC4_DETOUR(addr::SC4MajorBridge_UninitializedCopy, MajorBridgeUninitializedCopy),
    OPENSC4_DETOUR(addr::SC4MajorBridge_UninitializedFill, MajorBridgeUninitializedFill),
    OPENSC4_DETOUR(addr::SC4MajorBridgeVector_InsertOverflow, MajorBridgesInsertOverflow),
    OPENSC4_DETOUR(addr::SC4MajorBridgeVector_PushBack, MajorBridgesPushBack),
};

#undef OPENSC4_DETOUR

bool InstallBridgePick(registry::Mode) { return InstallDetours(kBridgePick, "bridgepick"); }
bool InstallBridgePlace(registry::Mode) { return InstallDetours(kBridgePlace, "bridgeplace"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.bridgepick", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_FindMajorBridges,
                        "Our network tool's bridge search: FindMajorBridges (over water) and FitMinorBridges (over "
                        "a dip), AddMinorBridge, the span's finding, fitting and growing, the record's pick, the "
                        "step and occupant tests, the cell's least corner and the major bridge's altitude (11 "
                        "detours; src/core/nettool_bridge_b.cpp)",
                        false, InstallBridgePick}),
    registry::Register({"simulators.nettool.bridgeplace", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_LayBridgeCells,
                        "Our network tool's bridge cells: LayBridgeCells (a major bridge's cells from its record's "
                        "layout) and the containers under the bridges (the cell's and the cell vector's copies, "
                        "the major bridges' copy, fill, InsertOverflow and PushBack, the 32-byte vector's copy; 9 "
                        "detours; src/core/nettool_bridge_b.cpp)",
                        false, InstallBridgePlace}),
};

} // namespace
} // namespace opensc4::nettool_sim
