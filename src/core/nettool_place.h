// The network tool's placing (build 641; part t6): the layouts the part's
// code reads and the helpers its two files share
// (src/core/nettool_place_1.cpp: PlaceAllSegments, the segment placer,
// slots 7 and 8 and the RUL0 intersections, whose top has the NAM, register,
// wrapper, float and stack leftover analysis of the whole part;
// src/core/nettool_place_2.cpp: the cell passes, the edge word helpers and
// the intersections' template code). Each helper is a call of a game
// function at its address or the game's inlined code; all of it works on
// the game's memory in place.
#pragma once

#include "core/nettool_occupants.h"
#include "core/nettool_rul.h"
#include "core/nettool_sim.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim::place {

using occupants::Bits;
using occupants::Cells;
using occupants::Entry;
using occupants::Float;
using occupants::ListNode;
using occupants::Row;
using occupants::Steps;
using occupants::Terrain;
using occupants::TypeFlag;

// ====================================================================================
// Layouts
// ====================================================================================

// The tool's fields this part reads beyond nettool_sim.h's (offsets from
// the tool's start).
namespace pfield {
inline constexpr std::size_t kOverrides = 0x08;     // vector of 12-byte records {x, z, a word}
inline constexpr std::size_t kStepsEnd = 0x58;
inline constexpr std::size_t kByte6C = 0x6C;        // picks the side MarkWideSides marks (inf)
inline constexpr std::size_t kRegularSteps = 0x74;  // the regular step count
inline constexpr std::size_t kDirection1 = 0x7C, kDirection2 = 0x80; // the drag's directions (0..7)
inline constexpr std::size_t kSolvedEnd = 0x9C, kSolvedCapacity = 0xA0;
inline constexpr std::size_t kChanged = 0xA4;       // the hash set of changed cells (cell keys)
inline constexpr std::size_t kIntersectionsEnd = 0xD0, kIntersectionsCapacity = 0xD4;
inline constexpr std::size_t kMajorBridgesEnd = 0xDC, kRecordsE4End = 0xE8;
inline constexpr std::size_t kEdgeConnections = 0x100, kEdgeConnectionsEnd = 0x104;
inline constexpr std::size_t kNeighborConnectionsEnd = 0x110;
inline constexpr std::size_t kAnchorsEnd = 0x11C, kAnchorsCapacity = 0x120;
inline constexpr std::size_t kHighlightTree = 0x228;
inline constexpr std::size_t kVertexOffsetsA = 0x250, kVertexOffsetsB = 0x270; // by direction (eight words each)
inline constexpr std::size_t kTunnelAngle = 0x2AC;  // params +0x1C: a float whose tangent FindTunnelEnd takes
inline constexpr std::size_t kTunnelHeights = 0x2FC; // params +0x6C: floats
} // namespace pfield

// A type row's words this part reads.
namespace prow {
inline constexpr std::size_t kPenWidthX = 0x1C, kPenWidthZ = 0x20;
inline constexpr std::size_t kCrewC8 = 0xC8, kCrewCC = 0xCC, kCrewD0 = 0xD0, kCrewD4 = 0xD4, kCrewD8 = 0xD8;
} // namespace prow

// An intersection (+0xCC, 0x28 bytes): the RUL0 rule ID, x, z, the step it
// starts at, the rule's +0x10 and +0x14 plus that step (the second is the
// step the placing passes compare with the valid step count: inf, its last
// step), a word +0x18, and a uint32 vector at +0x1C.
namespace isect {
inline constexpr std::size_t kSize = 0x28, kRule = 0x0, kX = 0x4, kZ = 0x8, kFirstStep = 0xC;
inline constexpr std::size_t kWord10 = 0x10, kEndStep = 0x14, kWord18 = 0x18, kCells = 0x1C;
inline constexpr std::size_t kWords = 7; // the words +0x0 to +0x18 the copies move
} // namespace isect

// A major bridge (+0xD8, 0x28 bytes) as its assignment copies it: six words,
// a byte +0x18 and the cell vector +0x1C.
namespace mbridge {
inline constexpr std::size_t kSize = 0x28, kByte18 = 0x18, kCells = 0x1C;
} // namespace mbridge

// What the RUL0 rule's words this part reads are (nettool_rul.h's rul0).
namespace prule {
inline constexpr std::size_t kTypes = 0xE4;  // the network flags the rule applies to
inline constexpr std::size_t kEnabled = 0xE9; // uint8
inline constexpr std::size_t kWord10 = 0x10, kWord14 = 0x14;
} // namespace prule

// An edge connection record (0x18 bytes, +0x100 and +0x10C; t7's oedge).
namespace pedge {
inline constexpr std::size_t kSize = 0x18, kKey = 0x0, kEdge = 0x4, kX = 0x8, kZ = 0xC, kKind = 0x10, kByte = 0x14;
} // namespace pedge

// A rule cell record (+0xF0, 0x14 bytes): x, z, an edge word, a network
// type, the one-way bits and their mask.
namespace rcell {
inline constexpr std::size_t kSize = 0x14, kX = 0x0, kZ = 0x4, kEdge = 0x8, kType = 0xC, kBits = 0x10, kMask = 0x11;
} // namespace rcell

// A tunnel record (16 bytes; the +0xE4 vector's element): the start height,
// the end height, the start step, the end step.
namespace tunnel {
inline constexpr std::size_t kStartHeight = 0x0, kEndHeight = 0x4, kStartStep = 0x8, kEndStep = 0xC;
} // namespace tunnel

// The tool's slots this part calls through the vtable (byte offsets).
namespace pslot {
inline constexpr std::size_t kSetUsable = 0x50; // the network occupant's
inline constexpr std::size_t kInsertOccupantsForSolvedCells = 0xDC, kCanTilesBeSupported = 0xE0;
inline constexpr std::size_t kUpdateTerrainHeights = 0xE4, kUpdateOccupantHighlights = 0xEC;
inline constexpr std::size_t kDestroyHighlighted = 0xF0, kAllowTunnels = 0xF8;
inline constexpr std::size_t kIdentifyStartingConditions = 0x108, kCheckForReservedTiles = 0x110;
inline constexpr std::size_t kCheckForGoodTerrain = 0x114, kSlot70 = 0x118, kCreateCellInfos = 0x11C;
inline constexpr std::size_t kInsertNetworkOccupant = 0x120, kGetNetworkVertexHt = 0x124;
} // namespace pslot

// The other objects' slots.
namespace pxslot {
inline constexpr std::size_t kTerrainVertexAltitude = 0x30, kTerrainUnderWater = 0x88; // the terrain
inline constexpr std::size_t kNeighborAdd = 0x18, kNeighborRemove = 0x1C; // the neighbours simulator
inline constexpr std::size_t kCrewInit = 0x10;                           // the crew
inline constexpr std::size_t kOccupantEdges = 0x98, kOccupantPiece = 0x5C, kOccupantRotation = 0x68;
inline constexpr std::size_t kOccupantIsOfType = 0x58;
} // namespace pxslot

// The failure reasons the segment placer sets.
inline constexpr uint32_t kFailStart = 0xF0000001, kFailReserved = 0xF0000002, kFailBridges = 0xF0000003;
inline constexpr uint32_t kFailTunnels = 0xF0000004, kFailIntersections = 0xF0000005, kFailHeights = 0xF0000006;
inline constexpr uint32_t kFailSolve = 0xF0000007, kFailSupport = 0xF0000008;

inline constexpr uint32_t kTypeHighway = 2, kTypeOneWay = 10;
inline constexpr uint32_t kLotVertexOffset = 100000; // a network lot's own vertices (0x186A0)

// ====================================================================================
// Helpers
// ====================================================================================

inline Vector& VectorAt(char* object, std::size_t offset) { return At<Vector>(object, offset); }

// The 12-byte record push_back the game inlines where it has no
// InsertOverflow call (0x00629EB0 into +0x98, 0x0063B340 into +0x8): in
// place when there is room (nothing written at a null end, the end moved
// on all the same); else new memory for max(size, 1) + size records
// (unsigned compare), the old records copied (where the new memory is not
// null), the new one written after them, the old memory freed.
inline void PushRecord12(Vector& v, const uint32_t record[3])
{
    char* const end = v.end;
    if (end != v.capacity) {
        if (end) {
            reinterpret_cast<uint32_t*>(end)[0] = record[0];
            reinterpret_cast<uint32_t*>(end)[1] = record[1];
            reinterpret_cast<uint32_t*>(end)[2] = record[2];
        }
        v.end = end + 0xC;
        return;
    }
    const int32_t size = static_cast<int32_t>(end - v.begin) / 0xC;
    const uint32_t grow = static_cast<uint32_t>(size) < 1u ? 1u : static_cast<uint32_t>(size);
    const uint32_t count = grow + static_cast<uint32_t>(size);
    char* const memory = count ? CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, count * 0xC) : nullptr;
    char* to = memory;
    for (char* from = v.begin; from != end; from += 0xC, to += 0xC)
        if (to) {
            reinterpret_cast<uint32_t*>(to)[0] = reinterpret_cast<uint32_t*>(from)[0];
            reinterpret_cast<uint32_t*>(to)[1] = reinterpret_cast<uint32_t*>(from)[1];
            reinterpret_cast<uint32_t*>(to)[2] = reinterpret_cast<uint32_t*>(from)[2];
        }
    if (to) {
        reinterpret_cast<uint32_t*>(to)[0] = record[0];
        reinterpret_cast<uint32_t*>(to)[1] = record[1];
        reinterpret_cast<uint32_t*>(to)[2] = record[2];
    }
    if (v.begin)
        CallCdecl<void, void*>(addr::SC4_Free, v.begin);
    v.begin = memory;
    v.end = to + 0xC;
    v.capacity = memory + count * 0xC;
}

// The next node of an STLport hash table's iteration as the game inlines
// it: the node's next, else the first non-empty bucket after the node's
// own (key % bucket count, unsigned; the count by a signed shift), or null.
inline char* HashNext(char* table, char* node)
{
    if (char* const next = At<char*>(node, rul::table::kNext))
        return next;
    char** const begin = At<char**>(table, rul::table::kBuckets);
    const uint32_t count = static_cast<uint32_t>(
        static_cast<int32_t>(reinterpret_cast<char*>(At<char**>(table, rul::table::kBucketsEnd)) -
                             reinterpret_cast<char*>(begin)) >>
        2);
    uint32_t index = At<uint32_t>(node, rul::table::kKey) % count;
    for (;;) {
        ++index;
        if (index >= count)
            return nullptr;
        if (char* const found = At<char**>(table, rul::table::kBuckets)[index])
            return found;
    }
}

// The terrain's altitude at a vertex (slot 0x30, ST0).
inline double VertexAltitude(void* terrain, uint32_t vertex)
{
    return Virtual<double>(terrain, pxslot::kTerrainVertexAltitude, vertex);
}

} // namespace opensc4::nettool_sim::place
