// The network tool's tile solver (build 641; part t8): the layouts the
// solver's code reads and the helpers its two files share
// (src/core/nettool_solve_1.cpp: the solver, the rule files, the overrides,
// the verifiers and the cell sets, whose top has the NAM, register and stack
// leftover analysis of the whole part; src/core/nettool_solve_2.cpp: the edge
// fix-ups, PatchTilePair and the drag's cells) and their test
// (src/core/nettool_solve_autotest.cpp). Each helper is the game's inlined
// code (a hash set search or increment, a push_back) or a call of a game
// function at its address; all of it works on the game's memory in place.
#pragma once

#include "core/nettool_rul.h"
#include "core/nettool_sim.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim::solve {

namespace table = rul::table;

// The layouts of this part (offsets from the tool's start unless said).
// The vector ResolveCellTypes appends {x, z, flags} records to.
inline constexpr std::size_t kOverrides = 0x08;
// A solved cell (12 bytes): the piece, the rotation byte (+0x5 to +0x7 the
// original's stack leftovers, see nettool_solve_1.cpp's top), the cell key.
inline constexpr std::size_t kSolvedSize = 0xC, kPiece = 0x0, kRotation = 0x4, kKey = 0x8;
// The intersections (+0xCC, 0x28 bytes).
inline constexpr std::size_t kIntersectionEnd = 0xD0, kIntersectionSize = 0x28;
inline constexpr std::size_t kIntersectionRule = 0x0, kIntersectionX = 0x4, kIntersectionZ = 0x8;
inline constexpr std::size_t kIntersectionStep = 0x14, kIntersectionCells = 0x1C;
// The major bridges (+0xD8, 0x28 bytes): a step, a vector of 0x50-byte
// cells with x +0x4 and z +0x8.
inline constexpr std::size_t kBridgeEnd = 0xDC, kBridgeSize = 0x28, kBridgeStep = 0x8, kBridgeCells = 0x1C;
inline constexpr std::size_t kBridgeCellSize = 0x50, kBridgeCellX = 0x4, kBridgeCellZ = 0x8;
// The +0xE4 records (0x10 bytes: the first and the last step).
inline constexpr std::size_t kRecordsE4 = 0xE4, kRecordE4End = 0xE8, kRecordE4Size = 0x10;
inline constexpr std::size_t kRecordFirstStep = 0x8, kRecordLastStep = 0xC;
// The rule cell records (+0xF0, 0x14 bytes: x, z).
inline constexpr std::size_t kRuleCellsEnd = 0xF4, kRuleCellSize = 0x14;
// A step (+0x54): the first and the last dragged cell.
inline constexpr std::size_t kStepFirst = 0x0, kStepLast = 0x4;
// The cell info's mark the drag code sets.
inline constexpr std::size_t kMarked = 0x57;
// The RUL2 multimap's node (the value at +0x10: the key, four pairs of a
// piece word and a rotation byte).
inline constexpr std::size_t kPair1Rotation = 0x18, kPair2Piece = 0x1C, kPair2Rotation = 0x20;
inline constexpr std::size_t kPair3Piece = 0x24, kPair3Rotation = 0x28, kPair4Piece = 0x2C, kPair4Rotation = 0x30;
// A tile verifier node: next, the key, a vector of 8-byte entries (a piece
// word, its rotation byte, a direction byte).
inline constexpr std::size_t kVerifierEntries = 0x8, kVerifierEntriesEnd = 0xC, kVerifierEntrySize = 0x8;
inline constexpr std::size_t kEntryPiece = 0x0, kEntryRotation = 0x4, kEntryDirection = 0x5;
// A RUL1 pair table node: next, the two edge words (the key), the piece, the
// rotation byte.
inline constexpr std::size_t kPairLow = 0x4, kPairHigh = 0x8, kPairPiece = 0xC, kPairRotation = 0x10;
// A rule tile (8 bytes: the piece, a position byte, the rotation byte).
inline constexpr std::size_t kTilePiece = 0x0, kTileRotation = 0x5, kTileSize = 0x8;
// The checked points of a RUL0 rule (+0x4C, 8-byte points).
inline constexpr std::size_t kRuleChecked = 0x4C, kRuleCheckedEnd = 0x50, kRulePointSize = 0x8;
// The network occupant's slots.
inline constexpr std::size_t kOccupantFlags = 0x30, kOccupantLotEdges = 0x34, kOccupantPiece = 0x5C;
inline constexpr std::size_t kOccupantRotation = 0x68;
// What the code tests: the edge bits above a side's two kind bits, the
// layered networks (types 2, 8 and 9), the types whose edges the one-way
// byte follows, the one-way road.
inline constexpr uint32_t kHighEdgeBits = 0xF8F8F8F8, kLayered = 0x304, kOneWaySources = 0x849;
inline constexpr uint32_t kOneWayType = 10;
inline constexpr uint32_t kSides = 4;
inline constexpr uint32_t kBuckets = 100;

// The game's allocator, free and memmove.
inline char* Allocate(uint32_t size)
{
    return CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, size);
}
inline void Free(void* block)
{
    CallCdecl<void, void*>(addr::SC4_Free, block);
}
inline void FreeIfSet(void* block)
{
    if (block)
        Free(block);
}

// The static tables.
inline uint32_t TypeFlag(uint32_t type)
{
    return Global<uint32_t>(addr::cSC4NetworkTool_SolverTypeFlags + type * 4);
}
inline uint32_t SideDx(uint32_t side)
{
    return Global<uint32_t>(addr::cSC4NetworkTool_SideDx + side * 4);
}
inline uint32_t SideDz(uint32_t side)
{
    return Global<uint32_t>(addr::cSC4NetworkTool_SideDz + side * 4);
}
inline int32_t Priority(uint32_t type)
{
    return Global<int32_t>(addr::cSC4NetworkTool_TypePriority + type * 4);
}
inline char* TypeRow(uint32_t type)
{
    return reinterpret_cast<char*>(addr::cSC4NetworkConstructionCrew_NetworkToolTuning + type * kRowSize);
}

// A cell key from x and z, as the game adds them (32 bits, wrapping).
inline uint32_t Key(uint32_t x, uint32_t z)
{
    return (z << 16) + x;
}

// The tool's cell entry (made when the cache has none), a cell info's edge
// word by type (the type as a 32-bit index, as the game scales it).
inline char* Entry(char* self, uint32_t key)
{
    return CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, key);
}
inline uint32_t* EdgeWord(char* info, uint32_t type)
{
    return reinterpret_cast<uint32_t*>(info + cell::kByType + type * 4);
}
inline void Combine(char* info)
{
    CallThis<uint32_t>(addr::cSC4NetworkCellInfo_CombineEdges, info);
}

// The hash sets (`rul::table`): the bucket count, the node of a key (the
// game's inlined search), the node after `node` (its inlined increment),
// and the game's own Construct, Resize + Insert, Erase, EraseAt, Clear and
// Begin.
inline uint32_t BucketCount(char* set)
{
    const Vector& buckets = At<Vector>(set, table::kBuckets);
    return static_cast<uint32_t>(static_cast<int32_t>(buckets.end - buckets.begin) >> 2);
}
inline char* FindNode(char* set, uint32_t key)
{
    char* node = reinterpret_cast<char**>(At<Vector>(set, table::kBuckets).begin)[key % BucketCount(set)];
    while (node && At<uint32_t>(node, table::kKey) != key)
        node = At<char*>(node, table::kNext);
    return node;
}
inline char* NextNode(char* node, char* set)
{
    if (char* const next = At<char*>(node, table::kNext))
        return next;
    const uint32_t count = BucketCount(set);
    uint32_t index = At<uint32_t>(node, table::kKey) % count;
    while (++index < BucketCount(set))
        if (char* const head = reinterpret_cast<char**>(At<Vector>(set, table::kBuckets).begin)[index])
            return head;
    return nullptr;
}
inline void Construct(char* set)
{
    CallThis<void*, uint32_t>(addr::SC4Uint32HashSet_Construct, set, solve::kBuckets);
}
inline void Insert(char* set, const uint32_t* key)
{
    CallThis<void, uint32_t>(addr::SC4Uint32HashSet_Resize, set, At<uint32_t>(set, table::kCount) + 1);
    uint32_t result[4] = {0, 0, 0, 0};
    CallThis<void*, uint32_t*, const uint32_t*>(addr::SC4Uint32HashSet_Insert, set, result, key);
}
inline void Erase(char* set, const uint32_t* key)
{
    CallThis<uint32_t, const uint32_t*>(addr::SC4Uint32HashSet_Erase, set, key);
}
inline void EraseAt(char* set, char* node)
{
    std::uintptr_t iterator[2] = {reinterpret_cast<std::uintptr_t>(node), reinterpret_cast<std::uintptr_t>(set)};
    CallThis<void, std::uintptr_t*>(addr::SC4Uint32HashMap_EraseAt, set, iterator);
}
inline void Destroy(char* set)
{
    CallThis<void>(addr::SC4Uint32HashSet_Clear, set);
    FreeIfSet(At<Vector>(set, table::kBuckets).begin);
}
inline char* Begin(char* set)
{
    std::uintptr_t iterator[2] = {0, 0};
    CallThis<void*, std::uintptr_t*>(addr::SC4HashTable_Begin, set, iterator);
    return reinterpret_cast<char*>(iterator[0]);
}
inline uint32_t Count(char* set)
{
    return At<uint32_t>(set, table::kCount);
}

// A uint32 appended to a vector as the game's inlined push_back does: in
// place (nothing written at a null end), else new memory for size +
// max(size, 1) words (SC4::stl_allocate), the old words moved (SC4::memmove,
// only when there are some), the value stored, the old memory freed.
inline void PushWord(Vector& v, uint32_t value)
{
    if (v.end != v.capacity) {
        if (v.end)
            At<uint32_t>(v.end, 0) = value;
        v.end += 4;
        return;
    }
    const uint32_t size = static_cast<uint32_t>(static_cast<int32_t>(v.end - v.begin) >> 2);
    const uint32_t grown = (size < 1 ? 1u : size) + size;
    char* const buffer = grown ? Allocate(grown * 4) : nullptr;
    char* at = buffer;
    if (v.end != v.begin) {
        const uint32_t bytes = static_cast<uint32_t>(v.end - v.begin);
        at = CallCdecl<char*, char*, char*, uint32_t>(addr::SC4_memmove, buffer, v.begin, bytes) + bytes;
    }
    At<uint32_t>(at, 0) = value;
    FreeIfSet(v.begin);
    v.begin = buffer;
    v.end = at + 4;
    v.capacity = buffer + grown * 4;
}

// A 12-byte record appended as the game's inlined push_back does (in place,
// nothing written at a null end; else the vector's InsertOverflow with the
// value's address, an unread tag, 1 and 1).
inline void PushRecord(Vector& v, const uint32_t* record)
{
    if (v.end != v.capacity) {
        if (v.end) {
            At<uint32_t>(v.end, 0) = record[0];
            At<uint32_t>(v.end, 4) = record[1];
            At<uint32_t>(v.end, 8) = record[2];
        }
        v.end += 0xC;
        return;
    }
    uint8_t tag = 0;
    CallThis<void, char*, const uint32_t*, uint8_t*, uint32_t, uint32_t>(addr::SC4Vector12_InsertOverflow, &v, v.end,
                                                                         record, &tag, 1u, 1u);
}

// The solved count, as the game divides (signed by 12).
inline int32_t SolvedCount(char* self)
{
    const Vector& solved = At<Vector>(self, field::kSolved);
    return static_cast<int32_t>(solved.end - solved.begin) / static_cast<int32_t>(solve::kSolvedSize);
}

// A cell's solved piece appended to +0x98 with its index written into the
// cell info first (when there is one).
inline void AddSolved(char* self, char* info, uint32_t piece, uint8_t rotation, uint32_t key)
{
    if (info)
        At<int32_t>(info, cell::kSolvedIndex) = SolvedCount(self);
    const uint32_t record[3] = {piece, rotation, key};
    PushRecord(At<Vector>(self, field::kSolved), record);
}

} // namespace opensc4::nettool_sim::solve
