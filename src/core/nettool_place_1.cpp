// Our cSC4NetworkTool, part t6, first file (build 641; the class map and the
// fields are in src/core/nettool_sim.h, this part's layouts in
// nettool_place.h): the placing. A player's drag ends in slot 19
// PlaceAllSegments, which (when building a surface network) makes the
// construction crew and hands each two consecutive anchor points to the
// guarded DrawNetworkLine (slot 18) and to the segment placer 0x0063B830:
// the checks that cut the valid steps (slots 66, 68, 59, 69, the tunnels'
// search, the major bridges), the cell infos (slot 71) and their edges, the
// RUL0 intersections, the solver (t8), the heights (t9), then the
// occupants (slot 55 and the bridges', tunnels' and intersections'
// occupants, t7) and the crew's AddSegment; or, previewing, the highlights.
// Slot 7 PlaceNetworkOccupants (a straight run between two cells, the
// network manager's AutoconnectLotNodes and the region's connection
// changes) and slot 8 PlaceNetworkOccupantById (one piece, the network
// manager's CreateConnectionNode) go through the same code. Five groups in
// two files, each a list of detours at the game's addresses:
//   simulators.nettool.place (this file): slot 19 PlaceAllSegments
//     0x0063BDC0, the segment placer PlaceSegment 0x0063B830, slot 7
//     PlaceNetworkOccupants 0x00629210, slot 8 PlaceNetworkOccupantById
//     0x00624FD0, MarkInvalidSteps 0x00624BF0 (SetValidStepCount 0x0062CA10
//     jumps to it), ApplyNeighborConnections 0x00624610 (also called by the
//     power line tool 0x00652980), ApplyEdgeConnections 0x00626EB0,
//     ClearOverrideRecords 0x00639040 (8 detours);
//   simulators.nettool.placerules (this file): the RUL0 intersections while
//     placing: FindRuleIntersections 0x0063B340, SC4Rul0Rule::FitsInCity
//     0x00608CD0 (also called by 0x00661260), MarkIntersectionCells
//     0x0063B130, MarkIntersections 0x0063B7F0 and PlaceIntersectionOccupants
//     0x0062C4B0 (both also called by the guarded 0x0062CD50) (5 detours);
//   simulators.nettool.placecells (second file): the cell passes:
//     SetStepEdges 0x00638160, MarkWideSides 0x006383A0, ApplyRuleCellRecords
//     0x006384E0 (also the guarded 0x0062CD50's), FindTunnels 0x00639490,
//     FindTunnelEnd 0x00638770, PreviewChangedCellHeights 0x00626000, the
//     preview drawable's UpdateCellHeights 0x00620930, ResolveChangedCells
//     0x00629CF0 and ResolveChangedVertices 0x00629EB0 (both also the
//     guarded 0x0062CD50's) (9 detours);
//   simulators.nettool.edgewords (second file): the edge word helpers
//     MergeSideKinds 0x00620F00, SetSideByte 0x00620FC0 (also called by t8's
//     SetSideKind 0x006335B0 and FixEdgesKind2 0x00633C10 at its address),
//     SetSideFromDirection 0x00620FF0, ClearSideHighBits 0x00621440 (also
//     called by t8's FixCellEdges 0x006359B0) (4 detours);
//   simulators.nettool.isectvector (second file): the template code of the
//     intersections' vector (0x28-byte elements with a uint32 vector):
//     UninitializedCopy 0x00627160, UninitializedFill 0x006271C0, CopyRange
//     0x00627220, Erase 0x0062BE80, InsertOverflow 0x0062BFE0, PushBack
//     0x0062C0F0, the uint32 vector's CopyConstruct 0x006251F0, and the major
//     bridges' assignment copy 0x0062C510 (Erase, PushBack and 0x0062C510
//     also the guarded 0x0062CD50's) (8 detours).
// The split: what makes the city's occupants and the crew (place), the RUL0
// rule search (placerules), the cell infos' edges, heights and tunnels
// (placecells), pure word functions (edgewords) and template code
// (isectvector); each can be tried and bisected alone.
//
// Every function was written from the disassembly. Calls between them go
// to the game's addresses (through the address entries; the tool's own
// slots through its vtable, as the game calls them), so each detour stands
// alone and OPENSC4_NETTOOL_SKIP bisects them.
//
// Left as the game's: nothing of the part. Under it the game's (or the
// earlier parts' or the crew's own) code at its address: the guarded
// DrawNetworkLine (slot 18, through the vtable), the solver SolveTiles
// 0x00635CB0 and CollectDraggedCells 0x006355C0 (t8), FitHeights 0x00637A80
// (t9), the slots 53 to 72 the placer calls through the vtable, PlaceTunnels
// 0x006287D0, AddEdgeConnection 0x00628BD0 and PlaceRuleOccupants 0x0062C3A0
// (t7), the bridges' 0x0062F610, 0x00632830, 0x00632AD0 (t10/t11), the
// one-way pass 0x0063A640 (t5), the RUL0 match 0x00609A00 (t12), the RUL0
// store's FindOrInsert 0x0062C2F0, MakeTypeCellInfo 0x00632EF0, the world
// cache's functions, the cell entry 0x00633220, GetNetworkOccupantAt
// 0x00624B70, the satisfier's GetResult 0x0065EB00, the preview drawable's
// EraseCell 0x00620CF0, AddCell 0x00620820 and VertexHeights 0x0061FBF0
// (drawing: called at its address, as part t4's AddCellPreview calls the
// drawable), the crew's constructor, Init, AddSegment, Start and
// FinishPieces, and the shared container code (SC4Vector12::Copy and
// InsertOverflow, SC4CornerVector::CopyRange, SC4Vector16::Copy,
// SC4PointVector::InsertN, the pair vector's Clear and PushBack, the lane
// entry vector's PushBack (the 16-byte tunnel records), SC4IntVector::
// Assign, the bridge cell vector's operator= 0x0062C1E0,
// DestroyMajorBridges 0x0062C590, DestroyIntersections 0x0062BE40, the
// list's Construct, Clear and Destroy, the occupant set's Insert
// 0x00467B80, SC4RefHolder's constructor, the hash set's Clear and Begin,
// SC4String's FromText and Destroy, operator new, SC4::stl_allocate and
// SC4::Free). Nothing here computes geometry, UVs or textures or calls a
// render, texture or model service: UpdateCellHeights only walks the
// drawable's two trees and hands each record to VertexHeights at its
// address; MarkInvalidSteps hands points and a colour to EraseCell and
// AddCell; the tunnel code reads terrain altitudes for the simulation.
//
// NAM. None of these is guarded. The guarded 0x0062CD50 calls Erase
// (0x0062CDAA), 0x0062C510 (0x0062CDC7), PushBack (0x0062CE48),
// ApplyRuleCellRecords (0x0062D28E), MarkIntersections (0x0062D2B2),
// ResolveChangedCells (0x0062D397), ResolveChangedVertices (0x0062D3A3) and
// PlaceIntersectionOccupants (0x0062D3EE) at their addresses; DrawNetworkLine
// 0x0063AF40, 0x006099A0, 0x00634D70, 0x00636690 and 0x00637EB0 call none of
// them. NAM's DLL reads the tool, the cell infos, the solved cells, the
// intersections and the occupants by layout: all stay in the game's memory
// with the game's layout, nothing is cached across a call, every container
// grows through the game's helpers or allocators as the game's code does
// (inline where the game inlines, SC4::stl_allocate / SC4::Free, the
// template code at its addresses).
//
// Registers. Every game caller of each function was scanned after the
// call (both branches; ECX, EDX and EAX): none reads ECX or EDX before
// writing it (0x006382B6's `mov dl, ...` writes DL and reads only DL after;
// SetSideKind 0x006335B0 returns straight after SetSideByte, and t8 found
// no caller of it that reads ECX or EDX). EAX is read after FitsInCity
// (0x0063B47D, 0x00661638: AL), FindTunnelEnd (0x006396F6: AL), the segment
// placer (0x0062940B, 0x0062959C, 0x00629889: AL stored), FindRuleIntersections
// (0x0063BAC2: all of it, the count), the copies 0x00627160 (0x0062C03C,
// 0x0062C0BD), 0x006271C0 (0x0062C09C), 0x00627220 (0x0062BE9B) and
// 0x0062C510 (0x0062CDC7, 0x0063B8ED: the end pointers), and SetSideByte's is
// handed on by SetSideKind: ours give the same. Slot 7's callers
// (0x004AAFF3, 0x004AB13B, 0x006117BB) and slot 8's (0x00611336,
// 0x00611413) read nothing after; slot 19's (the tool UI's 0x00645447,
// 0x006454DD, 0x0064577D, 0x00646BA8, four pushes each; the four-push
// `call [reg+0x4C]` sites of the power line tool, 0x006500DB and 0x00652461;
// every `call [reg+0x4C]` of 0x00600000 to 0x00670000 checked) read
// nothing. Still, slot 7 gives the game's
// EAX (the anchors' old end above AL) and slot 19 the game's (Release's
// answer above the old +0x52, or the last segment placer's EAX); the
// segment placer's own EAX above AL is not the game's (nobody reads it but
// slot 19, which hands it on unread). So all 34 are plain detours.
//
// Wrappers: no file of src/core/game_*.cpp or *fix*.cpp detours a function
// of this part or tests return addresses in 0x00608CD0 to 0x0063BF30
// (game_quiet.cpp names 0x006208D0 in a comment; its OPENSC4_TEST_QUIET road
// calls PlaceAllSegments through the interface). game.quiet.network
// (game_quiet.cpp) withholds the crew's sound service in the crew's Init,
// which our PlaceAllSegments calls through the crew's vtable as the game
// does: with the game's Init the wrapper's detour at the entry runs, with
// ours (crew_sim::InitIsOurs) its observer switch; neither looks at the
// caller. sim_timers.cpp's kCallOwners is the crew's own range 0x00605490
// to 0x00608C50: the stopwatches are started, read and stopped by the
// crew's code (Start 0x00608770 and the jobs), which ours calls at its
// addresses, so the callers sim_timers sees are the crew's own; nothing of
// this part reads a stopwatch. fixed_seed.cpp keys generator seeds by the
// first code address up the stack: no function here constructs or seeds a
// generator (the crew's constructor 0x006070C0 does not either; the
// pedestrians' generator is seeded in the crew's code at 0x006077CB). The
// crew's FinishPieces (crew_life.cpp) lists the tool's call site 0x0063BD15
// for its convention only. So nothing needed a mark.
//
// Floats. The game runs the x87 at 53 bits, so intermediates are doubles
// where every operation is +, -, *, /, fabs or a compare and every float
// load or store rounds as `float`. No function keeps a value on the x87
// stack across a call (each callee's ST0 is stored, compared or combined
// with memory at once). The values:
//   - slot 8: per corner the terrain's altitude (slot 0x30, ST0) handed as
//     a float to slot 74, whose ST0 h gives the height float(h), the range
//     low float(h - margin) from the UNROUNDED h (FST, then FSUB on the same
//     ST0) and the high float(margin + float(h)) (margin the float +0x2C4);
//   - PreviewChangedCellHeights / ResolveChangedCells: slot 73's ST0 h
//     compared with the stored float (FUCOMPP, `test ah, 0x44; jnp`:
//     changed when not equal or unordered: `!(h == old)`), then stored as
//     float(h);
//   - FindTunnels / FindTunnelEnd: a step's ground is four terrain
//     altitudes s0 = float(a0), s1 = float(a1 + s0), s2 = float(a2 + s1),
//     g = (a3 + s2) * 0.25f (the float at 0x00A8821C), g unrounded; FindTunnels
//     stores float(g) as the next step's previous and goes on only when g -
//     previous > +0x23C (`test ah, 0x41; jne`: above, ordered); FindTunnelEnd
//     takes a step when g < the previous float (ordered), keeps it when
//     |float(g) - start| < best (ordered; best stored as a float), stops
//     when the first tunnel height plus the start reaches float(g) (`test
//     ah, 1; je`: a NaN goes on); its last test, tan(+0x2AC) * +0x238 * span
//     (FIMUL) against best, uses FPTAN, which C++ cannot repeat: a naked
//     helper (X87TunnelSlope, second file) runs the game's instructions and
//     gives the status word.
//
// Stack leftovers the game stores (ours are 0):
//   - slot 8's solved cell {ID, 2, 0} on its stack: the three bytes after
//     the rotation byte 2 are never written. Its only reader is slot 72
//     (0x006278A0), which takes the byte (`mov cl, [edi+4]` at 0x00627F20,
//     `mov dl, [eax+4]` at 0x0062825E) and keeps no copy. Harmless.
//   - slot 8's cell info on its stack: the constructor 0x00647850 writes the
//     occupant, the edge words, +0x4C, +0x50 and +0xB8, slot 8 the rest it
//     needs; +0x11 to +0x13, +0x14, +0x51 to +0x57 and the second pairs +0x88
//     to +0xA7 are never written. Readers: slot 72 (x, z, the flags, the
//     edge words, the heights; it writes +0x54), slot 54 CreateNetworkOccupant
//     (x, z, the flags, the occupant, the edge words, +0x50, the heights),
//     VertexClearanceCategory (heights, vertices), SetUpPieceSupports
//     0x00643FB0 (heights and vertices only: every use of its cell argument
//     scanned), ClearCellForOccupant (x and z by value), slot 80 (the edge
//     word's address). None reads those bytes; the cell info is not kept.
//     Harmless.
//   - ResolveChangedCells' and ResolveChangedVertices' solved cells: the
//     rotation byte from the occupant's slot 0x68 goes into the low byte of
//     a frame dword whose three upper bytes are never written, and the
//     whole dword is appended to +0x98: the same leftover as t8's (its top
//     lists the readers of +0x98's elements: all take the low byte).
//     Harmless; the ntsv test hashes the low byte only.
//   - FindRuleIntersections' intersection record on its stack: its +0x18 is
//     never written and is copied into the appended element (both paths).
//     The guarded 0x0062CD50 leaves the same word unwritten in the
//     intersections it appends (0x0062CE4D to 0x0062CE6E write +0x0 to +0x14
//     only). Readers of the intersections' elements: the copies (which
//     carry the word along), PlaceIntersectionOccupants and MarkIntersections
//     (+0x0, +0x4, +0x8, +0x14), MarkIntersectionCells (+0x0, +0x4, +0x8),
//     CollectIntersectionCells 0x0063B1A0 (+0x1C), GetCostOfSolution 0x00625640,
//     slot 55's previews, FitHeights' 0x006371E0 (scanned after each load of
//     +0xCC in the tool's range: no read of +0x18). NAM's DLL reads the
//     intersections by layout (not checked against its binary). Verdict:
//     harmless; the ntpl test hashes +0x0 to +0x14 only.
//   - FindTunnels' 16-byte record: its end height (+0x4) is written by
//     FindTunnelEnd only together with the end step, and the record is
//     appended only when the end step was set in that call: no leftover.
// Other stores into the game's stack, never read again as what they were:
// slot 7 keeps its answer in the low byte of its type argument's slot, the
// occupant out in z1's, the pair list in x1's, the axis index in x0's and
// the axis count in z0's; the segment placer keeps the edge connection flag
// in the low byte of its type argument's slot, the step count variable in
// its first argument's and the highlight holder in its fourth's (after the
// last read of the byte there), and its set Insert's result overwrites its
// row pointer (not read after on that path); FindRuleIntersections uses its
// second argument's slot for the last FindOrInsert key; FindTunnelEnd and
// ResolveChangedVertices reuse their argument slots for a span and a key;
// UpdateCellHeights writes the key into its first argument's slot. Ours
// use locals. The unread tags the template calls get are the addresses of
// argument slots in the game, of locals in ours.
//
// Calls, each count from the asm's pushes for that call and the callee's
// own `ret N` (slot numbers as byte offsets):
//   - the part's own, at the game's addresses: the segment placer five (ret
//     0x14), FindRuleIntersections two (ret 8), MarkIntersectionCells two
//     (ret 8), MarkIntersections one (ret 4), FitsInCity four (ret 0x10),
//     MarkInvalidSteps one (ret 4), ApplyNeighborConnections,
//     ApplyEdgeConnections and ClearOverrideRecords none, the second file's
//     (SetStepEdges, MarkWideSides one; ApplyRuleCellRecords,
//     PreviewChangedCellHeights, ResolveChangedCells none; ResolveChangedVertices
//     one; FindTunnels one; PlaceIntersectionOccupants one; Erase two (ret 8);
//     InsertOverflow five (ret 0x14); PushBack one (ret 4); CopyConstruct one
//     (ret 4); SC4MajorBridgeVector::CopyRange cdecl, five pushed, three
//     read);
//   - the tool's slots through the vtable: 0x40 (16) none, 0x48 (18) four
//     (AL), 0xD4 (53) two (AL), 0xDC (55) three, 0xE0 (56) one (AL), 0xE4
//     (57) one, 0xEC (59) one (EAX), 0xF0 (60) none, 0x108 (66) one, 0x110
//     (68) three, 0x114 (69) one, 0x118 (70) none, 0x11C (71) one, 0x120 (72)
//     three, 0x128 (74) one float (ST0);
//   - the game's: SolveTiles two (ret 8, EAX), CollectDraggedCells none,
//     FitHeights one (ret 4, AL), PlaceTunnels one (ret 4), AddEdgeConnection
//     two (ret 8, AL), PlaceRuleOccupants six (ret 0x18), 0x0062F610 one (ret
//     4), 0x00632830 two (ret 8), 0x00632AD0 two (ret 8), 0x0063A640 one (ret
//     4), 0x00609A00 three (ret 0xC, EAX), FindOrInsert one (ret 4: the ID's
//     address; EAX the rule), GetNetworkOccupantAt four (ret 0x10, AL), the
//     world cache's SaveCells, RestoreCells, Reset, ClearCellIndices none and
//     GetCell one (ret 4: the key), the cell entry one (ret 4), CombineEdges
//     none, the cell info's constructor none, EraseCell one (ret 4), AddCell
//     five (ret 0x14), the network filter's constructor none (EAX this),
//     the no-ops 0x00407990 (none) and 0x00648F10 (one, `ret 4`);
//   - the crew: operator new 0x5E55E0 cdecl one, the constructor none (EAX
//     this), AddRef (slot 1) none, Init (slot 4, 0x10) six, AddSegment three
//     (ret 0xC), Start none, Release (slot 2) none, FinishPieces cdecl two;
//   - containers: SC4::stl_allocate and SC4::Free cdecl one; SC4Vector12::
//     Copy, SC4CornerVector::CopyRange and SC4Vector16::Copy cdecl, five
//     pushed (first, last, destination, an unread tag, 0), three read;
//     SC4PointVector::InsertN five (ret 0x14: where, the value, a tag, 1, 1);
//     SC4Vector12::InsertOverflow five (ret 0x14); the pair vector's Clear
//     none and PushBack one (ret 4); DestroyMajorBridges cdecl three; the
//     hash set's Clear none; SC4Vector12::Clear 0x00626130 none; the list's
//     Construct one (ret 4, unread), Clear none, Destroy none; SC4RefHolder's
//     constructor one (ret 4); the occupant set's Insert two (ret 8: the
//     result out (a node and a byte), the value's address); SC4String::
//     FromText one (ret 4, EAX the string) and Destroy none;
//   - other objects: the neighbours simulator (0x00B43D8C) slots 0x18 four
//     and 0x1C three (vtable 0x00AB1EE0's `ret` sizes); the network occupant
//     slots 0x50 SetUsable one, 0x2C ClearNetworkFlag one, 0x58 IsOfType one
//     (AL), 0x98 one (its answer unused), 0xC0 AsOccupant none; a filter's
//     AddRef / Release; the callback tool's slot 7 eight; the terrain slot
//     0x30 one (ST0).
#include "core/nettool_place.h"

#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim {
namespace {

using namespace place;

// The construction crew's (0x54 bytes; crew_sim.h).
constexpr uint32_t kCrewSize = 0x54;
// The jump table of PlaceAllSegments by network type (0x0063BF20, 13 bytes:
// 0 makes a crew, 1 does not; types 4 pipe, 5 power line and 7 subway).
constexpr uint8_t kCrewByType[13] = {0, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0};
constexpr uint32_t kLastCrewType = 12;
// What a list node of slot 7's pairs is: next, previous and four words.
constexpr uint32_t kPairNodeSize = 0x18;
constexpr uint32_t kListNodeSize = 0xC;
// The preview's red for an invalid step's cells, and the height
// MarkInvalidSteps gives the highway's (15.5f).
constexpr uint32_t kInvalidColour = 0x80FF0000, kHighwayPreviewHeightBits = 0x41780000;

inline char* Store() { return reinterpret_cast<char*>(addr::cSC4NetworkTool_Rul0Store); }

inline char* RuleOf(uint32_t id)
{
    uint32_t key = id;
    return CallThis<char*, const uint32_t*>(addr::SC4Rul0Store_FindOrInsert, Store(), &key);
}

// A point appended to the anchors as the game inlines it: in place when
// there is room (nothing written at a null end), else InsertN with the
// unread tag, one copy, at the end.
void PushAnchor(char* self, const int32_t* point, void* tag)
{
    Vector& anchors = VectorAt(self, field::kAnchors);
    char* const end = anchors.end;
    if (end != anchors.capacity) {
        if (end) {
            At<int32_t>(end, 0) = point[0];
            At<int32_t>(end, 4) = point[1];
        }
        anchors.end += kPointSize;
        return;
    }
    CallThis<void, char*, const int32_t*, void*, uint32_t, uint32_t>(addr::SC4PointVector_InsertN, &anchors, end,
                                                                      point, tag, 1u, 1u);
}

// The game's inline copy of an empty range over the anchors: the end set
// to the begin.
inline void ClearAnchors(char* self)
{
    Vector& anchors = VectorAt(self, field::kAnchors);
    anchors.end = anchors.begin;
}

// A pair node appended to slot 7's list (0x18 bytes from SC4::stl_allocate,
// the four words written unless the value's address is null).
void PushPair(ListNode* head, int32_t a, int32_t b, int32_t c, int32_t d)
{
    char* const node = CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, kPairNodeSize);
    int32_t* const value = reinterpret_cast<int32_t*>(node + 8);
    if (value) {
        value[0] = a;
        value[1] = b;
        value[2] = c;
        value[3] = d;
    }
    ListNode* const n = reinterpret_cast<ListNode*>(node);
    ListNode* const last = head->previous;
    n->next = head;
    n->previous = last;
    last->next = n;
    head->previous = n;
}

// A point clamped to [0, last] as slot 7 does it: the smaller of the two
// (signed; the value when equal), then 0 when that is negative.
inline int32_t Clamp(int32_t value, int32_t last)
{
    const int32_t smaller = last < value ? last : value;
    return smaller < 0 ? 0 : smaller;
}

// The sign the game computes as (v >> 31) - (-v >> 31) (0 for INT_MIN).
inline int32_t Sign(int32_t value)
{
    const int32_t negated = static_cast<int32_t>(0u - static_cast<uint32_t>(value));
    return (value >> 31) - (negated >> 31);
}

inline uint8_t DrawLine(char* self, const int32_t* start, const int32_t* end, uint32_t word, int32_t type)
{
    return Virtual<uint8_t>(self, slot::kDrawNetworkLine, start, end, word, type);
}

inline uint32_t PlaceSegmentAt(char* self, uint32_t place, void* crew, uint32_t word, uint32_t neighbor, int32_t type)
{
    return CallThis<uint32_t, uint32_t, void*, uint32_t, uint32_t, int32_t>(addr::cSC4NetworkTool_PlaceSegment, self,
                                                                             place, crew, word, neighbor, type);
}

inline uint8_t OccupantAt(char* self, int32_t x, int32_t z, void* filter, void** out)
{
    return CallThis<uint8_t, int32_t, int32_t, void*, void**>(addr::cSC4NetworkTool_GetNetworkOccupantAt, self, x, z,
                                                              filter, out);
}

// ====================================================================================
// Slot 19 and the segment placer
// ====================================================================================

// 0x0063BDC0 (slot 19, ret 0x10: a build byte, a word for slot 18, a
// neighbour connection byte, a network type): +0x52 kept and cleared for
// the call; the no-op 0x00407990; when the build byte is set and the type
// is 0 to 3, 6 or 8 to 12, a construction crew (operator new 0x54, the
// constructor; a failed allocation null, and the next call crashes as the
// game's does), referenced, Init with the type row's +0x1C, +0xC8, +0xCC,
// +0xD0, +0xD4 and +0xD8; for each two consecutive anchors (the count
// taken once, the anchors re-read for each) DrawNetworkLine (the two points
// copied to the stack, the word, the type), slot 16 and the segment placer
// (the build byte, the crew, the word, the byte, the type); the crew's
// Start and Release. +0x52 put back. EAX: with a crew Release's answer with
// the old +0x52 in its low byte, else the last segment placer's EAX (the
// anchors' begin when there was no segment), as the game's leaves them (no
// caller reads EAX: header void, the view control's calls checked).
uint32_t __fastcall PlaceAllSegments(char* self, void*, uint32_t build, uint32_t word, uint32_t neighbor, int32_t type)
{
    static bool logged = false;
    FirstCall("PlaceAllSegments", logged);
    const uint8_t old = At<uint8_t>(self, field::kByte52);
    At<uint8_t>(self, field::kByte52) = 0;
    CallThis<void>(addr::SC4AgentBase_MonthPassed, self);
    char* const row = Row(static_cast<uint32_t>(type));
    void* crew = nullptr;
    if (static_cast<uint8_t>(build) && static_cast<uint32_t>(type) <= kLastCrewType &&
        kCrewByType[static_cast<uint32_t>(type)] == 0) {
        void* const memory = CallCdecl<void*, uint32_t>(addr::SC4_operator_new, kCrewSize);
        crew = memory ? CallThis<void*>(addr::cSC4NetworkConstructionCrew_cSC4NetworkConstructionCrew, memory)
                      : nullptr;
        AddRefOf(crew);
        Virtual<uint8_t>(crew, pxslot::kCrewInit, At<uint32_t>(row, prow::kPenWidthX), At<uint32_t>(row, prow::kCrewC8),
                         At<uint32_t>(row, prow::kCrewCC), At<uint32_t>(row, prow::kCrewD0),
                         At<uint32_t>(row, prow::kCrewD4), At<uint32_t>(row, prow::kCrewD8));
    }
    const Vector& anchors = VectorAt(self, field::kAnchors);
    uint32_t result = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(anchors.begin));
    const int32_t segments = (static_cast<int32_t>(anchors.end - anchors.begin) >> 3) - 1;
    for (int32_t i = 0; i < segments; ++i) {
        const int32_t* const at = reinterpret_cast<const int32_t*>(VectorAt(self, field::kAnchors).begin) + i * 2;
        const int32_t start[2] = {at[0], at[1]};
        const int32_t end[2] = {at[2], at[3]};
        DrawLine(self, start, end, word, type);
        Virtual<void>(self, slot::kClearOccupantHighlights);
        result = PlaceSegmentAt(self, build, crew, word, neighbor, type);
    }
    if (crew) {
        CallThis<void>(addr::cSC4NetworkConstructionCrew_Start, crew);
        const uint32_t released = ReleaseOf(crew);
        At<uint8_t>(self, field::kByte52) = old;
        return (released & 0xFFFFFF00u) | old;
    }
    At<uint8_t>(self, field::kByte52) = old;
    return result;
}

// "ET: ..." handed to the no-op 0x00648F10 as a game string (FromText,
// the call, Destroy: the string's buffer comes from the game's allocator
// and goes back to it), when placing.
void Trace(char* self, std::uintptr_t text)
{
    GameString string{};
    void* const made =
        CallThis<void*, const char*>(addr::SC4String_FromText, &string, reinterpret_cast<const char*>(text));
    CallThis<void, void*>(addr::cSC4AirportDeveloper_EmptySlot, self, made);
    CallThis<void>(addr::SC4String_Destroy, &string);
}

// A pass's verdict: a count below the valid step count sets the failure
// and no valid step.
inline void Check(char* self, int32_t count, uint32_t failure)
{
    if (count < At<int32_t>(self, field::kValidSteps)) {
        At<uint32_t>(self, field::kFailure) = failure;
        At<int32_t>(self, field::kValidSteps) = 0;
    }
}

// The segment placer's build (0x0063BA44 to 0x0063BB80): true when the
// heights fit and slot 56 agrees; false with the failure set.
bool Build(char* self, char* row, int32_t type)
{
    CallThis<void>(addr::SC4Uint32HashSet_Clear, self + field::kSet84);
    CallThis<void>(addr::SC4Vector12_Clear, self + field::kSolved);
    CallThis<void>(addr::cSC4NetworkWorldCache_ClearCellIndices, self + field::kWorldCache);
    CallThis<void>(addr::cSC4NetworkTool_ClearOverrideRecords, self);
    Virtual<void>(self, pslot::kCreateCellInfos, type);
    CallThis<void, int32_t>(addr::cSC4NetworkTool_SetStepEdges, self, type);
    if (At<uint32_t>(row, prow::kPenWidthX) > 1u)
        CallThis<void, int32_t>(addr::cSC4NetworkTool_MarkWideSides, self, type);
    if (At<uint8_t>(self, field::kReplacing))
        Trace(self, addr::cSC4NetworkTool_TraceInitialText);
    CallThis<void>(addr::cSC4NetworkTool_ApplyRuleCellRecords, self);
    int32_t count = CallThis<int32_t, int32_t, int32_t>(addr::cSC4NetworkTool_FindRuleIntersections, self,
                                                        At<int32_t>(self, field::kValidSteps), type);
    if (count < At<int32_t>(self, field::kValidSteps)) {
        At<uint32_t>(self, field::kFailure) = kFailIntersections;
        return false;
    }
    CallThis<void>(addr::cSC4NetworkTool_CollectDraggedCells, self);
    if (type == static_cast<int32_t>(kTypeOneWay))
        CallThis<void, int32_t>(addr::cSC4NetworkTool_OneWayRoadPass, self, type);
    CallThis<void>(addr::cSC4NetworkTool_ApplyEdgeConnections, self);
    count = CallThis<int32_t, int32_t, uint32_t>(addr::cSC4NetworkTool_SolveTiles, self, type, 0u);
    if (count < At<int32_t>(self, field::kValidSteps)) {
        At<uint32_t>(self, field::kFailure) = kFailSolve;
        return false;
    }
    if (At<uint8_t>(self, field::kReplacing))
        Trace(self, addr::cSC4NetworkTool_TracePostSolveText);
    const Vector& bridges = VectorAt(self, field::kMajorBridges);
    int32_t radius = bridges.begin != bridges.end ? 1 : 0;
    uint32_t failure;
    do {
        if (!CallThis<uint8_t, int32_t>(addr::cSC4NetworkTool_FitHeights, self, radius))
            failure = kFailHeights;
        else if (Virtual<uint8_t>(self, pslot::kCanTilesBeSupported, type))
            return true;
        else
            failure = kFailSupport;
        ++radius;
    } while (radius < 4);
    At<uint32_t>(self, field::kFailure) = failure;
    return false;
}

// 0x0063B830 (ret 0x14: a place byte, the construction crew or null, a
// word for slot 18, a neighbour connection byte, a network type): the
// segment placer. No step: AL 1. Else +0x50 is the place byte, the edge
// and neighbour connection records emptied, and slot 53 (word, byte) may
// take the placement (AL 1). Then, until no edge connection is added:
// the world cache's cells saved, the major bridges, the +0xE4 records and
// the intersections emptied, the valid step count set to the step count
// and cut by the passes (slot 66, the tunnels, slot 68, slot 59, the major
// bridges, slot 69, each setting its failure when it cuts), the bridge
// pass for a type with a bridge record; with valid steps the build (the
// sets and cell infos, slot 71, the step edges, the wide sides, the rule
// cell records, the intersections, the dragged cells, the one-way pass,
// the edge connections, the solver, the heights over up to four radii and
// slot 56); a failed build empties the valid steps, restores the world
// cache and answers AL 0 unless +0x52 is set. With the byte, all steps
// valid and two anchors or more, an edge connection (AddEdgeConnection)
// marks the intersections and goes round again, once; after one, placing
// commits the neighbour connections. Then a list of the new occupants:
// with valid steps, placing: slot 70, slot 60, slot 57 into a vector of
// changed vertices, the changed cells and vertices resolved into solved
// cells; previewing: the changed cells' preview heights; with preview
// updates on (+0x234) slot 55, the major bridges', the tunnels' and the
// intersections' occupants. Placing: the crew's AddSegment (all steps
// valid, or no regular step, or a one-cell pen) and FinishPieces(list, 1);
// previewing: each new occupant into the highlight set +0x228 (through a
// referenced holder) and the invalid steps marked. The list destroyed; AL
// 1. EAX's upper bytes are not the game's (its callers read AL only:
// 0x00629210 stores it, 0x0063BDC0 hands EAX on to callers that read
// nothing).
uint32_t __fastcall PlaceSegment(char* self, void*, uint32_t place, void* crew, uint32_t word, uint32_t neighbor,
                                 int32_t type)
{
    static bool logged = false;
    FirstCall("PlaceSegment", logged);
    char* const row = Row(static_cast<uint32_t>(type));
    uint8_t edgeDone = 0; // the game's: the low byte of its fifth argument's slot
    if (At<int32_t>(self, field::kStepCount) == 0)
        return 1;
    uint32_t tag = place; // the game hands its first argument's slot as the unread tag
    At<uint8_t>(self, field::kReplacing) = static_cast<uint8_t>(place);
    {
        Vector& edges = VectorAt(self, pfield::kEdgeConnections);
        edges.end = CallCdecl<char*, char*, char*, char*, void*, uint32_t>(addr::SC4CornerVector_CopyRange, edges.end,
                                                                          edges.end, edges.begin, &tag, 0u);
        Vector& neighbors = VectorAt(self, field::kNeighborConnections);
        neighbors.end = CallCdecl<char*, char*, char*, char*, void*, uint32_t>(
            addr::SC4CornerVector_CopyRange, neighbors.end, neighbors.end, neighbors.begin, &tag, 0u);
    }
    if (Virtual<uint8_t>(self, slot::kDoNetworkSpecificPlacement, word, neighbor))
        return 1;
    for (;;) {
        CallThis<void>(addr::cSC4NetworkWorldCache_SaveCells, self + field::kWorldCache);
        {
            Vector& bridges = VectorAt(self, field::kMajorBridges);
            char* const kept = CallCdecl<char*, char*, char*, char*, void*, uint32_t>(
                addr::SC4MajorBridgeVector_CopyRange, bridges.end, bridges.end, bridges.begin, &tag, 0u);
            CallCdecl<void, char*, char*, void*>(addr::cSC4NetworkTool_DestroyMajorBridges, kept, bridges.end, &tag);
            bridges.end = kept;
            Vector& records = VectorAt(self, field::kVectorE4);
            records.end = CallCdecl<char*, char*, char*, char*, void*, uint32_t>(addr::SC4Vector16_Copy, records.end,
                                                                                records.end, records.begin, &tag, 0u);
            Vector& intersections = VectorAt(self, field::kIntersections);
            CallThis<char*, char*, char*>(addr::SC4IntersectionVector_Erase, &intersections, intersections.begin,
                                          intersections.end);
        }
        At<int32_t>(self, field::kValidSteps) = At<int32_t>(self, field::kStepCount);
        int32_t count = At<int32_t>(self, field::kStepCount);
        Virtual<void>(self, pslot::kIdentifyStartingConditions, &count);
        Check(self, count, kFailStart);
        CallThis<void, int32_t*>(addr::cSC4NetworkTool_FindTunnels, self, &count);
        Check(self, count, kFailTunnels);
        Virtual<void>(self, pslot::kCheckForReservedTiles, &count, type, 0u);
        Check(self, count, kFailReserved);
        count = Virtual<int32_t>(self, pslot::kUpdateOccupantHighlights, At<int32_t>(self, field::kValidSteps));
        Check(self, count, kFailReserved);
        CallThis<void, int32_t*, int32_t>(addr::cSC4NetworkTool_FindMajorBridges, self, &count, type);
        Check(self, count, kFailBridges);
        Virtual<void>(self, pslot::kCheckForGoodTerrain, &count);
        Check(self, count, kFailStart);
        if (At<uint8_t>(self, field::kHasBridge))
            CallThis<void, int32_t, int32_t>(addr::cSC4NetworkTool_FitMinorBridges, self,
                                             At<int32_t>(self, field::kValidSteps), type);
        while (At<int32_t>(self, field::kValidSteps) > 0) {
            if (Build(self, row, type))
                break;
            At<int32_t>(self, field::kValidSteps) = 0;
            CallThis<void>(addr::cSC4NetworkWorldCache_RestoreCells, self + field::kWorldCache);
            if (!At<uint8_t>(self, field::kByte52))
                return 0;
        }
        if (static_cast<uint8_t>(neighbor) &&
            At<int32_t>(self, field::kValidSteps) == At<int32_t>(self, field::kStepCount)) {
            if (!edgeDone) {
                const Vector& anchors = VectorAt(self, field::kAnchors);
                const uint32_t points = static_cast<uint32_t>(static_cast<int32_t>(anchors.end - anchors.begin) >> 3);
                if (points >= 2u &&
                    CallThis<uint8_t, uint32_t, int32_t>(addr::cSC4NetworkTool_AddEdgeConnection, self, word, type)) {
                    edgeDone = 1;
                    CallThis<void, uint32_t>(addr::cSC4NetworkTool_MarkIntersections, self, 0u);
                    continue;
                }
            } else if (At<uint8_t>(self, field::kReplacing)) {
                CallThis<void>(addr::cSC4NetworkTool_ApplyNeighborConnections, self);
            }
        }
        break;
    }
    int32_t unread = 0; // the game hands its count variable's address to the list constructor, which ignores it
    ListNode* list = nullptr;
    CallThis<void, int32_t*>(addr::SC4RefList_Construct, &list, &unread);
    CallThis<void>(addr::SC4StlList_Clear, &list);
    if (At<int32_t>(self, field::kValidSteps) > 0) {
        if (At<uint8_t>(self, field::kReplacing)) {
            Vector changed{};
            Virtual<void>(self, pslot::kSlot70);
            Virtual<void>(self, pslot::kDestroyHighlighted);
            Virtual<void>(self, pslot::kUpdateTerrainHeights, &changed);
            CallThis<void>(addr::cSC4NetworkTool_ResolveChangedCells, self);
            CallThis<void, Vector*>(addr::cSC4NetworkTool_ResolveChangedVertices, self, &changed);
            if (changed.begin)
                CallCdecl<void, void*>(addr::SC4_Free, changed.begin);
        } else {
            CallThis<void>(addr::cSC4NetworkTool_PreviewChangedCellHeights, self);
        }
        if (At<uint8_t>(self, field::kPreviewUpdate)) {
            Virtual<void>(self, pslot::kInsertOccupantsForSolvedCells, self + field::kSolved, &list, 0u);
            CallThis<void, ListNode**>(addr::cSC4NetworkTool_PlaceMajorBridgeOccupants, self, &list);
            CallThis<void, int32_t>(addr::cSC4NetworkTool_PlaceTunnels, self, type);
            CallThis<void, ListNode**>(addr::cSC4NetworkTool_PlaceIntersectionOccupants, self, &list);
        }
    }
    if (At<uint8_t>(self, field::kReplacing)) {
        if (crew) {
            const int32_t regular = At<int32_t>(self, pfield::kRegularSteps);
            if (regular == At<int32_t>(self, field::kValidSteps) || regular == 0 ||
                (At<uint32_t>(row, prow::kPenWidthX) == 1u && At<uint32_t>(row, prow::kPenWidthZ) == 1u))
                CallThis<void, ListNode**, char*, char*>(addr::cSC4NetworkConstructionCrew_AddSegment, crew, &list,
                                                         self + field::kSteps, self + field::kCells);
        }
        CallCdecl<void, ListNode**, uint32_t>(addr::cSC4NetworkConstructionCrew_FinishPieces, &list, 1u);
    } else {
        ListNode* const head = list;
        for (ListNode* node = head->next; node != head; node = node->next) {
            void* const occupant = Virtual<void*>(node->value, occupants::oslot::kAsOccupant);
            void* holder = nullptr;
            CallThis<void, void*>(addr::SC4RefHolder_SC4RefHolder, &holder, occupant);
            uint32_t inserted[2] = {};
            CallThis<void, uint32_t*, void**>(addr::SC4OccupantSet_Insert, self + pfield::kHighlightTree, inserted,
                                              &holder);
            if (holder)
                ReleaseOf(holder);
        }
        if (At<uint8_t>(self, field::kPreviewUpdate))
            CallThis<void, int32_t>(addr::cSC4NetworkTool_MarkInvalidSteps, self,
                                    At<int32_t>(self, field::kValidSteps));
    }
    if (At<uint8_t>(self, field::kReplacing))
        Trace(self, addr::cSC4NetworkTool_TraceFinalText);
    CallThis<void>(addr::SC4PairList_Destroy, &list);
    return 1;
}

// ====================================================================================
// Slots 7 and 8
// ====================================================================================

// 0x00629210 (slot 7, ret 0x20: x0, z0, x1, z1, a network mask, a callback
// tool, a place byte, a network type or a negative for the tool's): +0x208
// 3 and no failure for the call; the points clamped to the city; the steps
// emptied, the step counts 0, the world cache reset when placing. Mask 0:
// the anchors are the two clamped points, DrawNetworkLine (0 for its word)
// and the segment placer (the place byte, no crew, 0, 0, the type). Else a
// network filter of the mask (operator new 0x10; a failed allocation null,
// whose mask store crashes as the game's does); a diagonal (x and z both
// differ, raw arguments) ends there; a single cell looks at its four
// neighbours (an occupant of the filter makes the run go along that axis)
// and with none places the cell alone (the pair vector's Clear and
// PushBack, the line, the placer); then for each axis of the run (the
// signs of the raw differences) the cells just before the start and past
// the end: an occupant of another network type there is kept as a pair
// (the end and that cell) for the callback, one of this type (or, when not
// placing, a cached cell of the mask) moves the end onto it; the anchors,
// the line and the placer for that axis. A failure set clears the answer;
// placing with a callback hands it each pair (its slot 7 with 0, 0, the
// place byte and -1). The list, the last occupant and the filter
// released. Last the anchors emptied and +0x208 1. AL the answer, the
// rest of EAX the anchors' end before they were emptied (as the game's).
uint32_t __fastcall PlaceNetworkOccupants(char* self, void*, int32_t x0, int32_t z0, int32_t x1, int32_t z1,
                                          uint32_t mask, void* callback, uint32_t place, int32_t typeArg)
{
    static bool logged = false;
    FirstCall("PlaceNetworkOccupants", logged);
    int32_t type = At<int32_t>(self, field::kType);
    if (typeArg >= 0)
        type = typeArg;
    uint32_t tag = place; // the game hands its seventh argument's slot as the unread tag
    const int32_t lastZ = At<int32_t>(self, field::kCellsZ) - 1;
    At<uint32_t>(self, field::kWord208) = 3;
    At<uint32_t>(self, field::kFailure) = 0;
    uint8_t ok = 1; // the game's: the low byte of its eighth argument's slot
    const int32_t lastX = At<int32_t>(self, field::kCellsX) - 1;
    const int32_t start[2] = {Clamp(x0, lastX), Clamp(z0, lastZ)};
    const int32_t end[2] = {Clamp(x1, lastX), Clamp(z1, lastZ)};
    {
        Vector& steps = VectorAt(self, field::kSteps);
        steps.end = CallCdecl<char*, char*, char*, char*, void*, uint32_t>(addr::SC4Vector12_Copy, steps.end, steps.end,
                                                                          steps.begin, &tag, 0u);
    }
    At<int32_t>(self, field::kStepCount) = 0;
    At<int32_t>(self, pfield::kRegularSteps) = 0;
    if (static_cast<uint8_t>(place))
        CallThis<void>(addr::cSC4NetworkWorldCache_Reset, self + field::kWorldCache);
    if (mask == 0) {
        ClearAnchors(self);
        PushAnchor(self, start, &tag);
        PushAnchor(self, end, &tag);
        ok = DrawLine(self, start, end, 0u, type);
        if (ok)
            ok = static_cast<uint8_t>(PlaceSegmentAt(self, place, nullptr, 0u, 0u, type));
        if (At<uint32_t>(self, field::kFailure))
            ok = 0;
    } else {
        void* const memory = CallCdecl<void*, uint32_t>(addr::SC4_operator_new, life::kFilterSize);
        void* const filter =
            memory ? CallThis<void*>(addr::cSC4NetworkManager_NetworkFilterConstructDefault, memory) : nullptr;
        if (filter)
            AddRefOf(filter);
        int32_t dx = static_cast<int32_t>(static_cast<uint32_t>(x1) - static_cast<uint32_t>(x0));
        int32_t dz = static_cast<int32_t>(static_cast<uint32_t>(z1) - static_cast<uint32_t>(z0));
        At<uint32_t>(filter, 0xC) = mask;
        void* out = nullptr;
        if (dx != 0 && dz != 0) {
            ReleaseOf(filter);
        } else {
            ListNode* const list = CallCdecl<ListNode*, uint32_t>(addr::SC4_stl_allocate, kPairNodeSize);
            list->next = list;
            list->previous = list;
            bool alone = false;
            if (dx == 0 && dz == 0) {
                uint8_t found[4];
                for (uint32_t side = 0; side < 4; ++side) {
                    const int32_t x = occupants::EdgeSideDx(side) + x0;
                    const int32_t z = occupants::EdgeSideDz(side) + z0;
                    found[side] = 0;
                    if (void* const was = out) {
                        out = nullptr;
                        ReleaseOf(was);
                    }
                    found[side] = OccupantAt(self, x, z, filter, &out);
                }
                if (found[0] || found[2])
                    dx = 1;
                if (found[1] || found[3])
                    dz = 1;
                if (dx == 0 && dz == 0) {
                    alone = true;
                    char* const anchors = self + field::kAnchors;
                    CallThis<void>(addr::SC4PairVector_Clear, anchors);
                    CallThis<void, const int32_t*>(addr::SC4PairVector_PushBack, anchors, start);
                    CallThis<void, const int32_t*>(addr::SC4PairVector_PushBack, anchors, end);
                    ok = DrawLine(self, start, end, 0u, type);
                    if (ok)
                        ok = static_cast<uint8_t>(PlaceSegmentAt(self, place, nullptr, 0u, 0u, type));
                }
            }
            if (!alone) {
                int32_t axes[2][2] = {};
                int32_t count = 0;
                if (dx != 0) {
                    axes[0][0] = Sign(dx);
                    axes[0][1] = 0;
                    count = 1;
                }
                if (dz != 0) {
                    axes[count][0] = 0;
                    axes[count][1] = Sign(dz);
                    ++count;
                }
                for (int32_t k = 0; k < count; ++k) {
                    const int32_t stepX = axes[k][0], stepZ = axes[k][1];
                    int32_t p[2] = {start[0], start[1]};
                    const int32_t beforeX = start[0] - stepX, beforeZ = start[1] - stepZ;
                    const int32_t pastZ = stepZ + end[1], pastX = stepX + end[0];
                    int32_t q[2] = {end[0], end[1]};
                    if (void* const was = out) {
                        out = nullptr;
                        ReleaseOf(was);
                    }
                    if (OccupantAt(self, beforeX, beforeZ, filter, &out)) {
                        if (!Virtual<uint8_t>(out, pxslot::kOccupantIsOfType, type))
                            PushPair(list, start[0], start[1], beforeX, beforeZ);
                        else {
                            p[0] = beforeX;
                            p[1] = beforeZ;
                        }
                    } else if (!static_cast<uint8_t>(place)) {
                        char* const cell = CallThis<char*, uint32_t>(
                            addr::cSC4NetworkWorldCache_GetCell, self + field::kWorldCache,
                            (static_cast<uint32_t>(beforeZ) << 16) + static_cast<uint32_t>(beforeX));
                        if (cell && (At<uint32_t>(cell, cell::kFlags) & mask)) {
                            p[0] = beforeX;
                            p[1] = beforeZ;
                        }
                    }
                    if (void* const was = out) {
                        out = nullptr;
                        ReleaseOf(was);
                    }
                    if (OccupantAt(self, pastX, pastZ, filter, &out)) {
                        if (!Virtual<uint8_t>(out, pxslot::kOccupantIsOfType, type))
                            PushPair(list, pastX, pastZ, end[0], end[1]);
                        else {
                            q[0] = pastX;
                            q[1] = pastZ;
                        }
                    } else if (!static_cast<uint8_t>(place)) {
                        char* const cell = CallThis<char*, uint32_t>(
                            addr::cSC4NetworkWorldCache_GetCell, self + field::kWorldCache,
                            (static_cast<uint32_t>(pastZ) << 16) + static_cast<uint32_t>(pastX));
                        if (cell && (At<uint32_t>(cell, cell::kFlags) & mask)) {
                            q[0] = pastX;
                            q[1] = pastZ;
                        }
                    }
                    ClearAnchors(self);
                    PushAnchor(self, p, &tag);
                    PushAnchor(self, q, &tag);
                    ok = DrawLine(self, p, q, 0u, type);
                    if (ok)
                        ok = static_cast<uint8_t>(PlaceSegmentAt(self, place, nullptr, 0u, 0u, type));
                }
            }
            if (At<uint32_t>(self, field::kFailure))
                ok = 0;
            if (At<uint8_t>(self, field::kReplacing) && callback)
                for (ListNode* node = list->next; node != list; node = node->next) {
                    const int32_t* const v = reinterpret_cast<const int32_t*>(reinterpret_cast<char*>(node) + 8);
                    Virtual<uint32_t>(callback, slot::kPlaceNetworkOccupants, v[0], v[1], v[2], v[3], 0u, 0u, place,
                                      -1);
                }
            for (ListNode* node = list->next; node != list;) {
                ListNode* const next = node->next;
                CallCdecl<void, void*>(addr::SC4_Free, node);
                node = next;
            }
            list->next = list;
            list->previous = list;
            CallCdecl<void, void*>(addr::SC4_Free, list);
            if (out)
                ReleaseOf(out);
            ReleaseOf(filter);
        }
    }
    Vector& anchors = VectorAt(self, field::kAnchors);
    const uint32_t oldEnd = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(anchors.end));
    anchors.end = anchors.begin;
    At<uint32_t>(self, field::kWord208) = 1;
    return (oldEnd & 0xFFFFFF00u) | ok;
}

// 0x00624FD0 (slot 8, ret 0x14: x, z, a piece ID, a select byte, a
// network type): a cell info on the stack (its constructor 0x00647850),
// the type's flag, the four heights 0, the vertex indices (row z, x; the
// row stride +0x248), x and z, not in the placement; per corner the
// terrain's altitude at the vertex (slot 0x30) handed as a float to slot 74
// (GetNetworkHeightForTerrainHeight): the height its float, the range low
// float(h - margin) from the unrounded h, the high float(margin + the
// stored height) (margin +0x2C4). +0x50 set for the call (kept before),
// slot 72 with the cell info, the solved cell {ID, 2, key 0} and a new list
// (a 12-byte head from SC4::stl_allocate); each listed occupant made usable
// and its flag 0x4000 cleared when selecting, then released; +0x50 put
// back, the list's nodes and head freed, the cell info's occupant (slot 72
// stores the one it made) released.
void __fastcall PlaceNetworkOccupantById(char* self, void*, int32_t x, int32_t z, uint32_t id, uint32_t select,
                                         uint32_t type)
{
    static bool logged = false;
    FirstCall("PlaceNetworkOccupantById", logged);
    alignas(4) char info[cell::kSize] = {};
    CallThis<char*>(addr::cSC4NetworkCellInfo_Construct, info);
    for (uint32_t i = 0; i < 4; ++i)
        At<uint32_t>(info, cell::kHeights + i * 4) = 0;
    At<uint32_t>(info, cell::kFlags) = TypeFlag(type);
    const uint32_t stride = At<uint32_t>(self, field::kVertexStride);
    const uint32_t first = stride * static_cast<uint32_t>(z) + static_cast<uint32_t>(x);
    At<uint32_t>(info, cell::kVertices + 4) = stride + first;
    At<uint32_t>(info, cell::kVertices + 8) = stride + first + 1;
    At<uint32_t>(info, cell::kVertices) = first;
    At<uint32_t>(info, cell::kVertices + 12) = first + 1;
    At<int32_t>(info, cell::kZ) = z;
    At<int32_t>(info, cell::kX) = x;
    At<uint8_t>(info, cell::kInPlacement) = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        const double altitude = VertexAltitude(Terrain(self), At<uint32_t>(info, cell::kVertices + i * 4));
        const double h = Virtual<double>(self, slot::kGetNetworkHeightForTerrainHeight, static_cast<float>(altitude));
        const float height = static_cast<float>(h);
        At<float>(info, cell::kHeights + i * 4) = height;
        At<float>(info, cell::kRanges + i * 8) =
            static_cast<float>(h - static_cast<double>(At<float>(self, field::kMarginNew)));
        At<float>(info, cell::kRanges + i * 8 + 4) =
            static_cast<float>(static_cast<double>(At<float>(self, field::kMarginNew)) + static_cast<double>(height));
    }
    // The solved cell: the rotation byte 2 (the original's three bytes after
    // it are its stack's; 0 here), the key 0.
    const uint32_t solved[3] = {id, 2u, 0u};
    const uint8_t saved = At<uint8_t>(self, field::kReplacing);
    At<uint8_t>(self, field::kReplacing) = 1;
    ListNode* list = CallCdecl<ListNode*, uint32_t>(addr::SC4_stl_allocate, kListNodeSize);
    list->next = list;
    list->previous = list;
    Virtual<void>(self, pslot::kInsertNetworkOccupant, info, solved, &list);
    ListNode* const head = list;
    for (ListNode* node = head->next; node != head; node = node->next) {
        void* const occupant = node->value;
        if (static_cast<uint8_t>(select)) {
            Virtual<void>(occupant, pslot::kSetUsable, 1u);
            Virtual<void>(occupant, occupants::oslot::kClearFlag, occupants::oflag::kTranslucent);
        }
        ReleaseOf(occupant);
    }
    At<uint8_t>(self, field::kReplacing) = saved;
    for (ListNode* node = list->next; node != list;) {
        ListNode* const next = node->next;
        CallCdecl<void, void*>(addr::SC4_Free, node);
        node = next;
    }
    list->next = list;
    list->previous = list;
    CallCdecl<void, void*>(addr::SC4_Free, list);
    if (void* const occupant = At<void*>(info, cell::kOccupant))
        ReleaseOf(occupant);
}

// ====================================================================================
// The small placing helpers
// ====================================================================================

// 0x00624BF0 (ret 4, the argument not read; SetValidStepCount 0x0062CA10
// jumps here with its own): from the valid step count to the step count
// (re-read), the first such step skipped unless the valid count is 0 or
// one less than the step count; each other step's cells (the last cell
// re-read) erased from the preview drawable and added again in the
// invalid colour (height 15.5 for the highway, else 0). EAX as the game
// leaves it: the step count as last read when the loop ran, else the valid
// count (SetValidStepCount, slot 24, hands it on).
uint32_t __fastcall MarkInvalidSteps(char* self, void*, uint32_t)
{
    static bool logged = false;
    FirstCall("MarkInvalidSteps", logged);
    const int32_t count = At<int32_t>(self, field::kStepCount);
    const int32_t valid = At<int32_t>(self, field::kValidSteps);
    uint8_t skip = 1;
    if (valid == count - 1 || valid == 0)
        skip = 0;
    uint32_t answer = static_cast<uint32_t>(valid);
    for (int32_t i = valid; i < At<int32_t>(self, field::kStepCount);
         ++i, answer = At<uint32_t>(self, field::kStepCount)) {
        if (skip) {
            skip = 0;
            continue;
        }
        for (uint32_t c = At<uint32_t>(Steps(self) + i * step::kSize, 0);
             c <= At<uint32_t>(Steps(self) + i * step::kSize, step::kLastCell); ++c) {
            char* const at = Cells(self) + c * kPointSize;
            uint32_t point[2] = {At<uint32_t>(at, 0), At<uint32_t>(at, 4)};
            CallThis<void, uint32_t*>(addr::cSC4NetworkToolPreview_EraseCell, At<void*>(self, field::kPreview), point);
            const uint32_t height =
                At<uint32_t>(self, field::kType) == kTypeHighway ? kHighwayPreviewHeightBits : 0u;
            CallThis<void, uint32_t*, uint32_t, uint32_t, uint32_t, uint32_t>(
                addr::cSC4NetworkToolPreview_AddCell, At<void*>(self, field::kPreview), point, kInvalidColour, 0u,
                height, 0u);
        }
    }
    return answer;
}

// 0x00624610 (no arguments): for each possible neighbour connection (the
// begin taken once, the end re-read; 0x18-byte records), each network type
// other than the tool's with a flag in the tool's class mask and a
// neighbour connection kind loses the connection at the record's cell
// (the neighbours simulator's slot 0x1C: kind, x, z); then the record's
// own (slot 0x18: its kind, x, z, its byte).
void __fastcall ApplyNeighborConnections(char* self, void*)
{
    static bool logged = false;
    FirstCall("ApplyNeighborConnections", logged);
    void* const neighbors = Global<void*>(addr::SC4_MonthEndService);
    for (char* record = VectorAt(self, field::kNeighborConnections).begin;
         record != At<char*>(self, pfield::kNeighborConnectionsEnd); record += pedge::kSize) {
        for (int32_t type = 0; type < static_cast<int32_t>(kTypes); ++type) {
            if (type == At<int32_t>(self, field::kType))
                continue;
            if (!(TypeFlag(static_cast<uint32_t>(type)) & At<uint32_t>(self, field::kClassMask)))
                continue;
            const uint32_t kind = occupants::NeighborKind(static_cast<uint32_t>(type));
            if (!kind)
                continue;
            Virtual<void>(neighbors, pxslot::kNeighborRemove, kind, At<uint32_t>(record, pedge::kX),
                          At<uint32_t>(record, pedge::kZ));
        }
        Virtual<void>(neighbors, pxslot::kNeighborAdd, At<uint32_t>(record, pedge::kKind),
                      At<uint32_t>(record, pedge::kX), At<uint32_t>(record, pedge::kZ),
                      static_cast<uint32_t>(At<uint8_t>(record, pedge::kByte)));
    }
}

// 0x00626EB0 (no arguments): for each edge connection record (the begin
// taken once, the end re-read), its cell's edge word for the tool's type
// (the cell entry; no null test, as the game's): when any side of it is
// straight (kind 2), each set bit of the record's edge byte moves up one
// (bit 0 of each byte to bit 1); the word ORs the record's edge, then
// CombineEdges. Then the records emptied (SC4CornerVector::CopyRange of
// nothing with the unread tag and 0).
void __fastcall ApplyEdgeConnections(char* self, void*)
{
    static bool logged = false;
    FirstCall("ApplyEdgeConnections", logged);
    uint8_t tag = 0;
    for (char* record = VectorAt(self, pfield::kEdgeConnections).begin;
         record != At<char*>(self, pfield::kEdgeConnectionsEnd); record += pedge::kSize) {
        char* const info = Entry(self, At<uint32_t>(record, pedge::kKey));
        const uint32_t word = At<uint32_t>(info, cell::kByType + At<uint32_t>(self, field::kType) * 4);
        if ((word & 0x3000000) == 0x2000000 || (word & 0x30000) == 0x20000 || (word & 0x300) == 0x200 ||
            (word & 3) == 2) {
            uint32_t edge = At<uint32_t>(record, pedge::kEdge);
            for (uint32_t bit = 1; bit != 0; bit <<= 8)
                if (bit & edge)
                    edge = (bit + bit) | (~bit & edge);
            At<uint32_t>(record, pedge::kEdge) = edge;
        }
        At<uint32_t>(info, cell::kByType + At<uint32_t>(self, field::kType) * 4) =
            At<uint32_t>(record, pedge::kEdge) | word;
        CallThis<void>(addr::cSC4NetworkCellInfo_CombineEdges, info);
    }
    Vector& records = VectorAt(self, pfield::kEdgeConnections);
    records.end = CallCdecl<char*, char*, char*, char*, void*, uint32_t>(addr::SC4CornerVector_CopyRange, records.end,
                                                                        records.end, records.begin, &tag, 0u);
}

// 0x00639040 (no arguments): the records at +0x8 emptied (SC4Vector12::Copy
// of nothing with the unread tag and 0).
void __fastcall ClearOverrideRecords(char* self, void*)
{
    static bool logged = false;
    FirstCall("ClearOverrideRecords", logged);
    uint8_t tag = 0;
    Vector& records = VectorAt(self, pfield::kOverrides);
    records.end = CallCdecl<char*, char*, char*, char*, void*, uint32_t>(addr::SC4Vector12_Copy, records.end,
                                                                        records.end, records.begin, &tag, 0u);
}

// ====================================================================================
// The RUL0 intersections
// ====================================================================================

// 0x00608CD0 (ret 0x10: x, z, the city's cells in x and z; this a RUL0
// rule): EAX 1 when the rule's layout moved to the point is inside the
// city: its smallest x and z (+0x18, +0x1C) plus the point not negative
// (their OR), its largest (+0x20, +0x24) plus the point below the sizes
// (unsigned); else 0.
uint32_t __fastcall FitsInCity(char* rule, void*, uint32_t x, uint32_t z, uint32_t sizeX, uint32_t sizeZ)
{
    static bool logged = false;
    FirstCall("FitsInCity", logged);
    const uint32_t a = At<uint32_t>(rule, rul::rul0::kMinX) + x;
    const uint32_t b = At<uint32_t>(rule, rul::rul0::kMinZ) + z;
    if (static_cast<int32_t>(a | b) < 0)
        return 0;
    if (At<uint32_t>(rule, rul::rul0::kMaxX) + x >= sizeX)
        return 0;
    if (At<uint32_t>(rule, rul::rul0::kMaxZ) + z >= sizeZ)
        return 0;
    return 1;
}

// 0x0063B130 (ret 8: an intersection record (rule ID, x, z), a byte): the
// rule (FindOrInsert with the ID); for each of its checked points (+0x4C,
// the end taken once) the cell at the record's point plus the point's gets
// the byte at +0x52 when it has a cell info.
void __fastcall MarkIntersectionCells(char* self, void*, const uint32_t* record, uint32_t mark)
{
    static bool logged = false;
    FirstCall("MarkIntersectionCells", logged);
    char* const rule = RuleOf(record[0]);
    char* const end = At<char*>(rule, rul::rul0::kChecked + 4);
    for (char* point = At<char*>(rule, rul::rul0::kChecked); point != end; point += rul::point::kSize) {
        const uint32_t key = ((At<uint32_t>(point, rul::point::kZ) + record[2]) << 16) +
                             At<uint32_t>(point, rul::point::kX) + record[1];
        if (char* const info = Entry(self, key))
            At<uint8_t>(info, cell::kHasFlag100000) = static_cast<uint8_t>(mark);
    }
}

// 0x0063B7F0 (ret 4: a byte): MarkIntersectionCells(intersection, byte) for
// each intersection (the end taken once) until the first whose step is not
// below the valid step count.
void __fastcall MarkIntersections(char* self, void*, uint32_t mark)
{
    static bool logged = false;
    FirstCall("MarkIntersections", logged);
    char* const end = At<char*>(self, pfield::kIntersectionsEnd);
    for (char* at = VectorAt(self, field::kIntersections).begin; at != end; at += isect::kSize) {
        if (At<int32_t>(at, isect::kEndStep) >= At<int32_t>(self, field::kValidSteps))
            break;
        CallThis<void, char*, uint32_t>(addr::cSC4NetworkTool_MarkIntersectionCells, self, at, mark);
    }
}

// 0x0062C4B0 (ret 4: a list): PlaceRuleOccupants (x, z, the rule ID, the
// list, +0x50, 0) for each intersection (the end taken once) until the
// first whose step is not below the valid step count.
void __fastcall PlaceIntersectionOccupants(char* self, void*, ListNode** list)
{
    static bool logged = false;
    FirstCall("PlaceIntersectionOccupants", logged);
    char* const end = At<char*>(self, pfield::kIntersectionsEnd);
    for (char* at = VectorAt(self, field::kIntersections).begin; at != end; at += isect::kSize) {
        if (At<int32_t>(at, isect::kEndStep) >= At<int32_t>(self, field::kValidSteps))
            break;
        CallThis<void, uint32_t, uint32_t, uint32_t, ListNode**, uint32_t, uint32_t>(
            addr::cSC4NetworkTool_PlaceRuleOccupants, self, At<uint32_t>(at, isect::kX), At<uint32_t>(at, isect::kZ),
            At<uint32_t>(at, isect::kRule), list, static_cast<uint32_t>(At<uint8_t>(self, field::kReplacing)), 0u);
    }
}

// The intersection record FindRuleIntersections builds on its stack, and
// its appends (0x0063B55D to 0x0063B6F7 and 0x0063B728 to 0x0063B7D3).
struct Pending {
    uint32_t words[isect::kSize / 4]; // the rule ID, x, z, the step, +0x10, +0x14, +0x18, the vector +0x1C
};

// 0x0063B340 (ret 8: the valid step count n, a network type): the
// intersections emptied (Erase of all); with n above 0, for each of the
// first n steps and each of its cells with a cell info that has an
// occupant: the occupant's slot 0x98 with the type (its answer unused),
// then every rule of the RUL0 store (the hash table walked as the game
// inlines it) that is enabled (+0xE9), applies to the cell's networks
// (+0xE4 against the cell's flags), fits in the city at the cell (0x00608CD0)
// and matches there (0x00609A00 answers 0) makes the step a matching one;
// the first match while none is pending opens an intersection (the rule
// ID, x, z, the step) and marks its cells (0x0063B130 with 1). A step
// without a match closes the pending intersection: +0x10 and +0x14 are the
// rule's plus the step (FindOrInsert twice), the record appended to +0xCC
// (in place or through 0x0062BFE0), and {x, z, the type} to +0x8 (in place
// or grown inline). After the steps a pending one is closed the same way
// but appended through 0x0062C0F0 and with {x, z, 4} (in place or through
// InsertOverflow 0x00626240). EAX n.
int32_t __fastcall FindRuleIntersections(char* self, void*, int32_t count, uint32_t type)
{
    static bool logged = false;
    FirstCall("FindRuleIntersections", logged);
    Pending pending{};
    uint8_t open = 0;
    {
        Vector& intersections = VectorAt(self, field::kIntersections);
        CallThis<char*, char*, char*>(addr::SC4IntersectionVector_Erase, &intersections, intersections.begin,
                                      intersections.end);
    }
    if (count <= 0)
        return count;
    char* const store = Store();
    for (int32_t i = 0; i < count; ++i) {
        uint8_t matched = 0;
        for (uint32_t c = At<uint32_t>(Steps(self) + i * step::kSize, 0);
             c <= At<uint32_t>(Steps(self) + i * step::kSize, step::kLastCell); ++c) {
            const uint32_t x = At<uint32_t>(Cells(self) + c * kPointSize, 0);
            const uint32_t z = At<uint32_t>(Cells(self) + c * kPointSize, 4);
            char* const info = Entry(self, (z << 16) + x);
            if (!info || !At<void*>(info, cell::kOccupant))
                continue;
            Virtual<uint32_t>(At<void*>(info, cell::kOccupant), pxslot::kOccupantEdges, type);
            char* node = nullptr;
            {
                char** const begin = At<char**>(store, rul::table::kBuckets);
                char** const bucketsEnd = At<char**>(store, rul::table::kBucketsEnd);
                const uint32_t buckets = static_cast<uint32_t>(
                    static_cast<int32_t>(reinterpret_cast<char*>(bucketsEnd) - reinterpret_cast<char*>(begin)) >> 2);
                for (uint32_t b = 0; b < buckets; ++b)
                    if (begin[b]) {
                        node = begin[b];
                        break;
                    }
            }
            for (; node; node = HashNext(store, node)) {
                char* const rule = node + rul::store::kRule;
                if (!At<uint8_t>(rule, prule::kEnabled))
                    continue;
                if (!(At<uint32_t>(info, cell::kFlags) & At<uint32_t>(rule, prule::kTypes)))
                    continue;
                if (!static_cast<uint8_t>(CallThis<uint32_t, uint32_t, uint32_t, uint32_t, uint32_t>(
                        addr::SC4Rul0Rule_FitsInCity, rule, x, z, At<uint32_t>(self, field::kCellsX),
                        At<uint32_t>(self, field::kCellsZ))))
                    continue;
                if (CallThis<uint32_t, uint32_t, uint32_t, char*>(addr::SC4Rul0Rule_MatchAt, rule, x, z, self))
                    continue;
                matched = 1;
                if (open)
                    continue;
                pending.words[1] = x;
                pending.words[0] = At<uint32_t>(node, rul::table::kKey);
                pending.words[2] = z;
                open = 1;
                pending.words[3] = static_cast<uint32_t>(i);
                CallThis<void, const uint32_t*, uint32_t>(addr::cSC4NetworkTool_MarkIntersectionCells, self,
                                                          pending.words, 1u);
            }
        }
        if (matched || !open)
            continue;
        const uint32_t id = pending.words[0];
        pending.words[4] = At<uint32_t>(RuleOf(id), prule::kWord10) + pending.words[3];
        pending.words[5] = At<uint32_t>(RuleOf(id), prule::kWord14) + pending.words[3];
        {
            Vector& intersections = VectorAt(self, field::kIntersections);
            char* const end = intersections.end;
            if (end != intersections.capacity) {
                if (end) {
                    for (std::size_t w = 0; w < isect::kWords; ++w)
                        At<uint32_t>(end, w * 4) = pending.words[w];
                    CallThis<void, const uint32_t*>(addr::SC4Uint32Vector_CopyConstruct, end + isect::kCells,
                                                    pending.words + isect::kCells / 4);
                }
                intersections.end += isect::kSize;
            } else {
                uint32_t tag = type;
                CallThis<void, char*, const uint32_t*, void*, uint32_t, uint32_t>(
                    addr::SC4IntersectionVector_InsertOverflow, &intersections, end, pending.words, &tag, 1u, 1u);
            }
        }
        const uint32_t record[3] = {pending.words[1], pending.words[2], type};
        PushRecord12(VectorAt(self, pfield::kOverrides), record);
        open = 0;
    }
    if (open) {
        const uint32_t id = pending.words[0];
        pending.words[4] = At<uint32_t>(RuleOf(id), prule::kWord10) + pending.words[3];
        pending.words[5] = At<uint32_t>(RuleOf(id), prule::kWord14) + pending.words[3];
        CallThis<void, const uint32_t*>(addr::SC4IntersectionVector_PushBack, self + field::kIntersections,
                                        pending.words);
        Vector& records = VectorAt(self, pfield::kOverrides);
        const uint32_t record[3] = {pending.words[1], pending.words[2], 4u};
        char* const end = records.end;
        if (end != records.capacity) {
            if (end) {
                At<uint32_t>(end, 0) = record[0];
                At<uint32_t>(end, 4) = record[1];
                At<uint32_t>(end, 8) = record[2];
            }
            records.end = end + 0xC;
        } else {
            uint32_t tag = type;
            CallThis<void, char*, const uint32_t*, void*, uint32_t, uint32_t>(addr::SC4Vector12_InsertOverflow,
                                                                               &records, end, record, &tag, 1u, 1u);
        }
    }
    return count;
}

// ====================================================================================
// Installation
// ====================================================================================

#define OPENSC4_DETOUR(address, fn) OPENSC4_NETTOOL_DETOUR(address, fn)

const Detour kPlace[] = {
    OPENSC4_DETOUR(addr::cSC4NetworkTool_PlaceAllSegments, PlaceAllSegments),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_PlaceSegment, PlaceSegment),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_PlaceNetworkOccupants, PlaceNetworkOccupants),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_PlaceNetworkOccupantById, PlaceNetworkOccupantById),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_MarkInvalidSteps, MarkInvalidSteps),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_ApplyNeighborConnections, ApplyNeighborConnections),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_ApplyEdgeConnections, ApplyEdgeConnections),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_ClearOverrideRecords, ClearOverrideRecords),
};

const Detour kPlaceRules[] = {
    OPENSC4_DETOUR(addr::SC4Rul0Rule_FitsInCity, FitsInCity),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_MarkIntersectionCells, MarkIntersectionCells),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_MarkIntersections, MarkIntersections),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_PlaceIntersectionOccupants, PlaceIntersectionOccupants),
    OPENSC4_DETOUR(addr::cSC4NetworkTool_FindRuleIntersections, FindRuleIntersections),
};

#undef OPENSC4_DETOUR

bool InstallPlace(registry::Mode) { return InstallDetours(kPlace, "place"); }
bool InstallPlaceRules(registry::Mode) { return InstallDetours(kPlaceRules, "placerules"); }

const bool g_registered[] = {
    registry::Register({"simulators.nettool.place", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_PlaceAllSegments,
                        "Our network tool's placing: slot 19 PlaceAllSegments (with the construction crew), the "
                        "segment placer, slot 7 PlaceNetworkOccupants, slot 8 PlaceNetworkOccupantById, "
                        "MarkInvalidSteps, the neighbour and edge connections and the override records' clear (8 "
                        "detours; src/core/nettool_place_1.cpp)",
                        false, InstallPlace}),
    registry::Register({"simulators.nettool.placerules", "simulators/nettool", registry::Kind::Detour,
                        registry::Phase::PostAppInit, addr::cSC4NetworkTool_FindRuleIntersections,
                        "Our network tool's RUL0 intersections while placing: the rule fit test, the search over the "
                        "steps, the cell marks and the intersections' occupants (5 detours; "
                        "src/core/nettool_place_1.cpp)",
                        false, InstallPlaceRules}),
};

} // namespace
} // namespace opensc4::nettool_sim
