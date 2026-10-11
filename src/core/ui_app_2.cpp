// The application's main window (0x004E08C0 to 0x004E1320): the window
// every screen of the game is a child of.
//
// The object is 0x158 bytes (constructor 0x004E0EE0, made by the
// application at 0x0044943E): the framework's window at +0 (vtable
// 0x00A92F28, 151 slots, the window base's layout) and a message target at
// +0xD8 (vtable 0x00A92F18, 4 slots, ID 0x452294AA). The global 0x00B43CBC
// points at it.
//
// What it does:
//
//   - Init (window slot 4) names the window "WinSC4App", gives it the ID
//     0x6104489A and the main window's size, loads the key accelerators
//     (resource 0xA2E3D533 / 0x6A231EAA / 0x6A9362F1 into an object of
//     class 0x42E967BE kept at +0xF8), starts the frame stopwatch, puts
//     the first screen up (the intro, or with "-intro:off" on the command
//     line the screen after it), takes the service at 0x00B43C9C,
//     subscribes to message 0x2A9496C9 and registers the cheat word
//     "ShowTime" (0xEA5EDB88), and shows the clock when the options say so;
//   - Shutdown (slot 5) undoes that;
//   - the window message handler (slot 3): a key press or release (type
//     0x1B, kind 5 or 6) goes through the accelerators and the command it
//     maps to is run; the cheat "ShowTime" shows the clock; an activation
//     (0x4348B111) is passed to the application and sent on as a message;
//   - the screens: one screen is up at a time, kept at +0xEC. Four makers
//     (the intro 0x108 bytes, a screen of 0x118 bytes, the region screen
//     0x200 bytes, the city view 0x448 bytes) each do nothing when a screen
//     is up; DropScreen takes the screen off. The city view is also kept
//     as an object at +0xF0, where 0x004E0960 reads it;
//   - message 0x2A9496C9 (the target's slot 3) runs the cheat box as a
//     modal window;
//   - slot 88 counts the frames and keeps the frame rate at +0x118.
//
// Replacement, off by default:
//
//   ui.app.part2: everything named above, the constructor and the
//   destructors included.
//
// Not the main window's, and left the game's: 0x004E03F0, 0x004E0480,
// 0x004E05A0 and 0x004E0830 before it belong to the application's
// background file installer (vtable 0x00A92EC0: a worker thread that
// copies the files named in FileList.txt), and 0x004E1320, 0x004E13E0,
// 0x004E13F0, 0x004E1430 and 0x004E14B0 after it to the sound event system
// (cSC4AudioAEMSEventSystem, vtable 0x00A93200). Neither is interface
// code.
//
// The window: +0x4 the window manager, +0xD8 the message target, +0xDC the
// application (the constructor's argument), +0xE0 unused, +0xE4 the
// service of 0x00B43C9C, +0xE8 its interface 0xC47B747C, +0xEC the screen
// that is up (a window), +0xF0 the city view, +0xF4 an object nothing in
// this range sets, +0xF8 the key accelerators, +0x100 a stopwatch (0x18
// bytes), +0x118 the frame rate, +0x11C a string, +0x130 to +0x14C eight
// dwords of 1000000, +0x150 the frame count, +0x154 an object released by
// the destructor.
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

namespace win { // the main window
constexpr std::size_t kManager = 0x04;
constexpr std::size_t kTarget = 0xD8;
constexpr std::size_t kApp = 0xDC;
constexpr std::size_t kFieldE0 = 0xE0;
constexpr std::size_t kService = 0xE4;
constexpr std::size_t kServiceInterface = 0xE8;
constexpr std::size_t kScreen = 0xEC;
constexpr std::size_t kCityView = 0xF0;
constexpr std::size_t kObjectF4 = 0xF4;
constexpr std::size_t kAccelerators = 0xF8;
constexpr std::size_t kStopwatch = 0x100;
constexpr std::size_t kFrameRate = 0x118; // float
constexpr std::size_t kString = 0x11C;
constexpr std::size_t kMillions = 0x130;  // eight dwords
constexpr std::size_t kFrames = 0x150;
constexpr std::size_t kObject154 = 0x154;
} // namespace win

// The city view's window part.
constexpr std::size_t kViewWindow = 0x0C;

constexpr uint32_t kTargetId = 0x452294AA;
constexpr uint32_t kWindowId = 0x6104489A;
constexpr uint32_t kCheatBoxMessage = 0x2A9496C9;
constexpr uint32_t kShowTimeCheat = 0xEA5EDB88;
constexpr uint32_t kKeyMessage = 0x1B;
constexpr uint32_t kCheatMessage = 0x230E27AC;
constexpr uint32_t kActivateMessage = 0x4348B111;

// A window message as window procedures get it.
struct WinMessage {
    uint32_t type;
    uint32_t a;
    uint32_t b;
    uint32_t c;
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
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

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

// The inlined "release and forget" of the shutdown: the field is emptied
// after the release.
void Drop(void*& holder)
{
    if (void* const object = holder) {
        Release(object);
        holder = nullptr;
    }
}

// The inlined destructor of a string.
void DestroyString(GameString& text)
{
    char* const buffer = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (buffer)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(buffer));
}

uint8_t __fastcall Shutdown(char* self, void*);
uint8_t __fastcall ShowIntro(char* self, void*);
uint8_t __fastcall ShowScreen118(char* self, void*);

// --- Small methods --------------------------------------------------------------------

// The game's 0x004E08C0 (thiscall, ret 8; `this` is the window). The
// window's QueryInterface: the message target for its ID, else the window
// base's answer. The original's null check of `this` is kept as it stands
// (with no `this` it stores null and then faults in AddRef).
uint8_t __fastcall WindowQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid != kTargetId)
        return This<uint8_t>(addr::SC4Win_QueryInterface, self, iid, out);
    *out = self ? self + win::kTarget : nullptr;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// The game's 0x004E0FE0 (thiscall, ret 8; `this` is the message target).
// The target's QueryInterface: the window's.
uint8_t __fastcall TargetQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    return WindowQueryInterface(self - win::kTarget, nullptr, iid, out);
}

// The game's 0x004E0900 (thiscall, no arguments; `this` is the window).
// The frame rate, left on the x87 stack. The one caller is 0x005C8D59.
double __fastcall GetFrameRate(char* self, void*)
{
    return static_cast<double>(At<float>(self, win::kFrameRate));
}

// The game's 0x004E0910 (thiscall, no arguments; window slot 88). Counts a
// frame, works the frame rate out of the stopwatch's reading (1000000
// over it, the reading taken as unsigned) and starts the stopwatch anew.
uint8_t __fastcall CountFrame(char* self, void*)
{
    At<uint32_t>(self, win::kFrames) = At<uint32_t>(self, win::kFrames) + 1;
    char* const stopwatch = self + win::kStopwatch;
    const uint32_t elapsed = This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, stopwatch);
    At<float>(self, win::kFrameRate) =
        static_cast<float>(static_cast<double>(Global<float>(addr::SC4_Float867AC)) / static_cast<double>(elapsed));
    This<void>(addr::GZStopwatch_Start, stopwatch);
    return 1;
}

// The game's 0x004E0960 (thiscall, no arguments; `this` is the window).
// The city view, or null when no city is up. The one caller is 0x0060259A.
void* __fastcall GetCityView(char* self, void*)
{
    return At<void*>(self, win::kCityView);
}

// --- The screens ----------------------------------------------------------------------

// What the four makers share once the new screen's window exists: the
// inlined assignment to the holder at +0xEC, then the screen is added as a
// child (window slot 0x38), told to start (its slot 0x114) and handed to
// 0x0099C9DF.
uint8_t PutScreenUp(char* self, void* window)
{
    void* const old = At<void*>(self, win::kScreen);
    if (window != old) {
        if (window)
            AddRef(window);
        At<void*>(self, win::kScreen) = window;
        if (old)
            Release(old);
    }
    Virtual<void>(self, 0x38, At<void*>(self, win::kScreen));
    Virtual<void>(At<void*>(self, win::kScreen), 0x114);
    This<void>(addr::SC4Win_F99C9DF, self, At<void*>(self, win::kScreen));
    return 1;
}

// A screen of `size` bytes made by `constructor`, put up when none is.
uint8_t ShowScreen(char* self, uint32_t size, std::uintptr_t constructor)
{
    if (At<void*>(self, win::kScreen))
        return 0;
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, size);
    void* const window = memory ? This<void*>(constructor, memory) : nullptr;
    return PutScreenUp(self, window);
}

// The game's 0x004E0C20 (thiscall, no arguments; `this` is the window).
// Puts the intro window up (0x108 bytes, constructor 0x0079CED0). False
// when a screen is up already.
uint8_t __fastcall ShowIntro(char* self, void*)
{
    return ShowScreen(self, 0x108, addr::SC4UI_F79CED0);
}

// The game's 0x004E0CB0 (thiscall, no arguments). The same with the screen
// of 0x118 bytes (constructor 0x007BE4E0).
uint8_t __fastcall ShowScreen118(char* self, void*)
{
    return ShowScreen(self, 0x118, addr::SC4UI_Construct7BE4E0);
}

// The game's 0x004E0D40 (thiscall, no arguments). The same with the region
// screen (0x200 bytes, constructor 0x007B0C00).
uint8_t __fastcall ShowScreen200(char* self, void*)
{
    return ShowScreen(self, 0x200, addr::cSC4WinRegionScreen_Construct);
}

// The game's 0x004E0DD0 (thiscall, no arguments). The same with the city
// view (0x448 bytes, constructor 0x007F0A70): the object is kept at +0xF0
// (no reference taken) and its window part (+0xC) is the screen.
uint8_t __fastcall ShowCityView(char* self, void*)
{
    if (At<void*>(self, win::kScreen))
        return 0;
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x448});
    char* const view = memory ? This<char*>(addr::SC4UI_F7F0A70, memory) : nullptr;
    At<char*>(self, win::kCityView) = view;
    return PutScreenUp(self, view ? view + kViewWindow : nullptr);
}

// The game's 0x004E0E70 (thiscall, no arguments). Takes the screen off:
// removed as a child (window slot 0x40), released, and the city view's
// pointer forgotten. False when no screen is up.
uint8_t __fastcall DropScreen(char* self, void*)
{
    void* const screen = At<void*>(self, win::kScreen);
    if (!screen)
        return 0;
    Virtual<void>(self, 0x40, screen);
    if (void* const held = At<void*>(self, win::kScreen)) {
        At<void*>(self, win::kScreen) = nullptr;
        Release(held);
    }
    At<void*>(self, win::kCityView) = nullptr;
    return 1;
}

// --- The cheat box --------------------------------------------------------------------

// The game's 0x004E0BB0 (thiscall, ret 4; `this` is the window). For
// message 0x2A9496C9 only: makes the cheat box (0x120 bytes, constructor
// 0x0079BD00), runs it as a modal window (the window manager's slot 0xA4)
// and takes it off the manager (slot 0x5C). Always answers false.
uint8_t __fastcall RunCheatBox(char* self, void*, uint32_t message)
{
    if (message != kCheatBoxMessage)
        return 0;
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x120});
    void* const box = memory ? This<void*>(addr::SC4UI_F79BD00, memory) : nullptr;
    if (box)
        AddRef(box);
    Virtual<uint32_t>(At<void*>(self, win::kManager), 0xA4, box);
    Virtual<void>(At<void*>(self, win::kManager), 0x5C, box);
    if (box)
        Release(box);
    return 0;
}

// The game's 0x004E0EB0 (thiscall, ret 4; `this` is the message target at
// +0xD8; its slot 3). The cheat box for message 0x2A9496C9.
uint8_t __fastcall TargetDoMessage(char* self, void*, void* message)
{
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == kCheatBoxMessage)
        RunCheatBox(self - win::kTarget, nullptr, type);
    return 1;
}

// --- Window messages ------------------------------------------------------------------

// The game's 0x004E0A70 (thiscall, ret 4; window slot 3). The window
// message handler:
//
//   - a key (type 0x1B) going down (kind 5) or up (kind 6): the
//     accelerators at +0xF8 are asked (slot 0x28: key, modifiers, down,
//     the command out) and, when they know it, the command is run through
//     the command service's slot 0x50; true then, false when they do not
//     (the base class is not asked). The accelerators are not checked for
//     null, as in the original;
//   - the cheat "ShowTime" (type 0x230E27AC with 0xEA5EDB88): the clock is
//     shown; true;
//   - an activation (type 0x4348B111): the application's slot 0x30 gets
//     "active or not", and a message of the same type carrying the value
//     is sent through the message server; then the base class;
//   - everything else: the base class (0x0099CCF0).
//
// When the message cannot be allocated the original goes on with a null
// pointer and faults; so does this.
uint8_t __fastcall WindowMessage(char* self, void*, WinMessage* message)
{
    const uint32_t type = message->type;
    if (type == kKeyMessage) {
        const uint32_t kind = message->b;
        if (kind == 5 || kind == 6) {
            const uint32_t* const key = reinterpret_cast<const uint32_t*>(message->c);
            const uint32_t modifiers = key[2];
            const uint32_t code = key[1];
            void* const accelerators = At<void*>(self, win::kAccelerators);
            uint32_t command = 0;
            if (!Virtual<uint8_t>(accelerators, 0x28, code, modifiers, kind == 5 ? uint32_t{1} : uint32_t{0}, &command))
                return 0;
            Virtual<void>(Global<void*>(addr::SC4_Commands), 0x50, command, uint32_t{0}, uint32_t{0});
            return 1;
        }
    } else if (type == kCheatMessage) {
        if (message->a == kShowTimeCheat) {
            Cdecl<void>(addr::SC4UI_F78C590);
            return 1;
        }
    } else if (type == kActivateMessage) {
        if (void* const app = At<void*>(self, win::kApp))
            Virtual<void>(app, 0x30, message->a != 0 ? uint32_t{1} : uint32_t{0});
        if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
            void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
            void* const sent = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
            if (sent)
                AddRef(sent);
            Virtual<void>(sent, 0x14, kActivateMessage);
            Virtual<void>(sent, 0x2C, message->a);
            Virtual<uint8_t>(server, 0xC, sent);
            Release(sent);
            return This<uint8_t>(addr::SC4Win_F99CCF0, self, message);
        }
    }
    return This<uint8_t>(addr::SC4Win_F99CCF0, self, message);
}

// --- Start and shutdown ---------------------------------------------------------------

// The game's 0x004E1060 (thiscall, no arguments; window slot 4). Init; see
// the top of the file. Always true.
//
// Locals the game fills on our stack, sized from the original's frame
// (0x40 bytes of locals): the two temporary names share one plain string
// of 0x14 bytes at the frame's top (made by 0x00408480, not the 0x20-byte
// localized kind), the command line option's value is a plain string of
// 0x14 bytes right below it, the resource key is three dwords and the
// resource's holder one.
//
// As in the original: the service at 0x00B43C9C is not checked for null,
// and the accelerators are loaded only when both the resource and the
// class are there.
uint8_t __fastcall Init(char* self, void*)
{
    if (This<uint8_t>(addr::GZWin_IsInitialized, self))
        return 1;
    This<uint8_t>(addr::GZWin_Init, self);
    {
        GameString name;
        void* const made = This<void*>(addr::SC4String_FromText, &name, "WinSC4App");
        Virtual<void>(self, 0x128, made);
        DestroyString(name);
    }
    Virtual<void>(self, 0x100, kWindowId);
    Virtual<void>(self, 0x110, uint32_t{0x800}, uint32_t{0});
    void* const mainForHeight = Virtual<void*>(self, 0x28);
    void* const mainForWidth = Virtual<void*>(self, 0x28);
    const int32_t height = Virtual<int32_t>(mainForHeight, 0xA8);
    const int32_t width = Virtual<int32_t>(mainForWidth, 0xA4);
    Virtual<void>(self, 0xDC, int32_t{0}, int32_t{0}, width, height);

    uint32_t key[3] = {0xA2E3D533, 0x6A231EAA, 0x6A9362F1};
    void* resource = nullptr;
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, &key[0], uint32_t{0x42E3EA4B}, &resource,
                         uint32_t{0}, uint32_t{0})) {
        void* const com = Cdecl<void*>(addr::RZGetCOM);
        void** const accelerators = &At<void*>(self, win::kAccelerators);
        if (Virtual<uint8_t>(com, 0xC, uint32_t{0x42E967BE}, uint32_t{0xE2C1B3C4}, accelerators))
            Virtual<void>(resource, 0xC, *accelerators);
    }
    This<void>(addr::SC4Stopwatch_Start, self + win::kStopwatch);

    void* const commandLine = Virtual<void*>(Cdecl<void*>(addr::RZGetFramework), 0x54);
    GameString value;
    value.body.begin = nullptr;
    value.body.end = nullptr;
    value.body.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    value.body.capacity = buffer + 8;
    value.body.begin = buffer;
    value.body.end = buffer;
    *buffer = 0;
    value.vtable = addr::SC4String_Vtable;
    value.zero = 0;
    bool found;
    {
        GameString name;
        void* const made = This<void*>(addr::SC4String_FromText, &name, "intro");
        found = Virtual<uint8_t>(commandLine, 0x20, made, &value, uint32_t{1}) != 0;
        DestroyString(name);
    }
    bool skip = false;
    if (found) {
        This<void>(addr::cRZBaseString_MakeLower, &value);
        skip = Cdecl<uint8_t>(addr::SC4String_Equals, &value.body, "off") != 0;
    }
    if (skip)
        ShowScreen118(self, nullptr);
    else
        ShowIntro(self, nullptr);

    void* const service = Global<void*>(addr::SC4_Service43C9C);
    At<void*>(self, win::kService) = service;
    AddRef(service);
    Virtual<uint8_t>(At<void*>(self, win::kService), 0x0, uint32_t{0xC47B747C},
                     &At<void*>(self, win::kServiceInterface));

    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        Virtual<uint8_t>(server, 0x14, static_cast<void*>(self + win::kTarget), kCheatBoxMessage);
    if (void* const cheats = Global<void*>(addr::SC4_CheatCodes)) {
        Virtual<void>(cheats, 0x14, static_cast<void*>(self), uint32_t{0});
        GameString word;
        void* const made =
            This<void*>(addr::SC4String_FromText, &word, Global<const char*>(addr::SC4_CheatNameShowTime));
        Virtual<void>(cheats, 0xC, kShowTimeCheat, made);
        DestroyString(word);
    }
    if (At<uint8_t>(Global<char*>(addr::SC4_Options43CD8), 0x4EE))
        Cdecl<void>(addr::SC4UI_F78C590);

    DestroyString(value);
    if (resource)
        Release(resource);
    return 1;
}

// The game's 0x004E0970 (thiscall, no arguments; window slot 5). Shutdown:
// nothing when the window is not started. Else, with the "started" flag
// down: the accelerators are let go, the window and the cheat word taken
// off the cheat codes, the message subscription ended, the city view (if
// one is up) shut down through its window part and released, the service
// and its interface let go, the object at +0xF4 told (slot 0x1C with the
// string at +0x11C) and let go; then the flag goes up again and the base
// class's shutdown (0x0099D2FE) runs. Always true.
uint8_t __fastcall Shutdown(char* self, void*)
{
    if (!This<uint8_t>(addr::GZWin_IsInitialized, self))
        return 1;
    This<void>(addr::GZWin_SetInitialized, self, uint32_t{0});
    Drop(At<void*>(self, win::kAccelerators));
    if (void* const cheats = Global<void*>(addr::SC4_CheatCodes)) {
        Virtual<void>(cheats, 0x18, static_cast<void*>(self), uint32_t{0});
        Virtual<void>(cheats, 0x10, kShowTimeCheat);
    }
    if (void* const server = Global<void*>(addr::SC4_MessageServer))
        Virtual<uint8_t>(server, 0x18, static_cast<void*>(self + win::kTarget), kCheatBoxMessage);
    if (char* const view = At<char*>(self, win::kCityView)) {
        Virtual<void>(view + kViewWindow, 0x14);
        Release(At<void*>(self, win::kCityView));
        At<void*>(self, win::kCityView) = nullptr;
    }
    Drop(At<void*>(self, win::kService));
    Drop(At<void*>(self, win::kServiceInterface));
    if (void* const object = At<void*>(self, win::kObjectF4)) {
        Virtual<void>(object, 0x1C, static_cast<void*>(self + win::kString));
        Release(At<void*>(self, win::kObjectF4));
        At<void*>(self, win::kObjectF4) = nullptr;
    }
    This<void>(addr::GZWin_SetInitialized, self, uint32_t{1});
    This<void>(addr::SC4Win_F99D2FE, self);
    return 1;
}

// --- The object -----------------------------------------------------------------------

// The game's 0x004E0EE0 (thiscall, ret 4; `this` is the new window, the
// argument the application). The constructor; returns this.
char* __fastcall Construct(char* self, void*, void* app)
{
    This<void*>(addr::GZWin_Construct, self);
    // The message target's base vtable first (shared by many classes).
    At<std::uintptr_t>(self, win::kTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, 0) = addr::SC4MainWindow_Vtable;
    At<std::uintptr_t>(self, win::kTarget) = addr::SC4MainWindow_TargetVtable;
    At<void*>(self, win::kApp) = app;
    At<void*>(self, win::kFieldE0) = nullptr;
    At<void*>(self, win::kService) = nullptr;
    At<void*>(self, win::kServiceInterface) = nullptr;
    At<void*>(self, win::kScreen) = nullptr;
    At<void*>(self, win::kCityView) = nullptr;
    At<void*>(self, win::kObjectF4) = nullptr;
    At<void*>(self, win::kAccelerators) = nullptr;
    This<void*>(addr::SC4Stopwatch_Construct, self + win::kStopwatch, uint32_t{3});
    At<float>(self, win::kFrameRate) = 1.0f;

    GameString& text = At<GameString>(self, win::kString);
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    text.body.begin = buffer;
    text.body.end = buffer;
    text.body.capacity = buffer + 8;
    *buffer = 0;
    text.zero = 0;
    text.vtable = addr::SC4String_Vtable;

    At<uint32_t>(self, win::kFrames) = 0;
    At<void*>(self, win::kObject154) = nullptr;
    for (std::size_t i = 0; i < 8; ++i)
        At<uint32_t>(self, win::kMillions + 4 * i) = 1000000;
    return self;
}

// The game's 0x004E0FF0 (thiscall, no arguments). The destructor: shuts
// the window down, lets the object at +0x154 go, destroys the string and
// the stopwatch, lets the screen go and ends in the window base's
// destructor.
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::SC4MainWindow_Vtable;
    At<std::uintptr_t>(self, win::kTarget) = addr::SC4MainWindow_TargetVtable;
    Shutdown(self, nullptr);
    if (void* const object = At<void*>(self, win::kObject154))
        Release(object);
    DestroyString(At<GameString>(self, win::kString));
    This<void>(addr::SC4Stopwatch_Destruct, self + win::kStopwatch);
    if (void* const screen = At<void*>(self, win::kScreen))
        Release(screen);
    This<void>(addr::GZWin_Destruct, self);
}

// The game's 0x004E1300 (thiscall, ret 4; window slot 148). The deleting
// destructor.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// --- Registration ---------------------------------------------------------------------

bool InstallApp2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[24] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F4E08C0, reinterpret_cast<void*>(&WindowQueryInterface)},
        {addr::SC4UI_F4E0900, reinterpret_cast<void*>(&GetFrameRate)},
        {addr::SC4UI_F4E0910, reinterpret_cast<void*>(&CountFrame)},
        {addr::SC4UI_F4E0960, reinterpret_cast<void*>(&GetCityView)},
        {addr::SC4UI_F4E0970, reinterpret_cast<void*>(&Shutdown)},
        {addr::SC4UI_F4E0A70, reinterpret_cast<void*>(&WindowMessage)},
        {addr::SC4UI_F4E0BB0, reinterpret_cast<void*>(&RunCheatBox)},
        {addr::SC4UI_F4E0C20, reinterpret_cast<void*>(&ShowIntro)},
        {addr::SC4MainWindow_ShowScreen118, reinterpret_cast<void*>(&ShowScreen118)},
        {addr::SC4MainWindow_ShowScreen200, reinterpret_cast<void*>(&ShowScreen200)},
        {addr::SC4UI_F4E0DD0, reinterpret_cast<void*>(&ShowCityView)},
        {addr::SC4MainWindow_DropScreen, reinterpret_cast<void*>(&DropScreen)},
        {addr::SC4UI_F4E0EB0, reinterpret_cast<void*>(&TargetDoMessage)},
        {addr::SC4UI_F4E0EE0, reinterpret_cast<void*>(&Construct)},
        {addr::SC4UI_F4E0FE0, reinterpret_cast<void*>(&TargetQueryInterface)},
        {addr::SC4UI_F4E0FF0, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4UI_F4E1060, reinterpret_cast<void*>(&Init)},
        {addr::SC4UI_F4E1300, reinterpret_cast<void*>(&DeletingDestruct)},
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

const bool g_app2Registered = registry::Register({
    "ui.app.part2",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F4E08C0,
    "Our main window: screens, key accelerators, the cheat box, start and shutdown (docs/ui.md)",
    false,
    InstallApp2,
});

} // namespace
} // namespace opensc4::ui
