// Our cSC4FoundationOccupant (build 641): the class map, every vtable slot
// by slot and the object's fields. A foundation occupant is the slab under
// one building on a slope: its foundation type, the height it may reach,
// its position (the middle of its top), its size, a usage number and its
// orientation. The building development simulator makes it
// (AddFoundationOccupant, src/core/development_occupants.cpp) when the lot
// developer asks for one (src/core/lot_build.cpp, docs/lot.md). The rest
// (position in the occupant manager, removal counts, flags, the property
// holder with the exemplar) is the occupant base's, cSC4Occupant
// (src/core/occbase_sim.h), at the object + 4.
//
// Class map
//   class ID 0x49C05C8F (GetGZCLSID 0x005D6130), occupant type 0x68FD0C69,
//   cISC4FoundationOccupant IID 0xC8FCE101 (the class ID itself is not
//   answered by QueryInterface)
//   0x5C bytes, from the pool at 0x00B0A830 (8 KB pages at address &
//   0xFFFFE000: free list +0x8, used count +0xC; cSC4FoundationOccupant::
//   Allocate 0x005D6220 and, inline, the factory 0x005D64C0)
//   constructor 0x005D6440, called by the factory at 0x005D64EC (the class
//   registration's pointer, pushed at 0x004607F7: a save's reader makes
//   every foundation with it; it returns the object + 4) and by the
//   building development simulator's AddFoundationOccupant at 0x00695B0B
//   (after Allocate at 0x00695AFD): the only two sites
//   deleting destructor 0x005D6500, reached only through the holder part's
//   slot 22 (thunk 0x005D6200), which the base's Release 0x005E8DF0 calls
//   at the last reference
//   code 0x005D6130 to 0x005D6575. 0x005D6120 before it is a thunk of the
//   class before (vtable 0x00AA2758); 0x005D6580 (a segment clipped to a
//   box) and 0x005D67E0 on are geometry helpers other classes call
//   (0x005F0FCC, 0x005F4E40, 0x0061B3EC, 0x0064A9F8, 0x0075A2C1,
//   0x00637A6D), not this class's. In the range but folded with the flora
//   occupant's vtables: 0x005D6140, 0x005D6150, 0x005D6360, 0x005D6380.
//   No derived class: the five vtables are stored by the constructor and
//   the destructor only, and no other vtable holds one of the class's own
//   functions.
//
// +0x00 primary cISC4FoundationOccupant, vtable 0x00AA29A0, 13 slots (byte
// offset, stack arguments from the callee's ret N, function, owner; the
// abstract interface's vtable follows at 0x00AA29D4):
//    0 +00 2 QueryInterface 0x005D6160              class
//    1 +04 0 AddRef 0x005E28F0 (to the base's 0x005E89B0 with this + 4)   base
//    2 +08 0 Release 0x005E2900 (to the base's 0x005E8DF0)                base
//    3 +0C 0 AsOccupant 0x005FD7E0 (this + 4)       shared stub
//    4 +10 0 GetFoundationType 0x0080BC00 (+0x38)   shared stub (31 vtables)
//    5 +14 1 SetFoundationType 0x006CE240           shared stub (6 vtables)
//    6 +18 0 GetMaxHeight 0x005D4AC0 (fld +0x3C)    shared stub (the class before has it twice, 0x00AA2758 slots 13 and 22)
//    7 +1C 1 SetMaxHeight 0x006CE230                shared stub (3 vtables)
//    8 +20 1 SetSize 0x005D6400                     class
//    9 +24 0 GetUsage 0x005D6140 (+0x58, signed)    shared stub (the flora occupant's 0x00A88608 slot 10)
//   10 +28 1 SetUsage 0x005D6150                    shared stub (0x00A88608 slot 11)
//   11 +2C 0 GetOrientation 0x005D6190              class
//   12 +30 1 SetOrientation 0x005D61A0              class
//   (gzcom-dll's cISC4FoundationOccupant.h has the order right.)
// +0x04 cISC4Occupant (the base cSC4Occupant starts here; its constructor
//   0x005E9220, destructor 0x005E8C40), vtable 0x00AA2918, 34 slots:
//    0 +00 2 QueryInterface thunk 0x005D61D0 (this - 4)   class
//    1, 2    AddRef 0x005E29B0, Release 0x005E29C0        base
//    3 +0C 0 Init 0x005E8C90                              base
//    4 +10 0 Shutdown 0x005E8980                          base
//    5 +14 0 IsInitialized 0x00458A40                     base
//    6 +18 0 AsPropertyHolder 0x006C2040 (this + 8)       shared stub
//    7 +1C 0 GetType 0x006BDD80 (+0x24 of the part)       shared stub
//    8 +20 1 GetPosition 0x005D6360 (+0x3C of the part)   shared stub (the flora occupant's 0x00A88580 slot 8)
//    9 +24 1 SetPosition 0x005D6380                       shared stub (0x00A88580 slot 9)
//   10 +28 2 GetBoundingBox 0x005D63A0                    class
//   11-26    the base's (0x005E8E20 ... 0x005E8C20; 16 GetHighlight
//            0x00458A50, 19 GetPlaceableObject 0x006AAC50, 21
//            SetPlaceableObject 0x0078DAF0)
//   27 +6C 1 0x005DEB40 (false)                           shared stub (74 vtables)
//   28 +70 1 0x00648F10 (nothing)                         shared stub
//   29-33    SetFlag 0x005E8B70 ... GetFlags 0x005E8BE0   base
// +0x08 cIGZSerializable, vtable 0x00AA28FC, 6 slots: 0 QueryInterface
//   thunk 0x005D61E0 (class), 1 and 2 AddRef 0x005E29E0 and Release
//   0x005E29F0 (base), 3 Write 0x005D6250 (1; class), 4 Read 0x005D62D0
//   (1; class), 5 GetGZCLSID 0x005D6130 (class). Write and Read get `this`
//   = object + 8.
// +0x0C the property holder (cISCPropertyHolder; 0x1C bytes with its
//   exemplar holder part), vtable 0x00AA2898, 25 slots (0x00AA28FC follows
//   it): 0 QueryInterface thunk 0x005D61F0 (class), 1 and 2 0x005E2A10 and
//   0x005E2A20 (base), 3-21 the shared holder code (0x005FEC20 ...), 22
//   the deleting destructor thunk 0x005D6200 (1 argument; class), 23 the
//   Write thunk 0x005D6420 and 24 the Read thunk 0x005D6430 (both this -
//   4; class).
// +0x20 cISCExemplarPropertyHolder, vtable 0x00AA2880, 5 slots: 0
//   QueryInterface thunk 0x005D6210 (class), 1 and 2 0x005E06A0 and
//   0x005E06B0 (base), 3 SetPropertyExemplar 0x00458E60 (1; shared stub,
//   in every occupant class's vtable), 4 the getter 0x0060CE80 (shared
//   stub).
// The constructor first stores 0x00AA29D4, the abstract interface's vtable
// (13 pure-call entries), which no call can meet.
//
// None of the class's functions draws, builds geometry, loads a model or
// talks to the renderer.
//
// Nothing of the class follows frames or the view: the fields change only
// through the setters (the building development simulator calls them when
// it makes a foundation) and by a save read. Every field from +0x38 to
// +0x59 is set by the constructor; +0x5A and +0x5B are never written (the
// pool's memory).
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::foundocc_sim {

constexpr uint32_t kClassId = 0x49C05C8F;
constexpr uint32_t kOccupantType = 0x68FD0C69;
constexpr uint32_t kIidFoundation = 0xC8FCE101;
constexpr std::size_t kObjectSize = 0x5C;

// Fields (offsets from the object's start). +0x04 to +0x37 are the occupant
// base's (evidence: its constructor 0x005E9220, Write 0x005E8CD0 and the
// slots named, which get the object + 4), +0x38 on this class's (evidence:
// the constructor 0x005D6440, Write 0x005D6250 and the slots named).
namespace field {
constexpr std::size_t kOccupant = 0x4;        // base: the cISC4Occupant part
constexpr std::size_t kSerializable = 0x8;    // base: the cIGZSerializable part
constexpr std::size_t kHolder = 0xC;          // base: the property holder (vtable, count +0x10, properties +0x14 to +0x1C)
constexpr std::size_t kRefCount = 0x10;       // base: uint32 (AddRef 0x005E89B0)
constexpr std::size_t kProperties = 0x14;     // base: the holder's own properties: begin, end (+0x18), capacity; 8-byte entries {ID, property}
constexpr std::size_t kExemplarPart = 0x20;   // base: the cISCExemplarPropertyHolder part
constexpr std::size_t kExemplar = 0x24;       // base: the exemplar, referenced (the development simulator's LoadFoundationProperties sets the type's); its resource key is saved
constexpr std::size_t kType = 0x28;           // base: uint32 occupant type; the constructor sets 0x68FD0C69; saved
constexpr std::size_t kManagerBox = 0x2C;     // base: uint8[4], the occupant manager's cells (0xFF: none); saved
constexpr std::size_t kRemoval = 0x30;        // base: uint8[2] removal counts; saved
constexpr std::size_t kState = 0x32;          // base: uint8: bit 0 initialized, bit 1 set by Read, the high four bits the highlight
constexpr std::size_t kFlags = 0x33;          // base: uint8 flags (the base's 4; this constructor adds none); saved without 0x02 and 0x20
constexpr std::size_t kPlaceable = 0x34;      // base: the placeable object, referenced
constexpr std::size_t kFoundationType = 0x38; // uint32 (slots 4, 5); zero from the constructor; saved
constexpr std::size_t kMaxHeight = 0x3C;      // float (slots 6, 7; the type exemplar's property 0x88FCC3E3); zero from the constructor; saved
constexpr std::size_t kPosition = 0x40;       // float[3] (occupant slots 8, 9): the middle of the slab's top; zero from the constructor; saved
constexpr std::size_t kSize = 0x4C;           // float[3] width, height, depth (slot 8; the box in GetBoundingBox); the constructor copies the float at 0x00B2303C three times; saved
constexpr std::size_t kUsage = 0x58;          // int8 (slots 9, 10; read sign extended); zero from the constructor; saved
constexpr std::size_t kOrientation = 0x59;    // int8 0 to 3 (slots 11, 12; read sign extended, clamped when set); zero from the constructor; saved
// +0x5A and +0x5B are never written: the pool's memory.
} // namespace field

// Byte offsets of the primary vtable's slots, for Virtual<T>(foundation, slot::k...).
namespace slot {
constexpr std::size_t kAsOccupant = 0x0C, kGetFoundationType = 0x10, kSetFoundationType = 0x14;
constexpr std::size_t kGetMaxHeight = 0x18, kSetMaxHeight = 0x1C, kSetSize = 0x20, kGetUsage = 0x24;
constexpr std::size_t kSetUsage = 0x28, kGetOrientation = 0x2C, kSetOrientation = 0x30;
} // namespace slot

// Byte offsets of the occupant part's slots (the part is the object + 4).
namespace occupant {
constexpr std::size_t kIsInitialized = 0x14, kAsPropertyHolder = 0x18, kGetType = 0x1C, kGetPosition = 0x20;
constexpr std::size_t kSetPosition = 0x24, kGetBoundingBox = 0x28, kIsFlagSet = 0x80, kGetFlags = 0x84;
} // namespace occupant

// Logs a function's first call once ("foundocc: first <name>"; defined in
// foundocc_sim.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::foundocc_sim
