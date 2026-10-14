// The network tool's tile solver in the autotest (part t8;
// src/core/nettool_solve.h has the layouts, nettool_solve_1.cpp the analysis).
//
// The daily part ` | ntsv s<solved> c<cells> o<overrides> i<intersection
// keys> <hash>` (always, once the network manager is initialised; ` | ntsv
// none` before) reads the network manager's own 13 tools (GetNetworkTool(type,
// 0): no reference added) and hashes what the solver leaves on them, values
// only:
//   - per tool its type, then the solved cells (+0x98) in order: the piece
//     ID, the LOW BYTE of the rotation dword (+0x5 to +0x7 are the
//     original's stack leftovers, zeros in ours: never hashed) and the cell
//     key;
//   - the cells to solve (+0x84, a hash set): the count and an order-free
//     sum and xor of the keys;
//   - the RUL1 override records (+0x08, 12 bytes: x, z, flags) in order;
//   - each intersection's (+0xCC, 0x28 bytes) key vector +0x1C in order.
// The counts of solved cells, cells to solve, override records and
// intersection keys over the 13 tools are given in clear. Everything is read
// in place; it is valid in every state the game leaves a tool in (the set's
// buckets exist from the constructor; the vectors may be empty).
//
// OPENSC4_TEST_NETTOOL_TILES=1 (with OPENSC4_TEST_NETTOOL=<day>) adds, on that
// day, a new road tool (GetNetworkTool(0, unique), AddRef, Init; at the end
// Shutdown and Release: the tool is deleted) and on it:
//   cells   the first cell with a network occupant on every 4th row and
//           column, and the cells of the 5 x 5 block around it (inside the
//           city) that have one, at most 25, through the tool's cell entry
//           0x00633220 (cell infos in this tool's own world cache);
//   drag    those cells written as the tool's dragged cells (+0x60, the
//           game's allocator) with one regular step over all of them (+0x54:
//           first 0, last n - 1, 0) and +0x78 = 1; CollectDraggedCells
//           0x006355C0 at its address (the cells into +0x84, marked, spread
//           four passes along their edges by SpreadConnected, and the
//           intersection, +0xE4 and bridge erasers on the tool's empty
//           vectors); +0x84 and the cells' marks hashed;
//   solve   +0x78 = 0 and SolveTiles 0x00635CB0 at its address with (-1, 1),
//           as DestroyOccupantsAndResolve 0x0062A5C0 calls it to re-solve
//           cells in place: the cells grown, their edges fixed, multi-type
//           cells resolved, every type row's rule files applied, the
//           NAM-guarded RUL2 pass 0x00634D70 and the tile verifiers; the
//           answer, the solved cells, +0x84, the override records and each
//           cell's info (flags, the 13 edge words, the combined word, the
//           one-way byte, the mark, the solved index) hashed;
//   the dragged cells and the step freed and the vectors and +0x78 put back
//   empty before Shutdown.
// Nothing of it touches the city: the cell infos live in the new tool's
// world cache, the rule match and the verifiers only read the occupants and
// the rule files. One log line `autotest: ntsv: ...` gives the counts and
// the stages' hashes; the daily part then ends with ` drive <hash>`.
//
// What the crew test (OPENSC4_TEST_CREW) reaches of this part: its drags go
// through PlaceAllSegments 0x0063BDC0, whose segment placer 0x0063B830 calls
// CollectDraggedCells (and so SpreadConnected, CollectIntersectionCells,
// EraseRecordE4Cells, EraseBridgeCells) and SolveTiles (and so all the
// solver's helpers: GrowCellSet and CellRectAround, FixCellEdges and the
// edge fix-ups the type rows of kinds 1 and 2 pick, ResolveCellTypes on
// cells with two or more networks, ApplyRuleFile and MatchRuleFile, the set
// helpers, the guarded RUL2 pass with PatchTilePair where a RUL2 override
// applies, VerifyTiles and EqualRange when the tile verifier table has
// entries).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/netmgr_sim.h"
#include "core/nettool_solve.h"
#include "core/nettool_sim.h"

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

// The part's fields (src/core/nettool_solve.h).
using nettool_sim::solve::kIntersectionCells;
using nettool_sim::solve::kIntersectionSize;
using nettool_sim::solve::kMarked;
using nettool_sim::solve::kOverrides;
using nettool_sim::solve::kSolvedSize;
constexpr std::size_t kRecordSize = 0xC; // an override record: x, z, flags
// The drive's limits.
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

struct Totals {
    uint32_t solved = 0, cells = 0, overrides = 0, keys = 0;
};

// The cells to solve (+0x84): the count and an order-free sum of the keys.
Sum SumCells(char* self)
{
    Sum total;
    char* const set = self + field::kSet84;
    const Vector& buckets = At<Vector>(set, table::kBuckets);
    for (char* const* at = reinterpret_cast<char* const*>(buckets.begin);
         at != reinterpret_cast<char* const*>(buckets.end); ++at)
        for (char* node = *at; node; node = At<char*>(node, table::kNext))
            total.Add(At<uint32_t>(node, table::kKey));
    return total;
}

// What the solver keeps on one tool.
void MixSolver(Hash& hash, char* self, Totals& totals)
{
    hash.Mix(At<uint32_t>(self, field::kType));
    const Vector& solved = At<Vector>(self, field::kSolved);
    hash.Mix(static_cast<uint32_t>(solved.end - solved.begin));
    for (char* at = solved.begin; at && at < solved.end; at += kSolvedSize) {
        hash.Mix(At<uint32_t>(at, 0));
        hash.Mix(At<uint8_t>(at, 4)); // the low byte only (see the top)
        hash.Mix(At<uint32_t>(at, 8));
        ++totals.solved;
    }
    hash.Mix(At<uint32_t>(self, field::kSet84 + table::kCount));
    const Sum cells = SumCells(self);
    cells.MixInto(hash);
    totals.cells += cells.count;
    const Vector& overrides = At<Vector>(self, kOverrides);
    hash.Mix(static_cast<uint32_t>(overrides.end - overrides.begin));
    for (char* at = overrides.begin; at && at < overrides.end; at += kRecordSize) {
        for (std::size_t word = 0; word < kRecordSize; word += 4)
            hash.Mix(At<uint32_t>(at, word));
        ++totals.overrides;
    }
    const Vector& intersections = At<Vector>(self, field::kIntersections);
    hash.Mix(static_cast<uint32_t>(intersections.end - intersections.begin));
    for (char* at = intersections.begin; at && at < intersections.end; at += kIntersectionSize) {
        const Vector& keys = At<Vector>(at, kIntersectionCells);
        hash.Mix(static_cast<uint32_t>(keys.end - keys.begin));
        for (char* key = keys.begin; key && key < keys.end; key += 4) {
            hash.Mix(At<uint32_t>(key, 0));
            ++totals.keys;
        }
    }
}

// A cell info's fields the solver writes.
void MixInfo(Hash& hash, char* info)
{
    if (!info) {
        hash.Mix(0xFFFFFFFFu);
        return;
    }
    hash.Mix(At<uint32_t>(info, cell::kFlags));
    for (uint32_t type = 0; type < cell::kEdgeWords; ++type)
        hash.Mix(At<uint32_t>(info, cell::kByType + type * 4));
    hash.Mix(At<uint32_t>(info, cell::kCombined));
    hash.Mix(At<uint8_t>(info, cell::kOneWay));
    hash.Mix(At<uint8_t>(info, kMarked));
    hash.Mix(At<uint32_t>(info, cell::kSolvedIndex));
}

// ---- The drive -------------------------------------------------------------------

bool g_done = false;
uint32_t g_sum = 0;

char* Allocate(uint32_t size)
{
    return CallCdecl<char*, uint32_t>(addr::SC4_stl_allocate, size);
}

void Drive(const Day& day)
{
    if (g_done || !day.counted || !OnRunDay(day, "OPENSC4_TEST_NETTOOL") ||
        EnvInt("OPENSC4_TEST_NETTOOL_TILES", 0) != 1)
        return;
    g_done = true;
    cISC4NetworkManager* const manager = Manager(day.city);
    if (!manager) {
        log::Write("autotest: ntsv: no network manager; nothing driven");
        return;
    }
    char* const self = reinterpret_cast<char*>(manager->GetNetworkTool(static_cast<int32_t>(kRoadType), true));
    if (!self) {
        log::Write("autotest: ntsv: no new road tool; nothing driven");
        return;
    }
    Hash hash;
    Virtual<uint32_t>(self, slot::kAddRef);
    hash.Mix(Virtual<uint8_t>(self, slot::kInit));
    const uint32_t sizeX = At<uint32_t>(self, field::kCellsX), sizeZ = At<uint32_t>(self, field::kCellsZ);
    // The first cell with a network occupant, then its block.
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
    uint32_t stages[3] = {hash.h, 0, 0};
    int32_t answer = 0;
    Totals totals;
    uint32_t marked = 0;
    if (keys.empty()) {
        log::Write("autotest: ntsv: no cell with a network occupant; solver not driven");
    } else if (steps.begin || cells.begin || At<uint32_t>(self, field::kSet84 + table::kCount) ||
               nettool_sim::Count(self, field::kIntersections, 1) ||
               nettool_sim::Count(self, field::kMajorBridges, 1) || nettool_sim::Count(self, field::kVectorE4, 1) ||
               nettool_sim::Count(self, field::kRuleCellRecords, 1)) {
        // The intersections, major bridges, +0xE4 records and rule cell
        // records must be empty: with intersections the collector would look
        // rules up in the shared RUL0 store (its find-or-insert inserts).
        log::Write("autotest: ntsv: the new tool has steps, cells, cells to solve or records; solver not driven");
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
        Hash drag;
        const Sum collected = SumCells(self);
        collected.MixInto(drag);
        for (const uint32_t key : keys) {
            char* const info = CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, key);
            const uint8_t mark = info ? At<uint8_t>(info, kMarked) : 0xEE;
            drag.Mix(mark);
            marked += mark == 1;
        }
        stages[1] = drag.h;
        At<int32_t>(self, field::kValidSteps) = 0;
        answer = CallThis<int32_t, uint32_t, uint32_t>(addr::cSC4NetworkTool_SolveTiles, self, 0xFFFFFFFFu, 1u);
        Hash solve;
        solve.Mix(static_cast<uint32_t>(answer));
        MixSolver(solve, self, totals);
        for (const uint32_t key : keys)
            MixInfo(solve, CallThis<char*, uint32_t>(addr::cSC4NetworkManager_ToolCellEntry, self, key));
        stages[2] = solve.h;
        hash.Mix(stages[1]);
        hash.Mix(stages[2]);
        CallCdecl<void, void*>(addr::SC4_Free, points);
        CallCdecl<void, void*>(addr::SC4_Free, step);
        cells = Vector{nullptr, nullptr, nullptr};
        steps = Vector{nullptr, nullptr, nullptr};
        At<int32_t>(self, field::kValidSteps) = validWas;
        log::Write("autotest: ntsv: drive: %u cells around (%d, %d), %u collected, %u marked; solver answer %d, "
                   "%u solved, %u left to solve, %u override records",
                   count, centreX, centreZ, collected.count, marked, answer, totals.solved, totals.cells,
                   totals.overrides);
    }
    hash.Mix(Virtual<uint8_t>(self, slot::kShutdown));
    Virtual<uint32_t>(self, slot::kRelease);
    g_sum = hash.h;
    log::Write("autotest: ntsv: stages: cells %08X drag %08X solve %08X; hash %08X", stages[0], stages[1], stages[2],
               g_sum);
}

void Fingerprint(cISC4City* city, std::string& line)
{
    cISC4NetworkManager* const manager = Manager(city);
    if (!manager) {
        line += " | ntsv none";
        return;
    }
    Hash hash;
    Totals totals;
    for (uint32_t type = 0; type < nettool_sim::kTypes; ++type) {
        char* const self = reinterpret_cast<char*>(manager->GetNetworkTool(static_cast<int32_t>(type), false));
        if (!self) {
            hash.Mix(0xFFFFFFFFu);
            continue;
        }
        MixSolver(hash, self, totals);
    }
    char text[128];
    std::snprintf(text, sizeof text, " | ntsv s%u c%u o%u i%u %08X", totals.solved, totals.cells, totals.overrides,
                  totals.keys, hash.h);
    line += text;
    if (g_done) {
        std::snprintf(text, sizeof text, " drive %08X", g_sum);
        line += text;
    }
}

const FingerprintPart kFingerprint{"ntsv", 179, &Fingerprint};
const DayHook kDrive{"ntsv.drive", 201, &Drive};

} // namespace
} // namespace opensc4::autotest
