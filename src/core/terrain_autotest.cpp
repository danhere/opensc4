// The terrain's part of the autotest (src/core/autotest_hooks.h): the state
// of the city's terrain object (cSTETerrain, the service 0x00B43CF4; fields
// in terrain_sim.h) read from its memory, and with OPENSC4_TEST_TERRAIN=1 a
// comparison, inside the run, of every query slot with the game's own
// function.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/terrain_sim.h"

#include <opensc4/addresses.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace opensc4::autotest {
namespace {

using terrain_sim::AtAddress;
using terrain_sim::Bits;
using terrain_sim::RecordAddress;
namespace field = terrain_sim::field;
namespace vertex = terrain_sim::vertex;
namespace water = terrain_sim::water;

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

// The game's own functions behind the terrain's 98 slots (vtable
// 0x00AB3D18), to ask them the same questions as whatever the object's
// vtable holds now.
constexpr std::uintptr_t kGameSlots[terrain_sim::kSlotCount] = {
    0x00741E90, 0x004F7BA0, 0x00741F70, 0x0074FEA0, 0x007435C0, 0x005B9D30, 0x0074C5C0, 0x0060FB80,
    0x004ABAD0, 0x0074C5D0, 0x0074C5E0, 0x0074C610, 0x007413C0, 0x00741260, 0x007413E0, 0x00742A00,
    0x007443C0, 0x007415F0, 0x00744620, 0x00741420, 0x007414A0, 0x0074C670, 0x0074C680, 0x0074C690,
    0x0074C6A0, 0x0074C6B0, 0x00742B10, 0x00742CF0, 0x007417D0, 0x00741760, 0x007449B0, 0x00744980,
    0x007444E0, 0x00744AB0, 0x00743FA0, 0x00744030, 0x00747680, 0x007476D0, 0x00741B30, 0x00740750,
    0x00749540, 0x00749A00, 0x0040CF10, 0x0074A220, 0x0074C0F0, 0x00743550, 0x00740850, 0x0074C660,
    0x00746440, 0x00744F10, 0x00744E60, 0x00744F90, 0x00741F90, 0x00744B40, 0x0074C6E0, 0x007448C0,
    0x00747720, 0x0074F0E0, 0x007415D0, 0x00747A30, 0x00747BB0, 0x00747AE0, 0x00747C90, 0x00747D60,
    0x0074BAD0, 0x00747510, 0x00741810, 0x00742930, 0x00749850, 0x00743460, 0x00741C20, 0x007418F0,
    0x00741830, 0x00741890, 0x00741AB0, 0x00741AF0, 0x00741C60, 0x00741CB0, 0x007420B0, 0x00749A70,
    0x0074C6F0, 0x00742000, 0x00741770, 0x00741740, 0x007434E0, 0x00741750, 0x00741FF0, 0x00743B40,
    0x007443A0, 0x0074ED40, 0x00742150, 0x0074DB90, 0x00744A80, 0x00741180, 0x007411F0, 0x00742170,
    0x00747800, 0x007477C0,
};

// A slot of the object's vtable as it is now (ours when the group is
// installed), or the game's function for the same slot.
template <typename R, typename... Args>
R Call(bool game, char* terrain, int slot, Args... args)
{
    using Fn = R(__thiscall*)(void*, Args...);
    const auto fn = game ? reinterpret_cast<Fn>(kGameSlots[slot])
                         : reinterpret_cast<Fn>((*reinterpret_cast<void***>(terrain))[slot]);
    return fn(terrain, args...);
}

// The cell width the game's code uses: the float 16.0 at 0x00AB3B5C (.rdata).
constexpr uint32_t kCellWidthAddress = 0x00AB3B5C;

struct Tally {
    uint32_t asked = 0;
    uint32_t agreed = 0;
    int logged = 0;
};

uint32_t ArgBits(float value) { return Bits(value); }
uint32_t ArgBits(uint32_t value) { return value; }
uint32_t ArgBits(int32_t value) { return static_cast<uint32_t>(value); }
template <typename T>
uint32_t ArgBits(T*)
{
    return 0; // an address: not logged
}

template <typename... Args>
std::string Describe(Args... args)
{
    std::string text;
    char part[16];
    const uint32_t bits[] = {0u, ArgBits(args)...};
    for (std::size_t i = 1; i < sizeof bits / sizeof bits[0]; ++i) {
        std::snprintf(part, sizeof part, " %08X", bits[i]);
        text += part;
    }
    return text;
}

void Count(Tally& tally, int slot, bool same, const std::string& arguments, uint64_t ours, uint64_t game)
{
    ++tally.asked;
    if (same) {
        ++tally.agreed;
        return;
    }
    if (tally.logged++ < 12)
        log::Write("terrain test: slot %d (%s ) differs: ours %08X%08X, the game's %08X%08X", slot,
                   arguments.c_str(), static_cast<unsigned>(ours >> 32), static_cast<unsigned>(ours),
                   static_cast<unsigned>(game >> 32), static_cast<unsigned>(game));
}

template <typename R>
uint64_t Raw(R value)
{
    uint64_t raw = 0;
    std::memcpy(&raw, &value, sizeof value);
    return raw;
}

// A slot whose result is a value: R is double for one returned in ST0 (the
// bits of the double are compared, so an unrounded result must match to the
// last bit), uint32_t for EAX, uint8_t for AL, int64_t for EDX:EAX.
template <typename R, typename... Args>
void Ask(Tally& tally, char* terrain, int slot, Args... args)
{
    const uint64_t ours = Raw(Call<R>(false, terrain, slot, args...));
    const uint64_t game = Raw(Call<R>(true, terrain, slot, args...));
    Count(tally, slot, ours == game, Describe(args...), ours, game);
}

// A slot that writes `count` dwords through its last argument (and whose
// EAX means nothing): both start from the same sentinel values.
template <typename... Args>
void AskOut(Tally& tally, char* terrain, int slot, int count, Args... args)
{
    uint32_t ours[4], game[4];
    for (int i = 0; i < 4; ++i)
        ours[i] = game[i] = 0xC0DE0000u + static_cast<uint32_t>(i);
    Call<void>(false, terrain, slot, args..., static_cast<uint32_t*>(ours));
    Call<void>(true, terrain, slot, args..., static_cast<uint32_t*>(game));
    const bool same = std::memcmp(ours, game, sizeof(uint32_t) * static_cast<std::size_t>(count)) == 0;
    Count(tally, slot, same, Describe(args...), (static_cast<uint64_t>(ours[1]) << 32) | ours[0],
          (static_cast<uint64_t>(game[1]) << 32) | game[0]);
}

struct Random {
    uint32_t state;
    uint32_t Next()
    {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
};

// OPENSC4_TEST_TERRAIN=1: every slot of simulators.terrain.queries asked of
// the object's vtable and of the game's function, results compared bit for
// bit (ST0 results as doubles). Pure queries only; nothing is passed that
// would make the GAME read outside its arrays:
//   - no arguments: slots 5 to 8, 21 to 24, 29, 42, 47, 54, 80, 83, 85;
//   - vertex and cell coordinates (9, 14, 17, 18, 28, 33 to 38, 70, 74 to
//     76, 82): every pair on the grid's edges, an eighth of the others by a
//     hash that changes daily, and pairs outside on every side (cells + 1,
//     cells + 2, -1, 0x80000000, 0x7FFFFFFF): these slots all test the range;
//   - vertex indices (10, 12, 31, 92): a sample, the last ones and outside;
//   - positions (11, 13, 19, 25, 30, 55, 77; 26, 27, 32 where the game's
//     cell is inside the grid): all pairs of a list of special values
//     (negative, -0, 0, a denormal, around the first vertices, around the
//     far edge, the far edge itself, beyond it, 1e9, 3e30) and 4,000
//     positions a day: on vertices, on vertex lines, on the cells' diagonals
//     and anti-diagonals (the ties of the triangle tests), on half cells
//     (the ties of the rounding) and anywhere. Slots 27 and 32 are not asked
//     on the far edges themselves (x = width or z = depth): the game reads
//     past its arrays there. No NaN is passed (slot 26 lets one through its
//     range test).
//   - rectangles (20, 39: inside, empty, reversed and over each edge; 53,
//     15, 16, which test nothing: inside only, reversed ones included; 15
//     and 16 into buffers with three layouts, whole buffers compared);
//   - brushes (58 for types 0 to 7; 61 to 63 for the seven IDs and two
//     unknown ones).
void Queries(char* terrain, int32_t day, Tally& tally)
{
    const uint32_t vx = At<uint32_t>(terrain, field::kVertsX);
    const uint32_t vz = At<uint32_t>(terrain, field::kVertsZ);
    const uint32_t cellsX = At<uint32_t>(terrain, field::kCellsX);
    const uint32_t cellsZ = At<uint32_t>(terrain, field::kCellsZ);
    const uint32_t count = At<uint32_t>(terrain, field::kVertexCount);
    const float sizeX = At<float>(terrain, field::kSizeX);
    const float sizeZ = At<float>(terrain, field::kSizeZ);
    const float inverse = terrain_sim::InverseCellWidth();
    // The sizes the arguments below are made safe for.
    if (cellsX < 4 || cellsZ < 4 || cellsX > 1024 || cellsZ > 1024 || vx != cellsX + 1 || vz != cellsZ + 1 ||
        count != vx * vz || inverse != 0.0625f || sizeX != static_cast<float>(cellsX * 16) ||
        sizeZ != static_cast<float>(cellsZ * 16)) {
        log::Write("terrain test: unexpected sizes (%u x %u cells, %u x %u vertices); nothing asked", cellsX,
                   cellsZ, vx, vz);
        return;
    }
    Random random{static_cast<uint32_t>(day) * 2654435761u + 12345u};

    for (const int slot : {6, 21, 22, 23, 24, 29, 47})
        Ask<double>(tally, terrain, slot);
    for (const int slot : {7, 8, 42, 54})
        Ask<uint32_t>(tally, terrain, slot);
    for (const int slot : {5, 80, 83, 85})
        Ask<uint8_t>(tally, terrain, slot);

    // Vertex and cell coordinates.
    std::vector<uint32_t> xs, zs;
    for (uint32_t x = 0; x < vx + 2; ++x)
        xs.push_back(x);
    for (uint32_t z = 0; z < vz + 2; ++z)
        zs.push_back(z);
    for (const uint32_t outside : {0xFFFFFFFFu, 0x80000000u, 0x7FFFFFFFu, 0xFFFFFF00u}) {
        xs.push_back(outside);
        zs.push_back(outside);
    }
    const uint32_t salt = static_cast<uint32_t>(day) * 40503u;
    for (const uint32_t x : xs)
        for (const uint32_t z : zs) {
            const bool inside = x < vx && z < vz;
            const bool edge = x < 2 || z < 2 || x + 3 > vx || z + 3 > vz;
            if (inside && !edge && ((x * 73856093u) ^ (z * 19349663u) ^ salt) % 8u != 0)
                continue;
            // Far outside, a sample of the rows and columns is enough.
            if (!inside && x >= vx + 2 && z < vz && z % 16u != 0)
                continue;
            if (!inside && z >= vz + 2 && x < vx && x % 16u != 0)
                continue;
            Ask<uint32_t>(tally, terrain, 9, x, z);
            Ask<double>(tally, terrain, 14, x, z);
            AskOut(tally, terrain, 17, 4, x, z);
            Ask<double>(tally, terrain, 18, x, z);
            AskOut(tally, terrain, 28, 3, x, z);
            Ask<double>(tally, terrain, 33, x, z);
            for (const int slot : {34, 35, 36, 37, 38, 70, 74, 75, 76})
                Ask<uint8_t>(tally, terrain, slot, x, z);
            Ask<uint32_t>(tally, terrain, 82, x, z);
        }

    // Vertex indices.
    std::vector<uint32_t> indices;
    for (uint32_t index = 0; index < count; ++index)
        if (index < 4 || index + 4 > count || ((index * 2246822519u) ^ salt) % 16u == 0)
            indices.push_back(index);
    for (const uint32_t outside : {count, count + 1, count + vx, 0xFFFFFFFFu, 0x80000000u, 0x7FFFFFFFu})
        indices.push_back(outside);
    for (const uint32_t index : indices) {
        Ask<double>(tally, terrain, 12, index);
        Ask<double>(tally, terrain, 31, index);
        Ask<double>(tally, terrain, 92, index);
        uint32_t ours[2] = {0xC0DE0001u, 0xC0DE0002u}, game[2] = {0xC0DE0001u, 0xC0DE0002u};
        Call<void>(false, terrain, 10, index, &ours[0], &ours[1]);
        Call<void>(true, terrain, 10, index, &game[0], &game[1]);
        Count(tally, 10, ours[0] == game[0] && ours[1] == game[1], Describe(index),
              (static_cast<uint64_t>(ours[1]) << 32) | ours[0], (static_cast<uint64_t>(game[1]) << 32) | game[0]);
    }

    // Positions.
    struct Position {
        float x, z;
    };
    std::vector<Position> positions;
    {
        std::vector<float> xSpecial, zSpecial;
        for (int axis = 0; axis < 2; ++axis) {
            const float size = axis == 0 ? sizeX : sizeZ;
            float beyond = size;
            uint32_t bits = Bits(size) + 1u;
            std::memcpy(&beyond, &bits, sizeof beyond);
            float within = size;
            bits = Bits(size) - 1u;
            std::memcpy(&within, &bits, sizeof within);
            std::vector<float>& list = axis == 0 ? xSpecial : zSpecial;
            list = {-1.0e9f,       -16.0f,       -0.001f,       -0.0f,          0.0f,          1.0e-40f,
                    0.001f,        7.999f,       8.0f,          15.999f,        16.0f,         16.001f,
                    24.0f,         size * 0.5f,  size - 16.001f, size - 16.0f,  size - 8.0f,   within,
                    size,          beyond,       size + 0.001f, size + 16.0f,   size * 2.0f,   1.0e9f,
                    3.0e30f};
        }
        for (const float x : xSpecial)
            for (const float z : zSpecial)
                positions.push_back({x, z});
        for (int i = 0; i < 4000; ++i) {
            const uint32_t cx = random.Next() % cellsX, cz = random.Next() % cellsZ;
            const float fx = static_cast<float>(random.Next() & 0xFFFF) / 4096.0f; // 0 to 16 in 4096ths
            const float fz = static_cast<float>(random.Next() & 0xFFFF) / 4096.0f;
            const float baseX = static_cast<float>(cx * 16), baseZ = static_cast<float>(cz * 16);
            switch (i % 8) {
            case 0: // on a vertex
                positions.push_back({baseX, baseZ});
                break;
            case 1: // on a vertex line
                positions.push_back({baseX, baseZ + fz});
                break;
            case 2:
                positions.push_back({baseX + fx, baseZ});
                break;
            case 3: // on the diagonal
                positions.push_back({baseX + fx, baseZ + fx});
                break;
            case 4: // on the anti-diagonal
                positions.push_back({baseX + fx, baseZ + (16.0f - fx)});
                break;
            case 5: // on half cells
                positions.push_back({baseX + 8.0f, (i & 8) ? baseZ + 8.0f : baseZ + fz});
                break;
            case 6: // anywhere, with all the bits a float has there
                positions.push_back({baseX + static_cast<float>(random.Next()) / 1048576.0f,
                                     baseZ + static_cast<float>(random.Next()) / 1048576.0f});
                break;
            default:
                positions.push_back({baseX + fx, baseZ + fz});
                break;
            }
        }
    }
    for (const Position& position : positions) {
        const float x = position.x, z = position.z;
        Ask<uint8_t>(tally, terrain, 11, x, z);
        Ask<double>(tally, terrain, 13, x, z);
        Ask<double>(tally, terrain, 19, x, z);
        Ask<uint32_t>(tally, terrain, 25, x, z);
        Ask<double>(tally, terrain, 30, x, z);
        Ask<double>(tally, terrain, 55, x, z);
        Ask<uint8_t>(tally, terrain, 77, x, z);
        // The game's cell for the position, as slots 26, 27 and 32 take it
        // (they do not clamp it): asked only when its four corners exist.
        const double tx = static_cast<double>(inverse) * static_cast<double>(x);
        const double tz = static_cast<double>(static_cast<float>(static_cast<double>(inverse) * static_cast<double>(z)));
        const bool outside = !(x >= 0.0f && x <= sizeX && z >= 0.0f && z <= sizeZ);
        const bool cellInside = tx < static_cast<double>(cellsX) && tz < static_cast<double>(cellsZ);
        if (outside || cellInside) {
            AskOut(tally, terrain, 26, 3, x, z);
            AskOut(tally, terrain, 27, 3, x, z);
            Ask<double>(tally, terrain, 32, x, z);
        }
    }

    // Rectangles.
    struct Rect {
        int32_t x0, z0, x1, z1;
    };
    std::vector<Rect> insideRects, anyRects;
    const int32_t lastX = static_cast<int32_t>(vx) - 1, lastZ = static_cast<int32_t>(vz) - 1;
    insideRects.push_back({0, 0, lastX, lastZ});
    insideRects.push_back({0, 0, 0, 0});
    insideRects.push_back({lastX, lastZ, lastX, lastZ});
    insideRects.push_back({0, lastZ, lastX, lastZ});
    insideRects.push_back({lastX, 0, lastX, lastZ});
    insideRects.push_back({5, 5, 4, 9});  // reversed in x
    insideRects.push_back({5, 9, 9, 5});  // reversed in z
    insideRects.push_back({9, 9, 5, 5});
    for (int i = 0; i < 300; ++i) {
        const int32_t x0 = static_cast<int32_t>(random.Next() % vx), z0 = static_cast<int32_t>(random.Next() % vz);
        int32_t x1 = x0 + static_cast<int32_t>(random.Next() % 14u) - 1;
        int32_t z1 = z0 + static_cast<int32_t>(random.Next() % 14u) - 1;
        if (x1 < 0)
            x1 = 0;
        if (z1 < 0)
            z1 = 0;
        if (x1 > lastX)
            x1 = lastX;
        if (z1 > lastZ)
            z1 = lastZ;
        insideRects.push_back({x0, z0, x1, z1});
    }
    anyRects = insideRects;
    for (const int32_t out : {-1, static_cast<int32_t>(vx), static_cast<int32_t>(vx) + 5, 0x7FFFFFFF,
                              static_cast<int32_t>(0x80000000u)}) {
        anyRects.push_back({out, 3, 8, 6});
        anyRects.push_back({3, out, 8, 6});
        anyRects.push_back({3, 3, out, 6});
        anyRects.push_back({3, 3, 8, out});
        anyRects.push_back({out, out, out, out});
    }
    // A value slot taking a rectangle, with the rectangle in the log.
    const auto AskRect = [&tally](int slot, const Rect& rect, uint64_t ours, uint64_t game) {
        Count(tally, slot, ours == game, Describe(rect.x0, rect.z0, rect.x1, rect.z1), ours, game);
    };
    for (const Rect& rect : anyRects) {
        AskRect(20, rect, Raw(Call<double>(false, terrain, 20, &rect.x0)), Raw(Call<double>(true, terrain, 20, &rect.x0)));
        AskRect(39, rect, Raw(Call<uint8_t>(false, terrain, 39, &rect.x0)), Raw(Call<uint8_t>(true, terrain, 39, &rect.x0)));
    }
    std::vector<uint32_t> oursOut(2048), gameOut(2048);
    for (const Rect& rect : insideRects) {
        AskRect(53, rect, Raw(Call<uint32_t>(false, terrain, 53, &rect.x0)),
                Raw(Call<uint32_t>(true, terrain, 53, &rect.x0)));
        const int32_t dx = rect.x1 - rect.x0, dz = rect.z1 - rect.z0;
        if (dx > 15 || dz > 15)
            continue;
        const int32_t columns = dx < 0 ? 1 : dx + 1, rows = dz < 0 ? 1 : dz + 1;
        const int32_t layouts[3][2] = {{columns, 1}, {1, rows}, {columns * 2 + 3, 2}};
        for (const int slot : {15, 16})
            for (const auto& layout : layouts) {
                for (std::size_t i = 0; i < oursOut.size(); ++i)
                    oursOut[i] = gameOut[i] = 0xC0DE0000u + static_cast<uint32_t>(i);
                Call<void>(false, terrain, slot, &rect.x0, oursOut.data(), layout[0], layout[1]);
                Call<void>(true, terrain, slot, &rect.x0, gameOut.data(), layout[0], layout[1]);
                Count(tally, slot, oursOut == gameOut, Describe(rect.x0, rect.z0, rect.x1, rect.z1, layout[0], layout[1]),
                      0, 0);
            }
    }

    // Brushes.
    for (uint32_t type = 0; type < 8; ++type) {
        uint32_t ours = 0xC0DE0000u, game = 0xC0DE0000u;
        const uint32_t oursResult = Call<uint32_t>(false, terrain, 58, type, &ours);
        const uint32_t gameResult = Call<uint32_t>(true, terrain, 58, type, &game);
        Count(tally, 58, ours == game && (oursResult & 0xFF) == (gameResult & 0xFF), Describe(type),
              (static_cast<uint64_t>(oursResult) << 32) | ours, (static_cast<uint64_t>(gameResult) << 32) | game);
        for (const uint32_t brush : {game, type == 0 ? 0u : 0x12345678u + type}) {
            Ask<double>(tally, terrain, 61, brush);
            Ask<int64_t>(tally, terrain, 62, brush);
            uint32_t oursElements[10], gameElements[10];
            for (int i = 0; i < 10; ++i)
                oursElements[i] = gameElements[i] = 0xC0DE0000u + static_cast<uint32_t>(i);
            const uint8_t oursFound = Call<uint8_t>(false, terrain, 63, brush, static_cast<uint32_t*>(oursElements));
            const uint8_t gameFound = Call<uint8_t>(true, terrain, 63, brush, static_cast<uint32_t*>(gameElements));
            Count(tally, 63, oursFound == gameFound && std::memcmp(oursElements, gameElements, sizeof oursElements) == 0,
                  Describe(brush), oursFound, gameFound);
        }
    }
}

// The terrain (build 641): the vertex grid's size, the cell width, the sea
// level, the minimum and maximum height, and checksums over every vertex of
// the altitudes (a), the flags words (f: hidden triangles, flipped diagonals,
// locks), the water depths and sediment values of the water object's records
// (w) and the vertex normals (n), then the count of water vertices (v); all
// read from the object's memory, not through its slots.
// OPENSC4_TEST_TERRAIN=1 adds q<agreeing>/<asked> of Queries above.
void Fingerprint(cISC4City*, std::string& line)
{
    char* const terrain = game::IsBuild641() ? Global<char*>(addr::SC4_Terrain) : nullptr;
    if (!terrain || !At<uint8_t>(terrain, field::kInitialized) || !At<uint32_t>(terrain, field::kVertices) ||
        !At<void*>(terrain, field::kWater) || !At<uint32_t>(At<void*>(terrain, field::kWater), water::kRecords)) {
        line += " | terrain none";
        return;
    }
    const uint32_t count = At<uint32_t>(terrain, field::kVertexCount);
    const uint32_t vertices = terrain_sim::VertexBase(terrain);
    const uint32_t records = terrain_sim::WaterBase(terrain);
    Hash altitudes, flags, wet, normals;
    for (uint32_t index = 0; index < count; ++index) {
        const uint32_t at = RecordAddress(vertices, index);
        altitudes.Mix(AtAddress<uint32_t>(at + vertex::kAltitude));
        flags.Mix(AtAddress<uint16_t>(at + vertex::kFlags));
        for (uint32_t i = 0; i < 3; ++i)
            normals.Mix(AtAddress<uint32_t>(at + vertex::kNormal + 4 * i));
        const uint32_t record = RecordAddress(records, index);
        wet.Mix(AtAddress<uint32_t>(record + water::kDepth));
        wet.Mix(AtAddress<uint32_t>(record + water::kSediment));
    }
    char text[200];
    std::snprintf(text, sizeof text, " | terrain %ux%u c%08X s%08X m%08X,%08X a%08X f%08X w%08X n%08X v%u",
                  At<uint32_t>(terrain, field::kVertsX), At<uint32_t>(terrain, field::kVertsZ),
                  AtAddress<uint32_t>(kCellWidthAddress),
                  At<uint32_t>(terrain, field::kSeaLevel), At<uint32_t>(terrain, field::kMinHeight),
                  At<uint32_t>(terrain, field::kMaxHeight), altitudes.h, flags.h, wet.h, normals.h,
                  At<uint32_t>(terrain, field::kWaterVertices));
    line += text;
    if (EnvInt("OPENSC4_TEST_TERRAIN", 0) == 1) {
        static int32_t day = 0;
        Tally tally;
        Queries(terrain, ++day, tally);
        std::snprintf(text, sizeof text, " q%u/%u", tally.agreed, tally.asked);
        line += text;
    }
}

const FingerprintPart kFingerprint{"terrain", 112, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
