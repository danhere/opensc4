// Six small window classes of the city view that sit back to back
// (0x00769400 to 0x0076B102):
//
//   - the building style panel (0x1C bytes, vtable 0x00AB4A78; the city
//     view's interface object keeps it at +0x2FC): four style check boxes
//     (0x2000 to 0x2003), "all at once" or "one after the other" and the
//     years between styles, all kept by the service at 0x00B43D24;
//   - the rotation indicator (0x14 bytes, vtables 0x00AB4AB4, 0x00AB4AA0 at
//     +4, 0x00AB4A90 at +0xC): shows the child window that matches the
//     camera's rotation;
//   - the text page dialog (0x10 bytes, vtable 0x00AB4AFC): a modal window
//     with one HTML page, opened from the options dialog;
//   - the key filter of the name dialog's edit boxes (0x1C bytes, vtable
//     0x00AB4B18): hands Tab and Enter to the dialog's window;
//   - the name dialog of a new city (0x20 bytes, vtable 0x00AB4B34): city
//     name, mayor name and the three difficulty buttons;
//   - the options dialog (0x10 bytes, vtable 0x00AB4B64), its first part:
//     constructor, Init, Run and Close. Its window procedure and what
//     follows are in the next part.
//
// Replacement, off by default:
//
//   ui.dialogs.part4: everything above.
//
// Windows, check boxes, edit boxes and the HTML control are the game's,
// called through their vtables.
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

namespace style { // the building style panel
constexpr std::size_t kReferences = 0x04;
constexpr std::size_t kStarted = 0x08;  // byte
constexpr std::size_t kWindow = 0x0C;   // the panel's window
constexpr std::size_t kSmall = 0x10;    // child 0x8BCA20C3, shown when the panel is folded
constexpr std::size_t kLarge = 0x14;    // child 0x2BC619AF, shown when it is open
constexpr std::size_t kOpen = 0x18;     // byte
} // namespace style

namespace compass { // the rotation indicator
constexpr std::size_t kCounted = 0x04; // the counted base (vtable, count at +8)
constexpr std::size_t kTarget = 0x0C;  // the message target part
constexpr std::size_t kWindow = 0x10;
} // namespace compass

namespace page { // the text page dialog
constexpr std::size_t kReferences = 0x04;
constexpr std::size_t kWindow = 0x08; // the window's cIGZWinGen
constexpr std::size_t kHtml = 0x0C;   // the HTML control
} // namespace page

namespace keys { // the key filter
constexpr std::size_t kReferences = 0x04;
constexpr std::size_t kBuffer = 0x08; // an empty vector (begin, end, capacity), +0x14 zero
constexpr std::size_t kTarget = 0x18; // the dialog's window
} // namespace keys

namespace naming { // the name dialog
constexpr std::size_t kReferences = 0x04;
constexpr std::size_t kBuffer = 0x08;     // an empty vector, +0x14 zero
constexpr std::size_t kDifficulty = 0x18; // 0 to 2
constexpr std::size_t kWindow = 0x1C;     // the window's cIGZWinGen
} // namespace naming

namespace options { // the options dialog
constexpr std::size_t kWindow = 0x04;     // the window's cIGZWinGen
constexpr std::size_t kSettings = 0x08;   // the game's settings (0x00B43CD8)
constexpr std::size_t kReferences = 0x0C;
} // namespace options

constexpr uint32_t kWinGenIid = 0x5386D516;
constexpr uint32_t kCheckIid = 0x8810;
constexpr uint32_t kEditIid = 0x231A1C57;
constexpr uint32_t kHtmlIid = 0x4A11FD4A;
constexpr uint32_t kHtmlClass = 0xAA12E5F5;
constexpr uint32_t kUiGroup = 0x96A006B0;
constexpr uint32_t kCommand = 0x287259F6;  // a button's command in a type 3 window message
constexpr uint32_t kWindowUp = 0xA2BF8AD5;
constexpr uint32_t kWindowDown = 0xA2BF8AD6;
constexpr uint32_t kSliderMoved = 0x682876C9;
constexpr uint32_t kCityLoaded = 0x26D31EC1;
constexpr uint32_t kViewChanged = 0xA6B79602;

constexpr uint32_t kFirstStyle = 0x2000;
constexpr uint32_t kLastStyle = 0x2003;
constexpr uint32_t kAllAtOnce = 0xCBC61559;   // check box: every chosen style at once
constexpr uint32_t kInTurn = 0xEBC61560;      // check box: one style after the other
constexpr uint32_t kYearsSlider = 0xABC61550;
constexpr uint32_t kYearsText = 0xABC61589;

constexpr uint32_t kCityName = 0xAA4149BF;
constexpr uint32_t kMayorName = 0xAA4149D2;
constexpr uint32_t kAccept = 0x2A4144BE;
constexpr uint32_t kFirstDifficulty = 0x2AB6C373;

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

// A string from the text resources: a string first, 0x20 bytes.
struct LocalizedText {
    GameString text;
    char rest[0xC];
};
static_assert(sizeof(LocalizedText) == 0x20);

struct IdVector {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};

// One window layout of a dialog: the key Init is given, the resource and
// the window in it.
struct Layout {
    uint32_t key;
    uint32_t instance;
    uint32_t window;
};

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

// The game's inlined reference holder: empty it.
void Clear(void*& holder)
{
    if (void* const object = holder) {
        holder = nullptr;
        Release(object);
    }
}

// The game's inlined reference holder: assign.
void Assign(void*& holder, void* object)
{
    void* const old = holder;
    if (object != old) {
        if (object)
            AddRef(object);
        holder = object;
        if (old)
            Release(old);
    }
}

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

// An empty string, as the game builds one inline.
void InitString(GameString& text)
{
    text.body = {nullptr, nullptr, nullptr};
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    text.body.begin = buffer;
    text.body.end = buffer;
    text.body.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

void DestroyString(GameString& text)
{
    char* const begin = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (begin)
        Free(begin);
}

// Whether the string has a character above the space.
bool HasText(const GameString& text)
{
    const char* const begin = text.body.begin;
    const uint32_t length = static_cast<uint32_t>(text.body.end - begin);
    for (uint32_t i = 0; i < length; ++i) {
        if (static_cast<uint8_t>(begin[i]) > 0x20)
            return true;
    }
    return false;
}

void CheckChild(void* window, uint32_t id, uint8_t on)
{
    Cdecl<uint8_t>(addr::SC4Win_CheckChild, window, id, uint32_t{on});
}

// A modal window closed through its window manager: the modal state ended
// (with 1) when the window is modal (flag 0x1000), then the window taken
// off.
void CloseModal(void* windowGen)
{
    void* const window = Virtual<void*>(windowGen, 0xC);
    void* const manager = Virtual<void*>(window, 0x18);
    if (manager) {
        if (Virtual<uint8_t>(window, 0x10C, uint32_t{0x1000}))
            Virtual<void>(manager, 0x24, uint32_t{0xF}, window, uint32_t{1});
        Virtual<void>(manager, 0x5C, window);
    }
}

// The entry of a dialog's layout table for a key.
const Layout* FindLayout(std::uintptr_t table, std::size_t count, uint32_t key)
{
    const Layout* const layouts = reinterpret_cast<const Layout*>(table);
    for (std::size_t i = 0; i < count; ++i) {
        if (layouts[i].key == key)
            return &layouts[i];
    }
    return nullptr;
}

// --- The building style panel ---------------------------------------------------------

void* Styles()
{
    return Global<void*>(addr::SC4_Service43D24);
}

// The game's 0x007695F0. `this` is the panel. Shutdown: the window gets no
// window procedure any more and the two children are let go.
uint8_t __fastcall StyleShutdown(char* self, void*)
{
    if (At<uint8_t>(self, style::kStarted)) {
        void* gen; // not set before the call, as in the original
        At<uint8_t>(self, style::kStarted) = 0;
        if (Virtual<uint8_t>(At<void*>(self, style::kWindow), 0x0, kWinGenIid, &gen)) {
            Virtual<void>(gen, 0x1C, static_cast<void*>(nullptr));
            Release(gen);
        }
        Clear(At<void*>(self, style::kSmall));
        Clear(At<void*>(self, style::kLarge));
    }
    return 1;
}

// The game's 0x00769420. `this` is the panel. Init: finds the two children,
// shows the open one, becomes the window procedure and moves two children
// below the "all at once" check box; then every child reports to the window.
uint8_t __fastcall StyleInit(char* self, void*)
{
    if (!At<uint8_t>(self, style::kStarted)) {
        Assign(At<void*>(self, style::kSmall),
               Virtual<void*>(At<void*>(self, style::kWindow), 0x88, uint32_t{0x8BCA20C3}));
        Assign(At<void*>(self, style::kLarge),
               Virtual<void*>(At<void*>(self, style::kWindow), 0x88, uint32_t{0x2BC619AF}));
        Virtual<void>(At<void*>(self, style::kSmall), 0x118);
        Virtual<void>(At<void*>(self, style::kLarge), 0x114);

        void* gen; // not set before the call, as in the original
        if (Virtual<uint8_t>(At<void*>(self, style::kWindow), 0x0, kWinGenIid, &gen)) {
            Virtual<void>(gen, 0x1C, static_cast<void*>(self));
            Release(gen);
        }
        Virtual<void>(At<void*>(self, style::kWindow), 0x110, uint32_t{0x800}, uint32_t{0});
        Virtual<void>(At<void*>(self, style::kSmall), 0x110, uint32_t{0x800}, uint32_t{0});
        Virtual<void>(At<void*>(self, style::kLarge), 0x110, uint32_t{0x800}, uint32_t{0});

        void* check = nullptr;
        if (Virtual<uint8_t>(At<void*>(self, style::kWindow), 0x94, kAllAtOnce, kCheckIid, &check)) {
            Virtual<void>(check, 0x84);
            void* const first = Virtual<void*>(At<void*>(self, style::kWindow), 0x88, uint32_t{0xCBC3C2B9});
            void* const second = Virtual<void*>(At<void*>(self, style::kWindow), 0x88, uint32_t{0xCBC61581});
            if (first && second) {
                void* const checkWindow = Virtual<void*>(check, 0xC);
                const int32_t firstTop = Virtual<int32_t>(checkWindow, 0xB4) + 2;
                const int32_t firstLeft = Virtual<int32_t>(first, 0xB0);
                Virtual<void>(first, 0xE0, firstLeft, firstTop);
                const int32_t secondTop = Virtual<int32_t>(first, 0xB4) + 8;
                const int32_t secondLeft = Virtual<int32_t>(second, 0xB0);
                Virtual<void>(second, 0xE0, secondLeft, secondTop);
            }
        }
        At<uint8_t>(self, style::kStarted) = 1;
        if (check)
            Release(check);
    }
    void* const window = At<void*>(self, style::kWindow);
    Cdecl<void>(addr::SC4Win_SetChildrenTarget, window, window);
    return 1;
}

// The game's 0x00769660. `this` is the panel. Opens or folds the panel.
void __fastcall StyleSetOpen(char* self, void*, uint8_t open)
{
    if (open) {
        Virtual<void>(At<void*>(self, style::kSmall), 0x118);
        Virtual<void>(At<void*>(self, style::kLarge), 0x114);
        At<uint8_t>(self, style::kOpen) = 1;
    } else {
        Virtual<void>(At<void*>(self, style::kLarge), 0x118);
        Virtual<void>(At<void*>(self, style::kSmall), 0x114);
        At<uint8_t>(self, style::kOpen) = 0;
    }
}

// The game's 0x007696B0. `this` is the panel. Hides the panel's window; the
// original jumps to the window's slot and returns what it returns.
uint32_t __fastcall StyleHide(char* self, void*)
{
    return Virtual<uint32_t>(At<void*>(self, style::kWindow), 0x118);
}

// The game's 0x007696C0. `this` is the panel. Whether the window is shown.
uint32_t __fastcall StyleIsShown(char* self, void*)
{
    return Virtual<uint32_t>(At<void*>(self, style::kWindow), 0x11C);
}

// The game's 0x007696D0. `this` is the panel. The last checked style cannot
// be switched off: with one box checked that box is disabled, with more all
// four are enabled.
void __fastcall StyleUpdateBoxes(char* self, void*)
{
    uint32_t checked = 0;
    for (uint32_t id = kFirstStyle; id <= kLastStyle; ++id) {
        if (Cdecl<uint8_t>(addr::SC4Win_IsChildChecked, At<void*>(self, style::kWindow), id))
            ++checked;
    }
    if (checked > 1) {
        for (uint32_t id = kFirstStyle; id <= kLastStyle; ++id)
            Cdecl<uint8_t>(addr::SC4Win_EnableChild, At<void*>(self, style::kWindow), id, uint32_t{1});
        return;
    }
    for (uint32_t id = kFirstStyle; id <= kLastStyle; ++id) {
        if (Cdecl<uint8_t>(addr::SC4Win_IsChildChecked, At<void*>(self, style::kWindow), id))
            Cdecl<uint8_t>(addr::SC4Win_EnableChild, At<void*>(self, style::kWindow), id, uint32_t{0});
    }
}

// The game's 0x00769750. `this` is the panel's memory (0x1C bytes).
char* __fastcall StyleConstruct(char* self, void*, void* window)
{
    At<std::uintptr_t>(self, 0) = addr::SC4UI_StylePanelVtable;
    At<uint32_t>(self, style::kReferences) = 0;
    At<uint8_t>(self, style::kStarted) = 0;
    At<void*>(self, style::kWindow) = window;
    if (window)
        AddRef(window);
    At<void*>(self, style::kSmall) = nullptr;
    At<void*>(self, style::kLarge) = nullptr;
    At<uint8_t>(self, style::kOpen) = 1;
    return self;
}

// The game's 0x00769790. `this` is the panel (slot 5). The deleting
// destructor.
char* __fastcall StyleDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0) = addr::SC4UI_StylePanelVtable;
    StyleShutdown(self, nullptr);
    Clear(At<void*>(self, style::kWindow));
    if (void* const large = At<void*>(self, style::kLarge))
        Release(large);
    if (void* const small = At<void*>(self, style::kSmall))
        Release(small);
    if (void* const window = At<void*>(self, style::kWindow))
        Release(window);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x007697F0. `this` is not used. Adds a style to the service's
// list when it is not in it.
void __fastcall StyleAdd(char*, void*, uint32_t id)
{
    void* const service = Styles();
    IdVector list;
    This<void*>(addr::SC4Uint32Vector_Copy, &list, Virtual<void*>(service, 0x24));
    uint32_t* const end = list.end;
    uint32_t* begin = list.begin;
    if (Cdecl<uint32_t*>(addr::SC4_FindUint32, begin, end, &id, &id) == end) {
        if (end != list.capacity) {
            if (end)
                *end = id;
            list.end = list.end + 1;
        } else {
            This<void>(addr::SC4PtrVector_InsertN, &list, end, &id, &id, uint32_t{1}, uint32_t{1});
        }
        Virtual<void>(service, 0x28, &list);
        begin = list.begin;
    }
    if (begin)
        Free(begin);
}

// The game's 0x00769890. `this` is not used. Takes a style out of the
// service's list.
void __fastcall StyleRemove(char*, void*, uint32_t id)
{
    void* const service = Styles();
    IdVector list;
    This<void*>(addr::SC4Uint32Vector_Copy, &list, Virtual<void*>(service, 0x24));
    uint32_t* end = list.end;
    uint32_t* begin = list.begin;
    uint32_t* const found = Cdecl<uint32_t*>(addr::SC4_FindUint32, begin, end, &id, &id);
    if (found != end) {
        uint32_t* const next = found + 1;
        if (next != end) {
            Cdecl<void*>(addr::SC4_memmove, static_cast<void*>(found), static_cast<void*>(next),
                         static_cast<uint32_t>(reinterpret_cast<char*>(end) - reinterpret_cast<char*>(next)));
            end = list.end;
        }
        list.end = end - 1;
        Virtual<void>(service, 0x28, &list);
        begin = list.begin;
    }
    if (begin)
        Free(begin);
}

// The years between styles written under the slider.
void StyleShowYears(char* self, void* service)
{
    GameString text;
    This<void*>(addr::SC4String_SC4String, &text);
    const int32_t years = Virtual<int32_t>(service, 0x34);
    Cdecl<void>(addr::cRZBaseString_Sprintf, &text, "%d", years);
    Cdecl<uint8_t>(addr::SC4Win_SetChildText, At<void*>(self, style::kWindow), kYearsText, &text);
    DestroyString(text);
}

// The game's 0x00769920. `this` is the panel (slot 3). The window procedure:
// the style boxes, the two mode boxes, the fold and open buttons and the
// slider of the years between styles.
uint8_t __fastcall StyleWinMessage(char* self, void*, void*, const WinMessage* message)
{
    if (message->type == 3) {
        if (!Cdecl<uint8_t>(addr::SC4Tutorial_FilterWinMessage, message))
            return 1;
        if (message->a != kCommand)
            return 0;
        const uint32_t id = message->b;
        void* const service = Styles();
        if (id == kAllAtOnce) {
            Virtual<void>(service, 0x30, uint32_t{0});
            CheckChild(At<void*>(self, style::kWindow), kAllAtOnce, 1);
            CheckChild(At<void*>(self, style::kWindow), kInTurn, 0);
        } else if (id == kInTurn) {
            Virtual<void>(service, 0x30, uint32_t{1});
            CheckChild(At<void*>(self, style::kWindow), kInTurn, 1);
            CheckChild(At<void*>(self, style::kWindow), kAllAtOnce, 0);
        } else if (id == 0xCBC61567) {
            Virtual<void>(At<void*>(self, style::kLarge), 0x118);
            Virtual<void>(At<void*>(self, style::kSmall), 0x114);
            At<uint8_t>(self, style::kOpen) = 0;
        } else if (id == 0xEBC619FD) {
            Virtual<void>(At<void*>(self, style::kSmall), 0x118);
            Virtual<void>(At<void*>(self, style::kLarge), 0x114);
            At<uint8_t>(self, style::kOpen) = 1;
        } else if (id == 0x0BC61548 || id == 0x2BC619F3) {
            StyleSetOpen(self, nullptr, At<uint8_t>(self, style::kOpen) == 0);
        } else if (id >= kFirstStyle && id <= kLastStyle) {
            if (message->c != 0)
                StyleAdd(self, nullptr, id);
            else
                StyleRemove(self, nullptr, id);
            StyleUpdateBoxes(self, nullptr);
        }
        return 0;
    }
    if (message->type == kSliderMoved && message->a == kYearsSlider) {
        void* const service = Styles();
        Virtual<void>(service, 0x38, message->b);
        StyleShowYears(self, service);
    }
    return 0;
}

// The game's 0x00769B30. `this` is the panel. Fills the panel from the
// service and shows it.
void __fastcall StyleRefresh(char* self, void*)
{
    void* const service = Styles();
    {
        void* const window = At<void*>(self, style::kWindow);
        CheckChild(window, kInTurn, Virtual<uint8_t>(service, 0x2C));
    }
    {
        void* const window = At<void*>(self, style::kWindow);
        CheckChild(window, kAllAtOnce, Virtual<uint8_t>(service, 0x2C) ? 0 : 1);
    }
    for (uint32_t id = kFirstStyle; id <= kLastStyle; ++id) {
        uint32_t wanted = id;
        char tag; // not set, as in the original
        IdVector list;
        This<void*>(addr::SC4Uint32Vector_Copy, &list, Virtual<void*>(Styles(), 0x24));
        uint32_t* const begin = list.begin;
        const uint8_t on = Cdecl<uint32_t*>(addr::SC4_FindUint32, begin, list.end, &wanted, &tag) != list.end;
        if (begin)
            Free(begin);
        CheckChild(At<void*>(self, style::kWindow), id, on);
    }
    StyleUpdateBoxes(self, nullptr);

    GameString text;
    InitString(text);
    const int32_t years = Virtual<int32_t>(service, 0x34);
    Cdecl<void>(addr::cRZBaseString_Sprintf, &text, "%d", years);
    Cdecl<uint8_t>(addr::SC4Win_SetChildText, At<void*>(self, style::kWindow), kYearsText, &text);
    {
        void* const window = At<void*>(self, style::kWindow);
        Cdecl<uint8_t>(addr::SC4Win_SetChildValue, window, kYearsSlider, Virtual<int32_t>(service, 0x34));
    }
    Virtual<void>(At<void*>(self, style::kWindow), 0x114);
    DestroyString(text);
}

// --- The rotation indicator -----------------------------------------------------------

// The game's 0x00769CA0. `this` is the indicator (slot 0).
uint8_t __fastcall CompassQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == 1 || iid == 0x22E85D8E)
        *out = self;
    else if (iid == 0x452294AA)
        *out = self ? self + compass::kTarget : nullptr;
    else
        return 0;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

// The game's 0x00769D40. `this` is the indicator. The deleting destructor.
char* __fastcall CompassDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0) = addr::SC4UI_CompassVtable;
    At<std::uintptr_t>(self, compass::kCounted) = addr::SC4UI_CompassVtable4;
    At<std::uintptr_t>(self, compass::kTarget) = addr::SC4UI_CompassVtableC;
    This<void>(addr::cRZBaseRefCount_Destruct, self + compass::kCounted);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x00769D10: the deleting destructor from the counted part.
char* __fastcall CompassDelete4(char* part, void*, uint32_t flags)
{
    return CompassDelete(part - compass::kCounted, nullptr, flags);
}

// The game's 0x00769D20: QueryInterface from the message target part.
uint8_t __fastcall CompassQueryInterfaceC(char* part, void*, uint32_t iid, void** out)
{
    return CompassQueryInterface(part - compass::kTarget, nullptr, iid, out);
}

// The game's 0x00769D80. `this` is the indicator.
void __fastcall CompassRegister(char* self, void*)
{
    void* const server = Global<void*>(addr::SC4_MessageServer);
    Virtual<void>(server, 0x14, static_cast<void*>(self ? self + compass::kTarget : nullptr), kCityLoaded);
    Virtual<void>(server, 0x14, static_cast<void*>(self ? self + compass::kTarget : nullptr), kViewChanged);
}

// The game's 0x00769DD0. `this` is the indicator.
void __fastcall CompassUnregister(char* self, void*)
{
    void* const server = Global<void*>(addr::SC4_MessageServer);
    Virtual<void>(server, 0x18, static_cast<void*>(self ? self + compass::kTarget : nullptr), kViewChanged);
    Virtual<void>(server, 0x18, static_cast<void*>(self ? self + compass::kTarget : nullptr), kCityLoaded);
}

// The game's 0x00769E20. `this` is the indicator. Of the children numbered
// from the window's ID + 1 on, shows the one for the camera's rotation.
void __fastcall CompassUpdate(char* self, void*)
{
    void* const camera = Virtual<void*>(Global<void*>(addr::SC4_View43DD0), 0x40);
    const int32_t rotation = At<int32_t>(camera, 0x10C);
    const uint32_t first = Virtual<uint32_t>(At<void*>(self, compass::kWindow), 0xFC) + 1;
    int32_t index = 0;
    void* child = Virtual<void*>(At<void*>(self, compass::kWindow), 0x88, first);
    while (child) {
        Virtual<void>(child, 0x110, uint32_t{1}, static_cast<uint32_t>(rotation == index));
        ++index;
        child = Virtual<void*>(At<void*>(self, compass::kWindow), 0x88, static_cast<uint32_t>(index) + first);
    }
}

// The game's 0x00769E90. `this` is the indicator's memory (0x14 bytes).
char* __fastcall CompassConstruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::SC4NewsTicker_WinProcBaseVtable;
    This<void*>(addr::cRZBaseRefCount_Construct, self + compass::kCounted);
    At<std::uintptr_t>(self, compass::kTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, compass::kCounted) = addr::SC4UI_CompassVtable4;
    At<std::uintptr_t>(self, 0) = addr::SC4UI_CompassVtable;
    At<std::uintptr_t>(self, compass::kTarget) = addr::SC4UI_CompassVtableC;
    At<void*>(self, compass::kWindow) = nullptr;
    return self;
}

// The game's 0x00769ED0. `this` is the indicator (slot 3). The window
// procedure: takes the window when it comes up and listens for the camera.
uint8_t __fastcall CompassWinMessage(char* self, void*, void* window, const WinMessage* message)
{
    if (message->type == kWindowUp) {
        At<void*>(self, compass::kWindow) = window;
        CompassRegister(self, nullptr);
        return 1;
    }
    if (message->type == kWindowDown) {
        At<void*>(self, compass::kWindow) = nullptr;
        CompassUnregister(self, nullptr);
        return 1;
    }
    return 0;
}

// The game's 0x00769F10. `this` is the message target part (+0xC, slot 3).
// A city loaded, or the view changed with another value at +4 of what the
// message carries: the indicator is brought up to date.
uint8_t __fastcall CompassDoMessage(char* part, void*, void* message)
{
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type != kCityLoaded && type != kViewChanged)
        return 0;
    char* const before = Virtual<char*>(message, 0x28);
    char* const after = Virtual<char*>(message, 0x3C);
    if (type == kCityLoaded || At<uint32_t>(before, 4) != At<uint32_t>(after, 4))
        CompassUpdate(part - compass::kTarget, nullptr);
    return 1;
}

// --- The text page dialog -------------------------------------------------------------

// The game's 0x00769F70. `this` is the dialog. Runs the window modally.
uint8_t __fastcall PageRun(char* self, void*)
{
    uint8_t guard[4];
    This<void*>(addr::SC4ModalGuard_Construct, &guard[0], uint32_t{0}, uint32_t{0}, uint32_t{1}, uint32_t{1});
    void* const window = Virtual<void*>(At<void*>(self, page::kWindow), 0xC);
    if (window)
        AddRef(window);
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x28, window, uint32_t{1});
    Virtual<void>(Cdecl<void*>(addr::SC4_GetWinManager), 0xA4, window); // modal: returns when the page closes
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x18, window);
    if (window)
        Release(window);
    This<void>(addr::SC4ModalGuard_Destruct, &guard[0]);
    return 1;
}

// The game's 0x00769FE0. `this` is the dialog. Ends the modal state, takes
// the window off and lets the window and the HTML control go.
uint32_t __fastcall PageClose(char* self, void*)
{
    Virtual<uint32_t>(self, 0x4);
    if (void* const gen = At<void*>(self, page::kWindow))
        CloseModal(gen);
    Clear(At<void*>(self, page::kWindow));
    Clear(At<void*>(self, page::kHtml));
    return Virtual<uint32_t>(self, 0x8);
}

// The game's 0x0076A060. `this` is the dialog's memory (0x10 bytes).
char* __fastcall PageConstruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::SC4UI_TextPageVtable;
    At<uint32_t>(self, page::kReferences) = 0;
    At<void*>(self, page::kWindow) = nullptr;
    At<void*>(self, page::kHtml) = nullptr;
    return self;
}

// The game's 0x0076A080. `this` is the dialog (slot 2). The last reference
// destroys and frees it.
uint32_t __fastcall PageRelease(char* self, void*)
{
    const uint32_t references = At<uint32_t>(self, page::kReferences);
    if (references > 1) {
        At<uint32_t>(self, page::kReferences) = references - 1;
        return references - 1;
    }
    At<std::uintptr_t>(self, 0) = addr::SC4UI_TextPageVtable;
    if (void* const html = At<void*>(self, page::kHtml))
        Release(html);
    if (void* const gen = At<void*>(self, page::kWindow))
        Release(gen);
    Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return 0;
}

// The game's 0x0076A0C0. `this` is the dialog (slot 3). The window
// procedure: the button 0xEA59207D closes the page.
uint8_t __fastcall PageWinMessage(char* self, void*, void* window, const WinMessage* message)
{
    if (window != Virtual<void*>(At<void*>(self, page::kWindow), 0xC))
        return 0;
    if (!Virtual<uint8_t>(window, 0x11C))
        return 0;
    if (message->type != 3)
        return 0;
    if (message->a == kCommand && message->b == 0xEA59207D)
        PageClose(self, nullptr);
    return 1;
}

// The game's 0x0076A120. `this` is the dialog. Puts an HTML control in the
// place of the text window 0x0A5D0E13 and gives it the page: the text
// resource 0x4A5D648F of group 0x8A4924F3.
void __fastcall PageFill(char* self, void*)
{
    void* const window = Virtual<void*>(At<void*>(self, page::kWindow), 0xC);
    void* const place = Virtual<void*>(window, 0x8C, uint32_t{0x0A5D0E13});
    void* const parent = Virtual<void*>(place, 0x2C);
    if (!parent)
        return;
    void*& html = At<void*>(self, page::kHtml);
    if (html)
        return;
    void* const com = Cdecl<void*>(addr::RZGetCOM);
    Clear(html);
    if (!Virtual<uint8_t>(com, 0xC, kHtmlClass, kHtmlIid, &html))
        return;

    void* const area = Virtual<void*>(place, 0xC0);
    {
        LocalizedText source;
        void* const made = This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &source, uint32_t{0x8A4924F3},
                                       uint32_t{0x4A5D648F}, uint32_t{1});
        Virtual<void>(place, 0x128, made);
        DestroyString(source.text);
    }
    GameString text;
    This<void*>(addr::SC4String_FromIGZString, &text, Virtual<void*>(place, 0x124));
    Virtual<void>(parent, 0x19C);
    Virtual<void>(html, 0x84, reinterpret_cast<const uint32_t*>(addr::SC4UI_TextPageFontSizes));
    Virtual<void>(Virtual<void*>(html, 0xC), 0xD8, area);
    Virtual<void>(Virtual<void*>(html, 0xC), 0x1AC, uint32_t{0xC8}, uint32_t{0xDC}, uint32_t{0xDC});
    Virtual<void>(html, 0x70, uint32_t{0x40});
    Virtual<void>(html, 0x70, uint32_t{0x4});
    Virtual<void>(html, 0x68, uint32_t{0x8});
    Virtual<void>(parent, 0x38, Virtual<void*>(html, 0xC));
    {
        // The control takes the string's body, not the string.
        void* const control = html;
        GameString base;
        void* const basePointer = This<void*>(addr::SC4String_SC4String, &base);
        Virtual<void>(control, 0x58, &text.body, basePointer);
        DestroyString(base);
    }
    Virtual<void>(parent, 0x40, place);
    DestroyString(text);
}

// The game's 0x0076A2C0. `this` is the dialog. Loads the window of the
// layout `key` (the table has one, key 0), fills it and becomes its window
// procedure.
uint8_t __fastcall PageLoad(char* self, void*, uint32_t key)
{
    const Layout* const layout = FindLayout(addr::SC4UI_TextPageLayouts, 1, key);
    if (!layout)
        return 0;
    uint32_t resource[3] = {0, kUiGroup, layout->instance};
    void*& gen = At<void*>(self, page::kWindow);
    Clear(gen);
    if (!Cdecl<uint8_t>(addr::SC4UI_LoadWindow, &resource[0], static_cast<void*>(nullptr), layout->window, kWinGenIid,
                        &gen))
        return 0;
    PageFill(self, nullptr);
    Virtual<void>(gen, 0x1C, static_cast<void*>(self));
    return 1;
}

// --- The key filter -------------------------------------------------------------------

// The game's 0x0076A390. `this` is the filter (slot 3). A key message
// (type 4) with Tab or Enter goes to the dialog's window.
uint8_t __fastcall KeysWinMessage(char* self, void*, void*, const WinMessage* message)
{
    if (message->type != 4)
        return 0;
    const uint8_t key = static_cast<uint8_t>(message->a);
    if (key != 0x9 && key != 0xD)
        return 0;
    return Virtual<uint8_t>(At<void*>(self, keys::kTarget), 0xC, message);
}

// The game's 0x0076A3C0. `this` is the filter's memory (0x1C bytes).
char* __fastcall KeysConstruct(char* self, void*, void* target)
{
    At<std::uintptr_t>(self, 0) = addr::SC4UI_KeyFilterVtable;
    At<uint32_t>(self, keys::kReferences) = 0;
    At<void*>(self, keys::kBuffer) = nullptr;
    At<void*>(self, keys::kBuffer + 4) = nullptr;
    At<void*>(self, keys::kBuffer + 8) = nullptr;
    At<uint32_t>(self, keys::kBuffer + 0xC) = 0;
    At<void*>(self, keys::kTarget) = target;
    if (target)
        AddRef(target);
    return self;
}

// The game's 0x0076A3F0. `this` is the filter (slot 4). The deleting
// destructor.
char* __fastcall KeysDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0) = addr::SC4UI_KeyFilterVtable;
    if (void* const target = At<void*>(self, keys::kTarget))
        Release(target);
    if (void* const buffer = At<void*>(self, keys::kBuffer))
        Free(buffer);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// --- The name dialog ------------------------------------------------------------------

// The game's 0x0076A430. `this` is the dialog. Closes the window: a modal
// one ends its modal state (with 1 when not accepted), another is taken out
// of its parent. The window manager is not checked for null, as in the
// original.
void __fastcall NameClose(char* self, void*, uint8_t accepted)
{
    if (!At<void*>(self, naming::kWindow))
        return;
    Virtual<uint32_t>(self, 0x4);
    Virtual<void>(At<void*>(self, naming::kWindow), 0x1C, static_cast<void*>(nullptr));
    void* const manager = Virtual<void*>(Virtual<void*>(At<void*>(self, naming::kWindow), 0xC), 0x18);
    if (Virtual<uint8_t>(Virtual<void*>(At<void*>(self, naming::kWindow), 0xC), 0x10C, uint32_t{0x1000})) {
        const uint32_t result = accepted == 0;
        void* const window = Virtual<void*>(At<void*>(self, naming::kWindow), 0xC);
        Virtual<void>(manager, 0x24, uint32_t{0xF}, window, result);
    } else {
        void* const gen = At<void*>(self, naming::kWindow);
        void* const parent = Virtual<void*>(Virtual<void*>(gen, 0xC), 0x2C);
        Virtual<void>(parent, 0x40, Virtual<void*>(gen, 0xC));
    }
    Clear(At<void*>(self, naming::kWindow));
    Virtual<uint32_t>(self, 0x8);
}

// The game's 0x0076A4E0. `this` is the dialog. Closes without accepting.
uint8_t __fastcall NameCancel(char* self, void*)
{
    NameClose(self, nullptr, 0);
    return 1;
}

// The game's 0x0076A4F0. `this` is the dialog (slot 5). The deleting
// destructor.
char* __fastcall NameDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0) = addr::SC4UI_NameDialogVtable;
    if (void* const gen = At<void*>(self, naming::kWindow))
        Release(gen);
    if (void* const buffer = At<void*>(self, naming::kBuffer))
        Free(buffer);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x0076A530. `this` is the dialog. The accept button is enabled
// only while both names have a character above the space; returns that, and
// true when an edit box or the button is missing.
uint8_t __fastcall NameCheck(char* self, void*)
{
    void* mayor = nullptr;
    void* city = nullptr;
    uint8_t good = 1;
    void* window = Virtual<void*>(At<void*>(self, naming::kWindow), 0xC);
    if (Virtual<uint8_t>(window, 0x94, kCityName, kEditIid, &city)) {
        window = Virtual<void*>(At<void*>(self, naming::kWindow), 0xC);
        if (Virtual<uint8_t>(window, 0x94, kMayorName, kEditIid, &mayor)) {
            void* const button =
                Virtual<void*>(Virtual<void*>(At<void*>(self, naming::kWindow), 0xC), 0x8C, kAccept);
            if (button) {
                GameString text;
                This<void*>(addr::SC4String_SC4String, &text);
                Virtual<void>(city, 0xB4, &text);
                good = HasText(text);
                DestroyString(text);
                if (good) {
                    InitString(text);
                    Virtual<void>(mayor, 0xB4, &text);
                    good = HasText(text);
                    DestroyString(text);
                }
                if (good)
                    Virtual<void>(button, 0x110, uint32_t{2}, uint32_t{1});
                else
                    Virtual<void>(button, 0x110, uint32_t{2}, uint32_t{0});
            }
        }
    }
    if (mayor)
        Release(mayor);
    if (city)
        Release(city);
    return good;
}

// The game's 0x0076A700. `this` is the dialog. A button: the three
// difficulty buttons, accept (also 0x42B7C356) and cancel (0x42B7C353,
// 0x8A414491).
uint8_t __fastcall NameOnCommand(char* self, void*, const WinMessage* message)
{
    if (message->a != kCommand)
        return 0;
    const uint32_t id = message->b;
    if (id >= kFirstDifficulty && id <= kFirstDifficulty + 2) {
        At<uint32_t>(self, naming::kDifficulty) = id - kFirstDifficulty;
        void* const window = Virtual<void*>(At<void*>(self, naming::kWindow), 0xC);
        Cdecl<void>(addr::SC4Win_SelectChildInRange, window, kFirstDifficulty, uint32_t{3}, id);
        return 1;
    }
    if (id == kAccept || id == 0x42B7C356) {
        if (NameCheck(self, nullptr))
            NameClose(self, nullptr, 1);
        return 1;
    }
    if (id == 0x42B7C353 || id == 0x8A414491) {
        NameClose(self, nullptr, 0);
        return 1;
    }
    return 0;
}

// The game's 0x0076A7E0. `this` is the dialog. Takes the city's difficulty,
// makes the window's children report to it, marks the difficulty button and
// checks the names.
uint8_t __fastcall NameStart(char* self, void*)
{
    At<uint32_t>(self, naming::kDifficulty) = Virtual<uint32_t>(Global<void*>(addr::SC4_City), 0x60);
    void* const gen = At<void*>(self, naming::kWindow);
    void* const target = Virtual<void*>(gen, 0xC);
    void* const window = Virtual<void*>(gen, 0xC);
    Cdecl<void>(addr::SC4Win_SetChildrenTarget, window, target);
    const uint32_t button = At<uint32_t>(self, naming::kDifficulty) + kFirstDifficulty;
    void* const again = Virtual<void*>(At<void*>(self, naming::kWindow), 0xC);
    Cdecl<void>(addr::SC4Win_SelectChildInRange, again, kFirstDifficulty, uint32_t{3}, button);
    return NameCheck(self, nullptr);
}

// The game's 0x0076A840. `this` is the dialog's memory (0x20 bytes).
char* __fastcall NameConstruct(char* self, void*, void* gen)
{
    At<std::uintptr_t>(self, 0) = addr::SC4UI_NameDialogVtable;
    At<uint32_t>(self, naming::kReferences) = 0;
    At<void*>(self, naming::kBuffer) = nullptr;
    At<void*>(self, naming::kBuffer + 4) = nullptr;
    At<void*>(self, naming::kBuffer + 8) = nullptr;
    At<uint32_t>(self, naming::kBuffer + 0xC) = 0;
    At<uint32_t>(self, naming::kDifficulty) = 0;
    At<void*>(self, naming::kWindow) = gen;
    if (gen)
        AddRef(gen);
    NameStart(self, nullptr);
    return self;
}

// The edit box selected from the start to the end of its text and given the
// keyboard.
void FocusEdit(void* edit)
{
    const uint32_t length = Virtual<uint32_t>(edit, 0xBC);
    Virtual<void>(edit, 0x12C, uint32_t{0}, length);
    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    Virtual<void>(manager, 0x94, Virtual<void*>(edit, 0x14));
}

// The game's 0x0076A880. `this` is the dialog. A key: Tab goes from one
// name to the other, Enter accepts when both names are there.
uint8_t __fastcall NameOnKey(char* self, void*, uint8_t key)
{
    if (key == 0x9) {
        void* city; // neither is set before its call, as in the original
        void* mayor;
        if (Virtual<uint8_t>(Virtual<void*>(At<void*>(self, naming::kWindow), 0xC), 0x94, kCityName, kEditIid,
                             &city)) {
            if (Virtual<uint8_t>(Virtual<void*>(At<void*>(self, naming::kWindow), 0xC), 0x94, kMayorName, kEditIid,
                                 &mayor)) {
                void* const focus = Virtual<void*>(Cdecl<void*>(addr::SC4_GetWinManager), 0x90);
                void* left;
                if (focus == Virtual<void*>(city, 0x14)) {
                    FocusEdit(mayor);
                    left = city;
                } else {
                    FocusEdit(city);
                    left = mayor;
                }
                Virtual<void>(left, 0x12C, uint32_t{0}, uint32_t{0});
                Release(mayor);
            }
            Release(city);
        }
        return 1;
    }
    if (key == 0xD) {
        if (NameCheck(self, nullptr))
            NameClose(self, nullptr, 1);
        return 1;
    }
    return 0;
}

// The game's 0x0076A9D0. `this` is the dialog (slot 3). The window
// procedure: buttons, keys, a changed text (0x9B1552D8) and message 0x11,
// which puts the keyboard in the city name.
uint8_t __fastcall NameWinMessage(char* self, void*, void* window, const WinMessage* message)
{
    if (window != Virtual<void*>(At<void*>(self, naming::kWindow), 0xC))
        return 0;
    switch (message->type) {
    case 0x9B1552D8:
        NameCheck(self, nullptr);
        return 1;
    case 3:
        return NameOnCommand(self, nullptr, message);
    case 4:
        return NameOnKey(self, nullptr, static_cast<uint8_t>(message->a));
    case 0x11: {
        void* city; // not set before the call, as in the original
        if (Virtual<uint8_t>(Virtual<void*>(At<void*>(self, naming::kWindow), 0xC), 0x94, kCityName, kEditIid,
                             &city)) {
            void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
            Virtual<void>(manager, 0x94, Virtual<void*>(city, 0x14));
            Release(city);
        }
        return 1;
    }
    case kWindowUp:
    case kWindowDown:
        return 1;
    default:
        return 0;
    }
}

// --- The options dialog ---------------------------------------------------------------

// The game's 0x0076AAB0. `this` is the dialog. When the dialog is up: ends
// the modal state, takes the window off and marks the dialog as down.
uint32_t __fastcall OptionsClose(char* self, void*)
{
    if (!Global<uint8_t>(addr::SC4UI_OptionsDialogUp))
        return 0; // AL is the flag's byte, 0, in the original
    Virtual<uint32_t>(self, 0x4);
    if (void* const gen = At<void*>(self, options::kWindow))
        CloseModal(gen);
    Clear(At<void*>(self, options::kWindow));
    Global<uint8_t>(addr::SC4UI_OptionsDialogUp) = 0;
    return Virtual<uint32_t>(self, 0x8);
}

// The game's 0x0076AB30. `this` is the dialog's memory (0x10 bytes).
char* __fastcall OptionsConstruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0) = addr::SC4UI_OptionsDialogVtable;
    At<void*>(self, options::kWindow) = nullptr;
    At<void*>(self, options::kSettings) = nullptr;
    At<uint32_t>(self, options::kReferences) = 0;
    return self;
}

// The game's 0x0076AB50. `this` is the dialog. Runs the window modally with
// the keyboard in it, gives the keyboard back to the window that had it and
// closes. The window manager is not checked for null, as in the original.
uint8_t __fastcall OptionsRun(char* self, void*)
{
    uint8_t guard[4];
    This<void*>(addr::SC4ModalGuard_Construct, &guard[0], uint32_t{0}, uint32_t{0}, uint32_t{1}, uint32_t{1});
    void* manager = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, uint32_t{0xA417445E}, uint32_t{0x5A4}, &manager);
    void* const focus = Virtual<void*>(manager, 0x90);
    void* const window = Virtual<void*>(At<void*>(self, options::kWindow), 0xC);
    if (window)
        AddRef(window);
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x28, window, uint32_t{1});
    Virtual<void>(manager, 0x94, window);
    Virtual<void>(manager, 0xA4, window); // modal: returns when the dialog closes
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x18, window);
    if (Virtual<uint8_t>(manager, 0x60, focus))
        Virtual<void>(manager, 0x94, focus);
    OptionsClose(self, nullptr);
    if (window)
        Release(window);
    This<void>(addr::SC4RefPtr_Release, &manager);
    This<void>(addr::SC4ModalGuard_Destruct, &guard[0]);
    return 1;
}

// A check box of the options window set from a byte of the settings.
void OptionsShowSetting(char* self, void*& check, uint32_t id, std::size_t offset)
{
    void* const window = Virtual<void*>(At<void*>(self, options::kWindow), 0xC);
    Clear(check);
    if (Virtual<uint8_t>(window, 0x94, id, kCheckIid, &check)) {
        if (At<uint8_t>(At<void*>(self, options::kSettings), offset))
            Virtual<void>(check, 0x18);
        else
            Virtual<void>(check, 0x1C);
    }
}

// The game's 0x0076AC30. `this` is the dialog (slot 5). Loads the window of
// the layout `key` (the table has one, key 0) at the size of its parent,
// sets the five check boxes from the settings, makes the buttons report to
// the window and becomes its window procedure. Returns true only when the
// dialog is up already; a fresh load returns false, like a failed one.
uint8_t __fastcall OptionsInit(char* self, void*, uint32_t key)
{
    if (!Global<uint8_t>(addr::SC4UI_OptionsDialogGuard)) {
        Global<uint8_t>(addr::SC4UI_OptionsDialogGuard) = 1;
        Global<uint8_t>(addr::SC4UI_OptionsDialogUp) = 0;
    } else if (Global<uint8_t>(addr::SC4UI_OptionsDialogUp)) {
        return 1;
    }
    const Layout* const layout = FindLayout(addr::SC4UI_OptionsDialogLayouts, 1, key);
    if (!layout)
        return 0;
    uint32_t resource[3] = {0, kUiGroup, layout->instance};
    void*& gen = At<void*>(self, options::kWindow);
    Clear(gen);
    if (!Cdecl<uint8_t>(addr::SC4UI_LoadWindow, &resource[0], static_cast<void*>(nullptr), layout->window, kWinGenIid,
                        &gen))
        return 0;

    int32_t width;
    int32_t height;
    if (void* const parent = Virtual<void*>(Virtual<void*>(gen, 0xC), 0x2C)) {
        width = Virtual<int32_t>(parent, 0xA4);
        height = Virtual<int32_t>(parent, 0xA8);
    } else {
        width = 0x280;
        height = 0x1E0;
    }
    Virtual<void>(Virtual<void*>(gen, 0xC), 0xCC, width);
    Virtual<void>(Virtual<void*>(gen, 0xC), 0xD0, height);
    Virtual<void>(Virtual<void*>(Virtual<void*>(gen, 0xC), 0x8C, uint32_t{0x2A57DC83}), 0x110, uint32_t{1},
                  uint32_t{0});
    Virtual<void>(Virtual<void*>(Virtual<void*>(gen, 0xC), 0x8C, uint32_t{0x2A57DB84}), 0x110, uint32_t{1},
                  uint32_t{1});

    bool fill = true;
    if (!At<void*>(self, options::kSettings)) {
        void* const settings = Global<void*>(addr::SC4_Options43CD8);
        At<void*>(self, options::kSettings) = settings;
        fill = settings != nullptr;
    }
    if (fill) {
        void* check = nullptr;
        OptionsShowSetting(self, check, 0x4A7F19F2, 0xF08);
        OptionsShowSetting(self, check, 0x4A7F19F3, 0xF09);
        OptionsShowSetting(self, check, 0x4A7F19F4, 0x4EC);
        OptionsShowSetting(self, check, 0x4A7F19F5, 0xF0B);
        OptionsShowSetting(self, check, 0x4A7F19F6, 0xF0A);

        static constexpr uint32_t kButtons[] = {0x6A57DA48, 0xEA57DA59, 0x0A57DA65};
        for (const uint32_t id : kButtons) {
            void* const holder = gen;
            void* const button = Virtual<void*>(Virtual<void*>(holder, 0xC), 0x8C, id);
            Virtual<void>(button, 0x158, Virtual<void*>(holder, 0xC));
        }

        void* const window = Virtual<void*>(gen, 0xC);
        Clear(check);
        if (Virtual<uint8_t>(window, 0x94, uint32_t{0xEA5E99D9}, kCheckIid, &check)) {
            if (!Global<void*>(addr::SC4_City)) {
                void* const holder = gen;
                void* const button = Virtual<void*>(check, 0xC);
                Virtual<void>(button, 0x158, Virtual<void*>(holder, 0xC));
            } else {
                Virtual<void>(Virtual<void*>(check, 0xC), 0x110, uint32_t{2}, uint32_t{0});
            }
        }

        static constexpr uint32_t kOuterButtons[] = {0xEA57DB7E, 0xEA57DB7F};
        for (const uint32_t id : kOuterButtons) {
            void* const holder = gen;
            void* const parent = Virtual<void*>(Virtual<void*>(holder, 0xC), 0x2C);
            void* const button = Virtual<void*>(parent, 0x8C, id);
            Virtual<void>(button, 0x158, Virtual<void*>(holder, 0xC));
        }
        if (check)
            Release(check);
    }
    Global<uint8_t>(addr::SC4UI_OptionsDialogUp) = 1;
    Virtual<void*>(gen, 0xC);
    Virtual<void>(gen, 0x1C, static_cast<void*>(self));
    return 0;
}

bool InstallDialogs4(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[48] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F769420, reinterpret_cast<void*>(&StyleInit)},
        {addr::SC4UI_F7695F0, reinterpret_cast<void*>(&StyleShutdown)},
        {addr::SC4UI_F769660, reinterpret_cast<void*>(&StyleSetOpen)},
        {addr::SC4UI_F7696B0, reinterpret_cast<void*>(&StyleHide)},
        {addr::SC4UI_F7696C0, reinterpret_cast<void*>(&StyleIsShown)},
        {addr::SC4UI_F7696D0, reinterpret_cast<void*>(&StyleUpdateBoxes)},
        {addr::SC4UI_F769750, reinterpret_cast<void*>(&StyleConstruct)},
        {addr::SC4UI_F769790, reinterpret_cast<void*>(&StyleDelete)},
        {addr::SC4UI_F7697F0, reinterpret_cast<void*>(&StyleAdd)},
        {addr::SC4UI_F769890, reinterpret_cast<void*>(&StyleRemove)},
        {addr::SC4UI_F769920, reinterpret_cast<void*>(&StyleWinMessage)},
        {addr::SC4UI_F769B30, reinterpret_cast<void*>(&StyleRefresh)},
        {addr::SC4UI_F769CA0, reinterpret_cast<void*>(&CompassQueryInterface)},
        {addr::SC4UI_F769D10, reinterpret_cast<void*>(&CompassDelete4)},
        {addr::SC4UI_F769D20, reinterpret_cast<void*>(&CompassQueryInterfaceC)},
        {addr::SC4UI_F769D40, reinterpret_cast<void*>(&CompassDelete)},
        {addr::SC4UI_F769D80, reinterpret_cast<void*>(&CompassRegister)},
        {addr::SC4UI_F769DD0, reinterpret_cast<void*>(&CompassUnregister)},
        {addr::SC4UI_F769E20, reinterpret_cast<void*>(&CompassUpdate)},
        {addr::SC4UI_F769E90, reinterpret_cast<void*>(&CompassConstruct)},
        {addr::SC4UI_F769ED0, reinterpret_cast<void*>(&CompassWinMessage)},
        {addr::SC4UI_F769F10, reinterpret_cast<void*>(&CompassDoMessage)},
        {addr::SC4UI_F769F70, reinterpret_cast<void*>(&PageRun)},
        {addr::SC4UI_F769FE0, reinterpret_cast<void*>(&PageClose)},
        {addr::SC4UI_F76A060, reinterpret_cast<void*>(&PageConstruct)},
        {addr::SC4UI_F76A080, reinterpret_cast<void*>(&PageRelease)},
        {addr::SC4UI_F76A0C0, reinterpret_cast<void*>(&PageWinMessage)},
        {addr::SC4UI_F76A120, reinterpret_cast<void*>(&PageFill)},
        {addr::SC4UI_F76A2C0, reinterpret_cast<void*>(&PageLoad)},
        {addr::SC4UI_F76A390, reinterpret_cast<void*>(&KeysWinMessage)},
        {addr::SC4UI_F76A3C0, reinterpret_cast<void*>(&KeysConstruct)},
        {addr::SC4UI_F76A3F0, reinterpret_cast<void*>(&KeysDelete)},
        {addr::SC4UI_F76A430, reinterpret_cast<void*>(&NameClose)},
        {addr::SC4UI_F76A4E0, reinterpret_cast<void*>(&NameCancel)},
        {addr::SC4UI_F76A4F0, reinterpret_cast<void*>(&NameDelete)},
        {addr::SC4UI_F76A530, reinterpret_cast<void*>(&NameCheck)},
        {addr::SC4UI_F76A700, reinterpret_cast<void*>(&NameOnCommand)},
        {addr::SC4UI_F76A7E0, reinterpret_cast<void*>(&NameStart)},
        {addr::SC4UI_F76A840, reinterpret_cast<void*>(&NameConstruct)},
        {addr::SC4UI_F76A880, reinterpret_cast<void*>(&NameOnKey)},
        {addr::SC4UI_F76A9D0, reinterpret_cast<void*>(&NameWinMessage)},
        {addr::SC4UI_F76AAB0, reinterpret_cast<void*>(&OptionsClose)},
        {addr::SC4UI_F76AB30, reinterpret_cast<void*>(&OptionsConstruct)},
        {addr::SC4UI_F76AB50, reinterpret_cast<void*>(&OptionsRun)},
        {addr::SC4UI_F76AC30, reinterpret_cast<void*>(&OptionsInit)},
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

const bool g_dialogs4Registered = registry::Register({
    "ui.dialogs.part4",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F769920,
    "Our building style panel, rotation indicator, text page, name dialog and options dialog's start (docs/ui.md)",
    false,
    InstallDialogs4,
});

} // namespace
} // namespace opensc4::ui
