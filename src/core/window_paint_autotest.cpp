// A test that gets the paints of the interface's small windows to run
// (docs/renderer.md, "Small windows"): the ten that no other test reaches,
// because nothing puts their windows on screen in an unattended run. Each
// window is brought up the way the game brings it up where that can be done
// (the cursor's info tip, the animation recorder with its frame, a build
// menu in its frame of arrows, a graph of the graphs window), or made alone
// and given the screen's canvas, and its paint (window slot 88) is called.
// Nothing here compares pixels with the game's: that is the renderer's
// comparison (`PaintCompared`), which sees every paint this test causes.
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

// A game string (0x14 bytes) made from text by the game's constructor and
// freed as the game frees one.
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

// A window's canvas (+0x68) is the picture its paint draws into; the window
// base's paint driver (slot 123) sets the canvas's origin and clip to the
// window's place on it (+0x34) and then calls the paint (slot 88), when the
// window is visible (flag 1 at +0xC8) and marked for painting (+0x70).
constexpr std::size_t kCanvas = 0x68;
constexpr std::size_t kPlace = 0x34;
constexpr std::size_t kMarked = 0x70;
constexpr std::size_t kFlags = 0xC8;
constexpr std::size_t kOwn = 0x64;    // the window's own canvas, when it has one
constexpr std::size_t kShared = 0x71; // byte: the canvas is the window's own or a parent's, not the screen's
constexpr uint32_t kOwnCanvas = 0x10000;
constexpr std::size_t kFill = 0xD4; // the window's fill colour, three bytes
constexpr std::size_t kPaintSlot = 88;

// The paint slot of one window is replaced, for the length of a call of the
// driver, by a function that counts and calls on: that tells whether the
// driver got as far as the paint.
using PaintFn = uint8_t(__thiscall*)(void*);
PaintFn g_paint = nullptr;
int g_paintCalls = 0;
uint8_t g_paintAnswer = 0;
uint8_t __fastcall CountedPaint(void* self, void*)
{
    ++g_paintCalls;
    g_paintAnswer = g_paint(self);
    return g_paintAnswer;
}

struct Painter {
    int painted = 0;  // paints called through the window base's driver
    int direct = 0;   // paints the driver did not reach, called directly
    int skipped = 0;  // windows that could not be brought up
    uint32_t drew = 0; // bit n: paint n changed its window's canvas
    uint32_t answers = 2166136261u;
    uint32_t pixels = 2166136261u; // the canvases in memory after each paint, and the tip's two pictures

    // A picture's memory as a checksum (as the renderer's comparison takes
    // it); 0 when there is none or it cannot be read.
    static uint32_t Sum(void* picture)
    {
        if (!picture)
            return 0;
        const uint8_t* const bits = Virtual<const uint8_t*>(picture, 0x88);
        const uint32_t stride = Virtual<uint32_t>(picture, 0x8C), height = Virtual<uint32_t>(picture, 0x28);
        if (!bits || !stride || !height || height > 4096 || stride > 4096 * 4)
            return 0;
        uint32_t h = 2166136261u;
        for (std::size_t i = 0; i < std::size_t{stride} * height; ++i)
            h = (h ^ bits[i]) * 16777619u;
        return h;
    }
    static uint32_t Pixels(char* window) { return Sum(At<void*>(window, kCanvas)); }

    // The window painted as the window base paints it.
    void PaintOnce(const char* name, const char* how, char* window)
    {
        const uint32_t before = Pixels(window);
        const uint32_t flags = At<uint32_t>(window, kFlags);
        At<uint32_t>(window, kFlags) = flags | 1;
        At<uint8_t>(window, kMarked) = 1;

        // The class's vtable copied with the word before it, the paint slot
        // changed in the copy.
        static std::uintptr_t table[1 + 256];
        std::uintptr_t* const real = At<std::uintptr_t*>(window, 0);
        std::memcpy(&table[0], real - 1, sizeof table);
        g_paint = reinterpret_cast<PaintFn>(table[1 + kPaintSlot]);
        table[1 + kPaintSlot] = reinterpret_cast<std::uintptr_t>(&CountedPaint);
        g_paintCalls = 0;
        At<std::uintptr_t*>(window, 0) = &table[1];
        Virtual<uint8_t>(window, 0x1EC);
        At<std::uintptr_t*>(window, 0) = real;
        const bool reached = g_paintCalls > 0;
        if (!reached) {
            g_paintAnswer = Virtual<uint8_t>(window, 0x160);
            ++direct;
        }
        At<uint32_t>(window, kFlags) = (At<uint32_t>(window, kFlags) & ~1u) | (flags & 1);

        const uint32_t after = Pixels(window);
        if (before != after && painted < 32)
            drew |= 1u << painted;
        ++painted;
        answers = (answers ^ g_paintAnswer) * 16777619u;
        if (before != after)
            pixels = (pixels ^ after) * 16777619u;
        const int32_t* const area = &At<int32_t>(window, kPlace);
        log::Write("autotest: window paints: %s%s painted %s (%d), place %d %d %d %d, canvas %s", name, how,
                   reached ? "by the window base" : "directly", g_paintAnswer, area[0], area[1], area[2], area[3],
                   before != after ? "changed" : "not changed");
        if (before != after)
            log::Write("autotest: window paints: %s%s: canvas %08X", name, how, after);
    }

    // A window without a canvas is first given one the way the base does
    // it (slot 95: its own, a parent's or the screen's). The screen's
    // canvas is not a picture in memory, so a window that paints on it is
    // painted a second time on a canvas of its own (the window flag
    // 0x10000, which the recorder and the graph have from the game): the
    // same paint, with a result that can be read and compared. `prepare`
    // runs before each of the two, for a window that has to be told that
    // its contents are stale.
    template <typename Prepare>
    void Paint(const char* name, char* window, Prepare&& prepare)
    {
        if (!At<void*>(window, kCanvas))
            Virtual<void>(window, 0x17C);
        if (!At<void*>(window, kCanvas)) {
            ++skipped;
            log::Write("autotest: window paints: %s has no canvas", name);
            return;
        }
        const bool screen = At<uint8_t>(window, kShared) == 0;
        prepare();
        PaintOnce(name, screen ? " on the screen's canvas" : "", window);
        const uint32_t flags = At<uint32_t>(window, kFlags);
        if (!screen || (flags & kOwnCanvas))
            return;
        At<uint32_t>(window, kFlags) = flags | 1;
        Virtual<void>(window, 0x110, kOwnCanvas, uint32_t{1});
        if (At<void*>(window, kOwn)) {
            prepare();
            PaintOnce(name, " on a canvas of its own", window);
        } else {
            log::Write("autotest: window paints: %s got no canvas of its own", name);
        }
        Virtual<void>(window, 0x110, kOwnCanvas, uint32_t{0});
        At<uint32_t>(window, kFlags) = (At<uint32_t>(window, kFlags) & ~1u) | (flags & 1);
    }
    void Paint(const char* name, char* window)
    {
        Paint(name, window, [] {});
    }
    void Skip(const char* name, const char* why)
    {
        ++skipped;
        log::Write("autotest: window paints: %s skipped: %s", name, why);
    }
};

using NewFn = void*(__cdecl*)(std::size_t);
using ThisFn = void*(__thiscall*)(void*);

char* Make(std::size_t size, std::uintptr_t constructor)
{
    char* const memory = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(size));
    if (!memory)
        return nullptr;
    std::memset(memory, 0, size);
    reinterpret_cast<ThisFn>(constructor)(memory);
    return memory;
}

// The cursor's info tip (the view's +0x3D8, a child of the view's window):
// a cursor text set through the view's interface fills and shows it. With
// a title and a body, then with a title alone. The tip builds its bubble
// only when a setter has marked it stale, so before each paint it is given
// a new place to point at.
void PaintInfoTip(Painter& p, char* view)
{
    char* const tip = At<char*>(view, 0x3D8);
    if (!tip) {
        p.Skip("info tip", "the view has none");
        return;
    }
    constexpr uint32_t kId = 0x7E570011;
    OwnString title("A title"), body("The body of a cursor text, long enough to be wider than its title"), other("Alone");
    int32_t point[2] = {300, 300};
    const auto moved = [&] {
        point[0] += 7;
        Virtual<void>(tip, 0x3C, &point[0]);
        At<uint32_t>(tip, 0x12C) |= 1; // the contents too
    };
    Virtual<void>(view, 0xA0, kId, uint32_t{5}, static_cast<void*>(&title.bytes[0]), static_cast<void*>(&body.bytes[0]),
                  static_cast<void*>(nullptr));
    const auto built = [&](const char* name) {
        void* const text = At<void*>(tip, 0x144);
        void* const bubble = At<void*>(tip, 0x148);
        log::Write("autotest: window paints: %s built a text picture %d by %d (%08X) and a bubble %d by %d (%08X), flags %X", name,
                   text ? Virtual<int32_t>(text, 0x24) : 0, text ? Virtual<int32_t>(text, 0x28) : 0, Painter::Sum(text),
                   bubble ? Virtual<int32_t>(bubble, 0x24) : 0, bubble ? Virtual<int32_t>(bubble, 0x28) : 0, Painter::Sum(bubble),
                   At<uint32_t>(tip, 0x12C));
        p.pixels = (p.pixels ^ Painter::Sum(text)) * 16777619u;
        p.pixels = (p.pixels ^ Painter::Sum(bubble)) * 16777619u;
    };
    p.Paint("info tip", tip + 4, moved);
    built("info tip");
    Virtual<void>(view, 0xA0, kId, uint32_t{5}, static_cast<void*>(&other.bytes[0]), static_cast<void*>(nullptr),
                  static_cast<void*>(nullptr));
    p.Paint("info tip, title only", tip + 4, moved);
    built("info tip, title only");
    Virtual<void>(view, 0xA4, kId);
}

// The animation recorder and the frame that marks what it records (the
// recorder's +0xD8), brought up by the view's own function. That function
// gives the recorder the ID 0xA802B4EB before it adds it to the view's
// window, and the recorder's Init then takes 0x6ACB1AF1: the view never
// finds it again by the first ID, so it is put away as its Close button
// does it (the window's slot 143), which posts the closing.
void PaintRecorder(Painter& p, char* view)
{
    constexpr uint32_t kRecorderId = 0x6ACB1AF1;
    using ShowFn = uint8_t(__thiscall*)(void*, uint8_t);
    char* const window = view + 0xC;
    if (Virtual<void*>(window, 0x88, kRecorderId)) {
        p.Skip("recorder", "one is up already");
        return;
    }
    reinterpret_cast<ShowFn>(addr::SC4UI_F7F0840)(view, 1);
    char* const recorder = Virtual<char*>(window, 0x88, kRecorderId);
    if (!recorder) {
        p.Skip("recorder", "not made");
        return;
    }
    p.Paint("recorder", recorder);
    if (char* const frame = At<char*>(recorder, 0xD8))
        p.Paint("recorder frame", frame);
    else
        p.Skip("recorder frame", "the recorder has none");
    Virtual<uint8_t>(recorder, 0x23C, 0x287259F6u, 0x080293A4u);
}

// A build menu in its frame of arrows, opened the way the buttons open
// one: a pop-up of the tool bar, then the first menu button found in it.
// Painted as opened and again with the first row moved down by one, which
// changes what the arrows show.
void PaintMenu(Painter& p, char* view, cISC4City* city)
{
    void* const bar = At<void*>(view, 0x18C);
    if (!bar || !city->GetEstablished()) {
        p.Skip("pop-up menu", "no tool bar or a city not established");
        p.Skip("pop-up arrows", "no tool bar or a city not established");
        return;
    }
    char* const window = view + 0xC;
    const auto tools = [&] { return (At<char*>(view, 0x138) - At<char*>(view, 0x134)) / 4; };
    const auto noTools = [&] {
        for (int i = 0; i < 8 && tools() > 0; ++i)
            Virtual<uint8_t>(view, 0x1C, 0x6A935CF1u, static_cast<void*>(nullptr), uint32_t{0});
    };
    const auto click = [&](void* from, uint32_t id) {
        uint32_t message[4] = {3, 0x287259F6, id, 0};
        Virtual<uint8_t>(view + 8, 0xC, from, &message[0]);
    };
    static const uint32_t kOpeners[] = {0x0991EE13, 0x0991EE39, 0x6991EE42, 0xA994824D, 0xE991EE2F};
    static const uint32_t kMenuButtons[] = {0x29920899, 0xC998AF00, 0xA998AF42, 0x30,       0x29,       0x31,       0x299237BF,
                                            0x6999BF56, 0xA99234A6, 0xE99234B3, 0x2B730C5B, 0x2B79E398, 0xEA6E379D, 0x37,
                                            0x38,       0x89DD5405, 0x42,       0x3,        0x09930709, 0x34};
    const int32_t children = Virtual<int32_t>(window, 0x34);
    noTools();
    bool done = false;
    for (const uint32_t opener : kOpeners) {
        click(bar, opener);
        if (!At<void*>(view, 0x1F8))
            continue;
        for (const uint32_t id : kMenuButtons) {
            void* const found = Virtual<void*>(window, 0x8C, id);
            void* const from = found ? Virtual<void*>(found, 0x2C) : nullptr;
            if (!from)
                continue;
            click(from, id);
            char* const frame = At<char*>(view, 0x1FC);
            char* menu = nullptr;
            if (frame && Virtual<uint8_t>(frame, 0x94, 0x8A2CAD8Bu, 0x49D98264u, &menu) && menu) {
                const bool arrows = At<std::uintptr_t>(frame, 0) == addr::SC4MenuArrows_WindowVtable;
                log::Write("autotest: window paints: menu %08X of the pop-up %08X, %d rows", id, opener,
                           At<int32_t>(menu, 0xE8));
                p.Paint("pop-up menu", menu + 4);
                if (arrows)
                    p.Paint("pop-up arrows", frame);
                else
                    p.Skip("pop-up arrows", "the menu's frame is of another class");
                Virtual<void>(menu, 0x40, int32_t{1}); // the first row shown
                p.Paint("pop-up menu, moved", menu + 4);
                if (arrows)
                    p.Paint("pop-up arrows, moved", frame);
                Virtual<void>(menu, 0x40, int32_t{0});
                Virtual<uint32_t>(menu, 0x8);
                done = true;
            }
            noTools();
            if (done)
                break;
        }
        noTools();
        Virtual<void>(view, 0xF0);
        Virtual<void>(view, 0xEC);
        if (done)
            break;
    }
    if (!done) {
        p.Skip("pop-up menu", "no build menu came up");
        p.Skip("pop-up arrows", "no build menu came up");
    }
    if (At<void*>(view, 0x1F8) || At<void*>(view, 0x1FC) || Virtual<int32_t>(window, 0x34) != children)
        log::Write("autotest: window paints: a menu stayed open");
}

// The graph control of the graphs window (the view's +0x2F8; the control
// at the window object's +0x24) with the first graph shown: its paint draws
// the lines through the control's own line function (0x0076B660).
void PaintGraph(Painter& p, char* view)
{
    char* const graphs = At<char*>(view, 0x2F8);
    if (!graphs || !At<void*>(graphs, 0x18) || !At<void*>(graphs, 0x20)) {
        p.Skip("graph", "no graphs window");
        return;
    }
    using ShowFn = void(__thiscall*)(void*, uint32_t);
    const uint32_t number = At<uint32_t>(graphs, 0x14);
    reinterpret_cast<ShowFn>(addr::SC4UI_F76E4B0)(graphs, 1);
    if (char* const chart = At<char*>(graphs, 0x24))
        p.Paint("graph", chart);
    else
        p.Skip("graph", "no chart");
    reinterpret_cast<ShowFn>(addr::SC4UI_F76E4B0)(graphs, number);
    reinterpret_cast<ThisFn>(addr::SC4UI_F76E470)(graphs); // Hide
}

// The progress bar, made by its class factory and left without a parent:
// empty, a quarter full and full as a plain bar, then half full with a
// picture (the info tips' frame picture).
void PaintProgressBar(Painter& p)
{
    char* const bar = reinterpret_cast<char*(__cdecl*)()>(addr::SC4UI_F797F20)(); // the bar's interface, at +0xE0
    if (!bar) {
        p.Skip("progress bar", "not made");
        return;
    }
    Virtual<uint32_t>(bar, 0x4);
    char* const window = bar - 0xE0;
    Virtual<void>(window, 0xDC, int32_t{40}, int32_t{40}, int32_t{240}, int32_t{60});
    At<uint32_t>(window, kFill) = 0x004080C0;
    for (const double share : {0.0, 0.25, 1.0}) {
        Virtual<void>(bar, 0x10, share);
        p.Paint("progress bar", window);
    }
    char loader[0x30] = {};
    uint32_t key[3] = {0x856DDBAC, 0x46A006B0, 0x14416208};
    reinterpret_cast<void*(__thiscall*)(void*, uint32_t*, uint32_t, uint32_t)>(addr::SC4ImageLoader_ConstructKey)(&loader[0],
                                                                                                                 &key[0], 1, 0);
    if (void* const picture = At<void*>(&loader[0], 0x4)) {
        Virtual<void>(bar, 0xC, picture);
        Virtual<void>(bar, 0x10, 0.5);
        p.Paint("progress bar with a picture", window);
        Virtual<void>(bar, 0xC, static_cast<void*>(nullptr));
    } else {
        p.Skip("progress bar with a picture", "the picture did not load");
    }
    reinterpret_cast<ThisFn>(addr::SC4ImageLoader_Destruct)(&loader[0]);
    Virtual<uint32_t>(bar, 0x8);
}

// The picture file dialog and the intro window, made alone as the panels
// test makes them and never started: the dialog's paint fills its area,
// the intro's clears the screen through the driver and counts its steps.
void PaintAlone(Painter& p)
{
    if (char* const picker = Make(0xF0, addr::SC4UI_F79D810)) {
        Virtual<uint32_t>(picker, 0x4);
        Virtual<void>(picker, 0xDC, int32_t{60}, int32_t{80}, int32_t{360}, int32_t{280});
        At<uint32_t>(picker, kFill) = 0x00C08040;
        p.Paint("picture file dialog", picker);
        Virtual<uint32_t>(picker, 0x8);
    } else {
        p.Skip("picture file dialog", "not made");
    }
    if (char* const intro = Make(0x108, addr::SC4UI_F79CED0)) {
        Virtual<uint32_t>(intro, 0x4);
        Virtual<void>(intro, 0xDC, int32_t{0}, int32_t{0}, int32_t{320}, int32_t{200});
        p.Paint("intro", intro);
        p.Paint("intro, again", intro);
        Virtual<uint32_t>(intro, 0x8);
    } else {
        p.Skip("intro", "not made");
    }
}

// An advisor's head view set up for the view's window, its models loaded,
// and the window's paint message sent to it: it takes the window's area
// and draws the head (0x004209D0) through the 3D render.
void PaintHead(Painter& p, char* view)
{
    constexpr uint32_t kWinPaint = 0xA2BF8ACE;
    constexpr uint32_t kWinInit = 0xA2BF8AD5;
    constexpr uint32_t kWinShutdown = 0xA2BF8AD6;
    char* const memory = static_cast<char*>(reinterpret_cast<NewFn>(addr::SC4_operator_new)(0xF8));
    if (!memory) {
        p.Skip("advisor head", "not made");
        return;
    }
    char* const head = reinterpret_cast<char*(__thiscall*)(void*, uint32_t)>(addr::SC4UI_F420B70)(memory, 0x19);
    void* const window = view + 0xC;
    const auto send = [&](uint32_t type) {
        uint32_t message[8] = {type};
        return Virtual<uint8_t>(head, 0xC, window, &message[0]);
    };
    send(kWinInit);
    for (const uint32_t mood : {0u, 3u}) {
        Virtual<uint32_t>(head, 0x24, mood);
        Virtual<uint32_t>(head, 0x2C, uint32_t{1}); // the models
        const bool model = At<void*>(head, 0x24) != nullptr;
        const uint8_t answer = send(kWinPaint);
        ++p.painted;
        p.answers = (p.answers ^ answer) * 16777619u;
        log::Write("autotest: window paints: advisor head drawn (%d), mood %u, %s", answer, mood,
                   model ? "with a model" : "without a model");
    }
    Virtual<uint32_t>(head, 0x2C, uint32_t{0});
    send(kWinShutdown);
    Virtual<void*>(head, 0x14, uint32_t{1}); // delete
}

std::string g_token;

// OPENSC4_TEST_WINDOW_PAINTS=<day> (with OPENSC4_TEST_TUTORIAL_OFF=1, in an
// established city): on that run day every small window above is brought
// up and painted, then put away. The token says how many paints were
// called, how many of them the window base's driver did not reach (called
// directly instead), which changed their window's canvas (a bit
// each, in the order of the calls), a checksum of every canvas a paint
// changed and of the two pictures the info tip builds, and how many windows
// could not be brought up. OPENSC4_TEST_WINDOW_PAINTS_SKIP=<bits> leaves parts out: 1
// the info tip, 2 the recorder, 4 the menu, 8 the graph, 16 the progress
// bar, 32 the dialog and the intro, 64 the advisor head.
void DriveWindowPaints(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_WINDOW_PAINTS") || !game::IsBuild641())
        return;
    char* const view = View();
    if (!view) {
        g_token = " | wpaint none";
        log::Write("autotest: window paints: no view");
        return;
    }
    const int skip = EnvInt("OPENSC4_TEST_WINDOW_PAINTS_SKIP", 0);
    Painter p;
    if (!(skip & 1))
        PaintInfoTip(p, view);
    if (!(skip & 2))
        PaintRecorder(p, view);
    if (!(skip & 4))
        PaintMenu(p, view, day.city);
    if (!(skip & 8))
        PaintGraph(p, view);
    if (!(skip & 16))
        PaintProgressBar(p);
    if (!(skip & 32))
        PaintAlone(p);
    if (!(skip & 64))
        PaintHead(p, view);
    char text[120];
    std::snprintf(text, sizeof text, " | wpaint %d direct %d drew %08X answers %08X pixels %08X skipped %d", p.painted, p.direct,
                  p.drew, p.answers, p.pixels, p.skipped);
    g_token = text;
    log::Write("autotest: window paints driven:%s", text);
}

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"windows.paints", 106, &Fingerprint};
const DayHook kPaints{"windows.paints", 106, &DriveWindowPaints};

} // namespace
} // namespace opensc4::autotest
