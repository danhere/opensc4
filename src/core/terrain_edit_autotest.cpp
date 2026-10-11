// The terrain setters' part of the autotest (src/core/autotest_hooks.h):
// day hooks that call every slot of simulators.terrain.edit
// (src/core/terrain_edit.cpp) on the city's live terrain through the object's
// vtable as it is then (ours when the group is installed, the game's when
// not), with fixed arguments in a fixed order, and a fingerprint part with a
// checksum of what each call answered and left behind.
//
// OPENSC4_TEST_TERRAIN_EDIT=<n>: on the n-th day of the run, the local
// edits: long operation and texture gathering around altitude edits of a few
// vertices, locks, hidden triangles, cell flags, the brush scrub brackets,
// the two display states and the terrain type (set to what it is; with
// OPENSC4_TEST_TERRAIN_TYPE=<t> also to t and back).
// OPENSC4_TEST_TERRAIN_SEA=<n>: on the n-th day, what changes the whole map:
// the sea level, the terrain level, CreateFlatAreas and CreateShoreCliffs.
//
// The places come from the terrain itself: the first block of 6 x 6 vertices
// (rows first, four vertices or more from every edge) that is dry, unlocked
// and has none of the cell flags set, so both runs of a pair pick the same.
// Some edits are left in place on purpose (raised and lowered vertices, a
// locked column, hidden triangles, flags), so the terrain fingerprint
// ("terrain", terrain_autotest.cpp) differs from an unedited run's.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/terrain_sim.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
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
    kSetTerrainType = 41,
    kTerrainType = 42,
    kCreateFlatAreas = 43,
    kCreateShoreCliffs = 44,
    kSetSeaLevel = 45,
    kChangeSeaLevel = 46,
    kDefaultRaiseLowerAmount = 47,
    kChangeTerrainLevel = 48,
    kSetAltitudeInRect = 49,
    kSetAltitudeAtIndex = 50,
    kSetAltitudeAtVertex = 51,
    kLockAltitude = 52,
    kGetBrushExemplarID = 58,
    kBeginBrushScrub = 59,
    kEndBrushScrub = 60,
    kRedisplayRect = 68,
    kHideTerrainCell = 71,
    kHideEvenTriangle = 72,
    kHideOddTriangle = 73,
    kStartAccumulating = 78,
    kStopAccumulating = 79,
    kSetTerrainCellFlags = 81,
    kGetTerrainCellFlags = 82,
    kSetWaterSurfaceDisplayState = 84,
    kSetPlumbingInfoDisplayState = 86,
    kBeginLongOperation = 87,
    kEndLongOperation = 88,
    kSetFlipped1 = 93,
    kSetFlipped2 = 94,
};

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

// What the two tests gathered, for the fingerprint.
struct Gathered {
    Hash hash;
    uint32_t calls = 0;
    bool ran = false;
};
Gathered g_edit;
Gathered g_sea;

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

// The state of one vertex: altitude and water depth bits and the flags word.
void MixVertex(Hash& hash, char* terrain, uint32_t index)
{
    hash.Mix(AtAddress<uint32_t>(VertexAt(terrain, index)));
    hash.Mix(AtAddress<uint32_t>(WaterAt(terrain, index) + water::kDepth));
    hash.Mix(Flags(terrain, index));
}

// The state of the block of vertices the local edits work in.
uint32_t BlockState(char* terrain, uint32_t bx, uint32_t bz)
{
    Hash hash;
    for (uint32_t z = bz; z < bz + 6; ++z)
        for (uint32_t x = bx; x < bx + 6; ++x)
            MixVertex(hash, terrain, Index(terrain, x, z));
    return hash.h;
}

// The state of every vertex, the sea level and the height range.
uint32_t MapState(char* terrain)
{
    Hash hash;
    const uint32_t count = U32(terrain, field::kVertexCount);
    for (uint32_t index = 0; index < count; ++index)
        MixVertex(hash, terrain, index);
    hash.Mix(U32(terrain, field::kSeaLevel));
    hash.Mix(U32(terrain, field::kMinHeight));
    hash.Mix(U32(terrain, field::kMaxHeight));
    return hash.h;
}

// The fields outside the vertex records that the setters write.
uint32_t ObjectState(char* terrain)
{
    Hash hash;
    hash.Mix(U32(terrain, field::kTerrainType));
    hash.Mix(At<uint8_t>(terrain, field::kAccumulating));
    for (uint32_t i = 0; i < 4; ++i)
        hash.Mix(U32(terrain, field::kAccumulated + 4 * i));
    hash.Mix(At<uint8_t>(terrain, field::kShowWater));
    hash.Mix(Global<uint8_t>(addr::STE_DebugCells));
    hash.Mix(U32(terrain, kScrubbedCount));
    return hash.h;
}

// One log line per call, and the call's answer and what it left in the
// checksum.
void Note(Gathered& gathered, const char* what, uint32_t answer, uint32_t state, char* terrain)
{
    const uint32_t object = ObjectState(terrain);
    gathered.hash.Mix(answer);
    gathered.hash.Mix(state);
    gathered.hash.Mix(object);
    ++gathered.calls;
    log::Write("terrain edit test: %s: answer %08X, state %08X, object %08X", what, answer, state, object);
}

char* LiveTerrain()
{
    char* const terrain = game::IsBuild641() ? Global<char*>(addr::SC4_Terrain) : nullptr;
    if (!terrain || !At<uint8_t>(terrain, field::kInitialized) || !U32(terrain, field::kVertices) ||
        !At<void*>(terrain, field::kWater) || !At<uint32_t>(At<void*>(terrain, field::kWater), water::kRecords))
        return nullptr;
    return terrain;
}

// The first clean block of 6 x 6 vertices: dry, unlocked, no cell flags.
bool FindBlock(char* terrain, uint32_t& bx, uint32_t& bz)
{
    const uint32_t vertsX = U32(terrain, field::kVertsX), vertsZ = U32(terrain, field::kVertsZ);
    constexpr uint32_t kFlagBits = 0x0020 | 0x0040 | 0x0200 | 0x1000 | 0x2000 | 0x4000;
    for (uint32_t z = 4; z + 10 <= vertsZ; ++z) {
        for (uint32_t x = 4; x + 10 <= vertsX; ++x) {
            bool clean = true;
            for (uint32_t dz = 0; dz < 6 && clean; ++dz)
                for (uint32_t dx = 0; dx < 6 && clean; ++dx) {
                    const uint32_t index = Index(terrain, x + dx, z + dz);
                    if ((Flags(terrain, index) & kFlagBits) != 0 ||
                        !(AtAddress<float>(WaterAt(terrain, index) + water::kDepth) <= 0.0f))
                        clean = false;
                }
            if (clean) {
                bx = x;
                bz = z;
                return true;
            }
        }
    }
    return false;
}

// The first vertex under water (depth above 0) that is unlocked, eight
// vertices or more from every edge; false when there is none.
bool FindWetVertex(char* terrain, uint32_t& wx, uint32_t& wz)
{
    const uint32_t vertsX = U32(terrain, field::kVertsX), vertsZ = U32(terrain, field::kVertsZ);
    for (uint32_t z = 8; z + 8 < vertsZ; ++z)
        for (uint32_t x = 8; x + 8 < vertsX; ++x) {
            const uint32_t index = Index(terrain, x, z);
            if ((Flags(terrain, index) & 0x4000) == 0 && AtAddress<float>(WaterAt(terrain, index) + water::kDepth) > 0.0f) {
                wx = x;
                wz = z;
                return true;
            }
        }
    return false;
}

// --- OPENSC4_TEST_TERRAIN_EDIT -----------------------------------------------

void LocalEdits(char* terrain)
{
    Gathered& g = g_edit;
    g.ran = true;
    const uint32_t cellsX = U32(terrain, field::kCellsX), cellsZ = U32(terrain, field::kCellsZ);
    if (U32(terrain, field::kVertsX) < 24 || U32(terrain, field::kVertsZ) < 24 ||
        U32(terrain, field::kVertsX) != cellsX + 1 || U32(terrain, field::kVertsZ) != cellsZ + 1) {
        log::Write("terrain edit test: unexpected sizes (%u x %u cells); nothing done", cellsX, cellsZ);
        return;
    }
    uint32_t bx = 4, bz = 4;
    const bool clean = FindBlock(terrain, bx, bz);
    log::Write("terrain edit test: block at vertex %u,%u (%s); %u x %u cells, sea level %08X", bx, bz,
               clean ? "clean" : "none clean, using the corner", cellsX, cellsZ, U32(terrain, field::kSeaLevel));
    g.hash.Mix(bx);
    g.hash.Mix(bz);
    const auto block = [&] { return BlockState(terrain, bx, bz); };
    const auto at = [&](uint32_t dx, uint32_t dz) { return Index(terrain, bx + dx, bz + dz); };

    // The brackets the game's tools put around edits.
    Call<void>(terrain, kBeginLongOperation);
    Note(g, "BeginLongTerrainOperation", 0, block(), terrain);
    Call<void>(terrain, kStartAccumulating);
    Note(g, "StartAccumulatingTextureUpdates", 0, block(), terrain);
    Call<void>(terrain, kStartAccumulating);
    Note(g, "StartAccumulatingTextureUpdates again", 0, block(), terrain);

    // SetAltitude(rect, altitude, update, how) as the game's own call at
    // 0x0074A879: (rect, altitude, 1, 2). Nine vertices to the first one's
    // altitude + 6.
    {
        int32_t rect[4] = {static_cast<int32_t>(bx), static_cast<int32_t>(bz), static_cast<int32_t>(bx + 2),
                           static_cast<int32_t>(bz + 2)};
        const float altitude = Altitude(terrain, at(0, 0)) + 6.0f;
        Call<void>(terrain, kSetAltitudeInRect, static_cast<void*>(rect), altitude, 1u, 2u);
        Note(g, "SetAltitude(rect) + 6", Bits(altitude), block(), terrain);
    }

    // SetAltitude(x, z, altitude): up, down, the same.
    {
        float altitude = Altitude(terrain, at(4, 0)) + 4.0f;
        Call<void>(terrain, kSetAltitudeAtVertex, bx + 4, bz, altitude);
        Note(g, "SetAltitude(x, z) + 4", Bits(altitude), block(), terrain);
        altitude = Altitude(terrain, at(4, 1)) - 3.0f;
        Call<void>(terrain, kSetAltitudeAtVertex, bx + 4, bz + 1, altitude);
        Note(g, "SetAltitude(x, z) - 3", Bits(altitude), block(), terrain);
        altitude = Altitude(terrain, at(4, 2));
        Call<void>(terrain, kSetAltitudeAtVertex, bx + 4, bz + 2, altitude);
        Note(g, "SetAltitude(x, z) the same", Bits(altitude), block(), terrain);
        // SetAltitude(index, altitude).
        altitude = Altitude(terrain, at(4, 3)) - 2.0f;
        Call<void>(terrain, kSetAltitudeAtIndex, at(4, 3), altitude);
        Note(g, "SetAltitude(index) - 2", Bits(altitude), block(), terrain);
        // The redisplay the game's callers follow such edits with (slot 49
        // does it with 1, 1, rect, how).
        int32_t rect[4] = {static_cast<int32_t>(bx + 4), static_cast<int32_t>(bz), static_cast<int32_t>(bx + 4),
                           static_cast<int32_t>(bz + 3)};
        Call<void>(terrain, kRedisplayRect, 1u, 1u, static_cast<void*>(rect), 0u);
        Note(g, "RedisplayTerrain(rect) after them", 0, block(), terrain);
    }

    // A vertex under water keeps its water surface in SetAltitude(index).
    {
        uint32_t wx = 0, wz = 0;
        if (FindWetVertex(terrain, wx, wz)) {
            const uint32_t index = Index(terrain, wx, wz);
            const float altitude = Altitude(terrain, index) - 1.0f;
            Call<void>(terrain, kSetAltitudeAtIndex, index, altitude);
            Hash hash;
            MixVertex(hash, terrain, index);
            log::Write("terrain edit test: wet vertex %u,%u", wx, wz);
            Note(g, "SetAltitude(index) - 1 under water", Bits(altitude), hash.h, terrain);
            int32_t rect[4] = {static_cast<int32_t>(wx), static_cast<int32_t>(wz), static_cast<int32_t>(wx),
                               static_cast<int32_t>(wz)};
            Call<void>(terrain, kRedisplayRect, 1u, 1u, static_cast<void*>(rect), 0u);
            Note(g, "RedisplayTerrain(rect) after it", 0, hash.h, terrain);
        } else {
            log::Write("terrain edit test: no vertex under water; SetAltitude(index) not asked for one");
        }
    }

    // LockAltitude, and every altitude setter on locked vertices.
    {
        int32_t rect[4] = {static_cast<int32_t>(bx), static_cast<int32_t>(bz + 4), static_cast<int32_t>(bx + 1),
                           static_cast<int32_t>(bz + 5)};
        Call<void>(terrain, kLockAltitude, static_cast<void*>(rect), 1u);
        Note(g, "LockAltitude(rect, 1)", 0, block(), terrain);
        float altitude = Altitude(terrain, at(0, 4)) + 5.0f;
        Call<void>(terrain, kSetAltitudeAtIndex, at(0, 4), altitude);
        Note(g, "SetAltitude(index) + 5, locked", Bits(altitude), block(), terrain);
        altitude = Altitude(terrain, at(1, 4)) + 5.0f;
        Call<void>(terrain, kSetAltitudeAtVertex, bx + 1, bz + 4, altitude);
        Note(g, "SetAltitude(x, z) + 5, locked", Bits(altitude), block(), terrain);
        altitude = Altitude(terrain, at(0, 5)) - 5.0f;
        Call<void>(terrain, kSetAltitudeAtVertex, bx, bz + 5, altitude);
        Note(g, "SetAltitude(x, z) - 5, locked", Bits(altitude), block(), terrain);
        altitude = Altitude(terrain, at(0, 4)) + 3.0f;
        Call<void>(terrain, kSetAltitudeInRect, static_cast<void*>(rect), altitude, 1u, 2u);
        Note(g, "SetAltitude(rect) + 3, locked", Bits(altitude), block(), terrain);
        // One column unlocked again, the other stays locked.
        rect[2] = static_cast<int32_t>(bx);
        Call<void>(terrain, kLockAltitude, static_cast<void*>(rect), 0u);
        Note(g, "LockAltitude(column, 0)", 0, block(), terrain);
        // An empty rectangle (x1 < x0): nothing.
        rect[2] = static_cast<int32_t>(bx) - 1;
        Call<void>(terrain, kLockAltitude, static_cast<void*>(rect), 1u);
        Note(g, "LockAltitude(empty, 1)", 0, block(), terrain);
    }

    // Hidden triangles: set, cleared, and cells outside the grid.
    Call<void>(terrain, kHideTerrainCell, bx + 2, bz + 4, 1u);
    Note(g, "HideTerrainCell(1)", Call<uint32_t>(terrain, kGetTerrainCellFlags, bx + 2, bz + 4), block(), terrain);
    Call<void>(terrain, kHideEvenTriangle, bx + 3, bz + 4, 1u);
    Note(g, "HideEvenTriangle(1)", Call<uint32_t>(terrain, kGetTerrainCellFlags, bx + 3, bz + 4), block(), terrain);
    Call<void>(terrain, kHideOddTriangle, bx + 4, bz + 4, 1u);
    Note(g, "HideOddTriangle(1)", Call<uint32_t>(terrain, kGetTerrainCellFlags, bx + 4, bz + 4), block(), terrain);
    Call<void>(terrain, kHideTerrainCell, bx + 2, bz + 5, 1u);
    Call<void>(terrain, kHideTerrainCell, bx + 2, bz + 5, 0u);
    Note(g, "HideTerrainCell(1) then (0)", Call<uint32_t>(terrain, kGetTerrainCellFlags, bx + 2, bz + 5), block(),
         terrain);
    Call<void>(terrain, kHideEvenTriangle, bx + 3, bz + 5, 1u);
    Call<void>(terrain, kHideOddTriangle, bx + 3, bz + 5, 1u);
    Call<void>(terrain, kHideEvenTriangle, bx + 3, bz + 5, 0u);
    Note(g, "HideEven(1), HideOdd(1), HideEven(0)", Call<uint32_t>(terrain, kGetTerrainCellFlags, bx + 3, bz + 5),
         block(), terrain);
    Call<void>(terrain, kHideOddTriangle, bx + 2, bz + 5, 1u);
    Call<void>(terrain, kHideOddTriangle, bx + 2, bz + 5, 0u);
    Note(g, "HideOddTriangle(1) then (0)", Call<uint32_t>(terrain, kGetTerrainCellFlags, bx + 2, bz + 5), block(),
         terrain);
    Call<void>(terrain, kHideTerrainCell, cellsX, 0u, 1u);
    Call<void>(terrain, kHideEvenTriangle, 0u, cellsZ, 1u);
    Call<void>(terrain, kHideOddTriangle, 0xFFFFFFFFu, 0u, 1u);
    Note(g, "the three outside the grid", 0, MapState(terrain), terrain);

    // SetTerrainCellFlags.
    {
        uint32_t answer = Call<uint8_t>(terrain, kSetTerrainCellFlags, bx + 4, bz + 5, 0xDu);
        Note(g, "SetTerrainCellFlags(0xD)", answer, block(), terrain);
        answer = Call<uint8_t>(terrain, kSetTerrainCellFlags, bx + 4, bz + 5, 0xDu);
        Note(g, "SetTerrainCellFlags(0xD) again", answer, block(), terrain);
        answer = Call<uint8_t>(terrain, kSetTerrainCellFlags, bx + 4, bz + 5, 0x2u);
        Note(g, "SetTerrainCellFlags(2)", answer | (Call<uint32_t>(terrain, kGetTerrainCellFlags, bx + 4, bz + 5) << 8),
             block(), terrain);
        answer = Call<uint8_t>(terrain, kSetTerrainCellFlags, cellsX, 0u, 1u);
        answer |= static_cast<uint32_t>(Call<uint8_t>(terrain, kSetTerrainCellFlags, 0u, cellsZ, 1u)) << 8;
        Note(g, "SetTerrainCellFlags outside the grid", answer, MapState(terrain), terrain);
    }

    // The two flags of slots 93 and 94.
    Call<void>(terrain, kSetFlipped1, bx + 5, bz + 4, 1u);
    Note(g, "slot 93 (1)", Flags(terrain, at(5, 4)), block(), terrain);
    Call<void>(terrain, kSetFlipped1, bx + 5, bz + 4, 1u);
    Call<void>(terrain, kSetFlipped1, bx + 5, bz + 4, 0u);
    Note(g, "slot 93 (1) again then (0)", Flags(terrain, at(5, 4)), block(), terrain);
    Call<void>(terrain, kSetFlipped1, bx + 5, bz + 5, 1u);
    Note(g, "slot 93 (1), left", Flags(terrain, at(5, 5)), block(), terrain);
    Call<void>(terrain, kSetFlipped2, bx + 5, bz + 3, 1u);
    Note(g, "slot 94 (1), left", Flags(terrain, at(5, 3)), block(), terrain);
    Call<void>(terrain, kSetFlipped2, bx + 5, bz + 2, 1u);
    Call<void>(terrain, kSetFlipped2, bx + 5, bz + 2, 0u);
    Note(g, "slot 94 (1) then (0)", Flags(terrain, at(5, 2)), block(), terrain);
    Call<void>(terrain, kSetFlipped1, cellsX, 0u, 1u);
    Call<void>(terrain, kSetFlipped2, 0u, cellsZ, 1u);
    Note(g, "slots 93 and 94 outside the grid", 0, MapState(terrain), terrain);

    Call<void>(terrain, kStopAccumulating);
    Note(g, "StopAccumulatingTextureUpdates", 0, block(), terrain);
    Call<void>(terrain, kStopAccumulating);
    Note(g, "StopAccumulatingTextureUpdates again", 0, block(), terrain);

    // The brush scrub brackets, with the first brush's exemplar ID (the
    // terrain's own slot 58) and with an ID no brush has.
    {
        uint32_t id = 0;
        const uint32_t known = Call<uint32_t>(terrain, kGetBrushExemplarID, 0u, &id) & 0xFF;
        Call<void>(terrain, kBeginBrushScrub, id, 0u);
        Note(g, "BeginBrushScrub(first brush)", id ^ known, block(), terrain);
        Call<void>(terrain, kEndBrushScrub, id, 0u);
        Note(g, "EndBrushScrub(first brush)", id, MapState(terrain), terrain);
        Call<void>(terrain, kBeginBrushScrub, 0x12345678u, 0u);
        Call<void>(terrain, kEndBrushScrub, 0x12345678u, 0u);
        Note(g, "Begin and EndBrushScrub(unknown brush)", 0, MapState(terrain), terrain);
    }

    // The display states: changed and put back (the game's callers at
    // 0x007A4A70 and 0x007A4A80 pass a bool).
    {
        const uint32_t water = At<uint8_t>(terrain, field::kShowWater);
        Call<void>(terrain, kSetWaterSurfaceDisplayState, water ? 0u : 1u);
        Note(g, "SetWaterSurfaceDisplayState(other)", At<uint8_t>(terrain, field::kShowWater), 0, terrain);
        Call<void>(terrain, kSetWaterSurfaceDisplayState, water);
        Call<void>(terrain, kSetWaterSurfaceDisplayState, water);
        Note(g, "SetWaterSurfaceDisplayState(back), twice", At<uint8_t>(terrain, field::kShowWater), 0, terrain);
        const uint32_t cells = Global<uint8_t>(addr::STE_DebugCells);
        Call<void>(terrain, kSetPlumbingInfoDisplayState, cells ? 0u : 1u);
        Note(g, "SetPlumbingInfoDisplayState(other)", Global<uint8_t>(addr::STE_DebugCells), 0, terrain);
        Call<void>(terrain, kSetPlumbingInfoDisplayState, cells);
        Note(g, "SetPlumbingInfoDisplayState(back)", Global<uint8_t>(addr::STE_DebugCells), 0, terrain);
    }

    // The terrain type: to what it is (nothing happens); to another only
    // when asked (it rebuilds the whole terrain's textures).
    {
        const uint32_t type = Call<uint32_t>(terrain, kTerrainType);
        uint32_t answer = Call<uint8_t>(terrain, kSetTerrainType, type);
        Note(g, "SetTerrainType(the same)", answer | (type << 8), 0, terrain);
        const int other = EnvInt("OPENSC4_TEST_TERRAIN_TYPE", -1);
        if (other >= 0 && static_cast<uint32_t>(other) != type) {
            answer = Call<uint8_t>(terrain, kSetTerrainType, static_cast<uint32_t>(other));
            Note(g, "SetTerrainType(other)", answer | (U32(terrain, field::kTerrainType) << 8), MapState(terrain),
                 terrain);
            answer = Call<uint8_t>(terrain, kSetTerrainType, type);
            Note(g, "SetTerrainType(back)", answer | (U32(terrain, field::kTerrainType) << 8), MapState(terrain),
                 terrain);
        }
    }

    Call<void>(terrain, kEndLongOperation);
    Note(g, "EndLongTerrainOperation", 0, MapState(terrain), terrain);
    log::Write("terrain edit test: %u calls noted, checksum %08X", g.calls, g.hash.h);
}

// --- OPENSC4_TEST_TERRAIN_SEA ------------------------------------------------

void WholeMapEdits(char* terrain)
{
    Gathered& g = g_sea;
    g.ran = true;
    const auto sea = [&] { return U32(terrain, field::kSeaLevel); };
    log::Write("terrain edit test: whole map; sea level %08X, heights %08X to %08X", sea(),
               U32(terrain, field::kMinHeight), U32(terrain, field::kMaxHeight));
    Call<void>(terrain, kBeginLongOperation);
    Note(g, "BeginLongTerrainOperation", 0, MapState(terrain), terrain);

    const float original = At<float>(terrain, field::kSeaLevel);
    const float lowest = Global<float>(addr::cSTETerrain_MinSeaLevel);
    const float highest = Global<float>(addr::cSTETerrain_MaxSeaLevel);
    uint32_t answer = Call<uint8_t>(terrain, kChangeSeaLevel, 1u);
    Note(g, "ChangeSeaLevel(up)", answer | (sea() & 0xFFFFFF00u), MapState(terrain), terrain);
    answer = Call<uint8_t>(terrain, kChangeSeaLevel, 0u);
    Note(g, "ChangeSeaLevel(down)", answer | (sea() & 0xFFFFFF00u), MapState(terrain), terrain);
    answer = Call<uint8_t>(terrain, kSetSeaLevel, lowest - 1.0f);
    Note(g, "SetSeaLevel(below the range)", answer, MapState(terrain), terrain);
    answer = Call<uint8_t>(terrain, kSetSeaLevel, highest + 1.0f);
    Note(g, "SetSeaLevel(above the range)", answer, MapState(terrain), terrain);
    answer = Call<uint8_t>(terrain, kSetSeaLevel, std::numeric_limits<float>::quiet_NaN());
    Note(g, "SetSeaLevel(NaN)", answer, MapState(terrain), terrain);
    // One metre up when the range allows it, else one down; left like that.
    const float moved = original + 1.0f <= highest ? original + 1.0f : original - 1.0f;
    answer = Call<uint8_t>(terrain, kSetSeaLevel, moved);
    Note(g, "SetSeaLevel(1 from the original)", answer | (Bits(moved) & 0xFFFFFF00u), MapState(terrain), terrain);
    // The cities' sea level (250) is itself outside the range the setter
    // takes, so everything above is refused. A level inside the range, to
    // run the setter's whole path, and a step down from there; left like
    // that.
    answer = Call<uint8_t>(terrain, kSetSeaLevel, highest - 10.0f);
    Note(g, "SetSeaLevel(inside the range)", answer | (sea() & 0xFFFFFF00u), MapState(terrain), terrain);
    answer = Call<uint8_t>(terrain, kChangeSeaLevel, 0u);
    Note(g, "ChangeSeaLevel(down) inside the range", answer | (sea() & 0xFFFFFF00u), MapState(terrain), terrain);
    answer = Call<uint8_t>(terrain, kChangeSeaLevel, 1u);
    Note(g, "ChangeSeaLevel(up) inside the range", answer | (sea() & 0xFFFFFF00u), MapState(terrain), terrain);

    // ChangeTerrainLevel as the city view's handler calls it (0x007E7699,
    // 0x007E76CB): (1 or 0, the terrain's slot 47 amount). Up by the amount,
    // down by half of it.
    const float amount = Call<float>(terrain, kDefaultRaiseLowerAmount);
    Call<void>(terrain, kChangeTerrainLevel, 1u, amount);
    Note(g, "ChangeTerrainLevel(up)", Bits(amount), MapState(terrain), terrain);
    Call<void>(terrain, kChangeTerrainLevel, 0u, amount * 0.5f);
    Note(g, "ChangeTerrainLevel(down, half)", Bits(amount * 0.5f), MapState(terrain), terrain);

    // As the city view's handler: (1) each (0x007E764A, 0x007E7667).
    Call<void>(terrain, kCreateFlatAreas, 1u);
    Note(g, "CreateFlatAreas(1)", 0, MapState(terrain), terrain);
    answer = Call<uint8_t>(terrain, kCreateShoreCliffs, 1u);
    Note(g, "CreateShoreCliffs(1)", answer, MapState(terrain), terrain);

    Call<void>(terrain, kEndLongOperation);
    Note(g, "EndLongTerrainOperation", 0, MapState(terrain), terrain);
    log::Write("terrain edit test: whole map: %u calls noted, checksum %08X", g.calls, g.hash.h);
}

void Edit(const Day& day)
{
    const bool local = OnRunDay(day, "OPENSC4_TEST_TERRAIN_EDIT");
    const bool whole = OnRunDay(day, "OPENSC4_TEST_TERRAIN_SEA");
    if (!local && !whole)
        return;
    char* const terrain = LiveTerrain();
    if (!terrain) {
        log::Write("terrain edit test: no terrain; nothing done");
        return;
    }
    if (local)
        LocalEdits(terrain);
    if (whole)
        WholeMapEdits(terrain);
}

// With either switch set: e<checksum>/<calls> of the local edits and
// s<checksum>/<calls> of the whole-map ones once they ran, then the fields
// outside the vertex records that the setters write and the terrain
// fingerprint does not show: the terrain type (t), whether texture updates
// are gathered (a) and the gathered rectangle, the water display state (w),
// the cell overlay global (d) and the size of the scrub set (c).
void Fingerprint(cISC4City*, std::string& line)
{
    if (EnvInt("OPENSC4_TEST_TERRAIN_EDIT", -1) < 0 && EnvInt("OPENSC4_TEST_TERRAIN_SEA", -1) < 0)
        return;
    char* const terrain = LiveTerrain();
    if (!terrain) {
        line += " | terrainedit none";
        return;
    }
    char text[240];
    line += " | terrainedit";
    if (g_edit.ran) {
        std::snprintf(text, sizeof text, " e%08X/%u", g_edit.hash.h, g_edit.calls);
        line += text;
    }
    if (g_sea.ran) {
        std::snprintf(text, sizeof text, " s%08X/%u", g_sea.hash.h, g_sea.calls);
        line += text;
    }
    std::snprintf(text, sizeof text, " t%u a%u,%d,%d,%d,%d w%u d%u c%u", U32(terrain, field::kTerrainType),
                  static_cast<unsigned>(At<uint8_t>(terrain, field::kAccumulating)),
                  At<int32_t>(terrain, field::kAccumulated), At<int32_t>(terrain, field::kAccumulated + 4),
                  At<int32_t>(terrain, field::kAccumulated + 8), At<int32_t>(terrain, field::kAccumulated + 12),
                  static_cast<unsigned>(At<uint8_t>(terrain, field::kShowWater)),
                  static_cast<unsigned>(Global<uint8_t>(addr::STE_DebugCells)), U32(terrain, kScrubbedCount));
    line += text;
}

const DayHook kEdit{"terrainedit", 112, &Edit};
const FingerprintPart kFingerprint{"terrainedit", 113, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
