// The small tools' part of the autotest (src/core/autotest_hooks.h;
// tool_small_1.cpp, tool_small_2.cpp): drives the drag-to-scroll control,
// the view's mission icon control, the flora tool and the level terrain tool
// and hashes what comes out, so that a run with ui.tools.small1 and
// ui.tools.small2 on and one with them off can be compared. Only the game's
// addresses and vtable slots are called, never our own functions.
//
//   OPENSC4_TEST_SMALL_TOOLS=<day>    token "smalltools", sub-hashes on the
//     log lines "autotest: smalltools: ...":
//
//   1. scroll: the drag-to-scroll control made the way the view's right
//      button handler makes it (constructor 0x004B7DC0 on 0x48 new bytes
//      filled with 0x5A, the view's slot 0x64 with 0, Start 0x004B82F0 at
//      400, 300): the fields after the constructor and after Init and
//      Start, whether the anchor picture's window (ID 0x48E945B4) is a child
//      of the view's window and where it is, Start again at another point,
//      Init again. It goes twice: once taken off by the view (slot 0x68: the
//      tool's Shutdown takes the anchor away), once by its own OnMouseMove
//      (slot 20), which finds the right button up and so is OnMouseUpR and
//      PutAway (slot 27). Should the button really be down at that moment
//      (someone at the mouse), OnMouseUpR (slot 19) is called instead and
//      the log says so. After each: the anchor window gone, the view's tool
//      as before, the fields, and what the last Release answers.
//   2. icons: the view's own mission icon control (the persistent control
//      with the ID 0xABEC6CFE): its fields; OnMouseMove (slot 20),
//      OnMouseDownL (16) and OnMouseUpL (18) at a point far off the screen,
//      with and without a made-up lit or pressed icon in +0x34 and +0x38;
//      DoMessage of the part at +0x28 with the three message types it knows
//      and one it does not; Shutdown and Init (slots 4 and 3), each twice.
//      Then a control of our own: the constructor 0x004B8690, Init without
//      a window, the last Release (Shutdown and the deleting destructor),
//      and the deleting destructor without the free bit on one that holds
//      the two managers.
//   3. flora: the flora tool made as the view's menu command makes it
//      (constructor 0x004BB900 with the lowest flora type that has a price,
//      the view's slot 0x64 with 2): the fields; the three handlers at a
//      point off the ground; OnMouseMove over a grid of screen points with
//      the button up (whether each spot would take a tree); put away by the
//      view.
//   4. level: the level terrain tool (constructor 0x004BDC60, the view's
//      slot 0x64 with 2): the fields; the handlers with no drag on; a drag
//      set by hand and UpdateRect 0x004BDB00 with the cells in both orders;
//      a real press at the first screen point that picks the ground and a
//      move to the last one; then the view takes the tool off in the drag
//      (Shutdown lets go of the mouse) and the test puts the terrain view's
//      cell cursor back as OnMouseUpL would have.
//
//   OPENSC4_TEST_SMALL_TOOLS_COMMIT=1 adds what changes the city (a tutorial
//   city may hold it back: OPENSC4_TEST_TUTORIAL_OFF=1):
//
//   5. plant: a second flora tool. OnMouseDownL at the first spot that takes
//      a tree, three OnMouseMove with the button down at the next spots,
//      OnMouseUpL; then with the tool's price field set far above the funds
//      a press, a move (the refused sound) and a release; then the same
//      three at the first screen point over water. The fields, the answers
//      and the funds after every call. A new tree's lot tag (+0x3C) is pool
//      memory the class never wrote, and in the original it is not the same
//      in every run (Rush Hour: one of the four trees, a different value
//      each time, which the floraocc part of the fingerprint then shows on
//      every later day): the test finds the trees that were not there
//      before and clears the tag of each, on both sides; the log has how
//      many had one.
//   6. flatten: a second level tool with a drag of two by two cells set by
//      hand in the middle of the first eight by eight cells the lot manager
//      calls free; OnMouseUpL at a point off the ground levels them to the
//      altitude of the start vertex. The nine altitudes before and after.
//      OPENSC4_TEST_SMALL_TOOLS_LEVEL=0 leaves this step out.
//
// Not reached: the scroll control's OnMouseMove with the button held (the
// rates, the arrow cursors, SetScrolling: it moves the camera on frame
// time); anything with a real mission icon (the pick reads what the last
// frame drew, and a press and release on one starts a mission). Every
// pointer is left out of the hashes: only whether it is set.
#include "core/autotest_hooks.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"

#include <opensc4/addresses.h>

#include <GZServPtrs.h>
#include <cIGZMessage2Standard.h>
#include <cIGZWin.h>
#include <cISC4App.h>
#include <cISC4City.h>
#include <cISC4Lot.h>
#include <cISC4LotConfiguration.h>
#include <cISC4LotManager.h>
#include <cISC4View3DWin.h>
#include <cRZAutoRefCount.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

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

char g_token[40];

namespace field { // the base class (tool_base.cpp)
constexpr std::size_t kInited = 0x04, kCount = 0x08, kId = 0x0C, kCursorId = 0x10, kCursor = 0x14;
constexpr std::size_t kWindow = 0x18, kView = 0x1C;
} // namespace field

namespace scroll { // tool_small_1.cpp
constexpr std::size_t kAnchorX = 0x28, kAnchorZ = 0x2C, kRateX = 0x30, kRateZ = 0x34, kMoved = 0x38;
constexpr std::size_t kWasCapturing = 0x39, kImage = 0x3C, kInner = 0x40, kOuter = 0x44, kSize = 0x48;
} // namespace scroll

namespace icons { // tool_small_1.cpp
constexpr std::size_t kTarget = 0x28, kSituations = 0x2C, kDispatch = 0x30, kHover = 0x34, kPressed = 0x38;
constexpr std::size_t kSize = 0x3C;
} // namespace icons

namespace flora { // tool_small_2.cpp
constexpr std::size_t kType = 0x28, kSimulator = 0x2C, kSpotOk = 0x30, kPlanting = 0x31, kPrice = 0x38;
constexpr std::size_t kSpent = 0x40, kNoFunds = 0x48, kSize = 0x50;
} // namespace flora

namespace level { // tool_small_2.cpp
constexpr std::size_t kSaved = 0x24, kTerrain = 0x28, kOverlay = 0x2C, kDragging = 0x30, kStartX = 0x34;
constexpr std::size_t kStartZ = 0x38, kCurX = 0x3C, kCurZ = 0x40, kRect = 0x44, kCell = 0x54, kSize = 0x58;
} // namespace level

constexpr std::size_t kViewPersistent = 0x140, kViewPersistentEnd = 0x144; // ui_cityview_2.cpp
constexpr uint32_t kAnchorWindow = 0x48E945B4, kIconsId = 0xABEC6CFE;
constexpr int32_t kOff = -1000; // a screen point that picks nothing

template <typename R, typename... Args>
R This(std::uintptr_t function, void* self, Args... args)
{
    return reinterpret_cast<R(__thiscall*)(void*, Args...)>(function)(self, args...);
}

template <typename R, typename... Args>
R Cdecl(std::uintptr_t function, Args... args)
{
    return reinterpret_cast<R(__cdecl*)(Args...)>(function)(args...);
}

// The city view, as tool_bulldoze_autotest.cpp finds it.
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

// New memory filled with 0x5A, so that what a constructor leaves alone shows.
char* NewBlock(std::size_t size)
{
    char* const block = static_cast<char*>(Cdecl<void*>(addr::SC4_operator_new, static_cast<uint32_t>(size)));
    if (block)
        std::memset(block, 0x5A, size);
    return block;
}

// The base fields of a tool: the plain ones, and of the pointers whether
// they are set.
void HashTool(Fnv& f, char* tool)
{
    f.Add(At<uint8_t>(tool, field::kInited));
    f.Add(At<uint32_t>(tool, field::kCount));
    f.Add(At<uint32_t>(tool, field::kId));
    f.Add(At<uint32_t>(tool, field::kCursorId));
    for (const std::size_t offset : {field::kCursor, field::kWindow, field::kView})
        f.Add(uint8_t{At<void*>(tool, offset) != nullptr});
}

// The handlers: (x, z, modifiers) in the view window's pixels.
uint8_t MouseDownL(char* tool, int32_t x, int32_t z)
{
    return Virtual<uint8_t>(tool, 0x40, x, z, uint32_t{0});
}

uint8_t MouseUpL(char* tool, int32_t x, int32_t z)
{
    return Virtual<uint8_t>(tool, 0x48, x, z, uint32_t{0});
}

uint8_t MouseMove(char* tool, int32_t x, int32_t z)
{
    return Virtual<uint8_t>(tool, 0x50, x, z, uint32_t{0});
}

// A tool given to the view as its makers do: held, the view's slot 0x64.
// The reference stays ours until the caller lets go of it.
bool GiveToView(char* view, char* tool, uint32_t operation)
{
    Virtual<uint32_t>(tool, 0x4);
    Virtual<uint8_t>(view, 0x64, static_cast<void*>(tool), operation);
    return Virtual<void*>(view, 0x60) == tool;
}

// The screen points the tests try, row by row.
struct Point {
    int32_t x, z;
};

template <typename Fn>
void ForGrid(Fn&& fn)
{
    for (int32_t z = 60; z <= 700; z += 40)
        for (int32_t x = 40; x <= 980; x += 60)
            fn(Point{x, z});
}

// --- 1. The drag-to-scroll control ---------------------------------------------------------

void HashScroll(Fnv& f, char* tool)
{
    HashTool(f, tool);
    for (const std::size_t offset : {scroll::kAnchorX, scroll::kAnchorZ, scroll::kRateX, scroll::kRateZ, scroll::kInner,
                                     scroll::kOuter})
        f.Add(At<uint32_t>(tool, offset));
    f.Add(At<uint8_t>(tool, scroll::kMoved));
    f.Add(At<uint8_t>(tool, scroll::kWasCapturing));
    f.Add(uint8_t{At<void*>(tool, scroll::kImage) != nullptr});
}

cIGZWin* Anchor(char* view)
{
    return Virtual<cIGZWin*>(Virtual<void*>(view, 0xC), 0x88, kAnchorWindow);
}

// One life of the control. `byMove`: put away by its own OnMouseMove; else
// taken off by the view.
uint32_t DriveScrollOnce(char* view, bool byMove)
{
    Fnv f;
    void* const before = Virtual<void*>(view, 0x60);
    f.Add(uint8_t{before != nullptr});
    char* const tool = NewBlock(scroll::kSize);
    if (!tool || !This<void*>(addr::SC4ScrollDragTool_Construct, tool))
        return 0;
    HashScroll(f, tool);
    f.Add(uint8_t{At<std::uintptr_t>(tool, 0) == addr::SC4ScrollDragTool_Vtable});
    if (!GiveToView(view, tool, 0)) {
        log::Write("autotest: smalltools: the view did not take the scroll control");
        return 0;
    }
    This<void>(addr::SC4ScrollDragTool_Start, tool, int32_t{400}, int32_t{300});
    HashScroll(f, tool);
    cIGZWin* const anchor = Anchor(view);
    f.Add(uint8_t{anchor != nullptr});
    int32_t area[4] = {0, 0, 0, 0};
    if (anchor) {
        area[0] = anchor->GetL();
        area[1] = anchor->GetT();
        area[2] = anchor->GetR();
        area[3] = anchor->GetB();
    }
    f.Add(area);
    This<void>(addr::SC4ScrollDragTool_Start, tool, int32_t{431}, int32_t{287}); // a picture already: the point only
    HashScroll(f, tool);
    f.Add(uint8_t{Anchor(view) == anchor});
    f.Add(Virtual<uint8_t>(tool, 0xC)); // Init again: nothing
    HashScroll(f, tool);
    const uint32_t inner = At<uint32_t>(tool, scroll::kInner), outer = At<uint32_t>(tool, scroll::kOuter);

    uint8_t answer = 0;
    bool held = false;
    if (byMove) {
        // The button is up in an unattended run: OnMouseMove is OnMouseUpR.
        held = GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON) ? VK_LBUTTON : VK_RBUTTON) != 0;
        answer = held ? Virtual<uint8_t>(tool, 0x4C, int32_t{460}, int32_t{310}, uint32_t{0})
                      : MouseMove(tool, 460, 310);
    } else {
        answer = Virtual<uint8_t>(view, 0x68, uint32_t{0});
    }
    f.Add(answer);
    const bool gone = Anchor(view) == nullptr;
    const bool restored = Virtual<void*>(view, 0x60) == before;
    f.Add(uint8_t{gone});
    f.Add(uint8_t{restored});
    HashScroll(f, tool);
    const uint32_t count = At<uint32_t>(tool, field::kCount);
    const uint32_t last = Virtual<uint32_t>(tool, 0x8); // ours was the last: destroyed
    f.Add(last);
    log::Write("autotest: smalltools: scroll %s %08X (view had a tool: %d; distances %u, %u; anchor window %d at %d,%d "
               "to %d,%d; answer %u; anchor gone %d, the view's tool as before %d, count then %u, last release %u)%s",
               byMove ? "by its move" : "by the view", f.h, before != nullptr, inner, outer, anchor != nullptr, area[0],
               area[1], area[2], area[3], answer, gone, restored, count, last,
               held ? " THE BUTTON WAS DOWN: OnMouseUpR called instead" : "");
    return f.h;
}

uint32_t DriveScroll(char* view)
{
    Fnv f;
    f.Add(DriveScrollOnce(view, false));
    f.Add(DriveScrollOnce(view, true));
    return f.h;
}

// --- 2. The mission icon control -----------------------------------------------------------

void HashIcons(Fnv& f, char* control)
{
    HashTool(f, control);
    f.Add(uint8_t{At<void*>(control, icons::kSituations) != nullptr});
    f.Add(uint8_t{At<void*>(control, icons::kDispatch) != nullptr});
    f.Add(At<uint32_t>(control, icons::kHover));
    f.Add(At<uint32_t>(control, icons::kPressed));
}

// A message of the given type to the control's message target.
void Send(char* control, uint32_t type)
{
    using NewFn = void*(__cdecl*)(std::size_t size);
    using MessageFn = cIGZMessage2Standard*(__thiscall*)(void* memory);
    using DoMessageFn = bool(__thiscall*)(void* target, cIGZMessage2* message);
    void* const memory = reinterpret_cast<NewFn>(addr::RZ_operator_new)(0x2C);
    cIGZMessage2Standard* const message =
        memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    if (!message)
        return;
    message->AddRef();
    message->SetType(type);
    void* const target = control + icons::kTarget;
    reinterpret_cast<DoMessageFn>((*static_cast<void***>(target))[3])(target, message);
    message->Release();
}

uint32_t DriveIcons(char* view, void* city)
{
    Fnv f;
    char* control = nullptr;
    int32_t persistent = 0;
    for (void** at = At<void**>(view, kViewPersistent); at != At<void**>(view, kViewPersistentEnd); ++at, ++persistent)
        if (*at && At<uint32_t>(*at, field::kId) == kIconsId && !control)
            control = static_cast<char*>(*at);
    f.Add(persistent);
    f.Add(uint8_t{control != nullptr});
    void* const viewGlobal = Global<void*>(addr::SC4_Service43DC4);
    const int32_t viewLevel = viewGlobal ? Virtual<int32_t>(viewGlobal, 0x58) : -1;
    f.Add(viewLevel);
    uint32_t handlers = 0, messages = 0, trips = 0;
    bool vehicleView = false;
    if (control) {
        f.Add(uint8_t{At<std::uintptr_t>(control, 0) == addr::SC4MissionIconControl_Vtable});
        f.Add(uint8_t{At<std::uintptr_t>(control, icons::kTarget) == addr::SC4MissionIconControl_TargetVtable});
        HashIcons(f, control);
        void* const dispatch = At<void*>(control, icons::kDispatch);
        vehicleView = dispatch && Virtual<void*>(dispatch, 0x58) != nullptr;
        f.Add(uint8_t{vehicleView});

        // The handlers far off the screen: no icon is there.
        const auto step = [&](uint8_t answer) {
            f.Add(answer);
            HashIcons(f, control);
        };
        const uint32_t hover = At<uint32_t>(control, icons::kHover), pressed = At<uint32_t>(control, icons::kPressed);
        At<uint32_t>(control, icons::kHover) = 0;
        At<uint32_t>(control, icons::kPressed) = 0;
        step(MouseMove(control, -5000, -5000));
        At<uint32_t>(control, icons::kHover) = 0x7777; // a lit icon that is not there
        step(MouseMove(control, -5000, -5000));
        At<uint32_t>(control, icons::kHover) = 0;
        step(MouseDownL(control, -5000, -5000));
        At<uint32_t>(control, icons::kPressed) = 0x8888;
        step(MouseDownL(control, -5000, -5000));
        At<uint32_t>(control, icons::kPressed) = 0x8888;
        step(MouseUpL(control, -5000, -5000));
        At<uint32_t>(control, icons::kPressed) = 0;
        step(MouseUpL(control, -5000, -5000));
        handlers = f.h;

        for (const uint32_t type : {0xA6B79602u, 0x06CDB65Bu, 0xA6B79612u, 0x12345678u}) {
            At<uint32_t>(control, icons::kHover) = 0x7777;
            At<uint32_t>(control, icons::kPressed) = 0x8888;
            Send(control, type);
            HashIcons(f, control);
        }
        At<uint32_t>(control, icons::kHover) = hover;
        At<uint32_t>(control, icons::kPressed) = pressed;
        messages = f.h;

        step(Virtual<uint8_t>(control, 0x10)); // Shutdown
        step(Virtual<uint8_t>(control, 0x10));
        step(Virtual<uint8_t>(control, 0xC)); // Init
        step(Virtual<uint8_t>(control, 0xC));
        trips = f.h;
    }

    // A control of our own.
    uint32_t last = 9;
    if (char* const own = NewBlock(icons::kSize); own && This<void*>(addr::SC4MissionIconControl_Construct, own)) {
        HashIcons(f, own);
        f.Add(uint8_t{At<std::uintptr_t>(own, 0) == addr::SC4MissionIconControl_Vtable});
        f.Add(uint8_t{At<std::uintptr_t>(own, icons::kTarget) == addr::SC4MissionIconControl_TargetVtable});
        f.Add(Virtual<uint32_t>(own, 0x4));
        f.Add(Virtual<uint8_t>(own, 0xC)); // Init, no window
        HashIcons(f, own);
        f.Add(MouseMove(own, -5000, -5000));
        HashIcons(f, own);
        last = Virtual<uint32_t>(own, 0x8); // the last reference: Shutdown and the free
        f.Add(last);
    }
    // The destructor's own releases: a control that holds the two managers
    // and was never inited.
    uint8_t destructed = 9;
    if (char* const own = NewBlock(icons::kSize); own && This<void*>(addr::SC4MissionIconControl_Construct, own)) {
        void* const situations = Virtual<void*>(city, 0xC4);
        void* const dispatch = Virtual<void*>(city, 0x9C);
        for (void* const held : {situations, dispatch})
            if (held)
                Virtual<uint32_t>(held, 0x4);
        At<void*>(own, icons::kSituations) = situations;
        At<void*>(own, icons::kDispatch) = dispatch;
        destructed = Virtual<char*>(own, 0x68, uint32_t{0}) == own;
        f.Add(destructed);
        f.Add(uint8_t{At<std::uintptr_t>(own, 0) == addr::SC4ViewInputControl_Vtable});
        f.Add(uint8_t{At<void*>(own, icons::kSituations) == situations && At<void*>(own, icons::kDispatch) == dispatch});
        At<void*>(own, icons::kSituations) = nullptr;
        At<void*>(own, icons::kDispatch) = nullptr;
        f.Add(uint8_t{This<char*>(addr::SC4MissionIconControl_DeletingDestructor, own, uint32_t{1}) == own});
    }
    log::Write("autotest: smalltools: icons %08X (%d persistent controls, the icon control found %d, view level %d, "
               "vehicle view %d; handlers %08X, messages %08X, round trips %08X; own control's last release %u, "
               "destructor answered this %u)",
               f.h, persistent, control != nullptr, viewLevel, vehicleView, handlers, messages, trips, last, destructed);
    return f.h;
}

// --- 3 and 5. The flora tool ---------------------------------------------------------------

struct Words {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};

// The lowest flora type with a price (the lowest of all when none has one);
// 0 when the simulator knows no types.
uint32_t FloraType(void* simulator, uint32_t& count, int64_t& price)
{
    Words types{nullptr, nullptr, nullptr};
    Virtual<void>(simulator, 0x20, &types); // GetAllFloraTypesVector
    count = static_cast<uint32_t>(types.end - types.begin);
    uint32_t chosen = 0;
    price = 0;
    if (count) {
        std::sort(types.begin, types.end);
        chosen = types.begin[0];
        for (const uint32_t* at = types.begin; at != types.end; ++at) {
            const char* const record = Virtual<const char*>(simulator, 0x1C, *at);
            if (record && At<int64_t>(const_cast<char*>(record), 0x160) > 0) {
                chosen = *at;
                break;
            }
        }
        if (const char* const record = Virtual<const char*>(simulator, 0x1C, chosen))
            price = At<int64_t>(const_cast<char*>(record), 0x160);
    }
    if (types.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(types.begin));
    return chosen;
}

void HashFlora(Fnv& f, char* tool)
{
    HashTool(f, tool);
    f.Add(At<uint32_t>(tool, flora::kType));
    f.Add(uint8_t{At<void*>(tool, flora::kSimulator) != nullptr});
    f.Add(At<uint8_t>(tool, flora::kSpotOk));
    f.Add(At<uint8_t>(tool, flora::kPlanting));
    f.Add(At<int64_t>(tool, flora::kPrice));
    f.Add(At<int64_t>(tool, flora::kSpent));
    f.Add(At<uint8_t>(tool, flora::kNoFunds));
}

char* MakeFloraTool(char* view, uint32_t type, Fnv& f)
{
    char* const tool = NewBlock(flora::kSize);
    if (!tool || !This<void*>(addr::SC4FloraTool_Construct, tool, type))
        return nullptr;
    HashFlora(f, tool);
    f.Add(uint8_t{At<std::uintptr_t>(tool, 0) == addr::SC4FloraTool_Vtable});
    if (!GiveToView(view, tool, 2)) {
        log::Write("autotest: smalltools: the view did not take the flora tool");
        return nullptr;
    }
    HashFlora(f, tool);
    return tool;
}

// What the hover found: the spots that take a tree and a point over water.
struct Spots {
    Point ok[4] = {};
    int32_t oks = 0;
    Point water{};
    bool hasWater = false;
};

uint32_t DriveFlora(char* view, void* terrain, uint32_t type, Spots& spots)
{
    Fnv f;
    f.Add(uint8_t{Virtual<void*>(view, 0x60) != nullptr});
    char* const tool = MakeFloraTool(view, type, f);
    if (!tool)
        return 0;
    const uint32_t made = f.h;

    // Off the ground: nothing is picked.
    const auto step = [&](uint8_t answer) {
        f.Add(answer);
        HashFlora(f, tool);
    };
    step(MouseMove(tool, kOff, kOff));
    step(MouseDownL(tool, kOff, kOff));
    step(MouseMove(tool, kOff, kOff));
    step(MouseUpL(tool, kOff, kOff));
    const uint32_t off = f.h;

    // The hover over the grid.
    int32_t tried = 0, ground = 0, takes = 0;
    ForGrid([&](Point p) {
        ++tried;
        f.Add(MouseMove(tool, p.x, p.z));
        const uint8_t ok = At<uint8_t>(tool, flora::kSpotOk);
        f.Add(ok);
        takes += ok;
        if (ok && spots.oks < 4)
            spots.ok[spots.oks++] = p;
        float position[3] = {0.0f, 0.0f, 0.0f};
        if (Virtual<uint8_t>(view, 0x90, p.x, p.z, &position[0], uint32_t{0})) {
            ++ground;
            const int32_t cellX = static_cast<int32_t>(position[0] * 0.0625f);
            const int32_t cellZ = static_cast<int32_t>(position[2] * 0.0625f);
            if (!spots.hasWater && Virtual<uint8_t>(terrain, 0x94, cellX, cellZ)) {
                spots.hasWater = true;
                spots.water = p;
            }
        }
    });
    f.Add(ground);
    HashFlora(f, tool);
    const uint32_t hover = f.h;

    f.Add(Virtual<uint8_t>(view, 0x68, uint32_t{0}));
    const bool away = Virtual<void*>(view, 0x60) == nullptr;
    f.Add(uint8_t{away});
    HashFlora(f, tool);
    const uint32_t last = Virtual<uint32_t>(tool, 0x8);
    f.Add(last);
    log::Write("autotest: smalltools: flora %08X (made %08X, off the ground %08X, hover %08X: %d points, %d on the "
               "ground, %d take a tree, first at %d,%d, water %d at %d,%d; away %d, last release %u)",
               f.h, made, off, hover, tried, ground, takes, spots.ok[0].x, spots.ok[0].z, spots.hasWater, spots.water.x,
               spots.water.z, away, last);
    return f.h;
}

// Every tree of the city (the occupant manager's list by type, as
// flora_autotest.cpp walks it): the object, its type, where it stands and
// its birth date.
namespace tree {
constexpr std::size_t kType = 0x38, kLotTag = 0x3C, kPosition = 0x40, kBirthDate = 0x4C; // floraocc_sim.h
} // namespace tree

using TreeKey = std::array<uint32_t, 5>;

template <typename Fn>
void ForTrees(void* city, Fn&& fn)
{
    void* const occupants = Virtual<void*>(city, 0x88);
    if (!occupants)
        return;
    void** const sentinel = static_cast<void**>(Virtual<void*>(ListNodeAllocator(), 0xC, 0xCu));
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    void* head = sentinel;
    Virtual<uint8_t>(occupants, 0x9C, &head, 0u, 0u, 0x74758926u, -1);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) {
        void* flora = nullptr;
        if (!Virtual<uint8_t>(node[2], 0x0, 0xC83DD057u, &flora) || !flora)
            continue;
        char* const self = static_cast<char*>(flora);
        fn(self, TreeKey{static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(self)), At<uint32_t>(self, tree::kType),
                         At<uint32_t>(self, tree::kPosition), At<uint32_t>(self, tree::kPosition + 8),
                         At<uint32_t>(self, tree::kBirthDate)});
        Virtual<uint32_t>(flora, 0x8);
    }
    Virtual<uint8_t>(occupants, 0xC0, &head);
    reinterpret_cast<void(__thiscall*)(void*)>(addr::SC4List_Destroy2)(&head);
}

uint32_t DrivePlant(char* view, void* city, uint32_t type, const Spots& spots)
{
    Fnv f;
    std::vector<TreeKey> before;
    ForTrees(city, [&](char*, const TreeKey& key) { before.push_back(key); });
    std::sort(before.begin(), before.end());
    void* const budget = Global<void*>(addr::SC4_BudgetSimulator);
    char* const tool = MakeFloraTool(view, type, f);
    if (!tool || !budget)
        return 0;
    const auto funds = [&] { return Virtual<int64_t>(budget, 0x18); };
    const auto step = [&](const char* name, uint8_t answer) {
        f.Add(answer);
        HashFlora(f, tool);
        f.Add(funds());
        log::Write("autotest: smalltools: plant %s: answer %u, takes %u, planting %u, sum %lld, no funds %u, funds %lld",
                   name, answer, At<uint8_t>(tool, flora::kSpotOk), At<uint8_t>(tool, flora::kPlanting),
                   static_cast<long long>(At<int64_t>(tool, flora::kSpent)), At<uint8_t>(tool, flora::kNoFunds),
                   static_cast<long long>(funds()));
    };
    const int64_t fundsBefore = funds();
    f.Add(fundsBefore);
    f.Add(spots.oks);
    if (spots.oks) {
        const auto spot = [&](int32_t i) { return spots.ok[i < spots.oks ? i : 0]; };
        step("down", MouseDownL(tool, spot(0).x, spot(0).z));
        step("move 1", MouseMove(tool, spot(1).x, spot(1).z));
        step("move 2", MouseMove(tool, spot(2).x, spot(2).z));
        step("move 3", MouseMove(tool, spot(3).x, spot(3).z));
        step("up", MouseUpL(tool, spot(3).x, spot(3).z));
        step("up again", MouseUpL(tool, spot(3).x, spot(3).z));

        // A price the city cannot pay.
        const int64_t price = At<int64_t>(tool, flora::kPrice);
        At<int64_t>(tool, flora::kPrice) = int64_t{0x7FFFFFFF} << 32;
        step("poor down", MouseDownL(tool, spot(0).x, spot(0).z));
        step("poor move", MouseMove(tool, spot(0).x, spot(0).z));
        step("poor up", MouseUpL(tool, spot(0).x, spot(0).z));
        At<int64_t>(tool, flora::kPrice) = price;
    }
    f.Add(uint8_t{spots.hasWater});
    if (spots.hasWater) {
        step("water hover", MouseMove(tool, spots.water.x, spots.water.z));
        step("water down", MouseDownL(tool, spots.water.x, spots.water.z));
        step("water move", MouseMove(tool, spots.water.x, spots.water.z));
        step("water up", MouseUpL(tool, spots.water.x, spots.water.z));
    }
    f.Add(Virtual<uint8_t>(view, 0x68, uint32_t{0}));
    const bool away = Virtual<void*>(view, 0x60) == nullptr;
    f.Add(uint8_t{away});
    const uint32_t last = Virtual<uint32_t>(tool, 0x8);
    f.Add(last);

    // The new trees: counted, and the lot tag nobody wrote cleared.
    int32_t trees = 0, made = 0, tagged = 0;
    ForTrees(city, [&](char* self, const TreeKey& key) {
        ++trees;
        if (std::binary_search(before.begin(), before.end(), key))
            return;
        ++made;
        if (At<uint32_t>(self, tree::kLotTag)) {
            ++tagged;
            log::Write("autotest: smalltools: plant: a new tree's lot tag was %08X, cleared",
                       At<uint32_t>(self, tree::kLotTag));
            At<uint32_t>(self, tree::kLotTag) = 0;
        }
    });
    f.Add(static_cast<int32_t>(before.size()));
    f.Add(trees);
    f.Add(made);
    log::Write("autotest: smalltools: plant %08X (%d spots, water %d, funds %lld -> %lld; trees %d -> %d, %d new, %d of "
               "them with a lot tag; away %d, last release %u)",
               f.h, spots.oks, spots.hasWater, static_cast<long long>(fundsBefore), static_cast<long long>(funds()),
               static_cast<int32_t>(before.size()), trees, made, tagged, away, last);
    return f.h;
}

// --- 4 and 6. The level terrain tool -------------------------------------------------------

void HashLevel(Fnv& f, char* tool)
{
    HashTool(f, tool);
    f.Add(uint8_t{At<void*>(tool, level::kTerrain) != nullptr});
    f.Add(uint8_t{At<void*>(tool, level::kOverlay) != nullptr});
    f.Add(At<uint8_t>(tool, level::kDragging));
}

// The drag's cells and rectangle (only meaningful once a drag has set them).
void HashDrag(Fnv& f, char* tool)
{
    for (const std::size_t offset : {level::kStartX, level::kStartZ, level::kCurX, level::kCurZ})
        f.Add(At<int32_t>(tool, offset));
    for (std::size_t i = 0; i < 4; ++i)
        f.Add(At<int32_t>(tool, level::kRect + i * 4));
}

char* MakeLevelTool(char* view, Fnv& f)
{
    char* const tool = NewBlock(level::kSize);
    if (!tool || !This<void*>(addr::SC4TerrainLevelTool_Construct, tool))
        return nullptr;
    HashLevel(f, tool);
    f.Add(uint8_t{At<std::uintptr_t>(tool, 0) == addr::SC4TerrainLevelTool_Vtable});
    if (!GiveToView(view, tool, 2)) {
        log::Write("autotest: smalltools: the view did not take the level tool");
        return nullptr;
    }
    HashLevel(f, tool);
    f.Add(At<uint32_t>(tool, level::kCell));
    return tool;
}

void SetDrag(char* tool, int32_t sx, int32_t sz, int32_t ex, int32_t ez)
{
    At<uint8_t>(tool, level::kDragging) = 1;
    At<int32_t>(tool, level::kStartX) = sx;
    At<int32_t>(tool, level::kStartZ) = sz;
    At<int32_t>(tool, level::kCurX) = ex;
    At<int32_t>(tool, level::kCurZ) = ez;
}

uint32_t DriveLevel(char* view, void* terrain)
{
    Fnv f;
    f.Add(uint8_t{Virtual<void*>(view, 0x60) != nullptr});
    char* const tool = MakeLevelTool(view, f);
    if (!tool)
        return 0;
    const uint32_t made = f.h;
    const uint32_t cell = At<uint32_t>(tool, level::kCell);

    // No drag on.
    f.Add(MouseMove(tool, kOff, kOff));
    f.Add(MouseUpL(tool, kOff, kOff));
    f.Add(MouseDownL(tool, kOff, kOff)); // off the ground: no drag begins
    HashLevel(f, tool);

    // A drag set by hand: the rectangle in both orders, a move off the ground.
    SetDrag(tool, 9, 4, 5, 7);
    This<void>(addr::SC4TerrainLevelTool_UpdateRect, tool);
    HashDrag(f, tool);
    SetDrag(tool, 5, 7, 9, 4);
    This<void>(addr::SC4TerrainLevelTool_UpdateRect, tool);
    HashDrag(f, tool);
    SetDrag(tool, 6, 6, 6, 6);
    This<void>(addr::SC4TerrainLevelTool_UpdateRect, tool);
    HashDrag(f, tool);
    f.Add(MouseMove(tool, kOff, kOff));
    HashDrag(f, tool);
    At<uint8_t>(tool, level::kDragging) = 0;
    const uint32_t byHand = f.h;

    // A real press and a move: the first and the last grid point on the ground.
    Point first{}, last{};
    int32_t ground = 0;
    ForGrid([&](Point p) {
        float position[3] = {0.0f, 0.0f, 0.0f};
        if (Virtual<uint8_t>(view, 0x90, p.x, p.z, &position[0], uint32_t{0})) {
            if (!ground++)
                first = p;
            last = p;
        }
    });
    f.Add(ground);
    void* const overlay = Virtual<void*>(terrain, 0x108);
    uint8_t saved = 0;
    if (ground) {
        f.Add(MouseDownL(tool, first.x, first.z));
        HashLevel(f, tool);
        HashDrag(f, tool);
        saved = At<uint8_t>(tool, level::kSaved);
        f.Add(saved);
        f.Add(MouseMove(tool, last.x, last.z));
        HashDrag(f, tool);
        f.Add(MouseMove(tool, last.x, last.z)); // the same cell: nothing
        HashDrag(f, tool);
    }
    int32_t cells[4];
    for (std::size_t i = 0; i < 4; ++i)
        cells[i] = At<int32_t>(tool, level::kStartX + i * 4);
    const bool dragging = At<uint8_t>(tool, level::kDragging) != 0;

    // Taken off in the drag: Shutdown ends it; the cell cursor is not put
    // back by that, so the test does it.
    f.Add(Virtual<uint8_t>(view, 0x68, uint32_t{0}));
    if (dragging && overlay)
        Virtual<uint8_t>(overlay, 0x28, uint32_t{saved});
    const bool away = Virtual<void*>(view, 0x60) == nullptr;
    f.Add(uint8_t{away});
    HashLevel(f, tool);
    const uint32_t lastRelease = Virtual<uint32_t>(tool, 0x8);
    f.Add(lastRelease);
    log::Write("autotest: smalltools: level %08X (made %08X, cell size bits %08X, by hand %08X; %d grid points on the "
               "ground, press at %d,%d = cell %d,%d, move to %d,%d = cell %d,%d, dragging %d, cell cursor was %u; "
               "away %d, last release %u)",
               f.h, made, cell, byHand, ground, first.x, first.z, cells[0], cells[1], last.x, last.z, cells[2], cells[3],
               dragging, saved, away, lastRelease);
    return f.h;
}

// The first w x w block of cells (rows from the north-west, 3 cells in from
// the edge) the lot manager's CanCreateLot calls free (8) for some lot
// configuration of the city.
bool FreeBlock(void* city, int32_t sizeX, int32_t sizeZ, int32_t w, int32_t& x, int32_t& z)
{
    void* const lots = Global<void*>(addr::SC4_LotManager);
    void* const manager = Global<void*>(addr::SC4_Service43D18);
    cISC4LotManager* const lotManager = static_cast<cISC4City*>(city)->GetLotManager();
    if (!lots || !manager || !lotManager)
        return false;
    void* configuration = nullptr;
    for (int32_t zi = 0; zi < sizeZ && !configuration; ++zi)
        for (int32_t xi = 0; xi < sizeX && !configuration; ++xi) {
            cISC4Lot* const lot = lotManager->GetLot(xi, zi, false);
            cISC4LotConfiguration* const found = lot ? lot->GetLotConfiguration() : nullptr;
            if (found && found->GetID() && Virtual<void*>(manager, 0x20, found->GetID()) == found)
                configuration = found;
        }
    if (!configuration)
        return false;
    for (int32_t zi = 3; zi + w < sizeZ - 3; ++zi)
        for (int32_t xi = 3; xi + w < sizeX - 3; ++xi)
            if (Virtual<int32_t>(lots, 0x50, xi, zi, w, w, 1u, 0u, configuration) == 8) {
                x = xi;
                z = zi;
                return true;
            }
    return false;
}

uint32_t DriveFlatten(char* view, void* city, void* terrain, int32_t sizeX, int32_t sizeZ)
{
    Fnv f;
    int32_t blockX = 0, blockZ = 0;
    const bool found = FreeBlock(city, sizeX, sizeZ, 8, blockX, blockZ);
    f.Add(uint8_t{found});
    if (!found) {
        log::Write("autotest: smalltools: flatten: no free block of cells, nothing levelled");
        return f.h;
    }
    char* const tool = MakeLevelTool(view, f);
    void* const overlay = Virtual<void*>(terrain, 0x108);
    if (!tool || !overlay)
        return 0;
    const int32_t x = blockX + 3, z = blockZ + 3;
    const int32_t row = Virtual<int32_t>(terrain, 0x1C) + 1;
    uint32_t before[9], after[9];
    const auto read = [&](uint32_t* out) {
        for (int32_t j = 0; j < 3; ++j)
            for (int32_t i = 0; i < 3; ++i) {
                const float altitude = Virtual<float>(terrain, 0x30, static_cast<uint32_t>(row * (z + j) + x + i));
                std::memcpy(&out[j * 3 + i], &altitude, 4);
            }
    };
    read(before);
    f.Add(before);

    // The press, as OnMouseDownL begins one.
    This<uint8_t>(addr::SC4ViewInputControl_BeginCapture, tool);
    SetDrag(tool, x, z, x + 1, z + 1);
    At<uint8_t>(tool, level::kSaved) = Virtual<uint8_t>(overlay, 0x28, uint32_t{0});
    const uint8_t answer = MouseUpL(tool, kOff, kOff);
    f.Add(answer);
    HashLevel(f, tool);
    HashDrag(f, tool);
    read(after);
    f.Add(after);
    int32_t changed = 0;
    for (int32_t i = 0; i < 9; ++i)
        changed += before[i] != after[i];

    f.Add(Virtual<uint8_t>(view, 0x68, uint32_t{0}));
    const bool away = Virtual<void*>(view, 0x60) == nullptr;
    f.Add(uint8_t{away});
    const uint32_t last = Virtual<uint32_t>(tool, 0x8);
    f.Add(last);
    log::Write("autotest: smalltools: flatten %08X (cells %d,%d to %d,%d; answer %u; start vertex %08X; altitudes "
               "before %08X, after %08X, %d of 9 changed; away %d, last release %u)",
               f.h, x, z, x + 1, z + 1, answer, before[0], BytesChecksum(before, sizeof before),
               BytesChecksum(after, sizeof after), changed, away, last);
    return f.h;
}

void Drive(const Day& day)
{
    if (!OnRunDay(day, "OPENSC4_TEST_SMALL_TOOLS") || !game::IsBuild641())
        return;
    char* const view = CityView();
    void* const city = Global<void*>(addr::SC4_City);
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    void* const simulator = Global<void*>(addr::SC4_FloraSimulator43D80);
    if (!view || !city || !terrain || !simulator) {
        log::Write("autotest: smalltools: no city view");
        return;
    }
    const int32_t sizeX = Virtual<int32_t>(city, 0x174), sizeZ = Virtual<int32_t>(city, 0x178);
    if (sizeX < 64 || sizeZ < 64 || sizeX > 4096 || sizeZ > 4096) {
        log::Write("autotest: smalltools: city size %d x %d", sizeX, sizeZ);
        return;
    }
    const bool commit = EnvInt("OPENSC4_TEST_SMALL_TOOLS_COMMIT", 0) != 0;
    const bool flatten = commit && EnvInt("OPENSC4_TEST_SMALL_TOOLS_LEVEL", 1) != 0;

    Fnv all;
    all.Add(DriveScroll(view));
    all.Add(DriveIcons(view, city));

    uint32_t types = 0;
    int64_t price = 0;
    const uint32_t type = FloraType(simulator, types, price);
    log::Write("autotest: smalltools: flora type %08X of %u, price %lld", type, types, static_cast<long long>(price));
    Spots spots;
    if (type)
        all.Add(DriveFlora(view, terrain, type, spots));
    all.Add(DriveLevel(view, terrain));
    if (commit && type)
        all.Add(DrivePlant(view, city, type, spots));
    if (flatten)
        all.Add(DriveFlatten(view, city, terrain, sizeX, sizeZ));
    std::snprintf(g_token, sizeof g_token, " smalltools %08X", all.h);
}

const DayHook kSmallTools{"smalltools", 57, &Drive};

void Fingerprint(cISC4City*, std::string& line)
{
    line += g_token;
}

const FingerprintPart kFingerprint{"smalltools", 16, &Fingerprint};

} // namespace
} // namespace opensc4::autotest
