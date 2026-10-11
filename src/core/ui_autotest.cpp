// The interface code's part of the autotest (src/core/autotest_hooks.h;
// docs/ui.md): switches that call window classes directly, as their window
// would, and put what they leave into the fingerprint.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
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
#include <cstring>
#include <string>

namespace opensc4::autotest {
namespace {

struct Fnv {
    uint32_t h = 2166136261u;
    void Bytes(const void* data, std::size_t size)
    {
        for (std::size_t i = 0; i < size; ++i)
            h = (h ^ static_cast<const uint8_t*>(data)[i]) * 16777619u;
    }
    template <typename T>
    void Add(const T& value)
    {
        Bytes(&value, sizeof value);
    }
};

char g_budgetToken[120];

// The small window's static table of seven departments: of each row the
// percentage (+0x10), the cost (+0x18) and whether it changed (+0x20).
void AddBudgetTable(Fnv& f)
{
    for (std::size_t row = 0; row < 7; ++row) {
        char* const entry = reinterpret_cast<char*>(std::uintptr_t{0x00B0D8B8}) + row * 0x28;
        f.Bytes(entry + 0x10, 4);
        f.Bytes(entry + 0x18, 8);
        f.Bytes(entry + 0x20, 1);
    }
}

uint8_t Send(char* object, void* holder, uint32_t type, uint32_t code, uint32_t id)
{
    uint32_t message[4] = {type, code, id, 0};
    return Virtual<uint8_t>(object + 0x8, 0xC, Virtual<void*>(holder, 0xC), &message[0]);
}

// OPENSC4_TEST_BUDGET_WINDOWS=<day>: on that run day the two budget windows
// are made as the budget simulator makes them and worked through their
// window procedures: the small one filled, two sliders moved, applied and
// closed; the large one filled, a spinner moved, closed. What they wrote to
// the simulator is in the budget's own part of the fingerprint.
void DriveBudgetWindows(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_BUDGET_WINDOWS") || !game::IsBuild641())
        return;
    using NewFn = void*(__cdecl*)(std::size_t);
    using ConstructFn = char*(__thiscall*)(void*);
    Fnv f;
    int made = 0;
    if (void* const memory = reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x28)) {
        char* const small = reinterpret_cast<ConstructFn>(addr::BudgetWindow_Small)(memory);
        Virtual<uint32_t>(small, 0x4);
        if (void* const holder = At<void*>(small, 0x10)) {
            ++made;
            f.Add(Send(small, holder, 0xA2BF8AD5, 0, 0)); // filled
            AddBudgetTable(f);
            f.Bytes(small + 0x20, 8);
            f.Add(Send(small, holder, 3, 0x887113A3, 0x301)); // two sliders
            f.Add(Send(small, holder, 3, 0x887113A3, 0x304));
            AddBudgetTable(f);
            f.Add(Send(small, holder, 3, 0x287259F6, 0x111)); // applied and closed
            AddBudgetTable(f);
        }
        Virtual<uint32_t>(small, 0x8);
    }
    if (void* const memory = reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x60)) {
        char* const large = reinterpret_cast<ConstructFn>(addr::BudgetWindow_Large)(memory);
        Virtual<uint32_t>(large, 0x4);
        if (void* const holder = At<void*>(large, 0x14)) {
            ++made;
            f.Add(Send(large, holder, 0xA2BF8AD5, 0, 0)); // filled
            f.Bytes(large + 0x20, 0x40);
            f.Add(Send(large, holder, 0x682876C9, 0x502, 0)); // a spinner
            f.Bytes(large + 0x20, 0x40);
            f.Add(Send(large, holder, 3, 0x287259F6, 0x107)); // closed
        }
        Virtual<uint32_t>(large, 0x8);
    }
    std::snprintf(g_budgetToken, sizeof g_budgetToken, " budwin %d %08X", made, f.h);
    log::Write("autotest: budget windows driven:%s", g_budgetToken);
}

// --- The city view's live objects -------------------------------------------

// The city view (cSC4View3DWin, 0x448 bytes; as autotest.cpp finds it): it
// keeps the news ticker at +0x2DC, the MySim panel at +0x2E4, the budget
// panel at +0x2E8, the graphs window at +0x2F8, the building style panel at
// +0x2FC and the advice popup at +0x31C.
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

// A stand-in list control (docs/ui.md): it hashes each row handed to it.
struct WindowString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
};

Fnv g_rows;
int g_rowCount = 0;

void __fastcall ListAddRow(void*, void*, const WindowString* image1, const WindowString* image2,
                           const WindowString* message, const WindowString* title, uint32_t flag, char* advice,
                           const WindowString* image3, uint32_t zero, uint32_t style)
{
    for (const WindowString* text : {image1, image2, message, title, image3}) {
        if (text && text->begin && text->begin <= text->end)
            g_rows.Bytes(text->begin, static_cast<std::size_t>(text->end - text->begin));
        g_rows.Add(uint8_t{0});
    }
    g_rows.Add(static_cast<uint8_t>(flag));
    g_rows.Add(advice ? At<uint32_t>(advice, 0x8C) : 0u);
    g_rows.Add(zero);
    g_rows.Add(style);
    ++g_rowCount;
}

void __fastcall ListOneArgument(void*, void*, uint32_t value)
{
    g_rows.Add(value);
}

void __fastcall ListClear(void*, void*)
{
    g_rows.Add(uint8_t{0xC1});
}

void* g_listVtable[8] = {nullptr,
                         nullptr,
                         nullptr,
                         nullptr,
                         reinterpret_cast<void*>(&ListAddRow),
                         reinterpret_cast<void*>(&ListOneArgument),
                         reinterpret_cast<void*>(&ListOneArgument),
                         reinterpret_cast<void*>(&ListClear)};
void* g_list = g_listVtable;

// A stand-in window with no children: the two lookups the choice filler
// makes (by ID with an interface, slot 0x94; by ID, slot 0x8C) find nothing.
uint8_t __fastcall NoChildAs(void*, void*, uint32_t, uint32_t, void**)
{
    return 0;
}

void* __fastcall NoChild(void*, void*, uint32_t)
{
    return nullptr;
}

void* g_emptyWindowVtable[0x98 / sizeof(void*)];
void* g_emptyWindow = g_emptyWindowVtable;

char g_panelToken[160];

void AddPanelState(Fnv& f, char* panel)
{
    for (std::size_t at : {std::size_t{0x48}, std::size_t{0x4C}, std::size_t{0x50}, std::size_t{0x54}, std::size_t{0x5C},
                           std::size_t{0x60}, std::size_t{0x64}, std::size_t{0x68}, std::size_t{0x70}})
        f.Add(At<uint32_t>(panel, at));
    f.Add(At<uint8_t>(panel, 0x58));
    f.Add(At<uint8_t>(panel, 0x74));
    f.Add(At<void*>(panel, 0x18) != nullptr);
}

// OPENSC4_TEST_MYSIM_PANEL=<day>: the city view's own MySim panel, not on
// screen, worked as its buttons work it: shown (which fills its places);
// each place that has a MySim selected, its two advice lists shown and
// their rows taken by the stand-in list, its details pane, the car and
// figure choices counted for each wealth tab against a window with no
// children; the panel recounted and hidden.
void DriveMySimPanel(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_MYSIM_PANEL") || !game::IsBuild641())
        return;
    char* const view = View();
    char* const panel = view ? At<char*>(view, 0x2E4) : nullptr;
    if (!panel) {
        log::Write("autotest: MySim panel: none (view %p)", static_cast<void*>(view));
        return;
    }
    g_emptyWindowVtable[0x8C / sizeof(void*)] = reinterpret_cast<void*>(&NoChild);
    g_emptyWindowVtable[0x94 / sizeof(void*)] = reinterpret_cast<void*>(&NoChildAs);
    using ThisFn = void(__thiscall*)(void*);
    Fnv f;
    g_rows = Fnv{};
    g_rowCount = 0;
    int places = 0;
    reinterpret_cast<ThisFn>(addr::SC4UI_F76FCE0)(panel); // Show: the places filled, the frame shown
    AddPanelState(f, panel);
    for (int32_t place = 0; place < 5; ++place) {
        if (!At<void*>(panel, 0x1C + 4 * static_cast<std::size_t>(place)))
            continue;
        ++places;
        reinterpret_cast<void(__thiscall*)(void*, int32_t, uint32_t, uint32_t)>(addr::SC4UI_F771940)(panel, place, 1, 0);
        AddPanelState(f, panel);
        for (const std::uintptr_t show : {addr::SC4UI_F771440, addr::SC4UI_F771550}) {
            reinterpret_cast<ThisFn>(show)(panel); // one of the two advice lists
            AddPanelState(f, panel);
            void* const list = At<void*>(panel, 0x18);
            At<void*>(panel, 0x18) = &g_list;
            reinterpret_cast<ThisFn>(addr::SC4UI_F770740)(panel); // its rows
            At<void*>(panel, 0x18) = list;
        }
        reinterpret_cast<ThisFn>(addr::SC4UI_F76F450)(panel); // the details pane
        AddPanelState(f, panel);
        const uint32_t perPage = At<uint32_t>(panel, 0x60), page = At<uint32_t>(panel, 0x64);
        const uint32_t tab = At<uint32_t>(panel, 0x70), found = At<uint32_t>(panel, 0x5C);
        const uint32_t first = At<uint32_t>(panel, 0x68);
        const uint8_t figures = At<uint8_t>(panel, 0x74);
        for (const uint8_t kind : {uint8_t{0}, uint8_t{1}}) {
            for (uint32_t wealth = 0; wealth < 3; ++wealth) {
                for (const uint32_t at : {0u, 7u}) {
                    At<uint8_t>(panel, 0x74) = kind;
                    At<uint32_t>(panel, 0x70) = wealth;
                    At<uint32_t>(panel, 0x60) = kind ? 14 : 28;
                    At<uint32_t>(panel, 0x64) = at;
                    reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::SC4UI_F76FDB0)(panel, &g_emptyWindow);
                    f.Add(At<uint32_t>(panel, 0x5C));
                    f.Add(At<uint32_t>(panel, 0x64));
                    f.Add(At<uint32_t>(panel, 0x68));
                }
            }
        }
        At<uint32_t>(panel, 0x60) = perPage;
        At<uint32_t>(panel, 0x64) = page;
        At<uint32_t>(panel, 0x70) = tab;
        At<uint32_t>(panel, 0x5C) = found;
        At<uint32_t>(panel, 0x68) = first;
        At<uint8_t>(panel, 0x74) = figures;
    }
    if (places) {
        reinterpret_cast<ThisFn>(addr::SC4UI_F7719F0)(panel); // the recount
        AddPanelState(f, panel);
    }
    reinterpret_cast<ThisFn>(addr::SC4UI_F76E8E0)(panel); // Hide
    AddPanelState(f, panel);
    std::snprintf(g_panelToken, sizeof g_panelToken, " mspanel %d %08X rows %d %08X", places, f.h, g_rowCount, g_rows.h);
    log::Write("autotest: MySim panel driven:%s", g_panelToken);
}

char g_cityPanelsToken[200];

// OPENSC4_TEST_CITY_PANELS=<day>: the city view's own budget panel, graphs
// window and building style panel, worked directly and put away again.
// Budget panel: its figures read and written to its texts for both pages by
// month and by year, the tax incomes shown, the page shown and hidden.
// Graphs window: each graph number up to 24 shown in turn, its kind, counts,
// scales and rows taken (not the row fields the original leaves unset),
// then hidden. Style panel: refreshed, its boxes updated, hidden.
void DriveCityPanels(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_CITY_PANELS") || !game::IsBuild641())
        return;
    char* const view = View();
    if (!view) {
        log::Write("autotest: city panels: no city view");
        return;
    }
    using ThisFn = void(__thiscall*)(void*);
    Fnv budgetHash;
    int budget = 0;
    if (char* const panel = At<char*>(view, 0x2E8); panel && At<void*>(panel, 0x10)) {
        budget = 1;
        const uint8_t page = At<uint8_t>(panel, 0x194);
        const uint32_t yearly = At<uint32_t>(panel, 0x198);
        for (const uint32_t byYear : {0u, 1u}) {
            for (const uint8_t which : {uint8_t{0}, uint8_t{1}}) {
                At<uint8_t>(panel, 0x194) = which;
                At<uint32_t>(panel, 0x198) = byYear;
                reinterpret_cast<ThisFn>(addr::SC4UI_F767990)(panel); // Refresh
                budgetHash.Bytes(panel + 0x28, 0x128);
            }
            reinterpret_cast<ThisFn>(addr::SC4UI_F766D00)(panel); // the tax incomes
        }
        At<uint8_t>(panel, 0x194) = page;
        At<uint32_t>(panel, 0x198) = yearly;
        Virtual<void>(panel, 0x18); // the page shown
        budgetHash.Bytes(panel + 0x194, 0xB);
        Virtual<void>(panel, 0x1C); // all hidden
        budgetHash.Bytes(panel + 0x194, 0xB);
    }

    Fnv graphHash;
    int graphs = 0, rows = 0;
    if (char* const window = At<char*>(view, 0x2F8); window && At<void*>(window, 0x18) && At<void*>(window, 0x20)) {
        const uint32_t number = At<uint32_t>(window, 0x14);
        for (uint32_t graph = 1; graph <= 24; ++graph) {
            reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::SC4UI_F76E4B0)(window, graph); // ShowGraph
            if (At<void*>(window, 0x24))
                ++graphs;
            for (std::size_t at : {std::size_t{0x10}, std::size_t{0x14}, std::size_t{0x44}, std::size_t{0x48},
                                   std::size_t{0x50}, std::size_t{0x54}, std::size_t{0x58}})
                graphHash.Add(At<uint32_t>(window, at));
            graphHash.Add(At<void*>(window, 0x24) != nullptr);
            graphHash.Add(At<uint8_t>(window, 0x28));
            char* const first = At<char*>(window, 0x2C);
            char* const last = At<char*>(window, 0x30);
            for (char* row = first; first && row < last && last - first < 0x10000; row += 0x30, ++rows) {
                graphHash.Add(At<uint32_t>(row, 0x0));
                graphHash.Add(At<uint32_t>(row, 0xC));
                graphHash.Add(At<uint32_t>(row, 0x14));
                const auto* const name = reinterpret_cast<const WindowString*>(row + 0x18);
                if (name->begin && name->begin <= name->end)
                    graphHash.Bytes(name->begin, static_cast<std::size_t>(name->end - name->begin));
                graphHash.Add(uint8_t{0});
            }
        }
        reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::SC4UI_F76E4B0)(window, number);
        reinterpret_cast<ThisFn>(addr::SC4UI_F76E470)(window); // Hide
        graphHash.Add(At<uint8_t>(window, 0x28));
        graphHash.Add(At<void*>(window, 0x24) != nullptr);
    }

    Fnv styleHash;
    int style = 0;
    if (char* const panel = At<char*>(view, 0x2FC); panel && At<uint8_t>(panel, 0x8)) {
        style = 1;
        reinterpret_cast<ThisFn>(addr::SC4UI_F769B30)(panel); // Refresh
        styleHash.Add(At<uint8_t>(panel, 0x18));
        reinterpret_cast<ThisFn>(addr::SC4UI_F7696D0)(panel); // UpdateBoxes
        reinterpret_cast<ThisFn>(addr::SC4UI_F7696B0)(panel); // Hide
        styleHash.Add(At<uint8_t>(panel, 0x18));
        styleHash.Add(At<uint8_t>(panel, 0x8));
    }
    std::snprintf(g_cityPanelsToken, sizeof g_cityPanelsToken, " cvpanels budget %d %08X graphs %d %d %08X style %d %08X",
                  budget, budgetHash.h, graphs, rows, graphHash.h, style, styleHash.h);
    log::Write("autotest: city panels driven:%s", g_cityPanelsToken);
}

char g_mapToken[200];

// OPENSC4_TEST_MAP_WINDOW=<day>: the city view's map window (the data
// views) and a small map of the test's own, worked directly. Legends: the
// legend rows built for a list of views. Marks: for each kind of mark the
// buildings collected (as a set: the map is keyed by address). Views: each
// view chosen in turn, the fields it sets and its two colour tables taken;
// this runs the game's own picture drawing, which is not compared; the
// plain view is put back. Small map: its flags, colours, marked blocks and
// colour ramps set and read back, with no window.
void DriveMapWindow(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_MAP_WINDOW") || !game::IsBuild641())
        return;
    char* const view = View();
    void* const window = view ? At<void*>(view, 0x300) : nullptr;
    char* map = nullptr;
    if (window)
        Virtual<uint8_t>(window, 0x0, 0x67C5BEDEu, &map);
    Fnv legends, marks, views;
    int marked = 0, shown = 0;
    if (map) {
        Virtual<uint32_t>(map, 0x8); // the view keeps the window alive
        const uint32_t kinds[] = {1, 2, 3, 5, 7, 8, 9, 0xA, 0x18, 0x1E, 0x20, 0x28, 0x32, 0x3C, 0x4C, 0x50};
        for (const uint32_t legend : kinds) {
            reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::SC4UI_F7A04F0)(map, legend);
            legends.Add(At<uint8_t>(map, 0x9B6));
        }
        reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::SC4UI_F7A04F0)(map, At<uint32_t>(map, 0x9D4));

        const uint32_t kind = At<uint32_t>(map, 0x980);
        for (const uint32_t mark : {1u, 2u, 3u, 4u, 6u, 7u, 8u, 9u, 0u}) {
            At<uint32_t>(map, 0x980) = mark;
            marks.Add(reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::SC4UI_F7A1A00)(map));
            const uint32_t count = At<uint32_t>(map, 0x96C);
            marks.Add(count);
            marked += static_cast<int>(count);
            // The marks as a set: each one's size and place, summed.
            uint32_t sum = 0;
            char* const header = At<char*>(map, 0x968);
            uint32_t seen = 0;
            for (char* node = At<char*>(header, 0x8); node != header && seen < count; ++seen) {
                Fnv one;
                one.Bytes(node + 0x18, 0x10);
                one.Add(At<void*>(node, 0x14) != nullptr);
                sum += one.h;
                if (char* right = At<char*>(node, 0xC)) {
                    while (At<char*>(right, 0x8))
                        right = At<char*>(right, 0x8);
                    node = right;
                } else {
                    char* parent = At<char*>(node, 0x4);
                    while (node == At<char*>(parent, 0xC)) {
                        node = parent;
                        parent = At<char*>(parent, 0x4);
                    }
                    if (At<char*>(node, 0xC) != parent)
                        node = parent;
                }
            }
            marks.Add(sum);
        }
        At<uint32_t>(map, 0x980) = kind;

        for (const uint32_t id : kinds) {
            const uint8_t did = reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t)>(addr::SC4UI_F7A49B0)(map, id);
            shown += did;
            views.Add(did);
            for (std::size_t at : {std::size_t{0x9CC}, std::size_t{0x144}, std::size_t{0x148}, std::size_t{0x998},
                                   std::size_t{0x99C}, std::size_t{0x980}})
                views.Add(At<uint32_t>(map, at));
            views.Bytes(map + 0x14C, 0x800);
            views.Bytes(map + 0x94C, 0x2);
            views.Bytes(map + 0x9AD, 0x9);
        }
        views.Add(reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t)>(addr::SC4UI_F7A49B0)(map, 0));
        views.Add(At<uint32_t>(map, 0x9CC));
    }

    Fnv mini;
    if (char* const face = static_cast<char*>(reinterpret_cast<void*(__cdecl*)()>(addr::SC4UI_F7A8AF0)())) {
        char* const small = face - 0xD8;
        using FaceFn = void(__thiscall*)(void*, uint32_t);
        mini.Add(reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t, uint32_t)>(addr::SC4UI_F7A7770)(face, 0x1F, 0x5));
        mini.Add(At<uint32_t>(small, 0xE0));
        reinterpret_cast<FaceFn>(addr::SC4UI_F7A70E0)(face, 0x00112233);
        reinterpret_cast<void(__thiscall*)(void*, uint32_t, uint32_t)>(addr::SC4UI_F7A7120)(face, 3, 0x00445566);
        reinterpret_cast<void(__thiscall*)(void*, uint32_t, uint32_t)>(addr::SC4UI_F7A7120)(face, 40, 0x00778899);
        if (char* const colours = At<char*>(small, 0x108); colours && At<char*>(small, 0x10C) - colours == 0x40)
            mini.Bytes(colours, 0x40);
        std::memset(small + 0x120, 0, 0x40);
        const int32_t rect[4] = {16, 16, 80, 48};
        mini.Add(reinterpret_cast<uint8_t(__thiscall*)(void*, const int32_t*)>(addr::SC4UI_F7A71F0)(small, &rect[0]));
        mini.Bytes(small + 0x120, 0x40);
        reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4UI_F7A7450)(small); // the colour ramps
        mini.Bytes(small + 0x160, 0x800);
        mini.Bytes(small + 0x120, 0x40);
        mini.Add(At<uint8_t>(small, 0xFD));
        mini.Add(At<uint8_t>(small, 0xFE));
        reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4UI_F7A70C0)(face);
        Virtual<void*>(small, 148 * sizeof(void*), uint32_t{1}); // deleted
    }
    std::snprintf(g_mapToken, sizeof g_mapToken, " mapwin %d legends %08X marks %d %08X views %d %08X mini %08X",
                  map ? 1 : 0, legends.h, marked, marks.h, shown, views.h, mini.h);
    log::Write("autotest: map window driven:%s", g_mapToken);
}

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_budgetToken;
    line += g_panelToken;
    line += g_cityPanelsToken;
    line += g_mapToken;
}

const DayHook kMapWindow{"ui.map_window", 100, &DriveMapWindow};

const DayHook kCityPanels{"ui.city_panels", 99, &DriveCityPanels};

const DayHook kMySimPanel{"ui.mysim_panel", 98, &DriveMySimPanel};

const FingerprintPart kFingerprint{"ui", 97, &Fingerprint};
const DayHook kBudgetWindows{"ui.budget_windows", 97, &DriveBudgetWindows};

} // namespace
} // namespace opensc4::autotest
