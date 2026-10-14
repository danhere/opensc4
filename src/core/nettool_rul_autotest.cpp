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

bool g_done = false;
uint32_t g_drive = 0;
uint32_t g_drive2 = 0;
uint32_t g_drive3 = 0;

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
}

const FingerprintPart kFingerprint{"ntrul", 177, &Fingerprint};
const DayHook kDrive{"ntrul.drive", 199, &Drive};

} // namespace
} // namespace opensc4::autotest
