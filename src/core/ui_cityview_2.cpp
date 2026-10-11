// The city view, second part (0x007E7EA0 to 0x007EA5B0): cSC4View3DWin's
// tools, mouse handlers, window sets, cursor text and start.
//
// The city view (0x448 bytes, interface cISC4View3DWin, vtable 0x00ABCE40;
// two more interfaces at +4 and +8; its window part at +0xC, vtable
// 0x00ABCBC0) is the window a loaded city is shown and worked in. What lies
// in this range:
//
//   - the tools (view input controls). The view keeps a stack of them
//     (+0x134) and a row of persistent ones (+0x140). Here are the removal
//     of the current tool (interface slot 26), of one or all persistent
//     tools (slots 29 and 30), the message 0x09C8B8EA that tells of a change
//     (its class, 0x30 bytes, vtable 0x00ABCAB8, is a framework message with
//     one more reference), and the function that starts a tool for a command
//     of the menus (0x007E9FA0: seven kinds of tool, a disaster, a mission);
//   - the window's mouse handlers (slots 136, 137, 138 and 142 of the window
//     vtable): button let go left and right, the move, the exit. Each hands
//     the event to the persistent tools and then to the stack, from the last
//     to the first. The right button let go without a drag centres the
//     camera on what is under the cursor; the move works out the scrolling
//     at the screen's edges;
//   - the window sets: five vectors of windows at +0x1A4, shown (0x007E8B90)
//     and hidden (0x007E8CA0) together, with the panels that go with them,
//     and MinimizeUI (slot 55);
//   - the cursor's text (the info tip at +0x3D8 fed from a list at +0x3DC)
//     and the picture that follows the cursor (+0x3D4), which fades while it
//     drifts when its state (+0x3B4) is 2;
//   - the window's Init (slot 4): the command table, the cheat codes, the
//     size, the cursor, the loading picture;
//   - the view's owner (0x40 bytes, vtable 0x00ABCB60, made by the
//     application at 0x0044CB1C and kept at its +0x30C): constructor,
//     Release and Shutdown.
//
// Replacement, off by default:
//
//   ui.cityview.part2: everything named above but what is listed next.
//
// Still the game's: 0x007E8510, which builds the picture of the mayor
// rating's meter row by row before it places the meter's windows (drawing);
// the list code 0x007E9330, 0x007E9850, 0x007E98C0 (named, shared) and
// 0x007E9880 (a list insert the framework also calls, 0x0092DC1B).
//
// The view, from the object's start (the window's own fields are window
// offsets plus 0xC): +0xE4 what the window's Init returned, +0xF0 the camera
// control, +0xF4 the renderer, +0x100 a stopwatch, +0x11C the last mouse
// event's modifier keys, +0x120 an object of class 0x42E967BE, +0x125 a
// byte set by Init, +0x128 the message IDs the view listens for, +0x134 the
// tool stack (begin, end), +0x140 the persistent tools (begin, end), +0x14C
// a list of references, +0x15C a byte Init clears, +0x18C the control bar,
// +0x194 the window shown instead of it when the interface is minimized,
// +0x1A4 five vectors of windows (0xC bytes each), +0x1F4 the set that is
// up (-1 none), +0x2D4, +0x2D5 and +0x2D6 three bytes (the budget set, the
// MySim panel and set 4 are up), +0x2DC the news ticker, +0x2E4 the MySim
// panel, +0x2E8 the budget panel, +0x2EC a window, +0x2F8 the graphs
// window, +0x2FC the style panel, +0x300 the map window, +0x30C a window,
// +0x348 and +0x34C the keyboard's scrolling, +0x350 and +0x354 the
// scrolling at the screen's edges (-1, 0, 1 across and down), +0x358 and
// +0x35C where the mouse button went down, +0x360 a byte: the mouse moved
// more than 3 pixels since, +0x370 and +0x374 two dwords of the budget set,
// +0x3B4 the cursor picture's state, +0x3BC its time left in milliseconds,
// +0x3C0 and +0x3C4 the cursor, +0x3C8 and +0x3CC where the drift began,
// +0x3D0 the picture, +0x3D4 its window object, +0x3D8 the info tip, +0x3DC
// the list of cursor texts (nodes: +0x10 title, +0x24 text, +0x38 kind),
// +0x3E0 the tip's picture for kind 1, +0x3E4 a map Init clears.
//
// The owner: +0 vtable, +4 count, +8 a byte: started, +9 a byte, +0xC the
// view, +0x10 and +0x14 two references, +0x18 a timer.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <utility>

namespace opensc4::ui {
namespace {

constexpr std::size_t kMessageTarget = 0x4; // the view's second interface
constexpr std::size_t kWindow = 0xC;        // the view's window part

namespace view { // from the object's start
constexpr std::size_t kInitResult = 0xE4;
constexpr std::size_t kCamera = 0xF0;
constexpr std::size_t kRenderer = 0xF4;
constexpr std::size_t kWatch = 0x100;
constexpr std::size_t kModifiers = 0x11C;
constexpr std::size_t kObject120 = 0x120;
constexpr std::size_t kByte125 = 0x125;
constexpr std::size_t kMessageIds = 0x128;
constexpr std::size_t kStack = 0x134;
constexpr std::size_t kStackEnd = 0x138;
constexpr std::size_t kPersistent = 0x140;
constexpr std::size_t kPersistentEnd = 0x144;
constexpr std::size_t kReferences = 0x14C;
constexpr std::size_t kByte15C = 0x15C;
constexpr std::size_t kBar = 0x18C;
constexpr std::size_t kSmallBar = 0x194;
constexpr std::size_t kSets = 0x1A4;
constexpr std::size_t kSetUp = 0x1F4;
constexpr std::size_t kBudgetUp = 0x2D4;
constexpr std::size_t kMySimUp = 0x2D5;
constexpr std::size_t kFourUp = 0x2D6;
constexpr std::size_t kTicker = 0x2DC;
constexpr std::size_t kMySimPanel = 0x2E4;
constexpr std::size_t kBudgetPanel = 0x2E8;
constexpr std::size_t kWindow2EC = 0x2EC;
constexpr std::size_t kGraphs = 0x2F8;
constexpr std::size_t kStylePanel = 0x2FC;
constexpr std::size_t kMapWindow = 0x300;
constexpr std::size_t kWindow30C = 0x30C;
constexpr std::size_t kKeyScrollX = 0x348;
constexpr std::size_t kKeyScrollZ = 0x34C;
constexpr std::size_t kEdgeScrollX = 0x350;
constexpr std::size_t kEdgeScrollZ = 0x354;
constexpr std::size_t kDownX = 0x358;
constexpr std::size_t kDownZ = 0x35C;
constexpr std::size_t kDragged = 0x360;
constexpr std::size_t kBudget370 = 0x370;
constexpr std::size_t kBudget374 = 0x374;
constexpr std::size_t kPictureState = 0x3B4;
constexpr std::size_t kPictureTime = 0x3BC;
constexpr std::size_t kCursorX = 0x3C0;
constexpr std::size_t kCursorZ = 0x3C4;
constexpr std::size_t kDriftX = 0x3C8;
constexpr std::size_t kDriftZ = 0x3CC;
constexpr std::size_t kPictureImage = 0x3D0;
constexpr std::size_t kPicture = 0x3D4;
constexpr std::size_t kTip = 0x3D8;
constexpr std::size_t kTexts = 0x3DC;
constexpr std::size_t kTipPicture = 0x3E0;
constexpr std::size_t kMap3E4 = 0x3E4;
} // namespace view

namespace owner {
constexpr std::size_t kCount = 0x4;
constexpr std::size_t kStarted = 0x8;
constexpr std::size_t kByte9 = 0x9;
constexpr std::size_t kView = 0xC;
constexpr std::size_t kHeld10 = 0x10;
constexpr std::size_t kHeld14 = 0x14;
constexpr std::size_t kTimer = 0x18;
} // namespace owner

constexpr std::size_t kTipWindow = 0x4;      // the info tip's window part
constexpr std::size_t kMessageHeld = 0x2C;   // the tool message's extra reference

// A tool's slots (a view input control).
namespace tool {
constexpr std::size_t kShutdown = 0x10;
constexpr std::size_t kGetCursor = 0x1C;
constexpr std::size_t kIsSelfScrolling = 0x2C;
constexpr std::size_t kMouseUpL = 0x48;
constexpr std::size_t kMouseUpR = 0x4C;
constexpr std::size_t kMouseMove = 0x50;
constexpr std::size_t kMouseExit = 0x58;
constexpr std::size_t kDeactivate = 0x60;
constexpr std::size_t kIsCapturing = 0x64;
} // namespace tool

constexpr uint32_t kToolChanged = 0x09C8B8EA;
constexpr uint32_t kArrowCursor = 0xC16C4E1B;
constexpr uint32_t kViewId = 0x9A47B417;
constexpr uint32_t kUiGroup = 0x46A006B0;
constexpr uint32_t kOverlayWindow = 0x6A5E44B6;
constexpr uint32_t kOverlayIid = 0xCA5D3290;
constexpr uint32_t kBarButton = 0x4988BC6A;
constexpr uint32_t kBarCheck = 0x2988BC85;
constexpr uint32_t kMinimizedChild = 0xEA8CAD14;

// The camera view's projection of a world position to the screen (the
// camera view is the renderer's code: called by address only).
constexpr std::uintptr_t kCameraProject = 0x007F78E0;

// The game's string body and string.
struct StringBody {
    char* begin;
    char* end;
    char* capacity;
};

struct GameString {
    std::uintptr_t vtable;
    StringBody body;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// The picture loader the game makes on the stack.
struct ImageLoader {
    std::uintptr_t vtable;
    void* image;
    char rest[0x28];
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

// The inlined destructor of a string.
void DestroyString(GameString& text)
{
    char* const buffer = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (buffer)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(buffer));
}

// ftol (0x009EEF04): truncation.
int32_t Truncate(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// The number of pointers in a vector of the view.
int32_t Count(char* self, std::size_t vector)
{
    return static_cast<int32_t>(At<char*>(self, vector + 4) - At<char*>(self, vector)) >> 2;
}

// Hands an event to the tools of one vector, from the last to the first, as
// the handlers do: the index is checked against the count on every turn and
// the tool is held for the call. With `stop`, a tool that takes the event
// ends the walk and true comes back.
template <typename Call>
bool Offer(char* self, std::size_t vector, bool stop, Call call)
{
    for (int32_t index = Count(self, vector) - 1; index >= 0; --index) {
        if (static_cast<uint32_t>(index) >= static_cast<uint32_t>(Count(self, vector)))
            break;
        void* const control = At<void**>(self, vector)[index];
        if (control)
            AddRef(control);
        const uint8_t taken = call(control);
        if (stop && taken) {
            if (control)
                Release(control);
            return true;
        }
        Release(control);
    }
    return false;
}

// The news ticker is put away when none of the three sets that use it is up.
void PutTickerAway(char* self)
{
    if (!At<uint8_t>(self, view::kMySimUp) && !At<uint8_t>(self, view::kBudgetUp) &&
        !At<uint8_t>(self, view::kFourUp))
        Virtual<void>(At<void*>(self, view::kTicker), 0x18);
}

// The game's 0x007E7EA0. The owner's Shutdown (slot 5): the timer stopped,
// seven notifications dropped, the view's window taken off the main window,
// the three references let go.
uint8_t __fastcall OwnerShutdown(char* self, void*)
{
    if (At<uint8_t>(self, owner::kStarted)) {
        Virtual<void>(self + owner::kTimer, 0x1C);
        if (void* const server = Cdecl<void*>(addr::SC4_Singleton913B97)) {
            for (const uint32_t id : {0xCA56DD76u, 0x22C010D0u, 0x22C010D2u, 0x22C010D8u, 0x22C010D1u, 0x22C010D5u,
                                      0x22C010D6u})
                Virtual<uint8_t>(server, 0x18, static_cast<void*>(self), id);
        }
        if (void* const manager = Cdecl<void*>(addr::SC4_GetWinManager)) {
            if (void* const held = At<void*>(self, owner::kView)) {
                void* const mainWindow = Virtual<void*>(manager, 0xC);
                Virtual<void>(mainWindow, 0x40, Virtual<void*>(held, 0xC));
            }
            if (void* const held = At<void*>(self, owner::kView)) {
                At<void*>(self, owner::kView) = nullptr;
                Release(held);
            }
        }
        if (void* const held = At<void*>(self, owner::kHeld10)) {
            At<void*>(self, owner::kHeld10) = nullptr;
            Release(held);
        }
        if (void* const held = At<void*>(self, owner::kHeld14)) {
            At<void*>(self, owner::kHeld14) = nullptr;
            Release(held);
        }
        At<uint8_t>(self, owner::kStarted) = 0;
    }
    return 1;
}

// The game's 0x007E7F90. KillKeyboardScrolling (slot 35); the scrolling is
// worked out again by 0x007E6C60, whose result comes back.
uint32_t __fastcall KillKeyboardScrolling(char* self, void*)
{
    At<int32_t>(self, view::kKeyScrollX) = 0;
    At<int32_t>(self, view::kKeyScrollZ) = 0;
    return This<uint32_t>(addr::SC4UI_F7E6C60, self);
}

// A tool leaves the view: Deactivate, Shutdown, Release.
void Dismiss(void* control)
{
    Virtual<void>(control, tool::kDeactivate);
    Virtual<uint8_t>(control, tool::kShutdown);
    Release(control);
}

// The game's 0x007E7FB0. RemovePersistentViewInputControl (slot 29).
uint8_t __fastcall RemovePersistentTool(char* self, void*, void* control)
{
    uint8_t found = 0;
    void** at = At<void**>(self, view::kPersistent);
    if (at == At<void**>(self, view::kPersistentEnd))
        return found;
    do {
        void* const here = *at;
        if (here == control) {
            Dismiss(here);
            void** const end = At<void**>(self, view::kPersistentEnd);
            if (at + 1 != end) {
                Cdecl<void*>(addr::SC4_memmove, static_cast<void*>(at), static_cast<void*>(at + 1),
                             static_cast<uint32_t>(reinterpret_cast<char*>(end) - reinterpret_cast<char*>(at + 1)));
            }
            At<void**>(self, view::kPersistentEnd) -= 1;
            found = 1;
        } else {
            ++at;
        }
    } while (at != At<void**>(self, view::kPersistentEnd));
    return found;
}

// The game's 0x007E8030. RemoveAllPersistentViewInputControls (slot 30).
uint8_t __fastcall RemoveAllPersistentTools(char* self, void*)
{
    if (At<void**>(self, view::kPersistent) != At<void**>(self, view::kPersistentEnd)) {
        do {
            if (void* const last = At<void**>(self, view::kPersistentEnd)[-1])
                Dismiss(last);
            At<void**>(self, view::kPersistentEnd) -= 1;
        } while (At<void**>(self, view::kPersistent) != At<void**>(self, view::kPersistentEnd));
    }
    return 1;
}

// The game's 0x007E8090. Posts the message 0x09C8B8EA: the tool that was
// current and the one that is now. The message is not checked for null, as
// in the original.
void __fastcall PostToolChanged(char*, void*, void* before, void* now)
{
    void* const server = Global<void*>(addr::SC4_MessageServer);
    if (!server)
        return;
    char* message = nullptr;
    if (void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x30})) {
        message = static_cast<char*>(memory);
        This<void*>(addr::cGZMessage_cGZMessage, memory);
        At<std::uintptr_t>(message, 0) = addr::SC4View3DWin_ToolMessageVtable;
        At<void*>(message, kMessageHeld) = nullptr;
        AddRef(message);
    }
    Virtual<void>(message, 0x14, kToolChanged);
    Virtual<void>(message, 0x30, before);
    Virtual<void>(message, 0x84, before);
    Virtual<void>(message, 0x44, now);
    void* const old = At<void*>(message, kMessageHeld);
    if (now != old) {
        if (now)
            AddRef(now);
        At<void*>(message, kMessageHeld) = now;
        if (old)
            Release(old);
    }
    Virtual<uint8_t>(server, 0x10, static_cast<void*>(message), uint32_t{0});
    Release(message);
}

// The game's 0x007E8140. The tool message's destructor with the delete flag
// (slot 37 of its vtable).
void* __fastcall ToolMessageDelete(char* self, void*, uint32_t flags)
{
    if (void* const held = At<void*>(self, kMessageHeld))
        Release(held);
    This<void>(addr::cGZMessage_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::RZ_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x007E8170. The left button let go (window slot 136). The
// tutorial may take the event or move its place.
uint8_t __fastcall MouseUpL(char* part, void*, int32_t x, int32_t z, uint32_t modifiers)
{
    char* const self = part - kWindow;
    At<uint32_t>(self, view::kModifiers) = modifiers;
    void* const current = Virtual<void*>(self, 0x60);
    if (This<uint8_t>(addr::SC4Win_F99BE0A, part) && current && !Virtual<uint8_t>(current, tool::kIsSelfScrolling) &&
        Virtual<uint8_t>(current, tool::kIsCapturing))
        This<uint32_t>(addr::SC4UI_F7E6C60, self);

    if (void* const city = Global<void*>(addr::SC4_City)) {
        void* const tutorial = Virtual<void*>(city, 0x148);
        if (tutorial && Virtual<uint8_t>(tutorial, 0x20) && !Virtual<uint8_t>(tutorial, 0x40, &x, &z, uint32_t{1}))
            return 1;
    }

    const auto call = [&](void* control) { return Virtual<uint8_t>(control, tool::kMouseUpL, x, z, modifiers); };
    if (Offer(self, view::kPersistent, true, call))
        return 1;
    Offer(self, view::kStack, true, call);
    return 1;
}

// The game's 0x007E82E0. The right button let go (window slot 137). When the
// mouse did not move since the button went down and no modifier key is held,
// the camera is centred on the object under the cursor (the middle of its
// box, after two questions to the terrain whose answers are dropped), or on
// the ground there.
uint8_t __fastcall MouseUpR(char* part, void*, int32_t x, int32_t z, uint32_t modifiers)
{
    char* const self = part - kWindow;
    At<uint32_t>(self, view::kModifiers) = modifiers;
    if (This<uint8_t>(addr::SC4Win_F99BE0A, part))
        This<void>(addr::SC4Win_F99BE1E, part);

    if (!At<uint8_t>(self, view::kDragged) && !(modifiers & 7)) {
        const uint32_t unset = Global<uint32_t>(addr::SC4View3DWin_UnsetCoordinate);
        uint32_t position[3] = {unset, unset, unset};
        void* picked = nullptr;
        if (Virtual<uint8_t>(At<void*>(self, view::kRenderer), 0x104, x, z, uint32_t{0}, &picked)) {
            float box[6] = {1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f};
            float centre[3];
            Virtual<void>(picked, 0x20, &box[0]);
            This<void*>(addr::SC4Box_Center, &box[0], &centre[0]);
            void* const terrain = Global<void*>(addr::SC4_Terrain);
            uint32_t across;
            uint32_t along;
            std::memcpy(&across, &centre[0], sizeof(across));
            std::memcpy(&along, &centre[2], sizeof(along));
            Virtual<double>(terrain, 0x34, across, along);
            Virtual<double>(terrain, 0x78, across, along);
            void* const middle = This<void*>(addr::SC4Box_Center, &box[0], &centre[0]);
            This<void>(addr::cSC4CameraControl_SetCenter, At<void*>(self, view::kCamera), middle);
        } else if (Virtual<uint8_t>(At<void*>(self, view::kRenderer), 0xF0, x, z, &position[0], uint32_t{0},
                                    uint32_t{1})) {
            This<void>(addr::cSC4CameraControl_SetCenter, At<void*>(self, view::kCamera),
                       static_cast<void*>(&position[0]));
        }
        if (picked)
            Release(picked);
    }

    const auto call = [&](void* control) { return Virtual<uint8_t>(control, tool::kMouseUpR, x, z, modifiers); };
    if (Offer(self, view::kPersistent, true, call))
        return 1;
    Offer(self, view::kStack, true, call);
    return 1;
}

// The game's 0x007E8A90. The overlay over the whole view gets its picture:
// one for a running disaster, one for a mission, one for a paused game, none
// otherwise.
void __fastcall SetOverlayPicture(char* self, void*)
{
    void* overlay = nullptr;
    if (Virtual<uint8_t>(self + kWindow, 0x94, kOverlayWindow, kOverlayIid, &overlay)) {
        void* const city = Global<void*>(addr::SC4_City);
        void* const disasters = Virtual<void*>(city, 0x120);
        void* const situations = Virtual<void*>(city, 0xC4);
        void* const simulator = Global<void*>(addr::SC4_SimulatorService);
        uint32_t instance;
        if (disasters && Virtual<uint8_t>(disasters, 0x28)) {
            instance = 0x14315E60;
        } else if (situations && Virtual<uint32_t>(situations, 0x18) != 0) {
            instance = 0x14315E62;
        } else if (simulator && Virtual<uint8_t>(simulator, 0x3C)) {
            instance = 0x14315E61;
        } else {
            Virtual<void>(overlay, 0x10, static_cast<void*>(nullptr));
            if (overlay)
                Release(overlay);
            return;
        }
        ImageLoader loader;
        This<void*>(addr::SC4ImageLoader_ConstructParts, &loader, uint32_t{0}, kUiGroup, instance, uint32_t{1},
                    uint32_t{0});
        if (loader.image)
            Virtual<void>(overlay, 0x10, loader.image);
        This<void>(addr::SC4ImageLoader_Destruct, &loader);
    }
    if (overlay)
        Release(overlay);
}

// The game's 0x007E8B90. Shows a set of windows and what goes with it: the
// budget set (1), the MySim panel (2), set 4.
void __fastcall ShowSet(char* self, void*, int32_t set)
{
    char* const windows = self + view::kSets + static_cast<std::ptrdiff_t>(set) * 0xC;
    void** const end = At<void**>(windows, 4);
    for (void** at = At<void**>(windows, 0); at != end; ++at)
        Virtual<void>(*at, 0x110, uint32_t{1}, uint32_t{1});

    switch (static_cast<uint32_t>(set)) {
    case 1: {
        const uint32_t value = At<uint32_t>(self, view::kBudget370);
        At<uint8_t>(self, view::kMySimUp) = 0;
        At<uint8_t>(self, view::kFourUp) = 0;
        This<void>(addr::SC4UI_F7E6F60, self, value);
        At<uint8_t>(self, view::kBudgetUp) = 1;
        break;
    }
    case 2: {
        void* const advisors = Virtual<void*>(Global<void*>(addr::SC4_City), 0x144);
        Virtual<void>(advisors, 0x68);
        void* const panel = At<void*>(self, view::kMySimPanel);
        At<uint8_t>(self, view::kBudgetUp) = 0;
        At<uint8_t>(self, view::kFourUp) = 0;
        This<void>(addr::SC4UI_F76FCE0, panel);
        At<uint8_t>(self, view::kMySimUp) = 1;
        break;
    }
    case 4:
        At<uint8_t>(self, view::kFourUp) = 1;
        [[fallthrough]];
    case 0:
        if (void* const panel = At<void*>(self, view::kMySimPanel)) {
            This<void>(addr::SC4UI_F76E8E0, panel);
            At<uint8_t>(self, view::kMySimUp) = 0;
        }
        At<uint8_t>(self, view::kBudgetUp) = 0;
        break;
    default:
        break;
    }
    PutTickerAway(self);
}

// The game's 0x007E8CA0. Hides a set of windows (those without the flag
// 0x1000) and what goes with it. For the budget set the open panels are
// closed first; +0x374 is set when none was open.
void __fastcall HideSet(char* self, void*, int32_t set)
{
    Virtual<void>(self, 0xEC);
    char* const windows = self + view::kSets + static_cast<std::ptrdiff_t>(set) * 0xC;
    void** const end = At<void**>(windows, 4);
    for (void** at = At<void**>(windows, 0); at != end; ++at) {
        void* const window = *at;
        if (!Virtual<uint8_t>(window, 0x10C, uint32_t{0x1000}))
            Virtual<void>(window, 0x110, uint32_t{1}, uint32_t{0});
    }

    if (set == 1) {
        bool mark = true;
        bool skip = false;
        if (At<uint32_t>(self, view::kBudget374) == 0) {
            if (Virtual<uint8_t>(Virtual<void*>(Global<void*>(addr::SC4_City), 0x144), 0x6C)) {
                Virtual<void>(Virtual<void*>(Global<void*>(addr::SC4_City), 0x144), 0x68);
                mark = false;
            }
            if (void* const budget = At<void*>(self, view::kBudgetPanel)) {
                if (Virtual<uint8_t>(budget, 0x24)) {
                    Virtual<void>(At<void*>(self, view::kBudgetPanel), 0x1C);
                    mark = false;
                }
            }
            if (void* const map = At<void*>(self, view::kMapWindow)) {
                Virtual<void>(map, 0x118);
                mark = false;
            }
            if (void* const graphs = At<void*>(self, view::kGraphs)) {
                if (This<uint8_t>(addr::SC4UI_F76B6A0, graphs)) {
                    This<void>(addr::SC4UI_F76E470, At<void*>(self, view::kGraphs));
                    mark = false;
                }
            }
            if (void* const window = At<void*>(self, view::kWindow30C)) {
                Virtual<void>(window, 0x118);
                mark = false;
            }
            void* const style = At<void*>(self, view::kStylePanel);
            if (style && This<uint8_t>(addr::SC4UI_F7696C0, style)) {
                This<void>(addr::SC4UI_F7696B0, At<void*>(self, view::kStylePanel));
                skip = true;
            }
        }
        if (!skip && mark)
            At<uint32_t>(self, view::kBudget374) = 1;
        At<uint8_t>(self, view::kBudgetUp) = 0;
    } else if (set == 2) {
        This<void>(addr::SC4UI_F76E8E0, At<void*>(self, view::kMySimPanel));
        At<uint8_t>(self, view::kMySimUp) = 0;
    } else if (set == 4) {
        At<uint8_t>(self, view::kFourUp) = 0;
    }
    PutTickerAway(self);
}

// The game's 0x007E8E40. Shows the first cursor text in the info tip, or
// hides the tip when the list is empty.
void __fastcall UpdateCursorText(char* self, void*)
{
    char* const sentinel = At<char*>(self, view::kTexts);
    char* const first = At<char*>(sentinel, 0);
    if (first == sentinel) {
        Virtual<void>(At<char*>(self, view::kTip) + kTipWindow, 0x118);
        Virtual<void>(At<char*>(self, view::kTip) + kTipWindow, 0x170);
        return;
    }
    Virtual<void>(At<void*>(self, view::kTip), 0x18, static_cast<void*>(first + 0x10));
    Virtual<void>(At<void*>(self, view::kTip), 0x20, static_cast<void*>(first + 0x24));
    if (At<int32_t>(first, 0x38) == 1)
        Virtual<void>(At<void*>(self, view::kTip), 0x30, At<void*>(self, view::kTipPicture));
    else
        Virtual<void>(At<void*>(self, view::kTip), 0x30, static_cast<void*>(nullptr));
    if (!Virtual<uint8_t>(At<char*>(self, view::kTip) + kTipWindow, 0x11C)) {
        int32_t x;
        int32_t z;
        Virtual<void>(At<void*>(self, kWindow + 4), 0x88, static_cast<void*>(self + kWindow), &x, &z);
        int32_t place[2] = {x, z};
        Virtual<void>(At<void*>(self, view::kTip), 0x3C, &place[0]);
        Virtual<void>(At<char*>(self, view::kTip) + kTipWindow, 0x114);
    }
}

// The game's 0x007E8F30. Places the picture that follows the cursor: its
// right bottom corner 15 pixels up and left of the cursor. In state 2 the
// picture drifts and fades for the time left (0x007E7220 takes the fade)
// and is hidden when the time is up. The time passed comes in seconds.
void __fastcall MovePicture(char* self, void*, float seconds)
{
    void* const picture = At<void*>(self, view::kPicture);
    if (!picture)
        return;
    int32_t z = At<int32_t>(self, view::kCursorZ);
    int32_t x = At<int32_t>(self, view::kCursorX);
    if (At<int32_t>(self, view::kPictureState) == 2) {
        const double remaining =
            static_cast<double>(At<float>(self, view::kPictureTime)) - static_cast<double>(seconds) * 1000.0;
        At<float>(self, view::kPictureTime) = static_cast<float>(remaining);
        if (remaining <= 0.0) {
            At<int32_t>(self, view::kPictureState) = 0;
            Virtual<void>(Virtual<void*>(picture, 0xC), 0x118);
            return;
        }
        x = At<int32_t>(self, view::kDriftX);
        const double from = static_cast<double>(At<int32_t>(self, view::kDriftZ));
        const double gone = (static_cast<double>(1500.0f) - remaining) * static_cast<double>(0.13333334f);
        z = Truncate(from - gone);
        This<void>(addr::SC4UI_F7E7220, self, Truncate(remaining * static_cast<double>(0.17f)));
    }

    void* const image = At<void*>(self, view::kPictureImage);
    const int32_t height = Virtual<int32_t>(image, 0x28);
    const int32_t width = Virtual<int32_t>(image, 0x24);
    int32_t area[4];
    area[2] = x - 15;
    area[0] = x - 15 - width;
    area[1] = z - 15;
    area[3] = z - 15 + height;
    Virtual<void>(Virtual<void*>(At<void*>(self, view::kPicture), 0xC), 0xD8, &area[0]);
    const int32_t left = area[0];
    const int32_t top = area[1];
    const int32_t bottom = area[3];
    area[0] = left + -left;
    area[2] = area[2] + -left;
    area[1] = top + -top;
    area[3] = bottom + -top;
    Virtual<void>(At<void*>(self, view::kPicture), 0x20, &area[0]);
}

// The game's 0x007E9090. The cursor is at a new place: the tip, when it is
// shown, and the cursor's picture follow.
void __fastcall CursorMoved(char* self, void*, int32_t x, int32_t z)
{
    char* const tip = At<char*>(self, view::kTip);
    if (tip && Virtual<uint8_t>(tip + kTipWindow, 0x11C)) {
        int32_t place[2] = {x, z};
        Virtual<void>(At<void*>(self, view::kTip), 0x3C, &place[0]);
    }
    At<int32_t>(self, view::kCursorX) = x;
    At<int32_t>(self, view::kCursorZ) = z;
    MovePicture(self, nullptr, 0.0f);
}

// The game's 0x007E90F0. SetCursorTextPosition3D (slot 50): a place in the
// world, taken to the screen by the camera view. The original leaves no
// result of its own in EAX.
void __fastcall SetCursorTextPosition3D(char* self, void*, const float* world)
{
    float position[3];
    std::memcpy(&position[0], world, sizeof(position));
    void* const camera = Global<void*>(addr::SC4_CameraView);
    if (!camera)
        return;
    This<void>(kCameraProject, camera, &position[0]);
    const int32_t z = Truncate(static_cast<double>(position[1]));
    const int32_t x = Truncate(static_cast<double>(position[0]));
    CursorMoved(self, nullptr, x, z);
}

// The game's 0x007E9150. Asks with the window 0x2A41436C (a modal window)
// and on the answer 0x6A41BBB5 greys the bar's button 0x4988BC6A, hides set
// 4 when it is up and makes the tool of 0xA8 bytes (0x0049CF90) current.
void __fastcall AskAndStartTool(char* self, void*)
{
    if (Cdecl<uint32_t>(addr::SC4UI_F78E2F0, 0x2A41436Cu, 0x27DF05BEu) != 0x6A41BBB5)
        return;
    Cdecl<uint8_t>(addr::SC4Win_EnableChild, At<void*>(self, view::kBar), kBarButton, uint32_t{0});
    if (At<int32_t>(self, view::kSetUp) == 4)
        HideSet(self, nullptr, 4);
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0xA8});
    void* const control = memory ? This<void*>(addr::SC4ViewInputControls_F49CF90, memory) : nullptr;
    if (control)
        AddRef(control);
    Virtual<uint8_t>(self, 0x64, control, uint32_t{2});
    if (control)
        Release(control);
}

// The game's 0x007E91E0. MinimizeUI (slot 55): the control bar and the set
// that is up go away and the small bar comes, or the other way round.
// Nothing happens when the bar is already as asked.
void __fastcall MinimizeUI(char* self, void*, uint8_t minimize)
{
    void* const bar = At<void*>(self, view::kBar);
    if (minimize) {
        if (!Virtual<uint8_t>(bar, 0x11C))
            return;
        Virtual<void>(At<void*>(self, view::kBar), 0x118);
        if (At<int32_t>(self, view::kSetUp) != -1)
            HideSet(self, nullptr, At<int32_t>(self, view::kSetUp));
        Virtual<void>(At<void*>(self, view::kTicker), 0x18);
        Virtual<void>(self, 0xEC);
        if (void* const window = At<void*>(self, view::kWindow2EC))
            Virtual<void>(window, 0x118);
        Virtual<void>(At<void*>(self, view::kSmallBar), 0x114);
        Cdecl<uint8_t>(addr::SC4Win_ShowChild, static_cast<void*>(self + kWindow), kMinimizedChild, uint32_t{0},
                       uint32_t{0});
    } else {
        if (Virtual<uint8_t>(bar, 0x11C))
            return;
        Virtual<void>(At<void*>(self, view::kBar), 0x114);
        if (At<int32_t>(self, view::kSetUp) != -1)
            ShowSet(self, nullptr, At<int32_t>(self, view::kSetUp));
        if (void* const window = At<void*>(self, view::kWindow2EC))
            Virtual<void>(window, 0x114);
        if (At<uint8_t>(self, view::kMySimUp) || At<uint8_t>(self, view::kBudgetUp) ||
            At<uint8_t>(self, view::kFourUp))
            Virtual<void>(At<void*>(self, view::kTicker), 0x14);
        Virtual<void>(At<void*>(self, view::kSmallBar), 0x118);
        Cdecl<uint8_t>(addr::SC4Win_ShowChild, static_cast<void*>(self + kWindow), kMinimizedChild, uint32_t{1},
                       uint32_t{0});
    }
}

// The game's 0x007E9300. The owner's constructor.
void* __fastcall OwnerConstruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::SC4View3DWinOwner_Vtable;
    At<uint32_t>(self, owner::kCount) = 0;
    At<uint8_t>(self, owner::kStarted) = 0;
    At<uint8_t>(self, owner::kByte9) = 0;
    At<void*>(self, owner::kView) = nullptr;
    At<void*>(self, owner::kHeld10) = nullptr;
    At<void*>(self, owner::kHeld14) = nullptr;
    This<void*>(addr::cRZTimer_Construct, self + owner::kTimer);
    return self;
}

// A node of the game's lists of references.
struct RefNode {
    RefNode* next;
    RefNode* previous;
    void* value;
};

// The game's 0x007E9360 (C convention). The first node from `first` up to
// `last` whose object answers `id` at its slot 4; `last` when there is none.
RefNode** __cdecl FindById(RefNode** result, RefNode* first, RefNode* last, uint32_t id)
{
    RefNode* node = first;
    while (node != last) {
        if (Virtual<uint32_t>(node->value, 0x10) == id)
            break;
        node = node->next;
    }
    *result = node;
    return result;
}

// The game's 0x007E9390. RemoveCurrentViewInputControl (slot 26): the top
// of the stack goes; unless suppressed, the tool below gives the cursor and
// the change is posted, or the arrow cursor comes back when the stack is
// empty.
uint8_t __fastcall RemoveCurrentTool(char* self, void*, uint8_t suppress)
{
    void* const control = Virtual<void*>(self, 0x60);
    if (!control)
        return 0;
    AddRef(control);
    char* const part = self + kWindow;
    This<void>(addr::SC4Win_F99BE1E, part);
    At<void**>(self, view::kStackEnd) -= 1;
    Dismiss(control);
    if (!suppress) {
        if (void* const below = Virtual<void*>(self, 0x60)) {
            AddRef(below);
            Virtual<void>(part, 0x154, Virtual<void*>(below, tool::kGetCursor), uint32_t{1});
            PostToolChanged(self, nullptr, control, below);
            Release(below);
        } else {
            void* const cursors = This<void*>(addr::SC4Win_Cursors, part);
            if (void* const arrow = Virtual<void*>(cursors, 0x24, kArrowCursor))
                Virtual<void>(part, 0x154, arrow, uint32_t{1});
        }
    }
    Release(control);
    return 1;
}

// The game's 0x007E9460. The mouse moved (window slot 138). The tutorial
// may take the event; a move of more than 3 pixels from where the button
// went down counts as a drag; every persistent tool gets the event, then the
// stack until a tool takes it. Last the scrolling at the screen's edges
// (the option's byte at +0xF09 of 0x00B43CD8): on when the service
// 0x00B43C9C says so at its slot 12 or the current tool is capturing, unless
// that tool scrolls the view itself.
uint8_t __fastcall MouseMove(char* part, void*, int32_t x, int32_t z, uint32_t modifiers)
{
    char* const self = part - kWindow;
    At<uint32_t>(self, view::kModifiers) = modifiers;
    uint8_t scrolling = 1;
    if (void* const city = Global<void*>(addr::SC4_City)) {
        void* const tutorial = Virtual<void*>(city, 0x148);
        if (tutorial && Virtual<uint8_t>(tutorial, 0x20)) {
            if (!Virtual<uint8_t>(tutorial, 0x40, &x, &z, uint32_t{0}))
                return 1;
            scrolling = Virtual<uint8_t>(tutorial, 0x24);
        }
    }

    int32_t across = At<int32_t>(self, view::kDownX) - x;
    int32_t sign = across >> 31;
    across = (across ^ sign) - sign;
    bool dragged = across > 3;
    if (!dragged) {
        int32_t down = At<int32_t>(self, view::kDownZ) - z;
        sign = down >> 31;
        down = (down ^ sign) - sign;
        dragged = down > 3;
    }
    if (dragged)
        At<uint8_t>(self, view::kDragged) = 1;

    CursorMoved(self, nullptr, x, z);
    const auto call = [&](void* control) { return Virtual<uint8_t>(control, tool::kMouseMove, x, z, modifiers); };
    Offer(self, view::kPersistent, false, call);
    Offer(self, view::kStack, true, call);

    void* const current = Virtual<void*>(self, 0x60);
    if (scrolling && At<uint8_t>(Global<char*>(addr::SC4_Options43CD8), 0xF09)) {
        bool atEdges = false;
        if (!(current && Virtual<uint8_t>(current, tool::kIsSelfScrolling))) {
            if (Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C9C), 0x30))
                atEdges = true;
            else if (current && Virtual<uint8_t>(current, tool::kIsCapturing))
                atEdges = true;
        }
        if (atEdges) {
            if (x <= 1) {
                At<int32_t>(self, view::kEdgeScrollX) = -1;
            } else {
                const int32_t width = Virtual<int32_t>(part, 0xA4);
                At<int32_t>(self, view::kEdgeScrollX) = x >= width - 1 ? 1 : 0;
            }
            if (z <= 1) {
                At<int32_t>(self, view::kEdgeScrollZ) = -1;
            } else {
                const int32_t height = Virtual<int32_t>(part, 0xA8);
                At<int32_t>(self, view::kEdgeScrollZ) = z >= height - 1 ? 1 : 0;
            }
            This<uint32_t>(addr::SC4UI_F7E6C60, self);
            return 1;
        }
        if (Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C9C), 0x30))
            return 1;
        if (At<int32_t>(self, view::kEdgeScrollX) == 0 && At<int32_t>(self, view::kEdgeScrollZ) == 0)
            return 1;
    }
    At<int32_t>(self, view::kEdgeScrollZ) = 0;
    At<int32_t>(self, view::kEdgeScrollX) = 0;
    This<uint32_t>(addr::SC4UI_F7E6C60, self);
    return 1;
}

// The game's 0x007E96E0. The mouse left the window (slot 142): the edge
// scrolling stops, the cursor's things go far off the screen, the top tool
// and then every persistent tool is told.
uint8_t __fastcall MouseExit(char* part, void*, uint32_t)
{
    char* const self = part - kWindow;
    At<int32_t>(self, view::kEdgeScrollX) = 0;
    At<int32_t>(self, view::kEdgeScrollZ) = 0;
    This<uint32_t>(addr::SC4UI_F7E6C60, self);
    CursorMoved(self, nullptr, -10000, -10000);
    if (At<void**>(self, view::kStack) != At<void**>(self, view::kStackEnd))
        Virtual<uint8_t>(At<void**>(self, view::kStackEnd)[-1], tool::kMouseExit);
    Offer(self, view::kPersistent, false, [](void* control) { return Virtual<uint8_t>(control, tool::kMouseExit); });
    return 1;
}

// The game's 0x007E9780. Every tool leaves the view: the list at +0x14C is
// emptied, then RemoveAllViewInputControls(true) and
// RemoveAllPersistentViewInputControls, whose result comes back.
uint32_t __fastcall RemoveEveryTool(char* self, void*)
{
    This<void>(addr::SC4RefList_Clear, self + view::kReferences);
    Virtual<uint8_t>(self, 0x6C, uint32_t{1});
    return Virtual<uint32_t>(self, 0x78);
}

// The game's 0x007E97A0. The bar's button 0x4988BC6A is greyed, set 4 is
// hidden when it is up and the bar's button 0x2988BC85 is let up.
void __fastcall LeaveSetFour(char* self, void*)
{
    Cdecl<uint8_t>(addr::SC4Win_EnableChild, At<void*>(self, view::kBar), kBarButton, uint32_t{0});
    if (At<int32_t>(self, view::kSetUp) == 4)
        HideSet(self, nullptr, 4);
    Cdecl<uint8_t>(addr::SC4Win_CheckChild, At<void*>(self, view::kBar), kBarCheck, uint32_t{0});
}

// The game's 0x007E97F0. The owner's Release (slot 2); the last one shuts
// the owner down and frees it.
uint32_t __fastcall OwnerRelease(char* self, void*)
{
    const int32_t count = At<int32_t>(self, owner::kCount);
    if (count > 1) {
        At<int32_t>(self, owner::kCount) = count - 1;
        return static_cast<uint32_t>(count - 1);
    }
    At<std::uintptr_t>(self, 0) = addr::SC4View3DWinOwner_Vtable;
    OwnerShutdown(self, nullptr);
    This<void>(addr::cRZTimer_Destruct, self + owner::kTimer);
    if (void* const held = At<void*>(self, owner::kHeld14))
        Release(held);
    if (void* const held = At<void*>(self, owner::kHeld10))
        Release(held);
    if (void* const held = At<void*>(self, owner::kView))
        Release(held);
    Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return 0;
}

// The cheat codes the view registers: the index of the name in the table of
// 13 pointers at 0x00B0DD64 and the code's ID. The names, in the table's
// order: GOL, TastyZots, Zoneria, SizeOf, WatchMeMove, RenderProp, RP,
// TerrainQuery, Recorder, "You don't deserve it", LotPlop, BuildingPlop,
// toll.
struct Cheat {
    std::size_t name;
    uint32_t id;
};

constexpr Cheat kCheats[] = {
    {0, 0x8973A69C}, {3, 0xCA7BC5B5}, {1, 0xAA7A2746}, {2, 0x6A7A276E}, {7, 0x8AB730AE},
    {5, 0x88F3D4BB}, {6, 0x88F3D4BC}, {8, 0x6976AFA0}, {4, 0x4A972F37}, {9, 0x4A82253E},
};

constexpr Cheat kMoreCheats[] = {
    {10, 0x4AC096C6},
    {11, 0x4AC096CA},
    {12, 0x0BFD8064},
};

void RegisterCheat(void* cheats, const Cheat& cheat)
{
    GameString name;
    void* const made = This<void*>(addr::SC4String_FromText, &name,
                                   Global<const char*>(addr::SC4View3DWin_CheatNames + 4 * cheat.name));
    Virtual<uint8_t>(cheats, 0xC, cheat.id, made);
    DestroyString(name);
}

// The game's 0x007E9900. The window's Init (slot 4): the notifications for
// the view's message IDs, the 104 commands of the table at 0x00B0DD98 given
// to the service 0xEB903A32, the cheat codes (three more when the service
// 0x00B43C94 says so at its slot 21), the window's name, ID and size (the
// main window's), the arrow cursor, an object of class 0x42E967BE told to
// the resource 0xA2E3D533 / 0x6A231EAA / 0x6A9362EF, and the loading
// picture (0x007E5BB0). The service is not checked for null, as in the
// original.
uint8_t __fastcall WindowInit(char* part, void*)
{
    char* const self = part - kWindow;
    if (This<uint8_t>(addr::GZWin_IsInitialized, part))
        return At<uint8_t>(self, view::kInitResult);

    This<void>(addr::RZMessageIds_AddNotifications, self + view::kMessageIds,
               static_cast<void*>(self + kMessageTarget), static_cast<void*>(nullptr));
    void* commands = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, 0xEB903A32u, 0xCB90253Au, &commands);
    Virtual<void>(commands, 0x1C, reinterpret_cast<void*>(addr::cSC4View3DWin_Commands), uint32_t{0x68});

    if (void* const cheats = Global<void*>(addr::SC4_CheatCodes)) {
        Virtual<void>(cheats, 0x14, static_cast<void*>(part), uint32_t{0});
        for (const Cheat& cheat : kCheats)
            RegisterCheat(cheats, cheat);
        if (Virtual<uint8_t>(Global<void*>(addr::SC4_Service43C94), 0x54)) {
            for (const Cheat& cheat : kMoreCheats)
                RegisterCheat(cheats, cheat);
        }
    }

    At<uint8_t>(self, view::kInitResult) = This<uint8_t>(addr::GZWin_Init, part);
    {
        GameString name;
        void* const made = This<void*>(addr::SC4String_FromText, &name, "cSC4View3DWin");
        Virtual<void>(part, 0x128, made);
        DestroyString(name);
    }
    Virtual<void>(part, 0x100, kViewId);
    void* const mainForHeight = Virtual<void*>(part, 0x28);
    void* const mainForWidth = Virtual<void*>(part, 0x28);
    const int32_t height = Virtual<int32_t>(mainForHeight, 0xA8);
    const int32_t width = Virtual<int32_t>(mainForWidth, 0xA4);
    Virtual<void>(part, 0xDC, int32_t{0}, int32_t{0}, width, height);
    void* const cursors = This<void*>(addr::SC4Win_Cursors, part);
    Virtual<void>(part, 0x154, Virtual<void*>(cursors, 0x24, kArrowCursor), uint32_t{0});
    This<void>(addr::SC4_F7D26E0, self + view::kMap3E4);
    At<uint8_t>(self, view::kByte15C) = 0;
    This<void>(addr::SC4Stopwatch_Start, self + view::kWatch);

    uint32_t key[3] = {0xA2E3D533, 0x6A231EAA, 0x6A9362EF};
    void* resource = nullptr;
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, &key[0], 0x42E3EA4Bu, &resource, uint32_t{0},
                         uint32_t{0})) {
        void* const com = Cdecl<void*>(addr::RZGetCOM);
        void** const object = &At<void*>(self, view::kObject120);
        if (Virtual<uint8_t>(com, 0xC, 0x42E967BEu, 0xE2C1B3C4u, object))
            Virtual<void>(resource, 0xC, *object);
    }
    At<uint8_t>(self, view::kByte125) = 1;

    // The game pushes a dword whose low byte is the flag.
    uint32_t flag = 0;
    if (At<void*>(Virtual<char*>(Global<void*>(addr::SC4_Service43C94), 0x9C), 0x204))
        flag = 1;
    This<void>(addr::SC4UI_F7E5BB0, self, flag);
    if (resource)
        Release(resource);
    This<void>(addr::SC4RefPtr_Destruct562DD0, &commands);
    return At<uint8_t>(self, view::kInitResult);
}

// The game's 0x007E9DB0. Makes a disaster's tool current. 0xA9DE8784 has a
// tool of its own (0x80 bytes, 0x005805C0); any other ID is an exemplar of
// group 0x29B6C670 whose property 0x4A08C700 (a name) or 0x4A08C702 (a
// number) sets up the common tool (0xB4 bytes, 0x00580220), with the
// property 0x4A08C701 as an extra when it is not 0. The first tool is not
// checked for null, as in the original.
void __fastcall StartDisaster(char* self, void*, uint32_t id)
{
    if (id == 0xA9DE8784) {
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x80});
        void* const control = memory ? This<void*>(addr::SC4ViewInputControls_F5805C0, memory) : nullptr;
        AddRef(control);
        Virtual<uint8_t>(self, 0x64, control, uint32_t{2});
        Release(control);
        return;
    }

    uint32_t key[3] = {0x6534284A, 0x29B6C670, id};
    void* exemplar = nullptr;
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, &key[0], 0xA52160F5u, &exemplar, uint32_t{0},
                         uint32_t{0})) {
        void* const holder = Virtual<void*>(exemplar, 0x1C);
        uint32_t extra = 0;
        GameString name;
        name.body.begin = nullptr;
        name.body.end = nullptr;
        name.body.capacity = nullptr;
        char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
        name.body.begin = buffer;
        name.body.end = buffer;
        name.body.capacity = buffer + 8;
        *buffer = 0;
        name.vtable = addr::SC4String_Vtable;
        name.zero = 0;
        Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, 0x4A08C701u, &extra);

        void* control = nullptr;
        bool made = false;
        if (Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, 0x4A08C700u, &name)) {
            void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0xB4});
            control = memory ? This<void*>(addr::SC4ViewInputControls_F580220, memory, exemplar) : nullptr;
            if (control)
                AddRef(control);
            Virtual<uint8_t>(self, 0x64, control, uint32_t{2});
            This<void>(addr::SC4ViewInputControls_F4BB590, control, name.body.begin);
            made = true;
        } else {
            uint32_t number;
            if (Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, 0x4A08C702u, &number)) {
                void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0xB4});
                control = memory ? This<void*>(addr::SC4ViewInputControls_F580220, memory, exemplar) : nullptr;
                if (control)
                    AddRef(control);
                Virtual<uint8_t>(self, 0x64, control, uint32_t{2});
                This<void>(addr::SC4ViewInputControls_F4BB160, control, number);
                made = true;
            }
        }
        if (made) {
            if (extra != 0)
                This<void>(addr::SC4ViewInputControls_F580460, control, extra);
            if (control)
                Release(control);
        }
        DestroyString(name);
    }
    if (exemplar)
        Release(exemplar);
}

// The kind (0 to 0xC) of the 0x58-byte tool a command value stands for, or
// -1.
int32_t KindFor(uint32_t value)
{
    switch (value) {
    case 0x8C329937: return 0x0;
    case 0x00000029: return 0x1;
    case 0x00000031: return 0x2;
    case 0xEA6E379D: return 0x3;
    case 0x2992F72C: return 0x4;
    case 0x00000032: return 0x5;
    case 0x2B730C5B: return 0x6;
    case 0x299237BF: return 0x7;
    case 0x2B79E398: return 0x8;
    case 0x4BE099DE: return 0x9;
    case 0x0BE099C1: return 0xA;
    case 0x2BE099A9: return 0xB;
    case 0x0BE0998F: return 0xC;
    default: return -1;
    }
}

// The game's 0x007E9FA0. Starts what a command of the menus asks for.
// `close` shuts the open menus first (CloseTertiaryMenu, or
// CloseSecondaryMenu for 0x6A5AC478).
//   0x4A2B2462, 0x8A313C52: 0x007E5910 with the value and 1 or 0;
//   0x4A2B2464: a disaster's tool (0x007E9DB0);
//   0x4A2B2466: 0x007E6DA0 with a value below 16, in an established city;
//   0x4A2B2467: the tool of 0x58 bytes with one of 13 kinds (0x006620A0)
//     or, for 0x6A47xxxx, the tool of 0x90 bytes with the value's low word
//     (0x00661130), in an established city;
//   0x6A2B245F: the tool that places a lot (0x158 bytes, 0x004C0430), which
//     is told where the mouse is when the view is under it;
//   0x6A5AC478: 0x007E75B0 with the value;
//   0xEA5AD08D: the tool of 0x50 bytes (0x004BB900);
//   0x0BB2E43E: the value is looked up in the records of three services
//     (0x00B43D44, 0x00B43D4C, 0x00B43D50); the number found goes to the
//     city's situation manager (slot 23) with 1 and 1.
void __fastcall StartCommand(char* self, void*, uint32_t command, uint32_t value, uint8_t close)
{
    switch (command) {
    case 0x4A2B2462:
        if (close)
            Virtual<void>(self, 0xF0);
        This<void>(addr::SC4UI_F7E5910, self, value, uint32_t{1});
        return;

    case 0x8A313C52:
        if (close)
            Virtual<void>(self, 0xF0);
        This<void>(addr::SC4UI_F7E5910, self, value, uint32_t{0});
        return;

    case 0x4A2B2464:
        if (close)
            Virtual<void>(self, 0xF0);
        StartDisaster(self, nullptr, value);
        return;

    case 0x4A2B2466:
        if (close)
            Virtual<void>(self, 0xF0);
        if (!Virtual<uint8_t>(Global<void*>(addr::SC4_City), 0x58))
            return;
        if (value >= 0x10)
            return;
        This<void>(addr::SC4UI_F7E6DA0, self, value);
        return;

    case 0x4A2B2467: {
        if (close)
            Virtual<void>(self, 0xF0);
        if (!Virtual<uint8_t>(Global<void*>(addr::SC4_City), 0x58))
            return;
        void* holder = nullptr;
        void* made = nullptr;
        const int32_t kind = KindFor(value);
        if (kind >= 0) {
            if (void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x58}))
                made = This<void*>(addr::SC4ViewInputControls_F6620A0, memory, kind);
        } else if (value >= 0x6A470000 && value <= 0x6A47FFFF) {
            if (void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x90}))
                made = This<void*>(addr::SC4ViewInputControls_F661130, memory, value & 0xFFFF);
        } else {
            return;
        }
        This<void*>(addr::SC4RefPtr_Assign, &holder, made);
        void* const control = holder;
        if (!control)
            return;
        Virtual<uint8_t>(self, 0x64, control, uint32_t{2});
        Release(control);
        return;
    }

    case 0x6A2B245F: {
        if (close)
            Virtual<void>(self, 0xF0);
        if (!Virtual<uint8_t>(Global<void*>(addr::SC4_City), 0x58))
            return;
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x158});
        void* made = nullptr;
        if (memory) {
            made = This<void*>(addr::SC4ViewInputControls_F4C0430, memory, uint32_t{0}, value, uint32_t{0},
                               uint32_t{0});
        }
        void* control;
        This<void*>(addr::SC4RefHolder_SC4RefHolder, &control, made);
        Virtual<uint8_t>(self, 0x64, control, uint32_t{2});
        int32_t x;
        int32_t z;
        Virtual<void>(Cdecl<void*>(addr::SC4_GetWinManager), 0x84, &x, &z);
        char* const part = self + kWindow;
        void* const top = Virtual<void*>(part, 0x2C);
        if (Cdecl<void*>(addr::SC4UI_F78D720, top, x, z) == static_cast<void*>(part))
            Virtual<uint8_t>(control, tool::kMouseMove, x, z, uint32_t{0});
        if (control)
            Release(control);
        return;
    }

    case 0x6A5AC478:
        if (close)
            Virtual<void>(self, 0xEC);
        This<void>(addr::SC4UI_F7E75B0, self, value);
        return;

    case 0xEA5AD08D: {
        if (close)
            Virtual<void>(self, 0xF0);
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x50});
        void* const control = memory ? This<void*>(addr::SC4ViewInputControls_F4BB900, memory, value) : nullptr;
        if (control)
            AddRef(control);
        Virtual<uint8_t>(self, 0x64, control, uint32_t{2});
        if (control)
            Release(control);
        return;
    }

    case 0x0BB2E43E: {
        uint32_t found = 0;
        void* services[3];
        This<void*>(addr::SC4RefHolder_SC4RefHolder, &services[0], Global<void*>(addr::SC4_Service43D44));
        This<void*>(addr::SC4RefHolder_SC4RefHolder, &services[1], Global<void*>(addr::SC4_Service43D4C));
        This<void*>(addr::SC4RefHolder_SC4RefHolder, &services[2], Global<void*>(addr::SC4_Service43D50));
        for (uint32_t index = 0; index < 3; ++index) {
            void* const service = services[index];
            if (!service)
                continue;
            char* const records = Virtual<char*>(service, 0x70);
            char* record = At<char*>(records, 0);
            if (record == At<char*>(records, 4))
                continue;
            for (;;) {
                void* const object = At<void*>(record, 0x30);
                uint32_t triple[3] = {0, 0, 0};
                if (object) {
                    Virtual<void>(object, 0x2C, &triple[0]);
                    if (triple[2] == value) {
                        found = At<uint32_t>(record, 0x68);
                        break;
                    }
                }
                record += 0x6C;
                if (record == At<char*>(records, 4))
                    break;
            }
        }
        if (found != 0) {
            Virtual<void>(self, 0xF0);
            if (void* const situations = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC4))
                Virtual<void>(situations, 0x5C, found, uint32_t{1}, uint32_t{1});
        }
        reinterpret_cast<void(__stdcall*)(void*, uint32_t, int32_t, void*)>(addr::cSC4Advisors_F41D3C0)(
            &services[0], 4, 3, reinterpret_cast<void*>(addr::SC4RefHolder_Destruct));
        return;
    }

    default:
        return;
    }
}

bool InstallCityView2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[32] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F7E7EA0, reinterpret_cast<void*>(&OwnerShutdown)},
        {addr::SC4UI_F7E7F90, reinterpret_cast<void*>(&KillKeyboardScrolling)},
        {addr::SC4UI_F7E7FB0, reinterpret_cast<void*>(&RemovePersistentTool)},
        {addr::SC4UI_F7E8030, reinterpret_cast<void*>(&RemoveAllPersistentTools)},
        {addr::SC4UI_F7E8090, reinterpret_cast<void*>(&PostToolChanged)},
        {addr::SC4UI_F7E8140, reinterpret_cast<void*>(&ToolMessageDelete)},
        {addr::SC4UI_F7E8170, reinterpret_cast<void*>(&MouseUpL)},
        {addr::SC4UI_F7E82E0, reinterpret_cast<void*>(&MouseUpR)},
        {addr::SC4UI_F7E8A90, reinterpret_cast<void*>(&SetOverlayPicture)},
        {addr::SC4UI_F7E8B90, reinterpret_cast<void*>(&ShowSet)},
        {addr::SC4UI_F7E8CA0, reinterpret_cast<void*>(&HideSet)},
        {addr::SC4UI_F7E8E40, reinterpret_cast<void*>(&UpdateCursorText)},
        {addr::SC4UI_F7E8F30, reinterpret_cast<void*>(&MovePicture)},
        {addr::SC4UI_F7E9090, reinterpret_cast<void*>(&CursorMoved)},
        {addr::SC4UI_F7E90F0, reinterpret_cast<void*>(&SetCursorTextPosition3D)},
        {addr::SC4UI_F7E9150, reinterpret_cast<void*>(&AskAndStartTool)},
        {addr::SC4UI_F7E91E0, reinterpret_cast<void*>(&MinimizeUI)},
        {addr::SC4UI_F7E9300, reinterpret_cast<void*>(&OwnerConstruct)},
        {addr::SC4UI_F7E9360, reinterpret_cast<void*>(&FindById)},
        {addr::SC4UI_F7E9390, reinterpret_cast<void*>(&RemoveCurrentTool)},
        {addr::SC4UI_F7E9460, reinterpret_cast<void*>(&MouseMove)},
        {addr::SC4UI_F7E96E0, reinterpret_cast<void*>(&MouseExit)},
        {addr::SC4UI_F7E9780, reinterpret_cast<void*>(&RemoveEveryTool)},
        {addr::SC4UI_F7E97A0, reinterpret_cast<void*>(&LeaveSetFour)},
        {addr::SC4UI_F7E97F0, reinterpret_cast<void*>(&OwnerRelease)},
        {addr::SC4UI_F7E9900, reinterpret_cast<void*>(&WindowInit)},
        {addr::SC4UI_F7E9DB0, reinterpret_cast<void*>(&StartDisaster)},
        {addr::SC4UI_F7E9FA0, reinterpret_cast<void*>(&StartCommand)},
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

const bool g_cityView2Registered = registry::Register({
    "ui.cityview.part2",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F7E7EA0,
    "Our city view tools, mouse handlers, window sets, cursor text, window start and owner (docs/ui.md)",
    false,
    InstallCityView2,
});

} // namespace
} // namespace opensc4::ui
