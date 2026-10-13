// The start of the city view, cSC4View3DWin (0x007E5630 to 0x007E7EA0): the
// window a loaded city is shown and played in.
//
// The object is 0x448 bytes (constructor 0x007F0A70): the interface
// cISC4View3DWin at +0 (vtable 0x00ABCE40, 61 slots, IDs 1 and 0xFA47B3F9),
// a message target at +4 (0x00ABCE2C, ID 0x452294AA), a window procedure at
// +8 (0x00ABCE1C, ID 0xC6AE7085) and the framework's window at +0xC
// (0x00ABCBC0, 151 slots, ID 0x22BA0121). The window's reference count
// (window part +0xC) is the object's.
//
// What this part holds:
//
//   - QueryInterface, AddRef, Release and the small getters and setters of
//     the interface: the key accelerator, the view filter and the view
//     level (both announced with a "before" and an "after" message), the
//     tool on top of the stack, the terrain query switch;
//   - zoom and rotation: the view keeps its own zoom limits (+0x364,
//     +0x368), hands the work to the renderer (+0xF4) and, when the
//     renderer says the view changed, holds the simulator (HiddenPause)
//     and starts the stopwatch at +0x160; code past 0x007F6200 lets the
//     simulator go again when the stopwatch has run 2000 ms;
//   - picking through the renderer (PickTerrain, PickOccupant);
//   - the window's input handlers that walk the tools: the character
//     handler (window slot 0x204), key down and key up (called by the key
//     handler 0x007F3F1A), left and right mouse button down (0x218, 0x21C)
//     and the wheel (0x22C). Tools are view input controls; the stack is
//     the vector at +0x134 and the persistent ones the vector at +0x140. A
//     right press no tool takes starts the drag-to-scroll tool; a wheel
//     turn no stacked tool takes zooms;
//   - keyboard and edge scrolling: 0x007E6C60 turns the two directions kept
//     at +0x348 to +0x354 into SetScrolling(true, angle, speed), the speed
//     growing 1.2 times with each zoom step;
//   - the two pop-up windows below the tool bar ("secondary" and "tertiary"
//     menu, +0x1F8 and +0x1FC with the IDs they were opened for at +0x200
//     and +0x204): 0x007E5C10 and 0x007E5D80 load one from a layout,
//     0x007E7270 builds one around a menu of build items, slots 59 and 60
//     close them;
//   - the panels (advisor window, budget, map, graphs, style, +0x30C):
//     0x007E6F60 shows the one asked for and hides the others, 0x007E7830
//     does the same for a message from a panel's button;
//   - small command helpers: the speed buttons' pictures, the zoom buttons,
//     the day and night buttons, makers of five tools, the terrain commands
//     0 to 7 (0x007E75B0), the "leave the city" question (0x007E6E00), the
//     loading picture;
//   - the hover tip (0x40 bytes, vtable 0x00ABCB60, constructor 0x007E9300,
//     made by the application at 0x0044CB1C): its QueryInterface, its Init
//     (slot 4) and the callback of its 750 ms timer.
//
// Replacement, off by default:
//
//   ui.cityview.part1: everything named above.
//
// Not a function: 0x007E5870 is the end of PickTerrain (no renderer).
//
// Still the game's: GetKeyAccelerator 0x007E56F0 (seven bytes, shared with
// vtables of three unrelated classes) and 0x007E6E40, which gives the
// lighting manager the clock's time and asks the renderer for a full
// redraw (left with the drawing). The tools' classes, the menus, the info
// tip and the panels are called as the original calls them.
//
// The view, as far as this part touches it: +0xF0 the camera control,
// +0xF4 the renderer, +0x11C the modifier keys of the last key or mouse
// event, +0x120 the key accelerator, +0x126 terrain query enabled, +0x134
// the tool stack (begin, end), +0x140 the persistent tools, +0x154 the
// simulator, +0x160 a stopwatch, +0x178 the simulator is held, +0x18C the
// view's own layout window, +0x1F8 to +0x208 the pop-up windows, +0x2D4 a
// byte set when the advisor window is asked for, +0x2E8 the budget panel,
// +0x2F8 the graphs window, +0x2FC the style panel, +0x300 the map window,
// +0x30C another panel, +0x320 the view filter, +0x324 the view level,
// +0x32C to +0x33C the scroll's state, +0x340 and +0x344 the edge and the
// keyboard scroll speed, +0x348 and +0x34C the keyboard's directions,
// +0x350 and +0x354 the edges', +0x358 and +0x35C where the right button
// went down, +0x360 a byte cleared then, +0x364 and +0x368 the zoom
// limits, +0x370 the panel last asked for, +0x374 whether the panels are
// put away, +0x3D0 an object with interface 0x86D72B57, +0x3D8 the view's
// info tip, +0x3FC and +0x400 a switch and its handle, +0x404 the loading
// picture, +0x40C the mission status panel.
//
// The hover tip: +8 a byte "initialised", +0xC its info tip, +0x10 and
// +0x14 two pictures, +0x18 a timer.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace opensc4::ui {
namespace {

namespace view { // the city view
constexpr std::size_t kTarget = 0x04;
constexpr std::size_t kWinProc = 0x08;
constexpr std::size_t kWindow = 0x0C;
constexpr std::size_t kCamera = 0xF0;
constexpr std::size_t kRenderer = 0xF4;
constexpr std::size_t kModifiers = 0x11C;
constexpr std::size_t kAccelerator = 0x120;
constexpr std::size_t kTerrainQuery = 0x126;    // byte
constexpr std::size_t kTools = 0x134;           // begin, end
constexpr std::size_t kPersistent = 0x140;      // begin, end
constexpr std::size_t kSimulator = 0x154;
constexpr std::size_t kStopwatch = 0x160;
constexpr std::size_t kSimulatorHeld = 0x178;   // byte
constexpr std::size_t kLayout = 0x18C;
constexpr std::size_t kSecondary = 0x1F8;
constexpr std::size_t kTertiary = 0x1FC;
constexpr std::size_t kSecondaryId = 0x200;
constexpr std::size_t kTertiaryId = 0x204;
constexpr std::size_t kMenuPlace = 0x208;       // two dwords of the caller's: a menu's chosen row and first row
constexpr std::size_t kAdviceAsked = 0x2D4;     // byte
constexpr std::size_t kBudget = 0x2E8;
constexpr std::size_t kGraphs = 0x2F8;
constexpr std::size_t kStyle = 0x2FC;
constexpr std::size_t kMap = 0x300;
constexpr std::size_t kPanel30C = 0x30C;
constexpr std::size_t kFilter = 0x320;
constexpr std::size_t kLevel = 0x324;
constexpr std::size_t kScrollScale = 0x32C;     // float
constexpr std::size_t kScrolling = 0x330;       // byte
constexpr std::size_t kScrollSpeed = 0x334;     // float
constexpr std::size_t kScrollScaled = 0x338;    // byte
constexpr std::size_t kScrollStopping = 0x339;  // byte
constexpr std::size_t kScrollStopSpeed = 0x33C; // float
constexpr std::size_t kEdgeSpeed = 0x340;       // float
constexpr std::size_t kKeySpeed = 0x344;        // float
constexpr std::size_t kKeyX = 0x348;
constexpr std::size_t kKeyY = 0x34C;
constexpr std::size_t kEdgeX = 0x350;
constexpr std::size_t kEdgeY = 0x354;
constexpr std::size_t kDragX = 0x358;
constexpr std::size_t kDragY = 0x35C;
constexpr std::size_t kDragMoved = 0x360;       // byte
constexpr std::size_t kZoomMin = 0x364;
constexpr std::size_t kZoomMax = 0x368;
constexpr std::size_t kPanel = 0x370;
constexpr std::size_t kPanelsAway = 0x374;
constexpr std::size_t kObject3D0 = 0x3D0;
constexpr std::size_t kTip = 0x3D8;
constexpr std::size_t kSwitch = 0x3FC;          // byte
constexpr std::size_t kSwitchHandle = 0x400;
constexpr std::size_t kLoadingPicture = 0x404;
constexpr std::size_t kMissionPanel = 0x40C;
} // namespace view

namespace hover { // the hover tip
constexpr std::size_t kReady = 0x08; // byte
constexpr std::size_t kTip = 0x0C;
constexpr std::size_t kIconA = 0x10;
constexpr std::size_t kIconB = 0x14;
constexpr std::size_t kTimer = 0x18;
} // namespace hover

constexpr uint32_t kViewIid = 0xFA47B3F9;
constexpr uint32_t kTargetIid = 0x452294AA;
constexpr uint32_t kWinProcIid = 0xC6AE7085;
constexpr uint32_t kWinIid = 0x22BA0121;
constexpr uint32_t kHoverIid = 0x22C010CD;
constexpr uint32_t kWinGenIid = 0x5386D516;
constexpr uint32_t kCheckIid = 0x8810;
constexpr uint32_t kMenuIid = 0x49D98264;
constexpr uint32_t kOccupantIid = 0x26BB767E;
constexpr uint32_t kMapIid = 0x67C5BEDE;
constexpr uint32_t kPngType = 0x856DDBAC;
constexpr uint32_t kUiGroup = 0x96A006B0;
constexpr uint32_t kPictureGroup = 0x46A006B0;

constexpr uint32_t kCommand = 3;
constexpr uint32_t kButtonClicked = 0x287259F6;

// What ShowPanel is asked for.
constexpr uint32_t kPanelAdvice = 0x49EDF9B7;
constexpr uint32_t kPanelBudget = 0x41;
constexpr uint32_t kPanelMap = 0x99887755;
constexpr uint32_t kPanelGraphs = 0x15200002;
constexpr uint32_t kPanelOther = 0x15200003;
constexpr uint32_t kPanelStyle = 0xABC54125;

// The view's operations on its tool stack (SetCurrentViewInputControl).
constexpr uint32_t kKeepTools = 0;
constexpr uint32_t kReplaceTools = 2;

// A window message as controls send it.
struct WinMessage {
    uint32_t type;
    uint32_t code;
    uint32_t data;
    uint32_t more;
};

// The game's string body and string.
struct StringBody {
    char* begin;
    char* end;
    char* capacity;
};

struct GameString {
    std::uintptr_t vtable;
    StringBody body;
    uint32_t references;
};
static_assert(sizeof(GameString) == 0x14);

// The game's holder of a loaded image (0x30 bytes).
struct ImageLoader {
    std::uintptr_t vtable;
    void* image;
    uint32_t key[3];
    GameString name;
    uint32_t mode;
    uint8_t flag;
};
static_assert(sizeof(ImageLoader) == 0x30);

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

uint32_t AddRef(void* object)
{
    return Virtual<uint32_t>(object, 0x4);
}

uint32_t Release(void* object)
{
    return Virtual<uint32_t>(object, 0x8);
}

// The game's memory for an object, null when there is none.
void* New(uint32_t size)
{
    return Cdecl<void*>(addr::SC4_operator_new, size);
}

// A fresh framework message with one reference. The original does not
// check it for null either.
void* NewMessage(uint32_t type)
{
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
    void* const message = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
    AddRef(message);
    Virtual<void>(message, 0x14, type);
    return message;
}

// The game's _CIpow: base to the power of exponent, both on the x87 stack.
double Power(double base, double exponent)
{
    double result;
    const std::uintptr_t pow = addr::CRT_CIpow;
    __asm {
        fld base
        fld exponent
        call pow
        fstp result
    }
    return result;
}

// How many pointers a vector of the view holds, as the original counts.
int32_t Count(char* self, std::size_t vector)
{
    return static_cast<int32_t>(At<uint32_t>(self, vector + 4) - At<uint32_t>(self, vector)) >> 2;
}

void* Item(char* self, std::size_t vector, int32_t index)
{
    return At<void**>(self, vector)[index];
}

// The simulator held (HiddenPause) while the view changes, once, and the
// stopwatch started that lets it go again. The game's 0x007E5B40, which the
// zoom and rotation methods have inline.
void HoldSimulator(char* self)
{
    if (!At<uint8_t>(self, view::kSimulatorHeld)) {
        if (void* const simulator = At<void*>(self, view::kSimulator)) {
            At<uint8_t>(self, view::kSimulatorHeld) = 1;
            Virtual<void>(simulator, 0x28);
        }
    }
    This<void>(addr::GZStopwatch_Start, self + view::kStopwatch);
}

// A zoom level kept inside the view's limits.
int32_t ClampZoom(char* self, int32_t zoom)
{
    const int32_t lowest = At<int32_t>(self, view::kZoomMin);
    if (zoom < lowest)
        return lowest;
    const int32_t highest = At<int32_t>(self, view::kZoomMax);
    if (zoom > highest)
        return highest;
    return zoom;
}

// The game's 0x007E5630 (slot 0, ret 8). `this` is the view. The original
// also has a path for a null `this` (an artefact of the casts), left out.
uint8_t __fastcall ViewQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    switch (iid) {
    case 1:
    case kViewIid:
        *out = self;
        break;
    case kTargetIid:
        *out = self + view::kTarget;
        break;
    case kWinProcIid:
        *out = self + view::kWinProc;
        break;
    case kWinIid:
        *out = self + view::kWindow;
        break;
    default:
        return 0;
    }
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// The game's 0x007E56D0 (slot 1): the window part's count goes up.
uint32_t __fastcall ViewAddRef(char* self, void*)
{
    return This<uint32_t>(addr::SC4Win_AddRef, self + view::kWindow);
}

// The game's 0x007E56E0 (slot 2): the window part's Release.
uint32_t __fastcall ViewRelease(char* self, void*)
{
    return This<uint32_t>(addr::SC4Win_Release, self + view::kWindow);
}

// The game's 0x007E5700 (slot 32, SetKeyAccelerator, ret 4).
void __fastcall SetKeyAccelerator(char* self, void*, void* accelerator)
{
    if (At<void*>(self, view::kAccelerator) == accelerator)
        return;
    if (accelerator)
        AddRef(accelerator);
    if (void* const old = At<void*>(self, view::kAccelerator))
        Release(old);
    At<void*>(self, view::kAccelerator) = accelerator;
}

// The game's 0x007E5740 (slot 34, ScrollStop). A scroll under way is marked
// as stopping, from its speed (times the scale at +0x32C when the byte at
// +0x338 is set). True always.
uint8_t __fastcall ScrollStop(char* self, void*)
{
    if (!At<uint8_t>(self, view::kScrolling))
        return 1;
    At<uint8_t>(self, view::kScrollStopping) = 1;
    if (At<uint8_t>(self, view::kScrollScaled)) {
        At<float>(self, view::kScrollStopSpeed) =
            static_cast<float>(static_cast<double>(At<float>(self, view::kScrollSpeed)) *
                               static_cast<double>(At<float>(self, view::kScrollScale)));
    } else {
        At<uint32_t>(self, view::kScrollStopSpeed) = At<uint32_t>(self, view::kScrollSpeed);
    }
    return 1;
}

int32_t Zoom(char* self)
{
    return This<int32_t>(addr::cSC4CameraControl_EffectiveZoom, At<void*>(self, view::kCamera));
}

// The game's 0x007E5780 (slot 12, CanZoomIn).
uint8_t __fastcall CanZoomIn(char* self, void*)
{
    return Zoom(self) < At<int32_t>(self, view::kZoomMax);
}

// The game's 0x007E57A0 (slot 13, CanZoomOut).
uint8_t __fastcall CanZoomOut(char* self, void*)
{
    return Zoom(self) > At<int32_t>(self, view::kZoomMin);
}

// The game's 0x007E57C0 (slot 18, GetCurrentViewFilterInfo, ret 4): the
// filter with a reference for the caller. The filter is not checked for
// null; what comes back is the reference count, as in the original.
uint32_t __fastcall GetFilter(char* self, void*, void** out)
{
    void* const filter = At<void*>(self, view::kFilter);
    *out = filter;
    return AddRef(filter);
}

// The game's 0x007E57E0 (slot 20, GetCurrentViewLevelInfo, ret 4).
uint32_t __fastcall GetLevelInfo(char* self, void*, uint32_t* out)
{
    const uint32_t level = At<uint32_t>(self, view::kLevel);
    *out = level;
    return level;
}

// The game's 0x007E57F0 (slot 22, GetCurrentViewLevel).
uint32_t __fastcall GetLevel(char* self, void*)
{
    return At<uint32_t>(self, view::kLevel);
}

// The game's 0x007E5800 (slot 23, SetCurrentViewLevel, ret 4): slot 21 with
// the level when it differs. EAX is whatever is left, as in the original.
uint32_t __fastcall SetLevel(char* self, void*, uint32_t level)
{
    if (level == At<uint32_t>(self, view::kLevel))
        return level;
    return Virtual<uint32_t>(self, 0x54, &level);
}

// The game's 0x007E5820 (slot 27, RemoveAllViewInputControls, ret 4): slot
// 26 until it says no. False always.
uint8_t __fastcall RemoveAllTools(char* self, void*, uint32_t quiet)
{
    while (Virtual<uint8_t>(self, 0x68, quiet)) {
    }
    return 0;
}

// The game's 0x007E5840 (slot 36, PickTerrain, ret 0x10): the renderer's
// PickTerrain(x, y, out, 0, flag); with no renderer the three floats are
// cleared (the list's 0x007E5870).
uint8_t __fastcall PickTerrain(char* self, void*, int32_t x, int32_t y, uint32_t* out, uint32_t flag)
{
    if (void* const renderer = At<void*>(self, view::kRenderer))
        return Virtual<uint8_t>(renderer, 0xF0, x, y, out, uint32_t{0}, flag);
    out[2] = 0;
    out[1] = 0;
    out[0] = 0;
    return 0;
}

// The game's 0x007E5890 (slot 37, PickOccupant, ret 0xC): the model the
// renderer picks at the point, its owner (the model's slot 3) and the
// owner's occupant interface, which the caller gets with its reference.
uint8_t __fastcall PickOccupant(char* self, void*, int32_t x, int32_t y, void** out)
{
    *out = nullptr;
    void* const renderer = At<void*>(self, view::kRenderer);
    if (!renderer)
        return 0;
    void* model = nullptr;
    if (!Virtual<uint8_t>(renderer, 0x104, x, y, static_cast<void*>(nullptr), &model))
        return 0;
    uint8_t found = 0;
    if (void* const owner = Virtual<void*>(model, 0xC)) {
        void* occupant;
        if (Virtual<uint8_t>(owner, 0x0, kOccupantIid, &occupant)) {
            *out = occupant;
            found = 1;
        }
    }
    Release(model);
    return found;
}

// The game's 0x007E5900 (slot 39, GetTerrainQueryEnabled).
uint8_t __fastcall GetTerrainQueryEnabled(char* self, void*)
{
    return At<uint8_t>(self, view::kTerrainQuery);
}

// The game's 0x007E5910 (ret 8). Makes the tool of 0x130 bytes (constructor
// 0x004D9430 with the two arguments) the view's only tool. The memory is
// not checked before use, as in the original.
void __fastcall StartTool130(char* self, void*, uint32_t first, uint32_t second)
{
    void* const memory = New(0x130);
    void* const tool = memory ? This<void*>(addr::SC4TerraformTool_Construct, memory, first, second) : nullptr;
    AddRef(tool);
    Virtual<uint8_t>(self, 0x64, tool, kReplaceTools);
    Release(tool);
}

// The game's 0x007E5960 (ret 8). Whether the view level a command asks for
// differs from the one shown: 1, or 2 and 3 for the two IDs of command
// 0x4A2B2467.
uint8_t __fastcall LevelDiffers(char* self, void*, uint32_t command, uint32_t id)
{
    uint32_t level = 1;
    if (command == 0x4A2B2467) {
        if (id == 0x299237BF)
            level = 2;
        else if (id == 0x2992F72C)
            level = 3;
    }
    return level != At<uint32_t>(self, view::kLevel);
}

// The game's 0x007E59B0. The three speed buttons of the layout window: the
// one of the simulator's speed pressed in, and each showing the lower half
// of its picture while the simulator is paused (or missing) and the upper
// half while it runs. The picture and the rectangle are whatever the button
// hands out; the rectangle local is not cleared first, as in the original.
void __fastcall UpdateSpeedButtons(char* self, void*)
{
    static const uint32_t kButtons[3] = {0x6A4FBB31, 0x4A4FBB60, 0x8998BBDF};
    static const uint32_t kSpeeds[3] = {0, 1, 2};

    uint32_t speed = 0;
    uint8_t paused = 1;
    if (void* const simulator = At<void*>(self, view::kSimulator)) {
        speed = Virtual<uint32_t>(simulator, 0x6C);
        paused = Virtual<uint8_t>(At<void*>(self, view::kSimulator), 0x3C);
    }
    if (!At<void*>(self, view::kLayout))
        return;
    uint32_t area[4];
    void* picture;
    for (std::size_t i = 0; i < 3; ++i) {
        void* button = nullptr;
        if (!Virtual<uint8_t>(At<void*>(self, view::kLayout), 0x94, kButtons[i], kCheckIid, &button))
            continue;
        Virtual<void>(button, 0x14, uint32_t{1}, uint32_t{speed == kSpeeds[i]});
        Virtual<void>(button, 0x40, &area[0]);
        Virtual<void>(button, 0x48, &picture);
        const uint32_t half = Virtual<uint32_t>(picture, 0x28) >> 1;
        const uint32_t top = paused ? half : 0;
        if (area[1] != top || area[3] != top + half) {
            area[1] = top;
            area[3] = top + half;
            area[0] = 0;
            area[2] = Virtual<uint32_t>(picture, 0x24);
            const uint32_t state = Virtual<uint32_t>(button, 0x2C);
            Virtual<void>(button, 0x30, state, picture, &area[0]);
        }
        Release(button);
    }
}

// The game's 0x007E5B00. The two zoom buttons of the layout window enabled
// by CanZoomIn and CanZoomOut.
void __fastcall UpdateZoomButtons(char* self, void*)
{
    const uint8_t in = Virtual<uint8_t>(self, 0x30);
    Cdecl<uint8_t>(addr::SC4Win_EnableChild, At<void*>(self, view::kLayout), uint32_t{0x4992046A}, uint32_t{in});
    const uint8_t out = Virtual<uint8_t>(self, 0x34);
    Cdecl<uint8_t>(addr::SC4Win_EnableChild, At<void*>(self, view::kLayout), uint32_t{0xE9920494}, uint32_t{out});
}

// The game's 0x007E5B40.
void __fastcall HoldSimulatorHook(char* self, void*)
{
    HoldSimulator(self);
}

// The game's 0x007E5B70. The simulator let go (HiddenResume) if the view
// holds it, and the stopwatch cleared.
void __fastcall LetSimulatorGo(char* self, void*)
{
    if (At<uint8_t>(self, view::kSimulatorHeld)) {
        void* const simulator = At<void*>(self, view::kSimulator);
        At<uint8_t>(self, view::kSimulatorHeld) = 0;
        if (simulator)
            Virtual<void>(simulator, 0x34);
    }
    This<void>(addr::SC4Stopwatch_Reset, self + view::kStopwatch);
}

// The game's 0x007E5BA0 (the window's slot 0x23C, a button's command, ret
// 8): a bare jump to the framework's "no". `this` is the window part.
uint8_t __fastcall WindowCommand(char* window, void*, uint32_t command, uint32_t data)
{
    return This<uint8_t>(addr::GZWin_ReturnFalse8, window, command, data);
}

// The game's 0x007E5BB0 (ret 4). Makes the loading picture (0xC0 bytes,
// constructor 0x00776F90 with what the window's slot 0xC0 gives and the
// argument), keeps it at +0x404 with a reference and starts it
// (0x007778D0). The memory is not checked before use, as in the original.
void __fastcall MakeLoadingPicture(char* self, void*, uint32_t argument)
{
    void* const memory = New(0xC0);
    void* picture = nullptr;
    if (memory) {
        const uint32_t owner = Virtual<uint32_t>(self + view::kWindow, 0xC0);
        picture = This<void*>(addr::SC4UI_F776F90, memory, owner, argument);
    }
    At<void*>(self, view::kLoadingPicture) = picture;
    AddRef(picture);
    This<void>(addr::SC4UI_F7778D0, At<void*>(self, view::kLoadingPicture));
}

// A pop-up window loaded from a layout: the game's 0x007E5C10 and
// 0x007E5D80, which differ in the two fields and the slot that closes.
// The tools go; asked for the window that is open, the view only closes
// it; another open one is closed first. The window `id` of the interface
// group's layout `instance` is loaded as a child of the view, gets the
// view as its window procedure when it is a generic window, is placed so
// that its child 0xAAAA lies on the window `anchor` of the main window, is
// brought to the front and shown.
void OpenPopup(char* self, uint32_t instance, uint32_t id, uint32_t anchor, std::size_t window, std::size_t openId,
               std::size_t closeSlot)
{
    Virtual<uint8_t>(self, 0x6C, uint32_t{0});
    if (const uint32_t open = At<uint32_t>(self, openId)) {
        Virtual<void>(self, closeSlot);
        if (open == id)
            return;
    }
    uint32_t key[3] = {0, kUiGroup, instance};
    void* const parent = self + view::kWindow;
    void* const popup = Cdecl<void*>(addr::SC4UI_LoadWindowChild, &key[0], parent, id);
    void* gen = nullptr;
    if (!popup)
        return;
    Virtual<uint8_t>(popup, 0x0, kWinGenIid, &gen);
    if (gen) {
        Virtual<void>(popup, 0x1D0, static_cast<void*>(self + view::kWinProc));
        Virtual<void>(gen, 0x18, uint32_t{4}, uint32_t{0});
        Virtual<void>(gen, 0x18, uint32_t{1}, uint32_t{0});
        Release(gen);
    }
    const uint32_t popupId = Virtual<uint32_t>(popup, 0xFC);
    Cdecl<uint8_t>(addr::SC4UI_F78DB20, popupId, uint32_t{0xAAAA}, anchor, static_cast<void*>(nullptr));
    Virtual<void>(popup, 0x5C);
    Virtual<void>(popup, 0x114);
    At<void*>(self, window) = popup;
    At<uint32_t>(self, openId) = id;
}

// The game's 0x007E5C10 (ret 0x10): the secondary menu from a layout. The
// third argument is not used.
void __fastcall OpenSecondary(char* self, void*, uint32_t instance, uint32_t id, uint32_t, uint32_t anchor)
{
    OpenPopup(self, instance, id, anchor, view::kSecondary, view::kSecondaryId, 0xEC);
}

// The game's 0x007E5D20 (slot 59, CloseSecondaryMenu). Closes the tertiary
// menu, then hides the secondary one, lets it go and takes it out of the
// view's window (window slot 0x40).
void __fastcall CloseSecondary(char* self, void*)
{
    if (!At<void*>(self, view::kSecondary))
        return;
    Virtual<void>(self, 0xF0);
    Virtual<void>(At<void*>(self, view::kSecondary), 0x118);
    Release(At<void*>(self, view::kSecondary));
    Virtual<void>(self + view::kWindow, 0x40, At<void*>(self, view::kSecondary));
    At<void*>(self, view::kSecondary) = nullptr;
    At<uint32_t>(self, view::kSecondaryId) = 0;
}

// The game's 0x007E5D80 (ret 0x14): the tertiary menu from a layout. The
// third and the fifth argument are not used.
void __fastcall OpenTertiary(char* self, void*, uint32_t instance, uint32_t id, uint32_t, uint32_t anchor, uint32_t)
{
    OpenPopup(self, instance, id, anchor, view::kTertiary, view::kTertiaryId, 0xF0);
}

// The game's 0x007E5E90 (slot 60, CloseTertiaryMenu). When the opener left
// a place for them (+0x208), the menu's chosen row and first row (slots 17
// and 9 of the child 0x8A2CAD8B) are kept there; then the window is hidden,
// let go and taken out of the view's window.
void __fastcall CloseTertiary(char* self, void*)
{
    void* const popup = At<void*>(self, view::kTertiary);
    if (!popup)
        return;
    if (At<uint32_t*>(self, view::kMenuPlace)) {
        void* menu;
        if (Virtual<uint8_t>(popup, 0x94, uint32_t{0x8A2CAD8B}, kMenuIid, &menu)) {
            At<uint32_t*>(self, view::kMenuPlace)[0] = Virtual<uint32_t>(menu, 0x44);
            At<uint32_t*>(self, view::kMenuPlace)[1] = Virtual<uint32_t>(menu, 0x24);
            Release(menu);
        }
    }
    Virtual<void>(At<void*>(self, view::kTertiary), 0x118);
    Release(At<void*>(self, view::kTertiary));
    Virtual<void>(self + view::kWindow, 0x40, At<void*>(self, view::kTertiary));
    At<void*>(self, view::kTertiary) = nullptr;
    At<uint32_t>(self, view::kTertiaryId) = 0;
}

// The game's 0x007E5F40. `this` is not used. Asks the player for a setting
// of the terrain service with a modal window (the object of 0xC bytes made
// by 0x00734D20, run by 0x00734B90) and writes the answer back (the
// service's slots 42 and 41). The memory is not checked before use.
void __fastcall AskTerrainSetting(char*, void*)
{
    void* const terrain = Global<void*>(addr::SC4_Terrain);
    if (!terrain)
        return;
    uint32_t value = Virtual<uint32_t>(terrain, 0xA8);
    void* const memory = New(0xC);
    void* const dialog = memory ? This<void*>(addr::SC4UI_ChoiceDialogConstruct, memory) : nullptr;
    AddRef(dialog);
    This<uint8_t>(addr::SC4UI_ChoiceDialogRun, dialog, &value);
    Release(dialog);
    Virtual<void>(terrain, 0xA4, value);
}

// The game's 0x007E5FB0. Flips the switch at +0x3FC and has the object the
// terrain picker's slot 18 gives for 0 follow it: on makes 0x07F94205 there
// (slot 8) and keeps the handle, off takes the handle away (slot 15).
// Returns the switch.
uint8_t __fastcall FlipSwitch(char* self, void*)
{
    At<uint8_t>(self, view::kSwitch) = At<uint8_t>(self, view::kSwitch) == 0;
    if (void* const picker = Cdecl<void*>(addr::SC4_TerrainPicker)) {
        void* const layer = Virtual<void*>(picker, 0x48, uint32_t{0});
        if (At<uint8_t>(self, view::kSwitch)) {
            if (At<uint32_t>(self, view::kSwitchHandle) == 0xFFFFFFFF)
                At<uint32_t>(self, view::kSwitchHandle) = Virtual<uint32_t>(layer, 0x20, uint32_t{0x07F94205});
        } else {
            const uint32_t handle = At<uint32_t>(self, view::kSwitchHandle);
            if (handle != 0xFFFFFFFF) {
                Virtual<void>(layer, 0x3C, handle);
                At<uint32_t>(self, view::kSwitchHandle) = 0xFFFFFFFF;
            }
        }
    }
    return At<uint8_t>(self, view::kSwitch);
}

// The game's 0x007E6030 (the hover tip's slot 0, ret 8).
uint8_t __fastcall HoverQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid != 1 && iid != kHoverIid)
        return 0;
    *out = self;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// The game's 0x007E6060 (cdecl, no arguments read): the hover tip's timer
// has run. Posts the static message 0xCA56DD76 (made on first use) through
// the poster's slot 4.
void __cdecl HoverTick()
{
    void* const poster = Cdecl<void*>(addr::SC4_Singleton913B97);
    if (!poster)
        return;
    WinMessage& message = Global<WinMessage>(addr::SC4UI_HoverTickMessage);
    if (!(Global<uint8_t>(addr::SC4UI_HoverTickMessageGuard) & 1)) {
        Global<uint32_t>(addr::SC4UI_HoverTickMessageGuard) |= 1;
        message.type = 0xCA56DD76;
        message.code = 0;
        message.data = 0;
        message.more = 0;
    }
    Virtual<void>(poster, 0x10, &message, uint32_t{0});
}

// The game's 0x007E60B0 (slot 6, GetViewManager): the map window's
// interface 0x67C5BEDE, without a reference for the caller. The map window
// is not checked for null.
void* __fastcall GetViewManager(char* self, void*)
{
    void* manager;
    if (!Virtual<uint8_t>(At<void*>(self, view::kMap), 0x0, kMapIid, &manager))
        return nullptr;
    Release(manager);
    return manager;
}

// The game's 0x007E60E0 (slot 8, SetZoom, ret 4).
uint8_t __fastcall SetZoom(char* self, void*, int32_t zoom)
{
    const uint8_t changed = Virtual<uint8_t>(At<void*>(self, view::kRenderer), 0x8C, ClampZoom(self, zoom));
    if (changed)
        HoldSimulator(self);
    return changed;
}

// The game's 0x007E6150 (slot 11, SetZoomLimits, ret 8). An upper limit of
// 5 or more becomes 5 when the preferences' byte at +0xF10 is above zero
// (2 stands in when there are no preferences) and 4 when not. A zoom
// outside the limits as given is set to the nearer one. True always.
uint8_t __fastcall SetZoomLimits(char* self, void*, int32_t lowest, int32_t highest)
{
    At<int32_t>(self, view::kZoomMin) = lowest;
    At<int32_t>(self, view::kZoomMax) = highest;
    if (highest >= 5) {
        int32_t detail = 2;
        if (char* const preferences = Virtual<char*>(Global<void*>(addr::SC4_Service43C94), 0x98))
            detail = At<uint8_t>(preferences, 0xF10);
        At<int32_t>(self, view::kZoomMax) = (detail > 0 ? 1 : 0) + 4;
    }
    const int32_t zoom = Zoom(self);
    int32_t wanted;
    if (zoom < lowest)
        wanted = lowest;
    else if (zoom > highest)
        wanted = highest;
    else
        return 1;
    if (wanted != zoom)
        Virtual<uint8_t>(self, 0x20, wanted);
    return 1;
}

// The game's 0x007E61D0 (slot 14, SetRotation, ret 4).
uint8_t __fastcall SetRotation(char* self, void*, int32_t rotation)
{
    const uint8_t changed = Virtual<uint8_t>(At<void*>(self, view::kRenderer), 0x98, rotation);
    if (changed)
        HoldSimulator(self);
    return changed;
}

// The game's 0x007E6220 (slot 15, RotateLeft).
uint8_t __fastcall RotateLeft(char* self, void*)
{
    const uint8_t changed = Virtual<uint8_t>(At<void*>(self, view::kRenderer), 0x9C);
    if (changed)
        HoldSimulator(self);
    return changed;
}

// The game's 0x007E6270 (slot 16, RotateRight).
uint8_t __fastcall RotateRight(char* self, void*)
{
    const uint8_t changed = Virtual<uint8_t>(At<void*>(self, view::kRenderer), 0xA0);
    if (changed)
        HoldSimulator(self);
    return changed;
}

// The game's 0x007E62C0 (slot 9, ZoomIn).
uint8_t __fastcall ZoomIn(char* self, void*)
{
    if (Zoom(self) >= At<int32_t>(self, view::kZoomMax))
        return 0;
    const uint8_t changed = Virtual<uint8_t>(At<void*>(self, view::kRenderer), 0x90);
    if (changed)
        HoldSimulator(self);
    return changed;
}

// The game's 0x007E6320 (slot 10, ZoomOut).
uint8_t __fastcall ZoomOut(char* self, void*)
{
    if (Zoom(self) <= At<int32_t>(self, view::kZoomMin))
        return 0;
    const uint8_t changed = Virtual<uint8_t>(At<void*>(self, view::kRenderer), 0x94);
    if (changed)
        HoldSimulator(self);
    return changed;
}

// The game's 0x007E6380 (slot 17, SetZoomAndRotation, ret 8).
uint8_t __fastcall SetZoomAndRotation(char* self, void*, int32_t zoom, int32_t rotation)
{
    const uint8_t changed =
        Virtual<uint8_t>(At<void*>(self, view::kRenderer), 0xA4, ClampZoom(self, zoom), rotation);
    if (changed)
        HoldSimulator(self);
    return changed;
}

// The game's 0x007E63F0 (slot 19, SetCurrentViewFilterInfo, ret 4). The
// argument is the caller's holder of the new filter. Message 0x06CDB65A
// goes out before the change and 0x06CDB65B after it, both with the
// address of a holder of the old filter and the caller's holder. The
// message server is not checked for null. True always.
uint8_t __fastcall SetFilter(char* self, void*, void** holder)
{
    void* old = At<void*>(self, view::kFilter);
    if (old)
        AddRef(old);
    void* const server = Global<void*>(addr::SC4_MessageServer);

    void* message = NewMessage(0x06CDB65A);
    Virtual<void>(message, 0x30, &old);
    Virtual<void>(message, 0x44, holder);
    Virtual<void>(server, 0xC, message);
    Release(message);

    void* const filter = *holder;
    if (At<void*>(self, view::kFilter) != filter) {
        if (filter)
            AddRef(filter);
        if (void* const kept = At<void*>(self, view::kFilter))
            Release(kept);
        At<void*>(self, view::kFilter) = *holder;
    }

    message = NewMessage(0x06CDB65B);
    Virtual<void>(message, 0x30, &old);
    Virtual<void>(message, 0x44, holder);
    Virtual<void>(server, 0xC, message);
    Release(message);

    if (old)
        Release(old);
    return 1;
}

// The game's 0x007E6500 (slot 21, SetCurrentViewLevelInfo, ret 4). The
// argument points at the new level. When it differs the level is set and
// the messages 0xC6CDBF1A and 0xC6CDBF1B go out, each with the addresses
// of the old and the new level. EAX is whatever is left, as in the
// original: the level when nothing changed, else the last Release's count.
uint32_t __fastcall SetLevelInfo(char* self, void*, uint32_t* info)
{
    uint32_t was = At<uint32_t>(self, view::kLevel);
    uint32_t now = *info;
    if (was == now)
        return now;
    At<uint32_t>(self, view::kLevel) = now;
    void* const server = Global<void*>(addr::SC4_MessageServer);

    void* message = NewMessage(0xC6CDBF1A);
    Virtual<void>(message, 0x30, &was);
    Virtual<void>(message, 0x44, &now);
    Virtual<void>(server, 0xC, message);
    Release(message);

    message = NewMessage(0xC6CDBF1B);
    Virtual<void>(message, 0x30, &was);
    Virtual<void>(message, 0x44, &now);
    Virtual<void>(server, 0xC, message);
    return Release(message);
}

// The game's 0x007E65E0 (slot 24, GetCurrentViewInputControl): the tool on
// top of the stack, without a reference.
void* __fastcall GetCurrentTool(char* self, void*)
{
    void** const begin = At<void**>(self, view::kTools);
    void** const end = At<void**>(self, view::kTools + 4);
    return begin == end ? nullptr : end[-1];
}

// The game's 0x007E6600 (the window's slot 0x204, a character, ret 4).
// `this` is the window part. The stacked tools get it from the top down
// until one takes it; when none does, every persistent tool gets it. The
// second walk checks its index against the stack's size, not the
// persistent tools', as in the original. True always.
uint8_t __fastcall WindowCharacter(char* window, void*, uint32_t character)
{
    char* const self = window - view::kWindow;
    for (int32_t i = Count(self, view::kTools) - 1; i >= 0; --i) {
        if (static_cast<uint32_t>(i) >= static_cast<uint32_t>(Count(self, view::kTools)))
            break;
        void* const tool = Item(self, view::kTools, i);
        if (tool)
            AddRef(tool);
        if (Virtual<uint8_t>(tool, 0x34, character)) {
            if (tool)
                Release(tool);
            return 1;
        }
        Release(tool);
    }
    for (int32_t i = Count(self, view::kPersistent) - 1; i >= 0; --i) {
        if (static_cast<uint32_t>(i) >= static_cast<uint32_t>(Count(self, view::kTools)))
            break;
        void* const tool = Item(self, view::kPersistent, i);
        if (tool)
            AddRef(tool);
        Virtual<uint8_t>(tool, 0x34, character);
        Release(tool);
    }
    return 1;
}

// A key for the tools: the game's 0x007E66D0 (key down, tool slot 14) and
// 0x007E67D0 (key up, slot 15). The modifiers are kept; the stacked tools
// get the key from the top down until one takes it, then every persistent
// tool gets it. Returns whether any tool took it.
uint8_t ToolsKey(char* self, uint32_t key, uint32_t modifiers, std::size_t slot)
{
    At<uint32_t>(self, view::kModifiers) = modifiers;
    uint8_t taken = 0;
    for (int32_t i = Count(self, view::kTools) - 1; i >= 0; --i) {
        if (static_cast<uint32_t>(i) >= static_cast<uint32_t>(Count(self, view::kTools)))
            break;
        void* const tool = Item(self, view::kTools, i);
        if (tool)
            AddRef(tool);
        if (Virtual<uint8_t>(tool, slot, key, modifiers)) {
            taken = 1;
            if (tool)
                Release(tool);
            break;
        }
        Release(tool);
    }
    for (int32_t i = Count(self, view::kPersistent) - 1; i >= 0; --i) {
        if (static_cast<uint32_t>(i) >= static_cast<uint32_t>(Count(self, view::kPersistent)))
            break;
        void* const tool = Item(self, view::kPersistent, i);
        if (tool)
            AddRef(tool);
        if (Virtual<uint8_t>(tool, slot, key, modifiers))
            taken = 1;
        Release(tool);
    }
    return taken;
}

// The game's 0x007E66D0 (ret 8). `this` is the view.
uint8_t __fastcall ToolsKeyDown(char* self, void*, uint32_t key, uint32_t modifiers)
{
    return ToolsKey(self, key, modifiers, 0x38);
}

// The game's 0x007E67D0 (ret 8). `this` is the view.
uint8_t __fastcall ToolsKeyUp(char* self, void*, uint32_t key, uint32_t modifiers)
{
    return ToolsKey(self, key, modifiers, 0x3C);
}

// A mouse button for the tools of one vector, from the last one down, until
// one takes it. Returns whether one did.
bool ToolsButton(char* self, std::size_t vector, std::size_t slot, int32_t x, int32_t y, uint32_t modifiers)
{
    for (int32_t i = Count(self, vector) - 1; i >= 0; --i) {
        if (static_cast<uint32_t>(i) >= static_cast<uint32_t>(Count(self, vector)))
            break;
        void* const tool = Item(self, vector, i);
        if (tool)
            AddRef(tool);
        if (Virtual<uint8_t>(tool, slot, x, y, modifiers)) {
            if (tool)
                Release(tool);
            return true;
        }
        Release(tool);
    }
    return false;
}

// The game's 0x007E68D0 (the window's slot 0x218, left button down, ret
// 0xC). `this` is the window part. The modifiers are kept. A running
// tutorial sees the point first (its slot 16, which may move it) and can
// swallow the press. Then the persistent tools get it and then the stacked
// ones, each from the last down, until one takes it. True always.
uint8_t __fastcall WindowMouseDownL(char* window, void*, int32_t x, int32_t y, uint32_t modifiers)
{
    char* const self = window - view::kWindow;
    At<uint32_t>(self, view::kModifiers) = modifiers;
    if (void* const city = Global<void*>(addr::SC4_City)) {
        void* const tutorial = Virtual<void*>(city, 0x148);
        if (tutorial && Virtual<uint8_t>(tutorial, 0x20)) {
            if (!Virtual<uint8_t>(tutorial, 0x40, &x, &y, int32_t{-1}))
                return 1;
        }
    }
    if (ToolsButton(self, view::kPersistent, 0x40, x, y, modifiers))
        return 1;
    ToolsButton(self, view::kTools, 0x40, x, y, modifiers);
    return 1;
}

// The game's 0x007E6A00 (the window's slot 0x21C, right button down, ret
// 0xC). `this` is the window part. The modifiers are kept and the window's
// manager is told of the press (0x0099BDFE). The persistent tools get it,
// then the stacked ones, until one takes it. When none does, the point is
// kept and the drag-to-scroll tool (0x48 bytes, constructor 0x004B7DC0) is
// put on top of the stack and started at the point (0x004B82F0). The
// memory is not checked before use, as in the original. True always.
uint8_t __fastcall WindowMouseDownR(char* window, void*, int32_t x, int32_t y, uint32_t modifiers)
{
    char* const self = window - view::kWindow;
    At<uint32_t>(self, view::kModifiers) = modifiers;
    This<void>(addr::SC4Win_F99BDFE, window);
    if (ToolsButton(self, view::kPersistent, 0x44, x, y, modifiers))
        return 1;
    if (ToolsButton(self, view::kTools, 0x44, x, y, modifiers))
        return 1;
    At<int32_t>(self, view::kDragX) = x;
    At<int32_t>(self, view::kDragY) = y;
    At<uint8_t>(self, view::kDragMoved) = 0;
    void* const memory = New(0x48);
    void* const tool = memory ? This<void*>(addr::SC4ScrollDragTool_Construct, memory) : nullptr;
    AddRef(tool);
    Virtual<uint8_t>(self, 0x64, tool, kKeepTools);
    This<void>(addr::SC4ScrollDragTool_Start, tool, x, y);
    Release(tool);
    return 1;
}

// The game's 0x007E6B50 (the window's slot 0x22C, the wheel, ret 0x10).
// `this` is the window part. The modifiers are kept. The stacked tools get
// the turn from the top down until one takes it; when none does, every
// persistent tool gets it and the view zooms in for a turn above zero and
// out for any other. True always.
uint8_t __fastcall WindowMouseWheel(char* window, void*, int32_t x, int32_t y, uint32_t modifiers, int32_t turn)
{
    char* const self = window - view::kWindow;
    At<uint32_t>(self, view::kModifiers) = modifiers;
    for (int32_t i = Count(self, view::kTools) - 1; i >= 0; --i) {
        if (static_cast<uint32_t>(i) >= static_cast<uint32_t>(Count(self, view::kTools)))
            break;
        void* const tool = Item(self, view::kTools, i);
        if (tool)
            AddRef(tool);
        if (Virtual<uint8_t>(tool, 0x54, x, y, modifiers, turn)) {
            if (tool)
                Release(tool);
            return 1;
        }
        Release(tool);
    }
    for (int32_t i = Count(self, view::kPersistent) - 1; i >= 0; --i) {
        if (static_cast<uint32_t>(i) >= static_cast<uint32_t>(Count(self, view::kPersistent)))
            break;
        void* const tool = Item(self, view::kPersistent, i);
        if (tool)
            AddRef(tool);
        Virtual<uint8_t>(tool, 0x54, x, y, modifiers, turn);
        Release(tool);
    }
    if (turn > 0)
        Virtual<uint8_t>(self, 0x24);
    else
        Virtual<uint8_t>(self, 0x28);
    return 1;
}

// The game's 0x007E6C60 (slot 35, KillKeyboardScrolling, jumps here; six
// more callers). Turns the scroll directions into a scroll. The keyboard's
// directions (+0x348, +0x34C) go before the edges' (+0x350, +0x354), which
// are cleared when the options' byte at +0xF09 is set (but still used this
// once, as in the original). With no direction the scroll is stopped
// unless it is stopping already; else SetScrolling(true, angle, speed)
// with the angle of the direction pair and the speed (the keyboard's when a
// key gives a direction, else the edges') times 1.2 to the power of the
// camera control's zoom.
void __fastcall UpdateScroll(char* self, void*)
{
    // The angle for (x, y) at [4 + x + 3 * y], x and y from -1 to 1.
    static const uint32_t kAngles[9] = {
        0x407B53D2, 0x4096CBE4, 0x40AFEDE0, // 5/4, 3/2 and 7/4 of pi
        0x40490FDB, 0x00000000, 0x00000000, // pi, none, 0
        0x4016CBE4, 0x3FC90FDB, 0x3F490FDB, // 3/4, 1/2 and 1/4 of pi
    };

    int32_t x = At<int32_t>(self, view::kEdgeX);
    int32_t y = At<int32_t>(self, view::kEdgeY);
    const int32_t keyX = At<int32_t>(self, view::kKeyX);
    const bool keys = keyX != 0 || At<int32_t>(self, view::kKeyY) != 0;
    const int32_t zoom = static_cast<int32_t>(static_cast<int64_t>(
        static_cast<double>(At<int32_t>(At<char*>(self, view::kCamera), 0x108))));
    const double scale = Power(static_cast<double>(1.2f), static_cast<double>(zoom));
    if (At<uint8_t>(Global<char*>(addr::SC4_Options43CD8), 0xF09)) {
        At<int32_t>(self, view::kEdgeX) = 0;
        At<int32_t>(self, view::kEdgeY) = 0;
    }
    if (keyX)
        x = keyX;
    if (const int32_t keyY = At<int32_t>(self, view::kKeyY))
        y = keyY;
    if (x == 0 && y == 0) {
        if (!At<uint8_t>(self, view::kScrollStopping))
            Virtual<uint8_t>(self, 0x88);
        return;
    }
    const float base = keys ? At<float>(self, view::kKeySpeed) : At<float>(self, view::kEdgeSpeed);
    const float speed = static_cast<float>(static_cast<double>(base) * scale);
    Virtual<uint8_t>(self, 0x84, uint32_t{1}, kAngles[4 + x + 3 * y], speed);
}

// The game's 0x007E6DA0 (ret 4). In an established city, makes the tool of
// 0x98 bytes (constructor 0x004DAE80 with the argument) the view's only
// tool. The city is not checked for null.
void __fastcall StartTool98(char* self, void*, uint32_t argument)
{
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_City), 0x58))
        return;
    void* const memory = New(0x98);
    void* const tool = memory ? This<void*>(addr::SC4ViewTool_Construct4DAE80, memory, argument) : nullptr;
    if (tool)
        AddRef(tool);
    Virtual<uint8_t>(self, 0x64, tool, kReplaceTools);
    if (tool)
        Release(tool);
}

// The game's 0x007E6E00. `this` is not used. Runs the modal window
// 0x2A5CFB2C of layout 0x0A5CF71D (what it ends with is not looked at) and
// then asks the application to go to the region view without its own
// question (slot 12 with 0).
void __fastcall LeaveCity(char*, void*)
{
    uint8_t guard[4];
    This<void*>(addr::SC4ModalGuard_Construct, &guard[0], uint32_t{0}, uint32_t{0}, uint32_t{1}, uint32_t{1});
    Cdecl<uint32_t>(addr::SC4UI_F78E2F0, uint32_t{0x0A5CF71D}, uint32_t{0x2A5CFB2C});
    This<void>(addr::SC4ModalGuard_Destruct, &guard[0]);
    if (void* const app = Global<void*>(addr::SC4_Service43C94))
        Virtual<uint8_t>(app, 0x30, uint32_t{0});
}

// The game's 0x007E6EE0. The three day and night buttons of the secondary
// menu: the one of the clock's mode (slot 6: 0 to 2) pressed in.
void __fastcall UpdateDayNightButtons(char* self, void*)
{
    static const uint32_t kButtons[3] = {0xCA35CB74, 0xCA35CB76, 0xCA35CB78};
    static const uint32_t kModes[3] = {0, 1, 2};

    const uint32_t mode = Virtual<uint32_t>(Global<void*>(addr::SC4_Clock43DBC), 0x18);
    if (!At<void*>(self, view::kSecondary))
        return;
    for (std::size_t i = 0; i < 3; ++i) {
        Cdecl<uint8_t>(addr::SC4Win_CheckChild, At<void*>(self, view::kSecondary), kButtons[i],
                       uint32_t{mode == kModes[i]});
    }
}

// Shows the panel asked for and hides the other five: the advisor window
// (the advisor system's slots 25 to 27), the budget panel, the map window,
// the graphs window, the panel at +0x30C and the style panel. Returns
// whether nothing had to be hidden. The game's 0x007E6F60 while the panels
// are not put away; 0x007E7830 has the same code inline three times.
bool ShowOnly(char* self, uint32_t panel)
{
    bool nothingHidden = true;
    void* const advisors = Virtual<void*>(Global<void*>(addr::SC4_City), 0x144);
    if (panel == kPanelAdvice) {
        Virtual<void>(advisors, 0x64);
        At<uint8_t>(self, view::kAdviceAsked) = 1;
    } else if (Virtual<uint8_t>(advisors, 0x6C)) {
        Virtual<void>(Virtual<void*>(Global<void*>(addr::SC4_City), 0x144), 0x68);
        nothingHidden = false;
    }
    if (void* const budget = At<void*>(self, view::kBudget)) {
        if (panel == kPanelBudget) {
            Virtual<void>(budget, 0x18);
        } else if (Virtual<uint8_t>(budget, 0x24)) {
            Virtual<void>(At<void*>(self, view::kBudget), 0x1C);
            nothingHidden = false;
        }
    }
    if (void* const map = At<void*>(self, view::kMap)) {
        if (panel == kPanelMap) {
            Virtual<void>(map, 0x114);
        } else {
            Virtual<void>(map, 0x118);
            nothingHidden = false;
        }
    }
    if (void* const graphs = At<void*>(self, view::kGraphs)) {
        if (panel == kPanelGraphs) {
            This<void>(addr::SC4UI_F76E420, graphs);
        } else if (This<uint8_t>(addr::SC4UI_F76B6A0, graphs)) {
            This<void>(addr::SC4UI_F76E470, At<void*>(self, view::kGraphs));
            nothingHidden = false;
        }
    }
    if (void* const other = At<void*>(self, view::kPanel30C)) {
        if (panel == kPanelOther) {
            Virtual<void>(other, 0x114);
        } else {
            Virtual<void>(other, 0x118);
            nothingHidden = false;
        }
    }
    if (void* const style = At<void*>(self, view::kStyle)) {
        if (panel == kPanelStyle) {
            This<void>(addr::SC4UI_F769B30, style);
        } else if (This<uint8_t>(addr::SC4UI_F7696C0, style)) {
            This<void>(addr::SC4UI_F7696B0, At<void*>(self, view::kStyle));
            nothingHidden = false;
        }
    }
    return nothingHidden;
}

// The game's 0x007E6F60 (ret 4). Shows one panel and hides the others,
// unless the panels are put away (+0x374), and notes the panel asked for.
// Asked for none (0), it only hides, and marks the panels as put away when
// there was nothing to hide. The style panel asked for by its own ID is
// noted even then, like every panel but 0.
void __fastcall ShowPanel(char* self, void*, uint32_t panel)
{
    bool nothingHidden = true;
    if (!At<uint32_t>(self, view::kPanelsAway))
        nothingHidden = ShowOnly(self, panel);
    if (panel != 0) {
        At<uint32_t>(self, view::kPanel) = panel;
        At<uint32_t>(self, view::kPanelsAway) = 0;
        return;
    }
    if (nothingHidden)
        At<uint32_t>(self, view::kPanelsAway) = 1;
}

// The game's 0x007E70C0 (ret 4). A day and night button of the secondary
// menu was pressed: the clock's notifications go on (slot 29 with 1), its
// mode is set (slot 5: 0, 1 or 2 for the three buttons; none for any other
// ID), the mode it then has is written to the first byte of the
// application's preferences (not checked for null) and the buttons are
// shown again.
void __fastcall SetDayNight(char* self, void*, uint32_t button)
{
    void* const clock = Global<void*>(addr::SC4_Clock43DBC);
    uint8_t* const preferences = Virtual<uint8_t*>(Global<void*>(addr::SC4_Service43C94), 0x98);
    Virtual<void>(clock, 0x74, uint32_t{1});
    if (button == 0xCA35CB74)
        Virtual<void>(clock, 0x14, uint32_t{0});
    else if (button == 0xCA35CB76)
        Virtual<void>(clock, 0x14, uint32_t{1});
    else if (button == 0xCA35CB78)
        Virtual<void>(clock, 0x14, uint32_t{2});
    *preferences = Virtual<uint8_t>(clock, 0x18);
    UpdateDayNightButtons(self, nullptr);
}

// The game's 0x007E7130 (ret 8). A choice of a pop-up menu, by the ID in
// the message's third dword: 1 opens the tertiary menu 0x09DE8798 of
// layout 0x09DE3002, placed on the main window's child 1 (the first
// argument only goes into an argument 0x007E5D80 does not use);
// 0x0992FD36 has the disaster layer go to the next disaster (its slot 11);
// four IDs make the tool of 0x90 bytes (constructor 0x004BA7D0 with a
// kind: 0 for 0x33, 1 for 0x69A30679, 5 for 0x6BFC8C55, 6 for 0x4BFC8C2F)
// the view's only tool.
void __fastcall MenuChoice(char* self, void*, uint32_t passed, const uint32_t* message)
{
    uint32_t kind;
    switch (message[2]) {
    case 1:
        OpenTertiary(self, nullptr, 0x09DE3002, 0x09DE8798, passed, 1, 0xFFFFFFE7);
        return;
    case 0x0992FD36:
        Virtual<void>(Virtual<void*>(Global<void*>(addr::SC4_City), 0x120), 0x2C);
        return;
    case 0x33:
        kind = 0;
        break;
    case 0x69A30679:
        kind = 1;
        break;
    case 0x6BFC8C55:
        kind = 5;
        break;
    case 0x4BFC8C2F:
        kind = 6;
        break;
    default:
        return;
    }
    void* const memory = New(0x90);
    void* const tool = memory ? This<void*>(addr::SC4DispatchTool_Construct, memory, kind) : nullptr;
    if (tool)
        AddRef(tool);
    Virtual<uint8_t>(self, 0x64, tool, kReplaceTools);
    if (tool)
        Release(tool);
}

// The game's 0x007E7220 (ret 4). The object at +0x3D0, through its
// interface 0x86D72B57: slot 3 with (255, 255, 255, the argument).
void __fastcall SetObject3D0(char* self, void*, uint32_t value)
{
    void* part = nullptr;
    if (!Virtual<uint8_t>(At<void*>(self, view::kObject3D0), 0x0, uint32_t{0x86D72B57}, &part))
        return;
    Virtual<void>(part, 0xC, uint32_t{0xFF}, uint32_t{0xFF}, uint32_t{0xFF}, value);
    Release(part);
}

// The game's 0x007E7270 (ret 0x18). The secondary menu built around a menu
// of build items. `id` is what it is opened for, `bar` the window with the
// button `button` it hangs below, `picture` the instance of the frame's
// picture and `place`, when not null, two dwords with the chosen row and
// the first row to start with (and to hand back at closing).
//
// The menu gets cells of 44 by 44 with a gap of 5, the view as its window
// procedure and an info tip of its own with the view's tip's picture and
// font. Then, as in 0x007E5C10: the tools go, and asked for the menu that
// is open the view only closes it (the menu keeps the reference taken at
// the top: the original does not give it back on this path). Else a frame
// with scroll arrows (0x150 bytes, constructor 0x0079AFF0) is made a child
// of the view, laid out for the size the menu wants for 1 to 6 rows, placed
// below the middle of the button and above the layout window's top less
// 10, and shown; the menu's window goes into it. A running tutorial is
// told of the menu (its slot 15). Nothing made here is checked for null.
void __fastcall OpenMenu(char* self, void*, uint32_t id, void* bar, uint32_t button, void* menu, uint32_t picture,
                         uint32_t* place)
{
    void* memory = New(0x1B4);
    void* const tip = memory ? This<void*>(addr::SC4InfoTip_Construct, memory) : nullptr;
    AddRef(menu);
    Virtual<void>(menu, 0x30, uint32_t{0x2C}, uint32_t{0x2C}, uint32_t{5});
    Virtual<void>(Virtual<void*>(menu, 0xC), 0x158, static_cast<void*>(self + view::kWindow));
    uint32_t rows = Virtual<uint32_t>(menu, 0x38);
    if (rows < 1)
        rows = 1;
    else if (rows > 6)
        rows = 6;
    uint32_t wanted[2];
    const uint32_t* const size = Virtual<const uint32_t*>(menu, 0x34, &wanted[0], rows);
    const uint32_t width = size[0];
    const uint32_t height = size[1];

    AddRef(tip);
    Virtual<void>(tip, 0x2C, uint32_t{0xA85F1A83});
    Virtual<void>(tip, 0x28, uint32_t{0xE9C86C9D});
    Virtual<void>(Virtual<void*>(tip, 0xC), 0x118);
    void* const tipPicture = Virtual<void*>(At<void*>(self, view::kTip), 0x14);
    Virtual<void>(tip, 0x10, tipPicture, uint32_t{8}, uint32_t{5});
    const uint32_t tipFont = Virtual<uint32_t>(At<void*>(self, view::kTip), 0x50);
    Virtual<void>(tip, 0x4C, tipFont);
    Virtual<void>(menu, 0x58, tip);
    Release(tip);

    Virtual<uint8_t>(self, 0x6C, uint32_t{0});
    if (const uint32_t open = At<uint32_t>(self, view::kSecondaryId)) {
        Virtual<void>(self, 0xEC);
        if (open == id)
            return;
    }

    void* const anchor = Virtual<void*>(bar, 0x88, button);
    int32_t x = Virtual<int32_t>(anchor, 0xA4) / 2;
    x = x + Virtual<int32_t>(anchor, 0xAC) - 1;
    x += Virtual<int32_t>(bar, 0xAC);
    int32_t y = Virtual<int32_t>(anchor, 0xA8) / 2;
    y += Virtual<int32_t>(anchor, 0xB0);
    y += Virtual<int32_t>(bar, 0xB0);
    const int32_t top = Virtual<int32_t>(At<void*>(self, view::kLayout), 0xB0);

    uint32_t key[3] = {kPngType, kPictureGroup, picture};
    ImageLoader loader;
    This<void*>(addr::SC4ImageLoader_ConstructKey, &loader, &key[0], uint32_t{1}, uint32_t{0});
    void* const image = loader.image;

    memory = New(0x150);
    char* const arrows = memory ? This<char*>(addr::SC4UI_F79AFF0, memory) : nullptr;
    AddRef(arrows);
    Virtual<void>(self + view::kWindow, 0x38, Virtual<void*>(arrows, 0xC));
    Virtual<void>(arrows, 0x10, image, uint32_t{0x35}, uint32_t{0x19}, uint32_t{0x5E}, uint32_t{0x3E}, uint32_t{6},
                  uint32_t{0x28}, uint32_t{0x22});
    Virtual<void>(arrows, 0x14, width, height, x, y, uint32_t{10}, top - 10);
    char* const frame = arrows + 4;
    Virtual<void>(frame, 0x5C);
    Virtual<void>(frame, 0x114);
    uint32_t area[4];
    Virtual<void>(arrows, 0x18, &area[0]);
    Virtual<void>(Virtual<void*>(menu, 0xC), 0xD8, &area[0]);
    Virtual<void>(frame, 0x38, Virtual<void*>(menu, 0xC));
    Virtual<void>(frame, 0x158, Virtual<void*>(menu, 0xC));
    Release(menu);

    At<uint32_t*>(self, view::kMenuPlace) = place;
    if (place) {
        Virtual<void>(menu, 0x40, place[0]);
        Virtual<void>(menu, 0x28, place[1]);
    }
    if (void* const city = Global<void*>(addr::SC4_City)) {
        void* const tutorial = Virtual<void*>(city, 0x148);
        if (tutorial && Virtual<uint8_t>(tutorial, 0x20))
            Virtual<void>(tutorial, 0x3C, menu);
    }
    At<void*>(self, view::kSecondary) = frame;
    At<uint32_t>(self, view::kSecondaryId) = id;
    This<void>(addr::SC4ImageLoader_Destruct, &loader);
}

// The game's 0x007E75B0 (ret 4). A terrain command, 0 to 7. First a sound:
// the sound service's slot 7 with (1, the sound of the camera view's dword
// at +0xC from the table at 0x00ABC05C, a fresh number from 0x004F7BB0, 0,
// 0); the static object 0x004ECD60 hands out is made on the way and not
// used. Then: 0 asks for the terrain setting (0x007E5F40); 1 and 2 are the
// terrain service's slots 43 and 44 with 1; 3 and 4 its slot 48 with 1 or
// 0 and the float its slot 47 gives; 5 and 6 post the message 0x470BD214
// with 0 or 1 (when there is a message server); 7 makes the tool of 0x58
// bytes (constructor 0x004BDC60) the view's only tool. The terrain service
// is checked for null in 1 to 4; the tool's memory is not.
void __fastcall TerrainCommand(char* self, void*, uint32_t command)
{
    void* const sounds = Global<void*>(addr::SC4_Sound43CB8);
    if (sounds) {
        AddRef(sounds);
        Cdecl<void*>(addr::SC4Sound_StaticObject);
        const uint32_t number = Cdecl<uint32_t>(addr::SC4Sound_NextInstance);
        const uint32_t index = At<uint32_t>(Global<char*>(addr::SC4_CameraView), 0xC);
        const uint32_t sound = reinterpret_cast<const uint32_t*>(addr::SC4View3DWin_ZoomSounds)[index];
        Virtual<void>(sounds, 0x1C, uint32_t{1}, sound, number, uint32_t{0}, uint32_t{0});
    }
    switch (command) {
    case 0:
        AskTerrainSetting(self, nullptr);
        break;
    case 1:
        if (void* const terrain = Global<void*>(addr::SC4_Terrain))
            Virtual<void>(terrain, 0xAC, uint32_t{1});
        break;
    case 2:
        if (void* const terrain = Global<void*>(addr::SC4_Terrain))
            Virtual<void>(terrain, 0xB0, uint32_t{1});
        break;
    case 3:
    case 4:
        if (void* const terrain = Global<void*>(addr::SC4_Terrain)) {
            const float amount = static_cast<float>(Virtual<double>(terrain, 0xBC));
            Virtual<void>(terrain, 0xC0, uint32_t{command == 3}, amount);
        }
        break;
    case 5:
    case 6:
        if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
            void* const message = NewMessage(0x470BD214);
            Virtual<void>(message, 0x2C, uint32_t{command == 6});
            Virtual<void>(server, 0x10, message, uint32_t{0});
            Release(message);
        }
        break;
    case 7: {
        void* const memory = New(0x58);
        void* const tool = memory ? This<void*>(addr::SC4TerrainLevelTool_Construct, memory) : nullptr;
        AddRef(tool);
        Virtual<uint8_t>(self, 0x64, tool, kReplaceTools);
        Release(tool);
        break;
    }
    default:
        break;
    }
    if (sounds)
        Release(sounds);
}

// The game's 0x007E77F0 (slot 57, HideMissionStatusPanel). Takes the panel
// off the view's window (window slot 0x3C) and lets it go.
void __fastcall HideMissionPanel(char* self, void*)
{
    void* const panel = At<void*>(self, view::kMissionPanel);
    if (!panel)
        return;
    Virtual<void>(self + view::kWindow, 0x3C, panel);
    if (void* const kept = At<void*>(self, view::kMissionPanel)) {
        At<void*>(self, view::kMissionPanel) = nullptr;
        Release(kept);
    }
}

// The game's 0x007E7830 (ret 4). A panel is asked for by a pair of IDs (a
// control and what it stands for), as the buttons of other windows send
// them. The layout window's button 0xC988BC79 is set (slot 5 with 1, 1);
// without the layout window or the button nothing happens. Then, by the
// first ID:
//
//   0xEA5BDFBC  the budget panel. For 0xCA5BDFB0 and 0x4A5BDFB8 it is shown
//               (as ShowPanel does for 0x41) and its window gets the click
//               of 0xAA3AC40F or 0xAA3AC40E posted; any other second ID is
//               posted to it as a click as it is. The budget panel is not
//               checked for null.
//   0x6A720993  the graphs window is shown and told the second ID
//               (0x0076E4B0).
//   0x6A5BDFCD  the map window is shown and sent the click of 0x4A32CA92
//               and then the click of the second ID. The map window is not
//               checked for null there.
void __fastcall PanelRequest(char* self, void*, const uint32_t* request)
{
    void* button = nullptr;
    void* const layout = At<void*>(self, view::kLayout);
    if (!layout)
        return;
    Virtual<uint8_t>(layout, 0x94, uint32_t{0xC988BC79}, kCheckIid, &button);
    if (!button)
        return;
    Virtual<void>(button, 0x14, uint32_t{1}, uint32_t{1});

    if (request[0] == 0x6A5BDFCD) {
        ShowPanel(self, nullptr, kPanelMap);
        WinMessage message = {kCommand, kButtonClicked, 0x4A32CA92, 0};
        Virtual<uint8_t>(At<void*>(self, view::kMap), 0xC, &message);
        message.data = request[1];
        Virtual<uint8_t>(At<void*>(self, view::kMap), 0xC, &message);
    } else if (request[0] == 0x6A720993) {
        ShowPanel(self, nullptr, kPanelGraphs);
        if (void* const graphs = At<void*>(self, view::kGraphs))
            This<void>(addr::SC4UI_F76E4B0, graphs, request[1]);
    } else if (request[0] == 0xEA5BDFBC) {
        void* const window = Virtual<void*>(At<void*>(At<char*>(self, view::kBudget), 0x10), 0xC);
        if (window)
            AddRef(window);
        uint32_t click = request[1];
        if (click == 0x4A5BDFB8) {
            ShowPanel(self, nullptr, kPanelBudget);
            click = 0xAA3AC40E;
        } else if (click == 0xCA5BDFB0) {
            ShowPanel(self, nullptr, kPanelBudget);
            click = 0xAA3AC40F;
        }
        if (window) {
            WinMessage message = {kCommand, kButtonClicked, click, 0};
            Virtual<uint8_t>(self + view::kWindow, 0x24C, window, &message);
            Release(window);
        }
    }
    if (button)
        Release(button);
}

// Puts an object into a holder: the new one gains a reference, the old one
// loses one.
void Hold(void*& holder, void* object)
{
    void* const old = holder;
    if (object == old)
        return;
    if (object)
        AddRef(object);
    holder = object;
    if (old)
        Release(old);
}

// The game's 0x007E7C80 (the hover tip's slot 4, ret 0xC). `this` is the
// hover tip; the three arguments are not used. Once, and only when there is
// a window manager: asks for seven messages (the poster's slot 5), makes
// the info tip with its picture 0x14416190 and puts its window into the
// window manager's root window, loads the picture 0x14416192, sets the
// timer to 750 ms with the tick 0x007E6060 and marks itself ready. Neither
// the tip nor the pictures are checked for null. True always.
uint8_t __fastcall HoverInit(char* self, void*, uint32_t, uint32_t, uint32_t)
{
    static const uint32_t kMessages[7] = {
        0xCA56DD76, 0x22C010D0, 0x22C010D1, 0x22C010D2, 0x22C010D8, 0x22C010D5, 0x22C010D6,
    };

    if (At<uint8_t>(self, hover::kReady))
        return 1;
    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    if (!manager)
        return 1;
    if (void* const poster = Cdecl<void*>(addr::SC4_Singleton913B97)) {
        for (const uint32_t message : kMessages)
            Virtual<void>(poster, 0x14, static_cast<void*>(self), message);
    }

    void* const memory = New(0x1B4);
    void* const made = memory ? This<void*>(addr::SC4InfoTip_Construct, memory) : nullptr;
    Hold(At<void*>(self, hover::kTip), made);
    Virtual<void>(At<void*>(self, hover::kTip), 0x2C, uint32_t{0xA85F1A83});
    Virtual<void>(At<void*>(self, hover::kTip), 0x28, uint32_t{0xE9C86C9D});
    Virtual<void>(At<char*>(self, hover::kTip) + 4, 0x118);

    uint32_t key[3] = {kPngType, kPictureGroup, 0x14416190};
    ImageLoader first;
    This<void*>(addr::SC4ImageLoader_ConstructKey, &first, &key[0], uint32_t{1}, uint32_t{0});
    Hold(At<void*>(self, hover::kIconA), first.image);
    Virtual<void>(At<void*>(self, hover::kTip), 0x10, At<void*>(self, hover::kIconA), int32_t{-1}, int32_t{-1});
    void* const tip = At<void*>(self, hover::kTip);
    void* const root = Virtual<void*>(manager, 0xC);
    Virtual<void>(root, 0x38, Virtual<void*>(tip, 0xC));

    key[0] = kPngType;
    key[1] = kPictureGroup;
    key[2] = 0x14416192;
    ImageLoader second;
    This<void*>(addr::SC4ImageLoader_ConstructKey, &second, &key[0], uint32_t{1}, uint32_t{0});
    Hold(At<void*>(self, hover::kIconB), second.image);

    char* const timer = self + hover::kTimer;
    Virtual<void>(timer, 0xC, uint32_t{0x2EE});
    Virtual<void>(timer, 0x4, uint32_t{0x2EE});
    Virtual<void>(timer, 0x24, std::uintptr_t{addr::SC4UI_F7E6060}, uint32_t{0});
    At<uint8_t>(self, hover::kReady) = 1;
    This<void>(addr::SC4ImageLoader_Destruct, &second);
    This<void>(addr::SC4ImageLoader_Destruct, &first);
    return 1;
}

bool InstallCityView1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[64] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F7E5630, reinterpret_cast<void*>(&ViewQueryInterface)},
        {addr::SC4UI_F7E56D0, reinterpret_cast<void*>(&ViewAddRef)},
        {addr::SC4UI_F7E56E0, reinterpret_cast<void*>(&ViewRelease)},
        {addr::SC4UI_F7E5700, reinterpret_cast<void*>(&SetKeyAccelerator)},
        {addr::SC4UI_F7E5740, reinterpret_cast<void*>(&ScrollStop)},
        {addr::SC4UI_F7E5780, reinterpret_cast<void*>(&CanZoomIn)},
        {addr::SC4UI_F7E57A0, reinterpret_cast<void*>(&CanZoomOut)},
        {addr::SC4UI_F7E57C0, reinterpret_cast<void*>(&GetFilter)},
        {addr::SC4UI_F7E57E0, reinterpret_cast<void*>(&GetLevelInfo)},
        {addr::SC4UI_F7E57F0, reinterpret_cast<void*>(&GetLevel)},
        {addr::SC4UI_F7E5800, reinterpret_cast<void*>(&SetLevel)},
        {addr::SC4UI_F7E5820, reinterpret_cast<void*>(&RemoveAllTools)},
        {addr::SC4UI_F7E5840, reinterpret_cast<void*>(&PickTerrain)},
        {addr::SC4UI_F7E5890, reinterpret_cast<void*>(&PickOccupant)},
        {addr::SC4UI_F7E5900, reinterpret_cast<void*>(&GetTerrainQueryEnabled)},
        {addr::SC4UI_F7E5910, reinterpret_cast<void*>(&StartTool130)},
        {addr::SC4UI_F7E5960, reinterpret_cast<void*>(&LevelDiffers)},
        {addr::SC4UI_F7E59B0, reinterpret_cast<void*>(&UpdateSpeedButtons)},
        {addr::SC4UI_F7E5B00, reinterpret_cast<void*>(&UpdateZoomButtons)},
        {addr::SC4UI_F7E5B40, reinterpret_cast<void*>(&HoldSimulatorHook)},
        {addr::SC4UI_F7E5B70, reinterpret_cast<void*>(&LetSimulatorGo)},
        {addr::SC4UI_F7E5BA0, reinterpret_cast<void*>(&WindowCommand)},
        {addr::SC4UI_F7E5BB0, reinterpret_cast<void*>(&MakeLoadingPicture)},
        {addr::SC4UI_F7E5C10, reinterpret_cast<void*>(&OpenSecondary)},
        {addr::SC4UI_F7E5D20, reinterpret_cast<void*>(&CloseSecondary)},
        {addr::SC4UI_F7E5D80, reinterpret_cast<void*>(&OpenTertiary)},
        {addr::SC4UI_F7E5E90, reinterpret_cast<void*>(&CloseTertiary)},
        {addr::SC4UI_F7E5F40, reinterpret_cast<void*>(&AskTerrainSetting)},
        {addr::SC4UI_F7E5FB0, reinterpret_cast<void*>(&FlipSwitch)},
        {addr::SC4UI_F7E6030, reinterpret_cast<void*>(&HoverQueryInterface)},
        {addr::SC4UI_F7E6060, reinterpret_cast<void*>(&HoverTick)},
        {addr::SC4UI_F7E60B0, reinterpret_cast<void*>(&GetViewManager)},
        {addr::SC4UI_F7E60E0, reinterpret_cast<void*>(&SetZoom)},
        {addr::SC4UI_F7E6150, reinterpret_cast<void*>(&SetZoomLimits)},
        {addr::SC4UI_F7E61D0, reinterpret_cast<void*>(&SetRotation)},
        {addr::SC4UI_F7E6220, reinterpret_cast<void*>(&RotateLeft)},
        {addr::SC4UI_F7E6270, reinterpret_cast<void*>(&RotateRight)},
        {addr::SC4UI_F7E62C0, reinterpret_cast<void*>(&ZoomIn)},
        {addr::SC4UI_F7E6320, reinterpret_cast<void*>(&ZoomOut)},
        {addr::SC4UI_F7E6380, reinterpret_cast<void*>(&SetZoomAndRotation)},
        {addr::SC4UI_F7E63F0, reinterpret_cast<void*>(&SetFilter)},
        {addr::SC4UI_F7E6500, reinterpret_cast<void*>(&SetLevelInfo)},
        {addr::SC4UI_F7E65E0, reinterpret_cast<void*>(&GetCurrentTool)},
        {addr::SC4UI_F7E6600, reinterpret_cast<void*>(&WindowCharacter)},
        {addr::SC4UI_F7E66D0, reinterpret_cast<void*>(&ToolsKeyDown)},
        {addr::SC4UI_F7E67D0, reinterpret_cast<void*>(&ToolsKeyUp)},
        {addr::SC4UI_F7E68D0, reinterpret_cast<void*>(&WindowMouseDownL)},
        {addr::SC4UI_F7E6A00, reinterpret_cast<void*>(&WindowMouseDownR)},
        {addr::SC4UI_F7E6B50, reinterpret_cast<void*>(&WindowMouseWheel)},
        {addr::SC4UI_F7E6C60, reinterpret_cast<void*>(&UpdateScroll)},
        {addr::SC4UI_F7E6DA0, reinterpret_cast<void*>(&StartTool98)},
        {addr::SC4UI_F7E6E00, reinterpret_cast<void*>(&LeaveCity)},
        {addr::SC4UI_F7E6EE0, reinterpret_cast<void*>(&UpdateDayNightButtons)},
        {addr::SC4UI_F7E6F60, reinterpret_cast<void*>(&ShowPanel)},
        {addr::SC4UI_F7E70C0, reinterpret_cast<void*>(&SetDayNight)},
        {addr::SC4UI_F7E7130, reinterpret_cast<void*>(&MenuChoice)},
        {addr::SC4UI_F7E7220, reinterpret_cast<void*>(&SetObject3D0)},
        {addr::SC4UI_F7E7270, reinterpret_cast<void*>(&OpenMenu)},
        {addr::SC4UI_F7E75B0, reinterpret_cast<void*>(&TerrainCommand)},
        {addr::SC4UI_F7E77F0, reinterpret_cast<void*>(&HideMissionPanel)},
        {addr::SC4UI_F7E7830, reinterpret_cast<void*>(&PanelRequest)},
        {addr::SC4UI_F7E7C80, reinterpret_cast<void*>(&HoverInit)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].first);
        if (!found || !hook::Install(*found, hooks[i].second, &originals[i]))
            return false;
    }
    return true;
}

const bool g_cityView1Registered = registry::Register({
    "ui.cityview.part1",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F7E5630,
    "Our city view, first part: interface methods, zoom, tools' input, pop-up menus and panels (docs/ui.md)",
    false,
    InstallCityView1,
});

} // namespace
} // namespace opensc4::ui
