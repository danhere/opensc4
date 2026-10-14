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

// === Part r2: the type rows' rule files (src/core/nettool_rul_2.cpp) ==========
// A row's vector +0x24 holds pointers to rule file objects: AddRuleFile
// 0x0063D360 makes each (operator new 0x10, the constructor 0x00622A90, the
// load 0x00623020 with a resource key from LoadTypeInfo's table, two a row
// but row 5), the row's Clear 0x006421D0 deletes them by their slot 0
// (0x00622AE0, flag 1), and the tile solver reads them (its loop at
// 0x00635D70 over the 13 rows hands each to 0x006358C0, 0x006340D0 under
// it, just before the guarded RUL2 engine 0x00634D70). Not the RUL2 multimap (0x00B466D0: its own nodes, freed by
// 0x00624CE0 without vectors), not the RUL1 map (0x00B460DC, a hash set per
// node) and not the row's table +0x3C (exemplars).
//
// The object: +0x0 the vtable 0x00AA8338 (one slot, the deleting
// destructor), +0x4 an STLport multimap (nettool_sim.h's `tree`: the head
// node, the count at +0x8) keyed by a word, nodes of 0x34 bytes with a
// 0x24-byte value at +0x10: the key, then an entry (below) whose edge word
// is the key's source. The text format the load reads (comma separated,
// ';' lines are comments): a first token "#Transmogrify" (or any other
// '#...'; else the file is refused), then entries: an optional "noflip" or
// "notrans", "1" and four edges, then lines "2,<position>,<four edges or
// ?>" (checks) and "3,<position>,<ID>,<turn>,<flip>" (tiles) until a token
// that is not a digit (pushed back for the next entry) or the end. With
// #Transmogrify each entry is replaced by its distinct turned and mirrored
// variants (0x00622B50).
namespace rulefile {
inline constexpr std::size_t kSize = 0x10; // what AddRuleFile allocates (+0xC unused)
inline constexpr std::size_t kVtable = 0x0, kMap = 0x4, kCount = 0x8;
} // namespace rulefile

// The multimap's node value (0x24 bytes at the node's +0x10).
namespace rulevalue {
inline constexpr std::size_t kNodeSize = 0x34, kSize = 0x24;
inline constexpr std::size_t kKey = 0x0, kEntry = 0x4;
} // namespace rulevalue

// An entry (0x20 bytes): +0x0 the four edges (a byte each: ((n / 10) << 3)
// | n % 10 of the text's number n), +0x4 a vector of checks (below), +0x10 a
// vector of tiles (below), +0x1C which of the eight variants (bit i: quarter
// turns i & 3, mirrored when i & 4) Transmogrify makes: 0xFF, "noflip" 0xF,
// "notrans" 1; Transmogrify's own entries have 1.
namespace entry {
inline constexpr std::size_t kSize = 0x20;
inline constexpr std::size_t kEdges = 0x0, kChecks = 0x4, kTiles = 0x10, kMask = 0x1C;
inline constexpr uint32_t kAllVariants = 0xFF, kNoFlip = 0xF, kNoTrans = 1;
} // namespace entry

// A check (12 bytes): +0x0 uint8 a position (0 the cell, 1 to 8 the ring
// around it, 9 to 24 the ring after; the three bytes after it are not
// written by the game's in-place appends), +0x4 the four edges a neighbour
// must have, +0x8 their mask (0xFF a byte, 0 for '?').
namespace check {
inline constexpr std::size_t kSize = 0xC, kPosition = 0x0, kEdges = 0x4, kMask = 0x8;
} // namespace check

// A tile (8 bytes): +0x0 the tile's ID, +0x4 uint8 its position (as a
// check's), +0x5 uint8 its quarter turns (low bits) and 0x80 flipped; the
// two bytes after it are not written by the game's appends.
namespace tile {
inline constexpr std::size_t kSize = 0x8, kId = 0x0, kPosition = 0x4, kTurn = 0x5;
inline constexpr uint8_t kFlipped = 0x80;
} // namespace tile

// The load's text reader (0x38 bytes, on 0x00623020's stack): +0x0 uint8
// open, +0x4 the record (a cIGZFile from SC4::OpenResourceRecord), +0x8
// uint8 what the open answered, +0xC the current line and +0x20 the current
// token (game strings: the vtable 0x00A80810, begin, end, capacity, a
// reference count), +0x34 uint8 the record is used up, +0x35 uint8 the
// token is pushed back.
namespace reader {
inline constexpr std::size_t kSize = 0x38;
inline constexpr std::size_t kOpen = 0x0, kRecord = 0x4, kOpened = 0x8;
inline constexpr std::size_t kLine = 0xC, kLineBegin = 0x10, kToken = 0x20, kTokenBegin = 0x24;
inline constexpr std::size_t kEnded = 0x34, kPushedBack = 0x35;
inline constexpr uint32_t kFirstBuffer = 8; // what the constructor allocates for each string
} // namespace reader

// === Part r3: the RUL parsers, first part (src/core/nettool_rul_3.cpp) =========
// The RUL0 lexer (on the stack of 0x00640320; the constructor 0x0063C720
// writes up to +0x42): +0x0 the vtable (0x00AA8C58, then 0x00AA8C5C: one
// slot, `ret 4`), +0x4 the text's begin, +0x8 the cursor, +0xC the text's
// end, +0x10 a pushed-back token (-1 none), +0x14 the last identifier (an
// STL string body: begin, end, capacity; 8 bytes first), +0x20 the last
// integer, +0x24 the last float, +0x2C a word (-1), +0x40 / +0x41 / +0x42
// bytes (0, 1, 0). The constructor does not write +0x20, +0x24, +0x28,
// +0x2E to +0x3F. NextToken 0x0063DDF0 answers 0 (the end, a line end or
// ';'), kInteger, kFloat, kIdentifier or the character sign-extended.
namespace lexer {
inline constexpr std::size_t kSize = 0x44; // at least: the constructor's last write is +0x42
inline constexpr std::size_t kVtable = 0x0, kBegin = 0x4, kCursor = 0x8, kEnd = 0xC, kPushed = 0x10;
inline constexpr std::size_t kIdent = 0x14, kIdentBegin = 0x14, kIdentEnd = 0x18, kIdentCapacity = 0x1C;
inline constexpr std::size_t kInteger = 0x20, kFloat = 0x24, kWord2C = 0x2C;
inline constexpr std::size_t kByte40 = 0x40, kByte41 = 0x41, kByte42 = 0x42;
inline constexpr uint32_t kFirstBuffer = 8;
inline constexpr int32_t kNoToken = -1;
inline constexpr int32_t kTokenEnd = 0, kTokenInteger = 0x100, kTokenFloat = 0x101, kTokenIdentifier = 0x102;
} // namespace lexer

// A type row's placement parameters (row +0x50, 0xAC bytes), as Load
// 0x0063CBE0 reads them from the exemplar (group 0x084344E0, type
// 0x6534284A, the row's instance): floats (the angles +0x0, +0x4, +0x8,
// +0x18, +0x1C in degrees, stored in radians), a bool +0x24, the diagonal
// byte +0x25 (0 here), uint32 vectors +0x28, +0x54, +0x60, +0xA0, a float
// vector +0x6C, words and floats. LoadParams 0x0063D460 derives the row's
// +0x100 to +0x110 from the angles +0x0, +0x8 and +0x4 (FPTAN).
namespace placement {
inline constexpr std::size_t kSize = 0xAC;
inline constexpr std::size_t kAngle0 = 0x00, kAngle1 = 0x04, kAngle2 = 0x08;
inline constexpr std::size_t kBool24 = 0x24, kDiagonal = 0x25, kVector28 = 0x28, kBool44 = 0x44;
inline constexpr std::size_t kWord50 = 0x50, kVector54 = 0x54, kVector60 = 0x60, kFloats6C = 0x6C;
inline constexpr std::size_t kFloat78 = 0x78, kFloat7C = 0x7C, kWord80 = 0x80, kWord84 = 0x84, kWord88 = 0x88;
inline constexpr std::size_t kWord8C = 0x8C, kWord90 = 0x90, kWord94 = 0x94, kFloat98 = 0x98, kWord9C = 0x9C;
inline constexpr std::size_t kVectorA0 = 0xA0;
// The plain words and floats (hashed as words) and the vectors.
inline constexpr std::size_t kScalars[] = {0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x34,
                                           0x38, 0x3C, 0x40, 0x48, 0x4C, 0x50, 0x78, 0x7C, 0x80, 0x84,
                                           0x88, 0x8C, 0x90, 0x94, 0x98, 0x9C};
inline constexpr std::size_t kBytes[] = {0x24, 0x25, 0x44};
inline constexpr std::size_t kVectors[] = {0x28, 0x54, 0x60, 0x6C, 0xA0};
} // namespace placement

// A RUL1 pair table (the value of a node of the RUL1 map 0x00B460DC, at
// the node's +0x18): a hash table (`table`) of 20-byte nodes: next, the two
// network types (the key; bucket (second + 2 * first) % buckets), a word
// and a byte (+0x10: turns | 0x80 flipped; the three bytes after it are
// stack leftovers of the RUL1 pair line callback 0x0063D080 in the
// original, zeros in ours; the solver 0x00634BD3 reads the byte only).
namespace pairtable {
inline constexpr std::size_t kNodeSize = 0x14, kFirst = 0x4, kSecond = 0x8, kValue = 0xC, kByte = 0x10;
inline constexpr std::size_t kValueSize = 0x10; // the inserted value: first, second, word, byte word
} // namespace pairtable

// The RUL1 map 0x00B460DC: a tree (`tree`) whose node value is two words
// (a type pair) and a pair table.
namespace rul1map {
inline constexpr std::size_t kFirst = 0x10, kSecond = 0x14, kTable = 0x18;
} // namespace rul1map

// A type row's table +0x3C: 32-byte nodes: next, a key, a uint32 vector
// (+0x8), a float vector (+0x14). The inserted value is the key and the
// two vectors (0x1C bytes).
namespace table3c {
inline constexpr std::size_t kNodeSize = 0x20, kKey = 0x4, kUints = 0x8, kFloats = 0x14;
inline constexpr std::size_t kValueUints = 0x4, kValueFloats = 0x10;
} // namespace table3c

// A hash table of 20-byte nodes: next, a key, a point vector (+0x8)
// (0x0063D7E0; the value is the key and the vector).
namespace pointtable {
inline constexpr std::size_t kNodeSize = 0x14, kKey = 0x4, kPoints = 0x8;
} // namespace pointtable

// A RUL0 check type's value as FindOrInsert 0x0063C890 inserts it (0x1C
// bytes: the key byte, three bytes the game leaves unwritten, six words).
namespace checkvalue {
inline constexpr std::size_t kSize = 0x1C, kKey = 0x0, kWords = 0x4, kWordCount = 6;
} // namespace checkvalue

// The string-keyed hash map of the bridge loader (0x00640E10; nodes of
// 0x30 bytes: next, then the pair): the pair (0x2C bytes) is a game string
// (+0x0), a vector of 20-byte elements (+0x14) and a float vector (+0x20);
// the value FindOrInsert hands out is the pair's +0x14. Every node goes to
// bucket 0 (the game's hash is 0).
namespace stringmap {
inline constexpr std::size_t kNodeSize = 0x30, kPair = 0x4;
inline constexpr std::size_t kPairSize = 0x2C, kKey = 0x0, kVector20 = 0x14, kFloats = 0x20;
inline constexpr std::size_t kValueFloats = 0xC; // the value's float vector (after the 20-byte vector)
// The 20-byte elements (the bridge parser 0x0063EA50 builds them): a float,
// a word, a byte at +0x8 (the three bytes after it are that parser's stack
// leftovers, copied along as words), two words.
inline constexpr std::size_t kElement = 0x14;
} // namespace stringmap

// The int-keyed map of a bridge record (+0x16C; 0x0063DAE0 to 0x0063DD30):
// a tree (`tree`, the count at +0x4) of 0x2C-byte nodes whose value
// (+0x10, 0x1C bytes) is a signed key, a vector of 20-byte elements (+0x14)
// and a float vector (+0x20).
namespace intmap {
inline constexpr std::size_t kNodeSize = 0x2C, kKey = 0x10, kVector20 = 0x14, kFloats = 0x20;
inline constexpr std::size_t kValueVector20 = 0x4, kValueFloats = 0x10, kValueSize = 0x1C;
inline constexpr std::size_t kColor = 0x0;
} // namespace intmap

// A bridge record (0x178 bytes, a type row's vector +0x30): what part r3
// can read of it: a game string at +0x158, the int-keyed map at +0x16C.
namespace bridge {
inline constexpr std::size_t kName = 0x158, kIntMap = 0x16C;
} // namespace bridge

// The RUL2 multimap's node (0x34 bytes) and value (0x24 bytes).
namespace rul2node {
inline constexpr std::size_t kNodeSize = 0x34, kValueSize = 0x24, kValueWords = 9;
} // namespace rul2node

} // namespace opensc4::nettool_sim::rul
