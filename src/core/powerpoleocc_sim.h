// Our cSC4PowerPoleOccupant (build 641): the class map, the object's fields
// and the vtables' slots. A power pole is the tower that carries a power
// line over the land: one object per tower, 2 to 10 of them in a city's
// save (Making Money 4, Rush Hour 10). It knows where it stands, which other
// towers its wires run to (a list of records, one for each neighbour), the
// geometry of those wires (the points they pass through, as vectors of
// vectors of three floats) and four corner vectors of the tower's model. The
// occupant base (cSC4Occupant, src/core/occbase_sim.h) starts at +8, because
// the class has a message target at +4 besides its interface at +0.
//
// The first piece (src/core/powerpoleocc_sim.cpp) is the class's life and
// save, and the simple state and queries: Write, Read, GetGZCLSID,
// QueryInterface, the occupant part's Init, the message handler, the
// constructor 0x0064DB30, the destructor 0x0064DFC0, the deleting
// destructor 0x0064E380, the factory 0x0064E340 and the pool's allocation
// 0x006499D0, the bounding box and the small getters and setters. Not ours,
// the drawing code and what is mixed with it (see docs/powerpoleocc.md): the
// builders of the tower's model and of the wires' geometry (0x0064AC70,
// 0x0064AD90, 0x0064BFD0, 0x0064C2C0, 0x0064C470, 0x0064C800, the spline
// 0x006493B0 and its callers), the highlight (0x006491E0, 0x0064AA40), the
// model-instance list (0x00649D00) and the lighting-like drawing 0x0064B750.
//
// Class map
//   class ID 0x09C05C6A, occupant type 0x2890D4DE (what the occupant part's
//   GetType answers). 0x124 bytes from a pool (0x00B0C0C8: pages of 32 KB at
//   address & 0xFFFF8000, 112 items each, the free list at +8 of a page and
//   its used count at +0xC). Virtual inheritance: the model part at +0x3C
//   (the same part the network occupant has), a table pointer at +0x40 and a
//   virtual base at +0x120 (the constructor stores the abstract class's
//   vtable there while it runs, the destructor does the same on the way
//   out).
//
//   +0x00 the class's interface, vtable 0x00AA9EB0, 19 slots
//   +0x04 the message target (cIGZMessageTarget2), vtable 0x00AA9EA0, 4 slots
//   +0x08 the occupant part (cSC4Occupant starts here), vtable 0x00AA9E18,
//         34 slots (its own: Init, Shutdown, GetPosition, SetPosition,
//         GetBoundingBox, SetHighlight)
//   +0x0C cIGZSerializable, vtable 0x00AA9DFC (Write 0x0064B010, Read
//         0x0064EA00, GetGZCLSID 0x0064DD50); Write and Read get this + 0xC
//   +0x10 the property holder, vtable 0x00AA9D98
//   +0x24 the exemplar holder part, vtable 0x00AA9D80
//   +0x3C the model part, vtable 0x00AA9D7C
//   +0x120 the virtual base, vtable 0x00AA9D30
//
// The interface's slots (vtable 0x00AA9EB0; counts from the callee's `ret N`):
//    0 QueryInterface 0x0064A030 (2)         3 stub 0x006C2040 (shared)
//    4 GetRecordCount 0x00649D80 (0)         5 0x0064BE50 (2; the points of a wire)
//    6 0x0064DAE0 (1)  7 GetRecordTarget 0x006499B0 (1)
//    8 GetCellPair 0x00649210 (2)            9 0x0064A650 (0)
//   10 0x0064DD60 (0)                       11 IsFlag112 0x00649230 (0)
//   12 0x0064AA40 (1; the highlight model)   13 GetByte118 0x00649240 (0)
//   14 SetByte118 0x00649250 (1)             15 Init 0x0064CE40 (2)
//   16 FindRecord 0x00649DD0 (2)             17 0x00649D00 (5; the model list)
//   18 0x0064C6C0 (1)
// The message target: slot 0 the QueryInterface thunk, 1 and 2 AddRef and
// Release thunks, 3 DoMessage 0x0064A140 (1). The occupant part's slots 3 Init
// 0x00649FA0 (0), 4 Shutdown 0x0064BC80 (0), 8 GetPosition 0x00649DA0 (1),
// 9 SetPosition 0x0064C540 (1), 10 GetBoundingBox 0x00649A00 (2) and 17
// SetHighlight 0x006491E0 (2).
//
// Records (the vector at +0xAC, begin, +0xB0 end, +0xB4 capacity; 0x2C
// bytes each): +0x00 the other tower (a pointer, saved as a reference to a
// 0x09C05C6A object), +0x04 a list header (a circular doubly linked list of
// nodes, 12 bytes each: next, previous, a pointer to an object saved as a
// reference to class 0xC9C05C5D at the object + 8), +0x08 and +0x09 two
// bools, +0x0C and +0x10 two int32 (a cell: x and z), +0x14 and +0x20 two
// vectors of vectors of three floats (12 bytes each: begin, end, capacity).
// The three corner vectors at +0xE8, +0xF4 and +0x100 hold 0x18-byte
// records like the network occupant's corners.
#pragma once

#include "core/game_access.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::powerpoleocc_sim {

constexpr uint32_t kClassId = 0x09C05C6A;
constexpr uint32_t kOccupantType = 0x2890D4DE;
constexpr uint32_t kIidInterface = 0xA8C86142;    // the interface's IID (QueryInterface answers it and the class ID)
constexpr uint32_t kIidTarget = 0x452294AA;       // the message target at +4
constexpr uint32_t kIidResource = 0x26A68448;     // asked of the object at +0xBC when there is one
constexpr uint32_t kIidReference = 0xA9764C8C;    // answers like the interface
constexpr uint32_t kClassWireTarget = 0xC9C05C5D; // class of the list items saved in a record
constexpr uint32_t kMessageOccupant = 0x99EF1142; // the message the object listens to (DoMessage)
constexpr std::size_t kObjectSize = 0x124;

// Fields (offsets from the object's start; the base's are in
// src/core/occbase_sim.h, 8 higher than the base's own). Evidence: the
// constructor, Write, Read and the small functions.
namespace field {
constexpr std::size_t kMessageTarget = 0x04;  // the message target part
constexpr std::size_t kOccupant = 0x08;       // base: the cISC4Occupant part (the cSC4Occupant starts here)
constexpr std::size_t kSerializable = 0x0C;   // the cIGZSerializable part (Write, Read, GetGZCLSID)
constexpr std::size_t kHolder = 0x10;         // base: the property holder
constexpr std::size_t kExemplarPart = 0x24;   // base: the exemplar holder part
constexpr std::size_t kType = 0x2C;           // base: the occupant type
constexpr std::size_t kModelPart = 0x3C;      // the model part (constructor 0x00801CF0, destructor 0x00801810)
constexpr std::size_t kVbtable = 0x40;        // the virtual base table: [0] -4, [1] the virtual base's displacement
constexpr std::size_t kHighlight = 0x54;      // uint8 bit 0: highlighted (SetHighlight, saved)
constexpr std::size_t kHighlightValue = 0x55; // uint8 the highlight value (SetHighlight, saved)
constexpr std::size_t kFloat64 = 0x64;        // float, saved (Init sets it to 1.0)
constexpr std::size_t kTransform = 0x68;      // the 0x38-byte transform: bytes +0 and +1, matrix +4, translation +0x28
constexpr std::size_t kMatrix = 0x6C;         // 3 by 3 floats, the transform's
constexpr std::size_t kTranslation = 0x90;    // three floats, the transform's
constexpr std::size_t kScale = 0x9C;          // float, the transform's
constexpr std::size_t kPosition = 0xA0;       // x, y, z (saved; GetPosition, SetPosition)
constexpr std::size_t kRecords = 0xAC;        // vector of 0x2C-byte records: begin, +4 end, +8 capacity
constexpr std::size_t kCount = 0xB8;          // uint32, saved after the records (constructor: 0)
constexpr std::size_t kResource = 0xBC;       // referenced object (Init and Read release it), QueryInterface 0x26A68448
constexpr std::size_t kInts = 0xC0;           // four int32, saved: -1, -1, 0, 0 from the constructor
constexpr std::size_t kCellPair = 0xC8;       // the last two of them (GetCellPair)
constexpr std::size_t kBoundsDirty = 0xD0;    // uint8: the cell bounds below are to be worked out again
constexpr std::size_t kBounds = 0xD4;         // uint32 x4: lowest x, highest x, lowest z, highest z (cells; +0xD4,
                                              // +0xDC, +0xD8, +0xE0, see GetBoundingBox)
constexpr std::size_t kTable = 0xE4;          // pointer to a table of four vectors of three floats (not saved)
constexpr std::size_t kCorners0 = 0xE8;       // corner vector (0x18-byte records), saved
constexpr std::size_t kCorners1 = 0xF4;
constexpr std::size_t kCorners2 = 0x100;
constexpr std::size_t kFloat10C = 0x10C;      // float, saved
constexpr std::size_t kBool110 = 0x110;       // bool, saved
constexpr std::size_t kListening = 0x111;     // uint8: registered with the message server for 0x99EF1142
constexpr std::size_t kByte112 = 0x112;       // uint8, saved (IsFlag112)
constexpr std::size_t kHighlightModel = 0x114; // referenced object, made by the highlight (not saved)
constexpr std::size_t kByte118 = 0x118;       // uint8 (GetByte118, SetByte118; not saved)
} // namespace field

// Byte offsets of the interface's slots, for Virtual<T>(object, slot::k...).
namespace slot {
constexpr std::size_t kAddRef = 0x04, kRelease = 0x08, kGetRecordCount = 0x10, kGetWirePoints = 0x14;
constexpr std::size_t kGetRecordTarget = 0x1C, kGetCellPair = 0x20, kSlot9 = 0x24, kIsFlag112 = 0x2C;
constexpr std::size_t kGetByte118 = 0x34, kSetByte118 = 0x38, kFindRecord = 0x40;
} // namespace slot

// Offsets of the occupant part's slots, for Virtual<T>(object + kOccupant, occupant::k...).
namespace occupant {
constexpr std::size_t kInit = 0x0C, kIsInitialized = 0x14, kGetType = 0x1C, kGetPosition = 0x20;
constexpr std::size_t kGetBoundingBox = 0x28, kGetFlags = 0x84;
} // namespace occupant

// Logs a function's first call once ("powerpoleocc: first <name>"; defined
// in powerpoleocc_sim.cpp).
void FirstCall(const char* name, bool& logged);

} // namespace opensc4::powerpoleocc_sim
