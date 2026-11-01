// Our part of the HTML window (docs/html.md, piece h1, its parts h1a, h1b and
// h1c: "Ours: the window"): the window's life, showing a page, its COM slots
// and its settings (h1a); its keys and mouse, its view and scroll bars, its
// paint and its addresses (h1b); the viewer's directors and a shared block's
// release (h1c), written by hand from the disassembly. Each
// function is a detour on the game's, in groups, off by default:
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
//   ui.html.window.input     the keys (slot 130), the mouse (134, 136, 138,
//                            139), the node at a point, a link pressed
//   ui.html.window.scroll    the view moved to a point and to an anchor, the
//                            bars' pictures, a bar's message, the bars made,
//                            checked, their ranges and places
//   ui.html.window.paint     paint (slot 88), the area (slot 55), the client
//                            rectangle, the page's size, the margins
//   ui.html.window.address   the service, going to an address, a string and
//                            a fragment
//   ui.html.directors        the HTML director and the second, empty one:
//                            their constructors, IDs (slot 13) and static
//                            getters (h1c; they run only while the game
//                            starts)
//   ui.html.block            the release of the file cache's and the
//                            service's shared block (h1c)
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
// window's Init (whose answer this is), the two cursors (slot 138's; h1a
// took them for fonts), the missing picture's
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
// h1b: the area, paint and the page's size

// More fields of the window (h1b).
constexpr std::size_t kArea = 0x24;         // the framework window's area, which the client rectangle comes from
constexpr std::size_t kPicture = 0x68;      // the framework window's picture (drawn into)
constexpr std::size_t kOuter = 0xA8;        // the framework window's area as SetArea keeps it
constexpr std::size_t kVerticalPicture = 0x1AC;   // the bars' pictures and their parts
constexpr std::size_t kHorizontalPicture = 0x1B0;
constexpr std::size_t kVerticalPart = 0x1B4;
constexpr std::size_t kHorizontalPart = 0x1C4;
constexpr std::size_t kFrameColour = 0x1D4;

int32_t& Int(char* self, std::size_t field) { return At<int32_t>(self, field); }

// A message posted to the parent (+0x4C) through the window's slot 145.
struct Message {
    uint32_t id;
    char* window;
    uint32_t first;
    uint32_t second;
};

uint8_t Post(char* window, uint32_t id, uint32_t first, uint32_t second)
{
    Message message{id, window, first, second};
    return Virtual<uint8_t>(window, 0x244, At<uint32_t>(window, 0x4C), &message);
}

// The client rectangle (0x008FAC01): the area less the margins, the view's
// right and bottom moved with it. 1 when it changed.
uint32_t UpdateClientRect(char* self)
{
    int32_t before[4];
    for (std::size_t at = 0; at < 0x10; at += 4)
        before[at / 4] = Int(self, kClient + at);
    for (std::size_t at = 0; at < 0x10; at += 4)
        Int(self, kClient + at) = Int(self, kArea + at);
    Int(self, kClient) += Int(self, 0x184);
    Int(self, kClient + 8) -= Int(self, 0x18C);
    Int(self, kClient + 4) += Int(self, 0x188);
    Int(self, kClient + 0xC) -= Int(self, 0x190);
    Int(self, kView + 8) = Int(self, kView) + Int(self, kClient + 8) - Int(self, kClient);
    Int(self, kView + 0xC) = Int(self, kView + 4) - Int(self, kClient + 4) + Int(self, kClient + 0xC);
    const uint32_t same = CallThis<uint32_t>(addr::SC4Rect_EqualsFolded, static_cast<void*>(before), self + kClient);
    return static_cast<uint8_t>(same) == 0 ? 1u : 0u;
}

// Slot 55 (0x008FAC82): the framework window's area, the client rectangle;
// the page laid out again when the size changed, else +0x48 drawn again.
uint32_t SetArea(char* self, int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    int32_t before[2] = {Int(self, kOuter + 8) - Int(self, kOuter), Int(self, kOuter + 0xC) - Int(self, kOuter + 4)};
    CallThis<uint8_t>(addr::GZWin_SetArea, self, left, top, right, bottom);
    CallThis<uint32_t>(addr::HTMLWindow_UpdateClientRect, self);
    int32_t after[2] = {Int(self, kOuter + 8) - Int(self, kOuter), Int(self, kOuter + 0xC) - Int(self, kOuter + 4)};
    if (CallThis<uint8_t>(addr::SC4UI_F798070, static_cast<void*>(before), static_cast<void*>(after)) == 0)
        return CallThis<uint32_t>(addr::HTMLWindow_Relayout, self);
    if (void* const other = At<void*>(self, 0x48))
        return Virtual<uint32_t>(other, 0x170);
    return 1;
}

// The page laid out as slot 10 and the paint do it: the context begun, the
// body's slots 3 and 4, the document told (slot 15) and the scroll bars.
void LayOut(char* self, char* body)
{
    char* const context = self + kContext;
    CallThis<void*>(addr::HTMLLayout_Begin, context);
    Virtual<uint32_t>(body, 0xC, context);
    Virtual<uint32_t>(body, 0x10);
    Virtual<uint32_t>(At<char*>(self, kDocument), 0x3C, uint32_t{1});
    CallThis<uint8_t>(addr::HTMLWindow_UpdateScrollBars, self);
}

// Slot 10 of the +0xD8 interface (0x008FB5E5): the body's size, laid out
// first when the document says it is not.
int32_t* GetDocumentSize(char* face, int32_t* size)
{
    char* const self = face - kFace;
    char* const body = Virtual<char*>(At<char*>(self, kDocument), 0x30);
    if (!body) {
        size[1] = 0;
        size[0] = 0;
        return size;
    }
    if (Virtual<uint8_t>(At<char*>(self, kDocument), 0x38) == 0)
        LayOut(self, body);
    size[0] = Int(body, 0x14);
    size[1] = Int(body, 0x18);
    return size;
}

// Slot 31 of the +0xD8 interface (0x008FA4BB): the margins; EAX the last.
uint32_t GetMargins(char* face, int32_t* left, int32_t* top, int32_t* right, int32_t* bottom)
{
    *left = Field<int32_t>(face, 0x184);
    *top = Field<int32_t>(face, 0x188);
    *right = Field<int32_t>(face, 0x18C);
    *bottom = Field<int32_t>(face, 0x190);
    return static_cast<uint32_t>(*bottom);
}

// The window's colour over its area when the picture locks for it (0x8001):
// slot 103's colour through the picture's slot 20 (the picture's vtable read
// before slot 103, as the game does).
void FillBackground(char* self)
{
    if (Virtual<uint8_t>(At<void*>(self, kPicture), 0x18, uint32_t{0x8001}) == 0)
        return;
    void** const vtable = At<void**>(At<char*>(self, kPicture), 0);
    const uint32_t colour = Virtual<uint32_t>(self, 0x19C);
    reinterpret_cast<uint32_t(__thiscall*)(void*, uint32_t, void*, void*)>(vtable[20])(
        At<void*>(self, kPicture), colour, self + kArea, nullptr);
    Virtual<uint32_t>(At<void*>(self, kPicture), 0x1C, uint32_t{0x8001});
}

// Slot 88 (0x008FB3F5): with a page shown, the background, the frame and
// the page.
bool Paint(char* self)
{
    if (At<uint32_t>(self, kContext + 8) != At<uint32_t>(self, kPicture))
        At<uint32_t>(self, kContext + 8) = At<uint32_t>(self, kPicture);
    if (At<uint32_t>(self, kLoadState) != 2)
        return true;
    char* const doc = At<char*>(self, kDocument);
    char* const body = doc ? Virtual<char*>(doc, 0x30) : nullptr;
    if (!body) {
        if (At<uint32_t>(self, kFlags) & 0x80)
            FillBackground(self);
        return true;
    }
    if (static_cast<uint8_t>(CallThis<uint32_t>(addr::HTMLWindow_UpdateClientRect, self)) != 0)
        Virtual<uint32_t>(At<char*>(self, kDocument), 0x3C, uint32_t{0});
    if (const uint32_t* const colour = At<const uint32_t*>(body, 0x2C))
        CallThis<uint8_t>(addr::GZWin_SetBackgroundValue, self, *colour);
    if (At<void*>(body, 0x3C)) {
        if (Virtual<uint8_t>(At<void*>(self, kPicture), 0x18, uint32_t{0x8010}) != 0) {
            // BACKGROUND tiled over the area, lined up with the view's top
            // unless +0x40 says otherwise.
            int32_t shift = 0;
            if (At<uint8_t>(body, 0x40) == 0) {
                const int32_t height = Virtual<int32_t>(At<void*>(body, 0x3C), 0x28);
                shift = height - Int(self, kView + 4) % height;
            }
            void* const source = Virtual<void*>(At<void*>(body, 0x3C), 0x30);
            CallPlain<uint8_t>(addr::GZBuffer_TileBlit, At<void*>(self, kPicture), At<void*>(body, 0x3C), source,
                               static_cast<void*>(self + kArea), int32_t{0}, shift, static_cast<void*>(nullptr));
            Virtual<uint32_t>(At<void*>(self, kPicture), 0x1C, uint32_t{0x8010});
        }
    } else if (At<uint32_t>(self, kFlags) & 0x80) {
        FillBackground(self);
    }
    if ((At<uint32_t>(self, kFlags) & 0x40) &&
        Virtual<uint8_t>(At<void*>(self, kPicture), 0x18, uint32_t{0x8008}) != 0) {
        CallPlain<uint8_t>(addr::GZDraw_Frame, At<void*>(self, kPicture), static_cast<void*>(self + kArea),
                           At<uint32_t>(self, kFrameColour), uint32_t{1});
        Virtual<uint32_t>(At<void*>(self, kPicture), 0x1C, uint32_t{0x8008});
    }
    if (Virtual<uint8_t>(At<char*>(self, kDocument), 0x38) == 0)
        LayOut(self, body);
    CallThis<void*>(addr::HTMLLayout_Begin, self + kContext);
    Virtual<uint32_t>(body, 0x14, self + kContext);
    return true;
}

// ====================================================================================
// h1b: the view and the scroll bars

// Slot 7 of the +0xD8 interface (0x008FAD0E): the view moved to (x, y),
// clamped to the page; EAX the clamped x when it did not move.
uint32_t ScrollTo(char* face, int32_t x, int32_t y)
{
    char* const self = face - kFace;
    int32_t size[2];
    Virtual<uint32_t>(face, 0x28, static_cast<void*>(size));
    int32_t left = 0;
    int32_t top = 0;
    Virtual<uint32_t>(face, 0x18, &left, &top);
    const int32_t width = Int(self, kView + 8) - Int(self, kView);
    const int32_t height = Int(self, kView + 0xC) - Int(self, kView + 4);
    char* const body = Virtual<char*>(At<char*>(self, kDocument), 0x30);
    const int32_t mostX = Int(body, 0x14) - width;
    const int32_t mostY = Int(body, 0x18) - height;
    x = mostX < x ? mostX : x;
    x = x > 0 ? x : 0;
    y = mostY < y ? mostY : y;
    y = y > 0 ? y : 0;
    if (left == x && top == y)
        return static_cast<uint32_t>(x);
    Int(self, kView + 0xC) = height + y;
    Int(self, kView) = x;
    Int(self, kView + 4) = y;
    Int(self, kView + 8) = width + x;
    if (void* const bar = At<void*>(self, kHorizontal))
        Virtual<uint32_t>(bar, 0x14, x);
    if (void* const bar = At<void*>(self, kVertical))
        Virtual<uint32_t>(bar, 0x14, y);
    return Virtual<uint32_t>(self, 0x170);
}

// Slot 8 of the +0xD8 interface (0x008FA671): an anchor's box.
bool GetAnchorRect(char* face, const void* name, int32_t* rect)
{
    char* const node = Virtual<char*>(Field<char*>(face, kDocument), 0x34, name);
    if (!node || At<uint8_t>(node, 9) == 0)
        return false;
    for (std::size_t at = 0; at < 0x10; at += 4)
        rect[at / 4] = Int(node, 0xC + at);
    return true;
}

// Slot 9 of the +0xD8 interface (0x008FADE4): the view moved so that an
// anchor's box is in it.
bool ScrollToAnchor(char* face, const void* name)
{
    char* const self = face - kFace;
    char* const node = Virtual<char*>(At<char*>(self, kDocument), 0x34, name);
    if (!node || At<uint8_t>(node, 9) == 0)
        return false;
    const int32_t height = Int(self, kOuter + 0xC) - Int(self, 0x190) - Int(self, 0x188) - Int(self, kOuter + 4);
    const int32_t width = Int(self, kOuter + 8) - Int(self, 0x18C) - Int(self, 0x184) - Int(self, kOuter);
    int32_t x = 0;
    int32_t y = 0;
    Virtual<uint32_t>(face, 0x18, &x, &y);
    if (Int(node, 0x14) > x + width)
        x += Int(node, 0xC) - Int(node, 0x14) + width;
    if (Int(node, 0xC) < x)
        x = Int(node, 0xC);
    if (Int(node, 0x18) > y + height)
        y += Int(node, 0x10) - Int(node, 0x18) + height;
    if (Int(node, 0x10) < y)
        y = Int(node, 0x10);
    Virtual<uint32_t>(face, 0x1C, x, y);
    return true;
}

// A bar given its picture and part (its slot 17): the part, or the
// picture's own (its slot 12) when the part is empty.
void GiveBarPicture(char* self, std::size_t bar, std::size_t part, void* picture)
{
    if (CallThis<uint8_t>(addr::cRZRect_IsEmpty, self + part) != 0) {
        void* const own = Virtual<void*>(picture, 0x30);
        Virtual<uint32_t>(At<void*>(self, bar), 0x44, own, picture, uint32_t{0});
    } else {
        Virtual<uint32_t>(At<void*>(self, bar), 0x44, static_cast<void*>(self + part), picture, uint32_t{0});
    }
}

// Slots 11 and 12 of the +0xD8 interface (0x008FAEA0, 0x008FAF21).
bool SetBarPicture(char* face, std::size_t held, std::size_t part, std::size_t bar, void* picture,
                   const int32_t* rect)
{
    char* const self = face - kFace;
    if (picture)
        Virtual<uint32_t>(picture, 0x4);
    void*& kept = At<void*>(self, held);
    if (kept)
        Virtual<uint32_t>(kept, 0x8);
    kept = picture;
    for (std::size_t at = 0; at < 0x10; at += 4)
        Int(self, part + at) = rect[at / 4];
    if (picture && At<void*>(self, bar))
        GiveBarPicture(self, bar, part, picture);
    return true;
}

bool SetVerticalBarPicture(char* face, void* picture, const int32_t* rect)
{
    return SetBarPicture(face, kVerticalPicture, kVerticalPart, kVertical, picture, rect);
}

bool SetHorizontalBarPicture(char* face, void* picture, const int32_t* rect)
{
    return SetBarPicture(face, kHorizontalPicture, kHorizontalPart, kHorizontal, picture, rect);
}

// Slots 13 and 14 (0x008FA69F, 0x008FA6C9): AL whether there is a picture,
// the rest of EAX the first argument's.
uint32_t GetBarPicture(char* face, std::size_t held, std::size_t part, void** picture, int32_t* rect)
{
    *picture = Field<void*>(face, held);
    for (std::size_t at = 0; at < 0x10; at += 4)
        rect[at / 4] = Field<int32_t>(face, part + at);
    const uint32_t first = static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(picture));
    return (first & 0xFFFFFF00u) | (*picture != nullptr ? 1u : 0u);
}

uint32_t GetVerticalBarPicture(char* face, void** picture, int32_t* rect)
{
    return GetBarPicture(face, kVerticalPicture, kVerticalPart, picture, rect);
}

uint32_t GetHorizontalBarPicture(char* face, void** picture, int32_t* rect)
{
    return GetBarPicture(face, kHorizontalPicture, kHorizontalPart, picture, rect);
}

// Slot 143 (0x008FA9F4): a scroll bar moved (message 0x887113A3 from its
// window's ID, slot 63): the view to its position.
bool OnScrollMessage(char* self, uint32_t id, uint32_t sender)
{
    if (id != 0x887113A3u)
        return true;
    const auto from = [&](std::size_t bar) {
        void* const window = Virtual<void*>(At<void*>(self, bar), 0xC);
        return sender == Virtual<uint32_t>(window, 0xFC);
    };
    if (At<void*>(self, kVertical) && from(kVertical)) {
        const int32_t height = Int(self, kView + 0xC) - Int(self, kView + 4);
        const int32_t top = Virtual<int32_t>(At<void*>(self, kVertical), 0x18);
        Int(self, kView + 4) = top;
        Int(self, kView + 0xC) = top + height;
        Virtual<uint32_t>(self, 0x170);
    } else if (At<void*>(self, kHorizontal) && from(kHorizontal)) {
        const int32_t width = Int(self, kView + 8) - Int(self, kView);
        const int32_t left = Virtual<int32_t>(At<void*>(self, kHorizontal), 0x18);
        Int(self, kView) = left;
        Int(self, kView + 8) = left + width;
        Virtual<uint32_t>(self, 0x170);
    }
    return true;
}

// The bars made (0x008FAFA2): each asked for, allowed and not there, by the
// window factory's slot 17, given its picture and made a child; when the
// factory gives none, the flag taken out.
bool MakeScrollBars(char* self, uint8_t vertical, uint8_t horizontal)
{
    bool made = false;
    const auto make = [&](std::size_t field, uint32_t first, uint32_t second, std::size_t held, std::size_t part,
                          uint32_t flag) {
        made = true;
        void* const factory = CallPlain<void*>(addr::SC4_Singleton913C72);
        void* const bar = Virtual<void*>(factory, 0x44, first, second, uint32_t{0}, uint32_t{100}, uint32_t{0});
        At<void*>(self, field) = bar;
        if (!bar) {
            Virtual<uint32_t>(self + kFace, 0x70, flag);
            return;
        }
        if (void* const picture = At<void*>(self, held))
            GiveBarPicture(self, field, part, picture);
        void* const child = Virtual<void*>(At<void*>(self, field), 0xC);
        Virtual<uint32_t>(self, 0x38, child);
    };
    if (vertical && (At<uint32_t>(self, kFlags) & 8) && !At<void*>(self, kVertical))
        make(kVertical, 0, 2, kVerticalPicture, kVerticalPart, 8);
    if (horizontal && (At<uint32_t>(self, kFlags) & 4) && !At<void*>(self, kHorizontal))
        make(kHorizontal, 1, 1, kHorizontalPicture, kHorizontalPart, 4);
    if (made) {
        CallThis<uint32_t>(addr::HTMLWindow_Relayout, self);
        CallThis<void>(addr::HTMLWindow_PlaceScrollBars, self);
        CallThis<void>(addr::HTMLWindow_SetScrollRanges, self, uint8_t{1});
    }
    return true;
}

// The bars checked (0x008FB31A): each allowed one dropped when the page
// fits the view, made when it does not.
bool UpdateScrollBars(char* self)
{
    if (!At<char*>(self, kDocument) || !Virtual<char*>(At<char*>(self, kDocument), 0x30))
        return true;
    char* const body = Virtual<char*>(At<char*>(self, kDocument), 0x30);
    uint8_t addVertical = 0;
    uint8_t addHorizontal = 0;
    uint8_t dropVertical = 0;
    uint8_t dropHorizontal = 0;
    const uint32_t flags = At<uint32_t>(self, kFlags);
    if (flags & 8) {
        if (Int(body, 0x18) > Int(self, kView + 0xC) - Int(self, kView + 4)) {
            if (!At<void*>(self, kVertical))
                addVertical = 1;
        } else if (At<void*>(self, kVertical)) {
            dropVertical = 1;
        }
    }
    if (flags & 4) {
        if (Int(body, 0x14) > Int(self, kView + 8) - Int(self, kView)) {
            if (!At<void*>(self, kHorizontal))
                addHorizontal = 1;
        } else if (At<void*>(self, kHorizontal)) {
            dropHorizontal = 1;
        }
    }
    if (dropVertical || dropHorizontal)
        CallThis<void>(addr::HTMLWindow_DropScrollBars, self, dropVertical, dropHorizontal);
    if (addVertical || addHorizontal)
        CallThis<uint8_t>(addr::HTMLWindow_MakeScrollBars, self, addVertical, addHorizontal);
    return true;
}

// 0 for a negative difference (the game's `jns`).
int32_t AtLeastZero(int32_t a, int32_t b)
{
    const int32_t d = static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
    return d < 0 ? 0 : d;
}

// The bars' ranges and positions (0x008FA776). The horizontal ones go to
// the vertical bar, as in the game.
void SetScrollRanges(char* self, uint8_t ranges)
{
    if (!At<char*>(self, kDocument) || !Virtual<char*>(At<char*>(self, kDocument), 0x30))
        return;
    char* const body = Virtual<char*>(At<char*>(self, kDocument), 0x30);
    if (At<void*>(self, kVertical)) {
        if (ranges) {
            const int32_t height = Int(body, 0x18);
            const int32_t step = height / 20;
            const int32_t view = Int(self, kView + 0xC) - Int(self, kView + 4);
            Virtual<uint32_t>(At<void*>(self, kVertical), 0x1C, int32_t{0}, AtLeastZero(height, view));
            Virtual<uint32_t>(At<void*>(self, kVertical), 0x3C, step, height / view + 1);
        }
        Virtual<uint32_t>(At<void*>(self, kVertical), 0x14, Int(self, kView + 4));
    }
    if (At<void*>(self, kHorizontal)) {
        if (ranges) {
            const int32_t width = Int(body, 0x14);
            const int32_t step = width / 15;
            const int32_t view = Int(self, kView + 8) - Int(self, kView);
            Virtual<uint32_t>(At<void*>(self, kVertical), 0x1C, int32_t{0}, AtLeastZero(width, view));
            Virtual<uint32_t>(At<void*>(self, kVertical), 0x3C, step, width / view + 1);
        }
        Virtual<uint32_t>(At<void*>(self, kVertical), 0x14, Int(self, kView));
    }
}

// The bars placed (0x008FA88A): their windows' areas (slot 55) at the right
// and the bottom edge.
void PlaceScrollBars(char* self)
{
    if (At<void*>(self, kVertical)) {
        void* const sized = Virtual<void*>(At<void*>(self, kVertical), 0xC);
        void* const placed = Virtual<void*>(At<void*>(self, kVertical), 0xC);
        const int32_t height = Virtual<int32_t>(self, 0xA8);
        const int32_t width = Virtual<int32_t>(self, 0xA4);
        const int32_t barWidth = Virtual<int32_t>(sized, 0xA4);
        const int32_t left = 1 - barWidth + Virtual<int32_t>(self, 0xA4);
        Virtual<uint32_t>(placed, 0xDC, left, int32_t{0}, width, height);
    }
    if (At<void*>(self, kHorizontal)) {
        void* const sized = Virtual<void*>(At<void*>(self, kHorizontal), 0xC);
        void* const placed = Virtual<void*>(At<void*>(self, kHorizontal), 0xC);
        const int32_t height = Virtual<int32_t>(self, 0xA8);
        const int32_t width = Virtual<int32_t>(self, 0xA4);
        const int32_t barHeight = Virtual<int32_t>(sized, 0xA8);
        const int32_t top = Virtual<int32_t>(self, 0xA8) - barHeight;
        Virtual<uint32_t>(placed, 0xDC, int32_t{0}, top, width, height);
    }
}

// ====================================================================================
// h1b: the keys and the mouse

// Slot 130 (0x008FB7FE): a key, first offered to the parent (message
// 0xE4A85D8D); with flag 0x10 the view moved, then the bars and the window
// drawn again. Ctrl+R (and no Shift or Alt) reloads the page shown.
bool OnKey(char* self, uint32_t key, uint32_t modifiers)
{
    const bool shift = (modifiers & 1) != 0;
    const bool ctrl = ((modifiers >> 1) & 1) != 0;
    const bool alt = ((modifiers >> 2) & 1) != 0;
    if (Post(self, 0xE4A85D8Du, key, modifiers) != 0)
        return true;
    if (!At<char*>(self, kDocument) || !Virtual<char*>(At<char*>(self, kDocument), 0x30))
        return true;
    char* const body = Virtual<char*>(At<char*>(self, kDocument), 0x30);
    const auto scrolls = [&] { return (At<uint32_t>(self, kFlags) & 0x10) != 0; };
    int32_t& left = Int(self, kView);
    int32_t& top = Int(self, kView + 4);
    int32_t& right = Int(self, kView + 8);
    int32_t& bottom = Int(self, kView + 0xC);
    const auto clientHeight = [&] { return Int(self, kClient + 0xC) - Int(self, kClient + 4); };
    // Down by a step, held at the page's bottom.
    const auto down = [&](int32_t step) {
        const int32_t height = bottom - top;
        top += step;
        bottom = top + height;
        if (bottom > Int(body, 0x18)) {
            top = Int(body, 0x18) - height;
            bottom = top + height;
        }
    };
    // Up by a (negative) step, held at 0.
    const auto up = [&](int32_t step) {
        const int32_t height = bottom - top;
        top += step;
        bottom = top + height;
        if (top < 0) {
            bottom = height;
            top = 0;
        }
    };
    switch (key) {
    case 0x24: // Home
        if (!scrolls() || (top == 0 && left == 0))
            return true;
        {
            const int32_t height = bottom - top;
            const int32_t width = right - left;
            bottom = height;
            left = 0;
            right = width;
            top = 0;
        }
        break;
    case 0x23: // End: the view's right made its height
        if (!scrolls() || bottom == Int(body, 0x18))
            return true;
        {
            const int32_t height = bottom - top;
            top = Int(body, 0x18) - height;
            bottom = Int(body, 0x18);
            left = 0;
            right = height;
            if (top < 0) {
                top = 0;
                bottom = height;
            }
        }
        break;
    case 0x22: // Page Down
        if (!scrolls() || bottom >= Int(body, 0x18))
            return true;
        down(static_cast<int32_t>(static_cast<uint32_t>(clientHeight()) * 9u) / 10);
        break;
    case 0x28: // Down
        if (!scrolls() || bottom >= Int(body, 0x18))
            return true;
        down(clientHeight() / 25);
        break;
    case 0x21: // Page Up
        if (!scrolls() || top <= 0)
            return true;
        up(static_cast<int32_t>(static_cast<uint32_t>(Int(self, kClient + 4) - Int(self, kClient + 0xC)) * 9u) / 10);
        break;
    case 0x26: // Up
        if (!scrolls() || top <= 0)
            return true;
        up(clientHeight() / -25);
        break;
    case 0x25: // Left
        if (!scrolls() || left <= 0)
            return true;
        {
            const int32_t width = right - left;
            left += width / -25;
            right = left + width;
            if (left < 0) {
                left = 0;
                right = width;
            }
        }
        break;
    case 0x27: // Right
        if (!scrolls() || right >= Int(body, 0x14))
            return true;
        {
            const int32_t width = right - left;
            left += width / 25;
            right = left + width;
            if (right > Int(body, 0x14)) {
                left = Int(body, 0x14) - width;
                right = left + width;
            }
        }
        break;
    case 0x52: // R
        if (ctrl && !shift && !alt) {
            char* const face = self + kFace;
            Text shown;
            CallThis<void*>(addr::SC4String_SC4String, &shown);
            Virtual<uint32_t>(face, 0x3C, &shown);
            Virtual<uint8_t>(face, 0x48, &shown, uint32_t{1});
            shown.vtable = static_cast<uint32_t>(addr::SC4String_Vtable);
            if (shown.begin)
                Free(shown.begin);
        }
        return true;
    default:
        return true;
    }
    CallThis<void>(addr::HTMLWindow_SetScrollRanges, self, uint8_t{1});
    Virtual<uint32_t>(self, 0x170);
    return true;
}

// The node at a point (0x008FA57D): the body's hit test (slot 7) answering
// 2, the point moved by the view less the client rectangle's corner.
bool HitNode(char* self, char** node, int32_t x, int32_t y)
{
    char* const doc = At<char*>(self, kDocument);
    if (!doc)
        return false;
    char* const body = Virtual<char*>(doc, 0x30);
    if (!body || At<uint8_t>(body, 9) == 0)
        return false;
    if (Virtual<uint8_t>(self, 0x1E4, x, y) == 0)
        return false;
    const int32_t atY = Int(self, kView + 4) - Int(self, kClient + 4) + y;
    const int32_t atX = Int(self, kView) - Int(self, kClient) + x;
    return Virtual<uint32_t>(body, 0x1C, node, atX, atY) == 2;
}

// The lock held over a mouse slot: the game's guard (0x0048C9F0, which
// enters it), left through the lock's slot 2.
struct Guard {
    uint32_t vtable;
    void* lock;
};

// Slots 134 and 136 (0x008FAAA8, 0x008FAB08): under a reference and the
// lock, the node hit pressed (its slot 8) or let go (9); let go over an A
// with an address, slot 25 of the +0xD8 interface.
bool OnMouseButton(char* self, int32_t x, int32_t y, bool down)
{
    void* held = nullptr;
    CallThis<void*>(addr::cGZDBSegmentPackedFile_CountedPointerConstruct, &held, self);
    Guard guard;
    CallThis<void*>(addr::SC4LockGuard_Construct, &guard, self + kLock);
    char* node = nullptr;
    if (CallThis<uint8_t>(addr::HTMLWindow_HitNode, self, &node, x, y) != 0) {
        Virtual<uint32_t>(node, down ? 0x20 : 0x24, self + kContext);
        if (!down && At<uint32_t>(node, 4) == 4) {
            void* address = At<void*>(node, 0x2C);
            if (!address)
                address = At<void*>(node, 0x28);
            if (address)
                Virtual<uint8_t>(self + kFace, 0x64, address);
        }
    }
    Virtual<uint32_t>(guard.lock, 0x8);
    if (held)
        Virtual<uint32_t>(held, 0x8);
    return true;
}

bool OnMouseDown(char* self, int32_t x, int32_t y, uint32_t) { return OnMouseButton(self, x, y, true); }
bool OnMouseUp(char* self, int32_t x, int32_t y, uint32_t) { return OnMouseButton(self, x, y, false); }

// Slot 138 (0x008FAB8B): under the lock, with flag 0x20, the cursor: +0x164
// over an A with an address, else +0x168.
bool OnMouseMove(char* self, int32_t x, int32_t y, uint32_t)
{
    Guard guard;
    CallThis<void*>(addr::SC4LockGuard_Construct, &guard, self + kLock);
    if (At<uint32_t>(self, kFlags) & 0x20) {
        char* node = nullptr;
        const bool link = CallThis<uint8_t>(addr::HTMLWindow_HitNode, self, &node, x, y) != 0 &&
                          At<uint32_t>(node, 4) == 4 && (At<void*>(node, 0x28) || At<void*>(node, 0x2C));
        Virtual<uint32_t>(self, 0x154, At<void*>(self, link ? 0x164 : 0x168), uint32_t{1});
    }
    Virtual<uint32_t>(guard.lock, 0x8);
    return true;
}

// Slot 139 (0x008FA4EE): the wheel, without the mouse captured: a key up
// (0x26) or down (0x28) for each 100, then flag 0x10 set (the keys scroll
// only when it already was).
bool OnMouseWheel(char* self, uint32_t, uint32_t, uint32_t, int32_t delta)
{
    if (CallThis<uint8_t>(addr::SC4Win_F99BE0A, self) != 0)
        return true;
    const uint32_t flags = At<uint32_t>(self, kFlags) | 0x10;
    if (delta > 0) {
        for (int32_t n = delta / 100; n > 0; --n)
            Virtual<uint8_t>(self, 0x208, uint32_t{0x26}, uint32_t{0});
    } else if (delta < 0) {
        for (int32_t n = delta / -100; n > 0; --n)
            Virtual<uint8_t>(self, 0x208, uint32_t{0x28}, uint32_t{0});
    }
    At<uint32_t>(self, kFlags) = flags;
    return true;
}

// Slot 25 of the +0xD8 interface (0x008FB771): a link pressed, offered to
// the parent (message 0xE4A85D91 with a new game string of it); taken and
// with flag 0x20, gone to (slot 18).
bool OnLinkClicked(char* face, const void* text)
{
    char* const self = face - kFace;
    void* const memory = CallPlain<void*>(addr::SC4_operator_new, std::size_t{0x14});
    void* const made = memory ? CallThis<void*>(addr::SC4String_FromIGZString, memory, text) : nullptr;
    void* link = nullptr;
    CallThis<void*>(addr::SC4RefHolder_SC4RefHolder, &link, made);
    const uint8_t taken =
        Post(self, 0xE4A85D91u, static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(link)), 0);
    if (taken && (At<uint32_t>(self, kFlags) & 0x20))
        Virtual<uint8_t>(face, 0x48, link, uint32_t{0});
    if (link)
        Virtual<uint32_t>(link, 0x8);
    return taken != 0;
}

// ====================================================================================
// h1b: the addresses

// Slot 4 of the +0xD8 interface (0x008FA5F3): the Internet service.
bool SetService(char* face, void* service)
{
    void*& held = Field<void*>(face, kService);
    if (held != service) {
        if (service)
            Virtual<uint32_t>(service, 0x4);
        if (held)
            Virtual<uint32_t>(held, 0x8);
        held = service;
    }
    return true;
}

// A string's body (the string +4), null for none.
const void* Body(const Text* text) { return text ? &text->begin : nullptr; }

bool SameText(const Text* a, const Text* b)
{
    return CallPlain<uint8_t>(addr::SC4UI_F43AB70, Body(a), Body(b)) != 0;
}

// Slot 17 of the +0xD8 interface (0x008FB168): a page asked of the service.
bool GoToURL(char* face, char* url, uint8_t reload)
{
    char* const self = face - kFace;
    bool again = false;  // the page shown asked for again
    uint8_t fresh = 1;   // the service's last argument
    if (CallThis<uint8_t>(addr::GZWin_IsInitialized, self) == 0 || !Field<void*>(face, kService))
        return false;
    const Text* const text = CallThis<const Text*>(addr::SC4Url_Text, url);
    if (text->end == text->begin)
        return false;
    if (!reload) {
        const Text* const asked = CallThis<const Text*>(addr::SC4Url_Text, url);
        const Text* const shown = CallThis<const Text*>(addr::SC4Url_Text, self + kShown);
        if (SameText(shown, asked))
            return true;
    }
    const Text* const asked = CallThis<const Text*>(addr::SC4Url_Text, url);
    const Text* const shown = CallThis<const Text*>(addr::SC4Url_Text, self + kShown);
    if (SameText(shown, asked)) {
        again = true;
        fresh = 0;
        CallThis<void*>(addr::SC4Url_Assign, self + kAsked, self + kShown);
    } else {
        void* const base = self + kBase;
        const void* const from = CallThis<const void*>(addr::SC4Url_Text, url);
        CallThis<void*>(addr::SC4Url_ResolveFrom, self + kAsked, from, base);
    }
    uint32_t& state = At<uint32_t>(self, kLoadState);
    if (state == 1) {
        if (again)
            return true;
        Virtual<uint32_t>(face, 0x60);
    }
    state = 1;
    PostState(self, 1, 0);
    if (Virtual<uint32_t>(Field<void*>(face, kService), 0x40, self + kAsked, self + kTarget, uint32_t{0},
                          uint32_t{0}, uint32_t{fresh}) == 0) {
        state = 0;
        PostState(self, 0, 1);
    }
    return true;
}

// Slot 18 of the +0xD8 interface (0x008FBBF7): slot 17 from a string.
bool GoToURLText(char* face, const void* text, uint32_t reload)
{
    alignas(4) char address[0xC0];
    CallThis<void*>(addr::SC4Url_FromText, address, text);
    const uint8_t gone = Virtual<uint8_t>(face, 0x44, static_cast<void*>(address), reload);
    CallThis<void>(addr::SC4Url_Destroy, address);
    return gone != 0;
}

// Slot 20 of the +0xD8 interface (0x008FC2BD): the page shown's address
// with a fragment appended, through slot 18.
bool GoToFragment(char* face, const Text* fragment)
{
    Text address;
    CallThis<void*>(addr::SC4String_SC4String, &address);
    const void* const shown = CallThis<const void*>(addr::SC4Url_Text, face + (kShown - kFace));
    CallThis<void*>(addr::cRZBaseString_Assign, &address, shown);
    CallThis<void*>(addr::SC4String_AppendStd, &address.begin, Body(fragment));
    const uint8_t gone = Virtual<uint8_t>(face, 0x48, &address, uint32_t{0});
    address.vtable = static_cast<uint32_t>(addr::SC4String_Vtable);
    if (address.begin)
        Free(address.begin);
    return gone != 0;
}

// ====================================================================================
// h1c: the directors and the shared block's release

// The HTML director (0x4C bytes, made once at 0x00B623F0, its bit at
// 0x00B6243C, its teardown 0x00A7F38B) and the second, empty one (at
// 0x00B62440, its bit at 0x00B6248C, its teardown 0x00A7F395): each a COM
// director base with two vtables of its own.
constexpr std::uintptr_t kDirector = 0x00B623F0;
constexpr std::uintptr_t kDirectorMade = 0x00B6243C;
constexpr std::uintptr_t kDirectorTeardown = 0x00A7F38B;
constexpr std::uintptr_t kSubDirector = 0x00B62440;
constexpr std::uintptr_t kSubDirectorMade = 0x00B6248C;
constexpr std::uintptr_t kSubDirectorTeardown = 0x00A7F395;

// The HTML director's constructor (0x008F673E): the base, the vtables, the
// second director added to its list (the base's AddDirector), then the
// window's class (0x008FA317's ID) with its factory 0x008FBDBD.
char* ConstructDirector(char* self)
{
    CallThis<void*>(addr::cRZCOMDllDirector_Construct, self);
    At<uint32_t>(self, 0x0) = 0x00ACCC58;
    At<uint32_t>(self, 0x4) = 0x00ACCC28;
    void* const sub = CallPlain<void*>(addr::HTMLSubDirector_Get);
    CallThis<void>(addr::cRZCOMDllDirector_AddDirector, self, sub);
    const uint32_t classId = CallPlain<uint32_t>(addr::HTMLWindow_GetClassID);
    CallThis<uint32_t>(addr::cRZCOMDllDirector_AddCls, self, classId, static_cast<uint32_t>(addr::HTMLWindow_Create));
    return self;
}

// The second director's constructor (0x008FC379): the base and the
// vtables; it registers nothing.
char* ConstructSubDirector(char* self)
{
    CallThis<void*>(addr::cRZCOMDllDirector_Construct, self);
    At<uint32_t>(self, 0x0) = 0x00ACD378;
    At<uint32_t>(self, 0x4) = 0x00ACD34C;
    return self;
}

// Slot 13 of each (0x008F6776, 0x008FC392): the director's ID.
uint32_t DirectorId(char*) { return 0x649B0FBEu; }
uint32_t SubDirectorId(char*) { return 0xA49B0FD1u; }

// The static getters (0x008F677C, 0x008FC398): the director made the first
// time, with its teardown left to atexit.
char* GetStatic(std::uintptr_t director, std::uintptr_t made, std::uintptr_t construct, std::uintptr_t teardown)
{
    uint32_t& bits = *reinterpret_cast<uint32_t*>(made);
    if ((bits & 1) == 0) {
        bits |= 1;
        CallThis<char*>(construct, reinterpret_cast<char*>(director));
        CallPlain<int>(addr::SC4_atexit, teardown);
    }
    return reinterpret_cast<char*>(director);
}

char* GetDirector() { return GetStatic(kDirector, kDirectorMade, addr::HTMLDirector_Construct, kDirectorTeardown); }
char* GetSubDirector()
{
    return GetStatic(kSubDirector, kSubDirectorMade, addr::HTMLSubDirector_Construct, kSubDirectorTeardown);
}

// 0x008FC3C3, the file cache's and the service's shared block (its storage
// at +0, a 16-bit count at +8): the count taken down, or at the last (a
// count of 1 or 0) the storage and the block deleted. Answers the count
// left, 0 at the last.
uint32_t ReleaseBlock(char* self)
{
    const uint16_t count = At<uint16_t>(self, 0x8);
    if (count > 1) {
        At<uint16_t>(self, 0x8) = static_cast<uint16_t>(count - 1);
        return static_cast<uint16_t>(count - 1);
    }
    CallPlain<void>(addr::SC4_operator_delete, At<void*>(self, 0x0));
    CallPlain<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return 0;
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

// h1b: the keys and the mouse.
OPENSC4_THIS_DETOUR(bool, OnKey, 0x008FB7FE, OnKey, (, uint32_t key, uint32_t modifiers), (, key, modifiers))
OPENSC4_THIS_DETOUR(bool, OnMouseDown, 0x008FAAA8, OnMouseDown, (, int32_t x, int32_t y, uint32_t buttons),
                    (, x, y, buttons))
OPENSC4_THIS_DETOUR(bool, OnMouseUp, 0x008FAB08, OnMouseUp, (, int32_t x, int32_t y, uint32_t buttons),
                    (, x, y, buttons))
OPENSC4_THIS_DETOUR(bool, OnMouseMove, 0x008FAB8B, OnMouseMove, (, int32_t x, int32_t y, uint32_t buttons),
                    (, x, y, buttons))
OPENSC4_THIS_DETOUR(bool, HitNode, 0x008FA57D, HitNode, (, char** node, int32_t x, int32_t y), (, node, x, y))
OPENSC4_THIS_DETOUR(bool, OnMouseWheel, 0x008FA4EE, OnMouseWheel,
                    (, uint32_t a, uint32_t b, uint32_t c, int32_t delta), (, a, b, c, delta))
OPENSC4_THIS_DETOUR(bool, OnLinkClicked, 0x008FB771, OnLinkClicked, (, const void* text), (, text))

// h1b: the view and the scroll bars.
OPENSC4_THIS_DETOUR(uint32_t, ScrollTo, 0x008FAD0E, ScrollTo, (, int32_t x, int32_t y), (, x, y))
OPENSC4_THIS_DETOUR(bool, GetAnchorRect, 0x008FA671, GetAnchorRect, (, const void* name, int32_t* rect),
                    (, name, rect))
OPENSC4_THIS_DETOUR(bool, ScrollToAnchor, 0x008FADE4, ScrollToAnchor, (, const void* name), (, name))
OPENSC4_THIS_DETOUR(bool, SetVerticalBarPicture, 0x008FAEA0, SetVerticalBarPicture,
                    (, void* picture, const int32_t* rect), (, picture, rect))
OPENSC4_THIS_DETOUR(bool, SetHorizontalBarPicture, 0x008FAF21, SetHorizontalBarPicture,
                    (, void* picture, const int32_t* rect), (, picture, rect))
OPENSC4_THIS_DETOUR(uint32_t, GetVerticalBarPicture, 0x008FA69F, GetVerticalBarPicture,
                    (, void** picture, int32_t* rect), (, picture, rect))
OPENSC4_THIS_DETOUR(uint32_t, GetHorizontalBarPicture, 0x008FA6C9, GetHorizontalBarPicture,
                    (, void** picture, int32_t* rect), (, picture, rect))
OPENSC4_THIS_DETOUR(bool, OnScrollMessage, 0x008FA9F4, OnScrollMessage, (, uint32_t id, uint32_t sender),
                    (, id, sender))
OPENSC4_THIS_DETOUR(bool, MakeScrollBars, 0x008FAFA2, MakeScrollBars, (, uint8_t vertical, uint8_t horizontal),
                    (, vertical, horizontal))
OPENSC4_THIS_DETOUR(bool, UpdateScrollBars, 0x008FB31A, UpdateScrollBars, (), ())
OPENSC4_THIS_DETOUR(void, SetScrollRanges, 0x008FA776, SetScrollRanges, (, uint8_t ranges), (, ranges))
OPENSC4_THIS_DETOUR(void, PlaceScrollBars, 0x008FA88A, PlaceScrollBars, (), ())

// h1b: the area, paint and the page's size.
OPENSC4_THIS_DETOUR(uint32_t, UpdateClientRect, 0x008FAC01, UpdateClientRect, (), ())
OPENSC4_THIS_DETOUR(uint32_t, SetArea, 0x008FAC82, SetArea, (, int32_t left, int32_t top, int32_t right, int32_t bottom),
                    (, left, top, right, bottom))
OPENSC4_THIS_DETOUR(int32_t*, GetDocumentSize, 0x008FB5E5, GetDocumentSize, (, int32_t* size), (, size))
OPENSC4_THIS_DETOUR(uint32_t, GetMargins, 0x008FA4BB, GetMargins,
                    (, int32_t* left, int32_t* top, int32_t* right, int32_t* bottom), (, left, top, right, bottom))
OPENSC4_THIS_DETOUR(bool, Paint, 0x008FB3F5, Paint, (), ())

// h1b: the addresses.
OPENSC4_THIS_DETOUR(bool, SetService, 0x008FA5F3, SetService, (, void* service), (, service))
OPENSC4_THIS_DETOUR(bool, GoToURL, 0x008FB168, GoToURL, (, char* url, uint8_t reload), (, url, reload))
OPENSC4_THIS_DETOUR(bool, GoToURLText, 0x008FBBF7, GoToURLText, (, const void* text, uint32_t reload),
                    (, text, reload))
OPENSC4_THIS_DETOUR(bool, GoToFragment, 0x008FC2BD, GoToFragment, (, const Text* fragment), (, fragment))

// h1c: the directors and the shared block.
OPENSC4_THIS_DETOUR(char*, ConstructDirector, 0x008F673E, ConstructDirector, (), ())
OPENSC4_THIS_DETOUR(uint32_t, DirectorId, 0x008F6776, DirectorId, (), ())
OPENSC4_CDECL_DETOUR(char*, GetDirector, 0x008F677C, GetDirector, (), ())
OPENSC4_THIS_DETOUR(char*, ConstructSubDirector, 0x008FC379, ConstructSubDirector, (), ())
OPENSC4_THIS_DETOUR(uint32_t, SubDirectorId, 0x008FC392, SubDirectorId, (), ())
OPENSC4_CDECL_DETOUR(char*, GetSubDirector, 0x008FC398, GetSubDirector, (), ())
OPENSC4_THIS_DETOUR(uint32_t, ReleaseBlock, 0x008FC3C3, ReleaseBlock, (), ())

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

bool InstallInput(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(OnKey, addr::HTMLWindow_OnKey),
        OPENSC4_D(OnMouseDown, addr::HTMLWindow_OnMouseDown),
        OPENSC4_D(OnMouseUp, addr::HTMLWindow_OnMouseUp),
        OPENSC4_D(OnMouseMove, addr::HTMLWindow_OnMouseMove),
        OPENSC4_D(HitNode, addr::HTMLWindow_HitNode),
        OPENSC4_D(OnMouseWheel, addr::HTMLWindow_OnMouseWheel),
        OPENSC4_D(OnLinkClicked, addr::HTMLWindow_OnLinkClicked),
    };
    return InstallAll("input", detours);
}

bool InstallScroll(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(ScrollTo, addr::HTMLWindow_ScrollTo),
        OPENSC4_D(GetAnchorRect, addr::HTMLWindow_GetAnchorRect),
        OPENSC4_D(ScrollToAnchor, addr::HTMLWindow_ScrollToAnchor),
        OPENSC4_D(SetVerticalBarPicture, addr::HTMLWindow_SetVerticalBarPicture),
        OPENSC4_D(SetHorizontalBarPicture, addr::HTMLWindow_SetHorizontalBarPicture),
        OPENSC4_D(GetVerticalBarPicture, addr::HTMLWindow_GetVerticalBarPicture),
        OPENSC4_D(GetHorizontalBarPicture, addr::HTMLWindow_GetHorizontalBarPicture),
        OPENSC4_D(OnScrollMessage, addr::HTMLWindow_OnScrollMessage),
        OPENSC4_D(MakeScrollBars, addr::HTMLWindow_MakeScrollBars),
        OPENSC4_D(UpdateScrollBars, addr::HTMLWindow_UpdateScrollBars),
        OPENSC4_D(SetScrollRanges, addr::HTMLWindow_SetScrollRanges),
        OPENSC4_D(PlaceScrollBars, addr::HTMLWindow_PlaceScrollBars),
    };
    return InstallAll("scroll", detours);
}

bool InstallPaint(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(UpdateClientRect, addr::HTMLWindow_UpdateClientRect),
        OPENSC4_D(SetArea, addr::HTMLWindow_SetArea),
        OPENSC4_D(GetDocumentSize, addr::HTMLWindow_GetDocumentSize),
        OPENSC4_D(GetMargins, addr::HTMLWindow_GetMargins),
        OPENSC4_D(Paint, addr::HTMLWindow_Paint),
    };
    return InstallAll("paint", detours);
}

bool InstallAddress(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(SetService, addr::HTMLWindow_SetService),
        OPENSC4_D(GoToURL, addr::HTMLWindow_GoToURL),
        OPENSC4_D(GoToURLText, addr::HTMLWindow_GoToURLText),
        OPENSC4_D(GoToFragment, addr::HTMLWindow_GoToFragment),
    };
    return InstallAll("address", detours);
}

bool InstallDirectors(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(ConstructDirector, addr::HTMLDirector_Construct),
        OPENSC4_D(DirectorId, addr::HTMLDirector_GetID),
        OPENSC4_D(GetDirector, addr::HTMLDirector_Get),
        OPENSC4_D(ConstructSubDirector, addr::HTMLSubDirector_Construct),
        OPENSC4_D(SubDirectorId, addr::HTMLSubDirector_GetID),
        OPENSC4_D(GetSubDirector, addr::HTMLSubDirector_Get),
    };
    return InstallAll("directors", detours);
}

bool InstallBlock(registry::Mode)
{
    const Detour detours[] = {
        OPENSC4_D(ReleaseBlock, addr::HTMLSharedBlock_Release),
    };
    return InstallAll("block", detours);
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
    registry::Register({"ui.html.window.input", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLWindow_OnKey,
                        "Our HTML window's keys, mouse and links: slots 130, 134, 136, 138 and 139, the node hit, "
                        "the link pressed (7 detours; docs/html.md)",
                        false, InstallInput}),
    registry::Register({"ui.html.window.scroll", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLWindow_ScrollTo,
                        "Our HTML window's view and scroll bars: scrolling to a point and an anchor, the bars' "
                        "pictures, messages, making, checking, ranges and places (12 detours; docs/html.md)",
                        false, InstallScroll}),
    registry::Register({"ui.html.window.paint", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLWindow_Paint,
                        "Our HTML window's paint, area, client rectangle, the page's size and the margins (5 "
                        "detours; docs/html.md)",
                        false, InstallPaint}),
    registry::Register({"ui.html.window.address", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLWindow_GoToURL,
                        "Our HTML window's addresses: the service, going to an address, a string and a fragment "
                        "(4 detours; docs/html.md)",
                        false, InstallAddress}),
    registry::Register({"ui.html.directors", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLDirector_Construct,
                        "Our HTML viewer's two directors: their constructors, IDs and static getters (6 "
                        "detours; docs/html.md)",
                        false, InstallDirectors}),
    registry::Register({"ui.html.block", "ui/html", registry::Kind::Detour, registry::Phase::Attach,
                        addr::HTMLSharedBlock_Release,
                        "Our release of the HTML viewer's shared block (1 detour; docs/html.md)", false,
                        InstallBlock}),
};

} // namespace
} // namespace opensc4::html_window
