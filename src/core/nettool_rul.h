// The network tool's rule statics (build 641; part r1): the layouts of the
// RUL0 rule object, the RUL0 store, the RUL2 override multimap and the rows
// of the type table that the rule loader fills, shared by our translation
// (src/core/nettool_rul_1.cpp, whose top says what stays the game's, NAM,
// floats and registers) and its test (src/core/nettool_rul_autotest.cpp).
//
// Everything here lives in the game's memory with the game's layout: NAM's
// DLL reads the RUL0 rules (and edits them in place), the store, the
// multimap and the type rows by these offsets.
#pragma once

#include "core/nettool_sim.h"

#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim::rul {

// === A hash table as the game's STLport keeps it =============================
// +0x0 not written by the code here (inf: the hash functor), +0x4 the bucket
// vector (begin, end, capacity), +0x10 the element count. A node starts with
// the next node of its bucket; the key follows at +0x4.
namespace table {
inline constexpr std::size_t kBuckets = 0x4, kBucketsEnd = 0x8, kBucketsCapacity = 0xC, kCount = 0x10;
inline constexpr std::size_t kSize = 0x14;
inline constexpr std::size_t kNext = 0x0, kKey = 0x4;
// What the constructors give the bucket vector (SC4Uint32HashSet::Construct).
inline constexpr uint32_t kFirstBuckets = 100;
} // namespace table

// === The RUL0 rule (0xFC bytes) =============================================
// Evidence: the constructor 0x0062A480, the copy constructor 0x0062A1D0, the
// destructor 0x00626160, the two layout parsers 0x00609FC0 / 0x0060A560 and
// the transforms 0x00608D20, 0x006091F0, 0x006094B0. "(inf)" where the role
// is inferred.
//   +0x00 the rule ID (the RUL0 loader writes it; the constructor does not)
//   +0x04 a vector of 16-byte records: +0x0 a word, +0x4 uint8 a quarter
//         turn (0..3), +0x5 uint8 a flip, +0x8 / +0xC two floats (x, z)
//   +0x10, +0x14 words (0 from the constructor)
//   +0x18 / +0x1C the smallest x / z of the layout, +0x20 / +0x24 the largest
//         (ParseCellLayout; not written by the constructor)
//   +0x28 a vector of 8-byte points ('+' cells), +0x34 another (not filled
//         here), +0x4C a vector of 8-byte points (the checked cells: '+' and
//         the check types whose byte +0x1D is set)
//   +0x40, +0x44 words, +0x48 uint8 (copied; the first two not written by
//         the constructor)
//   +0x58 a vector of 12-byte cells: x, z, the check type's character (one
//         byte; the three bytes after it are not written)
//   +0x64 the check types: a hash table keyed by the character (32-byte
//         nodes: +0x4 the character, +0x8 / +0x9 two signed network types,
//         +0xC / +0x10 the first type's edge word and mask, +0x14 / +0x18 the
//         second's, +0x1D uint8 the cell is checked)
//   +0x78 a word (0), +0x7C a word (not written by the constructor)
//   +0x80 a string (the game's 0x14-byte string: vtable 0x00A80810, the
//         characters' begin, end, capacity, a reference count)
//   +0x94, +0x98 an offset in x and z (the transforms move and turn it)
//   +0x9C the cons grid (vector<uint8>, ParseConsLayout: 1 to 8 for
//         - | / \ F R X #), +0xA8 its width, +0xAC / +0xB0 the anchor's
//         column and row in it (-1 when none)
//   +0xB4 a word (0)
//   +0xB8 the cell grid (vector<uint8>: for each checked point (z << 4) +
//         x + 1), +0xC4 its width (not written by the constructor)
//   +0xC8, +0xCC words (0)
//   +0xD0 the cons cells: a hash table keyed by the grid value (16-byte
//         nodes: +0x4 the value, +0x8 the word ((x << 4) + z) << 4, +0xC
//         uint8 0 (a turn), +0xD uint8 1 unless the point is also a cell)
//   +0xE4 a word, +0xE8 uint8 the rotation (Rotate adds 2 per quarter turn,
//         Transpose reflects it; 0x006097E0 indexes 0x00AA752C with it),
//         +0xE9 uint8, +0xEC to +0xF8 four words (-1 from the constructor)
namespace rul0 {
inline constexpr std::size_t kSize = 0xFC;
inline constexpr std::size_t kId = 0x00, kRecords = 0x04, kWord10 = 0x10, kWord14 = 0x14;
inline constexpr std::size_t kMinX = 0x18, kMinZ = 0x1C, kMaxX = 0x20, kMaxZ = 0x24;
inline constexpr std::size_t kPoints = 0x28, kPoints34 = 0x34, kWord40 = 0x40, kWord44 = 0x44, kByte48 = 0x48;
inline constexpr std::size_t kChecked = 0x4C, kCells = 0x58, kCheckTypes = 0x64, kWord78 = 0x78, kWord7C = 0x7C;
inline constexpr std::size_t kText = 0x80, kOffsetX = 0x94, kOffsetZ = 0x98;
inline constexpr std::size_t kConsGrid = 0x9C, kConsWidth = 0xA8, kConsAnchorX = 0xAC, kConsAnchorZ = 0xB0;
inline constexpr std::size_t kWordB4 = 0xB4, kCellGrid = 0xB8, kCellWidth = 0xC4, kWordC8 = 0xC8, kWordCC = 0xCC;
inline constexpr std::size_t kConsCells = 0xD0, kWordE4 = 0xE4, kRotation = 0xE8, kByteE9 = 0xE9;
inline constexpr std::size_t kWordsEC = 0xEC; // four words
// The string at +0x80: the game's string vtable, then begin, end, capacity
// and the reference count (the copy constructor's CopyBuffer takes +0x84).
inline constexpr std::size_t kTextBegin = 0x84, kTextEnd = 0x88, kTextCapacity = 0x8C, kTextReferences = 0x90;
inline constexpr uint32_t kFirstTextBuffer = 8; // what the constructor allocates
// The bounds ParseCellLayout starts from.
inline constexpr int32_t kStartMin = 1000, kStartMax = -1000;
} // namespace rul0

namespace record {
inline constexpr std::size_t kSize = 0x10, kWord = 0x0, kTurn = 0x4, kFlip = 0x5, kX = 0x8, kZ = 0xC;
} // namespace record

namespace point {
inline constexpr std::size_t kSize = 0x8, kX = 0x0, kZ = 0x4;
} // namespace point

namespace cellrec {
inline constexpr std::size_t kSize = 0xC, kX = 0x0, kZ = 0x4, kKey = 0x8;
} // namespace cellrec

namespace checktype {
inline constexpr std::size_t kNodeSize = 0x20, kTypes = 0x8, kEdges = 0xC, kMasks = 0x10, kSide = 0x8;
inline constexpr std::size_t kChecked = 0x1D;
inline constexpr std::size_t kCopied = 0x8, kCopiedWords = 6; // what CopyCheckTypes copies after the key
} // namespace checktype

namespace conscell {
inline constexpr std::size_t kNodeSize = 0x10, kValue = 0x8, kTurn = 0xC, kAlone = 0xD;
} // namespace conscell

// === The RUL0 store (0x00B452B0) ============================================
// A hash table (above) of 0x104-byte nodes: +0x0 next, +0x4 the rule ID,
// +0x8 the rule. FindOrInsert 0x0062C2F0 builds a node's rule from a pair
// (the key and a rule) on its stack.
namespace store {
inline constexpr std::size_t kNodeSize = 0x104, kRule = 0x8;
inline constexpr std::size_t kPairRule = 0x4, kPairSize = 0x100;
} // namespace store

// === The RUL2 override multimap (0x00B466D0) =================================
// A tree as nettool_sim.h's `tree` (the head node, the count at +0x4); a
// node's value at +0x10 is 0x24 bytes: the key (the first rule ID), then
// four pairs (a rule ID word, a byte of rotation and flip at +0x4 whose
// three following bytes the line callback leaves unwritten).
namespace rul2 {
inline constexpr std::size_t kCount = 0x4;
inline constexpr std::size_t kKey = 0x10, kPairs = 0x14, kPairSize = 0x8, kPairCount = 4, kPairByte = 0x4;
} // namespace rul2

// === A row of the type table (0x00B452C8, 13 rows of 0x114 bytes) ===========
// What the static constructor 0x00641F70, the destructor 0x00643510 and the
// loaders write (nettool_sim.h's `row` has what the tool reads):
//   +0x00 a word (0, 1 or 2), +0x04 uint8 loaded (0x0063BF90)
//   +0x08 to +0x20 seven words: the view input control ID, a word, the tool
//         view ID, the cursor ID, the directional cursor base, the pen width
//         in x and z
//   +0x24 a vector of rule file objects (0x0063D360 appends)
//   +0x30 the bridge records (0x178 bytes each), +0x3C a hash table (100
//         buckets), +0x50 the placement parameters (0xAC bytes)
//   +0x75 uint8 (the parameters' +0x25, the diagonal flag: LoadTypeInfo sets
//         it for row 3, the street)
//   +0xFC the word ReadRul1TypeIds gives the row
//   +0x100 to +0x110 five floats (the constructor's 10, 7.0711, 10, 7.0711,
//         20; LoadParams derives them)
namespace typerow {
inline constexpr std::size_t kWord0 = 0x00, kLoaded = 0x04, kIds = 0x08, kIdCount = 7;
inline constexpr std::size_t kFiles = 0x24, kBridges = 0x30, kTable = 0x3C, kParams = 0x50;
inline constexpr std::size_t kDiagonal = 0x75, kRul1Id = 0xFC, kFloats = 0x100, kFloatCount = 5;
inline constexpr std::size_t kBridgeSize = 0x178;
} // namespace typerow

} // namespace opensc4::nettool_sim::rul
