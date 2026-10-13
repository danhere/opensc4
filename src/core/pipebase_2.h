// The intermediate class of the underground pieces (build 641; the base of
// the pipe occupant, src/core/pipeocc_sim.h, and of the subway occupant),
// second half: the constants of its life (constructor, destructor, deleting
// destructor, the occupant part's Init, Shutdown, Read) and of the functions
// that fill its saved vectors (slots 89 and 90). The class map, the small
// slots and Write are the first half's (src/core/pipebase_sim.h and
// docs/pipebase.md); this header holds only what src/core/pipebase_2.cpp
// needs, under its own namespace so that both can be included together.
//
// The object as these functions show it (0x278 bytes of its own when it is
// the most derived class, which it never is: the pipe's is 0x280):
//   +0x00 to +0x177 the network occupant (src/core/netocc_sim.h), with the
//     vtables of this class: +0x0 0x00AAAD40 (106 slots), +0x4 0x00AAACB8,
//     +0x8 0x00AAAC9C, +0xC 0x00AAAC38, +0x20 0x00AAAC20, +0x38 0x00AAAC1C;
//     the virtual base table at +0x3C is 0x00AAAEE8 (-4, then 0x238), the
//     virtual base at +0x274 has the vtable 0x00AAABD0 and the word before
//     it (+0x270) the displacement less 0x238.
//   +0x178 a texture binding (Init loads it by the descriptor's key at
//     +0x78), +0x1B0 another (the descriptor's +0x8C) and +0x1CC to +0x1E0
//     six more (slot 87 loads them): each released through 0x008090E0.
//   +0x17C the number of side walls in use (saved), +0x180 four vectors of
//     0x18-byte corner records, one per side wall (saved), +0x1B4 the floor's
//     vector (saved), +0x1C0 a sixth saved vector, +0x1E4 and +0x1F0 two
//     vectors that are not saved (slot 87 fills them through 0x0065D160).
//   +0x1FC a 4 by 4 float matrix (saved; identity from the constructor and
//     Init, computed by slot 85).
//   +0x23C and +0x240 the piece's cell (saved; Init asks the city, slot
//     0x17C), +0x244 a referenced object (released by Shutdown).
//   +0x248 to +0x24A three bytes (saved as bools), +0x24C four floats, the
//     heights Init was given for the four corners (saved), +0x25C a float
//     (saved), +0x260 to +0x262 three bytes (not saved: Read sets +0x261 to 1
//     and +0x262 to 0), +0x264 the piece type (Init's second argument; saved),
//     +0x268 a word (saved), +0x26C the descriptor (slot 101's answer).
// A corner record (the network occupant's corner layout): x, y, z, four
// bytes (the first three are set to 0xFF by slots 89 and 90, the fourth is
// the corner's own), then two floats, which slot 89 uses as the position
// across the wall (0 or 1) and the wall's height over 16.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::pipebase_2 {

namespace field {
constexpr std::size_t kOccupant = 0x4;      // base: the cISC4Occupant part
constexpr std::size_t kSerializable = 0x8;  // the cIGZSerializable part
constexpr std::size_t kHolder = 0xC;        // base: the property holder
constexpr std::size_t kExemplarPart = 0x20; // base: the exemplar holder part
constexpr std::size_t kModelPart = 0x38;    // the model part's thunk part
constexpr std::size_t kVbtable = 0x3C;      // the virtual base table pointer
constexpr std::size_t kModelState = 0x50;   // network occupant: the model part's 0x14 bytes (zeroed again here)
constexpr std::size_t kStateFloat = 0x60;   // the float at the end of the model state (saved by this class)
constexpr std::size_t kCorners = 0x9C;      // network occupant: four 0x18-byte corners
constexpr std::size_t kTexture178 = 0x178;  // texture binding
constexpr std::size_t kWallCount = 0x17C;   // uint32: side walls in use (index of the next wall vector)
constexpr std::size_t kWalls = 0x180;       // four vectors, 0xC bytes each: begin, end, capacity
constexpr std::size_t kTexture1B0 = 0x1B0;  // texture binding
constexpr std::size_t kFloor = 0x1B4;       // vector: the four corners (slot 90)
constexpr std::size_t kVector1C0 = 0x1C0;   // vector (saved)
constexpr std::size_t kTextures1CC = 0x1CC; // six texture bindings, to +0x1E0
constexpr std::size_t kVector1E4 = 0x1E4;   // vector (not saved)
constexpr std::size_t kVector1F0 = 0x1F0;   // vector (not saved)
constexpr std::size_t kMatrix = 0x1FC;      // sixteen floats
constexpr std::size_t kCellX = 0x23C;       // int32
constexpr std::size_t kCellZ = 0x240;       // int32
constexpr std::size_t kObject244 = 0x244;   // referenced object (Release through its slot 2)
constexpr std::size_t kByte248 = 0x248;     // three bytes: +0x248 picks the wall's pair of corners, +0x249 and
                                            // +0x24A differ when a wall is wanted without asking the neighbours
constexpr std::size_t kTops = 0x24C;        // four floats: the heights Init was given, by corner
constexpr std::size_t kFloat25C = 0x25C;    // float
constexpr std::size_t kDirty = 0x260;       // three bytes
constexpr std::size_t kPieceType = 0x264;   // uint32
constexpr std::size_t kWord268 = 0x268;     // uint32
constexpr std::size_t kDescriptor = 0x26C;  // the descriptor (slot 101's answer)
constexpr std::size_t kVbase = 0x274;       // the virtual base when this class is the most derived
} // namespace field

// The virtual base's displacement from +0x3C in an object of this class.
constexpr uint32_t kVbaseDisplacement = 0x238;

constexpr std::size_t kCornerSize = 0x18;
constexpr std::size_t kVectorSize = 0xC;

// Byte offsets of the primary vtable's slots these functions call.
namespace slot {
constexpr std::size_t kRelease = 0x8;             // of any referenced object
constexpr std::size_t kConnectsToNeighbor = 0xBC; // slot 47 (edge, type), the network occupant's
constexpr std::size_t kGetPieceType = 0x148;      // slot 82, 0x00648460 (none): the word at +0x264
constexpr std::size_t kSlot88 = 0x160;            // slot 88, 0x0065A3F0 (none): the first half's
constexpr std::size_t kRefresh = 0x190;           // slot 100, 0x0065AF10 (one)
constexpr std::size_t kGetDescriptor = 0x194;     // slot 101 (none; the derived class's)
constexpr std::size_t kGetNetworkType = 0x198;    // slot 102 (none; the derived class's)
} // namespace slot

// The pool of this class's own objects (never made: no factory): pages of
// 64 KB at address & 0xFFFF0000, the free list at +8 and the used count at
// +0xC; the page is opened again at the count 0x4000004D.
constexpr uint32_t kPoolPageMask = 0xFFFF0000u;
constexpr uint32_t kPoolReopenCount = 0x4000004Du;

} // namespace opensc4::pipebase_2
