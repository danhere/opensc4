// The terrain brushes' part of the autotest (src/core/autotest_hooks.h): a
// day hook that applies every terrain brush through the terrain's slot 57
// (ApplyBrush) the way the game's terraform tool does, and then runs the two
// whole-map shapers (slots 43 and 44), whose machinery is ours with
// simulators.terrain.brush (src/core/terrain_brush.cpp). Everything goes
// through the object's vtable as it is then, so the same test runs the
// game's code when the group is off.
//
// OPENSC4_TEST_TERRAIN_BRUSH=<n>: on the n-th day of the run:
// 1. ApplyBrush with an exemplar ID no brush has;
// 2. for each brush type 0 to 6 (the terrain's own slot 58 gives the
//    exemplar ID): BeginBrushScrub, then at each of four places (a dry
//    inland vertex, a shore vertex, a vertex one in from the west edge, a
//    vertex under water) and for two strengths (0.25 and 1.0) three
//    applications along a short line as a drag makes them, then
//    EndBrushScrub;
// 3. CreateFlatAreas(1) and CreateShoreCliffs(1).
// One log line per call with its answer, the applied rectangle and a
// checksum of the whole terrain.
//
// The calls are made as the terraform tool makes them (its step at
// 0x004D9740, the call at 0x004D9920; the scrub brackets at 0x004D9CA4 and
// 0x004D99FB): the terrain's slot 59 with (brush, 1 or 2), slot 57 with the
// brush and the tool's parameter block (bounds = the previous and the
// current cursor position in metres, zoom -1, the redisplay byte 1, a
// strength, a radius scaling, the same 1 or 2, an altitude), slot 60 with
// (brush, 1 or 2). The tool also charges the brush's cost when the city is
// not in god mode; the test does not. What the tool puts in the altitude
// (its +0xA8, set where the scrub begins) was not traced: the test gives the
// altitude of the place when the scrub begins.
//
// The places come from the terrain itself (rows first), so both runs of a
// pair pick the same.
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

// Fields terrain_sim.h does not have yet (see terrain_edit.cpp).
constexpr std::size_t kScrubbedCount = 0x1D8;

// Slot numbers of the terrain's vtable (0x00AB3D18).
enum : int {
    kCreateFlatAreas = 43,
    kCreateShoreCliffs = 44,
    kApplyBrush = 57,
    kGetBrushExemplarID = 58,
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
Gathered g_brush;

// A slot of the object's vtable as it is now.
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

// The state of every vertex (altitude, water depth, sediment, flags), the
// sea level, the height range and the size of the scrub set.
uint32_t MapState(char* terrain)
{
    Hash hash;
    const uint32_t count = U32(terrain, field::kVertexCount);
    for (uint32_t index = 0; index < count; ++index) {
        hash.Mix(AtAddress<uint32_t>(VertexAt(terrain, index)));
        hash.Mix(AtAddress<uint32_t>(WaterAt(terrain, index) + water::kDepth));
        hash.Mix(AtAddress<uint32_t>(WaterAt(terrain, index) + water::kSediment));
        hash.Mix(Flags(terrain, index));
    }
    hash.Mix(U32(terrain, field::kSeaLevel));
    hash.Mix(U32(terrain, field::kMinHeight));
    hash.Mix(U32(terrain, field::kMaxHeight));
    hash.Mix(U32(terrain, kScrubbedCount));
    return hash.h;
}

// The state of the terrain's random generator (an LCG: one dword).
uint32_t RandomState() { return Global<uint32_t>(addr::cSTETerrain_Random); }

void Note(const char* what, uint32_t answer, const int32_t* applied, char* terrain)
{
    Gathered& g = g_brush;
    const uint32_t state = MapState(terrain);
    const uint32_t random = RandomState();
    g.hash.Mix(answer);
    g.hash.Mix(state);
    g.hash.Mix(random);
    if (applied)
        for (int i = 0; i < 4; ++i)
            g.hash.Mix(static_cast<uint32_t>(applied[i]));
    ++g.calls;
    if (applied)
        log::Write("terrain brush test: %s: answer %08X, applied %d,%d,%d,%d, state %08X, random %08X", what, answer,
                   applied[0], applied[1], applied[2], applied[3], state, random);
    else
        log::Write("terrain brush test: %s: answer %08X, state %08X, random %08X", what, answer, state, random);
}

char* LiveTerrain()
{
    char* const terrain = game::IsBuild641() ? Global<char*>(addr::SC4_Terrain) : nullptr;
    if (!terrain || !At<uint8_t>(terrain, field::kInitialized) || !U32(terrain, field::kVertices) ||
        !At<void*>(terrain, field::kWater) || !At<uint32_t>(At<void*>(terrain, field::kWater), water::kRecords))
        return nullptr;
    return terrain;
}

constexpr uint32_t kMargin = 24; // vertices kept between a place and every edge
constexpr uint32_t kClear = 6;   // the half width of the square a place is judged by

struct Place {
    const char* name;
    bool found;
    uint32_t x, z;
};

// The first vertex (rows first), kMargin or more from every edge, whose
// square of kClear around it is unlocked and all dry (wanted 0), all wet
// (wanted 1), or dry at the vertex with water somewhere in the square
// (wanted 2).
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
            if ((wanted == 0 && wet == 0) || (wanted == 1 && wet == all) || (wanted == 2 && wet != 0 && wet * 4 < all)) {
                px = x;
                pz = z;
                return true;
            }
        }
    }
    return false;
}

void ApplyAt(char* terrain, uint32_t id, const Place& place, float strength, uint32_t brushType)
{
    const float altitude = Altitude(terrain, Index(terrain, place.x, place.z));
    float previousX = static_cast<float>(place.x) * 16.0f;
    float previousZ = static_cast<float>(place.z) * 16.0f;
    for (int step = 0; step < 3; ++step) {
        BrushParameters parameters{};
        parameters.x0 = previousX;
        parameters.z0 = previousZ;
        parameters.x1 = static_cast<float>(place.x) * 16.0f + static_cast<float>(step) * 8.0f;
        parameters.z1 = static_cast<float>(place.z) * 16.0f + static_cast<float>(step) * 4.0f;
        parameters.zoom = -1;
        parameters.redisplay = 1;
        parameters.strength = strength;
        parameters.radiusScaling = 1.0f;
        parameters.messageData = 1;
        parameters.altitude = altitude;
        const uint32_t answer = Call<uint32_t>(terrain, kApplyBrush, id, &parameters) & 0xFF;
        char what[96];
        std::snprintf(what, sizeof what, "ApplyBrush(type %u, %s, strength %08X, step %d)", brushType, place.name,
                      Bits(strength), step);
        Note(what, answer, parameters.applied, terrain);
        previousX = parameters.x1;
        previousZ = parameters.z1;
    }
}

void Brushes(char* terrain)
{
    Gathered& g = g_brush;
    g.ran = true;
    const uint32_t cellsX = U32(terrain, field::kCellsX), cellsZ = U32(terrain, field::kCellsZ);
    const uint32_t vertsX = U32(terrain, field::kVertsX), vertsZ = U32(terrain, field::kVertsZ);
    if (vertsX < 3 * kMargin || vertsZ < 3 * kMargin || vertsX != cellsX + 1 || vertsZ != cellsZ + 1) {
        log::Write("terrain brush test: unexpected sizes (%u x %u cells); nothing done", cellsX, cellsZ);
        return;
    }
    Place places[4] = {{"inland", false, 0, 0}, {"shore", false, 0, 0}, {"edge", true, 1, vertsZ / 2}, {"water", false, 0, 0}};
    places[0].found = FindPlace(terrain, 0, places[0].x, places[0].z);
    places[1].found = FindPlace(terrain, 2, places[1].x, places[1].z);
    places[3].found = FindPlace(terrain, 1, places[3].x, places[3].z);
    for (const Place& place : places) {
        log::Write("terrain brush test: place %s: %s at vertex %u,%u", place.name, place.found ? "found" : "none",
                   place.x, place.z);
        g.hash.Mix(place.found ? 1u : 0u);
        g.hash.Mix(place.x);
        g.hash.Mix(place.z);
    }
    Note("before", 0, nullptr, terrain);

    // An exemplar ID no brush has: the answer is 0 and the applied rectangle
    // is the empty one.
    {
        BrushParameters parameters{};
        parameters.x0 = parameters.x1 = static_cast<float>(vertsX / 2) * 16.0f;
        parameters.z0 = parameters.z1 = static_cast<float>(vertsZ / 2) * 16.0f;
        parameters.zoom = -1;
        parameters.redisplay = 1;
        parameters.strength = 1.0f;
        parameters.radiusScaling = 1.0f;
        parameters.messageData = 1;
        const uint32_t answer = Call<uint32_t>(terrain, kApplyBrush, 0x12345678u, &parameters) & 0xFF;
        Note("ApplyBrush(unknown brush)", answer, parameters.applied, terrain);
    }

    for (uint32_t type = 0; type < 7; ++type) {
        uint32_t id = 0;
        const uint32_t known = Call<uint32_t>(terrain, kGetBrushExemplarID, type, &id) & 0xFF;
        log::Write("terrain brush test: brush type %u: exemplar %08X (answer %u)", type, id, known);
        g.hash.Mix(id);
        if (!id)
            continue;
        Call<void>(terrain, kBeginBrushScrub, id, 1u);
        for (const Place& place : places) {
            if (!place.found)
                continue;
            ApplyAt(terrain, id, place, 0.25f, type);
            ApplyAt(terrain, id, place, 1.0f, type);
        }
        Call<void>(terrain, kEndBrushScrub, id, 1u);
        char what[48];
        std::snprintf(what, sizeof what, "EndBrushScrub(type %u)", type);
        Note(what, id, nullptr, terrain);
    }

    // The whole-map shapers, as the city view's handler calls them
    // (0x007E764A, 0x007E7667): their helpers are ours with the group on.
    Call<void>(terrain, kCreateFlatAreas, 1u);
    Note("CreateFlatAreas(1)", 0, nullptr, terrain);
    const uint32_t answer = Call<uint32_t>(terrain, kCreateShoreCliffs, 1u) & 0xFF;
    Note("CreateShoreCliffs(1)", answer, nullptr, terrain);
    log::Write("terrain brush test: %u calls noted, checksum %08X", g.calls, g.hash.h);
}

void Day_(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_TERRAIN_BRUSH"))
        return;
    char* const terrain = LiveTerrain();
    if (!terrain) {
        log::Write("terrain brush test: no terrain; nothing done");
        return;
    }
    Brushes(terrain);
}

// With the switch set: b<checksum>/<calls> once the test ran, the state of
// the terrain's random generator (r) and the size of the scrub set (c):
// state the brushes and the shore cliffs change that the terrain
// fingerprint does not show.
void Fingerprint(cISC4City*, std::string& line)
{
    if (EnvInt("OPENSC4_TEST_TERRAIN_BRUSH", -1) < 0)
        return;
    char* const terrain = LiveTerrain();
    if (!terrain) {
        line += " | terrainbrush none";
        return;
    }
    char text[96];
    line += " | terrainbrush";
    if (g_brush.ran) {
        std::snprintf(text, sizeof text, " b%08X/%u", g_brush.hash.h, g_brush.calls);
        line += text;
    }
    std::snprintf(text, sizeof text, " r%08X c%u", RandomState(), U32(terrain, kScrubbedCount));
    line += text;
}

const DayHook kBrush{"terrainbrush", 113, &Day_};
const FingerprintPart kFingerprint{"terrainbrush", 114, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
