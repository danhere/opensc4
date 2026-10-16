// The network construction crews' part of the autotest
// (src/core/autotest_hooks.h; docs/netlot.md, "The crew's clocks and seeds").
//
// OPENSC4_TEST_CREW=<day>: on that day, one straight network of
// OPENSC4_TEST_CREW_LENGTH cells (default 24; two thirds of that and so on,
// down to 8, where no strip fits) of each of six types that get
// a crew (road, rail, street, avenue, light rail, one-way road) is placed
// with the network manager's own tool of the type, on land the lot manager
// finds (no water, no network, one cell of margin; free land first, else land
// with lots, which the tool bulldozes as a player's drag does), and the crews
// the tool starts are left to work over the following days. Nothing here
// hooks the crew: what it does is read back from the network manager, the
// crews it holds, the city's network pieces and the network lot manager, so
// the test runs the same on the game's crew and on ours.
//
// The tool is handled as the game's own callers handle a kept tool
// (ProcessRetrofitList 0x00612820, src/core/netmgr_messages.cpp): GetNetworkTool
// with `unique` 0, AddRef, Init (which gives it the terrain at +0x44), the
// calls, Shutdown, Release. The calls are the release of a player's drag in
// the network tool's view control (0x006453FB to 0x00645516): Reset, the
// anchor points (slot 22, SetAnchorPoints: copied to the tool's +0x118),
// PlaceAllSegments(0, 0, 1, type) for the preview (the solution GetCostOfSolution
// sums), Reset and the anchors again, PlaceAllSegments(1, 0, 0, type) to build
// (the third argument is the control's answer to a neighbour connection,
// none here), then ClearCellPreviews and ClearOccupantHighlights.
// PlaceAllSegments (0x0063BDC0) draws a segment between each two consecutive
// anchor points and makes the crew when its first argument is set: with no
// anchors it builds nothing and its crew ends at once, which is all the road
// test of OPENSC4_TEST_QUIET (DrawNetworkLine, then PlaceAllSegments, no
// anchors) ever did. Dialogs a placement might open go to the modal stand-in
// and are answered 0.
//
// The daily part ` | crew ...` (only when the switch is set):
//   crew   crews registered with the network manager (its vector at +0x7C),
//          their jobs (the 0x80-byte records of the vector at crew +0x48),
//          the jobs not ended (+0x4 not -1) and the pieces still in the
//          crews' lists (crew +0x2C)
//   wait   over every network piece of the city: how many carry the property
//          0xAA24AE57 (the jobs at work on it), how many of those are above
//          0, and the sum
//   unusable  the pieces without the usable flag 0x10000000
//   lots   the network lot manager's lots
//   new    the pieces on cells that had none before the drive: how many, how
//          many are usable, and the sum and exclusive or of a checksum of each
//          (its cell, piece ID, usable flag and property value)
// A log line each day while a crew is registered, and one on the day the
// last has gone: `autotest: crew: day N: crews C, pieces waiting W, usable
// U, lots L`.
//
// Added with our crew's first part (src/core/crew_life.cpp), on the drive
// day, through the crew's functions at the game's addresses (so the game's
// or ours, whichever is installed):
//   life   before the drags, unless OPENSC4_TEST_CREW_LIFE=0: one crew made
//          as the network tool makes it (operator new 0x54, the constructor,
//          AddRef, Init with the road record of the tool's tuning table
//          0x00B452C8), its fields read after Init (the time per piece, the
//          scale's bits, the count, the initialised and frame-service
//          bytes, whether it holds the sound service, Init's last three,
//          the piece list's and the three vectors' lengths, whether the
//          network manager now holds it, the static byte), Init again
//          (the early answer), Shutdown (the bytes, the sound service gone,
//          the manager let go of it), and the last Release (the destructor
//          runs). A checksum of all of it, `life <hash>`; with
//          game.quiet.network on, Init is counted by it as a placement's is
//   work   after the drags: on the new piece with the lowest cell, the work
//          property read, IncrementWork, read, DecrementWork, read (the
//          property ends as it was): `work <before>,<inc>,<read>,<dec>,
//          <read>` (FFFFFFFF for a piece without the property)
// OPENSC4_TEST_CREW_PLUGIN=1 writes the byte 0xEB of the sounds plugin at
// 0x006071FC (the game's code, made writable for the write and protected
// again; the main thread, where every crew function runs, is the one
// writing) before the life crew and the drags, as the plugin does at load:
// every crew of the run then goes without the sound service, the game's
// Init by its own code and ours by reading the byte.
// OPENSC4_TEST_CREW_BRIDGE=1 (off by default; the leg without it is
// unchanged) adds, after the `work` part, drags across water: for road and
// then rail (=2 adds avenue and highway: two-wide types whose band the
// search already keeps clear, off at 1 so the first legs stay small), a
// straight row or column (rows first, then columns; free land first, then
// lots' land) with exactly W water cells between four land cells on each
// side and nothing but water and land of that verdict in the band around it
// (one cell before and after, one line on one side, two on the other, as
// FindStrip), W taken from the type's water bridge records (+0x99): each
// record's shortest span (+0x4) and the middle of its range up to +0x8,
// shortest first, up to four lines tried. Rail is dragged backwards (the
// bridge code's reversed paths). Each line is previewed (Reset, anchors,
// PlaceAllSegments(0, 0, 1, type)); the first whose preview has a major
// bridge (+0xD8) is then dragged for real: the preview again; with
// OPENSC4_TEST_CREW_BRIDGE_CHOICES (default 1) a preview per possible bridge
// (+0x1F4, its IDs as SetPreferredBridge takes them) with Reset, the
// preference, the anchors, the preview, the solution's hash and Reset (as
// the bridge UI's choice makes the view control preview again); the default
// preview; Reset, the anchors and the build with the game's own choice
// (Reset clears the preference); ClearCellPreviews and
// ClearOccupantHighlights. A preview makes bridge occupants
// (PlaceMajorBridgeOccupants inserts them into the occupant manager); the
// segment placer keeps them in the tool's +0x228 tree, and Reset's slot 16
// takes each out of the occupant manager (slot 0x54) and releases it: the
// test counts the city's network pieces before the first preview and after
// each Reset and stops the choice previews (logging it) when they differ.
// Then a road over a dip (a land line whose ends' lower vertex altitudes
// differ by no more than the road parameters' +0x40 and whose lowest
// between is more than +0x3C below the lower end: FitMinorBridges' minor
// bridge), logged when there is none. The hash of each solution (values
// only: the major bridges' words and land byte, their cells' words +0x0 to
// +0x28 with the turn byte alone, the flag byte +0x40 and, where it is set,
// +0x2C to +0x3C, the cable parts, the possible bridges, the cost, the
// failure reason) goes into ` brg <drags>,<bridges>,<choices>,<hash>` in
// the daily part, and a log line per drag (`autotest: crew: bridge ...`).
// OPENSC4_TEST_CREW_DRAWLINE=1 (off by default; legs without it are
// unchanged) makes every drag above (the six types and, with the bridge
// switch, each bridge preview and build) first do the view control's mouse
// move: Reset, ClearFailureReason, slot 18 DrawNetworkLine on copies of the
// two points, then the segment placer's read-only checks (slots 65 to 69);
// the six types' drags also probe a 45 degree line from their start (ties);
// ` dl <lines>,<drawn>,<hash>` in the daily part (DrawLineProbe below).
// OPENSC4_TEST_CREW_PUZZLE=<level> (off by default; legs without it are
// unchanged): after the six types, a four-cell drag of road, street and
// avenue across the middle of that type's strip, released as the view
// control releases (0x00645544): slot 49 QueryUserForPotentialIntersections
// after the placement, with the question answered yes by a modal stand-in;
// before it the tool's override records and the answers of MatchAt
// 0x00609A00 for the rules with text and cells that fit there are hashed.
// Level 2 also hands the crossing cell's new pieces to slot 50
// DestroyOccupantsAndResolve as the network manager's
// DestroyNetworkOccupants does. Level 3 also does the view control's puzzle
// piece click (the guarded 0x0062CD50 with flag 0) for four fitting rules
// at each of two cells of the road strip. ` pz <calls>,<candidates>,<hash>`
// in the daily part (PuzzleProbe, DirectPuzzle below).
#include "core/autotest_hooks.h"
#include "core/crew_sim.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/netlot_sim.h"
#include "core/netmgr_sim.h"
#include "core/nettool_bridge.h"
#include "core/nettool_place.h"
#include "core/netocc_sim.h"
#include "core/occmgr_sim.h"

#include <SC4Point.h>
#include <cISC4City.h>
#include <cISC4NetworkManager.h>
#include <cISC4NetworkOccupant.h>
#include <cISC4NetworkTool.h>

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace opensc4::autotest {
namespace {

namespace piece = netocc_sim;
namespace occupants = occmgr_sim;

// The crew (0x54 bytes, docs/netlot.md): the list of its pieces (a pointer
// to the list's head node; nodes are next, previous, piece) and the vector
// of jobs; a job's state (-1 when ended).
constexpr std::size_t kCrewPieces = 0x2C, kCrewJobs = 0x48, kJobSize = 0x80, kJobState = 0x4;
// The property a job adds 1 to on each of its pieces and takes back when it
// is done (0x00605660, 0x006056D0).
constexpr uint32_t kWorkProperty = 0xAA24AE57;
// The lot manager's CanCreateLot (slot 20: x, z, width, depth, then 1, 0, 0;
// as tool_zone_autotest.cpp asks it): the lowest verdict of the rectangle's
// cells, 0 outside, 1 water, 2 another zone, 3 a network, 7 a lot's, 8 free
// (src/core/zone_cells.cpp).
constexpr std::size_t kCanCreateLot = 0x50;
constexpr int32_t kLot = 7, kFree = 8;
// The tool's SetAnchorPoints (slot 22, 0x0062CA20: the vector operator= of
// 0x00611B00 into +0x118), called by offset with a vector of the test's own
// memory, which it only reads.
constexpr std::size_t kSetAnchorPoints = 0x58;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

// The six types drawn, in this order.
constexpr struct {
    cISC4NetworkOccupant::eNetworkType type;
    const char* name;
} kTypes[] = {
    {cISC4NetworkOccupant::Road, "road"},           {cISC4NetworkOccupant::Rail, "rail"},
    {cISC4NetworkOccupant::Street, "street"},       {cISC4NetworkOccupant::Avenue, "avenue"},
    {cISC4NetworkOccupant::LightRail, "lightrail"}, {cISC4NetworkOccupant::OneWayRoad, "oneway"},
};

bool g_driven = false;
uint32_t g_lastCrews = 0;
std::set<uint32_t> g_before; // cells ((x << 16) | z) that had a network piece before the drive
uint32_t g_dialogs = 0;
uint32_t g_lifeHash = 0;                                             // the `life` checksum
uint32_t g_work[5] = {0xFFFFFFFFu, 0, 0xFFFFFFFFu, 0, 0xFFFFFFFFu}; // the `work` values

// The city's network manager, once initialised; null otherwise.
char* Manager(cISC4City* city)
{
    if (!game::IsBuild641() || !city)
        return nullptr;
    char* const manager = reinterpret_cast<char*>(city->GetNetworkManager());
    if (!manager || !At<uint8_t>(manager, netmgr_sim::field::kInitialized))
        return nullptr;
    return manager;
}

// The city's network lot manager (the city's slot 42), once initialised.
char* LotManager(cISC4City* city)
{
    char* const lots = Virtual<char*>(city, netlot_sim::city_slot::kNetworkLotManager);
    return lots && At<uint8_t>(lots, netlot_sim::field::kInitialized) ? lots : nullptr;
}

uint32_t CrewCount(char* manager)
{
    const auto* const crews = reinterpret_cast<const netmgr_sim::Vector*>(manager + netmgr_sim::field::kCrews);
    return static_cast<uint32_t>(static_cast<std::size_t>(crews->end - crews->begin) / sizeof(void*));
}

// Every network piece of the city, found through the occupant manager as
// src/core/netocc_autotest.cpp finds them (each once; the order is the
// set's, so only order-free sums are made of them). False without a manager.
bool Collect(cISC4City* city, std::vector<char*>& found)
{
    char* const manager = Virtual<char*>(city, 0x88);
    if (!manager || !At<uint8_t>(manager, occupants::field::kInitialized))
        return false;
    std::unordered_set<char*> seen;
    occupants::LockOf(manager);
    const occupants::Vector* const grid = occupants::Grid(manager);
    const int32_t columns = static_cast<int32_t>((grid->end - grid->begin) / 12);
    for (int32_t x = 0; x < columns; ++x) {
        const auto* const column = reinterpret_cast<const occupants::Vector*>(grid->begin + x * 12);
        const int32_t depth = static_cast<int32_t>((column->end - column->begin) >> 2);
        for (int32_t z = 0; z < depth; ++z) {
            const occupants::Cell* const cell = occupants::CellAt(manager, x, z);
            if (!cell)
                continue;
            for (const occupants::ListNode* node = cell->head->next; node != cell->head; node = node->next) {
                void* const object = occupants::OccupantOf(node);
                if (Virtual<uint32_t>(object, 0x1C) == piece::kOccupantType)
                    seen.insert(static_cast<char*>(object) - piece::field::kOccupant);
            }
        }
    }
    occupants::UnlockOf(manager);
    found.assign(seen.begin(), seen.end());
    return true;
}

uint32_t CellKey(char* self)
{
    uint32_t x = 0, z = 0;
    Virtual<void>(self, piece::slot::kGetOccupiedCell, &x, &z);
    return (x << 16) | (z & 0xFFFF);
}

struct State {
    uint32_t crews = 0, jobs = 0, active = 0, queued = 0;
    uint32_t tagged = 0, waiting = 0, tagSum = 0, unusable = 0, lots = 0;
    uint32_t made = 0, madeUsable = 0, madeSum = 0, madeXor = 0;
};

uint32_t ListLength(char* listObject)
{
    const auto* const head = At<const netmgr_sim::ListNode*>(listObject, 0);
    uint32_t n = 0;
    if (head)
        for (const netmgr_sim::ListNode* node = head->next; node != head; node = node->next)
            ++n;
    return n;
}

State ReadState(cISC4City* city, char* manager)
{
    State s;
    const auto* const crews = reinterpret_cast<const netmgr_sim::Vector*>(manager + netmgr_sim::field::kCrews);
    for (char* const* at = reinterpret_cast<char* const*>(crews->begin);
         at != reinterpret_cast<char* const*>(crews->end); ++at) {
        char* const crew = *at;
        ++s.crews;
        const auto* const jobs = reinterpret_cast<const netmgr_sim::Vector*>(crew + kCrewJobs);
        for (char* job = jobs->begin; job != jobs->end; job += kJobSize) {
            ++s.jobs;
            s.active += At<int32_t>(job, kJobState) != -1;
        }
        s.queued += ListLength(crew + kCrewPieces);
    }
    std::vector<char*> pieces;
    if (Collect(city, pieces))
        for (char* const self : pieces) {
            uint32_t work = 0;
            void* const holder = self + piece::field::kHolder;
            const bool tagged = netmgr_sim::CallCdecl<uint8_t, void*, uint32_t, uint32_t*>(
                                    addr::SC4_GetPropertyUint32, holder, kWorkProperty, &work) != 0;
            const bool usable = Virtual<uint8_t>(self, piece::slot::kHasNetworkFlag, piece::flag::kUsable) != 0;
            s.tagged += tagged;
            s.waiting += tagged && work > 0;
            s.tagSum += work;
            s.unusable += !usable;
            const uint32_t cell = CellKey(self);
            if (!g_driven || g_before.count(cell))
                continue;
            Hash one;
            one.Mix(cell);
            one.Mix(Virtual<uint32_t>(self, piece::slot::kPieceId));
            one.Mix(usable);
            one.Mix(tagged ? work : 0xFFFFFFFFu);
            ++s.made;
            s.madeUsable += usable;
            s.madeSum += one.h;
            s.madeXor ^= one.h;
        }
    if (char* const lots = LotManager(city))
        s.lots = At<uint32_t>(lots, netlot_sim::field::kLots + netlot_sim::hashtable::kCount);
    return s;
}

// The first place, scanning rows from the north-west 4 cells in, where every
// cell of a `length` x 4 rectangle around the cells (x, z) to
// (x + length - 1, z) has at least the verdict `least`: one cell of margin
// before and after, one row above and two below (an avenue is two wide), so
// the network neither crosses nor touches water or another network.
bool FindStrip(void* lots, int32_t sizeX, int32_t sizeZ, int32_t length, int32_t least, int32_t& outX,
               int32_t& outZ)
{
    for (int32_t z = 4; z + 3 < sizeZ - 3; ++z)
        for (int32_t x = 4; x + length + 1 < sizeX - 3; ++x)
            if (Virtual<int32_t>(lots, kCanCreateLot, x - 1, z - 1, length + 2, int32_t{4}, 1u, 0u, 0u) >= least) {
                outX = x;
                outZ = z;
                return true;
            }
    return false;
}

// Free land first, then land with lots; the full length first, then shorter
// (down to 8 cells). False when nothing fits.
bool FindPlace(void* lots, int32_t sizeX, int32_t sizeZ, int32_t& length, int32_t& least, int32_t& x, int32_t& z)
{
    for (int32_t tried = length; tried >= 8; tried = tried * 2 / 3)
        for (const int32_t verdict : {kFree, kLot})
            if (FindStrip(lots, sizeX, sizeZ, tried, verdict, x, z)) {
                length = tried;
                least = verdict;
                return true;
            }
    return false;
}

// How many cells of the city have each verdict, logged once at the drive.
void LogVerdicts(void* lots, int32_t sizeX, int32_t sizeZ)
{
    uint32_t counts[9] = {};
    for (int32_t z = 0; z < sizeZ; ++z)
        for (int32_t x = 0; x < sizeX; ++x) {
            const int32_t verdict = Virtual<int32_t>(lots, kCanCreateLot, x, z, int32_t{1}, int32_t{1}, 1u, 0u, 0u);
            ++counts[verdict >= 0 && verdict <= 8 ? verdict : 0];
        }
    log::Write("autotest: crew: city %d x %d, cells by verdict: outside %u, water %u, zone %u, network %u, lot %u, "
               "free %u",
               sizeX, sizeZ, counts[0], counts[1], counts[2], counts[3], counts[7], counts[8]);
}

// A vector as the game keeps it, over the test's own two points.
struct Anchors {
    const SC4Point<uint32_t>* begin;
    const SC4Point<uint32_t>* end;
    const SC4Point<uint32_t>* capacity;
};

struct Drag {
    cISC4NetworkTool* tool;
    cISC4NetworkOccupant::eNetworkType type;
    SC4Point<uint32_t> points[2];
    int cost;
    uint32_t previewFailure;
    bool puzzle = false; // OPENSC4_TEST_CREW_PUZZLE: the release also asks slot 49
};

// OPENSC4_TEST_CREW_DRAWLINE=1: before each drag's PlaceAllSegments pair,
// the player's mouse move of the network view control (0x00645640): Reset,
// ClearFailureReason, then slot 18 DrawNetworkLine (the NAM-guarded
// 0x0063AF40, which classifies the drag and calls BuildDragPath 0x00639790)
// with copies of the two points, word 0 (the release's PlaceAllSegments
// gets the same) and the type. Then, when it drew a line with steps and
// cells, the read-only checks the segment placer asks (each with the step
// count as the placer passes it): slot 66 IdentifyStartingConditions, slot
// 68 CheckForReservedTiles (type, not skipped), slot 69 CheckForGoodTerrain,
// slot 65 TestForAutoConnectOccupant on the first and the last cell, and
// slot 67 on each cell (up to 64). They only read the city (occupant
// searches and the tool's own cell cache, which the drag's Reset empties).
// Hashed (values only): the answer and the points as DrawNetworkLine left
// them (BuildDragPath may shift them), the classifier's words +0x6C (byte),
// +0x70, +0x74, +0x7C, +0x80, every step's three words and every cell, each
// check's AL and count.
uint32_t g_lineHash = 2166136261u;
uint32_t g_lines = 0, g_linesDrawn = 0;
constexpr std::size_t kToolSteps = 0x54, kToolCells = 0x60, kToolStepByte = 0x6C, kToolStepCount = 0x70;
constexpr std::size_t kToolRegularSteps = 0x74, kToolDirection1 = 0x7C, kToolDirection2 = 0x80;
constexpr std::size_t kSlotClearFailure = 0x34, kSlotDrawNetworkLine = 0x48, kSlotTestAutoConnect = 0x104;
constexpr std::size_t kSlotIdentifyStart = 0x108, kSlotReservedAt = 0x10C, kSlotReserved = 0x110;
constexpr std::size_t kSlotGoodTerrain = 0x114;
constexpr uint32_t kMaxCellChecks = 64;

bool DrawLineOn()
{
    static const bool on = EnvInt("OPENSC4_TEST_CREW_DRAWLINE", 0) == 1;
    return on;
}

void ProbeLine(cISC4NetworkTool* tool, uint32_t type, uint32_t x0, uint32_t z0, uint32_t x1, uint32_t z1)
{
    char* const self = reinterpret_cast<char*>(tool);
    tool->Reset();
    Virtual<void>(tool, kSlotClearFailure);
    uint32_t start[2] = {x0, z0};
    uint32_t end[2] = {x1, z1};
    const uint8_t drawn = Virtual<uint8_t>(tool, kSlotDrawNetworkLine, start, end, 0u, type);
    Hash h;
    h.h = g_lineHash;
    for (const uint32_t v : {type, uint32_t{drawn}, start[0], start[1], end[0], end[1]})
        h.Mix(v);
    h.Mix(At<uint8_t>(self, kToolStepByte));
    for (const std::size_t at : {kToolStepCount, kToolRegularSteps, kToolDirection1, kToolDirection2})
        h.Mix(At<uint32_t>(self, at));
    const auto* const steps = reinterpret_cast<const crew_sim::Vector*>(self + kToolSteps);
    const auto* const cells = reinterpret_cast<const crew_sim::Vector*>(self + kToolCells);
    const uint32_t stepCount = steps->begin ? static_cast<uint32_t>((steps->end - steps->begin) / 12) : 0;
    const uint32_t cellCount = cells->begin ? static_cast<uint32_t>((cells->end - cells->begin) / 8) : 0;
    h.Mix(stepCount);
    for (uint32_t i = 0; i < stepCount * 3; ++i)
        h.Mix(reinterpret_cast<const uint32_t*>(steps->begin)[i]);
    h.Mix(cellCount);
    for (uint32_t i = 0; i < cellCount * 2; ++i)
        h.Mix(reinterpret_cast<const uint32_t*>(cells->begin)[i]);
    if (drawn && stepCount > 0 && cellCount > 0 && At<int32_t>(self, kToolStepCount) > 0) {
        const uint32_t* const cell = reinterpret_cast<const uint32_t*>(cells->begin);
        int32_t count = At<int32_t>(self, kToolStepCount);
        h.Mix(Virtual<uint8_t>(tool, kSlotIdentifyStart, &count));
        h.Mix(static_cast<uint32_t>(count));
        count = At<int32_t>(self, kToolStepCount);
        Virtual<void>(tool, kSlotReserved, &count, type, 0u);
        h.Mix(static_cast<uint32_t>(count));
        count = At<int32_t>(self, kToolStepCount);
        h.Mix(Virtual<uint8_t>(tool, kSlotGoodTerrain, &count));
        h.Mix(static_cast<uint32_t>(count));
        h.Mix(Virtual<uint8_t>(tool, kSlotTestAutoConnect, cell[0], cell[1]));
        h.Mix(Virtual<uint8_t>(tool, kSlotTestAutoConnect, cell[cellCount * 2 - 2], cell[cellCount * 2 - 1]));
        for (uint32_t i = 0; i < cellCount && i < kMaxCellChecks; ++i)
            h.Mix(Virtual<uint8_t>(tool, kSlotReservedAt, cell[2 * i], cell[2 * i + 1], type));
    }
    g_lineHash = h.h;
    ++g_lines;
    g_linesDrawn += drawn ? 1u : 0u;
}

// The drag's own line, then (diagonal = true, the six types' drags) a 45
// degree line from its start, up to 8 cells along the drag's longer axis and
// as many across it, toward the map's inside (the crew saves' maps are at
// least 64 cells): every move of it is a tie, which BuildDragPath settles
// by alternating the axes (0x006398B9).
void DrawLineProbe(cISC4NetworkTool* tool, uint32_t type, const SC4Point<uint32_t>* points, bool diagonal)
{
    if (!DrawLineOn())
        return;
    ProbeLine(tool, type, points[0].x, points[0].y, points[1].x, points[1].y);
    if (!diagonal)
        return;
    const int32_t dx = static_cast<int32_t>(points[1].x - points[0].x);
    const int32_t dz = static_cast<int32_t>(points[1].y - points[0].y);
    const int32_t ax = dx < 0 ? -dx : dx, az = dz < 0 ? -dz : dz;
    const int32_t longer = ax > az ? ax : az;
    const int32_t d = longer < 8 ? longer : 8;
    if (d == 0)
        return;
    const auto toward = [d](uint32_t from, int32_t along) {
        const int32_t step = along != 0 ? (along < 0 ? -d : d) : (from < 32 ? d : -d);
        return static_cast<uint32_t>(static_cast<int32_t>(from) + step);
    };
    ProbeLine(tool, type, points[0].x, points[0].y, toward(points[0].x, dx), toward(points[0].y, dz));
}

// OPENSC4_TEST_CREW_PUZZLE (see the top of the file).
int PuzzleLevel()
{
    static const int level = EnvInt("OPENSC4_TEST_CREW_PUZZLE", 0);
    return level;
}
uint32_t g_pzCalls = 0, g_pzCandidates = 0, g_pzAsked = 0;
uint32_t g_pzHash = 2166136261u;
bool g_pzAsking = false;
cISC4City* g_pzCity = nullptr;
uint32_t g_pzCrossX = 0xFFFFFFFFu, g_pzCrossZ = 0xFFFFFFFFu;
constexpr std::size_t kToolOverrides = 0x08;
constexpr std::size_t kSlotQueryIntersections = 0xC4, kSlotDestroyOccupants = 0xC8;
constexpr std::size_t kCityBudget = 0xD0, kBudgetCash = 0x18;
constexpr uint32_t kYesButton = 0x8A8DFD4E;
// The first node of the RUL0 store (its first non-empty bucket), as the
// game's inlined walks find it.
char* FirstStoreNode(char* store)
{
    namespace rul = nettool_sim::rul;
    char** const begin = At<char**>(store, rul::table::kBuckets);
    const uint32_t buckets = static_cast<uint32_t>(
        static_cast<int32_t>(reinterpret_cast<char*>(At<char**>(store, rul::table::kBucketsEnd)) -
                             reinterpret_cast<char*>(begin)) >>
        2);
    for (uint32_t b = 0; b < buckets; ++b)
        if (begin[b])
            return begin[b];
    return nullptr;
}

// A rule without a cell grid (+0x58 empty) keeps the original's stack
// leftovers in its bounds (+0x18 to +0x24; nettool_rul_1.cpp), ours zeros:
// the game asks FitsInCity only of rules it has chosen (0x0063B340 tests
// +0xE9 and the type mask first; the view control the player's piece), so
// the probes skip rules without cells.
bool HasCells(char* rule)
{
    namespace rul = nettool_sim::rul;
    return At<char*>(rule, rul::rul0::kCells) != At<char*>(rule, rul::rul0::kCells + 4);
}

uint32_t PieceCount(cISC4City* city);

int64_t CashOf(cISC4City* city)
{
    void* const budget = Virtual<void*>(city, kCityBudget);
    return budget ? Virtual<int64_t>(budget, kBudgetCash) : 0;
}

// Slot 50 on the pieces the drive made at the crossing cell, as the network
// manager's DestroyNetworkOccupants hands them to the tool (each referenced
// once; the tool gives the references back).
void ResolveCrossing(cISC4NetworkTool* tool, Hash& h)
{
    std::vector<char*> pieces;
    if (!Collect(g_pzCity, pieces))
        return;
    std::vector<void*> chosen;
    const uint32_t cell = (g_pzCrossX << 16) | (g_pzCrossZ & 0xFFFF);
    for (char* const self : pieces)
        if (CellKey(self) == cell)
            chosen.push_back(self);
    h.Mix(static_cast<uint32_t>(chosen.size()));
    if (chosen.empty())
        return;
    for (void* const piece : chosen)
        nettool_sim::AddRefOf(piece);
    struct List {
        void** begin;
        void** end;
        void** capacity;
    } list{chosen.data(), chosen.data() + chosen.size(), chosen.data() + chosen.size()};
    Virtual<void>(tool, kSlotDestroyOccupants, &list);
    std::vector<char*> after;
    uint32_t left = 0;
    if (Collect(g_pzCity, after))
        for (char* const self : after)
            left += CellKey(self) == cell;
    h.Mix(left);
    h.Mix(static_cast<uint32_t>(after.size()));
    h.Mix(At<uint32_t>(reinterpret_cast<char*>(tool), 0x20C));
}

// The release's call of slot 49 (0x00645544), with what the test reads
// around it hashed.
void PuzzleProbe(cISC4NetworkTool* tool)
{
    if (PuzzleLevel() <= 0 || !g_pzCity)
        return;
    namespace rul = nettool_sim::rul;
    char* const self = reinterpret_cast<char*>(tool);
    Hash h;
    h.h = g_pzHash;
    const auto* const overrides = reinterpret_cast<const crew_sim::Vector*>(self + kToolOverrides);
    const uint32_t count = overrides->begin ? static_cast<uint32_t>((overrides->end - overrides->begin) / 12) : 0;
    h.Mix(count);
    char* const store = reinterpret_cast<char*>(addr::cSC4NetworkTool_Rul0Store);
    uint32_t candidates = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t* const record = reinterpret_cast<const uint32_t*>(overrides->begin) + 3 * i;
        h.Mix(record[0]);
        h.Mix(record[1]);
        h.Mix(record[2]);
        for (char* node = FirstStoreNode(store); node; node = nettool_sim::place::HashNext(store, node)) {
            char* const rule = node + rul::store::kRule;
            if (At<uint32_t>(rule, 0x78) == 0 || !HasCells(rule))
                continue;
            const bool fits = (nettool_sim::CallThis<uint32_t, uint32_t, uint32_t, uint32_t, uint32_t>(
                                   addr::SC4Rul0Rule_FitsInCity, rule, record[0], record[1],
                                   At<uint32_t>(self, nettool_sim::field::kCellsX),
                                   At<uint32_t>(self, nettool_sim::field::kCellsZ)) &
                               0xFF) != 0;
            h.Mix(At<uint32_t>(rule, rul::rul0::kId));
            h.Mix(fits ? 1u : 0u);
            if (!fits)
                continue;
            const uint32_t answer = nettool_sim::CallThis<uint32_t, uint32_t, uint32_t, char*>(
                addr::SC4Rul0Rule_MatchAt, rule, record[0], record[1], self);
            h.Mix(answer);
            candidates += answer == 0;
        }
    }
    const int64_t cash = CashOf(g_pzCity);
    const uint32_t piecesBefore = PieceCount(g_pzCity);
    const uint32_t askedBefore = g_pzAsked;
    g_pzAsking = true;
    Virtual<void>(tool, kSlotQueryIntersections);
    g_pzAsking = false;
    const int64_t cashAfter = CashOf(g_pzCity);
    h.Mix(static_cast<uint32_t>(cash));
    h.Mix(static_cast<uint32_t>(cash >> 32));
    h.Mix(static_cast<uint32_t>(cashAfter));
    h.Mix(static_cast<uint32_t>(cashAfter >> 32));
    h.Mix(piecesBefore);
    h.Mix(PieceCount(g_pzCity));
    h.Mix(g_pzAsked - askedBefore);
    h.Mix(At<uint32_t>(self, 0x20C));
    if (PuzzleLevel() >= 2)
        ResolveCrossing(tool, h);
    ++g_pzCalls;
    g_pzCandidates += candidates;
    g_pzHash = h.h;
    log::Write("autotest: crew: puzzle: %u override records, %u rules fit, asked %u, cash %lld -> %lld", count,
               candidates, g_pzAsked - askedBefore, static_cast<long long>(cash),
               static_cast<long long>(cashAfter));
}

// The release of a player's drag (0x006453FB to 0x00645516), without the
// bridge settings and the neighbour connection question.
void Place(void* context)
{
    Drag& drag = *static_cast<Drag*>(context);
    DrawLineProbe(drag.tool, static_cast<uint32_t>(drag.type), drag.points, true);
    const Anchors anchors{drag.points, drag.points + 2, drag.points + 2};
    drag.tool->Reset();
    Virtual<void>(drag.tool, kSetAnchorPoints, &anchors);
    drag.tool->PlaceAllSegments(false, false, true, drag.type);
    drag.cost = drag.tool->GetCostOfSolution();
    drag.previewFailure = drag.tool->GetFailureState();
    drag.tool->Reset();
    Virtual<void>(drag.tool, kSetAnchorPoints, &anchors);
    drag.tool->PlaceAllSegments(true, false, false, drag.type);
    drag.tool->ClearCellPreviews();
    drag.tool->ClearOccupantHighlights();
    if (drag.puzzle)
        PuzzleProbe(drag.tool);
}

// The strips of the six straight drags (OPENSC4_TEST_CREW_PUZZLE crosses them).
struct Strip {
    int32_t x, z, length;
    bool drawn;
};
Strip g_strips[6] = {};

uint32_t AnswerDialog(void*, int depth)
{
    ++g_dialogs;
    if (g_pzAsking) {
        ++g_pzAsked;
        log::Write("autotest: crew: puzzle: the question opened (depth %d); answered yes", depth);
        return kYesButton;
    }
    log::Write("autotest: crew: a dialog opened (depth %d); answered 0", depth);
    return 0;
}

void DriveTypes(cISC4City* city, char* manager)
{
    void* const lots = Global<void*>(addr::SC4_LotManager);
    const int32_t sizeX = static_cast<int32_t>(city->CellCountX()), sizeZ = static_cast<int32_t>(city->CellCountZ());
    const int32_t wanted = EnvInt("OPENSC4_TEST_CREW_LENGTH", 24);
    if (!lots || wanted < 8 || wanted + 12 > sizeX) {
        log::Write("autotest: crew: no lot manager or no room (%d x %d, length %d); nothing drawn", sizeX, sizeZ,
                   wanted);
        return;
    }
    LogVerdicts(lots, sizeX, sizeZ);
    cISC4NetworkManager* const networks = city->GetNetworkManager();
    uint32_t kindIndex = 0;
    for (const auto& kind : kTypes) {
        const uint32_t index = kindIndex++;
        int32_t length = wanted, least = kFree, x = 0, z = 0;
        if (!FindPlace(lots, sizeX, sizeZ, length, least, x, z)) {
            log::Write("autotest: crew: %s: no strip of 8 cells or more without water or networks", kind.name);
            continue;
        }
        cISC4NetworkTool* const tool = networks->GetNetworkTool(kind.type, false);
        if (!tool) {
            log::Write("autotest: crew: %s: no tool", kind.name);
            continue;
        }
        const uint32_t madeBefore = ReadState(city, manager).made;
        tool->AddRef();
        tool->Init();
        Drag drag{tool,
                  kind.type,
                  {{static_cast<uint32_t>(x), static_cast<uint32_t>(z)},
                   {static_cast<uint32_t>(x + length - 1), static_cast<uint32_t>(z)}},
                  0,
                  0};
        if (!RunModalPlayer(&AnswerDialog, &Place, &drag))
            Place(&drag);
        g_strips[index] = {x, z, length, true};
        const uint32_t failure = tool->GetFailureState();
        tool->Shutdown();
        tool->Release();
        log::Write("autotest: crew: %s (%d,%d)-(%d,%d) on %s land: cost %d, failure %u (preview %u), %u new pieces; "
                   "crews now %u",
                   kind.name, x, z, x + length - 1, z, least == kFree ? "free" : "lots'", drag.cost, failure,
                   drag.previewFailure, ReadState(city, manager).made - madeBefore, CrewCount(manager));
    }
}

void DirectPuzzle(cISC4City* city);

// OPENSC4_TEST_CREW_PUZZLE (see the top of the file): a drag across the
// middle of the road's, the street's and the avenue's strips, from the row
// above to two rows below it.
void DrivePuzzle(cISC4City* city)
{
    if (PuzzleLevel() <= 0)
        return;
    g_pzCity = city;
    cISC4NetworkManager* const networks = city->GetNetworkManager();
    for (const uint32_t index : {0u, 2u, 3u}) {
        const Strip& strip = g_strips[index];
        if (!strip.drawn || strip.length < 8)
            continue;
        const auto& kind = kTypes[index];
        cISC4NetworkTool* const tool = networks->GetNetworkTool(kind.type, false);
        if (!tool)
            continue;
        const uint32_t crossX = static_cast<uint32_t>(strip.x + strip.length / 2);
        g_pzCrossX = crossX;
        g_pzCrossZ = static_cast<uint32_t>(strip.z);
        tool->AddRef();
        tool->Init();
        Drag drag{tool,
                  kind.type,
                  {{crossX, static_cast<uint32_t>(strip.z - 1)}, {crossX, static_cast<uint32_t>(strip.z + 2)}},
                  0,
                  0};
        drag.puzzle = true;
        if (!RunModalPlayer(&AnswerDialog, &Place, &drag))
            Place(&drag);
        log::Write("autotest: crew: puzzle: %s crossing (%u,%d)-(%u,%d): cost %d, failure %u (preview %u)",
                   kind.name, crossX, strip.z - 1, crossX, strip.z + 2, drag.cost, tool->GetFailureState(),
                   drag.previewFailure);
        tool->Shutdown();
        tool->Release();
    }
    DirectPuzzle(city);
}

// OPENSC4_TEST_CREW_PUZZLE=3: the view control's puzzle piece click without
// the final placement (flag 0), for the first four rules with cells that fit
// the city (FitsInCity, as the view control asks first) at two cells of the
// first strip: beside it and on it. The view control does not ask MatchAt;
// the drive hashes its answer and places whatever it says.
void DirectPuzzle(cISC4City* city)
{
    if (PuzzleLevel() < 3 || !g_strips[0].drawn)
        return;
    namespace rul = nettool_sim::rul;
    cISC4NetworkTool* const tool = city->GetNetworkManager()->GetNetworkTool(cISC4NetworkOccupant::Highway, false);
    if (!tool)
        return;
    char* const self = reinterpret_cast<char*>(tool);
    tool->AddRef();
    tool->Init();
    tool->Reset();
    char* const store = reinterpret_cast<char*>(addr::cSC4NetworkTool_Rul0Store);
    Hash h;
    h.h = g_pzHash;
    uint32_t tried = 0;
    const uint32_t sites[2][2] = {
        {static_cast<uint32_t>(g_strips[0].x + 2), static_cast<uint32_t>(g_strips[0].z + 1)},
        {static_cast<uint32_t>(g_strips[0].x + 8), static_cast<uint32_t>(g_strips[0].z)}};
    for (const auto& site : sites) {
        const uint32_t x = site[0], z = site[1];
        uint32_t here = 0;
        for (char* node = FirstStoreNode(store); node && here < 4; node = nettool_sim::place::HashNext(store, node)) {
            char* const rule = node + rul::store::kRule;
            if (!HasCells(rule))
                continue;
            const bool fits = (nettool_sim::CallThis<uint32_t, uint32_t, uint32_t, uint32_t, uint32_t>(
                                   addr::SC4Rul0Rule_FitsInCity, rule, x, z,
                                   At<uint32_t>(self, nettool_sim::field::kCellsX),
                                   At<uint32_t>(self, nettool_sim::field::kCellsZ)) &
                               0xFF) != 0;
            if (!fits)
                continue;
            const uint32_t answer =
                nettool_sim::CallThis<uint32_t, uint32_t, uint32_t, char*>(addr::SC4Rul0Rule_MatchAt, rule, x, z, self);
            const uint32_t id = At<uint32_t>(rule, rul::rul0::kId);
            h.Mix(answer);
            tool->Reset();
            const uint8_t made = nettool_sim::CallThis<uint8_t, uint32_t, uint32_t, uint32_t, uint32_t>(
                addr::cSC4NetworkTool_PlacePuzzlePiece, self, x, z, id, 0u);
            h.Mix(id);
            h.Mix(made);
            h.Mix(tool->GetFailureState());
            h.Mix(static_cast<uint32_t>(tool->GetCostOfSolution()));
            const auto* const cells = reinterpret_cast<const crew_sim::Vector*>(self + kToolCells);
            const uint32_t cellCount = cells->begin ? static_cast<uint32_t>((cells->end - cells->begin) / 8) : 0;
            h.Mix(cellCount);
            for (uint32_t i = 0; i < cellCount * 2; ++i)
                h.Mix(reinterpret_cast<const uint32_t*>(cells->begin)[i]);
            ++here;
            log::Write("autotest: crew: puzzle: rule %08X at (%u,%u): MatchAt %u, placed %u, failure %u, %u cells", id,
                       x, z, answer, made, tool->GetFailureState(), cellCount);
        }
        tried += here;
    }
    tool->Reset();
    g_pzHash = h.h;
    g_pzCalls += tried;
    log::Write("autotest: crew: puzzle: %u rules tried directly", tried);
    tool->Shutdown();
    tool->Release();
}

namespace crew = crew_sim;

// OPENSC4_TEST_CREW_PLUGIN=1: the plugin's byte at 0x006071FC, written the
// way the plugin writes it (only over the game's 0x74).
void PatchSoundJump()
{
    if (EnvInt("OPENSC4_TEST_CREW_PLUGIN", 0) != 1)
        return;
    auto* const at = reinterpret_cast<uint8_t*>(addr::cSC4NetworkConstructionCrew_SoundJump);
    const uint8_t was = *at;
    DWORD protection = 0;
    if (was != crew::kSoundJumpGame || !VirtualProtect(at, 1, PAGE_EXECUTE_READWRITE, &protection)) {
        log::Write("autotest: crew: the byte at 0x006071FC is 0x%02X; not written", was);
        return;
    }
    *at = crew::kSoundJumpPlugin;
    VirtualProtect(at, 1, protection, &protection);
    FlushInstructionCache(GetCurrentProcess(), at, 1);
    log::Write("autotest: crew: the sounds plugin's byte written at 0x006071FC (0x%02X, now 0x%02X)", was, *at);
}

uint32_t VectorLength(char* object, std::size_t offset, std::size_t element)
{
    const auto* const v = reinterpret_cast<const crew::Vector*>(object + offset);
    return static_cast<uint32_t>(static_cast<std::size_t>(v->end - v->begin) / element);
}

// The `life` part (see the top of the file).
void LifeTest(char* manager)
{
    if (EnvInt("OPENSC4_TEST_CREW_LIFE", 1) == 0)
        return;
    const char* const record = reinterpret_cast<const char*>(addr::cSC4NetworkConstructionCrew_NetworkToolTuning) +
                               cISC4NetworkOccupant::Road * crew::tuning::kRecordSize;
    auto value = [record](std::size_t offset) { return *reinterpret_cast<const uint32_t*>(record + offset); };
    const uint32_t count = value(crew::tuning::kCount), seconds = value(crew::tuning::kSeconds);
    const uint32_t scale = value(crew::tuning::kScale), a = value(crew::tuning::kA), b = value(crew::tuning::kB);
    const uint32_t c = value(crew::tuning::kC);
    void* const memory = crew::CallCdecl<void*, uint32_t>(addr::SC4_operator_new, uint32_t{crew::kObjectSize});
    if (!memory) {
        log::Write("autotest: crew: life: no memory");
        return;
    }
    char* const self = crew::CallThis<char*>(addr::cSC4NetworkConstructionCrew_cSC4NetworkConstructionCrew, memory);
    crew::AddRefOf(self);
    const uint32_t before = CrewCount(manager);
    Hash h;
    for (const uint32_t v : {count, seconds, scale, a, b, c})
        h.Mix(v);
    const uint8_t first = Virtual<uint8_t>(self, crew::slot::kInit, count, seconds, scale, a, b, c);
    const uint32_t milliseconds = At<uint32_t>(self, crew::field::kPieceMilliseconds);
    const bool sound = At<void*>(self, crew::field::kSound) != nullptr;
    const int32_t registered = static_cast<int32_t>(CrewCount(manager) - before);
    h.Mix(first);
    h.Mix(milliseconds);
    for (const std::size_t offset : {crew::field::kScale, crew::field::kCount, crew::field::kWorkerValueA,
                                     crew::field::kWorkerValueB, crew::field::kValueC})
        h.Mix(At<uint32_t>(self, offset));
    h.Mix(At<uint8_t>(self, crew::field::kInitialized));
    h.Mix(At<uint8_t>(self, crew::field::kInFrameService));
    h.Mix(sound);
    h.Mix(ListLength(self + crew::field::kPieces));
    h.Mix(VectorLength(self, crew::field::kSegments, 12));
    h.Mix(VectorLength(self, crew::field::kRecords, 8));
    h.Mix(VectorLength(self, crew::field::kJobs, crew::kJobSize));
    h.Mix(static_cast<uint32_t>(registered));
    h.Mix(Global<uint8_t>(addr::cSC4NetworkConstructionCrew_StaticInitialized));
    const uint8_t again = Virtual<uint8_t>(self, crew::slot::kInit, count, seconds, scale, a, b, c);
    h.Mix(again);
    h.Mix(static_cast<uint32_t>(CrewCount(manager) - before));
    const uint8_t down = Virtual<uint8_t>(self, crew::slot::kShutdown);
    const bool soundAfter = At<void*>(self, crew::field::kSound) != nullptr;
    const int32_t registeredAfter = static_cast<int32_t>(CrewCount(manager) - before);
    for (const uint32_t v : {uint32_t{down}, uint32_t{At<uint8_t>(self, crew::field::kInitialized)},
                             uint32_t{soundAfter}, static_cast<uint32_t>(registeredAfter)})
        h.Mix(v);
    const uint32_t left = crew::ReleaseOf(self); // 0: the destructor ran and the memory is gone
    g_lifeHash = h.h;
    log::Write("autotest: crew: life: Init %u (piece %u ms, count %u, sound %u, registered %d), again %u, Shutdown %u "
               "(sound %u, registered %d), last Release %u; life %08X",
               first, milliseconds, count, sound, registered, again, down, soundAfter, registeredAfter, left, h.h);
}

// The `work` part (see the top of the file).
void WorkTest(cISC4City* city)
{
    std::vector<char*> pieces;
    char* chosen = nullptr;
    uint32_t best = 0;
    if (Collect(city, pieces))
        for (char* const self : pieces) {
            const uint32_t cell = CellKey(self);
            if (g_before.count(cell))
                continue;
            if (!chosen || cell < best ||
                (cell == best &&
                 Virtual<uint32_t>(self, piece::slot::kPieceId) < Virtual<uint32_t>(chosen, piece::slot::kPieceId))) {
                chosen = self;
                best = cell;
            }
        }
    if (!chosen) {
        log::Write("autotest: crew: work: no new piece");
        return;
    }
    auto read = [](char* self) {
        uint32_t work = 0;
        const bool found = netmgr_sim::CallCdecl<uint8_t, void*, uint32_t, uint32_t*>(
                               addr::SC4_GetPropertyUint32, self + piece::field::kHolder, kWorkProperty, &work) != 0;
        return found ? work : 0xFFFFFFFFu;
    };
    g_work[0] = read(chosen);
    g_work[1] = crew::CallCdecl<uint32_t, void*>(addr::cSC4NetworkConstructionCrew_IncrementWork, chosen);
    g_work[2] = read(chosen);
    g_work[3] = crew::CallCdecl<uint32_t, void*>(addr::cSC4NetworkConstructionCrew_DecrementWork, chosen);
    g_work[4] = read(chosen);
    log::Write("autotest: crew: work: piece at (%u,%u): %08X, IncrementWork %u, %08X, DecrementWork %u, %08X",
               best >> 16, best & 0xFFFF, g_work[0], g_work[1], g_work[2], g_work[3], g_work[4]);
}

// ====================================================================================
// OPENSC4_TEST_CREW_BRIDGE (see the top of the file)
// ====================================================================================

namespace bridge = nettool_sim::bridges;

// The tool's fields and slots the bridge drags read (src/core/nettool_sim.h).
constexpr std::size_t kToolMajorBridges = 0xD8, kToolPossibleBridges = 0x1F4, kToolFailure = 0x20C;
constexpr std::size_t kToolSetPreferredBridge = 0x80; // slot 32 (int)
// The type table (13 rows of 0x114): the bridge records (+0x30, 0x178 bytes
// each: +0x4 / +0x8 the shortest and longest span in steps, +0xC the cells
// across, the bytes +0x98 land and +0x99 water) and the placement
// parameters' +0x3C / +0x40 (the minor bridge's dip depth and the ends'
// difference, FitMinorBridges 0x00632AD0).
constexpr std::size_t kRowSize = 0x114, kRowBridges = 0x30, kRecordSize = 0x178;
constexpr std::size_t kRecordShortest = 0x4, kRecordLongest = 0x8, kRecordLand = 0x98, kRecordWater = 0x99;
constexpr std::size_t kRowDipDepth = 0x50 + 0x3C, kRowDipEnds = 0x50 + 0x40;
constexpr int32_t kWater = 1, kNetwork = 3;
constexpr int32_t kApproach = 4; // land cells dragged before and after the water

uint32_t g_bridgeHash = 2166136261u;
uint32_t g_bridgeDrags = 0, g_bridgeMajors = 0, g_bridgeChoices = 0;

// The cell verdicts of the whole city (CanCreateLot per cell), 0 outside.
struct Verdicts {
    int32_t sizeX = 0, sizeZ = 0;
    std::vector<int8_t> cells;
    int32_t At(int32_t x, int32_t z) const
    {
        return x < 0 || z < 0 || x >= sizeX || z >= sizeZ ? 0 : cells[static_cast<std::size_t>(z) * sizeX + x];
    }
};

Verdicts ReadVerdicts(void* lots, int32_t sizeX, int32_t sizeZ)
{
    Verdicts v;
    v.sizeX = sizeX;
    v.sizeZ = sizeZ;
    v.cells.resize(static_cast<std::size_t>(sizeX) * sizeZ);
    for (int32_t z = 0; z < sizeZ; ++z)
        for (int32_t x = 0; x < sizeX; ++x)
            v.cells[static_cast<std::size_t>(z) * sizeX + x] = static_cast<int8_t>(
                Virtual<int32_t>(lots, kCanCreateLot, x, z, int32_t{1}, int32_t{1}, 1u, 0u, 0u));
    return v;
}

// A straight drag: `length` cells from (x, z) along x (or z when `alongZ`).
struct Line {
    int32_t x = 0, z = 0, length = 0;
    bool alongZ = false;
    int32_t CellX(int32_t i, int32_t across = 0) const { return alongZ ? x + across : x + i; }
    int32_t CellZ(int32_t i, int32_t across = 0) const { return alongZ ? z + i : z + across; }
};

bool IsLand(int32_t verdict, int32_t least) { return verdict >= least && verdict != kNetwork; }

// The band FindStrip keeps clear, along the line: one cell before and after,
// one line on one side and two on the other (an avenue is two wide); here
// water is allowed in it besides land of `least` or better.
bool BandClear(const Verdicts& v, const Line& line, int32_t least)
{
    for (int32_t i = -1; i <= line.length; ++i)
        for (const int32_t across : {-1, 0, 1, 2}) {
            const int32_t verdict = v.At(line.CellX(i, across), line.CellZ(i, across));
            if (verdict != kWater && !IsLand(verdict, least))
                return false;
        }
    return true;
}

// The first line (rows from the north-west, then columns) with exactly
// `water` water cells between kApproach land cells on each side, the band
// clear; free land first, then land with lots.
bool FindWaterLine(const Verdicts& v, int32_t water, Line& out)
{
    for (const int32_t least : {kFree, kLot})
        for (const bool alongZ : {false, true}) {
            const int32_t lines = alongZ ? v.sizeX : v.sizeZ, along = alongZ ? v.sizeZ : v.sizeX;
            for (int32_t l = 4; l + 3 < lines - 3; ++l)
                for (int32_t s = 4 + kApproach; s + water + kApproach + 4 < along; ++s) {
                    Line line;
                    line.alongZ = alongZ;
                    line.x = alongZ ? l : s - kApproach;
                    line.z = alongZ ? s - kApproach : l;
                    line.length = water + 2 * kApproach;
                    bool fits = true;
                    for (int32_t i = 0; fits && i < line.length; ++i) {
                        const int32_t verdict = v.At(line.CellX(i), line.CellZ(i));
                        const bool inWater = i >= kApproach && i < kApproach + water;
                        fits = inWater ? verdict == kWater : IsLand(verdict, least);
                    }
                    if (fits && BandClear(v, line, least)) {
                        out = line;
                        return true;
                    }
                }
        }
    return false;
}

// The water lengths the type's water records accept: each record's
// shortest and the middle of its range (capped at 30), each once, shortest
// first.
std::vector<int32_t> WaterLengths(uint32_t type, uint32_t& records)
{
    std::set<int32_t> lengths;
    records = 0;
    const char* const row = reinterpret_cast<const char*>(addr::cSC4NetworkConstructionCrew_NetworkToolTuning) +
                            type * kRowSize;
    const auto* const v = reinterpret_cast<const crew_sim::Vector*>(row + kRowBridges);
    for (const char* record = v->begin; record && record + kRecordSize <= v->end; record += kRecordSize) {
        if (!*reinterpret_cast<const uint8_t*>(record + kRecordWater))
            continue;
        ++records;
        const int32_t shortest = *reinterpret_cast<const int32_t*>(record + kRecordShortest);
        const int32_t longest = *reinterpret_cast<const int32_t*>(record + kRecordLongest);
        for (const int32_t n : {shortest, (shortest + longest) / 2})
            if (n >= 2 && n <= 30)
                lengths.insert(n);
    }
    return {lengths.begin(), lengths.end()};
}

// What a preview or a build left in the tool, values only: the major
// bridges (words +0x0 to +0x14, the byte +0x18; per cell the words +0x0 to
// +0x28 but the byte +0x10 alone, the byte +0x40 and, only where it is not
// 0, the words +0x2C to +0x3C (else they may be the original's stack
// leftovers); the cable parts' eight words), the possible bridges, the cost
// and the failure reason. Never a pointer, never padding.
uint32_t HashSolution(cISC4NetworkTool* tool, int cost, uint32_t& majors)
{
    char* const self = reinterpret_cast<char*>(tool);
    Hash h;
    const auto* const bridges = reinterpret_cast<const crew_sim::Vector*>(self + kToolMajorBridges);
    majors = 0;
    for (char* b = bridges->begin; b && b + bridge::major::kSize <= bridges->end; b += bridge::major::kSize) {
        ++majors;
        for (std::size_t w = 0; w <= bridge::major::kReferenceAltitude; w += 4)
            h.Mix(At<uint32_t>(b, w));
        h.Mix(At<uint8_t>(b, bridge::major::kLandBridge));
        char* const cellsEnd = At<char*>(b, bridge::major::kCellsEnd);
        uint32_t cells = 0;
        for (char* c = At<char*>(b, bridge::major::kCells); c && c != cellsEnd; c += bridge::cell::kSize) {
            ++cells;
            for (std::size_t w = 0; w <= bridge::cell::kPierDepth; w += 4)
                h.Mix(w == bridge::cell::kTurn ? At<uint8_t>(c, w) : At<uint32_t>(c, w));
            const uint8_t flags = At<uint8_t>(c, bridge::cell::kWallFlags);
            h.Mix(flags);
            if (flags)
                for (std::size_t w = bridge::cell::kWallScales; w <= bridge::cell::kWallFloat; w += 4)
                    h.Mix(At<uint32_t>(c, w));
            char* const partsEnd = At<char*>(c, bridge::cell::kPartsEnd);
            uint32_t parts = 0;
            for (char* p = At<char*>(c, bridge::cell::kParts); p && p != partsEnd; p += bridge::part::kSize) {
                ++parts;
                for (std::size_t w = 0; w < bridge::part::kSize; w += 4)
                    h.Mix(At<uint32_t>(p, w));
            }
            h.Mix(parts);
        }
        h.Mix(cells);
    }
    h.Mix(majors);
    const auto* const possible = reinterpret_cast<const crew_sim::Vector*>(self + kToolPossibleBridges);
    uint32_t count = 0;
    for (const char* p = possible->begin; p && p != possible->end; p += 4, ++count)
        h.Mix(*reinterpret_cast<const uint32_t*>(p));
    h.Mix(count);
    h.Mix(static_cast<uint32_t>(cost));
    h.Mix(At<uint32_t>(self, kToolFailure));
    return h.h;
}

uint32_t PieceCount(cISC4City* city)
{
    std::vector<char*> pieces;
    return Collect(city, pieces) ? static_cast<uint32_t>(pieces.size()) : 0xFFFFFFFFu;
}

struct BridgeDrag {
    cISC4City* city;
    cISC4NetworkTool* tool;
    cISC4NetworkOccupant::eNetworkType type;
    const char* name;
    SC4Point<uint32_t> points[2];
    bool tryOnly;        // preview only (the search for a line that makes a bridge)
    bool choices;        // the player's choice previews
    int cost = 0;
    uint32_t majors = 0, previewHash = 0, buildHash = 0, failure = 0, chosen = 0, leaked = 0;
    std::vector<uint32_t> choiceHashes;
};

void Preview(BridgeDrag& drag, int32_t preferred)
{
    DrawLineProbe(drag.tool, static_cast<uint32_t>(drag.type), drag.points, false);
    const Anchors anchors{drag.points, drag.points + 2, drag.points + 2};
    drag.tool->Reset();
    if (preferred >= 0)
        Virtual<void>(drag.tool, kToolSetPreferredBridge, preferred);
    Virtual<void>(drag.tool, kSetAnchorPoints, &anchors);
    drag.tool->PlaceAllSegments(false, false, true, drag.type);
    drag.cost = drag.tool->GetCostOfSolution();
}

// A player's drag across water, with the bridge choice: the preview with
// the game's choice, then (with `choices`) a preview per possible bridge
// with it preferred (as the bridge UI's choice makes the view control
// preview again), then the build with the game's own choice (Reset clears
// the preference, so the build is the default drag of Place()).
void PlaceBridge(void* context)
{
    BridgeDrag& drag = *static_cast<BridgeDrag*>(context);
    char* const self = reinterpret_cast<char*>(drag.tool);
    const uint32_t piecesBefore = PieceCount(drag.city);
    Preview(drag, -1);
    drag.previewHash = HashSolution(drag.tool, drag.cost, drag.majors);
    if (drag.tryOnly || drag.majors == 0) {
        drag.tool->Reset(); // takes the preview's occupants out again (slot 16)
        drag.tool->ClearCellPreviews();
        drag.tool->ClearOccupantHighlights();
        return;
    }
    if (drag.choices) {
        const auto* const possible = reinterpret_cast<const crew_sim::Vector*>(self + kToolPossibleBridges);
        std::vector<int32_t> ids;
        for (const char* p = possible->begin; p && p != possible->end; p += 4)
            ids.push_back(*reinterpret_cast<const int32_t*>(p));
        // Check that a preview leaves nothing in the city once Reset ran.
        drag.tool->Reset();
        if (PieceCount(drag.city) != piecesBefore) {
            drag.leaked = 1;
            log::Write("autotest: crew: bridge %s: the preview left %d pieces after Reset; no choice previews",
                       drag.name, static_cast<int>(PieceCount(drag.city) - piecesBefore));
        } else {
            for (const int32_t id : ids) {
                uint32_t majors = 0;
                Preview(drag, id);
                const uint32_t h = HashSolution(drag.tool, drag.cost, majors);
                drag.choiceHashes.push_back(h);
                log::Write("autotest: crew: bridge %s: choice %d: %u bridges, cost %d, failure %08X, %08X", drag.name,
                           id, majors, drag.cost, At<uint32_t>(self, kToolFailure), h);
                drag.tool->Reset();
                if (PieceCount(drag.city) != piecesBefore) {
                    drag.leaked = 1;
                    log::Write("autotest: crew: bridge %s: choice %d left pieces after Reset; choices stopped",
                               drag.name, id);
                    break;
                }
            }
        }
        // The default preview again (the hash of the choice previews is
        // separate), as the player's last move before the release.
        Preview(drag, -1);
    }
    DrawLineProbe(drag.tool, static_cast<uint32_t>(drag.type), drag.points, false);
    const Anchors anchors{drag.points, drag.points + 2, drag.points + 2};
    drag.tool->Reset();
    Virtual<void>(drag.tool, kSetAnchorPoints, &anchors);
    drag.tool->PlaceAllSegments(true, false, false, drag.type);
    uint32_t majors = 0;
    drag.buildHash = HashSolution(drag.tool, 0, majors);
    drag.failure = At<uint32_t>(self, kToolFailure);
    drag.tool->ClearCellPreviews();
    drag.tool->ClearOccupantHighlights();
}

void RunBridgeDrag(BridgeDrag& drag)
{
    if (!RunModalPlayer(&AnswerDialog, &PlaceBridge, &drag))
        PlaceBridge(&drag);
}

void MixBridge(const BridgeDrag& drag)
{
    Hash h;
    h.h = g_bridgeHash;
    h.Mix(drag.type);
    for (const uint32_t v : {drag.points[0].x, drag.points[0].y, drag.points[1].x, drag.points[1].y, drag.majors,
                             drag.previewHash, drag.buildHash, drag.failure, drag.leaked})
        h.Mix(v);
    for (const uint32_t v : drag.choiceHashes)
        h.Mix(v);
    g_bridgeHash = h.h;
    ++g_bridgeDrags;
    g_bridgeMajors += drag.majors;
    g_bridgeChoices += static_cast<uint32_t>(drag.choiceHashes.size());
}

// The lower of the two vertex altitudes across the line at step i (the
// terrain's vertex index slot 0x24 and altitude slot 0x30, ST0).
double LineAltitude(void* terrain, const Line& line, int32_t i)
{
    const uint32_t a = Virtual<uint32_t>(terrain, 0x24, static_cast<uint32_t>(line.CellX(i)),
                                         static_cast<uint32_t>(line.CellZ(i)));
    const uint32_t b = Virtual<uint32_t>(terrain, 0x24, static_cast<uint32_t>(line.CellX(i, 1)),
                                         static_cast<uint32_t>(line.CellZ(i, 1)));
    const double ha = Virtual<double>(terrain, 0x30, a), hb = Virtual<double>(terrain, 0x30, b);
    return ha < hb ? ha : hb;
}

// A land line (no water, band clear) whose two end vertices differ by no
// more than `ends` and whose lowest vertex between them is more than `depth`
// below the lower end: a dip a minor bridge spans (FitMinorBridges' test,
// approximated: the code decides itself).
bool FindDipLine(const Verdicts& v, void* terrain, double depth, double ends, Line& out)
{
    for (const bool alongZ : {false, true}) {
        const int32_t lines = alongZ ? v.sizeX : v.sizeZ, along = alongZ ? v.sizeZ : v.sizeX;
        for (int32_t length = 8; length <= 20; length += 4)
            for (int32_t l = 4; l + 3 < lines - 3; l += 2)
                for (int32_t s = 4; s + length + 4 < along; s += 2) {
                    Line line;
                    line.alongZ = alongZ;
                    line.x = alongZ ? l : s;
                    line.z = alongZ ? s : l;
                    line.length = length;
                    bool fits = true;
                    for (int32_t i = -1; fits && i <= length; ++i)
                        for (const int32_t across : {-1, 0, 1, 2})
                            fits = fits && IsLand(v.At(line.CellX(i, across), line.CellZ(i, across)), kLot);
                    if (!fits)
                        continue;
                    const double first = LineAltitude(terrain, line, 0), last = LineAltitude(terrain, line, length - 1);
                    if (!(first - last <= ends && last - first <= ends))
                        continue;
                    const double top = first < last ? first : last;
                    double lowest = top;
                    for (int32_t i = 1; i + 1 < length; ++i) {
                        const double h = LineAltitude(terrain, line, i);
                        if (h < lowest)
                            lowest = h;
                    }
                    if (top - lowest > depth) {
                        out = line;
                        return true;
                    }
                }
    }
    return false;
}

void DriveBridges(cISC4City* city, char* manager)
{
    const int mode = EnvInt("OPENSC4_TEST_CREW_BRIDGE", 0);
    if (mode <= 0)
        return;
    void* const lots = Global<void*>(addr::SC4_LotManager);
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    const int32_t sizeX = static_cast<int32_t>(city->CellCountX()), sizeZ = static_cast<int32_t>(city->CellCountZ());
    if (!lots || !terrain)
        return;
    cISC4NetworkManager* const networks = city->GetNetworkManager();
    const bool choices = EnvInt("OPENSC4_TEST_CREW_BRIDGE_CHOICES", 1) != 0;
    struct Kind {
        cISC4NetworkOccupant::eNetworkType type;
        const char* name;
        bool reversed;
    };
    std::vector<Kind> kinds = {{cISC4NetworkOccupant::Road, "road", false}, {cISC4NetworkOccupant::Rail, "rail", true}};
    if (mode >= 2) {
        kinds.push_back({cISC4NetworkOccupant::Avenue, "avenue", false});
        kinds.push_back({cISC4NetworkOccupant::Highway, "highway", true});
    }
    for (const Kind& kind : kinds) {
        uint32_t records = 0;
        const std::vector<int32_t> lengths = WaterLengths(kind.type, records);
        const Verdicts v = ReadVerdicts(lots, sizeX, sizeZ);
        cISC4NetworkTool* const tool = networks->GetNetworkTool(kind.type, false);
        if (!tool || lengths.empty()) {
            log::Write("autotest: crew: bridge %s: %u water records, %u lengths; nothing drawn", kind.name, records,
                       static_cast<uint32_t>(lengths.size()));
            continue;
        }
        tool->AddRef();
        tool->Init();
        bool done = false;
        uint32_t tried = 0;
        for (const int32_t water : lengths) {
            Line line;
            if (done || tried >= 4 || !FindWaterLine(v, water, line))
                continue;
            ++tried;
            BridgeDrag drag{city, tool, kind.type, kind.name, {}, true, choices, 0, 0, 0, 0, 0, 0, 0, {}};
            const SC4Point<uint32_t> a{static_cast<uint32_t>(line.CellX(0)), static_cast<uint32_t>(line.CellZ(0))};
            const SC4Point<uint32_t> b{static_cast<uint32_t>(line.CellX(line.length - 1)),
                                       static_cast<uint32_t>(line.CellZ(line.length - 1))};
            drag.points[0] = kind.reversed ? b : a;
            drag.points[1] = kind.reversed ? a : b;
            RunBridgeDrag(drag); // the preview alone
            log::Write("autotest: crew: bridge %s: water %d at (%u,%u)-(%u,%u): preview %u bridges, cost %d, %08X",
                       kind.name, water, drag.points[0].x, drag.points[0].y, drag.points[1].x, drag.points[1].y,
                       drag.majors, drag.cost, drag.previewHash);
            if (drag.majors == 0)
                continue;
            drag.tryOnly = false;
            const uint32_t madeBefore = ReadState(city, manager).made;
            RunBridgeDrag(drag);
            MixBridge(drag);
            done = true;
            log::Write("autotest: crew: bridge %s: built: %u bridges, failure %08X, %u choices, %u new pieces, "
                       "preview %08X, build %08X",
                       kind.name, drag.majors, drag.failure, static_cast<uint32_t>(drag.choiceHashes.size()),
                       ReadState(city, manager).made - madeBefore, drag.previewHash, drag.buildHash);
        }
        if (!done)
            log::Write("autotest: crew: bridge %s: no line made a bridge (%u water records, %u lines tried)", kind.name,
                       records, tried);
        tool->Shutdown();
        tool->Release();
    }

    // A minor (land) bridge: a road over a dip.
    const char* const roadRow = reinterpret_cast<const char*>(addr::cSC4NetworkConstructionCrew_NetworkToolTuning) +
                                cISC4NetworkOccupant::Road * kRowSize;
    const double depth = *reinterpret_cast<const float*>(roadRow + kRowDipDepth);
    const double ends = *reinterpret_cast<const float*>(roadRow + kRowDipEnds);
    uint32_t landRecords = 0;
    const auto* const roadBridges = reinterpret_cast<const crew_sim::Vector*>(roadRow + kRowBridges);
    for (const char* record = roadBridges->begin; record && record + kRecordSize <= roadBridges->end;
         record += kRecordSize)
        landRecords += *reinterpret_cast<const uint8_t*>(record + kRecordLand) != 0;
    if (landRecords == 0) {
        log::Write("autotest: crew: bridge road dip: the road has no land bridge record; nothing drawn");
        return;
    }
    const Verdicts v = ReadVerdicts(lots, sizeX, sizeZ);
    Line line;
    if (!FindDipLine(v, terrain, depth, ends, line)) {
        log::Write("autotest: crew: bridge road dip: no land line with a dip deeper than %.3f (ends within %.3f)", depth,
                   ends);
        return;
    }
    cISC4NetworkTool* const tool = networks->GetNetworkTool(cISC4NetworkOccupant::Road, false);
    if (!tool)
        return;
    tool->AddRef();
    tool->Init();
    BridgeDrag drag{city, tool, cISC4NetworkOccupant::Road, "road dip", {}, false, choices, 0, 0, 0, 0, 0, 0, 0, {}};
    drag.points[0] = {static_cast<uint32_t>(line.CellX(0)), static_cast<uint32_t>(line.CellZ(0))};
    drag.points[1] = {static_cast<uint32_t>(line.CellX(line.length - 1)),
                      static_cast<uint32_t>(line.CellZ(line.length - 1))};
    const uint32_t madeBefore = ReadState(city, manager).made;
    RunBridgeDrag(drag);
    MixBridge(drag);
    log::Write("autotest: crew: bridge road dip (%u,%u)-(%u,%u): %u bridges, cost %d, failure %08X, %u new pieces, "
               "preview %08X, build %08X",
               drag.points[0].x, drag.points[0].y, drag.points[1].x, drag.points[1].y, drag.majors, drag.cost,
               drag.failure, ReadState(city, manager).made - madeBefore, drag.previewHash, drag.buildHash);
    tool->Shutdown();
    tool->Release();
}

void Drive(const Day& day)
{
    if (Env("OPENSC4_TEST_CREW").empty() || !day.counted)
        return;
    char* const manager = Manager(day.city);
    if (!manager)
        return;
    if (!g_driven && OnRunDay(day, "OPENSC4_TEST_CREW")) {
        std::vector<char*> pieces;
        if (!Collect(day.city, pieces)) {
            log::Write("autotest: crew: no occupant manager; nothing drawn");
            return;
        }
        for (char* const self : pieces)
            g_before.insert(CellKey(self));
        g_driven = true;
        PatchSoundJump();
        LifeTest(manager);
        DriveTypes(day.city, manager);
        DrivePuzzle(day.city);
        WorkTest(day.city);
        DriveBridges(day.city, manager);
        const State s = ReadState(day.city, manager);
        log::Write("autotest: crew: drive done: crews %u, jobs %u, %u new pieces, %u dialogs", s.crews, s.jobs,
                   s.made, g_dialogs);
        g_lastCrews = s.crews;
        return;
    }
    if (!g_driven || (g_lastCrews == 0 && CrewCount(manager) == 0))
        return;
    const State s = ReadState(day.city, manager);
    if (s.crews > 0)
        log::Write("autotest: crew: day %d: crews %u, pieces waiting %u, usable %u, lots %u", day.day - day.first,
                   s.crews, s.waiting, s.madeUsable, s.lots);
    else
        log::Write("autotest: crew: day %d: the last crew has gone; pieces waiting %u, usable %u of %u, lots %u",
                   day.day - day.first, s.waiting, s.madeUsable, s.made, s.lots);
    g_lastCrews = s.crews;
}

void Fingerprint(cISC4City* city, std::string& line)
{
    if (Env("OPENSC4_TEST_CREW").empty())
        return;
    char* const manager = Manager(city);
    if (!manager) {
        line += " | crew none";
        return;
    }
    const State s = ReadState(city, manager);
    char text[288];
    std::snprintf(text, sizeof text,
                  " | crew %u,%u,%u,%u wait %u,%u,%u unusable %u lots %u new %u,%u,%08X,%08X life %08X work "
                  "%08X,%u,%08X,%u,%08X",
                  s.crews, s.jobs, s.active, s.queued, s.tagged, s.waiting, s.tagSum, s.unusable, s.lots, s.made,
                  s.madeUsable, s.madeSum, s.madeXor, g_lifeHash, g_work[0], g_work[1], g_work[2], g_work[3],
                  g_work[4]);
    line += text;
    if (EnvInt("OPENSC4_TEST_CREW_BRIDGE", 0) > 0) {
        std::snprintf(text, sizeof text, " brg %u,%u,%u,%08X", g_bridgeDrags, g_bridgeMajors, g_bridgeChoices,
                      g_bridgeHash);
        line += text;
    }
    if (DrawLineOn()) {
        std::snprintf(text, sizeof text, " dl %u,%u,%08X", g_lines, g_linesDrawn, g_lineHash);
        line += text;
    }
    if (PuzzleLevel() > 0) {
        std::snprintf(text, sizeof text, " pz %u,%u,%08X", g_pzCalls, g_pzCandidates, g_pzHash);
        line += text;
    }
}

const FingerprintPart kFingerprint{"crew", 174, &Fingerprint};
const DayHook kDrive{"crew.drive", 196, &Drive};

} // namespace
} // namespace opensc4::autotest
