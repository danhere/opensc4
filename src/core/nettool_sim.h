// Shared between the parts of our cSC4NetworkTool (build 641): the class map
// of the general network tool, its two vtables slot by slot, its fields, the
// type table row it reads, and the helpers every part uses. The tool is what
// a player's drag of a road, rail, pipe or any other network goes through:
// it turns the dragged cells into solved cells (a piece ID, a rotation and a
// flip per cell), fits the heights, picks bridges and tunnels, previews the
// result and places the pieces. The network manager keeps one per network
// type (13, made by its Init 0x00610C10) and hands them out by GetNetworkTool
// 0x006108D0 (docs/netmgr.md). Nothing of the tool is saved.
//
// Written in parts (docs/nettool.md has the plan). The first part,
// nettool_state.cpp: identity, the getters and setters, the failure state
// and its text, the type row's IDs, the bridge and neighbour queries,
// OccupantExists, the placement parameters' copy and the small cell helpers
// next to them. The second, nettool_cache.cpp: the world cache and the cell
// infos (their layouts below).
//
// THE NAM GUARD. Nine functions of the tool's range stay the game's for good
// because the NAM DLL or sc4fix patch inside them: 0x006099A0, 0x0062CD50,
// 0x00634D70, 0x00636690, 0x00637EB0, 0x0063AF40 (DrawNetworkLine, slot 18),
// 0x0063D1E0, 0x00645980, 0x0065ED80. None is in the first part. NAM also
// calls game functions of the range at their addresses (0x00633220,
// 0x00647A20, 0x00623E60, 0x00636000, 0x00636030, 0x00636360, 0x0065FF60) and
// reads, by layout, this object (+0x1C, +0x54, +0x60, +0x98, +0xCC, +0x124,
// +0x188, +0x240, +0x244, +0x2B5), the 0xBC-byte cell info, the solved cells,
// the type table 0x00B452C8, the RUL0 rule objects and the RUL2 multimap. So
// every part keeps all of these in the game's memory with the game's layout,
// keeps the conventions of the functions NAM calls (a detour there is fine:
// NAM then calls ours) and caches nothing across a call into the game.
//
// === cSC4NetworkTool =======================================================
// 0x368 bytes; constructor 0x0062C5F0 (ret 4: the network type), deleting
// destructor 0x0062CD30 (slot 51). No class ID of its own, no serializable
// part. Code 0x00620F00 to 0x00644F3F with helpers at 0x0061F5D0 to
// 0x00620ECF, interleaved with the RUL engine's loader and shared template
// code. Three classes derive from it: the underground tools' base
// (constructor 0x00657FD0; vtable 0x00AAA950, not made by itself), the pipe
// tool (0x398 bytes, 0x00648EC0, vtable 0x00AA99A8), the subway tool (0x398,
// 0x00653D60, vtable 0x00AAA5C0) and the power line tool (0x3B8, 0x006503C0,
// vtable 0x00AA9F30). Their vtables keep the general tool's code in most
// slots; a function of this class is ours in all of them when it is a
// detour, and a slot swap goes into every one of the five vtables that holds
// the game's address.
//
// QueryInterface (0x00623290): IIDs 1 and 0x0A74CA15 (the tool's own) give
// the object, 0x452294AA (cIGZMessageTarget2) the part at +4; each
// referenced through slot 1.
//
// --- +0x0: cISC4NetworkTool + cSC4NetworkTool, vtable 0x00AA8748, 81 slots --
// gzcom-dll's cISC4NetworkTool.h has slots 0 to 50 in the right order, but
// slot 10 (UpdateOnZoneChange) takes five arguments (`ret 0x14`), not none;
// the deleting destructor (slot 51) is not in the headers; cSC4NetworkTool.h's
// 29 methods are slots 52 to 80. Argument counts are each function's own
// `ret N`; "=" marks code shared with other classes.
//    0 QueryInterface(iid, out) 0x00623290       1 =AddRef 0x00695330
//    2 Release 0x00623270                        3 Init 0x006269C0
//    4 Shutdown 0x00626D30                       5 Reset 0x00626F90
//    6 GetUITool(out) 0x00623B20                 7 PlaceNetworkOccupants(8) 0x00629210
//    8 PlaceNetworkOccupantById(5) 0x00624FD0    9 OccupantExists(4) 0x006248D0
//   10 UpdateOnZoneChange(5) 0x0062C900 (`ret 0x14`, three bytes)
//   11 ForceReload 0x0062C8C0 (the byte +0x53 set)
//   12 =SetTraceFinal 0x00648F10 (`ret 4`)       13 ClearFailureReason 0x0062C8E0
//   14 GetFailureState 0x0062C8F0                15 GetFailureReasonString(text) 0x0062CA40
//   16 ClearOccupantHighlights 0x00625580        17 ClearCellPreviews 0x00625F10
//   18 DrawNetworkLine(4) 0x0063AF40 (GUARDED)   19 PlaceAllSegments(4) 0x0063BDC0
//   20 GetCostOfSolution 0x00625640              21 EnablePreviewUpdate(byte) 0x0062C8D0
//   22 SetAnchorPoints(points) 0x0062CA20 (`add ecx, 0x118; jmp 0x00611B00`)
//   23 GetDraggedCells(out) 0x0062CA30           24 SetValidStepCount(n) 0x0062CA10
//   25 =GetValidStepCount 0x00407AB0             26 =GetStraightStepCount 0x00407AC0
//   27 =GetTotalStepCount 0x00809240             28 IsRegularStep(i) 0x0062C9C0
//   29 GetLastValidPoint(out) 0x0062C9E0         30 GetPossibleBridges(out) 0x00626E80
//   31 =GetPreferredBridge 0x00623B70 (FOLDED: also slot 38 of cSC4City's vtable 0x00A87620,
//      GetNetworkManager: the city keeps its network manager at +0x200 too)
//   32 =SetPreferredBridge(id) 0x00623B80 (FOLDED: in 19 other vtables)
//   33 GetBridgeAltitudeOverride 0x00623B90 (a float in ST0)
//   34 SetBridgeAltitudeOverride(float) 0x00623BA0
//   35 GetBridgeName(text, id) 0x00624AF0        36 GetMajorBridge 0x00624130
//   37 GetMajorBridgeAltitude(out) 0x00624180    38 GetMajorBridgeReferenceAltitude(out) 0x00624150
//   39 IsMajorBridgeLandBridge 0x006241B0        40 GetBridgeBitmapKey(3) 0x00624B20
//   41 GetPossibleNeighborConnection(out) 0x006241D0
//   42 ViewInputControlId 0x0062C910             43 CursorId 0x0062C930
//   44 DirectionalCursorBase 0x0062C940          45 PenWidth(x out, z out) 0x0062C950
//   46 =StopPickAtWater 0x00648F00 (true)        47 GetToolViewId 0x0062C920
//   48 GetPlacementParams(out) 0x0062CAD0        49 QueryUserForPotentialIntersections 0x0062D450
//   50 DestroyOccupantsAndResolve(list) 0x0062A5C0
//   51 the deleting destructor(flags) 0x0062CD30
//   52 GetFailureReasonString(id, text) 0x0062BF60 (`this` not read; the power line tool's own)
//   53 =DoNetworkSpecificPlacement(2) 0x0062C980 ("xor al, al; ret 8", FOLDED: about 70 vtables)
//   54 CreateNetworkOccupant(4) 0x00624210       55 InsertOccupantsForSolvedCells(3) 0x006258C0
//   56 0x00644B20 (1)   57 0x00644530 (1)   58 AddCellPreview(2) 0x006246A0
//   59 UpdateOccupantHighlights(1) 0x006299A0    60 DestroyHighlightedOccupants 0x00624920
//   61 to 63 =0x00648F00 (true), 64 =0x00648EF0 (false): AllowBridges, AllowTunnels,
//      IsStoppedByWater, ShouldAutoConnect
//   65 0x00638680 (2)   66 0x00638710 (1)   67 0x006392F0 (3)   68 0x00639170 (3)   69 0x00638990 (1)
//   70 =0x00407990 (no-op)   71 0x00633250 (1)   72 InsertNetworkOccupant(3) 0x006278A0
//   73 0x006444F0 (1)   74 GetNetworkHeightForTerrainHeight(float) 0x0062C990 (the float back in ST0)
//   75 =0x00407990   76 0x006360C0 (1)   77 0x00637B90 (1)   78 and 79 =0x00648F10 (`ret 4`)
//   80 SetQuadTrianglesToDraw(2) 0x00623CE0
// --- +0x4: cIGZMessageTarget2, vtable 0x00AA8738, 4 slots -------------------
//    thunks 0x0062C9A0 (QueryInterface), =0x005200D0 (AddRef), 0x0062C9B0
//    (Release); 3 DoMessage 0x00624480 (stores the first data word of the
//    message 0xA6B79602 at +0xFC)
//
// Fields (offsets from the object's start). Evidence: the constructor
// 0x0062C5F0, Init 0x006269C0, Reset 0x00626F90, Shutdown 0x00626D30 and the
// functions named; "(inf)" where the role is inferred.
//   +0x08..+0x10 three words the constructor clears (inf: unused here)
//   +0x14 uint32 reference count (unsigned compare in Release)
//   +0x18 uint8  initialised (Init sets it once, Shutdown clears it)
//   +0x1C the world cache (cSC4NetworkWorldCache, 0x28 bytes, constructor
//         0x00648000; NAM calls its GetCell 0x00647A20)
//   +0x44 the terrain (0x00B43CF4), +0x48 the occupant manager (0x00B43D0C),
//         +0x4C what the city's slot 33 gives: each referenced by Init,
//         released by Shutdown
//   +0x50 uint8 set by the network manager's RemoveOrReplaceOccupant,
//         +0x52 uint8 1, +0x53 uint8 ForceReload's
//   +0x54 the drag's steps (vector of 0xC: first cell, last cell, a word
//         that is 0 for a regular step), +0x60 the dragged cells (vector of
//         8-byte points), +0x70 the step count (inf), +0x78 the valid step
//         count (SetValidStepCount)
//   +0x84, +0xA4, +0xB8 hash sets (0x14 bytes, 100 buckets by 0x00829BC0;
//         the count at +0x10)
//   +0x98 the solved cells (vector of 0xC), +0xCC the intersections (vector
//         of 0x28), +0xD8 the major bridges (vector; GetCostOfSolution
//         walks it in steps of 0x28, the getters read the last element's
//         +0xC, +0x10, +0x14 and the byte +0x18 as end - 0x1C to end - 0x10),
//         +0xE4 a vector of 0x10 (inf: tunnels), +0x10C the possible
//         neighbour connections (vector; the getter copies the first one's
//         +0x8 to +0x14)
//   +0xFC the word of the message 0xA6B79602 (DoMessage)
//   +0x118 the anchor points (vector of 8-byte points)
//   +0x124 and +0x188 two vertex height constraint satisfiers (0x64 each)
//   +0x1EC the mask of the other surface networks (~flag(type) & 0x1F4F)
//   +0x1F0 the preview drawable (0x58, made by Init)
//   +0x1F4 the possible bridges (vector of uint32)
//   +0x200 int32 the preferred bridge (-1), +0x204 float the bridge
//         altitude override (-1.0), +0x208 uint32 1
//   +0x20C uint32 the failure reason (0 or 0xF00000nn; the constructor does
//         not write it: Reset clears it through slot 13)
//   +0x210..+0x218 and +0x224..+0x22C two trees (head node at +0x214 and
//         +0x228, count at +0x218 and +0x22C; inf: cell previews and
//         highlights)
//   +0x234 uint8 EnablePreviewUpdate's (1)
//   +0x238 float the terrain's slot 6, +0x23C a float from it and +0x2A8
//   +0x240, +0x244 the city's cells in x and z; +0x248 to +0x28C sizes and
//         steps Init derives from them
//   +0x290 the placement parameters (0xAC bytes, copied from the type row's
//         +0x50 by Init; their byte +0x25, the object's +0x2B5, is the
//         diagonal flag NAM's two diagonal street patches test)
//   +0x33C..+0x350 what 0x0062F9D0 fills from the type row's bridges
//   +0x354 uint8 the type has a bridge record
//   +0x358 the type's row of the type table (Init sets it; null before)
//   +0x35C int32 the network type
//   +0x360 the network mask of the type's class (0x1F4F, or the type's flag
//         when it is not a surface network), +0x364 a network occupant
//         filter of that mask (0x10 bytes, 0x0061F510)
//
// === The type table =========================================================
// 0x00B452C8: 13 rows of 0x114 bytes, one per network type, filled by the
// RUL engine's load (0x006253D0). What the first part reads of a row: +0x8
// the view input control ID, +0x10 the tool view ID, +0x14 the cursor ID,
// +0x18 the directional cursor base, +0x1C and +0x20 the pen width in x and
// z, +0x30 the bridge records (a vector of 0x178-byte records: +0x148 and
// +0x14C the bitmap's group and instance, +0x158 the name, a game string
// whose characters' begin, +0x15C, GetBridgeName hands on),
// +0x50 the placement parameters.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim {

inline constexpr uint32_t kIidUnknown = 0x00000001;
inline constexpr uint32_t kIidTool = 0x0A74CA15;
inline constexpr uint32_t kIidMessageTarget = 0x452294AA;
inline constexpr uint32_t kIidNetworkOccupant = 0xA821EF94; // what 0x00624B70 asks a cell's occupant for

inline constexpr std::size_t kObjectSize = 0x368;
inline constexpr std::size_t kMessagePart = 0x4;
inline constexpr std::size_t kSlots = 81;
inline constexpr std::size_t kMessageSlots = 4;
inline constexpr uint32_t kTypes = 13;
inline constexpr std::size_t kPipeToolSize = 0x398, kSubwayToolSize = 0x398, kPowerLineToolSize = 0x3B8;

// The vtable of the part at +4 (the primary ones are address entries).
inline constexpr std::uintptr_t kMessageVtable = 0x00AA8738;

// The types whose tools are of a class of their own.
inline constexpr uint32_t kPipeType = 4, kPowerLineType = 5, kSubwayType = 7;
// The surface networks (every type but pipe, power line and subway).
inline constexpr uint32_t kSurfaceMask = 0x1F4F;
// The first type GetFirstNetworkTypeFromFlags never tests: its answer when
// none of types 0 to 11 has a flag in the mask.
inline constexpr uint32_t kTypesTested = 12;

namespace field {
inline constexpr std::size_t kRefCount = 0x14;
inline constexpr std::size_t kInitialized = 0x18;
inline constexpr std::size_t kWorldCache = 0x1C;
inline constexpr std::size_t kTerrain = 0x44, kOccupants = 0x48, kCityObject4C = 0x4C;
inline constexpr std::size_t kReplacing = 0x50, kByte52 = 0x52, kReload = 0x53;
inline constexpr std::size_t kSteps = 0x54, kCells = 0x60, kStepCount = 0x70, kValidSteps = 0x78;
inline constexpr std::size_t kSet84 = 0x84, kSolved = 0x98, kSetA4 = 0xA4, kSetB8 = 0xB8;
inline constexpr std::size_t kIntersections = 0xCC, kMajorBridges = 0xD8, kVectorE4 = 0xE4;
inline constexpr std::size_t kRefreshWord = 0xFC, kNeighborConnections = 0x10C, kAnchors = 0x118;
inline constexpr std::size_t kSatisfier1 = 0x124, kSatisfier2 = 0x188;
inline constexpr std::size_t kOtherSurfaceMask = 0x1EC, kPreview = 0x1F0, kPossibleBridges = 0x1F4;
inline constexpr std::size_t kPreferredBridge = 0x200, kAltitudeOverride = 0x204, kWord208 = 0x208;
inline constexpr std::size_t kFailure = 0x20C;
inline constexpr std::size_t kPreviewCount = 0x218, kHighlightCount = 0x22C;
inline constexpr std::size_t kPreviewUpdate = 0x234;
inline constexpr std::size_t kCellsX = 0x240, kCellsZ = 0x244;
inline constexpr std::size_t kVertexStride = 0x248; // vertices per row (the cell makers' index)
inline constexpr std::size_t kParams = 0x290;
// Two floats of the placement parameters (+0x14 and +0x34): the margins
// slot 71 puts around the heights of a cell already cached and of a new one.
inline constexpr std::size_t kMarginCached = 0x2A4, kMarginNew = 0x2C4;
inline constexpr std::size_t kRuleCellRecords = 0xF0; // a vector of 0x14-byte records (0x00629170)
inline constexpr std::size_t kHasBridge = 0x354, kRow = 0x358, kType = 0x35C;
inline constexpr std::size_t kClassMask = 0x360, kFilter = 0x364;
} // namespace field

// What the constructor and Reset leave in the two bridge settings.
inline constexpr int32_t kNoPreferredBridge = -1;
inline constexpr uint32_t kNoAltitudeOverrideBits = 0xBF800000; // -1.0f

// A step of the drag (the vector at +0x54): its third word is 0 for a
// regular step; GetLastValidPoint takes the second word of the last valid
// step as an index into the dragged cells.
namespace step {
inline constexpr std::size_t kSize = 0xC, kLastCell = 0x4, kIrregular = 0x8;
} // namespace step
inline constexpr std::size_t kPointSize = 8;

// The major bridges' getters read the vector's last element from its end.
namespace majorbridge {
inline constexpr std::ptrdiff_t kId = -0x1C, kAltitude = -0x18, kReferenceAltitude = -0x14, kLandBridge = -0x10;
inline constexpr std::size_t kStride = 0x28; // GetCostOfSolution's step (t7)
} // namespace majorbridge

// A possible neighbour connection: GetPossibleNeighborConnection copies the
// first one's words +0x8, +0xC, +0x10 and its byte +0x14 into the caller's
// 0xD bytes.
namespace neighbor {
inline constexpr std::size_t kFrom = 0x8, kOutSize = 0xD;
} // namespace neighbor

// The type table and what the first part reads of a row and of its bridge
// records.
inline constexpr std::size_t kRowSize = 0x114;
namespace row {
inline constexpr std::size_t kViewInputControlId = 0x8, kToolViewId = 0x10, kCursorId = 0x14;
inline constexpr std::size_t kDirectionalCursorBase = 0x18, kPenWidthX = 0x1C, kPenWidthZ = 0x20;
inline constexpr std::size_t kBridges = 0x30, kBridgesEnd = 0x34, kParams = 0x50;
} // namespace row
namespace bridge {
inline constexpr std::size_t kSize = 0x178, kBitmapGroup = 0x148, kBitmapInstance = 0x14C, kName = 0x15C;
} // namespace bridge

// The placement parameters (0xAC bytes; the tool's +0x290 and the type
// row's +0x50): words, two bytes, uint32 vectors (0x00462C10 assigns them)
// at +0x28, +0x54, +0x60 and +0xA0, a float vector (0x00464430) at +0x6C.
namespace params {
inline constexpr std::size_t kSize = 0xAC;
inline constexpr std::size_t kByte24 = 0x24, kDiagonal = 0x25, kVector28 = 0x28, kByte44 = 0x44;
inline constexpr std::size_t kVector54 = 0x54, kVector60 = 0x60, kFloats6C = 0x6C, kVectorA0 = 0xA0;
} // namespace params

// The cell info (0xBC bytes, the world cache's; NAM's cSC4NetworkCellInfo
// has the names in brackets). Evidence: the makers 0x00633030 (from a
// network occupant), 0x00632EF0 (for a network type) and slot 71
// 0x00633250 (for the current placement), the constructor 0x00647850, the
// copy 0x006478A0 and the assignment 0x00632D50 (nettool_cache.cpp).
//   +0x00 x, +0x04 z (the cell)
//   +0x08 network flags [networkTypeFlags]: the occupant's slot 0x30 & 0x1FFF,
//         or the type's flag (0x00AA8980); slot 71 ORs 0x40000 (step kind 1)
//         or 0x80000 (kind 2)
//   +0x0C the network occupant, referenced [networkOccupant]
//   +0x10 uint8 in the current placement (slot 71 sets it)
//   +0x14 the index of the step (slot 71), -1 from an occupant, 0 by type
//   +0x18 the edge words by network type, 13 [edgesPerNetwork] (occupant
//         slot 0x98 for the types its slot 0x58 says it is)
//   +0x4C the 13 words ORed (0x00647830) [edgeFlagsCombined]
//   +0x50 uint8 the occupant's slot 0x114 (its one-way byte, inf)
//   +0x51 uint8 made from an existing occupant (OccupantExists' test)
//   +0x52 uint8 the occupant has the flag 0x100000 (slot 0x20)
//   +0x53 uint8 immovable: the occupant's slot 0x44, or the step's kind is
//         not 0 [isImmovable]
//   +0x54 uint8 0; +0x55 uint8 the step's kind is 3
//   +0x56 uint8 the occupant has the flag 0x200000 [isNetworkLot]
//   +0x57 uint8 0
//   +0x58 four corner heights (floats: occupant slot 0x88 or the terrain)
//   +0x68 four pairs of floats (low, high) per corner, +0x88 four more
//   +0xA8 the four corners' vertex indices NW, SW, SE, NE [vertices]
//   +0xB8 int32 the index into the solved cells, -1 [idxInCellsBuffer]
// The bytes +0x11 to +0x13 are padding (no function copies them).
namespace cell {
inline constexpr std::size_t kSize = 0xBC;
inline constexpr std::size_t kX = 0x0, kZ = 0x4, kFlags = 0x8, kOccupant = 0xC, kInPlacement = 0x10;
inline constexpr std::size_t kStep = 0x14, kByType = 0x18, kEdgeWords = 13, kCombined = 0x4C;
inline constexpr std::size_t kOneWay = 0x50, kOccupied = 0x51, kHasFlag100000 = 0x52, kImmovable = 0x53;
inline constexpr std::size_t kByte54 = 0x54, kKind3 = 0x55, kNetworkLot = 0x56, kByte57 = 0x57;
inline constexpr std::size_t kHeights = 0x58, kRanges = 0x68, kRanges2 = 0x88, kVertices = 0xA8;
inline constexpr std::size_t kSolvedIndex = 0xB8;
inline constexpr uint32_t kStepKind1Flag = 0x40000, kStepKind2Flag = 0x80000;
inline constexpr uint32_t kOccupantFlagMask = 0x1FFF;
inline constexpr uint32_t kOccupantFlag100000 = 0x100000, kOccupantFlagNetworkLot = 0x200000;
// What 0x00633030 starts the pairs with (-FLT_MAX, FLT_MAX), and slot 71
// the second pairs of a cell it makes.
inline constexpr uint32_t kLowestBits = 0xFF7FFFFF, kHighestBits = 0x7F7FFFFF;
inline constexpr uint32_t kNewLowBits = 0xFDFFFFFF, kNewHighBits = 0x7DFFFFFF;
} // namespace cell

// The world cache (cSC4NetworkWorldCache, 0x28 bytes at the tool's +0x1C,
// constructor 0x00648000): two hash tables of cell infos keyed by
// (z << 16) + x, the cells at +0x0 (GetCell 0x00647A20 looks only there)
// and a copy at +0x14 (SaveCells 0x00647E40 and RestoreCells 0x00647E20).
// A table: +0x0 not written (inf: the hash functor), +0x4 the bucket vector
// (begin, end, capacity; 100 buckets from 0x00829BC0), +0x10 the count. A
// node is 0xC4 bytes from the STL allocator: +0x0 the next node, +0x4 the
// key, +0x8 the cell info.
namespace cache {
inline constexpr std::size_t kSize = 0x28, kMain = 0x0, kBackup = 0x14;
inline constexpr std::size_t kBuckets = 0x4, kCount = 0x10;
inline constexpr uint32_t kFirstBuckets = 100;
} // namespace cache
namespace node {
inline constexpr std::size_t kSize = 0xC4, kNext = 0x0, kKey = 0x4, kValue = 0x8;
} // namespace node

// The little object of the vtable 0x00AA8700 (constructor 0x00627080, t4):
// a cell and the tool, for its two slots that look at a neighbouring cell
// (0x00623F40 and 0x00623EE0).
namespace walker {
inline constexpr std::size_t kSize = 0x28, kX = 0x14, kZ = 0x18, kTool = 0x1C, kType = 0x20, kMask = 0x24;
inline constexpr std::size_t kIsClearSlot = 0x0, kEdgesSlot = 0x4;
} // namespace walker

// A RUL0 rule as 0x006097E0 reads it: +0x58 a vector of 0xC-byte cells (x
// and z offsets, a byte key), +0x68 the bucket vector of a hash table keyed
// by that byte (node: +0x0 next, +0x4 the key, +0x8 two signed network
// types, +0xC two pairs of words (edge pattern, mask), +0x1D a byte), +0xE8
// an index into the byte table 0x00AA752C.
namespace rule {
inline constexpr std::size_t kCells = 0x58, kCellSize = 0xC, kCellKey = 0x8, kBuckets = 0x68, kRotation = 0xE8;
inline constexpr std::size_t kNodeKey = 0x4, kNodeTypes = 0x8, kNodePairs = 0xC, kNodeByte1D = 0x1D;
inline constexpr int8_t kOneWayType = 10;
} // namespace rule

// The terrain's slots (+0x44) and the water's (0x00B43CF0) the cell code
// calls; and the network occupant's.
namespace terrain_slot {
inline constexpr std::size_t kVertexCell = 0x28, kVertexAltitude = 0x30;
} // namespace terrain_slot
namespace water_slot {
inline constexpr std::size_t kAltitude = 0x28;
} // namespace water_slot
namespace netocc_slot {
inline constexpr std::size_t kHasFlag = 0x20, kFlags = 0x30, kIsImmovable = 0x44, kIsOfType = 0x58;
inline constexpr std::size_t kVertexHeights = 0x88, kEdges = 0x98, kOneWay = 0x114;
} // namespace netocc_slot

// The failure texts: LTEXT group 0x2A592FD1, instance = the failure reason.
inline constexpr uint32_t kFailureTextGroup = 0x2A592FD1;
inline constexpr std::size_t kFirstTextBuffer = 8; // what GetFailureReasonString allocates for its string

// The tool's slots (byte offsets in the vtable 0x00AA8748).
namespace slot {
inline constexpr std::size_t kQueryInterface = 0x00, kAddRef = 0x04, kRelease = 0x08, kInit = 0x0C;
inline constexpr std::size_t kShutdown = 0x10, kReset = 0x14, kGetUITool = 0x18;
inline constexpr std::size_t kPlaceNetworkOccupants = 0x1C, kPlaceNetworkOccupantById = 0x20;
inline constexpr std::size_t kOccupantExists = 0x24, kUpdateOnZoneChange = 0x28, kForceReload = 0x2C;
inline constexpr std::size_t kSetTraceFinal = 0x30, kClearFailureReason = 0x34, kGetFailureState = 0x38;
inline constexpr std::size_t kGetFailureReasonString = 0x3C, kClearOccupantHighlights = 0x40;
inline constexpr std::size_t kClearCellPreviews = 0x44, kDrawNetworkLine = 0x48, kPlaceAllSegments = 0x4C;
inline constexpr std::size_t kGetCostOfSolution = 0x50, kEnablePreviewUpdate = 0x54, kSetAnchorPoints = 0x58;
inline constexpr std::size_t kGetDraggedCells = 0x5C, kSetValidStepCount = 0x60, kGetValidStepCount = 0x64;
inline constexpr std::size_t kGetStraightStepCount = 0x68, kGetTotalStepCount = 0x6C, kIsRegularStep = 0x70;
inline constexpr std::size_t kGetLastValidPoint = 0x74, kGetPossibleBridges = 0x78, kGetPreferredBridge = 0x7C;
inline constexpr std::size_t kSetPreferredBridge = 0x80, kGetBridgeAltitudeOverride = 0x84;
inline constexpr std::size_t kSetBridgeAltitudeOverride = 0x88, kGetBridgeName = 0x8C, kGetMajorBridge = 0x90;
inline constexpr std::size_t kGetMajorBridgeAltitude = 0x94, kGetMajorBridgeReferenceAltitude = 0x98;
inline constexpr std::size_t kIsMajorBridgeLandBridge = 0x9C, kGetBridgeBitmapKey = 0xA0;
inline constexpr std::size_t kGetPossibleNeighborConnection = 0xA4, kViewInputControlId = 0xA8;
inline constexpr std::size_t kCursorId = 0xAC, kDirectionalCursorBase = 0xB0, kPenWidth = 0xB4;
inline constexpr std::size_t kStopPickAtWater = 0xB8, kGetToolViewId = 0xBC, kGetPlacementParams = 0xC0;
inline constexpr std::size_t kQueryUserForPotentialIntersections = 0xC4, kDestroyOccupantsAndResolve = 0xC8;
inline constexpr std::size_t kDeletingDestructor = 0xCC, kGetFailureReasonText = 0xD0;
inline constexpr std::size_t kDoNetworkSpecificPlacement = 0xD4;
inline constexpr std::size_t kGetNetworkHeightForTerrainHeight = 0x128;
// The part at +4.
inline constexpr std::size_t kDoMessage = 0x0C;
} // namespace slot
// The same as vtable indices, for the slot swaps.
namespace slot_index {
inline constexpr int kUpdateOnZoneChange = 10, kGetPreferredBridge = 31, kSetPreferredBridge = 32;
inline constexpr int kDoNetworkSpecificPlacement = 53;
} // namespace slot_index

// === The third part (nettool_life.cpp) =====================================
// The fields the constructor, Init, Shutdown, Reset and the destructor
// touch beyond the ones above (offsets from the tool's start):
//   +0x08 a pointer the destructor frees (the constructor clears it)
//   +0xF0 the rule cell records (vector of 0x14; Reset empties it through
//         0x00624070), +0xE4, +0x100 vectors the destructor frees
//   +0x210 a preview list object (ClearCellPreviews frees it: +0x10 a
//         vtable it resets to 0x00A86364, +0x14 a count, +0x20 an array of
//         that many objects from operator new)
//   +0x214 / +0x218 the cell previews' tree (head node, count; nodes hold
//         an object at +0x10 whose slot 0x44 ClearCellPreviews calls with
//         two zeros), +0x228 / +0x22C the highlighted occupants' tree (nodes
//         hold a referenced occupant at +0x10)
//   +0x238 float the terrain's slot 0x18, +0x23C tan(+0x2A8) * +0x238
//   +0x248, +0x24C the vertices per row and per column (cells + 1);
//         +0x250 to +0x28C sixteen words Init derives from them (inf: the
//         vertex index steps of the four corners)
//   +0x2A8 a float of the placement parameters (their +0x18)
//   +0x33C, +0x348 the indices of the type's bridge records whose byte
//         +0x99 and +0x98 is set (vectors of uint32; 0x0062F9D0)
namespace life {
inline constexpr std::size_t kFreedPointer = 0x08, kRuleCells = 0xF0, kVectorE4 = 0xE4, kVector100 = 0x100;
inline constexpr std::size_t kPreviewList = 0x210, kCellPreviews = 0x214, kHighlights = 0x228;
inline constexpr std::size_t kTerrainScale = 0x238, kTerrainTan = 0x23C, kVerticesX = 0x248, kVerticesZ = 0x24C;
inline constexpr std::size_t kVertexSteps = 0x250, kAngle = 0x2A8;
inline constexpr std::size_t kBridgesBy99 = 0x33C, kBridgesBy98 = 0x348;
inline constexpr uint32_t kRefreshMessage = 0xA6B79602;
inline constexpr std::size_t kTreeNodeSize = 0x14; // the tool's two trees' head nodes
inline constexpr std::size_t kSatisfierSize = 0x64;
inline constexpr uint32_t kTempOccupantSize = 0x180, kUIToolSize = 0x134, kFilterSize = 0x10;
// What Init gives the render service's slot 0x80 with the preview drawable.
inline constexpr uint32_t kPreviewLayer = 5, kPreviewPriority = 1000;
// The vtables the constructors write before their final ones, and the one
// ClearCellPreviews writes into the preview list.
inline constexpr std::uintptr_t kToolFirstMessageVtable = 0x00A81174, kPreviewFirstVtable = 0x00A881C0;
inline constexpr std::uintptr_t kPreviewListVtable = 0x00A86364;
} // namespace life

// A bridge record's two bytes 0x0062F9D0 sorts by.
namespace bridge_kind {
inline constexpr std::size_t kByte98 = 0x98, kByte99 = 0x99;
} // namespace bridge_kind

// The preview drawable (0x58 bytes, constructor 0x00620AD0, made by Init and
// kept at +0x1F0, registered with the render service 0x00B43DD0 at layer 5).
// +0x0 its own vtable 0x00AA8314, +0x4 a cRZBaseUnknown part (vtable
// 0x00AA82FC, the reference count at +0x8), +0xC a material object made by
// the render state setup 0x0061FD10 (released when +0x54 is set), +0x10 /
// +0x14 a tree of cell nodes (0x7C bytes; head and count), +0x1C / +0x20 a
// tree of textured nodes (0x80 bytes; a texture binding at +0x7C), +0x28 to
// +0x44 four 8-byte render states the setup fills (0x007D2B50), +0x48 to
// +0x53 bytes of render state, +0x54 the setup has run. Its drawing
// (0x0061FD10, 0x00620160, 0x00620500) stays the game's.
namespace preview {
inline constexpr std::size_t kSize = 0x58, kBase = 0x4, kMaterial = 0xC;
inline constexpr std::size_t kCells = 0x10, kTextured = 0x1C, kStates = 0x28, kStatesEnd = 0x48;
inline constexpr std::size_t kSetUp = 0x54;
inline constexpr std::size_t kCellNodeSize = 0x7C, kTexturedNodeSize = 0x80, kTexture = 0x7C;
} // namespace preview

// A tree as the game's STL keeps it (the tool's and the drawable's): the
// object is +0x0 the head node, +0x4 the count; a node is +0x0 its colour,
// +0x4 the parent, +0x8 the left child, +0xC the right child, +0x10 the
// value (the head: +0x4 the root, +0x8 the leftmost, +0xC the rightmost).
namespace tree {
inline constexpr std::size_t kHead = 0x0, kCount = 0x4;
inline constexpr std::size_t kParent = 0x4, kLeft = 0x8, kRight = 0xC, kValue = 0x10;
} // namespace tree

// What other objects the first part calls. The occupant manager (+0x48):
// the first occupant of a cell that passes a filter (out, x, z, filter;
// 0x0049E690, ret 0x10; AL, the occupant referenced). A string (cIGZString,
// the game's implementation's vtable 0x00A80810): slot 3 (text, length;
// 0x0090F546, ret 8), slot 4 (a C string; 0x0090F50F, ret 4).
namespace occupants_slot {
inline constexpr std::size_t kGetFirstByCell = 0x80;
} // namespace occupants_slot
namespace string_slot {
inline constexpr std::size_t kFromCharLength = 0x0C, kFromChar = 0x10;
} // namespace string_slot

// A vector as the game keeps it.
struct Vector {
    char* begin;
    char* end;
    char* capacity;
};

// A string of the game (cRZBaseString, 0x14 bytes: the vtable 0x00A80810,
// the characters' begin, end and capacity, and a reference count).
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t references;
};

inline uint32_t AddRefOf(void* object) { return Virtual<uint32_t>(object, 0x4); }
inline uint32_t ReleaseOf(void* object) { return Virtual<uint32_t>(object, 0x8); }

// A game function at its address: thiscall with `self` in ECX, or cdecl.
template <typename R, typename... Args>
inline R CallThis(std::uintptr_t address, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(address)(self, args...);
}

template <typename R, typename... Args>
inline R CallCdecl(std::uintptr_t address, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(address)(args...);
}

// The number of elements of a vector of `size`-byte elements.
inline uint32_t Count(const char* object, std::size_t vector, std::size_t size)
{
    const Vector* const v = reinterpret_cast<const Vector*>(object + vector);
    return static_cast<uint32_t>(static_cast<std::size_t>(v->end - v->begin) / size);
}

// Logs "nettool: first <name>" once per flag (nettool_state.cpp).
void FirstCall(const char* name, bool& logged);

// A detour of a group: the game's address and our function. Skipped says
// whether OPENSC4_NETTOOL_SKIP (function addresses, hex, eight digits) lists
// the address; InstallDetours installs a group's list but for those and logs
// "nettool <what>: <n> detours".
struct Detour {
    std::uintptr_t address;
    void* ours;
};
#define OPENSC4_NETTOOL_DETOUR(address, fn) {address, reinterpret_cast<void*>(&fn)}
bool Skipped(std::uintptr_t address);
bool InstallDetourList(const Detour* detours, std::size_t count, const char* what);
template <std::size_t N>
inline bool InstallDetours(const Detour (&detours)[N], const char* what)
{
    return InstallDetourList(detours, N, what);
}

// A vtable slot of a group: the vtable, the slot's number, the address the
// game has there and our function. SwapSlots stores ours in every slot of
// the list (but for the skipped ones, by the original's address) after it
// has checked that each still holds the game's address, and logs "nettool
// <what>: <n> slots are ours". For code that is folded with other classes:
// only the tool family's vtables change.
struct Slot {
    std::uintptr_t vtable;
    int index;
    std::uintptr_t original;
    void* ours;
};
#define OPENSC4_NETTOOL_SLOT(vtable, index, original, fn) {vtable, index, original, reinterpret_cast<void*>(&fn)}
bool SwapSlotList(const Slot* slots, std::size_t count, const char* what);
template <std::size_t N>
inline bool SwapSlots(const Slot (&slots)[N], const char* what)
{
    return SwapSlotList(slots, N, what);
}

} // namespace opensc4::nettool_sim
