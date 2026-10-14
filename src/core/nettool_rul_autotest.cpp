// The network tool's rule statics in the autotest (part r1;
// src/core/nettool_rul_1.cpp has what is translated, src/core/nettool_rul.h
// the layouts).
//
// OPENSC4_TEST_NETTOOL_RUL=<day>. While it is set, every day's fingerprint
// line gets ` | ntrul s<count>:<hash> m<count>:<hash> t<hash>`, a read-only
// checksum of what the rule load builds:
//   s  the RUL0 store 0x00B452B0: its count, its bucket count and the sum
//      (order-free) over its nodes of the key and the rule's values (below);
//   m  the RUL2 override multimap 0x00B466D0: its count and the sum over its
//      nodes of the key, the four rule IDs and their rotation bytes;
//   t  the 13 type table rows: the word +0x0, the loaded byte, the IDs +0x8
//      to +0x20, the diagonal byte +0x75, the RUL1 word +0xFC, the five floats
//      +0x100 to +0x110 (bits), the numbers of rule files and bridge records.
// A rule's values: the ID; the records (word, turn, flip, float bits); the
// words +0x10, +0x14; the point vectors; the byte +0x48; the cells (x, z,
// the key byte, not the three bytes after it); the check types (count,
// buckets, the sum over nodes of the key, the two type bytes, the edge word
// and mask of each side whose type is not negative, the byte +0x1D); the
// word +0x78; the string's characters; the offset; the cons grid with its
// width and anchor; +0xB4; the cell grid; +0xC8, +0xCC; the cons cells
// (count, buckets, the sum over nodes of the key, the word and the two
// bytes); +0xE4, +0xE8, +0xE9, +0xEC to +0xF8. The bounds +0x18 to +0x24 and
// the width +0xC4 only when the cell grid is not empty (a rule without a cell
// layout keeps the stack leftovers of FindOrInsert's empty rule there in
// the original), and never +0x40, +0x44, +0x7C (same reason). No address is
// hashed; tables and the multimap are summed, not walked in order.
//
// On the given day the drive (` d<hash>` on that day's line and after)
// calls part r1's functions at the game's addresses (addr::...), so an A
// run runs the original and a B run whatever is installed, on the test's
// own memory only:
//   MirrorEdgeWord on 64 words (fixed ones and an LCG's);
//   FindOrInsert with the smallest key of the store (an existing key: no
//     insertion), the answer compared with that node's rule;
//   for each of the (at most) 48 rules of smallest key that have a cell
//     grid: CopyConstruct into a buffer on the test's stack (the copies under
//     it: CopyRecordVector, CopyRecords, CopyCheckTypes, CopyConsCells),
//     Rotate by 1 three times, Transpose, Translate by (3, -2), Rotate by 2
//     (RotatePoints, RotateCells, RotateGrid, TransposeGrid, MirrorEdgeWord
//     under them), the copy hashed after each step, then Destruct.
// Rules without a cell grid are not transformed: their grid width +0xC4 is
// a stack leftover in the original, and the grid code loops that many
// columns. The layout parsers, the load, the store's Clear and InsertNode
// and the erasers run at every city load (the first network tool Init after
// a resource load), so the daily checksum covers them; the pair table's Find
// (0x00633970) and the static constructor are not driven.
//
// Part r2 (src/core/nettool_rul_2.cpp) appends, on every day,
// ` f<files>:<hash>`: the type rows' rule files (+0x24), each multimap
// walked in order (its order comes from the insertion order, not from
// addresses): per node the key, the edges, the checks (position, edges,
// mask; not the three bytes after the position), the tiles (ID, position,
// turn byte; not the two bytes after them) and the mask. From the drive day
// on, ` e<hash>` after ` d`: part r2's functions called at the game's
// addresses on the test's own memory: MirrorEdges and RotateEdges on 64
// words (turns 0 to 3, 5 and -1), RotateCheck and RotateTile on made-up
// records (positions 0 to 31, turns 0 to 3, flipped or not); for the first
// 48 entries of row 0's first rule file: Mirror into an empty entry, Rotate
// that by 0 to 3, Equals both ways, the made entries' buffers freed; a local
// multimap (Construct) given copies of its first 32 entries with mask 0xFF
// (Insert, CreateNode), Transmogrify into a second one (InsertRange, the
// vectors' copies, assignments and growth, EraseSubtree under it), both
// destroyed (Destruct); and the same RUL file read again into a new rule
// file object (operator new, Construct, Load: the reader and the three
// parsers under it), compared with the loaded one and deleted by its slot 0.
//
// Part r3 (src/core/nettool_rul_3.cpp) appends, on every day, after
// everything above, ` p<hash> x<nodes>:<hash> r<count>:<hash>
// b<records>:<hash>/<hash>`: p the 13 rows' placement parameters (+0x50:
// the words and float bits, the bytes +0x24, +0x25, +0x44, the five
// vectors; the placement Load and LoadParams fill them); x the rows' tables
// +0x3C (count, bucket count, the order-free sum over nodes of the key and
// the two vectors); r the RUL1 map 0x00B460DC in key order (each node's type
// pair and its pair table: count, bucket count, the order-free sum over
// nodes of the two types, the word and the turn byte, not the three bytes
// after the byte: the original's stack leftovers); b the rows' bridge
// records in order (the name's characters and the int-keyed map +0x16C in
// key order: key, floats, the element count), the 20-byte elements in the
// hash after the slash (a float, a word, the byte +0x8, two words; not the
// three bytes after +0x8: the bridge parser 0x0063EA50, still the game's,
// builds each element on its stack writing only that byte, so they are its
// stack leftovers, and those change with whatever ran below that frame:
// part r3's own code among it). From the drive day on, ` g<hash>` at the very
// end: part r3's functions called at the game's addresses on the test's own
// memory: the three character tests on 0 to 255; TypeIndexOfName on the 13
// names and four near misses; the lexer (Construct, NextToken until the
// text's end, a pushed-back token) on five texts (numbers, hex, floats,
// '-', identifiers, comments, ';', line ends, bytes above 0x7F); the record
// vector (40 PushBacks, an InsertOverflow of 3 in the middle, Assign into
// an empty, a longer and a shorter vector and to itself); a check type
// table (60 FindOrInserts), a pair table (150 FindOrInserts with Resize
// under them, CopyFrom into another), a table +0x3C (24 inserts, then
// DestroyTable3C), a point table (24 equal-key inserts), the string map
// (FindOrInsert 0x00640E10, which runs ConstructPair, Resize and CopyPair,
// then FindIterator and Clear), the int-keyed map (24 InsertUniques, Assign
// to a copy, to itself and from an empty map: CopySubtree, CloneNode,
// CreateNode, EraseSubtree under them) and a RUL2 multimap (24
// InsertEquals), every table and node freed after.
//
// Part r4 (src/core/nettool_rul_4.cpp) appends, on every day, after
// everything above, ` v<nodes>:<hash> w<hash>`: v the tile verifier table
// 0x00B466DC (LoadTileVerifiers and its line callback fill it: count, bucket
// count, the order-free sum over nodes of the key and the 8-byte entries,
// both words: the upper bytes of the second are the table's own address,
// the same in every run); w the rows' bridge records' 24 floats +0xE4 to
// +0x140 and eight vectors (the record constructor sets them up, the bridge
// loader fills them). The RUL0 store (s), the rows' tables +0x3C (x), the
// RUL1 map (r) and the bridges (b) above already cover what the RUL0 line
// callback, the exemplar load, the RUL1 section reader and the bridge
// layout parser build. From the drive day on, ` k<hash>` at the very end:
// part r4's functions called at the game's addresses on the test's own
// memory: AppendTypeName for 0 to 13 (a naked call with ECX and EAX),
// ParseInt and ParseFloat over a text, the check type parser on nine
// values (accepted ones: the key, the types, each present side's edge word
// and mask, the three flags; not the bytes the parser never writes); the
// RUL0 line callback on a rule of the test's own (the game's constructor)
// with a LoadRul0-like context (the real store for CopyFrom, three list
// heads): CopyFrom of the smallest stored rule with a cell grid, then
// every other key (Piece, StepOffsets, CellLayout, ConsLayout, AutoTileBase,
// AutoPathBase, CheckType, Transpose, Rotate, Translate,
// ReplacementIntersection, ConvertQueryID, PlaceQueryID, HandleOffset,
// PreviewEffect, Costs, AutoPlace, OneWayDir, an unknown one) with
// complete scans only (a missing number keeps a pointer's value), the rule
// and its two lists hashed, all freed; the rule Assign on copies of stored
// rules with a cell grid (and to itself); the int-keyed map's hinted insert
// (hints at the leftmost node, the head, middle nodes, an empty map) and
// its copy constructor; the string map's CopyFrom into an empty map; the
// RUL1 map's InsertAt (left, right through CreateNode, by key, copying a
// real pair table) on a map of the test's own; the type row table's
// FindOrInsert; the tile verifier line callback on a test table (the
// entries' second words hashed without their upper half: the test
// table's address); the bridge record's constructor, Assign (from a loaded
// record with an int map, when there is one) and destructor; the bridge
// layout parser on a lexer of the test's own (+0x2C a list of one test
// string map, divisor 2): a part 'arch', layouts naming it (in order,
// reversed, counted), a loaded record's 'length(n)', refused and unsigned
// lines (the elements without the three bytes after +0x8, the floats).
// The 'length(n)' leg uses only a loaded part whose float vector is long
// enough for the divisor (else it is skipped); every other part is the
// test's own, and every scan line of the RUL0 callback is complete.
//
// Part r5 (src/core/nettool_rul_5.cpp) appends, on every day, after
// everything above, ` y<records>:<hash>/<hash>`: the rows' bridge records'
// fields the bridge line callback 0x00642220 fills from the exemplars
// (BridgeRecordFieldsHash): first those it always writes, after the slash
// those it writes only when the exemplar has the property (the original
// keeps stack leftovers in them otherwise, ours zeros: an exemplar without
// one would show as noise between A runs there). The RUL0 store (s: the
// Ordering rings +0xEC to +0xF8 that 0x006416B0 and 0x00641A50 write), the
// RUL1 map (r) and the bridges' int maps (b) above already cover the rest
// of what part r5 builds. From the drive day on, ` h<hash>` at the very
// end: a RUL1 map of the test's own (FindOrInsert, InsertUnique and the
// hinted insert with leftmost, head, middle and rightmost hints) and the
// bridge layouts read again (LoadBridgeLayouts with each row's ID and
// divisor, the same resources the load reads) into copies of the loaded
// records with emptied int maps, then the record vector's InsertOverflow of
// two (UninitializedFillN) and Erase (CopyRange). The Ordering callback is
// not driven (it needs a store of rules; the load runs it every time).
#include "core/autotest_hooks.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/nettool_rul.h"
#include "core/nettool_sim.h"

#include <cISC4City.h>

#include <opensc4/addresses.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace opensc4::autotest {
namespace {

using nettool_sim::CallCdecl;
using nettool_sim::CallThis;
using nettool_sim::GameString;
using nettool_sim::Vector;
namespace rul = nettool_sim::rul;
namespace tree = nettool_sim::tree;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

uint32_t Word(char* at, std::size_t offset) { return At<uint32_t>(at, offset); }
uint32_t Byte(char* at, std::size_t offset) { return At<uint8_t>(at, offset); }

uint32_t BucketCount(char* table)
{
    return static_cast<uint32_t>((At<char*>(table, rul::table::kBucketsEnd) - At<char*>(table, rul::table::kBuckets)) >>
                                 2);
}

// Every node of a table, in no particular order; at most `limit` nodes.
template <typename Fn>
void ForEachNode(char* table, Fn fn, uint32_t limit = 1u << 20)
{
    char** const buckets = At<char**>(table, rul::table::kBuckets);
    const uint32_t count = BucketCount(table);
    uint32_t seen = 0;
    for (uint32_t i = 0; i < count; ++i) {
        for (char* node = buckets[i]; node && seen < limit; node = At<char*>(node, rul::table::kNext), ++seen)
            fn(node);
    }
}

void MixVector(Hash& hash, char* at, std::size_t vector, std::size_t stride, std::size_t words)
{
    const Vector& v = At<Vector>(at, vector);
    const uint32_t count = static_cast<uint32_t>(static_cast<std::size_t>(v.end - v.begin) / stride);
    hash.Mix(count);
    for (char* e = v.begin; e != v.end; e += stride) {
        for (std::size_t i = 0; i < words; ++i)
            hash.Mix(Word(e, 4 * i));
    }
}

void MixBytes(Hash& hash, char* at, std::size_t vector)
{
    const Vector& v = At<Vector>(at, vector);
    hash.Mix(static_cast<uint32_t>(v.end - v.begin));
    for (char* e = v.begin; e != v.end; ++e)
        hash.Mix(static_cast<uint8_t>(*e));
}

bool HasCellGrid(char* r)
{
    const Vector& grid = At<Vector>(r, rul::rul0::kCellGrid);
    return grid.begin != grid.end;
}

uint32_t RuleHash(char* r)
{
    using namespace rul;
    Hash hash;
    hash.Mix(Word(r, rul0::kId));
    {
        const Vector& v = At<Vector>(r, rul0::kRecords);
        hash.Mix(static_cast<uint32_t>(v.end - v.begin) / record::kSize);
        for (char* e = v.begin; e != v.end; e += record::kSize) {
            hash.Mix(Word(e, record::kWord));
            hash.Mix(Byte(e, record::kTurn));
            hash.Mix(Byte(e, record::kFlip));
            hash.Mix(Word(e, record::kX));
            hash.Mix(Word(e, record::kZ));
        }
    }
    hash.Mix(Word(r, rul0::kWord10));
    hash.Mix(Word(r, rul0::kWord14));
    const bool laidOut = HasCellGrid(r);
    hash.Mix(laidOut);
    if (laidOut) {
        for (const std::size_t at : {rul0::kMinX, rul0::kMinZ, rul0::kMaxX, rul0::kMaxZ, rul0::kCellWidth})
            hash.Mix(Word(r, at));
    }
    MixVector(hash, r, rul0::kPoints, point::kSize, 2);
    MixVector(hash, r, rul0::kPoints34, point::kSize, 2);
    MixVector(hash, r, rul0::kChecked, point::kSize, 2);
    hash.Mix(Byte(r, rul0::kByte48));
    {
        const Vector& v = At<Vector>(r, rul0::kCells);
        hash.Mix(static_cast<uint32_t>(v.end - v.begin) / cellrec::kSize);
        for (char* e = v.begin; e != v.end; e += cellrec::kSize) {
            hash.Mix(Word(e, cellrec::kX));
            hash.Mix(Word(e, cellrec::kZ));
            hash.Mix(Byte(e, cellrec::kKey));
        }
    }
    {
        char* const types = r + rul0::kCheckTypes;
        hash.Mix(Word(types, table::kCount));
        hash.Mix(BucketCount(types));
        uint32_t sum = 0;
        ForEachNode(types, [&](char* node) {
            Hash one;
            one.Mix(Byte(node, table::kKey));
            for (std::size_t side = 0; side < 2; ++side) {
                const int8_t type = At<int8_t>(node, checktype::kTypes + side);
                one.Mix(static_cast<uint8_t>(type));
                if (type >= 0) {
                    one.Mix(Word(node, checktype::kEdges + side * checktype::kSide));
                    one.Mix(Word(node, checktype::kMasks + side * checktype::kSide));
                }
            }
            one.Mix(Byte(node, checktype::kChecked));
            sum += one.h;
        });
        hash.Mix(sum);
    }
    hash.Mix(Word(r, rul0::kWord78));
    {
        const char* const begin = At<char*>(r, rul0::kTextBegin);
        const char* const end = At<char*>(r, rul0::kTextEnd);
        hash.Mix(static_cast<uint32_t>(end - begin));
        for (const char* c = begin; c != end; ++c)
            hash.Mix(static_cast<uint8_t>(*c));
    }
    hash.Mix(Word(r, rul0::kOffsetX));
    hash.Mix(Word(r, rul0::kOffsetZ));
    MixBytes(hash, r, rul0::kConsGrid);
    for (const std::size_t at : {rul0::kConsWidth, rul0::kConsAnchorX, rul0::kConsAnchorZ, rul0::kWordB4})
        hash.Mix(Word(r, at));
    MixBytes(hash, r, rul0::kCellGrid);
    hash.Mix(Word(r, rul0::kWordC8));
    hash.Mix(Word(r, rul0::kWordCC));
    {
        char* const cons = r + rul0::kConsCells;
        hash.Mix(Word(cons, table::kCount));
        hash.Mix(BucketCount(cons));
        uint32_t sum = 0;
        ForEachNode(cons, [&](char* node) {
            Hash one;
            one.Mix(Byte(node, table::kKey));
            one.Mix(Word(node, conscell::kValue));
            one.Mix(Byte(node, conscell::kTurn));
            one.Mix(Byte(node, conscell::kAlone));
            sum += one.h;
        });
        hash.Mix(sum);
    }
    hash.Mix(Word(r, rul0::kWordE4));
    hash.Mix(Byte(r, rul0::kRotation));
    hash.Mix(Byte(r, rul0::kByteE9));
    for (std::size_t at = rul0::kWordsEC; at < rul0::kSize; at += 4)
        hash.Mix(Word(r, at));
    return hash.h;
}

char* Store() { return reinterpret_cast<char*>(addr::cSC4NetworkTool_Rul0Store); }

void StoreHash(uint32_t& count, uint32_t& value)
{
    char* const store = Store();
    count = Word(store, rul::table::kCount);
    Hash hash;
    hash.Mix(count);
    hash.Mix(BucketCount(store));
    uint32_t sum = 0;
    ForEachNode(store, [&](char* node) {
        Hash one;
        one.Mix(Word(node, rul::table::kKey));
        one.Mix(RuleHash(node + rul::store::kRule));
        sum += one.h;
    });
    hash.Mix(sum);
    value = hash.h;
}

void MultimapHash(uint32_t& count, uint32_t& value)
{
    char* const map = reinterpret_cast<char*>(addr::cSC4NetworkTool_Rul2Overrides);
    count = Word(map, rul::rul2::kCount);
    Hash hash;
    hash.Mix(count);
    uint32_t sum = 0, visited = 0;
    char* const head = At<char*>(map, 0);
    std::vector<char*> pending;
    if (head && count) {
        if (char* const root = At<char*>(head, tree::kParent))
            pending.push_back(root);
    }
    while (!pending.empty() && visited <= count) {
        char* const node = pending.back();
        pending.pop_back();
        ++visited;
        Hash one;
        one.Mix(Word(node, rul::rul2::kKey));
        for (std::size_t i = 0; i < rul::rul2::kPairCount; ++i) {
            char* const pair = node + rul::rul2::kPairs + i * rul::rul2::kPairSize;
            one.Mix(Word(pair, 0));
            one.Mix(Byte(pair, rul::rul2::kPairByte));
        }
        sum += one.h;
        for (const std::size_t child : {tree::kLeft, tree::kRight}) {
            if (char* const next = At<char*>(node, child))
                pending.push_back(next);
        }
    }
    hash.Mix(visited);
    hash.Mix(sum);
    value = hash.h;
}

uint32_t RowsHash()
{
    using namespace rul;
    Hash hash;
    for (uint32_t type = 0; type < nettool_sim::kTypes; ++type) {
        char* const row = reinterpret_cast<char*>(addr::cSC4NetworkConstructionCrew_NetworkToolTuning +
                                                  type * nettool_sim::kRowSize);
        hash.Mix(Word(row, typerow::kWord0));
        hash.Mix(Byte(row, typerow::kLoaded));
        for (std::size_t i = 0; i < typerow::kIdCount; ++i)
            hash.Mix(Word(row, typerow::kIds + 4 * i));
        hash.Mix(Byte(row, typerow::kDiagonal));
        hash.Mix(Word(row, typerow::kRul1Id));
        for (std::size_t i = 0; i < typerow::kFloatCount; ++i)
            hash.Mix(Word(row, typerow::kFloats + 4 * i));
        const Vector& files = At<Vector>(row, typerow::kFiles);
        hash.Mix(static_cast<uint32_t>(files.end - files.begin) / 4);
        const Vector& bridges = At<Vector>(row, typerow::kBridges);
        hash.Mix(static_cast<uint32_t>(static_cast<std::size_t>(bridges.end - bridges.begin) / typerow::kBridgeSize));
    }
    return hash.h;
}

// --- The drive -----------------------------------------------------------------

uint32_t Mirror(uint32_t word)
{
    return CallCdecl<uint32_t, uint32_t>(addr::SC4Rul0Rule_MirrorEdgeWord, word);
}

uint32_t DriveMirror()
{
    Hash hash;
    const uint32_t fixed[] = {0u, 0x01020304u, 0x03030303u, 0x04050601u, 0xFF010203u, 0x00000102u, 0x02000100u};
    for (const uint32_t w : fixed)
        hash.Mix(Mirror(w));
    uint32_t state = 12345u;
    for (int i = 0; i < 57; ++i) {
        state = state * 1103515245u + 12345u;
        // Bytes mostly 0 to 4 (the edge values) with some others.
        const uint32_t w = (state & 0x07070707u) ^ ((state >> 3) & 0x10000000u);
        hash.Mix(Mirror(w));
    }
    return hash.h;
}

// --- Part r2: the rule files ---------------------------------------------------

char* Row(uint32_t type)
{
    return reinterpret_cast<char*>(addr::cSC4NetworkConstructionCrew_NetworkToolTuning + type * nettool_sim::kRowSize);
}

// The in-order successor of a multimap node.
char* NextNode(char* node)
{
    if (char* right = At<char*>(node, tree::kRight)) {
        while (char* const left = At<char*>(right, tree::kLeft))
            right = left;
        return right;
    }
    char* x = node;
    char* y = At<char*>(x, tree::kParent);
    while (x == At<char*>(y, tree::kRight)) {
        x = y;
        y = At<char*>(y, tree::kParent);
    }
    if (At<char*>(x, tree::kRight) != y)
        x = y;
    return x;
}

void MixEntry(Hash& hash, char* e)
{
    using namespace rul;
    hash.Mix(Word(e, entry::kEdges));
    {
        const Vector& v = At<Vector>(e, entry::kChecks);
        hash.Mix(static_cast<uint32_t>(v.end - v.begin) / check::kSize);
        for (char* c = v.begin; c != v.end; c += check::kSize) {
            hash.Mix(Byte(c, check::kPosition));
            hash.Mix(Word(c, check::kEdges));
            hash.Mix(Word(c, check::kMask));
        }
    }
    {
        const Vector& v = At<Vector>(e, entry::kTiles);
        hash.Mix(static_cast<uint32_t>(v.end - v.begin) / tile::kSize);
        for (char* t = v.begin; t != v.end; t += tile::kSize) {
            hash.Mix(Word(t, tile::kId));
            hash.Mix(Byte(t, tile::kPosition));
            hash.Mix(Byte(t, tile::kTurn));
        }
    }
}

// A multimap {head, count} in order (at most count + 1 nodes).
uint32_t MapHash(char* map)
{
    using namespace rul;
    Hash hash;
    const uint32_t count = Word(map, tree::kCount);
    hash.Mix(count);
    char* const head = At<char*>(map, tree::kHead);
    if (!head)
        return hash.h;
    uint32_t seen = 0;
    for (char* node = At<char*>(head, tree::kLeft); node != head && seen <= count; node = NextNode(node), ++seen) {
        char* const value = node + tree::kValue;
        hash.Mix(Word(value, rulevalue::kKey));
        MixEntry(hash, value + rulevalue::kEntry);
        hash.Mix(Word(value, rulevalue::kEntry + entry::kMask));
    }
    hash.Mix(seen);
    return hash.h;
}

void FilesHash(uint32_t& files, uint32_t& value)
{
    Hash hash;
    files = 0;
    for (uint32_t type = 0; type < nettool_sim::kTypes; ++type) {
        const Vector& v = At<Vector>(Row(type), rul::typerow::kFiles);
        hash.Mix(static_cast<uint32_t>(v.end - v.begin) / 4);
        for (char* p = v.begin; p != v.end; p += 4) {
            char* const file = At<char*>(p, 0);
            ++files;
            hash.Mix(file ? MapHash(file + rul::rulefile::kMap) : 0u);
        }
    }
    value = hash.h;
}

// The buffers of an entry the test made through the game's code.
void FreeEntry(char* e)
{
    for (const std::size_t at : {rul::entry::kChecks, rul::entry::kTiles}) {
        if (char* const begin = At<char*>(e, at))
            CallCdecl<void, void*>(addr::SC4_Free, begin);
    }
}

uint32_t DriveRuleRecords()
{
    using namespace rul;
    Hash hash;
    // Edge words.
    uint32_t state = 777u;
    for (int i = 0; i < 64; ++i) {
        state = state * 1103515245u + 12345u;
        uint32_t word = i < 8 ? (0x01020304u * static_cast<uint32_t>(i)) ^ 0x0B111302u : (state & 0x3F3F3F3Fu);
        uint32_t out = 0xDEADBEEFu;
        CallThis<void, uint32_t*>(addr::SC4RuleEdges_Mirror, &word, &out);
        hash.Mix(out);
        for (const uint32_t turns : {0u, 1u, 2u, 3u, 5u, 0xFFFFFFFFu}) {
            out = 0xDEADBEEFu;
            CallThis<void, uint32_t, uint32_t*>(addr::SC4RuleEdges_Rotate, &word, turns, &out);
            hash.Mix(out);
        }
    }
    // Made-up checks and tiles.
    for (uint32_t position = 0; position < 32; ++position) {
        for (uint32_t turns = 0; turns < 4; ++turns) {
            alignas(4) char in[check::kSize] = {};
            alignas(4) char out[check::kSize] = {};
            At<uint8_t>(in, check::kPosition) = static_cast<uint8_t>(position);
            At<uint32_t>(in, check::kEdges) = 0x0B020311u + position;
            At<uint32_t>(in, check::kMask) = 0xFF00FFFFu;
            CallThis<void, uint32_t, char*>(addr::SC4RuleCheck_Rotate, in, turns, out);
            hash.Mix(Byte(out, check::kPosition));
            hash.Mix(Word(out, check::kEdges));
            hash.Mix(Word(out, check::kMask));
            for (const uint8_t turn : {uint8_t{0}, uint8_t{1}, uint8_t{2}, uint8_t{3}, uint8_t{0x80}, uint8_t{0x81},
                                       uint8_t{0x82}, uint8_t{0x83}}) {
                alignas(4) char tin[tile::kSize] = {};
                alignas(4) char tout[tile::kSize] = {};
                At<uint32_t>(tin, tile::kId) = 0x5E540000u + position;
                At<uint8_t>(tin, tile::kPosition) = static_cast<uint8_t>(position);
                At<uint8_t>(tin, tile::kTurn) = turn;
                CallThis<void, uint32_t, char*>(addr::SC4RuleTile_Rotate, tin, turns, tout);
                hash.Mix(Word(tout, tile::kId));
                hash.Mix(Byte(tout, tile::kPosition));
                hash.Mix(Byte(tout, tile::kTurn));
            }
        }
    }
    // Row 0's first rule file.
    const Vector& files = At<Vector>(Row(0), typerow::kFiles);
    char* const file = files.begin != files.end ? At<char*>(files.begin, 0) : nullptr;
    hash.Mix(file != nullptr);
    if (!file)
        return hash.h;
    char* const map = file + rulefile::kMap;
    char* const head = At<char*>(map, tree::kHead);
    uint32_t k = 0;
    for (char* node = At<char*>(head, tree::kLeft); node != head && k < 48; node = NextNode(node), ++k) {
        char* const e = node + tree::kValue + rulevalue::kEntry;
        alignas(4) char mirrored[entry::kSize] = {};
        alignas(4) char turned[entry::kSize] = {};
        CallThis<void, char*>(addr::SC4RuleEntry_Mirror, e, mirrored);
        CallThis<void, uint32_t, char*>(addr::SC4RuleEntry_Rotate, mirrored, k & 3, turned);
        MixEntry(hash, mirrored);
        MixEntry(hash, turned);
        hash.Mix(CallThis<uint8_t, char*>(addr::SC4RuleEntry_Equals, e, turned));
        hash.Mix(CallThis<uint8_t, char*>(addr::SC4RuleEntry_Equals, turned, e));
        hash.Mix(CallThis<uint8_t, char*>(addr::SC4RuleEntry_Equals, e, e));
        FreeEntry(turned);
        FreeEntry(mirrored);
    }
    // A local multimap of copies with every variant asked, transmogrified.
    alignas(4) char local[8] = {};
    alignas(4) char out[8] = {};
    CallThis<char*>(addr::SC4RuleFileMap_Construct, local);
    k = 0;
    for (char* node = At<char*>(head, tree::kLeft); node != head && k < 32; node = NextNode(node), ++k) {
        alignas(4) char value[rulevalue::kSize];
        std::memcpy(value, node + tree::kValue, rulevalue::kSize); // the vectors are only read (copied)
        At<uint32_t>(value, rulevalue::kEntry + entry::kMask) = entry::kAllVariants;
        char* y = At<char*>(local, tree::kHead);
        char* x = At<char*>(y, tree::kParent);
        while (x) {
            y = x;
            x = Word(value, rulevalue::kKey) < Word(x, tree::kValue + rulevalue::kKey) ? At<char*>(x, tree::kLeft)
                                                                                         : At<char*>(x, tree::kRight);
        }
        char* result = nullptr;
        CallThis<char**, char**, char*, char*, char*, char*>(addr::SC4RuleFileMap_Insert, local, &result, x, y, value,
                                                             nullptr);
    }
    hash.Mix(MapHash(local));
    CallThis<char*>(addr::SC4RuleFileMap_Construct, out);
    CallThis<void, char*, char*, uint32_t>(addr::SC4NetworkRuleFile_Transmogrify, file, out, local, 0u);
    hash.Mix(MapHash(out));
    CallThis<void>(addr::SC4RuleFileMap_Destruct, out);
    CallThis<void>(addr::SC4RuleFileMap_Destruct, local);
    // The file read again (row 0's first key, as LoadTypeInfo gives it).
    constexpr std::uintptr_t kRow0FirstFile = 0x00B0B424;
    char* const fresh = CallCdecl<char*, uint32_t>(addr::SC4_operator_new, rulefile::kSize);
    uint8_t loaded = 0;
    uint32_t again = 0;
    if (fresh) {
        CallThis<char*>(addr::SC4NetworkRuleFile_Construct, fresh);
        loaded = CallThis<uint8_t, const void*, uint32_t>(addr::SC4NetworkRuleFile_Load, fresh,
                                                          reinterpret_cast<const void*>(kRow0FirstFile), 0u);
        again = MapHash(fresh + rulefile::kMap);
        Virtual<char*, uint32_t>(fresh, 0, 1u);
    }
    const uint32_t original = MapHash(map);
    hash.Mix(loaded);
    hash.Mix(again);
    hash.Mix(again == original);
    log::Write("autotest: ntrul: drive r2: row 0 file %u entries, reload %s (%08X vs %08X)", Word(map, tree::kCount),
               loaded ? (again == original ? "same" : "DIFFERENT") : "FAILED", again, original);
    return hash.h;
}

// --- Part r3: the placement parameters, tables and maps --------------------------

char* GameAllocate(uint32_t size)
{
    return CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, size);
}
void GameFree(void* block)
{
    if (block)
        CallCdecl<void, void*>(addr::SC4_Free, block);
}

// A vector of words: its count and words.
void MixWords(Hash& hash, char* at, std::size_t vector)
{
    const Vector& v = At<Vector>(at, vector);
    hash.Mix(static_cast<uint32_t>(v.end - v.begin) / 4);
    for (char* e = v.begin; e != v.end; e += 4)
        hash.Mix(Word(e, 0));
}

// The 13 rows' placement parameters (+0x50): the words and floats (bits),
// the bytes, the vectors.
uint32_t PlacementHash()
{
    using namespace rul;
    Hash hash;
    for (uint32_t type = 0; type < nettool_sim::kTypes; ++type) {
        char* const p = Row(type) + typerow::kParams;
        for (const std::size_t at : placement::kScalars)
            hash.Mix(Word(p, at));
        for (const std::size_t at : placement::kBytes)
            hash.Mix(Byte(p, at));
        for (const std::size_t at : placement::kVectors)
            MixWords(hash, p, at);
    }
    return hash.h;
}

// The 13 rows' tables +0x3C: count, bucket count, the sum over nodes of the
// key and the two vectors.
uint32_t Tables3CHash(uint32_t& nodes)
{
    using namespace rul;
    Hash hash;
    nodes = 0;
    for (uint32_t type = 0; type < nettool_sim::kTypes; ++type) {
        char* const t = Row(type) + typerow::kTable;
        hash.Mix(Word(t, table::kCount));
        hash.Mix(BucketCount(t));
        uint32_t sum = 0;
        ForEachNode(t, [&](char* node) {
            Hash one;
            one.Mix(Word(node, table3c::kKey));
            MixWords(one, node, table3c::kUints);
            MixWords(one, node, table3c::kFloats);
            sum += one.h;
            ++nodes;
        });
        hash.Mix(sum);
    }
    return hash.h;
}

// A RUL1 pair table: count, bucket count, the sum over nodes of the key,
// the word and the byte (not the three bytes after it: stack leftovers in
// the original).
uint32_t PairTableHash(char* t)
{
    using namespace rul;
    Hash hash;
    hash.Mix(Word(t, table::kCount));
    hash.Mix(BucketCount(t));
    uint32_t sum = 0;
    ForEachNode(t, [&](char* node) {
        Hash one;
        one.Mix(Word(node, pairtable::kFirst));
        one.Mix(Word(node, pairtable::kSecond));
        one.Mix(Word(node, pairtable::kValue));
        one.Mix(Byte(node, pairtable::kByte));
        sum += one.h;
    });
    hash.Mix(sum);
    return hash.h;
}

// Every node of a tree {head, count}, in order (at most count + 1).
template <typename Fn>
uint32_t ForEachInOrder(char* map, Fn fn)
{
    const uint32_t count = Word(map, tree::kCount);
    char* const head = At<char*>(map, tree::kHead);
    uint32_t seen = 0;
    if (!head)
        return 0;
    for (char* node = At<char*>(head, tree::kLeft); node != head && seen <= count; node = NextNode(node), ++seen)
        fn(node);
    return seen;
}

// The RUL1 map 0x00B460DC: its count and, in key order (two words), each
// node's pair and table.
uint32_t Rul1MapHash(uint32_t& count)
{
    using namespace rul;
    char* const map = reinterpret_cast<char*>(addr::cSC4NetworkTool_Rul1Map);
    count = Word(map, tree::kCount);
    Hash hash;
    hash.Mix(count);
    hash.Mix(ForEachInOrder(map, [&](char* node) {
        hash.Mix(Word(node, rul1map::kFirst));
        hash.Mix(Word(node, rul1map::kSecond));
        hash.Mix(PairTableHash(node + rul1map::kTable));
    }));
    return hash.h;
}

// An int-keyed map in key order: per node the key, the 20-byte elements
// (into `elements`) and the floats.
void MixIntMap(Hash& hash, Hash& elements, char* map)
{
    using namespace rul;
    hash.Mix(Word(map, tree::kCount));
    hash.Mix(ForEachInOrder(map, [&](char* node) {
        hash.Mix(Word(node, intmap::kKey));
        MixWords(hash, node, intmap::kFloats);
        const Vector& v = At<Vector>(node, intmap::kVector20);
        hash.Mix(static_cast<uint32_t>(v.end - v.begin) / stringmap::kElement);
        // An element: a float, a word, a byte (+0x8; the three bytes after it
        // are stack leftovers of the bridge parser 0x0063EA50, which builds
        // the element on its stack with `mov [esp+0x60],dl` and copies it
        // as words (0x00889C90): not hashed), two words.
        for (char* e = v.begin; e != v.end; e += stringmap::kElement) {
            elements.Mix(Word(e, 0x0));
            elements.Mix(Word(e, 0x4));
            elements.Mix(Byte(e, 0x8));
            elements.Mix(Word(e, 0xC));
            elements.Mix(Word(e, 0x10));
        }
    }));
}

// The bridge records of the 13 rows, in order: the name's characters and
// the int-keyed map (+0x16C); the 20-byte elements' words go to a hash of
// their own.
void BridgesHash(uint32_t& records, uint32_t& value, uint32_t& elementsValue)
{
    using namespace rul;
    Hash hash, elements;
    records = 0;
    for (uint32_t type = 0; type < nettool_sim::kTypes; ++type) {
        const Vector& v = At<Vector>(Row(type), typerow::kBridges);
        for (char* r = v.begin; r != v.end; r += typerow::kBridgeSize) {
            ++records;
            const GameString& name = At<GameString>(r, bridge::kName);
            hash.Mix(static_cast<uint32_t>(name.end - name.begin));
            for (const char* c = name.begin; c != name.end; ++c)
                hash.Mix(static_cast<uint8_t>(*c));
            MixIntMap(hash, elements, r + bridge::kIntMap);
        }
    }
    value = hash.h;
    elementsValue = elements.h;
}

// --- Part r3: the drive ----------------------------------------------------------

// A game string (vtable 0x00A80810) over the test's own characters.
struct TestString {
    std::string text;
    GameString s{};
    explicit TestString(const char* t) : text(t)
    {
        s.vtable = 0x00A80810;
        s.begin = text.data();
        s.end = s.begin + text.size();
        s.capacity = s.end + 1;
        s.references = 0;
    }
    TestString(const TestString&) = delete;
    TestString& operator=(const TestString&) = delete;
};

void MixRecord(Hash& hash, char* r)
{
    using namespace rul;
    hash.Mix(Word(r, record::kWord));
    hash.Mix(Byte(r, record::kTurn));
    hash.Mix(Byte(r, record::kFlip));
    hash.Mix(Word(r, record::kX));
    hash.Mix(Word(r, record::kZ));
}

void MixRecords(Hash& hash, const Vector& v)
{
    hash.Mix(static_cast<uint32_t>(v.end - v.begin) / rul::record::kSize);
    for (char* r = v.begin; r != v.end; r += rul::record::kSize)
        MixRecord(hash, r);
}

void MakeRecord(char* r, uint32_t i)
{
    using namespace rul;
    std::memset(r, 0, record::kSize);
    At<uint32_t>(r, record::kWord) = 0x5D4A1000u + i * 7;
    At<uint8_t>(r, record::kTurn) = static_cast<uint8_t>(i & 3);
    At<uint8_t>(r, record::kFlip) = static_cast<uint8_t>(i & 1);
    At<float>(r, record::kX) = static_cast<float>(i) * 0.5f;
    At<float>(r, record::kZ) = -static_cast<float>(i) * 0.25f;
}

// A vector of `size` made records in a buffer of `capacity` (the game's
// allocator).
Vector MakeRecords(uint32_t size, uint32_t capacity, uint32_t seed)
{
    Vector v{};
    v.begin = GameAllocate(capacity * rul::record::kSize);
    v.end = v.begin + size * rul::record::kSize;
    v.capacity = v.begin + capacity * rul::record::kSize;
    for (uint32_t i = 0; i < size; ++i)
        MakeRecord(v.begin + i * rul::record::kSize, seed + i);
    return v;
}

uint32_t DriveLexer()
{
    using namespace rul;
    Hash hash;
    static const char* const kTexts[] = {
        "1, 0x1F2e, -3, -.5, 2.25, .75, Name_1 _x #comment\n; rest",
        "  \t#only a comment",
        "RotationRing=0x12345678,1,2.5e3,-7x,- 4,(a)b;c",
        "\xC3\xA9 9.9.9 0x 00x12 abc123_def ~ \r\n2",
        "",
    };
    for (const char* text : kTexts) {
        TestString source(text);
        alignas(4) char lexer[0x48] = {};
        CallThis<char*, GameString*>(addr::SC4RulLexer_Construct, lexer, &source.s);
        hash.Mix(static_cast<uint32_t>(At<char*>(lexer, lexer::kEnd) - At<char*>(lexer, lexer::kBegin)));
        hash.Mix(Word(lexer, lexer::kPushed));
        for (int i = 0; i < 64; ++i) {
            const int32_t token = CallThis<int32_t>(addr::SC4RulLexer_NextToken, lexer);
            hash.Mix(static_cast<uint32_t>(token));
            if (token == lexer::kTokenInteger)
                hash.Mix(Word(lexer, lexer::kInteger));
            else if (token == lexer::kTokenFloat)
                hash.Mix(Word(lexer, lexer::kFloat));
            else if (token == lexer::kTokenIdentifier) {
                const char* const begin = At<char*>(lexer, lexer::kIdentBegin);
                const char* const end = At<char*>(lexer, lexer::kIdentEnd);
                hash.Mix(static_cast<uint32_t>(end - begin));
                for (const char* c = begin; c != end; ++c)
                    hash.Mix(static_cast<uint8_t>(*c));
            }
            hash.Mix(static_cast<uint32_t>(At<char*>(lexer, lexer::kCursor) - At<char*>(lexer, lexer::kBegin)));
            if (token == 0 && At<char*>(lexer, lexer::kCursor) >= At<char*>(lexer, lexer::kEnd))
                break;
        }
        At<int32_t>(lexer, lexer::kPushed) = 0x2C;
        hash.Mix(static_cast<uint32_t>(CallThis<int32_t>(addr::SC4RulLexer_NextToken, lexer)));
        hash.Mix(Word(lexer, lexer::kPushed));
        hash.Mix(At<uint16_t>(lexer, lexer::kWord2C));
        hash.Mix(Byte(lexer, lexer::kByte40) | (Byte(lexer, lexer::kByte41) << 8) |
                 (Byte(lexer, lexer::kByte42) << 16));
        GameFree(At<char*>(lexer, lexer::kIdentBegin));
    }
    return hash.h;
}

uint32_t DriveRecords()
{
    using namespace rul;
    Hash hash;
    Vector v{};
    for (uint32_t i = 0; i < 40; ++i) {
        alignas(4) char r[record::kSize];
        MakeRecord(r, i);
        CallThis<void, char*>(addr::SC4Rul0RecordVector_PushBack, &v, r);
    }
    MixRecords(hash, v);
    {
        alignas(4) char r[record::kSize];
        MakeRecord(r, 1000);
        uint32_t tag = 0;
        CallThis<void, char*, char*, uint32_t*, uint32_t, uint32_t>(addr::SC4Rul0RecordVector_InsertOverflow, &v,
                                                                    v.begin + 5 * record::kSize, r, &tag, 3u, 0u);
    }
    MixRecords(hash, v);
    Vector empty{};
    Vector larger = MakeRecords(60, 100, 500);
    Vector smaller = MakeRecords(2, 100, 900);
    for (Vector* target : {&empty, &larger, &smaller}) {
        CallThis<char*, Vector*>(addr::SC4Rul0RecordVector_Assign, target, &v);
        hash.Mix(static_cast<uint32_t>(target->capacity - target->begin) / record::kSize);
        MixRecords(hash, *target);
    }
    CallThis<char*, Vector*>(addr::SC4Rul0RecordVector_Assign, &v, &v);
    MixRecords(hash, v);
    for (Vector* each : {&v, &empty, &larger, &smaller})
        GameFree(each->begin);
    return hash.h;
}

// A hash table made by the game's constructor (100 buckets) on the test's
// memory, and freed: cleared (SC4Uint32HashSet::Clear) and its buckets.
struct TestTable {
    alignas(4) char t[rul::table::kSize] = {};
    TestTable() { CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Construct, t, 100u); }
    ~TestTable()
    {
        CallThis<void>(addr::SC4Uint32HashSet_Clear, t);
        GameFree(At<char*>(t, rul::table::kBuckets));
    }
    TestTable(const TestTable&) = delete;
    TestTable& operator=(const TestTable&) = delete;
};

uint32_t DriveTables()
{
    using namespace rul;
    Hash hash;
    {
        TestTable types;
        for (uint32_t k = 0; k < 60; ++k) {
            uint8_t key = static_cast<uint8_t>((k * 37u) & 0x7F);
            char* const value = CallThis<char*, uint8_t*>(addr::SC4Rul0CheckTypes_FindOrInsert, types.t, &key);
            At<uint32_t>(value, 0) += k + 1;
            At<uint32_t>(value, 0x10) ^= static_cast<uint32_t>(key) << 8;
        }
        hash.Mix(Word(types.t, table::kCount));
        hash.Mix(BucketCount(types.t));
        uint32_t sum = 0;
        ForEachNode(types.t, [&](char* node) {
            Hash one;
            one.Mix(Byte(node, table::kKey));
            for (std::size_t i = 0; i < checkvalue::kWordCount; ++i)
                one.Mix(Word(node, 8 + 4 * i));
            sum += one.h;
        });
        hash.Mix(sum);
    }
    {
        TestTable pairs, copy;
        for (uint32_t n = 0; n < 150; ++n) {
            uint32_t key[2] = {n % 13, (n * 7) % 11};
            uint32_t* const value = CallThis<uint32_t*, uint32_t*>(addr::SC4PairHashTable_FindOrInsert, pairs.t, key);
            value[0] += n;
            value[1] = (value[1] & 0xFFFFFF00u) | ((n & 3) | ((n & 4) << 5));
        }
        CallThis<void, char*>(addr::SC4PairHashTable_CopyFrom, copy.t, pairs.t);
        hash.Mix(PairTableHash(pairs.t));
        hash.Mix(PairTableHash(copy.t));
    }
    {
        alignas(4) char t[table::kSize] = {};
        CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Construct, t, 100u);
        for (uint32_t n = 0; n < 24; ++n) {
            uint32_t uints[4] = {n, n * 3, 0xA0000000u + n, 7};
            float floats[3] = {static_cast<float>(n) * 1.5f, -2.0f, 0.1f * static_cast<float>(n)};
            alignas(4) char value[table3c::kValueFloats + 0xC] = {};
            At<uint32_t>(value, 0) = 0x10000 + n * 0x101;
            At<Vector>(value, table3c::kValueUints) = Vector{reinterpret_cast<char*>(uints),
                                                             reinterpret_cast<char*>(uints + n % 5),
                                                             reinterpret_cast<char*>(uints + 4)};
            At<Vector>(value, table3c::kValueFloats) = Vector{reinterpret_cast<char*>(floats),
                                                              reinterpret_cast<char*>(floats + n % 4),
                                                              reinterpret_cast<char*>(floats + 3)};
            CallThis<char*, char*>(addr::SC4NetworkTypeInfo_Table3CInsertNoResize, t, value);
        }
        hash.Mix(Word(t, table::kCount));
        hash.Mix(BucketCount(t));
        uint32_t sum = 0;
        ForEachNode(t, [&](char* node) {
            Hash one;
            one.Mix(Word(node, table3c::kKey));
            MixWords(one, node, table3c::kUints);
            MixWords(one, node, table3c::kFloats);
            sum += one.h;
        });
        hash.Mix(sum);
        CallThis<void>(addr::SC4NetworkTypeInfo_DestroyTable3C, t);
        hash.Mix(Word(t, table::kCount));
        GameFree(At<char*>(t, table::kBuckets));
    }
    {
        TestTable points;
        for (uint32_t n = 0; n < 24; ++n) {
            int32_t xy[8] = {static_cast<int32_t>(n), -1, 2, static_cast<int32_t>(n * n), 5, 6, -7, 8};
            alignas(4) char value[0x10] = {};
            At<uint32_t>(value, 0) = n % 7;
            At<Vector>(value, 4) = Vector{reinterpret_cast<char*>(xy), reinterpret_cast<char*>(xy + 2 * (1 + n % 4)),
                                          reinterpret_cast<char*>(xy + 8)};
            char* result[2] = {};
            CallThis<char**, char**, char*>(addr::SC4PointsHashTable_InsertEqualNoResize, points.t, result, value);
            hash.Mix(result[1] == points.t);
        }
        hash.Mix(Word(points.t, table::kCount));
        uint32_t sum = 0;
        ForEachNode(points.t, [&](char* node) {
            Hash one;
            one.Mix(Word(node, pointtable::kKey));
            MixWords(one, node, pointtable::kPoints);
            sum += one.h;
        });
        hash.Mix(sum);
        // Each bucket's chain in order (equal keys follow the first one).
        for (uint32_t i = 0; i < BucketCount(points.t); ++i) {
            for (char* node = At<char**>(points.t, table::kBuckets)[i]; node; node = At<char*>(node, table::kNext))
                hash.Mix(Word(node, pointtable::kKey) ^ (Word(At<char*>(node, pointtable::kPoints), 4) << 8));
        }
        ForEachNode(points.t, [&](char* node) { GameFree(At<char*>(node, pointtable::kPoints)); });
    }
    return hash.h;
}

uint32_t DriveStringMap()
{
    using namespace rul;
    Hash hash;
    TestTable map;
    static const char* const kKeys[] = {"Road", "Rail", "", "A_b", "road", "Road", "x", "Rail", "Avenue"};
    for (const char* text : kKeys) {
        TestString key(text);
        char* const value = CallThis<char*, GameString*>(addr::SC4StringHashMap_FindOrInsert, map.t, &key.s);
        hash.Mix(Word(map.t, table::kCount));
        hash.Mix(static_cast<uint32_t>(At<Vector>(value, 0).end - At<Vector>(value, 0).begin));
    }
    hash.Mix(BucketCount(map.t));
    static const char* const kLook[] = {"Road", "road", "", "missing", "Avenue"};
    for (const char* text : kLook) {
        TestString key(text);
        char* result[2] = {};
        CallThis<char**, char**, GameString*>(addr::SC4StringHashMap_FindIterator, map.t, result, &key.s);
        hash.Mix(result[0] != nullptr);
        hash.Mix(result[1] == map.t);
        if (result[0]) {
            const GameString& s = At<GameString>(result[0], stringmap::kPair);
            hash.Mix(static_cast<uint32_t>(s.end - s.begin));
            for (const char* c = s.begin; c != s.end; ++c)
                hash.Mix(static_cast<uint8_t>(*c));
        }
    }
    // Bucket 0's chain in order (every key's bucket).
    for (char* node = At<char**>(map.t, table::kBuckets)[0]; node; node = At<char*>(node, table::kNext)) {
        const GameString& s = At<GameString>(node, stringmap::kPair);
        hash.Mix(static_cast<uint32_t>(s.end - s.begin));
    }
    CallThis<void>(addr::SC4StringHashMap_Clear, map.t);
    hash.Mix(Word(map.t, table::kCount));
    return hash.h;
}

// A tree head as the test makes it (zeroed, linked to itself): `size`
// bytes from the game's allocator.
char* MakeHead(std::size_t size)
{
    char* const head = GameAllocate(static_cast<uint32_t>(size));
    std::memset(head, 0, size);
    At<char*>(head, tree::kLeft) = head;
    At<char*>(head, tree::kRight) = head;
    return head;
}

uint32_t DriveIntMap()
{
    using namespace rul;
    Hash hash, elements;
    char* maps[3][2] = {};
    for (auto& m : maps) {
        m[0] = MakeHead(intmap::kNodeSize);
        m[1] = nullptr;
    }
    char* const one = reinterpret_cast<char*>(maps[0]);
    char* const two = reinterpret_cast<char*>(maps[1]);
    char* const none = reinterpret_cast<char*>(maps[2]);
    for (uint32_t n = 0; n < 24; ++n) {
        uint32_t words[15];
        for (uint32_t i = 0; i < 15; ++i)
            words[i] = n * 100 + i;
        float floats[4] = {1.0f, static_cast<float>(n), -0.5f, 3.25f};
        alignas(4) char value[intmap::kValueSize] = {};
        At<int32_t>(value, 0) = static_cast<int32_t>((n * 13) % 17) - 8;
        At<Vector>(value, intmap::kValueVector20) =
            Vector{reinterpret_cast<char*>(words), reinterpret_cast<char*>(words + 5 * (n % 3)),
                   reinterpret_cast<char*>(words + 15)};
        At<Vector>(value, intmap::kValueFloats) = Vector{reinterpret_cast<char*>(floats),
                                                         reinterpret_cast<char*>(floats + n % 4),
                                                         reinterpret_cast<char*>(floats + 4)};
        alignas(4) char result[8] = {};
        CallThis<char*, char*, char*>(addr::SC4BridgeIntMap_InsertUnique, one, result, value);
        hash.Mix(Byte(result, 4));
        hash.Mix(Word(At<char*>(result, 0), intmap::kKey));
    }
    CallThis<char*, char*>(addr::SC4BridgeIntMap_Assign, two, one);
    MixIntMap(hash, elements, one);
    MixIntMap(hash, elements, two);
    CallThis<char*, char*>(addr::SC4BridgeIntMap_Assign, two, two);
    CallThis<char*, char*>(addr::SC4BridgeIntMap_Assign, two, none);
    CallThis<char*, char*>(addr::SC4BridgeIntMap_Assign, one, none);
    MixIntMap(hash, elements, one);
    MixIntMap(hash, elements, two);
    for (auto& m : maps)
        GameFree(m[0]);
    hash.Mix(elements.h);
    return hash.h;
}

// The nodes of a test multimap freed (post-order).
void FreeSubtree(char* node)
{
    while (node) {
        FreeSubtree(At<char*>(node, tree::kRight));
        char* const left = At<char*>(node, tree::kLeft);
        GameFree(node);
        node = left;
    }
}

uint32_t DriveRul2Map()
{
    using namespace rul;
    Hash hash;
    char* map[2] = {MakeHead(rul2node::kNodeSize), nullptr};
    char* const self = reinterpret_cast<char*>(map);
    for (uint32_t n = 0; n < 24; ++n) {
        uint32_t value[rul2node::kValueWords];
        value[0] = 0x5D000000u + (n * 7) % 11;
        for (uint32_t i = 1; i < rul2node::kValueWords; ++i)
            value[i] = n * 16 + i;
        char* result = nullptr;
        CallThis<char**, char**, uint32_t*>(addr::SC4Rul2Multimap_InsertEqual, self, &result, value);
        hash.Mix(Word(result, tree::kValue));
    }
    hash.Mix(ForEachInOrder(self, [&](char* node) {
        for (uint32_t i = 0; i < rul2node::kValueWords; ++i)
            hash.Mix(Word(node, tree::kValue + 4 * i));
    }));
    FreeSubtree(At<char*>(map[0], tree::kParent));
    GameFree(map[0]);
    return hash.h;
}

uint32_t DriveRul3()
{
    using namespace rul;
    Hash total;
    {
        Hash tests;
        for (uint32_t c = 0; c < 256; ++c) {
            tests.Mix(CallCdecl<uint32_t, uint32_t>(addr::SC4RulLexer_IsDigit, c) |
                      (CallCdecl<uint32_t, uint32_t>(addr::SC4RulLexer_IsIdentChar, c) << 1) |
                      (CallCdecl<uint32_t, uint32_t>(addr::SC4RulLexer_IsAlpha, c) << 2));
        }
        total.Mix(tests.h);
    }
    {
        Hash names;
        static const char* const kNames[] = {"Road",       "Rail",     "Highway", "Street",   "Pipe",
                                             "PowerLine",  "Avenue",   "Subway",  "LightRail", "Monorail",
                                             "OneWayRoad", "DirtRoad", "GroundHighway", "road", "Roads", "", "Pipe "};
        for (const char* text : kNames) {
            TestString name(text);
            names.Mix(static_cast<uint32_t>(
                CallCdecl<int32_t, GameString*>(addr::cSC4NetworkTool_TypeIndexOfName, &name.s)));
        }
        total.Mix(names.h);
    }
    const uint32_t lexer = DriveLexer();
    const uint32_t records = DriveRecords();
    const uint32_t tables = DriveTables();
    const uint32_t strings = DriveStringMap();
    const uint32_t ints = DriveIntMap();
    const uint32_t rul2 = DriveRul2Map();
    for (const uint32_t h : {lexer, records, tables, strings, ints, rul2})
        total.Mix(h);
    log::Write("autotest: ntrul: drive r3: lexer %08X, records %08X, tables %08X, string map %08X, int map %08X, "
               "rul2 map %08X, total %08X",
               lexer, records, tables, strings, ints, rul2, total.h);
    return total.h;
}

// --- Part r4: the daily checksums and the drive -----------------------------------

// The tile verifier table 0x00B466DC: count, bucket count, the order-free
// sum over nodes of the key and the entries (both words: the upper bytes of
// the second are the table's own fixed address in both runs).
uint32_t TileVerifiersHash(uint32_t& count)
{
    using namespace rul;
    char* const t = reinterpret_cast<char*>(addr::cSC4NetworkTool_TileVerifiers);
    count = Word(t, table::kCount);
    Hash hash;
    hash.Mix(count);
    hash.Mix(BucketCount(t));
    uint32_t sum = 0;
    ForEachNode(t, [&](char* node) {
        Hash one;
        one.Mix(Word(node, pointtable::kKey));
        MixVector(one, node, pointtable::kPoints, tileverifier::kEntrySize, 2);
        sum += one.h;
    });
    hash.Mix(sum);
    return hash.h;
}

// A bridge record's fields its constructor sets up and the loaders fill:
// the 24 floats +0xE4 to +0x140 (bits) and the eight vectors' words.
void MixBridgeFields(Hash& hash, char* r)
{
    using namespace rul;
    for (std::size_t i = 0; i < bridgerecord::kFloatCount; ++i)
        hash.Mix(Word(r, bridgerecord::kFloats + 4 * i));
    for (const std::size_t at : bridgerecord::kVectors)
        MixWords(hash, r, at);
}

uint32_t BridgeFieldsHash()
{
    using namespace rul;
    Hash hash;
    for (uint32_t type = 0; type < nettool_sim::kTypes; ++type) {
        const Vector& v = At<Vector>(Row(type), typerow::kBridges);
        for (char* r = v.begin; r != v.end; r += typerow::kBridgeSize)
            MixBridgeFields(hash, r);
    }
    return hash.h;
}

// Calls a game function that takes ECX and EAX (0x0063E240).
__declspec(naked) void __cdecl CallEcxEax(std::uintptr_t /*function*/, uint32_t /*ecx*/, void* /*eax*/)
{
    __asm {
        mov ecx, [esp + 8]
        mov eax, [esp + 12]
        call dword ptr [esp + 4]
        ret
    }
}

// An empty game string with an 8-byte buffer from the game's allocator.
void MakeGameString(GameString& s)
{
    char* const buffer = GameAllocate(8);
    s.vtable = 0x00A80810;
    s.begin = buffer;
    s.end = buffer;
    s.capacity = buffer + 8;
    s.references = 0;
    *buffer = 0;
}

void MixText(Hash& hash, const char* begin, const char* end)
{
    hash.Mix(static_cast<uint32_t>(end - begin));
    for (const char* c = begin; c != end; ++c)
        hash.Mix(static_cast<uint8_t>(*c));
}

// AppendTypeName for 0 to 13 on one string; ParseInt and ParseFloat over a
// text; the check type parser on accepted and refused values.
uint32_t DriveLexerNumbers()
{
    using namespace rul;
    Hash hash;
    {
        GameString s{};
        MakeGameString(s);
        for (uint32_t type = 0; type <= 13; ++type) {
            CallEcxEax(addr::cSC4NetworkTool_AppendTypeName, type, &s);
            MixText(hash, s.begin, s.end);
        }
        GameFree(s.begin);
    }
    {
        TestString source("12 -3 2.5 0x10 -1.25 word 7 .5 4294967295");
        alignas(4) char lexer[0x48] = {};
        CallThis<char*, GameString*>(addr::SC4RulLexer_Construct, lexer, &source.s);
        for (int i = 0; i < 12; ++i) {
            uint32_t word = 0x5A5A5A5Au;
            float value = 0.0f;
            const bool asFloat = (i & 1) != 0;
            const uint8_t ok = asFloat ? CallThis<uint8_t, float*>(addr::SC4RulLexer_ParseFloat, lexer, &value)
                                       : CallThis<uint8_t, uint32_t*>(addr::SC4RulLexer_ParseInt, lexer, &word);
            hash.Mix(ok);
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof bits);
            hash.Mix(asFloat ? bits : word);
        }
        GameFree(At<char*>(lexer, lexer::kIdentBegin));
    }
    static const char* const kChecks[] = {
        "a - Road:0x02020202",
        "b - Street:0x01000100,0xFF00FF00 Avenue:2 optional",
        "c - check multiok",
        "Zz - Road:1",
        "d - Road:1 Rail:2 Pipe:3",
        "e - Nowhere:1",
        "f - road:3,0x0F street:4",
        "g - Monorail",
        "h Road:1",
    };
    for (const char* text : kChecks) {
        TestString source(text);
        alignas(4) char lexer[0x48] = {};
        CallThis<char*, GameString*>(addr::SC4RulLexer_Construct, lexer, &source.s);
        const uint8_t ok = CallThis<uint8_t>(addr::SC4RulLexer_ParseCheckType, lexer);
        hash.Mix(ok);
        if (ok) {
            hash.Mix(Byte(lexer, checklexer::kKey));
            for (uint32_t side = 0; side < checklexer::kMaxTypes; ++side) {
                const int8_t type = static_cast<int8_t>(Byte(lexer, checklexer::kTypes + side));
                hash.Mix(static_cast<uint32_t>(type));
                if (type >= 0) {
                    hash.Mix(Word(lexer, checklexer::kEdges + checklexer::kSide * side));
                    hash.Mix(Word(lexer, checklexer::kEdges + checklexer::kSide * side + 4));
                }
            }
            hash.Mix(Byte(lexer, checklexer::kMultiOk) | (Byte(lexer, checklexer::kChecked) << 8) |
                     (Byte(lexer, checklexer::kOptional) << 16));
        }
        GameFree(At<char*>(lexer, lexer::kIdentBegin));
    }
    return hash.h;
}

// The RUL0 line callback on a rule of the test's own (the game's
// constructor) with a context like LoadRul0's (the real store for
// CopyFrom's lookup, three list heads from the game's allocator): CopyFrom
// a stored rule with a cell grid first, then every other key; the rule,
// the fields RuleHash leaves out (set here by the keys) and the two lists'
// lines hashed; the rule destroyed, the lists freed (0x0063C470).
uint32_t DriveRul0Lines()
{
    using namespace rul;
    Hash hash;
    alignas(4) char rule[rul0::kSize] = {};
    CallThis<char*>(addr::SC4Rul0Rule_Construct, rule);
    alignas(4) char context[rul0ctx::kSize] = {};
    At<char*>(context, rul0ctx::kStore) = Store();
    At<char*>(context, rul0ctx::kRule) = rule;
    for (const std::size_t list : {rul0ctx::kCellLines, rul0ctx::kConsLines, rul0ctx::kList14}) {
        char* const head = GameAllocate(rul0ctx::kListNodeSize);
        At<char*>(head, 0) = head;
        At<char*>(head, 4) = head;
        At<char*>(context, list) = head;
    }
    At<int32_t>(context, rul0ctx::kWord18) = -1;
    // The smallest stored ID whose rule has a cell grid (no CopyFrom when
    // there is none).
    uint32_t source = 0;
    bool haveSource = false;
    ForEachNode(Store(), [&](char* node) {
        const uint32_t id = Word(node, table::kKey);
        if (HasCellGrid(node + store::kRule) && (!haveSource || id < source)) {
            source = id;
            haveSource = true;
        }
    });
    hash.Mix(haveSource);
    char copyFrom[16];
    std::snprintf(copyFrom, sizeof copyFrom, "0x%08X", source);
    struct Line {
        const char* key;
        const char* value;
    };
    const Line lines[] = {
        {"CopyFrom", haveSource ? copyFrom : "0x0"},
        {"Piece", "1.5,-2.25,1,0,0x5D4A1234"},
        {"Piece", "0.5,4,3,1,0xABC"},
        {"Piece", "2,3"},
        {"StepOffsets", "2, -3"},
        {"CellLayout", "..+.."},
        {"CellLayout", ".a+b."},
        {"ConsLayout", "-|/"},
        {"AutoTileBase", "0x5E000000"},
        {"AutoPathBase", "123"},
        {"CheckType", "a - Road:0x02020202"},
        {"CheckType", "b - Street:0x01000100,0xFF00FF00 Avenue:2 optional"},
        {"CheckType", "c - check"},
        {"CheckType", "Zz - Road:1"},
        {"Transpose", ""},
        {"Rotate", "1"},
        {"Translate", "4,5"},
        {"ReplacementIntersection", "4660, 7"},
        {"ConvertQueryID", "0x1111"},
        {"PlaceQueryID", "0x2222"},
        {"HandleOffset", "1, 2"},
        {"PreviewEffect", "Effect_Name"},
        {"Costs", "25"},
        {"AutoPlace", ""},
        {"OneWayDir", "3"},
        {"Nothing", "1"},
        {"piece", "9,9,1,1,0x1"},
    };
    for (const Line& line : lines) {
        TestString key(line.key);
        TestString value(line.value);
        CallCdecl<void, GameString*, GameString*, char*>(addr::cSC4NetworkTool_Rul0LineCallback, &key.s, &value.s,
                                                         context);
    }
    hash.Mix(RuleHash(rule));
    hash.Mix(Word(rule, rul0::kWord40));
    hash.Mix(Word(rule, rul0::kWord44));
    hash.Mix(Byte(rule, rul0::kByte48));
    hash.Mix(Word(rule, rul0::kWord7C));
    for (const std::size_t list : {rul0ctx::kCellLines, rul0ctx::kConsLines}) {
        char* const head = At<char*>(context, list);
        for (char* node = At<char*>(head, 0); node != head; node = At<char*>(node, 0))
            MixText(hash, At<char*>(node, 0xC), At<char*>(node, 0x10));
    }
    CallThis<void>(addr::SC4Rul0Rule_Destruct, rule);
    CallThis<void>(addr::SC4Rul0LoaderLists_Destruct, context);
    return hash.h;
}

// A stored rule with a cell grid copied (the game's copy constructor), a
// second one assigned over it (0x0063E060), hashed, destroyed; for the
// first 8 pairs of such rules.
uint32_t DriveRuleAssign()
{
    using namespace rul;
    Hash hash;
    std::vector<std::pair<uint32_t, char*>> rules;
    ForEachNode(Store(), [&](char* node) {
        if (HasCellGrid(node + store::kRule))
            rules.emplace_back(Word(node, table::kKey), node + store::kRule);
    });
    std::sort(rules.begin(), rules.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    hash.Mix(static_cast<uint32_t>(rules.size()));
    for (std::size_t i = 0; i + 1 < rules.size() && i < 16; i += 2) {
        alignas(4) char copy[rul0::kSize] = {};
        CallThis<char*, char*>(addr::SC4Rul0Rule_CopyConstruct, copy, rules[i].second);
        CallThis<char*, char*>(addr::SC4Rul0Rule_Assign, copy, rules[i + 1].second);
        hash.Mix(RuleHash(copy));
        CallThis<char*, char*>(addr::SC4Rul0Rule_Assign, copy, copy);
        hash.Mix(RuleHash(copy));
        CallThis<void>(addr::SC4Rul0Rule_Destruct, copy);
    }
    return hash.h;
}

// The nodes of a test int-keyed map freed with their vectors (post-order).
void FreeIntSubtree(char* node)
{
    using namespace rul;
    while (node) {
        FreeIntSubtree(At<char*>(node, tree::kRight));
        char* const left = At<char*>(node, tree::kLeft);
        GameFree(At<char*>(node, intmap::kFloats));
        GameFree(At<char*>(node, intmap::kVector20));
        GameFree(node);
        node = left;
    }
}

char* IntNodeOf(char* map, int32_t key)
{
    char* found = nullptr;
    ForEachInOrder(map, [&](char* node) {
        if (!found && At<int32_t>(node, rul::intmap::kKey) == key)
            found = node;
    });
    return found;
}

// The int-keyed map's hinted insert (0x0063E500) with hints at the
// leftmost node, the head and a middle node and keys before, at and after
// them; then its copy constructor (0x0063E710).
uint32_t DriveIntMapHints()
{
    using namespace rul;
    Hash hash, elements;
    char* map[2] = {MakeHead(intmap::kNodeSize), nullptr};
    char* const self = reinterpret_cast<char*>(map);
    const auto valueOf = [](int32_t key, char* value) {
        std::memset(value, 0, intmap::kValueSize);
        At<int32_t>(value, 0) = key;
    };
    for (const int32_t key : {20, 0, 40, 10, 30}) {
        alignas(4) char value[intmap::kValueSize];
        valueOf(key, value);
        alignas(4) char result[8] = {};
        CallThis<char*, char*, char*>(addr::SC4BridgeIntMap_InsertUnique, self, result, value);
    }
    struct Hint {
        int32_t at; // the hint's key; 1000 the leftmost, 2000 the head
        int32_t key;
    };
    static const Hint kHints[] = {{1000, -5}, {1000, -5}, {1000, -20}, {1000, -4}, {2000, 50}, {2000, 25},
                                  {20, 15},   {20, 22},   {20, 20},    {20, 35},   {40, 45},  {40, 38},
                                  {0, -1},    {30, 31},   {10, 9},     {2000, 51}};
    for (const Hint& h : kHints) {
        char* const head = map[0];
        char* hint = h.at == 1000 ? At<char*>(head, tree::kLeft) : h.at == 2000 ? head : IntNodeOf(self, h.at);
        if (!hint)
            hint = head;
        alignas(4) char value[intmap::kValueSize];
        valueOf(h.key, value);
        char* result = nullptr;
        CallThis<char**, char**, char*, char*>(addr::SC4BridgeIntMap_InsertHint, self, &result, hint, value);
        hash.Mix(static_cast<uint32_t>(At<int32_t>(result, intmap::kKey)));
        hash.Mix(Word(self, tree::kCount));
    }
    MixIntMap(hash, elements, self);
    char* copy[2] = {};
    CallThis<char*, char*>(addr::SC4BridgeIntMap_CopyConstruct, copy, self);
    MixIntMap(hash, elements, reinterpret_cast<char*>(copy));
    char* single[2] = {MakeHead(intmap::kNodeSize), nullptr};
    {
        alignas(4) char value[intmap::kValueSize];
        valueOf(77, value);
        char* result = nullptr;
        CallThis<char**, char**, char*, char*>(addr::SC4BridgeIntMap_InsertHint, single, &result, single[0], value);
        hash.Mix(static_cast<uint32_t>(At<int32_t>(result, intmap::kKey)));
        hash.Mix(Word(reinterpret_cast<char*>(single), tree::kCount));
    }
    char* empty[2] = {MakeHead(intmap::kNodeSize), nullptr};
    char* copyEmpty[2] = {};
    CallThis<char*, char*>(addr::SC4BridgeIntMap_CopyConstruct, copyEmpty, reinterpret_cast<char*>(empty));
    hash.Mix(At<char*>(copyEmpty[0], tree::kLeft) == copyEmpty[0]);
    hash.Mix(Word(reinterpret_cast<char*>(copyEmpty), tree::kCount));
    for (char** m : {map, copy, single, empty, copyEmpty}) {
        FreeIntSubtree(At<char*>(m[0], tree::kParent));
        GameFree(m[0]);
    }
    hash.Mix(elements.h);
    return hash.h;
}

// The string map copied (0x0063E7C0) into an empty one, the RUL1 map's
// InsertAt and CreateNode, the type row table's FindOrInsert, the tile
// verifier line callback, the bridge records' constructor, Assign and
// destructor.
uint32_t DriveContainers2()
{
    using namespace rul;
    Hash hash;
    {
        TestTable source;
        static const char* const kKeys[] = {"Road", "Rail", "", "A_b", "Avenue", "x"};
        for (const char* text : kKeys) {
            TestString key(text);
            CallThis<char*, GameString*>(addr::SC4StringHashMap_FindOrInsert, source.t, &key.s);
        }
        alignas(4) char copy[table::kSize] = {};
        CallThis<void, char*>(addr::SC4StringHashMap_CopyFrom, copy, source.t);
        hash.Mix(Word(copy, table::kCount));
        hash.Mix(BucketCount(copy));
        for (uint32_t i = 0; i < BucketCount(copy); ++i) {
            for (char* node = At<char**>(copy, table::kBuckets)[i]; node; node = At<char*>(node, table::kNext)) {
                const GameString& s = At<GameString>(node, stringmap::kPair);
                hash.Mix(i);
                MixText(hash, s.begin, s.end);
                hash.Mix(static_cast<uint32_t>(s.vtable == 0x00A80810));
            }
        }
        CallThis<void>(addr::SC4StringHashMap_Clear, copy);
        GameFree(At<char*>(copy, table::kBuckets));
        CallThis<void>(addr::SC4StringHashMap_Clear, source.t);
    }
    {
        char* map[2] = {MakeHead(rul1node::kNodeSize), nullptr};
        char* const self = reinterpret_cast<char*>(map);
        // A real pair table to copy when the RUL1 map has one.
        char* const rul1 = reinterpret_cast<char*>(addr::cSC4NetworkTool_Rul1Map);
        char* const realHead = At<char*>(rul1, tree::kHead);
        char* realTable = nullptr;
        if (realHead && At<char*>(realHead, tree::kLeft) != realHead)
            realTable = At<char*>(realHead, tree::kLeft) + rul1map::kTable;
        const auto insert = [&](char* x, char* y, int32_t a, int32_t b, char* w, bool real) {
            alignas(4) char value[0x8 + table::kSize] = {};
            At<int32_t>(value, 0) = a;
            At<int32_t>(value, 4) = b;
            if (real && realTable)
                std::memcpy(value + 8, realTable, table::kSize);
            char* result = nullptr;
            CallThis<char**, char**, char*, char*, char*, char*>(addr::SC4Rul1Map_InsertAt, self, &result, x, y,
                                                                 value, w);
            return result;
        };
        // The node under which (a, b) goes, by InsertAt's own order (first
        // word, then second; signed): its child on that side is null.
        const auto parentOf = [&](int32_t a, int32_t b) {
            char* y = map[0];
            char* x = At<char*>(map[0], tree::kParent);
            while (x) {
                y = x;
                const int32_t first = At<int32_t>(x, rul1map::kFirst);
                const bool left = first > a || (first == a && b < At<int32_t>(x, rul1map::kSecond));
                x = left ? At<char*>(x, tree::kLeft) : At<char*>(x, tree::kRight);
            }
            return y;
        };
        char* const head = map[0];
        insert(nullptr, head, 5, 5, nullptr, false);
        insert(nullptr, At<char*>(head, tree::kRight), 7, 1, head, true);
        insert(head, At<char*>(head, tree::kLeft), 3, 9, nullptr, false);
        insert(nullptr, parentOf(7, 2), 7, 2, nullptr, false);
        insert(nullptr, parentOf(4, 0), 4, 0, nullptr, false);
        insert(nullptr, parentOf(6, 6), 6, 6, nullptr, true);
        insert(head, At<char*>(head, tree::kLeft), 1, 1, nullptr, false);
        hash.Mix(Word(self, tree::kCount));
        hash.Mix(ForEachInOrder(self, [&](char* node) {
            hash.Mix(Word(node, rul1map::kFirst));
            hash.Mix(Word(node, rul1map::kSecond));
            hash.Mix(PairTableHash(node + rul1map::kTable));
        }));
        std::vector<char*> nodes;
        ForEachInOrder(self, [&](char* node) { nodes.push_back(node); });
        for (char* node : nodes) {
            char* const t = node + rul1map::kTable;
            std::vector<char*> pairs;
            ForEachNode(t, [&](char* p) { pairs.push_back(p); });
            for (char* p : pairs)
                GameFree(p);
            GameFree(At<char*>(t, table::kBuckets));
            GameFree(node);
        }
        GameFree(head);
    }
    {
        alignas(4) char t[table::kSize] = {};
        CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Construct, t, 100u);
        char* first = nullptr;
        for (const uint32_t key : {5u, 105u, 5u, 7u, 205u, 105u}) {
            uint32_t k = key;
            char* const value = CallThis<char*, uint32_t*>(addr::SC4NetworkTypeInfo_Table3CFindOrInsert, t, &k);
            if (!first)
                first = value;
            hash.Mix(value == first);
            hash.Mix(Word(t, table::kCount));
            hash.Mix(static_cast<uint32_t>(At<Vector>(value, 0).end - At<Vector>(value, 0).begin));
        }
        CallThis<void>(addr::SC4NetworkTypeInfo_DestroyTable3C, t);
        GameFree(At<char*>(t, table::kBuckets));
    }
    {
        TestTable points;
        struct Line {
            const char* key;
            const char* value;
        };
        static const Line kLines[] = {
            {"12", "1,2,3,0,1,4,5,6,7,0,9,10,11,12,1"},
            {"12", "0,0,0,0,0"},
            {"7", "255,65536,1,256,-1"},
            {"0x10", "1,2,3,4"},
        };
        for (const Line& line : kLines) {
            TestString key(line.key);
            TestString value(line.value);
            CallCdecl<void, GameString*, GameString*, char*>(addr::cSC4NetworkTool_TileVerifierLineCallback, &key.s,
                                                             &value.s, points.t);
        }
        hash.Mix(Word(points.t, table::kCount));
        uint32_t sum = 0;
        ForEachNode(points.t, [&](char* node) {
            Hash one;
            one.Mix(Word(node, pointtable::kKey));
            const Vector& v = At<Vector>(node, pointtable::kPoints);
            one.Mix(static_cast<uint32_t>(v.end - v.begin) / tileverifier::kEntrySize);
            // The entry's second word's upper half is the table's address
            // (here the test's stack): only its low half is hashed.
            for (char* e = v.begin; e != v.end; e += tileverifier::kEntrySize) {
                one.Mix(Word(e, tileverifier::kWord));
                one.Mix(Word(e, tileverifier::kFlags) & 0xFFFFu);
            }
            sum += one.h;
        });
        hash.Mix(sum);
        ForEachNode(points.t, [&](char* node) { GameFree(At<char*>(node, pointtable::kPoints)); });
    }
    {
        // A loaded bridge record with an int map, when there is one.
        char* real = nullptr;
        for (uint32_t type = 0; type < nettool_sim::kTypes && !real; ++type) {
            const Vector& v = At<Vector>(Row(type), typerow::kBridges);
            for (char* r = v.begin; r != v.end && !real; r += typerow::kBridgeSize) {
                if (Word(r, bridgerecord::kMapCount))
                    real = r;
            }
        }
        alignas(4) char a[bridgerecord::kSize] = {};
        alignas(4) char b[bridgerecord::kSize] = {};
        CallThis<char*>(addr::SC4NetworkBridgeRecord_Construct, a);
        CallThis<char*>(addr::SC4NetworkBridgeRecord_Construct, b);
        Hash elements;
        MixBridgeFields(hash, a);
        hash.Mix(Word(a, bridgerecord::kMapCount));
        hash.Mix(real != nullptr);
        if (real)
            CallThis<char*, char*>(addr::SC4NetworkBridgeRecord_Assign, b, real);
        CallThis<char*, char*>(addr::SC4NetworkBridgeRecord_Assign, a, b);
        for (char* r : {a, b}) {
            MixBridgeFields(hash, r);
            const GameString& name = At<GameString>(r, bridgerecord::kNameVtable);
            MixText(hash, name.begin, name.end);
            MixIntMap(hash, elements, r + bridgerecord::kMapHead);
        }
        hash.Mix(elements.h);
        CallThis<void>(addr::SC4NetworkBridgeRecord_Destruct, a);
        CallThis<void>(addr::SC4NetworkBridgeRecord_Destruct, b);
    }
    return hash.h;
}

// The bridge layout parser (0x0063EA50) on a lexer of the test's own: the
// lexer's constructor, then +0x2C a list of one string map (a test table)
// and +0x30 the divisor 2. A part 'arch' parsed into the map's value, then
// layouts using it (by name, reversed, counted), a record's 'length(n)'
// when a loaded record has an int map, a refused line and an unsigned
// height. The elements (not the three bytes after +0x8) and floats hashed,
// the buffers freed.
uint32_t DriveBridgeParser()
{
    using namespace rul;
    Hash hash;
    alignas(4) char head[8] = {};
    alignas(4) char item[bridgelexer::kListMap + table::kSize] = {};
    At<char*>(head, 0) = item;
    At<char*>(head, 4) = item;
    At<char*>(item, 0) = head;
    At<char*>(item, 4) = head;
    char* const listMap = item + bridgelexer::kListMap;
    CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Construct, listMap, 100u);

    char* real = nullptr;
    int32_t realKey = 0;
    for (uint32_t type = 0; type < nettool_sim::kTypes && !real; ++type) {
        const Vector& v = At<Vector>(Row(type), typerow::kBridges);
        for (char* r = v.begin; r != v.end && !real; r += typerow::kBridgeSize) {
            char* const h = At<char*>(r, bridgerecord::kMapHead);
            if (!Word(r, bridgerecord::kMapCount))
                continue;
            // Only a part whose float vector is long enough for divisor 2:
            // the parser reads the part's floats [i / 2 + 1] (forward) and
            // [(n - 1) / 2] (reversed) for n elements, and its last one.
            char* const node = At<char*>(h, tree::kLeft);
            const Vector& elements = At<Vector>(node, intmap::kVector20);
            const Vector& floats = At<Vector>(node, intmap::kFloats);
            const uint32_t n = static_cast<uint32_t>(elements.end - elements.begin) / stringmap::kElement;
            const uint32_t f = static_cast<uint32_t>(floats.end - floats.begin) / 4;
            if (f >= n / 2 + 2) {
                real = r;
                realKey = At<int32_t>(node, intmap::kKey);
            }
        }
    }
    char lengthText[64];
    std::snprintf(lengthText, sizeof lengthText, "piece length(%d)\npiece 2 * length(%d)\nend\n", realKey, realKey);

    const auto parse = [&](const char* text, char* record, char* value) {
        TestString source(text);
        alignas(4) char lexer[0x48] = {};
        CallThis<char*, GameString*>(addr::SC4RulLexer_Construct, lexer, &source.s);
        At<char*>(lexer, bridgelexer::kMaps) = head;
        At<uint32_t>(lexer, bridgelexer::kDivisor) = 2;
        const uint8_t ok = CallThis<uint8_t, char*, char*>(addr::SC4RulLexer_ParseBridgeLayout, lexer, record, value);
        GameFree(At<char*>(lexer, lexer::kIdentBegin));
        return ok;
    };
    const auto mixValue = [&](char* value) {
        const Vector& e = At<Vector>(value, bridgevalue::kElements);
        hash.Mix(static_cast<uint32_t>(e.end - e.begin) / stringmap::kElement);
        for (char* p = e.begin; p != e.end; p += stringmap::kElement) {
            hash.Mix(Word(p, bridgevalue::kPosition));
            hash.Mix(Word(p, bridgevalue::kWord4));
            hash.Mix(Byte(p, bridgevalue::kByte8));
            hash.Mix(Word(p, bridgevalue::kFloatC));
            hash.Mix(Word(p, bridgevalue::kEnd));
        }
        MixWords(hash, value, bridgevalue::kFloats);
    };
    const auto freeValue = [](char* value) {
        GameFree(At<char*>(value, bridgevalue::kElements));
        GameFree(At<char*>(value, bridgevalue::kFloats));
    };
    {
        TestString name("arch");
        char* const arch = CallThis<char*, GameString*>(addr::SC4StringHashMap_FindOrInsert, listMap, &name.s);
        hash.Mix(parse("height 0\npiece <1, 2, 0.5, 1.0> : pylon\npiece 2 * <3, 4, 0.25, 0.75> : 1.5, end\n"
                       "piece <5, 6>\n",
                       nullptr, arch));
        mixValue(arch);
    }
    static const char* const kTexts[] = {
        "height 2.5\npiece arch\npiece 3 * rev(arch)\npiece <7, 8>\nend\n",
        "piece 2 * arch <9, 10, 1, 2> : end\n# a comment\npiece rev(arch) arch\n",
        "height 4294967295\npiece 2 * <1, 1>\n",
        "height -1\npiece <1, 1, 0.5>\nend\npiece <2, 2>\n",
        "piece <1 2>\n",
        "piece missing\n",
        "piece 0 * <1, 2>\npiece <3, 4> : 7, pylon, 2.5\n",
        "nonsense\n",
        "end extra\n",
    };
    for (const char* text : kTexts) {
        alignas(4) char value[0x18] = {};
        hash.Mix(parse(text, nullptr, value));
        mixValue(value);
        freeValue(value);
    }
    {
        alignas(4) char value[0x18] = {};
        hash.Mix(real != nullptr);
        hash.Mix(parse(lengthText, real, value));
        mixValue(value);
        freeValue(value);
        alignas(4) char value2[0x18] = {};
        hash.Mix(parse("piece length(3)\n", nullptr, value2));
        freeValue(value2);
    }
    // The map's nodes with their values (Clear), then its buckets.
    hash.Mix(Word(listMap, table::kCount));
    CallThis<void>(addr::SC4StringHashMap_Clear, listMap);
    GameFree(At<char*>(listMap, table::kBuckets));
    return hash.h;
}

uint32_t DriveRul4()
{
    const uint32_t numbers = DriveLexerNumbers();
    const uint32_t lines = DriveRul0Lines();
    const uint32_t assign = DriveRuleAssign();
    const uint32_t hints = DriveIntMapHints();
    const uint32_t containers = DriveContainers2();
    const uint32_t bridges = DriveBridgeParser();
    Hash total;
    for (const uint32_t h : {numbers, lines, assign, hints, containers, bridges})
        total.Mix(h);
    log::Write("autotest: ntrul: drive r4: lexer %08X, rul0 lines %08X, assign %08X, int map hints %08X, "
               "containers %08X, bridge parser %08X, total %08X",
               numbers, lines, assign, hints, containers, bridges, total.h);
    return total.h;
}

// --- Part r5 -----------------------------------------------------------------------

// The bridge records' fields the bridge line callback 0x00642220 writes,
// in row order: `always` gets the ones it always writes (+0x0 to +0x10,
// +0x34, +0x38, +0x40, +0x78, +0x9C to +0xD4, the byte +0xD8, +0x148 to
// +0x154; +0x3C when +0x40 is set; +0x7C when +0x10 is zero; +0xDC, +0xE0,
// +0x144 when +0xD8 is set); `property` the ones it writes only when the
// exemplar has the property (+0x2C, the byte +0x30, +0x5C, +0x7C with +0x10
// set, the bytes +0x98 / +0x99): in the original they keep stack leftovers
// without it, so they are kept apart. Never the bytes +0x31 to +0x33.
void BridgeRecordFieldsHash(uint32_t& records, uint32_t& always, uint32_t& property)
{
    using namespace rul;
    using namespace rul::bridgefields;
    Hash a, p;
    records = 0;
    for (uint32_t type = 0; type < nettool_sim::kTypes; ++type) {
        const Vector& v = At<Vector>(Row(type), typerow::kBridges);
        for (char* r = v.begin; r != v.end; r += typerow::kBridgeSize) {
            ++records;
            for (std::size_t at = 0; at <= kCount10; at += 4)
                a.Mix(Word(r, at));
            a.Mix(Word(r, kWord34));
            a.Mix(Word(r, kWord38));
            a.Mix(Word(r, kCount40));
            if (Word(r, kCount40))
                a.Mix(Word(r, kFloat3C));
            a.Mix(Word(r, kWord78));
            for (std::size_t at = kWords9C; at <= kWordsD4; at += 4)
                a.Mix(Word(r, at));
            a.Mix(Byte(r, kChained));
            if (Byte(r, kChained)) {
                a.Mix(Word(r, kWordDC));
                a.Mix(Word(r, kFloatE0));
                a.Mix(Word(r, kWord144));
            }
            for (std::size_t at = kKeys148; at <= kKeys150 + 4; at += 4)
                a.Mix(Word(r, at));
            if (Word(r, kCount10))
                p.Mix(Word(r, kWord7C));
            else
                a.Mix(Word(r, kWord7C));
            p.Mix(Word(r, kWord2C));
            p.Mix(Byte(r, kByte30));
            p.Mix(Word(r, kFloat5C));
            p.Mix(Byte(r, kByte98));
            p.Mix(Byte(r, kByte99));
        }
    }
    always = a.h;
    property = p.h;
}

// A RUL1 map of the test's own (a 0x2C-byte head): FindOrInsert 0x00641C40
// on 40 type pairs (repeats among them; LowerBound, the hinted insert
// 0x006413B0, InsertAt and CreateNode under it), InsertUnique 0x00640EF0 on
// 12 pairs (half already there) and the hinted insert with the leftmost
// node, the head and middle nodes as hints; per call the answer's pair (and
// InsertUnique's byte), then the map in key order (pairs and their tables'
// counts). Erased by EraseSubtree 0x00635C60, the head freed.
uint32_t DriveRul1Map()
{
    using namespace rul;
    Hash hash;
    char* map[2] = {MakeHead(rul1node::kNodeSize), nullptr};
    char* const self = reinterpret_cast<char*>(map);
    alignas(4) char value[rul1map::kTable - rul1map::kFirst + table::kSize] = {};
    char* const valueTable = value + rul1node::kValueTable;
    CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Construct, valueTable, table::kFirstBuckets);
    const auto pairOf = [](uint32_t n, int32_t* key) {
        key[0] = static_cast<int32_t>((n * 7) % 13) - 1;
        key[1] = static_cast<int32_t>((n * 5) % 11) - 3;
    };
    for (uint32_t n = 0; n < 40; ++n) {
        int32_t key[2];
        pairOf(n, key);
        char* const t = CallThis<char*, int32_t*>(addr::SC4Rul1Map_FindOrInsert, self, key);
        hash.Mix(Word(t - rul1map::kTable + rul1map::kFirst, 0));
        hash.Mix(Word(t - rul1map::kTable + rul1map::kSecond, 0));
        hash.Mix(BucketCount(t));
    }
    for (uint32_t n = 0; n < 12; ++n) {
        At<int32_t>(value, 0) = static_cast<int32_t>(n % 2 ? n * 3 : (n * 7) % 13 - 1) + 20 * (n % 2);
        At<int32_t>(value, 4) = static_cast<int32_t>(n) - 6;
        alignas(4) char result[8] = {};
        CallThis<char*, char*, char*>(addr::SC4Rul1Map_InsertUnique, self, result, value);
        hash.Mix(Word(At<char*>(result, 0), rul1map::kFirst));
        hash.Mix(Word(At<char*>(result, 0), rul1map::kSecond));
        hash.Mix(Byte(result, 4));
    }
    for (uint32_t n = 0; n < 24; ++n) {
        char* const head = map[0];
        char* hint = head;
        if (n % 4 == 0) {
            hint = At<char*>(head, tree::kLeft);
        } else if (n % 4 == 2) {
            // the (n / 4)-th node in order, bounded by the count
            uint32_t steps = (n / 4) % (Word(self, tree::kCount) + 1);
            hint = At<char*>(head, tree::kLeft);
            while (steps-- && hint != head)
                hint = NextNode(hint);
        } else if (n % 4 == 3) {
            hint = At<char*>(head, tree::kRight);
        }
        At<int32_t>(value, 0) = static_cast<int32_t>((n * 11) % 17) - 4;
        At<int32_t>(value, 4) = static_cast<int32_t>((n * 3) % 7) - 2;
        char* result = nullptr;
        CallThis<char**, char**, char*, char*>(addr::SC4Rul1Map_InsertHint, self, &result, hint, value);
        hash.Mix(Word(result, rul1map::kFirst));
        hash.Mix(Word(result, rul1map::kSecond));
    }
    hash.Mix(ForEachInOrder(self, [&](char* node) {
        hash.Mix(Word(node, rul1map::kFirst));
        hash.Mix(Word(node, rul1map::kSecond));
        hash.Mix(Word(node + rul1map::kTable, table::kCount));
    }));
    if (Word(self, tree::kCount))
        CallThis<void, char*>(addr::SC4Rul1Map_EraseSubtree, self, At<char*>(map[0], tree::kParent));
    GameFree(map[0]);
    CallThis<void>(addr::SC4Uint32HashSet_Clear, valueTable);
    GameFree(At<char*>(valueTable, table::kBuckets));
    return hash.h;
}

// The bridge layouts read again into records of the test's own: for each
// row with loaded bridge records, a test vector filled with copies of them
// (CopyConstruct 0x00640FA0; when full, InsertOverflow 0x00642030 as the
// line callback does) whose int maps are emptied (Assign from a new
// record's); LoadBridgeLayouts 0x006436D0 with the row's ID and divisor
// (the bridge lexer, Parse, ParseBridge, ParseLength, PushMap, ClearMaps,
// the int map's and string map's FindOrInsert under it); the records' int
// maps hashed (not the three bytes after each element's +0x8); then
// InsertOverflow of two copies in the middle (UninitializedFillN), Erase of
// the first record (CopyRange), the IDs and names hashed; every record
// destroyed and the buffers freed.
uint32_t DriveBridgeLayouts()
{
    using namespace rul;
    struct Setup {
        uint32_t type, divisor;
    };
    static constexpr Setup kSetups[] = {{0, 1}, {10, 1}, {11, 1}, {1, 1}, {8, 1}, {9, 1},
                                        {3, 1}, {2, 2}, {12, 2}, {6, 2}, {4, 1}, {7, 1}};
    Hash hash, elements;
    alignas(4) char fresh[bridgerecord::kSize] = {};
    CallThis<char*>(addr::SC4NetworkBridgeRecord_Construct, fresh);
    for (const Setup& s : kSetups) {
        const Vector& loaded = At<Vector>(Row(s.type), typerow::kBridges);
        if (loaded.begin == loaded.end)
            continue;
        Vector v{};
        for (char* r = loaded.begin; r != loaded.end; r += bridgerecord::kSize) {
            alignas(4) char copy[bridgerecord::kSize] = {};
            CallThis<char*, char*>(addr::SC4NetworkBridgeRecord_CopyConstruct, copy, r);
            CallThis<char*, char*>(addr::SC4BridgeIntMap_Assign, copy + bridgerecord::kMapHead,
                                   fresh + bridgerecord::kMapHead);
            if (v.end != v.capacity) {
                CallThis<char*, char*>(addr::SC4NetworkBridgeRecord_CopyConstruct, v.end, copy);
                v.end += bridgerecord::kSize;
            } else {
                uint8_t tag = 0;
                CallThis<char*, char*, char*, uint8_t*, uint32_t, uint32_t>(
                    addr::SC4BridgeRecordVector_InsertOverflow, &v, v.end, copy, &tag, 1u, 1u);
            }
            CallThis<void>(addr::SC4NetworkBridgeRecord_Destruct, copy);
        }
        CallCdecl<void, uint32_t, Vector*, uint32_t>(addr::SC4NetworkTypeInfo_LoadBridgeLayouts, 0x1000u + s.type, &v,
                                                     s.divisor);
        hash.Mix(s.type);
        for (char* r = v.begin; r != v.end; r += bridgerecord::kSize)
            MixIntMap(hash, elements, r + bridgerecord::kMapHead);
        // Two copies of the first record inserted before the last one.
        {
            const uint32_t count = static_cast<uint32_t>((v.end - v.begin) / bridgerecord::kSize);
            alignas(4) char copy[bridgerecord::kSize] = {};
            CallThis<char*, char*>(addr::SC4NetworkBridgeRecord_CopyConstruct, copy, v.begin);
            uint8_t tag = 0;
            char* const where = v.begin + (count - 1) * bridgerecord::kSize;
            CallThis<char*, char*, char*, uint8_t*, uint32_t, uint32_t>(addr::SC4BridgeRecordVector_InsertOverflow,
                                                                         &v, where, copy, &tag, 2u, 0u);
            CallThis<void>(addr::SC4NetworkBridgeRecord_Destruct, copy);
        }
        CallThis<char*, char*, char*>(addr::SC4BridgeRecordVector_Erase, &v, v.begin, v.begin + bridgerecord::kSize);
        hash.Mix(static_cast<uint32_t>((v.end - v.begin) / bridgerecord::kSize));
        for (char* r = v.begin; r != v.end; r += bridgerecord::kSize) {
            hash.Mix(Word(r, 0));
            const GameString& name = At<GameString>(r, bridgerecord::kNameVtable);
            MixText(hash, name.begin, name.end);
            hash.Mix(Word(r, bridgerecord::kMapCount));
        }
        for (char* r = v.begin; r != v.end; r += bridgerecord::kSize)
            CallThis<void>(addr::SC4NetworkBridgeRecord_Destruct, r);
        GameFree(v.begin);
    }
    CallThis<void>(addr::SC4NetworkBridgeRecord_Destruct, fresh);
    hash.Mix(elements.h);
    return hash.h;
}

uint32_t DriveRul5()
{
    const uint32_t rul1 = DriveRul1Map();
    const uint32_t layouts = DriveBridgeLayouts();
    Hash total;
    total.Mix(rul1);
    total.Mix(layouts);
    log::Write("autotest: ntrul: drive r5: rul1 map %08X, bridge layouts %08X, total %08X", rul1, layouts, total.h);
    return total.h;
}

bool g_done = false;
uint32_t g_drive = 0;
uint32_t g_drive2 = 0;
uint32_t g_drive3 = 0;
uint32_t g_drive4 = 0;
uint32_t g_drive5 = 0;

void Drive(const Day& day)
{
    if (g_done || !day.counted || !OnRunDay(day, "OPENSC4_TEST_NETTOOL_RUL"))
        return;
    g_done = true;
    Hash total;
    const uint32_t mirror = DriveMirror();
    total.Mix(mirror);

    // The store's keys, smallest first (not the store's bucket order).
    std::vector<std::pair<uint32_t, char*>> rules;
    ForEachNode(Store(), [&](char* node) { rules.emplace_back(Word(node, rul::table::kKey), node); });
    std::sort(rules.begin(), rules.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    if (rules.empty()) {
        log::Write("autotest: ntrul: drive: mirror %08X, the store is empty", mirror);
        g_drive = total.h;
        g_drive2 = DriveRuleRecords();
        g_drive3 = DriveRul3();
        g_drive4 = DriveRul4();
        g_drive5 = DriveRul5();
        return;
    }

    uint32_t key = rules.front().first;
    char* const found = CallThis<char*, uint32_t*>(addr::SC4Rul0Store_FindOrInsert, Store(), &key);
    const bool same = found == rules.front().second + rul::store::kRule;
    total.Mix(same);
    total.Mix(RuleHash(found));

    uint32_t transformed = 0;
    Hash steps;
    for (const auto& [id, node] : rules) {
        if (transformed >= 48)
            break;
        char* const source = node + rul::store::kRule;
        if (!HasCellGrid(source))
            continue;
        ++transformed;
        alignas(4) char copy[rul::rul0::kSize] = {};
        CallThis<char*, char*>(addr::SC4Rul0Rule_CopyConstruct, copy, source);
        steps.Mix(id);
        steps.Mix(RuleHash(copy));
        for (int i = 0; i < 3; ++i) {
            CallThis<void, uint32_t>(addr::SC4Rul0Rule_Rotate, copy, 1u);
            steps.Mix(RuleHash(copy));
        }
        CallThis<void>(addr::SC4Rul0Rule_Transpose, copy);
        steps.Mix(RuleHash(copy));
        CallThis<void, int32_t, int32_t>(addr::SC4Rul0Rule_Translate, copy, 3, -2);
        steps.Mix(RuleHash(copy));
        CallThis<void, uint32_t>(addr::SC4Rul0Rule_Rotate, copy, 2u);
        steps.Mix(RuleHash(copy));
        CallThis<void>(addr::SC4Rul0Rule_Destruct, copy);
    }
    total.Mix(transformed);
    total.Mix(steps.h);
    g_drive = total.h;
    log::Write("autotest: ntrul: drive: mirror %08X, lookup %s, %u of %u rules transformed (hash %08X), total %08X",
               mirror, same ? "same node" : "OTHER NODE", transformed, static_cast<uint32_t>(rules.size()), steps.h,
               g_drive);
    g_drive2 = DriveRuleRecords();
    log::Write("autotest: ntrul: drive r2 %08X", g_drive2);
    g_drive3 = DriveRul3();
    g_drive4 = DriveRul4();
    g_drive5 = DriveRul5();
}

// Once per run, a log line (never hashed) on the fields FindOrInsert's
// stack temporaries leave in a new rule: the original stores its stack's
// leftovers there, ours zeros. How many stored rules have +0x7C set, the
// first distinct values, and how many rules without a cell grid have
// nonzero bounds or width, or +0x40/+0x44 set.
bool g_probed = false;
void ProbeLeftovers()
{
    g_probed = true;
    uint32_t rules = 0, word7C = 0, noGrid = 0, noGridSet = 0, words40 = 0;
    uint32_t values[8] = {};
    uint32_t distinct = 0;
    ForEachNode(Store(), [&](char* node) {
        char* const r = node + rul::store::kRule;
        ++rules;
        const uint32_t v = Word(r, rul::rul0::kWord7C);
        if (v) {
            ++word7C;
            bool seen = false;
            for (uint32_t i = 0; i < distinct; ++i)
                seen = seen || values[i] == v;
            if (!seen && distinct < 8)
                values[distinct++] = v;
        }
        if (Word(r, 0x40) || Word(r, 0x44))
            ++words40;
        if (!HasCellGrid(r)) {
            ++noGrid;
            if (Word(r, 0x18) || Word(r, 0x1C) || Word(r, 0x20) || Word(r, 0x24) || Word(r, rul::rul0::kCellWidth))
                ++noGridSet;
        }
    });
    log::Write("autotest: ntrul: leftovers: %u rules, +0x7C set on %u (%08X %08X %08X %08X %08X %08X %08X %08X), "
               "+0x40/+0x44 set on %u, %u without a cell grid of which %u have bounds or width set",
               rules, word7C, values[0], values[1], values[2], values[3], values[4], values[5], values[6], values[7],
               words40, noGrid, noGridSet);
}

void Fingerprint(cISC4City*, std::string& line)
{
    if (Env("OPENSC4_TEST_NETTOOL_RUL").empty())
        return;
    if (!g_probed && Word(Store(), rul::table::kCount))
        ProbeLeftovers();
    uint32_t storeCount = 0, storeHash = 0, mapCount = 0, mapHash = 0;
    StoreHash(storeCount, storeHash);
    MultimapHash(mapCount, mapHash);
    uint32_t files = 0, filesHash = 0;
    FilesHash(files, filesHash);
    char text[128];
    std::snprintf(text, sizeof text, " | ntrul s%u:%08X m%u:%08X t%08X f%u:%08X", storeCount, storeHash, mapCount,
                  mapHash, RowsHash(), files, filesHash);
    line += text;
    if (g_done) {
        std::snprintf(text, sizeof text, " d%08X e%08X", g_drive, g_drive2);
        line += text;
    }
    // Part r3, after everything of r1 and r2.
    uint32_t nodes3C = 0, rul1Count = 0, records = 0, bridges = 0, elements = 0;
    const uint32_t tables3C = Tables3CHash(nodes3C);
    const uint32_t rul1 = Rul1MapHash(rul1Count);
    BridgesHash(records, bridges, elements);
    std::snprintf(text, sizeof text, " p%08X x%u:%08X r%u:%08X b%u:%08X/%08X", PlacementHash(), nodes3C, tables3C,
                  rul1Count, rul1, records, bridges, elements);
    line += text;
    if (g_done) {
        std::snprintf(text, sizeof text, " g%08X", g_drive3);
        line += text;
    }
    // Part r4, after everything of r1 to r3.
    uint32_t verifiers = 0;
    const uint32_t verifiersHash = TileVerifiersHash(verifiers);
    std::snprintf(text, sizeof text, " v%u:%08X w%08X", verifiers, verifiersHash, BridgeFieldsHash());
    line += text;
    if (g_done) {
        std::snprintf(text, sizeof text, " k%08X", g_drive4);
        line += text;
    }
    // Part r5, after everything of r1 to r4.
    uint32_t bridgeRecords = 0, always = 0, property = 0;
    BridgeRecordFieldsHash(bridgeRecords, always, property);
    std::snprintf(text, sizeof text, " y%u:%08X/%08X", bridgeRecords, always, property);
    line += text;
    if (g_done) {
        std::snprintf(text, sizeof text, " h%08X", g_drive5);
        line += text;
    }
}

const FingerprintPart kFingerprint{"ntrul", 177, &Fingerprint};
const DayHook kDrive{"ntrul.drive", 199, &Drive};

} // namespace
} // namespace opensc4::autotest
