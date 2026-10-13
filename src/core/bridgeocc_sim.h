// Our cSC4NetworkBridgeOccupant (build 641): the class map, every vtable by
// the slots the class owns and the object's fields. A bridge occupant is one
// piece of a bridge (one tile of its deck, with or without a pier): the
// pre-built model network occupant's state (the network occupant's corners,
// edges, flags and transform, src/core/netocc_sim.h, plus the model that
// class keeps) and one member of its own, a container of things attached to
// the piece's model instance, which the bridge starts for a piece that
// stands in deep water. Nothing of the bridge's own is saved.
//
// Class map
//   class ID 0x49CC1BCD (GetGZCLSID 0x00604BB0, the serializable part's slot
//   5), which is also the only interface ID the class answers itself. The
//   occupant type (what the occupant part's GetType answers) is the network
//   occupant's, 0xC772BF98: the bridge pieces are inside the network
//   occupant test's walk (src/core/netocc_autotest.cpp) and are told apart
//   by the class ID.
//   0x1F0 bytes, from the pool at 0x00B0B2D0 (pages of 32 KB at address &
//   0xFFFF8000, 66 items each, the free list at +8 and the used count at
//   +0xC; the pool's table is 0x00AA7180 while it lives and 0x00AA6EB8, with
//   the same functions, from its exit handler 0x00A7C020 on: slot 1 the free
//   0x00605120, slot 2 the page set-up 0x00604C60. Both tables are the
//   bridge pool's: the pre-built class's pool 0x00B0B3A8 has the tables
//   0x00AA8224 and 0x00AA81EC, the page set-up 0x0061D090 with 0x1D8-byte
//   items and the allocation 0x0061D200).
//   constructor 0x00605160 (one argument: non-zero for the most derived
//   object), called by the factory 0x00605450 (the class registration's
//   pointer, 0x00460743; it returns the object + 4) and by 0x006242E8 (a
//   maker function that picks one of the three classes by class ID and calls
//   the pool allocation 0x00605040 before it: its argument 0x1F0 is not
//   read).
//   destructor 0x006053F0 (the vtables, the member's destructor, then the
//   pre-built class's destructor 0x00604CA0 by a tail jump), called only by
//   the deleting destructor 0x00605390 (flags; the holder part's slot 22 and
//   the +0x38 part's slot 0 reach it through the thunks 0x00605230 and
//   0x00605250).
//   The class's code is 0x00604BB0 to 0x00604C57, 0x00604C60, and 0x00605040
//   to 0x0060548D. Between them lie the pre-built class's destructor
//   0x00604CA0, the network occupant's stubs 0x00604D10 to 0x00604E40, the
//   pre-built class's GetGZCLSID 0x00604E50, its adjustor thunks 0x00604E80
//   to 0x00605010 and its deleting destructor 0x00605020. NOT the bridge's:
//   0x00605490 (ret 8: two network occupants; the direction, 0 to 3, from
//   the first one's cell to the second's when they are neighbours, else -1).
//   Its only callers, 0x00605D91 and 0x00605DA6, are in a function of the
//   class that follows (0x00605D30) and load that class's `this` into ECX
//   before the call, which the function does not read: a member of that
//   class.
//
// The parent class. The bridge derives from the pre-built model network
// occupant (class ID 0x49C1A034, vtable 0x00AA7038, 0x1D8 bytes, another
// file's), which derives from the network occupant. Nothing of it is
// reimplemented here; its functions are called at their addresses:
//   - the constructor 0x0061E1A0 (ret 4: the most derived flag, 0 from the
//     bridge), which calls the network occupant's constructor with 0, zeroes
//     its fields and sets the flag 0x00400000;
//   - the destructor 0x00604CA0 (its vtables, the two references at +0x1C8
//     and +0x1C4 released, then the network occupant's destructor);
//   - QueryInterface 0x0061D060 (ret 8), the interface's slot 4 0x0061D0D0
//     (no argument) and the occupant part's Init 0x0061DB00 (no argument);
//   - its Write 0x0061D8E0 and Read 0x0061D9B0 are the bridge's too (the
//     serializable part's slots 3 and 4 are not overridden): version 4, the
//     network occupant's record, then the fields at +0x178 to +0x1B4 below
//     (tools/re/save_schemas/bridgeocc.py).
//
// Vtables, as the constructor stores them (the slots the bridge owns; the
// others are the network occupant's, the pre-built class's or shared stubs):
//   +0x00 primary, 0x00AA7300, 82 slots (the network occupant interface's 81
//     and the pre-built class's slot 81):
//       0 QueryInterface 0x00604BC0 (ret 8)
//       4 Shutdown 0x00604C10 (no argument)
//      62 CanBeLit: the shared stub 0x00648F00 (always 1; the parent has
//         the stub 0x00648EF0, always 0)
//      81 the model instance hook 0x00605070 (ret 4; the parent has the
//         empty stub 0x00648F10)
//   +0x04 cISC4Occupant, 0x00AA7278: 3 OccupantInit 0x00604BF0; 0 and 4 are
//     the thunks 0x006051F0 and 0x00605200 to QueryInterface and Shutdown.
//   +0x08 cIGZSerializable, 0x00AA725C: 5 GetGZCLSID 0x00604BB0; 3 Write and
//     4 Read are the parent's.
//   +0x0C property holder, 0x00AA71F8: 22 the deleting destructor thunk
//     0x00605230.
//   +0x20 exemplar holder part, 0x00AA71E0; +0x38 the model part's thunk
//     part, 0x00AA71DC (slot 0 a thunk to the deleting destructor).
//   +0x3C the virtual base table 0x00AA7448 (-4, then the displacement
//     0x1B0), the virtual base's vtable 0x00AA7190 (19 slots, all adjustor
//     thunks, 0x00605260 to 0x00605380; none is the bridge's own). The
//     abstract vtable the most derived constructor stores at +0x1EC first is
//     0x00AA1F38.
//
// The member at +0x1D0 (0x18 bytes; constructor 0x005E8670, destructor
// 0x00513E00, vtable 0x00AA4060; no other code between 0x00600000 and
// 0x00625000 touches +0x1D0): a container whose contents were not analysed
// (it is also built at 0x0051429E, 0x0053C7A4, 0x0053C7AF and 0x0054CE1A, by
// classes that were not looked at). Its constructor
// sets the vtable, an owner pointer (+4, zero), a tree's head node (+8, 0x40
// bytes from the game's STL allocator) and a count (+0xC, zero). What the
// bridge calls of it, with the argument counts of the functions' own
// `ret N`: slot 0 (0x005E5E50, none) the start, slot 1 (0x005E86B0, none)
// the stop (it empties the container and releases the owner), slot 2
// (0x005E5E60, one) sets the owner (a reference to it is kept), slot 3
// (0x0060CE80, none) answers the owner, slot 5 (0x005E6160, none) places the
// entries by the owner's transform and slot 6 (0x005E7580, one) hands its
// argument to every entry that is not yet active.
//
// Fields (object offsets). The network occupant's and the base's are in
// src/core/netocc_sim.h and src/core/occbase_sim.h.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::bridgeocc_sim {

constexpr uint32_t kClassId = 0x49CC1BCD;
constexpr uint32_t kIidBridge = 0x49CC1BCD;
constexpr uint32_t kIidPrebuilt = 0x49C1A034;
constexpr uint32_t kIidNetworkOccupant = 0xA821EF94;
constexpr uint32_t kOccupantType = 0xC772BF98;
constexpr std::size_t kObjectSize = 0x1F0;

// The flags the constructor sets in the network flags (the parent's
// constructor sets 0x00400000 before; the bridge's `or` has both).
constexpr uint32_t kBridgeFlags = 0x00C00000;

namespace field {
constexpr std::size_t kOccupant = 0x4;       // base: the cISC4Occupant part
constexpr std::size_t kSerializable = 0x8;   // the cIGZSerializable part
constexpr std::size_t kHolder = 0xC;         // base: the property holder
constexpr std::size_t kExemplarPart = 0x20;  // base: the exemplar holder part
constexpr std::size_t kModelPart = 0x38;     // the model part's thunk part
constexpr std::size_t kVbtable = 0x3C;       // the virtual base table pointer
constexpr std::size_t kFlags = 0x13C;        // network occupant: the network flags
constexpr std::size_t kSavedFloats = 0x178;  // parent: three floats, saved
constexpr std::size_t kSavedCount = 0x19C;   // parent: a word, saved; the four floats after it only when not zero
constexpr std::size_t kSavedFour = 0x1A0;    // parent: four floats (+0x1A0 to +0x1AC), saved with a count
constexpr std::size_t kSavedWord = 0x1B0;    // parent: a word, saved; zero from its constructor
constexpr std::size_t kSavedByte = 0x1B4;    // parent: a byte, saved; zero from its constructor
constexpr std::size_t kModelInstance = 0x1C4; // parent: the reference it is given with slot 81's argument (not saved)
constexpr std::size_t kAttachments = 0x1D0;  // the bridge's own member (0x18 bytes), see the class map
constexpr std::size_t kVbaseShift = 0x1E8;   // the virtual base's displacement word: 0 (the displacement less 0x1B0)
constexpr std::size_t kVbase = 0x1EC;        // the virtual base (4 bytes: its vtable)
} // namespace field

// The member at +0x1D0: its fields and the byte offsets of its slots.
namespace attachments {
constexpr std::size_t kOwner = 0x4, kCount = 0xC;
constexpr std::size_t kStart = 0x0, kStop = 0x4, kSetOwner = 0x8, kGetOwner = 0xC, kPlace = 0x14, kActivate = 0x18;
} // namespace attachments

// Byte offsets of the primary vtable's slots the tests and the functions use,
// for Virtual<T>(object, slot::k...).
namespace slot {
constexpr std::size_t kShutdown = 0x10, kGetOccupiedCell = 0xEC, kCanBeLit = 0xF8, kModelInstanceHook = 0x144;
} // namespace slot

// The occupant part's slots (the part is at +0x4).
namespace occupant {
constexpr std::size_t kInit = 0xC, kIsInitialized = 0x14, kGetType = 0x1C, kGetPosition = 0x20;
constexpr std::size_t kState = 0x2E; // the part's byte whose bit 0 says initialised (src/core/occbase_sim.h)
} // namespace occupant

// The terrain's slots (the object at 0x00B43CF4): the altitude and the water
// level at a point (x, z), both the unrounded value in ST0 (docs/terrain.md).
namespace terrain {
constexpr std::size_t kGetAltitude = 0x34, kGetWaterLevel = 0x78;
} // namespace terrain

// How far under water the piece's place must be for slot 81 to start the
// member's entries (the float at 0x00AA209C, a constant of the exe that other
// code shares).
constexpr float kDeepWater = 12.0f;

// Logs a function's first call once ("bridgeocc: first <name>"; defined in
// bridgeocc_sim.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::bridgeocc_sim
