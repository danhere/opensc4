// Our cSC4LotRetainingWallOccupant (build 641): the class map, every vtable
// slot by slot and the object's fields. A retaining wall occupant is the
// walls around one lot that was levelled into a slope: one object per lot,
// in the occupant manager at the lot's cells. It holds the wall type, the
// lot, a hash per side of what borders that side (the neighbouring lots and
// the terrain's heights along it; zero for a side without a wall), which
// sides have their geometry, the position (the middle of the lot at the
// lot's height) and the size (the lot's width and depth, and the walls'
// height). The lot developer makes and removes it through the helper
// 0x005E3070 (src/core/lot_build.cpp, docs/lot.md). The rest (position in
// the occupant manager, removal counts, flags, the property holder with the
// exemplar) is the occupant base's, cSC4Occupant (src/core/occbase_sim.h),
// at the object + 4.
//
// Class map
//   class ID 0x49C05C9F (GetGZCLSID 0x005E2990), occupant type 0x895D1169,
//   cISC4LotRetainingWallOccupant IID 0x295D1162 (the class ID itself is
//   not answered by QueryInterface)
//   0x6C bytes, from the pool at 0x00B0A8AC (8 KB pages of 75 items at
//   address & 0xFFFFE000: free list +0x8, used count +0xC; Allocate
//   0x005E2A50 and, inline, the factory 0x005E3330; the pool's own page
//   maker 0x005E2950 and item free 0x005E32F0 are in its tables 0x00AA2F24
//   and 0x00AA30B8)
//   constructor 0x005E2F40, called by the helper 0x005E3070 at 0x005E30F5
//   (after Allocate at 0x005E30E7) and jumped to by the factory at
//   0x005E335C (the class registration's pointer, pushed at 0x0046081B: a
//   save's reader makes every wall with it; it returns the object itself,
//   not the occupant part): the only two sites
//   deleting destructor 0x005E3370, reached only through the holder part's
//   slot 22 (thunk 0x005E2A30), which the base's Release 0x005E8DF0 calls
//   at the last reference
//   code 0x005E28C0 to 0x005E33F6. 0x005E28B0 before it is an adjustor of
//   the class before (the wall's drawable, vtable 0x00AA2EA8). In the range
//   but not the class's own: 0x005E28F0 to 0x005E2A20 (the AddRef and
//   Release thunks every occupant class's vtables share) and the pool's
//   0x005E2950 and 0x005E32F0. After it, from 0x005E3400, come the lot's
//   wall helpers that build the walls' drawables (0x005E3400, 0x005E3750,
//   0x005E37F0, 0x005E3960): drawing code, not this class's.
//   No derived class: the five vtables are stored by the constructor and
//   the destructor only, and no other vtable holds one of the class's own
//   functions.
//
// +0x00 primary cISC4LotRetainingWallOccupant, vtable 0x00AA3080, 13 slots
// (byte offset, stack arguments from the callee's ret N, function, owner;
// the abstract interface's vtable is 0x00AA2F30):
//    0 +00 2 QueryInterface 0x005E28C0              class
//    1 +04 0 AddRef 0x005E28F0 (to the base's 0x005E89B0 with this + 4)   base
//    2 +08 0 Release 0x005E2900 (to the base's 0x005E8DF0)                base
//    3 +0C 0 AsOccupant 0x005FD7E0 (this + 4)       shared stub
//    4 +10 1 SetSize 0x005E2C90                     class
//    5 +14 0 GetLot 0x008265C0 (+0x3C)              shared stub (8 vtables)
//    6 +18 1 SetLot 0x005E2CB0                      class
//    7 +1C 0 GetEdgeFlags 0x0068D1B0 (+0x40)        shared stub (10 vtables, 3 direct calls)
//    8 +20 1 SetEdgeFlags 0x007B62D0                shared stub (7 vtables)
//    9 +24 1 GetEdgeHashArray 0x005E2910            class
//   10 +28 1 SetEdgeHashArray 0x005E2930            class
//   11 +2C 0 GetRetainingWallType 0x0080BC00 (+0x38)   shared stub (31 vtables)
//   12 +30 1 SetRetainingWallType 0x006CE240        shared stub (6 vtables)
//   (gzcom-dll's cISC4LotRetainingWallOccupant.h has the order right.)
// +0x04 cISC4Occupant (the base cSC4Occupant starts here; its constructor
//   0x005E9220, destructor 0x005E8C40), vtable 0x00AA2FF8, 34 slots:
//    0 +00 2 QueryInterface thunk 0x005E29A0 (this - 4)   class
//    1, 2    AddRef 0x005E29B0, Release 0x005E29C0        base
//    3 +0C 0 Init 0x005DEA50 (a jump to the base's 0x005E8C90; in two
//            other classes' vtables too)                  base
//    4 +10 0 Shutdown 0x005E2A80                          class
//    5 +14 0 IsInitialized 0x00458A40                     base
//    6 +18 0 AsPropertyHolder 0x006C2040 (this + 8)       shared stub
//    7 +1C 0 GetType 0x006BDD80 (+0x24 of the part)       shared stub
//    8 +20 1 GetPosition 0x007FF230 (+0x50 of the part)   shared stub (folded with the camera's getter, called directly at 0x007F8431)
//    9 +24 1 SetPosition 0x005E2C10                       class
//   10 +28 2 GetBoundingBox 0x005E2C30                    class
//   11-26    the base's (0x005E8E20 ... 0x005E8C20; 16 GetHighlight
//            0x00458A50, 19 GetPlaceableObject 0x006AAC50, 21
//            SetPlaceableObject 0x0078DAF0)
//   27 +6C 1 0x005DEB40 (false)                           shared stub (74 vtables)
//   28 +70 1 0x00648F10 (nothing)                         shared stub
//   29-33    SetFlag 0x005E8B70 ... GetFlags 0x005E8BE0   base
// +0x08 cIGZSerializable, vtable 0x00AA2FDC, 6 slots: 0 QueryInterface
//   thunk 0x005E29D0 (class), 1 and 2 AddRef 0x005E29E0 and Release
//   0x005E29F0 (base), 3 Write 0x005E2AB0 (1; class), 4 Read 0x005E2B40
//   (1; class), 5 GetGZCLSID 0x005E2990 (class). Write and Read get `this`
//   = object + 8.
// +0x0C the property holder (cISCPropertyHolder; 0x1C bytes with its
//   exemplar holder part), vtable 0x00AA2F78, 25 slots: 0 QueryInterface
//   thunk 0x005E2A00 (class), 1 and 2 0x005E2A10 and 0x005E2A20 (base),
//   3-21 the shared holder code (0x005FEC20 ...), 22 the deleting
//   destructor thunk 0x005E2A30 (1 argument; class), 23 the Write thunk
//   0x005E2F20 and 24 the Read thunk 0x005E2F30 (both this - 4; class).
// +0x20 cISCExemplarPropertyHolder, vtable 0x00AA2F64, 5 slots: 0
//   QueryInterface thunk 0x005E2A40 (class), 1 and 2 0x005E06A0 and
//   0x005E06B0 (base), 3 SetPropertyExemplar 0x00458E60 (1; shared stub,
//   in every occupant class's vtable), 4 the getter 0x0060CE80 (shared
//   stub).
// The constructor first stores 0x00AA2F30, the abstract interface's vtable
// (13 pure-call entries), which no call can meet.
//
// Free functions of the class's file, between its methods:
//   0x005E2CE0 (lot, altitude, cells, hashes, highest): the four sides'
//     hashes. Called by the helper below (0x005E30CB) and by the lot's
//     wall update 0x005E37F0 (0x005E38AA). Ours (a detour).
//   0x005E2ED0 (lot): a wall type drawn from the lot configuration's list
//     with the file's own generator 0x00B230C8. Only 0x005E3070 calls it.
//     Ours (part of the helper).
//   0x005E3070 (lot, make): the lot's wall made and put in the occupant
//     manager, or found there and removed. Called by the lot developer
//     (0x006C8A3D, 0x006C8C23; src/core/lot_build.cpp calls it at its
//     address) and by the wall update (0x005E392E, 0x005E3947). Ours (a
//     detour).
//   0x005E2FC0 (placeable object, side, out): the drawable of one side
//     among a placeable object's drawables. Only the drawable builder
//     0x005E3400 calls it (0x005E36B3). Drawing code: the game's.
//
// Drawing: none of the class's methods draws, builds geometry or talks to
// the renderer. The walls' geometry is made by 0x005E3400 (a drawable of
// the class before per side, added to the occupant's placeable object),
// which also sets or clears the side's bit in the edge flags (+0x40)
// according to whether the geometry could be made. So that one field is
// the view's: the helper sets it for the sides with a hash, Read sets those
// bits again, and the drawing code changes it after (94 of the 184 walls
// of the test save Rush Hour d0365 have a hash on a side whose bit is
// clear).
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::wallocc_sim {

constexpr uint32_t kClassId = 0x49C05C9F;
constexpr uint32_t kOccupantType = 0x895D1169;
constexpr uint32_t kIidWall = 0x295D1162;
constexpr uint32_t kIidLot = 0xE818FCED;
constexpr std::size_t kObjectSize = 0x6C;

// Fields (offsets from the object's start). +0x04 to +0x37 are the occupant
// base's (evidence: its constructor 0x005E9220, Write 0x005E8CD0 and the
// slots named, which get the object + 4), +0x38 on this class's (evidence:
// the constructor 0x005E2F40, Write 0x005E2AB0 and the slots named). The
// constructor sets every one of them: nothing is left as the pool's memory.
namespace field {
constexpr std::size_t kOccupant = 0x4;      // base: the cISC4Occupant part
constexpr std::size_t kSerializable = 0x8;  // base: the cIGZSerializable part
constexpr std::size_t kHolder = 0xC;        // base: the property holder (vtable, count +0x10, properties +0x14 to +0x1C)
constexpr std::size_t kRefCount = 0x10;     // base: uint32 (AddRef 0x005E89B0)
constexpr std::size_t kProperties = 0x14;   // base: the holder's own properties: begin, end (+0x18), capacity; 8-byte entries {ID, property}
constexpr std::size_t kExemplarPart = 0x20; // base: the cISCExemplarPropertyHolder part
constexpr std::size_t kExemplar = 0x24;     // base: the exemplar, referenced (the development simulator's LoadLotRetainingWallProperties sets the type's); its resource key is saved
constexpr std::size_t kType = 0x28;         // base: uint32 occupant type; the constructor sets 0x895D1169; saved
constexpr std::size_t kManagerBox = 0x2C;   // base: uint8[4], the occupant manager's cells (0xFF: none); saved
constexpr std::size_t kRemoval = 0x30;      // base: uint8[2] removal counts; saved
constexpr std::size_t kState = 0x32;        // base: uint8: bit 0 initialized, bit 1 set by Read, the high four bits the highlight
constexpr std::size_t kFlags = 0x33;        // base: uint8 flags (the base's 4; this constructor adds none); saved without 0x02 and 0x20
constexpr std::size_t kPlaceable = 0x34;    // base: the placeable object, referenced (the walls' drawables hang on it)
constexpr std::size_t kWallType = 0x38;     // uint32 retaining wall type (slots 11, 12): the exemplar's instance; zero from the constructor; saved
constexpr std::size_t kLot = 0x3C;          // the lot (cISC4Lot*), referenced (slots 5, 6; released by Shutdown and the destructor); null from the constructor; saved as a reference
constexpr std::size_t kEdgeFlags = 0x40;    // int32 (slots 7, 8): bit n = side n has its geometry; the helper sets the sides with a hash, Read sets them again, the drawing code 0x005E3400 sets and clears them; zero from the constructor; saved
constexpr std::size_t kEdgeHashes = 0x44;   // uint32[4] (slots 9, 10): a hash per side from 0x005E2CE0, zero for no wall; zero from the constructor; saved
constexpr std::size_t kPosition = 0x54;     // float[3] (occupant slots 8, 9): the middle of the lot's cells, y the lot's altitude; zero from the constructor; saved
constexpr std::size_t kSize = 0x60;         // float[3] width, height, depth (slot 4; the box in GetBoundingBox); the constructor copies the float at 0x00B230C0 three times; saved
} // namespace field

// Byte offsets of the primary vtable's slots, for Virtual<T>(wall, slot::k...).
namespace slot {
constexpr std::size_t kAsOccupant = 0x0C, kSetSize = 0x10, kGetLot = 0x14, kSetLot = 0x18, kGetEdgeFlags = 0x1C;
constexpr std::size_t kSetEdgeFlags = 0x20, kGetEdgeHashArray = 0x24, kSetEdgeHashArray = 0x28;
constexpr std::size_t kGetRetainingWallType = 0x2C, kSetRetainingWallType = 0x30;
} // namespace slot

// Byte offsets of the occupant part's slots (the part is the object + 4).
namespace occupant {
constexpr std::size_t kInit = 0x0C, kShutdown = 0x10, kIsInitialized = 0x14, kAsPropertyHolder = 0x18, kGetType = 0x1C;
constexpr std::size_t kGetPosition = 0x20, kSetPosition = 0x24, kGetBoundingBox = 0x28, kIsFlagSet = 0x80;
constexpr std::size_t kGetFlags = 0x84;
} // namespace occupant

// Logs a function's first call once ("wallocc: first <name>"; defined in
// wallocc_sim.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::wallocc_sim
