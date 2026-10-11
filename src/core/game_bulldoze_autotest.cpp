// OPENSC4_TEST_BULLDOZE=<n>: on the n-th day of a run the middle half of the
// city is bulldozed through cSC4Demolition::DemolishRegion with the bulldoze
// tool's arguments (0x004B9D04), to check game.bulldoze's modes
// (OPENSC4_TEST_BULLDOZE_MODE, docs/bulldoze.md).
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/game_bulldoze.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <GZServPtrs.h>
#include <cIGZString.h>
#include <cIGZWin.h>
#include <cISC4App.h>
#include <cISC4City.h>
#include <cISC4View3DWin.h>
#include <cRZAutoRefCount.h>

#include <cstdint>
#include <string>

namespace opensc4::autotest {
namespace {

// SC4CellRegion: the rectangle and its cells (the game's own constructor and
// destructor for the cells).
struct CellRegion {
    int32_t x0, z0, x1, z1;
    uint8_t cells[0x14];
};

void Bulldoze(const Day& day)
{
    if (!game::IsBuild641() || !OnRunDay(day, "OPENSC4_TEST_BULLDOZE"))
        return;
    void* const demolition = day.city->GetDemolitionUtility();
    if (!demolition)
        return;
    using CellsFn = void*(__thiscall*)(void* cells, uint32_t width, uint32_t depth, const uint8_t* fill);
    using SelfFn = void(__thiscall*)(void* self);
    const int32_t cellsX = static_cast<int32_t>(day.city->CellCountX());
    const int32_t cellsZ = static_cast<int32_t>(day.city->CellCountZ());
    CellRegion region;
    const uint8_t fill = 1;
    region.x0 = cellsX / 4;
    region.z0 = cellsZ / 4;
    // OPENSC4_TEST_BULLDOZE_WIDE=<k>: k cells longer along x (the diagonal
    // strip's width).
    region.x1 = cellsX * 3 / 4 - 1 + EnvInt("OPENSC4_TEST_BULLDOZE_WIDE", 0);
    region.z1 = cellsZ * 3 / 4 - 1;
    reinterpret_cast<CellsFn>(addr::SC4CellRegion_ConstructCells)(
        region.cells, static_cast<uint32_t>(region.x1 - region.x0 + 1), static_cast<uint32_t>(region.z1 - region.z0 + 1),
        &fill);
    int64_t cost = 0;
    const uint8_t done = Virtual<uint8_t>(demolition, 0x18, 1u, &region, 1, 1u, 0u, static_cast<void*>(nullptr), &cost,
                                          static_cast<void*>(nullptr), static_cast<void*>(nullptr), 0, 0);
    *reinterpret_cast<std::uintptr_t*>(region.cells) = addr::SC4CellRegion_CellsVtable;
    reinterpret_cast<SelfFn>(addr::SC4CellRegion_DestroyCells)(region.cells);
    log::Write("autotest: bulldozed cells %d,%d to %d,%d: %s, cost %lld", region.x0, region.z0, region.x1, region.z1,
               done ? "done" : "refused", static_cast<long long>(cost));
}

// The city view, as autotest.cpp finds it for screenshots.
char* CityView()
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

// OPENSC4_TEST_BULLDOZE_LABEL=<n>: on the n-th day game.bulldoze's cursor
// text is set for every mode and taken away again, and the view's list of
// cursor texts (+0x3DC: nodes with the ID at +8 and the title at +0x10) is
// logged after each.
void Label(const Day& day)
{
    if (!game::IsBuild641() || !OnRunDay(day, "OPENSC4_TEST_BULLDOZE_LABEL"))
        return;
    char* const view = CityView();
    if (!view) {
        log::Write("autotest: bulldoze label: no city view");
        return;
    }
    const auto ours = [&]() -> std::string {
        char* const head = At<char*>(view, 0x3DC);
        int count = 0;
        for (char* node = At<char*>(head, 0x0); node != head && count < 64; node = At<char*>(node, 0x0), ++count)
            if (At<uint32_t>(node, 0x8) == 0x4F534342) {
                const char* const text = reinterpret_cast<cIGZString*>(node + 0x10)->ToChar();
                return text ? text : "";
            }
        return "(none)";
    };
    for (int i = 0; i < 8; ++i) {
        game_bulldoze::SetLabel(view, i & 3, (i & 4) != 0);
        log::Write("autotest: bulldoze label %d%s: \"%s\"", i & 3, (i & 4) ? " diagonal" : "", ours().c_str());
    }
    game_bulldoze::ClearLabel(view);
    log::Write("autotest: bulldoze label cleared: \"%s\"", ours().c_str());
}

const DayHook kBulldoze{"bulldoze", 58, &Bulldoze};
const DayHook kLabel{"bulldozelabel", 58, &Label};

} // namespace
} // namespace opensc4::autotest
