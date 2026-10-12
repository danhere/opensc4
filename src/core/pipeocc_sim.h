// Our cSC4PipeOccupant (build 641): the class map, every vtable by the slots
// the class owns and the object's fields. A pipe occupant is one piece of the
// city's water pipes (a tile of the pipe network): the network occupant's
// state (corners, edges, flags, the transform; src/core/netocc_sim.h) plus
// one word of its own, whether the pipe is watered, which the water simulator
// sets (src/core/water_sim.cpp, SetWatered) and which picks the tint the
// piece is drawn with.
//
// Class map
//   class ID 0x49C05B9F (GetGZCLSID 0x00648F40, in the code after the class's
//   range, shared with two other classes' vtables: not hooked), which is also
//   the interface ID the class answers to. The occupant type (what the
//   occupant part's GetType answers) is the network occupant's, 0xC772BF98:
//   the pipes are inside the network occupant test's walk
//   (src/core/netocc_autotest.cpp) and are told apart there by the class ID.
//   0x280 bytes, from the pool at 0x00B0BEC0 (pages of 64 KB at address &
//   0xFFFF0000, 102 items each, the free list at +8 and the used count at
//   +0xC; the pool's table at 0x00AA9960: slot 1 the free 0x00648970, slot 2
//   the page set-up 0x00648580).
//   constructor 0x006488F0 (one argument: non-zero for the most derived
//   object), called by the factory 0x006489B0 (the class registration's
//   pointer; it returns the object + 4) and by 0x00648D10 (a maker class's
//   function, which also calls the pool allocation 0x006487D0 before it:
//   its argument 0x280 is not read).
//   deleting destructor 0x006489F0 (flags; the holder part's slot 22 and the
//   +0x38 part's slot 0 reach it through thunks).
//   The class's code range is 0x00648400 to 0x00648F10 (62 function starts);
//   the pipe's own functions are listed per vtable below. NOT the pipe's:
//   0x006483C0 (a constructor of the class before, vtable 0x00AA9578) and
//   0x00648440 (its factory), 0x00648460 and 0x00648470 (slots 82 and 21,
//   shared with the intermediate class's vtable 0x00AAAD40 and another class's
//   0x00AAA3C8: code of the intermediate class), 0x00648C40, 0x00648CC0,
//   0x00648D10, 0x00648E90 and 0x00648EC0 (other classes').
//
// The intermediate class. The pipe derives from a class (vtable 0x00AAAD40,
// its constructor 0x0065C330, destructor 0x0065C0C0, code about 0x00652000 to
// 0x0065D000) that derives from the network occupant and is also the base of
// a second class (constructor around 0x006530D0, vtable 0x00AAA3C8). It is
// not described here and nothing of it is reimplemented; what the pipe's code
// shows of it:
//   - the constructor 0x0065C330 takes one argument (0 when a derived class
//     calls it) and calls the network occupant's constructor 0x0061BDD0
//     with 0; it stores its own five vtables, its virtual base displacement
//     (0x238 in its own object, so 0x240 from the pipe's; the virtual base
//     is at +0x3C plus that), zeroes its fields from +0x178 to +0x270 (not
//     +0x26C, which its occupant Init sets) and builds six vectors;
//   - its fields are at +0x178 up to +0x270: a word at +0x17C, six vectors of
//     0x18-byte records (the network occupant's corner layout) at +0x180,
//     +0x18C, +0x198, +0x1A4, +0x1B4 and +0x1C0, a 4 by 4 float matrix at
//     +0x1FC, words at +0x23C and +0x240 (position in the network's grid,
//     which it asks the network manager about), bytes at +0x248 to +0x24A,
//     floats at +0x24C to +0x25C, a dirty byte at +0x260 (set by the pipe's
//     Read), +0x261, +0x262, words at +0x264 and +0x268 and, at +0x26C,
//     the descriptor the pipe's slot 101 answers (stored by its occupant
//     Init, 0x0065C070);
//   - its Init 0x0065C6B0 (ret 0xC, the interface's slot 81) takes the
//     description structure the pipe's Init was given, the class ID and the
//     descriptor 0x00B0BED0;
//   - its Write 0x0065B140 and Read 0x0065E510 write and read its own
//     version (3), the network occupant's whole record and then those
//     fields (tools/re/save_schemas/pipeocc.py);
//   - its QueryInterface is 0x0065A080 (ret 8), AddRef 0x0065A860 and
//     Release 0x0065A870.
//
// Vtables, as the constructor stores them (the slots the pipe owns; the
// others are the network occupant's, the intermediate class's or shared
// stubs):
//   +0x00 primary, 0x00AA97B8, 106 slots (the network occupant interface's
//     81, then the intermediate class's from 81 on):
//       0 QueryInterface 0x006484D0 (ret 8)
//       3 Init 0x00648BF0 (ret 4)
//     101 the descriptor 0x006484B0 (0x00B0BED0)
//     103 the tint 0x006488A0 (ret 4)
//     105 the default flags 0x006484C0 (0x06000000)
//     (21 IsPlaced 0x00648470, 82 0x00648460, 100 the refresh 0x0065AF10 are
//     the intermediate class's)
//   +0x04 cISC4Occupant, 0x00AA9730: 3 OccupantInit 0x00648BC0; 0 is the
//     thunk 0x006485C0 to QueryInterface.
//   +0x08 cIGZSerializable, 0x00AA9714: 3 Write 0x00648800 and 4 Read
//     0x00648840 (both ret 4, the object + 8); 5 GetGZCLSID 0x00648F40.
//   +0x0C property holder, 0x00AA96B0: 22 the deleting destructor thunk
//     0x00648650, 23 and 24 the save thunks 0x006488D0 and 0x006488E0 (this
//     - 4 to Write and Read).
//   +0x20 exemplar holder part, 0x00AA9698; +0x38 the model part's thunk
//     part, 0x00AA9694 (slot 0 a thunk to the deleting destructor).
//   +0x3C the virtual base table 0x00AA9970 (-4, then the displacement
//     0x240; the table of the pool at 0x00AA9960 precedes it), the virtual
//     base's vtable 0x00AA9648 (19 slots, all adjustor thunks to the
//     network occupant's code, but slot 17 -> 0x006487B0 -> 0x00648500, the
//     pipe's own: the piece key). The abstract vtable the most derived
//     constructor stores at +0x27C first is 0x00AA1F38.
//
// Fields (object offsets). The network occupant's and the base's are in
// src/core/netocc_sim.h and src/core/occbase_sim.h.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::pipeocc_sim {

constexpr uint32_t kClassId = 0x49C05B9F;
constexpr uint32_t kIidPipe = 0x49C05B9F;
constexpr uint32_t kIidNetworkOccupant = 0xA821EF94;
constexpr uint32_t kIidObject = 0xA9764C8C;
constexpr uint32_t kOccupantType = 0xC772BF98;
constexpr std::size_t kObjectSize = 0x280;

namespace field {
constexpr std::size_t kOccupant = 0x4;       // base: the cISC4Occupant part
constexpr std::size_t kSerializable = 0x8;   // the cIGZSerializable part
constexpr std::size_t kHolder = 0xC;         // base: the property holder
constexpr std::size_t kExemplarPart = 0x20;  // base: the exemplar holder part
constexpr std::size_t kModelPart = 0x38;     // the model part's thunk part
constexpr std::size_t kVbtable = 0x3C;       // the virtual base table pointer
constexpr std::size_t kPieceKey = 0xFC;      // network occupant: the word saved before the variation
constexpr std::size_t kDirty = 0x260;        // intermediate class: byte; the pipe's Read sets it to 1
constexpr std::size_t kDescriptor = 0x26C;   // intermediate class: slot 101's answer, stored by its occupant Init
constexpr std::size_t kWatered = 0x270;      // the pipe's own word: 1 if watered; saved; constructor and Init zero it
constexpr std::size_t kVbaseShift = 0x278;   // the virtual base's displacement word: 0 (the displacement less 0x240)
constexpr std::size_t kVbase = 0x27C;        // the virtual base (4 bytes: its vtable)
} // namespace field

// Byte offsets of the primary vtable's slots the pipe owns, for Virtual<T>(object, slot::k...).
namespace slot {
constexpr std::size_t kInit = 0xC, kIsPlaced = 0x54, kRefresh = 0x190, kGetDescriptor = 0x194;
constexpr std::size_t kGetTint = 0x19C, kGetDefaultFlags = 0x1A4;
} // namespace slot

// The virtual base's slot 17 (the piece key; the virtual base is at +0x27C).
constexpr std::size_t kVbaseGetPieceKey = 0x44;

// The statics 0x00648A90 sets up once: eight floats from +0 (see InitStatics) and the byte at +0x28.
constexpr std::size_t kStaticsDone = 0x28;

// Logs a function's first call once ("pipeocc: first <name>"; defined in
// pipeocc_sim.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::pipeocc_sim
