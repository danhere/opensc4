// Our cSC4FloraOccupant (build 641): the class map, every vtable slot by
// slot and the object's fields. A flora occupant is one tree or plant of
// the city: its flora type, its position, the dates it was born and last
// seeded, its state, its orientation, the type's record of the flora
// simulator (src/core/flora_sim.cpp, docs/flora.md) and, for a lot's
// flora, the lot object it stands for. The rest (position in the occupant
// manager, removal counts, flags, the property holder with the exemplar)
// is the occupant base's, cSC4Occupant (src/core/occbase_sim.h), at the
// object + 4.
//
// Class map
//   class ID 0xA9C05C85 (GetGZCLSID 0x00472900), occupant type 0x74758926,
//   cISC4FloraOccupant IID 0xC83DD057
//   0x5C bytes, from the pool at 0x00B07FC8 (8 KB pages at address &
//   0xFFFFE000: free list +0x8, used count +0xC; cSC4FloraOccupant::
//   Allocate 0x00472960 and, inline, the factory 0x00472C30)
//   constructor 0x00472B30 (called by the factory, which a save's reader
//   uses for every tree, and by the flora simulator at 0x006B06FC, flora
//   it kept apart in a save, and 0x006B223C, a new tree)
//   deleting destructor 0x00472D30, reached only through the holder part's
//   slot 22 (thunk 0x00472BF0), which the base's Release 0x005E8DF0 calls
//   at the last reference
//   code 0x00472900 to 0x00472DBD. In that range but another class's:
//   0x00472C70, the release of a type record (cSC4StaticFloraData, 0x178
//   bytes, of the flora simulator; called by SetStaticFloraData, the
//   destructor and the simulator's 0x006B15B4). 0x00472DC0 on is other
//   classes'.
//   No derived class: the five vtables are stored by the constructor and
//   the destructor only, and no other vtable holds one of the class's
//   functions.
//
// +0x00 primary cISC4FloraOccupant, vtable 0x00A88608, 15 slots (byte
// offset, stack arguments from the callee's ret N, function, owner; a
// pool's vtable follows at 0x00A88644):
//    0 +00 2 QueryInterface 0x00472910              class
//    1 +04 0 AddRef 0x005E28F0 (to the base's 0x005E89B0 with this + 4)   base
//    2 +08 0 Release 0x005E2900 (to the base's 0x005E8DF0)                base
//    3 +0C 0 AsOccupant 0x005FD7E0 (this + 4)       shared stub
//    4 +10 0 GetFloraType 0x0080BC00 (+0x38)        shared stub (31 vtables)
//    5 +14 1 SetFloraType 0x006CE240                shared stub (6 vtables)
//    6 +18 0 GetBirthDate 0x0068D1C0 (+0x4C)        shared stub (6)
//    7 +1C 1 SetBirthDate 0x004302D0                shared stub (2)
//    8 +20 0 GetLastSeedingDate 0x0081F910 (+0x50)  shared stub (7, and called directly by other classes)
//    9 +24 1 SetLastSeedingDate 0x004302C0          shared stub (9)
//   10 +28 0 GetState 0x005D6140 (+0x58, signed)    shared stub (folded with 0x00AA29C4)
//   11 +2C 1 SetState 0x005D6150                    shared stub (folded with 0x00AA29C8)
//   12 +30 0 GetOrientation 0x00472BA0              class
//   13 +34 1 SetOrientation 0x00472BB0              class
//   14 +38 1 SetStaticFloraData 0x00472CD0          class
//   (gzcom-dll's cISC4FloraOccupant.h has the order right.)
// +0x04 cISC4Occupant (the base cSC4Occupant starts here; its constructor
//   0x005E9220, destructor 0x005E8C40), vtable 0x00A88580, 34 slots:
//    0 +00 2 QueryInterface thunk 0x00472BC0 (this - 4)   class
//    1, 2    AddRef 0x005E29B0, Release 0x005E29C0        base
//    3 +0C 0 Init 0x005E8C90                              base
//    4 +10 0 Shutdown 0x005E8980                          base
//    5 +14 0 IsInitialized 0x00458A40                     base
//    6 +18 0 AsPropertyHolder 0x006C2040 (this + 8)       shared stub
//    7 +1C 0 GetType 0x006BDD80 (+0x24 of the part)       shared stub
//    8 +20 1 GetPosition 0x005D6360 (+0x3C of the part)   shared stub (folded with 0x00AA2938)
//    9 +24 1 SetPosition 0x005D6380                       shared stub (folded with 0x00AA293C)
//   10 +28 2 GetBoundingBox 0x00472AB0                    class
//   11-26    the base's (0x005E8E20 ... 0x005E8C20; 16 GetHighlight
//            0x00458A50, 19 GetPlaceableObject 0x006AAC50)
//   27 +6C 1 GetLotTag 0x00472940                         class
//   28 +70 1 SetLotTag 0x006CE240 (+0x38 of the part)     shared stub (the same code as SetFloraType)
//   29-33    SetFlag 0x005E8B70 ... GetFlags 0x005E8BE0   base
// +0x08 cIGZSerializable, vtable 0x00A88564, 6 slots: 0 QueryInterface
//   thunk 0x00472BD0 (class), 1 and 2 AddRef 0x005E29E0 and Release
//   0x005E29F0 (base), 3 Write 0x00472990 (1; class), 4 Read 0x00472A20
//   (1; class), 5 GetGZCLSID 0x00472900 (class). Write and Read get `this`
//   = object + 8.
// +0x0C the property holder (cISCPropertyHolder; 0x1C bytes with its
//   exemplar holder part), vtable 0x00A88500, 25 slots (0x00A88564 follows
//   it): 0 QueryInterface thunk 0x00472BE0 (class), 1 and 2 0x005E2A10 and
//   0x005E2A20 (base), 3-21 the shared holder code (0x005FEC20 ...), 22
//   the deleting destructor thunk 0x00472BF0 (1 argument; class), 23 the
//   Write thunk 0x00472C10 and 24 the Read thunk 0x00472C20 (both this -
//   4; class).
// +0x20 cISCExemplarPropertyHolder, vtable 0x00A884E8, 5 slots: 0
//   QueryInterface thunk 0x00472C00 (class), 1 and 2 0x005E06A0 and
//   0x005E06B0 (base), 3 SetPropertyExemplar 0x00458E60 (1; shared stub,
//   in every occupant class's vtable), 4 the getter 0x0060CE80 (shared
//   stub).
// The constructor first stores 0x00A884AC, the abstract interface's vtable
// (15 pure-call entries), which no call can meet.
//
// Nothing of the class follows frames or the view: every field changes
// only by the flora simulator's day and month work, by the lot developer
// or by a save read. One field holds memory the class never wrote, the
// lot tag (below).
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::floraocc_sim {

constexpr uint32_t kClassId = 0xA9C05C85;
constexpr uint32_t kOccupantType = 0x74758926;
constexpr uint32_t kIidFlora = 0xC83DD057;
constexpr std::size_t kObjectSize = 0x5C;

// Fields (offsets from the object's start). +0x04 to +0x37 are the occupant
// base's (evidence: its constructor 0x005E9220, Write 0x005E8CD0 and the
// slots named, which get the object + 4), +0x38 on this class's (evidence:
// the constructor 0x00472B30, Write 0x00472990 and the slots named).
namespace field {
constexpr std::size_t kOccupant = 0x4;      // base: the cISC4Occupant part
constexpr std::size_t kSerializable = 0x8;  // base: the cIGZSerializable part
constexpr std::size_t kHolder = 0xC;        // base: the property holder (vtable, count +0x10, properties +0x14 to +0x1C)
constexpr std::size_t kRefCount = 0x10;     // base: uint32 (AddRef 0x005E89B0)
constexpr std::size_t kProperties = 0x14;   // base: the holder's own properties: begin, end (+0x18), capacity; 8-byte entries {ID, property}
constexpr std::size_t kExemplarPart = 0x20; // base: the cISCExemplarPropertyHolder part
constexpr std::size_t kExemplar = 0x24;     // base: the exemplar, referenced (+0x20 slot 3; SetStaticFloraData gives it the type record's); its resource key is saved
constexpr std::size_t kType = 0x28;         // base: uint32 occupant type; the constructor sets 0x74758926; saved
constexpr std::size_t kManagerBox = 0x2C;   // base: uint8[4], the occupant manager's cells (0xFF: none); saved
constexpr std::size_t kRemoval = 0x30;      // base: uint8[2] removal counts; saved
constexpr std::size_t kState = 0x32;        // base: uint8: bit 0 initialized, bit 1 set by Read, the high four bits the highlight
constexpr std::size_t kFlags = 0x33;        // base: uint8 flags; the constructor adds 8 to the base's 4 (every tree of the saves has 0x0D); saved without 0x02 and 0x20
constexpr std::size_t kPlaceable = 0x34;    // base: the placeable object, referenced
constexpr std::size_t kFloraType = 0x38;    // uint32 (slots 4, 5; GetBoundingBox asks the simulator for its record); zero from the constructor; saved
constexpr std::size_t kLotTag = 0x3C;       // uint32: the lot object's ID of a lot's flora (occupant slots 27, 28; 0xFFFFFFFF answers "none"). NOT set by the constructor: the pool's memory until someone sets it (zero in a fresh page, a dead tree's tag in a reused item); saved
constexpr std::size_t kPosition = 0x40;     // float[3] (occupant slots 8, 9; the box's middle and base); zero from the constructor; saved
constexpr std::size_t kBirthDate = 0x4C;    // uint32 day number (slots 6, 7); saved
constexpr std::size_t kLastSeeding = 0x50;  // uint32 day number (slots 8, 9); saved
constexpr std::size_t kData = 0x54;         // the type's record (cSC4StaticFloraData, counted at its +0x170; slot 14); null from the constructor and after a read, set by the first GetBoundingBox; not saved
constexpr std::size_t kTreeState = 0x58;    // int8 (slots 10, 11; read sign extended); saved
constexpr std::size_t kOrientation = 0x59;  // uint8 (slots 12, 13; read zero extended, stored as given); saved
// +0x5A and +0x5B are never written: the pool's memory.
} // namespace field

// The type record's fields the class reads (docs/flora.md has the rest).
namespace data {
constexpr std::size_t kType = 0x0;       // uint32 flora type
constexpr std::size_t kRadius = 0x38;    // float: the box's half width and depth (GetBoundingBox 0x00472AEC)
constexpr std::size_t kHeight = 0x44;    // float: the box's height
constexpr std::size_t kFamilies = 0x154; // the family list's storage, freed with the record
constexpr std::size_t kExemplar = 0x168; // the flora exemplar, referenced
constexpr std::size_t kRefCount = 0x170; // int32
} // namespace data

// Byte offsets of the primary vtable's slots, for Virtual<T>(flora, slot::k...).
namespace slot {
constexpr std::size_t kAsOccupant = 0x0C, kGetFloraType = 0x10, kSetFloraType = 0x14, kGetBirthDate = 0x18;
constexpr std::size_t kSetBirthDate = 0x1C, kGetLastSeedingDate = 0x20, kSetLastSeedingDate = 0x24;
constexpr std::size_t kGetState = 0x28, kSetState = 0x2C, kGetOrientation = 0x30, kSetOrientation = 0x34;
constexpr std::size_t kSetStaticFloraData = 0x38;
} // namespace slot

// Byte offsets of the occupant part's slots (the part is the object + 4).
namespace occupant {
constexpr std::size_t kIsInitialized = 0x14, kAsPropertyHolder = 0x18, kGetType = 0x1C, kGetPosition = 0x20;
constexpr std::size_t kSetPosition = 0x24, kGetBoundingBox = 0x28, kGetCells = 0x2C, kPostOccupantMessage = 0x3C;
constexpr std::size_t kGetHighlight = 0x40, kGetLotTag = 0x6C, kSetLotTag = 0x70, kIsFlagSet = 0x80, kGetFlags = 0x84;
} // namespace occupant

// Logs a function's first call once ("floraocc: first <name>"; defined in
// floraocc_sim.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::floraocc_sim
