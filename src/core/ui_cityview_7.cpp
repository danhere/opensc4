// The city view (cSC4View3DWin), its last part: the handlers
// (0x007F3C10 to 0x007F5F00).
//
// The view is one object of 0x448 bytes with four parts: the interface
// cISC4View3DWin at +0 (vtable 0x00ABCE40), a message target at +4 (vtable
// 0x00ABCE2C), a window notification target at +8 (vtable 0x00ABCE1C) and
// the window itself at +0xC (vtable 0x00ABCBC0). This file holds:
//
//   - TransportMenu (0x007F3C10): the pop-up menu of a transport button,
//     filled from occupant groups;
//   - DeleteView (0x007F3E30): the deleting destructor;
//   - WindowMessage (0x007F3E50, slot 3 of the window): keys through the
//     key accelerator into commands, the mouse entering and leaving, the
//     focus, a cheat's text, links;
//   - Start3D (0x007F41B0): on the message 0x26D31EC0 the camera, the camera
//     control, the 3D renderer and the view's helpers are made and wired
//     together, and the loading picture gets its line of text;
//   - OnCommand (0x007F4690): what a button of the view's bars does, by the
//     button's ID: menus opened, commands sent to the command server, the
//     data view panels put away;
//   - OnMessage (0x007F53C0, slot 3 of the message target): the city's and
//     the application's messages;
//   - OnNotify (0x007F58E0, slot 3 of the notification target): a click
//     (OnCommand) and the three mode buttons that toggle;
//   - SetCursorText (0x007F5CF0, slot 40 of the interface).
//
// The list of this part names nine more starts: 0x007F4830, 0x007F4AB0,
// 0x007F4B10, 0x007F4FF0, 0x007F52B0, 0x007F52E0 and 0x007F52F0 are labels
// inside OnCommand, 0x007F5C20 and 0x007F5CA0 labels inside OnNotify. The
// last function before the camera view, 0x007F5F00, is a vector's
// assignment that only the camera view calls: it is left alone.
//
// Replacement, off by default:
//
//   ui.cityview.part7: the eight functions above.
//
// Start3D makes the renderer's objects and hands them to each other; it
// draws nothing and sets no driver state, so it is here. The camera, the
// camera control, the renderer, windows, the command server and the view's
// other methods are the game's, called through their vtables or by address.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>

namespace opensc4::ui {
namespace {

namespace view {
constexpr std::size_t kTarget = 0x4;          // the message target
constexpr std::size_t kNotify = 0x8;          // the window notification target
constexpr std::size_t kWindow = 0xC;          // the window
constexpr std::size_t kManager = 0x10;        // the window manager (the window's +4)
constexpr std::size_t kStarted = 0xE5;        // byte, set by Start3D
constexpr std::size_t kHelperA = 0xE8;        // class 0xA9C73857
constexpr std::size_t kCamera = 0xEC;
constexpr std::size_t kControl = 0xF0;        // the camera control
constexpr std::size_t kRenderer = 0xF4;
constexpr std::size_t kHelperB = 0xF8;        // class 0xA9C741B3
constexpr std::size_t kMouseInside = 0x118;   // byte
constexpr std::size_t kMouseData = 0x11C;
constexpr std::size_t kKeys = 0x120;          // the key accelerator
constexpr std::size_t kPending = 0x14C;       // list of messages kept for later
constexpr std::size_t kHelperC = 0x150;       // class 0xA2FFB5D3
constexpr std::size_t kSimulator = 0x154;
constexpr std::size_t kBar = 0x18C;           // the window of the mode buttons
constexpr std::size_t kMode = 0x1F4;          // 1, 2, 3 or -1
constexpr std::size_t kMenuWindow = 0x1FC;    // the open menu's window
constexpr std::size_t kOpenMenu = 0x200;      // the open menu's command (target part's +0x1FC)
constexpr std::size_t kMenuCommand = 0x204;   // the command whose menu is open
constexpr std::size_t kCivicLists = 0x224;    // six 8-byte places, one per civic menu
constexpr std::size_t kTransportLists = 0x274; // six more, one per transport menu
constexpr std::size_t kTickerFlags = 0x2D4;   // three bytes
constexpr std::size_t kTicker = 0x2DC;
constexpr std::size_t kBudget = 0x2E8;
constexpr std::size_t kGraphs = 0x2F8;
constexpr std::size_t kStyles = 0x2FC;
constexpr std::size_t kMap = 0x300;
constexpr std::size_t kDataPanel = 0x30C;
constexpr std::size_t kFilter = 0x320;        // an occupant filter
constexpr std::size_t kScroll = 0x348;        // four dwords, zeroed when the focus goes
constexpr std::size_t kDataView = 0x370;
constexpr std::size_t kDataViewKept = 0x374;
constexpr std::size_t kCursorTexts = 0x3DC;   // list of cursor texts, highest priority first
constexpr std::size_t kLoading = 0x404;       // the loading picture
constexpr std::size_t kLoadingPlain = 0x408;  // byte: the picture's text without the city's name
constexpr std::size_t kTools = 0x40C;         // a window with the two halves 0xCC1A6983 and 0x10000005
} // namespace view

constexpr uint32_t kButtonIid = 0x8810;

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

// A string from the text resources: 0x20 bytes, a string first.
struct LocalizedString {
    GameString text;
    uint32_t rest[3];
};
static_assert(sizeof(LocalizedString) == 0x20);

// A set of numbers (the game's red-black tree): the header node and a count.
struct Uint32Set {
    char* header;
    uint32_t count;
};

struct InsertResult {
    void* node;
    uint32_t added;
};

// A window message as the window's handler gets it.
struct WinMessage {
    uint32_t type;
    uint32_t a;
    uint32_t b;
    uint32_t c;
};

// What a key message points at with its third number.
struct KeyData {
    uint32_t unused;
    uint32_t key;
    uint32_t modifiers;
};

// What the key accelerator fills for a key: a command and three numbers.
struct KeyCommand {
    uint32_t command;
    uint32_t a;
    uint32_t b;
    uint32_t c;
};

// The game's variant as the command parameters take it.
struct Variant {
    alignas(4) char raw[0x14];
};

// One cursor text, and its node in the list at +0x3DC.
struct CursorText {
    uint32_t id;
    uint32_t priority;
    GameString title;
    GameString body;
    int32_t icon;
};
static_assert(sizeof(CursorText) == 0x34);

struct CursorNode {
    CursorNode* next;
    CursorNode* previous;
    CursorText text;
};
static_assert(sizeof(CursorNode) == 0x3C);

// A node of the list of kept messages at +0x14C.
struct PendingNode {
    PendingNode* next;
    PendingNode* previous;
    void* message;
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

void Release(void* object)
{
    Virtual<uint32_t>(object, 0x8);
}

// An empty string as the game makes one inline: eight bytes of buffer.
void MakeEmpty(GameString& text)
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

void DestroyString(GameString& text)
{
    char* const buffer = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (buffer)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(buffer));
}

// The set's constructor and destructor as TransportMenu has them inline
// (the game's own copies are 0x0065E8C0 and 0x0046F9D0).
void MakeSet(Uint32Set& set)
{
    set.header = nullptr;
    set.header = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{0x14});
    set.count = 0;
    At<uint8_t>(set.header, 0x0) = 0;
    At<void*>(set.header, 0x4) = nullptr;
    At<void*>(set.header, 0x8) = set.header;
    At<void*>(set.header, 0xC) = set.header;
}

void DestroySet(Uint32Set& set)
{
    if (set.count != 0) {
        This<void>(addr::SC4Uint32Set_EraseNodes, &set, At<void*>(set.header, 0x4));
        At<void*>(set.header, 0x8) = set.header;
        At<void*>(set.header, 0x4) = nullptr;
        At<void*>(set.header, 0xC) = set.header;
        set.count = 0;
    }
    if (set.header)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(set.header));
}

// A command sent to the command server with no parameters.
void Send(uint32_t command)
{
    void* const commands = Global<void*>(addr::SC4_Commands);
    Virtual<uint32_t>(commands, 0x50, command, static_cast<void*>(nullptr), uint32_t{0});
}

// The pop-up menu made from two sets of occupant groups and shown at the
// button: the maker 0x007F06B0, the opener 0x007EAEB0.
void OpenGroupMenu(char* self, void* window, uint32_t command, uint32_t maker, Uint32Set& groups, Uint32Set& others,
                   uint32_t flags, uint32_t kind, char* list)
{
    void* const menu =
        This<void*>(addr::SC4UI_F7F06B0, static_cast<void*>(self), maker, &groups, &others, flags, int32_t{-1});
    This<void>(addr::SC4UI_F7EAEB0, static_cast<void*>(self), command, window, command, menu, kind, list);
    Release(menu);
}

// The game's 0x007F3C10. (window, message), ret 8; `this` is the view. The
// menu of one of the transport buttons, by the message's command; pressed
// again while its menu is open, the menu closes.
void __fastcall TransportMenu(char* self, void*, void* window, WinMessage* message)
{
    static const uint32_t kGroup1300 = 0x1300;
    static const uint32_t kGroup130A = 0x130A;
    static const uint32_t kGroup1508 = 0x1508;
    static const uint32_t kGroup1519 = 0x1519;

    const uint32_t command = message->b;
    Uint32Set groups;
    MakeSet(groups);
    Uint32Set others;
    MakeSet(others);

    InsertResult result;
    uint32_t flags = 0;
    char* list = nullptr;
    switch (message->b) {
    case 0x2B730C5B:
        flags = 0x4000;
        break;
    case 0x29:
        This<void*>(addr::SC4Uint32Set_Insert2, &groups, &result, &kGroup1300);
        flags = 0x4001;
        list = self + view::kTransportLists + 0x10;
        break;
    case 0x31:
        flags = 0x4005;
        list = self + view::kTransportLists + 0x8;
        break;
    case 0x299237BF:
        This<void*>(addr::cSC4AdvisorTransportation_ZotInsert, &groups, &result, &kGroup130A);
        flags = 0x4002;
        list = self + view::kTransportLists + 0x18;
        break;
    case 0x6999BF56:
        list = self + view::kTransportLists;
        flags = 0x4000;
        break;
    case 0xA99234A6:
        This<void*>(addr::SC4Uint32Set_Insert2, &groups, &result, &kGroup1519);
        list = self + view::kTransportLists + 0x28;
        break;
    case 0xE99234B3:
        This<void*>(addr::cSC4AdvisorTransportation_ZotInsert, &groups, &result, &kGroup1508);
        list = self + view::kTransportLists + 0x20;
        break;
    default:
        break;
    }

    if (At<uint32_t>(self, view::kMenuCommand) == command)
        Virtual<void>(self, 0xF0);
    else
        OpenGroupMenu(self, window, command, 0x07BDDF1C, groups, others, flags, 0x14215ED2, list);

    DestroySet(others);
    DestroySet(groups);
}

// The game's 0x007F3E30. (flags), ret 4: the deleting destructor, reached
// from the window's slot 0 through a thunk.
char* __fastcall DeleteView(char* self, void*, uint32_t flags)
{
    This<void>(addr::SC4UI_F7F1140, static_cast<void*>(self));
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(self));
    return self;
}

// A key that the view's own handlers did not take: the key accelerator's
// command for it, sent with its three numbers. Returns whether there was one.
uint8_t SendKeyCommand(char* self, uint32_t key, uint32_t modifiers, uint8_t down)
{
    KeyCommand found = {0, 0, 0, 0};
    if (!Virtual<uint8_t>(At<void*>(self, view::kKeys), 0x24, key, modifiers, down, &found))
        return 0;

    void* const commands = Global<void*>(addr::SC4_Commands);
    void* parameters = nullptr;
    Virtual<uint8_t>(commands, 0x14, This<void**>(addr::SC4RefPtr_Reset, &parameters));
    for (const uint32_t number : {found.a, found.b, found.c}) {
        Variant value;
        Virtual<void>(parameters, 0x20, This<void*>(addr::SC4ScriptNumber_FromUint, &value, number));
        This<void>(addr::SC4ScriptNumber_Destroy, &value);
    }
    Virtual<uint32_t>(Global<void*>(addr::SC4_Commands), 0x50, found.command, parameters, uint32_t{0});
    if (parameters)
        Release(parameters);
    return 1;
}

// The game's 0x007F3E50, slot 3 of the window. (message), ret 4, returns a
// byte; `this` is the window part.
uint8_t __fastcall WindowMessage(char* window, void*, WinMessage* message)
{
    char* const self = window - view::kWindow;
    Cdecl<uint8_t>(addr::SC4Tutorial_FilterWinMessage, message);

    const uint32_t type = message->type;
    switch (type) {
    case 0x3:
        // A child's notice 0x49D993D4: the view takes the focus and passes
        // the notice's two numbers on.
        if (message->a == 0x49D993D4) {
            This<void>(addr::SC4Win_F99BDE4, static_cast<void*>(window));
            This<void>(addr::SC4UI_F7E9FA0, static_cast<void*>(self), message->b, message->c, uint32_t{1});
            return 1;
        }
        break;

    case 0xD:
    case 0x13:
    case 0x14:
        // The mouse comes in (0x13) or goes (0x14).
        if (type == 0x13)
            At<uint8_t>(self, view::kMouseInside) = 1;
        else if (type == 0x14)
            At<uint8_t>(self, view::kMouseInside) = 0;
        This<void>(addr::SC4UI_F7EA810, static_cast<void*>(self));
        return This<uint8_t>(addr::SC4Win_F99CCF0, static_cast<void*>(window), message);

    case 0x10:
        // The focus goes: scrolling stops.
        if (Global<void*>(addr::SC4_City)) {
            At<uint32_t>(self, view::kScroll + 0x0) = 0;
            At<uint32_t>(self, view::kScroll + 0x4) = 0;
            At<uint32_t>(self, view::kScroll + 0x8) = 0;
            At<uint32_t>(self, view::kScroll + 0xC) = 0;
            This<void>(addr::SC4UI_F7E6C60, static_cast<void*>(self));
            Virtual<uint8_t>(self, 0x88);
            Virtual<void>(window, 0x238, uint32_t{0});
        }
        return 1;

    case 0x1B:
        // A key down (5) or up (6).
        if (message->b == 5 || message->b == 6) {
            const KeyData* const data = reinterpret_cast<const KeyData*>(message->c);
            const uint32_t key = data->key;
            const uint32_t modifiers = data->modifiers;
            const uint8_t down = message->b == 5;
            const std::uintptr_t handler = down ? addr::SC4UI_F7E66D0 : addr::SC4UI_F7E67D0;
            if (This<uint8_t>(handler, static_cast<void*>(self), key, modifiers))
                return 1;
            return SendKeyCommand(self, key, modifiers, down);
        }
        break;

    case 0x230E27AC: {
        // A cheat's text: its words go to the view's cheat handler.
        alignas(4) char words[0x3C];
        This<void*>(addr::SC4Tokenizer_SC4Tokenizer, static_cast<void*>(words),
                    Virtual<const char*>(reinterpret_cast<void*>(message->b), 0x14));
        const uint8_t taken =
            This<uint8_t>(addr::SC4UI_F7F3510, static_cast<void*>(self), message->a, static_cast<void*>(words));
        This<void>(addr::SC4Tokenizer_Destroy, static_cast<void*>(words));
        return taken;
    }

    case 0xE4A85D91: {
        // A link clicked in a text.
        uint8_t handler = 0;
        const uint8_t followed =
            This<uint8_t>(addr::SC4UI_FollowLink, &handler, reinterpret_cast<void*>(message->b), uint32_t{0});
        return followed ? 0 : 1;
    }

    case 0x6A5BDFCD:
    case 0x6A720993:
    case 0xEA5BDFBC:
        This<void>(addr::SC4UI_F7E7830, static_cast<void*>(self), message);
        break;

    default:
        break;
    }
    return This<uint8_t>(addr::SC4Win_F99CCF0, static_cast<void*>(window), message);
}

// One of the view's helper objects: asked of the framework by class, made
// here when the framework has none.
void Ensure(void*& place, uint32_t classId, uint32_t iid, uint32_t size, std::uintptr_t constructor)
{
    if (place)
        return;
    void* const com = Cdecl<void*>(addr::RZGetCOM);
    if (Virtual<uint8_t>(com, 0xC, classId, iid, &place))
        return;
    void* const memory = Cdecl<void*>(addr::SC4_operator_new, size);
    void* const made = memory ? This<void*>(constructor, memory) : nullptr;
    place = made;
    Virtual<uint32_t>(made, 0x4);
}

// The loading picture's line of text: "<label> <city name>", or a plain
// label when the byte at +0x408 is set.
void SetLoadingText(char* self)
{
    void* const city = Global<void*>(addr::SC4_City);
    GameString name;
    MakeEmpty(name);
    char* pending;
    if (!At<uint8_t>(self, view::kLoadingPlain)) {
        static const char kSpace[] = " ";
        Virtual<void>(city, 0x30, &name);
        LocalizedString label;
        GameString* const got = This<GameString*>(addr::SC4LocalizedString_SC4LocalizedString, &label,
                                                  uint32_t{0x6A231EAA}, uint32_t{0xAA765647}, uint32_t{1});
        StringBody* const source = got ? &got->body : nullptr;
        uint8_t tag; // never set in the original either
        GameString text;
        This<void>(addr::SC4String_CopyBuffer, &text.body, source, uint32_t{0}, uint32_t{0xFFFFFFFF},
                   static_cast<void*>(&tag));
        text.vtable = addr::SC4String_Vtable;
        text.zero = 0;
        DestroyString(label.text);
        This<void>(addr::cSC4Advisor_F40EBC0, &text.body, &kSpace[0], &kSpace[1], static_cast<void*>(&tag));
        This<void>(addr::SC4String_AppendRange, &text.body, name.body.begin, name.body.end, static_cast<void*>(&tag));
        This<void>(addr::SC4UI_F777640, At<void*>(self, view::kLoading), uint32_t{0}, &text);
        pending = text.body.begin;
        text.vtable = addr::SC4String_Vtable;
    } else {
        LocalizedString label;
        GameString* const got = This<GameString*>(addr::SC4LocalizedString_SC4LocalizedString, &label,
                                                  uint32_t{0x6A231EAA}, uint32_t{0xAA76563A}, uint32_t{1});
        This<void>(addr::SC4UI_F777640, At<void*>(self, view::kLoading), uint32_t{0}, got);
        pending = label.text.body.begin;
        label.text.vtable = addr::SC4String_Vtable;
    }
    if (pending)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(pending));
    DestroyString(name);
}

// The game's 0x007F41B0. No arguments; `this` is the view. Run once when
// the city's 3D side starts (message 0x26D31EC0).
void __fastcall Start3D(char* self, void*)
{
    void* const window = self + view::kWindow;
    const int32_t width = Virtual<int32_t>(window, 0xA4);
    const int32_t height = Virtual<int32_t>(window, 0xA8);
    At<uint8_t>(self, view::kStarted) = 1;

    void*& camera = At<void*>(self, view::kCamera);
    Ensure(camera, 0xE9C6262A, 0xE9C6262A, 0x1A8, addr::cS3DCamera_cS3DCamera);
    This<uint8_t>(addr::SC4_ReturnTrue648F00, camera);

    void*& control = At<void*>(self, view::kControl);
    Ensure(control, 0xC9C628EC, 0xC9C628EC, 0x160, addr::cSC4CameraControl_cSC4CameraControl);
    This<void>(addr::cSC4CameraControl_SetViewSize, control, width, height);
    float centre[3] = {512.0f, 0.0f, 512.0f};
    This<void>(addr::cSC4CameraControl_Init, control, &centre[0], camera);

    if (!At<void*>(self, view::kFilter)) {
        char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x10});
        char* filter = nullptr;
        if (memory) {
            This<void>(addr::SC4_OccupantFilterBase, static_cast<void*>(memory));
            At<std::uintptr_t>(memory, 0x0) = addr::SC4CityFiles_PictureFilterVtable;
            At<std::uintptr_t>(memory, 0x4) = addr::SC4CityFiles_PictureFilterVtable4;
            At<uint8_t>(memory, 0xC) = 0;
            At<uint8_t>(memory, 0xD) = 0;
            At<uint8_t>(memory, 0xE) = 0;
            filter = memory;
        }
        At<uint8_t>(filter, 0xE) = 1;
        At<uint8_t>(filter, 0xD) = 1;
        At<uint8_t>(filter, 0xC) = 1;
        At<void*>(self, view::kFilter) = filter;
        Virtual<uint32_t>(filter, 0x4);
    }

    void*& renderer = At<void*>(self, view::kRenderer);
    Ensure(renderer, 0xE9C622D8, 0x681BD529, 0x2E8, addr::cSC43DRender_cSC43DRender);

    // The renderer gets the graphic system, the view's size, the driver, the
    // camera and the camera control, then starts.
    void* graphics = nullptr;
    void* const service = Global<void*>(addr::SC4_Service43C9C);
    if (Virtual<uint8_t>(service, 0x0, uint32_t{0xC47B747C}, &graphics)) {
        void* const driver = Virtual<void*>(graphics, 0xC);
        Virtual<uint8_t>(renderer, 0x18, graphics);
        Virtual<uint8_t>(renderer, 0x30, width, height);
        Virtual<uint8_t>(renderer, 0x1C, driver);
        Virtual<uint8_t>(renderer, 0x3C, At<void*>(self, view::kCamera));
        Virtual<uint8_t>(renderer, 0x44, control);
        Virtual<uint8_t>(renderer, 0xC);
    }

    void*& helperC = At<void*>(self, view::kHelperC);
    if (!helperC) {
        void* const com = Cdecl<void*>(addr::RZGetCOM);
        Virtual<uint8_t>(com, 0xC, uint32_t{0xA2FFB5D3}, uint32_t{0x02FFBB60}, &helperC);
    }

    This<void>(addr::SC4UI_F7F1640, static_cast<void*>(self));
    if (At<void*>(self, view::kLoading))
        SetLoadingText(self);
    This<void>(addr::SC4UI_F7EABF0, static_cast<void*>(self));

    void*& helperA = At<void*>(self, view::kHelperA);
    Ensure(helperA, 0xA9C73857, 0x86AD10ED, 0x128, addr::SC4Service86AD10EC_Construct);
    Virtual<uint8_t>(helperA, 0xC);
    This<void>(addr::SC4UI_F7EABF0, static_cast<void*>(self));

    void*& helperB = At<void*>(self, view::kHelperB);
    Ensure(helperB, 0xA9C741B3, 0xA9C741B3, 0x114, addr::SC4ClassA9C741B3_Construct);
    This<void>(addr::SC4ClassA9C741B3_Start, helperB);
    Cdecl<void>(addr::SC4_FetchViewGlobals);
    This<void>(addr::SC4UI_F7EABF0, static_cast<void*>(self));
    This<void>(addr::SC4UI_F7EF820, static_cast<void*>(self));

    GameString none;
    MakeEmpty(none);
    Cdecl<void>(addr::SC4UI_PostLoadingText, uint32_t{2}, &none);
    DestroyString(none);

    This<void>(addr::SC4UI_F7EDEB0, static_cast<void*>(self));
    This<void>(addr::SC4UI_F7EABF0, static_cast<void*>(self));
    if (graphics)
        Release(graphics);
}

// The menu of a civic button of OnCommand: the groups by the command.
void CivicMenu(char* self, void* window, const WinMessage* message, uint32_t command)
{
    static const uint32_t kGroup1006 = 0x1006;
    static const uint32_t kGroup1500 = 0x1500;
    static const uint32_t kGroup1501 = 0x1501;
    static const uint32_t kGroup1502 = 0x1502;
    static const uint32_t kGroup1503 = 0x1503;
    static const uint32_t kGroup1504 = 0x1504;
    static const uint32_t kGroup1505 = 0x1505;
    static const uint32_t kGroup1506 = 0x1506;
    static const uint32_t kGroup1507 = 0x1507;
    static const uint32_t kGroup150A = 0x150A;
    static const uint32_t kGroup150B = 0x150B;

    Uint32Set groups;
    This<void*>(addr::SC4Uint32Set_Construct, &groups);
    Uint32Set others;
    This<void*>(addr::SC4Uint32Set_Construct, &others);

    InsertResult result;
    const auto add = [&](const uint32_t& group) {
        This<void*>(addr::cSC4AdvisorTransportation_ZotInsert, &groups, &result, &group);
    };
    char* list = nullptr;
    switch (message->b) {
    case 0x38:
        add(kGroup1502);
        list = self + view::kCivicLists + 0x8;
        break;
    case 0x3:
        add(kGroup1006);
        list = self + view::kCivicLists + 0x20;
        break;
    case 0x34:
        add(kGroup150B);
        break;
    case 0x37:
        add(kGroup1500);
        add(kGroup1501);
        list = self + view::kCivicLists;
        break;
    case 0x42:
        add(kGroup1503);
        add(kGroup1504);
        add(kGroup1505);
        add(kGroup1506);
        list = self + view::kCivicLists + 0x18;
        break;
    case 0x09930709:
        add(kGroup150A);
        list = self + view::kCivicLists + 0x28;
        break;
    case 0x89DD5405:
        add(kGroup1507);
        list = self + view::kCivicLists + 0x10;
        break;
    default:
        break;
    }

    if (At<uint32_t>(self, view::kMenuCommand) == command)
        Virtual<void>(self, 0xF0);
    else
        OpenGroupMenu(self, window, command, 0xC8DBCCBA, groups, others, 0, 0x14215ED4, list);

    This<void>(addr::SC4Uint32Set_Destroy46F9D0, &others);
    This<void>(addr::SC4Uint32Set_Destroy46F9D0, &groups);
}

// The data view buttons of OnCommand: the panels that go with a data view
// are put away, unless the view was told to keep them, and the plain view
// is noted.
void LeaveDataView(char* self)
{
    if (At<uint32_t>(self, view::kDataViewKept) == 0) {
        void* const city = Global<void*>(addr::SC4_City);
        if (Virtual<uint8_t>(Virtual<void*>(city, 0x144), 0x6C))
            Virtual<void>(Virtual<void*>(Global<void*>(addr::SC4_City), 0x144), 0x68);

        if (void* const budget = At<void*>(self, view::kBudget)) {
            if (Virtual<uint8_t>(budget, 0x24))
                Virtual<void>(At<void*>(self, view::kBudget), 0x1C);
        }
        if (void* const map = At<void*>(self, view::kMap))
            Virtual<void>(map, 0x118);
        if (void* const graphs = At<void*>(self, view::kGraphs))
            This<void>(addr::SC4UI_F76E420, graphs);
        if (void* const panel = At<void*>(self, view::kDataPanel))
            Virtual<void>(panel, 0x118);
        if (void* const styles = At<void*>(self, view::kStyles)) {
            if (This<uint8_t>(addr::SC4UI_F7696C0, styles))
                This<void>(addr::SC4UI_F7696B0, At<void*>(self, view::kStyles));
        }
    }
    void* const graphs = At<void*>(self, view::kGraphs);
    At<uint32_t>(self, view::kDataView) = 0x15200002;
    At<uint32_t>(self, view::kDataViewKept) = 0;
    if (graphs)
        This<void>(addr::SC4UI_F76E4B0, graphs, uint32_t{0xE});
}

// A menu made by 0x007E5C10 from two resource IDs, shown at the button.
void ResourceMenu(char* self, void* window, uint32_t first, uint32_t second, uint32_t command)
{
    if (window)
        This<void>(addr::SC4UI_F7E5C10, static_cast<void*>(self), first, second, window, command);
}

// The game's 0x007F4690. (window, message), ret 8; `this` is the view. What
// the button `message->b` does.
void __fastcall OnCommand(char* self, void*, void* window, WinMessage* message)
{
    if (self)
        Virtual<uint32_t>(self, 0x4);

    const uint32_t command = message->b;
    switch (command) {
    case 0x32:
    case 0x35:
    case 0x39:
    case 0x40:
        if (window)
            This<void>(addr::SC4UI_F7F39D0, static_cast<void*>(self), window, message);
        break;

    case 0x3:
    case 0x34:
    case 0x37:
    case 0x38:
    case 0x42:
    case 0x09930709:
    case 0x89DD5405:
        if (window)
            CivicMenu(self, window, message, command);
        break;

    case 0x43: {
        void* sound = nullptr;
        This<void*>(addr::SC4RefHolder_SC4RefHolder, &sound, Global<void*>(addr::SC4_Sound43CB8));
        if (sound) {
            Virtual<uint32_t>(sound, 0x2C, uint32_t{1}, uint32_t{0x4A5C31D7}, uint32_t{0}, uint32_t{0}, uint32_t{0});
            Release(sound);
        }
        Send(0x6A935E3F);
        break;
    }
    case 0x44:
        Send(0x6A935E3F);
        break;
    case 0x098F4F6C:
        Send(0x6A935E2D);
        break;

    case 0x0991EE13:
        ResourceMenu(self, window, 0xE9949936, 0x69923479, 0x0991EE13);
        break;
    case 0x0991EE39:
        ResourceMenu(self, window, 0xC9930681, 0x699306ED, 0x0991EE39);
        break;
    case 0x6991EE42:
        ResourceMenu(self, window, 0x899302FC, 0x0992FD17, 0x6991EE42);
        break;
    case 0xA994824D:
        ResourceMenu(self, window, 0xE99237FF, 0xC99237A0, command);
        break;
    case 0xE991EE2F:
        ResourceMenu(self, window, 0x4992F764, 0xE992F711, 0xE991EE2F);
        break;
    case 0xA9ED5617:
        if (window) {
            This<void>(addr::SC4UI_F7E5C10, static_cast<void*>(self), uint32_t{0xAA356502}, uint32_t{0xCA35CBED},
                       window, uint32_t{0xA9ED5617});
            This<void>(addr::SC4UI_F7E6EE0, static_cast<void*>(self));
        }
        break;

    case 0x1:
    case 0x33:
    case 0x0992FD36:
    case 0x09DE87AE:
    case 0x4BFC8C2F:
    case 0x69A30679:
    case 0x6BFC8C55:
    case 0xE9DE87A7:
        if (window)
            This<void>(addr::SC4UI_F7E7130, static_cast<void*>(self), window, message);
        break;

    case 0x0B954123:
        Send(0x0B980581);
        break;

    case 0x41:
    case 0x15200002:
    case 0x15200003:
    case 0x49EDF9B7:
    case 0x99887755:
    case 0xABC54125:
        This<void>(addr::SC4UI_F7E6F60, static_cast<void*>(self), command);
        break;

    case 0x09D27EB0:
    case 0x29D27EC0:
    case 0x49D27ED0:
    case 0xAA9211B3:
        LeaveDataView(self);
        break;

    case 0x2992F72C:
        Virtual<void>(self, 0xF0);
        Send(0x6A935DEE);
        break;
    case 0x2A4FBB08:
        Send(0x6A935CC9);
        break;

    case 0x29:
    case 0x31:
    case 0x299237BF:
    case 0x2B730C5B:
    case 0x2B79E398:
    case 0x6999BF56:
    case 0xA99234A6:
    case 0xE99234B3:
    case 0xEA6E379D:
        if (window)
            TransportMenu(self, nullptr, window, message);
        break;

    case 0x4992046A:
        Send(0x6A935CB7);
        break;
    case 0x4A4FBB60:
        Send(0x6A935CB2);
        break;

    case 0x4BB92C1F: {
        // The tool in hand gets an Escape key, when it is the tool 0x2B709676.
        void* const tool = Virtual<void*>(self, 0x60);
        if (tool && Virtual<uint32_t>(tool, 0x14) == 0x2B709676)
            Virtual<uint8_t>(tool, 0x38, uint32_t{0x1B}, uint32_t{0});
        break;
    }
    case 0x4BB92C20:
        if (void* const tools = At<void*>(self, view::kTools)) {
            Cdecl<uint8_t>(addr::SC4Win_ShowChild, tools, uint32_t{0xCC1A6983}, uint32_t{0}, uint32_t{0});
            Cdecl<uint8_t>(addr::SC4Win_ShowChild, At<void*>(self, view::kTools), uint32_t{0x10000005}, uint32_t{1},
                           uint32_t{0});
        }
        break;
    case 0x4BB92C21:
        if (void* const tools = At<void*>(self, view::kTools)) {
            Cdecl<uint8_t>(addr::SC4Win_ShowChild, tools, uint32_t{0xCC1A6983}, uint32_t{1}, uint32_t{0});
            Cdecl<uint8_t>(addr::SC4Win_ShowChild, At<void*>(self, view::kTools), uint32_t{0x10000005}, uint32_t{0},
                           uint32_t{0});
        }
        break;

    case 0x69B9324A:
        if (window) {
            void* const menu = Cdecl<void*>(addr::SC4UI_F78F100, uint32_t{0x29B6C670}, uint32_t{0x4A2B2464},
                                            uint32_t{0}, uint32_t{0x8A416A99});
            This<void>(addr::SC4UI_F7E7270, static_cast<void*>(self), uint32_t{0x4A2B2FE2}, window,
                       uint32_t{0x69B9324A}, menu, uint32_t{0x8A2564A2}, uint32_t{0});
        }
        break;

    case 0x6A4FBB31:
        Send(0x6A935CB0);
        break;
    case 0x8998BBDF:
        Send(0x6A935CB5);
        break;
    case 0x8A4FBAEA:
        Send(0x6A935CCC);
        break;
    case 0x8B96B73E:
        Send(0x6A935CF4);
        break;
    case 0x99887766:
        Send(0x6A935CF3);
        break;
    case 0xA98F4F88:
        Send(0x6A935E34);
        break;
    case 0xAA5543A1:
        Send(0x2A94A5B6);
        break;
    case 0xABA7ED0A:
        Send(0xABCDEE11);
        break;

    case 0xAB9537B7:
        // A menu with one item checked from the prop developer's slot 10.
        if (window) {
            This<void>(addr::SC4UI_F7E5D80, static_cast<void*>(self), uint32_t{0xCB95403E}, uint32_t{0xAB954023},
                       window, uint32_t{0xAB9537B7}, int32_t{-75});
            if (At<void*>(self, view::kMenuWindow)) {
                const uint8_t checked = Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DB8), 0x28);
                Cdecl<uint8_t>(addr::SC4Win_CheckChild, At<void*>(self, view::kMenuWindow), uint32_t{0xEB9541EB},
                               checked);
            }
        }
        break;

    case 0x30:
    case 0x29920899:
    case 0xA998AF42:
    case 0xC998AF00:
        if (window)
            This<void>(addr::SC4UI_F7EC740, static_cast<void*>(self), window, message);
        break;

    case 0xCA35CB74:
    case 0xCA35CB76:
    case 0xCA35CB78:
        This<void>(addr::SC4UI_F7E70C0, static_cast<void*>(self), command);
        break;

    case 0xC998BB81:
        Send(0x6A935CAD);
        break;
    case 0xCA58EBB7:
        Send(0x6A935E2A);
        break;
    case 0xCB95412C:
        Send(0x0B980587);
        break;
    case 0xE9920494:
        Send(0x6A935CBA);
        break;
    case 0xE999C820:
        Send(0x6A935D36);
        break;
    case 0xE99237B4:
        Send(0x6A935DC9);
        break;
    case 0xEB9541EB:
        Send(0x6B98058A);
        break;

    case 0x36:
    case 0x0A32DDD2:
    case 0x0AA44502:
    case 0x0AA44503:
    case 0x0AA44504:
    case 0x0AA44505:
    case 0x152231F3:
    case 0x29923215:
    case 0x49E95D2B:
    case 0x4A22EA06:
    case 0x4A32DDC7:
    case 0x4A551A6B:
    case 0x6A3D638C:
    case 0x8991EE08:
    case 0x899231E6:
    case 0x8A32DDDB:
    case 0xC99231DA:
    case 0xEA32DDBB:
    case 0xEA32DDE5:
        if (window)
            This<void>(addr::SC4UI_F7ED720, static_cast<void*>(self), window, message);
        break;

    case 0xEA6FA936:
        Virtual<void>(At<void*>(self, view::kDataPanel), 0x118);
        break;

    case 0xABB27A7A:
    case 0xABB27B91:
    case 0xEBF9E342:
    case 0xEBFAC717:
    case 0xEBFAC718:
    case 0xEBFAC719:
    case 0xEBFAC720:
        if (window)
            This<void>(addr::SC4UI_F7EF610, static_cast<void*>(self), window, message);
        break;

    default:
        break;
    }

    // The tutorial hears of every click.
    if (void* const city = Global<void*>(addr::SC4_City)) {
        void* const tutorial = Virtual<void*>(city, 0x148);
        if (tutorial && Virtual<uint8_t>(tutorial, 0x20))
            Virtual<void>(tutorial, 0x38);
    }
    if (self)
        Release(self);
}

// The pause button of the bar follows the simulator: on while paused, and
// flashing.
void ShowPaused(char* self)
{
    if (!At<void*>(self, view::kSimulator))
        return;
    void* button = nullptr;
    void** const out = This<void**>(addr::SC4RefPtr_Reset, &button);
    if (Virtual<uint8_t>(At<void*>(self, view::kBar), 0x90, uint32_t{0xC998BB81}, kButtonIid, out)) {
        const uint8_t paused = Virtual<uint8_t>(At<void*>(self, view::kSimulator), 0x3C);
        Virtual<uint8_t>(button, 0x14, uint32_t{1}, paused);
        const uint8_t still = Virtual<uint8_t>(At<void*>(self, view::kSimulator), 0x3C);
        Virtual<void>(button, 0xB0, still ? uint32_t{0xFFFFFFF0} : uint32_t{0}, uint32_t{0x3E8});
    }
    if (button)
        Release(button);
}

// The game's 0x007F53C0, slot 3 of the message target. (message), ret 4,
// always 1; `this` is the target part.
uint8_t __fastcall OnMessage(char* target, void*, void* message)
{
    char* const self = target - view::kTarget;
    void* const window = self + view::kWindow;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);

    // A command message goes to the interface's ProcessCommand.
    void* asCommand = nullptr;
    if (Virtual<uint8_t>(message, 0x0, uint32_t{0x4B99446A}, &asCommand)) {
        void* const input = Virtual<void*>(message, 0x28);
        void* const output = Virtual<void*>(message, 0x3C);
        Virtual<uint8_t>(self, 0x1C, type, input, output);
        if (asCommand)
            Release(asCommand);
        return 1;
    }

    switch (type) {
    case 0x06CDB65B:
        if (void* const renderer = At<void*>(self, view::kRenderer))
            Virtual<uint8_t>(renderer, 0x50);
        break;

    case 0x0772FAD4:
    case 0x08DAC3E7:
    case 0x08DAC3E8:
        This<void>(addr::SC4UI_F7EA970, static_cast<void*>(self));
        break;
    case 0x0AB99380:
        This<void>(addr::SC4UI_F7EAB60, static_cast<void*>(self));
        break;

    case 0x26C63341:
        This<void>(addr::SC4UI_F7EBFA0, static_cast<void*>(self), Virtual<uint32_t>(message, 0x28));
        break;
    case 0x26C63343:
        Virtual<uint8_t>(self, 0x6C, uint32_t{0});
        break;
    case 0x26C63344:
        This<void>(addr::SC4UI_F7EA5B0, static_cast<void*>(self), Virtual<uint32_t>(message, 0x28));
        break;

    case 0x26D31EC0:
        Start3D(self, nullptr);
        break;
    case 0x26D31EC1:
        This<void>(addr::SC4UI_F7ED0F0, static_cast<void*>(self));
        break;
    case 0x26D31EC2:
        This<void>(addr::SC4UI_F7E9780, static_cast<void*>(self));
        break;
    case 0x26D31EC4:
        This<void>(addr::SC4UI_F7EBF30, static_cast<void*>(self));
        break;
    case 0x26D31EC5:
        This<void>(addr::SC4UI_F7E97A0, static_cast<void*>(self));
        break;

    case 0x44778231: {
        const uint32_t first = Virtual<uint32_t>(message, 0x38);
        const uint32_t second = Virtual<uint32_t>(message, 0x4C);
        This<void>(addr::SC4UI_F7EC0B0, static_cast<void*>(self), first, second);
        break;
    }
    case 0x44778233:
        This<void>(addr::SC4UI_F7E6E40, static_cast<void*>(self));
        break;

    case 0x4A44667F: {
        // A line of text for the loading picture.
        void* text = nullptr;
        if (Virtual<uint8_t>(message, 0x74, uint32_t{0x089B7DC8}, &text)) {
            void* const held = text;
            const uint32_t line = Virtual<uint32_t>(message, 0x24);
            if (void* const picture = At<void*>(self, view::kLoading))
                This<void>(addr::SC4UI_F777640, picture, line, held);
        }
        if (text)
            Release(text);
        break;
    }

    case 0x4A7FB7E9:
        This<void>(addr::SC4UI_F7E59B0, static_cast<void*>(self));
        break;
    case 0x66956814:
        This<void>(addr::SC4UI_F7EA6E0, static_cast<void*>(self));
        This<void>(addr::SC4UI_F7EAAA0, static_cast<void*>(self));
        break;
    case 0x66956816:
        This<void>(addr::SC4UI_F7E8510, static_cast<void*>(self));
        break;

    case 0xA6B79602:
    case 0xA6B79612: {
        void* const app = Global<void*>(addr::SC4_App43CC4);
        if (At<uint8_t>(At<char*>(app, 0xC), 0x39C))
            break;
        if (type == 0xA6B79602) {
            This<void>(addr::SC4UI_F7E5B00, static_cast<void*>(self));
            This<void>(addr::SC4UI_F7F1640, static_cast<void*>(self));
        }
        This<void>(addr::SC4UI_F7E5B40, static_cast<void*>(self));
        if (At<uint8_t>(self, view::kMouseInside)) {
            // The original leaves both numbers unset before the call.
            int32_t x = 0;
            int32_t y = 0;
            Virtual<uint8_t>(At<void*>(self, view::kManager), 0x84, &x, &y);
            Virtual<uint8_t>(window, 0x228, x, y, At<uint32_t>(self, view::kMouseData));
        }
        break;
    }
    case 0xA6B79621:
        This<void>(addr::SC4UI_F7E5B40, static_cast<void*>(self));
        break;

    case 0x4A7FB7E2:
    case 0xAA7FB7E0:
    case 0xABBBCA64:
        ShowPaused(self);
        This<void>(addr::SC4UI_F7E59B0, static_cast<void*>(self));
        This<void>(addr::SC4UI_F7E8A90, static_cast<void*>(self));
        if (type == 0xABBBCA64) {
            const uint32_t second = Virtual<uint32_t>(message, 0x24);
            const uint32_t first = Virtual<uint32_t>(message, 0x80);
            This<void>(addr::SC4UI_F7EBB20, static_cast<void*>(self), first, second);
        }
        break;

    case 0xAA5CF1E3:
        // A notice that wants a pop-up: shown now when one may show, else
        // kept, one of each kind.
        if (Cdecl<uint8_t>(addr::SC4UI_CanShowPopup)) {
            This<void>(addr::SC4UI_F7E6E00, static_cast<void*>(self));
        } else {
            const uint32_t kind = Virtual<uint32_t>(message, 0x10);
            PendingNode* const end = At<PendingNode*>(self, view::kPending);
            PendingNode* found;
            Cdecl<PendingNode**>(addr::SC4UI_F7E9360, &found, end->next, end, kind);
            if (At<PendingNode*>(self, view::kPending) == found) {
                void* held = nullptr;
                This<void*>(addr::SC4RefHolder_SC4RefHolder, &held, message);
                This<void>(addr::SC4RefList_PushBack2, static_cast<void*>(self + view::kPending), &held);
                if (held)
                    Release(held);
            }
        }
        break;

    case 0xCA1B1E64:
    case 0xAA1B2002: {
        if (type == 0xCA1B1E64 && At<uint32_t>(self, view::kOpenMenu) == 0x49923239)
            Virtual<void>(self, 0xEC);
        void* const button = Virtual<void*>(window, 0x8C, uint32_t{0x8991EE08});
        const uint8_t on = type == 0xAA1B2002;
        Virtual<uint32_t>(button, 0x110, uint32_t{2}, on);
        This<void>(addr::SC4UI_F7E8A90, static_cast<void*>(self));
        break;
    }

    case 0xAC083D7D: {
        const uint32_t first = Virtual<uint32_t>(message, 0x80);
        This<void>(addr::SC4UI_F7EBB20, static_cast<void*>(self), first, uint32_t{1});
        break;
    }

    default:
        break;
    }

    if (asCommand)
        Release(asCommand);
    return 1;
}

// The view's mode after a mode button's click: the old mode's button let
// up and the new mode started, or the mode ended when its own button came up.
void SwitchMode(char* self, uint32_t mode, uint8_t down)
{
    static const uint32_t kButtons[5] = {0x2988BC85, 0xC988BC79, 0x4988BC6A, 0x8988BC94, 0x2988BC85};
    if (down) {
        const uint32_t old = At<uint32_t>(self, view::kMode);
        if (old != 0xFFFFFFFF) {
            This<void>(addr::SC4UI_F7E8CA0, static_cast<void*>(self), old);
            uint32_t button = 0xFFFFFFFF;
            const uint32_t now = At<uint32_t>(self, view::kMode);
            if (now <= 4)
                button = kButtons[now];
            Cdecl<uint8_t>(addr::SC4Win_CheckChild, At<void*>(self, view::kBar), button, uint32_t{0});
        }
        At<uint32_t>(self, view::kMode) = mode;
        This<void>(addr::SC4UI_F7E8B90, static_cast<void*>(self), mode);
    } else if (At<uint32_t>(self, view::kMode) == mode) {
        At<uint32_t>(self, view::kMode) = 0xFFFFFFFF;
        This<void>(addr::SC4UI_F7E8CA0, static_cast<void*>(self), mode);
    }
}

// The game's 0x007F58E0, slot 3 of the notification target. (window,
// message), ret 8, returns a byte; `this` is the notification part.
uint8_t __fastcall OnNotify(char* target, void*, void* window, WinMessage* message)
{
    char* const self = target - view::kNotify;
    if (!Cdecl<uint8_t>(addr::SC4Tutorial_FilterWinMessage, message))
        return 1;
    if (Virtual<uint8_t>(window, 0x10C, uint32_t{0x1000}))
        return 0;
    if (message->type == 0x12)
        return 1;
    if (message->type != 0x3)
        return 0;
    if (message->a == 0x287259F6) {
        OnCommand(self, nullptr, window, message);
        return 1;
    }
    if (message->a != 0x287259F7)
        return 0;

    uint32_t mode;
    switch (message->b) {
    case 0x2988BC85:
        This<void>(addr::SC4UI_F7EC2D0, static_cast<void*>(self), uint32_t{0x6A935C34});
        return 1;
    case 0x4988BC6A:
        mode = 2;
        break;
    case 0x8988BC94:
        mode = 3;
        break;
    case 0xC988BC79:
        mode = 1;
        break;
    default:
        return 1;
    }

    // The mode 3 button is asked for through the holder's own reset, the
    // other two directly.
    void* button = nullptr;
    uint8_t down = 0;
    void** const out = mode == 3 ? This<void**>(addr::SC4RefPtr_Reset, &button) : &button;
    const uint8_t found = Virtual<uint8_t>(At<void*>(self, view::kBar), 0x94, message->b, kButtonIid, out);
    if (found) {
        down = Virtual<uint8_t>(button, 0x10, uint32_t{1});
        // The mode 1 button comes up again when the city is not yet
        // established and 0x007EB320 says no.
        if (mode == 1 && down && !Virtual<uint8_t>(Global<void*>(addr::SC4_City), 0x58) &&
            !This<uint8_t>(addr::SC4UI_F7EB320, static_cast<void*>(self))) {
            Virtual<uint8_t>(button, 0x14, uint32_t{1}, uint32_t{0});
            if (button)
                Release(button);
            return 1;
        }
    }
    if (button)
        Release(button);

    Virtual<void>(self, 0xEC);
    SwitchMode(self, mode, down);

    if (At<uint8_t>(self, view::kTickerFlags + 1) || At<uint8_t>(self, view::kTickerFlags) ||
        At<uint8_t>(self, view::kTickerFlags + 2))
        Virtual<void>(At<void*>(self, view::kTicker), 0x14);
    return 1;
}

// The game's 0x007F5CF0, slot 40 of the interface. (id, priority, title,
// body, icon), ret 0x14. A cursor text set, changed, or taken away when
// both strings are missing or empty; the list is kept highest priority first.
void __fastcall SetCursorText(char* self, void*, uint32_t id, uint32_t priority, void* title, void* body, int32_t icon)
{
    const bool some = (title && Virtual<uint32_t>(title, 0x1C) != 0) || (body && Virtual<uint32_t>(body, 0x1C) != 0);
    if (!some) {
        Virtual<uint8_t>(self, 0xA4, id);
        This<void>(addr::SC4UI_F7E8E40, static_cast<void*>(self));
        return;
    }

    CursorNode* const end = At<CursorNode*>(self, view::kCursorTexts);
    CursorNode* node = end->next;
    while (node != end && node->text.id != id)
        node = node->next;

    if (node != end) {
        if (title)
            Virtual<void>(&node->text.title, 0x34, title);
        else
            Virtual<void>(&node->text.title, 0x58, uint32_t{0}, uint32_t{0xFFFFFFFF});
        if (body) {
            Virtual<void>(&node->text.body, 0x34, body);
        } else {
            // The original empties the title here, not the body.
            Virtual<void>(&node->text.title, 0x58, uint32_t{0}, uint32_t{0xFFFFFFFF});
        }
        node->text.icon = icon;
        This<void>(addr::SC4UI_F7E8E40, static_cast<void*>(self));
        return;
    }

    CursorNode* before = end->next;
    while (before != end && before->text.priority > priority)
        before = before->next;

    CursorText text;
    MakeEmpty(text.title);
    MakeEmpty(text.body);
    text.id = id;
    text.priority = priority;
    if (title)
        Virtual<void>(&text.title, 0x34, title);
    if (body)
        Virtual<void>(&text.body, 0x34, body);
    text.icon = icon;

    CursorNode* const made = Cdecl<CursorNode*>(addr::SC4_stl_allocate, uint32_t{0x3C});
    This<void*>(addr::SC4UI_F7EFE60, &made->text, &text);
    CursorNode* const previous = before->previous;
    made->next = before;
    made->previous = previous;
    previous->next = made;
    before->previous = made;

    DestroyString(text.body);
    DestroyString(text.title);
    This<void>(addr::SC4UI_F7E8E40, static_cast<void*>(self));
}

bool InstallCityView7(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[8] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F7F3C10, reinterpret_cast<void*>(&TransportMenu)},
        {addr::SC4UI_F7F3E30, reinterpret_cast<void*>(&DeleteView)},
        {addr::SC4UI_F7F3E50, reinterpret_cast<void*>(&WindowMessage)},
        {addr::SC4UI_F7F41B0, reinterpret_cast<void*>(&Start3D)},
        {addr::SC4UI_F7F4690, reinterpret_cast<void*>(&OnCommand)},
        {addr::SC4UI_F7F53C0, reinterpret_cast<void*>(&OnMessage)},
        {addr::SC4UI_F7F58E0, reinterpret_cast<void*>(&OnNotify)},
        {addr::SC4UI_F7F5CF0, reinterpret_cast<void*>(&SetCursorText)},
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

const bool g_cityView7Registered = registry::Register({
    "ui.cityview.part7",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F7F4690,
    "Our city view handlers: the window's messages and keys, the buttons' commands and menus, the city's messages, "
    "the mode buttons, the cursor text and the start of the 3D side (docs/ui.md)",
    false,
    InstallCityView7,
});

} // namespace
} // namespace opensc4::ui
