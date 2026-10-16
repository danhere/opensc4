// The network tool's major bridges (build 641; parts t10 and t11): the
// layouts the bridge code reads and writes. Part t10 is
// src/core/nettool_bridge_a.cpp (the bridge cells' build, the bridge pieces'
// occupants, the cell vector's growth and the float sort the cable geometry
// uses); part t11 is src/core/nettool_bridge_b.cpp. Every structure here
// stays in the game's memory with the game's layout (NAM's DLL reads the
// tool and the occupants by layout).
//
// What a major bridge is: when a drag crosses water (or a gap the type's
// bridge records allow), the tool keeps a 0x28-byte element in its vector
// +0xD8 per bridge: the steps it spans and the record (one of the type
// row's 0x178-byte bridge records, +0x30) it is built from. Its cells (the
// vector at +0x1C, 0x50 bytes each) are the bridge's pieces: a ramp piece
// at each end, pier pieces at the record's spacing and span pieces between,
// each with its piece ID, turn and heights. The placer (part t6) hands the
// bridges to PlaceMajorBridgeOccupants 0x0062F610, which makes a bridge
// occupant (class 0x49CC1BCD, src/core/bridgeocc_sim.cpp) per cell.
#pragma once

#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim::bridges {

// A major bridge (the tool's +0xD8, 0x28 bytes): +0x0 a word (inf: the
// network type, GetCostOfSolution's), +0x4 the first step, +0x8 the last
// step (inclusive), +0xC the index of its record in the type row's bridge
// records, +0x10 the deck altitude (float), +0x14 the reference altitude,
// +0x18 a byte (land bridge; the getters' -0x10), +0x1C the cell vector
// (begin, end, capacity).
namespace major {
inline constexpr std::size_t kSize = 0x28, kWord0 = 0x0, kFirst = 0x4, kLast = 0x8, kRecord = 0xC;
inline constexpr std::size_t kAltitude = 0x10, kReferenceAltitude = 0x14, kLandBridge = 0x18;
inline constexpr std::size_t kCells = 0x1C, kCellsEnd = 0x20, kCellsCapacity = 0x24;
} // namespace major

// A bridge cell (0x50 bytes; copy constructor 0x006267D0, which copies the
// words, the bytes +0x10 and +0x40 alone and the part vector through
// 0x00624EA0):
//   +0x00 the kind: 0 a ramp piece (the record's +0x14 pieces), 1 a pier
//         piece (+0x44), 2 a span piece (+0x2C)
//   +0x04 x, +0x08 z (the dragged cell)
//   +0x0C the piece ID, +0x10 its turn (a byte, 0 to 3)
//   +0x14 the four corner heights (floats; the height pass of part t9 reads
//         them)
//   +0x24 the deck height (the bridge occupant's model offset is it less
//         the occupant's y)
//   +0x28 a float: the pier's depth below the deck (-1.0 none; the record's
//         +0x20 / +0x50 floats)
//   +0x2C four floats and +0x3C a float (the record's +0xA4), +0x40 a flag
//         byte: the side walls' shape, read only by the wall geometry
//         0x0062DF30 (model geometry through AddPart, whose triangle records the
//         occupant saves) when +0x40 is not 0; 0x0062E690 raises the
//         four floats along the arch or suspension curve
//   +0x44 a vector of 32-byte parts (the cables' pieces in this cell, made
//         by 0x006307A0 from the record's cable points; each becomes four
//         AddPart triangles of the occupant's model)
namespace cell {
inline constexpr std::size_t kSize = 0x50, kKind = 0x0, kX = 0x4, kZ = 0x8, kPiece = 0xC, kTurn = 0x10;
inline constexpr std::size_t kHeights = 0x14, kDeck = 0x24, kPierDepth = 0x28;
inline constexpr std::size_t kWallScales = 0x2C, kWallFloat = 0x3C, kWallFlags = 0x40;
inline constexpr std::size_t kParts = 0x44, kPartsEnd = 0x48, kPartsCapacity = 0x4C;
inline constexpr uint32_t kKindRamp = 0, kKindPier = 1, kKindSpan = 2;
inline constexpr uint32_t kNoPierBits = 0xBF800000; // -1.0f
} // namespace cell

// A cable part (0x20 bytes, the cell's +0x44): two points (x, y, z), a half
// height (+0x18) and the material (+0x1C) AddPart gets.
namespace part {
inline constexpr std::size_t kSize = 0x20, kFrom = 0x0, kTo = 0xC, kHalfHeight = 0x18, kMaterial = 0x1C;
} // namespace part

// What the bridge code reads of a bridge record (0x178 bytes, the type
// row's +0x30; nettool_rul.h's `bridgefields` has the loader's view):
//   +0x0C the cells across (the pieces per step), +0x10 the ramp length in
//         steps
//   +0x14 the ramp pieces (a vector of 8-byte {piece, turn byte}), +0x20
//         their pier depths (floats, one per step)
//   +0x2C the span piece, +0x30 its turn (a byte)
//   +0x34 the step count per pier (the spacing's divisor), +0x38 and +0x7C
//         two step counts the pier run keeps off each end, +0x3C a float
//         (the pier spacing's fit), +0x40 the pier length in steps
//   +0x44 the pier pieces (8-byte {piece, turn byte}), +0x50 their depths
//   +0x60 a float per ramp step and the next (the ramp's corner drops),
//         +0x6C a float per ramp step (the deck drop)
//   +0x80, +0x8C the pier walls' corner offsets (four (x, z) pairs per
//         turn; +0x8C for pier pieces, +0x80 else)
//   +0x9C, +0xA0 two floats 0x0062E690 gets (the curve's shapes), +0xA4 the
//         wall float, +0xA8 / +0xAC the fewest and most piers, +0xB0 /
//         +0xB4 two words (both non-zero: the arch / suspension walls)
//   +0xC4 the pier piece's extra deck height, +0xD0 the passage ceiling's
//         drop, +0xD4 the deck's height over the bridge altitude
//   +0xD8 a byte: cables; +0xDC, +0xE0 the cables' half height and
//         material, +0xE4 to +0x140 eight cable points (x, y, z), +0x144
//         the subdivisions per span
namespace record {
inline constexpr std::size_t kAcross = 0x0C, kRampSteps = 0x10, kRampPieces = 0x14, kRampDepths = 0x20;
inline constexpr std::size_t kSpanPiece = 0x2C, kSpanTurn = 0x30, kPierDivisor = 0x34, kKeepOff38 = 0x38;
inline constexpr std::size_t kPierFit = 0x3C, kPierSteps = 0x40, kPierPieces = 0x44, kPierDepths = 0x50;
inline constexpr std::size_t kRampDrops = 0x60, kDeckDrops = 0x6C, kKeepOff7C = 0x7C;
inline constexpr std::size_t kWallOffsets = 0x80, kPierWallOffsets = 0x8C;
inline constexpr std::size_t kCurve9C = 0x9C, kCurveA0 = 0xA0, kWallFloat = 0xA4;
inline constexpr std::size_t kFewestPiers = 0xA8, kMostPiers = 0xAC, kWallsB0 = 0xB0, kWallsB4 = 0xB4;
inline constexpr std::size_t kPierDeck = 0xC4, kCeilingDrop = 0xD0, kDeckHeight = 0xD4;
inline constexpr std::size_t kCables = 0xD8, kCableHalfHeight = 0xDC, kCableMaterial = 0xE0;
inline constexpr std::size_t kCablePoints = 0xE4, kCableSubdivisions = 0x144;
inline constexpr std::size_t kPieceEntry = 0x8, kPieceTurn = 0x4; // the 8-byte {piece, turn} entries
} // namespace record

} // namespace opensc4::nettool_sim::bridges
