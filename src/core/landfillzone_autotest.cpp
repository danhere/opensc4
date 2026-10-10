// The landfill zones' part of the autotest (src/core/autotest_hooks.h;
// src/core/landfillzone_sim.cpp).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cISC4City.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace opensc4::autotest {
namespace {

constexpr uint32_t kZoneLandfill = 0xE;
constexpr uint32_t kTypeLandfillOccupant = 0x6A11BD2B;
constexpr uint32_t kTypeBaseTexture = 0x497F6D9D;
constexpr uint32_t kIidLandfillOccupant = 0x2A15F262;

struct Node {
    Node* next;
    Node* previous;
    void* value;
};

struct Hash {
    uint32_t h = 2166136261u;
    void Mix(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
    }
};

// A zone's region (SC4CellRegion): the rectangle, then the bit map's
// width, height and columns at +0x14, +0x18 and +0x20.
bool CellSet(void* region, int32_t x, int32_t z)
{
    const uint32_t dx = static_cast<uint32_t>(x - At<int32_t>(region, 0x0));
    const uint32_t dz = static_cast<uint32_t>(z - At<int32_t>(region, 0x4));
    if (dx >= At<uint32_t>(region, 0x14) || dz >= At<uint32_t>(region, 0x18))
        return false;
    return (At<uint32_t**>(region, 0x20)[dx][dz >> 5] & (1u << (dz & 0x1F))) != 0;
}

// The landfill developer's zone list (city slot 78, list head at +0x30).
Node* Zones(cISC4City* city)
{
    void* const developer = game::IsBuild641() ? Virtual<void*>(city, 0x138) : nullptr;
    return developer ? At<Node*>(developer, 0x30) : nullptr;
}

uint32_t ZoneCount(cISC4City* city)
{
    uint32_t count = 0;
    if (Node* const head = Zones(city))
        for (Node* node = head->next; node != head && count < 4096; node = node->next)
            ++count;
    return count;
}

// The landfill zones (build 641; src/core/landfillzone_sim.cpp), from the
// landfill developer's list: the count, then a checksum of what the
// `landfill` part does not print: per zone the reference count (+0x4), the
// initialised flag (+0x8), the bit map's size and every cell of the
// rectangle, the cell count and its dirty flag (+0x2C, +0x30), the road
// access cell (+0x3C, +0x40), whether there is a marker, each controller
// and the effect (+0x44 to +0x50, no addresses) and per tile +0x10 and
// +0x1C; then the rotations of the game's picture table (0x00B0D2D0, which
// the random picture changes). Then a checksum of the occupants on the
// zones' cells, asked of the occupant manager (city slot 34): per cell the
// landfill occupant's exemplar, rotation and plain flag (+0x5C, +0x60,
// +0x64 of the object behind IID 0x2A15F262) and whether the cell has a
// base texture.
// OPENSC4_TEST_LANDFILLZONE=1 adds a checksum of the zones' query functions
// at the game's addresses: CountActiveTiles, GetUsage, GetFreeCapacity,
// GetMaintenanceCost and IsEmpty (which only refreshes its own cache).
void Fingerprint(cISC4City* city, std::string& line)
{
    Node* const head = Zones(city);
    if (!head) {
        line += " | landfillzone none";
        return;
    }
    void* const occupants = Virtual<void*>(city, 0x88);
    Hash state, onCells, queries;
    const bool query = EnvInt("OPENSC4_TEST_LANDFILLZONE", 0) == 1;
    uint32_t count = 0;
    int cells = 0;
    for (Node* node = head->next; node != head && count < 4096; node = node->next, ++count) {
        void* const zone = node->value;
        state.Mix(At<uint32_t>(zone, 0x4));
        state.Mix(At<uint8_t>(zone, 0x8));
        state.Mix(At<uint32_t>(zone, 0x2C));
        state.Mix(At<uint8_t>(zone, 0x30));
        state.Mix(At<uint32_t>(zone, 0x3C));
        state.Mix(At<uint32_t>(zone, 0x40));
        for (std::size_t offset = 0x44; offset <= 0x50; offset += 4)
            state.Mix(At<void*>(zone, offset) != nullptr);
        char* const end = At<char*>(zone, 0x20);
        int tiles = 0;
        for (char* tile = At<char*>(zone, 0x1C); tile != end && tiles < 65536; tile += 0x20, ++tiles) {
            state.Mix(At<uint32_t>(tile, 0x10));
            state.Mix(At<uint32_t>(tile, 0x1C));
        }
        void* const region = At<void*>(zone, 0xC);
        if (!region)
            continue;
        state.Mix(At<uint32_t>(region, 0x14));
        state.Mix(At<uint32_t>(region, 0x18));
        const int32_t x0 = At<int32_t>(region, 0x0), z0 = At<int32_t>(region, 0x4);
        const int32_t x1 = At<int32_t>(region, 0x8), z1 = At<int32_t>(region, 0xC);
        for (int32_t x = x0; x <= x1 && cells < 16384; ++x) {
            for (int32_t z = z0; z <= z1 && cells < 16384; ++z, ++cells) {
                state.Mix(CellSet(region, x, z));
                if (!occupants)
                    continue;
                void* occupant = nullptr;
                if (Virtual<uint8_t>(occupants, 0x84, &occupant, x, z, kTypeLandfillOccupant)) {
                    void* landfill = nullptr;
                    if (Virtual<uint8_t>(occupant, 0x0, kIidLandfillOccupant, &landfill)) {
                        onCells.Mix(static_cast<uint32_t>(x));
                        onCells.Mix(static_cast<uint32_t>(z));
                        onCells.Mix(At<uint32_t>(landfill, 0x5C));
                        onCells.Mix(At<uint32_t>(landfill, 0x60));
                        onCells.Mix(At<uint8_t>(landfill, 0x64));
                        Virtual<uint32_t>(landfill, 0x8);
                    }
                    Virtual<uint32_t>(occupant, 0x8);
                }
                occupant = nullptr;
                if (Virtual<uint8_t>(occupants, 0x84, &occupant, x, z, kTypeBaseTexture)) {
                    onCells.Mix(static_cast<uint32_t>(x) * 65536u + static_cast<uint32_t>(z));
                    Virtual<uint32_t>(occupant, 0x8);
                }
            }
        }
        if (!query)
            continue;
        using IntFn = int32_t(__thiscall*)(void*);
        using Int64Fn = int64_t(__thiscall*)(void*);
        using BoolFn = uint8_t(__thiscall*)(void*);
        using UsageFn = void(__thiscall*)(void*, uint32_t*, uint32_t*, uint32_t*);
        queries.Mix(static_cast<uint32_t>(reinterpret_cast<IntFn>(addr::cSC4LandfillZone_CountActiveTiles)(zone)));
        uint32_t free = 0, used = 0, capacity = 0;
        reinterpret_cast<UsageFn>(addr::cSC4LandfillZone_GetUsage)(zone, &free, &used, &capacity);
        queries.Mix(free);
        queries.Mix(used);
        queries.Mix(capacity);
        queries.Mix(static_cast<uint32_t>(reinterpret_cast<IntFn>(addr::cSC4LandfillZone_GetFreeCapacity)(zone)));
        const int64_t cost = reinterpret_cast<Int64Fn>(addr::cSC4LandfillZone_GetMaintenanceCost)(zone);
        queries.Mix(static_cast<uint32_t>(cost));
        queries.Mix(static_cast<uint32_t>(static_cast<uint64_t>(cost) >> 32));
        queries.Mix(reinterpret_cast<BoolFn>(addr::cSC4LandfillZone_IsEmpty)(zone));
    }
    for (int i = 0; i < 16; ++i)
        state.Mix(Global<uint32_t>(addr::cSC4LandfillZone_Pictures + 12 * i + 8));
    char text[96];
    std::snprintf(text, sizeof text, " | landfillzone %u,%08X,%08X", count, state.h, onCells.h);
    line += text;
    if (query) {
        std::snprintf(text, sizeof text, ",q%08X", queries.h);
        line += text;
    }
}

const FingerprintPart kFingerprint{"landfillzone", 100, &Fingerprint};

// SC4CellRegion as the zone manager takes it: the rectangle and its cells
// (the game's own constructor and destructor for the cells).
struct CellRegion {
    int32_t x0, z0, x1, z1;
    uint8_t cells[0x14];
};

// The zone manager's PlaceZone (slot 11) on a rectangle, with the arguments
// src/core/zoning_autotest.cpp uses (and cSC4LandfillZone::ClearCell at
// 0x006BCB79 for zone type 0): place, no funds check, no slope check, no
// size check, the lot size checked, no outputs.
bool PlaceZone(void* zones, int32_t x0, int32_t z0, int32_t x1, int32_t z1, uint32_t type)
{
    using CellsFn = void*(__thiscall*)(void* cells, uint32_t width, uint32_t depth, const uint8_t* fill);
    using SelfFn = void(__thiscall*)(void* self);
    CellRegion region;
    const uint8_t fill = 1;
    region.x0 = x0;
    region.z0 = z0;
    region.x1 = x1;
    region.z1 = z1;
    reinterpret_cast<CellsFn>(addr::SC4CellRegion_ConstructCells)(region.cells, static_cast<uint32_t>(x1 - x0 + 1),
                                                                 static_cast<uint32_t>(z1 - z0 + 1), &fill);
    const uint8_t placed = Virtual<uint8_t>(zones, 0x2C, &region, type, 1u, 1u, 0u, 1u, 0u, 0u, 0u, 0u);
    *reinterpret_cast<std::uintptr_t*>(region.cells) = addr::SC4CellRegion_CellsVtable;
    reinterpret_cast<SelfFn>(addr::SC4CellRegion_DestroyCells)(region.cells);
    return placed != 0;
}

// Whether the rectangle is free land: the lot manager's CanCreateLot (slot
// 20) answers 8, as in src/core/zoning_autotest.cpp.
bool Free(void* lots, int32_t x, int32_t z, int32_t width, int32_t depth)
{
    return Virtual<int32_t>(lots, 0x50, x, z, width, depth, 1u, 0u, 0u) == 8;
}

// Whether a cell just outside the rectangle's sides has a network (the
// city's traffic network, slot 8, as the zone's own road access test asks).
bool NextToNetwork(cISC4City* city, int32_t x, int32_t z, int32_t width, int32_t depth)
{
    void* const network = Virtual<void*>(city, 0xA0);
    if (!network)
        return false;
    for (int32_t i = 0; i < width; ++i)
        if (Virtual<uint32_t>(network, 0x20, x + i, z - 1, 0x1F4Fu, 1u) ||
            Virtual<uint32_t>(network, 0x20, x + i, z + depth, 0x1F4Fu, 1u))
            return true;
    for (int32_t j = 0; j < depth; ++j)
        if (Virtual<uint32_t>(network, 0x20, x - 1, z + j, 0x1F4Fu, 1u) ||
            Virtual<uint32_t>(network, 0x20, x + width, z + j, 0x1F4Fu, 1u))
            return true;
    return false;
}

struct Zoned {
    int32_t x0, z0, x1, z1;
    bool placed;
};
Zoned g_zoned = {};

// OPENSC4_TEST_LANDFILL_ZONING=<n>: on the n-th day of a run 6 x 6 cells of
// free land are zoned as landfill (zone type 0xE) through the zone manager,
// as the zoning tool does: the first free square next to a network, scanning
// from the city's middle; any free square when none is next to one.
// OPENSC4_TEST_LANDFILL_DEZONING=<n>: on the n-th day its left half is
// dezoned again (zone type 0).
void Zone(const Day& day)
{
    if (!game::IsBuild641())
        return;
    void* const lots = Global<void*>(addr::SC4_LotManager);
    void* const zones = Global<void*>(addr::SC4_ZoneManager);
    if (!lots || !zones)
        return;
    if (OnRunDay(day, "OPENSC4_TEST_LANDFILL_ZONING")) {
        constexpr int32_t kSize = 6, kMargin = 3;
        const int32_t cellsX = static_cast<int32_t>(day.city->CellCountX());
        const int32_t cellsZ = static_cast<int32_t>(day.city->CellCountZ());
        const int32_t spanX = cellsX - 2 * kMargin - kSize, spanZ = cellsZ - 2 * kMargin - kSize;
        g_zoned.placed = false;
        bool found = false;
        for (int pass = 0; pass < 2 && !found && spanX > 0 && spanZ > 0; ++pass)
            for (int32_t zi = 0; zi < spanZ && !found; ++zi)
                for (int32_t xi = 0; xi < spanX && !found; ++xi) {
                    const int32_t x = kMargin + (spanX / 2 + xi) % spanX;
                    const int32_t z = kMargin + (spanZ / 2 + zi) % spanZ;
                    if (!Free(lots, x, z, kSize, kSize))
                        continue;
                    if (pass == 0 && !NextToNetwork(day.city, x, z, kSize, kSize))
                        continue;
                    found = true;
                    const bool placed = PlaceZone(zones, x, z, x + kSize - 1, z + kSize - 1, kZoneLandfill);
                    g_zoned = {x, z, x + kSize - 1, z + kSize - 1, placed};
                    log::Write("autotest: landfill zoning: %dx%d at %d,%d (%s a network): %s; %u landfill zones",
                               kSize, kSize, x, z, pass == 0 ? "next to" : "not next to",
                               placed ? "placed" : "refused", ZoneCount(day.city));
                }
        if (!found)
            log::Write("autotest: landfill zoning: no free %dx%d square (city %dx%d); %u landfill zones", kSize,
                       kSize, cellsX, cellsZ, ZoneCount(day.city));
    }
    if (OnRunDay(day, "OPENSC4_TEST_LANDFILL_DEZONING")) {
        if (!g_zoned.placed) {
            log::Write("autotest: landfill dezoning: nothing was zoned; %u landfill zones", ZoneCount(day.city));
            return;
        }
        const int32_t middle = (g_zoned.x0 + g_zoned.x1) / 2;
        const bool done = PlaceZone(zones, g_zoned.x0, g_zoned.z0, middle, g_zoned.z1, 0);
        log::Write("autotest: landfill dezoning: %d,%d to %d,%d: %s; %u landfill zones", g_zoned.x0, g_zoned.z0,
                   middle, g_zoned.z1, done ? "done" : "refused", ZoneCount(day.city));
    }
}

const DayHook kZone{"landfillzoning", 59, &Zone};

} // namespace
} // namespace opensc4::autotest
