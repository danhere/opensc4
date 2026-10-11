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

#include <windows.h>

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

// --- Modal dialogs with a player in place of the modal loop ------------------

// While a dialog runs, the window manager's DoModalWin (slot 0xA4) is this:
// it calls the player with the window and how deep the dialogs are nested,
// and answers what the player answers, as the loop answers the dialog's
// result when it is closed.
using PlayerFn = uint32_t (*)(void* window, int depth);
PlayerFn g_player = nullptr;
int g_depth = 0;

uint32_t __fastcall ModalStandIn(void*, void*, void* window)
{
    ++g_depth;
    const uint32_t answer = g_player ? g_player(window, g_depth) : 0;
    --g_depth;
    return answer;
}

template <typename F>
bool RunModal(PlayerFn player, F run)
{
    void* const manager = reinterpret_cast<void*(__cdecl*)()>(addr::SC4_GetWinManager)();
    if (!manager)
        return false;
    void** const slot = &(*static_cast<void***>(manager))[0xA4 / sizeof(void*)];
    void* const loop = *slot;
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old))
        return false;
    *slot = reinterpret_cast<void*>(&ModalStandIn);
    g_player = player;
    run();
    g_player = nullptr;
    *slot = loop;
    VirtualProtect(slot, sizeof *slot, old, &old);
    return true;
}

char g_dialogsToken[220];
Fnv g_played;
char* g_budgetPanel = nullptr;
char* g_options = nullptr;
char* g_pickPanel = nullptr;

uint8_t PanelSend(uint32_t type, uint32_t a, uint32_t b)
{
    uint32_t message[4] = {type, a, b, 0};
    void* const window = Virtual<void*>(At<void*>(g_budgetPanel, 0x10), 0xC);
    return Virtual<uint8_t>(g_budgetPanel + 0x8, 0xC, window, &message[0]);
}

// The tax window's player: two spinners moved (residential low wealth to
// 8.5 percent, commercial medium to 6.5), then the window left open, so
// the rates are applied when the loop ends.
uint32_t PlayTaxes(void*, int)
{
    g_played.Add(PanelSend(0x682876C9, 0x200, 85));
    g_played.Add(PanelSend(0x682876C9, 0x204, 65));
    g_played.Bytes(g_budgetPanel + 0x150, 0x30);
    return 0;
}

// The loan dialog's player: the list scrolled down and up, the amount set,
// a loan taken.
uint32_t PlayLoans(void*, int)
{
    g_played.Add(PanelSend(3, 0x287259F6, 0xAA4C339C));
    g_played.Add(PanelSend(3, 0x287259F6, 0x2A4C33A6));
    g_played.Add(PanelSend(0x682876C9, 0x8A4C34D4, At<uint32_t>(g_budgetPanel, 0x190)));
    g_played.Bytes(g_budgetPanel + 0x180, 0x14);
    g_played.Add(PanelSend(3, 0x287259F6, 0xAA4C33E6));
    g_played.Bytes(g_budgetPanel + 0x180, 0x14);
    return 0;
}

uint32_t PlayNothing(void*, int)
{
    g_played.Add(uint8_t{0xD1});
    return 0;
}

// The options dialog's player: its two pages, the text page it opens (a
// dialog inside the dialog, left at once), then Accept.
uint32_t PlayOptions(void*, int depth)
{
    if (depth != 1) {
        g_played.Add(uint8_t{0xD2});
        return 0;
    }
    void* const window = Virtual<void*>(At<void*>(g_options, 0x4), 0xC);
    for (const uint32_t button : {0xEA57DB7Eu, 0xEA5E99D9u, 0xEA57DB7Eu, 0x0A57DA65u, 0xEA57DA59u}) {
        uint32_t message[4] = {3, 0x287259F6, button, 0};
        g_played.Add(Virtual<uint8_t>(g_options, 0xC, window, &message[0]));
        g_played.Add(At<void*>(g_options, 0x4) != nullptr);
        if (!At<void*>(g_options, 0x4))
            break;
    }
    return 0;
}

// The pick-one dialog's player: the third choice pressed, then yes.
uint32_t PlayPick(void* window, int)
{
    uint32_t message[4] = {3, 0x287259F6, 0x34560002, 0};
    g_played.Add(Virtual<uint8_t>(g_pickPanel, 0xC, window, &message[0]));
    for (std::size_t at : {std::size_t{0x5C}, std::size_t{0x60}, std::size_t{0x64}, std::size_t{0x6C}, std::size_t{0x70}})
        g_played.Add(At<uint32_t>(g_pickPanel, at));
    return 1;
}

// OPENSC4_TEST_DIALOGS=<day>: the dialogs that only a button opens, opened
// and worked by pressing their buttons through their window procedures,
// with the players above in place of the window manager's modal loop: the
// budget panel's tax window (two rates changed and applied), its loan dialog
// (a loan taken) and five department dialogs; the options dialog (pages, the
// text page inside it, Accept); the MySim panel's dialog for a car and for a
// figure (one picked); and the map window's controls readied, a button of
// each row pressed, hidden. Use with OPENSC4_TEST_TUTORIAL_OFF=1: a tutorial
// holds button presses back. The tax rates, the loan and the car change the
// city, alike on both sides.
void DriveDialogs(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_DIALOGS") || !game::IsBuild641())
        return;
    char* const view = View();
    if (!view) {
        log::Write("autotest: dialogs: no city view");
        return;
    }
    using ThisFn = void(__thiscall*)(void*);
    Fnv budget, options, pick, rows;
    int did = 0;

    g_played = Fnv{};
    if (char* const panel = At<char*>(view, 0x2E8); panel && At<void*>(panel, 0x10)) {
        g_budgetPanel = panel;
        void* const simulator = Virtual<void*>(day.city, 0xD0);
        RunModal(&PlayTaxes, [&] { reinterpret_cast<ThisFn>(addr::SC4UI_F768130)(panel); });
        if (simulator)
            for (int32_t group = 0; group < 12; ++group)
                budget.Add(static_cast<float>(Virtual<double>(simulator, 0x90, group)));
        RunModal(&PlayLoans, [&] { reinterpret_cast<ThisFn>(addr::SC4UI_F765C30)(panel); });
        if (simulator)
            budget.Add(Virtual<int64_t>(simulator, 0xA4));
        for (const uint32_t which : {1u, 2u, 0x0A5A72D1u, 0xAA369059u, 0x4A357B40u}) {
            RunModal(&PlayNothing,
                     [&] { reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::SC4UI_F768460)(panel, which); });
            budget.Add(At<void*>(panel, 0x18) != nullptr);
        }
        Virtual<void>(panel, 0x1C); // all hidden
        budget.Bytes(panel + 0x180, 0x1F);
        budget.Add(g_played.h);
        g_budgetPanel = nullptr;
        ++did;
    }

    g_played = Fnv{};
    if (void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x10)) {
        char* const dialog = reinterpret_cast<char*(__thiscall*)(void*)>(addr::SC4UI_F76AB30)(memory);
        Virtual<uint32_t>(dialog, 0x4);
        Virtual<uint8_t>(dialog, 0x14, uint32_t{0}); // Init
        if (At<void*>(dialog, 0x4)) {
            g_options = dialog;
            RunModal(&PlayOptions, [&] { reinterpret_cast<ThisFn>(addr::SC4UI_F76AB50)(dialog); });
            g_options = nullptr;
            ++did;
        }
        options.Add(At<void*>(dialog, 0x4) != nullptr);
        if (char* const settings = Global<char*>(0x00B43CD8)) {
            options.Bytes(settings + 0xF08, 4);
            options.Add(At<uint8_t>(settings, 0x4EC));
        }
        options.Add(g_played.h);
        Virtual<uint32_t>(dialog, 0x8);
    }

    g_played = Fnv{};
    if (char* const panel = At<char*>(view, 0x2E4)) {
        reinterpret_cast<ThisFn>(addr::SC4UI_F76FCE0)(panel); // Show
        for (int32_t place = 0; place < 5; ++place) {
            if (!At<void*>(panel, 0x1C + 4 * static_cast<std::size_t>(place)))
                continue;
            reinterpret_cast<void(__thiscall*)(void*, int32_t, uint32_t, uint32_t)>(addr::SC4UI_F771940)(panel, place, 1, 0);
            g_pickPanel = panel;
            for (const uint32_t figures : {0u, 1u}) {
                RunModal(&PlayPick,
                         [&] { reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::SC4UI_F770420)(panel, figures); });
                pick.Add(At<void*>(panel, 0x78) != nullptr);
                pick.Add(At<uint8_t>(panel, 0x74));
            }
            g_pickPanel = nullptr;
            ++did;
            break;
        }
        reinterpret_cast<ThisFn>(addr::SC4UI_F76E8E0)(panel); // Hide
        pick.Add(g_played.h);
    }

    void* const window = At<void*>(view, 0x300);
    char* map = nullptr;
    if (window)
        Virtual<uint8_t>(window, 0x0, 0x67C5BEDEu, &map);
    if (map) {
        Virtual<uint32_t>(map, 0x8);
        const auto state = [&] {
            for (std::size_t at : {std::size_t{0x9A0}, std::size_t{0x9A4}, std::size_t{0x9A8}, std::size_t{0x9AC},
                                   std::size_t{0x9CC}, std::size_t{0x9D4}})
                rows.Add(At<uint32_t>(map, at));
            rows.Add(At<uint8_t>(map, 0x9D8));
        };
        reinterpret_cast<ThisFn>(addr::SC4UI_F7A6290)(map + 0x8); // the controls readied
        state();
        for (const uint32_t button : {0x5002u, 0x5005u, 0x6003u, 0x8004u, 0x8100u, 0x8101u, 0x5108u, 0x5001u}) {
            uint32_t message[4] = {3, 0x287259F6, button, 0};
            rows.Add(static_cast<uint8_t>(
                reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t*)>(addr::SC4UI_F7A56E0)(map + 0x8, &message[0])));
            state();
        }
        reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::SC4UI_F7A6270)(map, 0); // the plain view again
        reinterpret_cast<ThisFn>(addr::SC4UI_F7A64D0)(map + 0x8);                         // hidden
        state();
        ++did;
    }
    std::snprintf(g_dialogsToken, sizeof g_dialogsToken, " dialogs %d budget %08X options %08X pick %08X rows %08X", did,
                  budget.h, options.h, pick.h, rows.h);
    log::Write("autotest: dialogs driven:%s", g_dialogsToken);
}

const DayHook kDialogs{"ui.dialogs", 101, &DriveDialogs};

// --- The region view's dialogs, the loading picture, the department pages -----

char g_windowsToken[220];
char* g_pickDialog = nullptr;
uint32_t g_answer = 0;

// The player of the dialogs run through their makers: nothing is pressed
// (the dialog's object is the maker's own), the run ends with g_answer.
uint32_t PlayAnswer(void* window, int depth)
{
    g_played.Add(depth);
    g_played.Add(window && Virtual<void*>(window, 0x8C, uint32_t{0x1000}) != nullptr);
    return g_answer;
}

// The pick-one dialog's player: the rows its list got, then OK with nothing
// chosen and cancel, pressed through the dialog's window procedure.
uint32_t PlayRegionPick(void* window, int)
{
    char* const dialog = g_pickDialog;
    g_played.Add(At<void*>(dialog, 0xC) != nullptr);
    char* const head = At<char*>(dialog, 0x24);
    int rows = 0;
    for (char* node = At<char*>(head, 0x0); node != head && rows < 4096; node = At<char*>(node, 0x0), ++rows)
        g_played.Bytes(node + 0x8, 0xC);
    g_played.Add(rows);
    if (At<void*>(dialog, 0xC))
        for (const uint32_t button : {0x8A5B9959u, 0xCA5B9966u}) {
            uint32_t message[4] = {3, 0x287259F6, button, 0};
            g_played.Add(Virtual<uint8_t>(dialog, 0xC, window, &message[0]));
        }
    return 0;
}

void AddDepartmentState(Fnv& f, char* dialog)
{
    for (std::size_t at : {std::size_t{0x20}, std::size_t{0x24}, std::size_t{0x28}, std::size_t{0x80}, std::size_t{0x84},
                           std::size_t{0x88}, std::size_t{0x98}, std::size_t{0x9C}, std::size_t{0xA0}, std::size_t{0x11C},
                           std::size_t{0x120}, std::size_t{0x124}, std::size_t{0x128}, std::size_t{0x134}})
        f.Add(At<uint32_t>(dialog, at));
    f.Bytes(dialog + 0x90, 8);
    for (std::size_t at : {std::size_t{0x2C}, std::size_t{0x38}, std::size_t{0x44}, std::size_t{0x50}, std::size_t{0x5C},
                           std::size_t{0x68}, std::size_t{0x74}})
        f.Add(static_cast<uint32_t>(At<char*>(dialog, at + 4) - At<char*>(dialog, at)));
    for (std::size_t at : {std::size_t{0x5C}, std::size_t{0x74}})
        if (const char* const begin = At<char*>(dialog, at))
            f.Bytes(begin, static_cast<std::size_t>(At<char*>(dialog, at + 4) - begin));
    const uint32_t count = At<uint32_t>(dialog, 0xA0);
    if (count && count < 4096) {
        if (const char* const ids = At<char*>(dialog, 0xA4))
            f.Bytes(ids, count * 4);
        if (const char* const on = At<char*>(dialog, 0xA8))
            f.Bytes(on, count * 2);
    }
}

// A department dialog's player. Its window procedure (slot 4) takes the
// message apart: (window, type, three numbers). The window's start message
// (0xA2BF8AD0), which builds the page, comes from the modal loop in the
// game, which also puts the window under the main window; the player does
// both. The closing is left to the game: the dialog posts its own end when
// the run is over, and the stop message follows from that. After the start:
// the page's state as built; the four scroll arrows pressed when the page
// has them; on the ordinance page a check box clicked (its state is not
// flipped, so nothing is switched), a description opened (and left for the
// dialog's end to drop: putting it away with its own button here leaves
// the game unable to exit, the original as well), the page filled again; on a group's page the first two rows' buttons pressed,
// each of which opens the department's buildings in a dialog of its own
// (the same class, run inside this run and played by this player: its
// arrows pressed, its rows filled again); the funding rows refreshed.
uint32_t PlayDepartment(void*, int depth)
{
    char* dialog = At<char*>(g_budgetPanel, 0x18);
    if (dialog && depth > 1)
        dialog = At<char*>(dialog, 0x1C);
    void* const window = dialog ? At<void*>(dialog, 0xC) : nullptr;
    if (!window || depth > 2)
        return 0;
    const auto send = [&](uint32_t type, uint32_t first, uint32_t second) {
        return Virtual<uint8_t>(dialog, 0x10, window, type, first, second, uint32_t{0});
    };
    const auto press = [&](uint32_t button) {
        g_played.Add(send(3, 0x287259F6, button));
        AddDepartmentState(g_played, dialog);
    };
    cISC4AppPtr app;
    cIGZWin* const main = app ? app->GetMainWindow() : nullptr;
    if (!main)
        return 0;
    Virtual<uint32_t>(dialog, 0x4);
    const bool added = Virtual<void*>(window, 0x2C) == nullptr && Virtual<uint8_t>(main, 0x38, window); // ChildAdd
    if (!At<uint8_t>(dialog, 0x8))
        g_played.Add(send(0xA2BF8AD0, 0, 0));
    AddDepartmentState(g_played, dialog);
    g_played.Add(depth);
    const uint32_t kind = At<uint32_t>(dialog, 0x20);
    const bool buildings = depth == 2;
    if (Virtual<void*>(window, 0x8C, uint32_t{0x454})) {
        for (const uint32_t button : {0x452u, 0x454u, 0x451u, 0x453u})
            if (Virtual<void*>(window, 0x8C, button))
                press(button);
        if (buildings) {
            reinterpret_cast<void(__thiscall*)(void*, void*, uint8_t)>(addr::SC4UI_F785740)(dialog, window, 0);
            g_played.Add(static_cast<uint32_t>(At<char*>(dialog, 0x48) - At<char*>(dialog, 0x44)));
            g_played.Add(At<uint32_t>(dialog, 0x138));
        } else if (kind == 1) {
            if (Virtual<void*>(window, 0x8C, uint32_t{0x12C}))
                press(0x12C);
            if (Virtual<void*>(window, 0x8C, uint32_t{0x1F4})) {
                press(0x1F4);
                g_played.Add(At<void*>(dialog, 0x14) != nullptr);
            }
            reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::SC4UI_F77ADA0)(dialog, window);
        } else if (kind == 2) {
            reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::SC4UI_F781C90)(dialog, window);
        } else {
            for (const uint32_t button : {0x0ABCE600u, 0x0ABCE601u})
                if (Virtual<void*>(window, 0x8C, button))
                    press(button);
        }
        AddDepartmentState(g_played, dialog);
    }
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4UI_F77AB40)(dialog);
    g_played.Add(added);
    Virtual<uint32_t>(dialog, 0x8);
    return 0;
}

// OPENSC4_TEST_WINDOWS=<day>: the loading screen's picture made, readied
// and shut down (its shuffled lines); the region view's five dialogs run
// under the main window with players in place of the modal loop (new
// region and load region left with cancel, the two questions answered no
// and yes, the pick-one dialog with its list of the region's cities, OK and
// cancel pressed); and the budget panel's ordinance, deals and department
// pages with their arrows and row buttons pressed and filled again, the
// buildings dialog opened from them; and three small classes (below).
// Nothing is made, deleted or changed in the city; the clock window stays
// up. Use with OPENSC4_TEST_TUTORIAL_OFF=1.
void DriveWindows(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_WINDOWS") || !game::IsBuild641())
        return;
    Fnv picture, region, pages;
    int did = 0;

    if (char* const memory = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0xC0))) {
        const int32_t area[4] = {0, 0, 800, 600};
        char* const loading =
            reinterpret_cast<char*(__thiscall*)(void*, const int32_t*, uint8_t)>(addr::SC4UI_F776F90)(memory, &area[0], 0);
        Virtual<uint32_t>(loading, 0x4);
        reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4UI_F7778D0)(loading); // Init
        picture.Add(At<uint8_t>(loading, 0xB8));
        picture.Add(At<uint32_t>(loading, 0x94));
        const char* const begin = At<char*>(loading, 0x20);
        const char* const end = At<char*>(loading, 0x24);
        picture.Add(static_cast<uint32_t>(end - begin));
        for (const char* line = begin; line && line < end; line += 0x14) {
            const WindowString& text = *reinterpret_cast<const WindowString*>(line);
            picture.Bytes(text.begin, static_cast<std::size_t>(text.end - text.begin));
        }
        reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4UI_F777760)(loading); // Shutdown
        picture.Add(At<uint8_t>(loading, 0xB8));
        Virtual<uint32_t>(loading, 0x8);
        ++did;
    }

    cISC4AppPtr app;
    g_played = Fnv{};
    if (cIGZWin* const main = app ? app->GetMainWindow() : nullptr) {
        using IndexFn = int32_t(__cdecl*)(void*);
        using YesFn = uint8_t(__cdecl*)(void*);
        g_answer = 0xFFFFFFFF;
        RunModal(&PlayAnswer, [&] { region.Add(reinterpret_cast<IndexFn>(addr::SC4UI_F777FF0)(main)); });
        RunModal(&PlayAnswer, [&] { region.Add(reinterpret_cast<IndexFn>(addr::SC4UI_F7780C0)(main)); });
        for (const uint32_t answer : {0u, 1u}) {
            g_answer = answer;
            RunModal(&PlayAnswer, [&] { region.Add(reinterpret_cast<YesFn>(addr::SC4UI_F778130)(main)); });
            RunModal(&PlayAnswer, [&] { region.Add(reinterpret_cast<YesFn>(addr::SC4UI_F778220)(main)); });
        }
        if (void* const memory = reinterpret_cast<void*(__cdecl*)(std::size_t)>(addr::SC4_operator_new)(0x68)) {
            char* const dialog =
                reinterpret_cast<char*(__thiscall*)(void*, uint32_t, uint32_t)>(addr::SC4UI_F778C10)(memory, 1, 1);
            Virtual<uint32_t>(dialog, 0x4);
            g_pickDialog = dialog;
            RunModal(&PlayRegionPick, [&] { region.Add(Virtual<int32_t>(dialog, 0x14, static_cast<void*>(main))); });
            g_pickDialog = nullptr;
            Virtual<uint32_t>(dialog, 0x8);
        }
        uint32_t out[3] = {};
        g_answer = 0;
        RunModal(&PlayAnswer, [&] {
            region.Add(reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t, uint32_t, uint32_t*, uint32_t*, uint32_t*, void*)>(
                addr::SC4UI_F778D80)(main, 2, 2, &out[0], &out[1], &out[2], nullptr));
        });
        region.Add(g_played.h);
        ++did;
    }

    g_played = Fnv{};
    char* const view = View();
    if (char* const panel = view ? At<char*>(view, 0x2E8) : nullptr; panel && At<void*>(panel, 0x10)) {
        g_budgetPanel = panel;
        for (const uint32_t which : {1u, 2u, 0x0A5A72D1u, 0xAA369059u, 0x4A357B40u}) {
            RunModal(&PlayDepartment,
                     [&] { reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::SC4UI_F768460)(panel, which); });
            pages.Add(g_played.h);
            log::Write("autotest: windows: department %08X played %08X", which, g_played.h);
        }
        Virtual<void>(panel, 0x1C); // all hidden
        g_budgetPanel = nullptr;
        ++did;
    }
    // Three small classes, as far as they go with no window of their own on
    // show: the HTML control's link handler made, asked for its interface,
    // started, stopped and deleted; the tips window's two answers (was it
    // shown, are there tips; its key list is not asked for: that reads every
    // exemplar again, and twice in some 110 runs the game then crashed on a freed
    // resource record, the original as well as ours); the clock made and deleted, then shown (it runs on the
    // computer's time, so its text is not compared).
    Fnv small;
    using NewFn = void*(__cdecl*)(std::size_t);
    if (char* const links = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x10))) {
        reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4UI_F78D050)(links);
        void* part = nullptr;
        small.Add(Virtual<uint8_t>(links + 0x8, 0x0, 0xC49B192Cu, &part));
        small.Add(part == links + 0x8);
        small.Add(At<uint32_t>(links, 0x4));
        small.Add(Virtual<uint8_t>(links + 0x8, 0xC)); // Init
        small.Add(At<void*>(links, 0xC) != nullptr);
        small.Add(Virtual<uint8_t>(links + 0x8, 0x10)); // Shutdown
        small.Add(At<void*>(links, 0xC) != nullptr);
        Virtual<void*>(links, 0x0, uint32_t{1}); // deleted
    }
    small.Add(reinterpret_cast<uint8_t(__cdecl*)()>(addr::SC4UI_F78C760)());
    small.Add(reinterpret_cast<uint8_t(__cdecl*)()>(addr::SC4UI_F78CCE0)());
    if (char* const clock = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(0x38))) {
        reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4UI_F78C340)(clock);
        small.Add(At<uint32_t>(clock, 0x4));
        small.Add(At<uint32_t>(clock, 0x18));
        Virtual<void*>(clock, 0x14, uint32_t{1}); // deleted
    }
    small.Add(reinterpret_cast<uint8_t(__cdecl*)()>(addr::SC4UI_F78C590)());
    small.Add(reinterpret_cast<uint8_t(__cdecl*)()>(addr::SC4UI_F78C590)());
    ++did;

    std::snprintf(g_windowsToken, sizeof g_windowsToken, " windows %d picture %08X region %08X pages %08X small %08X", did,
                  picture.h, region.h, pages.h, small.h);
    log::Write("autotest: windows driven:%s", g_windowsToken);
}

const DayHook kWindows{"ui.windows", 102, &DriveWindows};

// --- Shared helpers, menus, the advice list and small windows ------------------

char g_panelsToken[240];

// A game string (0x14 bytes: vtable, begin, end, capacity, a zero) made
// from text by the game's constructor and freed as the game frees one.
struct OwnString {
    char bytes[0x14];
    explicit OwnString(const char* text)
    {
        reinterpret_cast<void*(__thiscall*)(void*, const char*)>(addr::SC4String_FromText)(&bytes[0], text);
    }
    ~OwnString()
    {
        if (void* const buffer = At<void*>(&bytes[0], 0x4))
            reinterpret_cast<void(__cdecl*)(void*)>(addr::SC4_Free)(buffer);
    }
    OwnString(const OwnString&) = delete;
    OwnString& operator=(const OwnString&) = delete;
};

void AddText(Fnv& f, char* string)
{
    const WindowString& text = *reinterpret_cast<const WindowString*>(string);
    if (text.begin)
        f.Bytes(text.begin, static_cast<std::size_t>(text.end - text.begin));
    f.Add(uint8_t{0});
}

// The graphics options dialog's player: a button of each choice group
// pressed through the window procedure, the boxes read back by the
// dialog's own fill. Nothing is accepted: the settings stay as they are.
char* g_graphics = nullptr;

uint32_t PlayGraphics(void*, int)
{
    char* const dialog = g_graphics;
    void* const gen = At<void*>(dialog, 0x4);
    void* const window = gen ? Virtual<void*>(gen, 0xC) : nullptr;
    g_played.Add(window != nullptr);
    if (!window)
        return 0;
    for (const uint32_t button : {0x6A57DA4Au, 0x6A57DA4Du, 0x6A57DA50u, 0x2AB6C374u, 0xAB81DED6u, 0x2B85CF9Fu}) {
        uint32_t message[4] = {3, 0x287259F6, button, 0};
        g_played.Add(Virtual<uint8_t>(dialog, 0xC, window, &message[0]));
    }
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4UI_F78FFC0)(dialog); // the boxes from the settings again
    g_played.Bytes(dialog + 0x10, 0xF);
    return 0;
}

// OPENSC4_TEST_PANELS=<day>: the interface's shared helpers and small
// windows called directly, with no window of their own on show: a colour
// ramp; the child under a few points of the main window; the main
// window's children collected into a set; a link followed that leads
// nowhere; a build menu made from the game's exemplars (its items' names,
// descriptions, commands and IDs) and worked (found, chosen, sized, the
// others switched off); the advice list control made through the class
// factory, given rows and emptied; an info tip's texts, place and meter;
// the progress bar's share; the frame window's edge test over a grid; the
// cheat box, the intro window and the picture file dialog made and
// deleted; the message, question and plain modal windows run with a player
// that answers; the graphics options dialog started, a button of each
// group pressed, left without accepting. Nothing changes the city or the
// settings. Use with OPENSC4_TEST_TUTORIAL_OFF=1.
void DrivePanels(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_PANELS") || !game::IsBuild641())
        return;
    using NewFn = void*(__cdecl*)(std::size_t);
    using ThisFn = void(__thiscall*)(void*);
    const auto make = [](std::size_t size) {
        char* const memory = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(size));
        if (memory)
            std::memset(memory, 0, size);
        return memory;
    };
    cISC4AppPtr app;
    cIGZWin* const main = app ? app->GetMainWindow() : nullptr;
    Fnv helpers, menus, lists, tips, small, modal;
    int did = 0;

    {
        uint32_t ramp[256] = {};
        const uint32_t pairs[8] = {16, 0xFF102030, 96, 0xFF80C0E0, 160, 0x00FFFFFF, 240, 0xFF000000};
        reinterpret_cast<void(__cdecl*)(uint32_t*, int32_t, const uint32_t*, int32_t)>(addr::SC4UI_F78D520)(&ramp[0], 256, &pairs[0],
                                                                                                             8);
        helpers.Bytes(&ramp[0], sizeof ramp);
        OwnString action("sc4://action/1?x"), nothing("sc4://nothing/1"), plain("plain");
        char none = 0;
        for (OwnString* link : {&action, &nothing, &plain})
            helpers.Add(reinterpret_cast<uint8_t(__thiscall*)(void*, void*, uint32_t)>(addr::SC4UI_FollowLink)(&none, &link->bytes[0],
                                                                                                                 0));
        if (main) {
            using AtFn = void*(__cdecl*)(void*, int32_t, int32_t);
            for (const int32_t x : {8, 200, 500, 900})
                for (const int32_t y : {8, 300, 700}) {
                    void* const child = reinterpret_cast<AtFn>(addr::SC4UI_F78D6E0)(main, x, y);
                    void* const deepest = reinterpret_cast<AtFn>(addr::SC4UI_F78D720)(main, x, y);
                    helpers.Add(child ? Virtual<uint32_t>(child, 0xFC) : 0u);
                    helpers.Add(deepest ? Virtual<uint32_t>(deepest, 0xFC) : 0u);
                }
            struct Set {
                char* head;
                uint32_t count;
            } set{};
            set.head = static_cast<char*>(reinterpret_cast<void*(__cdecl*)(uint32_t)>(addr::SC4_stl_allocate)(0x14));
            std::memset(set.head, 0, 0x14);
            At<char*>(set.head, 0x8) = set.head;
            At<char*>(set.head, 0xC) = set.head;
            helpers.Add(reinterpret_cast<uint32_t(__cdecl*)(void*, void*)>(addr::SC4UIWindows_Collect)(main, &set));
            helpers.Add(set.count);
            helpers.Add(reinterpret_cast<uint32_t(__cdecl*)(void*, void*)>(addr::SC4UIWindows_Collect)(main, &set));
        }
        ++did;
    }

    using MenuFn = char*(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t);
    char* advice = nullptr;
    if (char* const menu = reinterpret_cast<MenuFn>(addr::SC4UI_F78F100)(0x2A3858E4, 0x12345678, 0, 0)) {
        Virtual<uint32_t>(menu, 0x4);
        char** const begin = At<char**>(menu, 0xDC);
        char** const end = At<char**>(menu, 0xE0);
        const uint32_t count = static_cast<uint32_t>(end - begin);
        menus.Add(count);
        uint32_t firstId = 0;
        for (char** item = begin; item != end; ++item) {
            AddText(menus, *item + 0x4);
            AddText(menus, *item + 0x18);
            menus.Add(At<uint32_t>(*item, 0x34));
            menus.Add(At<uint32_t>(*item, 0x38));
            menus.Add(At<uint8_t>(*item, 0x40));
            if (item == begin)
                firstId = At<uint32_t>(*item, 0x38);
        }
        reinterpret_cast<void(__thiscall*)(void*, int32_t, int32_t, int32_t)>(addr::SC4UI_F79A0E0)(menu, 180, 22, 2);
        int32_t size[2] = {};
        reinterpret_cast<int32_t*(__thiscall*)(void*, int32_t*, uint32_t)>(addr::SC4UI_F79A620)(menu, &size[0], 5);
        menus.Bytes(&size[0], sizeof size);
        if (count) {
            void* const found = reinterpret_cast<void*(__thiscall*)(void*, uint32_t)>(addr::SC4UI_F79A3F0)(menu, firstId);
            menus.Add(found == *begin);
            reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(addr::SC4UI_F79A850)(menu, firstId);
            menus.Add(reinterpret_cast<uint32_t(__thiscall*)(void*)>(addr::SC4UI_F79A820)(menu) == firstId);
            reinterpret_cast<void(__thiscall*)(void*, uint32_t, uint32_t)>(addr::SC4UI_F79A430)(menu, firstId, 0);
            for (char** item = begin; item != end; ++item)
                menus.Add(At<uint8_t>(*item, 0x40));
        }
        menus.Add(reinterpret_cast<void*(__thiscall*)(void*, uint32_t)>(addr::SC4UI_F79A3F0)(menu, 0xFFFFFFF0) != nullptr);
        Virtual<uint32_t>(menu, 0x8);
        ++did;
    }
    if ((advice = make(0x44)) != nullptr) { // a menu item of the test's own: a counted object for the list's rows to hold
        reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4UI_F79B6B0)(advice);
        Virtual<uint32_t>(advice, 0x4);
        menus.Add(At<uint8_t>(advice, 0x40));
        reinterpret_cast<void(__thiscall*)(void*, uint8_t)>(addr::SC4UI_F79B740)(advice, 0);
        menus.Add(At<uint8_t>(advice, 0x40));
        menus.Add(At<uint32_t>(advice, 0x3C));
    }

    if (advice) {
        using CreateFn = char*(__cdecl*)();
        if (char* const part = reinterpret_cast<CreateFn>(addr::SC4UI_F793D20)()) {
            Virtual<uint32_t>(part, 0x4);
            char* const window = part - 0xD8;
            OwnString open("open.png"), shut("shut.png"), close("close.png"), message("<b>message</b>"), title("Title"), other("Other");
            const auto add = [&](OwnString& theTitle, uint8_t isOpen, uint8_t isPlain, uint32_t background) {
                Virtual<void>(part, 0x10, static_cast<void*>(&open.bytes[0]), static_cast<void*>(&shut.bytes[0]),
                              static_cast<void*>(&message.bytes[0]), static_cast<void*>(&theTitle.bytes[0]), uint32_t{isOpen},
                              static_cast<void*>(advice), static_cast<void*>(&close.bytes[0]), uint32_t{isPlain}, background);
            };
            const auto rows = [&] {
                char* const head = At<char*>(window, 0xE0);
                int count = 0;
                for (char* node = At<char*>(head, 0x0); node != head && count < 64; node = At<char*>(node, 0x0), ++count) {
                    char* const row = node + 0x8;
                    for (std::size_t at : {std::size_t{0}, std::size_t{0x14}, std::size_t{0x28}, std::size_t{0x3C}, std::size_t{0x50}})
                        AddText(lists, row + at);
                    lists.Add(At<uint8_t>(row, 0x64));
                    lists.Add(At<void*>(row, 0x68) == static_cast<void*>(advice));
                    lists.Add(At<uint8_t>(row, 0x6C));
                }
                lists.Add(count);
                lists.Add(At<uint32_t>(window, 0xE4));
            };
            add(title, 1, 0, 0x11111111);
            add(other, 0, 1, 0x22222222);
            add(title, 0, 0, 0x33333333);
            rows();
            Virtual<void>(part, 0x1C); // Clear
            rows();
            add(other, 1, 1, 0x44444444);
            rows();
            Virtual<uint32_t>(part, 0x8);
            ++did;
        }
        lists.Add(At<uint32_t>(advice, 0x3C));
        Virtual<uint32_t>(advice, 0x8);
    }

    if (char* const tip = make(0x1B4)) {
        reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4InfoTip_Construct)(tip);
        Virtual<uint32_t>(tip, 0x4);
        OwnString title("A title"), text("Some text of the tip");
        const auto state = [&] {
            AddText(tips, Virtual<char*>(tip, 0x1C));
            AddText(tips, Virtual<char*>(tip, 0x24));
            tips.Bytes(Virtual<char*>(tip, 0x40), 8);
            tips.Bytes(Virtual<char*>(tip, 0x48), 16);
            tips.Bytes(Virtual<char*>(tip, 0x50), 16);
            tips.Add(At<uint32_t>(tip, 0x12C));
            tips.Add(At<uint32_t>(tip, 0x14C));
            tips.Add(At<uint32_t>(tip, 0x150));
        };
        state();
        Virtual<void>(tip, 0x18, static_cast<void*>(&title.bytes[0]));
        Virtual<void>(tip, 0x20, static_cast<void*>(&text.bytes[0]));
        state();
        const int32_t point[2] = {120, 80};
        const int32_t clear[4] = {100, 60, 140, 100};
        const int32_t bounds[4] = {16, 16, 1000, 740};
        Virtual<void>(tip, 0x3C, &point[0]);
        Virtual<void>(tip, 0x44, &clear[0]);
        Virtual<void>(tip, 0x4C, &bounds[0]);
        state();
        Virtual<void>(tip, 0x3C, &point[0]); // the same again
        state();
        Virtual<void>(tip, 0x10, static_cast<void*>(nullptr), int32_t{-1}, int32_t{-1}); // the frame's margins
        for (const float share : {-1.0f, 0.5f, 2.0f}) {
            Virtual<void>(tip, 0x38, static_cast<void*>(nullptr), uint32_t{3}, share);
            state();
        }
        Virtual<uint32_t>(tip, 0x8);
        ++did;
    }

    using CreateFn = char*(__cdecl*)();
    if (char* const bar = reinterpret_cast<CreateFn>(addr::SC4UI_F797F20)()) {
        Virtual<uint32_t>(bar, 0x4);
        small.Bytes(bar + 0x8, 8);
        for (const double share : {-1.0, 0.25, 2.0}) {
            Virtual<void>(bar, 0x10, share);
            small.Bytes(bar + 0x8, 8);
        }
        void* out = nullptr;
        small.Add(Virtual<uint8_t>(bar, 0x0, 0x4A5D1208u, &out));
        small.Add(out == static_cast<void*>(bar));
        if (out)
            Virtual<uint32_t>(bar, 0x8);
        Virtual<uint32_t>(bar, 0x8);
    }
    if (char* const frame = make(0xFC)) {
        reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4UI_F794430)(frame);
        Virtual<uint32_t>(frame, 0x4);
        Virtual<void>(frame, 0xDC, int32_t{100}, int32_t{100}, int32_t{260}, int32_t{220}); // its area
        reinterpret_cast<void(__thiscall*)(void*, uint8_t, uint8_t)>(addr::SC4UI_F794260)(frame, 1, 1);
        for (const int32_t x : {-3, 0, 2, 80, 157, 159, 163})
            for (const int32_t y : {-3, 0, 2, 60, 117, 119, 123}) {
                uint32_t edge = 0, cursor = 0;
                small.Add(reinterpret_cast<uint8_t(__thiscall*)(void*, int32_t, int32_t, uint32_t*, uint32_t*)>(addr::SC4UI_F7944C0)(
                    frame, x, y, &edge, &cursor));
                small.Add(edge);
                small.Add(cursor);
            }
        Virtual<uint32_t>(frame, 0x8);
    }
    if (char* const box = make(0x120)) { // the cheat box
        reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4UI_F79BD00)(box);
        small.Bytes(box + 0x110, 0x10);
        small.Add(reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, uint32_t)>(addr::SC4UI_F79C400)(box, 0x41, 0));
        reinterpret_cast<void*(__thiscall*)(void*, uint32_t)>(addr::SC4UI_F79BD80)(box, 1);
    }
    if (char* const intro = make(0x108)) {
        reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4UI_F79CED0)(intro);
        void* out = nullptr;
        small.Add(reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, void**)>(addr::SC4UI_F79CF20)(intro, 0x2A3832AA, &out));
        small.Add(out == static_cast<void*>(intro));
        reinterpret_cast<ThisFn>(addr::SC4UI_F79D400)(intro); // Skip: nothing to skip
        small.Add(reinterpret_cast<uint8_t(__thiscall*)(void*, uint32_t, uint32_t)>(addr::SC4UI_F79D470)(intro, 0x1B, 0));
        small.Bytes(intro + 0xD8, 0xC);
        if (out)
            Virtual<uint32_t>(intro, 0x8);
        else
            reinterpret_cast<void*(__thiscall*)(void*, uint32_t)>(addr::SC4UI_F79D610)(intro, 1);
    }
    if (char* const picker = make(0xF0)) { // the picture file dialog
        reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4UI_F79D810)(picker);
        Virtual<uint32_t>(picker, 0x4);
        OwnString path("C:\\pictures\\one.png"), read("");
        reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::SC4UI_F79DBB0)(picker, &path.bytes[0]);
        reinterpret_cast<void(__thiscall*)(void*, void*)>(addr::SC4UI_F79DB60)(picker, &read.bytes[0]);
        AddText(small, &read.bytes[0]);
        reinterpret_cast<ThisFn>(addr::SC4UI_F79D670)(picker); // Shutdown with no browser
        Virtual<uint32_t>(picker, 0x8);
    }
    ++did;

    g_played = Fnv{};
    if (main) {
        OwnString title("A title"), text("A text"), yes("Yes"), no("No");
        g_answer = 0;
        RunModal(&PlayAnswer, [&] {
            reinterpret_cast<void(__cdecl*)(void*, void*)>(addr::SC4UI_MessageDialog)(&title.bytes[0], &text.bytes[0]);
        });
        using AskFn = uint8_t(__cdecl*)(void*, void*, void*, void*);
        for (const uint32_t answer : {0u, 0x8A8DFD4Eu}) {
            g_answer = answer;
            RunModal(&PlayAnswer, [&] {
                modal.Add(reinterpret_cast<AskFn>(addr::SC4UI_QuestionDialog)(&title.bytes[0], &text.bytes[0], &yes.bytes[0],
                                                                              &no.bytes[0]));
            });
        }
        g_answer = 0x1234;
        RunModal(&PlayAnswer, [&] {
            modal.Add(reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t)>(addr::SC4UI_F78E2F0)(0xCA8CBF0F, 0xAA8DEF97));
        });
        if (char* const dialog = make(0x20)) {
            reinterpret_cast<void*(__thiscall*)(void*)>(addr::SC4UI_F791360)(dialog);
            Virtual<uint32_t>(dialog, 0x4);
            modal.Add(Virtual<uint8_t>(dialog, 0x14, uint32_t{0})); // Init
            modal.Bytes(dialog + 0x10, 0xF);
            if (At<void*>(dialog, 0x4)) {
                g_graphics = dialog;
                RunModal(&PlayGraphics, [&] { modal.Add(reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::SC4UI_F7913A0)(dialog)); });
                g_graphics = nullptr;
            }
            modal.Add(At<void*>(dialog, 0x4) != nullptr);
            Virtual<uint32_t>(dialog, 0x8);
        }
        modal.Add(g_played.h);
        ++did;
    }

    std::snprintf(g_panelsToken, sizeof g_panelsToken, " panels %d helpers %08X menus %08X lists %08X tips %08X small %08X modal %08X",
                  did, helpers.h, menus.h, lists.h, tips.h, small.h, modal.h);
    log::Write("autotest: panels driven:%s", g_panelsToken);
}

const DayHook kPanels{"ui.panels", 103, &DrivePanels};


void Fingerprint(cISC4City*, std::string& line)
{
    line += g_budgetToken;
    line += g_dialogsToken;
    line += g_windowsToken;
    line += g_panelsToken;
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
