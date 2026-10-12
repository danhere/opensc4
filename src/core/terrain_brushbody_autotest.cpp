// The brush body's part of the autotest (src/core/autotest_hooks.h): a day
// hook that applies, through the terrain's slot 57 (ApplyBrush), the brushes
// the existing brush test (terrain_brush_autotest.cpp) does not reach. That
// test takes its brushes from the terrain's slot 58, whose seven types are
// brushes with layers of kinds 0, 1, 2 and 5 only (one type has no exemplar
// in the game's packages). The game's packages hold 30 brush exemplars
// (type 0x6534284A, group 0x6A01FC2A); the ones applied here, by exemplar
// instance, with their layer kinds:
//
//   set 1 (land):   0x6A4BAAFA ErosionBrush        6 (no picture), 2
//                   0x6A33E1BA GodMountainDefault  0, 0 (the second follows the stroke)
//                   0xEA393C05 PlainsBrush         2, 5
//                   0x8A33E17A CraterBrush         0 (neutral 0x80: raises and lowers)
//                   0x00000014 PipeSinkHole        0 (neutral 0xFF: lowers only)
//                   0xCA835E0E SuperLevelBrush     5
//   set 2 (water):  0xEB70C719 GodValleyLake       8, 9
//                   0xCB8D4CA7 GodValleyRain       9, 4
//                   0x4B9539D1 GodValleySpring     10 (no picture)
//   set 4 (effect): 0xEA353064 Animals             4
// No shipped brush has a layer of kind 3 or 7, and every layer with a
// per-vertex routine has a picture, so the walker without a picture
// (0x00742440) and those two kinds are not reached by any test.
//
// OPENSC4_TEST_TERRAIN_BRUSHBODY=<n>: on the n-th day of the run, the sets
// OPENSC4_TEST_TERRAIN_BRUSHBODY_SET names (a sum of 1, 2, 4; 1 when not
// set): for each brush BeginBrushScrub, then at an inland and a shore place
// two strengths, three applications each along a short line, then
// EndBrushScrub, as the existing test does it (see there for how the
// terraform tool makes these calls). The first application of each brush is
// made with the step given (the parameters' +0x10 = 4), the others with -1
// (the terrain's own step). One log line per application with its answer,
// the applied rectangle, a checksum of the whole terrain (altitudes, both
// water floats, sediment, flags, the scrub set's size), the random
// generator's state, the brush's turn and direction (the floats +0x68 and
// +0x6C of its record, which FPATAN and the turn's arithmetic produce), the
// flora type count and the number of pending rectangles.
//
// Sets 2 and 4 are not on by default: the water kinds hand over to the
// surface water object (whether what it starts repeats between two runs was
// not established: nothing here was run in the game), and kind 4 starts
// effects, which live on frame time.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/terrain_sim.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace opensc4::autotest {
namespace {

using terrain_sim::AtAddress;
using terrain_sim::Bits;
using terrain_sim::RecordAddress;
namespace field = terrain_sim::field;
namespace vertex = terrain_sim::vertex;
namespace water = terrain_sim::water;

// Fields terrain_sim.h does not have yet (see terrain_brushbody.cpp).
constexpr std::size_t kFloraTypeCount = 0x108;
constexpr std::size_t kPendingRects = 0x1B0;
constexpr std::size_t kScrubbedCount = 0x1D8;

// Slot numbers of the terrain's vtable (0x00AB3D18).
enum : int {
    kApplyBrush = 57,
    kBeginBrushScrub = 59,
    kEndBrushScrub = 60,
};

// tBrushParameters (gzcom-dll cISTETerrain.h), 0x38 bytes.
struct BrushParameters {
    float x0, z0, x1, z1; // the previous and the current position, metres
    int32_t zoom;         // below 0: the terrain's own (+0xBC)
    uint8_t redisplay;
    uint8_t pad[3];
    float strength;
    float radiusScaling;
    uint32_t messageData;
    int32_t applied[4];
    float altitude;
};
static_assert(sizeof(BrushParameters) == 0x38);
static_assert(offsetof(BrushParameters, strength) == 0x18);
static_assert(offsetof(BrushParameters, applied) == 0x24);

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
Gathered g_body;

template <typename R, typename... Args>
R Call(char* terrain, int slot, Args... args)
{
    using Fn = R(__thiscall*)(void*, Args...);
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(terrain))[slot])(terrain, args...);
}

uint32_t U32(char* terrain, std::size_t offset) { return At<uint32_t>(terrain, offset); }

uint32_t Index(char* terrain, uint32_t x, uint32_t z) { return U32(terrain, field::kVertsX) * z + x; }

uint32_t VertexAt(char* terrain, uint32_t index) { return RecordAddress(terrain_sim::VertexBase(terrain), index); }

uint32_t WaterAt(char* terrain, uint32_t index) { return RecordAddress(terrain_sim::WaterBase(terrain), index); }

float Altitude(char* terrain, uint32_t index) { return AtAddress<float>(VertexAt(terrain, index)); }

uint32_t Flags(char* terrain, uint32_t index) { return AtAddress<uint16_t>(VertexAt(terrain, index) + vertex::kFlags); }

bool Wet(char* terrain, uint32_t index) { return AtAddress<float>(WaterAt(terrain, index) + water::kDepth) > 0.0f; }

// The state of every vertex (altitude, water depth, the water record's
// second float, which layer kind 10 changes, sediment, flags), the sea
// level, the height range and the size of the scrub set.
uint32_t MapState(char* terrain)
{
    Hash hash;
    const uint32_t count = U32(terrain, field::kVertexCount);
    for (uint32_t index = 0; index < count; ++index) {
        hash.Mix(AtAddress<uint32_t>(VertexAt(terrain, index)));
        hash.Mix(AtAddress<uint32_t>(WaterAt(terrain, index) + water::kDepth));
        hash.Mix(AtAddress<uint32_t>(WaterAt(terrain, index) + 0x4));
        hash.Mix(AtAddress<uint32_t>(WaterAt(terrain, index) + water::kSediment));
        hash.Mix(Flags(terrain, index));
    }
    hash.Mix(U32(terrain, field::kSeaLevel));
    hash.Mix(U32(terrain, field::kMinHeight));
    hash.Mix(U32(terrain, field::kMaxHeight));
    hash.Mix(U32(terrain, kScrubbedCount));
    return hash.h;
}

uint32_t RandomState() { return Global<uint32_t>(addr::cSTETerrain_Random); }

// The records of the list +0x1B0 (layer kind 7 adds to it; the timer's work
// takes from it).
uint32_t PendingRects(char* terrain)
{
    const uint32_t head = U32(terrain, kPendingRects);
    if (!head)
        return 0;
    uint32_t count = 0;
    for (uint32_t node = AtAddress<uint32_t>(head); node != head && count < 100000; node = AtAddress<uint32_t>(node))
        ++count;
    return count;
}

// A brush's record in the map 0x00B4BD58 (null for an exemplar the map does
// not have), found as slot 57 finds it.
char* BrushRecord(uint32_t id)
{
    using FindFn = void(__thiscall*)(void*, char**, const uint32_t*);
    char* node = nullptr;
    reinterpret_cast<FindFn>(addr::SC4Uint32Map_Find)(reinterpret_cast<void*>(addr::cSTETerrain_Brushes), &node, &id);
    if (node == Global<char*>(addr::cSTETerrain_Brushes))
        return nullptr;
    return node + 0x14;
}

void Note(const char* what, uint32_t answer, const int32_t* applied, char* terrain, uint32_t id)
{
    Gathered& g = g_body;
    const uint32_t state = MapState(terrain);
    const uint32_t random = RandomState();
    char* const record = BrushRecord(id);
    const uint32_t turn = record ? At<uint32_t>(record, 0x68) : 0;
    const uint32_t direction = record ? At<uint32_t>(record, 0x6C) : 0;
    const uint32_t flora = U32(terrain, kFloraTypeCount);
    const uint32_t pending = PendingRects(terrain);
    g.hash.Mix(answer);
    g.hash.Mix(state);
    g.hash.Mix(random);
    g.hash.Mix(turn);
    g.hash.Mix(direction);
    g.hash.Mix(flora);
    g.hash.Mix(pending);
    int32_t none[4] = {0, 0, 0, 0};
    if (!applied)
        applied = none;
    for (int i = 0; i < 4; ++i)
        g.hash.Mix(static_cast<uint32_t>(applied[i]));
    ++g.calls;
    log::Write("terrain brushbody test: %s: answer %08X, applied %d,%d,%d,%d, state %08X, random %08X, turn %08X, "
               "direction %08X, flora types %u, pending %u",
               what, answer, applied[0], applied[1], applied[2], applied[3], state, random, turn, direction, flora, pending);
}

char* LiveTerrain()
{
    char* const terrain = game::IsBuild641() ? Global<char*>(addr::SC4_Terrain) : nullptr;
    if (!terrain || !At<uint8_t>(terrain, field::kInitialized) || !U32(terrain, field::kVertices) ||
        !At<void*>(terrain, field::kWater) || !At<uint32_t>(At<void*>(terrain, field::kWater), water::kRecords))
        return nullptr;
    return terrain;
}

constexpr uint32_t kMargin = 40; // vertices kept between a place and every edge
constexpr uint32_t kClear = 6;   // the half width of the square a place is judged by

struct Place {
    const char* name;
    bool found;
    uint32_t x, z;
};

// The first vertex (rows first), kMargin or more from every edge, whose
// square of kClear around it is unlocked and all dry (wanted 0), or dry at
// the vertex with water somewhere in the square (wanted 2).
bool FindPlace(char* terrain, int wanted, uint32_t& px, uint32_t& pz)
{
    const uint32_t vertsX = U32(terrain, field::kVertsX), vertsZ = U32(terrain, field::kVertsZ);
    for (uint32_t z = kMargin; z + kMargin < vertsZ; ++z) {
        for (uint32_t x = kMargin; x + kMargin < vertsX; ++x) {
            if (wanted == 2 && Wet(terrain, Index(terrain, x, z)))
                continue;
            bool locked = false;
            uint32_t wet = 0, all = 0;
            for (uint32_t zz = z - kClear; zz <= z + kClear && !locked; ++zz)
                for (uint32_t xx = x - kClear; xx <= x + kClear; ++xx) {
                    const uint32_t index = Index(terrain, xx, zz);
                    if (Flags(terrain, index) & 0x4000) {
                        locked = true;
                        break;
                    }
                    ++all;
                    if (Wet(terrain, index))
                        ++wet;
                }
            if (locked)
                continue;
            if ((wanted == 0 && wet == 0) || (wanted == 2 && wet != 0 && wet * 4 < all)) {
                px = x;
                pz = z;
                return true;
            }
        }
    }
    return false;
}

struct Brush {
    uint32_t id;
    const char* name;
    int set;
};

const Brush kBrushes[] = {
    {0x6A4BAAFAu, "ErosionBrush", 1},  {0x6A33E1BAu, "GodMountainDefault", 1}, {0xEA393C05u, "PlainsBrush", 1},
    {0x8A33E17Au, "CraterBrush", 1},   {0x00000014u, "PipeSinkHole", 1},       {0xCA835E0Eu, "SuperLevelBrush", 1},
    {0xEB70C719u, "GodValleyLake", 2}, {0xCB8D4CA7u, "GodValleyRain", 2},      {0x4B9539D1u, "GodValleySpring", 2},
    {0xEA353064u, "Animals", 4},
};

void ApplyAt(char* terrain, const Brush& brush, const Place& place, float strength, bool& first)
{
    const float altitude = Altitude(terrain, Index(terrain, place.x, place.z));
    float previousX = static_cast<float>(place.x) * 16.0f;
    float previousZ = static_cast<float>(place.z) * 16.0f;
    for (int step = 0; step < 3; ++step) {
        BrushParameters parameters{};
        parameters.x0 = previousX;
        parameters.z0 = previousZ;
        parameters.x1 = static_cast<float>(place.x) * 16.0f + static_cast<float>(step) * 8.0f;
        parameters.z1 = static_cast<float>(place.z) * 16.0f - static_cast<float>(step) * 12.0f;
        parameters.zoom = first ? 4 : -1;
        first = false;
        parameters.redisplay = 1;
        parameters.strength = strength;
        parameters.radiusScaling = 1.0f;
        parameters.messageData = 1;
        parameters.altitude = altitude;
        const uint32_t answer = Call<uint32_t>(terrain, kApplyBrush, brush.id, &parameters) & 0xFF;
        char what[112];
        std::snprintf(what, sizeof what, "ApplyBrush(%s, %s, strength %08X, step %d)", brush.name, place.name,
                      Bits(strength), step);
        Note(what, answer, parameters.applied, terrain, brush.id);
        previousX = parameters.x1;
        previousZ = parameters.z1;
    }
}

void Brushes(char* terrain)
{
    Gathered& g = g_body;
    g.ran = true;
    const int sets = EnvInt("OPENSC4_TEST_TERRAIN_BRUSHBODY_SET", 1);
    const uint32_t cellsX = U32(terrain, field::kCellsX), cellsZ = U32(terrain, field::kCellsZ);
    const uint32_t vertsX = U32(terrain, field::kVertsX), vertsZ = U32(terrain, field::kVertsZ);
    if (vertsX < 3 * kMargin || vertsZ < 3 * kMargin || vertsX != cellsX + 1 || vertsZ != cellsZ + 1) {
        log::Write("terrain brushbody test: unexpected sizes (%u x %u cells); nothing done", cellsX, cellsZ);
        return;
    }
    Place places[2] = {{"inland", false, 0, 0}, {"shore", false, 0, 0}};
    places[0].found = FindPlace(terrain, 0, places[0].x, places[0].z);
    places[1].found = FindPlace(terrain, 2, places[1].x, places[1].z);
    for (const Place& place : places) {
        log::Write("terrain brushbody test: place %s: %s at vertex %u,%u", place.name, place.found ? "found" : "none",
                   place.x, place.z);
        g.hash.Mix(place.found ? 1u : 0u);
        g.hash.Mix(place.x);
        g.hash.Mix(place.z);
    }
    Note("before", 0, nullptr, terrain, 0);
    for (const Brush& brush : kBrushes) {
        if (!(sets & brush.set))
            continue;
        if (!BrushRecord(brush.id)) {
            log::Write("terrain brushbody test: no brush %08X (%s)", brush.id, brush.name);
            g.hash.Mix(brush.id);
            continue;
        }
        Call<void>(terrain, kBeginBrushScrub, brush.id, 1u);
        bool first = true;
        for (const Place& place : places) {
            if (!place.found)
                continue;
            ApplyAt(terrain, brush, place, 0.25f, first);
            ApplyAt(terrain, brush, place, 1.0f, first);
        }
        Call<void>(terrain, kEndBrushScrub, brush.id, 1u);
        char what[64];
        std::snprintf(what, sizeof what, "EndBrushScrub(%s)", brush.name);
        Note(what, brush.id, nullptr, terrain, brush.id);
    }
    log::Write("terrain brushbody test: %u calls noted, checksum %08X", g.calls, g.hash.h);
}

void Day_(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_TERRAIN_BRUSHBODY"))
        return;
    char* const terrain = LiveTerrain();
    if (!terrain) {
        log::Write("terrain brushbody test: no terrain; nothing done");
        return;
    }
    Brushes(terrain);
}

// With the switch set: b<checksum>/<calls> once the test ran, and the state
// of the terrain's random generator.
void Fingerprint(cISC4City*, std::string& line)
{
    if (EnvInt("OPENSC4_TEST_TERRAIN_BRUSHBODY", -1) < 0)
        return;
    char* const terrain = LiveTerrain();
    if (!terrain) {
        line += " | terrainbody none";
        return;
    }
    char text[96];
    line += " | terrainbody";
    if (g_body.ran) {
        std::snprintf(text, sizeof text, " b%08X/%u", g_body.hash.h, g_body.calls);
        line += text;
    }
    std::snprintf(text, sizeof text, " r%08X", RandomState());
    line += text;
}

const DayHook kBody{"terrainbody", 123, &Day_};
const FingerprintPart kFingerprint{"terrainbody", 124, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
