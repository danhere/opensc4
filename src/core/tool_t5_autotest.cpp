// The terraform and obliterate tools' part of the autotest
// (src/core/autotest_hooks.h; tool_brush.cpp, tool_obliterate.cpp): drives the
// two tools and hashes what comes out, so that a run with ui.tools.brush and
// ui.tools.obliterate on and one with them off can be compared. Only the
// game's addresses and vtable slots are called, never our own functions.
//
//   OPENSC4_TEST_TOOLS5=<day>    token "tools5", sub-hashes on the log lines
//     "autotest: tools5: ...". Everything here leaves the city as it was:
//
//   1. terraform: what the terrain says of the ten brushes (types 0 to 6 of
//      its slot 58 and the mayor's three fixed ones): radius, cost, the ten
//      IDs of the looks. The tool made as the view makes it (constructor
//      0x004D9430 on 0x130 new bytes filled with 0x5A, with a brush and
//      whether it is free; the view's slot 0x64 with 2): the fields after the
//      constructor (free and not) and after Init; LoadTuning by address and
//      the six tuning values, TuningReset; SetLooks, UpdateCursor and
//      ApplyModifiers (0 to 3) by address; the keys (F1 to F10 and the digits
//      with modifier 1, others, 0x10 and 0x11 down and up with modifiers 0 to
//      3); CountApplication once and at the tuning's count; Apply with
//      nothing queued; PressAltitude and CursorPoint by address and
//      OnMouseMove over a grid of screen points; the handlers at a point off
//      the ground (a press there begins nothing); the frame message and
//      another with no scrub on; two stretches queued by address (the
//      destructor drops them); the view puts the tool away. (No key after
//      that: OnKeyDown asks the view the base's Shutdown has let go of, in the
//      game too.)
//   2. obliterate: the tool (constructor 0x0049CF90 on 0xA8 bytes): the
//      fields; LoadTuning by address, the three tuning values and the curve
//      at a few times, TuningReset; the filter's function over a few types;
//      Init through the view's slot 0x64 with 2, the fields after it, the
//      filter object, the list of blocks; BlockHasOccupants by address over
//      every block against the test's own question of the occupant manager
//      (how many agree; which blocks have occupants is on a "varies" log
//      line: a prop at the city's edge comes and goes from run to run in the
//      game); a key; the frame message in states 4 and 0 (set by hand:
//      in state 1 it would demolish); Post by address with a type nobody
//      takes; Escape, which has the view put the tool away (Shutdown) at
//      once, before any frame message reaches it. The C runtime's rand()
//      state (the simulation draws from it, and Init shuffles the blocks
//      with it) is kept and put back around Init.
//   OPENSC4_TEST_TOOLS5_PARTS: 1 terraform, 2 obliterate.
//
//   OPENSC4_TEST_TOOLS5_COMMIT=1 adds what changes the ground (and, for the
//   brush that costs, the funds). For a free brush and for one that costs:
//
//   3. a scrub inside the one hook: OnMouseDownL on the ground (the first
//      application), OnMouseMove to a second point (an application), a second
//      move (not taken before a message), three frame messages (the third
//      queues the last point again: an application), OnMouseExit,
//      OnMouseUpL. Then a press at a third point and the view puts the tool
//      away in the scrub (Shutdown ends it; the test takes the tool off the
//      frame service, which Shutdown leaves to OnMouseUpL).
//      The strength of every application after the first of a scrub follows
//      real time in the game (the stopwatch at +0xC0): the test sets the
//      "nothing applied yet" byte (+0xD8) before each, so every application
//      has the plain strength and the stopwatch is never read.
//
//   OPENSC4_TEST_TOOLS5_OBLITERATE=1 wipes the city in memory: a second
//   obliterate tool is inited, 50000 microseconds at a time are added to its
//   time and SweepTick (0x0049D5A0) called until the finale begins (state
//   2), FinaleTick (0x0049D070) with the tuning's time removes, demolishes
//   and dezones everything (state 3), and the view puts the tool away. All
//   inside the one hook: no frame message reaches the tool.
//   NEVER beyond state 3: End (0x0049CC30), AfterWipe (0x0049CE20) and the
//   tool's message 0xEA623590 post 0xEA65224E, at which the application
//   deletes the city's file and writes it again. The test calls none of
//   them, sets the state of the put-away tool to 4 (End does nothing then)
//   and keeps its reference, so the tool is never freed; it also listens for
//   0xEA65224E for the rest of the run: the line "autotest: tools5: the day
//   after" says how often it came (0), and a token with "SCENEOVER" on the
//   fingerprint line would say it came.
//
// Not reached: the tool's two view messages (they ask the window manager
// where the real pointer is), Caps Lock (the level plane of CursorPoint in a
// scrub), an application that is not the first of its scrub with its real
// strength, the budget refusing, funds at 0; of the obliterate tool End,
// AfterWipe and the effect's message, by the rule above.
// Kept out of the hashes: every pointer (only whether it is set), the two
// stopwatches, the order of the shuffled blocks and the count of blocks
// without occupants in a row at the end (both follow rand(); on the log),
// which blocks have occupants (the "varies" lines).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/terrain_sim.h"

#include <opensc4/addresses.h>

#include <GZCLSIDDefs.h>
#include <GZServPtrs.h>
#include <cIGZMessage2.h>
#include <cIGZMessageServer2.h>
#include <cIGZMessageTarget2.h>
#include <cIGZWin.h>
#include <cISC4App.h>
#include <cISC4City.h>
#include <cISC4View3DWin.h>
#include <cRZAutoRefCount.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace opensc4::autotest {
namespace {

struct Fnv {
    uint32_t h = 2166136261u;
    void Bytes(const void* data, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
            h = (h ^ static_cast<const uint8_t*>(data)[i]) * 16777619u;
    }
    template <typename T>
    void Add(const T& value)
    {
        Bytes(&value, sizeof value);
    }
};

char g_token[48];
int32_t g_sceneOver = 0; // how often 0xEA65224E came
int32_t g_ranOn = -1;    // the day the test ran

namespace field { // the base class (tool_base.cpp)
constexpr std::size_t kInited = 0x04, kCount = 0x08, kId = 0x0C, kCursorId = 0x10, kCursor = 0x14;
constexpr std::size_t kWindow = 0x18, kView = 0x1C, kTarget = 0x28;
} // namespace field

namespace brush { // tool_brush.cpp
constexpr std::size_t kSaved = 0x24, kScrubbing = 0x64, kStrength = 0x8C, kRadius = 0x90;
constexpr std::size_t kApplied = 0x98, kApplications = 0xB0, kSpent = 0xB8, kFirst = 0xD8, kEffect = 0xE8;
constexpr std::size_t kZoomEffect = 0xEC, kPressAltitude = 0x124, kQueue = 0x128, kSize = 0x130;
} // namespace brush

namespace wipe { // tool_obliterate.cpp
constexpr std::size_t kFilter = 0x2C, kOccupants = 0x30, kZones = 0x34, kDemolition = 0x38, kLastX = 0x3C;
constexpr std::size_t kLastZ = 0x40, kState = 0x44, kBlocks = 0x48, kList = 0x60, kElapsed = 0x78, kAsked = 0x90;
constexpr std::size_t kFinale = 0x98, kDemolished = 0x9C, kEmptyRun = 0xA0, kSize = 0xA8;
} // namespace wipe

constexpr uint32_t kMessageFrame = 0x86AD10EE, kMessageSceneOver = 0xEA65224E, kFrameCode = 0xC0;
constexpr int32_t kOff = -1000; // a screen point that picks nothing

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

// The city view, as tool_bulldoze_autotest.cpp finds it.
char* CityView()
{
    constexpr uint32_t kWinSC4App = 0x6104489A;
    constexpr uint32_t kWinView3D = 0x9A47B417;
    cISC4AppPtr app;
    cIGZWin* const main = app ? app->GetMainWindow() : nullptr;
    cIGZWin* const parent = main ? main->GetChildWindowFromID(kWinSC4App) : nullptr;
    cRZAutoRefCount<cISC4View3DWin> view;
    if (parent)
        parent->GetChildAs(kWinView3D, kGZIID_cISC4View3DWin, view.AsPPVoid());
    return reinterpret_cast<char*>(static_cast<cISC4View3DWin*>(view));
}

// New memory filled with 0x5A, so that what a constructor leaves alone shows.
char* NewBlock(std::size_t size)
{
    char* const block = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, static_cast<uint32_t>(size)));
    if (block)
        std::memset(block, 0x5A, size);
    return block;
}

// The base fields of a tool: the plain ones, and of the pointers whether
// they are set.
void HashTool(Fnv& f, char* tool)
{
    f.Add(At<uint8_t>(tool, field::kInited));
    f.Add(At<uint32_t>(tool, field::kCount));
    f.Add(At<uint32_t>(tool, field::kId));
    f.Add(At<uint32_t>(tool, field::kCursorId));
    for (const std::size_t offset : {field::kCursor, field::kWindow, field::kView})
        f.Add(uint8_t{At<void*>(tool, offset) != nullptr});
}

// The handlers: (x, z, modifiers) in the view window's pixels.
uint8_t MouseDownL(char* tool, int32_t x, int32_t z)
{
    return Virtual<uint8_t>(tool, 0x40, x, z, uint32_t{0});
}

uint8_t MouseUpL(char* tool, int32_t x, int32_t z)
{
    return Virtual<uint8_t>(tool, 0x48, x, z, uint32_t{0});
}

uint8_t MouseMove(char* tool, int32_t x, int32_t z)
{
    return Virtual<uint8_t>(tool, 0x50, x, z, uint32_t{0});
}

uint8_t KeyDown(char* tool, uint32_t key, uint32_t modifiers)
{
    return Virtual<uint8_t>(tool, 0x38, key, modifiers);
}

uint8_t KeyUp(char* tool, uint32_t key, uint32_t modifiers)
{
    return Virtual<uint8_t>(tool, 0x3C, key, modifiers);
}

// A message of a type with a second number, to the message target part of a
// tool (its slot 3).
void Message(char* tool, uint32_t type, uint32_t second)
{
    Deliver(tool + field::kTarget - 0x4, type, nullptr, second, 0);
}

// A tool given to the view as its makers do: held, the view's slot 0x64.
// The reference stays ours until the caller lets go of it.
bool GiveToView(char* view, char* tool)
{
    Virtual<uint32_t>(tool, 0x4);
    Virtual<uint8_t>(view, 0x64, static_cast<void*>(tool), uint32_t{2});
    return Virtual<void*>(view, 0x60) == tool;
}

// The screen points the tests try, row by row.
struct Point {
    int32_t x, z;
};

template <typename Fn>
void ForGrid(Fn&& fn)
{
    for (int32_t z = 60; z <= 700; z += 40)
        for (int32_t x = 40; x <= 980; x += 60)
            fn(Point{x, z});
}

// A grid point with ground under it well inside the city: the view's
// PickTerrain (slot 36) as the tool's CursorPoint asks.
struct Ground {
    Point at{};
    float point[3] = {};
};

std::vector<Ground> GroundPoints(char* view, cISC4City* city)
{
    std::vector<Ground> found;
    const float sizeX = static_cast<float>(city->CellCountX()) * 16.0f;
    const float sizeZ = static_cast<float>(city->CellCountZ()) * 16.0f;
    constexpr float kMargin = 160.0f;
    ForGrid([&](Point p) {
        Ground g;
        g.at = p;
        if (Virtual<uint8_t>(view, 0x90, p.x, p.z, &g.point[0], uint32_t{0}) && g.point[0] >= kMargin &&
            g.point[2] >= kMargin && g.point[0] < sizeX - kMargin && g.point[2] < sizeZ - kMargin)
            found.push_back(g);
    });
    return found;
}

int64_t Funds()
{
    void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
    return budget ? Virtual<int64_t>(budget, 0x18) : -1;
}

// The state of every vertex of the terrain (altitude, water depth, sediment,
// flags), the sea level, the height range and the size of the scrub set, as
// terrain_brush_autotest.cpp takes it.
uint32_t MapState()
{
    namespace ts = terrain_sim;
    char* const terrain = Global<char*>(addr::SC4_Terrain);
    if (!terrain || !At<uint32_t>(terrain, ts::field::kVertices) || !At<void*>(terrain, ts::field::kWater) ||
        !At<uint32_t>(At<void*>(terrain, ts::field::kWater), ts::water::kRecords))
        return 0;
    Fnv f;
    const uint32_t count = At<uint32_t>(terrain, ts::field::kVertexCount);
    for (uint32_t index = 0; index < count; ++index) {
        const uint32_t vertex = ts::RecordAddress(ts::VertexBase(terrain), index);
        const uint32_t wet = ts::RecordAddress(ts::WaterBase(terrain), index);
        f.Add(ts::AtAddress<uint32_t>(vertex));
        f.Add(ts::AtAddress<uint32_t>(wet + ts::water::kDepth));
        f.Add(ts::AtAddress<uint32_t>(wet + ts::water::kSediment));
        f.Add(ts::AtAddress<uint16_t>(vertex + ts::vertex::kFlags));
    }
    f.Add(At<uint32_t>(terrain, ts::field::kSeaLevel));
    f.Add(At<uint32_t>(terrain, ts::field::kMinHeight));
    f.Add(At<uint32_t>(terrain, ts::field::kMaxHeight));
    f.Add(At<uint32_t>(terrain, 0x1D8));
    return f.h;
}

// --- The terraform tool --------------------------------------------------------------------

// How many stretches are queued, and their points (not their times).
int32_t HashQueue(Fnv& f, char* tool)
{
    char* const head = At<char*>(tool, brush::kQueue);
    int32_t count = 0;
    if (head)
        for (char* node = At<char*>(head, 0); node != head && count < 1000; node = At<char*>(node, 0)) {
            f.Bytes(node + 0xC, 0x10);
            ++count;
        }
    f.Add(count);
    return count;
}

// Every field but the pointers (whether they are set) and the stopwatches.
void HashBrushTool(Fnv& f, char* tool)
{
    HashTool(f, tool);
    f.Bytes(tool + brush::kSaved, 0x4); // the saved cell cursor state
    f.Bytes(tool + 0x2C, 0x30); // the brush, its ten IDs, the frames
    for (const std::size_t offset : {std::size_t{0x5C}, std::size_t{0x60}, std::size_t{0x70}, brush::kEffect,
                                     brush::kZoomEffect})
        f.Add(uint8_t{At<void*>(tool, offset) != nullptr});
    f.Bytes(tool + 0x64, 0xC);   // scrubbing, inside, the last point
    f.Bytes(tool + 0x74, 0x4C);  // the parameter block, moved, applications, free, spent
    f.Bytes(tool + 0xD8, 0x10);  // first, 1.0, the keys' strength and radius
    f.Bytes(tool + 0xF0, 0x18);  // the looks' point, the two cursors, +0x104
    f.Bytes(tool + 0x120, 0x8);  // the queue's delay, the pressed altitude
    HashQueue(f, tool);
}

// The six tuning values of the terraform tool.
struct BrushTuning {
    uint32_t perFlush, bit1Strength, bit1Radius, bit0Strength, bit0Radius, rate;
};

BrushTuning ReadBrushTuning()
{
    BrushTuning t;
    t.perFlush = Global<uint32_t>(addr::SC4TerraformTool_UpdatesPerFlush);
    t.bit1Strength = Global<uint32_t>(addr::SC4TerraformTool_Bit1Strength);
    t.bit1Radius = Global<uint32_t>(addr::SC4TerraformTool_Bit1Radius);
    t.bit0Strength = Global<uint32_t>(addr::SC4TerraformTool_Bit0Strength);
    t.bit0Radius = Global<uint32_t>(addr::SC4TerraformTool_Bit0Radius);
    t.rate = Global<uint32_t>(addr::SC4TerraformTool_StrengthRate);
    return t;
}

void WriteBrushTuning(const BrushTuning& t)
{
    Global<uint32_t>(addr::SC4TerraformTool_UpdatesPerFlush) = t.perFlush;
    Global<uint32_t>(addr::SC4TerraformTool_Bit1Strength) = t.bit1Strength;
    Global<uint32_t>(addr::SC4TerraformTool_Bit1Radius) = t.bit1Radius;
    Global<uint32_t>(addr::SC4TerraformTool_Bit0Strength) = t.bit0Strength;
    Global<uint32_t>(addr::SC4TerraformTool_Bit0Radius) = t.bit0Radius;
    Global<uint32_t>(addr::SC4TerraformTool_StrengthRate) = t.rate;
}

// What the terrain knows of a brush.
struct BrushFacts {
    uint32_t id = 0;
    uint8_t found = 0;
    uint32_t radius = 0; // a float's bits
    int64_t cost = 0;
    uint32_t looks[10] = {};
};

BrushFacts FactsOf(void* terrain, uint32_t id)
{
    BrushFacts facts;
    facts.id = id;
    std::memset(&facts.looks[0], 0x5A, sizeof facts.looks);
    facts.found = Virtual<uint8_t>(terrain, 0xFC, id, &facts.looks[0]);
    const float radius = Virtual<float>(terrain, 0xF4, id);
    std::memcpy(&facts.radius, &radius, sizeof radius);
    facts.cost = Virtual<int64_t>(terrain, 0xF8, id);
    return facts;
}

void HashFacts(Fnv& f, const BrushFacts& facts)
{
    f.Add(facts.id);
    f.Add(facts.found);
    f.Add(facts.radius);
    f.Add(facts.cost);
    f.Add(facts.looks);
}

// A terraform tool as the view's 0x007E5910 makes one; null when it cannot
// be made.
char* NewBrushTool(uint32_t id, uint32_t free)
{
    char* const tool = NewBlock(brush::kSize);
    if (!tool || !This<void*>(addr::SC4TerraformTool_Construct, tool, id, free))
        return nullptr;
    return tool;
}

uint32_t DriveBrush(char* view, const std::vector<Ground>& ground, uint32_t* freeId, uint32_t* paidId)
{
    Fnv f;
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    const uint32_t mapBefore = MapState();
    const int64_t fundsBefore = Funds();
    f.Add(uint8_t{Virtual<void*>(view, 0x60) != nullptr});
    f.Add(mapBefore);

    // The brushes.
    BrushFacts facts[10];
    for (uint32_t type = 0; type < 7; ++type) {
        uint32_t id = 0;
        const uint8_t known = Virtual<uint8_t>(terrain, 0xE8, type, &id);
        f.Add(known);
        facts[type] = FactsOf(terrain, id);
    }
    facts[7] = FactsOf(terrain, 0x0A33E37B);
    facts[8] = FactsOf(terrain, 0xEA33E2FD);
    facts[9] = FactsOf(terrain, 0x4A524342);
    for (int i = 0; i < 10; ++i) {
        HashFacts(f, facts[i]);
        log::Write("autotest: tools5: brush %d: %08X, looks found %u, radius %08X, cost %lld, effects %08X %08X / "
                   "%08X %08X, cursors %08X %08X",
                   i, facts[i].id, facts[i].found, facts[i].radius, static_cast<long long>(facts[i].cost),
                   facts[i].looks[0], facts[i].looks[4], facts[i].looks[2], facts[i].looks[6], facts[i].looks[8],
                   facts[i].looks[9]);
    }
    *freeId = facts[0].id;
    *paidId = facts[1].id;
    for (int i = 9; i >= 7; --i)
        if (facts[i].found && facts[i].cost > 0)
            *paidId = facts[i].id;
    const uint32_t brushes = f.h;
    if (!*freeId) {
        log::Write("autotest: tools5: the terrain has no brush of type 0");
        return f.h;
    }

    // The constructor: a brush that costs (let go at once: the destructor of
    // a tool that was never inited), and the free one the rest is done with.
    char* const other = NewBrushTool(*paidId, 0);
    char* const tool = NewBrushTool(*freeId, 1);
    if (!other || !tool)
        return 0;
    HashBrushTool(f, other);
    Virtual<uint32_t>(other, 0x4);
    f.Add(Virtual<uint32_t>(other, 0x8));
    HashBrushTool(f, tool);
    f.Add(uint8_t{At<std::uintptr_t>(tool, 0) == addr::SC4TerraformTool_Vtable});
    f.Add(uint8_t{At<std::uintptr_t>(tool, field::kTarget) == addr::SC4TerraformTool_TargetVtable});
    const uint32_t made = f.h;

    // The tuning by address, with every value cleared first; then as it was.
    const uint8_t loadedBefore = Global<uint8_t>(addr::SC4TerraformTool_TuningLoaded);
    f.Add(loadedBefore);
    const BrushTuning kept = ReadBrushTuning();
    WriteBrushTuning(BrushTuning{});
    Cdecl<void>(addr::SC4TerraformTool_LoadTuning);
    const BrushTuning read = ReadBrushTuning();
    f.Add(read);
    WriteBrushTuning(kept);

    if (!GiveToView(view, tool)) {
        log::Write("autotest: tools5: the view did not take the terraform tool");
        return 0;
    }
    HashBrushTool(f, tool);
    const BrushTuning tuning = ReadBrushTuning();
    f.Add(tuning);
    const uint8_t loaded = Global<uint8_t>(addr::SC4TerraformTool_TuningLoaded);
    f.Add(loaded);
    f.Add(Cdecl<uint8_t>(addr::SC4TerraformTool_TuningReset));
    f.Add(Global<uint8_t>(addr::SC4TerraformTool_TuningLoaded));
    f.Add(Cdecl<uint8_t>(addr::SC4TerraformTool_TuningReset));
    f.Add(Global<uint8_t>(addr::SC4TerraformTool_TuningLoaded));
    Global<uint8_t>(addr::SC4TerraformTool_TuningLoaded) = loaded;
    const bool effects = At<void*>(tool, brush::kEffect) != nullptr;
    const uint32_t inited = f.h;

    const auto step = [&](uint8_t answer) {
        f.Add(answer);
        HashBrushTool(f, tool);
    };

    // The looks, the pointer's place and the modifiers by address.
    This<void>(addr::SC4TerraformTool_SetLooks, tool, uint32_t{1});
    HashBrushTool(f, tool);
    This<void>(addr::SC4TerraformTool_SetLooks, tool, uint32_t{0});
    HashBrushTool(f, tool);
    const float place[3] = {512.0f, 270.0f, 384.0f};
    This<void>(addr::SC4TerraformTool_UpdateCursor, tool, &place[0], uint32_t{1});
    HashBrushTool(f, tool);
    This<void>(addr::SC4TerraformTool_UpdateCursor, tool, &place[0], uint32_t{0});
    HashBrushTool(f, tool);
    for (uint32_t modifiers = 0; modifiers < 4; ++modifiers) {
        This<void>(addr::SC4TerraformTool_ApplyModifiers, tool, modifiers);
        HashBrushTool(f, tool);
    }
    const uint32_t direct = f.h;

    // The keys.
    for (uint32_t key = 0x70; key <= 0x79; ++key)
        step(KeyDown(tool, key, 1));
    for (uint32_t key = 0x30; key <= 0x39; ++key)
        step(KeyDown(tool, key, 3));
    step(KeyDown(tool, 0x75, 1));
    step(KeyDown(tool, 0x37, 1));
    const uint32_t chosenStrength = At<uint32_t>(tool, brush::kStrength);
    const uint32_t chosenRadius = At<uint32_t>(tool, brush::kRadius);
    step(KeyDown(tool, 0x41, 1));
    step(KeyDown(tool, 0x70, 0));
    step(KeyDown(tool, 0x7A, 1));
    for (const uint32_t key : {0x10u, 0x11u})
        for (uint32_t modifiers = 0; modifiers < 4; ++modifiers) {
            step(KeyDown(tool, key, modifiers));
            step(KeyUp(tool, key, 3 - modifiers));
        }
    step(KeyUp(tool, 0x41, 1));
    for (uint32_t modifiers = 0; modifiers < 4; ++modifiers) {
        This<void>(addr::SC4TerraformTool_ApplyModifiers, tool, modifiers);
        HashBrushTool(f, tool);
    }
    step(KeyDown(tool, 0x79, 1)); // the strength and the radius scaling back to 1
    step(KeyDown(tool, 0x30, 1));
    const uint32_t keys = f.h;

    // The count of applications: one more, and past the tuning's count.
    This<void>(addr::SC4TerraformTool_CountApplication, tool);
    HashBrushTool(f, tool);
    At<uint32_t>(tool, brush::kApplications) = tuning.perFlush;
    This<void>(addr::SC4TerraformTool_CountApplication, tool);
    HashBrushTool(f, tool);
    const uint32_t countedTo = At<uint32_t>(tool, brush::kApplications);
    // Nothing queued: nothing applied.
    This<void>(addr::SC4TerraformTool_Apply, tool, uint32_t{0});
    This<void>(addr::SC4TerraformTool_Apply, tool, uint32_t{1});
    HashBrushTool(f, tool);
    const uint32_t counted = f.h;

    // The ground under the grid's points, by address.
    int32_t pressed = 0, pointed = 0;
    ForGrid([&](Point p) {
        const uint8_t press = This<uint8_t>(addr::SC4TerraformTool_PressAltitude, tool, p.x, p.z);
        f.Add(press);
        f.Add(At<uint32_t>(tool, brush::kPressAltitude));
        float point[3] = {-1.0f, -2.0f, -3.0f};
        const uint8_t at = This<uint8_t>(addr::SC4TerraformTool_CursorPoint, tool, p.x, p.z, &point[0]);
        f.Add(at);
        f.Add(point);
        pressed += press;
        pointed += at;
    });
    const uint32_t picks = f.h;

    // The handlers with no scrub.
    ForGrid([&](Point p) { step(MouseMove(tool, p.x, p.z)); });
    const uint32_t moves = f.h;
    step(MouseMove(tool, kOff, kOff));
    const uint8_t offDown = MouseDownL(tool, kOff, kOff);
    step(offDown);
    const uint8_t offUp = MouseUpL(tool, kOff, kOff);
    step(offUp);
    step(Virtual<uint8_t>(tool, 0x58)); // OnMouseExit
    Message(tool, kMessageFrame, 0);
    HashBrushTool(f, tool);
    Message(tool, 0x12345678, 0);
    HashBrushTool(f, tool);

    // Two stretches queued; they are never applied.
    const float from[2] = {100.0f, 200.0f}, to[2] = {300.0f, 400.0f};
    This<void>(addr::SC4TerraformTool_Queue, tool, &from[0], &to[0]);
    This<void>(addr::SC4TerraformTool_Queue, tool, &to[0], &from[0]);
    Fnv unused;
    const int32_t queued = HashQueue(unused, tool);
    HashBrushTool(f, tool);
    const uint32_t idle = f.h;

    // The view puts the tool away.
    f.Add(Virtual<uint8_t>(view, 0x68, uint32_t{0}));
    const bool away = Virtual<void*>(view, 0x60) == nullptr;
    f.Add(uint8_t{away});
    HashBrushTool(f, tool);
    const uint32_t lastRelease = Virtual<uint32_t>(tool, 0x8);
    f.Add(lastRelease);
    const uint32_t mapAfter = MapState();
    const int64_t fundsAfter = Funds();
    f.Add(mapAfter);
    f.Add(uint8_t{fundsBefore == fundsAfter});
    log::Write("autotest: tools5: terraform %08X (brushes %08X; free brush %08X, one that costs %08X; made %08X; "
               "tuning loaded before %u, by address %u %08X %08X %08X %08X %08X, after Init %u %u %08X %08X %08X "
               "%08X %08X; inited %08X, effects %d; by address %08X; keys %08X, F6 and 7 chose %08X %08X)",
               f.h, brushes, *freeId, *paidId, made, loadedBefore, read.perFlush, read.bit1Strength, read.bit1Radius,
               read.bit0Strength, read.bit0Radius, read.rate, loaded, tuning.perFlush, tuning.bit1Strength,
               tuning.bit1Radius, tuning.bit0Strength, tuning.bit0Radius, tuning.rate, inited, effects, direct, keys,
               chosenStrength, chosenRadius);
    log::Write("autotest: tools5: terraform counted %08X (to %u at the tuning's count); picks %08X (%d pressed, %d "
               "pointed of the grid; %d terrain points); moves %08X; idle %08X (a press off the ground answered %u, "
               "the release %u; %d queued); away %d, last release %u; terrain %08X to %08X, funds %lld to %lld",
               counted, countedTo, picks, pressed, pointed, static_cast<int32_t>(ground.size()), moves, idle, offDown,
               offUp, queued, away, lastRelease, mapBefore, mapAfter, static_cast<long long>(fundsBefore),
               static_cast<long long>(fundsAfter));
    return f.h;
}

// 3. A scrub on the ground and a press ended by the view.
uint32_t DriveScrub(char* view, uint32_t id, uint32_t free, const std::vector<Ground>& ground)
{
    Fnv f;
    const char* const what = free ? "free" : "paid";
    if (ground.size() < 8 || !id) {
        log::Write("autotest: tools5: %s scrub: too few terrain points", what);
        return f.h;
    }
    const Ground& a = ground[ground.size() / 2];
    const Ground& b = ground[ground.size() / 2 + 1];
    const Ground& c = ground[ground.size() / 4];
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    const int64_t cost = Virtual<int64_t>(terrain, 0xF8, id);
    const int64_t fundsBefore = Funds();
    const uint32_t mapBefore = MapState();
    f.Add(cost);
    f.Add(fundsBefore);
    f.Add(mapBefore);
    char* const tool = NewBrushTool(id, free);
    if (!tool)
        return 0;
    if (!GiveToView(view, tool)) {
        log::Write("autotest: tools5: the view did not take the terraform tool");
        return 0;
    }
    uint32_t maps[6] = {};
    int32_t applied[4] = {};
    const auto step = [&](uint8_t answer, int stage) {
        f.Add(answer);
        HashBrushTool(f, tool);
        maps[stage] = MapState();
        f.Add(maps[stage]);
        f.Add(Funds());
    };
    // The press: the first application of the scrub.
    const uint8_t down = MouseDownL(tool, a.at.x, a.at.z);
    step(down, 0);
    const uint8_t scrubbing = At<uint8_t>(tool, brush::kScrubbing);
    std::memcpy(&applied[0], tool + brush::kApplied, sizeof applied);
    uint8_t moved = 0xEE, again = 0xEE, up = 0xEE;
    if (scrubbing) {
        // A move: an application, as the first (no time in its strength).
        At<uint8_t>(tool, brush::kFirst) = 1;
        moved = MouseMove(tool, b.at.x, b.at.z);
        step(moved, 1);
        // No second move before a message.
        At<uint8_t>(tool, brush::kFirst) = 1;
        again = MouseMove(tool, a.at.x, a.at.z);
        step(again, 2);
        // Three frame messages: the third queues the last point again.
        for (int i = 0; i < 3; ++i) {
            At<uint8_t>(tool, brush::kFirst) = 1;
            Message(tool, kMessageFrame, 0);
            step(0, 3);
        }
        step(Virtual<uint8_t>(tool, 0x58), 3); // OnMouseExit
        At<uint8_t>(tool, brush::kFirst) = 1;
        up = MouseUpL(tool, b.at.x, b.at.z);
        step(up, 4);
    }
    const int64_t spent = At<int64_t>(tool, brush::kSpent);
    const int64_t fundsAfterScrub = Funds();
    const uint32_t scrub = f.h;

    // A press at another point; the view puts the tool away in the scrub.
    const uint8_t second = MouseDownL(tool, c.at.x, c.at.z);
    step(second, 5);
    const uint8_t inScrub = At<uint8_t>(tool, brush::kScrubbing);
    const uint8_t saved = At<uint8_t>(tool, brush::kSaved);
    f.Add(Virtual<uint8_t>(view, 0x68, uint32_t{0}));
    const bool away = Virtual<void*>(view, 0x60) == nullptr;
    f.Add(uint8_t{away});
    HashBrushTool(f, tool);
    // Shutdown leaves the frame service and the terrain view's cell cursor to
    // OnMouseUpL: both done here, before the tool goes.
    if (inScrub) {
        if (void* const frames = Cdecl<void*>(addr::SC4_GetService86AD10EC))
            Virtual<uint8_t>(frames, 0x18, static_cast<void*>(tool + field::kTarget), kFrameCode);
        if (void* const terrainView = Virtual<void*>(terrain, 0x108))
            Virtual<uint8_t>(terrainView, 0x28, uint32_t{saved});
    }
    const uint32_t lastRelease = Virtual<uint32_t>(tool, 0x8);
    f.Add(lastRelease);
    const uint32_t mapAfter = MapState();
    const int64_t fundsAfter = Funds();
    f.Add(mapAfter);
    f.Add(fundsAfter);
    log::Write("autotest: tools5: %s scrub %08X (brush %08X, cost %lld; press at %d,%d answered %u, scrubbing %u, "
               "applied to %d,%d,%d,%d; move answered %u, a second %u, release %u; terrain %08X, after the press "
               "%08X, the move %08X, the second %08X, the messages %08X, the release %08X; spent %lld, funds %lld to "
               "%lld; scrub %08X; second press answered %u, scrubbing %u, terrain %08X; away %d, last release %u; "
               "terrain at the end %08X, funds %lld)",
               what, f.h, id, static_cast<long long>(cost), a.at.x, a.at.z, down, scrubbing, applied[0], applied[1],
               applied[2], applied[3], moved, again, up, mapBefore, maps[0], maps[1], maps[2], maps[3], maps[4],
               static_cast<long long>(spent), static_cast<long long>(fundsBefore),
               static_cast<long long>(fundsAfterScrub), scrub, second, inScrub, maps[5], away, lastRelease, mapAfter,
               static_cast<long long>(fundsAfter));
    return f.h;
}

// --- The obliterate tool -------------------------------------------------------------------

// Listens for the message that has the application write the city's file
// again: it must never come.
class SceneOverWatch final : public cIGZMessageTarget2 {
public:
    bool QueryInterface(uint32_t riid, void** object) override
    {
        if (riid == GZCLSID::kcIGZMessageTarget2 || riid == GZIID_cIGZUnknown) {
            *object = static_cast<cIGZMessageTarget2*>(this);
            AddRef();
            return true;
        }
        *object = nullptr;
        return false;
    }
    // Lives for the whole process.
    uint32_t AddRef() override { return ++refs_; }
    uint32_t Release() override { return refs_ > 0 ? --refs_ : 0; }

    bool DoMessage(cIGZMessage2*) override
    {
        ++g_sceneOver;
        log::Write("autotest: tools5: MESSAGE 0xEA65224E CAME: the application rewrites the city's file");
        return true;
    }

private:
    uint32_t refs_ = 0;
};
SceneOverWatch g_watch;
bool g_watching = false;

// How many blocks the iterator's list holds.
int32_t ListCount(char* tool)
{
    const char* const begin = At<const char*>(tool, wipe::kList);
    const char* const end = At<const char*>(tool, wipe::kList + 4);
    return begin && end >= begin ? static_cast<int32_t>((end - begin) / 2) : 0;
}

// Every field but the pointers (whether they are set).
void HashWipeTool(Fnv& f, char* tool)
{
    HashTool(f, tool);
    for (const std::size_t offset : {wipe::kFilter, wipe::kOccupants, wipe::kZones, wipe::kDemolition, wipe::kFinale})
        f.Add(uint8_t{At<void*>(tool, offset) != nullptr});
    f.Bytes(tool + wipe::kLastX, 0xC);       // the last cells, the state
    f.Bytes(tool + wipe::kBlocks, 0x18);     // the iterator: its bytes, rectangle and count
    f.Bytes(tool + wipe::kBlocks + 0x24, 0xC); // and where it is
    f.Add(ListCount(tool));
    f.Bytes(tool + wipe::kElapsed, 0x20);    // the times, the city's rectangle, blocks asked for
    f.Bytes(tool + wipe::kDemolished, 0xC);  // demolished, empty in a row, the situations' switch
}

// The blocks of the list: in the order the shuffle left them (`order`) and
// sorted (`blocks`).
void HashList(char* tool, uint32_t* blocks, uint32_t* order)
{
    const int32_t count = ListCount(tool);
    const uint16_t* const begin = At<const uint16_t*>(tool, wipe::kList);
    std::vector<uint16_t> cells(begin, begin + count);
    Fnv raw;
    raw.Bytes(cells.data(), cells.size() * sizeof(uint16_t));
    *order = raw.h;
    std::sort(cells.begin(), cells.end());
    Fnv sorted;
    sorted.Bytes(cells.data(), cells.size() * sizeof(uint16_t));
    *blocks = sorted.h;
}

struct WipeTuning {
    int32_t side;
    uint32_t finale, seconds;
};

WipeTuning ReadWipeTuning()
{
    return {Global<int32_t>(addr::SC4ObliterateTool_BlockSize), Global<uint32_t>(addr::SC4ObliterateTool_FinaleTime),
            Global<uint32_t>(addr::SC4ObliterateTool_SweepSeconds)};
}

void WriteWipeTuning(const WipeTuning& t)
{
    Global<int32_t>(addr::SC4ObliterateTool_BlockSize) = t.side;
    Global<uint32_t>(addr::SC4ObliterateTool_FinaleTime) = t.finale;
    Global<uint32_t>(addr::SC4ObliterateTool_SweepSeconds) = t.seconds;
}

// The curve of blocks over seconds at a few times.
uint32_t CurveHash()
{
    Fnv f;
    for (const float seconds : {0.0f, 0.25f, 0.5f, 1.0f, 2.0f, 3.5f, 5.0f, 8.0f, 12.0f, 60.0f})
        f.Add(This<float>(addr::SC4Curve_Evaluate, reinterpret_cast<void*>(addr::SC4ObliterateTool_SweepCurve),
                          seconds));
    return f.h;
}

// The state of the C runtime's rand() of this thread.
uint32_t& RandState()
{
    return At<uint32_t>(Cdecl<char*>(addr::CRT_getptd), 0x14);
}

// The type of the first occupant the filter lets through in a rectangle of
// cells (the occupant manager's slot 40, as the tool asks); 0 for none.
uint32_t FirstType(char* tool, const int32_t* rect)
{
    void* const occupants = At<void*>(tool, wipe::kOccupants);
    void** const sentinel = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    void* head = sentinel;
    int32_t low[2] = {rect[0], rect[1]}, high[2] = {rect[2], rect[3]};
    Virtual<uint8_t>(occupants, 0xA0, &head, &low[0], &high[0], At<void*>(tool, wipe::kFilter), uint32_t{1});
    uint32_t type = 0;
    if (sentinel[0] != sentinel)
        type = Virtual<uint32_t>(static_cast<void**>(sentinel[0])[2], 0x1C);
    Virtual<uint8_t>(occupants, 0xC0, &head);
    This<void>(addr::SC4List_Destroy2, &head);
    return type;
}

char* NewWipeTool()
{
    char* const tool = NewBlock(wipe::kSize);
    if (!tool || !This<void*>(addr::SC4ObliterateTool_Construct, tool))
        return nullptr;
    return tool;
}

uint32_t DriveWipe(char* view)
{
    Fnv f;
    f.Add(uint8_t{Virtual<void*>(view, 0x60) != nullptr});
    char* const tool = NewWipeTool();
    if (!tool)
        return 0;
    HashWipeTool(f, tool);
    f.Add(uint8_t{At<std::uintptr_t>(tool, 0) == addr::SC4ObliterateTool_Vtable});
    f.Add(uint8_t{At<std::uintptr_t>(tool, field::kTarget) == addr::SC4ObliterateTool_TargetVtable});
    const uint32_t made = f.h;

    // The tuning by address, with every value cleared first; then as it was.
    const uint8_t loadedBefore = Global<uint8_t>(addr::SC4ObliterateTool_TuningLoaded);
    f.Add(loadedBefore);
    const WipeTuning kept = ReadWipeTuning();
    WriteWipeTuning(WipeTuning{});
    Cdecl<void>(addr::SC4ObliterateTool_LoadTuning);
    const WipeTuning read = ReadWipeTuning();
    f.Add(read);
    const uint32_t curve = CurveHash();
    f.Add(curve);
    WriteWipeTuning(kept);

    // The filter's function (it does not read `this`).
    for (const uint32_t type : {0x74758926u, 0x69D25C5Fu, 0xA823821Eu, 0x278128A0u, 0u, 0x74758927u})
        f.Add(This<uint8_t>(addr::SC4ObliterateTool_FilterIncludesType, static_cast<void*>(nullptr), type));

    // Init: the shuffle draws from rand(); the state is put back.
    const uint32_t random = RandState();
    if (!GiveToView(view, tool)) {
        log::Write("autotest: tools5: the view did not take the obliterate tool");
        return 0;
    }
    const uint32_t drawn = RandState();
    RandState() = random;
    HashWipeTool(f, tool);
    uint32_t blocks = 0, order = 0;
    HashList(tool, &blocks, &order);
    f.Add(blocks);
    const WipeTuning tuning = ReadWipeTuning();
    f.Add(tuning);
    const uint8_t loaded = Global<uint8_t>(addr::SC4ObliterateTool_TuningLoaded);
    f.Add(loaded);
    f.Add(Cdecl<uint8_t>(addr::SC4ObliterateTool_TuningReset));
    f.Add(Global<uint8_t>(addr::SC4ObliterateTool_TuningLoaded));
    f.Add(Cdecl<uint8_t>(addr::SC4ObliterateTool_TuningReset));
    f.Add(Global<uint8_t>(addr::SC4ObliterateTool_TuningLoaded));
    Global<uint8_t>(addr::SC4ObliterateTool_TuningLoaded) = loaded;
    // The filter object: its vtables and its function through the slot.
    char* const filter = At<char*>(tool, wipe::kFilter);
    f.Add(uint8_t{At<std::uintptr_t>(filter, 0) == addr::SC4ObliterateTool_FilterVtable});
    f.Add(uint8_t{At<std::uintptr_t>(filter, 4) == addr::SC4ObliterateTool_FilterVtable4});
    for (const uint32_t type : {0x74758926u, 0x69D25C5Fu, 0xA823821Eu})
        f.Add(Virtual<uint8_t>(filter, 0x10, type));
    const uint32_t inited = f.h;

    // Which blocks have occupants the filter lets through: the tool's answer
    // against the test's own question of the occupant manager. Which blocks
    // they are is not hashed: in the game too a prop at the city's edge is
    // there in one run and not in the next.
    int32_t occupied = 0, all = 0, agreed = 0;
    std::string which;
    std::vector<std::pair<uint32_t, int32_t>> types;
    if (tuning.side > 0) {
        const int32_t lastX = At<int32_t>(tool, wipe::kLastX), lastZ = At<int32_t>(tool, wipe::kLastZ);
        for (int32_t z = 0; z <= lastZ / tuning.side; ++z)
            for (int32_t x = 0; x <= lastX / tuning.side; ++x) {
                int32_t rect[4] = {tuning.side * x, tuning.side * z, tuning.side * x + tuning.side - 1,
                                   tuning.side * z + tuning.side - 1};
                rect[2] = std::min(rect[2], lastX);
                rect[3] = std::min(rect[3], lastZ);
                const uint8_t has = This<uint8_t>(addr::SC4ObliterateTool_BlockHasOccupants, tool, &rect[0]);
                const uint32_t type = FirstType(tool, &rect[0]);
                agreed += has == (type != 0 ? 1 : 0);
                occupied += has;
                if (type) {
                    char text[8];
                    std::snprintf(text, sizeof text, " %X", static_cast<unsigned>(all));
                    which += text;
                    auto found =
                        std::find_if(types.begin(), types.end(), [&](const auto& t) { return t.first == type; });
                    if (found == types.end())
                        types.emplace_back(type, 1);
                    else
                        ++found->second;
                }
                ++all;
            }
    }
    f.Add(all);
    f.Add(agreed);

    // A key, the mouse slots (a shared stub), the frame message where it
    // demolishes nothing: in state 4 (nothing at all) and in state 0 (only
    // the time).
    f.Add(KeyDown(tool, 0x41, 0));
    f.Add(MouseDownL(tool, kOff, kOff));
    f.Add(MouseUpL(tool, kOff, kOff));
    f.Add(MouseMove(tool, kOff, kOff));
    HashWipeTool(f, tool);
    At<int32_t>(tool, wipe::kState) = 4;
    Message(tool, kMessageFrame, 1000);
    HashWipeTool(f, tool);
    At<int32_t>(tool, wipe::kState) = 0;
    Message(tool, kMessageFrame, 1000);
    Message(tool, 0x12345678, 1000);
    HashWipeTool(f, tool);
    const uint32_t counted = At<uint32_t>(tool, wipe::kElapsed);
    At<uint32_t>(tool, wipe::kElapsed) = 0;
    At<int32_t>(tool, wipe::kState) = 1;
    Cdecl<void>(addr::SC4ObliterateTool_Post, uint32_t{0x12345678}, uint32_t{1}, uint32_t{2}, uint32_t{3});

    // Escape: the tool has the view put it away.
    f.Add(KeyDown(tool, 0x1B, 0));
    const bool away = Virtual<void*>(view, 0x60) == nullptr;
    f.Add(uint8_t{away});
    HashWipeTool(f, tool);
    const uint32_t lastRelease = Virtual<uint32_t>(tool, 0x8);
    f.Add(lastRelease);
    std::sort(types.begin(), types.end());
    std::string kinds;
    for (const auto& t : types) {
        char text[24];
        std::snprintf(text, sizeof text, " %08X x %d", t.first, t.second);
        kinds += text;
    }
    log::Write("autotest: tools5: varies: obliterate: %d of %d blocks with occupants:%s", occupied, all, which.c_str());
    log::Write("autotest: tools5: varies: obliterate: the type of the first occupant in them:%s", kinds.c_str());
    log::Write("autotest: tools5: obliterate %08X (made %08X; tuning loaded before %u, by address: side %d, finale "
               "%u, sweep %08X, curve %08X; after Init %u: %d, %u, %08X; inited %08X: %d blocks %08X, their order "
               "%08X, rand %08X to %08X and put back; BlockHasOccupants as the occupant manager says for %d of %d "
               "blocks; state 0 counted %u; away %d, last release %u)",
               f.h, made, loadedBefore, read.side, read.finale, read.seconds, curve, loaded, tuning.side,
               tuning.finale, tuning.seconds, inited, ListCount(tool), blocks, order, random, drawn, agreed, all,
               counted, away, lastRelease);
    return f.h;
}

// The whole sweep and the wipe inside this hook; never past state 3.
uint32_t DriveWipeCity(char* view)
{
    Fnv f;
    char* const tool = NewWipeTool();
    if (!tool)
        return 0;
    if (!GiveToView(view, tool)) {
        log::Write("autotest: tools5: the view did not take the obliterate tool");
        return 0;
    }
    HashWipeTool(f, tool);
    uint32_t blocks = 0, order = 0;
    HashList(tool, &blocks, &order);
    f.Add(blocks);
    constexpr uint32_t kStep = 50000;
    int32_t ticks = 0;
    while (At<int32_t>(tool, wipe::kState) == 1 && ticks < 20000) {
        At<uint32_t>(tool, wipe::kElapsed) += kStep;
        This<void>(addr::SC4ObliterateTool_SweepTick, tool);
        ++ticks;
    }
    const int32_t afterSweep = At<int32_t>(tool, wipe::kState);
    const int32_t demolished = At<int32_t>(tool, wipe::kDemolished), askedFor = At<int32_t>(tool, wipe::kAsked);
    const int32_t emptyRun = At<int32_t>(tool, wipe::kEmptyRun);
    const uint8_t effect = At<void*>(tool, wipe::kFinale) != nullptr;
    f.Add(ticks);
    f.Add(afterSweep);
    f.Add(demolished);
    f.Add(askedFor);
    f.Add(effect);
    f.Add(At<uint32_t>(tool, wipe::kElapsed));
    int32_t afterFinale = -1;
    if (afterSweep == 2) {
        // Short of the tuning's time: nothing yet. Then the rest of it.
        const uint32_t time = Global<uint32_t>(addr::SC4ObliterateTool_FinaleTime);
        if (time > 1) {
            This<void>(addr::SC4ObliterateTool_FinaleTick, tool, time - 1);
            f.Add(At<int32_t>(tool, wipe::kState));
            This<void>(addr::SC4ObliterateTool_FinaleTick, tool, uint32_t{1});
        } else {
            This<void>(addr::SC4ObliterateTool_FinaleTick, tool, time);
        }
        afterFinale = At<int32_t>(tool, wipe::kState);
        f.Add(afterFinale);
        f.Bytes(tool + wipe::kElapsed, 0x20);
    }
    // The view puts the tool away: Shutdown takes it off the frame service
    // and the effect's message. State 4 and the reference kept: see the top.
    f.Add(Virtual<uint8_t>(view, 0x68, uint32_t{0}));
    const bool away = Virtual<void*>(view, 0x60) == nullptr;
    f.Add(uint8_t{away});
    f.Add(At<uint8_t>(tool, field::kInited));
    f.Add(uint8_t{At<void*>(tool, wipe::kFinale) != nullptr});
    f.Add(At<int32_t>(tool, wipe::kState));
    const int32_t atEnd = At<int32_t>(tool, wipe::kState);
    At<int32_t>(tool, wipe::kState) = 4;
    log::Write("autotest: tools5: obliterate city %08X (%d blocks %08X, their order %08X; %d ticks of %u microseconds: "
               "state %d, %d blocks asked for, %d demolished, finale effect %u; after the finale's time state %d; "
               "away %d, inited %u, state %d; the tool is kept, its state set to 4)",
               f.h, ListCount(tool), blocks, order, ticks, kStep, afterSweep, askedFor, demolished, effect,
               afterFinale, away, At<uint8_t>(tool, field::kInited), atEnd);
    log::Write("autotest: tools5: varies: obliterate city: %d blocks without occupants in a row at the sweep's end",
               emptyRun);
    return f.h;
}

void Drive(const Day& day)
{
    if (g_watching && g_ranOn >= 0 && day.day == g_ranOn + 1)
        log::Write("autotest: tools5: the day after: message 0xEA65224E came %d times", g_sceneOver);
    if (!OnRunDay(day, "OPENSC4_TEST_TOOLS5") || !game::IsBuild641())
        return;
    char* const view = CityView();
    if (!view || !day.city || !Global<void*>(addr::SC4_City) || !Global<void*>(addr::SC4_Terrain) ||
        !Global<void*>(addr::SC4_BudgetSimulator)) {
        log::Write("autotest: tools5: no city view");
        return;
    }
    const bool commit = EnvInt("OPENSC4_TEST_TOOLS5_COMMIT", 0) != 0;
    const bool obliterate = EnvInt("OPENSC4_TEST_TOOLS5_OBLITERATE", 0) != 0;
    const int parts = EnvInt("OPENSC4_TEST_TOOLS5_PARTS", 3); // 1 terraform, 2 obliterate
    g_ranOn = day.day;

    const std::vector<Ground> ground = GroundPoints(view, day.city);
    Fnv all;
    all.Add(static_cast<int32_t>(ground.size()));
    if (parts & 1) {
        uint32_t freeId = 0, paidId = 0;
        all.Add(DriveBrush(view, ground, &freeId, &paidId));
        if (commit) {
            all.Add(DriveScrub(view, freeId, 1, ground));
            all.Add(DriveScrub(view, paidId, 0, ground));
        }
    }
    if (parts & 2) {
        if (!g_watching) {
            cIGZMessageServer2Ptr server;
            g_watching = server && server->AddNotification(&g_watch, kMessageSceneOver);
            log::Write("autotest: tools5: listening for message 0xEA65224E: %d", g_watching);
        }
        all.Add(DriveWipe(view));
        if (obliterate)
            all.Add(DriveWipeCity(view));
    }
    std::snprintf(g_token, sizeof g_token, " tools5 %08X", all.h);
}

const DayHook kTools5{"tools5", 57, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
    if (g_sceneOver)
        line += " SCENEOVER";
}

const FingerprintPart kFingerprint{"tools5", 19, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
