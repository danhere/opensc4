// Our cSC4NetworkOccupant (build 641): the class map as far as the first
// piece shows it, the object's fields and the interface's slots. A network
// occupant is one piece of a network in the city (a road, rail, highway,
// pipe, power pole, subway ... tile): its flags, its rotation, the heights
// of its four corners, its edges (which of its four sides connect and how),
// its demolition cost and a few properties it keeps in its property holder
// (retrofit, the connection node's edge types, the path ID, the passage
// ceiling, the one-way directions, the origin of a complex piece). The
// network simulator makes and changes them; the traffic simulator, the
// renderer and the save code read them. The rest (position in the occupant
// manager, removal counts, the property holder with the exemplar) is the
// occupant base's, cSC4Occupant (src/core/occbase_sim.h), at the object + 4.
//
// This is the first piece (src/core/netocc_state.cpp): 43 small functions of
// the interface, state and queries. Not in it, left for later pieces: the
// constructor 0x0061BDD0 and factory 0x0061C480 and the save (life and
// save), geometry and paths (GetPathInfo, AddPathConnection, GetAltitude,
// GetNormal, ConnectsToNeighbor, TransformNode ...), and the drawing code,
// which stays out for the Renderer thread: SetQuadDraw, AddPart,
// GetModelInstances, MarkLightingUpdateNeeded, SetTranslucency, SetLit.
//
// Class map
//   class ID 0xC9C05C6E, occupant type 0xC772BF98 (what the occupant part's
//   GetType answers; the bridge, tunnel and pipe classes derive from this
//   class and share most of its vtable), interface IID 0xA821EF94.
//   Virtual inheritance: besides the primary cISC4NetworkOccupant at +0x0
//   there are vtable pointers at +0x4 (cISC4Occupant, 0x00AA7FF8, the base
//   cSC4Occupant starts here), +0x8, +0xC (the property holder), +0x20,
//   +0x38 and a virtual base found through the table at +0x3C.
//
// +0x00 primary cISC4NetworkOccupant, vtable 0x00AA8080, 74 slots: three
//   IUnknown slots then the interface in gzcom-dll's order (its header
//   cISC4NetworkOccupant.h is right for the order; the byte offset of a
//   slot is its number x 4). The 43 functions of this piece, with the
//   argument counts from their own `ret N`:
//     6 CheckForRetrofit 0x00619180 (0)        7 SetRetrofitFlag 0x0061A120 (1)
//     8 HasNetworkFlag 0x00604D10 (1)          9 HasAnyNetworkFlag 0x00604D30 (1)
//    10 SetNetworkFlag 0x00619EC0 (1)         11 ClearNetworkFlag 0x00619F70 (1)
//    13 GetConnectionNodeEdgeTypes 0x00619110 (2)
//    14 SetConnectionNodeEdgeTypes 0x0061A020 (2)
//    15 GetConnectionPathIDOverride 0x00619150 (0)
//    16 SetConnectionPathIDOverride 0x0061A090 (1)
//    17 IsImmovable 0x00604D50 (0)            18 SetImmovable 0x00604D60 (1)
//    19 IsUsable 0x00604D80 (0)               20 SetUsable 0x00604D90 (1)
//    21 IsPlaced 0x00604DB0 (0)               22 IsOfType 0x00619520 (1)
//    24 GetRotation 0x00604DC0 (0)            25 GetFlip 0x00604DD0 (0)
//    26 GetRotationAndFlip 0x00604DE0 (0)     27 SetVariation 0x0061B7F0 (1)
//    28 GetVariation 0x00619880 (0)           29 SetUnderTexture 0x0061B880 (1)
//    34 GetVertexHeights 0x0065A6C0 (1)       35 GetVertexHeight 0x006194C0 (1)
//    36 SetVertexHeights 0x00619650 (1)       39 SetEdgeType 0x0061B430 (3)
//    40 GetActiveEdges 0x0061AE90 (0)         41 GetActiveEdgesForType 0x0061AEE0 (1)
//    42 GetActiveEdgesForTypeNoGroup 0x0061AF10 (1)
//    43 GetGroupedEdges 0x0061AF40 (0)        51 SetPathIDOverride 0x006197D0 (1)
//    52 GetPathIDOverride 0x006197E0 (0)      55 IsIntersection 0x0061BD80 (0)
//    60 IsLit 0x00619890 (0)                  63 GetCostOfDemolition 0x00604E10 (0)
//    64 SetCostOfDemolition 0x00604E20 (2)    67 GetPassageCeiling 0x00619900 (1)
//    68 SetPassageCeiling 0x0061B9A0 (1)      69 GetOneWayDirections 0x00619940 (0)
//    70 SetOneWayDirections 0x0061B9F0 (1)    71 GetComplexPieceOrigin 0x00619970 (2)
//    72 SetComplexPieceOrigin 0x0061BA90 (2)  73 IsSupportPylonPresent 0x006199C0 (0)
//   Slots 37 and 38 (SetEdgeTypesFlag, GetEdgeTypesFlag) are the adjustor
//   thunks 0x0061B480 and 0x0061AF80 (this + 0x108, then a jump to the
//   edge store's Set 0x00619A40 and Get 0x00619B70) and slot 44 (SetEdgeStore)
//   0x00604E00 (to the store's copy 0x006199D0): not hooked, shared with
//   other classes' vtables.
//   Slot 23 (PieceId in the header) is what IsIntersection looks up in the
//   map at 0x00B4528C: a piece ID.
//
// The edge store (cSC4EdgeConnectionStore in the header) is the sub-object
// at +0x108: a small map from a byte key (the network type, 0 to 12) to a
// 32-bit value of four bytes, one per edge (shift 8 x edge, edges 0 to 3).
// +0x108 a count of further entries (byte), +0x10C the key of the first
// entry (byte) and +0x110 its value, +0x114 a pointer to the further
// entries, 8 bytes each (key byte, pad, value at +4). In an edge value's
// byte the low three bits are the edge's connection (non-zero: active) and
// bit 2 (0x04) the grouped bit (GetActiveEdgesForTypeNoGroup counts the low
// two bits only). Lookup 0x00619B70 (0 when absent) and store 0x00619A40
// (0 removes the entry) are the game's and called at their addresses.
//
// Property holder properties (the holder is at +0xC; slot 0x24 asks for a
// property by ID, 0x38 sets one (ID, property, 0), 0x44 removes (ID, 0);
// a property's slot 0x14 gives its variant, whose slots 0x19C, 0x124,
// 0x144 and 0x104 give a pointer to its uint32 array, uint32, float and
// byte; the setters make a 0x14-byte script number (vtable 0x00ACECD0) of
// the matching type):
//   0x4835C6A7 connection node edge types (two uint32)
//   0x4835C6A8 passage ceiling (float)
//   0x4835C6A9 connection path ID override (uint32)
//   0x4835C6AA one-way directions (byte)
//   0x4835C6AB complex piece origin (x in the low 16 bits, y in the high)
//   0x4835C6AC retrofit (present or not; the setter stores an empty number)
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::netocc_sim {

constexpr uint32_t kClassId = 0xC9C05C6E;
constexpr uint32_t kOccupantType = 0xC772BF98;
constexpr uint32_t kIidNetworkOccupant = 0xA821EF94;

// Fields (offsets from the object's start; the base's are in
// src/core/occbase_sim.h at +0x4 to +0x37). Evidence: the 43 functions.
namespace field {
constexpr std::size_t kOccupant = 0x4;        // base: the cISC4Occupant part (slot 0x84 GetFlags: bit 0 = placed)
constexpr std::size_t kHolder = 0xC;          // base: the property holder
constexpr std::size_t kVertexHeights = 0xA0;  // four floats, one every 0x18 bytes (0xA0, 0xB8, 0xD0, 0xE8)
constexpr std::size_t kVertexStride = 0x18;
constexpr std::size_t kUnderTexture = 0x100;  // uint32 (SetUnderTexture)
constexpr std::size_t kRotationFlip = 0x104;  // uint8: bits 0-6 rotation, bit 7 flip
constexpr std::size_t kVariation = 0x105;     // uint8 (SetVariation)
constexpr std::size_t kEdgeStore = 0x108;     // the edge store, see the class map
constexpr std::size_t kEdgeCount = 0x108;     // store: uint8 number of further entries
constexpr std::size_t kEdgeFirstValue = 0x110; // store: the first entry's value (its key at +0x10C)
constexpr std::size_t kEdgeEntries = 0x114;   // store: pointer to the further entries (8 bytes each, value at +4)
constexpr std::size_t kFlags = 0x13C;         // uint32 network flags, see flag::
constexpr std::size_t kPathIDOverride = 0x164; // uint32 (SetPathIDOverride, SetConnectionPathIDOverride)
constexpr std::size_t kDemolitionCost = 0x170; // int64 (low word at +0x170, high at +0x174)
} // namespace field

// Network flags (the word at +0x13C), the ones these functions look at.
namespace flag {
constexpr uint32_t kIntersectionCandidate = 0x00000040; // IsIntersection asks for it first
constexpr uint32_t kPathInfo = 0x00002000;              // SetConnectionPathIDOverride clears it
constexpr uint32_t kTranslucent = 0x00004000;           // SetNetworkFlag/ClearNetworkFlag call SetTranslucency
constexpr uint32_t kImmovable = 0x00400000;             // IsImmovable, SetImmovable
constexpr uint32_t kUsable = 0x10000000;                // IsUsable, SetUsable; a change posts a message
constexpr uint32_t kLit = 0x20000000;                   // IsLit
constexpr uint32_t kSupportPylon = 0x40000000;          // IsSupportPylonPresent
} // namespace flag

// Property IDs in the property holder.
namespace property {
constexpr uint32_t kNodeEdgeTypes = 0x4835C6A7;
constexpr uint32_t kPassageCeiling = 0x4835C6A8;
constexpr uint32_t kPathIDOverride = 0x4835C6A9;
constexpr uint32_t kOneWayDirections = 0x4835C6AA;
constexpr uint32_t kComplexPieceOrigin = 0x4835C6AB;
constexpr uint32_t kRetrofit = 0x4835C6AC;
} // namespace property

// Messages the functions post (the framework's standard message: type,
// the occupant as object, the data words).
constexpr uint32_t kMessageUsableChanged = 0x89D3E9F3; // SetNetworkFlag/ClearNetworkFlag of the usable flag, placed
constexpr uint32_t kMessageChanged = 0x99EF1145;       // SetVariation, SetUnderTexture (the occupant's "changed")

// Byte offsets of the primary interface's slots, for Virtual<T>(object, slot::k...).
namespace slot {
constexpr std::size_t kIsManagedGlobally = 0x14, kCheckForRetrofit = 0x18, kSetRetrofitFlag = 0x1C;
constexpr std::size_t kHasNetworkFlag = 0x20, kHasAnyNetworkFlag = 0x24, kSetNetworkFlag = 0x28;
constexpr std::size_t kClearNetworkFlag = 0x2C, kGetNetworkFlag = 0x30;
constexpr std::size_t kGetConnectionNodeEdgeTypes = 0x34, kSetConnectionNodeEdgeTypes = 0x38;
constexpr std::size_t kGetConnectionPathIDOverride = 0x3C, kSetConnectionPathIDOverride = 0x40;
constexpr std::size_t kIsImmovable = 0x44, kSetImmovable = 0x48, kIsUsable = 0x4C, kSetUsable = 0x50;
constexpr std::size_t kIsPlaced = 0x54, kIsOfType = 0x58, kPieceId = 0x5C, kGetRotation = 0x60, kGetFlip = 0x64;
constexpr std::size_t kGetRotationAndFlip = 0x68, kSetVariation = 0x6C, kGetVariation = 0x70;
constexpr std::size_t kSetUnderTexture = 0x74, kGetVertexHeights = 0x88, kGetVertexHeight = 0x8C;
constexpr std::size_t kSetVertexHeights = 0x90, kSetEdgeTypesFlag = 0x94, kGetEdgeTypesFlag = 0x98;
constexpr std::size_t kSetEdgeType = 0x9C, kGetActiveEdges = 0xA0, kGetActiveEdgesForType = 0xA4;
constexpr std::size_t kGetActiveEdgesForTypeNoGroup = 0xA8, kGetGroupedEdges = 0xAC;
constexpr std::size_t kReleasePathInfo = 0xC8, kSetPathIDOverride = 0xCC, kGetPathIDOverride = 0xD0;
constexpr std::size_t kIsIntersection = 0xDC, kSetTranslucency = 0xE8, kIsLit = 0xF0;
constexpr std::size_t kGetCostOfDemolition = 0xFC, kSetCostOfDemolition = 0x100;
constexpr std::size_t kGetPassageCeiling = 0x10C, kSetPassageCeiling = 0x110, kGetOneWayDirections = 0x114;
constexpr std::size_t kSetOneWayDirections = 0x118, kGetComplexPieceOrigin = 0x11C;
constexpr std::size_t kSetComplexPieceOrigin = 0x120, kIsSupportPylonPresent = 0x124;
} // namespace slot

// Property holder slots (the holder is at +0xC).
namespace holder {
constexpr std::size_t kGetProperty = 0x24, kSetProperty = 0x38, kRemoveProperty = 0x44;
} // namespace holder

// Logs a function's first call once ("netocc: first <name>"; defined in
// netocc_state.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::netocc_sim
