// The edge reconciling's and the terrain leftovers' part of the autotest
// (src/core/autotest_hooks.h): a day hook that calls the read-only part of
// the edge reconciling the way the terrain's slot 89 does, and the small
// functions of simulators.terrain.rest (src/core/terrain_rest.cpp) that an
// ordinary city run does not reach. Every function is called at its game
// address, so the same test runs the game's code when the groups are off.
//
// OPENSC4_TEST_TERRAIN_EDGES=<n>: on the n-th day of the run:
// 1. the edge search 0x0074D010 with a set made as slot 89 makes it (a map
//    head of 0x18 bytes), first without the empty places (second argument
//    0), then, the set emptied as slot 89 empties it, with them (1, as
//    DoMessage calls slot 89): each time the number of vertices found and a
//    checksum of their indices and wanted altitudes, in order;
// 2. the collecting 0x0074B0D0 on that second set with two hash sets made as
//    slot 89 makes them: the number of cells and vertices and a checksum of
//    each (sums, so the order in the tables does not matter);
// 3. the leftovers: the normals and steep cells of the whole map
//    (0x00743B60, which calls 0x00740900 and 0x00742FC0), the shore marks
//    (0x007428E0), one shore line from the first shore vertex (0x0074B570
//    with the mark 1, as 0x00761C2F calls it) and the mark cleared again
//    everywhere (0x00740980, as 0x00761C9E), the square around five places
//    (0x00740F10), three rectangles widened (0x00741E30), and one dry inland
//    vertex raised by 0.5, lowered by 0.5, raised past the greatest height
//    and left alone while locked (0x00744CA0); that vertex's altitude, water
//    depth and flags are then put back as they were.
// One log line per call with what it answered and a checksum of the whole
// terrain.
//
// What the test must not do, and does not: it never calls slot 89 itself
// (its dialogs), nor 0x0074B2D0 (it tells the view which cells to show) nor
// 0x00748C90 (it changes this city's land along the edges). The search only
// reads: the neighbouring cities' files are opened for reading (0x005DBFE0
// with write 0) and closed again; nothing is written to any file. Like
// slot 89 the test does nothing unless the application's slot 23 answers
// (0x005F9090). A new city's terrain (simulators.terrain.newcity) cannot be
// asked for in a loaded city and is not tested here.
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
Gathered g_edges;

// A function of the class at its game address (detoured or not).
template <typename R, typename... Args>
R At641(std::uintptr_t function, void* self, Args... args)
{
    using Fn = R(__thiscall*)(void*, Args...);
    return reinterpret_cast<Fn>(function)(self, args...);
}

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

uint32_t U32(char* terrain, std::size_t offset) { return At<uint32_t>(terrain, offset); }

uint32_t VertexAt(char* terrain, uint32_t index) { return RecordAddress(terrain_sim::VertexBase(terrain), index); }

uint32_t WaterAt(char* terrain, uint32_t index) { return RecordAddress(terrain_sim::WaterBase(terrain), index); }

uint32_t Flags(char* terrain, uint32_t index) { return AtAddress<uint16_t>(VertexAt(terrain, index) + vertex::kFlags); }

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

void Note(const char* what, uint32_t a, uint32_t b, char* terrain)
{
    Gathered& g = g_edges;
    const uint32_t state = MapState(terrain);
    g.hash.Mix(a);
    g.hash.Mix(b);
    g.hash.Mix(state);
    ++g.calls;
    log::Write("terrain edges test: %s: %u, %08X, state %08X", what, a, b, state);
}

char* LiveTerrain()
{
    char* const terrain = game::IsBuild641() ? Global<char*>(addr::SC4_Terrain) : nullptr;
    if (!terrain || !At<uint8_t>(terrain, field::kInitialized) || !U32(terrain, field::kVertices) ||
        !At<void*>(terrain, field::kWater) || !At<uint32_t>(At<void*>(terrain, field::kWater), water::kRecords))
        return nullptr;
    return terrain;
}

// --- The edge set and the hash sets, as slot 89 keeps them ---------------------

// The map of vertex index to wanted altitude (a red-black tree: the head's
// parent +4 is the root, its left +8 the first node; a node's key at +0x10,
// its value at +0x14).
struct EdgeSet {
    char* head;
    uint32_t count;
};

void MakeEdgeSet(EdgeSet& set)
{
    set.head = Cdecl<char*>(addr::SC4_stl_allocate, 0x18u);
    set.count = 0;
    At<uint8_t>(set.head, 0x0) = 0;
    At<char*>(set.head, 0x4) = nullptr;
    At<char*>(set.head, 0x8) = set.head;
    At<char*>(set.head, 0xC) = set.head;
}

// As at the end of slot 89 (0x0074EF5E).
void EmptyEdgeSet(EdgeSet& set)
{
    if (!set.count)
        return;
    At641<void>(addr::cSTETerrain_FreeEdgeNodes, &set, At<void*>(set.head, 0x4));
    At<char*>(set.head, 0x8) = set.head;
    At<char*>(set.head, 0x4) = nullptr;
    At<char*>(set.head, 0xC) = set.head;
    set.count = 0;
}

char* NextNode(char* at)
{
    if (char* down = At<char*>(at, 0xC)) {
        while (char* left = At<char*>(down, 0x8))
            down = left;
        return down;
    }
    char* up = At<char*>(at, 0x4);
    while (at == At<char*>(up, 0xC)) {
        at = up;
        up = At<char*>(up, 0x4);
    }
    if (At<char*>(at, 0xC) != up)
        at = up;
    return at;
}

// The set's entries in order: indices and altitudes (bits).
uint32_t EdgeSetChecksum(const EdgeSet& set, uint32_t& walked)
{
    Hash hash;
    walked = 0;
    for (char* at = At<char*>(set.head, 0x8); at != set.head && walked <= set.count; at = NextNode(at)) {
        hash.Mix(At<uint32_t>(at, 0x10));
        hash.Mix(At<uint32_t>(at, 0x14));
        ++walked;
    }
    return hash.h;
}

// The game's hash set as it lies on slot 89's stack (0x14 bytes).
struct HashSet {
    uint32_t first;
    uint32_t bucketsBegin, bucketsEnd, bucketsCapacity;
    uint32_t count;
};
static_assert(sizeof(HashSet) == 0x14);

void MakeHashSet(HashSet& hash)
{
    hash = HashSet{};
    At641<void>(addr::SC4Uint32HashSet_Construct, &hash, 0x64u);
}

void DestroyHashSet(HashSet& hash)
{
    At641<void>(addr::SC4Uint32HashSet_Clear, &hash);
    if (hash.bucketsBegin)
        Cdecl<void>(addr::SC4_Free, hash.bucketsBegin);
}

// The elements of a hash set, whatever their order: how many the buckets
// hold, their sum and the sum of their squares.
uint32_t HashSetChecksum(const HashSet& hash, uint32_t& walked)
{
    uint32_t sum = 0, squares = 0;
    walked = 0;
    for (uint32_t bucket = hash.bucketsBegin; bucket < hash.bucketsEnd; bucket += 4u) {
        for (char* at = AtAddress<char*>(bucket); at; at = At<char*>(at, 0)) {
            const uint32_t value = At<uint32_t>(at, 4);
            sum += value;
            squares += value * value;
            if (++walked > hash.count)
                break;
        }
    }
    Hash mixed;
    mixed.Mix(sum);
    mixed.Mix(squares);
    mixed.Mix(walked);
    return mixed.h;
}

void Edges(char* terrain)
{
    if (!(Cdecl<uint32_t>(addr::SC4UI_Service43C94Slot23) & 0xFFu)) {
        // The search and the collecting only read (this city's terrain and
        // the neighbours' save files), so they are run all the same.
        log::Write("terrain edges test: the application's slot 23 is false (slot 89 would do nothing); searching all the same");
    }
    EdgeSet found;
    MakeEdgeSet(found);
    uint32_t walked = 0;

    At641<void>(addr::cSTETerrain_FindEdgeDifferences, terrain, static_cast<void*>(&found), 0u);
    uint32_t checksum = EdgeSetChecksum(found, walked);
    log::Write("terrain edges test: search without the empty places: %u vertices (%u walked)", found.count, walked);
    Note("FindEdgeDifferences(set, 0)", found.count, checksum, terrain);
    EmptyEdgeSet(found);

    At641<void>(addr::cSTETerrain_FindEdgeDifferences, terrain, static_cast<void*>(&found), 1u);
    checksum = EdgeSetChecksum(found, walked);
    log::Write("terrain edges test: search with the empty places: %u vertices (%u walked)", found.count, walked);
    Note("FindEdgeDifferences(set, 1)", found.count, checksum, terrain);

    HashSet vertices;
    MakeHashSet(vertices);
    HashSet cells;
    MakeHashSet(cells);
    At641<void>(addr::cSTETerrain_CollectEdgeChanges, terrain, static_cast<void*>(&found), static_cast<void*>(&cells),
                static_cast<void*>(&vertices));
    checksum = HashSetChecksum(cells, walked);
    log::Write("terrain edges test: collected %u cells (%u walked) and %u vertices", cells.count, walked, vertices.count);
    Note("CollectEdgeChanges: cells", cells.count, checksum, terrain);
    checksum = HashSetChecksum(vertices, walked);
    Note("CollectEdgeChanges: vertices", vertices.count, checksum, terrain);
    DestroyHashSet(cells);
    DestroyHashSet(vertices);
    EmptyEdgeSet(found);
    if (found.head)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(found.head));
}

// --- The leftovers ---------------------------------------------------------------

// A std::deque<uint32_t> of the game (STLport; blocks of 0x80 bytes).
struct IndexDeque {
    uint32_t startCurrent, startFirst, startLast, startNode;
    uint32_t finishCurrent, finishFirst, finishLast, finishNode;
    uint32_t map, mapSize;
};

int32_t DequeSize(const IndexDeque& queue)
{
    return (static_cast<int32_t>(queue.finishNode - queue.startNode) >> 2) * 32 +
           (static_cast<int32_t>(queue.finishCurrent - queue.finishFirst) >> 2) +
           (static_cast<int32_t>(queue.startLast - queue.startCurrent) >> 2) - 0x20;
}

uint32_t DequeAt(const IndexDeque& queue, int32_t i)
{
    const int32_t position = (static_cast<int32_t>(queue.startCurrent - queue.startFirst) >> 2) + i;
    if (position >= 0 && position < 0x20)
        return AtAddress<uint32_t>(queue.startCurrent + 4u * static_cast<uint32_t>(i));
    const int32_t block = position > 0 ? position / 32 : (position + 1) / 32 - 1;
    const uint32_t first = AtAddress<uint32_t>(queue.startNode + 4u * static_cast<uint32_t>(block));
    return AtAddress<uint32_t>(first + 4u * static_cast<uint32_t>(position - block * 32));
}

// As 0x00761CA3 frees one.
void DestroyDeque(IndexDeque& queue)
{
    if (!queue.map)
        return;
    for (uint32_t node = queue.startNode; node < queue.finishNode + 4u; node += 4u)
        if (AtAddress<uint32_t>(node))
            Cdecl<void>(addr::SC4_Free, AtAddress<uint32_t>(node));
    Cdecl<void>(addr::SC4_Free, queue.map);
}

void Rest(char* terrain)
{
    const uint32_t cellsX = U32(terrain, field::kCellsX), cellsZ = U32(terrain, field::kCellsZ);
    const uint32_t vertsX = U32(terrain, field::kVertsX), vertsZ = U32(terrain, field::kVertsZ);
    if (vertsX != cellsX + 1 || vertsZ != cellsZ + 1 || cellsX < 60 || cellsZ < 60) {
        log::Write("terrain edges test: unexpected sizes (%u x %u cells); the leftovers not called", cellsX, cellsZ);
        return;
    }
    Gathered& g = g_edges;

    // The normals and the steep cells of the whole map, as slot 68 asks for
    // them: 0x00743B60 unflips (0x00740900) and marks (0x00742FC0).
    At641<void>(addr::cSTETerrain_PrepareRect, terrain, static_cast<void*>(&At<int32_t>(terrain, field::kCellRect)));
    {
        uint32_t steep = 0, flipped = 0;
        for (uint32_t index = 0; index < U32(terrain, field::kVertexCount); ++index) {
            steep += (Flags(terrain, index) & 0x0008) ? 1u : 0u;
            flipped += (Flags(terrain, index) & 0x0800) ? 1u : 0u;
        }
        Note("PrepareRect(whole): marked steep, flipped", steep, flipped, terrain);
    }

    // The shore marks, and one shore line.
    At641<void>(addr::cSTETerrain_MarkShoreVertices, terrain);
    uint32_t shore = 0, firstShore = 0xFFFFFFFFu;
    for (uint32_t index = 0; index < U32(terrain, field::kVertexCount); ++index) {
        if (!(Flags(terrain, index) & 0x0010))
            continue;
        ++shore;
        if (firstShore == 0xFFFFFFFFu && !(Flags(terrain, index) & 0x0001))
            firstShore = index;
    }
    Note("MarkShoreVertices: shore vertices, the first", shore, firstShore, terrain);
    if (firstShore != 0xFFFFFFFFu) {
        IndexDeque queue{};
        At641<void>(addr::SC4CellDeque_Construct, &queue, 0u);
        At641<void>(addr::cSTETerrain_CollectShore, terrain, firstShore, static_cast<void*>(&queue), 1u);
        const int32_t size = DequeSize(queue);
        Hash line;
        for (int32_t i = 0; i < size && i < 1000000; ++i)
            line.Mix(DequeAt(queue, i));
        Note("CollectShore(first shore vertex, mark 1): length, line", static_cast<uint32_t>(size), line.h, terrain);
        DestroyDeque(queue);
        At641<void>(addr::cSTETerrain_ClearFlagsEverywhere, terrain, 1u);
        uint32_t marked = 0;
        for (uint32_t index = 0; index < U32(terrain, field::kVertexCount); ++index)
            marked += (Flags(terrain, index) & 0x0001) ? 1u : 0u;
        Note("ClearFlagsEverywhere(1): still marked", marked, 0, terrain);
    }

    // The square around a place.
    const int32_t around[5][3] = {{5, 5, 10},
                                  {static_cast<int32_t>(cellsX) - 2, static_cast<int32_t>(cellsZ) - 3, 7},
                                  {static_cast<int32_t>(cellsX / 2), static_cast<int32_t>(cellsZ / 2), 4},
                                  {0, 0, 0},
                                  {static_cast<int32_t>(cellsX), static_cast<int32_t>(cellsZ), 300}};
    for (const auto& place : around) {
        int32_t rect[4] = {-1, -1, -1, -1};
        At641<int32_t*>(addr::cSTETerrain_RectAround, terrain, rect, place[0], place[1], place[2]);
        Hash hash;
        for (int32_t value : rect)
            hash.Mix(static_cast<uint32_t>(value));
        g.hash.Mix(hash.h);
        ++g.calls;
        log::Write("terrain edges test: RectAround(%d, %d, %d): %d,%d,%d,%d", place[0], place[1], place[2], rect[0],
                   rect[1], rect[2], rect[3]);
    }

    // A rectangle widened.
    const int32_t grown[3][5] = {{3, 4, 10, 12, 5},
                                 {static_cast<int32_t>(cellsX) - 4, static_cast<int32_t>(cellsZ) - 4,
                                  static_cast<int32_t>(cellsX) - 1, static_cast<int32_t>(cellsZ) - 1, 6},
                                 {10, 10, 20, 20, -3}};
    for (const auto& one : grown) {
        int32_t rect[4] = {one[0], one[1], one[2], one[3]};
        At641<int32_t*>(addr::cSTETerrain_GrowRect, terrain, rect, one[4]);
        Hash hash;
        for (int32_t value : rect)
            hash.Mix(static_cast<uint32_t>(value));
        g.hash.Mix(hash.h);
        ++g.calls;
        log::Write("terrain edges test: GrowRect(%d,%d,%d,%d by %d): %d,%d,%d,%d", one[0], one[1], one[2], one[3], one[4],
                   rect[0], rect[1], rect[2], rect[3]);
    }

    // One dry, unlocked inland vertex (the first, rows first, 24 or more
    // from every edge) raised and lowered; then put back as it was.
    uint32_t place = 0xFFFFFFFFu;
    for (uint32_t z = 24; z + 24 < vertsZ && place == 0xFFFFFFFFu; ++z) {
        for (uint32_t x = 24; x + 24 < vertsX; ++x) {
            const uint32_t index = vertsX * z + x;
            if ((Flags(terrain, index) & 0x4000) || AtAddress<float>(WaterAt(terrain, index) + water::kDepth) > 0.0f ||
                !(AtAddress<float>(VertexAt(terrain, index)) > At<float>(terrain, field::kSeaLevel)))
                continue;
            place = index;
            break;
        }
    }
    if (place == 0xFFFFFFFFu) {
        log::Write("terrain edges test: no dry inland vertex; RaiseVertex not called");
        return;
    }
    const uint32_t altitude = AtAddress<uint32_t>(VertexAt(terrain, place));
    const uint32_t depth = AtAddress<uint32_t>(WaterAt(terrain, place) + water::kDepth);
    const uint16_t flags = AtAddress<uint16_t>(VertexAt(terrain, place) + vertex::kFlags);
    Note("before RaiseVertex: the vertex, its altitude", place, altitude, terrain);
    At641<void>(addr::cSTETerrain_RaiseVertex, terrain, place, 0.5f);
    Note("RaiseVertex(+0.5)", AtAddress<uint32_t>(VertexAt(terrain, place)),
         AtAddress<uint32_t>(WaterAt(terrain, place) + water::kDepth), terrain);
    At641<void>(addr::cSTETerrain_RaiseVertex, terrain, place, -0.5f);
    Note("RaiseVertex(-0.5)", AtAddress<uint32_t>(VertexAt(terrain, place)),
         AtAddress<uint32_t>(WaterAt(terrain, place) + water::kDepth), terrain);
    At641<void>(addr::cSTETerrain_RaiseVertex, terrain, place, 1000.0f);
    Note("RaiseVertex(+1000)", AtAddress<uint32_t>(VertexAt(terrain, place)),
         AtAddress<uint32_t>(WaterAt(terrain, place) + water::kDepth), terrain);
    AtAddress<uint16_t>(VertexAt(terrain, place) + vertex::kFlags) |= 0x4000;
    At641<void>(addr::cSTETerrain_RaiseVertex, terrain, place, -3.0f);
    Note("RaiseVertex(-3) while locked", AtAddress<uint32_t>(VertexAt(terrain, place)),
         AtAddress<uint32_t>(WaterAt(terrain, place) + water::kDepth), terrain);
    AtAddress<uint32_t>(VertexAt(terrain, place)) = altitude;
    AtAddress<uint32_t>(WaterAt(terrain, place) + water::kDepth) = depth;
    AtAddress<uint16_t>(VertexAt(terrain, place) + vertex::kFlags) = flags;
    Note("the vertex put back", place, altitude, terrain);
}

void Day_(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_TERRAIN_EDGES"))
        return;
    char* const terrain = LiveTerrain();
    if (!terrain) {
        log::Write("terrain edges test: no terrain; nothing done");
        return;
    }
    g_edges.ran = true;
    Note("before", 0, 0, terrain);
    Edges(terrain);
    Rest(terrain);
    log::Write("terrain edges test: %u lines noted, checksum %08X", g_edges.calls, g_edges.hash.h);
}

// With the switch set: g<checksum>/<lines> once the test ran.
void Fingerprint(cISC4City*, std::string& line)
{
    if (EnvInt("OPENSC4_TEST_TERRAIN_EDGES", -1) < 0)
        return;
    if (!LiveTerrain()) {
        line += " | terrainedges none";
        return;
    }
    line += " | terrainedges";
    if (g_edges.ran) {
        char text[64];
        std::snprintf(text, sizeof text, " g%08X/%u", g_edges.hash.h, g_edges.calls);
        line += text;
    }
}

const DayHook kEdges{"terrainedges", 119, &Day_};
const FingerprintPart kFingerprint{"terrainedges", 120, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
