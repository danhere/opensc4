// The U-Drive-It driving control, third piece (translator B of T8): the
// keys, the left press, the mouse move, slot 27 and the driving helpers
// (0x00562C90 to 0x00566D55). Replacement, off by default:
//
//   ui.tools.udrive3: 14 functions of cSC4UDriveControl (0xC0 bytes, main
//     vtable 0x00A9D9B0; docs/tools.md): OnKeyDown (slot 14), OnKeyUp (15),
//     OnMouseDownL (16), OnMouseMove (20), PutAway (27), the key binding
//     0x00566A70 and the two command switches 0x00563D40 / 0x00564060, the
//     two picks 0x005630C0 / 0x00563140, the listeners' message 0x005644E0,
//     the driving windows 0x00564570, the start of a drive 0x00565160 and the
//     simulator pause 0x00562F00.
//
// The driven object (+0x30) is a cISC4Automaton: the keys go through the
// city's automata script system (+0xB0, its slot 0x38 turns a key into a
// command 0 to 12, 13 none), a command first through the automaton's key
// binding records (0x00566A70: the state 0x10 of the automaton keeps a byte
// per command at +0x205; a record can start or stop named effects and
// automata controllers), and when the binding does not take it, through
// the command switch: HandlePlayerDriveCommand (slot 0xF0) with the
// command and down/up, the service of 0x00602640 told some of them (slot
// 0x14 down, 0x18 up), two buttons of the driving window pressed and
// released. Escape puts the control away (slot 27): the listeners are told
// 0x0BB324BD, a disabled automaton is removed, 0x00562FD0, the hovered
// automaton let go, disasters back on, then the base's PutAway. A left
// press on a network piece the network manager takes (slot 0xA8) plays a
// sound there and marks its cell in the point holder (+0x9C); with nothing
// driven a press on an automaton starts driving it (0x00565160: disasters
// off, the UI minimised, the windows made, the automaton pushed into state
// 0x10 and followed by the vehicle manager, the zoom limits from its
// description, the stopwatch +0x48 stopped and reset, the simulator and the
// four automata managers resumed). The mouse move puts the pointer's cell
// into the point holder and, while driving, the screen point into the
// state's +0x214/+0x218; without driving it keeps the automaton under the
// pointer at +0x60.
//
// No random number is drawn and no clock is read; 0x00565160 stops and
// resets the stopwatch at +0x48. The picks (the renderer's slot 0x104, the
// view's slot 0x90) depend on the camera and the frame; the mouse handlers
// take real input. Calls to functions of ui.tools.udrive1 (CanDrive) and of
// ui.tools.udrive2 (0x00562FD0) go through the game's entries. Nothing here
// draws.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tool_udrive_3 {
namespace {

// The base class's fields.
namespace base {
constexpr std::size_t kView = 0x1C; // the city view
} // namespace base

// The control's fields (T7a's names where tool_udrive_1.cpp has them).
namespace field {
constexpr std::size_t kTarget = 0x28;           // the message target part (vtable 0x00A9D9A0)
constexpr std::size_t kThird = 0x2C;            // the third interface (vtable 0x00A9D98C)
constexpr std::size_t kDriven = 0x30;           // the driven automaton (referenced)
constexpr std::size_t kRef34 = 0x34;            // the overlay manager (the dispatch service's slot 0x58)
constexpr std::size_t kWord38 = 0x38;           // the overlay on the driven automaton
constexpr std::size_t kFloat40 = 0x40;          // 0.042f or the description's third value
constexpr std::size_t kByte44 = 0x44;           // command 4's toggle (1 at a start)
constexpr std::size_t kByte45 = 0x45;           // command 10's toggle (0 at a start)
constexpr std::size_t kStopwatch = 0x48;        // 0x18 bytes
constexpr std::size_t kRef60 = 0x60;            // the automaton under the pointer (referenced)
constexpr std::size_t kList = 0x64;             // the listeners: a list head, nodes {next, prev, target}
constexpr std::size_t kAutomatonFilter = 0x84;  // occupant filter of the automaton pick (vtable 0x00A9D894)
constexpr std::size_t kNetworkFilter = 0x88;    // occupant filter of the network pick (vtable 0x00A8FE14)
constexpr std::size_t kVehicleManager = 0x8C;   // the city's four automata managers (slots 43 to 46)
constexpr std::size_t kPedestrianManager = 0x90;
constexpr std::size_t kAircraftManager = 0x94;
constexpr std::size_t kWatercraftManager = 0x98;
constexpr std::size_t kPointHolder = 0x9C;      // +0x1C/+0x20 the pointer's cell, +0x24/+0x28 the picked piece's cell
constexpr std::size_t kSimulator = 0xA0;        // the simulator service (0x00B43D5C)
constexpr std::size_t kWindow = 0xA4;           // the driving window (referenced)
constexpr std::size_t kWindow2 = 0xA8;          // the second window (referenced)
constexpr std::size_t kNetworkManager = 0xAC;   // the network manager (0x00B43D54)
constexpr std::size_t kScriptSystem = 0xB0;     // the city's automata script system (slot 48)
} // namespace field

// The point holder's fields.
namespace holder {
constexpr std::size_t kHoverX = 0x1C;
constexpr std::size_t kHoverZ = 0x20;
constexpr std::size_t kPickX = 0x24;
constexpr std::size_t kPickZ = 0x28;
} // namespace holder

// The driving state (the automaton's state 0x10, FindState).
namespace state {
constexpr std::size_t kKeys = 0x205;   // a byte per command
constexpr std::size_t kMouseX = 0x214; // the screen point of the last mouse move
constexpr std::size_t kMouseZ = 0x218;
constexpr std::size_t kByte229 = 0x229; // command 9's toggle
} // namespace state

// A key binding record (0x40 bytes, made by 0x0051D040): three strings of
// 0x14 bytes (the first the named effects, the third the automata
// controller's template) and the flags at +0x3C.
struct Binding {
    alignas(4) uint8_t bytes[0x40];
};
constexpr std::size_t kBindingTemplate = 0x28;
constexpr std::size_t kBindingFlags = 0x3C;
constexpr uint32_t kBindingOff = 0x02;     // the command off, as a toggle
constexpr uint32_t kBindingFollow = 0x01;  // the command follows the key
constexpr uint32_t kBindingType = 0x04;    // the effects' transition type (bit 2)
constexpr uint32_t kBindingTaken = 0x08;   // the binding takes the command
constexpr uint32_t kBindingEffects = 0x10; // named effects
constexpr uint32_t kBindingController = 0x20; // an automata controller

// The image holder of 0x00602B70 (0x30 bytes, the image at +4).
struct ImageHolder {
    alignas(4) uint32_t words[0xC];
};

constexpr uint32_t kFlagStopped = 0x8000000; // the automaton's flags asked through slot 0x7C
constexpr uint32_t kFlagOne = 1;
constexpr uint32_t kStateDriving = 0x10;
constexpr uint32_t kCommandNone = 0xD;
constexpr uint32_t kKeyEscape = 0x1B;
constexpr uint32_t kKeyRepeat = 0x40000000;
constexpr uint32_t kCursorText = 0xAB73510C;
constexpr uint32_t kIidNetworkOccupant = 0xA821EF94;
constexpr uint32_t kIidAutomaton = 0xA9B40F05;
constexpr uint32_t kIidExemplar = 0xA52160F5;
constexpr uint32_t kIidWindowInterface = 0x22BA0121;
constexpr uint32_t kIidListener = 0x5386D516;
constexpr uint32_t kMessageListeners = 0x0BB324BD;
constexpr uint32_t kSoundNetwork = 0x8BE8D9AC;
constexpr uint32_t kGroupDrivable = 0x4006;
constexpr uint32_t kFloat0042 = 0x3D2C0831;

// What commands 0, 1, 5 and 7 tell the service of 0x00602640 (slot 0x14 on
// a press, 0x18 on a release); 2 and 3 tell it 3 in their own cases.
constexpr uint32_t kServiceCodes[8] = {1, 2, 0, 0, 0, 6, 0, 4};

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

void AddRef(void* object)
{
    Virtual<uint32_t>(object, 0x4);
}

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

void* Driven(char* self)
{
    return At<void*>(self, field::kDriven);
}

void* City()
{
    return Global<void*>(addr::SC4_City);
}

void* DriveService()
{
    return Cdecl<void*>(addr::SC4UDrive_GetService602640);
}

// The city situation manager's countdown (city slot 0xC4, its slot 0x80).
bool CountdownRunning()
{
    void* const situations = Virtual<void*>(City(), 0xC4);
    return Virtual<uint8_t>(situations, 0x80) != 0;
}

// A driven automaton whose flags 0x8000000 and 1 are clear.
bool DrivenTakesInput(char* self)
{
    void* const driven = Driven(self);
    if (!driven)
        return false;
    if (Virtual<uint8_t>(driven, 0x7C, kFlagStopped))
        return false;
    return !Virtual<uint8_t>(Driven(self), 0x7C, kFlagOne);
}

bool CanDrive(char* self)
{
    return This<uint8_t>(addr::cSC4UDriveControl_CanDrive, self) != 0;
}

// HandlePlayerDriveCommand (slot 0xF0).
void Command(char* self, uint32_t command, uint32_t down)
{
    Virtual<uint8_t>(Driven(self), 0xF0, command, down);
}

// The view's ClearCursorText (slot 41) after letting go of the automaton
// under the pointer, when there was one.
void DropHovered(char* self)
{
    if (void* const hovered = At<void*>(self, field::kRef60)) {
        At<void*>(self, field::kRef60) = nullptr;
        Release(hovered);
        Virtual<uint8_t>(At<void*>(self, base::kView), 0xA4, kCursorText);
    }
}

// The child `id` of the driving window (slot 0x90 with 0x8810) gets its
// slot 0x24 with `on`.
void PressButton(char* self, uint32_t id, uint32_t on)
{
    void* const window = At<void*>(self, field::kWindow);
    if (!window)
        return;
    void* button = nullptr;
    void** const out = This<void**>(addr::SC4RefPtr_Reset, &button);
    if (Virtual<uint8_t>(window, 0x90, id, uint32_t{0x8810}, out))
        Virtual<void>(button, 0x24, on);
    if (button)
        Release(button);
}

// Centres a window over the view's window at its foot: the window's slot
// 0xE0 with ((view width - width) / 2, view height - height), sizes by the
// window slots 0xA4 (width) and 0xA8 (height). The game asks the view's
// window twice (slot 3) and, for the second window, each size of the window
// before the view's.
void Centre(char* self, void* window, bool windowFirst)
{
    void* const view = At<void*>(self, base::kView);
    void* const viewWindow = Virtual<void*>(view, 0xC);
    void* const viewWindow2 = Virtual<void*>(view, 0xC);
    int32_t down;
    int32_t across;
    if (windowFirst) {
        const int32_t height = Virtual<int32_t>(window, 0xA8);
        down = Virtual<int32_t>(viewWindow, 0xA8) - height;
        const int32_t width = Virtual<int32_t>(window, 0xA4);
        across = (Virtual<int32_t>(viewWindow2, 0xA4) - width) / 2;
    } else {
        const int32_t viewHeight = Virtual<int32_t>(viewWindow, 0xA8);
        down = viewHeight - Virtual<int32_t>(window, 0xA8);
        const int32_t viewWidth = Virtual<int32_t>(viewWindow2, 0xA4);
        across = (viewWidth - Virtual<int32_t>(window, 0xA4)) / 2;
    }
    Virtual<void>(window, 0xE0, across, down);
}

// A window's listener interface (0x5386D516) told about the control's third
// interface (slot 0x1C); the interface let go after.
void Listen(char* self, void* window, void** listener)
{
    if (Virtual<uint8_t>(window, 0x0, kIidListener, listener))
        Virtual<void>(*listener, 0x1C, self + field::kThird);
}

// The game's 0x005630C0 (ret 0xC) and 0x00563140 (ret 0xC): the occupant at
// a screen point through the renderer's pick (slot 0x104) with the filter at
// `filter`, the picked model's owner (slot 3) asked for `iid` into `out`
// (referenced); the model let go. False and null when there is none.
uint8_t Pick(char* self, int32_t x, int32_t z, void** out, std::size_t filter, uint32_t iid)
{
    void* const renderer = Global<void*>(addr::SC4_View43DD0);
    uint8_t found = 0;
    *out = nullptr;
    void* model = nullptr;
    if (Virtual<uint8_t>(renderer, 0x104, x, z, At<void*>(self, filter), &model)) {
        void* picked = nullptr;
        if (void* const owner = Virtual<void*>(model, 0xC)) {
            if (Virtual<uint8_t>(owner, 0x0, iid, &picked)) {
                *out = picked;
                found = 1;
            }
        }
        Release(model);
    }
    return found;
}

// The game's 0x005630C0: the network occupant at a screen point (filter
// +0x88).
uint8_t __fastcall PickNetworkOccupant(char* self, void*, int32_t x, int32_t z, void** out)
{
    return Pick(self, x, z, out, field::kNetworkFilter, kIidNetworkOccupant);
}

// The game's 0x00563140: the automaton at a screen point (filter +0x84).
uint8_t __fastcall PickAutomaton(char* self, void*, int32_t x, int32_t z, void** out)
{
    return Pick(self, x, z, out, field::kAutomatonFilter, kIidAutomaton);
}

// The game's 0x00562F00 (ret 4): with `pause` the simulator service (+0xA0)
// paused when it is not (slots 15, 9) and the four automata managers paused
// (slot 9); without, the simulator resumed when it is paused (slot 12) and
// the managers resumed (slot 10). Missing ones are skipped.
void __fastcall Pause(char* self, void*, uint8_t pause)
{
    void* const simulator = At<void*>(self, field::kSimulator);
    const std::size_t managers[] = {field::kVehicleManager, field::kPedestrianManager, field::kAircraftManager,
                                    field::kWatercraftManager};
    if (pause) {
        if (simulator && !Virtual<uint8_t>(simulator, 0x3C))
            Virtual<uint8_t>(At<void*>(self, field::kSimulator), 0x24);
        for (const std::size_t at : managers) {
            if (void* const manager = At<void*>(self, at))
                Virtual<void>(manager, 0x24);
        }
    } else {
        if (simulator && Virtual<uint8_t>(simulator, 0x3C))
            Virtual<uint8_t>(At<void*>(self, field::kSimulator), 0x30);
        for (const std::size_t at : managers) {
            if (void* const manager = At<void*>(self, at))
                Virtual<void>(manager, 0x28);
        }
    }
}

// The game's 0x005644E0 (ret 8): when the listener list (+0x64) is not
// empty, a message 0x0BB324BD (0x2C bytes, cGZMessage; not tested for null)
// with the control as void 1, `a` as data 2 and `b` as data 3, given to
// each listener's slot 3 (the next node taken before the call, the end read
// again after each), then let go.
void __fastcall NotifyListeners(char* self, void*, uint32_t a, uint32_t b)
{
    void** const head = At<void**>(self, field::kList);
    if (static_cast<void*>(head[0]) == static_cast<void*>(head))
        return;
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, std::size_t{0x2C});
    void* const message = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
    AddRef(message);
    Virtual<void>(message, 0x14, kMessageListeners); // SetType
    Virtual<void>(message, 0x30, static_cast<void*>(self)); // SetVoid1
    Virtual<void>(message, 0x40, a); // SetData2
    Virtual<void>(message, 0x54, b); // SetData3
    void** node = static_cast<void**>(At<void**>(self, field::kList)[0]);
    while (static_cast<void*>(node) != At<void*>(self, field::kList)) {
        void* const target = node[2];
        node = static_cast<void**>(node[0]);
        if (target)
            Virtual<uint8_t>(target, 0xC, message); // DoMessage
    }
    Release(message);
}

// The game's 0x00564570 (ret 4): the driving windows, once (nothing when
// +0xA4 is set). From the exemplar (0x6534284A, 0x690F693F, `instance`):
// property 0x2BE8E61B the layout of the driving window, 0xEC1B2361 a
// mission panel number. The driving window (layout (0, 0x96A006B0, it),
// window 0x4BCB938A) into +0xA4 under the view's window; with the three
// properties 0x2BE8E834 (child IDs), 0x2BE8E6CB (pictures) and 0xABE8E6CC
// (the second numbers) each child gets its picture (0, 0x46A006B0, n; slot
// 0x10) and its number (slot 0x18); the window's listener told about the
// third interface; the child 0x0BC3B559 (0xCA318385) its slot 0x20 with
// (0x11, 0x10); the window centred at the view's foot. The second window
// (0, 0x96A006B0, 0x8C1A5C9F; window 0xEC1A5CBF) into +0xA8 likewise
// (slot 0x118 first, centred, listener). At the end the exemplar let go and
// a mission panel number shown (the view's ShowMissionStatusPanel).
void __fastcall MakeWindows(char* self, void*, uint32_t instance)
{
    if (At<void*>(self, field::kWindow))
        return;
    void* resource = nullptr;
    uint32_t panel = 0;
    uint32_t exemplarKey[3] = {0x6534284A, 0x690F693F, instance};
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, &exemplarKey[0], kIidExemplar, &resource,
                         uint32_t{0}, uint32_t{0})) {
        void* const properties = Virtual<void*>(resource, 0x1C);
        uint32_t windowKey[3] = {0, 0x96A006B0, 0};
        uint32_t secondKey[3] = {0, 0x96A006B0, 0x8C1A5C9F};
        Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, properties, uint32_t{0x2BE8E61B}, &windowKey[2]);
        Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, properties, uint32_t{0xEC1B2361}, &panel);
        if (void* const old = At<void*>(self, field::kWindow)) {
            At<void*>(self, field::kWindow) = nullptr;
            Release(old);
        }
        Cdecl<uint8_t>(addr::SC4UI_LoadWindow, &windowKey[0], Virtual<void*>(At<void*>(self, base::kView), 0xC),
                       uint32_t{0x4BCB938A}, kIidWindowInterface, &At<void*>(self, field::kWindow));
        if (At<void*>(self, field::kWindow)) {
            void* const ids = Virtual<void*>(properties, 0x24, uint32_t{0x2BE8E834});
            void* const pictures = Virtual<void*>(properties, 0x24, uint32_t{0x2BE8E6CB});
            void* const numbers = Virtual<void*>(properties, 0x24, uint32_t{0xABE8E6CC});
            if (ids && pictures && numbers) {
                const uint32_t count = Virtual<uint32_t>(Virtual<void*>(ids, 0x14), 0x10);
                for (uint32_t i = 0; i < count; ++i) {
                    const uint32_t id = Virtual<uint32_t*>(Virtual<void*>(ids, 0x14), 0x19C)[i];
                    const uint32_t picture = Virtual<uint32_t*>(Virtual<void*>(pictures, 0x14), 0x19C)[i];
                    const uint32_t number = Virtual<uint32_t*>(Virtual<void*>(numbers, 0x14), 0x19C)[i];
                    void* child = nullptr;
                    if (Virtual<uint8_t>(At<void*>(self, field::kWindow), 0x90, id, uint32_t{0x0BCBF1DF}, &child)) {
                        ImageHolder image;
                        This<void*>(addr::SC4ImageLoader_ConstructParts, &image, uint32_t{0}, uint32_t{0x46A006B0},
                                    picture, uint32_t{1}, uint32_t{0});
                        Virtual<void>(child, 0x10, image.words[1]);
                        Virtual<void>(child, 0x18, number);
                        This<void>(addr::SC4ImageLoader_Destruct, &image);
                    }
                    if (child)
                        Release(child);
                }
            }
            void* listener = nullptr;
            void* knob = nullptr;
            Listen(self, At<void*>(self, field::kWindow), &listener);
            if (Virtual<uint8_t>(At<void*>(self, field::kWindow), 0x90, uint32_t{0x0BC3B559}, uint32_t{0xCA318385},
                                 &knob))
                Virtual<void>(knob, 0x20, uint32_t{0x11}, uint32_t{0x10});
            Centre(self, At<void*>(self, field::kWindow), false);
            if (void* const old = At<void*>(self, field::kWindow2)) {
                At<void*>(self, field::kWindow2) = nullptr;
                Release(old);
            }
            Cdecl<uint8_t>(addr::SC4UI_LoadWindow, &secondKey[0], Virtual<void*>(At<void*>(self, base::kView), 0xC),
                           uint32_t{0xEC1A5CBF}, kIidWindowInterface, &At<void*>(self, field::kWindow2));
            if (void* const second = At<void*>(self, field::kWindow2)) {
                void* secondListener = nullptr;
                Virtual<void>(second, 0x118);
                Centre(self, second, true);
                Listen(self, At<void*>(self, field::kWindow2), &secondListener);
                if (secondListener)
                    Release(secondListener);
            }
            if (listener)
                Release(listener);
            if (knob)
                Release(knob);
        }
    }
    if (resource)
        Release(resource);
    if (panel)
        Virtual<uint8_t>(At<void*>(self, base::kView), 0xE0, panel); // ShowMissionStatusPanel
}

// The game's 0x00565160 (ret 8): starts driving the automaton that drives
// `automaton` (its FindDrivingAutomaton, slot 0xC8). Nothing without an
// automaton; disasters off (the disaster layer's EnableDisasters with 0)
// before the driving automaton is asked for, nothing more without one.
// Then 0x00562FD0, the automaton under the pointer let go, the view's UI
// minimised and its child 0x43 hidden, the windows made (0x00564570) for
// the occupant's property 0x2BE8FBF3 or, without it, 0x6BE8846C when the
// automaton's +4 interface's slot 0x1C answers 0x480E6151 and 0x0BE8824F
// otherwise; the service of 0x00602640 given the occupant's properties with
// 0xCBBBA52F (slot 3), 0 (slot 5) and the static at 0x00B229A8 (slots 7, 8,
// 9); the automaton held at +0x30 and pushed into state 0x10 (PushState),
// the vehicle manager (+0x8C, not tested for null) follows it; +0x40 0.042f
// and the zoom limits (1, 5) or the description's (slot 0x10C: +0x64, or
// inherited through +0xC by 0x0046B4A0); the view's SetZoomLimits (first -
// 1, 5 when the app's +0xF10 byte is above 0 or there is no app, else 4) and
// SetZoom (second - 1); the window's zoom buttons enabled as the view can
// zoom; +0x44 1, +0x45 0; a marker 6 on the occupant (overlay manager +0x34)
// into +0x38; the target part as the automaton's callback; the stopwatch
// stopped and reset; the simulator resumed. `fromClick` is not read.
void __fastcall StartDriving(char* self, void*, void* automaton, uint8_t)
{
    if (!automaton)
        return;
    if (void* const layer = Virtual<void*>(City(), 0x120))
        Virtual<uint8_t>(layer, 0x14, uint32_t{0}); // EnableDisasters
    char* const driver = Virtual<char*>(automaton, 0xC8);
    if (!driver)
        return;
    This<void>(addr::cSC4UDriveControl_F562FD0, self);
    DropHovered(self);
    void* const view = At<void*>(self, base::kView);
    Virtual<uint8_t>(view, 0xDC, uint32_t{1}); // MinimizeUI
    Cdecl<uint8_t>(addr::SC4Win_ShowChild, Virtual<void*>(At<void*>(self, base::kView), 0xC), uint32_t{0x43},
                   uint32_t{0}, uint32_t{0});
    uint32_t instance = 0;
    void* const occupant = Virtual<void*>(driver, 0x14);
    if (!Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, Virtual<void*>(occupant, 0x18), uint32_t{0x2BE8FBF3},
                        &instance)) {
        const uint32_t kind = Virtual<uint32_t>(driver + 4, 0x1C);
        instance = kind == 0x480E6151 ? 0x6BE8846C : 0x0BE8824F;
    }
    MakeWindows(self, nullptr, instance);
    void* const driverOccupant = Virtual<void*>(driver, 0x14);
    void* const service = DriveService();
    Virtual<void>(service, 0xC, Virtual<void*>(driverOccupant, 0x18), uint32_t{0xCBBBA52F});
    Virtual<void>(DriveService(), 0x14, uint32_t{0});
    const uint32_t still = Global<uint32_t>(addr::SC4UDrive_VectorFillY);
    Virtual<void>(DriveService(), 0x1C, still);
    Virtual<void>(DriveService(), 0x20, still);
    Virtual<void>(DriveService(), 0x24, still);
    void* const old = Driven(self);
    if (driver != old) {
        AddRef(driver);
        At<void*>(self, field::kDriven) = driver;
        if (old)
            Release(old);
    }
    Virtual<uint8_t>(Driven(self), 0xD0, kStateDriving, static_cast<void*>(nullptr), -1.0f, uint32_t{0},
                     uint32_t{0}, uint32_t{0}, static_cast<void*>(nullptr)); // PushState
    Virtual<void>(At<void*>(self, field::kVehicleManager), 0x88, Driven(self)); // SetCameraFollow
    At<uint32_t>(self, field::kFloat40) = kFloat0042;
    int32_t lower = 1;
    int32_t zoom = 5;
    if (char* const description = Virtual<char*>(Driven(self), 0x10C)) {
        uint32_t* settings = At<uint32_t*>(description, 0x64);
        if (!settings) {
            void* const parent = At<void*>(description, 0xC);
            settings = parent ? This<uint32_t*>(addr::SC4AutomatonTemplate_FindInherited64, parent) : nullptr;
        }
        if (settings) {
            At<uint32_t>(self, field::kFloat40) = settings[2];
            lower = static_cast<int32_t>(settings[0]);
            zoom = static_cast<int32_t>(settings[1]);
        }
    }
    int32_t mode = 2;
    if (char* const app = Virtual<char*>(Global<void*>(addr::SC4_Service43C94), 0x98))
        mode = At<uint8_t>(app, 0xF10);
    Virtual<uint8_t>(At<void*>(self, base::kView), 0x2C, lower - 1, mode > 0 ? 5 : 4); // SetZoomLimits
    Virtual<int32_t>(At<void*>(self, base::kView), 0x20, zoom - 1);                    // SetZoom
    if (At<void*>(self, field::kWindow)) {
        const uint32_t in = Virtual<uint32_t>(At<void*>(self, base::kView), 0x30); // CanZoomIn
        Cdecl<uint8_t>(addr::SC4Win_EnableChild, At<void*>(self, field::kWindow), uint32_t{0x4992046A}, in);
        const uint32_t out = Virtual<uint32_t>(At<void*>(self, base::kView), 0x34); // CanZoomOut
        Cdecl<uint8_t>(addr::SC4Win_EnableChild, At<void*>(self, field::kWindow), uint32_t{0xE9920494}, out);
    }
    At<uint8_t>(self, field::kByte44) = 1;
    At<uint8_t>(self, field::kByte45) = 0;
    if (void* const overlays = At<void*>(self, field::kRef34))
        At<uint32_t>(self, field::kWord38) = This<uint32_t>(addr::SC4OverlayManager_Add, overlays,
                                                            Virtual<void*>(Driven(self), 0x14), uint32_t{6},
                                                            uint32_t{0}, uint32_t{0});
    Virtual<void>(driver, 0x9C, self + field::kTarget); // AddCallback
    This<void>(addr::SC4Stopwatch_Stop, self + field::kStopwatch);
    This<void>(addr::SC4Stopwatch_Reset, self + field::kStopwatch);
    Pause(self, nullptr, 0);
}

// The game's 0x005650E0 (slot 27, PutAway): the listeners told (1, 0); a
// driven automaton that answers its slot 0x48 removed (slot 0x34 with 0);
// 0x00562FD0; the automaton under the pointer let go; disasters back on;
// the base's PutAway (a jump in the game), whose answer is the answer.
uint32_t __fastcall PutAway(char* self, void*)
{
    NotifyListeners(self, nullptr, 1, 0);
    if (Driven(self) && Virtual<uint8_t>(Driven(self), 0x48))
        Virtual<void>(Driven(self), 0x34, uint32_t{0});
    This<void>(addr::cSC4UDriveControl_F562FD0, self);
    DropHovered(self);
    if (void* const layer = Virtual<void*>(City(), 0x120))
        Virtual<uint8_t>(layer, 0x14, uint32_t{1}); // EnableDisasters
    return This<uint32_t>(addr::SC4ViewInputControl_PutAway, self);
}

// The game's 0x00566A70 (ret 8): a command through the driven automaton's
// key binding records. Nothing (false) without a driven automaton that
// takes input. The driving state (FindState 0x10) and the description
// (slot 0x10C) are asked for and a record made (0x0051D040); without both,
// or when the description's map (0x005204D0) has no record for the
// command, false. Else the command's byte in the state takes its new
// value: 0 with flag 2 (and the change counts as a toggle when the key is
// down), the key's state with flag 1, else on a press the byte flipped
// (released: unchanged). When it changed or toggled: with flag 0x10 the
// record's named effects (transition type = flag 4) recreated (toggle),
// started (on) or stopped (off) on the automaton; with flag 0x20 the
// automata controller of the record's template on the automaton's occupant
// (the city's automata controller manager, slot 19; made with slot 5 when
// on or toggled and missing) told on (slot 0x48 with 1) or, when not a
// toggle, off. True when the record has flag 8. The record destroyed
// (0x0051D0D0).
uint8_t __fastcall ApplyBinding(char* self, void*, uint32_t command, uint8_t down)
{
    if (!DrivenTakesInput(self))
        return 0;
    char* const driving = Virtual<char*>(Driven(self), 0xE4, kStateDriving); // FindState
    char* const description = Virtual<char*>(Driven(self), 0x10C);
    Binding binding;
    This<void*>(addr::SC4KeyBinding_Construct, &binding);
    uint8_t taken = 0;
    if (driving && description && This<uint8_t>(addr::SC4KeyBindingMap_Find, description, command, &binding)) {
        const uint32_t flags = At<uint32_t>(&binding, kBindingFlags);
        const uint8_t old = At<uint8_t>(driving, state::kKeys + command);
        uint8_t value = old;
        uint8_t toggled = 0;
        if (flags & kBindingOff) {
            value = 0;
            toggled = down;
        } else if (flags & kBindingFollow) {
            value = down;
        } else if (down) {
            value = old == 0 ? 1 : 0;
        }
        const bool changed = value != old || toggled;
        At<uint8_t>(driving, state::kKeys + command) = value;
        if (changed) {
            if (flags & kBindingEffects) {
                const uint32_t type = (flags & kBindingType) ? 1 : 0;
                if (toggled)
                    Virtual<void>(Driven(self), 0xBC, &binding, type, uint32_t{1});
                else if (value)
                    Virtual<uint8_t>(Driven(self), 0xB0, &binding, type, uint32_t{1});
                else
                    Virtual<void>(Driven(self), 0xB4, &binding, type);
            }
            if (flags & kBindingController) {
                if (void* const controllers = Virtual<void*>(City(), 0xBC)) {
                    void* const name = &At<uint8_t>(&binding, kBindingTemplate);
                    void* controller =
                        Virtual<void*>(controllers, 0x4C, Virtual<void*>(Driven(self), 0x14), name); // GetController
                    if (!controller && (value || toggled))
                        controller = Virtual<void*>(controllers, 0x14, Virtual<void*>(Driven(self), 0x14), name,
                                                    static_cast<void*>(nullptr)); // AddControllerFromTemplateName
                    if (controller) {
                        if (value)
                            Virtual<void>(controller, 0x48, uint32_t{1});
                        else if (!toggled)
                            Virtual<void>(controller, 0x48, uint32_t{0});
                    }
                }
            }
        }
        if (flags & kBindingTaken)
            taken = 1;
    }
    This<void>(addr::SC4KeyBinding_Destruct, &binding);
    return taken;
}

// The game's 0x00563D40 (ret 4): a command's press. 8 and above 12 false,
// the rest true. With CanDrive: HandlePlayerDriveCommand with (command, 1)
// for 0, 1, 2, 3, 5, 6, 7, 9, 11, 12 and the service of 0x00602640 its slot
// 0x14 with 1, 2, 3, 3, 6, 4 for 0, 1, 2, 3, 5, 7; 4 and 10 flip +0x44 /
// +0x45 (also without CanDrive) and give the new value; 9 flips the
// driving state's +0x229 byte. 2 and 3 first press the window's buttons
// 0x8BCB93FC / 0x6BCB93F5 (slot 0x24 with 1).
uint8_t __fastcall CommandDown(char* self, void*, uint32_t command)
{
    switch (command) {
    case 0:
    case 1:
    case 5:
    case 7:
        if (CanDrive(self)) {
            Command(self, command, 1);
            Virtual<void>(DriveService(), 0x14, kServiceCodes[command]);
        }
        return 1;
    case 2:
    case 3:
        PressButton(self, command == 2 ? 0x8BCB93FC : 0x6BCB93F5, 1);
        if (CanDrive(self)) {
            Command(self, command, 1);
            Virtual<void>(DriveService(), 0x14, uint32_t{3});
        }
        return 1;
    case 4:
        At<uint8_t>(self, field::kByte44) = At<uint8_t>(self, field::kByte44) == 0 ? 1 : 0;
        if (CanDrive(self))
            Command(self, 4, At<uint8_t>(self, field::kByte44));
        return 1;
    case 6:
    case 11:
    case 12:
        if (CanDrive(self))
            Command(self, command, 1);
        return 1;
    case 9:
        if (CanDrive(self)) {
            Command(self, 9, 1);
            if (char* const driving = Virtual<char*>(Driven(self), 0xE4, kStateDriving))
                At<uint8_t>(driving, state::kByte229) = At<uint8_t>(driving, state::kByte229) == 0 ? 1 : 0;
        }
        return 1;
    case 10:
        At<uint8_t>(self, field::kByte45) = At<uint8_t>(self, field::kByte45) == 0 ? 1 : 0;
        if (CanDrive(self))
            Command(self, 10, At<uint8_t>(self, field::kByte45));
        return 1;
    default:
        return 0;
    }
}

// The game's 0x00564060 (ret 4): a command's release. 8 and above 12
// false, the rest true. With CanDrive: HandlePlayerDriveCommand with
// (command, 0) for 0, 1, 2, 3, 5, 6, 7, 9, 11, 12 and the service of
// 0x00602640 its slot 0x18 with 1, 2, 3, 3, 6, 4 for 0, 1, 2, 3, 5, 7; 4
// and 10 do nothing. 2 and 3 first release the window's buttons (slot 0x24
// with 0).
uint8_t __fastcall CommandUp(char* self, void*, uint32_t command)
{
    switch (command) {
    case 0:
    case 1:
    case 5:
    case 7:
        if (CanDrive(self)) {
            Command(self, command, 0);
            Virtual<void>(DriveService(), 0x18, kServiceCodes[command]);
        }
        return 1;
    case 2:
    case 3:
        PressButton(self, command == 2 ? 0x8BCB93FC : 0x6BCB93F5, 0);
        if (CanDrive(self)) {
            Command(self, command, 0);
            Virtual<void>(DriveService(), 0x18, uint32_t{3});
        }
        return 1;
    case 4:
    case 10:
        return 1;
    case 6:
    case 9:
    case 11:
    case 12:
        if (CanDrive(self))
            Command(self, command, 0);
        return 1;
    default:
        return 0;
    }
}

// The game's 0x00566C60 (slot 14, OnKeyDown, ret 8): true for a repeat
// (modifier bit 0x40000000); Escape puts the control away (slot 27), true;
// during a city situation countdown true; without the script system (+0xB0)
// false; the key's command (its slot 0x38) 13: false; else the binding
// (0x00566A70 with 1) or, when it does not take it, the press (0x00563D40);
// true.
uint8_t __fastcall OnKeyDown(char* self, void*, uint32_t key, uint32_t modifiers)
{
    if (modifiers & kKeyRepeat)
        return 1;
    if (key == kKeyEscape) {
        Virtual<uint32_t>(self, 0x6C);
        return 1;
    }
    if (CountdownRunning())
        return 1;
    void* const scripts = At<void*>(self, field::kScriptSystem);
    if (!scripts)
        return 0;
    const uint32_t command = Virtual<uint32_t>(scripts, 0x38, key);
    if (command == kCommandNone)
        return 0;
    if (!ApplyBinding(self, nullptr, command, 1))
        CommandDown(self, nullptr, command);
    return 1;
}

// The game's 0x00566CF0 (slot 15, OnKeyUp, ret 8): as OnKeyDown without the
// repeat and Escape tests, with the binding given 0 and the release
// (0x00564060).
uint8_t __fastcall OnKeyUp(char* self, void*, uint32_t key, uint32_t)
{
    if (CountdownRunning())
        return 1;
    void* const scripts = At<void*>(self, field::kScriptSystem);
    if (!scripts)
        return 0;
    const uint32_t command = Virtual<uint32_t>(scripts, 0x38, key);
    if (command == kCommandNone)
        return 0;
    if (!ApplyBinding(self, nullptr, command, 0))
        CommandUp(self, nullptr, command);
    return 1;
}

// The game's 0x005655D0 (slot 16, OnMouseDownL, ret 0xC). A network piece
// under the pointer that the network manager (+0xAC) takes (slot 0xA8):
// the sound 0x8BE8D9AC at the piece's position (the audio director's
// object, 0x004F7D20 and 0x004F7D90; the position starts as the static at
// 0x00B229AC thrice), the piece let go, its cell (slot 0xEC, read after
// the release) into the point holder's +0x24/+0x28 when it differs; true.
// A piece it does not take is let go. Then with a driven automaton that
// takes input: nothing during a countdown; when its occupant is of group
// 0x4006 and CanDrive, HandlePlayerDriveCommand (7, 1) and the service of
// 0x00602640 its slot 0x14 with 4. With nothing driven: an automaton under
// the pointer starts a drive (0x00565160 with 1) and is let go. True.
uint8_t __fastcall OnMouseDownL(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    void* piece = nullptr;
    if (PickNetworkOccupant(self, nullptr, x, z, &piece)) {
        if (Virtual<uint8_t>(At<void*>(self, field::kNetworkManager), 0xA8, piece)) {
            if (void* const director = Cdecl<void*>(addr::SC4Sound_DirectorObject60)) {
                const uint32_t instance = Cdecl<uint32_t>(addr::SC4Sound_NextInstance);
                This<uint8_t>(addr::SC4Audio_F4F7D20, director, kSoundNetwork, instance);
                const float start = Global<float>(addr::SC4UDrive_VectorFill);
                float position[3] = {start, start, start};
                Virtual<uint8_t>(Virtual<void*>(piece, 0xC0), 0x20, &position[0]); // AsOccupant, GetPosition
                This<uint8_t>(addr::SC4Audio_F4F7D90, director, instance, position[0], position[1], position[2]);
            }
            Release(piece);
            int32_t cellX;
            int32_t cellZ;
            Virtual<void>(piece, 0xEC, &cellX, &cellZ); // GetOccupiedCell
            char* const points = At<char*>(self, field::kPointHolder);
            if (cellX != At<int32_t>(points, holder::kPickX) || cellZ != At<int32_t>(points, holder::kPickZ)) {
                At<int32_t>(points, holder::kPickZ) = cellZ;
                At<int32_t>(points, holder::kPickX) = cellX;
            }
            return 1;
        }
        Release(piece);
    }
    if (DrivenTakesInput(self)) {
        if (CountdownRunning())
            return 1;
        void* const occupant = Virtual<void*>(Driven(self), 0x14);
        if (Virtual<uint8_t>(occupant, 0x58, kGroupDrivable) && CanDrive(self)) { // IsOccupantGroup
            Command(self, 7, 1);
            Virtual<void>(DriveService(), 0x14, uint32_t{4});
        }
        return 1;
    }
    if (Driven(self))
        return 1;
    void* automaton = nullptr;
    if (PickAutomaton(self, nullptr, x, z, &automaton)) {
        StartDriving(self, nullptr, automaton, 1);
        Release(automaton);
    }
    return 1;
}

// The game's 0x005631C0 (slot 20, OnMouseMove, ret 0xC). The ground point
// under the pointer (the view's slot 0x90 with 0; the point starts as the
// static at 0x00B229AC thrice) turned into a cell (the city's
// PositionToCell) goes into the point holder's +0x1C/+0x20 when it
// differs; without a point both become -1 (when not already). With a
// driven automaton that takes input: the screen point into the driving
// state's +0x214 (x) / +0x218 (z) when there is the state; true. Else the
// automaton under the pointer is held at +0x60 (SC4RefPtr::Assign when it
// differs, the pick's reference let go); with none, +0x60 let go and the
// view's ClearCursorText; true.
uint8_t __fastcall OnMouseMove(char* self, void*, int32_t x, int32_t z, uint32_t)
{
    const float start = Global<float>(addr::SC4UDrive_VectorFill);
    float point[3] = {start, start, start};
    if (Virtual<uint8_t>(At<void*>(self, base::kView), 0x90, x, z, &point[0], uint32_t{0})) {
        int32_t cellX;
        int32_t cellZ;
        Virtual<void>(City(), 0x17C, point[0], point[2], &cellX, &cellZ); // PositionToCell
        char* const held = At<char*>(self, field::kPointHolder);
        if (cellX != At<int32_t>(held, holder::kHoverX) || cellZ != At<int32_t>(held, holder::kHoverZ)) {
            At<int32_t>(held, holder::kHoverX) = cellX;
            At<int32_t>(held, holder::kHoverZ) = cellZ;
        }
    } else {
        char* const held = At<char*>(self, field::kPointHolder);
        if (At<int32_t>(held, holder::kHoverX) != -1 || At<int32_t>(held, holder::kHoverZ) != -1) {
            At<int32_t>(held, holder::kHoverX) = -1;
            At<int32_t>(held, holder::kHoverZ) = -1;
        }
    }
    if (DrivenTakesInput(self)) {
        if (char* const driving = Virtual<char*>(Driven(self), 0xE4, kStateDriving)) {
            At<int32_t>(driving, state::kMouseZ) = z;
            At<int32_t>(driving, state::kMouseX) = x;
        }
        return 1;
    }
    void* picked = nullptr;
    if (PickAutomaton(self, nullptr, x, z, &picked)) {
        if (picked != At<void*>(self, field::kRef60))
            This<void>(addr::SC4RefPtr_Assign, self + field::kRef60, picked);
        Release(picked);
        return 1;
    }
    if (void* const hovered = At<void*>(self, field::kRef60)) {
        At<void*>(self, field::kRef60) = nullptr;
        Release(hovered);
    }
    Virtual<uint8_t>(At<void*>(self, base::kView), 0xA4, kCursorText); // ClearCursorText
    return 1;
}

// --- Registration ----------------------------------------------------------------------

bool InstallUDrive3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::cSC4UDriveControl_OnKeyDown, reinterpret_cast<void*>(&OnKeyDown)},
        {addr::cSC4UDriveControl_OnKeyUp, reinterpret_cast<void*>(&OnKeyUp)},
        {addr::cSC4UDriveControl_ApplyBinding, reinterpret_cast<void*>(&ApplyBinding)},
        {addr::cSC4UDriveControl_CommandDown, reinterpret_cast<void*>(&CommandDown)},
        {addr::cSC4UDriveControl_CommandUp, reinterpret_cast<void*>(&CommandUp)},
        {addr::cSC4UDriveControl_OnMouseDownL, reinterpret_cast<void*>(&OnMouseDownL)},
        {addr::cSC4UDriveControl_PickNetworkOccupant, reinterpret_cast<void*>(&PickNetworkOccupant)},
        {addr::cSC4UDriveControl_PickAutomaton, reinterpret_cast<void*>(&PickAutomaton)},
        {addr::cSC4UDriveControl_OnMouseMove, reinterpret_cast<void*>(&OnMouseMove)},
        {addr::cSC4UDriveControl_PutAway, reinterpret_cast<void*>(&PutAway)},
        {addr::cSC4UDriveControl_NotifyListeners, reinterpret_cast<void*>(&NotifyListeners)},
        {addr::cSC4UDriveControl_MakeWindows, reinterpret_cast<void*>(&MakeWindows)},
        {addr::cSC4UDriveControl_F565160, reinterpret_cast<void*>(&StartDriving)},
        {addr::cSC4UDriveControl_F562F00, reinterpret_cast<void*>(&Pause)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_registered = registry::Register({
    "ui.tools.udrive3",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4UDriveControl_OnKeyDown,
    "Our U-Drive-It control's keys, left press, mouse move and drive start (docs/tools.md)",
    false,
    InstallUDrive3,
});

} // namespace
} // namespace opensc4::tool_udrive_3
