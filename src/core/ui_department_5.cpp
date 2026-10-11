// What follows the budget panel's department dialog (0x0078C200 to
// 0x0078D140): three small classes of the interface that have nothing to do
// with the dialog.
//
// The clock window (0x38 bytes, vtable 0x00AB54D8, no class ID) shows the
// time of day by the computer's clock. 0x0078C590 brings it up under the
// main window (layout 0xAA5E60D1, window 0xCA5E6261), or brings an existing
// one to the front; the main window's code calls it on message 0xEA5EDB88
// and at start when the settings' byte +0x4EE is set. Each tick message
// (0xA2BF8ACD) it reads the clock again and, when the second has changed,
// has the language utility make the text "hours:minutes" for the text
// control 0x4A5E623E.
//
// The tips window (0x24 bytes, vtable 0x00AB5500, no class ID) shows one
// HTML page at a time out of the exemplars of type 0x29 (resource type
// 0x6534284A), with buttons for the next and the previous page and a check
// box. 0x0078C8C0 brings it up under the main window (layout 0xCA3BDD94,
// window 0x4A3BE152); the city view's start (0x007ED66F) does so once in a
// run, when 0x0078CCE0 says that there is at least one such exemplar. The
// settings keep the check box at +0x49C and the last page's instance at
// +0x4A0, so every opening starts one page further.
//
// The link handler (0x10 bytes, vtables 0x00AB55A0 and 0x00AB5584 at +8,
// interface 0xC49B192C) is what an HTML control asks about a link: its
// slot 5 says whether the link's scheme is "sc4". 0x004510FA makes one and
// gives it to an HTML control (slot 0x64). Its slot 6 (0x0078D330), which
// follows the link, starts after this range and stays the game's.
//
// Replacement, off by default:
//
//   ui.department.part5: everything named above.
//
// Not functions: 0x0078C220 and 0x0078CF60 are labels inside 0x0078C200
// and 0x0078CF30 (their "no" and "window is up" ends, after padding).
//
// The clock window: +0 vtable (a window procedure), +4 the reference
// count, +8 a byte set by Init (slot 6, shared code at 0x0045E110) and
// cleared by Shutdown, which the constructor leaves as it finds it, +0xC
// the window's cIGZWinGen, +0x10 the text control, +0x14 the language
// manager, +0x18 a stopwatch (0x18 bytes, mode 5), +0x30 the moment shown
// (seconds).
//
// The tips window: +0 vtable (a window procedure), +4 the reference count,
// +8 a vector of resource keys (begin, end, capacity), +0x14 the index of
// the page shown, +0x18 the window's cIGZWinGen, +0x1C the HTML control,
// +0x20 a reference that nothing sets. Four bytes at 0x00B4DFD0: a tips
// window has come up, an exemplar was found, the search only asks whether
// there is one, the search has run.
//
// The link handler: +0 vtable (the counted base), +4 the reference count,
// +8 the handler interface, +0xC the resource manager between Init and
// Shutdown.
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

namespace clockwin {
constexpr std::size_t kCount = 0x4;
constexpr std::size_t kUp = 0x8;
constexpr std::size_t kWindow = 0xC;
constexpr std::size_t kText = 0x10;
constexpr std::size_t kLanguage = 0x14;
constexpr std::size_t kWatch = 0x18;
constexpr std::size_t kMoment = 0x30;
} // namespace clockwin

namespace tips {
constexpr std::size_t kCount = 0x4;
constexpr std::size_t kKeys = 0x8;
constexpr std::size_t kKeysEnd = 0xC;
constexpr std::size_t kKeysCapacity = 0x10;
constexpr std::size_t kIndex = 0x14;
constexpr std::size_t kWindow = 0x18;
constexpr std::size_t kHtml = 0x1C;
constexpr std::size_t kSpare = 0x20;
} // namespace tips

namespace links {
constexpr std::size_t kHandler = 0x8;
constexpr std::size_t kResources = 0xC;
} // namespace links

constexpr uint32_t kWinGenIid = 0x5386D516;
constexpr uint32_t kTextIid = 0x212CDC1F;
constexpr uint32_t kCheckIid = 0x8810;
constexpr uint32_t kHtmlIid = 0x4A11FD4A;
constexpr uint32_t kHtmlClass = 0xAA12E5F5;
constexpr uint32_t kExemplarIid = 0xA52160F5;
constexpr uint32_t kExemplarType = 0x6534284A;
constexpr uint32_t kUiGroup = 0x96A006B0;

constexpr uint32_t kCommand = 3;
constexpr uint32_t kButtonClicked = 0x287259F6;
constexpr uint32_t kTick = 0xA2BF8ACD;
constexpr uint32_t kStop = 0xA2BF8AD1;
constexpr uint32_t kWindowUp = 0xA2BF8AD5;
constexpr uint32_t kWindowDown = 0xA2BF8AD6;
constexpr uint32_t kEndModal = 0xF;

constexpr uint32_t kClockLayout = 0xAA5E60D1;
constexpr uint32_t kClockWindow = 0xCA5E6261;
constexpr uint32_t kClockText = 0x4A5E623E;
constexpr uint32_t kCloseButton = 0x42B7C356;

constexpr uint32_t kTipsLayout = 0xCA3BDD94;
constexpr uint32_t kTipsWindow = 0x4A3BE152;
constexpr uint32_t kTipsPlace = 0xAA3BE17E;
constexpr uint32_t kTipsCheck = 0xAA3BE181;
constexpr uint32_t kTipsClose = 0x42B7C353;
constexpr uint32_t kTipsOk = 0xAA3BE170;
constexpr uint32_t kTipsNext = 0x2A3EA604;
constexpr uint32_t kTipsPrevious = 0x4A3EA5FD;

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

// A localized text: the string first (the constructor writes 0x20 bytes).
struct LocalizedText {
    GameString text;
    char rest[0xC];
};
static_assert(sizeof(LocalizedText) == 0x20);

// A resource key as the tips window's vector holds it.
struct Key {
    uint32_t type;
    uint32_t group;
    uint32_t instance;
};
static_assert(sizeof(Key) == 0xC);

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

// The game's inlined reference holder: empty it.
void Clear(void*& holder)
{
    if (void* const object = holder) {
        holder = nullptr;
        Release(object);
    }
}

// The inlined assignment to a reference holder.
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

// An empty string as the game's inlined constructor makes it.
void InitString(GameString& text)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    text.body.capacity = buffer + 8;
    text.body.begin = buffer;
    text.body.end = buffer;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// The inlined destructor.
void DestroyString(GameString& text)
{
    char* const buffer = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (buffer)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(buffer));
}

// The inlined closing of a window made from a layout: a modal one ends its
// modal state with 1, another is taken out of its parent.
void CloseWindow(void* gen)
{
    void* const manager = Virtual<void*>(Virtual<void*>(gen, 0xC), 0x18);
    if (Virtual<uint8_t>(Virtual<void*>(gen, 0xC), 0x10C, uint32_t{0x1000})) {
        void* const window = Virtual<void*>(gen, 0xC);
        Virtual<void>(manager, 0x24, kEndModal, window, uint32_t{1});
    } else {
        void* const parent = Virtual<void*>(Virtual<void*>(gen, 0xC), 0x2C);
        Virtual<void>(parent, 0x40, Virtual<void*>(gen, 0xC));
    }
}

// --- The clock window -----------------------------------------------------------------

// The game's 0x0078C200 (slot 0): interfaces 1 and 0x22E85D8E (a window
// procedure). The news ticker's click handler (vtable 0x00AB51E4) has the
// same code as its slot 0.
uint8_t __fastcall ClockQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid != 0x22E85D8E && iid != 1)
        return 0;
    *out = self;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// The game's 0x0078C230 (slot 7): Shutdown. Tells the service at
// 0x00B43CE0 that the window is gone, clears the settings' "clock shown"
// byte and lets the window and the text control go. The kept window is not
// checked for null, as in the original.
uint8_t __fastcall ClockShutdown(char* self, void*)
{
    if (!At<uint8_t>(self, clockwin::kUp))
        return 1;
    void* const gen = At<void*>(self, clockwin::kWindow);
    At<uint8_t>(self, clockwin::kUp) = 0;
    void* const target = Global<void*>(addr::SC4_Service43CE0);
    Virtual<void>(target, 0x18, Virtual<void*>(gen, 0xC));
    At<uint8_t>(Global<char*>(addr::SC4_Options43CD8), 0x4EE) = 0;
    void* const kept = At<void*>(self, clockwin::kWindow);
    if (kept)
        AddRef(kept);
    Clear(At<void*>(self, clockwin::kWindow));
    Clear(At<void*>(self, clockwin::kText));
    Virtual<void>(kept, 0x1C, static_cast<void*>(nullptr));
    Release(kept);
    return 1;
}

// The game's 0x0078C2B0. `this` is the clock window. Closes the window,
// shuts down and lets go of one reference (the original ends in a jump to
// Release).
uint32_t __fastcall ClockClose(char* self, void*)
{
    if (!At<void*>(self, clockwin::kWindow))
        return 0;
    Virtual<uint32_t>(self, 0x4);
    CloseWindow(At<void*>(self, clockwin::kWindow));
    Virtual<uint8_t>(self, 0x1C);
    return Virtual<uint32_t>(self, 0x8);
}

// The game's 0x0078C340: the constructor. The moment starts as midnight of
// the computer's date, so the first update always writes the text. The byte
// at +8 is not set.
char* __fastcall ClockConstruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4ClockWindow_Vtable;
    At<uint32_t>(self, clockwin::kCount) = 0;
    At<void*>(self, clockwin::kWindow) = nullptr;
    At<void*>(self, clockwin::kText) = nullptr;
    At<void*>(self, clockwin::kLanguage) = nullptr;
    This<void*>(addr::SC4Stopwatch_Construct, self + clockwin::kWatch, int32_t{5});
    uint32_t date[2];
    void* const today = This<void*>(addr::cRZDate_DefaultBase, &date[0]);
    uint32_t moment;
    At<uint32_t>(self, clockwin::kMoment) =
        *This<uint32_t*>(addr::SC4DateTime_Construct, &moment, today, uint32_t{0}, uint32_t{0}, uint32_t{0});
    This<void>(addr::SC4Stopwatch_Start, self + clockwin::kWatch);
    return self;
}

// The game's 0x0078C3A0 (slot 5): the deleting destructor.
char* __fastcall ClockDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4ClockWindow_Vtable;
    This<void>(addr::SC4Stopwatch_Destruct, self + clockwin::kWatch);
    if (void* const language = At<void*>(self, clockwin::kLanguage))
        Release(language);
    if (void* const text = At<void*>(self, clockwin::kText))
        Release(text);
    if (void* const gen = At<void*>(self, clockwin::kWindow))
        Release(gen);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x0078C3F0. `this` is the clock window. Reads the computer's
// clock and, when it differs from the moment shown, writes the hour and the
// minute through the language utility (slot 9, MakeTimeString, with -1 for
// the second) into the text control. The original works the local time out
// twice, once for the minute and once for the hour; 0x00417730 answers
// whether a moment lies in daylight saving time.
void __fastcall ClockUpdateText(char* self, void*)
{
    uint32_t moment = 0;
    This<uint32_t*>(addr::SC4_TimeNow, &moment);
    if (At<uint32_t>(self, clockwin::kMoment) == moment)
        return;
    At<uint32_t>(self, clockwin::kMoment) = moment;
    GameString text;
    InitString(text);
    void* const utility = Virtual<void*>(At<void*>(self, clockwin::kLanguage), 0x98, uint32_t{0});

    uint32_t forMinute = At<uint32_t>(self, clockwin::kMoment) - Global<uint32_t>(addr::SC4_Timezone);
    moment = forMinute;
    if (This<int32_t>(addr::cSC4Advisors_F417730, &moment))
        forMinute += 0xE10;
    uint32_t forHour = At<uint32_t>(self, clockwin::kMoment) - Global<uint32_t>(addr::SC4_Timezone);
    moment = forHour;
    if (This<int32_t>(addr::cSC4Advisors_F417730, &moment))
        forHour += 0xE10;

    const uint32_t minute = forMinute % 0xE10 / 60;
    const uint32_t hour = forHour % 0x15180 / 0xE10;
    Virtual<uint8_t>(utility, 0x24, hour, minute, int32_t{-1}, &text);
    Virtual<void>(At<void*>(self, clockwin::kText), 0x10, &text);
    DestroyString(text);
}

// The game's 0x0078C510 (slot 3): a message of the clock's window. The
// close button closes; a tick (and any other command) restarts the
// stopwatch when more than one millisecond has passed, updates the text and
// has the window drawn again; the stop message shuts down.
uint8_t __fastcall ClockWinMessage(char* self, void*, void* window, const WinMessage* message)
{
    switch (message->type) {
    case kCommand:
        if (message->a == kButtonClicked && message->b == kCloseButton) {
            ClockClose(self, nullptr);
            return 1;
        }
        break;
    case kTick:
        break;
    case kStop:
        Virtual<uint8_t>(self, 0x1C);
        return 0;
    default:
        return 0;
    }
    char* const watch = self + clockwin::kWatch;
    if (This<uint32_t>(addr::GZStopwatch_ElapsedMilliseconds, watch) > 1) {
        This<void>(addr::GZStopwatch_Start, watch);
        ClockUpdateText(self, nullptr);
    }
    Virtual<void>(window, 0x5C);
    return 0;
}

// The game's 0x0078C590 (cdecl, no arguments). Brings the clock window up
// under the main window. With one there already (child 0xCA5E6261) that one
// is brought to the front and false comes back; true when a new one was
// made. A failed allocation is not checked, as in the original.
uint8_t __cdecl ShowClock()
{
    void* const main = Global<void*>(addr::SC4_MainWindow);
    if (!main)
        return 0;
    if (void* const child = Virtual<void*>(main, 0x8C, kClockWindow)) {
        Virtual<void>(child, 0x114);
        Virtual<void>(child, 0x110, uint32_t{0x800}, uint32_t{1});
        Virtual<void>(child, 0x5C);
        Virtual<void>(child, 0x110, uint32_t{0x800}, uint32_t{0});
        return 0;
    }

    void* gen = nullptr;
    uint32_t key[3] = {0, kUiGroup, kClockLayout};
    if (!Cdecl<uint8_t>(addr::SC4UI_LoadWindow, &key[0], main, kClockWindow, kWinGenIid, &gen)) {
        if (gen)
            Release(gen);
        return 0;
    }

    char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x38});
    char* const self = memory ? ClockConstruct(memory, nullptr) : nullptr;
    Virtual<uint32_t>(self, 0x4);
    This<void*>(addr::SC4RefPtr_AssignFrom, self + clockwin::kWindow, &gen);
    void* const window = Virtual<void*>(gen, 0xC);
    Clear(At<void*>(self, clockwin::kText));
    Virtual<uint8_t>(window, 0x94, kClockText, kTextIid, &At<void*>(self, clockwin::kText));

    void* language = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, uint32_t{0x441E5070}, uint32_t{0x0054B7D5}, &language);
    This<void*>(addr::SC4RefPtr_Assign, self + clockwin::kLanguage, language);

    {
        void* const target = Global<void*>(addr::SC4_Service43CE0);
        Virtual<void>(target, 0x28, Virtual<void*>(gen, 0xC), uint32_t{1});
    }
    Virtual<void>(Virtual<void*>(gen, 0xC), 0x110, uint32_t{0x800}, uint32_t{1});
    Virtual<void>(Virtual<void*>(gen, 0xC), 0x5C);
    Virtual<void>(gen, 0x1C, static_cast<void*>(self));
    ClockUpdateText(self, nullptr);
    Virtual<uint32_t>(self, 0x8);
    This<void>(addr::SC4_ReleaseRef2, &language);
    if (gen)
        Release(gen);
    return 1;
}

// --- The tips window ------------------------------------------------------------------

// The number of keys: the game's signed division of the byte length by 12.
uint32_t KeyCount(char* self)
{
    return static_cast<uint32_t>(At<Key*>(self, tips::kKeysEnd) - At<Key*>(self, tips::kKeys));
}

// The game's 0x0078C760 (cdecl): whether a tips window has come up in this
// run.
uint8_t __cdecl TipsShown()
{
    return Global<uint8_t>(addr::SC4TipsWindow_Shown);
}

// The game's 0x0078C770. `this` is the tips window. Closes it: the check
// box goes to the settings (+0x49C) and so does the instance of the page
// shown (+0x4A0); then the window is closed and everything let go.
void __fastcall TipsClose(char* self, void*)
{
    if (!At<void*>(self, tips::kWindow))
        return;
    Virtual<uint32_t>(self, 0x4);
    char* const settings = Global<char*>(addr::SC4_Options43CD8);
    void* check = nullptr;
    void* const window = Virtual<void*>(At<void*>(self, tips::kWindow), 0xC);
    Clear(check);
    if (Virtual<uint8_t>(window, 0x90, kTipsCheck, kCheckIid, &check))
        At<uint8_t>(settings, 0x49C) = Virtual<uint8_t>(check, 0x28) != 0;
    if (At<Key*>(self, tips::kKeys) != At<Key*>(self, tips::kKeysEnd))
        At<uint32_t>(settings, 0x4A0) = At<Key*>(self, tips::kKeys)[At<uint32_t>(self, tips::kIndex)].instance;
    Virtual<void>(At<void*>(self, tips::kWindow), 0x1C, static_cast<void*>(nullptr));
    CloseWindow(At<void*>(self, tips::kWindow));
    Clear(At<void*>(self, tips::kWindow));
    Clear(At<void*>(self, tips::kHtml));
    Clear(At<void*>(self, tips::kSpare));
    Virtual<uint32_t>(self, 0x8);
    if (check)
        Release(check);
}

// The game's 0x0078C8C0 (cdecl). Brings the tips window up under the main
// window unless there is one (child 0x4A3BE152); the object (its
// constructor is inlined here) goes to `out` with a reference when `out` is
// given. True when a window was made. A failed allocation is not checked,
// as in the original.
uint8_t __cdecl ShowTips(void** out)
{
    void* const main = Global<void*>(addr::SC4_MainWindow);
    if (!main)
        return 0;
    uint8_t made = 0;
    if (Virtual<void*>(main, 0x8C, kTipsWindow))
        return made;

    void* gen = nullptr;
    uint32_t key[3] = {0, kUiGroup, kTipsLayout};
    if (Cdecl<uint8_t>(addr::SC4UI_LoadWindow, &key[0], main, kTipsWindow, kWinGenIid, &gen)) {
        char* const self = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x24});
        if (self) {
            At<std::uintptr_t>(self, 0x0) = addr::SC4TipsWindow_Vtable;
            At<uint32_t>(self, tips::kCount) = 0;
            At<Key*>(self, tips::kKeys) = nullptr;
            At<Key*>(self, tips::kKeysEnd) = nullptr;
            At<Key*>(self, tips::kKeysCapacity) = nullptr;
            At<uint32_t>(self, tips::kIndex) = 0;
            At<void*>(self, tips::kWindow) = nullptr;
            At<void*>(self, tips::kHtml) = nullptr;
            At<void*>(self, tips::kSpare) = nullptr;
        }
        Virtual<uint32_t>(self, 0x4);
        This<void*>(addr::SC4RefPtr_AssignFrom, self + tips::kWindow, &gen);
        Virtual<void>(Virtual<void*>(gen, 0xC), 0x5C);
        Virtual<void>(Virtual<void*>(gen, 0xC), 0x110, uint32_t{0x800}, uint32_t{0});
        Virtual<void>(gen, 0x1C, static_cast<void*>(self));
        if (out)
            *out = self;
        else
            Virtual<uint32_t>(self, 0x8);
        made = 1;
    }
    if (gen)
        Release(gen);
    return made;
}

// The game's 0x0078C9E0 (slot 5): the deleting destructor.
char* __fastcall TipsDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4TipsWindow_Vtable;
    if (void* const spare = At<void*>(self, tips::kSpare))
        Release(spare);
    if (void* const html = At<void*>(self, tips::kHtml))
        Release(html);
    if (void* const gen = At<void*>(self, tips::kWindow))
        Release(gen);
    if (Key* const keys = At<Key*>(self, tips::kKeys))
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(keys));
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x0078CA40. `this` is the tips window. Shows the page of one
// exemplar: its property 0x6A3E9D02 is the key of the title's text, and the
// page itself is the address "sc4://<type>_<group>_<instance>.html" made of
// the exemplar's own key. The control takes the title's body, not the
// string.
void __fastcall TipsShowPage(char* self, void*, const Key* key)
{
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    if (!resources || !At<void*>(self, tips::kHtml))
        return;
    void* exemplar = nullptr;
    if (Virtual<uint8_t>(resources, 0xC, key, kExemplarIid, &exemplar, uint32_t{0}, uint32_t{0})) {
        void* const holder = Virtual<void*>(exemplar, 0x1C);
        uint32_t textKey[3] = {0, 0, 0};
        if (Cdecl<uint8_t>(addr::SC4_GetPropertyResourceKey, holder, uint32_t{0x6A3E9D02}, &textKey[0])) {
            LocalizedText title;
            This<void*>(addr::cRZBaseString_FromTextKey, &title, &textKey[0], uint32_t{1});
            GameString url;
            This<void*>(addr::SC4String_SC4String, &url);
            Cdecl<void>(addr::cRZBaseString_Sprintf, &url, "sc4://0x%08x_0x%08x_0x%08x.html", key->type, key->group,
                        key->instance);
            Virtual<void>(At<void*>(self, tips::kHtml), 0x58, &title.text.body, &url);
            DestroyString(url);
            DestroyString(title.text);
        }
    }
    if (exemplar)
        Release(exemplar);
}

// The game's 0x0078CB40 (cdecl): what the resource manager's key list calls
// for every exemplar. One of type 0x29 (property 0x10) either sets the
// "found" byte, when the search only asks whether there is one, or is added
// to the window's keys. The resource manager is not checked for null, as in
// the original.
void __cdecl TipsKey(const Key* key, char* context)
{
    if (Global<uint8_t>(addr::SC4TipsWindow_Asking) && Global<uint8_t>(addr::SC4TipsWindow_Found))
        return;
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    void* exemplar = nullptr;
    if (Virtual<uint8_t>(resources, 0xC, key, kExemplarIid, &exemplar, uint32_t{0}, uint32_t{0})) {
        void* const holder = Virtual<void*>(exemplar, 0x1C);
        uint32_t type = 0;
        if (Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, uint32_t{0x10}, &type) && type == 0x29) {
            if (Global<uint8_t>(addr::SC4TipsWindow_Asking))
                Global<uint8_t>(addr::SC4TipsWindow_Found) = 1;
            else
                This<void>(addr::SC4KeyVector_PushBack, context + tips::kKeys, key);
        }
    }
    if (exemplar)
        Release(exemplar);
}

// The game's 0x0078CBF0. `this` is the tips window. One page on or back,
// going round at the ends, and the page shown (the original ends in a jump
// to it).
void __fastcall TipsStep(char* self, void*, uint8_t forward)
{
    if (At<Key*>(self, tips::kKeys) == At<Key*>(self, tips::kKeysEnd))
        return;
    const uint32_t index = At<uint32_t>(self, tips::kIndex);
    if (forward) {
        const uint32_t next = index + 1;
        At<uint32_t>(self, tips::kIndex) = next < KeyCount(self) ? next : 0;
    } else if (index > 0) {
        At<uint32_t>(self, tips::kIndex) = index - 1;
    } else {
        At<uint32_t>(self, tips::kIndex) = KeyCount(self) - 1;
    }
    TipsShowPage(self, nullptr, &At<Key*>(self, tips::kKeys)[At<uint32_t>(self, tips::kIndex)]);
}

// The game's 0x0078CC70 (cdecl). Empties the window's keys (when a window
// is given) and has the resource manager's list of exemplars call
// 0x0078CB40 for each. The list is not released when the manager says it is
// empty, as in the original.
void __cdecl TipsCollect(char* window)
{
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    if (!resources)
        return;
    void* list = nullptr;
    if (!Virtual<uint32_t>(resources, 0x48, &list, kExemplarType))
        return;
    if (window) {
        Key* const begin = At<Key*>(window, tips::kKeys);
        Key* const end = At<Key*>(window, tips::kKeysEnd);
        uint8_t tag;
        At<Key*>(window, tips::kKeysEnd) =
            Cdecl<Key*>(addr::SC4KeyVector_CopyRange, end, end, begin, &tag, uint32_t{0});
    }
    Virtual<void>(list, 0x1C, std::uintptr_t{addr::SC4UI_F78CB40}, static_cast<void*>(window));
    Release(list);
}

// The game's 0x0078CCE0 (cdecl): whether there is an exemplar of type 0x29.
// The search runs once in a run of the game.
uint8_t __cdecl TipsExist()
{
    if (Global<uint8_t>(addr::SC4TipsWindow_Found))
        return Global<uint8_t>(addr::SC4TipsWindow_Found);
    if (Global<uint8_t>(addr::SC4TipsWindow_Searched))
        return 0;
    Global<uint8_t>(addr::SC4TipsWindow_Searched) = 1;
    Global<uint8_t>(addr::SC4TipsWindow_Asking) = 1;
    uint8_t found = 0;
    if (void* const resources = Global<void*>(addr::SC4_ResourceManager)) {
        void* list = nullptr;
        if (Virtual<uint32_t>(resources, 0x48, &list, kExemplarType)) {
            Virtual<void>(list, 0x1C, std::uintptr_t{addr::SC4UI_F78CB40}, static_cast<void*>(nullptr));
            Release(list);
        }
        found = Global<uint8_t>(addr::SC4TipsWindow_Found);
    }
    Global<uint8_t>(addr::SC4TipsWindow_Asking) = 0;
    return found;
}

// The game's 0x0078CD50. `this` is the tips window. A button click: three
// buttons close, two turn the page. False for anything else.
uint8_t __fastcall TipsButton(char* self, void*, const WinMessage* message)
{
    if (message->a != kButtonClicked)
        return 0;
    switch (message->b) {
    case kCloseButton:
    case kTipsClose:
    case kTipsOk:
        TipsClose(self, nullptr);
        return 1;
    case kTipsNext:
    case kTipsPrevious:
        TipsStep(self, nullptr, message->b == kTipsNext);
        return 1;
    default:
        return 0;
    }
}

// The game's 0x0078CDB0. `this` is the tips window, whose window has just
// come up. Puts an HTML control over the place 0xAA3BE17E, collects the
// exemplars, finds the page the settings remember and shows the one after
// it.
void __fastcall TipsOpened(char* self, void*)
{
    if (!At<void*>(self, tips::kWindow))
        return;
    Global<uint8_t>(addr::SC4TipsWindow_Shown) = 1;
    void* const place = Virtual<void*>(Virtual<void*>(At<void*>(self, tips::kWindow), 0xC), 0x88, kTipsPlace);
    if (place) {
        void* const com = Cdecl<void*>(addr::RZGetCOM);
        void*& html = At<void*>(self, tips::kHtml);
        Clear(html);
        if (Virtual<uint8_t>(com, 0xC, kHtmlClass, kHtmlIid, &html)) {
            const uint32_t* const area = Virtual<const uint32_t*>(place, 0xC0);
            uint32_t copy[4] = {area[0], area[1], area[2], area[3]};
            Virtual<void>(Virtual<void*>(html, 0xC), 0xD8, &copy[0]);
            Virtual<void>(html, 0x70, uint32_t{0x40});
            Virtual<void>(html, 0x70, uint32_t{0x4});
            Virtual<void>(html, 0x70, uint32_t{0x8});
            void* const control = html;
            void* const window = Virtual<void*>(At<void*>(self, tips::kWindow), 0xC);
            Virtual<void>(window, 0x38, Virtual<void*>(control, 0xC));
        }
    }
    TipsCollect(self);

    char* const settings = Global<char*>(addr::SC4_Options43CD8);
    if (At<Key*>(self, tips::kKeys) != At<Key*>(self, tips::kKeysEnd)) {
        const uint32_t count = KeyCount(self);
        if (count != 0) {
            const Key* const keys = At<Key*>(self, tips::kKeys);
            const uint32_t last = At<uint32_t>(settings, 0x4A0);
            for (uint32_t i = 0; i < count; ++i) {
                if (keys[i].instance == last) {
                    At<uint32_t>(self, tips::kIndex) = i;
                    break;
                }
            }
        }
    }
    if (At<Key*>(self, tips::kKeys) != At<Key*>(self, tips::kKeysEnd)) {
        const uint32_t next = At<uint32_t>(self, tips::kIndex) + 1;
        const uint32_t index = next < KeyCount(self) ? next : 0;
        At<uint32_t>(self, tips::kIndex) = index;
        TipsShowPage(self, nullptr, &At<Key*>(self, tips::kKeys)[index]);
    }
}

// The game's 0x0078CF30 (slot 3): a message of the tips window's own
// window. The window is not checked for null, as in the original.
uint8_t __fastcall TipsWinMessage(char* self, void*, void* window, const WinMessage* message)
{
    if (window != Virtual<void*>(At<void*>(self, tips::kWindow), 0xC))
        return 0;
    switch (message->type) {
    case kCommand:
        return TipsButton(self, nullptr, message);
    case kWindowUp:
        TipsOpened(self, nullptr);
        return 1;
    case kWindowDown:
        return 1;
    default:
        return 0;
    }
}

// --- The link handler -----------------------------------------------------------------

// The game's 0x0078CF80 (slot 0 of the part at +8): interfaces 1 and
// 0xC49B192C give this part; the count is the whole object's (its slot 2 is
// AddRef).
uint8_t __fastcall LinkQueryInterface(char* part, void*, uint32_t iid, void** out)
{
    if (iid != 1 && iid != 0xC49B192C)
        return 0;
    char* const self = part - links::kHandler;
    *out = self ? part : nullptr;
    Virtual<uint32_t>(self, 0x8);
    return 1;
}

// The game's 0x0078CFC0 (slot 3 of the part at +8): Init. Takes the
// resource manager (service 0x056B906E) from the framework.
uint8_t __fastcall LinkInit(char* part, void*)
{
    void* fetched = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, uint32_t{0x056B906E}, uint32_t{0x656B8EFC}, &fetched);
    Hold(At<void*>(part, links::kResources - links::kHandler), fetched);
    This<void>(addr::SC4RefPtr_Destruct, &fetched);
    return 1;
}

// The game's 0x0078D030 (slot 4 of the part at +8): Shutdown.
uint8_t __fastcall LinkShutdown(char* part, void*)
{
    Clear(At<void*>(part, links::kResources - links::kHandler));
    return 1;
}

// The game's 0x0078D050: the constructor.
char* __fastcall LinkConstruct(char* self, void*)
{
    This<void*>(addr::cRZBaseRefCount_Construct, self);
    At<std::uintptr_t>(self, links::kHandler) = addr::SC4LinkHandler_HandlerBaseVtable;
    At<std::uintptr_t>(self, 0x0) = addr::SC4LinkHandler_Vtable;
    At<std::uintptr_t>(self, links::kHandler) = addr::SC4LinkHandler_HandlerVtable;
    At<void*>(self, links::kResources) = nullptr;
    return self;
}

// The game's 0x0078D080 (slot 0): the deleting destructor.
char* __fastcall LinkDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4LinkHandler_Vtable;
    At<std::uintptr_t>(self, links::kHandler) = addr::SC4LinkHandler_HandlerVtable;
    if (void* const resources = At<void*>(self, links::kResources))
        Release(resources);
    This<void>(addr::cRZBaseRefCount_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x0078D0C0 (slot 5 of the part at +8): whether a link is this
// handler's: its scheme compared with "sc4" (the string's slot 10 with 0).
// The string's allocator tag is the address of the argument, as in the
// original.
uint8_t __fastcall LinkIsOurs(char*, void*, void* url)
{
    static const char kScheme[] = "sc4";
    GameString scheme;
    scheme.body.begin = nullptr;
    scheme.body.end = nullptr;
    scheme.body.capacity = nullptr;
    This<void>(addr::SC4String_FromRange, &scheme.body, &kScheme[0], &kScheme[3], static_cast<void*>(&url));
    scheme.vtable = addr::SC4String_Vtable;
    scheme.zero = 0;
    void* const theirs = This<void*>(addr::SC4Url_Scheme, url);
    const uint8_t same = Virtual<uint8_t>(theirs, 0x28, &scheme, uint32_t{0});
    DestroyString(scheme);
    return same;
}

bool InstallDepartment5(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[28] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F78C200, reinterpret_cast<void*>(&ClockQueryInterface)},
        {addr::SC4UI_F78C230, reinterpret_cast<void*>(&ClockShutdown)},
        {addr::SC4UI_F78C2B0, reinterpret_cast<void*>(&ClockClose)},
        {addr::SC4UI_F78C340, reinterpret_cast<void*>(&ClockConstruct)},
        {addr::SC4UI_F78C3A0, reinterpret_cast<void*>(&ClockDelete)},
        {addr::SC4UI_F78C3F0, reinterpret_cast<void*>(&ClockUpdateText)},
        {addr::SC4UI_F78C510, reinterpret_cast<void*>(&ClockWinMessage)},
        {addr::SC4UI_F78C590, reinterpret_cast<void*>(&ShowClock)},
        {addr::SC4UI_F78C760, reinterpret_cast<void*>(&TipsShown)},
        {addr::SC4UI_F78C770, reinterpret_cast<void*>(&TipsClose)},
        {addr::SC4UI_F78C8C0, reinterpret_cast<void*>(&ShowTips)},
        {addr::SC4UI_F78C9E0, reinterpret_cast<void*>(&TipsDelete)},
        {addr::SC4UI_F78CA40, reinterpret_cast<void*>(&TipsShowPage)},
        {addr::SC4UI_F78CB40, reinterpret_cast<void*>(&TipsKey)},
        {addr::SC4UI_F78CBF0, reinterpret_cast<void*>(&TipsStep)},
        {addr::SC4UI_F78CC70, reinterpret_cast<void*>(&TipsCollect)},
        {addr::SC4UI_F78CCE0, reinterpret_cast<void*>(&TipsExist)},
        {addr::SC4UI_F78CD50, reinterpret_cast<void*>(&TipsButton)},
        {addr::SC4UI_F78CDB0, reinterpret_cast<void*>(&TipsOpened)},
        {addr::SC4UI_F78CF30, reinterpret_cast<void*>(&TipsWinMessage)},
        {addr::SC4UI_F78CF80, reinterpret_cast<void*>(&LinkQueryInterface)},
        {addr::SC4UI_F78CFC0, reinterpret_cast<void*>(&LinkInit)},
        {addr::SC4UI_F78D030, reinterpret_cast<void*>(&LinkShutdown)},
        {addr::SC4UI_F78D050, reinterpret_cast<void*>(&LinkConstruct)},
        {addr::SC4UI_F78D080, reinterpret_cast<void*>(&LinkDelete)},
        {addr::SC4UI_F78D0C0, reinterpret_cast<void*>(&LinkIsOurs)},
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

const bool g_department5Registered = registry::Register({
    "ui.department.part5",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F78C590,
    "Our clock window, tips window and the HTML control's sc4 link handler (docs/ui.md)",
    false,
    InstallDepartment5,
});

} // namespace
} // namespace opensc4::ui
