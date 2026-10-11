// Four pieces of the interface between the graphics options dialog and the
// frame window's own code (0x00792F10 to 0x007946A0).
//
// The graphics options dialog's last two functions (the class is in
// ui_panels_2.cpp, vtable 0x00AB55F8): Release 0x00792F10 and the window
// procedure 0x00792F50, which hands the clicks of the dialog's own buttons to
// the command handler 0x00791D70.
//
// The advice list (0xE8 bytes, a window; vtables 0x00AB58B0 and 0x00AB5880 at
// +0xD8; class ID 0xCA1492AC, interface 0xCA1492A2, factory 0x00793D20). The
// advisor window, the MySim panel and the news ticker fill it through the
// interface at +0xD8: Clear (slot 7), SetBackground (slot 6, shared code at
// 0x00989960), Add (slot 4) once for every advice and Rebuild (slot 5). It
// keeps the rows in a list and shows them as one HTML table in an HTML
// control of its own size: an image, the title as a link "toggle", and an
// image as a link "close"; an open row has its message under the title. A
// click on a link comes back as message 0xE4A85D91: "toggle" opens or shuts
// the row, tells the advice (flag 0x20) and posts 0x426841A1 with the
// advice's advisor and mood; "close" sets the advice's flag 8 and sends
// 0xCA09FA6C with the advice; both build the table again. Any other link is
// followed the usual way (0x0078D140).
//
// The overlay picture (0xEC bytes, a window; vtables 0x00AB5B48, 0x00AB5B34
// at +0xD8 and 0x00AB5B20 at +0xE0; class ID 0xCA5D3294, interface
// 0xCA5D3290, factory 0x007941C0). The city view makes one over its whole
// area (window 0x6A5E44B6, 0x007EF029) and gives it a picture (0x007E8A90)
// through slot 4 of the interface at +0xE0, which makes magenta the
// picture's transparent colour. Its painting (0x00794100) stays the game's.
//
// The frame window's first functions (vtable 0x00AB5DA8, the constructor
// 0x00794430 is called from 0x00797067; the rest of the class starts at
// 0x00794760): a window that can be dragged and sized by its edges. Here are
// the constructor, the destructor, Shutdown, the mouse button let go, two
// setters and the test of a point against the edges, which also picks the
// cursor. Two mouse handlers of the class with vtable 0x00AB6010 lie between
// them (0x00794360, 0x00794390).
//
// Replacement, off by default:
//
//   ui.panels.part3: everything named above but what is listed next.
//
// Still the game's: the two paint functions 0x00794100 (the overlay picture)
// and 0x007943B0 (the class with vtable 0x00AB6010), as drawing; the six
// counting thunks 0x00793EB0, 0x00793EC0, 0x00793FA0, 0x00793FB0, 0x00793FD0
// and 0x00793FE0, which the vtables at 0x00AB6488 and 0x00AB64B8 of another
// class also hold.
//
// Not functions: 0x00792480, 0x00792BF0, 0x00792DF0 and 0x00792E80 are labels
// inside the graphics options dialog's command handler 0x00791D70.
//
// The advice list: +0xD8 the interface, +0xDC the HTML control (not counted:
// the control is a child window), +0xE0 the rows (a list's sentinel node,
// 0x78 bytes like every node), +0xE4 the background picture's instance (0
// for a plain colour). A row is 0x70 bytes: five strings (the close image,
// the image of an open row, the image of a shut row, the message, the
// title), +0x64 a byte: the row is open, +0x68 the advice, +0x6C a byte: the
// row has no links.
//
// The overlay picture: +0xD8 a counted base (vtable, count), +0xE0 the
// interface, +0xE4 the picture, +0xE8 the application (0x00B43CC4).
//
// The frame window: +0xD8 and +0xD9 two bytes set by 0x00794260 (the window
// may be sized, the window may be moved), +0xDA a byte and +0xDC a dword
// cleared when a drag ends, +0xE0 to +0xE8 three colours, +0xEC the cursor
// set, +0xF0 and +0xF4 two dwords, +0xF8 a reference.
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

namespace graphics { // the graphics options dialog (ui_panels_2.cpp)
constexpr std::size_t kWindow = 0x4;
constexpr std::size_t kReferences = 0xC;
} // namespace graphics

namespace list { // the advice list
constexpr std::size_t kInterface = 0xD8;
constexpr std::size_t kHtml = 0xDC;
constexpr std::size_t kRows = 0xE0;
constexpr std::size_t kBackground = 0xE4;
} // namespace list

namespace overlay { // the overlay picture
constexpr std::size_t kCounted = 0xD8;
constexpr std::size_t kInterface = 0xE0;
constexpr std::size_t kPicture = 0xE4;
constexpr std::size_t kApp = 0xE8;
} // namespace overlay

namespace frame { // the frame window
constexpr std::size_t kSizable = 0xD8;
constexpr std::size_t kMovable = 0xD9;
constexpr std::size_t kDragging = 0xDA;
constexpr std::size_t kEdge = 0xDC;
constexpr std::size_t kColors = 0xE0;
constexpr std::size_t kCursor = 0xEC;
constexpr std::size_t kF0 = 0xF0;
constexpr std::size_t kF4 = 0xF4;
constexpr std::size_t kHeld = 0xF8;
} // namespace frame

namespace other { // the class with vtable 0x00AB6010
constexpr std::size_t kDownX = 0x144;
constexpr std::size_t kDownY = 0x148;
} // namespace other

constexpr uint32_t kCommand = 3;
constexpr uint32_t kButtonClicked = 0x287259F6;
constexpr uint32_t kLinkClicked = 0xE4A85D91;
constexpr uint32_t kMoodMessage = 0x426841A1;
constexpr uint32_t kAdviceRemoved = 0xCA09FA6C;
constexpr uint32_t kListIid = 0xCA1492A2;
constexpr uint32_t kListClass = 0xCA1492AC;
constexpr uint32_t kOverlayIid = 0xCA5D3290;
constexpr uint32_t kOverlayClass = 0xCA5D3294;
constexpr uint32_t kWindowIid = 0x22BA0121;
constexpr uint32_t kHtmlIid = 0x4A11FD4A;
constexpr uint32_t kHtmlClass = 0xAA12E5F5;

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

// A row of the advice list.
struct Row {
    GameString closeImage;
    GameString openImage;
    GameString shutImage;
    GameString message;
    GameString title;
    uint8_t open;
    uint8_t pad65[3];
    void* advice;
    uint8_t plain;
    uint8_t pad6D[3];
};
static_assert(sizeof(Row) == 0x70);
static_assert(offsetof(Row, open) == 0x64);
static_assert(offsetof(Row, advice) == 0x68);
static_assert(offsetof(Row, plain) == 0x6C);

struct Node {
    Node* next;
    Node* previous;
    Row row;
};
static_assert(sizeof(Node) == 0x78);

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
    text.body.begin = buffer;
    text.body.end = buffer;
    text.body.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// The inlined destructor.
void DestroyString(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (char* const buffer = text.body.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(buffer));
}

// A new message, or null (the callers do not check).
void* NewMessage()
{
    void* const memory = Cdecl<void*>(addr::RZ_operator_new, uint32_t{0x2C});
    return memory ? This<void*>(addr::cGZMessage_cGZMessage, memory) : nullptr;
}

// Appends a literal to a string's body (0x0040EBC0 with the text's ends).
template <std::size_t N>
void AppendText(GameString& text, const char (&literal)[N], uint8_t* tag)
{
    This<void*>(addr::cSC4Advisor_F40EBC0, &text.body, &literal[0], &literal[N - 1], static_cast<void*>(tag));
}

// Appends a range of characters to a string's body.
void AppendRange(GameString& text, const StringBody& from, uint8_t* tag)
{
    This<void>(addr::SC4String_AppendRange, &text.body, from.begin, from.end, static_cast<void*>(tag));
}

// The string's Sprintf as the original calls it: through the pointer in the
// string's vtable (slot 27, 0x00A8087C), with the C convention.
using SprintfFn = void*(__cdecl*)(void*, const char*, ...);

SprintfFn StringSprintf()
{
    return Global<SprintfFn>(addr::SC4String_Vtable + 0x6C);
}

// --- The graphics options dialog's last two functions ---------------------------------

// The game's 0x00792F10 (slot 2): Release. The last reference forgets the
// settings (slot 6), lets the window go and frees the dialog.
uint32_t __fastcall GraphicsRelease(char* self, void*)
{
    const uint32_t count = At<uint32_t>(self, graphics::kReferences);
    if (count > 1) {
        At<uint32_t>(self, graphics::kReferences) = count - 1;
        return count - 1;
    }
    Virtual<void>(self, 0x18);
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_GraphicsDialogVtable;
    if (void* const gen = At<void*>(self, graphics::kWindow))
        Release(gen);
    Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return 0;
}

// Whether a button is one the dialog's command handler knows.
bool IsGraphicsButton(uint32_t id)
{
    if (id <= 0x6A57DA58) {
        if (id >= 0x6A57DA54)
            return true;
        if (id > 0x2B85CF9F) {
            if (id == 0x2B85CFAA)
                return true;
            return id > 0x6A57DA47 && id <= 0x6A57DA51;
        }
        if (id == 0x2B85CF9F)
            return true;
        if (id > 0x0B85CF92)
            return id >= 0x2AB6C373 && id <= 0x2AB6C377;
        return id == 0x0B85CF92 || id == 0x0A8B7E00;
    }
    if (id > 0xEA57DA59)
        return id == 0xEA57DA6F || id == 0xEA5E99D9;
    if (id == 0xEA57DA59)
        return true;
    if (id > 0x6A57DA60)
        return id >= 0xAB81DED5 && id <= 0xAB81DED7;
    if (id == 0x6A57DA60)
        return true;
    return id >= 0x6A57DA5A && id <= 0x6A57DA5D;
}

// The game's 0x00792F50 (slot 3): a message of the dialog's window while it
// is shown. A click on one of the dialog's buttons goes to the command
// handler; every command counts as handled.
uint8_t __fastcall GraphicsWinMessage(char* self, void*, void* window, const WinMessage* message)
{
    if (window != Virtual<void*>(At<void*>(self, graphics::kWindow), 0xC))
        return 0;
    if (!Virtual<uint8_t>(window, 0x11C))
        return 0;
    if (message->type != kCommand)
        return 0;
    if (message->a == kButtonClicked && IsGraphicsButton(message->b))
        This<uint8_t>(addr::SC4UI_F791D70, self, message->b, window);
    return 1;
}

// --- The advice list ------------------------------------------------------------------

// The game's 0x00793070 (cdecl): the class ID.
uint32_t __cdecl ListClassId()
{
    return kListClass;
}

// The game's 0x00793080 (slot 0): interfaces 1 and 0xCA1492A2 give the part
// at +0xD8, 0x22BA0121 the window. Nothing goes on to the window's own
// QueryInterface. A null window is not checked for before the count is
// raised, as in the original.
uint8_t __fastcall ListQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == 1 || iid == kListIid) {
        *out = self ? self + list::kInterface : nullptr;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    if (iid == kWindowIid) {
        *out = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    return 0;
}

// The game's 0x007930E0 (window slot 5): Shutdown. The rows go, then the
// window's own shutdown (the original ends in a jump to it).
uint8_t __fastcall ListShutdown(char* self, void*)
{
    Virtual<void>(self + list::kInterface, 0x1C);
    return This<uint8_t>(addr::SC4Win_F99D2FE, self);
}

// The game's 0x00793100 (window slot 55): SetArea. The HTML control keeps
// the window's size.
void __fastcall ListSetArea(char* self, void*, int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    This<void>(addr::GZWin_SetArea, self, left, top, right, bottom);
    if (void* const html = At<void*>(self, list::kHtml)) {
        void* const window = Virtual<void*>(html, 0xC);
        Virtual<void>(window, 0xDC, int32_t{0}, int32_t{0}, right - left, bottom - top);
    }
}

// The game's 0x00793150 (interface slot 8). `this` is the part at +0xD8.
// Four values go on to the HTML control's slot 32 (the original jumps to
// it); 0 without a control.
uint32_t __fastcall ListHtml80(char* part, void*, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    void* const html = At<void*>(part, list::kHtml - list::kInterface);
    if (!html)
        return 0;
    return Virtual<uint32_t>(html, 0x80, a, b, c, d);
}

// The game's 0x00793170 (interface slot 9). `this` is the part at +0xD8. A
// jump to the HTML control's slot 9, which takes one value; the control is
// not checked for null, as in the original.
uint32_t __fastcall ListHtml24(char* part, void*, uint32_t a)
{
    return Virtual<uint32_t>(At<void*>(part, list::kHtml - list::kInterface), 0x24, a);
}

// The game's 0x00793180 (interface slot 10). `this` is the part at +0xD8.
// The HTML control's slot 28 with 8.
uint32_t __fastcall ListHtml70(char* part, void*)
{
    return Virtual<uint32_t>(At<void*>(part, list::kHtml - list::kInterface), 0x70, uint32_t{8});
}

// The game's 0x00793190 (window slot 4): Init. Makes the HTML control at the
// window's size as a child. The control's pointer is kept without a count.
uint8_t __fastcall ListInit(char* self, void*)
{
    if (This<uint8_t>(addr::GZWin_IsInitialized, self))
        return 1;
    void* html = nullptr;
    if (!This<uint8_t>(addr::GZWin_Init, self)) {
        if (html)
            Release(html);
        return 0;
    }
    void* const com = Cdecl<void*>(addr::RZGetCOM);
    if (void* const old = html) {
        html = nullptr;
        Release(old);
    }
    if (!Virtual<uint8_t>(com, 0xC, kHtmlClass, kHtmlIid, &html)) {
        if (html)
            Release(html);
        return 0;
    }
    void* const window = Virtual<void*>(html, 0xC);
    const int32_t height = Virtual<int32_t>(self, 0xA8);
    const int32_t width = Virtual<int32_t>(self, 0xA4);
    Virtual<void>(window, 0xDC, int32_t{0}, int32_t{0}, width, height);
    Virtual<void>(html, 0x70, uint32_t{0x40});
    Virtual<void>(self, 0x38, Virtual<void*>(html, 0xC));
    At<void*>(self, list::kHtml) = html;
    if (html)
        Release(html);
    return 1;
}

// The game's 0x00793280 (cdecl): moves a position in a list `count` nodes on,
// or back when `count` is negative.
void __cdecl Advance(Node** position, int32_t count)
{
    if (count > 0) {
        do {
            *position = (*position)->next;
        } while (--count != 0);
    } else if (count != 0) {
        count = -count;
        do {
            *position = (*position)->previous;
        } while (--count != 0);
    }
}

// The game's 0x007932B0: a row's constructor. The two bytes are not set.
Row* __fastcall RowConstruct(Row* self, void*)
{
    InitString(self->closeImage);
    InitString(self->openImage);
    InitString(self->shutImage);
    InitString(self->message);
    InitString(self->title);
    self->advice = nullptr;
    return self;
}

// The game's 0x00793380: a row's destructor.
void __fastcall RowDestruct(Row* self, void*)
{
    if (void* const advice = self->advice)
        Release(advice);
    DestroyString(self->title);
    DestroyString(self->message);
    DestroyString(self->shutImage);
    DestroyString(self->openImage);
    DestroyString(self->closeImage);
}

// The game's 0x00793400 (window slot 3): a message of the window. Only a
// clicked link (0xE4A85D91, the address in the third dword) is this class's;
// the answer is true only for a link that could not be followed.
uint8_t __fastcall ListDoMessage(char* self, void*, WinMessage* message)
{
    if (message->type != kLinkClicked)
        return This<uint8_t>(addr::SC4Win_F99CCF0, self, message);
    GameString* const url = reinterpret_cast<GameString*>(std::uintptr_t{message->b});

    // The click sound.
    void* sound = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, uint32_t{0x23CECBA0}, uint32_t{0x106077D2}, &sound);
    if (sound)
        Virtual<void>(sound, 0x2C, uint32_t{1}, uint32_t{0x2A5C322B}, uint32_t{0}, uint32_t{0}, uint32_t{0});
    This<void*>(addr::SC4RefPtr_AssignGuarded, &sound, static_cast<void*>(nullptr));

    uint8_t tag = 0;
    int32_t index;
    using ScanFn = int(__cdecl*)(const char*, const char*, ...);
    const ScanFn scan = reinterpret_cast<ScanFn>(addr::SC4_sscanf);

    if (This<uint8_t>(addr::cRZBaseString_BeginsWith, url, "sc4://action/toggle?", uint32_t{0x14})) {
        const uint32_t length = static_cast<uint32_t>(url->body.end - url->body.begin) - 0x14;
        StringBody rest;
        This<void*>(addr::SC4String_CopyBuffer, &rest, url ? &url->body : nullptr, uint32_t{0x14}, length,
                    static_cast<void*>(&tag));
        char* buffer = rest.begin;
        if (scan(buffer, "item=%d", &index) == 1) {
            Node* position = At<Node*>(self, list::kRows)->next;
            Advance(&position, index);
            Row& row = position->row;
            row.open = row.open == 0;
            void* const sent = NewMessage();
            AddRef(sent);
            Virtual<void>(sent, 0x14, kMoodMessage);
            Virtual<void>(sent, 0x2C, Virtual<uint32_t>(row.advice, 0x2C));
            Virtual<void>(sent, 0x40, Virtual<uint32_t>(row.advice, 0x18));
            Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0x10, sent, uint32_t{0});
            Release(sent);
            Virtual<void>(row.advice, 0x38, uint32_t{0x20}, uint32_t{row.open});
            Virtual<void>(self + list::kInterface, 0x14, uint32_t{1});
            buffer = rest.begin;
        }
        if (buffer)
            Cdecl<void>(addr::SC4_Free, static_cast<void*>(buffer));
        This<void>(addr::SC4RefPtr_DestructGuarded, &sound);
        return 0;
    }

    if (This<uint8_t>(addr::cRZBaseString_BeginsWith, url, "sc4://action/close?", uint32_t{0x13})) {
        const uint32_t length = static_cast<uint32_t>(url->body.end - url->body.begin) - 0x13;
        GameString rest;
        This<void*>(addr::SC4String_Substring, &rest, url, uint32_t{0x13}, length);
        char* const buffer = rest.body.begin;
        if (scan(buffer, "item=%d", &index) == 1) {
            Node* position = At<Node*>(self, list::kRows)->next;
            Advance(&position, index);
            Row& row = position->row;
            Virtual<void>(row.advice, 0x38, uint32_t{8}, uint32_t{1});
            void* const sent = NewMessage();
            if (sent)
                AddRef(sent);
            Virtual<void>(sent, 0x14, kAdviceRemoved);
            Virtual<void>(sent, 0x84, row.advice);
            Virtual<void>(sent, 0x2C, kAdviceRemoved);
            Virtual<void>(Global<void*>(addr::SC4_MessageServer), 0xC, sent);
            Virtual<void>(self + list::kInterface, 0x14, uint32_t{1});
            Release(sent);
        }
        if (buffer)
            Cdecl<void>(addr::SC4_Free, static_cast<void*>(buffer));
        This<void>(addr::SC4RefPtr_DestructGuarded, &sound);
        return 0;
    }

    uint8_t handler = 0; // an object without fields
    const uint8_t followed = This<uint8_t>(addr::SC4UI_FollowLink, &handler, url, uint32_t{0});
    This<void>(addr::SC4RefPtr_DestructGuarded, &sound);
    return followed ? 0 : 1;
}

// The game's 0x007936D0. `this` is the place of the list's sentinel node.
// Destroys and frees every row.
void __fastcall RowsClear(Node** self, void*)
{
    Node* node = (*self)->next;
    while (node != *self) {
        Node* const gone = node;
        node = node->next;
        RowDestruct(&gone->row, nullptr);
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(gone));
    }
    (*self)->next = *self;
    (*self)->previous = *self;
}

// A string copied as the row's copy constructor does it.
void CopyString(GameString& into, GameString& from, void* tag)
{
    This<void*>(addr::SC4String_CopyBuffer, &into.body, &from.body, uint32_t{0}, uint32_t{0xFFFFFFFF}, tag);
    into.vtable = addr::SC4String_Vtable;
    into.zero = 0;
}

// The game's 0x00793710: a row's copy constructor. The allocator tag is the
// address of the argument, as in the original.
Row* __fastcall RowCopy(Row* self, void*, Row* source)
{
    void* const tag = &source;
    This<void*>(addr::SC4String_CopyBuffer, &self->closeImage.body, source ? &source->closeImage.body : nullptr,
                uint32_t{0}, uint32_t{0xFFFFFFFF}, tag);
    self->closeImage.vtable = addr::SC4String_Vtable;
    self->closeImage.zero = 0;
    CopyString(self->openImage, source->openImage, tag);
    CopyString(self->shutImage, source->shutImage, tag);
    CopyString(self->message, source->message, tag);
    CopyString(self->title, source->title, tag);
    self->open = source->open;
    self->advice = source->advice;
    if (self->advice)
        AddRef(self->advice);
    self->plain = source->plain;
    return self;
}

// The game's 0x00793800 (interface slot 7): Clear. `this` is the part at
// +0xD8.
void __fastcall ListClear(char* part, void*)
{
    RowsClear(&At<Node*>(part, list::kRows - list::kInterface), nullptr);
}

// The game's 0x00793810 (interface slot 5): Rebuild. `this` is the part at
// +0xD8. Writes the rows as one HTML table and gives it to the control with
// "sc4://" as its base; with `keepPlace` the control's scroll position is
// read before and set again after. The lone 8 bytes allocated after the
// column's text and freed at the end are the original's (a string's body of
// which nothing else is left).
void __fastcall ListRebuild(char* part, void*, uint8_t keepPlace)
{
    uint8_t tag = 0;
    uint32_t placeX = 0;
    uint32_t placeY = 0;
    GameString page;
    InitString(page);
    if (keepPlace)
        Virtual<void>(At<void*>(part, list::kHtml - list::kInterface), 0x18, &placeX, &placeY);

    void* const window = Virtual<void*>(At<void*>(part, list::kHtml - list::kInterface), 0xC);
    const int32_t width = Virtual<int32_t>(window, 0xA4) - 0x3D;
    GameString column;
    InitString(column);
    Cdecl<void*>(addr::cRZBaseString_Sprintf, &column, "</TD><TD WIDTH=\"%d\">", width);
    char* const spare = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    *spare = 0;

    const uint32_t background = At<uint32_t>(part, list::kBackground - list::kInterface);
    if (background == 0) {
        GameString head;
        This<void*>(addr::SC4String_FromText, &head,
                    "<HTML><HEAD></HEAD><BODY BGCOLOR=\"#dee8e3\"><FONT FACE=\"Arta\" SIZE=3><TABLE BORDER=0>");
        char* const begin = head.body.begin;
        This<void*>(addr::SC4String_AssignRange, &page.body, begin, head.body.end, static_cast<void*>(&tag));
        if (begin)
            Cdecl<void>(addr::SC4_Free, static_cast<void*>(begin));
    } else {
        Cdecl<void*>(addr::cRZBaseString_Sprintf, &page,
                     "<HTML><HEAD></HEAD><BODY BACKGROUND=\"sc4://HTML/46a006b0/%x\"><FONT FACE=\"Arta\" SIZE=3>"
                     "<TABLE BORDER=0>",
                     background);
    }

    Node* const end = At<Node*>(part, list::kRows - list::kInterface);
    int32_t index = 0;
    for (Node* node = end->next; node != end; node = node->next, ++index) {
        Row& row = node->row;
        AppendText(page, "<TR><TD WIDTH=\"18\">", &tag);
        GameString& image = row.open ? row.openImage : row.shutImage;
        if (!row.plain) {
            GameString link;
            InitString(link);
            void* const made =
                StringSprintf()(&link, "<A NAME=\"item%d\" HREF=\"sc4://action/toggle?item=%d\">", index, index);
            This<void*>(addr::cRZBaseString_AppendString, &page, made);
            DestroyString(link);
        }
        AppendRange(page, image.body, &tag);
        if (!row.plain)
            AppendText(page, "</A>", &tag);
        AppendRange(page, column.body, &tag);
        if (!row.plain) {
            GameString link;
            InitString(link);
            void* const made = StringSprintf()(
                &link, "<A HREF=\"sc4://action/toggle?item=%d\"><SPAN STYLE=\"text-decoration: none\">", index);
            This<void*>(addr::cRZBaseString_AppendString, &page, made);
            DestroyString(link);
        }
        AppendRange(page, row.title.body, &tag);
        if (!row.plain)
            AppendText(page, "</SPAN></A>", &tag);
        if (row.open) {
            AppendText(page, "<BR>", &tag);
            AppendRange(page, row.message.body, &tag);
        }
        {
            GameString link;
            InitString(link);
            void* const made = StringSprintf()(
                &link, "</TD><TD WIDTH=\"18\"><A NAME=\"item%d\" HREF=\"sc4://action/close?item=%d\">", index, index);
            This<void*>(addr::cRZBaseString_AppendString, &page, made);
            DestroyString(link);
        }
        AppendRange(page, row.closeImage.body, &tag);
        AppendText(page, "</A></TD></TR>", &tag);
    }
    AppendText(page, "</TABLE></FONT></BODY></HTML>", &tag);

    // The base address, a string made in place.
    GameString base;
    base.body.begin = nullptr;
    base.body.end = nullptr;
    base.body.capacity = nullptr;
    char* const text = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{7});
    base.body.begin = text;
    base.body.end = text;
    base.body.capacity = text + 7;
    char* const textEnd = Cdecl<char*>(addr::SC4_memmove, text, "sc4://", uint32_t{6}) + 6;
    base.body.end = textEnd;
    *textEnd = 0;
    base.vtable = addr::SC4String_Vtable;
    base.zero = 0;
    Virtual<void>(At<void*>(part, list::kHtml - list::kInterface), 0x58, &page.body, &base);
    DestroyString(base);

    if (keepPlace)
        Virtual<void>(At<void*>(part, list::kHtml - list::kInterface), 0x1C, placeX, placeY);
    Cdecl<void>(addr::SC4_Free, static_cast<void*>(spare));
    DestroyString(column);
    DestroyString(page);
}

// The game's 0x00793CB0: the constructor.
char* __fastcall ListConstruct(char* self, void*)
{
    This<void*>(addr::GZWin_Construct, self);
    At<std::uintptr_t>(self, list::kInterface) = addr::SC4UI_AdviceListInterfaceBaseVtable;
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_AdviceListVtable;
    At<std::uintptr_t>(self, list::kInterface) = addr::SC4UI_AdviceListInterfaceVtable;
    At<void*>(self, list::kHtml) = nullptr;
    At<Node*>(self, list::kRows) = nullptr;
    Node* const end = Cdecl<Node*>(addr::SC4_stl_allocate, uint32_t{0x78});
    end->next = end;
    end->previous = end;
    At<Node*>(self, list::kRows) = end;
    At<uint32_t>(self, list::kBackground) = 0;
    return self;
}

// The game's 0x00793D10 (interface slot 0): QueryInterface of the part at
// +0xD8.
uint8_t __fastcall ListInterfaceQueryInterface(char* part, void*, uint32_t iid, void** out)
{
    return ListQueryInterface(part - list::kInterface, nullptr, iid, out);
}

// The game's 0x00793D20 (cdecl): the class's factory. Gives the part at
// +0xD8.
void* __cdecl ListCreate()
{
    char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0xE8});
    if (memory) {
        if (char* const self = ListConstruct(memory, nullptr))
            return self + list::kInterface;
    }
    return nullptr;
}

// The game's 0x00793D50 (window slot 148): the deleting destructor.
char* __fastcall ListDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_AdviceListVtable;
    At<std::uintptr_t>(self, list::kInterface) = addr::SC4UI_AdviceListInterfaceVtable;
    RowsClear(&At<Node*>(self, list::kRows), nullptr);
    if (Node* const end = At<Node*>(self, list::kRows))
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(end));
    This<void>(addr::GZWin_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x00793DA0 (interface slot 4): Add. `this` is the part at
// +0xD8. A row goes to the end of the list (an empty one is made, copied
// into the new node and destroyed; its two bytes are copied unset, as in the
// original, whose test of the new row's address for null is left out) and
// takes the caller's values; the background is the whole list's.
void __fastcall ListAdd(char* part, void*, void* openImage, void* shutImage, void* message, void* title,
                        uint8_t open, void* advice, void* closeImage, uint8_t plain, uint32_t background)
{
    alignas(4) char storage[sizeof(Row)];
    Row* const empty = RowConstruct(reinterpret_cast<Row*>(&storage[0]), nullptr);
    Node* const end = At<Node*>(part, list::kRows - list::kInterface);
    Node* const node = Cdecl<Node*>(addr::SC4_stl_allocate, uint32_t{0x78});
    RowCopy(&node->row, nullptr, empty);
    Node* const last = end->previous;
    node->next = end;
    node->previous = last;
    last->next = node;
    end->previous = node;
    RowDestruct(reinterpret_cast<Row*>(&storage[0]), nullptr);

    Row& row = At<Node*>(part, list::kRows - list::kInterface)->previous->row;
    row.open = open;
    Virtual<void*>(&row.openImage, 0x34, openImage);
    Virtual<void*>(&row.shutImage, 0x34, shutImage);
    Virtual<void*>(&row.title, 0x34, title);
    Virtual<void*>(&row.message, 0x34, message);
    Hold(row.advice, advice);
    Virtual<void*>(&row.closeImage, 0x34, closeImage);
    row.plain = plain;
    At<uint32_t>(part, list::kBackground - list::kInterface) = background;
}

// --- The overlay picture --------------------------------------------------------------

// The game's 0x00793EA0 (cdecl): the class ID.
uint32_t __cdecl OverlayClassId()
{
    return kOverlayClass;
}

// The game's 0x00793ED0 (slot 0): interfaces 1 and 0xCA5D3290 give the part
// at +0xE0, 0x22BA0121 the window; the rest is the window's (the original
// jumps on).
uint8_t __fastcall OverlayQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid == 1 || iid == kOverlayIid) {
        *out = self ? self + overlay::kInterface : nullptr;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    if (iid == kWindowIid) {
        *out = self;
        Virtual<uint32_t>(self, 0x4);
        return 1;
    }
    return This<uint8_t>(addr::SC4Win_QueryInterface, self, iid, out);
}

// The game's 0x00793F30: the destructor.
void __fastcall OverlayDestruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_OverlayPictureVtable;
    At<std::uintptr_t>(self, overlay::kCounted) = addr::SC4UI_OverlayPictureCountedVtable;
    At<std::uintptr_t>(self, overlay::kInterface) = addr::SC4UI_OverlayPictureInterfaceVtable;
    if (void* const app = At<void*>(self, overlay::kApp))
        Release(app);
    if (void* const picture = At<void*>(self, overlay::kPicture))
        Release(picture);
    This<void>(addr::cRZBaseRefCount_Destruct, self + overlay::kCounted);
    This<void>(addr::GZWin_Destruct, self);
}

// The game's 0x007940E0 (window slot 148): the deleting destructor.
char* __fastcall OverlayDelete(char* self, void*, uint32_t flags)
{
    OverlayDestruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x00793F80 (interface slot 3): the window of the part at +0xE0.
char* __fastcall OverlayAsWindow(char* part, void*)
{
    return part - overlay::kInterface;
}

// The game's 0x00793F90 (slot 0 of the counted base at +0xD8): its deleting
// destructor.
char* __fastcall OverlayCountedDelete(char* counted, void*, uint32_t flags)
{
    return OverlayDelete(counted - overlay::kCounted, nullptr, flags);
}

// The game's 0x00793FC0 (interface slot 0): QueryInterface of the part at
// +0xE0.
uint8_t __fastcall OverlayInterfaceQueryInterface(char* part, void*, uint32_t iid, void** out)
{
    return OverlayQueryInterface(part - overlay::kInterface, nullptr, iid, out);
}

// The game's 0x00793FF0 (interface slot 4): SetPicture. `this` is the part
// at +0xE0. The picture is kept, magenta becomes its transparent colour, and
// the window is drawn again.
void __fastcall OverlaySetPicture(char* part, void*, void* picture)
{
    Hold(At<void*>(part, overlay::kPicture - overlay::kInterface), picture);
    if (void* const kept = At<void*>(part, overlay::kPicture - overlay::kInterface)) {
        const uint32_t color = Virtual<uint32_t>(kept, 0x78, uint32_t{0xFF}, uint32_t{0}, uint32_t{0xFF});
        Virtual<void>(kept, 0x5C, color);
    }
    Virtual<void>(part - overlay::kInterface, 0x170);
}

// The game's 0x00794060: the constructor. Keeps the application.
char* __fastcall OverlayConstruct(char* self, void*)
{
    This<void*>(addr::GZWin_Construct, self);
    This<void*>(addr::cRZBaseRefCount_Construct, self + overlay::kCounted);
    At<std::uintptr_t>(self, overlay::kInterface) = addr::SC4UI_OverlayPictureInterfaceBaseVtable;
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_OverlayPictureVtable;
    At<std::uintptr_t>(self, overlay::kCounted) = addr::SC4UI_OverlayPictureCountedVtable;
    At<std::uintptr_t>(self, overlay::kInterface) = addr::SC4UI_OverlayPictureInterfaceVtable;
    At<void*>(self, overlay::kPicture) = nullptr;
    At<void*>(self, overlay::kApp) = nullptr;
    Hold(At<void*>(self, overlay::kApp), Global<void*>(addr::SC4_App43CC4));
    return self;
}

// The game's 0x007941C0 (cdecl): the class's factory. Gives the part at
// +0xE0.
void* __cdecl OverlayCreate()
{
    char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0xEC});
    if (memory) {
        if (char* const self = OverlayConstruct(memory, nullptr))
            return self + overlay::kInterface;
    }
    return nullptr;
}

// --- The frame window's first functions -----------------------------------------------

// The game's 0x007941F0 (window slot 5): Shutdown. Lets the held object go,
// then the window's own shutdown (the original ends in a jump to it).
uint8_t __fastcall FrameShutdown(char* self, void*)
{
    if (void* const held = At<void*>(self, frame::kHeld)) {
        Release(held);
        At<void*>(self, frame::kHeld) = nullptr;
    }
    return This<uint8_t>(addr::SC4Win_F99D2FE, self);
}

// The game's 0x00794220 (window slot 136): the mouse button let go. A drag
// ends: the mouse is given back and the window drawn again.
uint8_t __fastcall FrameMouseUp(char* self, void*, int32_t, int32_t, uint32_t)
{
    if (This<uint8_t>(addr::SC4Win_F99BE0A, self)) {
        At<uint8_t>(self, frame::kDragging) = 0;
        At<uint32_t>(self, frame::kEdge) = 0;
        This<void>(addr::SC4Win_F99BE1E, self);
        Virtual<void>(self, 0x170);
    }
    return 1;
}

// The game's 0x00794260. `this` is the frame window. Sets the two bytes; a
// drag under way that the new values no longer allow is ended, the cursor
// put back and the window drawn again.
void __fastcall FrameSetAllowed(char* self, void*, uint8_t sizable, uint8_t movable)
{
    At<uint8_t>(self, frame::kSizable) = sizable;
    At<uint8_t>(self, frame::kMovable) = movable;
    if (!This<uint8_t>(addr::SC4Win_F99BE0A, self))
        return;
    const uint8_t allowed = At<uint32_t>(self, frame::kEdge) == 0 ? At<uint8_t>(self, frame::kSizable)
                                                                  : At<uint8_t>(self, frame::kMovable);
    if (allowed)
        return;
    At<uint8_t>(self, frame::kDragging) = 0;
    At<uint32_t>(self, frame::kEdge) = 0;
    This<void>(addr::SC4Win_F99BE1E, self);
    if (At<uint32_t>(self, frame::kCursor) != 0) {
        At<uint32_t>(self, frame::kCursor) = 0;
        Virtual<void>(self, 0x154, uint32_t{0}, uint32_t{0});
    }
    Virtual<void>(self, 0x170);
}

// The game's 0x007942F0. `this` is the frame window. Three colours, each
// from three values through the window's slot 108.
void __fastcall FrameSetColors(char* self, void*, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t b1, uint32_t b2,
                               uint32_t b3, uint32_t c1, uint32_t c2, uint32_t c3)
{
    At<uint32_t>(self, frame::kColors) = Virtual<uint32_t>(self, 0x1B0, a1, a2, a3);
    At<uint32_t>(self, frame::kColors + 0x4) = Virtual<uint32_t>(self, 0x1B0, b1, b2, b3);
    At<uint32_t>(self, frame::kColors + 0x8) = Virtual<uint32_t>(self, 0x1B0, c1, c2, c3);
}

// The game's 0x00794360 (slot 134 of the class with vtable 0x00AB6010): the
// mouse button pressed. Takes the mouse and keeps the point.
uint8_t __fastcall OtherMouseDown(char* self, void*, int32_t x, int32_t y, uint32_t)
{
    if (This<uint8_t>(addr::SC4Win_F99BDFE, self)) {
        At<int32_t>(self, other::kDownX) = x;
        At<int32_t>(self, other::kDownY) = y;
    }
    return 1;
}

// The game's 0x00794390 (slot 136 of the class with vtable 0x00AB6010): the
// mouse button let go.
uint8_t __fastcall OtherMouseUp(char* self, void*, int32_t, int32_t, uint32_t)
{
    if (This<uint8_t>(addr::SC4Win_F99BE0A, self))
        This<void>(addr::SC4Win_F99BE1E, self);
    return 1;
}

// The game's 0x00794430: the frame window's constructor.
char* __fastcall FrameConstruct(char* self, void*)
{
    This<void*>(addr::GZWin_Construct, self);
    At<uint8_t>(self, frame::kSizable) = 1;
    At<uint8_t>(self, frame::kMovable) = 1;
    At<uint8_t>(self, frame::kDragging) = 0;
    At<uint32_t>(self, frame::kEdge) = 0;
    At<uint32_t>(self, frame::kCursor) = 0;
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_FrameWindowVtable;
    At<uint32_t>(self, frame::kColors) = 0xFFFFFF01;
    At<uint32_t>(self, frame::kColors + 0x4) = 0xFFFFFF01;
    At<uint32_t>(self, frame::kColors + 0x8) = 0xFFFFFF01;
    At<uint32_t>(self, frame::kF0) = 0;
    At<uint32_t>(self, frame::kF4) = 0;
    At<void*>(self, frame::kHeld) = nullptr;
    return self;
}

// The game's 0x00794490 (window slot 148): the frame window's deleting
// destructor.
char* __fastcall FrameDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_FrameWindowVtable;
    This<void>(addr::GZWin_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// The game's 0x007944C0. `this` is the frame window. Which edge a point is
// on, within four pixels (0 none, 1 left, 2 right, 3 top, 4 bottom, 5 left
// top, 6 right top, 7 left bottom, 8 right bottom), and the cursor for it.
// True when the window may be dragged there: off the edges only with the
// byte at +0xD9, and anywhere only with the byte at +0xD8. The width and the
// height are asked for twice, as in the original.
uint8_t __fastcall FrameEdgeAt(char* self, void*, int32_t x, int32_t y, uint32_t* edge, uint32_t* cursor)
{
    const bool left = x >= -4 && x <= 4;
    const bool top = y >= -4 && y <= 4;
    bool right = false;
    if (x >= Virtual<int32_t>(self, 0xA4) - 4)
        right = x <= Virtual<int32_t>(self, 0xA4) + 4;
    bool bottom = false;
    if (y >= Virtual<int32_t>(self, 0xA8) - 4)
        bottom = y <= Virtual<int32_t>(self, 0xA8) + 4;

    *edge = 0;
    if (left)
        *edge = top ? 5 : bottom ? 7 : 1;
    else if (right)
        *edge = top ? 6 : bottom ? 8 : 2;
    else if (top)
        *edge = 3;
    else if (bottom)
        *edge = 4;

    static const uint32_t cursors[9] = {
        0x11,       0xC8024B17, 0xC8024B17, 0xE8024B13, 0xE8024B13,
        0x68024B0E, 0x88024B02, 0x88024B02, 0x68024B0E,
    };
    *cursor = cursors[*edge];

    if (*edge == 0 && !At<uint8_t>(self, frame::kMovable))
        return 0;
    return At<uint8_t>(self, frame::kSizable) ? 1 : 0;
}

bool InstallPanels3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[44] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F792F10, reinterpret_cast<void*>(&GraphicsRelease)},
        {addr::SC4UI_F792F50, reinterpret_cast<void*>(&GraphicsWinMessage)},
        {addr::SC4UI_F793070, reinterpret_cast<void*>(&ListClassId)},
        {addr::SC4UI_F793080, reinterpret_cast<void*>(&ListQueryInterface)},
        {addr::SC4UI_F7930E0, reinterpret_cast<void*>(&ListShutdown)},
        {addr::SC4UI_F793100, reinterpret_cast<void*>(&ListSetArea)},
        {addr::SC4UI_F793150, reinterpret_cast<void*>(&ListHtml80)},
        {addr::SC4UI_F793170, reinterpret_cast<void*>(&ListHtml24)},
        {addr::SC4UI_F793180, reinterpret_cast<void*>(&ListHtml70)},
        {addr::SC4UI_F793190, reinterpret_cast<void*>(&ListInit)},
        {addr::SC4UI_F793280, reinterpret_cast<void*>(&Advance)},
        {addr::SC4UI_F7932B0, reinterpret_cast<void*>(&RowConstruct)},
        {addr::SC4UI_F793380, reinterpret_cast<void*>(&RowDestruct)},
        {addr::SC4UI_F793400, reinterpret_cast<void*>(&ListDoMessage)},
        {addr::SC4UI_F7936D0, reinterpret_cast<void*>(&RowsClear)},
        {addr::SC4UI_F793710, reinterpret_cast<void*>(&RowCopy)},
        {addr::SC4UI_F793800, reinterpret_cast<void*>(&ListClear)},
        {addr::SC4UI_F793810, reinterpret_cast<void*>(&ListRebuild)},
        {addr::SC4UI_F793CB0, reinterpret_cast<void*>(&ListConstruct)},
        {addr::SC4UI_F793D10, reinterpret_cast<void*>(&ListInterfaceQueryInterface)},
        {addr::SC4UI_F793D20, reinterpret_cast<void*>(&ListCreate)},
        {addr::SC4UI_F793D50, reinterpret_cast<void*>(&ListDelete)},
        {addr::SC4UI_F793DA0, reinterpret_cast<void*>(&ListAdd)},
        {addr::SC4UI_F793EA0, reinterpret_cast<void*>(&OverlayClassId)},
        {addr::SC4UI_F793ED0, reinterpret_cast<void*>(&OverlayQueryInterface)},
        {addr::SC4UI_F793F30, reinterpret_cast<void*>(&OverlayDestruct)},
        {addr::SC4UI_F793F80, reinterpret_cast<void*>(&OverlayAsWindow)},
        {addr::SC4UI_F793F90, reinterpret_cast<void*>(&OverlayCountedDelete)},
        {addr::SC4UI_F793FC0, reinterpret_cast<void*>(&OverlayInterfaceQueryInterface)},
        {addr::SC4UI_F793FF0, reinterpret_cast<void*>(&OverlaySetPicture)},
        {addr::SC4UI_F794060, reinterpret_cast<void*>(&OverlayConstruct)},
        {addr::SC4UI_F7940E0, reinterpret_cast<void*>(&OverlayDelete)},
        {addr::SC4UI_F7941C0, reinterpret_cast<void*>(&OverlayCreate)},
        {addr::SC4UI_F7941F0, reinterpret_cast<void*>(&FrameShutdown)},
        {addr::SC4UI_F794220, reinterpret_cast<void*>(&FrameMouseUp)},
        {addr::SC4UI_F794260, reinterpret_cast<void*>(&FrameSetAllowed)},
        {addr::SC4UI_F7942F0, reinterpret_cast<void*>(&FrameSetColors)},
        {addr::SC4UI_F794360, reinterpret_cast<void*>(&OtherMouseDown)},
        {addr::SC4UI_F794390, reinterpret_cast<void*>(&OtherMouseUp)},
        {addr::SC4UI_F794430, reinterpret_cast<void*>(&FrameConstruct)},
        {addr::SC4UI_F794490, reinterpret_cast<void*>(&FrameDelete)},
        {addr::SC4UI_F7944C0, reinterpret_cast<void*>(&FrameEdgeAt)},
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

const bool g_panels3Registered = registry::Register({
    "ui.panels.part3",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F793810,
    "Our advice list control, overlay picture window and the frame window's first functions (docs/ui.md)",
    false,
    InstallPanels3,
});

} // namespace
} // namespace opensc4::ui
