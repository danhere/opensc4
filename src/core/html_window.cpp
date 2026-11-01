// Our part of the HTML window (docs/html.md, piece h1, the first part h1a:
// "Ours: the window"): the window's life, showing a page, its COM slots and
// its settings, written by hand from the disassembly. Each function is a
// detour on the game's, in groups, off by default:
//
//   ui.html.window.life      the constructor, the factory, the destructor
//                            and the deleting one, slots 4 (create) and 5
//                            (destroy), the service's smart pointer, the
//                            colour (slot 107), the layout context's
//                            destructor
//   ui.html.window.show      ShowHTML, slot 22, the scroll bars dropped, the
//                            page laid out again, the Internet service's
//                            answer (the +0xDC part's slot 3), the timer's
//                            callback, the text (slot 21)
//   ui.html.window.slots     QueryInterface and the jumps of the +0xD8 and
//                            +0xDC parts, the class ID
//   ui.html.window.settings  the +0xD8 interface's small slots: the load
//                            state, the missing picture's name, the view's
//                            corner, the flags, the margins, the font
//                            steps, the base, the two addresses, the load
//                            state posted
//
// The window is a framework window (GZWin, 0x0099xxxx) with an interface of
// its own at +0xD8 (vtable 0x00ACD050), the one the game's controls call,
// and a message target at +0xDC (0x00ACD040). Slots of those parts get the
// window +0xD8 or +0xDC as `this`. Everything else the window calls stays
// the game's: the framework window's functions and slots, the timer, the
// lock, the addresses (SC4Url), the strings, the document's functions
// (ours or the game's through their addresses), operator new and delete.
// Ours call them with the same arguments in the same order, so the heap sees
// the same allocations and frees. Calls between these functions go through
// the game's addresses (or the vtables), so any mix of groups works.
//
// html_parser::g_useGame (set by the HTML tests) sends every detour to the
// game's function, so one run can do the same work through the game's code
// and through ours and compare.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::html_parser {
extern bool g_useGame;
}

namespace opensc4::html_window {
namespace {

using html_parser::g_useGame;

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("html window: first %s", name);
}

#define OPENSC4_FIRST(name)                                                                                    \
    do {                                                                                                       \
        static bool logged_ = false;                                                                           \
        if (!logged_)                                                                                          \
            FirstCall(name, logged_);                                                                          \
    } while (0)

template <typename R, typename... A>
R CallThis(std::uintptr_t fn, void* self, A... a)
{
    return reinterpret_cast<R(__thiscall*)(void*, A...)>(fn)(self, a...);
}

template <typename R, typename... A>
R CallPlain(std::uintptr_t fn, A... a)
{
    return reinterpret_cast<R(__cdecl*)(A...)>(fn)(a...);
}

// The game's string object (cRZString's layout): a vtable, begin, end, the
// end of its storage, a spare dword. 0x14 bytes.
struct Text {
    uint32_t vtable;
    char* begin;
    char* end;
    char* storage;
    uint32_t spare;
};
static_assert(sizeof(Text) == 0x14);

void Free(void* p) { CallPlain<void>(addr::SC4_Free, p); }

// Fields of the window.
constexpr std::size_t kFace = 0xD8;       // the interface 0x4A11FD4A
constexpr std::size_t kTarget = 0xDC;     // the message target 0x452294AA
constexpr std::size_t kContext = 0xE0;    // the layout context (0x68 bytes)
constexpr std::size_t kClient = 0xEC;     // the client rectangle
constexpr std::size_t kView = 0xFC;       // the part of the page in view
constexpr std::size_t kDocument = 0x148;  // the document shown
constexpr std::size_t kLock = 0x14C;      // the lock
constexpr std::size_t kMissing = 0x16C;   // the name of the missing picture
constexpr std::size_t kFlags = 0x180;     // scroll bars, frame, background
constexpr std::size_t kVertical = 0x1A4;  // the vertical scroll bar
constexpr std::size_t kHorizontal = 0x1A8;
constexpr std::size_t kReader = 0x1D8;    // the picture reader
constexpr std::size_t kTimer = 0x1DC;     // the 500 ms timer (BLINK)
constexpr std::size_t kSteps = 0x204;     // the seven font steps
constexpr std::size_t kBase = 0x220;      // the base address
constexpr std::size_t kShown = 0x2E0;     // the address of the page shown
constexpr std::size_t kAsked = 0x3A0;     // the address of the page asked for
constexpr std::size_t kService = 0x460;   // the Internet service
constexpr std::size_t kLoadState = 0x464; // 1 asked for, 2 shown, 0 failed

// The message the window posts to its parent (+0x4C) through its own slot
// 145: ID 0xE4A85D8F, the window, the load state and a fourth word.
struct StateMessage {
    uint32_t id;
    char* window;
    uint32_t state;
    uint32_t other;
};

uint32_t PostState(char* window, uint32_t state, uint32_t other)
{
    StateMessage message{0xE4A85D8Fu, window, state, other};
    return Virtual<uint32_t>(window, 0x244, At<uint32_t>(window, 0x4C), &message);
}

// ====================================================================================
// Life

// The constructor (0x008FBC38): the framework window's, the three vtables
// (each written as the game's inlined bases write them), the layout
// context, the lock, the string, the defaults, the timer, the three
// addresses, the background colour (slot 107's function) and the font steps
// none.
char* Construct(char* self)
{
    CallThis<void*>(addr::GZWin_Construct, self);
    At<uint32_t>(self, kFace) = 0x00ACCFB8;
    At<uint32_t>(self, kTarget) = 0x00A81174;
    At<uint32_t>(self, kTarget) = 0x00ACD040;
    At<uint32_t>(self, 0x0) = 0x00ACD0D8;
    At<uint32_t>(self, kFace) = 0x00ACD050;
    CallThis<void*>(addr::HTMLLayout_Construct, self + kContext);
    At<uint32_t>(self, kDocument) = 0;
    CallThis<void*>(addr::RZCriticalSection_Construct, self + kLock);
    for (std::size_t at = 0x154; at < 0x164; at += 4)
        At<uint32_t>(self, at) = 0;
    At<uint32_t>(self, 0x164) = 0;
    At<uint32_t>(self, 0x168) = 0;
    CallThis<void*>(addr::SC4String_SC4String, self + kMissing);
    At<uint32_t>(self, kFlags) = 0xFA;
    At<uint32_t>(self, 0x18C) = 0x16;
    At<uint32_t>(self, 0x194) = 0xA;
    At<uint32_t>(self, 0x198) = 1;
    At<uint32_t>(self, 0x1A0) = 1;
    At<uint32_t>(self, 0x19C) = 0;
    At<uint32_t>(self, kVertical) = 0;
    At<uint32_t>(self, kHorizontal) = 0;
    At<uint32_t>(self, 0x1AC) = 0;
    At<uint32_t>(self, 0x1B0) = 0;
    At<uint32_t>(self, 0x184) = 4;
    At<uint32_t>(self, 0x188) = 4;
    At<uint32_t>(self, 0x190) = 4;
    for (std::size_t at = 0x1B4; at < 0x1D4; at += 4)
        At<uint32_t>(self, at) = 0;
    At<uint32_t>(self, 0x1D4) = 0;
    At<uint32_t>(self, kReader) = 0;
    CallThis<void*>(addr::cRZTimer_Construct, self + kTimer);
    CallThis<void*>(addr::SC4Url_Construct, self + kBase);
    CallThis<void*>(addr::SC4Url_Construct, self + kShown);
    CallThis<void*>(addr::SC4Url_Construct, self + kAsked);
    At<uint32_t>(self, kService) = 0;
    At<uint32_t>(self, kLoadState) = 0;
    CallThis<uint32_t>(addr::HTMLWindow_SetColour, self, 0xC0u, 0xC0u, 0xC0u);
    for (std::size_t at = kSteps; at != kBase; at += 4)
        At<uint32_t>(self, at) = 0;
    return self;
}

// The factory (0x008FBDBD): a new window's +0xD8 interface, null when the
// allocation failed.
char* Create()
{
    char* const memory = CallPlain<char*>(addr::SC4_operator_new, std::size_t{0x468});
    char* const window = memory ? CallThis<char*>(addr::HTMLWindow_Construct, memory) : nullptr;
    return window ? window + kFace : nullptr;
}

// The destructor (0x008FB6B2): the vtables, slot 5's function, then the
// members in the reverse order and the framework window's destructor.
void Destruct(char* self)
{
    At<uint32_t>(self, 0x0) = 0x00ACD0D8;
    At<uint32_t>(self, kFace) = 0x00ACD050;
    At<uint32_t>(self, kTarget) = 0x00ACD040;
    CallThis<uint8_t>(addr::HTMLWindow_OnDestroy, self);
    CallThis<void>(addr::SC4Url_Destroy, self + kAsked);
    CallThis<void>(addr::SC4Url_Destroy, self + kShown);
    CallThis<void>(addr::SC4Url_Destroy, self + kBase);
    CallThis<void>(addr::cRZTimer_Destruct, self + kTimer);
    CallThis<void>(addr::SC4String_Destroy, self + kMissing);
    CallThis<void>(addr::RZCriticalSection_Destroy, self + kLock);
    CallThis<void>(addr::HTMLLayout_Destruct, self + kContext);
    CallThis<void>(addr::GZWin_Destruct, self);
}

// The deleting destructor (slot 148, 0x008FBDA1).
char* Delete(char* self, uint32_t flags)
{
    CallThis<void>(addr::HTMLWindow_Destruct, self);
    if (flags & 1)
        CallPlain<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// What a field holds released (slot 2) and cleared.
void ReleaseAt(char* self, std::size_t field)
{
    void*& held = At<void*>(self, field);
    if (held) {
        Virtual<uint32_t>(held, 0x8);
        held = nullptr;
    }
}

// Slot 5 (0x008FA329): only when created. The load state posted (the +0xD8
// part's slot 24), the timer stopped, what the window holds released, then
// the framework window's own.
bool OnDestroy(char* self)
{
    if (CallThis<uint8_t>(addr::GZWin_IsInitialized, self) == 0)
        return true;
    Virtual<uint32_t>(self + kFace, 0x60);
    Virtual<uint32_t>(self + kTimer, 0x1C);
    ReleaseAt(self, kReader);
    ReleaseAt(self, kService);
    ReleaseAt(self, 0x164);
    ReleaseAt(self, 0x168);
    ReleaseAt(self, kDocument);
    ReleaseAt(self, kVertical);
    ReleaseAt(self, kHorizontal);
    ReleaseAt(self, 0x1AC);
    ReleaseAt(self, 0x1B0);
    return CallThis<uint8_t>(addr::SC4Win_F99D2FE, self) != 0;
}

// The Internet service's smart pointer (0x008FA948, 0x008FA96D): the
// framework's system service 0x449B1939 with interface 0x449B1912, and its
// release.
void** ConstructServiceRef(void** self)
{
    *self = nullptr;
    if (void* const framework = CallPlain<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, 0x449B1939u, 0x449B1912u, self);
    return self;
}

void DestructServiceRef(void** self)
{
    if (*self)
        Virtual<uint32_t>(*self, 0x8);
}

// The view (+0xFC) made the client rectangle (+0xEC) at 0, 0.
void ViewFromClient(char* self)
{
    const int32_t left = At<int32_t>(self, kClient);
    const int32_t top = At<int32_t>(self, kClient + 4);
    for (std::size_t at = 0; at < 0x10; at += 4)
        At<int32_t>(self, kView + at) = At<int32_t>(self, kClient + at);
    At<int32_t>(self, kView) -= left;
    At<int32_t>(self, kView + 8) -= left;
    At<int32_t>(self, kView + 4) -= top;
    At<int32_t>(self, kView + 0xC) -= top;
}

// Slot 4 (0x008FC0F6): once created, true. Otherwise the framework
// window's Init (whose answer this is), the fonts, the missing picture's
// name, the picture reader, the title, the Internet service, the context's
// window and picture, the client rectangle, and the page asked for before
// the window was made.
bool OnCreate(char* self)
{
    if (CallThis<uint8_t>(addr::GZWin_IsInitialized, self) != 0)
        return true;
    const uint8_t made = CallThis<uint8_t>(addr::GZWin_Init, self);
    void* const manager = Virtual<void*>(self, 0x18);
    void* const fonts = Virtual<void*>(manager, 0x80);
    void*& font = At<void*>(self, 0x164);
    font = Virtual<void*>(fonts, 0x24, uint32_t{0x12});
    if (!font)
        font = Virtual<void*>(fonts, 0x24, uint32_t{0x11});
    if (!font)
        font = Virtual<void*>(fonts, 0x24, uint32_t{1});
    if (font)
        Virtual<uint32_t>(font, 0x4);
    void*& second = At<void*>(self, 0x168);
    second = Virtual<void*>(fonts, 0x24, uint32_t{1});
    if (second)
        Virtual<uint32_t>(second, 0x4);
    {
        // The temporary's characters freed, its vtable not put back.
        Text missing;
        CallThis<Text*>(addr::SC4String_FromText, &missing, reinterpret_cast<const char*>(0x00ACD340)); // Missing.jpg
        CallThis<void*>(addr::cRZBaseString_Assign, self + kMissing, &missing);
        if (missing.begin)
            Free(missing.begin);
    }
    if (!At<void*>(self, kReader)) {
        void* const com = CallPlain<void*>(addr::RZGetCOM);
        Virtual<uint8_t>(com, 0xC, 0xA2FFB5D3u, 0x02FFBB60u, self + kReader);
    }
    {
        Text title;
        CallThis<Text*>(addr::SC4String_FromText, &title, reinterpret_cast<const char*>(0x00ACD334)); // HTML Window
        Virtual<uint32_t>(self, 0x128, &title);
        title.vtable = static_cast<uint32_t>(addr::SC4String_Vtable);
        if (title.begin)
            Free(title.begin);
    }
    void*& service = At<void*>(self, kService);
    if (!service) {
        void* ref = nullptr;
        service = *CallThis<void**>(addr::HTMLServiceRef_Construct, &ref);
        CallThis<void>(addr::HTMLServiceRef_Destruct, &ref);
        if (service) {
            Virtual<uint32_t>(service, 0x4);
        } else {
            void* const com = CallPlain<void*>(addr::RZGetCOM);
            Virtual<uint8_t>(com, 0xC, 0x849B190Au, 0x449B1912u, &service);
        }
    }
    At<char*>(self, kContext) = self;
    At<uint32_t>(self, kContext + 8) = At<uint32_t>(self, 0x68);
    CallThis<uint8_t>(addr::HTMLWindow_UpdateClientRect, self);
    const Text* const shown = CallThis<const Text*>(addr::SC4Url_Text, self + kShown);
    if (shown->end != shown->begin && Virtual<uint8_t>(self + kFace, 0x44, self + kShown, uint32_t{0}) != 0)
        ViewFromClient(self);
    return made != 0;
}

// Slot 107 (0x008FA62A): the framework window's colour, and slot 108's
// colour of it kept at +0x11C.
uint32_t SetColour(char* self, uint32_t red, uint32_t green, uint32_t blue)
{
    CallThis<void>(addr::GZWin_SetBackgroundRGB, self, red, green, blue);
    const uint32_t colour = Virtual<uint32_t>(self, 0x1B0, red, green, blue);
    At<uint32_t>(self, 0x11C) = colour;
    return colour;
}

// The layout context's destructor (0x008FB68A): its two vectors' storage
// freed, then its list.
void DestructContext(char* self)
{
    if (void* const right = At<void*>(self, 0x5C))
        Free(right);
    if (void* const left = At<void*>(self, 0x50))
        Free(left);
    CallThis<void>(addr::cGZDBSegmentPackedFile_ListDestruct, self + 0x4C);
}

// ====================================================================================
// Showing a page

// ShowHTML (0x008FBDE4): a new document parses the text; when it did, it
// takes the place of the one shown.
bool ShowHTML(char* self, void* text, void* base)
{
    char* const memory = CallPlain<char*>(addr::SC4_operator_new, std::size_t{0x1C4});
    char* const doc = memory ? CallThis<char*>(addr::HTMLDocument_Construct, memory) : nullptr;
    Virtual<uint32_t>(doc, 0x4);
    CallThis<uint8_t>(addr::HTMLDocument_Init, doc, self, At<void*>(self, kService), base);
    if (At<uint32_t>(self, kSteps) != 0)
        Virtual<uint32_t>(doc, 0x1C, self + kSteps, int32_t{7});
    Text copy;
    CallThis<Text*>(addr::cSC4Advice_F408B90, &copy, text);
    const bool failed = Virtual<uint8_t>(doc, 0x20, &copy) == 0;
    copy.vtable = static_cast<uint32_t>(addr::SC4String_Vtable);
    if (copy.begin)
        Free(copy.begin);
    if (failed) {
        Virtual<uint32_t>(doc, 0x8);
        return false;
    }
    char* const lock = self + kLock;
    Virtual<uint32_t>(lock, 0x4);
    char* const timer = self + kTimer;
    if (Virtual<uint8_t>(doc, 0x50) != 0) {
        if (Virtual<uint8_t>(timer, 0x20) == 0) {
            Virtual<uint32_t>(timer, 0x24, static_cast<uint32_t>(addr::HTMLWindow_OnTimer), self);
            Virtual<uint32_t>(timer, 0x14, uint32_t{500}, uint32_t{0});
        }
    } else if (Virtual<uint8_t>(timer, 0x20) != 0) {
        Virtual<uint32_t>(timer, 0x1C);
    }
    Virtual<uint8_t>(self + kFace, 0x4C, base);
    ViewFromClient(self);
    if (At<char*>(self, kDocument)) {
        CallThis<uint8_t>(addr::HTMLDocument_ReleaseImages, At<char*>(self, kDocument));
        Virtual<uint32_t>(At<char*>(self, kDocument), 0x8);
        At<char*>(self, kDocument) = nullptr;
    }
    At<char*>(self, kDocument) = doc;
    At<char*>(self, kContext + 4) = doc;
    CallThis<void*>(addr::SC4Url_Assign, self + kShown, self + kAsked);
    CallThis<void>(addr::HTMLWindow_DropScrollBars, self, uint32_t{1}, uint32_t{1});
    Virtual<uint32_t>(lock, 0x8);
    return true;
}

// Slot 22 of the +0xD8 interface (0x008FBF73): ShowHTML, the load state
// made 2 (posted when it changed) and the window drawn again.
bool ShowHTMLText(char* face, void* text, void* base)
{
    char* const self = face - kFace;
    if (CallThis<uint8_t>(addr::HTMLWindow_ShowHTML, self, text, base) == 0)
        return false;
    uint32_t& state = At<uint32_t>(self, kLoadState);
    if (state != 2) {
        const uint32_t before = state;
        state = 2;
        PostState(self, 2, before);
    }
    Virtual<uint32_t>(self, 0x170);
    return true;
}

// The scroll bars dropped (0x008FA6F3): each asked for and there taken out
// of the children (its slot 3 the window to the window's slot 16) and
// released; the page laid out again when one was.
void DropScrollBars(char* self, uint8_t vertical, uint8_t horizontal)
{
    bool dropped = false;
    const auto drop = [&](std::size_t field) {
        void*& bar = At<void*>(self, field);
        dropped = true;
        void* const child = Virtual<void*>(bar, 0xC);
        Virtual<uint32_t>(self, 0x40, child);
        Virtual<uint32_t>(bar, 0x8);
        bar = nullptr;
    };
    if (vertical && At<void*>(self, kVertical))
        drop(kVertical);
    if (horizontal && At<void*>(self, kHorizontal))
        drop(kHorizontal);
    if (dropped)
        CallThis<uint32_t>(addr::HTMLWindow_Relayout, self);
}

// The page laid out again (0x008FA559): the document's slot 15 with 0 and
// the window drawn again. With no document EAX is the field's address.
uint32_t Relayout(char* self)
{
    char* const doc = At<char*>(self, kDocument);
    if (!doc)
        return static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(self + kDocument));
    Virtual<uint32_t>(doc, 0x3C, uint32_t{0});
    return Virtual<uint32_t>(self, 0x170);
}

// The +0xDC part's slot 3 (0x008FBFE5): the Internet service's answer to
// the page asked for.
bool OnMessage(char* target, char* message)
{
    if (Virtual<uint32_t>(message, 0x10) != 0x75758483u)
        return false;
    const bool succeeded = Virtual<uint32_t>(message, 0x38) != 0;
    Virtual<uint32_t>(message, 0x28);
    char* const request = Virtual<char*>(message, 0x50);
    char* const self = target - kTarget;
    uint32_t& state = At<uint32_t>(self, kLoadState);
    if (state != 1)
        return true;
    if (succeeded) {
        char* const text = request + 0x30;
        if (At<char*>(text, 0x4) != At<char*>(text, 0x0)) {
            void* const base = CallThis<void*>(addr::SC4Url_Text, self + kAsked);
            if (CallThis<uint8_t>(addr::HTMLWindow_ShowHTML, self, static_cast<void*>(text), base) == 0)
                return true;
        }
        state = 2;
    } else {
        state = At<char*>(self, kDocument) ? 2 : 0;
    }
    PostState(self, state, 1);
    Virtual<uint32_t>(self, 0x16C);
    return true;
}

// The timer's callback (0x008FA31D): the window drawn again.
uint32_t OnTimer(char* window) { return Virtual<uint32_t>(window, 0x170); }

// Slot 21 of the +0xD8 interface (0x008FC0C5): the document's text (its
// slot 4; the string's length, slot 7, before its characters, slot 6).
bool GetText(char* face, void* out)
{
    char* const doc = At<char*>(face, kDocument - kFace);
    char* const text = Virtual<char*>(doc, 0x10);
    const uint32_t length = Virtual<uint32_t>(text, 0x1C);
    char* const begin = Virtual<char*>(text, 0x18);
    CallThis<void*>(addr::SC4StringBody_AssignChars, out, begin, begin + length);
    return true;
}

// ====================================================================================
// The COM slots

// QueryInterface (0x008FA40C): the window, its +0xD8 interface or its +0xDC
// part, each with a reference; anything else the framework window's.
bool QueryInterface(char* self, uint32_t iid, void** out)
{
    switch (iid) {
    case 0x22BA0121u:
        *out = self;
        break;
    case 1:
    case 0x4A11FD4Au:
        *out = self ? self + kFace : nullptr;
        break;
    case 0x452294AAu:
        *out = self ? self + kTarget : nullptr;
        break;
    default:
        return CallThis<uint8_t>(addr::SC4Win_QueryInterface, self, iid, out) != 0;
    }
    Virtual<uint32_t>(self, 0x4);
    return true;
}

bool QueryInterfaceFace(char* face, uint32_t iid, void** out)
{
    return CallThis<uint8_t>(addr::HTMLWindow_QueryInterface, face - kFace, iid, out) != 0;
}

bool QueryInterfaceTarget(char* target, uint32_t iid, void** out)
{
    return CallThis<uint8_t>(addr::HTMLWindow_QueryInterface, target - kTarget, iid, out) != 0;
}

uint32_t AddRefTarget(char* target) { return CallThis<uint32_t>(addr::SC4Win_AddRefJump8FB729, target - kTarget); }
uint32_t ReleaseTarget(char* target)
{
    return CallThis<uint32_t>(addr::SC4Win_ReleaseJump8FB72E, target - kTarget);
}

uint32_t GetClassID() { return 0xAA12E5F5u; }

// ====================================================================================
// The settings (slots of the +0xD8 interface; `face` is the window +0xD8)

template <typename T>
T& Field(char* face, std::size_t windowOffset)
{
    return At<T>(face, windowOffset - kFace);
}

uint32_t GetLoadState(char* face) { return Field<uint32_t>(face, kLoadState); }

// Slot 23: the string's slot 13 (its assign).
void* SetMissingImage(char* face, const void* name) { return Virtual<void*>(face + (kMissing - kFace), 0x34, name); }

uint32_t GetPageSize(char* face, uint32_t* width, uint32_t* height)
{
    *width = Field<uint32_t>(face, kView);
    const uint32_t top = Field<uint32_t>(face, kView + 4);
    *height = top;
    return top;
}

uint32_t AddFlags(char* face, uint32_t flags)
{
    const uint32_t now = Field<uint32_t>(face, kFlags);
    if (flags & now)
        return now;
    return Virtual<uint32_t>(face, 0x6C, now | flags);
}

void* SetFlags(char* face, uint32_t flags)
{
    uint32_t& now = Field<uint32_t>(face, kFlags);
    if (flags != now)
        now = flags;
    return &now;
}

uint32_t RemoveFlags(char* face, uint32_t flags)
{
    const uint32_t now = Field<uint32_t>(face, kFlags);
    if (!(flags & now))
        return flags;
    return Virtual<uint32_t>(face, 0x6C, ~flags & now);
}

uint32_t HasFlags(char* face, uint32_t flags) { return (Field<uint32_t>(face, kFlags) & flags) != 0 ? 1u : 0u; }

uint32_t SetMargins(char* face, int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    Field<int32_t>(face, 0x184) = left;
    Field<int32_t>(face, 0x188) = top;
    Field<int32_t>(face, 0x18C) = right;
    Field<int32_t>(face, 0x190) = bottom;
    char* const self = face - kFace;
    CallThis<uint8_t>(addr::HTMLWindow_UpdateClientRect, self);
    return CallThis<uint32_t>(addr::HTMLWindow_Relayout, self);
}

int32_t* CopyFontSizes(const int32_t* first, const int32_t* last, int32_t* to)
{
    uint8_t tag = 0;
    return CallPlain<int32_t*>(addr::SC4_CopyDwords, first, last, to, &tag);
}

int32_t* SetFontSizes(char* face, const int32_t* sizes)
{
    return CallPlain<int32_t*>(addr::HTMLWindow_CopyFontSizes, sizes, sizes + 7, &Field<int32_t>(face, kSteps));
}

bool SetBase(char* face, const void* text)
{
    alignas(4) char address[0xC0];
    CallThis<void*>(addr::SC4Url_FromText, address, text);
    CallThis<void*>(addr::SC4Url_Assign, &Field<char>(face, kBase), static_cast<const void*>(address));
    CallThis<void>(addr::SC4Url_Destroy, address);
    return true;
}

// Slots 15 and 16: an address's text into `out`; AL whether it is not
// empty, the rest of EAX the string's begin (as the game leaves it).
uint32_t GetAddress(char* face, std::size_t field, Text* out)
{
    const void* const text = CallThis<const void*>(addr::SC4Url_Text, &Field<char>(face, field));
    CallThis<void*>(addr::cRZBaseString_Assign, out, text);
    const uint32_t begin = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(out->begin));
    return (begin & 0xFFFFFF00u) | (out->begin != out->end ? 1u : 0u);
}

uint32_t GetShownURL(char* face, Text* out) { return GetAddress(face, kShown, out); }
uint32_t GetAskedURL(char* face, Text* out) { return GetAddress(face, kAsked, out); }

// Slot 24 (0x008FA9A7): while a page is asked for, the state made 0 and
// posted (AL 0, the rest of EAX the post's); else EAX 1.
uint32_t PostLoadState(char* face)
{
    uint32_t& state = Field<uint32_t>(face, kLoadState);
    if (state != 1)
        return 1;
    state = 0;
    return PostState(face - kFace, 0, 1) & 0xFFFFFF00u;
}

// ====================================================================================
// The detours

#define OPENSC4_UNPAREN(...) __VA_ARGS__

// A thiscall function of the game: the detour is a fastcall one (EDX not
// read) that hands its arguments on.
#define OPENSC4_THIS_DETOUR(Ret, Name, Address, Ours, Params, Args)                                            \
    using Name##Fn = Ret(__thiscall*)(char* self OPENSC4_UNPAREN Params);                                    \
    Name##Fn g_##Name = nullptr;                                                                               \
    Ret __fastcall D##Name(char* self, void* OPENSC4_UNPAREN Params)                                         \
    {                                                                                                          \
        if (g_useGame)                                                                                         \
            return g_##Name(self OPENSC4_UNPAREN Args);                                                        \
        OPENSC4_FIRST(#Name " " #Address);                                                                     \
        return Ours(self OPENSC4_UNPAREN Args);                                                                \
    }

// A cdecl function of the game.
#define OPENSC4_CDECL_DETOUR(Ret, Name, Address, Ours, Params, Args)                                           \
    using Name##Fn = Ret(__cdecl*) Params;                                                                     \
    Name##Fn g_##Name = nullptr;                                                                               \
    Ret __cdecl D##Name Params                                                                                 \
    {                                                                                                          \
        if (g_useGame)                                                                                         \
            return g_##Name Args;                                                                              \
        OPENSC4_FIRST(#Name " " #Address);                                                                     \
        return Ours Args;                                                                                      \
    }

// Life.
void** ConstructServiceRefAt(char* self) { return ConstructServiceRef(reinterpret_cast<void**>(self)); }
void DestructServiceRefAt(char* self) { DestructServiceRef(reinterpret_cast<void**>(self)); }
OPENSC4_THIS_DETOUR(char*, Construct, 0x008FBC38, Construct, (), ())
OPENSC4_CDECL_DETOUR(char*, Create, 0x008FBDBD, Create, (), ())
OPENSC4_THIS_DETOUR(void, Destruct, 0x008FB6B2, Destruct, (), ())
OPENSC4_THIS_DETOUR(char*, Delete, 0x008FBDA1, Delete, (, uint32_t flags), (, flags))
OPENSC4_THIS_DETOUR(bool, OnDestroy, 0x008FA329, OnDestroy, (), ())
OPENSC4_THIS_DETOUR(bool, OnCreate, 0x008FC0F6, OnCreate, (), ())
OPENSC4_THIS_DETOUR(void**, ConstructServiceRef, 0x008FA948, ConstructServiceRefAt, (), ())
OPENSC4_THIS_DETOUR(void, DestructServiceRef, 0x008FA96D, DestructServiceRefAt, (), ())
OPENSC4_THIS_DETOUR(uint32_t, SetColour, 0x008FA62A, SetColour, (, uint32_t red, uint32_t green, uint32_t blue),
                    (, red, green, blue))
OPENSC4_THIS_DETOUR(void, DestructContext, 0x008FB68A, DestructContext, (), ())

// Showing a page.
OPENSC4_THIS_DETOUR(bool, ShowHTML, 0x008FBDE4, ShowHTML, (, void* text, void* base), (, text, base))
OPENSC4_THIS_DETOUR(bool, ShowHTMLText, 0x008FBF73, ShowHTMLText, (, void* text, void* base), (, text, base))
OPENSC4_THIS_DETOUR(void, DropScrollBars, 0x008FA6F3, DropScrollBars, (, uint8_t vertical, uint8_t horizontal),
                    (, vertical, horizontal))
OPENSC4_THIS_DETOUR(uint32_t, Relayout, 0x008FA559, Relayout, (), ())
OPENSC4_THIS_DETOUR(bool, OnMessage, 0x008FBFE5, OnMessage, (, char* message), (, message))
OPENSC4_CDECL_DETOUR(uint32_t, OnTimer, 0x008FA31D, OnTimer, (char* window), (window))
OPENSC4_THIS_DETOUR(bool, GetText, 0x008FC0C5, GetText, (, void* out), (, out))

// The COM slots.
OPENSC4_THIS_DETOUR(bool, QueryInterface, 0x008FA40C, QueryInterface, (, uint32_t iid, void** out), (, iid, out))
OPENSC4_THIS_DETOUR(bool, QueryInterfaceFace, 0x008FB745, QueryInterfaceFace, (, uint32_t iid, void** out),
                    (, iid, out))
OPENSC4_THIS_DETOUR(bool, QueryInterfaceTarget, 0x008FB750, QueryInterfaceTarget, (, uint32_t iid, void** out),
                    (, iid, out))
OPENSC4_THIS_DETOUR(uint32_t, AddRefTarget, 0x008FB75B, AddRefTarget, (), ())
OPENSC4_THIS_DETOUR(uint32_t, ReleaseTarget, 0x008FB766, ReleaseTarget, (), ())
OPENSC4_CDECL_DETOUR(uint32_t, GetClassID, 0x008FA317, GetClassID, (), ())

// The settings.
OPENSC4_THIS_DETOUR(uint32_t, GetLoadState, 0x008FB733, GetLoadState, (), ())
OPENSC4_THIS_DETOUR(void*, SetMissingImage, 0x008FB73A, SetMissingImage, (, const void* name), (, name))
OPENSC4_THIS_DETOUR(uint32_t, GetPageSize, 0x008FA65C, GetPageSize, (, uint32_t* width, uint32_t* height),
                    (, width, height))
OPENSC4_THIS_DETOUR(uint32_t, AddFlags, 0x008FA45D, AddFlags, (, uint32_t flags), (, flags))
OPENSC4_THIS_DETOUR(void*, SetFlags, 0x008FA4A8, SetFlags, (, uint32_t flags), (, flags))
OPENSC4_THIS_DETOUR(uint32_t, RemoveFlags, 0x008FA478, RemoveFlags, (, uint32_t flags), (, flags))
OPENSC4_THIS_DETOUR(uint32_t, HasFlags, 0x008FA495, HasFlags, (, uint32_t flags), (, flags))
OPENSC4_THIS_DETOUR(uint32_t, SetMargins, 0x008FB127, SetMargins,
                    (, int32_t left, int32_t top, int32_t right, int32_t bottom), (, left, top, right, bottom))
OPENSC4_CDECL_DETOUR(int32_t*, CopyFontSizes, 0x008FB654, CopyFontSizes,
                     (const int32_t* first, const int32_t* last, int32_t* to), (first, last, to))
OPENSC4_THIS_DETOUR(int32_t*, SetFontSizes, 0x008FB66F, SetFontSizes, (, const int32_t* sizes), (, sizes))
OPENSC4_THIS_DETOUR(bool, SetBase, 0x008FBBB9, SetBase, (, const void* text), (, text))
OPENSC4_THIS_DETOUR(uint32_t, GetShownURL, 0x008FC327, GetShownURL, (, Text* out), (, out))
OPENSC4_THIS_DETOUR(uint32_t, GetAskedURL, 0x008FC350, GetAskedURL, (, Text* out), (, out))
OPENSC4_THIS_DETOUR(uint32_t, PostLoadState, 0x008FA9A7, PostLoadState, (), ())

// ====================================================================================
// Installation

struct Detour {
    std::uintptr_t address;
    void* detour;
    void** original;
};

template <typename Fn>
void** Slot(Fn* fn)
{
    return reinterpret_cast<void**>(fn);
}

#define OPENSC4_D(Name, Address) {Address, reinterpret_cast<void*>(&D##Name), Slot(&g_##Name)}

template <std::size_t N>
bool InstallAll(const char* name, const Detour (&detours)[N])
{
    if (!game::IsBuild641())
        return false;
    for (const Detour& detour : detours) {
        const addr::Entry* entry = addr_check::Find(detour.address);
        if (!entry || !hook::Install(*entry, detour.detour, detour.original))
            return false;
    }
    log::Write("html window: %s: %zu functions ours", name, N);
    return true;
}

bool InstallLife(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(Construct, addr::HTMLWindow_Construct),
        OPENSC4_D(Create, addr::HTMLWindow_Create),
        OPENSC4_D(Destruct, addr::HTMLWindow_Destruct),
        OPENSC4_D(Delete, addr::HTMLWindow_Delete),
        OPENSC4_D(OnDestroy, addr::HTMLWindow_OnDestroy),
        OPENSC4_D(OnCreate, addr::HTMLWindow_OnCreate),
        OPENSC4_D(ConstructServiceRef, addr::HTMLServiceRef_Construct),
        OPENSC4_D(DestructServiceRef, addr::HTMLServiceRef_Destruct),
        OPENSC4_D(SetColour, addr::HTMLWindow_SetColour),
        OPENSC4_D(DestructContext, addr::HTMLLayout_Destruct),
    };
    return InstallAll("life", detours);
}

bool InstallShow(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(ShowHTML, addr::HTMLWindow_ShowHTML),
        OPENSC4_D(ShowHTMLText, addr::HTMLWindow_ShowHTMLText),
        OPENSC4_D(DropScrollBars, addr::HTMLWindow_DropScrollBars),
        OPENSC4_D(Relayout, addr::HTMLWindow_Relayout),
        OPENSC4_D(OnMessage, addr::HTMLWindow_OnMessage),
        OPENSC4_D(OnTimer, addr::HTMLWindow_OnTimer),
        OPENSC4_D(GetText, addr::HTMLWindow_GetText),
    };
    return InstallAll("show", detours);
}

bool InstallSlots(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(QueryInterface, addr::HTMLWindow_QueryInterface),
        OPENSC4_D(QueryInterfaceFace, addr::HTMLWindow_QueryInterfaceD8),
        OPENSC4_D(QueryInterfaceTarget, addr::HTMLWindow_QueryInterfaceDC),
        OPENSC4_D(AddRefTarget, addr::HTMLWindow_AddRefDC),
        OPENSC4_D(ReleaseTarget, addr::HTMLWindow_ReleaseDC),
        OPENSC4_D(GetClassID, addr::HTMLWindow_GetClassID),
    };
    return InstallAll("slots", detours);
}

bool InstallSettings(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(GetLoadState, addr::HTMLWindow_GetLoadState),
        OPENSC4_D(SetMissingImage, addr::HTMLWindow_SetMissingImage),
        OPENSC4_D(GetPageSize, addr::HTMLWindow_GetPageSize),
        OPENSC4_D(AddFlags, addr::HTMLWindow_AddFlags),
        OPENSC4_D(SetFlags, addr::HTMLWindow_SetFlags),
        OPENSC4_D(RemoveFlags, addr::HTMLWindow_RemoveFlags),
        OPENSC4_D(HasFlags, addr::HTMLWindow_HasFlags),
        OPENSC4_D(SetMargins, addr::HTMLWindow_SetMargins),
        OPENSC4_D(CopyFontSizes, addr::HTMLWindow_CopyFontSizes),
        OPENSC4_D(SetFontSizes, addr::HTMLWindow_SetFontSizes),
        OPENSC4_D(SetBase, addr::HTMLWindow_SetBase),
        OPENSC4_D(GetShownURL, addr::HTMLWindow_GetShownURL),
        OPENSC4_D(GetAskedURL, addr::HTMLWindow_GetAskedURL),
        OPENSC4_D(PostLoadState, addr::HTMLWindow_PostLoadState),
    };
    return InstallAll("settings", detours);
}

const bool g_registered[] = {
    registry::Register({"ui.html.window.life", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLWindow_Construct,
                        "Our HTML window's life: its constructor, factory, destructors, slots 4 and 5, the "
                        "service's smart pointer, the colour, the layout context's destructor (10 detours; "
                        "docs/html.md)",
                        false, InstallLife}),
    registry::Register({"ui.html.window.show", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLWindow_ShowHTML,
                        "Our HTML window's pages: ShowHTML, slot 22, the scroll bars dropped, the page laid out "
                        "again, the service's answer, the timer, the text (7 detours; docs/html.md)",
                        false, InstallShow}),
    registry::Register({"ui.html.window.slots", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLWindow_QueryInterface,
                        "Our HTML window's QueryInterface, its parts' jumps and the class ID (6 detours; "
                        "docs/html.md)",
                        false, InstallSlots}),
    registry::Register({"ui.html.window.settings", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLWindow_SetFontSizes,
                        "Our HTML window's settings: the load state, the missing picture, the flags, the margins, "
                        "the font steps, the base and the addresses (14 detours; docs/html.md)",
                        false, InstallSettings}),
};

} // namespace
} // namespace opensc4::html_window
