// The application's requests, first half (0x00454350 to 0x00454E50): what
// cSC4App does when the player asks for a new city, for the region view, to
// leave the city or to quit, with the questions it asks first.
//
// The application object has its main vtable at +0 (0x00A86C2C, 15 slots), a
// message target at +4 and the interface cISC4App at +0xC (vtable
// 0x00A86AF0); the global 0x00B43C94 holds the interface. The requests are
// interface methods, so their `this` is object + 0xC; the two main slots and
// the helpers get the object itself.
//
// What this part holds:
//
//   - RequestNewCity (interface slot 6): closes the open city after asking,
//     keeps the regional city it was given and makes the city (main slot 13);
//   - RequestGoToRegionView (slot 12), RequestQuit (slot 10) and
//     RequestQuitFromRegion (slot 11): each runs a modal window of layout
//     group 0x96A006B0 (SC4UI::F78E2F0, the window runner of
//     ui_panels_1.cpp) and acts on the button it ended with: 0x8A921F5B
//     saves first, 0x8A921F65 goes on without saving, 0x8A921F70 or
//     anything else gives up. A city that may not be saved (city slot
//     0x1A4) gets another window, which ends with 0xEA57DA59 to go on.
//     Quitting is a message, 0x29A89B05, posted to the application's own
//     message target;
//   - RequestCloseCity (slot 8): a yes or no question (texts 0x2A56CDA8 and
//     0x2A56CD9B of group 0x6A231EAA); yes saves and closes, NO CLOSES
//     WITHOUT SAVING, there is no way back;
//   - SaveCity(fast) (slot 16): asks the city for its file's path and calls
//     SaveCity(path, fast) (slot 15, the game's);
//   - main slot 13, MakeCity: makes the city object, tells it of the
//     regional city, sends the messages around its Init (0x26D31EC0 before,
//     0x2B96B3EA, 0x26D31EC1, 0xAB96B05F, 0xEA8AE29A after) and shows the
//     city view. Only RequestNewCity comes here: LoadCity has its own copy;
//   - main slot 14, Quit: once only; closes the city, takes the screen away
//     and tells the framework to quit;
//   - DestroyCity: the messages around the city's Shutdown (0x26D31EC2,
//     0x26D31EC3), the city released, its file closed;
//   - SaveObliterated: writes the city's file anew through city slot 0x1B4.
//
// Replacement, off by default:
//
//   ui.app.part4: everything named above.
//
// Not functions: 0x00454350 is the jump table of the function before it,
// 0x00454550 is the middle of SaveObliterated (0x00454510).
//
// Still the game's, because cSC4App::LoadCity and cSC4App::SaveCity call
// them and those two stay as they are: 0x004545D0 (writes the application's
// record of a save: version 2, the city, the regional city), 0x00454620
// (reads it back), 0x00454A10 (takes the city's file out of the resource
// manager and closes it), 0x00454B20 (opens a city's file and gives it to
// the resource manager) and 0x00454BB0 (two strings differ; code shared
// with the whole game).
//
// The application, as far as this part touches it (offsets from the
// object): +0x04 the message target, +0x0C the interface, +0x48 a byte set
// when quitting has begun, +0x50 the main window, +0x58 the holder of the
// regional city (a pointer to a pointer, the caller's), +0x5C the city,
// +0x654 the city's open file.
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

namespace app { // offsets from the object
constexpr std::size_t kTarget = 0x04;
constexpr std::size_t kInterface = 0x0C;
constexpr std::size_t kCity = 0x5C;
} // namespace app

namespace iface { // the same fields from the interface (object + 0xC)
constexpr std::size_t kMainWindow = 0x44;
constexpr std::size_t kRegionalCity = 0x4C;
constexpr std::size_t kCity = 0x50;
} // namespace iface

// What the modal windows end with.
constexpr uint32_t kEndSave = 0x8A921F5B;
constexpr uint32_t kEndDoNotSave = 0x8A921F65;
constexpr uint32_t kEndCancel = 0x8A921F70;
constexpr uint32_t kEndGoOn = 0xEA57DA59;

// The windows: instance of the layout, ID of the window.
constexpr uint32_t kNoSaveLayout = 0x4A89B3F2;
constexpr uint32_t kNoSaveWindow = 0x2A96ED21;
constexpr uint32_t kRegionLayout = 0x6A553AA4;
constexpr uint32_t kQuitLayout = 0x0A55161D;
constexpr uint32_t kQuitRegionLayout = 0x4A551B4C;
constexpr uint32_t kAskWindow = 0xAA921F4F;

constexpr uint32_t kQuitMessage = 0x29A89B05;
constexpr uint32_t kAltKey = 0x12;

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

// The game's localized string. 0x20 bytes: RequestCloseCity's frame has the
// two of them at +0x10 and +0x30 of 0x44 bytes of locals, the second ending
// where the frame ends.
struct LocalizedString {
    GameString text;
    uint32_t rest[3];
};
static_assert(sizeof(LocalizedString) == 0x20);

// The scope that 0x005F8630 starts and 0x005F90D0 ends: a number of
// milliseconds and a stopwatch at +8. 0x20 bytes: in both frames that hold
// one it runs from its start to the end of the locals.
struct TimedScope {
    uint32_t words[8];
};
static_assert(sizeof(TimedScope) == 0x20);

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

// The inlined end of a game string.
void DestroyString(GameString& string)
{
    string.vtable = addr::SC4String_Vtable;
    if (string.body.begin)
        Cdecl<void>(addr::SC4_Free, string.body.begin);
}

// Both message pumps of the application (interface slots 4 and 5).
void Pump(void* appInterface)
{
    Virtual<uint8_t>(appInterface, 0x10, uint32_t{100}, uint32_t{500}, uint32_t{5000});
    Virtual<uint8_t>(appInterface, 0x14, uint32_t{100}, uint32_t{500}, uint32_t{5000});
}

// Slot 17 of the framework singleton 0x00913C1A, twice, as the original has
// it before a city is closed.
void TwiceSlot17()
{
    void* service = Cdecl<void*>(addr::SC4_Singleton913C1A);
    Virtual<void>(service, 0x44);
    service = Cdecl<void*>(addr::SC4_Singleton913C1A);
    Virtual<void>(service, 0x44);
}

// Posts the quit message to the application's message target. The server
// is read before the message is made; neither is checked.
void PostQuit(char* appInterface)
{
    void* const server = Global<void*>(addr::SC4_MessageServer);
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
    void* const message = memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
    if (message)
        AddRef(message);
    Virtual<void>(message, 0x14, kQuitMessage);
    char* const object = appInterface - app::kInterface;
    void* const target = object ? object + app::kTarget : nullptr;
    Virtual<uint8_t>(server, 0x1C, message, target);
    Release(message);
}

// The game's 0x00454370, cISC4App::RequestNewCity (interface slot 6).
// (holder of the regional city), ret 4; `this` is the interface. Closes an
// open city after asking; false when it stays open.
uint8_t __fastcall RequestNewCity(char* self, void*, void* regionalCity)
{
    if (At<void*>(self, iface::kCity)) {
        Virtual<uint8_t>(self, 0x20, uint32_t{1});
        if (At<void*>(self, iface::kCity))
            return 0;
    }
    At<void*>(self, iface::kRegionalCity) = regionalCity;
    return Virtual<uint8_t>(self - app::kInterface, 0x34);
}

// The game's 0x004543B0, cISC4App::RequestGoToRegionView (interface slot
// 12). (bool ask), ret 4; `this` is the interface. The original keeps the
// modal guard in the argument's place on the stack.
uint8_t __fastcall RequestGoToRegionView(char* self, void*, uint32_t ask)
{
    if (static_cast<uint8_t>(ask)) {
        char guard[4];
        This<void*>(addr::SC4ModalGuard_Construct, &guard[0], uint32_t{0}, uint32_t{1}, uint32_t{1}, uint32_t{1});
        void* const city = At<void*>(self, iface::kCity);
        if (city && Virtual<uint8_t>(city, 0x1A4)) {
            if (Cdecl<uint32_t>(addr::SC4UI_F78E2F0, kNoSaveLayout, kNoSaveWindow) != kEndGoOn) {
                This<void>(addr::SC4ModalGuard_Destruct, &guard[0]);
                return 0;
            }
        } else {
            const uint32_t ended = Cdecl<uint32_t>(addr::SC4UI_F78E2F0, kRegionLayout, kAskWindow);
            if (ended == kEndSave) {
                Pump(self);
                // The key's byte goes into a local that still holds `this`,
                // and the whole dword is pushed.
                const uint8_t fast = Cdecl<uint8_t>(addr::SC4_IsKeyDown, kAltKey);
                const uint32_t local = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(self));
                const uint32_t pushed = (local & 0xFFFFFF00u) | fast;
                Virtual<uint8_t>(self, 0x24, uint32_t{0}, pushed);
            } else if (ended != kEndDoNotSave) {
                This<void>(addr::SC4ModalGuard_Destruct, &guard[0]);
                return 0;
            }
        }
        This<void>(addr::SC4ModalGuard_Destruct, &guard[0]);
    }
    TwiceSlot17();
    Virtual<uint8_t>(self, 0x38);
    if (!At<void*>(self, iface::kCity)) {
        This<uint8_t>(addr::SC4MainWindow_DropScreen, At<void*>(self, iface::kMainWindow));
        This<uint8_t>(addr::SC4MainWindow_ShowScreen200, At<void*>(self, iface::kMainWindow));
    }
    return 1;
}

// The game's 0x00454510 (0x00454550 is its second half). (path, a game
// string), ret 4; `this` is the object. Opens the file at the path for
// writing and has the city write itself there as obliterated (city slot
// 0x1B4). Always true. The original does not check the new file object
// either.
uint8_t __fastcall SaveObliterated(char* self, void*, void* path)
{
    TimedScope timed;
    This<void*>(addr::SC4TimedScope_Construct, &timed, uint32_t{1000});
    char guard[4];
    This<void*>(addr::SC4ModalGuard_Construct, &guard[0], uint32_t{0}, uint32_t{1}, uint32_t{1}, uint32_t{1});
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x1DC});
    void* const file = memory ? This<void*>(addr::cSC4DBSegmentPackedFile_Construct, memory) : nullptr;
    void* const segment = Virtual<void*>(file, 0xC);
    AddRef(segment);
    if (Virtual<uint8_t>(segment, 0x10)) {
        Virtual<void>(segment, 0x2C, path);
        if (Virtual<uint8_t>(segment, 0x18, uint32_t{0}, uint32_t{1})) {
            Virtual<uint8_t>(At<void*>(self, app::kCity), 0x1B4, segment);
            Virtual<void>(segment, 0x20);
        }
        Virtual<void>(segment, 0x14);
    }
    Release(segment);
    This<void>(addr::SC4ModalGuard_Destruct, &guard[0]);
    This<void>(addr::SC4TimedScope_Destruct, &timed);
    return 1;
}

// The game's 0x004546B0, cISC4App::RequestQuit (interface slot 10).
// (bool ask, bool save), ret 8; `this` is the interface. Without asking,
// `save` decides; a city that may not be saved is never saved.
uint8_t __fastcall RequestQuit(char* self, void*, uint32_t ask, uint32_t save)
{
    char guard[4];
    This<void*>(addr::SC4ModalGuard_Construct, &guard[0], uint32_t{0}, uint32_t{1}, uint32_t{1}, uint32_t{1});
    bool saveFirst = false;
    void* const city = At<void*>(self, iface::kCity);
    if (city && Virtual<uint8_t>(city, 0x1A4)) {
        if (static_cast<uint8_t>(ask)
            && Cdecl<uint32_t>(addr::SC4UI_F78E2F0, kNoSaveLayout, kNoSaveWindow) != kEndGoOn) {
            This<void>(addr::SC4ModalGuard_Destruct, &guard[0]);
            return 0;
        }
    } else if (static_cast<uint8_t>(ask)) {
        const uint32_t ended = Cdecl<uint32_t>(addr::SC4UI_F78E2F0, kQuitLayout, kAskWindow);
        if (ended == kEndSave) {
            saveFirst = true;
        } else if (ended != kEndDoNotSave) {
            This<void>(addr::SC4ModalGuard_Destruct, &guard[0]);
            return 0;
        }
    } else {
        saveFirst = static_cast<uint8_t>(save) != 0;
    }
    if (saveFirst) {
        Pump(self);
        // The key's byte goes into the first argument's place, and the
        // whole dword is pushed.
        const uint8_t fast = Cdecl<uint8_t>(addr::SC4_IsKeyDown, kAltKey);
        Virtual<uint8_t>(self, 0x24, uint32_t{0}, (ask & 0xFFFFFF00u) | fast);
    }
    PostQuit(self);
    This<void>(addr::SC4ModalGuard_Destruct, &guard[0]);
    return 1;
}

// The game's 0x00454800, cISC4App::RequestQuitFromRegion (interface slot
// 11). (bool ask), ret 4; `this` is the interface. Only the cancel button
// stops it: a window that did not run (0) quits.
uint8_t __fastcall RequestQuitFromRegion(char* self, void*, uint32_t ask)
{
    if (static_cast<uint8_t>(ask)) {
        char guard[4];
        This<void*>(addr::SC4ModalGuard_Construct, &guard[0], uint32_t{0}, uint32_t{1}, uint32_t{1}, uint32_t{1});
        const uint32_t ended = Cdecl<uint32_t>(addr::SC4UI_F78E2F0, kQuitRegionLayout, kAskWindow);
        This<void>(addr::SC4ModalGuard_Destruct, &guard[0]);
        if (ended == kEndCancel)
            return 0;
    }
    PostQuit(self);
    return 1;
}

// The game's 0x00454C10, cISC4App::RequestCloseCity (interface slot 8).
// (bool ask), ret 4; `this` is the interface. False with no city. Yes
// saves (with the notice; fast with the Alt key down) and closes only when
// the save worked; no closes without saving.
uint8_t __fastcall RequestCloseCity(char* self, void*, uint32_t ask)
{
    char guard[4];
    This<void*>(addr::SC4ModalGuard_Construct, &guard[0], uint32_t{0}, uint32_t{1}, uint32_t{1}, uint32_t{1});
    if (!At<void*>(self, iface::kCity)) {
        This<void>(addr::SC4ModalGuard_Destruct, &guard[0]);
        return 0;
    }
    if (static_cast<uint8_t>(ask)) {
        LocalizedString title;
        LocalizedString text;
        This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &title, uint32_t{0x6A231EAA}, uint32_t{0x2A56CDA8},
                    uint32_t{1});
        This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, uint32_t{0x6A231EAA}, uint32_t{0x2A56CD9B},
                    uint32_t{1});
        if (Cdecl<uint8_t>(addr::SC4UI_QuestionDialog, &title, &text, uint32_t{0}, uint32_t{0})) {
            // The key's byte goes into the argument's place, and the whole
            // dword is pushed.
            const uint8_t fast = Cdecl<uint8_t>(addr::SC4_IsKeyDown, kAltKey);
            uint8_t closed = 0;
            if (Virtual<uint8_t>(self, 0x24, uint32_t{1}, (ask & 0xFFFFFF00u) | fast))
                closed = Virtual<uint8_t>(self, 0x38);
            DestroyString(text.text);
            DestroyString(title.text);
            This<void>(addr::SC4ModalGuard_Destruct, &guard[0]);
            return closed;
        }
        DestroyString(text.text);
        DestroyString(title.text);
    }
    const uint8_t closed = Virtual<uint8_t>(self, 0x38);
    This<void>(addr::SC4ModalGuard_Destruct, &guard[0]);
    return closed;
}

bool InstallApp4(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[16] = {};
    const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F454370, reinterpret_cast<void*>(&RequestNewCity)},
        {addr::SC4UI_F4543B0, reinterpret_cast<void*>(&RequestGoToRegionView)},
        {addr::SC4UI_F454510, reinterpret_cast<void*>(&SaveObliterated)},
        {addr::SC4UI_F4546B0, reinterpret_cast<void*>(&RequestQuit)},
        {addr::SC4UI_F454800, reinterpret_cast<void*>(&RequestQuitFromRegion)},
        {addr::SC4UI_F454C10, reinterpret_cast<void*>(&RequestCloseCity)},
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

const bool g_app4Registered = registry::Register({
    "ui.app.part4",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F454370,
    "Our application requests, first half: new city, region view, close, quit and their questions (docs/ui.md)",
    false,
    InstallApp4,
});

} // namespace
} // namespace opensc4::ui
