// The network tool's height pass in the autotest (part t9;
// src/core/nettool_heights_1.cpp has the analysis).
//
// The daily part ` | ntht a<cells to fit> b<border vertices> r<results>
// <hash>` (always, once the network manager is initialised; ` | ntht none`
// before) reads the network manager's own 13 tools (GetNetworkTool(type,
// 0): no reference added) and hashes what the height pass and slot 56 leave
// on them, values only:
//   - per tool its type, then the two height sets (+0xA4 cell keys, +0xB8
//     vertices; hash sets whose order depends on nothing but the keys, still
//     summed order-free): the count and an order-free sum and xor;
//   - the two satisfiers' (+0x124 and +0x188) begun byte +0x4 and results
//     (+0x14: vertex -> height bits): the count and an order-free sum of the
//     mixed pairs;
//   - the row spans 0x00B460E8 (the static scratch CollectHeightCells fills
//     when not every step was solved), the first +0x24C pairs of an
//     initialised road tool.
// The satisfiers' answer byte +0x5 is not hashed (the constructor leaves it
// unwritten), nor any address. It is valid in every state the game leaves a
// tool in (the sets' and the tables' buckets exist from the constructors).
//
// OPENSC4_TEST_NETTOOL_HEIGHTS=1 (with OPENSC4_TEST_NETTOOL=<day>) adds, on
// that day, a new road tool (GetNetworkTool(0, unique), AddRef, Init; at the
// end Shutdown and Release: the tool is deleted) and on it the tiles drive's
// set-up (src/core/nettool_solve_autotest.cpp: the cells with a network
// occupant around the first one found, written as the dragged cells with one
// regular step, CollectDraggedCells, SolveTiles (-1, 1) with +0x78 = 0),
// then:
//   pass    +0x70 = +0x74 = 1, +0x7C = +0x80 = 0 (the constructor leaves
//           them unwritten), +0x78 = 1 and FitHeights 0x00637A80 at its
//           address with radius 3 (as the guarded 0x0062CD50 calls it):
//           AddIntersectionCells, CollectHeightCells (the span branch unless
//           the solved count is 1), the Resize, the portions (none: no
//           bridges or records), slot 77 with ComputeSegmentPlane and
//           IsVertexBlocked, the first satisfier, InsertCellConstraints
//           (slot 76, the guarded 0x00636690 with GetCrossSections and the
//           two inserts, slot 78), InsertIntersectionConstraints, Solve; the
//           answer (AL), the sets, the first satisfier's results and the
//           cells' heights and pairs (+0x58 to +0xA7) hashed;
//   support slot 56 CanTilesBeSupportedOnTerrain (0) through the vtable:
//           its AL and the second satisfier's results hashed;
//   heights slot 73 GetNetworkVertexHt for each cell's four vertices (the
//           double's bits);
//   the fields put back, the dragged cells and the step freed before
//   Shutdown.
// Nothing of it touches the city: the cell infos live in the new tool's
// world cache, the satisfiers are the tool's, the row spans are the game's
// scratch; the terrain, the lots and the occupants are only read. Slot 57
// UpdateTerrainHeights (it writes the terrain) is never driven. One log line
// `autotest: ntht: ...` gives the counts and the stages' hashes; the daily
// part then ends with ` drive <hash>`.
//
// What the crew test (OPENSC4_TEST_CREW) reaches of this part: its drags go
// through PlaceAllSegments 0x0063BDC0, whose segment placer 0x0063B830 calls
// FitHeights (and so every helper above, slot 77, 76 and the guarded
// 0x00636690 with GetCrossSections, IsCellInHeightSet and the two inserts;
// SetBridgeCellHeights / SetRecordCellHeights with portions only for drags
// with bridges or tunnels, AddIntersectionCells / InsertIntersection-
// Constraints only with intersections) and slot 56; the placement then
// calls slot 57 (0x00628119 and after) when it commits, and slot 73 is read
// by the preview and placement code (0x00625BDE, 0x00629D55).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/netmgr_sim.h"
#include "core/nettool_satisfier.h"
#include "core/nettool_sim.h"
#include "core/nettool_solve.h"

#include <cISC4City.h>
#include <cISC4NetworkManager.h>

#include <opensc4/addresses.h>

#include <bit>
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

constexpr std::size_t kSetA4 = 0xA4, kSetB8 = 0xB8; // the cells to fit, the border vertices
constexpr std::size_t kWord74 = 0x74, kDirection7C = 0x7C, kDirection80 = 0x80, kVerticesZ = 0x24C;
constexpr std::size_t kSatBegun = 0x4, kSatResults = 0x14;
constexpr std::size_t kSlot56 = 0xE0, kSlot73 = 0x124;
constexpr uint32_t kRoadType = 0, kStep = 4, kRadius = 2, kMostCells = 25, kPassRadius = 3;

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

// A hash set's keys (or a table's key and value) summed order-free.
Sum SumTable(char* set, bool withValue)
{
    Sum total;
    const Vector& buckets = At<Vector>(set, table::kBuckets);
    for (char* const* at = reinterpret_cast<char* const*>(buckets.begin);
         at != reinterpret_cast<char* const*>(buckets.end); ++at)
        for (char* node = *at; node; node = At<char*>(node, table::kNext)) {
            Hash pair;
            pair.Mix(At<uint32_t>(node, table::kKey));
            if (withValue)
                pair.Mix(At<uint32_t>(node, 8));
            total.Add(pair.h);
        }
    return total;
}

struct Totals {
    uint32_t a4 = 0, b8 = 0, results = 0;
};

void MixSatisfier(Hash& hash, char* sat, Totals& totals)
{
    hash.Mix(At<uint8_t>(sat, kSatBegun));
    const Sum results = SumTable(sat + kSatResults, true);
    results.MixInto(hash);
    totals.results += results.count;
}

// What the height pass keeps on one tool.
void MixHeights(Hash& hash, char* self, Totals& totals)
{
    hash.Mix(At<uint32_t>(self, field::kType));
    const Sum a4 = SumTable(self + kSetA4, false);
    a4.MixInto(hash);
    totals.a4 += a4.count;
    const Sum b8 = SumTable(self + kSetB8, false);
    b8.MixInto(hash);
    totals.b8 += b8.count;
    MixSatisfier(hash, self + field::kSatisfier1, totals);
    MixSatisfier(hash, self + field::kSatisfier2, totals);
}

// A cell info's heights and pairs (+0x58 to +0xA7).
void MixCell(Hash& hash, char* info)
{
    if (!info) {
        hash.Mix(0xFFFFFFFFu);
        return;
    }
    for (std::size_t at = cell::kHeights; at < cell::kVertices; at += 4)
        hash.Mix(At<uint32_t>(info, at));
}

// ---- The drive -------------------------------------------------------------------

bool g_done = false;
uint32_t g_sum = 0;

char* Allocate(uint32_t size) { return CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, size); }

void Drive(const Day& day)
{
    if (g_done || !day.counted || !OnRunDay(day, "OPENSC4_TEST_NETTOOL") ||
        EnvInt("OPENSC4_TEST_NETTOOL_HEIGHTS", 0) != 1)
        return;
    g_done = true;
    cISC4NetworkManager* const manager = Manager(day.city);
    if (!manager) {
        log::Write("autotest: ntht: no network manager; nothing driven");
        return;
    }
    char* const self = reinterpret_cast<char*>(manager->GetNetworkTool(static_cast<int32_t>(kRoadType), true));
    if (!self) {
        log::Write("autotest: ntht: no new road tool; nothing driven");
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
    uint32_t passAnswer = 0xEE, supportAnswer = 0xEE;
    Totals totals;
    if (keys.empty()) {
        log::Write("autotest: ntht: no cell with a network occupant; height pass not driven");
    } else if (steps.begin || cells.begin || At<uint32_t>(self, field::kSet84 + table::kCount) ||
               nettool_sim::Count(self, field::kIntersections, 1) ||
               nettool_sim::Count(self, field::kMajorBridges, 1) || nettool_sim::Count(self, field::kVectorE4, 1) ||
               nettool_sim::Count(self, field::kRuleCellRecords, 1)) {
        log::Write("autotest: ntht: the new tool has steps, cells, cells to solve or records; height pass not driven");
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
        const uint32_t stepCountWas = At<uint32_t>(self, field::kStepCount);
        const uint32_t word74Was = At<uint32_t>(self, kWord74);
        const uint32_t dir7CWas = At<uint32_t>(self, kDirection7C), dir80Was = At<uint32_t>(self, kDirection80);
        At<int32_t>(self, field::kValidSteps) = 1;
        CallThis<void>(addr::cSC4NetworkTool_CollectDraggedCells, self);
        At<int32_t>(self, field::kValidSteps) = 0;
        const int32_t solveAnswer =
            CallThis<int32_t, uint32_t, uint32_t>(addr::cSC4NetworkTool_SolveTiles, self, 0xFFFFFFFFu, 1u);
        Hash setup;
        setup.Mix(static_cast<uint32_t>(solveAnswer));
        setup.Mix(static_cast<uint32_t>(nettool_sim::solve::SolvedCount(self)));
        stages[1] = setup.h;
        // The height pass.
        At<uint32_t>(self, field::kStepCount) = 1;
        At<uint32_t>(self, kWord74) = 1;
        At<uint32_t>(self, kDirection7C) = 0;
        At<uint32_t>(self, kDirection80) = 0;
        At<int32_t>(self, field::kValidSteps) = 1;
        passAnswer = CallThis<uint8_t, uint32_t>(addr::cSC4NetworkTool_FitHeights, self, kPassRadius);
        Hash pass;
        pass.Mix(passAnswer);
        pass.Mix(static_cast<uint32_t>(nettool_sim::solve::SolvedCount(self)));
        MixHeights(pass, self, totals);
        for (const uint32_t key : keys)
            MixCell(pass, CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, key));
        stages[2] = pass.h;
        // Slot 56, then slot 73.
        supportAnswer = Virtual<uint8_t>(self, kSlot56, kRoadType);
        Hash support;
        support.Mix(supportAnswer);
        Totals after;
        MixSatisfier(support, self + field::kSatisfier2, after);
        for (const uint32_t key : keys) {
            char* const info = CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, key);
            if (!info) {
                support.Mix(0xFFFFFFFFu);
                continue;
            }
            for (std::size_t k = 0; k < 4; ++k) {
                const double height = Virtual<double>(self, kSlot73, At<uint32_t>(info, cell::kVertices + 4 * k));
                const uint64_t bits = std::bit_cast<uint64_t>(height);
                support.Mix(static_cast<uint32_t>(bits));
                support.Mix(static_cast<uint32_t>(bits >> 32));
            }
        }
        stages[3] = support.h;
        for (std::size_t s = 1; s < 4; ++s)
            hash.Mix(stages[s]);
        CallCdecl<void, void*>(addr::SC4_Free, points);
        CallCdecl<void, void*>(addr::SC4_Free, step);
        cells = Vector{nullptr, nullptr, nullptr};
        steps = Vector{nullptr, nullptr, nullptr};
        At<int32_t>(self, field::kValidSteps) = validWas;
        At<uint32_t>(self, field::kStepCount) = stepCountWas;
        At<uint32_t>(self, kWord74) = word74Was;
        At<uint32_t>(self, kDirection7C) = dir7CWas;
        At<uint32_t>(self, kDirection80) = dir80Was;
        log::Write("autotest: ntht: drive: %u cells around (%d, %d); solver %d; height pass AL %u: %u cells to fit, "
                   "%u border vertices, %u results; slot 56 AL %u: %u results",
                   count, centreX, centreZ, solveAnswer, passAnswer, totals.a4, totals.b8, totals.results,
                   supportAnswer, after.results);
    }
    hash.Mix(Virtual<uint8_t>(self, slot::kShutdown));
    Virtual<uint32_t>(self, slot::kRelease);
    g_sum = hash.h;
    log::Write("autotest: ntht: stages: cells %08X setup %08X pass %08X support %08X; hash %08X", stages[0],
               stages[1], stages[2], stages[3], g_sum);
}

void Fingerprint(cISC4City* city, std::string& line)
{
    cISC4NetworkManager* const manager = Manager(city);
    if (!manager) {
        line += " | ntht none";
        return;
    }
    Hash hash;
    Totals totals;
    uint32_t rows = 0;
    for (uint32_t type = 0; type < nettool_sim::kTypes; ++type) {
        char* const self = reinterpret_cast<char*>(manager->GetNetworkTool(static_cast<int32_t>(type), false));
        if (!self) {
            hash.Mix(0xFFFFFFFFu);
            continue;
        }
        MixHeights(hash, self, totals);
        if (type == kRoadType && At<uint8_t>(self, field::kInitialized))
            rows = At<uint32_t>(self, kVerticesZ);
    }
    for (uint32_t z = 0; z < rows; ++z)
        hash.Mix(Global<uint32_t>(addr::cSC4NetworkTool_RowSpans + z * 4));
    char text[128];
    std::snprintf(text, sizeof text, " | ntht a%u b%u r%u %08X", totals.a4, totals.b8, totals.results, hash.h);
    line += text;
    if (g_done) {
        std::snprintf(text, sizeof text, " drive %08X", g_sum);
        line += text;
    }
}

const FingerprintPart kFingerprint{"ntht", 180, &Fingerprint};
const DayHook kDrive{"ntht.drive", 202, &Drive};

} // namespace
} // namespace opensc4::autotest
