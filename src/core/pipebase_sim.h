// The intermediate class of the underground pieces (build 641): the class
// between the network occupant (src/core/netocc_sim.h) and the two classes
// whose pieces are drawn in the underground view, the pipe occupant
// (src/core/pipeocc_sim.h) and the subway occupant. It adds to the network
// occupant what such a piece needs to be drawn as a trench cut into the
// ground: the cell it lies in, the heights of the ground over its four
// corners, the depth of its floor, which of its sides are open, the corner
// records of its walls and floor, a matrix and its textures. This header is
// the class map for the whole class (both halves of the code:
// src/core/pipebase_1.cpp, the small slots, the geometry state and Write;
// src/core/pipebase_2.cpp, with its own src/core/pipebase_2.h, the class's
// life, Read and the walls). The two headers use different namespaces and
// can be included together.
//
// Class map
//   No class ID and no factory: the class is abstract (slots 101, 102 and
//   105 of its own vtable are the pure-call stub 0x005D4A10, and so is
//   GetGZCLSID) and only the pipe's and the subway's constructors call its
//   constructor 0x0065C330 (with 0). It answers the IIDs 0x4A10AFC2 (its
//   own), 0xA821EF94 (the network occupant's) and 0xA9764C8C with the object
//   itself; every other ID goes to the occupant base (0x005E89C0, on the
//   object + 4) and then to the model part (0x00801940, on the object +
//   0x68). The word at +0x264 is the derived class's ID (0x49C05B9F a pipe,
//   0xCA16374F a subway), given to Init and answered by slot 82; the
//   neighbour searches use it as an interface ID. The occupant type is the
//   network occupant's, 0xC772BF98.
//   0x278 bytes of its own (the pipe's object is 0x280, the subway's 0x2D8);
//   a pool at 0x00B0D008 that only the deleting destructor 0x0065C650 names.
//   Code: 0x00659B00 to 0x0065E800, and 0x00648460 and 0x00648470, which lie
//   in the pipe's range but are in all three vtables. 0x00657730 (slot 83,
//   a bare `ret 0xC`) is a stub that nine vtables of several classes share.
//
// Vtables, as the constructor stores them (the slots the class owns; the
// others are the network occupant's, the occupant base's or shared stubs;
// the argument counts are from each function's own `ret N`):
//   +0x00 primary, 0x00AAAD40, 106 slots (the network occupant interface's
//     81, then this class's 25):
//       0 QueryInterface 0x0065A080 (2)    1, 2 AddRef, Release: the jumps
//                                             0x0065A860 and 0x0065A870
//       4 Shutdown 0x0065CD90 (0)          10 SetNetworkFlag 0x0065A900 (1)
//      11 ClearNetworkFlag 0x0065A940 (1)  21 IsPlaced 0x00648470 (0)
//      30 SetPiece 0x0065ADB0 (2)          33 GetModelInstances 0x00659D70 (5)
//      78 0x0065CF20 (1; textures)         80 ComputeBoundingBox 0x00659E00 (2)
//      81 Init 0x0065C6B0 (3)              82 GetClassId 0x00648460 (0)
//      83 the stub 0x00657730 (3)          84 SetHeights 0x0065AE30 (1)
//      85 0x0065B700 (0; the matrix)       86 Rebuild 0x0065ADD0 (1)
//      87 0x0065D810 (3; textures, model)  88 UpdateSides 0x0065A3F0 (0)
//      89 SetSideWalls 0x0065E260 (1)      90 SetFloor 0x0065E410 (1)
//      91 the stub 0x00648F10 (1)          92 GetSides 0x0065A6F0 (3)
//      93 GetEdgeDepth 0x0065AA70 (3)      94 GetSurfaceHeights 0x00659DD0 (1)
//      95 SetSurfaceHeights 0x0065B6B0 (1) 96 GetShape 0x0065A810 (4)
//      97 SetDepthStatics 0x00659DB0 (3)   98 CountRun 0x0065A120 (3)
//      99 GetSideRuns 0x0065A200 (1)      100 Refresh 0x0065AF10 (1)
//     101 the descriptor (pure; the pipe's 0x006484B0, the subway's 0x00652CD0)
//     102 the network type (pure; the pipe's 0x00580F60 answers 4, the
//         subway's 0x00505070 answers 7)
//     103 GetTint 0x0065B020 (1; the pipe has its own)
//     104 GetTextures 0x0065A880 (2)
//     105 the default flags (pure; the pipe's 0x06000000, the subway's
//         0x07000000)
//     (34 GetVertexHeights 0x0065A6C0 lies in this class's range but is the
//     network occupant's own slot too: src/core/netocc_state.cpp has it.)
//   +0x04 cISC4Occupant, 0x00AAACB8, 34 slots: 3 OccupantInit 0x0065C070,
//     8 GetPosition 0x0065A720 (1), 10 GetBoundingBox 0x00659B00 (2),
//     15 PostOccupantMessage 0x0065A980 (2); 0, 1, 2 and 4 are thunks.
//   +0x08 cIGZSerializable, 0x00AAAC9C: 3 Write 0x0065B140 and 4 Read
//     0x0065E510 (both ret 4, the object + 8); 5 GetGZCLSID is pure.
//   +0x0C property holder, 0x00AAAC38; +0x20 exemplar holder part,
//     0x00AAAC20; +0x38 the model part's thunk part, 0x00AAAC1C: thunks.
//   +0x3C the virtual base table 0x00AAAEE8 (-4, then the displacement
//     0x238), the virtual base at +0x274 with the vtable 0x00AAABD0 (19
//     slots, all adjustor thunks): 12 -> 0x0065B290 the draw (2), 17 ->
//     0x0065A0E0 the piece key (1). Both take the virtual base less the word
//     before it (the displacement less 0x238) and reach the object with a
//     fixed -0x274; the pipe and the subway have thunks of their own for the
//     draw (0x00648760, 0x00652F70: 8 and 0x60 more) and their own piece
//     keys, so 0x0065A0E0 is never reached.
//   Not in a vtable: 0x0065A640 (slot 89 calls it), 0x0065AB50 (the floor
//   depth; slots 30, 84, 86 and 95 call it), 0x0065B040 (the statics; the
//   pipe's and the subway's statics call it), 0x0065AE80 (the render states),
//   0x0065B070 (a corner vector into a stream; the vector in EBX and the
//   stream in ESI; other classes' Writes call it too), 0x0065CFF0 and
//   0x0065D160 (the second half's). Before the class's range and used by
//   it: the two searches of a cell's occupants, 0x006584D0 (by an interface
//   ID) and 0x006585E0 (by the interface 0x8A139184 and a type).
//
// Layout (object offsets; the network occupant's fields are in
// src/core/netocc_sim.h and the base's in src/core/occbase_sim.h):
//   +0x50, +0x51, +0x60  the model part's state: two bytes and a float
//                        (the network occupant's; this class's Write saves
//                        them)
//   +0x9C                network occupant: the four corners (x, y, z at +0,
//                        +4, +8 of each 0x18-byte record); y is the floor
//   +0xFC                network occupant: the piece key (0x4B00 and 0x300
//                        the flat pieces; 0x200, 0x800, 0xA00, 0x1400 slopes)
//   +0x104               network occupant: rotation (bits 0-6) and flip
//   +0x124, +0x130       network occupant: the box, low and high (slot 80)
//   +0x178               texture binding (not saved)
//   +0x17C               uint32: side walls in use (saved)
//   +0x180 +0x18C +0x198 +0x1A4  four vectors of corner records, a side wall
//                        each (saved)
//   +0x1B0               texture binding (not saved)
//   +0x1B4               vector: the floor's four corners (saved)
//   +0x1C0               vector (saved; empty in every corpus save)
//   +0x1CC to +0x1E0     six texture bindings, three pairs: slot 104 picks a
//                        pair by the network flags 0x20000 and 0x10000
//   +0x1E4, +0x1F0       two vectors (not saved; the draw's)
//   +0x1FC               sixteen floats, a 4 by 4 matrix (saved)
//   +0x23C, +0x240       int32: the piece's cell, x and z (saved; Init asks
//                        the city, slot 0x17C, with the piece's position)
//   +0x244               a referenced object (the draw reads a model from
//                        it; not saved)
//   +0x248 +0x249 +0x24A three bytes (saved as bools): slot 88 computes them
//                        from the piece key, rotation and flip, and sets the
//                        network flags 0x01000000 (as +0x248), 0x02000000 and
//                        0x04000000 (as the opposite of +0x249 and +0x24A)
//   +0x24C to +0x258     four floats: the ground's height over each corner
//                        (saved; Init's, slot 95 sets them)
//   +0x25C               float: the floor depth 0x0065AB50 computes (saved)
//   +0x260               byte: dirty; the refresh and the flag setters set
//                        it, the draw clears it
//   +0x261               byte: rebuild wanted; the refresh and Read set it,
//                        the draw rebuilds (slot 87) and clears it
//   +0x262               byte (slot 87 sets it, Read clears it)
//   +0x264               uint32: the derived class's ID (saved)
//   +0x268               uint32 (saved; zero in every corpus save)
//   +0x26C               the descriptor: slot 101's answer (not saved)
//   +0x270               the virtual base's displacement less 0x238
//   +0x274               the virtual base (its vtable), when most derived
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::pipebase_sim {

constexpr uint32_t kIidPipeBase = 0x4A10AFC2;
constexpr uint32_t kIidNetworkOccupant = 0xA821EF94;
constexpr uint32_t kIidObject = 0xA9764C8C;
constexpr uint32_t kOccupantType = 0xC772BF98;
constexpr uint32_t kPipeClassId = 0x49C05B9F;
constexpr uint32_t kSubwayClassId = 0xCA16374F;
constexpr std::size_t kObjectSize = 0x278;

// The virtual base's displacement from +0x3C in an object of this class.
constexpr uint32_t kVbaseDisplacement = 0x238;

constexpr std::size_t kCornerSize = 0x18;
constexpr std::size_t kVectorSize = 0xC;

namespace field {
constexpr std::size_t kOccupant = 0x4;       // base: the cISC4Occupant part
constexpr std::size_t kSerializable = 0x8;   // the cIGZSerializable part
constexpr std::size_t kHolder = 0xC;         // base: the property holder
constexpr std::size_t kExemplarPart = 0x20;  // base: the exemplar holder part
constexpr std::size_t kOccupantFlags = 0x33; // base: flags byte; bit 1 (2): the refresh tells the view
constexpr std::size_t kModelPart = 0x38;     // the model part's thunk part
constexpr std::size_t kVbtable = 0x3C;       // the virtual base table pointer
constexpr std::size_t kModelState = 0x50;    // network occupant: the model part's state; bytes +0x50, +0x51 saved
constexpr std::size_t kStateFloat = 0x60;    // the float at the end of the model state (saved by this class)
constexpr std::size_t kModelBase = 0x68;     // network occupant: given to the model's QueryInterface
constexpr std::size_t kCorners = 0x9C;       // network occupant: four 0x18-byte corners (x, y, z first)
constexpr std::size_t kCornerY = 0xA0;       // the first corner's y; the others every 0x18 bytes
constexpr std::size_t kCornerZ = 0xA4;       // the first corner's z
constexpr std::size_t kPieceKey = 0xFC;      // network occupant: uint32
constexpr std::size_t kRotationFlip = 0x104; // network occupant: uint8, bits 0-6 rotation, bit 7 flip
constexpr std::size_t kBoxLow = 0x124;       // network occupant: three floats
constexpr std::size_t kBoxHigh = 0x130;      // network occupant: three floats
constexpr std::size_t kFlags = 0x13C;        // network occupant: uint32 network flags
constexpr std::size_t kTexture178 = 0x178;   // texture binding
constexpr std::size_t kWallCount = 0x17C;    // uint32: side walls in use
constexpr std::size_t kWalls = 0x180;        // four vectors, 0xC bytes each: begin, end, capacity
constexpr std::size_t kTexture1B0 = 0x1B0;   // texture binding
constexpr std::size_t kFloor = 0x1B4;        // vector: the floor's corners
constexpr std::size_t kVector1C0 = 0x1C0;    // vector (saved)
constexpr std::size_t kTextures = 0x1CC;     // six texture bindings: the pairs at +0x1CC, +0x1D4 and +0x1DC
constexpr std::size_t kVector1E4 = 0x1E4;    // vector (not saved)
constexpr std::size_t kVector1F0 = 0x1F0;    // vector (not saved)
constexpr std::size_t kMatrix = 0x1FC;       // sixteen floats
constexpr std::size_t kCellX = 0x23C;        // int32
constexpr std::size_t kCellZ = 0x240;        // int32
constexpr std::size_t kModel = 0x244;        // referenced object
constexpr std::size_t kSides = 0x248;        // three bytes
constexpr std::size_t kSurface = 0x24C;      // four floats: the ground's height over each corner
constexpr std::size_t kFloorDepth = 0x25C;   // float
constexpr std::size_t kDirty = 0x260;        // byte
constexpr std::size_t kRebuild = 0x261;      // byte
constexpr std::size_t kByte262 = 0x262;      // byte
constexpr std::size_t kClassId = 0x264;      // uint32: the derived class's ID
constexpr std::size_t kWord268 = 0x268;      // uint32
constexpr std::size_t kDescriptor = 0x26C;   // slot 101's answer
constexpr std::size_t kVbaseShift = 0x270;   // the virtual base's displacement less 0x238
constexpr std::size_t kVbase = 0x274;        // the virtual base when this class is the most derived
} // namespace field

// Network flags (the word at +0x13C) these functions look at.
namespace flag {
constexpr uint32_t kTextureA = 0x00010000;  // slot 104: the pair at +0x1D4
constexpr uint32_t kTextureB = 0x00020000;  // slot 104: the pair at +0x1DC (asked first)
constexpr uint32_t kTextures = 0x00030000;  // slots 10 and 11 mark the piece dirty when either changes
constexpr uint32_t kSide248 = 0x01000000;   // slot 88: set when the byte at +0x248 is not zero
constexpr uint32_t kSide249 = 0x02000000;   // slot 88: set when the byte at +0x249 is zero
constexpr uint32_t kSide24A = 0x04000000;   // slot 88: set when the byte at +0x24A is zero
constexpr uint32_t kKeyed = 0x06000000;     // the piece key answers +0xFC when any is set, else the IID
constexpr uint32_t kTop = 0x80000000;       // 0x0065A640 asks only pieces with it about neighbours without
} // namespace flag

// Piece keys (+0xFC) the functions tell apart.
namespace key {
constexpr uint32_t kSlope200 = 0x200, kFlat300 = 0x300, kSlope800 = 0x800, kSlopeA00 = 0xA00, kC00 = 0xC00;
constexpr uint32_t kSlope1400 = 0x1400, k1E00 = 0x1E00, kFlat = 0x4B00, k14E00 = 0x14E00;
} // namespace key

// Byte offsets of the primary vtable's slots, for Virtual<T>(object, slot::k...).
namespace slot {
constexpr std::size_t kQueryInterface = 0x0, kAddRef = 0x4, kRelease = 0x8, kShutdown = 0x10;
constexpr std::size_t kHasNetworkFlag = 0x20, kHasAnyNetworkFlag = 0x24, kSetNetworkFlag = 0x28;
constexpr std::size_t kClearNetworkFlag = 0x2C, kGetNetworkFlags = 0x30, kIsPlaced = 0x54;
constexpr std::size_t kGetRotation = 0x60, kGetFlip = 0x64, kSetPiece = 0x78, kGetModelInstances = 0x84;
constexpr std::size_t kGetVertexHeights = 0x88, kConnectsToNeighbor = 0xBC, kSlot78 = 0x138;
constexpr std::size_t kComputeBoundingBox = 0x140, kInit = 0x144, kGetClassId = 0x148, kSlot83 = 0x14C;
constexpr std::size_t kSetHeights = 0x150, kBuildMatrix = 0x154, kRebuild = 0x158, kBuildModel = 0x15C;
constexpr std::size_t kUpdateSides = 0x160, kSetSideWalls = 0x164, kSetFloor = 0x168, kSlot91 = 0x16C;
constexpr std::size_t kGetSides = 0x170, kGetEdgeDepth = 0x174, kGetSurfaceHeights = 0x178;
constexpr std::size_t kSetSurfaceHeights = 0x17C, kGetShape = 0x180, kSetDepthStatics = 0x184;
constexpr std::size_t kCountRun = 0x188, kGetSideRuns = 0x18C, kRefresh = 0x190, kGetDescriptor = 0x194;
constexpr std::size_t kGetNetworkType = 0x198, kGetTint = 0x19C, kGetTextures = 0x1A0, kGetDefaultFlags = 0x1A4;
} // namespace slot

// Byte offsets of the occupant part's slots (the part is the object + 4).
namespace occupant {
constexpr std::size_t kInit = 0xC, kIsInitialized = 0x14, kGetType = 0x1C, kGetPosition = 0x20;
constexpr std::size_t kGetBoundingBox = 0x28, kPostOccupantMessage = 0x3C, kIsFlagSet = 0x80, kGetFlags = 0x84;
} // namespace occupant

// The serializable part's GetGZCLSID (the part is the object + 8).
constexpr std::size_t kGetGZCLSID = 0x14;

// The virtual base's slots (the virtual base is the object + 0x3C + the
// second word of the table at +0x3C).
constexpr std::size_t kVbaseDraw = 0x30;
constexpr std::size_t kVbaseGetPieceKey = 0x44;

// The statics slot 97 sets (0x00B0CFD0, .data; offsets from it): the depth
// of a piece's floor under the ground (3.0), the byte that says the floor is
// measured from the ground (1) and not from the corners, and the rise of the
// floor over the corners used otherwise (10.0).
namespace statics {
constexpr std::size_t kDepth = 0x0;      // float
constexpr std::size_t kFromGround = 0x4; // byte
constexpr std::size_t kRise = 0x8;       // float
} // namespace statics

// At 0x00B0CFE8 follow four pairs of int32, the cell steps (x, z) over the
// four edges: (-1, 0), (0, -1), (1, 0), (0, 1).
constexpr std::size_t kEdgeStepSize = 8;

// The occupant messages the occupant part's slot 15 answers with a second
// message (the occupant manager's inserted and removed messages,
// src/core/occmgr_sim.h), and that second message for each.
constexpr uint32_t kMessageInserted = 0x99EF1142;
constexpr uint32_t kMessageRemoved = 0x99EF1143;
constexpr uint32_t kMessagePieceInserted = 0x8B87A974;
constexpr uint32_t kMessagePieceRemoved = 0x8B87A975;

// The object's virtual base, wherever the most derived class put it.
inline char* VirtualBase(char* self)
{
    return self + field::kVbtable + At<uint32_t>(At<char*>(self, field::kVbtable), 4);
}

// Logs a function's first call once ("pipebase: first <name>"; defined in
// pipebase_1.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::pipebase_sim
