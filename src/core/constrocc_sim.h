// Our cSC4ConstructionOccupant (build 641): the class map, every vtable slot
// by slot and the object's fields. A construction occupant is the building
// site shown while a lot's building goes up: scaffolding, a crane, props
// and effects, and the schedule the construction follows. The lot
// developer's StartConstruction makes one for a lot it keeps under
// construction (src/core/lot_construct.cpp, docs/lot.md), gives it the lot
// (slot 4) and inserts it in the occupant manager. On each simulator step
// (message 0x66956814) the occupant looks at the day and moves its stage
// machine on; at the end it completes the building, sets the lot to state
// 3, leaves the occupant manager and shuts down.
//
// Nearly all of the class's code (0x005CA7A0 to 0x005D1511, about 27 KB) is
// drawing and animation: the scaffolding's geometry, its textures, the
// props and effects, the renderer's interface. That is another thread's and
// stays the game's. Ours are identity, the box, the save record, the object
// (constructor, pool, factory, destructor) and the two functions of the
// simulation: SetLot's schedule and the daily step.
//
// Class map
//   class ID 0xA97F909E (GetGZCLSID 0x005D04A0), occupant type 0x096E763C,
//   interface 0x496E7636 (the class ID itself is not answered by
//   QueryInterface)
//   0x1A0 bytes, from the pool at 0x00B0A808 (32 KB pages at address &
//   0xFFFF8000: free list +0x8, used count +0xC, 78 items; Allocate
//   0x005CA8A0 and, inline, the factory 0x005D0BC0; the pool's own vtables
//   0x00AA1F98 and 0x00AA20A0 hold its page functions 0x005CD210, an item
//   back to its page, and 0x005CA860, a new page, both left the game's)
//   constructor 0x005D00F0 (this, mostDerived), called by the factory at
//   0x005D0BEE (the class registration's pointer, pushed at 0x004607E5: the
//   COM makes one for StartConstruction and a save's reader makes every
//   site with it; it returns the object itself) and by StartConstruction
//   at 0x006CC805 when the COM gave none (after Allocate at 0x006CC7F5):
//   the only two sites, both with mostDerived 1
//   destructor 0x005D0C60, deleting destructor 0x005D0C00, reached only
//   through the holder part's slot 22 (thunk 0x005D04F0), which the base's
//   Release calls at the last reference, and the model part's slot 0
//   (thunk 0x005D0510)
//   code 0x005CA7A0 to 0x005D1511. 0x005CA770 before it is another class's;
//   0x005D0010 (a vector's insert), 0x005CE080 and 0x005D1290 (a vector's
//   resize; called from 0x004A196C, 0x004A1AC2, 0x00734019, 0x00734402)
//   are container code in the range. No derived class: each of the class's
//   functions is in one vtable only, and the vtables are stored by the
//   constructor only (the destructor stores none).
//
// +0x00 primary, vtable 0x00AA2250, 5 slots (byte offset, stack arguments
// from the callee's ret N, function, owner):
//    0 +00 2 QueryInterface 0x005CA7A0             class
//    1 +04 0 AddRef 0x006491C0 (to the base's with this + 8)   base (shared with the prop occupants)
//    2 +08 0 Release 0x006491D0                    base
//    3 +0C 0 AsOccupant 0x006C2040 (this + 8)      shared stub
//    4 +10 1 SetLot 0x005D0890                     class
// +0x04 message target, vtable 0x00AA2240, 4 slots: 0 QueryInterface thunk
//   0x005D04B0 (class), 1 and 2 0x0064DD70 and 0x0064DD80 (base), 3
//   DoMessage 0x005D1090 (1; class, left the game's: view and frame
//   messages; it calls the step 0x005D0680 on 0x66956814).
// +0x08 cISC4Occupant (the base cSC4Occupant starts here; its constructor
//   0x005E9220, destructor 0x005E8C40), vtable 0x00AA21B8, 34 slots:
//    0 +00 2 QueryInterface thunk 0x005D04C0 (this - 8)   class
//    1, 2    AddRef 0x0064DD90, Release 0x0064DDA0        base
//    3 +0C 0 Init 0x005D1300                              class (left the game's: textures, the render state)
//    4 +10 0 Shutdown 0x005D0CE0                          class (left the game's: frees the geometry and textures)
//    5 +14 0 IsInitialized 0x00458A40                     base
//    6 +18 0 AsPropertyHolder 0x006C2040 (this + 8)       shared stub
//    7 +1C 0 GetType 0x006BDD80                           shared stub
//    8 +20 1 GetPosition 0x005CA7D0                       class
//    9 +24 1 SetPosition 0x00648F10 (nothing)             shared stub
//   10 +28 2 GetBoundingBox 0x005CA820                    class
//   11-26    the base's (0x005E8E20 ... 0x005E8C20; 16 GetHighlight
//            0x00458A50, 19 GetPlaceableObject 0x006AAC50, 21
//            SetPlaceableObject 0x0078DAF0)
//   27 +6C 1 0x005DEB40 (false)                           shared stub
//   28 +70 1 0x00648F10 (nothing)                         shared stub
//   29-33    SetFlag 0x005E8B70 ... GetFlags 0x005E8BE0   base
// +0x0C cIGZSerializable, vtable 0x00AA219C, 6 slots: 0 QueryInterface
//   thunk 0x005D04D0 (class), 1 and 2 0x0064DDB0 and 0x0064DDC0 (base), 3
//   Write 0x005CD250 (1; class), 4 Read 0x005CE770 (1; class), 5 GetGZCLSID
//   0x005D04A0 (class). Write and Read get `this` = object + 0xC.
// +0x10 the property holder (0x1C bytes with its exemplar holder part),
//   vtable 0x00AA2138, 25 slots: 0 QueryInterface thunk 0x005D04E0 (class),
//   1 and 2 0x0064DDD0 and 0x0064DDE0 (base), 3-21 the shared holder code
//   (0x005FEC20 ...), 22 the deleting destructor thunk 0x005D04F0 (1
//   argument; class), 23 the Write thunk 0x005D0660 and 24 the Read thunk
//   0x005D0670 (both this - 4; class).
// +0x24 cISCExemplarPropertyHolder, vtable 0x00AA2120, 5 slots: 0
//   QueryInterface thunk 0x005D0500 (class), 1 and 2 0x0064DE00 and
//   0x0064DE10 (base), 3 SetPropertyExemplar 0x00458E60 and 4 the getter
//   0x0060CE80 (shared stubs).
// +0x3C the model part (constructor 0x00801CF0, destructor 0x00801810; the
//   drawn object's data, +0x40 its virtual base table 0x00AA2264 {-4,
//   0x15C}), vtable 0x00AA211C, 1 slot: the deleting destructor thunk
//   0x005D0510 (class).
// +0x19C the model part's virtual base (what the renderer holds; the
//   displacement word at +0x198 is zero), vtable 0x00AA20D0, 19 slots, all
//   thunks that subtract the displacement at this - 4:
//    0 QueryInterface 0x005D0520 (class), 1 and 2 0x005D0530 and 0x005D0540
//    (to the prop occupants' AddRef and Release), 3 0x005D0550 (the
//    occupant part or null; class), 4 0x005DECD0 (shared), 5, 6, 11, 13-16,
//    18 the model part's (0x00801890, 0x008018A0, 0x005DEB40, 0x008018E0,
//    0x008018F0, 0x00801E10, 0x00801E50, 0x004E13E0), and the class's
//    drawing functions: 7 0x005CAA20 (the transformed box), 8 0x005CAC00
//    (the box), 9 0x005CAC40 and 10 0x005CACD0 (the transform read and
//    set), 12 0x005CC410 (Draw), 17 0x005CC3E0. All left the game's but
//    slot 0.
// The constructor first stores the abstract interfaces' vtables 0x00AA1F84
// and 0x00A81174, which no call can meet.
//
// Drawing, animation and model code of the class, left the game's: Init
// 0x005D1300, Shutdown 0x005D0CE0, DoMessage 0x005D1090, 0x005CA8D0,
// 0x005CA960, 0x005CAA20, 0x005CAC00, 0x005CAC40, 0x005CACD0, 0x005CAD60,
// 0x005CB7C0, 0x005CB9F0, 0x005CBC10, 0x005CBD20, 0x005CBD90, 0x005CBE10,
// 0x005CC020, 0x005CC3E0, 0x005CC410, 0x005CC820, 0x005CCD70, 0x005CCF20,
// 0x005CD570, 0x005CD710, 0x005CDDA0, 0x005CE110 (the frame's animation),
// 0x005CE360, 0x005CE3F0, 0x005CE4D0, 0x005CE580, 0x005CEC20, 0x005CED80
// (the shared geometry tables), 0x005CF380, 0x005CF610, 0x005CF790,
// 0x005CFA60, 0x005CFC40, 0x005CFE20, 0x005D0550, 0x005D0560.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::constrocc_sim {

constexpr uint32_t kClassId = 0xA97F909E;
constexpr uint32_t kOccupantType = 0x096E763C;
constexpr uint32_t kIidConstruction = 0x496E7636;
constexpr std::size_t kObjectSize = 0x1A0;

// Fields (offsets from the object's start). +0x08 to +0x3B are the occupant
// base's (evidence: its constructor 0x005E9220, Write 0x005E8CD0 and the
// slots named, which get the object + 8), +0x3C to +0x67 and +0x198 on the
// model part's (0x00801CF0), +0x68 on this class's (evidence: the
// constructor 0x005D00F0, Write 0x005CD250, Read 0x005CE770, SetLot
// 0x005D0890, the step 0x005D0680). "Saved" is in the class's own record;
// "frames" marks what the frame's animation (0x005CE110, message
// 0x86AD10EE) or the view changes.
namespace field {
constexpr std::size_t kMessageTarget = 0x4;   // the message target part
constexpr std::size_t kOccupant = 0x8;        // base: the cISC4Occupant part
constexpr std::size_t kSerializable = 0xC;    // base: the cIGZSerializable part
constexpr std::size_t kHolder = 0x10;         // base: the property holder (vtable, count +0x14, properties +0x18 to +0x20)
constexpr std::size_t kRefCount = 0x14;       // base: uint32
constexpr std::size_t kProperties = 0x18;     // base: the holder's own properties: begin, end (+0x1C), capacity; 8-byte entries {ID, property}
constexpr std::size_t kExemplarPart = 0x24;   // base: the cISCExemplarPropertyHolder part
constexpr std::size_t kExemplar = 0x28;       // base: the exemplar, referenced; its resource key is saved
constexpr std::size_t kType = 0x2C;           // base: uint32 occupant type; the constructor sets 0x096E763C; saved
constexpr std::size_t kManagerBox = 0x30;     // base: uint8[4], the occupant manager's cells (0xFF: none); saved
constexpr std::size_t kRemoval = 0x34;        // base: uint8[2] removal counts; saved
constexpr std::size_t kState = 0x36;          // base: uint8: bit 0 initialized, bit 1 set by Read, the high four bits the highlight
constexpr std::size_t kFlags = 0x37;          // base: uint8 flags; 0x02 here means the model is in the renderer (0x005CA960; the view's)
constexpr std::size_t kPlaceable = 0x38;      // base: the placeable object, referenced
constexpr std::size_t kModelPart = 0x3C;      // the model part (vtable; +0x40 the virtual base table; +0x50 a dword the constructor sets to 5)
constexpr std::size_t kVbtable = 0x40;
constexpr std::size_t kModelKind = 0x50;
constexpr std::size_t kLot = 0x68;            // the lot (cISC4Lot), referenced; SetLot; saved as a reference
constexpr std::size_t kBuilding = 0x6C;       // the lot's building occupant, referenced; SetLot; saved as a reference
constexpr std::size_t kTransform = 0x70;      // 0x38 bytes: uint8 serial, uint8 flags (+0x71), float[9] matrix (+0x74), float[3] translation (+0x98), float scale (+0xA4); the renderer's slots 9 and 10; saved
constexpr std::size_t kBox = 0xA8;            // float[6]: low, high; the occupant's box (slots 8 and 10); the constructor's empty box is low 1, high 0; saved
constexpr std::size_t kInnerBox = 0xC0;       // float[6]: a second box; its high y (+0xD0) is the scaffolding's height now, set by 0x005CC020 from the frame's animation; saved; +0xD0 frames
constexpr std::size_t kHadTexture = 0xD8;     // uint8: Read stores here the bool Write made of +0x104; the constructor leaves it unset (pool memory); read only when +0xD9 is set
constexpr std::size_t kLoaded = 0xD9;         // uint8: 1 from Read (the geometry is rebuilt on message 0x26D31EC1); 0 from the constructor
constexpr std::size_t kBuffers = 0xDC;        // six pointers (+0xDC to +0xF0): vertex buffers, made by 0x005CAD60, freed by Shutdown; drawing
constexpr std::size_t kBuffer6 = 0xF4;        // three more dwords of the drawing's (+0xF4, +0xF8, +0xFC), a byte +0x100
constexpr std::size_t kCrane = 0x104;         // a pointer of the drawing's; whether it is set is saved as a bool
constexpr std::size_t kCranePlace = 0x108;    // float[3]; its y (+0x10C) follows the height (0x005CC020); saved; frames
constexpr std::size_t kRows = 0x118;          // a vector of vectors (begin, end +0x11C, capacity +0x120): the textures, six rows of five; drawing
constexpr std::size_t kSides = 0x124;         // uint8, set by 0x005CD710, unset by the constructor (pool memory until then); saved
constexpr std::size_t kProps = 0x128;         // a list (sentinel pointer; 0xC-byte nodes {next, previous, prop occupant}); saved as a count and references
constexpr std::size_t kStageProps = 0x12C;    // a second such list: the props the step removes when the floors are done; saved
constexpr std::size_t kEffects = 0x130;       // a vector of visual effect props (begin, end +0x134, capacity +0x138); saved as a count and references
constexpr std::size_t kEffect = 0x13C;        // one more visual effect prop; saved as a reference
constexpr std::size_t kStage = 0x140;         // int32: 0 start, 1 between floors, 2 a floor, 3 finishing, 4 done; saved as one byte
constexpr std::size_t kStartDay = 0x144;      // uint32: the day SetLot ran (simulator slot 8); saved
constexpr std::size_t kUnused148 = 0x148;     // uint32: zero from the constructor, no writer found but Read; saved
constexpr std::size_t kNextDay = 0x14C;       // uint32: the day of the next step; saved
constexpr std::size_t kFloors = 0x150;        // uint32: the floors to build (set by the drawing's setup 0x005CAD60); saved
constexpr std::size_t kFloor = 0x154;         // uint32: the floors started; saved
constexpr std::size_t kHeight158 = 0x158;     // float: a height of the last stage (set by the drawing's setup 0x005CAD60); saved
constexpr std::size_t kFirstHeight = 0x15C;   // float: the first stage's height (set by the drawing's setup 0x005CAD60); saved
constexpr std::size_t kFirstDays = 0x160;     // int32: days of stage 0; saved
constexpr std::size_t kPauseDays = 0x164;     // int32: days of stage 1; saved
constexpr std::size_t kLastDays = 0x168;      // int32: days of stage 3; saved
constexpr std::size_t kFloorDays = 0x16C;     // uint32: days of stage 2 per floor; saved
constexpr std::size_t kElapsed = 0x170;       // uint32: milliseconds since the stage began: zeroed by SetLot and by every step that is due, counted up by the frame's animation; saved; frames
constexpr std::size_t kCraneState = 0x174;    // int32 (0x005CD570 from the frame's animation), saved as one byte; frames
constexpr std::size_t kCraneAngle = 0x178;    // float[2] (+0x178, +0x17C; set by the drawing's setup 0x005CDDA0); saved
constexpr std::size_t kCraneBusy = 0x180;     // uint8 bool: zero from the constructor, no writer found but Read; saved
constexpr std::size_t kCraneTime = 0x184;     // uint32[2] (+0x184, +0x188); saved; frames
constexpr std::size_t kCraneTarget = 0x18C;   // float[2] (+0x18C, +0x190); saved; frames
constexpr std::size_t kNetwork = 0x194;       // a list (0x10-byte nodes {next, previous, two bools and two unset bytes, network occupant}); saved
constexpr std::size_t kDisplacement = 0x198;  // the virtual base's displacement word: zero
constexpr std::size_t kModelBase = 0x19C;     // the model part's virtual base (vtable only)
// Never written by the constructor (the pool's memory): +0xD8, +0xDA,
// +0xDB, +0x101 to +0x103, +0x124 to +0x127, +0x181 to +0x183.
} // namespace field

// Byte offsets of the primary vtable's slots.
namespace slot {
constexpr std::size_t kAsOccupant = 0x0C, kSetLot = 0x10;
} // namespace slot

// Byte offsets of the occupant part's slots (the part is the object + 8).
namespace occupant {
constexpr std::size_t kInit = 0x0C, kShutdown = 0x10, kIsInitialized = 0x14, kAsPropertyHolder = 0x18;
constexpr std::size_t kGetType = 0x1C, kGetPosition = 0x20, kGetBoundingBox = 0x28, kIsFlagSet = 0x80;
constexpr std::size_t kGetFlags = 0x84;
} // namespace occupant

// Logs a function's first call once ("constrocc: first <name>"; defined in
// constrocc_sim.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::constrocc_sim
