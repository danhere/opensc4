// Our pre-built-model network occupant (build 641): the class map, every
// vtable by the slots the class owns and the object's fields. The class has
// no name in the exe; cSC4PrebuiltNetworkOccupant is ours. It is a network
// occupant (src/core/netocc_sim.h) whose piece is drawn from a model made
// beforehand, found through the piece's exemplar, and not from the network's
// own textured quads: the pieces of the bridge approaches, of the bridges and
// of the tunnel entrances. On top of the network occupant's state it keeps
// where the model stands from the piece's position, an optional support
// pylon (a second model, its angle and where it stands), the two planes of
// its surface (two triangles of the four corners, as slopes from the piece's
// position) and what the view needs to draw it (two model instances, two
// effect IDs). The bridge occupant (class ID 0x49CC1BCD) and the tunnel
// occupant (0x8A4BD52B) derive from it.
//
// Class map
//   class ID 0x49C1A034 (GetGZCLSID 0x00604E50, a six-byte stub among the
//   shared stubs before the class's code: not hooked), which is also the
//   interface ID the class answers to; every other ID goes to the network
//   occupant's QueryInterface. The occupant type (what the occupant part's
//   GetType answers) is the network occupant's, 0xC772BF98: the objects are
//   inside the network occupant test's walk (src/core/netocc_autotest.cpp)
//   and are told apart by the class ID.
//   0x1D8 bytes, from the pool at 0x00B0B3A8 (pages of 32 KB at address &
//   0xFFFF8000, 69 items each, the free list at +8 and the used count at
//   +0xC; the pool's table at 0x00AA81EC: slot 1 the free 0x0061E7E0, slot 2
//   the page set-up 0x0061D090).
//   constructor 0x0061E1A0 (one argument: non-zero for the most derived
//   object; the bridge's and the tunnel's constructors call it with 0),
//   called by the factory 0x0061E820 (the class registration's pointer; it
//   returns the object + 4) and by a maker (the call at 0x00624306), after
//   the pool allocation 0x0061D200 (its argument 0x1D8 is not read).
//   destructor 0x00604CA0 (the vtables, the two model instances released,
//   then the network occupant's destructor 0x0061C170 by a tail jump; the
//   bridge's and the tunnel's deleting destructors call it too: nobody's
//   detour), deleting destructor 0x00605020 (flags; the holder part's slot
//   22 and the +0x38 part's slot 0 reach it through the thunks 0x00604EC0
//   and 0x00604EE0), which frees with 0x0061E300 (cdecl, the item).
//   The class's code is 0x0061D040 to 0x0061F4B8, with the stubs and thunks
//   0x00604E50 to 0x00605020 and the deleting destructor after them.
//
// Vtables, as the constructor stores them (the slots the class owns; the
// others are the network occupant's or shared stubs):
//   +0x00 primary, 0x00AA7038, 82 slots (the network occupant interface):
//       0 QueryInterface 0x0061D060 (ret 8)
//       4 Shutdown 0x0061D0D0
//      33 GetModelInstances 0x0061E8A0 (ret 0x14): drawing, the game's
//      49 GetPathInfo 0x0061D380
//      56 GetAltitude 0x0061D790 (ret 8)
//      57 GetNormal 0x0061D840 (ret 0xC)
//      58 SetTranslucency 0x0061D540 (ret 8): drawing, the game's
//      61 SetLit 0x0061F4A0 (ret 4): the base's and the effects, the game's
//      65 TransformNode 0x0061D660 (ret 4)
//      73 IsSupportPylonPresent 0x0061D040
//      79 0x0061F480 (ret 4): the base's lighting and the effects, the game's
//      80 the bounding box 0x0061DB50 (ret 8): asks for the model
//         instances, the game's
//      81 the stub 0x00648F10
//   +0x04 cISC4Occupant, 0x00AA6FB0, 34 slots: 3 OccupantInit 0x0061DB00;
//     18 SetVisibility 0x0061F430 and 30 SetAllFlags 0x0061F460 (the
//     base's, then the effects: the game's); 0 and 4 are the thunks
//     0x00604E80 and 0x00604E90 to QueryInterface and Shutdown.
//   +0x08 cIGZSerializable, 0x00AA6F94: 3 Write 0x0061D8E0 and 4 Read
//     0x0061D9B0 (both ret 4, the object + 8); 5 GetGZCLSID 0x00604E50.
//   +0x0C property holder, 0x00AA6F30: 22 the deleting destructor thunk
//     0x00604EC0, 23 and 24 the save thunks 0x00604E60 and 0x00604E70 (this
//     - 4 to Write and Read).
//   +0x20 exemplar holder part, 0x00AA6F18 (slot 3 sets the exemplar,
//     0x00458E60, slot 4 answers it, 0x0060CE80); +0x38 the model part's
//     thunk part, 0x00AA6F14 (slot 0 the thunk 0x00604EE0 to the deleting
//     destructor).
//   +0x3C the virtual base table 0x00AA81FC (-4, then the displacement
//     0x198; the pool's table precedes it), the virtual base's vtable
//     0x00AA6EC8 (19 slots, all adjustor thunks 0x00604EF0 to 0x00605010 to
//     the network occupant's and the model part's code, but slot 12 ->
//     0x00604FB0 -> 0x0061E860, the class's own draw: the game's). The
//     abstract vtable the most derived constructor stores at +0x1D4 first is
//     0x00AA1F38.
//
// Not virtual: 0x0061D120 (the set-up a maker calls: exemplar, model
// offset, rotation and flip; callers 0x00612C94, 0x006248A8, 0x006282E7),
// 0x0061D230 (the two planes from the corners; the occupant part's Init and
// 0x006243D3, 0x006277AB, 0x00627C24), 0x0061D340 (the support pylon;
// 0x00628252), 0x0061D200 (the pool allocation; 0x006242F6), 0x0061E340
// (the effects' refresh; the game's).
//
// Fields (object offsets). The network occupant's and the base's are in
// src/core/netocc_sim.h and src/core/occbase_sim.h.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::prebuiltocc_sim {

constexpr uint32_t kClassId = 0x49C1A034;
constexpr uint32_t kIidPrebuilt = 0x49C1A034;
constexpr uint32_t kIidNetworkOccupant = 0xA821EF94;
constexpr uint32_t kIidObject = 0xA9764C8C;
constexpr uint32_t kOccupantType = 0xC772BF98;
constexpr std::size_t kObjectSize = 0x1D8;

namespace field {
constexpr std::size_t kOccupant = 0x4;       // base: the cISC4Occupant part
constexpr std::size_t kSerializable = 0x8;   // the cIGZSerializable part
constexpr std::size_t kHolder = 0xC;         // base: the property holder
constexpr std::size_t kExemplarPart = 0x20;  // base: the exemplar holder part (the exemplar at +0x24)
constexpr std::size_t kModelPart = 0x38;     // the model part's thunk part
constexpr std::size_t kVbtable = 0x3C;       // the virtual base table pointer
constexpr std::size_t kModelWord = 0x4C;     // network occupant: 0x1A from its Init; this class's makes it 0x4A
constexpr std::size_t kCorners = 0x9C;       // network occupant: four 0x18-byte corners, x y z first
constexpr std::size_t kVertexHeights = 0xA0; // network occupant: the corners' y (0xA0, 0xB8, 0xD0, 0xE8)
constexpr std::size_t kVertexStride = 0x18;
constexpr std::size_t kBox = 0x124;          // network occupant: the box, low x y z, then high at +0xC
constexpr std::size_t kFlags = 0x13C;        // network occupant: the network flags
constexpr std::size_t kReference = 0x140;    // network occupant: the path information
constexpr std::size_t kPathTable = 0x144;    // network occupant: the path connection table, 8 words
constexpr std::size_t kPathIDOverride = 0x164; // network occupant: the path ID override
constexpr std::size_t kPathOriginX = 0x168;  // network occupant: a float here (added to a node's x; the constructor
constexpr std::size_t kPathOriginZ = 0x16C;  // zeroes both again)
constexpr std::size_t kModelOffset = 0x178;  // three floats: where the model stands from the position; saved; set-up
constexpr std::size_t kPlaneA = 0x184;       // three floats: x slope, z slope, height of the first triangle's plane
constexpr std::size_t kPlaneB = 0x190;       // three floats: the second triangle's
constexpr std::size_t kPylon = 0x19C;        // uint32: the support pylon's model, 0 for none; saved
constexpr std::size_t kPylonAngle = 0x1A0;   // float; saved with a pylon; the constructor leaves it
constexpr std::size_t kPylonOffset = 0x1A4;  // three floats; saved with a pylon
constexpr std::size_t kPieceOverride = 0x1B0;    // uint32: stands in for the piece ID in the effects' look-up; saved
constexpr std::size_t kRotationOverride = 0x1B4; // uint8: its rotation and flip byte; saved
constexpr std::size_t kEffects = 0x1B8;      // two effect IDs (-1 none, -2 asked for): the view's
constexpr std::size_t kSeenZoom = 0x1C0;     // int8: the zoom GetModelInstances last saw (-1): the view's
constexpr std::size_t kSeenDetail = 0x1C1;   // uint8: the detail preference the draw last saw: the view's
constexpr std::size_t kModel = 0x1C4;        // the model instance, released by Shutdown and the destructor
constexpr std::size_t kPylonModel = 0x1C8;   // the pylon's model instance, the same
constexpr std::size_t kVbaseShift = 0x1D0;   // the virtual base's displacement word: 0 (the displacement less 0x198)
constexpr std::size_t kVbase = 0x1D4;        // the virtual base (4 bytes: its vtable)
} // namespace field

// Network flags (the word at +0x13C) the class's functions look at or set.
namespace flag {
constexpr uint32_t kPathInfo = 0x00002000;       // network occupant: set when the path table is filled
constexpr uint32_t kNoPathRegister = 0x00100000; // GetPathInfo: the information's slot 0x68 is not called
constexpr uint32_t kImmovable = 0x00400000;      // the constructor sets it
constexpr uint32_t kOtherDiagonal = 0x01000000;  // the two triangles are split along the other diagonal
} // namespace flag

// Byte offsets of the primary vtable's slots used here, for Virtual<T>(object, slot::k...).
namespace slot {
constexpr std::size_t kShutdown = 0x10, kHasNetworkFlag = 0x20, kClearNetworkFlag = 0x2C, kGetRotation = 0x60;
constexpr std::size_t kGetFlip = 0x64, kGetPathInfo = 0xC4, kReleasePathInfo = 0xC8, kGetAltitude = 0xE0;
constexpr std::size_t kGetNormal = 0xE4, kGetOccupiedCell = 0xEC, kSetLit = 0xF4, kTransformNode = 0x104;
constexpr std::size_t kIsSupportPylonPresent = 0x124, kSetRotationAndFlip = 0x128, kGetPosition = 0x12C;
constexpr std::size_t kComputeBoundingBox = 0x140;
} // namespace slot

// The property the set-up adds to the object's holder (a true) when the
// exemplar has no property 0x6A845768.
constexpr uint32_t kPropertySetUp = 0x4835C6A6;
constexpr uint32_t kExemplarProperty = 0x6A845768;

// Logs a function's first call once ("prebuiltocc: first <name>"; defined in
// prebuiltocc_sim.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::prebuiltocc_sim
