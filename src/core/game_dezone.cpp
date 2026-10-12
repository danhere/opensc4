// game.dezone: with Shift held, the de-zone tool leaves the cells with a
// road, rail or other transport network alone, so streets inside the area
// are kept (docs/dezone.md). An improvement beyond the original and part of
// the game as it comes: without Shift the tool is the original's.
//
// The zone manager's PlaceZone already clears from the caller's region the
// cells that cannot take the zone; this clears the network cells the same
// way before it runs, so the game's own code never sees them. The idea is
// from the Bulldoze Extensions plugin by Nicholas Hayes; the code is ours.
#include "core/game_dezone.h"

#include "core/address_check.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"
#include "core/tool_zone.h"

#include <windows.h>

#include <cstdlib>

#include <opensc4/addresses.h>

namespace opensc4::game_dezone {
namespace {

// The zoning tool's code that calls PlaceZone (constructor 0x004DAE80): the
// preview at 0x004DB312 and the placing at 0x004DA846. With the tool's
// translation on (ui.tools.zone) those calls come from core.dll; the tool
// marks them (tool_zone::PlaceZoneSite()).
constexpr std::uintptr_t kToolBegin = 0x004DA7A0, kToolEnd = 0x004DC1A0;
// Every transport network; not water pipes (4), power lines (5), subway (7).
constexpr uint32_t kNetworkMask = 0x1F4F;

bool g_on = false;
bool g_test = false; // OPENSC4_TEST_DEZONE_KEEP=1: for every caller, no key

using PlaceFn = uint8_t(__fastcall*)(char* self, void* edx, int32_t* region, uint32_t type, uint32_t place,
                                     uint32_t skipFunds, uint32_t customZoning, uint32_t skipSize, uint32_t wholeLots,
                                     int64_t* cost, int32_t* error, void* lotSet);
PlaceFn g_place = nullptr;

uint8_t __fastcall PlaceZone(char* self, void* edx, int32_t* region, uint32_t type, uint32_t place, uint32_t skipFunds,
                             uint32_t customZoning, uint32_t skipSize, uint32_t wholeLots, int64_t* cost,
                             int32_t* error, void* lotSet)
{
    KeepRoads(region, type, customZoning, __builtin_return_address(0));
    return g_place(self, edx, region, type, place, skipFunds, customZoning, skipSize, wholeLots, cost, error, lotSet);
}

bool Install(registry::Mode)
{
    char value[8];
    g_test = GetEnvironmentVariableA("OPENSC4_TEST_DEZONE_KEEP", value, sizeof(value)) == 1 && value[0] == '1';
    // The game's PlaceZone; ours (simulators.zonemgr.place) calls KeepRoads
    // itself.
    const addr::Entry* place = addr_check::Find(addr::cSC4ZoneManager_PlaceZone);
    g_on = place && hook::Install(*place, &PlaceZone, &g_place);
    log::Write("dezone: Shift keeps roads when de-zoning: %s", g_on ? "installed" : "FAILED");
    return g_on;
}

const bool g_registered = registry::Register({
    "game.dezone",
    "game",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4ZoneManager_PlaceZone,
    "De-zoning with Shift held keeps roads and other transport networks",
    false,
    Install,
    true, // nothing changes until Shift is held
});

} // namespace

void KeepRoads(int32_t* region, uint32_t type, uint32_t customZoning, const void* caller)
{
    // Zone types above 15 are taken as 0 by PlaceZone.
    if (!g_on || !region || (type != 0 && type < 16))
        return;
    if (!g_test) {
        const auto from = reinterpret_cast<std::uintptr_t>(caller);
        const bool fromTool = (from >= kToolBegin && from < kToolEnd) || tool_zone::PlaceZoneSite() != 0;
        if (!static_cast<uint8_t>(customZoning) || !fromTool)
            return;
        static bool logged = false;
        if (!logged) {
            logged = true;
            log::Write("dezone: PlaceZone call from the zone tool recognised (%s)",
                       tool_zone::PlaceZoneSite() ? "marked call of our tool" : "return address in the game's tool");
        }
        if (GetKeyState(VK_SHIFT) >= 0)
            return;
    }
    void* const city = Global<void*>(addr::SC4_City);
    void* const traffic = city ? Virtual<void*>(city, 0xA0) : nullptr;
    if (!traffic)
        return;
    // SC4CellRegion: the rectangle, then at +0x10 the cells (width +0x14,
    // depth +0x18, a row of bits per x at +0x20).
    const uint32_t width = static_cast<uint32_t>(region[5]), depth = static_cast<uint32_t>(region[6]);
    uint32_t** const rows = reinterpret_cast<uint32_t**>(region[8]);
    if (!rows)
        return;
    for (int32_t x = region[0]; x <= region[2]; ++x)
        for (int32_t z = region[1]; z <= region[3]; ++z) {
            const uint32_t i = static_cast<uint32_t>(x - region[0]), j = static_cast<uint32_t>(z - region[1]);
            if (i >= width || j >= depth || !(rows[i][j >> 5] & (1u << (j & 31))))
                continue;
            if (Virtual<void*>(traffic, 0x20, x, z, kNetworkMask, 1u))
                rows[i][j >> 5] &= ~(1u << (j & 31));
        }
}

} // namespace opensc4::game_dezone
