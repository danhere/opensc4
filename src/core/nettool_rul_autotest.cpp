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

bool g_done = false;
uint32_t g_drive = 0;
uint32_t g_drive2 = 0;

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
}

const FingerprintPart kFingerprint{"ntrul", 177, &Fingerprint};
const DayHook kDrive{"ntrul.drive", 199, &Drive};

} // namespace
} // namespace opensc4::autotest
