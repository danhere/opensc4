// Tests of the city view (cSC4View3DWin; docs/ui.md) that its own test
// (ui_autotest.cpp, OPENSC4_TEST_CITY_VIEW) keeps away from: zoom and
// rotation, which hold the simulator and move the camera, and the menus,
// panels and modes worked the way their buttons work them.
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
#include <cstring>
#include <utility>
#include <cstdio>
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

// The view's members used here (docs/ui.md) and the camera control's
// (docs/renderer.md, "The camera control").
constexpr std::size_t kCamera = 0xF0;
constexpr std::size_t kHeld = 0x178;    // byte: the view holds the simulator
constexpr std::size_t kZoomMin = 0x364; // two ints: the view's zoom limits
constexpr std::size_t kCameraCenter = 0x44; // three floats
constexpr std::size_t kCameraFine = 0xF0;   // float: the fine zoom
constexpr std::size_t kCameraOld = 0x100;   // six ints: zoom and rotation before, now, after
constexpr std::size_t kCameraZoom = 0x108;
constexpr std::size_t kCameraRotation = 0x10C;

std::string g_zoomToken;

// OPENSC4_TEST_CITY_ZOOM=<day>: on that run day the city view's zoom and
// rotation methods are called through its interface, as the buttons and
// keys call them: every zoom set, limits narrowed and widened again, zoomed
// out and in as far as each goes and once more, every rotation set, turned
// left and right four times each, zoom and rotation set together. After
// each call the token takes what the call answered, the camera's zoom and
// rotation values (before, now, after), its fine zoom, whether the view may
// zoom in and out, its limits and whether it holds the simulator. The
// `home` part is the camera's centre after everything is put back, against
// the centre at the start (1 when equal); `changed` counts the calls that
// answered yes and `zooms` and `rotations` are the sets of values the camera
// took. At the end the view lets the
// simulator go (0x007E5B70), as its own timer does two seconds after the
// last change.
void DriveZoom(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_CITY_ZOOM") || !game::IsBuild641())
        return;
    char* const view = View();
    char* const camera = view ? At<char*>(view, kCamera) : nullptr;
    if (!camera) {
        g_zoomToken = " | cvzoom none";
        log::Write("autotest: city view zoom: no view");
        return;
    }
    Fnv f;
    int calls = 0, changed = 0;
    uint32_t zooms = 0, rotations = 0; // bit n: the camera was at zoom or rotation n after a call
    const auto note = [&](uint8_t answer) {
        ++calls;
        changed += answer != 0;
        zooms |= 1u << (At<uint32_t>(camera, kCameraZoom) & 31);
        rotations |= 1u << (At<uint32_t>(camera, kCameraRotation) & 31);
        f.Add(answer);
        f.Bytes(camera + kCameraOld, 0x18);
        f.Bytes(camera + kCameraFine, 4);
        f.Add(Virtual<uint8_t>(view, 0x30));
        f.Add(Virtual<uint8_t>(view, 0x34));
        f.Bytes(view + kZoomMin, 8);
        f.Add(At<uint8_t>(view, kHeld));
    };
    const int32_t zoom = At<int32_t>(camera, kCameraZoom);
    const int32_t rotation = At<int32_t>(camera, kCameraRotation);
    const int32_t limits[2] = {At<int32_t>(view, kZoomMin), At<int32_t>(view, kZoomMin + 4)};
    float center[3];
    std::memcpy(center, camera + kCameraCenter, sizeof center);
    f.Add(zoom);
    f.Add(rotation);
    note(0);

    for (const int32_t wanted : {0, 1, 2, 3, 4, 5, 6, -1, 2})
        note(Virtual<uint8_t>(view, 0x20, wanted)); // SetZoom
    note(Virtual<uint8_t>(view, 0x2C, int32_t{1}, int32_t{3})); // SetZoomLimits
    note(Virtual<uint8_t>(view, 0x20, int32_t{0}));
    note(Virtual<uint8_t>(view, 0x20, int32_t{4}));
    note(Virtual<uint8_t>(view, 0x2C, int32_t{3}, int32_t{3}));
    note(Virtual<uint8_t>(view, 0x2C, int32_t{0}, int32_t{9}));
    note(Virtual<uint8_t>(view, 0x2C, limits[0], limits[1]));
    for (int i = 0; i < 7; ++i)
        note(Virtual<uint8_t>(view, 0x28)); // ZoomOut
    for (int i = 0; i < 7; ++i)
        note(Virtual<uint8_t>(view, 0x24)); // ZoomIn
    for (const int32_t wanted : {0, 1, 2, 3, 3, 4, -1, 0})
        note(Virtual<uint8_t>(view, 0x38, wanted)); // SetRotation
    for (int i = 0; i < 4; ++i)
        note(Virtual<uint8_t>(view, 0x3C)); // RotateLeft
    for (int i = 0; i < 4; ++i)
        note(Virtual<uint8_t>(view, 0x40)); // RotateRight
    for (const auto& both : {std::pair<int32_t, int32_t>{1, 2}, {1, 2}, {9, 1}, {3, 7}, {0, 3}})
        note(Virtual<uint8_t>(view, 0x44, both.first, both.second)); // SetZoomAndRotation

    note(Virtual<uint8_t>(view, 0x44, zoom, rotation));
    const bool home = std::memcmp(center, camera + kCameraCenter, sizeof center) == 0;
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4UI_F7E5B70)(view);
    f.Add(At<uint8_t>(view, kHeld));

    char text[128];
    std::snprintf(text, sizeof text, " | cvzoom %d %08X changed %d zooms %02X rotations %02X at %d %d home %d", calls, f.h,
                  changed, zooms, rotations, At<int32_t>(camera, kCameraZoom), At<int32_t>(camera, kCameraRotation),
                  home ? 1 : 0);
    g_zoomToken = text;
    log::Write("autotest: city view zoom driven:%s", text);
}

std::string g_menuToken;

// What the menu test reads of the view after each step (docs/ui.md): the
// mode, the two menus (what each was opened for, whether its window is
// there, its ID, visibility, place and children), the remembered choice of
// every menu, the open build menu's rows, the tools in hand, the view
// window's children, the five window sets, the bars, the mode buttons and
// which panels are shown. Pointers go in as "there or not" only.
struct MenuProbe {
    char* view;
    char* window;
    Fnv f;
    int steps = 0;
    int missed = 0; // presses whose button was not found among the view's windows

    static uint8_t Checked(void* parent, uint32_t id)
    {
        return reinterpret_cast<uint8_t(__cdecl*)(void*, uint32_t)>(addr::SC4Win_IsChildChecked)(parent, id);
    }
    int32_t Tools() const { return static_cast<int32_t>(At<char*>(view, 0x138) - At<char*>(view, 0x134)) / 4; }
    void AddWindow(void* w)
    {
        f.Add(w != nullptr);
        if (!w)
            return;
        f.Add(Virtual<uint32_t>(w, 0xFC));
        f.Add(Virtual<uint8_t>(w, 0x11C));
        for (const std::size_t slot : {0xA4u, 0xA8u, 0xACu, 0xB0u})
            f.Add(Virtual<int32_t>(w, slot));
        f.Add(Virtual<int32_t>(w, 0x34));
    }
    void AddText(char* string)
    {
        const char* const begin = At<const char*>(string, 0x4);
        const char* const end = At<const char*>(string, 0x8);
        if (begin && end >= begin)
            f.Bytes(begin, static_cast<std::size_t>(end - begin));
        f.Add(uint8_t{0});
    }
    void Snapshot()
    {
        ++steps;
        f.Add(At<int32_t>(view, 0x1F4));
        f.Add(At<uint32_t>(view, 0x200));
        f.Add(At<uint32_t>(view, 0x204));
        char* const place = At<char*>(view, 0x208);
        f.Add(place ? static_cast<int32_t>(place - view) : 0);
        f.Bytes(view + 0x2D4, 3);
        f.Bytes(view + 0x370, 8);
        f.Bytes(view + 0x20C, 0x2D4 - 0x20C);
        f.Add(Tools());
        void* const tool = Virtual<void*>(view, 0x60);
        f.Add(tool ? Virtual<uint32_t>(tool, 0x14) : 0u);
        f.Add(static_cast<int32_t>(At<char*>(view, 0x144) - At<char*>(view, 0x140)) / 4);
        f.Add(Virtual<int32_t>(window, 0x34));
        AddWindow(At<void*>(view, 0x1F8));
        void* const frame = At<void*>(view, 0x1FC);
        AddWindow(frame);
        if (frame) {
            char* menu = nullptr;
            if (Virtual<uint8_t>(frame, 0x94, 0x8A2CAD8Bu, 0x49D98264u, &menu) && menu) {
                f.Bytes(menu + 0xE8, 0x1C);
                for (char** item = At<char**>(menu, 0xDC); item != At<char**>(menu, 0xE0); ++item) {
                    AddText(*item + 0x4);
                    AddText(*item + 0x18);
                    f.Add(At<uint32_t>(*item, 0x2C));
                    f.Add(At<uint32_t>(*item, 0x34));
                    f.Add(At<uint32_t>(*item, 0x38));
                    f.Add(At<uint8_t>(*item, 0x40));
                }
                Virtual<uint32_t>(menu, 0x8);
            } else {
                f.Add(uint8_t{0xEE});
            }
        }
        for (int set = 0; set < 5; ++set) {
            void** const begin = At<void**>(view, 0x1A4 + 0xC * static_cast<std::size_t>(set));
            void** const end = At<void**>(view, 0x1A8 + 0xC * static_cast<std::size_t>(set));
            f.Add(static_cast<int32_t>(end - begin));
            for (void** w = begin; w != end; ++w) {
                f.Add(Virtual<uint32_t>(*w, 0xFC));
                f.Add(Virtual<uint8_t>(*w, 0x11C));
                f.Add(Virtual<uint8_t>(*w, 0x10C, uint32_t{0x1000}));
            }
        }
        void* const bar = At<void*>(view, 0x18C);
        f.Add(bar ? Virtual<uint8_t>(bar, 0x11C) : uint8_t{2});
        void* const small = At<void*>(view, 0x194);
        f.Add(small ? Virtual<uint8_t>(small, 0x11C) : uint8_t{2});
        if (bar)
            for (const uint32_t id : {0x2988BC85u, 0xC988BC79u, 0x4988BC6Au, 0x8988BC94u})
                f.Add(Checked(bar, id));
        void* const budget = At<void*>(view, 0x2E8);
        f.Add(budget ? Virtual<uint8_t>(budget, 0x24) : uint8_t{2});
        void* const map = At<void*>(view, 0x300);
        f.Add(map ? Virtual<uint8_t>(map, 0x11C) : uint8_t{2});
        void* const graphs = At<void*>(view, 0x2F8);
        f.Add(graphs ? reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::SC4UI_F76B6A0)(graphs) : uint8_t{2});
        void* const data = At<void*>(view, 0x30C);
        f.Add(data ? Virtual<uint8_t>(data, 0x11C) : uint8_t{2});
        void* const style = At<void*>(view, 0x2FC);
        f.Add(style ? reinterpret_cast<uint8_t(__thiscall*)(void*)>(addr::SC4UI_F7696C0)(style) : uint8_t{2});
    }
    // A press of a button, as the button's window reports it to the view.
    void Click(void* from, uint32_t id)
    {
        uint32_t message[4] = {3, 0x287259F6, id, 0};
        f.Add(from ? Virtual<uint8_t>(view + 8, 0xC, from, &message[0]) : uint8_t{0xEE});
        Snapshot();
    }
    // The window a button of the view's panels sits in, or null.
    void* ParentOf(uint32_t button)
    {
        void* const found = Virtual<void*>(window, 0x8C, button);
        return found ? Virtual<void*>(found, 0x2C) : nullptr;
    }
    void ClickOwn(uint32_t id)
    {
        void* const from = ParentOf(id);
        if (!from) {
            ++missed;
            log::Write("autotest: city view menus: button %08X is not loaded", id);
        }
        Click(from, id);
    }
    void Cancel()
    {
        f.Add(Virtual<uint8_t>(view, 0x1C, 0x6A935CF1u, static_cast<void*>(nullptr), uint32_t{0}));
        Snapshot();
    }
    void NoTools()
    {
        for (int i = 0; i < 8 && Tools() > 0; ++i)
            Virtual<uint8_t>(view, 0x1C, 0x6A935CF1u, static_cast<void*>(nullptr), uint32_t{0});
    }
    // A mode button set down or up without its own notice, then the notice
    // sent to the view as the button sends it.
    void Mode(uint32_t button, bool down)
    {
        void* const bar = At<void*>(view, 0x18C);
        void* control = nullptr;
        if (!bar || !Virtual<uint8_t>(bar, 0x94, button, uint32_t{0x8810}, &control) || !control) {
            f.Add(uint8_t{0xEE});
            return;
        }
        Virtual<void>(control, 0x14, uint32_t{1}, static_cast<uint32_t>(down));
        Virtual<uint32_t>(control, 0x8);
        uint32_t message[4] = {3, 0x287259F7, button, 0};
        f.Add(Virtual<uint8_t>(view + 8, 0xC, bar, &message[0]));
        Snapshot();
    }
};

// OPENSC4_TEST_CITY_MENUS=<day> (with OPENSC4_TEST_TUTORIAL_OFF=1): on that
// run day the live city view's menus, panels and modes are worked the way
// their buttons work them, a snapshot taken after every step: the layout
// pop-ups opened, shut by the same button, replaced by one another and
// stacked; Escape taking one thing at a time; the day and night and the
// signs pop-ups; the disasters menu; then each pop-up opened in turn and
// every build menu button found in it pressed (zones, transport, civic,
// the bar's own, god mode, missions), one menu over the other, with a zone
// row chosen once; the
// five panels asked for, put away and asked for again, and the data view
// left; the modes through the buttons' notices (mayor, MySim, options, god,
// one let up) and through the commands; two window sets shown and hidden;
// the interface minimized and restored. No menu row that places a building
// or starts a mission is chosen. At the end the mode, the panels and the
// remembered choices are put back, and `left` says what stayed open: tools,
// a menu, or more or fewer children of the view's window than at the start;
// `missed` counts the openers whose button no window of the view has and
// `pressed` the menu buttons found and pressed.
void DriveMenus(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_CITY_MENUS") || !game::IsBuild641())
        return;
    char* const view = View();
    void* const bar = view ? At<void*>(view, 0x18C) : nullptr;
    if (!view || !bar || !day.city->GetEstablished()) {
        g_menuToken = " | cvmenu none";
        log::Write("autotest: city view menus: no view, no bar or a city not established");
        return;
    }
    MenuProbe p{view, view + 0xC, Fnv{}, 0, 0};
    using SetFn = void(__thiscall*)(void*, int32_t);
    using PanelFn = void(__thiscall*)(void*, uint32_t);
    const auto showPanel = [&](uint32_t panel) { reinterpret_cast<PanelFn>(addr::SC4UI_F7E6F60)(view, panel); };

    const int32_t mode = At<int32_t>(view, 0x1F4);
    const uint32_t panel[2] = {At<uint32_t>(view, 0x370), At<uint32_t>(view, 0x374)};
    uint8_t pairs[0x2D4 - 0x20C];
    std::memcpy(pairs, view + 0x20C, sizeof pairs);
    const int32_t children = Virtual<int32_t>(p.window, 0x34);
    p.NoTools();
    p.Snapshot();
    const uint32_t start = p.f.h;

    // Layout pop-ups.
    p.Click(bar, 0x0991EE13);
    p.Click(bar, 0x0991EE13);
    p.Click(bar, 0x0991EE39);
    p.Click(bar, 0x6991EE42);
    p.Click(bar, 0xA994824D);
    p.Click(bar, 0xE991EE2F);
    {
        const uint32_t message[4] = {3, 0x287259F6, 1, 0};
        reinterpret_cast<void(__thiscall*)(void*, void*, const uint32_t*)>(addr::SC4UI_F7E7130)(view, bar, &message[0]);
        p.Snapshot();
    }
    p.Cancel();
    p.Cancel();
    p.Click(bar, 0xE991EE2F);
    {
        const uint32_t message[4] = {3, 0x287259F6, 1, 0};
        reinterpret_cast<void(__thiscall*)(void*, void*, const uint32_t*)>(addr::SC4UI_F7E7130)(view, bar, &message[0]);
        p.Snapshot();
    }
    Virtual<void>(view, 0xEC);
    p.Snapshot();
    p.Click(bar, 0xA9ED5617); // day and night
    if (void* const popup = At<void*>(view, 0x1F8))
        for (const uint32_t id : {0xCA35CB74u, 0xCA35CB76u, 0xCA35CB78u})
            p.f.Add(MenuProbe::Checked(popup, id));
    Virtual<void>(view, 0xEC);
    p.Snapshot();
    p.Click(bar, 0xAB9537B7); // signs: a tertiary menu with no secondary one
    if (void* const popup = At<void*>(view, 0x1FC))
        p.f.Add(MenuProbe::Checked(popup, 0xEB9541EB));
    Virtual<void>(view, 0xEC);
    p.Snapshot();
    Virtual<void>(view, 0xF0);
    p.Snapshot();
    const uint32_t popups = p.f.h;

    // Build menus. Their buttons sit in the pop-ups, which are only loaded
    // while open: each pop-up is opened in turn and every menu button found
    // in it is pressed, one menu over the other, the last one twice.
    p.ClickOwn(0x69B9324A); // disasters
    p.ClickOwn(0x69B9324A);
    static const uint32_t kMenuButtons[] = {
        0x29920899, 0xC998AF00, 0xA998AF42, 0x30,                                     // zones
        0x29, 0x31, 0x299237BF, 0x6999BF56, 0xA99234A6, 0xE99234B3, 0x2B730C5B, 0x2B79E398, 0xEA6E379D, // transport
        0x37, 0x38, 0x89DD5405, 0x42, 0x3, 0x09930709, 0x34,                         // civic
        0x35, 0x39, 0x40, 0x32,                                                       // the bar's own
        0xC99231DA, 0x152231F3, 0x6A3D638C, 0x29923215, 0x4A22EA06,                   // god mode
        0xEBFAC717, 0xEBFAC718, 0xEBFAC719};                                          // missions, nothing chosen
    static const uint32_t kOpeners[] = {0x0991EE13, 0x0991EE39, 0x6991EE42, 0xA994824D, 0xE991EE2F,
                                        0x49E95D2B, 0x8991EE08, 0xABB27A7A, 0x36};
    int pressed = 0;
    bool chosen = false;
    for (std::size_t opener = 0; opener < sizeof kOpeners / sizeof kOpeners[0]; ++opener) {
        if (opener < 5)
            p.Click(bar, kOpeners[opener]);
        else
            p.ClickOwn(kOpeners[opener]);
        if (!At<void*>(view, 0x1F8))
            continue;
        uint32_t last = 0;
        for (const uint32_t id : kMenuButtons) {
            void* const from = p.ParentOf(id);
            if (!from)
                continue;
            ++pressed;
            last = id;
            log::Write("autotest: city view menus: %08X pressed in the pop-up of %08X", id, kOpeners[opener]);
            p.Click(from, id);
            void* const frame = At<void*>(view, 0x1FC);
            char* menu = nullptr;
            if (!chosen && frame && Virtual<uint8_t>(frame, 0x94, 0x8A2CAD8Bu, 0x49D98264u, &menu) && menu) {
                // A row chosen once, from the first menu that has two enabled
                // rows of the zone command.
                uint32_t choice[4] = {3, 0x49D993D4, 0, 0};
                int enabled = 0;
                for (char** item = At<char**>(menu, 0xDC); item != At<char**>(menu, 0xE0); ++item)
                    if (At<uint8_t>(*item, 0x40) && At<uint32_t>(*item, 0x34) == 0x4A2B2466 && ++enabled == 2) {
                        choice[2] = At<uint32_t>(*item, 0x34);
                        choice[3] = At<uint32_t>(*item, 0x38);
                    }
                Virtual<uint32_t>(menu, 0x8);
                if (choice[3]) {
                    chosen = true;
                    p.f.Add(Virtual<uint8_t>(p.window, 0xC, &choice[0]));
                    p.Snapshot();
                    p.Cancel();
                    p.Click(from, id);
                }
            }
            p.NoTools();
        }
        if (last)
            if (void* const from = p.ParentOf(last))
                p.Click(from, last);
        p.NoTools();
        Virtual<void>(view, 0xF0);
        p.Snapshot();
        Virtual<void>(view, 0xEC);
        p.Snapshot();
    }
    const uint32_t builds = p.f.h;

    // Panels.
    for (const uint32_t id : {0x41u, 0x99887755u, 0x15200002u, 0x15200003u, 0xABC54125u})
        p.Click(bar, id);
    showPanel(0);
    p.Snapshot();
    showPanel(0);
    p.Snapshot();
    p.Click(bar, 0x15200002);
    p.Click(bar, 0x15200002);
    p.Click(bar, 0xAA9211B3); // the data view left
    const uint32_t panels = p.f.h;

    // Modes.
    p.Click(bar, 0x0991EE13);
    p.Mode(0xC988BC79, true);
    p.Mode(0x4988BC6A, true);
    p.Mode(0x8988BC94, true);
    p.Mode(0x2988BC85, true);
    p.Mode(0x2988BC85, false);
    for (int i = 0; i < 2; ++i) {
        p.f.Add(Virtual<uint8_t>(view, 0x1C, 0x6A935CA4u, static_cast<void*>(nullptr), uint32_t{0}));
        p.Snapshot();
    }
    for (const int32_t set : {3, 4}) {
        reinterpret_cast<SetFn>(addr::SC4UI_F7E8B90)(view, set);
        p.Snapshot();
        reinterpret_cast<SetFn>(addr::SC4UI_F7E8CA0)(view, set);
        p.Snapshot();
    }
    p.Mode(0xC988BC79, true);
    Virtual<void>(view, 0xDC, uint32_t{1});
    p.Snapshot();
    Virtual<void>(view, 0xDC, uint32_t{0});
    p.Snapshot();
    const uint32_t modes = p.f.h;

    // Everything put back.
    static const uint32_t kModeButtons[5] = {0x2988BC85, 0xC988BC79, 0x4988BC6A, 0x8988BC94, 0x2988BC85};
    if (mode >= 0 && mode <= 4)
        p.Mode(kModeButtons[mode], true);
    else if (At<int32_t>(view, 0x1F4) >= 0 && At<int32_t>(view, 0x1F4) <= 4)
        p.Mode(kModeButtons[At<int32_t>(view, 0x1F4)], false);
    p.NoTools();
    Virtual<void>(view, 0xF0);
    Virtual<void>(view, 0xEC);
    showPanel(0);
    if (panel[1] == 0 && panel[0] != 0) {
        At<uint32_t>(view, 0x374) = 0;
        showPanel(panel[0]);
    }
    At<uint32_t>(view, 0x370) = panel[0];
    At<uint32_t>(view, 0x374) = panel[1];
    std::memcpy(view + 0x20C, pairs, sizeof pairs);
    p.Snapshot();
    const int left = (p.Tools() > 0 ? 1 : 0) + (At<void*>(view, 0x1F8) ? 2 : 0) + (At<void*>(view, 0x1FC) ? 4 : 0) +
                     (Virtual<int32_t>(p.window, 0x34) != children ? 8 : 0);

    char text[240];
    std::snprintf(text, sizeof text, " | cvmenu %d start %08X popups %08X builds %08X panels %08X modes %08X end %08X mode %d left %d missed %d pressed %d",
                  p.steps, start, popups, builds, panels, modes, p.f.h, At<int32_t>(view, 0x1F4), left, p.missed, pressed);
    g_menuToken = text;
    log::Write("autotest: city view menus driven:%s", text);
}

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_zoomToken;
    line += g_menuToken;
}

const FingerprintPart kFingerprint{"cityview.drive", 104, &Fingerprint};
const DayHook kZoom{"cityview.zoom", 104, &DriveZoom};
const DayHook kMenus{"cityview.menus", 105, &DriveMenus};

} // namespace
} // namespace opensc4::autotest
