// The network tool's occupants, tunnels and cost (build 641; part t7): the
// layouts the part's code reads and the helpers its two files share
// (src/core/nettool_occupants_1.cpp: slots 54, 55 and 72 with their
// helpers, whose top has the NAM, register, float and stack leftover
// analysis of the whole part; src/core/nettool_occupants_2.cpp: the tunnels,
// the edge connections, the RUL0 intersections' occupants, GetCostOfSolution
// and the bridge cell vectors) and their test
// (src/core/nettool_occupants_autotest.cpp). Each helper is a call of a game
// function at its address or the game's inlined code; all of it works on
// the game's memory in place.
#pragma once

#include "core/nettool_rul.h"
#include "core/nettool_sim.h"

#include <opensc4/addresses.h>

#include <bit>
#include <cstddef>
#include <cstdint>

namespace opensc4::nettool_sim::occupants {

// ====================================================================================
// Layouts
// ====================================================================================

// The tool's fields this part reads beyond nettool_sim.h's (offsets from
// the tool's start; +0x290 is the placement parameters, so +0x2C8 is their
// +0x38 and so on).
namespace ofield {
inline constexpr std::size_t kWord74 = 0x74;                          // inf: the regular step count
inline constexpr std::size_t kDirection7C = 0x7C, kDirection80 = 0x80; // the drag's directions (0..7)
inline constexpr std::size_t kStepsEnd = 0x58;
inline constexpr std::size_t kIntersectionsEnd = 0xD0, kBridgesEnd = 0xDC, kRecordsEnd = 0xE8;
inline constexpr std::size_t kEdgeConnections = 0x100;   // vector of 0x18-byte records (below)
inline constexpr std::size_t kAnchorsEnd = 0x11C;
inline constexpr std::size_t kHighlightCost = 0x220;     // the low dword of the last highlight query's cost
inline constexpr std::size_t kVerticesZ = 0x24C;
inline constexpr std::size_t kClearance = 0x2C8;         // float: a vertex this far above the terrain wants piers
inline constexpr std::size_t kTunnelPieces = 0x2E4, kTunnelPiecesEnd = 0x2E8; // params +0x54: uint32 instances
inline constexpr std::size_t kTunnelTurns = 0x2F0;       // params +0x60: uint32 (low byte a turn)
inline constexpr std::size_t kTunnelHeights = 0x2FC;     // params +0x6C: floats
inline constexpr std::size_t kBridgeStepCost = 0x320;    // params +0x90
inline constexpr std::size_t kRecordStepCost = 0x324;    // params +0x94
inline constexpr std::size_t kHeightCostScale = 0x328;   // params +0x98: float
inline constexpr std::size_t kDemolitionCost = 0x32C;    // params +0x9C
} // namespace ofield

// A type row's words this part reads (row::kParams + n for the params).
namespace orow {
inline constexpr std::size_t kGroup = 0x0C;      // the exemplar group of the type's pieces
inline constexpr std::size_t kPieceTable = 0x3C; // the hash table FindPieceRecord searches
inline constexpr std::size_t kPylons = 0xF0;     // params +0xA0: the pylon IDs (uint32 vector)
inline constexpr std::size_t kCellCost = 0xDC;   // params +0x8C: a cell's cost for the type
inline constexpr std::size_t kCrossTypes = 0xFC; // a bit per network type (RUL1's type word)
} // namespace orow

// A bridge record's cost words (row::kBridges, 0x178 bytes each).
namespace obridge {
inline constexpr std::size_t kCostBase = 0x150, kCostPerStep = 0x154;
} // namespace obridge

// A major bridge (+0xD8, 0x28 bytes) as GetCostOfSolution reads it: the
// network type, the first and the last step, the record's index.
namespace omajor {
inline constexpr std::size_t kSize = 0x28, kType = 0x0, kFirst = 0x4, kLast = 0x8, kRecord = 0xC;
} // namespace omajor

// A +0xE4 record (0x10 bytes): the two heights, the first and the last step.
namespace orecord {
inline constexpr std::size_t kSize = 0x10, kFirstHeight = 0x0, kLastHeight = 0x4, kFirst = 0x8, kLast = 0xC;
} // namespace orecord

// An intersection (+0xCC, 0x28 bytes): the RUL0 rule ID, x, z, and the
// vector of its cell keys at +0x1C.
namespace oisect {
inline constexpr std::size_t kSize = 0x28, kRule = 0x0, kX = 0x4, kZ = 0x8, kCells = 0x1C, kCellsEnd = 0x20;
} // namespace oisect

// What this part reads of a RUL0 rule beyond nettool_rul.h's: the cost
// (+0xB4) GetCostOfSolution adds, the piece base (+0xC8) and the path ID
// base (+0xCC) of the intersection occupants.
namespace orule {
inline constexpr std::size_t kCost = 0xB4, kPieceBase = 0xC8, kPathBase = 0xCC;
inline constexpr std::size_t kConsBuckets = 0xD4, kConsBucketsEnd = 0xD8; // the cons cell table's buckets
inline constexpr std::size_t kValueId = 0x0, kValueTurn = 0x4;            // a cons cell's value (node + 8)
} // namespace orule

// An edge connection (0x18 bytes, AddEdgeConnection 0x00628BD0 appends one
// to +0x100 and +0x10C): the cell key, the edge value, the cell across the
// city edge (x, z), the neighbour kind, a byte (+0x15 to +0x17 are the
// original's stack leftovers, zeros in ours).
namespace oedge {
inline constexpr std::size_t kSize = 0x18, kKey = 0x0, kEdge = 0x4, kX = 0x8, kZ = 0xC, kKind = 0x10, kByte = 0x14;
} // namespace oedge

// A major bridge's cell (0x50 bytes; the vector at the bridge's +0x1C):
// words, a byte +0x10, words +0x14 to +0x3C, a byte +0x40 and a vector of
// 32-byte elements at +0x44.
namespace bcell {
inline constexpr std::size_t kSize = 0x50, kByte10 = 0x10, kByte40 = 0x40, kVector = 0x44;
} // namespace bcell

// The kinds slot 54 makes (the occupant's class or interface IDs).
inline constexpr uint32_t kKindNetwork = 0;
inline constexpr uint32_t kKindPrebuilt = 0x49C1A034;
inline constexpr uint32_t kKindBridge = 0x49CC1BCD;
inline constexpr uint32_t kKindTunnel = 0x8A4BD52B;
inline constexpr uint32_t kSizeNetwork = 0x180, kSizePrebuilt = 0x1D8, kSizeBridge = 0x1F0, kSizeTunnel = 0x1E0;

// The exemplars (resource manager slot 3: key, IID, out, 0, 0).
inline constexpr uint32_t kExemplarType = 0x6534284A, kExemplarIid = 0xA52160F5;
inline constexpr uint32_t kRuleCellGroup = 0xA8434037; // the RUL0 intersections' pieces
// The preview texture's type and group (the instance from MapPreviewTexture).
inline constexpr uint32_t kTextureType = 0x7AB50E44, kTextureGroup = 0x1ABE787D;
inline constexpr uint32_t kColourIntersection = 0xFF0000FF, kColourNoPiece = 0xFF8080FF;
inline constexpr uint32_t kColourTunnel = 0x8000FF00;
inline constexpr uint32_t kLotOffset = 100000; // a network lot's own vertices

// The power pole (the occupants slot 72's cell clearing may demolish).
inline constexpr uint32_t kTypePowerPole = 0x2890D4DE, kIidPowerPole = 0xA8C86142;

// The network occupant's flags this part sets and clears.
namespace oflag {
inline constexpr uint32_t kTranslucent = 0x4000, kImmovable = 0x400000, kNetworkLot = 0x200000;
inline constexpr uint32_t kPlanar = 0x1000000, kFlag2000000 = 0x2000000, kFlag4000000 = 0x4000000;
inline constexpr uint32_t kFlag8000000 = 0x8000000, kUsable = 0x10000000, kSupportPylon = 0x40000000;
inline constexpr uint32_t kFlag80000000 = 0x80000000, kFlags500000 = 0x500000;
inline constexpr uint32_t kStepKindMask = 0xC0000; // what slot 54 keeps of a cell's flags in a preview
} // namespace oflag

// The network occupant's slots (byte offsets, netocc_sim.h has the names)
// and the occupant part's.
namespace oslot {
inline constexpr std::size_t kQueryInterface = 0x0, kAddRef = 0x4, kRelease = 0x8, kInit = 0xC;
inline constexpr std::size_t kHasFlag = 0x20, kSetFlag = 0x28, kClearFlag = 0x2C, kGetFlags = 0x30;
inline constexpr std::size_t kGetNodeEdgeTypes = 0x34, kGetPathOverride = 0x3C, kSetUsable = 0x50;
inline constexpr std::size_t kGetRotationAndFlip = 0x68, kSetPiece = 0x78, kSetEdgeTypes = 0x94;
inline constexpr std::size_t kAsOccupant = 0xC0, kGetPathInfo = 0xC4, kSetPathIdOverride = 0xCC;
inline constexpr std::size_t kGetAltitude = 0xE0, kGetNormal = 0xE4, kSetDemolitionCost = 0x100;
inline constexpr std::size_t kGetOneWay = 0x114, kSetOneWay = 0x118, kSetComplexOrigin = 0x120;
// The occupant part's (cISC4Occupant).
inline constexpr std::size_t kGetType = 0x1C, kGetPosition = 0x20, kOccupantSlot80 = 0x80;
// The path information's slot the tunnels call.
inline constexpr std::size_t kPathLink = 0x80;
} // namespace oslot

// The tool's own slots this part calls through the vtable.
namespace tslot {
inline constexpr std::size_t kDrawNetworkLine = 0x48, kCreateNetworkOccupant = 0xD8;
inline constexpr std::size_t kAddCellPreview = 0xE8, kInsertNetworkOccupant = 0x120;
inline constexpr std::size_t kGetNetworkVertexHt = 0x124, kSetQuadTrianglesToDraw = 0x140;
} // namespace tslot

// The other objects' slots.
namespace xslot {
inline constexpr std::size_t kGetResource = 0xC;                       // the resource manager
inline constexpr std::size_t kInsertOccupant = 0x50, kRemoveOccupant = 0x54; // the occupant manager
inline constexpr std::size_t kGetByCityCells = 0xA0, kReleaseOccupantList = 0xC0;
inline constexpr std::size_t kFilterMatches = 0xC;                     // a network occupant filter
inline constexpr std::size_t kDemolishOccupant = 0x20;                 // the demolition service 0x00B43D00
inline constexpr std::size_t kMapPreviewTexture = 0x34;                // the network manager
inline constexpr std::size_t kTerrainCellSize = 0x18, kTerrainVertexAltitude = 0x30; // the terrain
inline constexpr std::size_t kTerrainSetVertexAltitude = 0xC8, kTerrainBeginUpdate = 0x15C;
inline constexpr std::size_t kTerrainUpdateRect = 0x110, kTerrainEndUpdate = 0x160;
inline constexpr std::size_t kNeighborCityAt = 0x30, kNeighborConnectionAt = 0x2C; // 0x00B43D8C
inline constexpr std::size_t kListAllocate = 0xC;                      // the list node allocator
} // namespace xslot

// The types whose tools never clear power poles off a cell (power line,
// subway, pipe), the network types' count.
inline constexpr uint32_t kTypePowerLine = 5, kTypeSubway = 7, kTypePipe = 4;

// ====================================================================================
// Helpers
// ====================================================================================

// A list node of the game's lists (the head is a node of its own).
struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};

inline float Float(uint32_t bits) { return std::bit_cast<float>(bits); }
inline uint32_t Bits(float value) { return std::bit_cast<uint32_t>(value); }

inline char* Entry(char* self, uint32_t key)
{
    return CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, key);
}
inline uint32_t TypeFlag(uint32_t type)
{
    return Global<uint32_t>(static_cast<uint32_t>(addr::cSC4NetworkTool_TypeFlags + type * 4));
}
// A type's row as the game indexes the table (32-bit arithmetic: the
// game's type 13, "no type", lands just past the table).
inline char* Row(uint32_t type)
{
    const uint32_t address =
        static_cast<uint32_t>(addr::cSC4NetworkConstructionCrew_NetworkToolTuning + type * kRowSize);
    return reinterpret_cast<char*>(static_cast<std::uintptr_t>(address));
}
inline char* ToolRow(char* self) { return At<char*>(self, field::kRow); }
inline void* Terrain(char* self) { return At<void*>(self, field::kTerrain); }
inline char* Cells(char* self) { return At<Vector>(self, field::kCells).begin; }
inline char* Steps(char* self) { return At<Vector>(self, field::kSteps).begin; }
inline uint32_t PointKey(const char* point)
{
    return (At<uint32_t>(const_cast<char*>(point), 4) << 16) + At<uint32_t>(const_cast<char*>(point), 0);
}

// A word at a computed address: the game reads through null records'
// offsets where it does not test them; so do ours.
template <typename T>
inline T Read(const void* base, std::size_t offset)
{
    return *reinterpret_cast<const volatile T*>(reinterpret_cast<std::uintptr_t>(base) + offset);
}

// The node of `key` in a hash table (nettool_rul.h's layout) as the game's
// inlined searches find it: bucket key % ((end - begin) >> 2, arithmetic),
// the division unsigned, then the chain.
inline char* FindNode(char* table, uint32_t key)
{
    char* const begin = At<char*>(table, rul::table::kBuckets);
    const uint32_t count =
        static_cast<uint32_t>(static_cast<int32_t>(At<char*>(table, rul::table::kBucketsEnd) - begin) >> 2);
    char* node = reinterpret_cast<char**>(begin)[key % count];
    while (node && At<uint32_t>(node, rul::table::kKey) != key)
        node = At<char*>(node, rul::table::kNext);
    return node;
}
// The rule of an ID in the RUL0 store (node + 8), or null.
inline char* StoreRule(uint32_t id)
{
    char* const node = FindNode(reinterpret_cast<char*>(addr::cSC4NetworkTool_Rul0Store), id);
    return node ? node + rul::store::kRule : nullptr;
}

// An exemplar from the resource manager (key, IID, out, 0, 0); AL.
inline uint8_t GetResource(const uint32_t* key, void** out)
{
    return Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), xslot::kGetResource, key, kExemplarIid,
                            out, 0u, 0u);
}

// A value appended to a list as the game's inlined push_back of these
// functions does: a 12-byte node from SC4::stl_allocate, the value written
// unless the node is null, the node linked before the head.
inline void PushBack(ListNode* head, void* value)
{
    ListNode* const node = CallCdecl<ListNode*, uint32_t>(addr::SC4_stl_allocate, sizeof(ListNode));
    if (reinterpret_cast<std::uintptr_t>(node) + 8 != 0)
        node->value = value;
    ListNode* const last = head->previous;
    node->next = head;
    node->previous = last;
    last->next = node;
    head->previous = node;
}

// The network occupant's flag calls.
inline void SetFlag(void* occupant, uint32_t flag) { Virtual<void>(occupant, oslot::kSetFlag, flag); }
inline void ClearFlag(void* occupant, uint32_t flag) { Virtual<void>(occupant, oslot::kClearFlag, flag); }

// The game's static tables and constants this part reads.
inline int32_t EdgeSideDx(uint32_t side)
{
    return Global<int32_t>(static_cast<uint32_t>(addr::cSC4NetworkTool_EdgeSideDx + side * 4));
}
inline int32_t EdgeSideDz(uint32_t side)
{
    return Global<int32_t>(static_cast<uint32_t>(addr::cSC4NetworkTool_EdgeSideDz + side * 4));
}
inline int32_t EdgePriority(uint32_t type)
{
    return Global<int32_t>(static_cast<uint32_t>(addr::cSC4NetworkTool_EdgeTypePriority + type * 4));
}
inline uint32_t NeighborKind(uint32_t type)
{
    return Global<uint32_t>(static_cast<uint32_t>(addr::cSC4NetworkTool_NeighborConnectionKinds + type * 4));
}
inline uint32_t PreviewHeight(uint32_t type)
{
    return Global<uint32_t>(static_cast<uint32_t>(addr::cSC4NetworkTool_PreviewHeightByType + type * 4));
}

} // namespace opensc4::nettool_sim::occupants
