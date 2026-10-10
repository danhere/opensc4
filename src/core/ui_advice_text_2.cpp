// The advice window's code, second half: functions between 0x0041F2D0 and
// 0x00420D30. Replacement, off by default:
//
//   ui.advice_text.part2: the rest of the advice window's controller (the
//       helper object the advisor system keeps at +0x5C: choosing an advisor,
//       switching between its advice and its news, its window procedure and
//       its message handler) and the advisor head view (the window procedure
//       that shows an advisor's animated head model in a small window).
//
// The controller (0x70 bytes, vtables 0x00A83548 and 0x00A83538):
//   +0x00 window procedure vtable, +0x04 message target vtable, +0x08 count,
//   +0x0C the chosen advisor, +0x10 its type (0x15 to 0x1B, -1 none),
//   +0x14 its index in the type table 0x00A83508 (-1 none), +0x18 the object
//   whose slot 0xC gives the advice window, +0x1C seven head windows,
//   +0x38 seven head views (by index), +0x54 registered for messages,
//   +0x55 showing news, +0x58 list mode (1 advice, 2 news), +0x5C the list.
//
// The head view (0xF8 bytes, vtables 0x00A83AC0 and 0x00A83AB0):
//   +0x00 window procedure vtable, +0x04 message target vtable, +0x08 count,
//   +0x0C set up, +0x0D active, +0x10 mood (0 to 3), +0x14 the idle model's
//   ID, +0x18 the idle model, +0x1C the second model's ID, +0x20 the second
//   model, +0x24 the model shown, +0x28 its window, +0x2C the window's area
//   (left, top, right, bottom), +0x3C and +0x3E the main window's size,
//   +0x40 the camera, +0x44 the advisor's type, +0x48 the projection
//   (transposed), +0xC8 the model's last state, +0xCC and +0xD0 the count
//   of idle loops and its target, +0xD8 a random generator, +0xF0 an offset.
// The model pointers point at a class with a virtual base: the reference
// count and cIS3DModelInstance are at pointer + 4 + the offset in its table.
//
// The drawing function (0x004209D0), the shared smart pointer helpers and
// a thunk folded with other classes stay the game's. Functions before
// 0x0041F2D0 are another part's and are called by address.
#include "core/address_check.h"
#include "core/fixed_seed.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::ui {
namespace {

namespace helper {
constexpr std::size_t kAdvisor = 0xC;
constexpr std::size_t kType = 0x10;
constexpr std::size_t kIndex = 0x14;
constexpr std::size_t kWindowHolder = 0x18;
constexpr std::size_t kHeadViews = 0x38;
constexpr std::size_t kNews = 0x55;
constexpr std::size_t kListMode = 0x58;
constexpr std::size_t kList = 0x5C;
} // namespace helper

namespace view {
constexpr std::size_t kSetUp = 0xC;
constexpr std::size_t kActive = 0xD;
constexpr std::size_t kMood = 0x10;
constexpr std::size_t kIdleId = 0x14;
constexpr std::size_t kIdleModel = 0x18;
constexpr std::size_t kSecondId = 0x1C;
constexpr std::size_t kSecondModel = 0x20;
constexpr std::size_t kShown = 0x24;
constexpr std::size_t kWindow = 0x28;
constexpr std::size_t kArea = 0x2C;
constexpr std::size_t kMainWidth = 0x3C;
constexpr std::size_t kMainHeight = 0x3E;
constexpr std::size_t kCamera = 0x40;
constexpr std::size_t kAdvisorType = 0x44;
constexpr std::size_t kProjection = 0x48;
constexpr std::size_t kLastState = 0xC8;
constexpr std::size_t kLoops = 0xCC;
constexpr std::size_t kLoopTarget = 0xD0;
constexpr std::size_t kRandom = 0xD8;
constexpr std::size_t kOffset = 0xF0;
} // namespace view

// Window messages (the type at +0, then three values).
constexpr uint32_t kWinCommand = 3;
constexpr uint32_t kWinPaint = 0xA2BF8ACE;
constexpr uint32_t kWinInit = 0xA2BF8AD5;
constexpr uint32_t kWinShutdown = 0xA2BF8AD6;

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

void FreeIfSet(void* memory)
{
    if (memory)
        Cdecl<void>(addr::SC4_Free, memory);
}

// The advice window: the parent of the window the holder gives.
void* AdviceWindow(void* holder)
{
    void* const window = Virtual<void*>(holder, 0xC);
    return Virtual<void*>(window, 0x2C);
}

// The reference counted part of a model pointer (a virtual base).
char* ModelBase(void* model)
{
    char* const start = static_cast<char*>(model);
    return start + At<int32_t>(At<char*>(start, 4), 4) + 4;
}

// Empties a model pointer of the head view.
void DropModel(char* self, std::size_t offset)
{
    void* const model = At<void*>(self, offset);
    if (model) {
        At<void*>(self, offset) = nullptr;
        Virtual<uint32_t>(ModelBase(model), 0x8);
    }
}

// Empties an ordinary counted pointer.
void DropRef(void** ref)
{
    void* const object = *ref;
    if (object) {
        *ref = nullptr;
        Virtual<uint32_t>(object, 0x8);
    }
}

// ---------------------------------------------------------------------------
// The controller.

// The game's 0x0041F2D0. `this` is the controller. Chooses the advisor of a
// type (0x15 to 0x1B): closes the head views, keeps the advisor, shows the
// advice or the news list, makes the advisor's head view, sets the window's
// titles to "name - personal name" and fills the window.
void __fastcall ShowAdvisor(char* self, void*, uint32_t type)
{
    uint32_t index = 0;
    while (Global<uint32_t>(addr::SC4AdvisorHelper_AdvisorTypes + index * 4) != type) {
        if (++index >= 7)
            return;
    }
    void* const system = Virtual<void*>(Global<void*>(addr::SC4_City), 0x144);
    void* const parent = AdviceWindow(At<void*>(self, helper::kWindowHolder));
    void* button = Virtual<void*>(parent, 0x8C, uint32_t{0x6A15C767});
    This<void>(addr::SC4UI_F41D530, self);
    void* const advisor = Virtual<void*>(system, 0x1C, type);
    void* const old = At<void*>(self, helper::kAdvisor);
    if (advisor != old) {
        if (advisor)
            Virtual<uint32_t>(advisor, 0x4);
        At<void*>(self, helper::kAdvisor) = advisor;
        if (old)
            Virtual<uint32_t>(old, 0x8);
    }
    At<uint32_t>(self, helper::kType) = type;
    At<uint32_t>(self, helper::kIndex) = index;
    if (button)
        Virtual<uint32_t>(button, 0x110, int32_t{1}, int32_t{0});

    void** const list = &At<void*>(self, helper::kList);
    if (At<uint8_t>(self, helper::kNews) != 0) {
        button = Virtual<void*>(parent, 0x8C, uint32_t{0x2A1D96B1});
        At<uint32_t>(self, helper::kListMode) = 2;
        if (*list == nullptr)
            Virtual<uint8_t>(button, 0x94, uint32_t{0x100101}, uint32_t{0xCA1492A2}, list);
        This<void>(addr::SC4UI_F41DCE0, self, At<uint32_t>(self, helper::kType), uint32_t{0xAA1D97DF});
    } else {
        button = Virtual<void*>(parent, 0x8C, uint32_t{0xAA15EF06});
        At<uint32_t>(self, helper::kListMode) = 1;
        if (*list == nullptr)
            Virtual<uint8_t>(button, 0x94, uint32_t{0x100100}, uint32_t{0xCA1492A2}, list);
        This<void>(addr::SC4UI_F41DCE0, self, type, uint32_t{0xEA15EFC7});
    }
    if (button)
        Virtual<uint32_t>(button, 0x110, int32_t{1}, int32_t{1});

    using RangeFn = void(__thiscall*)(void*, char*, char*, void*);
    // The length doubles as the allocator argument of the string helpers.
    void* const personal = Virtual<void*>(At<void*>(self, helper::kAdvisor), 0x20);
    uint32_t length = Virtual<uint32_t>(personal, 0x1C);
    char* chars = Virtual<char*>(personal, 0x18);
    alignas(4) char personalName[0x14];
    At<uint32_t>(personalName, 0x4) = 0;
    At<uint32_t>(personalName, 0x8) = 0;
    At<uint32_t>(personalName, 0xC) = 0;
    reinterpret_cast<RangeFn>(addr::SC4String_FromRange)(personalName + 4, chars, chars + length, &length);
    At<uint32_t>(personalName, 0x0) = addr::SC4String_Vtable;
    At<uint32_t>(personalName, 0x10) = 0;

    void* const name = Virtual<void*>(At<void*>(self, helper::kAdvisor), 0x18);
    length = Virtual<uint32_t>(name, 0x1C);
    chars = Virtual<char*>(name, 0x18);
    alignas(4) char advisorName[0x14];
    At<uint32_t>(advisorName, 0x4) = 0;
    At<uint32_t>(advisorName, 0x8) = 0;
    At<uint32_t>(advisorName, 0xC) = 0;
    reinterpret_cast<RangeFn>(addr::SC4String_FromRange)(advisorName + 4, chars, chars + length, &length);
    At<uint32_t>(advisorName, 0x0) = addr::SC4String_Vtable;
    At<uint32_t>(advisorName, 0x10) = 0;

    alignas(4) char withDash[0xC];
    alignas(4) char whole[0xC];
    char* const first = Cdecl<char*>(addr::SC4String_ConcatBodyText, static_cast<void*>(withDash),
                                     static_cast<void*>(advisorName + 4), " - ");
    char* const second = Cdecl<char*>(addr::cSC4Advice_F408AE0, static_cast<void*>(whole),
                                      static_cast<void*>(first), static_cast<void*>(personalName + 4));
    alignas(4) char title[0x14];
    At<uint32_t>(title, 0x4) = 0;
    At<uint32_t>(title, 0x8) = 0;
    At<uint32_t>(title, 0xC) = 0;
    char* const secondEnd = At<char*>(second, 4);
    reinterpret_cast<RangeFn>(addr::cSC4Advice_F408420)(title + 4, At<char*>(second, 0), secondEnd, &length);
    At<uint32_t>(title, 0x0) = addr::SC4String_Vtable;
    At<uint32_t>(title, 0x10) = 0;
    FreeIfSet(At<void*>(whole, 0));
    FreeIfSet(At<void*>(withDash, 0));
    FreeIfSet(At<void*>(advisorName, 4));
    FreeIfSet(At<void*>(personalName, 4));

    void* caption = Virtual<void*>(parent, 0x8C, uint32_t{0x2A15F5E8});
    Virtual<uint8_t>(caption, 0x128, static_cast<void*>(title));
    caption = Virtual<void*>(parent, 0x8C, uint32_t{0xEA1D980D});
    Virtual<uint8_t>(caption, 0x128, static_cast<void*>(title));
    Virtual<uint8_t>(button, 0x128, static_cast<void*>(title));
    This<void>(addr::SC4UI_F41E000, self, At<void*>(self, helper::kAdvisor));
    This<void>(addr::SC4UI_F41E4B0, self);
    void* const titleBuffer = At<void*>(title, 4);
    At<uint32_t>(title, 0x0) = addr::SC4String_Vtable;
    FreeIfSet(titleBuffer);
}

// Switches the window between the advice list and the news list: hides one
// button, shows the other, takes the list control from it, drops the head
// view of the advisor and makes it again, and fills the window.
void SwitchList(char* self, uint32_t hideId, uint32_t showId, uint32_t listInterface, uint32_t mode, uint8_t news,
                uint32_t headWindowId)
{
    if (At<uint32_t>(self, helper::kIndex) >= 7)
        return;
    void* const parent = AdviceWindow(At<void*>(self, helper::kWindowHolder));
    void* const hidden = Virtual<void*>(parent, 0x8C, hideId);
    if (hidden)
        Virtual<uint32_t>(hidden, 0x110, int32_t{1}, int32_t{0});
    void* const shown = Virtual<void*>(parent, 0x8C, showId);
    if (shown)
        Virtual<uint32_t>(shown, 0x110, int32_t{1}, int32_t{1});
    void** const list = &At<void*>(self, helper::kList);
    if (*list) {
        Virtual<uint32_t>(*list, 0x1C);
        DropRef(list);
    }
    if (*list == nullptr)
        Virtual<uint8_t>(shown, 0x94, listInterface, uint32_t{0xCA1492A2}, list);
    const uint32_t type = At<uint32_t>(self, helper::kType);
    At<uint32_t>(self, helper::kListMode) = mode;
    At<uint8_t>(self, helper::kNews) = news;
    This<void>(addr::SC4UI_F41D4D0, self, type);
    This<void>(addr::SC4UI_F41DCE0, self, At<uint32_t>(self, helper::kType), headWindowId);
    void* const system = Virtual<void*>(Global<void*>(addr::SC4_City), 0x144);
    void* const advisor = Virtual<void*>(system, 0x1C, At<uint32_t>(self, helper::kType));
    This<void>(addr::SC4UI_F41E000, self, advisor);
    This<void>(addr::SC4UI_F41E4B0, self);
}

// The game's 0x0041F610. `this` is the controller. Shows the news list.
void __fastcall ShowNews(char* self, void*)
{
    SwitchList(self, 0xAA15EF06, 0x2A1D96B1, 0x100101, 2, 1, 0xAA1D97DF);
}

// The game's 0x0041F720. `this` is the controller. Shows the advice list.
void __fastcall ShowAdvice(char* self, void*)
{
    SwitchList(self, 0x2A1D96B1, 0xAA15EF06, 0x100100, 1, 0, 0xEA15EFC7);
}

// The game's 0x0041F830. `this` is the controller (slot 3 of its window
// procedure vtable). Messages of the advice window: at set up it registers
// for the advisors' messages and becomes the notification target of the
// window's controls; commands are the buttons (code 0x287259F6) and a
// value for a head view (code 0x287259F8). Always answers false.
uint8_t __fastcall HelperWinMessage(char* self, void*, void* window, uint32_t* message)
{
    if (window != Virtual<void*>(At<void*>(self, helper::kWindowHolder), 0xC))
        return 0;
    Cdecl<uint8_t>(addr::SC4Tutorial_FilterWinMessage, message);
    const uint32_t type = message[0];
    if (type == kWinInit) {
        void* const own = Virtual<void*>(At<void*>(self, helper::kWindowHolder), 0xC);
        alignas(4) char text[0x14]; // made and freed, never used
        This<void*>(addr::SC4String_SC4String, text);
        This<void>(addr::SC4UI_F41D580, self, int32_t{1});
        void* const parent = Virtual<void*>(own, 0x2C);
        static const uint32_t controls[] = {
            0xCA19DD28, 0x2A84B45A, 0x8A15EFE6, 0x0A15EFBC, 0xAA1D9734, 0x2A84B45B, 0x2A1D973E, 0x6A1D97D5,
            0x4A15EFDE, 0xCA1D974A, 0xAA15C795, 0x6A15C7AA, 0x6A15C7BE, 0xCA15C7CF, 0xAA15C7E2, 0x2A15C7F1,
            0x8A15C802, 0xAA1D969D, 0x6A1D96C3, 0x6A19D8F2, 0xAA19DC53, 0x0A1D96CC,
        };
        for (const uint32_t id : controls) {
            void* const control = Virtual<void*>(parent, 0x8C, id);
            Virtual<uint8_t>(control, 0x158, own);
        }
        FreeIfSet(At<void*>(text, 4));
        return 0;
    }
    if (type == kWinShutdown) {
        This<void>(addr::SC4UI_F41D580, self, int32_t{0});
        return 0;
    }
    if (type != kWinCommand)
        return 0;

    const uint32_t code = message[1];
    const uint32_t id = message[2];
    if (code == 0x287259F8) {
        std::size_t offset;
        switch (id) {
        case 0xAA15C795: offset = helper::kHeadViews + 0x00; break;
        case 0x6A15C7AA: offset = helper::kHeadViews + 0x04; break;
        case 0x6A15C7BE: offset = helper::kHeadViews + 0x08; break;
        case 0xCA15C7CF: offset = helper::kHeadViews + 0x0C; break;
        case 0xAA15C7E2: offset = helper::kHeadViews + 0x10; break;
        case 0x2A15C7F1: offset = helper::kHeadViews + 0x14; break;
        case 0x8A15C802: offset = helper::kHeadViews + 0x18; break;
        default: return 0;
        }
        void* const headView = At<void*>(self, offset);
        if (headView)
            Virtual<void>(headView, 0x1C, message[3]);
        return 0;
    }
    if (code != 0x287259F6)
        return 0;
    switch (id) {
    case 0x6A1D97D5:
    case 0x8A15EFE6:
    case 0x2A1D973E:
    case 0x0A15EFBC:
        This<void>(addr::SC4UI_F41E1C0, self);
        break;
    case 0xAA19DC53:
    case 0x6A19D8F2:
    case 0x0A1D96CC:
        This<void>(addr::SC4AdvisorHelper_Call2, self);
        break;
    case 0xAA1D9734:
    case 0x2A84B45B:
        ShowAdvice(self, nullptr);
        break;
    case 0xCA19DD28:
    case 0x2A84B45A:
        ShowNews(self, nullptr);
        break;
    case 0x6A15C7AA: ShowAdvisor(self, nullptr, 0x15); break;
    case 0x6A15C7BE: ShowAdvisor(self, nullptr, 0x16); break;
    case 0xCA15C7CF: ShowAdvisor(self, nullptr, 0x17); break;
    case 0xAA15C7E2: ShowAdvisor(self, nullptr, 0x18); break;
    case 0xAA15C795: ShowAdvisor(self, nullptr, 0x19); break;
    case 0x2A15C7F1: ShowAdvisor(self, nullptr, 0x1A); break;
    case 0x8A15C802: ShowAdvisor(self, nullptr, 0x1B); break;
    default: break;
    }
    return 0;
}

// The advice a message carries (its object's interface 0x8A09F5F5), when it
// is one of the chosen advisor's; `ref` keeps the advice either way.
bool OwnAdvice(char* target, void* message, void** ref)
{
    void* const source = Virtual<void*>(message, 0x80);
    DropRef(ref);
    if (!Virtual<uint8_t>(source, 0x0, uint32_t{0x8A09F5F5}, ref))
        return false;
    return Virtual<uint32_t>(*ref, 0x2C) == At<uint32_t>(target, helper::kType - 4);
}

// The game's 0x0041FD10. `this` is the controller's message target (+4).
// Advice added (0x8A09FA64) and removed (0xCA09FA6C) go to the lists,
// advice changes (0x8A09FA65, 0x8A09FA67) light two controls, and each of
// them fills the window again; 0x426841A1 passes a mood to the head view of
// an advisor type. Always answers true.
uint8_t __fastcall HelperMessage(char* target, void*, void* message)
{
    char* const self = target - 4;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type == 0x426841A1) {
        const uint32_t which = Virtual<uint32_t>(message, 0x24) - 0x15;
        if (which > 6)
            return 1;
        static const std::size_t offsets[7] = {0x3C, 0x40, 0x44, 0x48, 0x38, 0x4C, 0x50};
        void* const headView = At<void*>(self, offsets[which]);
        if (!headView)
            return 1;
        void** const vtable = *static_cast<void***>(headView);
        const uint32_t mood = Virtual<uint32_t>(message, 0x38);
        reinterpret_cast<void(__thiscall*)(void*, uint32_t)>(vtable[0x20 / 4])(headView, mood);
        return 1;
    }
    if (type != 0x8A09FA64 && type != 0x8A09FA65 && type != 0x8A09FA67 && type != 0xCA09FA6C)
        return 1;

    void* advice = nullptr;
    if (OwnAdvice(target, message, &advice)) {
        if (type == 0x8A09FA64) {
            This<void>(addr::SC4UI_F41DA00, self, advice);
        } else if (type == 0xCA09FA6C) {
            This<void>(addr::SC4UI_F41D9B0, self, advice);
        } else {
            void* const parent = AdviceWindow(At<void*>(self, helper::kWindowHolder));
            void* control = Virtual<void*>(parent, 0x8C, uint32_t{0xAA1D969D});
            Virtual<uint32_t>(control, 0x110, int32_t{2}, int32_t{1});
            control = Virtual<void*>(parent, 0x8C, uint32_t{0x6A1D96C3});
            Virtual<uint32_t>(control, 0x110, int32_t{2}, int32_t{1});
        }
    }
    This<void>(addr::SC4UI_F41DF90, self);
    This<void>(addr::SC4UI_F41E4B0, self);
    if (advice)
        Virtual<uint32_t>(advice, 0x8);
    return 1;
}

// ---------------------------------------------------------------------------
// The head view.

// The game's 0x00420030. `this` is the head view. QueryInterface: the
// window procedure (0x22E85D8E) and 1 at +0, the message target
// (0x452294AA) at +4.
uint8_t __fastcall ViewQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == 0x22E85D8E) {
        *out = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    if (iid == 0x452294AA) {
        *out = self ? self + 4 : nullptr;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    if (iid == 1) {
        *out = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    return 0;
}

// The game's 0x004202E0. `this` is the head view's message target (+4).
uint8_t __fastcall ViewQueryInterface4(char* target, void*, uint32_t iid, void** out)
{
    return ViewQueryInterface(target - 4, nullptr, iid, out);
}

// The game's 0x004200A0. `this` is the head view (slot 6). Turns the view
// on or off: the frame service (0x86AD10EC) sends its message target the
// tick while it is on, and the models are loaded or dropped (slot 11).
void __fastcall ViewSetActive(char* self, void*, uint32_t active)
{
    const uint8_t on = static_cast<uint8_t>(active);
    if (At<uint8_t>(self, view::kActive) == on)
        return;
    At<uint8_t>(self, view::kActive) = on;
    if (on) {
        if (void* const service = Cdecl<void*>(addr::SC4_GetService86AD10EC))
            Virtual<uint8_t>(service, 0x14, static_cast<void*>(self + 4), uint32_t{0xC0});
        Virtual<void>(self, 0x2C, int32_t{1});
    } else {
        if (void* const service = Cdecl<void*>(addr::SC4_GetService86AD10EC))
            Virtual<uint8_t>(service, 0x18, static_cast<void*>(self + 4), uint32_t{0xC0});
        Virtual<void>(self, 0x2C, int32_t{0});
    }
}

// The game's 0x00420100. `this` is the head view's message target (+4).
// The frame tick (0x86AD10EE) advances the model (slot 13) by the time the
// message carries. Always answers true.
uint8_t __fastcall ViewMessage(char* target, void*, void* message)
{
    if (Virtual<uint32_t>(message, 0x10) == 0x86AD10EE) {
        const uint32_t elapsed = Virtual<uint32_t>(message, 0x4C);
        Virtual<void>(target - 4, 0x34, elapsed);
    }
    return 1;
}

// The game's 0x00420140. `this` is the head view (slot 9). Sets the mood
// from an advice mood (0 to 5; other values are kept as they are) and loads
// the mood's models when it changed.
void __fastcall ViewSetMood(char* self, void*, uint32_t mood)
{
    uint32_t value = mood;
    switch (mood) {
    case 0: value = 3; break;
    case 1: value = 2; break;
    case 3: value = 0; break;
    case 5: value = 1; break;
    default: break;
    }
    if (At<uint32_t>(self, view::kMood) == value)
        return;
    const uint32_t active = At<uint8_t>(self, view::kActive);
    At<uint32_t>(self, view::kMood) = value;
    At<uint32_t>(self, view::kLoops) = 0;
    Virtual<void>(self, 0x2C, active);
}

// The game's 0x00420270. `this` is the head view. The destructor's body.
void __fastcall ViewDestruct(char* self, void*)
{
    At<uint32_t>(self, 0x0) = addr::SC4UI_HeadViewVtable;
    At<uint32_t>(self, 0x4) = addr::SC4UI_HeadViewVtable4;
    if (void* const camera = At<void*>(self, view::kCamera))
        Virtual<uint32_t>(camera, 0x8);
    if (void* const window = At<void*>(self, view::kWindow))
        Virtual<uint32_t>(window, 0x8);
    if (void* const model = At<void*>(self, view::kShown))
        Virtual<uint32_t>(ModelBase(model), 0x8);
    if (void* const model = At<void*>(self, view::kSecondModel))
        Virtual<uint32_t>(ModelBase(model), 0x8);
    if (void* const model = At<void*>(self, view::kIdleModel))
        Virtual<uint32_t>(ModelBase(model), 0x8);
}

// x times the cosine (sine) of an angle as the game's FCOS (FSIN) and FMUL
// leave it: the cosine stays in the register, it is not rounded to a double
// before the multiplication.
double TimesCos(double angle, float x)
{
    double result;
    __asm {
        fld angle
        fcos
        fmul x
        fstp result
    }
    return result;
}

double TimesSin(double angle, float x)
{
    double result;
    __asm {
        fld angle
        fsin
        fmul x
        fstp result
    }
    return result;
}

// a - b and a + b stored as floats, done in the x87 like the game's (the
// result does not depend on the precision setting of the caller then).
float DifferenceAsFloat(double a, double b)
{
    float result;
    __asm {
        fld a
        fsub b
        fstp result
    }
    return result;
}

float SumAsFloat(double a, double b)
{
    float result;
    __asm {
        fld a
        fadd b
        fstp result
    }
    return result;
}

// The game's 0x004202F0. `this` is the head view. Set up for a window (the
// window procedure's set up message): keeps the window and its area, makes
// a camera with a box as large as the window, 400 deep either way, keeps
// the projection transposed, and turns the camera's transform half a turn
// (the float pi) about its second axis. Answers true.
uint8_t __fastcall ViewInit(char* self, void*, void* window)
{
    if (At<uint8_t>(self, view::kSetUp) != 0)
        return 1;
    At<uint8_t>(self, view::kSetUp) = 1;
    void* const oldWindow = At<void*>(self, view::kWindow);
    if (window != oldWindow) {
        if (window)
            Virtual<uint32_t>(window, 0x4);
        At<void*>(self, view::kWindow) = window;
        if (oldWindow)
            Virtual<uint32_t>(oldWindow, 0x8);
    }
    const int32_t* const area = Virtual<const int32_t*>(window, 0xC8);
    int32_t* const own = &At<int32_t>(self, view::kArea);
    own[0] = area[0];
    own[1] = area[1];
    own[2] = area[2];
    own[3] = area[3];
    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    void* const mainWindow = Virtual<void*>(manager, 0xC);
    At<uint16_t>(self, view::kMainWidth) = static_cast<uint16_t>(Virtual<uint32_t>(mainWindow, 0xA4));
    At<uint16_t>(self, view::kMainHeight) = static_cast<uint16_t>(Virtual<uint32_t>(mainWindow, 0xA8));

    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x1A8});
    void* const camera = memory ? This<void*>(addr::cS3DCamera_cS3DCamera, memory) : nullptr;
    void* const oldCamera = At<void*>(self, view::kCamera);
    if (camera != oldCamera) {
        if (camera)
            Virtual<uint32_t>(camera, 0x4);
        At<void*>(self, view::kCamera) = camera;
        if (oldCamera)
            Virtual<uint32_t>(oldCamera, 0x8);
    }
    This<uint8_t>(addr::SC4_ReturnTrue648F00, At<void*>(self, view::kCamera));
    const float height = static_cast<float>(own[3] - own[1]);
    const float width = static_cast<float>(own[2] - own[0]);
    This<void>(addr::cS3DCamera_SetFrustum, At<void*>(self, view::kCamera), 0.0f, width, 0.0f, height, -400.0f,
               400.0f);

    char* const projection = This<char*>(addr::cS3DCamera_GetProjection, At<void*>(self, view::kCamera));
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column)
            At<uint32_t>(self, view::kProjection + row * 16 + column * 4) =
                At<uint32_t>(projection, row * 4 + column * 16);
    }

    // The camera's transform: a change count, a flag byte, a 3 by 3 matrix
    // at +4 (m0 to m8), a position and a scale.
    char* const transform = This<char*>(addr::cS3DCamera_GetTransform, At<void*>(self, view::kCamera));
    const double angle = Global<double>(addr::SC4UI_HeadViewTurn);
    const float m0 = At<float>(transform, 0x04);
    const float m2 = At<float>(transform, 0x0C);
    const float m3 = At<float>(transform, 0x10);
    const float m5 = At<float>(transform, 0x18);
    const float m6 = At<float>(transform, 0x1C);
    const float m8 = At<float>(transform, 0x24);
    const float m5Sin = static_cast<float>(TimesSin(angle, m5));
    const float m8Sin = static_cast<float>(TimesSin(angle, m8));
    const double m0Cos = TimesCos(angle, m0);
    const float m3Cos = static_cast<float>(TimesCos(angle, m3));
    const float m6Cos = static_cast<float>(TimesCos(angle, m6));
    const float newM0 = DifferenceAsFloat(m0Cos, TimesSin(angle, m2));
    const float newM3 = DifferenceAsFloat(m3Cos, m5Sin);
    const float newM6 = DifferenceAsFloat(m6Cos, m8Sin);
    At<float>(transform, 0x1C) = newM6;
    At<float>(transform, 0x04) = newM0;
    At<float>(transform, 0x10) = newM3;
    const float m2Cos = static_cast<float>(TimesCos(angle, m2));
    const float m5Cos = static_cast<float>(TimesCos(angle, m5));
    const float m8Cos = static_cast<float>(TimesCos(angle, m8));
    const float m0Sin = static_cast<float>(TimesSin(angle, m0));
    const float m3Sin = static_cast<float>(TimesSin(angle, m3));
    const double m6Sin = TimesSin(angle, m6);
    At<float>(transform, 0x24) = SumAsFloat(m6Sin, m8Cos);
    At<float>(transform, 0x0C) = SumAsFloat(m0Sin, m2Cos);
    At<float>(transform, 0x18) = SumAsFloat(m3Sin, m5Cos);
    At<uint8_t>(transform, 1) |= 4;
    ++At<uint8_t>(transform, 0);
    return 1;
}

// The game's 0x00420540. `this` is the head view. The window procedure's
// shut down message: turns the view off and lets go of the window, the
// camera and the models. Answers true.
uint8_t __fastcall ViewShutdown(char* self, void*, void*)
{
    if (At<uint8_t>(self, view::kSetUp) == 0)
        return 1;
    At<uint8_t>(self, view::kSetUp) = 0;
    Virtual<void>(self, 0x18, int32_t{0});
    DropRef(&At<void*>(self, view::kWindow));
    DropRef(&At<void*>(self, view::kCamera));
    DropModel(self, view::kIdleModel);
    DropModel(self, view::kSecondModel);
    DropModel(self, view::kShown);
    return 1;
}

// Gets a model resource by key and has it make an instance in `slot`; false
// when there is no such resource. `resource` keeps what the manager gave.
bool MakeModel(void* manager, uint32_t* key, void** resource, void** slot)
{
    if (!Virtual<uint8_t>(manager, 0xC, key, uint32_t{0x9AD0E7E3}, resource, int32_t{0}, int32_t{0}))
        return false;
    if (Virtual<uint8_t>(*resource, 0x10, int32_t{0}, slot))
        Virtual<uint32_t>(*slot, 0x50, int32_t{1});
    return true;
}

// The game's 0x004205D0. `this` is the head view (slot 11). With true,
// loads the two head models of the advisor's mood (the advisor's slot 0x44
// gives their IDs for the numbers in two tables) unless the first is loaded
// already, and shows the first; with false, drops both. With the
// DollyLlama byte set, the models with ID + 0x100 are tried first.
void __fastcall ViewLoadModels(char* self, void*, uint32_t load)
{
    if (static_cast<uint8_t>(load) == 0) {
        At<uint32_t>(self, view::kIdleId) = 0;
        DropModel(self, view::kIdleModel);
        At<uint32_t>(self, view::kSecondId) = 0;
        DropModel(self, view::kSecondModel);
        return;
    }
    void* const system = Virtual<void*>(Global<void*>(addr::SC4_City), 0x144);
    void* const advisor = Virtual<void*>(system, 0x1C, At<uint32_t>(self, view::kAdvisorType));
    if (!advisor)
        return;
    const uint32_t idleNumber = Global<uint32_t>(addr::SC4UI_HeadAnimsIdle + At<uint32_t>(self, view::kMood) * 4);
    const uint32_t idleId = Virtual<uint32_t>(advisor, 0x44, idleNumber);
    const uint32_t secondNumber = Global<uint32_t>(addr::SC4UI_HeadAnimsSecond + At<uint32_t>(self, view::kMood) * 4);
    const uint32_t secondId = Virtual<uint32_t>(advisor, 0x44, secondNumber);
    if (At<uint32_t>(self, view::kIdleId) == idleId)
        return;
    void* const manager = Global<void*>(addr::SC4_ResourceManager);
    void** const idleSlot = &At<void*>(self, view::kIdleModel);
    At<uint32_t>(self, view::kIdleId) = idleId;
    DropModel(self, view::kIdleModel);
    At<uint32_t>(self, view::kSecondId) = secondId;
    DropModel(self, view::kSecondModel);

    constexpr uint32_t kModelType = 0x5AD0E817;
    constexpr uint32_t kModelGroup = 0xBADB57F1;
    uint32_t idleAltKey[3] = {kModelType, kModelGroup, At<uint32_t>(self, view::kIdleId) + 0x100};
    uint32_t idleKey[3] = {kModelType, kModelGroup, At<uint32_t>(self, view::kIdleId)};
    uint32_t secondAltKey[3] = {kModelType, kModelGroup, At<uint32_t>(self, view::kSecondId) + 0x100};
    uint32_t secondKey[3] = {kModelType, kModelGroup, At<uint32_t>(self, view::kSecondId)};
    void* resource = nullptr;

    bool made = false;
    if (Global<uint8_t>(addr::cSC4AdvisorSystem_DebugGuids) != 0) {
        made = MakeModel(manager, idleAltKey, &resource, idleSlot);
        if (!made)
            DropRef(&resource);
    }
    if (!made)
        MakeModel(manager, idleKey, &resource, idleSlot);

    void** const secondSlot = &At<void*>(self, view::kSecondModel);
    made = false;
    if (Global<uint8_t>(addr::cSC4AdvisorSystem_DebugGuids) != 0) {
        DropRef(&resource);
        made = MakeModel(manager, secondAltKey, &resource, secondSlot);
    }
    if (!made) {
        DropRef(&resource);
        MakeModel(manager, secondKey, &resource, secondSlot);
    }

    void** const shown = &At<void*>(self, view::kShown);
    This<void*>(addr::SC4UI_F420220, shown, idleSlot);
    if (*shown)
        Virtual<void>(*shown, 0x28, int32_t{0});
    if (resource)
        Virtual<uint32_t>(resource, 0x8);
}

// The game's 0x00420870. `this` is the head view (slot 3). The window's
// messages: paint (takes the window's area, draws through slot 12 and
// invalidates the window), set up and shut down.
uint8_t __fastcall ViewWinMessage(char* self, void*, void* window, uint32_t* message)
{
    const uint32_t type = message[0];
    if (type == kWinPaint) {
        const int32_t* const area = Virtual<const int32_t*>(At<void*>(self, view::kWindow), 0xC8);
        int32_t* const own = &At<int32_t>(self, view::kArea);
        own[0] = area[0];
        own[1] = area[1];
        own[2] = area[2];
        own[3] = area[3];
        Virtual<void>(self, 0x30);
        Virtual<void>(window, 0x170);
        return 1;
    }
    if (type == kWinInit)
        return ViewInit(self, nullptr, window);
    if (type == kWinShutdown)
        return ViewShutdown(self, nullptr, window);
    return 0;
}

// The game's 0x00420900. `this` is the head view (slot 13). Advances the
// model shown by a time. When the model has come to rest (state 0 after a
// state that was not 0): the idle model starts again and a number of idle
// loops (1 or 2) is drawn, and once that many more rests have passed the
// second model plays once.
void __fastcall ViewAdvance(char* self, void*, uint32_t elapsed)
{
    void** const shown = &At<void*>(self, view::kShown);
    if (!*shown)
        return;
    Virtual<void>(*shown, 0x3C, elapsed);
    if (Virtual<uint32_t>(*shown, 0x2C) == 0 && At<uint32_t>(self, view::kLastState) != 0) {
        const uint32_t loops = At<uint32_t>(self, view::kLoops);
        if (loops == 0) {
            At<uint32_t>(self, view::kLoops) = 1;
            This<void*>(addr::SC4UI_F420220, shown, &At<void*>(self, view::kIdleModel));
            if (*shown)
                Virtual<void>(*shown, 0x28, int32_t{0});
            At<uint32_t>(self, view::kLoopTarget) =
                This<uint32_t>(addr::cRZRandom_Between, self + view::kRandom, uint32_t{1}, uint32_t{3});
        } else {
            At<uint32_t>(self, view::kLoops) = loops + 1;
            if (loops + 1 == At<uint32_t>(self, view::kLoopTarget)) {
                At<uint32_t>(self, view::kLoops) = 0;
                This<void*>(addr::SC4UI_F420220, shown, &At<void*>(self, view::kSecondModel));
                if (*shown)
                    Virtual<void>(*shown, 0x28, int32_t{0});
            }
        }
    }
    if (*shown)
        At<uint32_t>(self, view::kLastState) = Virtual<uint32_t>(*shown, 0x2C);
}

// The game's 0x00420B70. `this` is the head view's memory (0xF8 bytes).
// The constructor, for an advisor type.
char* __fastcall ViewConstruct(char* self, void*, uint32_t advisorType)
{
    At<uint32_t>(self, 0x4) = addr::cSC4TrafficSimulator_VtableBase4;
    At<uint32_t>(self, 0x0) = addr::SC4UI_HeadViewVtable;
    At<uint32_t>(self, 0x4) = addr::SC4UI_HeadViewVtable4;
    At<uint32_t>(self, 0x8) = 0;
    At<uint8_t>(self, view::kSetUp) = 0;
    At<uint8_t>(self, view::kActive) = 0;
    for (std::size_t at = 0x10; at <= 0x38; at += 4)
        At<uint32_t>(self, at) = 0;
    At<uint16_t>(self, view::kMainWidth) = 0;
    At<uint16_t>(self, view::kMainHeight) = 0;
    At<uint32_t>(self, view::kCamera) = 0;
    At<uint32_t>(self, view::kAdvisorType) = advisorType;
    At<uint32_t>(self, view::kLastState) = 0;
    At<uint32_t>(self, view::kLoops) = 0;
    At<uint32_t>(self, view::kLoopTarget) = 0;
    {
        // The clock seed counts under the original's call site.
        const fixed_seed::SiteOverride site(0x00420BE5);
        This<void*>(addr::SC4Random_Construct, self + view::kRandom, int32_t{-1});
    }
    At<uint32_t>(self, view::kOffset) = 0;
    return self;
}

// The game's 0x00420C00. `this` is the head view (slot 5). The deleting
// destructor.
char* __fastcall ViewDeletingDestruct(char* self, void*, uint32_t flags)
{
    ViewDestruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x00420C20. `this` is the head view (slot 10). Places the
// model shown: its position, and its transform with the scale given (the
// transform is read from the model after an identity is prepared). The
// values are passed on as they came, bit for bit.
void __fastcall ViewPlaceModel(char* self, void*, uint32_t x, uint32_t y, uint32_t z, uint32_t scaleBits)
{
    void* model = At<void*>(self, view::kShown);
    if (!model)
        return;
    uint32_t position[3] = {x, y, z};
    Virtual<void>(model, 0x4, static_cast<void*>(position));
    model = At<void*>(self, view::kShown);
    alignas(4) char transform[0x38];
    At<float>(transform, 0x04) = 1.0f;
    At<float>(transform, 0x10) = 0.0f;
    At<float>(transform, 0x1C) = 0.0f;
    At<float>(transform, 0x08) = 0.0f;
    At<float>(transform, 0x14) = 1.0f;
    At<float>(transform, 0x20) = 0.0f;
    At<float>(transform, 0x0C) = 0.0f;
    At<float>(transform, 0x18) = 0.0f;
    At<float>(transform, 0x24) = 1.0f;
    At<float>(transform, 0x34) = 1.0f;
    At<float>(transform, 0x28) = 0.0f;
    At<float>(transform, 0x2C) = 0.0f;
    At<float>(transform, 0x30) = 0.0f;
    At<uint8_t>(transform, 1) = 0;
    At<uint8_t>(transform, 0) = 0;
    Virtual<void>(ModelBase(model), 0x24, static_cast<void*>(transform));
    float scale;
    std::memcpy(&scale, &scaleBits, sizeof(scale));
    At<uint32_t>(transform, 0x34) = scaleBits;
    uint8_t flags = At<uint8_t>(transform, 1);
    if (scale != 1.0f)
        flags |= 2;
    else
        flags &= 0xFD;
    model = At<void*>(self, view::kShown);
    At<uint8_t>(transform, 1) = flags;
    ++At<uint8_t>(transform, 0);
    Virtual<void>(ModelBase(model), 0x28, static_cast<void*>(transform));
}

bool InstallAdviceText2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[24] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F41F2D0, reinterpret_cast<void*>(&ShowAdvisor)},
        {addr::SC4UI_F41F610, reinterpret_cast<void*>(&ShowNews)},
        {addr::SC4UI_F41F720, reinterpret_cast<void*>(&ShowAdvice)},
        {addr::SC4UI_F41F830, reinterpret_cast<void*>(&HelperWinMessage)},
        {addr::SC4UI_F41FD10, reinterpret_cast<void*>(&HelperMessage)},
        {addr::SC4UI_F420030, reinterpret_cast<void*>(&ViewQueryInterface)},
        {addr::SC4UI_F4200A0, reinterpret_cast<void*>(&ViewSetActive)},
        {addr::SC4UI_F420100, reinterpret_cast<void*>(&ViewMessage)},
        {addr::SC4UI_F420140, reinterpret_cast<void*>(&ViewSetMood)},
        {addr::SC4UI_F420270, reinterpret_cast<void*>(&ViewDestruct)},
        {addr::SC4UI_F4202E0, reinterpret_cast<void*>(&ViewQueryInterface4)},
        {addr::SC4UI_F4202F0, reinterpret_cast<void*>(&ViewInit)},
        {addr::SC4UI_F420540, reinterpret_cast<void*>(&ViewShutdown)},
        {addr::SC4UI_F4205D0, reinterpret_cast<void*>(&ViewLoadModels)},
        {addr::SC4UI_F420870, reinterpret_cast<void*>(&ViewWinMessage)},
        {addr::SC4UI_F420900, reinterpret_cast<void*>(&ViewAdvance)},
        {addr::SC4UI_F420B70, reinterpret_cast<void*>(&ViewConstruct)},
        {addr::SC4UI_F420C00, reinterpret_cast<void*>(&ViewDeletingDestruct)},
        {addr::SC4UI_F420C20, reinterpret_cast<void*>(&ViewPlaceModel)},
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

const bool g_adviceText2Registered = registry::Register({
    "ui.advice_text.part2",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F41F2D0,
    "Our advice window controller (second half) and advisor head view",
    false,
    InstallAdviceText2,
});

} // namespace
} // namespace opensc4::ui
