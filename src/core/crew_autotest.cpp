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
#include "core/autotest_hooks.h"
#include "core/crew_sim.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/netlot_sim.h"
#include "core/netmgr_sim.h"
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
};

// The release of a player's drag (0x006453FB to 0x00645516), without the
// bridge settings and the neighbour connection question.
void Place(void* context)
{
    Drag& drag = *static_cast<Drag*>(context);
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
}

uint32_t AnswerDialog(void*, int depth)
{
    ++g_dialogs;
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
    for (const auto& kind : kTypes) {
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
        const uint32_t failure = tool->GetFailureState();
        tool->Shutdown();
        tool->Release();
        log::Write("autotest: crew: %s (%d,%d)-(%d,%d) on %s land: cost %d, failure %u (preview %u), %u new pieces; "
                   "crews now %u",
                   kind.name, x, z, x + length - 1, z, least == kFree ? "free" : "lots'", drag.cost, failure,
                   drag.previewFailure, ReadState(city, manager).made - madeBefore, CrewCount(manager));
    }
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
        WorkTest(day.city);
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
}

const FingerprintPart kFingerprint{"crew", 174, &Fingerprint};
const DayHook kDrive{"crew.drive", 196, &Drive};

} // namespace
} // namespace opensc4::autotest
