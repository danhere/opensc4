// Our cSC4FireOccupant (build 641): the class map, every vtable slot by
// slot and the object's fields. A fire occupant is one fire burning on a
// building, a lot object or a tree (its target, another occupant): its
// heat, its stage, the number of stages the target has, how many are
// hosing it, the time it goes out and its family (the fires one start
// made). The fire disaster's instance, already ours (src/core/
// disaster_fire.cpp, docs/disasters.md), makes them, steps their heat and
// stage and removes them. The fire has no place of its own: its position
// and box are the target's. What is seen of it is a list of flames, each a
// visual effect at a spot on the target's model. The rest (the occupant
// manager's cells, removal counts, flags, the property holder) is the
// occupant base's, cSC4Occupant (src/core/occbase_sim.h), at the object + 4.
//
// Class map
//   class ID 0x29D25F1D (GetGZCLSID 0x0046FDC0), occupant type 0x69D25C5F,
//   cISC4FireOccupant IID 0x69D25C60 (the class ID itself is not answered
//   by QueryInterface)
//   0x6C bytes from the game's operator new (0x005E55E0): no pool
//   constructor 0x004711C0 (cSC4FireOccupant::Construct in
//   data/addresses/disasters.toml), called by the factory 0x004715E0 (a
//   jump; the class registration's pointer, pushed at 0x004606E9: a save's
//   reader makes every fire with it; it returns the object itself, not the
//   occupant part) and by the fire instance's start at 0x0057A993 (after
//   operator new at 0x0057A985; our src/core/disaster_fire.cpp calls the
//   same two addresses): the only two sites
//   destructor 0x00471290 (only from the deleting destructor 0x00471600,
//   which is reached only through the holder part's slot 22, thunk
//   0x00471260; the base's Release 0x005E8DF0 calls that at the last
//   reference)
//   code: 0x0046FDC0 to 0x0046FE57, then, between helpers that are not the
//   class's, 0x004700A0 to 0x004702A1, 0x00470BC0 to 0x00471283 (less
//   the containers' 0x00471070 and 0x004710A0), 0x00471290, 0x004715E0,
//   0x00471600, 0x00471900 to 0x00471B93, 0x00471CF0 to 0x004728F5. The
//   flora occupant begins at 0x00472900. In the range and not the class's:
//     0x0046FD50 (slot 3 of the vtable 0x00A88274, the class before),
//     0x0046FD70 (a filter object's test, in the vtable at 0x00A949B8:
//       true for an occupant of type 0x69D25C5F whose fire interface has a
//       target; left the game's, it is that class's),
//     0x0046FFC0 (a box's middle; callers 0x0059AF10, 0x005B2590,
//       0x007D883D, 0x007E83A5, 0x007E83D7), 0x00470010 and 0x00470B50 (a
//       date from parts; 43 callers all over the exe),
//     the containers' code the linker put here (0x0046FE60 to 0x0046FFB8,
//       0x004702B0 to 0x00470AD5, 0x00471070, 0x004710A0, 0x004712E0 to
//       0x004715D2, 0x00471620 to 0x004718F4, 0x00471BA0 to 0x00471CE5:
//       vectors of 0x14-byte spots, 0x28-byte triangles and 0x38-byte
//       transforms, used by the flame spot search only).
//   No derived class: the five vtables are stored by the constructor and
//   the destructor only, and no other vtable holds one of the class's own
//   functions.
//
// +0x00 primary cISC4FireOccupant, vtable 0x00A88460, 15 slots (byte
// offset, stack arguments from the callee's ret N, function, owner; the
// text "fire_occupant" follows at 0x00A8849C):
//    0 +00 2 QueryInterface 0x0046FE30              class
//    1 +04 0 AddRef 0x005E28F0 (to the base's with this + 4)   base
//    2 +08 0 Release 0x005E2900                     base
//    3 +0C 0 AsOccupant 0x005FD7E0 (this + 4)       shared stub
//    4 +10 1 SetTarget 0x00471900                   class
//    5 +14 0 GetTarget 0x006AAEB0 (+0x54)           shared stub
//    6 +18 0 GetStage 0x00820250 (+0x44)            shared stub
//    7 +1C 0 GetStageCount 0x00698CF0 (+0x48)       shared stub
//    8 +20 1 SetStage 0x004728B0                    class
//    9 +24 0 GetHeat 0x0068D1B0 (+0x40)             shared stub
//   10 +28 1 SetHeat 0x007B62D0                     shared stub
//   11 +2C 0 GetHosed 0x0068D1C0 (+0x4C)            shared stub
//   12 +30 1 SetHosed 0x0046FDD0 (adds its argument) class
//   13 +34 0 GetFamily 0x0068D1D0 (+0x68)           shared stub
//   14 +38 1 SetFamily 0x006D6E00                   shared stub
// +0x04 cISC4Occupant (the base cSC4Occupant starts here; its constructor
//   0x005E9220, destructor 0x005E8C40), vtable 0x00A883D8, 34 slots (the
//   primary vtable follows it):
//    0 +00 2 QueryInterface thunk 0x00471230 (this - 4)   class
//    1, 2    AddRef 0x005E29B0, Release 0x005E29C0        base
//    3 +0C 0 Init 0x00471170                              class
//    4 +10 0 Shutdown 0x00470DC0                          class
//    5 +14 0 IsInitialized 0x00458A40                     base
//    6 +18 0 AsPropertyHolder 0x006C2040 (this + 8)       shared stub
//    7 +1C 0 GetType 0x006BDD80 (+0x24 of the part)       shared stub
//    8 +20 1 GetPosition 0x00470210                       class
//    9 +24 1 SetPosition 0x00648F10 (nothing)             shared stub
//   10 +28 2 GetBoundingBox 0x00470230                    class
//   11-26    the base's (0x005E8E20 ... 0x005E8C20; 16 GetHighlight
//            0x00458A50, 19 GetPlaceableObject 0x006AAC50, 21
//            SetPlaceableObject 0x0078DAF0)
//   27 +6C 1 0x005DEB40 (false)                           shared stub
//   28 +70 1 0x00648F10 (nothing)                         shared stub
//   29-33    SetFlag 0x005E8B70 ... GetFlags 0x005E8BE0   base
// +0x08 cIGZSerializable, vtable 0x00A883BC, 6 slots: 0 QueryInterface
//   thunk 0x00471240 (class), 1 and 2 AddRef 0x005E29E0 and Release
//   0x005E29F0 (base), 3 Write 0x00470130 (1; class), 4 Read 0x00471A70
//   (1; class), 5 GetGZCLSID 0x0046FDC0 (class). Write and Read get `this`
//   = object + 8.
// +0x0C the property holder (cISCPropertyHolder; 0x1C bytes with its
//   exemplar holder part), vtable 0x00A88358, 25 slots (0x00A883BC follows
//   it): 0 QueryInterface thunk 0x00471250 (class), 1 and 2 0x005E2A10 and
//   0x005E2A20 (base), 3-21 the shared holder code (0x005FEC20 ...), 22
//   the deleting destructor thunk 0x00471260 (1 argument; class), 23 the
//   Write thunk 0x00471280 and 24 the Read thunk 0x00471B90 (both this -
//   4; class).
// +0x20 cISCExemplarPropertyHolder, vtable 0x00A88344, 5 slots: 0
//   QueryInterface thunk 0x00471270 (class), 1 and 2 0x005E06A0 and
//   0x005E06B0 (base), 3 SetPropertyExemplar 0x00458E60 (1; shared stub),
//   4 the getter 0x0060CE80 (shared stub).
// The constructor first stores 0x00A88308, the abstract interface's vtable
// (15 pure-call entries), which no call can meet.
//
// Not a slot, called directly by the fire instance: 0x0046FE00 (the end
// time; cSC4FireOccupant::CheckBurnOut in disasters.toml; ours, a detour),
// 0x00470250 (shows or hides the flames' effects with the view;
// cSC4FireOccupant::SetView) and 0x00472790 (puts the flames on the model's
// spots again; cSC4FireOccupant::Refresh): the last two are drawing and
// stay the game's.
//
// A flame (0x50 bytes, operator new; made by the placing 0x00472380 and by
// Read, deleted by Shutdown):
//   +0x00 uint8 started (its effect asked for)
//   +0x01 uint8 shown (1 from the constructor; SetView and the placing
//         clear or set it with the view: the view's)
//   +0x02 uint8 the kind (which of the two effect name tables, 0x00B07F90
//         or 0x00B07FA4, by stage); saved
//   +0x04 uint32 a quarter turn (times 90 degrees in the effect's
//         transform); saved
//   +0x08 float[3] the spot; saved
//   +0x14 a 0x38-byte transform (serial, flags, matrix +0x18, translation
//         +0x3C, scale +0x48): identity from the constructor and never
//         changed but by Read; saved
//   +0x4C the visual effect (referenced), null until started
//   +0x03, +0x16, +0x17 are never written (the heap's memory).
//
// Drawing, effect and model code of the class, left the game's and called
// at its address where the rest needs it:
//   0x00470250 SetView, 0x00470CA0 (a flame's transform to its effect),
//   0x004710D0 (a flame's effect for a stage, made by the effects manager
//   0x00B43D1C and started), 0x00471CF0 with 0x00470EC0 and the containers
//   (walks the triangles of the target's model for flame spots, with a
//   generator of its own seeded -1), 0x00472380 (the placing: the flames
//   made at the model's spots, or without a model at draws from the class's
//   generator 0x00B21648 inside the box; shown or not by the view
//   0x00B43DC4's slot 22), 0x00472790 Refresh.
//
// What follows frames or the view, and what does not:
//   by the simulation: the target, the heat, the stage, the stage count,
//   the hosed count, the end time (a time of the fire instance's
//   stopwatch), the family; the two flame numbers (+0x38, +0x3C) follow
//   from the target's box alone;
//   by the view: the flames (how many, which kind, where: the placing
//   takes the model's spots when the target has a model object at that
//   moment, +0x64 then set, or draws places itself and then makes one
//   flame more), each flame's shown byte and effect.
// Unset by the constructor (the heap's memory until written): +0x65 to
// +0x67 (never written) and the family at +0x68, which SetFamily sets when
// the fire instance makes a fire but nothing sets after a save is read (it
// is not saved).
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::fireocc_sim {

constexpr uint32_t kClassId = 0x29D25F1D;
constexpr uint32_t kOccupantType = 0x69D25C5F;
constexpr uint32_t kIidFire = 0x69D25C60;
constexpr std::size_t kObjectSize = 0x6C;
constexpr std::size_t kFlameSize = 0x50;

// Fields (offsets from the object's start). +0x04 to +0x37 are the occupant
// base's (evidence: its constructor 0x005E9220, Write 0x005E8CD0 and the
// slots named, which get the object + 4), +0x38 on this class's (evidence:
// the constructor 0x004711C0, Write 0x00470130 and the functions named).
namespace field {
constexpr std::size_t kOccupant = 0x4;      // base: the cISC4Occupant part
constexpr std::size_t kSerializable = 0x8;  // base: the cIGZSerializable part
constexpr std::size_t kHolder = 0xC;        // base: the property holder (vtable, count +0x10, properties +0x14 to +0x1C)
constexpr std::size_t kRefCount = 0x10;     // base: uint32
constexpr std::size_t kProperties = 0x14;   // base: the holder's own properties: begin, end (+0x18), capacity; 8-byte entries {ID, property}
constexpr std::size_t kExemplarPart = 0x20; // base: the cISCExemplarPropertyHolder part
constexpr std::size_t kExemplar = 0x24;     // base: the exemplar, referenced (a fire has none)
constexpr std::size_t kType = 0x28;         // base: uint32 occupant type; the constructor sets 0x69D25C5F; saved
constexpr std::size_t kManagerBox = 0x2C;   // base: uint8[4], the occupant manager's cells (0xFF: none); saved
constexpr std::size_t kRemoval = 0x30;      // base: uint8[2] removal counts; saved
constexpr std::size_t kState = 0x32;        // base: uint8: bit 0 initialized, bit 1 set by Read, the high four bits the highlight
constexpr std::size_t kFlags = 0x33;        // base: uint8 flags; saved without 0x02 and 0x20
constexpr std::size_t kPlaceable = 0x34;    // base: the placeable object, referenced
constexpr std::size_t kFlamesLow = 0x38;    // uint32: SetTarget, the first curve (0x00B21638) at the box's width times depth, rounded; the placing gives that many flames the kind 0; zero from the constructor; not saved
constexpr std::size_t kFlamesHigh = 0x3C;   // uint32: SetTarget, the second curve (0x00B2163C) at twice the box's height times (width + depth), rounded; zero from the constructor; not saved
constexpr std::size_t kHeat = 0x40;         // uint32 (slots 9, 10); zero from the constructor; saved
constexpr std::size_t kStage = 0x44;        // int32 (slots 6, 8); -1 from the constructor; saved
constexpr std::size_t kStageCount = 0x48;   // uint32 (slot 7): the target's property 0x49BEDA31, 2 without it (SetTarget); zero from the constructor; saved
constexpr std::size_t kHosed = 0x4C;        // uint32 (slots 11, 12); zero from the constructor; not saved
constexpr std::size_t kEndTime = 0x50;      // uint32 (0x0046FE00; the fire instance reads it): the stopwatch time the fire goes out, 0 for none; zero from the constructor; not saved
constexpr std::size_t kTarget = 0x54;       // the burning occupant (its cISC4Occupant), referenced (slots 4, 5); saved as a reference
constexpr std::size_t kFlames = 0x58;       // vector of flame pointers: begin, end (+0x5C), capacity (+0x60); saved (count, then each flame)
constexpr std::size_t kOnModel = 0x64;      // uint8: the placing took the model's spots (Refresh then moves the flames to new ones); zero from the constructor; not saved
// +0x65 to +0x67 are never written: the heap's memory.
constexpr std::size_t kFamily = 0x68;       // uint32 (slots 13, 14): the group of fires one start made; NOT set by the constructor, not saved
} // namespace field

// A flame's fields.
namespace flame {
constexpr std::size_t kStarted = 0x0, kShown = 0x1, kKind = 0x2, kTurn = 0x4, kSpot = 0x8;
constexpr std::size_t kTransform = 0x14, kTransformFlags = 0x15, kMatrix = 0x18, kTranslation = 0x3C;
constexpr std::size_t kScale = 0x48, kEffect = 0x4C;
} // namespace flame

// Byte offsets of the primary vtable's slots, for Virtual<T>(fire, slot::k...).
namespace slot {
constexpr std::size_t kAsOccupant = 0x0C, kSetTarget = 0x10, kGetTarget = 0x14, kGetStage = 0x18;
constexpr std::size_t kGetStageCount = 0x1C, kSetStage = 0x20, kGetHeat = 0x24, kSetHeat = 0x28;
constexpr std::size_t kGetHosed = 0x2C, kSetHosed = 0x30, kGetFamily = 0x34, kSetFamily = 0x38;
} // namespace slot

// Byte offsets of the occupant part's slots (the part is the object + 4;
// the target's too).
namespace occupant {
constexpr std::size_t kIsInitialized = 0x14, kAsPropertyHolder = 0x18, kGetType = 0x1C, kGetPosition = 0x20;
constexpr std::size_t kGetBoundingBox = 0x28, kSetRemovalFlags = 0x30, kUnsetRemovalFlags = 0x34;
constexpr std::size_t kAddOccupantGroup = 0x5C, kIsFlagSet = 0x80, kGetFlags = 0x84;
} // namespace occupant

// Logs a function's first call once ("fireocc: first <name>"; defined in
// fireocc_sim.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::fireocc_sim
