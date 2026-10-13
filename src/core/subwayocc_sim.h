// Our subway occupant (build 641; the game has no name for the class in its
// ID table, `cSC4SubwayOccupant` is ours): the class map, every vtable by the
// slots the class owns and the object's fields. A subway occupant is one
// underground piece of the city's subway (a tile of the subway network): the
// network occupant's state (corners, edges, flags, the transform;
// src/core/netocc_sim.h), the state of the intermediate class of the
// underground pieces (the pipe's base too; src/core/pipeocc_sim.h says what
// the pipe's code shows of it) and four corner records of its own, which
// the height query (slot 56) reads and slot 106 makes from the intermediate
// class's vectors when the path information is made.
//
// Class map
//   class ID 0xCA16374F (GetGZCLSID 0x00653D90, after the class's own
//   functions and in two other vtables too: 0x00AAA220, slot 38 of the
//   0x250-byte class's vtable 0x00AAA188, and 0x00AAA704, slot 81 of the
//   maker class's vtable 0x00AAA5C0; not hooked), which is also the only
//   interface ID the class adds (QueryInterface 0x00652D00 compares with
//   nothing else; 0xEA0A5ACF is the class ID and IID of another class, the
//   one before the pipe: GetGZCLSID 0x00648040, QueryInterface 0x00648080,
//   factory 0x00648440; no subway vtable or function has it). GetGZCLSID is
//   also called directly by the class registration (0x004606B8, with the
//   factory pushed at 0x004606B3). The occupant type (what the occupant
//   part's GetType answers) is the network occupant's, 0xC772BF98: the
//   subway pieces are inside the network occupant test's walk
//   (src/core/netocc_autotest.cpp) and are told apart by the class ID.
//   0x2D8 bytes, from the pool at 0x00B0CDB8 (pages of 64 KB at address &
//   0xFFFF0000, 90 items each, the free list at +8 and the used count at
//   +0xC; a full page's count has 0x40000000 added, and it is opened again
//   when a free brings it to 0x40000043). The pool's table: slot 1 the free
//   0x00653520, slot 2 the page set-up 0x00652E00, in two copies: 0x00AAA578
//   is what the exe's data has at 0x00B0CDB8, 0x00AAA590 what the static
//   initialiser 0x00A73F10 stores there at start-up after it has called the
//   page set-up for the first page (before any hook of ours is in place).
//   constructor 0x006530B0 (one argument: non-zero for the most derived
//   object), called by the factory 0x00653560 (the class registration's
//   pointer, pushed at 0x004606B3; it returns the object + 4) and by
//   0x00653A00 (the maker class's slot 82, which also calls the pool
//   allocation 0x00652FE0 before it: its argument 0x2D8 is not read).
//   deleting destructor 0x006535A0 (flags; the holder part's slot 22 and the
//   +0x38 part's slot 0 reach it through the thunks 0x00652E80 and
//   0x00652EA0).
//   The class's functions are between 0x00652CD0 and 0x006539F5. NOT the
//   subway's: 0x00652BD0 (before the range; slot 44 of the 0x250-byte class's
//   vtable, ret 8: asks an object for the subway IID and the answer for
//   0x4A10AFC2), 0x00652C30 and 0x00652CB0 (the
//   constructor and the factory of a 0x250-byte class, vtable 0x00AAA188,
//   whose base constructor is 0x00654F00; `SC4PlopOccupantB::Construct` in
//   data/addresses/tools.toml), 0x00652CF0 (slot 4, a jump to the
//   intermediate class's 0x0065CD90, in the pipe's vtable too), 0x00653A00
//   (the maker class's slot 82: vtable 0x00AAA5C0),
//   0x00653B80 (cdecl, called from 0x00610545 and 0x00653C63), 0x00653C00 and
//   0x00653CC0 (in the table at 0x00AA7A94), 0x00653D60 (the maker class's
//   constructor, called from 0x006109C5 and 0x00610F07), 0x00653DA0 (an exit
//   function pushed at 0x006546A2 and 0x00654747) and 0x00653DC0 (pointed to
//   from 0x00AA95A0, 0x00AAA1B0 and 0x00AAA868 and 0x80 after each: three
//   other classes' vtables, none of the subway's, which lie between
//   0x00AAA258 and 0x00AAA570).
//
// In the saves: the Rush Hour Tutorial city has 16 subway pieces (a straight
// line in the cells x 7, z 99 to 114: 14 with the piece key 0x4B00 and the
// two ends with 0x300), in every save made from it; the other two tutorial
// cities have none.
//
// The intermediate class (vtable 0x00AAAD40, constructor 0x0065C330,
// destructor 0x0065C0C0; the address names `SC4PipeBase::` in
// data/addresses/pipeocc.toml) is not this file's: its functions are called
// at their addresses. What the subway's code uses of it: the class ID word at
// +0x264 (stored by its Init; slot 82 answers it), the byte at +0x262 (set
// when its geometry is made: GetPathInfo calls slot 86 with 0 first when it
// is clear), the vector of four 0x18-byte corner records at +0x1B4 (saved)
// and the vector at +0x1E4 (not saved; slot 106 wants eight records of it
// and reads up to the twelfth), slot 99 (0x0065A200, one argument), the
// dirty byte at +0x260 (the subway's Read sets it to 1) and the descriptor
// at +0x26C (its occupant Init stores slot 101's answer there).
//
// Vtables, as the constructor stores them (the slots the subway owns; the
// others are the network occupant's, the intermediate class's or shared
// stubs):
//   +0x00 primary, 0x00AAA3C8, 107 slots (the network occupant interface's
//     81, the intermediate class's from 81 on, and one more, 106):
//       0 QueryInterface 0x00652D00 (ret 8)
//       3 Init 0x006537A0 (ret 4)
//      49 GetPathInfo 0x006537E0
//      56 the height at a point 0x00652D70 (ret 8; the result in ST0)
//     101 the descriptor 0x00652CD0 (0x00B0CDC8)
//     105 the default flags 0x00652CE0 (0x07000000)
//     106 the four corner records 0x00653130
//     (4 0x00652CF0 a jump to the intermediate class's 0x0065CD90, 21
//     IsPlaced 0x00648470, 82 0x00648460 are the intermediate class's; 102
//     is the shared stub 0x00505070, which answers 7)
//   +0x04 cISC4Occupant, 0x00AAA340: 3 OccupantInit 0x00653770; 0 is the
//     thunk 0x00652E40 to QueryInterface.
//   +0x08 cIGZSerializable, 0x00AAA324: 3 Write 0x00653010 and 4 Read
//     0x00653040 (both ret 4, the object + 8); 5 GetGZCLSID 0x00653D90.
//   +0x0C property holder, 0x00AAA2C0: 22 the deleting destructor thunk
//     0x00652E80, 23 and 24 the save thunks 0x00653090 and 0x006530A0 (this
//     - 4 to Write and Read).
//   +0x20 exemplar holder part, 0x00AAA2A8; +0x38 the model part's thunk
//     part, 0x00AAA2A4 (slot 0 a thunk to the deleting destructor).
//   +0x3C the virtual base table 0x00AAA588 (-4, then the displacement
//     0x298; the table of the pool at 0x00AAA578 precedes it), the virtual
//     base's vtable 0x00AAA258 (19 slots, all adjustor thunks to the
//     network occupant's code, but slot 17 -> 0x00652FC0 -> 0x00652D30, the
//     subway's own: the piece key). The abstract vtable the most derived
//     constructor stores at +0x2D4 first is 0x00AA1F38.
//
// Fields (object offsets). The network occupant's and the base's are in
// src/core/netocc_sim.h and src/core/occbase_sim.h.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::subwayocc_sim {

constexpr uint32_t kClassId = 0xCA16374F;
constexpr uint32_t kIidSubway = 0xCA16374F;
constexpr uint32_t kIidUnderground = 0x4A10AFC2; // the intermediate class's own IID (0x0065A080)
constexpr uint32_t kIidNetworkOccupant = 0xA821EF94;
constexpr uint32_t kIidObject = 0xA9764C8C;
constexpr uint32_t kIidOccupant = 0x26BB767E;
constexpr uint32_t kIidSerializable = 0xE4FDA3D4;
constexpr uint32_t kOccupantType = 0xC772BF98;
constexpr std::size_t kObjectSize = 0x2D8;

// The pool: the page mask, the items of a page and the used count at which
// a full page is opened again.
constexpr std::uintptr_t kPageMask = 0xFFFF0000u;
constexpr uint32_t kPageItems = 0x5A;
constexpr uint32_t kReopenCount = 0x40000043u;

// The piece keys (the word at +0xFC) whose corner records slot 106 takes from
// the vector at +0x1E4 and not from the one at +0x1B4.
constexpr uint32_t kKeySpecialA = 0x300, kKeySpecialB = 0x4B00;

namespace field {
constexpr std::size_t kOccupant = 0x4;       // base: the cISC4Occupant part
constexpr std::size_t kSerializable = 0x8;   // the cIGZSerializable part
constexpr std::size_t kHolder = 0xC;         // base: the property holder
constexpr std::size_t kRefCount = 0x10;      // base: the reference count (occupant part + 0xC)
constexpr std::size_t kExemplarPart = 0x20;  // base: the exemplar holder part
constexpr std::size_t kState = 0x32;         // base: the state byte (occupant part + 0x2E), bit 0 initialised
constexpr std::size_t kModelPart = 0x38;     // the model part's thunk part
constexpr std::size_t kVbtable = 0x3C;       // the virtual base table pointer
constexpr std::size_t kPieceKey = 0xFC;      // network occupant: the word saved before the variation
constexpr std::size_t kRotation = 0x104;     // network occupant: the rotation (low seven bits) and the flip bit
constexpr std::size_t kFlags = 0x13C;        // network occupant: the network flags
constexpr std::size_t kPathInfo = 0x140;     // network occupant: the path information (GetPathInfo)
constexpr std::size_t kPathTable = 0x144;    // network occupant: eight words, the path connection table
constexpr std::size_t kPathOverride = 0x164; // network occupant: the path ID override
constexpr std::size_t kPlain = 0x1B4;        // intermediate class: vector (begin) of four corner records; saved
constexpr std::size_t kWide = 0x1E4;         // intermediate class: vector (begin, end at +0x1E8); not saved
constexpr std::size_t kCellX = 0x23C;        // intermediate class: the piece's cell in the network's grid (int32)
constexpr std::size_t kCellZ = 0x240;        // intermediate class: the same, z
constexpr std::size_t kDirty = 0x260;        // intermediate class: byte; the subway's Read sets it to 1
constexpr std::size_t kBuilt = 0x262;        // intermediate class: byte; clear: GetPathInfo calls slot 86 first
constexpr std::size_t kClassWord = 0x264;    // intermediate class: the class ID its Init was given; saved
constexpr std::size_t kDescriptor = 0x26C;   // intermediate class: slot 101's answer, stored by its occupant Init
constexpr std::size_t kCorners = 0x270;      // the subway's own: four 0x18-byte corner records (x, y, z floats first)
constexpr std::size_t kVbaseShift = 0x2D0;   // the virtual base's displacement word: 0 (the displacement less 0x298)
constexpr std::size_t kVbase = 0x2D4;        // the virtual base (4 bytes: its vtable)
} // namespace field

// One corner record: 0x18 bytes, the network occupant's corner layout (x, y
// and z floats, four bytes, two more floats). The four records of a piece
// are corner 0 at (x0, z0), corner 1 at (x0, z0 + 16), corner 2 at (x0 + 16,
// z0 + 16) and corner 3 at (x0 + 16, z0).
constexpr std::size_t kCornerStride = 0x18;

// Byte offsets of the primary vtable's slots the subway owns or calls, for Virtual<T>(object, slot::k...).
namespace slot {
constexpr std::size_t kAddRef = 0x4, kRelease = 0x8;
constexpr std::size_t kInit = 0xC, kHasAnyNetworkFlag = 0x24, kIsPlaced = 0x54, kPieceId = 0x5C;
constexpr std::size_t kGetRotation = 0x60, kGetFlip = 0x64, kGetPathInfo = 0xC4, kReleasePathInfo = 0xC8;
constexpr std::size_t kGetAltitude = 0xE0, kGetPieceType = 0x148, kBuild = 0x158, kUseWide = 0x18C;
constexpr std::size_t kGetDescriptor = 0x194;
constexpr std::size_t kGetSeven = 0x198, kGetDefaultFlags = 0x1A4, kUpdateCorners = 0x1A8;
constexpr std::size_t kOccupantInit = 0xC, kIsInitialized = 0x14, kGetType = 0x1C; // the occupant part's
constexpr std::size_t kWrite = 0xC, kRead = 0x10, kGetGZCLSID = 0x14;              // the serializable part's
constexpr std::size_t kHolderRead = 0x60; // the holder's slot 24: the thunk (this - 4) to Read
} // namespace slot

// The virtual base's slot 17 (the piece key; the virtual base is at +0x2D4).
constexpr std::size_t kVbaseGetPieceKey = 0x44;

// The statics 0x00653640 sets up once: ten floats from +0 (see InitStatics) and the byte at +0x28.
constexpr std::size_t kStaticsDone = 0x28;

// Logs a function's first call once ("subwayocc: first <name>"; defined in
// subwayocc_sim.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::subwayocc_sim
