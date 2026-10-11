// A test that reaches the Data Views map's two coordinate setters
// (0x0079DDE0, 0x0079DDF0; docs/renderer.md, "The data map"). They are
// handed as functions to two grid mappers (0x0068C270, 0x006B4710) by the
// picture's grid filler (0x007A2080), and the picture function calls that
// only for the maps of types 0x40 to 0x4B when the window's style (+0x148,
// from the data view's exemplar) is 5, which no data view of the game has.
// So the test picks such a map through the window's own set-map function,
// sets the style to 5 for one call of the picture function, and puts the
// style back. The two mappers are hooked for the length of that call to
// count the setters' calls and take what they leave in each cell.
// Nothing here compares pictures with the game's: that is the renderer's
// comparison (`renderer.datamap.picture` in compare mode).
#include "core/address_check.h"
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <GZServPtrs.h>
#include <cIGZWin.h>
#include <cISC4App.h>
#include <cISC4City.h>
#include <cISC4View3DWin.h>
#include <cRZAutoRefCount.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace opensc4::autotest {
namespace {

// The city view, as autotest.cpp finds it for screenshots.
char* View()
{
    constexpr uint32_t kWinSC4App = 0x6104489A;
    constexpr uint32_t kWinView3D = 0x9A47B417;
    cISC4AppPtr app;
    cIGZWin* const main = app ? app->GetMainWindow() : nullptr;
    cIGZWin* const parent = main ? main->GetChildWindowFromID(kWinSC4App) : nullptr;
    cRZAutoRefCount<cISC4View3DWin> view;
    if (parent)
        parent->GetChildAs(kWinView3D, kGZIID_cISC4View3DWin, view.AsPPVoid());
    return reinterpret_cast<char*>(static_cast<cISC4View3DWin*>(view));
}

constexpr std::size_t kMapType = 0x144;
constexpr std::size_t kStyle = 0x148;
constexpr std::size_t kGrid = 0x9B8;
constexpr uint32_t kGridStyle = 5;

// The setters as the mappers call them: (cell, value, data), cdecl.
using SetterFn = uint32_t(__cdecl*)(int16_t*, uint32_t, void*);
using MapperFn = void(__cdecl*)(void*, void*, SetterFn, void*);

bool g_counting = false;
struct Counted {
    SetterFn real = nullptr;
    int calls = 0;
    uint32_t cells = 2166136261u; // each cell as the setter left it
};
Counted g_set, g_scale;

void Note(Counted& c, const int16_t* cell)
{
    ++c.calls;
    const uint16_t value = static_cast<uint16_t>(*cell);
    c.cells = (c.cells ^ (value & 0xFF)) * 16777619u;
    c.cells = (c.cells ^ (value >> 8)) * 16777619u;
}
uint32_t __cdecl CountedSet(int16_t* cell, uint32_t value, void* data)
{
    const uint32_t result = g_set.real(cell, value, data);
    Note(g_set, cell);
    return result;
}
uint32_t __cdecl CountedScale(int16_t* cell, uint32_t value, void* data)
{
    const uint32_t result = g_scale.real(cell, value, data);
    Note(g_scale, cell);
    return result;
}

MapperFn g_fromSint16 = nullptr, g_fromUint16 = nullptr;
void Mapped(MapperFn mapper, void* target, void* source, SetterFn add, void* data)
{
    if (g_counting && add == reinterpret_cast<SetterFn>(addr::DataMap_SetCoordinate)) {
        g_set.real = add;
        add = &CountedSet;
    } else if (g_counting && add == reinterpret_cast<SetterFn>(addr::DataMap_ScaleCoordinate)) {
        g_scale.real = add;
        add = &CountedScale;
    }
    mapper(target, source, add, data);
}
void __cdecl DFromSint16(void* target, void* source, SetterFn add, void* data)
{
    Mapped(g_fromSint16, target, source, add, data);
}
void __cdecl DFromUint16(void* target, void* source, SetterFn add, void* data)
{
    Mapped(g_fromUint16, target, source, add, data);
}

// The two mappers hooked, once; false when another replacement has one of
// them (the growth developer's own mapper), and then nothing is counted.
bool HookMappers()
{
    static int state = 0;
    if (state)
        return state > 0;
    state = -1;
    if (hook::IsInstalledAt(addr::SC4GridMapper_FromSint16) || hook::IsInstalledAt(addr::cSC4GrowthDeveloper_MapUint16Grid))
        return false;
    const addr::Entry* const first = addr_check::Find(addr::SC4GridMapper_FromSint16);
    const addr::Entry* const second = addr_check::Find(addr::cSC4GrowthDeveloper_MapUint16Grid);
    if (first && second && hook::Install(*first, &DFromSint16, &g_fromSint16) && hook::Install(*second, &DFromUint16, &g_fromUint16))
        state = 1;
    return state > 0;
}

std::string g_token;

// OPENSC4_TEST_DATA_MAP_GRID=<day>: on that run day every data view from 1
// to 87 is tried on the map window until three have given a map of a type
// from 0x40 to 0x4B; for each of those the picture is painted once more with
// the style 5, the style put back, and at the end the plain view. The token
// says how many maps were painted that way, how many of those paints made
// the window's grid, how often each setter was called (in compare mode both
// the game's run and ours count) and a checksum of the cells they set.
void DriveDataMapGrid(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_DATA_MAP_GRID") || !game::IsBuild641())
        return;
    char* const view = View();
    void* const window = view ? At<void*>(view, 0x300) : nullptr;
    char* map = nullptr;
    if (window)
        Virtual<uint8_t>(window, 0x0, 0x67C5BEDEu, &map);
    if (!map) {
        g_token = " | dmgrid none";
        log::Write("autotest: data map grid: no map window");
        return;
    }
    Virtual<uint32_t>(map, 0x8); // the view keeps the window alive
    const bool counted = HookMappers();
    using SetMapFn = uint8_t(__thiscall*)(void*, uint32_t);
    using PaintFn = void(__thiscall*)(void*);
    int painted = 0, grids = 0;
    uint32_t types = 0;
    for (uint32_t id = 1; id <= 87 && painted < 3; ++id) {
        if (!reinterpret_cast<SetMapFn>(addr::SC4UI_F7A49B0)(map, id))
            continue;
        const uint32_t type = At<uint32_t>(map, kMapType);
        if (type < 0x40 || type > 0x4B || (types & (1u << (type - 0x40))))
            continue;
        types |= 1u << (type - 0x40);
        const uint32_t style = At<uint32_t>(map, kStyle);
        const int before = g_set.calls + g_scale.calls;
        At<uint32_t>(map, kStyle) = kGridStyle;
        g_counting = true;
        reinterpret_cast<PaintFn>(addr::cSC4WinMapView_PaintPicture)(map);
        g_counting = false;
        At<uint32_t>(map, kStyle) = style;
        ++painted;
        grids += At<void*>(map, kGrid) != nullptr;
        log::Write("autotest: data map grid: view %u, map type %u, style %u put back, grid %s, %d setter calls", id, type, style,
                   At<void*>(map, kGrid) ? "made" : "not made", g_set.calls + g_scale.calls - before);
    }
    reinterpret_cast<SetMapFn>(addr::SC4UI_F7A49B0)(map, 0);
    char text[160];
    std::snprintf(text, sizeof text, " | dmgrid %d types %03X grids %d set %d %08X scale %d %08X%s", painted, types, grids, g_set.calls,
                  g_set.cells, g_scale.calls, g_scale.cells, counted ? "" : " uncounted");
    g_token = text;
    log::Write("autotest: data map grid driven:%s", text);
}

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"datamap.grid", 173, &Fingerprint};
const DayHook kDrive{"datamap.grid", 173, &DriveDataMapGrid};

} // namespace
} // namespace opensc4::autotest
