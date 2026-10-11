// Our cSC4Occupant (build 641), the base class of every occupant: the class
// map, the slots and the object's fields. Buildings, props, flora, network
// pieces, base textures, automata and the rest (57 classes) derive from it
// and put its functions in their own vtables; what it holds is what they
// share: the reference count, the property holder with the exemplar, the
// occupant type, the occupant manager's cells, the removal counts, the
// state byte, the flags byte and the placeable object.
//
// Class map
//   class ID 0xA9BC9AB6 (GetGZCLSID 0x005E8970), cISC4Occupant IID
//   0x26BB767E, occupant type 0x74758925 until a derived constructor sets
//   its own
//   0x34 bytes; the factory 0x005E9330 takes them from 0x005E55E0, derived
//   classes embed the base at +4 (most), +8 or further, and one lives on
//   the stack (0x005B6CA1 constructs a 0x00AA0DA0 occupant in a local and
//   destroys it with 0x005E8C40)
//   constructor 0x005E9220 (the factory and 19 derived constructors),
//   destructor 0x005E8C40 (30 derived destructors), deleting destructor
//   0x005E9280 (the holder part's slot 22 through the thunk 0x005E8C80)
//   code 0x005E8970 to 0x005E95AD. Before it 0x005E8810 is another class's;
//   after it 0x005E95B0 is the GetGZCLSID of class 0xA9C74DC9. In the
//   range and the class's, though no methods: 0x005E8F70 and 0x005E9500,
//   two cdecl helpers about an exemplar's occupant groups that other
//   classes call. Eight more of its slots are elsewhere, folded with other
//   classes' identical code or laid out with another class: 0x00458A40,
//   0x00458A50, 0x006C2040, 0x006BDD80, 0x006AAC50, 0x0078DAF0, 0x005DEB40
//   and 0x00648F10.
//
// +0x00 cISC4Occupant, vtable 0x00AA41D8, 34 slots (byte offset, stack
// arguments from the callee's ret N, function; in how many of the 57
// occupant vtables the slot holds this function):
//    0 +00 2 QueryInterface 0x005E89C0            3 (the others end in a jump to it)
//    1 +04 0 AddRef 0x005E89B0                    3 (the others are thunks to it)
//    2 +08 0 Release 0x005E8DF0                   3 (same)
//    3 +0C 0 Init 0x005E8C90                      5 (16 direct calls and jumps from derived Inits)
//    4 +10 0 Shutdown 0x005E8980                  5 (17 direct calls and jumps from derived Shutdowns)
//    5 +14 0 IsInitialized 0x00458A40             57
//    6 +18 0 AsPropertyHolder 0x006C2040          57 (and 41 foreign vtables)
//    7 +1C 0 GetType 0x006BDD80                   57 (and 14 foreign)
//    8 +20 1 GetPosition 0x005E8A20               1 (zeros)
//    9 +24 1 SetPosition 0x00648F10               12 (nothing; 128 foreign)
//   10 +28 2 GetBoundingBox 0x005E8A40            1 (zeros)
//   11 +2C 1 GetBoundingCityCells 0x005E8E20      57
//   12 +30 1 SetRemovalFlags 0x005E8A60           57
//   13 +34 1 UnsetRemovalFlags 0x005E8AA0         57
//   14 +38 1 CanRemove 0x005E8AE0                 57
//   15 +3C 2 PostOccupantMessage 0x005E9010       50 (the other 7 call it first)
//   16 +40 0 GetHighlight 0x00458A50              57
//   17 +44 2 SetHighlight 0x005E90E0              46
//   18 +48 2 SetVisibility 0x005E8B10             53 (the other 4 call it first)
//   19 +4C 0 GetPlaceableObject 0x006AAC50        57 (and 16 foreign)
//   20 +50 0 GetOrCreatePlaceableObject 0x005E91C0   57
//   21 +54 1 SetPlaceableObject 0x0078DAF0        57
//   22 +58 1 IsOccupantGroup 0x005E92D0           56
//   23 +5C 1 AddOccupantGroup 0x005E9350          57
//   24 +60 1 GetOccupantGroups 0x005E9410         57
//   25 +64 1 GetOccupantManagerBBox 0x005E8BF0    57
//   26 +68 1 SetOccupantManagerBBox 0x005E8C20    57
//   27 +6C 1 GetLotTag 0x005DEB40                 49 (false; 74 foreign)
//   28 +70 1 SetLotTag 0x00648F10                 49 (nothing)
//   29 +74 1 SetFlag 0x005E8B70                   57
//   30 +78 1 SetAllFlags 0x005E8B90               48 (the other 9 call it)
//   31 +7C 1 ClearFlag 0x005E8BA0                 57
//   32 +80 1 IsFlagSet 0x005E8BC0                 57
//   33 +84 0 GetFlags 0x005E8BE0                  57
// +0x04 cIGZSerializable, vtable 0x00AA41BC, 6 slots: 0-2 thunks (this - 4)
//   to QueryInterface, AddRef, Release; 3 Write 0x005E8CD0 (1), 4 Read
//   0x005E8D60 (1), 5 GetGZCLSID 0x005E8970. Write and Read get `this` =
//   object + 4; the derived classes' Write and Read call them directly
//   first (16 callers each).
// +0x08 the property holder (cSCPropertyHolder, 0x1C bytes with its
//   exemplar part; constructor 0x005FF1C0, destructor 0x005FF220), vtable
//   0x00AA4158, 25 slots: 0-2 thunks (this - 8), 3-21 the holder's own code,
//   22 the deleting destructor thunk 0x005E8C80 (this - 8, to 0x005E9280),
//   23 and 24 thunks to Write and Read.
// +0x1C the exemplar holder part, vtable 0x00AA4140, 5 slots: 0-2 thunks
//   (this - 0x1C), 3 SetPropertyExemplar 0x00458E60 and 4 the getter
//   0x0060CE80 (the holder base's code, folded into 77 and 150 vtables).
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::occbase_sim {

constexpr uint32_t kClassId = 0xA9BC9AB6;
constexpr uint32_t kOccupantType = 0x74758925;    // the constructor's; every derived class sets its own
constexpr uint32_t kIidOccupant = 0x26BB767E;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr uint32_t kPropertyGroups = 0xAA1DD396;  // the occupant groups: uint32 list, the holder's own or its exemplar's
constexpr std::size_t kObjectSize = 0x34;

// The two occupant types of the automaton classes (registered together at
// 0x00514A00): they move by frames.
constexpr uint32_t kTypeAutomaton = 0xC7CC75F1;
constexpr uint32_t kTypeAutomaton2 = 0x496E5783;
constexpr uint32_t kTypeProp = 0xA823821E;

// Occupant messages the base posts (PostOccupantMessage's first argument).
constexpr uint32_t kMessageInit = 0x99EF1140;      // Init, unless the occupant was read from a save
constexpr uint32_t kMessageShutdown = 0x99EF1141;
constexpr uint32_t kMessageChanged = 0x99EF1145;   // SetVisibility, SetHighlight with its second argument
constexpr uint32_t kMessageSent = 0x99EF1146;      // sent at once (not queued) when there is a placeable object
constexpr uint32_t kMessageGroupAdded = 0xEBFF18A3;
constexpr uint32_t kMessageHighlight = 0xE6E85114; // not an occupant message: SetHighlight's own, data 2 old, data 3 new

// Fields (offsets from the base's own `this`, the cISC4Occupant part; a
// derived class with the base at +4 has them 4 higher).
namespace field {
constexpr std::size_t kSerializable = 0x04; // vtable of the cIGZSerializable part
constexpr std::size_t kHolder = 0x08;       // the property holder: vtable, +0x0C count, +0x10 properties
constexpr std::size_t kRefCount = 0x0C;     // int32, the holder's (AddRef 0x005E89B0, Release 0x005E8DF0)
constexpr std::size_t kProperties = 0x10;   // the holder's own properties: begin, end, capacity
constexpr std::size_t kExemplarPart = 0x1C; // vtable of the exemplar holder part
constexpr std::size_t kExemplar = 0x20;     // the exemplar, referenced (the holder's destructor releases it)
constexpr std::size_t kType = 0x24;         // uint32 occupant type (slot 7; constructor, Read, Write)
constexpr std::size_t kManagerBox = 0x28;   // uint8[4]: the occupant manager's cells; first byte 0xFF: none (slots 25, 26)
constexpr std::size_t kRemoval2 = 0x2C;     // uint8: count of removal flag 2 (slots 12-14)
constexpr std::size_t kRemoval1 = 0x2D;     // uint8: count of removal flag 1
constexpr std::size_t kState = 0x2E;        // uint8: bit 0 initialized (Init, Shutdown), bit 1 read from a save (Read), bits 4-7 the highlight (slots 16, 17)
constexpr std::size_t kFlags = 0x2F;        // uint8 flags (slots 29-33, 18); 4 from the constructor; saved without 0x02 and 0x20
constexpr std::size_t kPlaceable = 0x30;    // the placeable object, referenced (slots 19-21; pool 0x00B0A8C8)
} // namespace field

// State bits (+0x2E) and the flags (+0x2F) this class itself touches.
constexpr uint8_t kStateInitialized = 0x01;
constexpr uint8_t kStateRead = 0x02;
constexpr uint8_t kFlagInManager = 0x01; // the occupant manager's (its insert and remove)
constexpr uint8_t kFlagVisible = 0x04;   // SetVisibility
constexpr uint8_t kFlagsSaved = 0xDD;    // Write's mask: 0x02 and 0x20 are never saved

// Byte offsets of the primary vtable's slots, for Virtual<T>(occupant, slot::k...).
namespace slot {
constexpr std::size_t kQueryInterface = 0x00, kAddRef = 0x04, kRelease = 0x08, kInit = 0x0C, kShutdown = 0x10;
constexpr std::size_t kIsInitialized = 0x14, kAsPropertyHolder = 0x18, kGetType = 0x1C, kGetPosition = 0x20;
constexpr std::size_t kSetPosition = 0x24, kGetBoundingBox = 0x28, kGetBoundingCityCells = 0x2C;
constexpr std::size_t kSetRemovalFlags = 0x30, kUnsetRemovalFlags = 0x34, kCanRemove = 0x38;
constexpr std::size_t kPostOccupantMessage = 0x3C, kGetHighlight = 0x40, kSetHighlight = 0x44, kSetVisibility = 0x48;
constexpr std::size_t kGetPlaceableObject = 0x4C, kGetOrCreatePlaceableObject = 0x50, kSetPlaceableObject = 0x54;
constexpr std::size_t kIsOccupantGroup = 0x58, kAddOccupantGroup = 0x5C, kGetOccupantGroups = 0x60;
constexpr std::size_t kGetOccupantManagerBBox = 0x64, kSetOccupantManagerBBox = 0x68, kGetLotTag = 0x6C;
constexpr std::size_t kSetLotTag = 0x70, kSetFlag = 0x74, kSetAllFlags = 0x78, kClearFlag = 0x7C, kIsFlagSet = 0x80;
constexpr std::size_t kGetFlags = 0x84;
} // namespace slot

// The game's set of uint32 (GetOccupantGroups' argument): a red-black tree
// behind a header node whose parent is the root.
struct TreeNode {
    uint32_t color;
    TreeNode* parent;
    TreeNode* left;
    TreeNode* right;
    uint32_t value;
};
struct Set {
    TreeNode* header;
    uint32_t size;
};

// Logs a function's first call once ("occbase: first <name>"; defined in
// occbase_sim.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::occbase_sim
