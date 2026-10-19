// Five classes derived from the prop occupant (build 641): the terrain decal
// (cSC4PropOccupantTerrainDecal: a picture laid flat on the ground, with an
// opacity that can fade), the visual effect (cSC4PropOccupantVisualEffect:
// an effect of the effects manager at the prop's place), the automata
// controller prop (cSC4PropOccupantAutomataController: a prop that has the
// automata controller manager run a controller of a named template), the
// burnt building (cSC4PropOccupantBurntBuilding: the ruin a fire leaves,
// drawn by the class itself) and the text decal (cSC4PropOccupantTextDecal:
// the label, a line of text on the ground). The class maps, every vtable
// part and the objects' fields; the code is in propkinds_decal.cpp,
// propkinds_effect.cpp, propkinds_automata.cpp, propkinds_burnt.cpp and
// propkinds_textdecal.cpp.
//
// Both objects start with an interface of their own at +0 and hold the
// whole prop (cSC4PropOccupant, 0x68 bytes, src/core/propocc_sim.h) at +4,
// so the prop's parts lie at +4 (the prop interface), +8 (the message
// target), +0xC (the occupant; the base cSC4Occupant starts here), +0x10
// (cIGZSerializable), +0x14 (the property holder) and +0x28 (the exemplar
// holder). The constructors call the prop's constructor 0x005EE050 on the
// object + 4 and then store seven vtables of their own. Most slots of those
// are the prop's and the occupant base's functions or 8-byte thunks that
// adjust `this` and jump; only what is listed below is the classes' own.
// The class factories and the prop manager (0x004A24D0, 0x004A3040) hand
// out the object + 4, the prop interface.
//
// cSC4PropOccupantTerrainDecal
//   class ID 0x2977AA48 (GetGZCLSID 0x005ED5A0), interface 0xE982078A
//   0xA0 bytes from the pool at 0x00B0A8F0 (16 KB pages at address &
//   0xFFFFC000: free list +0x8, used count +0xC; allocator 0x005ED6E0 and,
//   inline, the factory 0x005EF180)
//   constructor 0x005EE360 (the factory; the prop manager at 0x004A2599 and
//   0x004A30E6; the terrain code at 0x0075EE52 and 0x006CC3C3), destructor
//   body 0x005ED500, deleting destructor 0x005EF1C0 (the holder part's slot
//   22, thunk 0x005ED6A0)
//   +0x00 vtable 0x00AA4790, 7 slots (stack arguments from the callee's ret N):
//      0 2 QueryInterface 0x005ECB10
//      1, 2 AddRef 0x005ECAF0, Release 0x005ECB00 (this + 0xC, to the base's)
//      3 2 SetDecalTexture 0x005ED7E0 (key, size)
//      4 1 SetOpacity 0x005ED840
//      5 4 SetOpacityFade 0x005ECB90 (time, action, from, to)
//      6 1 SetDecalSize 0x005ED870
//      (gzcom-dll's cISC4PropOccupantTerrainDecal.h has the order right; its
//      interface ID is the class ID, the game's is 0xE982078A.)
//   +0x04 the prop interface, vtable 0x00AA4730, 24 slots: all the prop's
//   +0x08 the message target, vtable 0x00AA4720: 3 DoMessage 0x005EE580
//   +0x0C the occupant, vtable 0x00AA4698, 34 slots; the class's own: 3 Init
//      0x005EE410, 4 Shutdown 0x005EE4B0, 9 SetPosition 0x005EE6D0, 30
//      SetAllFlags 0x005ED8D0
//   +0x10 cIGZSerializable, vtable 0x00AA467C: 3 Write 0x005ED710, 4 Read
//      0x005EF4E0, 5 GetGZCLSID 0x005ED5A0
//   +0x14 the property holder, vtable 0x00AA4618, 25 slots: 22 the deleting
//      destructor's thunk 0x005ED6A0, 23 and 24 thunks to Write and Read
//   +0x28 the exemplar holder, vtable 0x00AA4600, 5 slots
//
// cSC4PropOccupantVisualEffect
//   class ID 0x2977AA49 (GetGZCLSID 0x005F02B0), interface 0x09820792
//   0x94 bytes from the pool at 0x00B0A8FC (16 KB pages; allocator
//   0x005ED9A0 and, inline, the factory 0x005F0560)
//   constructor 0x005F0210 (the factory; the prop manager at 0x004A2578 and
//   0x004A30C5; 0x005CBC2C and 0x005CECD3), destructor body 0x005F0340,
//   deleting destructor 0x005F05A0 (thunk 0x005F0310)
//   +0x00 vtable 0x00AA4ED8, 9 slots:
//      0 2 QueryInterface 0x005ECC10
//      1, 2 AddRef 0x005ECAF0, Release 0x005ECB00
//      3 0 AsOccupant 0x005F8400 (this + 0xC; shared with three other
//          classes, the game's)
//      4 0 AsPropOccupant 0x005FD7E0 (this + 4; a shared stub, the game's)
//      5 1 SetEffectType 0x005EF7D0 (an effect ID)
//      6 1 SetEffectType 0x005F0780 (an effect name)
//      7 1 StartEffect 0x005EDA50
//      8 1 StopEffect 0x005EDAA0
//      (gzcom-dll's cISC4PropOccupantVisualEffect.h has the two
//      SetEffectType overloads the other way round.)
//   +0x04 the prop interface, vtable 0x00AA4E78, 24 slots; the class's own:
//      10 SetOrientation 0x005EF280, 11 SetState 0x005EF2C0
//   +0x08 the message target, vtable 0x00AA4E68: 3 the prop's DoMessage
//   +0x0C the occupant, vtable 0x00AA4DE0, 34 slots; the class's own: 3 Init
//      0x005EF5D0, 4 Shutdown 0x005EE790, 9 SetPosition 0x005EF220, 30
//      SetAllFlags 0x005ED9D0
//   +0x10 cIGZSerializable, vtable 0x00AA4DC4: 3 Write 0x005F03B0, 4 Read
//      0x005EF750, 5 GetGZCLSID 0x005F02B0
//   +0x14 the property holder, vtable 0x00AA4D60: 22 the deleting
//      destructor's thunk 0x005F0310, 23 and 24 thunks to Write and Read
//   +0x28 the exemplar holder, vtable 0x00AA4D4C
//   Not virtual: UpdateTransform 0x005EE800 and MakeEffect 0x005EE9C0.
//
// cSC4PropOccupantAutomataController
//   class ID 0x2A4017A0 (GetGZCLSID 0x005EFE40), interface 0x8A401788
//   0x88 bytes from the pool at 0x00B0A914 (allocator 0x005EDD80 and,
//   inline, the factory 0x005F0490)
//   constructor 0x005EFDB0 (the factory; 0x00529636), destructor body
//   0x005EFEF0, deleting destructor 0x005F04D0 (thunk 0x005EFEB0)
//   +0x00 vtable 0x00AA4D28, 7 slots:
//      0 2 QueryInterface 0x005ECC70
//      1, 2 AddRef 0x005ECAF0, Release 0x005ECB00
//      3 0 AsOccupant 0x005F8400, 4 0 AsPropOccupant 0x005FD7E0 (shared
//          stubs, the game's)
//      5 1 SetTemplateName 0x005F06F0 (a C string)
//      6 1 SetEnabled 0x005EDE60
//   +0x04 the prop interface, vtable 0x00AA4CC8, 24 slots; the class's own:
//      11 SetState 0x005F0530
//   +0x08 the message target, vtable 0x00AA4CB8: 3 the prop's DoMessage
//   +0x0C the occupant, vtable 0x00AA4C30, 34 slots; the class's own: 3 Init
//      0x005EFF60, 4 Shutdown 0x005EEDD0
//   +0x10 cIGZSerializable, vtable 0x00AA4C14: 3 Write 0x005EDDB0, 4 Read
//      0x005EFBF0, 5 GetGZCLSID 0x005EFE40
//   +0x14 the property holder, vtable 0x00AA4BB0: 22 the deleting
//      destructor's thunk 0x005EFEB0, 23 and 24 thunks to Write and Read
//   +0x28 the exemplar holder, vtable 0x00AA4B98
//   Not virtual: MakeController 0x005F00B0 (the controller made again).
//
// cSC4PropOccupantBurntBuilding (the prop at +0; see namespace burnt)
//   class ID 0xC9EFB016 (GetGZCLSID 0x005EF950); no interface of its own
//   0xD8 bytes from the pool at 0x00B0A908 (allocator 0x005EDAE0 and,
//   inline, the factory 0x005EFC80)
//   constructor 0x005EF810 (ret 4: 1 for the most derived object; the
//   factory; the lot developer's ExplodeLot at 0x006CC320), destructor body
//   0x005EFB20, deleting destructor 0x005EFCC0 (thunks 0x005EF9A0 and
//   0x005EF9C0)
//   +0x00 the prop interface, vtable 0x00AA4B30, 24 slots; the class's own:
//      0 QueryInterface 0x005ECC60 (a jump to the prop's)
//   +0x04 the message target, vtable 0x00AA4B20: 3 the prop's DoMessage
//   +0x08 the occupant, vtable 0x00AA4A98, 34 slots; the class's own: 3 Init
//      0x005F0600, 4 Shutdown 0x005EFD20, 30 SetAllFlags 0x005EDB50
//   +0x0C cIGZSerializable, vtable 0x00AA4A7C: 3 Write 0x005EDB10, 4 Read
//      0x005EFBA0, 5 GetGZCLSID 0x005EF950
//   +0x10 the property holder, vtable 0x00AA4A18: 22 the deleting
//      destructor's thunk 0x005EF9A0, 23 and 24 thunks to Write and Read
//   +0x24 the exemplar holder, vtable 0x00AA4A00
//   +0x68 the model part, vtable 0x00AA49FC: 0 the deleting destructor's
//      thunk 0x005EF9C0; vbptr at +0x6C (0x00AA4B90)
//   +0xD4 the virtual base, the model interface, vtable 0x00AA49B0 (19
//      thunks, the drawing: the game's)
//   Not virtual: SetOccupant 0x005EEB70 (ExplodeLot: the building's box)
//   and the quad builder 0x005EDC60 (drawing, the game's).
//
// cSC4PropOccupantTextDecal (the label; see namespace textdecal)
//   class ID 0x8B79C707 (GetGZCLSID 0x005F8430), interface 0x2B3B7D86; Init
//   gives the prop type 0xCB79919B
//   0xBC bytes from the pool at 0x00B0A9B8 (allocator 0x005F7D80 and,
//   inline, the factory 0x005F84D0; the pool's own page functions
//   0x005F7680 and 0x005F82A0, in its vtables 0x00AA54A8 and 0x00AA54E4,
//   stay the game's)
//   constructor 0x005F8300 (the factory; the prop manager's TextDecal
//   command at 0x004A364D; the label tool at 0x004BD97F), destructor body
//   0x005F8570, deleting destructor 0x005F8510 (thunk 0x005F8490)
//   +0x00 vtable 0x00AA5680, 11 slots:
//      0 2 QueryInterface 0x005F7650
//      1, 2 AddRef 0x005ECAF0, Release 0x005ECB00
//      3 0 AsOccupant 0x005F8400 (shared, the game's)
//      4 1 SetText 0x005F82E0 (a string)
//      5 1 GetText 0x005F8410 (a string to copy into)
//      6 1 SetSize 0x005F7B10
//      7 1 SetMoving 0x005F8170
//      8 3 SetColour 0x005F7B50
//      9 1 SetDirection 0x005F7D60
//      10 1 SetOpacity 0x005F7C00
//   +0x04 the prop interface, vtable 0x00AA5620, 24 slots: all the prop's
//      (8 SetPropType 0x005EF4C0)
//   +0x08 the message target, vtable 0x00AA5610: 3 DoMessage 0x005F8030
//   +0x0C the occupant, vtable 0x00AA5588, 34 slots; the class's own: 3 Init
//      0x005F7F40, 4 Shutdown 0x005F76C0, 9 SetPosition 0x005F7AA0, 30
//      SetAllFlags 0x005F81F0
//   +0x10 cIGZSerializable, vtable 0x00AA556C: 3 Write 0x005F7DB0, 4 Read
//      0x005F7E50, 5 GetGZCLSID 0x005F8430
//   +0x14 the property holder, vtable 0x00AA5508, 25 slots: 22 the deleting
//      destructor's thunk 0x005F8490, 23 and 24 thunks to Write and Read
//   +0x28 the exemplar holder, vtable 0x00AA54F0, 5 slots
//   Not virtual: RemoveOverlays 0x005F77C0, MakeOverlays 0x005F7810,
//   MoveOverlays 0x005F7A50, PickOverlayManagers 0x005F7C80 and MakeText
//   0x005F8190.
//
// What follows frames and the view, not game days:
//   - base flag 2 of both (the occupant's flags at the object + 0x3B):
//     SetAllFlags (occupant slot 30) acts on a change of it. Who drives it
//     through that slot is not traced; adding a prop to the city alone does
//     not set it. With it come the decal's overlay (+0x9C) and its ID
//     (+0x6C), made the first time the flag is set and kept until Shutdown,
//     and the effect's fresh byte (+0x91).
//   - the decal's fade: the frame service's message 0x86AD10EE carries the
//     frame's time in milliseconds; the opacity being faded (+0x8C), the
//     shown opacity (+0x88), the end of the fade (the overlay removed, or
//     the occupant taken out of the city) follow it.
//   - the burnt building's model in the renderer: SetAllFlags adds it with
//     base flag 2 and takes it off without.
// The rest is plain state: the decal's texture, size, centre, the fade's
// settings; the effect's name, ID, the two property counts, the started
// byte; and the effect object itself exists whenever the prop is
// initialized and has an effect to show.
#pragma once

#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::propkinds {

// The parts both classes share (offsets from the object's start).
namespace layout {
constexpr std::size_t kProp = 0x4;         // the cSC4PropOccupant: add to propocc_sim::field offsets
constexpr std::size_t kMessageTarget = 0x8;
constexpr std::size_t kOccupant = 0xC;     // the cISC4Occupant part; the base cSC4Occupant
constexpr std::size_t kSerializable = 0x10;
constexpr std::size_t kHolder = 0x14;      // the property holder
constexpr std::size_t kExemplarPart = 0x28;
constexpr std::size_t kBaseState = 0x3A;   // base: uint8, bit 0 initialized, bit 1 read from a save
constexpr std::size_t kFlags = 0x3B;       // base: uint8 flags; 1 in the occupant manager, 2 shown (the placeable flag)
constexpr std::size_t kMin = 0x40;         // the prop's box: float[3]
constexpr std::size_t kMax = 0x4C;         // float[3]
constexpr std::size_t kDates = 0x58;       // the prop's date range or null
constexpr std::size_t kOrientation = 0x65; // the prop's int8 orientation
constexpr std::size_t kState = 0x67;       // the prop's uint8 state
} // namespace layout

namespace decal {
constexpr uint32_t kClassId = 0x2977AA48;
constexpr uint32_t kIid = 0xE982078A;
constexpr std::size_t kObjectSize = 0xA0;

// Fields (evidence: the constructor 0x005EE360, Write 0x005ED710 and the
// slots named).
constexpr std::size_t kOverlayId = 0x6C;  // uint32: the decal's ID in the overlay manager, -1 none; saved as it is
constexpr std::size_t kTexture = 0x70;    // uint32[3]: the texture's resource key (SetDecalTexture); saved. Shutdown zeroes the third
constexpr std::size_t kSize = 0x7C;       // float: the decal's side in metres (SetDecalTexture, SetDecalSize); saved
constexpr std::size_t kCentre = 0x80;     // float[2]: x and z (SetPosition); saved
constexpr std::size_t kOpacity = 0x88;    // float, 1 from the constructor (SetOpacity; a fade's steps); saved
constexpr std::size_t kFadeNow = 0x8C;    // float, 1: the fading opacity (SetOpacityFade's third argument at first); saved
constexpr std::size_t kFadeEnd = 0x90;    // float, 0: where the fade ends; saved
constexpr std::size_t kFadeRate = 0x94;   // float, 1; Init of a new decal sets 0: opacity per second, 0 no fade; saved
constexpr std::size_t kFadeAction = 0x98; // int8: at the end 0 nothing, 1 the overlay removed, 2 the occupant removed; saved
constexpr std::size_t kSelfReference = 0x99; // uint8: the object holds a reference on itself (action 2); saved
constexpr std::size_t kFrames = 0x9A;     // uint8: registered with the frame service; saved
constexpr std::size_t kOverlays = 0x9C;   // the overlay manager, referenced, while the decal is shown

// What a fade does when it ends (+0x98).
constexpr int kActionNone = 0, kActionRemoveOverlay = 1, kActionRemoveOccupant = 2;

// Byte offsets of the class's own slots in its primary vtable.
namespace slot {
constexpr std::size_t kSetDecalTexture = 0x0C, kSetOpacity = 0x10, kSetOpacityFade = 0x14, kSetDecalSize = 0x18;
} // namespace slot
} // namespace decal

namespace effect {
constexpr uint32_t kClassId = 0x2977AA49;
constexpr uint32_t kIid = 0x09820792;
constexpr std::size_t kObjectSize = 0x94;

// Fields (evidence: the constructor 0x005F0210, Write 0x005F03B0, Init
// 0x005EF5D0 and MakeEffect 0x005EE9C0).
constexpr std::size_t kEffect = 0x6C;     // the effect (cISC4VisualEffect), referenced, or null
// +0x70 the effect's name: a game string, 0x14 bytes (vtable, begin, end,
// capacity, a zero); saved when no property gave it.
constexpr std::size_t kName = 0x70;
constexpr std::size_t kNameBegin = 0x74;
constexpr std::size_t kNameEnd = 0x78;
constexpr std::size_t kNameCapacity = 0x7C;
constexpr std::size_t kNameZero = 0x80;
constexpr std::size_t kEffectId = 0x84;   // uint32: the effect's ID, 0 none; saved when no property gave it
constexpr std::size_t kNameCount = 0x88;  // uint32: how many of the name properties 0xC98204B8 on the exemplar has (one per state)
constexpr std::size_t kIdCount = 0x8C;    // uint32: the same for the ID properties 0x2A6DEF88 on
constexpr std::size_t kStarted = 0x90;    // uint8: the effect is to run; saved
constexpr std::size_t kFresh = 0x91;      // uint8, 1 from the constructor: not shown yet (the first start is of kind 0); saved

constexpr uint32_t kPropertySize = 0x27812810; // three floats: the box's size
constexpr uint32_t kPropertyName = 0xC98204B8; // a string; + the prop's state, up to 16
constexpr uint32_t kPropertyId = 0x2A6DEF88;   // a uint32; + the prop's state, up to 16
constexpr uint32_t kEffectGroup = 0xAA6DEC65;  // what the effects manager is given with an ID

// Byte offsets of the class's own slots in its primary vtable.
namespace slot {
constexpr std::size_t kSetEffectTypeById = 0x14, kSetEffectTypeByName = 0x18, kStartEffect = 0x1C, kStopEffect = 0x20;
} // namespace slot
} // namespace effect

namespace automata {
constexpr uint32_t kClassId = 0x2A4017A0;
constexpr uint32_t kIid = 0x8A401788;
constexpr std::size_t kObjectSize = 0x88;

// Fields (evidence: the constructor 0x005EFDB0, Write 0x005EDDB0, Init
// 0x005EFF60 and MakeController 0x005F00B0).
constexpr std::size_t kController = 0x6C; // the automata controller (vtable 0x00A95D90), referenced, or null
// +0x70 the controller template's name: a game string, 0x14 bytes (vtable,
// begin, end, capacity, a zero); saved.
constexpr std::size_t kName = 0x70;
constexpr std::size_t kNameBegin = 0x74;
constexpr std::size_t kNameEnd = 0x78;
constexpr std::size_t kNameCapacity = 0x7C;
constexpr std::size_t kNameZero = 0x80;
constexpr std::size_t kEnabled = 0x84; // uint8: the controller is to be active; saved

constexpr uint32_t kPropertySize = 0x27812810;     // three floats: the box's size
constexpr uint32_t kPropertyTemplate = 0xC98204B8; // a string: the template's name
constexpr uint32_t kPropertyExemplarName = 0x20;   // the exemplar's name, when the one above is missing

// Byte offsets of the class's own slots in its primary vtable.
namespace slot {
constexpr std::size_t kSetTemplateName = 0x14, kSetEnabled = 0x18;
} // namespace slot
} // namespace automata

// The burnt building holds the prop at +0, not at +4: its parts lie four
// bytes before the other classes' (+0 the prop interface, +4 the message
// target, +8 the occupant, +0xC the serializable, +0x10 the property
// holder, +0x24 the exemplar holder; the prop's fields at their own
// offsets). After the prop comes a model part with a virtual base.
namespace burnt {
constexpr uint32_t kClassId = 0xC9EFB016;
constexpr std::size_t kObjectSize = 0xD8;

constexpr std::size_t kMessageTarget = 0x4;
constexpr std::size_t kOccupant = 0x8;
constexpr std::size_t kSerializable = 0xC;
constexpr std::size_t kHolder = 0x10;
constexpr std::size_t kExemplarPart = 0x24;
constexpr std::size_t kBaseState = 0x36; // base: uint8, bit 0 initialized, bit 1 read from a save
constexpr std::size_t kFlags = 0x37;     // base: uint8 flags; 2 shown
constexpr std::size_t kMin = 0x3C;       // the prop's box: float[3]
constexpr std::size_t kMax = 0x48;       // float[3]

// Fields (evidence: the constructor 0x005EF810, Write 0x005EDB10, Init
// 0x005F0600, Shutdown 0x005EFD20 and the quad builder 0x005EDC60).
constexpr std::size_t kModelPart = 0x68;   // the model part (SC4Model::ConstructModelPart 0x00801CF0), vtable 0x00AA49FC
constexpr std::size_t kVbptr = 0x6C;       // its virtual base table, 0x00AA4B90 {-4, 0x68}
constexpr std::size_t kVtordisp = 0xD0;    // int32: 0 (the virtual base's displacement)
constexpr std::size_t kVirtualBase = 0xD4; // the model interface, vtable 0x00AA49B0 (19 slots; 0x00AA1F38 before)
constexpr std::size_t kTextures = 0x94;    // a vector of five texture bindings (begin, end, capacity)
constexpr std::size_t kBox = 0xA0;         // float[6]: the burnt occupant's box (min, max); saved
constexpr std::size_t kQuad = 0xB8;        // the quad (0x50 bytes) the quad builder makes, or null
constexpr std::size_t kRenderState = 0xBC; // the render state (SC4RenderState::Init 0x007D2B50), 8 bytes
constexpr std::size_t kScaled = 0xC4;      // float[3]: the box's low corner, scaled (the quad builder)

constexpr uint32_t kTextureCount = 5;
// The texture every binding is asked for (Init).
constexpr uint32_t kTexture[3] = {0x7AB50E44, 0x1ABE787D, 0xA9F0E710};
} // namespace burnt

// The text decal (the label) holds the prop at +4 like the first three.
namespace textdecal {
constexpr uint32_t kClassId = 0x8B79C707;
constexpr uint32_t kIid = 0x2B3B7D86;
constexpr uint32_t kPropType = 0xCB79919B; // what Init gives the prop's SetPropType
constexpr std::size_t kObjectSize = 0xBC;

// Fields (evidence: the constructor 0x005F8300, Write 0x005F7DB0 and
// MakeOverlays 0x005F7810).
// +0x6C the text: a game string, 0x14 bytes (vtable, begin, end, capacity, a
// zero); saved.
constexpr std::size_t kText = 0x6C;
constexpr std::size_t kTextBegin = 0x70;
constexpr std::size_t kTextEnd = 0x74;
constexpr std::size_t kTextCapacity = 0x78;
constexpr std::size_t kTextZero = 0x7C;
constexpr std::size_t kSize = 0x80;       // float, 1: the letters' size (SetSize, 0 to 8 exclusive); saved
constexpr std::size_t kOverlayId = 0x84;  // uint32: the text's ID in the first overlay manager, -1 none
constexpr std::size_t kOverlayId2 = 0x88; // uint32: the same in the second
constexpr std::size_t kMoving = 0x8C;     // uint8: the label is being moved (overlay managers 2 and 3, not 0 and 1)
constexpr std::size_t kWidth = 0x90;      // float, 100: the text's width in metres (MakeOverlays); saved
constexpr std::size_t kPosition = 0x94;   // float[2]: x and z (SetPosition); saved (twice)
constexpr std::size_t kColour = 0x9C;     // float[3], 255 each (SetColour); saved
constexpr std::size_t kOpacity = 0xA8;    // float, 1 (SetOpacity); saved
constexpr std::size_t kDirection = 0xAC;  // int32, 1: which way the text runs (SetDirection); saved as a byte
constexpr std::size_t kTexture = 0xB0;    // the text's texture (the service 0x00B43CE4 makes it), referenced, or null
constexpr std::size_t kOverlays = 0xB4;   // the first overlay manager (the terrain view's 0, or 2 while moving), referenced
constexpr std::size_t kOverlays2 = 0xB8;  // the second (1, or 3 while moving), referenced

// Byte offsets of the class's own slots in its primary vtable (the label
// tool's and the prop manager's TextDecal command's calls).
namespace slot {
constexpr std::size_t kSetText = 0x10, kGetText = 0x14, kSetSize = 0x18, kSetMoving = 0x1C, kSetColour = 0x20;
constexpr std::size_t kSetDirection = 0x24, kSetOpacity = 0x28;
} // namespace slot
} // namespace textdecal

template <typename R, typename... Args>
inline R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
inline R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

// Logs a function's first call once ("propkinds: first <name>").
inline void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("propkinds: first %s", name);
}

struct Detour {
    std::uintptr_t address;
    void* ours;
};

// A detour at each address (entry bytes checked by hook::Install). The
// originals are not called again.
template <std::size_t N>
inline bool InstallDetours(const Detour (&detours)[N], const char* what)
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : detours)
        if (!addr_check::Find(detour.address)) {
            log::Write("propkinds %s: no address entry for 0x%08X", what, static_cast<unsigned>(detour.address));
            return false;
        }
    for (const Detour& detour : detours) {
        void* original = nullptr;
        if (!hook::Install(*addr_check::Find(detour.address), detour.ours, &original))
            return false;
    }
    log::Write("propkinds %s: %u detours", what, static_cast<unsigned>(N));
    return true;
}

// An item of a pool of prop objects (0x005ED6E0, 0x005ED9A0, and inline in
// the factories): the current page's free head, the pool refilled when the
// page has none (0x0090CF9A returns the new head); the page is read again
// after.
inline void* PoolAllocate(std::uintptr_t poolAddress)
{
    char* const pool = reinterpret_cast<char*>(poolAddress);
    void* item = At<void*>(At<char*>(pool, 0x4), 0x8);
    if (!item)
        item = This<void*>(addr::SC4EdgePool_Grow, pool);
    At<void*>(At<char*>(pool, 0x4), 0x8) = *static_cast<void**>(item);
    ++At<uint32_t>(At<char*>(pool, 0x4), 0xC);
    return item;
}

// An object back to its pool (the tail of the deleting destructors
// 0x005EF1C0 and 0x005F05A0): the page header is at the address with its
// low 14 bits clear; a page left empty goes back to the pool, and a page
// whose count comes down to `reopenAt` (0x4000004C for the decal's pool,
// 0x40000052 for the effect's: a full page less one) is opened again.
inline void PoolFree(std::uintptr_t poolAddress, char* self, uint32_t reopenAt)
{
    char* const page = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(self) & 0xFFFFC000u);
    At<void*>(self, 0x0) = At<void*>(page, 0x8);
    const uint32_t used = At<uint32_t>(page, 0xC) - 1;
    At<void*>(page, 0x8) = self;
    At<uint32_t>(page, 0xC) = used;
    if (used == 0)
        This<void>(addr::SC4Pool_ReleaseBlock, reinterpret_cast<void*>(poolAddress), static_cast<void*>(page));
    else if (used == reopenAt)
        This<void>(addr::SC4Pool_ReopenBlock, reinterpret_cast<void*>(poolAddress), static_cast<void*>(page));
}

} // namespace opensc4::propkinds
