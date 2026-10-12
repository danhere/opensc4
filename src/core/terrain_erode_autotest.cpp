// The terrain's erosion, flood and water distance map in the autotest
// (src/core/autotest_hooks.h), for simulators.terrain.erode and
// simulators.terrain.erode_normals (src/core/terrain_erode.cpp,
// terrain_erode_2.cpp). Everything is called the way the game's own
// callers do: the internal functions through their game addresses (so the
// detour runs when the group is on, the game's code when it is off) and the
// slots through the object's vtable as it is then.
//
// OPENSC4_TEST_TERRAIN_ERODE=<n>: on the n-th day of the run, in this order,
// with one log line after each (a checksum of every vertex's altitude, water
// depth, sediment and flags, the sea level, the height range and the water
// vertex count; the terrain's random generator, which none of this draws
// from, to show it; and a checksum of every vertex's normal):
// 0. the normals 0x00743B60 with the whole rectangle (+0x40), as
//    DoPostCityInit calls it, with a checksum of the two rows of triangle
//    normals it leaves (+0x118, +0x124: after the whole rectangle every
//    entry of both is of this call), and with a rectangle of 12 x 9
//    vertices a quarter into the map;
// 1. the erosion worker 0x00747B20 with (0, 0), then slot 68 with (1, 1,
//    the whole rectangle, 1), as DoMessage does for message 0x470BD214;
// 2. the same with kind 1 (the smoothing alone);
// 3. the same with kind 2 (nothing but the redisplay);
// 4. the same with (0, 1), the strong set the new-terrain set-up uses
//    (0x0074826A): its three numbers are the dwords at 0x00B4E1C0, which the
//    region screen's tuning fills; they are logged, and the call is left out
//    when they are not small (more than 8 runs, 200 iterations or 50
//    smoothing passes);
// 5. the water distance map 0x007440C0 with the whole rectangle, as the
//    timer's work calls it, with a checksum of the rows, the water vertex
//    count, +0xA8 and the gathered rectangle;
// 6. the normals with the whole rectangle once more.
// Slot 68 with a first argument of 1 runs the normals itself (0x007498CD),
// on the rectangle it is given grown by one.
// The erosion changes the land, so the "terrain" fingerprint part differs
// from an unedited run's afterwards.
//
// OPENSC4_TEST_TERRAIN_FLOOD=<n>: on the n-th day:
// 1. slot 56, StartFlood (which runs the first step itself);
// 2. the step 0x007467F0 40 times, a line after each (the same checksum,
//    the window, the counter, the flooding byte and a checksum of the saved
//    depths);
// 3. the counter +0x1AC set to 351 by the test and 10 more steps: from 351
//    on a step lets the water sink towards the saved depths, which would
//    otherwise take 350 steps to reach;
// 4. the counter set to 501 and one more step: the game's own end of the
//    flood (the step that finds the counter above 500 clears +0xD4, puts
//    every water depth back and returns);
// 5. the water distance map as above.
// With OPENSC4_TEST_TERRAIN_FLOOD_FULL=1 steps 2 to 4 are instead the step
// called until +0xD4 clears by itself (501 more calls), a line every 25.
//
// Why the test ends the flood. In the game the timer message steps the
// flood, on the timer service's time: left running, the flood would go on
// for 500 steps at moments that differ between two runs, and every later
// day's water with them. The day hook runs inside the simulator's tick, so
// no timer message comes between the test's calls; when it returns +0xD4 is
// clear again and the water depths are the saved ones, in both runs alike.
// Should +0xD4 still be set after step 4 (it cannot be, by the code read),
// the test says so and clears the byte itself.
//
// What the test cannot do safely: nothing checks the map against the
// flood's fixed window (vertices 20 to 40), so the test does not start a
// flood on a map of fewer than 41 vertices a side; the brushes' erosion
// layer (the other caller of 0x00746AD0) is the brush test's.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/terrain_sim.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>

namespace opensc4::autotest {
namespace {

using terrain_sim::AtAddress;
using terrain_sim::RecordAddress;
namespace field = terrain_sim::field;
namespace vertex = terrain_sim::vertex;
namespace water = terrain_sim::water;

// Fields terrain_sim.h does not have yet (see terrain_erode.cpp).
constexpr std::size_t kElapsed = 0xA8, kChanged = 0xAC, kFlooding = 0xD4, kSavedDepths = 0xE0, kFaceRowA = 0x118,
                      kFaceRowB = 0x124, kRows = 0x190, kFloodRect = 0x19C, kFloodSteps = 0x1AC;

// Slot numbers of the terrain's vtable (0x00AB3D18).
enum : int {
    kStartFlood = 56,
    kRedisplay = 68,
};

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

struct Gathered {
    Hash hash;
    uint32_t calls = 0;
    bool ran = false;
};
Gathered g_erode, g_flood;

// A slot of the object's vtable as it is now.
template <typename R, typename... Args>
R Call(char* terrain, int slot, Args... args)
{
    using Fn = R(__thiscall*)(void*, Args...);
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(terrain))[slot])(terrain, args...);
}

// A function of the class at its game address (detoured or not).
template <typename R, typename... Args>
R At641(std::uintptr_t function, char* terrain, Args... args)
{
    using Fn = R(__thiscall*)(void*, Args...);
    return reinterpret_cast<Fn>(function)(terrain, args...);
}

uint32_t U32(char* terrain, std::size_t offset) { return At<uint32_t>(terrain, offset); }

uint32_t VertexAt(char* terrain, uint32_t index) { return RecordAddress(terrain_sim::VertexBase(terrain), index); }

uint32_t WaterAt(char* terrain, uint32_t index) { return RecordAddress(terrain_sim::WaterBase(terrain), index); }

// The state of every vertex (altitude, water depth, sediment, flags), the
// sea level, the height range and the water vertex count.
uint32_t MapState(char* terrain)
{
    Hash hash;
    const uint32_t count = U32(terrain, field::kVertexCount);
    for (uint32_t index = 0; index < count; ++index) {
        hash.Mix(AtAddress<uint32_t>(VertexAt(terrain, index)));
        hash.Mix(AtAddress<uint32_t>(WaterAt(terrain, index) + water::kDepth));
        hash.Mix(AtAddress<uint32_t>(WaterAt(terrain, index) + water::kSediment));
        hash.Mix(AtAddress<uint16_t>(VertexAt(terrain, index) + vertex::kFlags));
    }
    hash.Mix(U32(terrain, field::kSeaLevel));
    hash.Mix(U32(terrain, field::kMinHeight));
    hash.Mix(U32(terrain, field::kMaxHeight));
    hash.Mix(U32(terrain, field::kWaterVertices));
    return hash.h;
}

// Every vertex's normal (+4 of the record, three floats).
uint32_t NormalsState(char* terrain)
{
    Hash hash;
    const uint32_t count = U32(terrain, field::kVertexCount);
    for (uint32_t index = 0; index < count; ++index)
        for (uint32_t i = 0; i < 3; ++i)
            hash.Mix(AtAddress<uint32_t>(VertexAt(terrain, index) + vertex::kNormal + 4u * i));
    return hash.h;
}

// The two rows of triangle normals 0x00743B60 works in (vectors of 12-byte
// entries): every dword of both.
uint32_t FacesState(char* terrain)
{
    Hash hash;
    for (const std::size_t row : {kFaceRowA, kFaceRowB}) {
        const uint32_t begin = U32(terrain, row), end = U32(terrain, row + 4);
        const uint32_t dwords = begin && end >= begin ? (end - begin) / 4u : 0;
        hash.Mix(dwords);
        for (uint32_t i = 0; i < dwords; ++i)
            hash.Mix(AtAddress<uint32_t>(begin + 4u * i));
    }
    return hash.h;
}

// The water depths StartFlood saved (+0xE0), as dwords.
uint32_t SavedState(char* terrain)
{
    Hash hash;
    const uint32_t saved = U32(terrain, kSavedDepths);
    if (!saved)
        return 0;
    const uint32_t count = U32(terrain, field::kVertexCount);
    for (uint32_t index = 0; index < count; ++index)
        hash.Mix(AtAddress<uint32_t>(saved + 4u * index));
    return hash.h;
}

// The water distance map: every byte of every row (+0x190: a vector of
// 12-byte vectors), with the rows' count and lengths.
uint32_t RowsState(char* terrain, uint32_t& rows)
{
    Hash hash;
    const uint32_t begin = U32(terrain, kRows), end = U32(terrain, kRows + 4);
    rows = begin && end >= begin ? (end - begin) / 0xCu : 0;
    for (uint32_t row = 0; row < rows; ++row) {
        const uint32_t first = AtAddress<uint32_t>(begin + 0xCu * row);
        const uint32_t last = AtAddress<uint32_t>(begin + 0xCu * row + 4u);
        const uint32_t length = first && last >= first ? last - first : 0;
        hash.Mix(length);
        for (uint32_t i = 0; i < length; ++i)
            hash.h = (hash.h ^ AtAddress<uint8_t>(first + i)) * 16777619u;
    }
    return hash.h;
}

// The state of the terrain's random generator (an LCG: one dword).
uint32_t RandomState() { return Global<uint32_t>(addr::cSTETerrain_Random); }

char* LiveTerrain()
{
    char* const terrain = game::IsBuild641() ? Global<char*>(addr::SC4_Terrain) : nullptr;
    if (!terrain || !At<uint8_t>(terrain, field::kInitialized) || !U32(terrain, field::kVertices) ||
        !At<void*>(terrain, field::kWater) || !At<uint32_t>(At<void*>(terrain, field::kWater), water::kRecords))
        return nullptr;
    return terrain;
}

// 0x00741660: the whole rectangle as DoMessage takes it (0, 0, cells x,
// cells z).
void WholeRect(char* terrain, int32_t* rect)
{
    rect[1] = 0;
    rect[0] = 0;
    rect[2] = static_cast<int32_t>(U32(terrain, field::kCellsX));
    rect[3] = static_cast<int32_t>(U32(terrain, field::kCellsZ));
}

void NoteErode(const char* what, char* terrain)
{
    Gathered& g = g_erode;
    const uint32_t state = MapState(terrain);
    const uint32_t normals = NormalsState(terrain);
    const uint32_t random = RandomState();
    g.hash.Mix(state);
    g.hash.Mix(normals);
    g.hash.Mix(random);
    ++g.calls;
    log::Write("terrain erode test: %s: state %08X, normals %08X, min %08X, max %08X, water vertices %u, random %08X",
               what, state, normals, U32(terrain, field::kMinHeight), U32(terrain, field::kMaxHeight),
               U32(terrain, field::kWaterVertices), random);
}

// The normals of a rectangle at the game's address (0x007495F9).
void Normals(char* terrain, int32_t* rect, bool whole, const char* what)
{
    At641<void>(addr::cSTETerrain_PrepareRect, terrain, static_cast<void*>(rect));
    if (whole) {
        const uint32_t faces = FacesState(terrain);
        g_erode.hash.Mix(faces);
        log::Write("terrain erode test: %s: triangle rows %08X", what, faces);
    }
    NoteErode(what, terrain);
}

// The distance map refilled over the whole rectangle (0x0074A459) and what
// it leaves.
void Distances(const char* test, Gathered& g, char* terrain)
{
    At641<void>(addr::cSTETerrain_UpdateWaterDistances, terrain, static_cast<void*>(terrain + field::kCellRect));
    uint32_t rows = 0;
    const uint32_t checksum = RowsState(terrain, rows);
    g.hash.Mix(checksum);
    g.hash.Mix(rows);
    g.hash.Mix(U32(terrain, field::kWaterVertices));
    g.hash.Mix(U32(terrain, kElapsed));
    for (int i = 0; i < 4; ++i)
        g.hash.Mix(U32(terrain, kChanged + 4u * static_cast<unsigned>(i)));
    ++g.calls;
    log::Write("terrain %s test: UpdateWaterDistances(whole): rows %u, checksum %08X, water vertices %u, elapsed %u, "
               "gathered %d,%d,%d,%d",
               test, rows, checksum, U32(terrain, field::kWaterVertices), U32(terrain, kElapsed),
               At<int32_t>(terrain, kChanged), At<int32_t>(terrain, kChanged + 4), At<int32_t>(terrain, kChanged + 8),
               At<int32_t>(terrain, kChanged + 12));
}

void ErodeAs(char* terrain, uint32_t kind, uint32_t strong, const char* what)
{
    At641<void>(addr::cSTETerrain_Erode, terrain, kind, strong);
    int32_t rect[4];
    WholeRect(terrain, rect);
    Call<void>(terrain, kRedisplay, 1u, 1u, static_cast<void*>(rect), 1u);
    NoteErode(what, terrain);
}

void Erosion(char* terrain)
{
    Gathered& g = g_erode;
    g.ran = true;
    NoteErode("before", terrain);
    Normals(terrain, &At<int32_t>(terrain, field::kCellRect), true, "PrepareRect(whole)");
    int32_t part[4] = {static_cast<int32_t>(U32(terrain, field::kVertsX) / 4),
                       static_cast<int32_t>(U32(terrain, field::kVertsZ) / 4), 0, 0};
    part[2] = part[0] + 11;
    part[3] = part[1] + 8;
    Normals(terrain, part, false, "PrepareRect(12 x 9 vertices)");
    ErodeAs(terrain, 0, 0, "Erode(0, 0) and redisplay");
    ErodeAs(terrain, 1, 0, "Erode(1, 0) and redisplay");
    ErodeAs(terrain, 2, 0, "Erode(2, 0) and redisplay");
    const uint32_t smooth = Global<uint32_t>(addr::SC4RegionScreen_Tuning4E1C0);
    const uint32_t iterations = Global<uint32_t>(addr::SC4RegionScreen_Tuning4E1C0 + 4);
    const uint32_t runs = Global<uint32_t>(addr::SC4RegionScreen_Tuning4E1C0 + 8);
    log::Write("terrain erode test: the strong set: %u runs of %u iterations, %u smoothing passes", runs, iterations,
               smooth);
    g.hash.Mix(runs);
    g.hash.Mix(iterations);
    g.hash.Mix(smooth);
    if (runs <= 8 && iterations <= 200 && smooth <= 50)
        ErodeAs(terrain, 0, 1, "Erode(0, 1) and redisplay");
    else
        log::Write("terrain erode test: the strong set is not small; Erode(0, 1) left out");
    Distances("erode", g, terrain);
    Normals(terrain, &At<int32_t>(terrain, field::kCellRect), true, "PrepareRect(whole) at the end");
    log::Write("terrain erode test: %u lines noted, checksum %08X", g.calls, g.hash.h);
}

void NoteFlood(const char* what, int step, char* terrain)
{
    Gathered& g = g_flood;
    const uint32_t state = MapState(terrain);
    const uint32_t saved = SavedState(terrain);
    g.hash.Mix(state);
    g.hash.Mix(saved);
    for (int i = 0; i < 4; ++i)
        g.hash.Mix(U32(terrain, kFloodRect + 4u * static_cast<unsigned>(i)));
    g.hash.Mix(U32(terrain, kFloodSteps));
    g.hash.Mix(At<uint8_t>(terrain, kFlooding));
    g.hash.Mix(RandomState());
    ++g.calls;
    log::Write("terrain flood test: %s %d: state %08X, saved %08X, window %d,%d,%d,%d, counter %d, flooding %u, "
               "random %08X",
               what, step, state, saved, At<int32_t>(terrain, kFloodRect), At<int32_t>(terrain, kFloodRect + 4),
               At<int32_t>(terrain, kFloodRect + 8), At<int32_t>(terrain, kFloodRect + 12),
               At<int32_t>(terrain, kFloodSteps), static_cast<unsigned>(At<uint8_t>(terrain, kFlooding)), RandomState());
}

void Step(char* terrain) { At641<void>(addr::cSTETerrain_FloodStep, terrain); }

void Flood(char* terrain)
{
    Gathered& g = g_flood;
    g.ran = true;
    if (U32(terrain, field::kVertsX) < 41 || U32(terrain, field::kVertsZ) < 41 || !U32(terrain, kSavedDepths)) {
        log::Write("terrain flood test: the map is smaller than the flood's window, or +0xE0 is not there; nothing done");
        return;
    }
    if (At<uint8_t>(terrain, kFlooding)) {
        log::Write("terrain flood test: a flood is running already; nothing done");
        return;
    }
    NoteFlood("before", 0, terrain);
    Call<void>(terrain, kStartFlood);
    NoteFlood("StartFlood", 0, terrain);
    if (EnvInt("OPENSC4_TEST_TERRAIN_FLOOD_FULL", 0) != 0) {
        int step = 0;
        while (At<uint8_t>(terrain, kFlooding) && step < 600) {
            Step(terrain);
            ++step;
            if (step % 25 == 0 || !At<uint8_t>(terrain, kFlooding))
                NoteFlood("step", step, terrain);
        }
    } else {
        for (int step = 1; step <= 40 && At<uint8_t>(terrain, kFlooding); ++step) {
            Step(terrain);
            NoteFlood("step", step, terrain);
        }
        At<int32_t>(terrain, kFloodSteps) = 351;
        for (int step = 1; step <= 10 && At<uint8_t>(terrain, kFlooding); ++step) {
            Step(terrain);
            NoteFlood("sinking step", step, terrain);
        }
        At<int32_t>(terrain, kFloodSteps) = 501;
        if (At<uint8_t>(terrain, kFlooding))
            Step(terrain);
        NoteFlood("last step", 0, terrain);
    }
    if (At<uint8_t>(terrain, kFlooding)) {
        log::Write("terrain flood test: +0xD4 is still set after the flood's end; cleared by the test");
        At<uint8_t>(terrain, kFlooding) = 0;
        g.hash.Mix(0xBADF100Du);
    }
    Distances("flood", g, terrain);
    log::Write("terrain flood test: %u lines noted, checksum %08X", g.calls, g.hash.h);
}

void Day_(const Day& day)
{
    const bool erode = OnRunDay(day, "OPENSC4_TEST_TERRAIN_ERODE");
    const bool flood = OnRunDay(day, "OPENSC4_TEST_TERRAIN_FLOOD");
    if (!erode && !flood)
        return;
    char* const terrain = LiveTerrain();
    if (!terrain) {
        log::Write("terrain erode test: no terrain; nothing done");
        return;
    }
    if (U32(terrain, field::kVertsX) != U32(terrain, field::kCellsX) + 1 ||
        U32(terrain, field::kVertsZ) != U32(terrain, field::kCellsZ) + 1) {
        log::Write("terrain erode test: unexpected sizes; nothing done");
        return;
    }
    if (erode) {
        if (U32(terrain, field::kVertsX) < 64 || U32(terrain, field::kVertsZ) < 64)
            log::Write("terrain erode test: the map is too small; nothing done");
        else
            Erosion(terrain);
    }
    if (flood)
        Flood(terrain);
}

// With either switch set: e<checksum>/<lines> once the erosion test ran,
// f<checksum>/<lines> and the flood's counter (s) once the flood test ran,
// and the flooding byte (F). The rows and +0xA8 are in the "terrainobj" part
// (they follow the timer).
void Fingerprint(cISC4City*, std::string& line)
{
    if (EnvInt("OPENSC4_TEST_TERRAIN_ERODE", -1) < 0 && EnvInt("OPENSC4_TEST_TERRAIN_FLOOD", -1) < 0)
        return;
    char* const terrain = LiveTerrain();
    if (!terrain) {
        line += " | terrainerode none";
        return;
    }
    char text[96];
    line += " | terrainerode";
    if (g_erode.ran) {
        std::snprintf(text, sizeof text, " e%08X/%u", g_erode.hash.h, g_erode.calls);
        line += text;
    }
    if (g_flood.ran) {
        std::snprintf(text, sizeof text, " f%08X/%u s%d", g_flood.hash.h, g_flood.calls,
                      At<int32_t>(terrain, kFloodSteps));
        line += text;
    }
    std::snprintf(text, sizeof text, " F%u", static_cast<unsigned>(At<uint8_t>(terrain, kFlooding)));
    line += text;
}

const DayHook kErode{"terrainerode", 117, &Day_};
const FingerprintPart kFingerprint{"terrainerode", 118, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
