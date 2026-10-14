// The network tool's occupants, tunnels and cost in the autotest (part t7;
// src/core/nettool_occupants_1.cpp has the analysis).
//
// The daily part ` | ntoc e<edge records> <hash>` (always, once the network
// manager is initialised; ` | ntoc none` before) reads the network
// manager's own 13 tools (GetNetworkTool(type, 0): no reference added) and
// hashes the one persistent state this part leaves that no other part
// hashes: the city-edge connection records AddEdgeConnection appends to
// +0x100 and +0x10C (0x18 bytes each), in order, per tool its type and each
// record's key, edge value, cell across (x, z), kind and the byte +0x14
// (never +0x15 to +0x17: the original's stack leftovers). The occupants
// this part makes are the city's (hashed by the occupant parts' and the
// network manager's own fingerprints), the cell infos' marks and heights by
// the nettool part's `cells`, the tunnels' terrain by the terrain's. Values
// only, read in place; valid in every state the game leaves a tool in.
//
// OPENSC4_TEST_NETTOOL_COST=1 (with OPENSC4_TEST_NETTOOL=<day>) adds, on
// that day, a new road tool (GetNetworkTool(0, unique), AddRef, Init; at the
// end Shutdown and Release: the tool is deleted) and on it the tiles
// drive's set-up (src/core/nettool_solve_autotest.cpp: the cells with a
// network occupant around the first one found, written as the dragged cells
// with one regular step, CollectDraggedCells, SolveTiles (-1, 1) with +0x78
// = 0; only when the new tool's records are all empty), then, read-only:
//   cost    slot 20 GetCostOfSolution through the vtable (the solved cells'
//           cost words and height differences, the intersections' rule
//           costs);
//   cells   for each solved cell, at the game's addresses: FindPieceRecord
//           of its piece (found or not, the record's uint32 count),
//           GetPieceExemplar of its piece with the row's group (found or
//           not; the reference given back), VertexClearanceCategory of its
//           cell info, HasNeighborAlongX and HasNeighborAlongZ of its cell
//           with its flags;
//   rules   for each rule of the RUL0 store, its ID and for each of its
//           checked points ConsCellAt 0x00624F00 (and so the table find
//           0x00624D20): the cons cell's ID and turn byte, or none; summed
//           order-free.
// Nothing of it touches the city: the cell infos live in the new tool's
// world cache (the cell entry makes the neighbours' too), the exemplars are
// only looked up, the store only read (ConsCellAt never inserts). Slots 54,
// 55 and 72, PlaceRuleOccupants and PlaceRuleCellOccupant, the tunnels and
// AddEdgeConnection make or change occupants, the terrain or the store, so
// they are never driven. One log line `autotest: ntoc: ...` gives the counts
// and the stages' hashes; the daily part then ends with ` drive <hash>`.
//
// What the crew test (OPENSC4_TEST_CREW) reaches of this part: its drags go
// through PlaceAllSegments 0x0063BDC0, whose segment placer 0x0063B830 calls
// slot 55 (through the vtable, placing: slot 72 for each new cell, and so
// slot 54, FindPieceRecord, VertexClearanceCategory, ClearCellForOccupant,
// the prebuilt path with its pylon, SetUpPrebuiltOccupant, GetPieceExemplar
// and HasNeighborAlongX/Z where a piece has an exemplar; the network lot and
// immovable branches only over such cells), PlaceTunnels (its loop only
// with +0xE4 records: drags with tunnels), 0x0062C4B0 (PlaceRuleOccupants,
// PlaceRuleCellOccupant, ConsCellAt and the table find only with
// intersections) and AddEdgeConnection (only for a drag that ends at the
// city's edge). The preview branch of slot 55 runs while a drag is shown
// (the guarded 0x0062CD50). GetCostOfSolution is asked by the tool's view
// input control while dragging; the bridge cell vectors only copy major
// bridges (drags over water).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/netmgr_sim.h"
#include "core/nettool_occupants.h"
#include "core/nettool_sim.h"
#include "core/nettool_solve.h"

#include <cISC4City.h>
#include <cISC4NetworkManager.h>

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace opensc4::autotest {
namespace {

using nettool_sim::CallCdecl;
using nettool_sim::CallThis;
using nettool_sim::Vector;
namespace field = nettool_sim::field;
namespace slot = nettool_sim::slot;
namespace cell = nettool_sim::cell;
namespace table = nettool_sim::rul::table;
namespace oc = nettool_sim::occupants;

constexpr uint32_t kRoadType = 0, kStep = 4, kRadius = 2, kMostCells = 25;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

struct Sum {
    uint32_t count = 0, sum = 0, xored = 0;
    void Add(uint32_t h)
    {
        ++count;
        sum += h;
        xored ^= h;
    }
    void MixInto(Hash& hash) const
    {
        hash.Mix(count);
        hash.Mix(sum);
        hash.Mix(xored);
    }
};

cISC4NetworkManager* Manager(cISC4City* city)
{
    if (!game::IsBuild641() || !city)
        return nullptr;
    cISC4NetworkManager* const manager = city->GetNetworkManager();
    if (!manager || !At<uint8_t>(manager, netmgr_sim::field::kInitialized))
        return nullptr;
    return manager;
}

// One vector of edge connection records, in order; the count.
uint32_t MixEdges(Hash& hash, char* self, std::size_t offset)
{
    const Vector& records = At<Vector>(self, offset);
    const uint32_t count = static_cast<uint32_t>(static_cast<std::size_t>(records.end - records.begin) /
                                                 nettool_sim::occupants::oedge::kSize);
    hash.Mix(count);
    for (uint32_t i = 0; i < count; ++i) {
        char* const record = records.begin + i * nettool_sim::occupants::oedge::kSize;
        for (std::size_t word = 0; word < nettool_sim::occupants::oedge::kByte; word += 4)
            hash.Mix(At<uint32_t>(record, word));
        hash.Mix(At<uint8_t>(record, nettool_sim::occupants::oedge::kByte));
    }
    return count;
}

// ---- The drive -------------------------------------------------------------------

bool g_done = false;
uint32_t g_sum = 0;

char* Allocate(uint32_t size) { return CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, size); }

// The RUL0 store's rules: each rule's ID and its checked points' cons cells
// (ConsCellAt at the game's address), summed order-free.
Sum DriveRules()
{
    Sum total;
    char* const store = reinterpret_cast<char*>(addr::cSC4NetworkTool_Rul0Store);
    const Vector& buckets = At<Vector>(store, table::kBuckets);
    for (char* const* at = reinterpret_cast<char* const*>(buckets.begin);
         at != reinterpret_cast<char* const*>(buckets.end); ++at)
        for (char* node = *at; node; node = At<char*>(node, table::kNext)) {
            char* const rule = node + nettool_sim::rul::store::kRule;
            Hash one;
            one.Mix(At<uint32_t>(node, table::kKey));
            const Vector& points = At<Vector>(rule, nettool_sim::rul::rul0::kChecked);
            for (char* point = points.begin; point != points.end; point += nettool_sim::rul::point::kSize) {
                char* const value = CallThis<char*, uint32_t, uint32_t>(
                    addr::SC4Rul0Rule_ConsCellAt, rule, At<uint32_t>(point, nettool_sim::rul::point::kX),
                    At<uint32_t>(point, nettool_sim::rul::point::kZ));
                if (!value) {
                    one.Mix(0xFFFFFFFFu);
                    continue;
                }
                one.Mix(At<uint32_t>(value, nettool_sim::occupants::orule::kValueId));
                one.Mix(At<uint8_t>(value, nettool_sim::occupants::orule::kValueTurn));
            }
            total.Add(one.h);
        }
    return total;
}

void Drive(const Day& day)
{
    if (g_done || !day.counted || !OnRunDay(day, "OPENSC4_TEST_NETTOOL") ||
        EnvInt("OPENSC4_TEST_NETTOOL_COST", 0) != 1)
        return;
    g_done = true;
    cISC4NetworkManager* const manager = Manager(day.city);
    if (!manager) {
        log::Write("autotest: ntoc: no network manager; nothing driven");
        return;
    }
    char* const self = reinterpret_cast<char*>(manager->GetNetworkTool(static_cast<int32_t>(kRoadType), true));
    if (!self) {
        log::Write("autotest: ntoc: no new road tool; nothing driven");
        return;
    }
    Hash hash;
    Virtual<uint32_t>(self, slot::kAddRef);
    hash.Mix(Virtual<uint8_t>(self, slot::kInit));
    const uint32_t sizeX = At<uint32_t>(self, field::kCellsX), sizeZ = At<uint32_t>(self, field::kCellsZ);
    int32_t centreX = -1, centreZ = -1;
    for (uint32_t z = 0; z < sizeZ && centreX < 0; z += kStep)
        for (uint32_t x = 0; x < sizeX && centreX < 0; x += kStep) {
            char* const info = CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, (z << 16) + x);
            if (info && At<void*>(info, cell::kOccupant)) {
                centreX = static_cast<int32_t>(x);
                centreZ = static_cast<int32_t>(z);
            }
        }
    hash.Mix(static_cast<uint32_t>(centreX));
    hash.Mix(static_cast<uint32_t>(centreZ));
    std::vector<uint32_t> keys;
    if (centreX >= 0) {
        const int32_t radius = static_cast<int32_t>(kRadius);
        for (int32_t z = centreZ - radius; z <= centreZ + radius; ++z)
            for (int32_t x = centreX - radius; x <= centreX + radius && keys.size() < kMostCells; ++x) {
                if (x < 0 || z < 0 || static_cast<uint32_t>(x) >= sizeX || static_cast<uint32_t>(z) >= sizeZ)
                    continue;
                const uint32_t key = (static_cast<uint32_t>(z) << 16) + static_cast<uint32_t>(x);
                char* const info = CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, key);
                if (info && At<void*>(info, cell::kOccupant))
                    keys.push_back(key);
            }
    }
    for (const uint32_t key : keys)
        hash.Mix(key);
    Vector& steps = At<Vector>(self, field::kSteps);
    Vector& cells = At<Vector>(self, field::kCells);
    uint32_t stages[4] = {hash.h, 0, 0, 0};
    int32_t cost = 0;
    uint32_t solvedCount = 0, records = 0, exemplars = 0;
    const Sum rules = DriveRules();
    Hash rulesHash;
    rules.MixInto(rulesHash);
    stages[3] = rulesHash.h;
    if (keys.empty()) {
        log::Write("autotest: ntoc: no cell with a network occupant; tool not driven");
    } else if (steps.begin || cells.begin || At<uint32_t>(self, field::kSet84 + table::kCount) ||
               nettool_sim::Count(self, field::kIntersections, 1) ||
               nettool_sim::Count(self, field::kMajorBridges, 1) || nettool_sim::Count(self, field::kVectorE4, 1) ||
               nettool_sim::Count(self, field::kRuleCellRecords, 1)) {
        log::Write("autotest: ntoc: the new tool has steps, cells, cells to solve or records; tool not driven");
    } else {
        const uint32_t count = static_cast<uint32_t>(keys.size());
        char* const points = Allocate(count * 8);
        for (uint32_t i = 0; i < count; ++i) {
            At<uint32_t>(points, i * 8) = keys[i] & 0xFFFF;
            At<uint32_t>(points, i * 8 + 4) = keys[i] >> 16;
        }
        cells = Vector{points, points + count * 8, points + count * 8};
        char* const step = Allocate(nettool_sim::step::kSize);
        At<uint32_t>(step, 0) = 0;
        At<uint32_t>(step, 4) = count - 1;
        At<uint32_t>(step, 8) = 0;
        steps = Vector{step, step + nettool_sim::step::kSize, step + nettool_sim::step::kSize};
        const int32_t validWas = At<int32_t>(self, field::kValidSteps);
        At<int32_t>(self, field::kValidSteps) = 1;
        CallThis<void>(addr::cSC4NetworkTool_CollectDraggedCells, self);
        At<int32_t>(self, field::kValidSteps) = 0;
        const int32_t solveAnswer =
            CallThis<int32_t, uint32_t, uint32_t>(addr::cSC4NetworkTool_SolveTiles, self, 0xFFFFFFFFu, 1u);
        solvedCount = static_cast<uint32_t>(nettool_sim::solve::SolvedCount(self));
        Hash setup;
        setup.Mix(static_cast<uint32_t>(solveAnswer));
        setup.Mix(solvedCount);
        // Slot 20.
        cost = Virtual<int32_t>(self, slot::kGetCostOfSolution);
        setup.Mix(static_cast<uint32_t>(cost));
        stages[1] = setup.h;
        // The helpers over the solved cells (in order).
        Hash helpers;
        const Vector& solved = At<Vector>(self, field::kSolved);
        for (uint32_t i = 0; i < solvedCount; ++i) {
            char* const record = solved.begin + i * nettool_sim::solve::kSolvedSize;
            const uint32_t piece = At<uint32_t>(record, nettool_sim::solve::kPiece);
            const uint32_t key = At<uint32_t>(record, nettool_sim::solve::kKey);
            helpers.Mix(piece);
            char* const found = CallThis<char*, uint32_t>(addr::cSC4NetworkTool_FindPieceRecord, self, piece);
            helpers.Mix(found ? 1u : 0u);
            if (found) {
                ++records;
                helpers.Mix(static_cast<uint32_t>(static_cast<std::size_t>(At<Vector>(found, 0).end -
                                                                           At<Vector>(found, 0).begin) /
                                                  4));
            }
            void* const exemplar =
                CallThis<void*, uint32_t, uint32_t>(addr::cSC4NetworkTool_GetPieceExemplar, self, piece, 0u);
            helpers.Mix(exemplar ? 1u : 0u);
            if (exemplar) {
                ++exemplars;
                nettool_sim::ReleaseOf(exemplar);
            }
            char* const info = CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, key);
            if (!info) {
                helpers.Mix(0xFFFFFFFFu);
                continue;
            }
            helpers.Mix(CallThis<uint32_t, char*>(addr::cSC4NetworkTool_VertexClearanceCategory, self, info));
            const uint32_t x = At<uint32_t>(info, cell::kX), z = At<uint32_t>(info, cell::kZ);
            const uint32_t flags = At<uint32_t>(info, cell::kFlags);
            helpers.Mix(CallThis<uint8_t, uint32_t, uint32_t, uint32_t>(addr::cSC4NetworkTool_HasNeighborAlongX, self,
                                                                         x, z, flags));
            helpers.Mix(CallThis<uint8_t, uint32_t, uint32_t, uint32_t>(addr::cSC4NetworkTool_HasNeighborAlongZ, self,
                                                                         x, z, flags));
        }
        stages[2] = helpers.h;
        CallCdecl<void, void*>(addr::SC4_Free, points);
        CallCdecl<void, void*>(addr::SC4_Free, step);
        cells = Vector{nullptr, nullptr, nullptr};
        steps = Vector{nullptr, nullptr, nullptr};
        At<int32_t>(self, field::kValidSteps) = validWas;
        log::Write("autotest: ntoc: drive: %u cells around (%d, %d); solver %d, %u solved; cost %d; %u piece "
                   "records, %u exemplars; %u rules",
                   count, centreX, centreZ, solveAnswer, solvedCount, cost, records, exemplars, rules.count);
    }
    for (std::size_t s = 1; s < 4; ++s)
        hash.Mix(stages[s]);
    hash.Mix(Virtual<uint8_t>(self, slot::kShutdown));
    Virtual<uint32_t>(self, slot::kRelease);
    g_sum = hash.h;
    log::Write("autotest: ntoc: stages: cells %08X cost %08X helpers %08X rules %08X; hash %08X", stages[0], stages[1],
               stages[2], stages[3], g_sum);
}

void Fingerprint(cISC4City* city, std::string& line)
{
    cISC4NetworkManager* const manager = Manager(city);
    if (!manager) {
        line += " | ntoc none";
        return;
    }
    Hash hash;
    uint32_t records = 0;
    for (uint32_t type = 0; type < nettool_sim::kTypes; ++type) {
        char* const self = reinterpret_cast<char*>(manager->GetNetworkTool(static_cast<int32_t>(type), false));
        if (!self) {
            hash.Mix(0xFFFFFFFFu);
            continue;
        }
        hash.Mix(At<uint32_t>(self, field::kType));
        records += MixEdges(hash, self, oc::ofield::kEdgeConnections);
        records += MixEdges(hash, self, field::kNeighborConnections);
    }
    char text[96];
    std::snprintf(text, sizeof text, " | ntoc e%u %08X", records, hash.h);
    line += text;
    if (g_done) {
        std::snprintf(text, sizeof text, " drive %08X", g_sum);
        line += text;
    }
}

const FingerprintPart kFingerprint{"ntoc", 203, &Fingerprint};
const DayHook kDrive{"ntoc.drive", 204, &Drive};

} // namespace
} // namespace opensc4::autotest
