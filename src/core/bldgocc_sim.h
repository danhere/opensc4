// Our cSC4BuildingOccupant (build 641): the class map, every vtable slot by
// slot and the object's fields. A building occupant is one building of the
// city: its building type, its box in world coordinates, its orientation,
// whether its night lights are on and how far its construction is. The
// rest (position in the occupant manager, removal counts, flags, the
// property holder with the exemplar) is the occupant base's, cSC4Occupant,
// which every kind of occupant shares and which stays the game's.
//
// Class map
//   class ID 0xA9BD882D (GetGZCLSID 0x004587C0), occupant type 0x278128A0,
//   cISC4BuildingOccupant IID 0x87DDA3A9
//   0x5C bytes, from the pool at 0x00B07F1C (8 KB pages at address &
//   0xFFFFE000: free list +0x8, used count +0xC; cSC4BuildingOccupant::
//   Allocate 0x00458A60 and, inline, the factory 0x00458F40)
//   constructor 0x00458E90 (called by the factory, which a save's reader
//   uses, and by the development simulator's 0x00698E2D)
//   deleting destructor 0x00458F80, reached only through the holder part's
//   slot 22 (thunk 0x00458A20), which the base's Release 0x005E8DF0 calls
//   at the last reference
//   code 0x004587C0 to 0x004592FB. In that range but not the class's:
//   0x00458960 (the zone manager's density helper), 0x004589B0 and
//   0x00458F00 (the pool vtable's two functions, shared by four pools),
//   0x00458E00, 0x00459000, 0x00459170, 0x004591E0, 0x00459220 (map, vector
//   and building profile helpers many classes call).
//
// +0x00 primary cISC4BuildingOccupant, vtable 0x00A872C0, 20 slots (byte
// offset, stack arguments from the callee's ret N, function, owner):
//    0 +00 2 QueryInterface 0x004587D0              class
//    1 +04 0 AddRef 0x005E28F0 (to the base's 0x005E89B0 with this + 4)   base
//    2 +08 0 Release 0x005E2900 (to the base's 0x005E8DF0)                base
//    3 +0C 0 AsOccupant 0x005FD7E0 (this + 4)       shared stub
//    4 +10 0 GetBuildingType 0x0080BC00 (+0x38)     shared stub
//    5 +14 1 SetBuildingType 0x006CE240             shared stub
//    6 +18 0 GetBuildingAge 0x004E13E0 (zero)       shared stub
//    7 +1C 1 SetBuildingAge 0x00648F10 (nothing)    shared stub
//    8 +20 2 SetBoundingBox 0x00458C60              class (folded with the prop occupants')
//    9 +24 0 GetCompletionPercent 0x00458810        class
//   10 +28 1 SetCompletionPercent 0x00458820        class
//   11 +2C 0 GetBuildingProfile 0x00459250          class
//   12 +30 0 GetBuildingName 0x004590E0             class
//   13 +34 0 GetExemplarName 0x00459050             class
//   14 +38 0 GetOrientation 0x00458830              class
//   15 +3C 1 SetOrientation 0x00458840              class
//   16 +40 0 IsLit 0x00458870                       class
//   17 +44 1 SetLit 0x00458C90                      class
//   18 +48 1 SetName 0x00458880                     class
//   19 +4C 1 GetName 0x004588F0                     class
//   (gzcom-dll's cISC4BuildingOccupant.h has the order right.)
// +0x04 cISC4Occupant (the base cSC4Occupant starts here; its constructor
//   0x005E9220, destructor 0x005E8C40), vtable 0x00A87238, 34 slots:
//    0 +00 2 QueryInterface thunk 0x004589F0 (this - 4)   class
//    1, 2    AddRef 0x005E29B0, Release 0x005E29C0        base
//    3 +0C 0 Init 0x005EC940                              base
//    4 +10 0 Shutdown 0x00458800 (a jump to the base's 0x005E8980)  class
//    5 +14 0 IsInitialized 0x00458A40                     class (base inline, folded: 57 vtables)
//    6 +18 0 AsPropertyHolder 0x006C2040 (this + 8)       shared stub
//    7 +1C 0 GetType 0x006BDD80 (+0x24 of the part)       shared stub
//    8 +20 1 GetPosition 0x00458BA0                       class
//    9 +24 1 SetPosition 0x00458BD0                       class
//   10 +28 2 GetBoundingBox 0x00458C30                    class
//   11-14    GetBoundingCityCells 0x005E8E20, SetRemovalFlags 0x005E8A60,
//            UnsetRemovalFlags 0x005E8AA0, CanRemove 0x005E8AE0   base
//   15 +3C 2 PostOccupantMessage 0x00458D10               class
//   16 +40 0 GetHighlight 0x00458A50                      class (base inline, folded: 57 vtables)
//   17, 18   SetHighlight 0x005E90E0, SetVisibility 0x005E8B10   base
//   19 +4C 0 GetPlaceableObject 0x006AAC50                shared stub
//   20-26    0x005E91C0, 0x0078DAF0, 0x005E92D0, 0x005E9350, 0x005E9410,
//            0x005E8BF0, 0x005E8C20                       base
//   27 +6C 1 GetLotTag 0x005DEB40 (false), 28 SetLotTag 0x00648F10   shared stubs
//   29-33    SetFlag 0x005E8B70, SetAllFlags 0x005E8B90, ClearFlag
//            0x005E8BA0, IsFlagSet 0x005E8BC0, GetFlags 0x005E8BE0   base
// +0x08 cIGZSerializable, vtable 0x00A8721C, 6 slots: 0 QueryInterface
//   thunk 0x00458A00 (class), 1 and 2 AddRef 0x005E29E0 and Release
//   0x005E29F0 (base), 3 Write 0x00458A90 (1; class), 4 Read 0x00458B10
//   (1; class), 5 GetGZCLSID 0x004587C0 (class). Write and Read get `this`
//   = object + 8.
// +0x0C the property holder (cISCPropertyHolder; 0x1C bytes with its
//   exemplar holder part), vtable 0x00A871B8, 25 slots (0x00A8721C follows
//   it): 0 QueryInterface thunk 0x00458A10 (class), 1 and 2 0x005E2A10 and
//   0x005E2A20 (base), 3-21 the shared holder code (0x005FEC20 ...), 22
//   the deleting destructor thunk 0x00458A20 (1 argument; class), 23 the
//   Write thunk 0x00458E40 and 24 the Read thunk 0x00458E50 (both this -
//   4; class).
// +0x20 cISCExemplarPropertyHolder, vtable 0x00A871A0, 5 slots: 0
//   QueryInterface thunk 0x00458A30 (class), 1 and 2 0x005E06A0 and
//   0x005E06B0 (base), 3 SetPropertyExemplar 0x00458E60 (1; the holder
//   base's inline code, folded into every occupant class's vtable), 4 the
//   getter 0x0060CE80 (shared stub).
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::bldgocc_sim {

constexpr uint32_t kClassId = 0xA9BD882D;
constexpr uint32_t kOccupantType = 0x278128A0;
constexpr uint32_t kIidBuilding = 0x87DDA3A9;
constexpr std::size_t kObjectSize = 0x5C;

// Fields (offsets from the object's start). +0x04 to +0x37 are the occupant
// base's (evidence: its constructor 0x005E9220, Write 0x005E8CD0 and the
// slots named, which get the object + 4), +0x38 on this class's (evidence:
// the constructor 0x00458E90, Write 0x00458A90 and the slots named).
namespace field {
constexpr std::size_t kOccupant = 0x4;      // base: the cISC4Occupant part
constexpr std::size_t kSerializable = 0x8;  // base: the cIGZSerializable part
constexpr std::size_t kHolder = 0xC;        // base: the property holder (vtable, count +0x10, properties +0x14 to +0x1C)
constexpr std::size_t kRefCount = 0x10;     // base: uint32 (AddRef 0x005E89B0)
constexpr std::size_t kProperties = 0x14;   // base: the holder's own properties: begin, end (+0x18), capacity; 8-byte entries {ID, property}
constexpr std::size_t kExemplarPart = 0x20; // base: the cISCExemplarPropertyHolder part
constexpr std::size_t kExemplar = 0x24;     // base: the exemplar's holder, referenced (+0x20 slots 3, 4); its resource key is saved
constexpr std::size_t kType = 0x28;         // base: uint32 occupant type; the constructor sets 0x278128A0; saved
constexpr std::size_t kManagerBox = 0x2C;   // base: uint8[4], the occupant manager's cells (0xFF: none); saved
constexpr std::size_t kRemoval = 0x30;      // base: uint8[2] removal counts; saved
constexpr std::size_t kState = 0x32;        // base: uint8: bit 0 initialized (slot 5), bit 1 set by Read, the high four bits the highlight (slot 16; saved)
constexpr std::size_t kFlags = 0x33;        // base: uint8 flags (slots 29-33); saved without 0x02 and 0x20
constexpr std::size_t kPlaceable = 0x34;    // base: the placeable object, referenced (slot 19)
constexpr std::size_t kBuildingType = 0x38; // uint32 (slots 4, 5; GetBuildingProfile and the names ask the development simulator with it); saved
constexpr std::size_t kMin = 0x3C;          // float[3]: the box's low corner (primary slot 8, occupant slots 8-10); saved
constexpr std::size_t kMax = 0x48;          // float[3]: the box's high corner; saved
constexpr std::size_t kOrientation = 0x54;  // int8 0-3 (slots 14, 15); saved
constexpr std::size_t kLit = 0x55;          // uint8: bit 0 the night lights (slots 16, 17; SetLit flips it); saved whole
constexpr std::size_t kCompletion = 0x58;   // float: construction completion, 1.0 from the constructor (slots 9, 10); saved
// +0x56 and +0x57 are never written: the pool's memory.
} // namespace field

// Byte offsets of the primary vtable's slots, for Virtual<T>(building, slot::k...).
namespace slot {
constexpr std::size_t kAsOccupant = 0x0C, kGetBuildingType = 0x10, kSetBuildingType = 0x14, kGetBuildingAge = 0x18;
constexpr std::size_t kSetBoundingBox = 0x20, kGetCompletionPercent = 0x24, kSetCompletionPercent = 0x28;
constexpr std::size_t kGetBuildingProfile = 0x2C, kGetBuildingName = 0x30, kGetExemplarName = 0x34;
constexpr std::size_t kGetOrientation = 0x38, kSetOrientation = 0x3C, kIsLit = 0x40, kSetLit = 0x44;
constexpr std::size_t kSetName = 0x48, kGetName = 0x4C;
} // namespace slot

// Byte offsets of the occupant part's slots (the part is the object + 4).
namespace occupant {
constexpr std::size_t kIsInitialized = 0x14, kAsPropertyHolder = 0x18, kGetType = 0x1C, kGetPosition = 0x20;
constexpr std::size_t kSetPosition = 0x24, kGetBoundingBox = 0x28, kGetCells = 0x2C, kPostOccupantMessage = 0x3C;
constexpr std::size_t kGetHighlight = 0x40, kIsFlagSet = 0x80, kGetFlags = 0x84;
} // namespace occupant

// Logs a function's first call once ("bldgocc: first <name>"; defined in
// bldgocc_sim.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::bldgocc_sim
