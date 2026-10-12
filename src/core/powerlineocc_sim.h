// Our cSC4PowerLineOccupant (build 641): the class map, every vtable slot by
// slot and the object's fields. A power line occupant is the wire between two
// power poles: it holds the two poles it joins (referenced, with a flag), the
// position of the wire (a point in the city, 3 floats) and the cell of that
// position, and a preview flag the power line tool sets while the player is
// still placing the wire. The power network's builder
// (the function around 0x00651CD4, which calls the allocator 0x0064A2E0 and
// the constructor 0x0064A270) makes one for each pair of poles; the poles are
// another class, cSC4PowerPoleOccupant. The rest (position in the occupant
// manager, removal counts, flags, the property holder with the exemplar) is
// the occupant base's, cSC4Occupant (src/core/occbase_sim.h), at the
// object + 4. This class derives from the plain occupant base, not from the
// network occupant (src/core/netocc_sim.h).
//
// Class map
//   class ID 0xC9C05C5D (GetGZCLSID 0x00649260), occupant type 0x0990B2EA,
//   cISC4PowerLineOccupant IID 0xC98F3B24 (the class ID and 0xA9764C8C are
//   answered too, by the same object)
//   0x58 bytes, from the pool at 0x00B0C258 (8 KB pages at address &
//   0xFFFFE000: free list +0x8, used count +0xC; allocator 0x0064A2E0 and,
//   inline, the factory 0x0064AB70)
//   constructor 0x0064A270, called by the factory (a tail jump; the class
//   registration's pointer, pushed at 0x004606FB: a save's reader makes
//   every wire with it, and it returns the OBJECT, not the occupant part)
//   and by the power network's builder at 0x00651CE4 (after the allocator at
//   0x00651CD6): the only two sites
//   deleting destructor 0x0064ABF0, reached only through the holder part's
//   slot 22 (thunk 0x006498F0), which the base's Release 0x005E8DF0 calls at
//   the last reference
//   code in pieces among the pole occupant's (class ID 0x09C05C6A, the class ID
//   of the two references a wire saves; its QueryInterface answers the IID
//   0xA8C86142 this class's Read gives ReadReference): 0x00649260 to 0x006493A0, the thunks 0x006498C0 to
//   0x00649900, 0x0064A270 to 0x0064A4A0, the thunks 0x0064A540 and
//   0x0064A550, and 0x0064AB70 to 0x0064AC62. The range 0x00648F10 to
//   0x0064AC00 holds 60 function starts, 23 of them this class's (with the
//   thunks); the others are listed in docs/powerlineocc.md.
//   No derived class: the five vtables are stored by the constructor and the
//   destructor only, and no other vtable holds one of the class's own
//   functions.
//
// +0x00 cISC4PowerLineOccupant, vtable 0x00AA9C80, 8 slots (byte offset,
// stack arguments from the callee's ret N, function, owner):
//    0 +00 2 QueryInterface 0x00649350                  class
//    1 +04 0 AddRef 0x005E28F0 (to the base's 0x005E89B0 with this + 4)   base
//    2 +08 0 Release 0x005E2900 (to the base's 0x005E8DF0)                base
//    3 +0C 3 GetConnectedPoles 0x00649290               class
//    4 +10 3 SetConnectedPoles 0x006492D0               class
//    5 +14 2 GetCell 0x00649330                         class
//    6 +18 0 IsPreview 0x00649390                       class
//    7 +1C 1 SetPreview 0x006493A0                      class
//   (gzcom-dll's cISC4PowerLineOccupant.h has the order and the counts
//   right but types the first output as a power line and every method as
//   bool; the asm gives two pole pointers and a byte out, and EAX is
//   whatever the code leaves.) The abstract interface's vtable follows at
//   0x00AA9B40, stored first by the constructor and overwritten.
// +0x04 cISC4Occupant (the base cSC4Occupant starts here; its constructor
//   0x005E9220, destructor 0x005E8C40), vtable 0x00AA9BF8, 34 slots:
//    0 +00 2 QueryInterface thunk 0x006498C0 (this - 4)   class
//    1, 2    AddRef 0x005E29B0, Release 0x005E29C0        base
//    3 +0C 0 0x005DEA50 (a thunk to the base's Init)      shared
//    4 +10 0 Shutdown 0x00649270                          class
//    5 +14 0 IsInitialized 0x00458A40                     base
//    6 +18 0 AsPropertyHolder 0x006C2040 (this + 8)       shared stub
//    7 +1C 0 GetType 0x006BDD80 (+0x24 of the part)       shared stub
//    8 +20 1 GetPosition 0x005B2B00                       shared stub
//    9 +24 1 SetPosition 0x0064A460                       class
//   10 +28 2 GetBoundingBox 0x0064A4A0                    class
//   11-26    the base's (0x005E8E20 ... 0x005E8C20; 16 GetHighlight
//            0x00458A50, 19 GetPlaceableObject 0x006AAC50, 21
//            SetPlaceableObject 0x0078DAF0)
//   27 +6C 1 0x005DEB40 (false)                           shared stub
//   28 +70 1 0x00648F10 (nothing)                         shared stub
//   29-33    SetFlag 0x005E8B70 ... GetFlags 0x005E8BE0   base
// +0x08 cIGZSerializable, vtable 0x00AA9BDC, 6 slots: 0 QueryInterface
//   thunk 0x006498D0 (class), 1 and 2 AddRef 0x005E29E0 and Release
//   0x005E29F0 (base), 3 Write 0x0064A310 (1; class), 4 Read 0x0064A3A0
//   (1; class), 5 GetGZCLSID 0x00649260 (class). Write and Read get `this`
//   = object + 8.
// +0x0C the property holder (cISCPropertyHolder; 0x1C bytes with its
//   exemplar holder part), vtable 0x00AA9B78, 25 slots: 0 QueryInterface
//   thunk 0x006498E0 (class), 1 and 2 0x005E2A10 and 0x005E2A20 (base),
//   3-21 the shared holder code (0x005FEC20 ...), 22 the deleting
//   destructor thunk 0x006498F0 (1 argument; class), 23 the Write thunk
//   0x0064A540 and 24 the Read thunk 0x0064A550 (both this - 4; class).
// +0x20 cISCExemplarPropertyHolder, vtable 0x00AA9B60, 5 slots: 0
//   QueryInterface thunk 0x00649900 (class), 1 and 2 0x005E06A0 and
//   0x005E06B0 (base), 3 SetPropertyExemplar 0x00648F10 (1; the stub that
//   does nothing), 4 the getter 0x0060CE80 (shared stub).
//
// None of the class's functions draws, builds geometry, loads a model or
// talks to the renderer. (Whatever models the wires have is the poles' and the
// geometry helpers' in the range; docs/powerlineocc.md.)
//
// Nothing of the class follows frames or the view but the preview flag:
// the fields change only through the setters (the power network's builder
// calls them when it makes a wire) and by a save read. Every field from
// +0x38 to +0x55 is set by the constructor; +0x56 and +0x57 are never
// written (the pool's memory).
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::powerlineocc_sim {

constexpr uint32_t kClassId = 0xC9C05C5D;
constexpr uint32_t kOccupantType = 0x0990B2EA;
constexpr uint32_t kIidPowerLine = 0xC98F3B24;
constexpr uint32_t kIidShared = 0xA9764C8C;
constexpr uint32_t kIidPole = 0xA8C86142; // the pole occupant's: what Read asks ReadReference for
constexpr std::size_t kObjectSize = 0x58;

// Fields (offsets from the object's start). +0x04 to +0x37 are the occupant
// base's (evidence: its constructor 0x005E9220, Write 0x005E8CD0 and the
// slots named, which get the object + 4), +0x38 on this class's (evidence:
// the constructor 0x0064A270, Write 0x0064A310 and the slots named).
namespace field {
// base: the cISC4Occupant part, the cIGZSerializable part, the property
// holder (vtable, count +0x10, properties +0x14 to +0x1C).
constexpr std::size_t kOccupant = 0x4;
constexpr std::size_t kSerializable = 0x8;
constexpr std::size_t kHolder = 0xC;
// base: uint32 reference count (AddRef 0x005E89B0).
constexpr std::size_t kRefCount = 0x10;
// base: the holder's own properties: begin, end (+0x18), capacity; 8-byte
// entries {ID, property}.
constexpr std::size_t kProperties = 0x14;
// base: the cISCExemplarPropertyHolder part.
constexpr std::size_t kExemplarPart = 0x20;
// base: the exemplar, referenced; its resource key is saved.
constexpr std::size_t kExemplar = 0x24;
// base: uint32 occupant type; the constructor sets 0x0990B2EA; saved.
constexpr std::size_t kType = 0x28;
// base: uint8[4], the occupant manager's cells (0xFF: none); saved.
constexpr std::size_t kManagerBox = 0x2C;
// base: uint8[2] removal counts; saved.
constexpr std::size_t kRemoval = 0x30;
// base: uint8: bit 0 initialized, bit 1 set by Read, the high four bits the
// highlight.
constexpr std::size_t kState = 0x32;
// base: uint8 flags (this constructor adds none); saved without 0x02 and
// 0x20.
constexpr std::size_t kFlags = 0x33;
// base: the placeable object, referenced.
constexpr std::size_t kPlaceable = 0x34;
// float[3] (occupant slots 9, 10): the wire's point; the constructor copies
// the float at 0x00B46778 three times; saved.
constexpr std::size_t kPosition = 0x38;
// int32: the position's x times 0.0625, truncated (SetPosition); zero from
// the constructor; saved. kCellZ is the same for z.
constexpr std::size_t kCellX = 0x44;
constexpr std::size_t kCellZ = 0x48;
// The two poles (referenced; slots 3, 4); zero from the constructor; saved
// as references.
constexpr std::size_t kPole1 = 0x4C;
constexpr std::size_t kPole2 = 0x50;
// uint8 (the third argument of slot 4; ReadBool normalises it to 0 or 1);
// saved.
constexpr std::size_t kFlag = 0x54;
// uint8 (slots 6, 7); zero from the constructor; NOT saved.
constexpr std::size_t kPreview = 0x55;
// +0x56 and +0x57 are never written: the pool's memory.
} // namespace field

// Byte offsets of the primary vtable's slots, for Virtual<T>(wire, slot::k...).
namespace slot {
constexpr std::size_t kGetConnectedPoles = 0x0C, kSetConnectedPoles = 0x10, kGetCell = 0x14, kIsPreview = 0x18;
constexpr std::size_t kSetPreview = 0x1C;
} // namespace slot

// Byte offsets of the occupant part's slots (the part is the object + 4).
namespace occupant {
constexpr std::size_t kInit = 0x0C, kShutdown = 0x10, kIsInitialized = 0x14, kAsPropertyHolder = 0x18;
constexpr std::size_t kGetType = 0x1C, kGetPosition = 0x20, kSetPosition = 0x24, kGetBoundingBox = 0x28;
constexpr std::size_t kIsFlagSet = 0x80, kGetFlags = 0x84;
} // namespace occupant

// Logs a function's first call once ("powerlineocc: first <name>"; defined
// in powerlineocc_sim.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::powerlineocc_sim
