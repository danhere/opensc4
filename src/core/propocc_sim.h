// Our cSC4PropOccupant (build 641): the class map, every vtable slot by
// slot and the object's fields. A prop occupant is one prop of the city: a
// lot's prop, a car or a person standing on a lot, a sign, a timed prop.
// Four of five occupants of a city are props. The prop holds its type, its
// box, orientation, a state the models show, and what switches that state:
// a range of hours, a range of dates, and four timer bits. The rest
// (position in the occupant manager, removal counts, flags, the property
// holder with the exemplar) is the occupant base's, cSC4Occupant
// (src/core/occbase_sim.h), at the object + 8.
//
// Class map
//   class ID 0x2977AA47 (GetGZCLSID 0x005EE0E0), occupant type 0xA823821E,
//   cISC4PropOccupant IID 0xE9793A65
//   0x68 bytes, from the pool at 0x00B0A8E4 (8 KB pages at address &
//   0xFFFFE000: free list +0x8, used count +0xC; cSC4PropOccupant::Allocate
//   0x005ECE60 and, inline, the factory 0x005EEF50)
//   constructor 0x005EE050 (called by the factory, which a save's reader
//   uses, by the prop manager at 0x004A255E and 0x004A30AB, and by the six
//   derived constructors)
//   deleting destructor 0x005EEF90, reached only through the holder part's
//   slot 22 (thunk 0x005EE130), which the base's Release calls at the last
//   reference. The class has no destructor body of its own: the date range
//   is freed by Shutdown, not here.
//   code 0x005EC960 to 0x005EF4E0, mixed with other classes'.
//
// Derived classes (each calls the prop's constructor, then stores its own
// vtables; "prop at" is where the cSC4PropOccupant part starts):
//   cSC4PropOccupantTerrainDecal  constructor 0x005EE360, prop at +4,
//       vtables 0x00AA4790 / prop 0x00AA4730 / occupant 0x00AA4698
//   the burnt building's prop      constructor 0x005EF810, prop at +0,
//       prop 0x00AA4B30 / occupant 0x00AA4A98
//   0x005EFDB0                     prop at +4, prop 0x00AA4CC8 / occupant
//       0x00AA4C30
//   cSC4PropOccupantVisualEffect  constructor 0x005F0210, prop at +4,
//       prop 0x00AA4E78 / occupant 0x00AA4DE0
//   two more (constructors near 0x005F5510 and at 0x005F8300), prop at +4,
//       prop 0x00AA5418 and 0x00AA5620 / occupant 0x00AA5380, 0x00AA5588
// Their vtables hold most of the prop's functions, and their code calls
// the prop's QueryInterface, Write, Read, DoMessage, SetPosition, Shutdown
// and three helpers directly. So every function of the prop that they
// share is replaced by a detour at its address; "shared" below.
//
// In the class's code range but not the class's: 0x005EC850, 0x005EC8C0
// (container helpers), 0x005EC930 (another class ID), 0x005EC940 (the
// occupant base's Init, folded into the building's vtable too), 0x005ECAF0
// to 0x005ECC70 and everything named for the terrain decal, the visual
// effect and the burnt building, 0x005ECD10 to 0x005ECDD0 and 0x005EEE50 to
// 0x005EEF10 (four pools' page functions), 0x005ECE10 to 0x005ECE40 and
// 0x005ED0C0 to 0x005ED150 (the placement properties), 0x005ED5A0 to
// 0x005ED6D0 (the decal's thunks).
//
// +0x00 primary cISC4PropOccupant, vtable 0x00AA4900, 24 slots (byte
// offset, stack arguments from the callee's ret N, function, owner; the
// four pools' vtables follow at 0x00AA4960):
//    0 +00 2 QueryInterface 0x005EC960             class (derived jump to it)
//    1 +04 0 AddRef 0x006491C0 (to the base's 0x005E89B0 with this + 8)   base
//    2 +08 0 Release 0x006491D0 (to the base's 0x005E8DF0)                base
//    3 +0C 0 AsOccupant 0x006C2040 (this + 8)      shared stub
//    4 +10 2 SetBoundingBox 0x00458C60             shared stub (the building occupant's, folded)
//    5 +14 0 GetPropPlacementProperties 0x004E13E0 (zero)   shared stub
//    6 +18 1 SetPropPlacementProperties 0x00648F10 (nothing)   shared stub
//    7 +1C 0 GetPropType 0x005ED280                class, shared
//    8 +20 1 SetPropType 0x005EF4C0                class, shared
//    9 +24 0 GetOrientation 0x005EC9D0             class, shared
//   10 +28 1 SetOrientation 0x005EC9E0             class, shared (not the visual effect's)
//   11 +2C 1 SetState 0x005ED290                   class, shared (0x00AA4CC8 has 0x005F0530, the effect 0x005EF2C0)
//   12 +30 0 GetState 0x005ED560                   class, shared
//   13 +34 2 SetTimeOfDayRange 0x005ED400          class, shared
//   14 +38 2 GetTimeOfDayRange 0x005ECA10          class, shared
//   15 +3C 4 SetSimulatorDateRange 0x005EEFF0      class, shared
//   16 +40 1 TickSimulatorDate 0x005EE230          class, shared
//   17 +44 1 SetRandomChance 0x007C2740 (+0x62)    shared stub (folded with 0x00ABAB60's slot 55)
//   18 +48 0 GetRandomChance 0x005ED570            class, shared (four bytes: slot swaps)
//   19 +4C 0 GetPropTimerBitfield 0x005ED580       class, shared
//   20 +50 1 GetPropTimerState 0x005ED590          class, shared
//   21 +54 2 SetPropTimerState 0x005ECA50          class, shared
//   22 +58 0 GetPropUsage 0x00477120 (+0x64)       shared stub (folded with 0x00A8CE28's slot 9)
//   23 +5C 1 SetPropUsage 0x005ECAE0               class, shared
//   (gzcom-dll's cISC4PropOccupant.h has the order right.)
// +0x04 cIGZMessageTarget2 (IID 0x452294AA), vtable 0x00AA48F0, 4 slots:
//    0 QueryInterface thunk 0x005EE0F0 (this - 4; class), 1 and 2
//    0x0064DD70 and 0x0064DD80 (base), 3 DoMessage 0x005ECFB0 (1; class,
//    shared: three derived vtables hold it and two classes call it).
// +0x08 cISC4Occupant (the base cSC4Occupant starts here), vtable
//   0x00AA4868, 34 slots:
//    0 +00 2 QueryInterface thunk 0x005EE100 (this - 8)   class
//    1, 2    AddRef 0x0064DD90, Release 0x0064DDA0        base
//    3 +0C 0 Init 0x005EC940                              base (folded with the building's)
//    4 +10 0 Shutdown 0x005EE160                          class (two derived Shutdowns call it)
//    5 +14 0 IsInitialized 0x00458A40                     base
//    6 +18 0 AsPropertyHolder 0x006C2040 (this + 8)       shared stub
//    7 +1C 0 GetType 0x006BDD80                           shared stub
//    8 +20 1 GetPosition 0x005ED1F0                       class, shared (all six)
//    9 +24 1 SetPosition 0x005EDE00                       class, shared (two; two more call it)
//   10 +28 2 GetBoundingBox 0x005DED60                    shared stub (folded with 0x00AA2D90's)
//   11-26    the base's (0x005E8E20 ... 0x005E8C20; 16 GetHighlight
//            0x00458A50, 19 GetPlaceableObject 0x006AAC50)
//   27 +6C 1 GetLotTag 0x005ECAC0                         class, shared (all six)
//   28 +70 1 SetLotTag 0x004302C0 (+0x58)                 shared stub (the flora occupant's, folded)
//   29-33    SetFlag 0x005E8B70 ... GetFlags 0x005E8BE0   base
// +0x0C cIGZSerializable, vtable 0x00AA484C, 6 slots: 0 QueryInterface
//   thunk 0x005EE110 (class), 1 and 2 0x0064DDB0 and 0x0064DDC0 (base), 3
//   Write 0x005ECE90 (1; class, six derived Writes call it), 4 Read
//   0x005EF360 (1; class, six derived Reads call it), 5 GetGZCLSID
//   0x005EE0E0 (class, this vtable only). Write and Read get `this` =
//   object + 0xC.
// +0x10 the property holder (cISCPropertyHolder), vtable 0x00AA47E8, 25
//   slots: 0 QueryInterface thunk 0x005EE120 (class), 1 and 2 0x0064DDD0
//   and 0x0064DDE0 (base), 3-21 the shared holder code (0x005FEC20 ...),
//   22 the deleting destructor thunk 0x005EE130 (1 argument; class), 23
//   the Write thunk 0x005EE150 and 24 the Read thunk 0x005EFC70 (both this
//   - 4; class).
// +0x24 cISCExemplarPropertyHolder, vtable 0x00AA47D0, 5 slots: 0
//   QueryInterface thunk 0x005EE140 (class), 1 and 2 0x0064DE00 and
//   0x0064DE10 (base), 3 SetPropertyExemplar 0x00458E60 (shared stub), 4
//   the getter 0x0060CE80 (shared stub).
//
// Not virtual: SetPlaceableFlag 0x005ED170 and SetInManager 0x005EE1A0
// (base flags 2 and 1), SetSize 0x005ED220 (derived classes only), the
// type table's lookup 0x005EF2E0, the small list 0x005ECCC0 (SetState and
// DoMessage only; written inline in ours).
//
// What follows frames, not game days (kept out of the default fingerprint
// and of the decoded save record):
//   - the state +0x63 of props with timers or the night property: the prop
//     developer switches night props in steps of its frame message
//     0x86AD10EE (0x006F0EC0; docs/prop.md), and SetPropTimerState derives
//     the state from the timer bits with a draw from a generator shared by
//     all props (0x00B231B0), whose order of draws follows the bits below;
//   - timer bit 1 (hours): from the 24 hour clock, which runs with the
//     view, by the developer's message 0x44778231 and by SetTimeOfDayRange
//     at the moment the prop is made;
//   - timer bit 4: from the view (service 0x00B43DC8, interface 0xFA47B3F9,
//     and message 0xAA375FE8);
//   - timer bit 8: from the light service by cell (message 0x8A07AE00);
//   - base flags 1 and 2 (in the occupant manager; the placeable object
//     told): the terrain code at 0x0075EED9 and the decal change them;
//   - where the box lies inside its cells (docs/occmgr.md: some props move
//     by frames);
//   - nothing else of DoMessage: it only passes frame time to the models.
// Timer bit 2 (dates) follows the simulator's date only (the developer
// calls TickSimulatorDate on message 0x66956814), as do the date range
// itself, the hours, the type, orientation, chance, usage and the lot tag.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::propocc_sim {

constexpr uint32_t kClassId = 0x2977AA47;
constexpr uint32_t kOccupantType = 0xA823821E;
constexpr uint32_t kIidProp = 0xE9793A65;
constexpr uint32_t kIidMessageTarget = 0x452294AA;
constexpr std::size_t kObjectSize = 0x68;

// Fields (offsets from the object's start). +0x08 to +0x3B are the occupant
// base's (evidence: its constructor 0x005E9220 on the object + 8 and the
// base's slots), +0x3C on this class's (evidence: the constructor
// 0x005EE050, Write 0x005ECE90 and the slots named).
namespace field {
constexpr std::size_t kMessageTarget = 0x4; // the cIGZMessageTarget2 part
constexpr std::size_t kOccupant = 0x8;      // base: the cISC4Occupant part
constexpr std::size_t kSerializable = 0xC;  // base: the cIGZSerializable part
constexpr std::size_t kHolder = 0x10;       // base: the property holder (count +0x14, properties +0x18 to +0x20)
constexpr std::size_t kProperties = 0x18;   // base: the holder's own properties: begin, end, capacity; 8-byte entries
constexpr std::size_t kExemplarPart = 0x24; // base: the cISCExemplarPropertyHolder part
constexpr std::size_t kExemplar = 0x28;     // base: the exemplar, referenced
constexpr std::size_t kType = 0x2C;         // base: uint32 occupant type; the constructor sets 0xA823821E
constexpr std::size_t kManagerBox = 0x30;   // base: uint8[4], the occupant manager's cells
constexpr std::size_t kRemoval = 0x34;      // base: uint8[2] removal counts
constexpr std::size_t kBaseState = 0x36;    // base: uint8: bit 0 initialized, bit 1 read from a save, high half the highlight
constexpr std::size_t kFlags = 0x37;        // base: uint8 flags; for a prop bit 1 = in the occupant manager (0x005EE1A0), bit 2 = the placeable object told (0x005ED170)
constexpr std::size_t kPlaceable = 0x38;    // base: the placeable object (occupant slot 19)
constexpr std::size_t kMin = 0x3C;          // float[3]: the box's low corner (primary slot 4, occupant slots 8-10); saved
constexpr std::size_t kMax = 0x48;          // float[3]: the box's high corner; saved
constexpr std::size_t kDates = 0x54;        // the date range, 0x28 bytes from the game's heap, or null (slots 15, 16; freed by Shutdown); saved when there
constexpr std::size_t kLotTag = 0x58;       // uint32, -1 from the constructor: none (occupant slots 27, 28); saved
constexpr std::size_t kTypeIndex = 0x5C;    // uint16: index of the prop type's ID in the table 0x00B231A4 (slots 7, 8); the ID is saved
constexpr std::size_t kHours = 0x5E;        // uint8[2]: start and end hour in tenths (slots 13, 14); saved
constexpr std::size_t kTimer = 0x60;        // uint8: the timer bits 1 hours, 2 dates, 4 the view, 8 the cell (slots 19-21); saved (version 6)
constexpr std::size_t kOrientation = 0x61;  // int8 0-3 (slots 9, 10); saved
constexpr std::size_t kChance = 0x62;       // uint8, 100 from the constructor (slots 17, 18); saved
constexpr std::size_t kState = 0x63;        // uint8 (slots 11, 12): 0 shown, 1 hidden for timer props; saved
constexpr std::size_t kUsage = 0x64;        // uint8 (slots 22, 23); saved
// +0x65 to +0x67 are never written: the pool's memory.
} // namespace field

// The date range at +0x54 (evidence: SetSimulatorDateRange 0x005EEFF0,
// TickSimulatorDate 0x005EE230, Write). A date is the game's 16-byte
// cRZDate: vtable, day number, the cIGZDate interface (what is passed
// on), a count.
namespace dates {
constexpr std::size_t kInterval = 0x0;  // uint32 days between two starts (365: the same day every year)
constexpr std::size_t kDuration = 0x4;  // uint32 days, at least 1
constexpr std::size_t kStart = 0x8;     // cRZDate: the next start
constexpr std::size_t kEnd = 0x18;      // cRZDate: the end of the current or last run
constexpr std::size_t kDayNumber = 0x4; // within a date
constexpr std::size_t kInterface = 0x8; // within a date
constexpr std::size_t kSize = 0x28;
} // namespace dates

// Byte offsets of the primary vtable's slots, for Virtual<T>(prop, slot::k...).
namespace slot {
constexpr std::size_t kAsOccupant = 0x0C, kGetPropPlacementProperties = 0x14, kGetPropType = 0x1C;
constexpr std::size_t kGetOrientation = 0x24, kSetState = 0x2C, kGetState = 0x30, kGetTimeOfDayRange = 0x38;
constexpr std::size_t kGetRandomChance = 0x48, kGetPropTimerBitfield = 0x4C, kGetPropTimerState = 0x50;
constexpr std::size_t kSetPropTimerState = 0x54, kGetPropUsage = 0x58;
} // namespace slot

// Byte offsets of the occupant part's slots (the part is the object + 8).
namespace occupant {
constexpr std::size_t kIsInitialized = 0x14, kAsPropertyHolder = 0x18, kGetType = 0x1C, kGetPosition = 0x20;
constexpr std::size_t kSetPosition = 0x24, kGetBoundingBox = 0x28, kGetPlaceableObject = 0x4C, kGetLotTag = 0x6C;
} // namespace occupant

// The table of prop type IDs (0x00B231A4): the ID at an index, 0 past its
// end. For reading only.
inline uint32_t TypeAt(uint32_t index)
{
    const uint32_t* const begin = Global<const uint32_t*>(addr::cSC4PropOccupant_TypeTable);
    const uint32_t* const end = Global<const uint32_t*>(addr::cSC4PropOccupant_TypeTable + 4);
    return begin && begin + index < end ? begin[index] : 0;
}

// Logs a function's first call once ("propocc: first <name>"; defined in
// propocc_sim.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::propocc_sim
