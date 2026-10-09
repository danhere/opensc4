// Our cSC4WeatherSimulator (build 641), third part: the water bodies (found
// in January, kept when they touch the city's edge, marked into the water
// bits) and Init's random manual moisture, each a replacement on the
// original object (docs/weather.md, "Water bodies").
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/weather_sim.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::weather_sim {
namespace {

using FreeFn = void(__cdecl*)(void* p);
using AllocateFn = void*(__cdecl*)(std::size_t size);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);
const auto Allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);

struct Body {
    int32_t x0, z0, x1, z1;
};

struct BodyVector {
    Body* begin;
    Body* end;
    Body* capacity;
};

// push_back as the game's vector does (capacity grows by max(size, 1)).
void PushBack(BodyVector& v, const Body& body)
{
    if (v.end != v.capacity) {
        *v.end++ = body;
        return;
    }
    const uint32_t size = static_cast<uint32_t>(v.end - v.begin);
    const uint32_t grown = size + (size < 1 ? 1 : size);
    auto* const fresh = grown ? static_cast<Body*>(Allocate(grown * sizeof(Body))) : nullptr;
    Body* out = fresh;
    for (Body* in = v.begin; in != v.end; ++in, ++out)
        *out = *in;
    *out = body;
    if (v.begin)
        Free(v.begin);
    v.begin = fresh;
    v.end = out + 1;
    v.capacity = fresh + grown;
}

// The terrain height at a position as a float (cells times their width,
// converted as unsigned values).
double HeightAt(void* terrain, uint32_t x, uint32_t z)
{
    return Virtual<double>(terrain, 0x34, static_cast<float>(x), static_cast<float>(z));
}

// 0x007283D0: rectangles of cells below the water level, grown along x
// then z, at least `minimum` by `maximum` (either way round); marked in a
// 256 by 256 scratch so no cell is taken twice. True when any was found.
uint8_t __cdecl FindWaterBodies(void* terrain, BodyVector* bodies, uint32_t a, uint32_t b, uint32_t cellsX,
                                uint32_t cellsZ, uint32_t widthX, uint32_t widthZ, const int32_t* rect)
{
    static bool logged = false;
    FirstCall("FindWaterBodies", logged);
    static uint8_t scratch[256][256];
    const uint32_t lo = b < a ? b : a;
    const uint32_t hi = a < b ? b : a;
    uint32_t xStart, zStart, xEnd, zEnd;
    if (rect) {
        xStart = static_cast<uint32_t>(rect[0]);
        zStart = static_cast<uint32_t>(rect[1]);
        xEnd = static_cast<uint32_t>(rect[2]);
        zEnd = static_cast<uint32_t>(rect[3]);
    } else {
        xStart = 0;
        zStart = 0;
        xEnd = cellsX - lo + 1;
        zEnd = cellsZ - lo + 1;
    }
    const float level = static_cast<float>(Virtual<double>(terrain, 0x74));
    bodies->end = bodies->begin;
    for (uint32_t x = xStart; x < cellsX; ++x)
        if (xStart < cellsZ)
            std::memset(&scratch[x][xStart], 0, cellsZ - xStart);
    for (uint32_t z = zStart; z < zEnd; ++z) {
        const uint32_t zMetres = z * widthZ;
        for (uint32_t x = xStart; x < xEnd; ++x) {
            if (scratch[x][z])
                continue;
            if (!(HeightAt(terrain, x * widthX, zMetres) <= level))
                continue;
            // Along x from here while the cells are water and free.
            uint32_t x1 = x;
            while (x1 < cellsX && !scratch[x1][z] && HeightAt(terrain, x1 * widthX, zMetres) <= level)
                ++x1;
            if (x1 - x < lo)
                continue;
            // Then along z, the x range shrinking to the water in each row.
            uint32_t bodyX0 = x, bodyX1 = x1, zEndBody = cellsZ;
            uint32_t rowX0 = x, rowX1 = x1;
            for (uint32_t z1 = z; z1 < cellsZ; ++z1) {
                const uint32_t z1Metres = z1 * widthZ;
                bool found = false;
                for (uint32_t cx = rowX0; cx < rowX1; ++cx) {
                    if (HeightAt(terrain, cx * widthX, z1Metres) <= level && !scratch[cx][z1]) {
                        found = true;
                        continue;
                    }
                    if (found) {
                        rowX1 = cx;
                        break;
                    }
                    ++rowX0;
                }
                if (rowX1 - rowX0 < lo) {
                    zEndBody = z1;
                    rowX0 = bodyX0;
                    rowX1 = bodyX1;
                    break;
                }
                bodyX0 = rowX0;
                bodyX1 = rowX1;
            }
            const uint32_t depth = zEndBody - z;
            const uint32_t width = rowX1 - rowX0;
            const uint32_t smaller = depth < width ? depth : width;
            const uint32_t larger = width < depth ? depth : width;
            if (smaller < lo || larger < hi)
                continue;
            for (uint32_t cx = rowX0; cx < rowX1; ++cx)
                if (z < zEndBody)
                    std::memset(&scratch[cx][z], 1, zEndBody - z);
            PushBack(*bodies, {static_cast<int32_t>(rowX0), static_cast<int32_t>(z), static_cast<int32_t>(rowX1),
                               static_cast<int32_t>(zEndBody)});
        }
    }
    return bodies->begin != bodies->end;
}

// 0x00728220: only the bodies that touch the city's edge are kept.
void __fastcall KeepEdgeBodies(void* self, void*, BodyVector* bodies)
{
    static bool logged = false;
    FirstCall("KeepEdgeBodies", logged);
    using AssignFn = void(__thiscall*)(BodyVector* target, const BodyVector* source);
    BodyVector kept{};
    for (Body* b = bodies->begin; b != bodies->end; ++b) {
        if (b->x0 > 0 && b->z0 > 0 && b->x1 < At<int32_t>(self, field::kCellsX) - 1 &&
            b->z1 < At<int32_t>(self, field::kCellsZ) - 1)
            continue;
        PushBack(kept, *b);
    }
    const uint32_t before = static_cast<uint32_t>(reinterpret_cast<char*>(bodies->end) -
                                                  reinterpret_cast<char*>(bodies->begin));
    const uint32_t after = static_cast<uint32_t>(reinterpret_cast<char*>(kept.end) -
                                                 reinterpret_cast<char*>(kept.begin));
    if ((before ^ after) & 0xFFFFFFF0u)
        reinterpret_cast<AssignFn>(addr::cSC4WeatherSimulator_AssignBodies)(bodies, &kept);
    if (kept.begin)
        Free(kept.begin);
}

void SetBit(void* rows, int32_t x, int32_t z, bool on)
{
    uint32_t mask;
    uint32_t* const word = BitWord(rows, x, z, mask);
    *word = on ? (*word | mask) : (*word & ~mask);
}

// 0x00726510: the water bits: cleared, then each body grown by +0xCC and
// clamped to the city (one entirely outside marks cell 0, 0).
void __fastcall MarkWaterCells(void* self, void*, const BodyVector* bodies, void* bitRows)
{
    static bool logged = false;
    FirstCall("MarkWaterCells", logged);
    const int32_t lastX = At<int32_t>(self, field::kCellsX) - 1;
    const int32_t lastZ = At<int32_t>(self, field::kCellsZ) - 1;
    for (int32_t x = 0; x < At<int32_t>(self, field::kCellsX); ++x)
        for (int32_t z = 0; z < At<int32_t>(self, field::kCellsZ); ++z)
            SetBit(At<void*>(self, field::kWaterBits), x, z, false);
    for (const Body* b = bodies->begin; b != bodies->end; ++b) {
        const int32_t margin = At<int32_t>(self, field::kBodyMargin);
        int32_t x0 = b->x0 - margin, z0 = b->z0 - margin, x1 = b->x1 + margin, z1 = b->z1 + margin;
        if (x1 < 0 || x0 > lastX || z1 < 0 || z0 > lastZ) {
            x0 = z0 = x1 = z1 = 0;
        } else {
            if (x0 <= 0)
                x0 = 0;
            if (z0 <= 0)
                z0 = 0;
            if (x1 >= lastX)
                x1 = lastX;
            if (z1 >= lastZ)
                z1 = lastZ;
            if (x0 > x1)
                continue;
        }
        for (int32_t x = x0; x <= x1; ++x)
            for (int32_t z = z0; z <= z1; ++z)
                SetBit(*static_cast<void**>(bitRows), x, z, true);
    }
}

// 0x00726400: the manual moisture seeded at Init: a random walk (-10..10)
// between the climate's limits (60 and 100 by default), each cell averaged
// with the one above it.
void __fastcall SeedCellMoisture(void* self, void*)
{
    static bool logged = false;
    FirstCall("SeedCellMoisture", logged);
    using RangeFn = int32_t(__thiscall*)(void* random, int32_t low, int32_t high);
    uint8_t limits[2] = {60, 100};
    if (void* const climate = Virtual<void*>(Global<void*>(0x00B43C94), 0x8C))
        Virtual<uint32_t>(climate, 0x54, &limits[0], &limits[1]);
    uint32_t value = (static_cast<uint32_t>(limits[0]) + limits[1]) / 2;
    char* const rows = static_cast<char*>(self);
    for (int32_t x = 0; x < At<int32_t>(self, field::kCellsX); ++x) {
        uint8_t* const row = *reinterpret_cast<uint8_t**>(At<char*>(rows, field::kManual) + 12 * x);
        for (int32_t z = 0; z < At<int32_t>(self, field::kCellsZ); ++z) {
            uint8_t v = static_cast<uint8_t>(value);
            if (x > 0) {
                const uint8_t* const above = *reinterpret_cast<uint8_t**>(At<char*>(rows, field::kManual) + 12 * (x - 1));
                v = static_cast<uint8_t>((static_cast<int32_t>(above[z]) + v) / 2);
            }
            const int32_t step = reinterpret_cast<RangeFn>(addr::SC4Random_Range)(rows + field::kRandom, -10, 10);
            const int32_t next = static_cast<int32_t>(v) + step;
            if (next < limits[0])
                v = limits[0];
            else if (next > limits[1])
                v = limits[1];
            else
                v = static_cast<uint8_t>(v + step);
            row[z] = v;
            value = (value & ~0xFFu) | v;
        }
    }
}

void* g_original[4];

bool InstallBodies(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4WeatherSimulator_FindWaterBodies, reinterpret_cast<void*>(&FindWaterBodies)},
        {addr::cSC4WeatherSimulator_KeepEdgeBodies, reinterpret_cast<void*>(&KeepEdgeBodies)},
        {addr::cSC4WeatherSimulator_MarkWaterCells, reinterpret_cast<void*>(&MarkWaterCells)},
        {addr::cSC4WeatherSimulator_SeedCellMoisture, reinterpret_cast<void*>(&SeedCellMoisture)},
    };
    int i = 0;
    for (const auto& h : hooks) {
        const addr::Entry* entry = addr_check::Find(h.address);
        if (!entry || !hook::Install(*entry, h.ours, &g_original[i++]))
            return false;
    }
    return true;
}

const bool g_registeredBodies = registry::Register({
    "simulators.weather.bodies",
    "simulators/weather",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4WeatherSimulator_FindWaterBodies,
    "Our weather water bodies and Init's seeded moisture (docs/weather.md)",
    false,
    InstallBodies,
});

} // namespace
} // namespace opensc4::weather_sim
