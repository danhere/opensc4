// Autotest: zones and dezones land, which no unattended run does
// (src/core/autotest_hooks.h; docs/zone.md).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <cISC4City.h>

#include <cstdint>

namespace opensc4::autotest {
namespace {

// SC4CellRegion as the zone manager takes it: the rectangle and its cells
// (the game's own constructor and destructor for the cells).
struct CellRegion {
    int32_t x0, z0, x1, z1;
    uint8_t cells[0x14];
};

using CellsFn = void*(__thiscall*)(void* cells, uint32_t width, uint32_t depth, const uint8_t* fill);
using SelfFn = void(__thiscall*)(void* self);

// The zone manager's PlaceZone (slot 11) on a rectangle, with the arguments
// the port developers use when they dezone a port's ground (0x006E9E3C):
// place, no funds check, no slope check, no size check, the lot size
// checked, no outputs.
bool PlaceZone(void* zones, int32_t x0, int32_t z0, int32_t x1, int32_t z1, uint32_t type)
{
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
// 20; zone not needed, no configuration) answers 8 (1 is water, 3 a
// network, 7 a cell some lot has).
bool Free(void* lots, int32_t x0, int32_t z0, int32_t x1, int32_t z1)
{
    return Virtual<int32_t>(lots, 0x50, x0, z0, x1 - x0 + 1, z1 - z0 + 1, 1u, 0u, 0u) == 8;
}

struct Zoned {
    int32_t x0, z0, x1, z1;
    bool placed;
};
Zoned g_zoned[4] = {};

// OPENSC4_TEST_ZONING=<n>: on the n-th day of a run four rectangles of free
// land are zoned through the zone manager, as the zoning tool does: low
// density residential 6 x 4, medium commercial 4 x 4, agriculture 8 x 8 and
// medium industry 4 x 6 cells, each at the first free place found
// (scanning from a different corner of the city for each).
// OPENSC4_TEST_DEZONING=<n>: on the n-th day the left half of each is dezoned
// again (zone type 0).
void Zone(const Day& day)
{
    if (!game::IsBuild641())
        return;
    void* const developer = Virtual<void*>(day.city, 0x12C);
    void* const lots = Global<void*>(addr::SC4_LotManager);
    void* const zones = Global<void*>(addr::SC4_ZoneManager);
    void* const seaport = Virtual<void*>(day.city, 0x130);
    if (!developer || !lots || !zones || !seaport)
        return;
    if (OnRunDay(day, "OPENSC4_TEST_ZONING")) {
        // The city's size in cells, from the seaport developer's map.
        const int32_t cellsX = At<int32_t>(seaport, 0x74), cellsZ = At<int32_t>(seaport, 0x78);
        static const struct {
            uint32_t type;
            int32_t width, depth;
        } kWanted[4] = {{1, 6, 4}, {5, 4, 4}, {7, 8, 8}, {8, 4, 6}};
        for (int i = 0; i < 4; ++i) {
            g_zoned[i].placed = false;
            const int32_t w = kWanted[i].width, d = kWanted[i].depth;
            bool found = false;
            for (int32_t zi = 3; zi + d < cellsZ - 3 && !found; ++zi)
                for (int32_t xi = 3; xi + w < cellsX - 3 && !found; ++xi) {
                    // Corner by corner: 0 from the north-west, 1 the north-east,
                    // 2 the south-west, 3 the south-east.
                    const int32_t x = (i & 1) ? cellsX - 1 - xi - w : xi;
                    const int32_t z = (i & 2) ? cellsZ - 1 - zi - d : zi;
                    if (!Free(lots, x, z, x + w - 1, z + d - 1))
                        continue;
                    // Not where an earlier rectangle of this call went.
                    bool overlaps = false;
                    for (int j = 0; j < i; ++j)
                        overlaps = overlaps || (g_zoned[j].placed && x <= g_zoned[j].x1 + 2 &&
                                                x + w - 1 >= g_zoned[j].x0 - 2 && z <= g_zoned[j].z1 + 2 &&
                                                z + d - 1 >= g_zoned[j].z0 - 2);
                    if (overlaps)
                        continue;
                    found = true;
                    const bool placed = PlaceZone(zones, x, z, x + w - 1, z + d - 1, kWanted[i].type);
                    g_zoned[i] = {x, z, x + w - 1, z + d - 1, placed};
                    log::Write("autotest: zoning: type %u, %dx%d at %d,%d: %s; %d lots in the city", kWanted[i].type,
                               w, d, x, z, placed ? "placed" : "refused", Virtual<int32_t>(lots, 0x40));
                }
            if (!found) {
                uint32_t seen[10] = {};
                for (int32_t z = 3; z + d < cellsZ - 3; ++z)
                    for (int32_t x = 3; x + w < cellsX - 3; ++x) {
                        const int32_t verdict = Virtual<int32_t>(lots, 0x50, x, z, w, d, 1u, 0u, 0u);
                        if (verdict >= 0 && verdict < 10)
                            ++seen[verdict];
                    }
                log::Write("autotest: zoning: no free %dx%d rectangle for type %u (city %dx%d; answers 0:%u 1:%u "
                           "2:%u 3:%u 4:%u 7:%u 8:%u)",
                           w, d, kWanted[i].type, cellsX, cellsZ, seen[0], seen[1], seen[2], seen[3], seen[4], seen[7],
                           seen[8]);
            }
        }
    }
    if (OnRunDay(day, "OPENSC4_TEST_DEZONING")) {
        for (const Zoned& zoned : g_zoned) {
            if (!zoned.placed)
                continue;
            const int32_t middle = (zoned.x0 + zoned.x1) / 2;
            const bool done = PlaceZone(zones, zoned.x0, zoned.z0, middle, zoned.z1, 0);
            log::Write("autotest: zoning: dezoned %d,%d to %d,%d: %s; %d lots in the city", zoned.x0, zoned.z0,
                       middle, zoned.z1, done ? "done" : "refused", Virtual<int32_t>(lots, 0x40));
        }
    }
}

const DayHook kZone{"zoning", 58, &Zone};

} // namespace
} // namespace opensc4::autotest
