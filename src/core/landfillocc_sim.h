// Our cSC4LandfillOccupant (build 641): the class map, every vtable slot by
// slot and the object's fields. A landfill occupant is the heap of garbage
// on one tile of a landfill zone: the tile's exemplar ID (which heap is
// drawn), its orientation, its position with the box around it and one flag
// the landfill zone keeps. The landfill zone places one on every tile that
// holds garbage and removes it when the tile is empty
// (src/core/landfillzone_cells.cpp: PlaceTileOccupant, UpdateTileOccupants;
// docs/landfill.md). The rest (position in the occupant manager, removal
// counts, flags, the property holder with the exemplar) is the occupant
// base's, cSC4Occupant (src/core/occbase_sim.h), at the object + 4.
//
// Class map
//   class ID 0x8A22BEBE (GetGZCLSID 0x00477100), occupant type 0x6A11BD2B,
//   cISC4LandfillOccupant IID 0x2A15F262
//   0x68 bytes, from the pool at 0x00B09428 (8 KB pages at address &
//   0xFFFFE000: free list +0x8, used count +0xC; cSC4LandfillOccupant::
//   Allocate 0x00477320 and, inline, the factory 0x004776B0)
//   two constructors: 0x00477270 (tile ID, orientation; called by the
//   landfill zone at 0x006BC82F after the allocator at 0x006BC817, the only
//   direct `new`) and 0x004774C0 (no arguments; reached only by the
//   factory's jump, so only when a save is read)
//   deleting destructor 0x004776F0, reached only through the holder part's
//   slot 22 (thunk 0x00477250), which the base's Release 0x005E8DF0 calls
//   at the last reference
//   code 0x00477100 to 0x00477765. Before it, 0x00477050 (a message handler,
//   in the vtable 0x00A8CCB0) and 0x004770F0 are other classes'; after it,
//   0x00477770 and 0x00477790 are a container's iterator steps (called from
//   0x009D1xxx) and 0x004777B0 begins cSC4LotConfiguration. In the range
//   but not the class's: 0x004771D0 and 0x00477660, the pool class's page
//   maker and item release (the pool vtable 0x00A8CE50, shared by the
//   props' pool and by 0x00A8CE88 and 0x00AA4960), and the stubs named
//   below.
//   No derived class: the five vtables are stored by the two constructors
//   and the destructor only.
//
// +0x00 primary cISC4LandfillOccupant, vtable 0x00A8CE28, 10 slots (byte
// offset, stack arguments from the callee's ret N, function, owner):
//    0 +00 2 QueryInterface 0x00477130              class
//    1 +04 0 AddRef 0x005E28F0 (to the base's with this + 4)   base
//    2 +08 0 Release 0x005E2900                     base
//    3 +0C 0 Init 0x004771B0 (jumps to the base's 0x005E8C90 with this + 4)   class
//    4 +10 0 Shutdown 0x00619100 (the same to 0x005E8980)      shared stub (also in 0x00AA8090; called at 0x0061D115, 0x0065CF0A)
//    5 +14 0 AsOccupant 0x005FD7E0 (this + 4)       shared stub (81 vtables)
//    6 +18 1 SetTileID 0x00477450                   class
//    7 +1C 0 GetOrientation 0x00477110 (+0x60)      shared stub (also in 0x00AB28F4)
//    8 +20 1 SetOrientation 0x004771C0              class
//    9 +24 0 the flag's getter 0x00477120 (+0x64, a byte)   shared stub (8 vtables)
// +0x04 cISC4Occupant (the base cSC4Occupant starts here; its constructor
//   0x005E9220, destructor 0x005E8C40), vtable 0x00A8CDA0, 34 slots:
//    0 +00 2 QueryInterface thunk 0x00477210 (this - 4)   class
//    1, 2    AddRef 0x005E29B0, Release 0x005E29C0        base
//    3 +0C 0 Init thunk 0x00477220 (this - 4, to 0x004771B0)   class
//    4 +10 0 Shutdown thunk 0x0061BFD0 (this - 4, to 0x00619100)   shared stub (also in 0x00AA8008)
//    5 +14 0 IsInitialized 0x00458A40                     base
//    6 +18 0 AsPropertyHolder 0x006C2040 (this + 8)       shared stub
//    7 +1C 0 GetType 0x006BDD80 (+0x24 of the part)       shared stub
//    8 +20 1 GetPosition 0x005B2B00 (+0x34 of the part)   shared stub (4 vtables)
//    9 +24 1 SetPosition 0x00477600                       class
//   10 +28 2 GetBoundingBox 0x00477420                    class
//   11-26    the base's (0x005E8E20 ... 0x005E8C20; 16 GetHighlight
//            0x00458A50, 19 GetPlaceableObject 0x006AAC50 a shared stub)
//   27 +6C 1 0x005DEB40, 28 +70 1 0x00648F10              shared stubs (123 and 189 vtables)
//   29-33    SetFlag 0x005E8B70 ... GetFlags 0x005E8BE0   base
// +0x08 cIGZSerializable, vtable 0x00A8CD84, 6 slots: 0 QueryInterface
//   thunk 0x00477230 (class), 1 and 2 AddRef 0x005E29E0 and Release
//   0x005E29F0 (base), 3 Write 0x00477350 (1; class), 4 Read 0x00477560
//   (1; class), 5 GetGZCLSID 0x00477100 (class). Write and Read get `this`
//   = object + 8.
// +0x0C the property holder (cISCPropertyHolder; 0x1C bytes with its
//   exemplar holder part), vtable 0x00A8CD20, 25 slots (0x00A8CD84 follows
//   it): 0 QueryInterface thunk 0x00477240 (class), 1 and 2 0x005E2A10 and
//   0x005E2A20 (base), 3-21 the shared holder code (0x005FEC20 ...), 22
//   the deleting destructor thunk 0x00477250 (1 argument; class), 23 the
//   Write thunk 0x004774B0 and 24 the Read thunk 0x004776A0 (both this -
//   4; class).
// +0x20 cISCExemplarPropertyHolder, vtable 0x00A8CD08, 5 slots: 0
//   QueryInterface thunk 0x00477260 (class), 1 and 2 0x005E06A0 and
//   0x005E06B0 (base), 3 SetPropertyExemplar 0x00458E60 (1; shared stub,
//   in every occupant class's vtable), 4 the getter 0x0060CE80 (shared
//   stub).
// Both constructors first store 0x00A8CE60, the abstract interface's vtable
// (10 pure-call entries), which no call can meet.
//
// Nothing of the class is drawing code, and nothing of it follows frames
// or the view: its fields change only by the landfill zone's work or by a
// save read. Three bytes hold memory the class never wrote (+0x65 to
// +0x67).
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::landfillocc_sim {

constexpr uint32_t kClassId = 0x8A22BEBE;
constexpr uint32_t kOccupantType = 0x6A11BD2B;
constexpr uint32_t kIidLandfill = 0x2A15F262;
constexpr uint32_t kIidOccupant = 0x26BB767E;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr uint32_t kDefaultTile = 0x02F10000;
constexpr std::size_t kObjectSize = 0x68;

// Fields (offsets from the object's start). +0x04 to +0x37 are the occupant
// base's (evidence: its constructor 0x005E9220, Write 0x005E8CD0 and the
// slots named, which get the object + 4), +0x38 on this class's (evidence:
// the constructors 0x00477270 and 0x004774C0, Write 0x00477350 and the
// slots named).
namespace field {
constexpr std::size_t kOccupant = 0x4;      // base: the cISC4Occupant part
constexpr std::size_t kSerializable = 0x8;  // base: the cIGZSerializable part
constexpr std::size_t kHolder = 0xC;        // base: the property holder (vtable, count +0x10, properties +0x14 to +0x1C)
constexpr std::size_t kRefCount = 0x10;     // base: uint32
constexpr std::size_t kProperties = 0x14;   // base: the holder's own properties: begin, end (+0x18), capacity; 8-byte entries {ID, property}
constexpr std::size_t kExemplarPart = 0x20; // base: the cISCExemplarPropertyHolder part
constexpr std::size_t kExemplar = 0x24;     // base: the exemplar, referenced (the landfill zone sets it, +0x20 slot 3, at 0x006BC84C; SetTileID changes its key's instance); its resource key is saved
constexpr std::size_t kType = 0x28;         // base: uint32 occupant type; the constructors set 0x6A11BD2B; saved
constexpr std::size_t kManagerBox = 0x2C;   // base: uint8[4], the occupant manager's cells (0xFF: none); saved
constexpr std::size_t kRemoval = 0x30;      // base: uint8[2] removal counts; saved
constexpr std::size_t kState = 0x32;        // base: uint8: bit 0 initialized, bit 1 set by Read, the high four bits the highlight
constexpr std::size_t kFlags = 0x33;        // base: uint8 flags (4 from the base's constructor; the two-argument constructor calls SetFlag(4) again); saved without 0x02 and 0x20
constexpr std::size_t kPlaceable = 0x34;    // base: the placeable object, referenced
constexpr std::size_t kPosition = 0x38;     // float[3] (occupant slots 8, 9); the constructors copy the float 0x00B21668 (0.0); saved
constexpr std::size_t kBoxLow = 0x44;       // float[3]: x - 8, y, z - 8 (SetPosition, Read; occupant slot 10); the constructors copy 0x00B21668; not saved
constexpr std::size_t kBoxHigh = 0x50;      // float[3]: x + 8, y + 16, z + 8
constexpr std::size_t kTile = 0x5C;         // uint32: the tile's exemplar instance ID (slot 6; the constructor's first argument, else 0x02F10000); saved
constexpr std::size_t kOrientation = 0x60;  // uint32 (slots 7, 8; the constructor's second argument, else 0); saved
constexpr std::size_t kPlain = 0x64;        // uint8 (slot 9): 0 from the constructors; the landfill zone writes it when it places the occupant (0x006BC887 on); saved as whether it is not zero, read as whether the byte is 1
// +0x65 to +0x67 are never written: the pool's memory.
} // namespace field

// Byte offsets of the primary vtable's slots, for Virtual<T>(landfill, slot::k...).
namespace slot {
constexpr std::size_t kInit = 0x0C, kShutdown = 0x10, kAsOccupant = 0x14, kSetTileID = 0x18;
constexpr std::size_t kGetOrientation = 0x1C, kSetOrientation = 0x20, kIsPlain = 0x24;
} // namespace slot

// Byte offsets of the occupant part's slots (the part is the object + 4).
namespace occupant {
constexpr std::size_t kIsInitialized = 0x14, kAsPropertyHolder = 0x18, kGetType = 0x1C, kGetPosition = 0x20;
constexpr std::size_t kSetPosition = 0x24, kGetBoundingBox = 0x28, kGetCells = 0x2C, kSetAllFlags = 0x78;
constexpr std::size_t kIsFlagSet = 0x80, kGetFlags = 0x84;
} // namespace occupant

// Logs a function's first call once ("landfillocc: first <name>"; defined
// in landfillocc_sim.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::landfillocc_sim
