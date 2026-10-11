// The city view, part five (0x007EF200 to 0x007F1640): the object's making
// and unmaking and what hangs on them.
//
// The city view (cSC4View3DWin, 0x448 bytes) has four parts: +0 its own
// interface (vtable 0x00ABCE40, cISC4View3DWin), +4 a message target
// (0x00ABCE2C), +8 a key filter (0x00ABCE1C) and +0xC the window
// (0x00ABCBC0, the framework's window with 159 slots). Here:
//
//   - the constructor (0x007F0A70) and destructor (0x007F1140), and the
//     ten thunks that bring QueryInterface, AddRef, Release and the
//     deleting destructor from the other three parts to the object;
//   - the window's Shutdown (0x007F1320, slot 5 of the window part) and
//     the routine it calls to take the panels and child windows off and
//     let them go (0x007EF200);
//   - small interface methods: slot 5 (the object kept at +0xF8), slot 41
//     ClearCursorText, slot 43 the cost indicator with one number, slot 49
//     the coverage helper, slots 51 to 54 the last selected building and
//     lot;
//   - the render properties (0x007EF820): four numbers from an exemplar,
//     then every entry of the section "Render Properties" of the game's
//     ini file and every "RenderProp" option of the command line, each
//     handed to 0x007ECDA0 as a name and a value;
//   - the menu of buildings to place (0x007F06B0, with 0x007EFF40 making
//     one item for every building type whose occupant groups are wanted);
//   - the animation recorder shown and put away (0x007F0840);
//   - the handler of four buttons of the view's own tool bar (0x007EF610);
//   - the copy constructor of a cursor text record (0x007EFE60);
//   - the message handler of the view's hover tip helper (0x007EFBD0, slot
//     3 of the six-slot class 0x00ABCB60).
//
// The view's fields, as far as this file names them (offsets from the
// object; the window part's own code reaches them 0xC lower):
//   +0x10 the window manager (the window base's field), +0xE4 a byte: the
//   window is initialised, +0xE8 and +0xF4 two objects shut down with
//   their slot 4, +0xEC an object of 0x00648F00's class, +0xF0 the camera
//   control, +0xF8 what slot 5 gives, +0x100 and +0x160 stopwatches,
//   +0x120 a reference, +0x128 the set of message IDs the view listens to,
//   +0x134 and +0x140 two buffers, +0x14C a list, +0x150 and +0x154
//   references, +0x15C a byte, +0x184 and +0x188 two objects ended with
//   0x0047A780, +0x18C and +0x194 two child windows, +0x190 a reference,
//   +0x198 a vector of child windows, +0x1A4 to +0x1DC five vectors,
//   +0x1E0 to +0x1F0 five references, +0x1F4 a number (-1 at first),
//   +0x1F8 the tool bar's window, +0x2D8, +0x2EC and +0x2F0 references,
//   +0x2DC the news ticker, +0x2E4 the MySim panel, +0x2E8 the budget
//   panel, +0x2F8 the graphs window, +0x2FC the style panel, +0x300 the
//   map window, +0x304, +0x308 and +0x30C three more windows, +0x310 to
//   +0x318 three panels (their window by slot 3), +0x31C the advice popup,
//   +0x320 a reference, +0x334 to +0x344 the render properties (float,
//   byte, float, float), +0x380 an object of 0x30 bytes (0x007E3650),
//   +0x3D0 and +0x3D4 references, +0x3D8 the cursor's info tip, +0x3DC the
//   list of cursor texts, +0x3E0 a reference, +0x3E4 the coverage helper,
//   +0x404 the loading picture, +0x40C a reference, +0x410 an object told
//   0 by its slot 4 at shutdown, +0x414 a string, +0x428 a set of numbers,
//   +0x43C and +0x444 the last selected building and whether one was set,
//   +0x440 and +0x445 the same for the lot, +0x446 a byte: every building
//   of the menu can be placed.
//
// A cursor text record (0x34 bytes): +0 the cursor ID, +4 the priority, +8
// the title, +0x1C the text (strings), +0x30 the icon. The list's nodes
// hold one after the two links (0x3C bytes).
//
// The hover tip helper (0x40 bytes, made by 0x007E9300): +4 a count, +8
// and +9 bytes (+9: a tip is wanted), +0xC the info tip, +0x10 and +0x14
// the frames for a tip with and without a title, +0x18 a timer.
//
// Nothing here draws. Left as the game's: the four container helpers
// 0x007EFEE0, 0x007EFF10, 0x007F09B0 and 0x007F0A10 (copies of vectors of
// 0x20 and 0x28 byte records, shared with the class at 0x007DE000 and with
// the camera view).
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

namespace view {
constexpr std::size_t kTarget = 0x4;
constexpr std::size_t kKeys = 0x8;
constexpr std::size_t kWindow = 0xC;
constexpr std::size_t kManager = 0x10;
constexpr std::size_t kInitialised = 0xE4;
constexpr std::size_t kObjectE8 = 0xE8;
constexpr std::size_t kObjectEC = 0xEC;
constexpr std::size_t kCameraControl = 0xF0;
constexpr std::size_t kObjectF4 = 0xF4;
constexpr std::size_t kObjectF8 = 0xF8;
constexpr std::size_t kWatch100 = 0x100;
constexpr std::size_t kHeld120 = 0x120;
constexpr std::size_t kMessages = 0x128;
constexpr std::size_t kBuffer134 = 0x134;
constexpr std::size_t kBuffer140 = 0x140;
constexpr std::size_t kList14C = 0x14C;
constexpr std::size_t kHeld150 = 0x150;
constexpr std::size_t kHeld154 = 0x154;
constexpr std::size_t kByte15C = 0x15C;
constexpr std::size_t kWatch160 = 0x160;
constexpr std::size_t kObject184 = 0x184;
constexpr std::size_t kObject188 = 0x188;
constexpr std::size_t kWindow18C = 0x18C;
constexpr std::size_t kHeld190 = 0x190;
constexpr std::size_t kWindow194 = 0x194;
constexpr std::size_t kWindows = 0x198;
constexpr std::size_t kWindowsEnd = 0x19C;
constexpr std::size_t kVectors = 0x1A4;
constexpr std::size_t kHeld1E0 = 0x1E0;
constexpr std::size_t kToolBar = 0x1F8;
constexpr std::size_t kHeld2D8 = 0x2D8;
constexpr std::size_t kTicker = 0x2DC;
constexpr std::size_t kMySimPanel = 0x2E4;
constexpr std::size_t kBudgetPanel = 0x2E8;
constexpr std::size_t kObject2EC = 0x2EC;
constexpr std::size_t kHeld2F0 = 0x2F0;
constexpr std::size_t kGraphs = 0x2F8;
constexpr std::size_t kStylePanel = 0x2FC;
constexpr std::size_t kMapWindow = 0x300;
constexpr std::size_t kWindow304 = 0x304;
constexpr std::size_t kWindow308 = 0x308;
constexpr std::size_t kWindow30C = 0x30C;
constexpr std::size_t kPanel310 = 0x310;
constexpr std::size_t kPanel314 = 0x314;
constexpr std::size_t kPanel318 = 0x318;
constexpr std::size_t kAdvicePopup = 0x31C;
constexpr std::size_t kHeld320 = 0x320;
constexpr std::size_t kRenderFloat334 = 0x334;
constexpr std::size_t kRenderByte338 = 0x338;
constexpr std::size_t kRenderFloat340 = 0x340;
constexpr std::size_t kRenderFloat344 = 0x344;
constexpr std::size_t kObject380 = 0x380;
constexpr std::size_t kHeld3D0 = 0x3D0;
constexpr std::size_t kHeld3D4 = 0x3D4;
constexpr std::size_t kCursorTip = 0x3D8;
constexpr std::size_t kCursorTexts = 0x3DC;
constexpr std::size_t kHeld3E0 = 0x3E0;
constexpr std::size_t kCoverage = 0x3E4;
constexpr std::size_t kLoadingPicture = 0x404;
constexpr std::size_t kHeld40C = 0x40C;
constexpr std::size_t kObject410 = 0x410;
constexpr std::size_t kString414 = 0x414;
constexpr std::size_t kSet428 = 0x428;
constexpr std::size_t kLastBuilding = 0x43C;
constexpr std::size_t kLastLot = 0x440;
constexpr std::size_t kHasBuilding = 0x444;
constexpr std::size_t kHasLot = 0x445;
constexpr std::size_t kPlaceAll = 0x446;
} // namespace view

namespace hover { // the hover tip helper
constexpr std::size_t kWanted = 0x9;
constexpr std::size_t kTip = 0xC;
constexpr std::size_t kTitledFrame = 0x10;
constexpr std::size_t kPlainFrame = 0x14;
constexpr std::size_t kTimer = 0x18;
} // namespace hover

namespace item { // a menu item (ui_panels_6.cpp)
constexpr std::size_t kName = 0x04;
constexpr std::size_t kDescription = 0x18;
constexpr std::size_t kState = 0x2C;
constexpr std::size_t kCommand = 0x34;
constexpr std::size_t kId = 0x38;
} // namespace item

constexpr uint32_t kExemplarIid = 0xA52160F5;
constexpr uint32_t kExemplarType = 0x6534284A;
constexpr uint32_t kVariantIid = 0x60FD4D2B;
constexpr uint32_t kPngType = 0x856DDBAC;
constexpr uint32_t kRecorderId = 0xA802B4EB;

// The messages of the tool bar's buttons (0x007EF610).
constexpr uint32_t kVehiclesButton = 0xEBFAC717;
constexpr uint32_t kAircraftButton = 0xEBFAC718;
constexpr uint32_t kWatercraftButton = 0xEBFAC719;
constexpr uint32_t kSituationsButton = 0xEBFAC720;
constexpr uint32_t kMenuButton = 0xABB27A7A;
constexpr uint32_t kAdvisorButton = 0xEBF9E342;

// The hover tip helper's messages.
constexpr uint32_t kHoverShow = 0x22C010D0;
constexpr uint32_t kHoverShowToo = 0x22C010D1;
constexpr uint32_t kHoverHide = 0x22C010D2;
constexpr uint32_t kHoverHideToo = 0x22C010D8;
constexpr uint32_t kHoverTick = 0xCA56DD76;

// The properties of a building's exemplar the menu reads.
constexpr uint32_t kOccupantGroups = 0xAA1DD396;
constexpr uint32_t kLotProperty = 0xEA260589;
constexpr uint32_t kItemNameKey = 0x8A416A99;
constexpr uint32_t kItemName = 0x899AFBAD;
constexpr uint32_t kExemplarName = 0x20;
constexpr uint32_t kItemDescriptionKey = 0xCA416AB5;
constexpr uint32_t kItemDescription = 0x8A2602A9;
constexpr uint32_t kItemIcon = 0x8A2602B8;
constexpr uint32_t kItemOrder = 0x8A2602B9;
constexpr uint32_t kItemState = 0x49CAC341;
constexpr uint32_t kConditional = 0xEA3209F8;
constexpr uint32_t kRewardProperty = 0xEA2E078B;
constexpr uint32_t kCountedProperty = 0x0BD86FD3;
constexpr uint32_t kPlaceLotCommand = 0x6A2B245F;

struct Point {
    int32_t x;
    int32_t y;
};

struct Rect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
};
static_assert(sizeof(Rect) == 0x10);

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

// A localised text on the stack: a string and what the original frame
// leaves after it.
struct LocalizedText {
    GameString text;
    uint32_t rest[3];
};
static_assert(sizeof(LocalizedText) == 0x20);

// An image by resource key (0x30 bytes).
struct ImageLoader {
    std::uintptr_t vtable;
    void* image;
    char rest[0x28];
};
static_assert(sizeof(ImageLoader) == 0x30);

// The game's ini file reader and its command line words on the stack.
struct IniFile {
    alignas(4) char bytes[0x3C];
};

struct Words {
    alignas(4) char bytes[0x3C];
};

// A node of the game's red-black trees; a map's node has `second`.
struct TreeNode {
    uint8_t colour;
    TreeNode* parent;
    TreeNode* left;
    TreeNode* right;
    void* value;
    void* second;
};
static_assert(sizeof(TreeNode) == 0x18);

struct Tree {
    TreeNode* head;
    uint32_t count;
};

// A vector of numbers.
struct NumberVector {
    uint32_t* begin;
    uint32_t* end;
    uint32_t* capacity;
};

// A menu's entry while the items are sorted: the order first.
struct OrderedItem {
    uint32_t order;
    char* item;
};

// A cursor text as the view keeps it.
struct CursorText {
    uint32_t id;
    uint32_t priority;
    GameString title;
    GameString text;
    uint32_t icon;
};
static_assert(sizeof(CursorText) == 0x34);

struct CursorTextNode {
    CursorTextNode* next;
    CursorTextNode* previous;
    CursorText value;
};
static_assert(sizeof(CursorTextNode) == 0x3C);

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

// The game's inlined reference holder: empty it (cleared first, then let go).
void Clear(void*& holder)
{
    if (void* const object = holder) {
        holder = nullptr;
        Release(object);
    }
}

// The other inlined emptying: let go first, then cleared.
void Drop(void*& holder)
{
    if (void* const object = holder) {
        Release(object);
        holder = nullptr;
    }
}

// The destructor's emptying: let go, the field left as it is.
void LetGo(char* self, std::size_t field)
{
    if (void* const object = At<void*>(self, field))
        Release(object);
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

// The inlined constructor from a literal.
template <std::size_t N>
void InitString(GameString& text, const char (&literal)[N], void* tag)
{
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    This<void>(addr::SC4String_FromRange, &text.body, &literal[0], &literal[N - 1], tag);
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

// The next node of a tree in order, as the game's inlined walk goes.
TreeNode* Next(TreeNode* node)
{
    if (TreeNode* down = node->right) {
        while (down->left)
            down = down->left;
        return down;
    }
    TreeNode* at = node;
    TreeNode* up = node->parent;
    while (at == up->right) {
        at = up;
        up = up->parent;
    }
    return at->right != up ? up : at;
}

// --- Letting the panels go ------------------------------------------------------------

// A child window taken off the window manager, shut down and let go.
void CloseWindow(char* self, std::size_t field)
{
    void* const window = At<void*>(self, field);
    if (!window)
        return;
    Virtual<void>(At<void*>(self, view::kManager), 0x5C, window);
    Virtual<void>(At<void*>(self, field), 0x14);
    Virtual<void>(At<void*>(self, field), 0x8);
    At<void*>(self, field) = nullptr;
}

// The same for a holder that is cleared before the object is let go.
void CloseHeldWindow(char* self, std::size_t field)
{
    void* const window = At<void*>(self, field);
    if (!window)
        return;
    Virtual<void>(At<void*>(self, view::kManager), 0x5C, window);
    Virtual<void>(At<void*>(self, field), 0x14);
    Clear(At<void*>(self, field));
}

// A panel that owns its window (its slot 3 gives it).
void ClosePanel(char* self, std::size_t field)
{
    void* const panel = At<void*>(self, field);
    if (!panel)
        return;
    void* const window = Virtual<void*>(panel, 0xC);
    Virtual<void>(At<void*>(self, view::kManager), 0x5C, window);
    Virtual<void>(Virtual<void*>(At<void*>(self, field), 0xC), 0x14);
    Virtual<void>(At<void*>(self, field), 0x8);
    At<void*>(self, field) = nullptr;
}

// A panel with a shutdown of its own at `shutdown`, then let go.
void EndPanel(char* self, std::size_t field, std::uintptr_t shutdown)
{
    void* const panel = At<void*>(self, field);
    if (!panel)
        return;
    This<void>(shutdown, panel);
    Virtual<void>(At<void*>(self, field), 0x8);
    At<void*>(self, field) = nullptr;
}

// The same for a holder that is cleared before the object is let go.
void EndHeldPanel(char* self, std::size_t field, std::uintptr_t shutdown)
{
    void* const panel = At<void*>(self, field);
    if (!panel)
        return;
    This<void>(shutdown, panel);
    Clear(At<void*>(self, field));
}

// The game's 0x007EF200 (thiscall, no arguments; `this` is the view). Takes
// every panel and child window off, shuts it down and lets it go. The
// advice popup at +0x31C is not checked for null.
void __fastcall ReleasePanels(char* self, void*)
{
    CloseWindow(self, view::kWindow18C);
    Drop(At<void*>(self, view::kHeld190));
    CloseWindow(self, view::kWindow194);

    void** const end = At<void**>(self, view::kWindowsEnd);
    for (void** at = At<void**>(self, view::kWindows); at != end; ++at) {
        void* const window = *at;
        Virtual<void>(window, 0x1D4, static_cast<void*>(self + view::kKeys));
        Virtual<void>(At<void*>(self, view::kManager), 0x5C, window);
        Virtual<void>(window, 0x14);
    }
    This<void>(addr::SC4RefVector_Erase, self + view::kWindows, At<void**>(self, view::kWindows),
               At<void**>(self, view::kWindowsEnd));

    for (std::size_t i = 0; i < 5; ++i)
        Drop(At<void*>(self, view::kHeld1E0 + 4 * i));

    EndPanel(self, view::kTicker, addr::SC4UI_F772A70);
    EndPanel(self, view::kMySimPanel, addr::SC4UI_F76ED10);
    Drop(At<void*>(self, view::kHeld2D8));
    if (void* const object = At<void*>(self, view::kObject2EC)) {
        Virtual<void>(object, 0x14);
        Virtual<void>(At<void*>(self, view::kObject2EC), 0x8);
        At<void*>(self, view::kObject2EC) = nullptr;
    }
    Drop(At<void*>(self, view::kHeld2F0));

    CloseHeldWindow(self, view::kWindow30C);
    CloseHeldWindow(self, view::kMapWindow);
    CloseHeldWindow(self, view::kWindow304);
    CloseHeldWindow(self, view::kWindow308);
    EndHeldPanel(self, view::kGraphs, addr::SC4UI_F76B750);
    EndHeldPanel(self, view::kStylePanel, addr::SC4UI_F7695F0);
    EndPanel(self, view::kBudgetPanel, addr::SC4UI_F764B70);
    ClosePanel(self, view::kPanel310);
    ClosePanel(self, view::kPanel314);
    ClosePanel(self, view::kPanel318);

    if (void* const tip = At<void*>(self, view::kCursorTip)) {
        void* const window = Virtual<void*>(tip, 0xC);
        Virtual<void>(self + view::kWindow, 0x40, window);
        Clear(At<void*>(self, view::kCursorTip));
    }
    Clear(At<void*>(self, view::kHeld3D0));
    Clear(At<void*>(self, view::kHeld3D4));
    This<void>(addr::SC4UI_F7EDE50, self + view::kCursorTexts);
    Clear(At<void*>(self, view::kHeld3E0));

    Virtual<void>(At<void*>(self, view::kAdvicePopup), 0x18);
    Virtual<void>(At<void*>(self, view::kAdvicePopup), 0x8);
    At<void*>(self, view::kAdvicePopup) = nullptr;
}

// --- The tool bar's buttons -----------------------------------------------------------

// The game's 0x007EF610 (thiscall, ret 8; `this` is the view). `from` is
// the window the message came from, `message` the message, whose +8 is the
// button. Three buttons open a menu for one of the city's automata
// managers (vehicles, aircraft, watercraft) through 0x007EDCD0; one
// switches the city situation manager's slot 29 by the button's check
// mark. The two other cases (a menu through 0x007E5C10 that also sets the
// check mark, and the advisor 0x1E asked for interface 0x0BF61276 and its
// slot 5) are not reached from the one caller, which passes the four
// buttons only.
void __fastcall ToolBarMessage(char* self, void*, void* from, char* message)
{
    const uint32_t button = At<uint32_t>(message, 0x8);
    switch (button) {
    case kVehiclesButton:
        This<void>(addr::SC4UI_F7EDCD0, self, from, button, Global<void*>(addr::SC4_Service43D44), uint32_t{0});
        return;
    case kAircraftButton:
        This<void>(addr::SC4UI_F7EDCD0, self, from, kAircraftButton, Global<void*>(addr::SC4_Service43D4C),
                   uint32_t{0});
        return;
    case kWatercraftButton:
        This<void>(addr::SC4UI_F7EDCD0, self, from, kWatercraftButton, Global<void*>(addr::SC4_Service43D50),
                   uint32_t{0});
        return;
    case kAdvisorButton: {
        void* const advisor = Virtual<void*>(Global<void*>(addr::SC4_Service43D34), 0x1C, uint32_t{0x1E});
        void* found = nullptr;
        if (!advisor)
            return;
        if (Virtual<uint8_t>(advisor, 0x0, uint32_t{0x0BF61276}, This<void**>(addr::SC4RefPtr_Reset, &found)))
            Virtual<void>(found, 0x14);
        if (found)
            Release(found);
        return;
    }
    case kMenuButton: {
        if (!from)
            return;
        This<void>(addr::SC4UI_F7E5C10, self, uint32_t{0x6BB27447}, uint32_t{0x8BB27C12}, from, kMenuButton);
        if (!At<void*>(self, view::kToolBar))
            return;
        void* const situations = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC4);
        const uint32_t on = Virtual<uint32_t>(situations, 0x78);
        Cdecl<uint8_t>(addr::SC4Win_CheckChild, At<void*>(self, view::kToolBar), kSituationsButton, on);
        return;
    }
    case kSituationsButton: {
        void* const situations = Virtual<void*>(Global<void*>(addr::SC4_City), 0xC4);
        const uint32_t on = Cdecl<uint32_t>(addr::SC4Win_IsChildChecked, At<void*>(self, view::kToolBar),
                                            kSituationsButton);
        Virtual<void>(situations, 0x74, on);
        return;
    }
    default:
        return;
    }
}

// --- Cursor texts -----------------------------------------------------------------------

// The game's 0x007EF7A0 (slot 41, ClearCursorText, ret 4). Takes the text
// of one cursor ID out of the list and has 0x007E8E40 show what is left.
// Returns whatever that leaves in EAX, as the original does.
uint32_t __fastcall ClearCursorText(char* self, void*, uint32_t id)
{
    CursorTextNode* const head = At<CursorTextNode*>(self, view::kCursorTexts);
    CursorTextNode* node = head->next;
    if (node != head) {
        while (node->value.id != id) {
            node = node->next;
            if (node == head)
                return This<uint32_t>(addr::SC4UI_F7E8E40, self);
        }
        CursorTextNode* const next = node->next;
        CursorTextNode* const previous = node->previous;
        previous->next = next;
        next->previous = previous;
        DestroyString(node->value.text);
        DestroyString(node->value.title);
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(node));
    }
    return This<uint32_t>(addr::SC4UI_F7E8E40, self);
}

// The game's 0x007EFE60 (thiscall, ret 4; `this` is the new record). The
// copy constructor of a cursor text record. The original's two null checks
// of the source's strings cannot fail and are left out.
CursorText* __fastcall CursorTextCopy(CursorText* self, void*, CursorText* from)
{
    self->id = from->id;
    self->priority = from->priority;
    This<void>(addr::SC4String_CopyBuffer, &self->title.body, &from->title.body, uint32_t{0}, uint32_t{0xFFFFFFFF},
               static_cast<void*>(&from));
    self->title.vtable = addr::SC4String_Vtable;
    self->title.zero = 0;
    This<void>(addr::SC4String_CopyBuffer, &self->text.body, &from->text.body, uint32_t{0}, uint32_t{0xFFFFFFFF},
               static_cast<void*>(&from));
    self->text.vtable = addr::SC4String_Vtable;
    self->text.zero = 0;
    self->icon = from->icon;
    return self;
}

// --- Render properties ------------------------------------------------------------------

// The game's 0x007EF820 (thiscall, no arguments; `this` is the view). Reads
// four numbers from the exemplar 0x690F693F of group 0xE90F6C84 into +0x334
// to +0x344, then hands 0x007ECDA0 every entry of the section "Render
// Properties" of the game's ini file (through the callback 0x007EDE30) and
// every "RenderProp" option of the command line, whose value is a name and
// a value parted by a comma or else a space; each option's word is taken
// out of the list once it is read. The exemplar's holder is not set to
// null before the resource manager is asked, as in the original.
void __fastcall LoadRenderProperties(char* self, void*)
{
    uint32_t key[3] = {kExemplarType, 0x690F693F, 0xE90F6C84};
    void* exemplar;
    if (Virtual<uint8_t>(Global<void*>(addr::SC4_ResourceManager), 0xC, &key[0], kExemplarIid, &exemplar, uint32_t{0},
                         uint32_t{0})) {
        void* const holder = Virtual<void*>(exemplar, 0x1C);
        Cdecl<uint8_t>(addr::SC4_GetPropertyFloat, holder, uint32_t{0x4975DD20},
                       static_cast<void*>(self + view::kRenderFloat334));
        Cdecl<uint8_t>(addr::SC4_GetPropertyBool, holder, uint32_t{0x4975DD25},
                       static_cast<void*>(self + view::kRenderByte338));
        Cdecl<uint8_t>(addr::SC4_GetPropertyFloat, holder, uint32_t{0x49E5522F},
                       static_cast<void*>(self + view::kRenderFloat340));
        Cdecl<uint8_t>(addr::SC4_GetPropertyFloat, holder, uint32_t{0xA9E581FB},
                       static_cast<void*>(self + view::kRenderFloat344));
        Release(exemplar);
    }

    uint8_t tag;
    GameString path;
    InitString(path);
    Virtual<void>(Global<void*>(addr::SC4_Service43C94), 0x74, &path);
    IniFile ini;
    This<void*>(addr::cRZIniFile_Construct, &ini, &path);
    GameString section;
    InitString(section, "Render Properties", &tag);
    This<void>(addr::cRZIniFile_ForEachEntry, &ini, &section, std::uintptr_t{addr::SC4UI_F7EDE30},
               static_cast<void*>(self));
    DestroyString(section);

    Words words;
    This<void*>(addr::SC4Tokenizer_FromCommandLine, &words, Virtual<void*>(Cdecl<void*>(addr::RZGetFramework), 0x54));
    GameString name;
    InitString(name, "RenderProp", &tag);
    GameString value;
    InitString(value);
    GameString parts[2];
    InitString(parts[0]);
    InitString(parts[1]);

    while (This<uint8_t>(addr::SC4Tokenizer_GetOption, &words, &name, &value, uint32_t{1})) {
        int32_t count = 2;
        bool two = Cdecl<uint8_t>(addr::SC4_SplitText, value.body.begin,
                                  static_cast<uint32_t>(value.body.end - value.body.begin), uint32_t{','}, &parts[0],
                                  &count) &&
                   count == 2;
        if (!two) {
            count = 2;
            two = Cdecl<uint8_t>(addr::SC4_SplitText, value.body.begin,
                                 static_cast<uint32_t>(value.body.end - value.body.begin), uint32_t{' '}, &parts[0],
                                 &count) &&
                  count == 2;
        }
        if (two)
            This<void>(addr::SC4UI_F7ECDA0, self, &parts[0], &parts[1]);

        const int32_t total = This<int32_t>(addr::SC4Tokenizer_Count, &words);
        for (int32_t index = 0; index < total; ++index) {
            void* const word = Virtual<void*>(&words, 0x40, index);
            if (Virtual<int32_t>(word, 0x60, name.body.begin, uint32_t{0}, uint32_t{1}) == 1) {
                This<void>(addr::SC4Tokenizer_RemoveWord, &words, index);
                break;
            }
        }
    }

    DestroyString(parts[1]);
    DestroyString(parts[0]);
    DestroyString(value);
    DestroyString(name);
    This<void>(addr::SC4Tokenizer_Destroy, &words);
    This<void>(addr::cRZIniFile_Destruct, &ini);
    DestroyString(path);
}

// --- The hover tip helper ---------------------------------------------------------------

// The game's 0x007EFBD0 (slot 3 of the helper's vtable 0x00ABCB60, ret 4).
// Takes three kinds of message. To show (0x22C010D0, 0x22C010D1): the text
// of the message's subject is parted at the first '|' into a title and the
// rest; with expanded tips on (the options' byte +0xF0A) and both parts
// there, the tip gets both and the titled frame, else the title alone (the
// rest when there is no title) and the plain frame. A title with "ESRB" in
// it is shown at once (0x007E6060 posts the tick), any other starts the
// timer. To hide (0x22C010D2, 0x22C010D8): the timer is stopped and the
// tip's window hidden. The tick (0xCA56DD76), while a tip is wanted: the
// tip is put at the cursor and its window shown. Returns 1 for a message
// it took.
uint8_t __fastcall HoverMessage(char* self, void*, uint32_t* message)
{
    const uint32_t type = message[0];

    if (type == kHoverTick) {
        if (At<uint8_t>(self, hover::kWanted)) {
            int32_t x;
            int32_t y;
            Virtual<void>(Cdecl<void*>(addr::SC4_GetWinManager), 0x84, &x, &y);
            Point at = {x, y};
            Virtual<void>(At<void*>(self, hover::kTip), 0x3C, &at);
            Virtual<void>(At<char*>(self, hover::kTip) + 4, 0x5C);
            if (!Virtual<uint8_t>(At<char*>(self, hover::kTip) + 4, 0x11C))
                Virtual<void>(At<char*>(self, hover::kTip) + 4, 0x114);
        }
        return 1;
    }

    if (type == kHoverHide || type == kHoverHideToo) {
        Virtual<void>(self + hover::kTimer, 0x1C);
        Virtual<void>(At<char*>(self, hover::kTip) + 4, 0x118);
        Virtual<void>(At<char*>(self, hover::kTip) + 4, 0x170);
        At<uint8_t>(self, hover::kWanted) = 0;
        return 1;
    }

    if (type != kHoverShow && type != kHoverShowToo)
        return 0;

    void* const subject = At<void*>(reinterpret_cast<char*>(static_cast<std::uintptr_t>(message[1])), 0x8);
    if (!subject)
        return 1;
    GameString rest;
    This<void*>(addr::SC4String_FromIGZString, &rest, subject);
    GameString cut;
    GameString title;
    void* const head = This<void*>(addr::SC4String_SplitOff, &rest, &cut, "|");
    This<void*>(addr::SC4String_Substring, &title, head, uint32_t{0}, uint32_t{0xFFFFFFFF});
    This<void>(addr::SC4String_Destroy, &cut);

    if (At<uint8_t>(Global<void*>(addr::SC4_Options43CD8), 0xF0A) && rest.body.begin != rest.body.end &&
        title.body.begin != title.body.end) {
        Virtual<void>(At<void*>(self, hover::kTip), 0x18, &title);
        Virtual<void>(At<void*>(self, hover::kTip), 0x20, &rest);
        Virtual<void>(At<void*>(self, hover::kTip), 0x10, At<void*>(self, hover::kTitledFrame), uint32_t{8},
                      uint32_t{8});
        Rect clear = {-0x1C, -0x1C, 0x1C, 0x1C};
        Virtual<void>(At<void*>(self, hover::kTip), 0x44, &clear);
    } else {
        void* const tip = At<void*>(self, hover::kTip);
        GameString none;
        Virtual<void>(tip, 0x18, This<void*>(addr::SC4String_SC4String, &none));
        This<void>(addr::SC4String_Destroy, &none);
        GameString* const shown = title.body.begin == title.body.end ? &rest : &title;
        Virtual<void>(At<void*>(self, hover::kTip), 0x20, shown);
        Virtual<void>(At<void*>(self, hover::kTip), 0x10, At<void*>(self, hover::kPlainFrame), uint32_t{5},
                      uint32_t{3});
        Rect clear = {-4, -4, 0x14, 0x14};
        Virtual<void>(At<void*>(self, hover::kTip), 0x44, &clear);
    }

    if (This<int32_t>(addr::SC4String_Find, &title, "ESRB", uint32_t{0}, uint32_t{1}) != -1) {
        At<uint8_t>(self, hover::kWanted) = 1;
        Cdecl<void>(addr::SC4UI_F7E6060, uint32_t{0});
    } else {
        Virtual<void>(self + hover::kTimer, 0x1C);
        Virtual<void>(self + hover::kTimer, 0x14, uint32_t{0}, uint32_t{0});
        At<uint8_t>(self, hover::kWanted) = 1;
    }
    This<void>(addr::SC4String_Destroy, &title);
    This<void>(addr::SC4String_Destroy, &rest);
    return 1;
}

// --- The menu of buildings --------------------------------------------------------------

// The item of one building, once its lot is known: the second half of the
// loop body of 0x007EFF40. Name and description as a menu item's (the text
// of a key, else a string property, else the exemplar's name; none: no
// item). A building with a reward property the civic simulator (slot 5)
// says is not available gets the localised text 0x0A765619 added to its
// description and is greyed. A conditional building (property 0xEA3209F8)
// that is still enabled asks the civic simulator (slot 6, by the
// exemplar's instance) for its state: bit 2 keeps it enabled, else the
// state's text is added and it is greyed; bit 1 leaves it out, unless the
// view's byte +0x446 says every building can be placed. An enabled
// building with property 0x0BD86FD3 is greyed when the traffic simulator's
// slot 32 gives 16 or more for it. The item goes into `items` under its
// order (property 0x8A2602B9) shifted up 16 bits plus its property
// 0x49CAC341, with the reference taken here.
void AddBuildingItem(char* self, void* civic, void* detokenizer, void* exemplar, void* holder, uint32_t lot,
                     GameString& name, GameString& description, Tree* items)
{
    Virtual<void>(detokenizer, 0x1C, std::uintptr_t{addr::SC4UI_F78EAC0}, holder, uint32_t{1});
    if (!Cdecl<uint8_t>(addr::SC4_GetPropertyText, &name, holder, kItemNameKey, detokenizer) &&
        !Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, kItemName, &name))
        Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, kExemplarName, &name);
    if (!Cdecl<uint8_t>(addr::SC4_GetPropertyText, &description, holder, kItemDescriptionKey, detokenizer) &&
        !Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, kItemDescription, &description))
        Cdecl<uint8_t>(addr::SC4_GetPropertyString, holder, kExemplarName, &description);
    Virtual<void>(detokenizer, 0x1C, std::uintptr_t{addr::SC4UI_F78EAC0}, holder, uint32_t{0});
    if (name.body.begin == name.body.end)
        return;
    if (description.body.begin == description.body.end)
        return;

    uint32_t own[3] = {0, 0, 0};
    uint32_t icon = 0x144161EC;
    uint32_t state = 0;
    uint8_t conditional = 0;
    uint8_t enabled = 1;
    uint8_t keep = 1;
    uint8_t scratch; // the original's one scratch byte: a tag, then the counted kind
    Virtual<void>(exemplar, 0x2C, &own[0]);
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, kItemState, &state);
    Cdecl<uint8_t>(addr::SC4_GetPropertyBool, holder, kConditional, &conditional);
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, kItemIcon, &icon);
    uint32_t key[3] = {kPngType, 0x6A386D26, icon};

    uint32_t reward;
    if (Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, Virtual<void*>(exemplar, 0x1C), kRewardProperty, &reward) &&
        Virtual<uint8_t>(civic, 0x14, reward)) {
        This<void>(addr::cSC4Advisor_F410F00, &description.body, "\n\n");
        LocalizedText localized;
        char* const made = This<char*>(addr::SC4LocalizedString_SC4LocalizedString, &localized,
                                       uint32_t{0x6A231EAA}, uint32_t{0x0A765619}, uint32_t{1});
        StringBody* const body = made ? reinterpret_cast<StringBody*>(made + 4) : nullptr;
        This<void>(addr::SC4String_AppendRange, &description.body, body->begin, body->end,
                   static_cast<void*>(&scratch));
        DestroyString(localized.text);
        enabled = 0;
    }

    bool checked = true;
    if (conditional) {
        if (!enabled) {
            checked = false;
        } else {
            enabled = 0;
            char* const condition = Virtual<char*>(civic, 0x18, own[2]);
            if (condition) {
                if (!(At<uint8_t>(condition, 0x0) & 2)) {
                    This<void>(addr::cSC4Advisor_F410F00, &description.body, "\n\n");
                    This<void>(addr::SC4String_AppendStd, &description.body, static_cast<void*>(condition + 8));
                } else {
                    enabled = 1;
                }
                if (At<uint8_t>(condition, 0x0) & 1)
                    keep = 0;
            }
            if (At<uint8_t>(self, view::kPlaceAll)) {
                enabled = 1;
                keep = 1;
            }
        }
    }
    if (checked) {
        if (enabled && Virtual<uint8_t>(holder, 0xC, kCountedProperty)) {
            Cdecl<uint8_t>(addr::SC4_GetPropertyUint8, holder, kCountedProperty, &scratch);
            if (Virtual<int32_t>(Global<void*>(addr::SC4_Service43DA8), 0x80, uint32_t{scratch}) >= 0x10)
                enabled = 0;
        }
        if (!keep)
            return;
    }

    char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x44});
    char* const made = memory ? This<char*>(addr::SC4UI_F79B6B0, memory) : nullptr;
    AddRef(made);
    ImageLoader loader;
    This<void*>(addr::SC4ImageLoader_ConstructKey, &loader, &key[0], uint32_t{1}, uint32_t{0});
    void* const image = loader.image;
    if (image) {
        const uint32_t clear =
            Virtual<uint32_t>(self + view::kWindow, 0x1B0, uint32_t{0xFF}, uint32_t{0}, uint32_t{0xFF});
        Virtual<void>(image, 0x5C, clear);
        This<void>(addr::cSC4Occupant_SetPlaceableObject, made, image);
    }
    uint32_t order = 0;
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, kItemOrder, &order);
    Virtual<void>(made + item::kName, 0x34, &name);
    Virtual<void>(made + item::kDescription, 0x34, &description);
    At<uint32_t>(made, item::kState) = state;
    At<uint32_t>(made, item::kId) = lot;
    At<uint32_t>(made, item::kCommand) = kPlaceLotCommand;
    Virtual<void>(made, 0x28, uint32_t{enabled});

    OrderedItem entry;
    entry.order = (order << 16) + state;
    entry.item = made;
    uint32_t where[4];
    This<void*>(addr::cSC4Advisor_F40F080, items, &where[0], &entry);
    This<void>(addr::SC4ImageLoader_Destruct, &loader);
}

// One building type's exemplar: the first half of the loop body. The
// building is taken when its occupant groups (an array of numbers) hold
// one of `wanted` and none of `unwanted`, and when its lot (property
// 0xEA260589) is one the lot configuration manager knows.
void AddBuilding(char* self, void* civic, void* detokenizer, void* exemplar, Tree* wanted, Tree* unwanted,
                 Tree* items)
{
    void* const holder = Virtual<void*>(exemplar, 0x1C);
    void* const property = Virtual<void*>(holder, 0x24, kOccupantGroups);
    if (!property)
        return;
    uint8_t match = 0;
    void* variant;
    if (!Virtual<uint8_t>(property, 0x0, kVariantIid, &variant))
        return;
    if (Virtual<uint32_t>(variant, 0xC) == 0x8006) {
        const int32_t count = Virtual<int32_t>(variant, 0x10);
        uint32_t* const groups = Virtual<uint32_t*>(variant, 0x19C);
        TreeNode* found[2];
        for (int32_t index = 0; index < count && !match; ++index) {
            This<void>(addr::SC4Uint32Map_Find, wanted, &found[0], &groups[index]);
            if (found[0] != wanted->head)
                match = 1;
        }
        for (int32_t index = 0; index < count && match; ++index) {
            This<void>(addr::SC4Uint32Map_Find, unwanted, &found[0], &groups[index]);
            if (found[0] != unwanted->head)
                match = 0;
        }
    }
    Release(variant);
    if (!match)
        return;

    GameString name;
    InitString(name);
    GameString description;
    InitString(description);
    uint32_t lot = 0;
    Cdecl<uint8_t>(addr::SC4_GetPropertyUint32, holder, kLotProperty, &lot);
    if (lot && Virtual<void*>(Global<void*>(addr::SC4_Service43D18), 0x20, lot))
        AddBuildingItem(self, civic, detokenizer, exemplar, holder, lot, name, description, items);
    DestroyString(description);
    DestroyString(name);
}

// The game's 0x007EFF40 (thiscall, ret 0x10; `this` is the view). Makes a
// menu item for every building type of the building development simulator
// (0x00B43D68) whose profile's byte +0x1C has none of its three low bits,
// whose exemplar the simulator knows, and which AddBuilding takes. The
// first argument is not used. A detokenizer of its own (0x94 bytes) serves
// all items; it is not checked for null, nor is the exemplar's holder set
// to null before the resource manager is asked, as in the original.
void __fastcall CollectBuildings(char* self, void*, uint32_t, Tree* wanted, Tree* unwanted, Tree* items)
{
    void* const civic = Global<void*>(addr::SC4_Service43DB4);
    void* const buildings = Global<void*>(addr::SC4_Service43D68);
    void* const resources = Global<void*>(addr::SC4_ResourceManager);
    NumberVector types = {nullptr, nullptr, nullptr};
    uint32_t total = 0;
    char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x94});
    void* const detokenizer = memory ? This<void*>(addr::SC4Detokenizer_Construct, memory) : nullptr;
    AddRef(detokenizer);
    total = Virtual<uint32_t>(buildings, 0x30, uint32_t{0}, &total);
    This<void>(addr::SC4PtrVector_Reserve, &types, total);
    Virtual<void>(buildings, 0x2C, &types);

    void* exemplar;
    uint32_t* const end = types.end;
    for (uint32_t* at = types.begin; at != end; ++at) {
        uint32_t key[3] = {0, 0, 0};
        char* const profile = Virtual<char*>(buildings, 0x9C, *at);
        if (At<uint8_t>(profile, 0x1C) & 7)
            continue;
        if (!Virtual<uint8_t>(buildings, 0x38, *at, &key[0]))
            continue;
        if (!Virtual<uint8_t>(resources, 0xC, &key[0], kExemplarIid, &exemplar, uint32_t{0}, uint32_t{0}))
            continue;
        AddBuilding(self, civic, detokenizer, exemplar, wanted, unwanted, items);
        Release(exemplar);
    }

    Release(detokenizer);
    if (types.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(types.begin));
}

// The game's 0x007F06B0 (thiscall, ret 0x14; `this` is the view). Makes a
// menu window (0x140 bytes) with a reference for the caller and fills it,
// in the items' order, with the menu exemplars of group 0x2A3858E4 that
// have the property `first` (command 0x4A2B2467) or `second` (command
// 0x4A2B2466) and with the buildings CollectBuildings takes. The menu
// holds the items from then on. The new menu is not checked for null.
char* __fastcall MakeBuildingMenu(char* self, void*, uint32_t unused, Tree* wanted, Tree* unwanted, uint32_t first,
                                  int32_t second)
{
    Tree items;
    items.head = nullptr;
    items.head = Cdecl<TreeNode*>(addr::SC4_stl_allocate, uint32_t{0x18});
    items.count = 0;
    items.head->colour = 0;
    items.head->parent = nullptr;
    items.head->left = items.head;
    items.head->right = items.head;

    char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x140});
    char* const menu = memory ? This<char*>(addr::SC4UI_F79B500, memory) : nullptr;
    AddRef(menu);
    if (first)
        Cdecl<void>(addr::SC4UI_F78EF30, &items, static_cast<void*>(menu), uint32_t{0x2A3858E4},
                    uint32_t{0x4A2B2467}, uint32_t{0}, first);
    if (second > 0)
        Cdecl<void>(addr::SC4UI_F78EF30, &items, static_cast<void*>(menu), uint32_t{0x2A3858E4},
                    uint32_t{0x4A2B2466}, uint32_t{0}, second);
    CollectBuildings(self, nullptr, unused, wanted, unwanted, &items);

    for (TreeNode* node = items.head->left; node != items.head; node = Next(node)) {
        Virtual<void>(menu, 0x10, node->second);
        Release(node->second);
    }

    if (items.count) {
        for (TreeNode* node = items.head->parent; node;) {
            This<void>(addr::SC4UI_F78DD40, &items, node->right);
            TreeNode* const next = node->left;
            Cdecl<void>(addr::SC4_Free, static_cast<void*>(node));
            node = next;
        }
        items.head->left = items.head;
        items.head->parent = nullptr;
        items.head->right = items.head;
        items.count = 0;
    }
    if (items.head)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(items.head));
    return menu;
}

// --- The animation recorder -------------------------------------------------------------

// The game's 0x007F0840 (thiscall, ret 4; `this` is the view). Shows the
// animation recorder (a child of the view's window with the ID 0xA802B4EB)
// or takes it off. A new one (0x218 bytes) gets the user's folder plus
// "RecordedAnimations\" as its output folder. Returns 1. The new object is
// not checked for null.
uint8_t __fastcall ShowRecorder(char* self, void*, uint8_t show)
{
    char* const window = self + view::kWindow;
    void* const child = Virtual<void*>(window, 0x88, kRecorderId);
    if (!show) {
        if (child)
            Virtual<void>(window, 0x3C, child);
        return 1;
    }
    if (child)
        return 1;

    char* const memory = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0x218});
    char* const recorder = memory ? This<char*>(addr::SC4UI_F796380, memory) : nullptr;
    AddRef(recorder);
    GameString folder;
    InitString(folder);
    Cdecl<void>(addr::SC4_GetUserDataDirectory, &folder);
    static const char kFolder[] = "RecordedAnimations";
    uint8_t tag;
    This<void*>(addr::cSC4Advisor_F40EBC0, &folder.body, &kFolder[0], &kFolder[sizeof(kFolder) - 1],
                static_cast<void*>(&tag));
    if (folder.body.end + 1 == folder.body.capacity) {
        const uint32_t size = static_cast<uint32_t>(folder.body.end - folder.body.begin);
        const uint32_t more = size < 1 ? 1 : size;
        This<void>(addr::cSC4Advisor_F40F1B0, &folder.body, more + size);
    }
    folder.body.end[1] = 0;
    *folder.body.end = '\\';
    ++folder.body.end;
    This<void>(addr::SC4UI_F795100, recorder, &folder, uint32_t{1});
    Virtual<void>(recorder, 0x100, kRecorderId);
    Virtual<void>(window, 0x38, static_cast<void*>(recorder));
    Release(recorder);
    DestroyString(folder);
    return 1;
}

// --- The object ---------------------------------------------------------------------------

// Clears the dwords from `from` up to and without `to`.
void Zero(char* self, std::size_t from, std::size_t to)
{
    for (std::size_t at = from; at < to; at += 4)
        At<uint32_t>(self, at) = 0;
}

// The game's 0x007F0A70 (thiscall, no arguments, returns this). The
// constructor. It leaves some fields as they come, among them the news
// ticker +0x2DC and the budget panel +0x2E8, which ReleasePanels tests.
// At the end it registers the 0x84 message IDs of the table 0x00ABC070 in
// the set at +0x128 and makes the object the global view (0x00B43DC4).
char* __fastcall Construct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::cSC4View3DWin_BaseVtable;
    At<std::uintptr_t>(self, view::kTarget) = addr::cSC4TrafficSimulator_VtableBase4;
    At<std::uintptr_t>(self, view::kKeys) = addr::SC4KeyFilter_BaseVtable;
    This<void*>(addr::GZWin_Construct, self + view::kWindow);
    At<std::uintptr_t>(self, 0x0) = addr::cSC4View3DWin_Vtable;
    At<std::uintptr_t>(self, view::kTarget) = addr::cSC4View3DWin_TargetVtable;
    At<std::uintptr_t>(self, view::kKeys) = addr::cSC4View3DWin_KeysVtable;
    At<std::uintptr_t>(self, view::kWindow) = addr::cSC4View3DWin_WindowVtable;
    At<uint8_t>(self, view::kInitialised) = 0;
    At<uint8_t>(self, 0xE5) = 0;
    Zero(self, 0xE8, 0x100);
    This<void*>(addr::SC4Stopwatch_Construct, self + view::kWatch100, uint32_t{3});

    At<uint8_t>(self, 0x118) = 0;
    At<uint32_t>(self, 0x11C) = 0;
    At<uint32_t>(self, view::kHeld120) = 0;
    At<uint8_t>(self, 0x124) = 0;
    At<uint8_t>(self, 0x125) = 1;
    At<uint8_t>(self, 0x126) = 0;
    This<void*>(addr::SC4MessageIds_Construct, self + view::kMessages);

    Zero(self, view::kBuffer134, 0x150);
    void** const list = Cdecl<void**>(addr::SC4_stl_allocate, uint32_t{0xC});
    list[0] = list;
    list[1] = list;
    At<void**>(self, view::kList14C) = list;
    At<uint32_t>(self, view::kHeld150) = 0;
    At<uint32_t>(self, view::kHeld154) = 0;
    At<uint32_t>(self, 0x158) = 0;
    At<uint8_t>(self, view::kByte15C) = 0;
    This<void*>(addr::SC4Stopwatch_Construct, self + view::kWatch160, uint32_t{4});

    At<uint8_t>(self, 0x178) = 0;
    At<uint32_t>(self, 0x17C) = 0x7D0;
    Zero(self, view::kObject184, 0x1E0);
    Zero(self, view::kToolBar, 0x208);
    At<uint32_t>(self, 0x20C) = 0;
    At<uint32_t>(self, 0x210) = 1;
    At<uint32_t>(self, 0x214) = 0;
    At<uint32_t>(self, 0x218) = 4;
    At<uint32_t>(self, 0x21C) = 0;
    At<uint32_t>(self, 0x220) = 7;
    Zero(self, 0x224, 0x278);
    At<uint32_t>(self, 0x278) = 0x8C329937;
    At<uint32_t>(self, 0x27C) = 0;
    At<uint32_t>(self, 0x280) = 0x31;
    At<uint32_t>(self, 0x284) = 0;
    At<uint32_t>(self, 0x288) = 0x29;
    At<uint32_t>(self, 0x28C) = 0;
    At<uint32_t>(self, 0x290) = 0x299237BF;
    Zero(self, 0x294, 0x2A8);
    At<uint32_t>(self, 0x2A8) = 0x6A33E1BA;
    At<uint32_t>(self, 0x2AC) = 0;
    At<uint32_t>(self, 0x2B0) = 0x2A33E1A3;
    At<uint32_t>(self, 0x2B4) = 0;
    At<uint32_t>(self, 0x2B8) = 0xEA353064;
    At<uint32_t>(self, 0x2BC) = 0;
    At<uint32_t>(self, 0x2C0) = 0x2A312F97;
    At<uint32_t>(self, 0x2C4) = 0;
    At<uint32_t>(self, 0x2C8) = 0x0A5242DF;
    At<uint32_t>(self, 0x2CC) = 0;
    At<uint32_t>(self, 0x2D0) = 0;
    At<uint8_t>(self, 0x2D4) = 0;
    At<uint8_t>(self, 0x2D5) = 0;
    At<uint8_t>(self, 0x2D6) = 0;
    At<uint32_t>(self, view::kMySimPanel) = 0;
    Zero(self, view::kGraphs, 0x32C);
    At<uint8_t>(self, 0x330) = 0;
    At<float>(self, view::kRenderFloat334) = 1.0f;
    At<uint8_t>(self, view::kRenderByte338) = 0;
    At<uint8_t>(self, 0x339) = 0;
    At<uint32_t>(self, view::kRenderFloat340) = 0x3E4CCCCD;
    At<uint32_t>(self, view::kRenderFloat344) = 0x3E4CCCCD;
    Zero(self, 0x348, 0x360);
    At<uint8_t>(self, 0x360) = 0;
    At<uint32_t>(self, 0x364) = 0;
    At<uint32_t>(self, 0x368) = 5;
    At<uint32_t>(self, 0x36C) = 0;
    At<uint32_t>(self, 0x370) = 0x15200003;
    At<uint32_t>(self, 0x374) = 0;
    At<uint32_t>(self, 0x378) = 0;
    At<uint32_t>(self, 0x37C) = 0;
    This<void*>(addr::SC4_F7E3650, self + view::kObject380);

    At<uint32_t>(self, 0x3B0) = 0;
    At<uint32_t>(self, 0x3B4) = 0;
    At<uint32_t>(self, 0x3BC) = 0;
    At<uint32_t>(self, view::kHeld3D0) = 0;
    At<uint32_t>(self, view::kHeld3D4) = 0;
    At<uint32_t>(self, view::kCursorTip) = 0;
    At<uint32_t>(self, view::kCursorTexts) = 0;
    void** const texts = Cdecl<void**>(addr::SC4_stl_allocate, uint32_t{0x3C});
    texts[0] = texts;
    texts[1] = texts;
    At<void**>(self, view::kCursorTexts) = texts;
    At<uint32_t>(self, view::kHeld3E0) = 0;
    This<void*>(addr::SC4CoverageHelper_Construct, self + view::kCoverage);

    At<uint8_t>(self, 0x3FC) = 0;
    At<uint32_t>(self, 0x400) = 0xFFFFFFFF;
    At<uint32_t>(self, view::kLoadingPicture) = 0;
    At<uint8_t>(self, 0x408) = 1;
    At<uint32_t>(self, view::kHeld40C) = 0;
    At<uint32_t>(self, view::kObject410) = 0;
    InitString(At<GameString>(self, view::kString414));
    Zero(self, view::kSet428 + 4, view::kSet428 + 0x14);
    This<void*>(addr::SC4Uint32HashSet_Construct, self + view::kSet428, uint32_t{0x64});

    At<uint32_t>(self, view::kLastBuilding) = 0;
    At<uint32_t>(self, view::kLastLot) = 0;
    At<uint8_t>(self, view::kHasBuilding) = 0;
    At<uint8_t>(self, view::kHasLot) = 0;
    At<uint8_t>(self, view::kPlaceAll) = 0;
    Zero(self, view::kHeld1E0, 0x1F4);
    At<uint32_t>(self, 0x1F4) = 0xFFFFFFFF;
    At<uint32_t>(self, view::kHeld2D8) = 0;
    At<uint32_t>(self, view::kObject2EC) = 0;
    At<uint32_t>(self, view::kHeld2F0) = 0;
    At<uint32_t>(self, view::kHeld320) = 0;
    At<uint32_t>(self, 0x324) = 1;
    This<uint8_t>(addr::SC4MessageIds_Add, self + view::kMessages, std::uintptr_t{addr::cSC4View3DWin_MessageIds},
                  uint32_t{0x84});
    Cdecl<uint8_t>(addr::SC4_SetView3D, static_cast<void*>(self));
    return self;
}

// The game's 0x007F0FE0 (slot 5): the object kept at +0xF8.
void* __fastcall GetObjectF8(char* self, void*)
{
    return At<void*>(self, view::kObjectF8);
}

// The game's 0x007F0FF0 (slot 43, ret 4): the cost indicator with one
// number is slot 42 with the number twice. Returns what that returns.
uint32_t __fastcall SetCostIndicator(char* self, void*, uint32_t cost)
{
    return Virtual<uint32_t>(self, 0xA8, cost, cost);
}

// The game's 0x007F1010 (slot 49): the coverage helper inside the view.
void* __fastcall GetCoverageHelper(char* self, void*)
{
    return self + view::kCoverage;
}

// The game's 0x007F1020 (slot 53, ret 4). EAX is left holding the argument.
uint32_t __fastcall SetLastSelectedBuilding(char* self, void*, uint32_t building)
{
    At<uint32_t>(self, view::kLastBuilding) = building;
    At<uint8_t>(self, view::kHasBuilding) = 1;
    return building;
}

// The game's 0x007F1040 (slot 54, ret 4).
uint32_t __fastcall SetLastSelectedLot(char* self, void*, uint32_t lot)
{
    At<uint32_t>(self, view::kLastLot) = lot;
    At<uint8_t>(self, view::kHasLot) = 1;
    return lot;
}

// The game's 0x007F1060 (slot 51): 0 until one was set.
uint32_t __fastcall GetLastSelectedBuilding(char* self, void*)
{
    return At<uint8_t>(self, view::kHasBuilding) ? At<uint32_t>(self, view::kLastBuilding) : 0;
}

// The game's 0x007F1080 (slot 52).
uint32_t __fastcall GetLastSelectedLot(char* self, void*)
{
    return At<uint8_t>(self, view::kHasLot) ? At<uint32_t>(self, view::kLastLot) : 0;
}

// The game's thunks 0x007F10A0 to 0x007F1130: QueryInterface, AddRef and
// Release of the parts at +4, +8 and +0xC, and the window part's deleting
// destructor (its slot 148).
uint8_t __fastcall TargetQuery(char* part, void*, uint32_t iid, void** out)
{
    return This<uint8_t>(addr::SC4UI_F7E5630, part - view::kTarget, iid, out);
}

uint32_t __fastcall TargetAddRef(char* part, void*)
{
    return This<uint32_t>(addr::SC4UI_F7E56D0, part - view::kTarget);
}

uint32_t __fastcall TargetRelease(char* part, void*)
{
    return This<uint32_t>(addr::SC4UI_F7E56E0, part - view::kTarget);
}

uint8_t __fastcall KeysQuery(char* part, void*, uint32_t iid, void** out)
{
    return This<uint8_t>(addr::SC4UI_F7E5630, part - view::kKeys, iid, out);
}

uint32_t __fastcall KeysAddRef(char* part, void*)
{
    return This<uint32_t>(addr::SC4UI_F7E56D0, part - view::kKeys);
}

uint32_t __fastcall KeysRelease(char* part, void*)
{
    return This<uint32_t>(addr::SC4UI_F7E56E0, part - view::kKeys);
}

uint8_t __fastcall WindowQuery(char* part, void*, uint32_t iid, void** out)
{
    return This<uint8_t>(addr::SC4UI_F7E5630, part - view::kWindow, iid, out);
}

uint32_t __fastcall WindowAddRef(char* part, void*)
{
    return This<uint32_t>(addr::SC4UI_F7E56D0, part - view::kWindow);
}

uint32_t __fastcall WindowRelease(char* part, void*)
{
    return This<uint32_t>(addr::SC4UI_F7E56E0, part - view::kWindow);
}

char* __fastcall WindowDelete(char* part, void*, uint32_t flags)
{
    return This<char*>(addr::SC4UI_F7F3E30, part - view::kWindow, flags);
}

// The game's 0x007F1140 (thiscall, no arguments; `this` is the view). The
// destructor: what Shutdown left is let go and freed, then the window
// base's destructor runs.
void __fastcall Destruct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::cSC4View3DWin_Vtable;
    At<std::uintptr_t>(self, view::kTarget) = addr::cSC4View3DWin_TargetVtable;
    At<std::uintptr_t>(self, view::kKeys) = addr::cSC4View3DWin_KeysVtable;
    At<std::uintptr_t>(self, view::kWindow) = addr::cSC4View3DWin_WindowVtable;
    This<void>(addr::SC4Uint32HashSet_Clear, self + view::kSet428);
    if (void* const buckets = At<void*>(self, view::kSet428 + 4))
        Cdecl<void>(addr::SC4_Free, buckets);
    DestroyString(At<GameString>(self, view::kString414));
    LetGo(self, view::kObject410);
    LetGo(self, view::kHeld40C);
    This<void>(addr::SC4CoverageHelper_Destruct, self + view::kCoverage);
    LetGo(self, view::kHeld3E0);
    This<void>(addr::SC4UI_F7EDE50, self + view::kCursorTexts);
    if (void* const head = At<void*>(self, view::kCursorTexts))
        Cdecl<void>(addr::SC4_Free, head);
    LetGo(self, view::kCursorTip);
    LetGo(self, view::kHeld3D4);
    LetGo(self, view::kHeld3D0);
    This<void>(addr::SC4_F7DE960, self + view::kObject380);
    LetGo(self, view::kWindow30C);
    LetGo(self, view::kWindow308);
    LetGo(self, view::kWindow304);
    LetGo(self, view::kMapWindow);
    LetGo(self, view::kStylePanel);
    LetGo(self, view::kGraphs);
    for (std::size_t i = 5; i-- > 0;) {
        if (void* const buffer = At<void*>(self, view::kVectors + 0xC * i))
            Cdecl<void>(addr::SC4_Free, buffer);
    }
    This<void>(addr::SC4RefVector_Destroy, self + view::kWindows);
    This<void>(addr::SC4Stopwatch_Destruct, self + view::kWatch160);
    This<void>(addr::SC4RefList_Clear, self + view::kList14C);
    if (void* const head = At<void*>(self, view::kList14C))
        Cdecl<void>(addr::SC4_Free, head);
    if (void* const buffer = At<void*>(self, view::kBuffer140))
        Cdecl<void>(addr::SC4_Free, buffer);
    if (void* const buffer = At<void*>(self, view::kBuffer134))
        Cdecl<void>(addr::SC4_Free, buffer);
    This<void>(addr::SC4MessageIds_Destruct, self + view::kMessages);
    This<void>(addr::SC4Stopwatch_Destruct, self + view::kWatch100);
    This<void>(addr::GZWin_Destruct, self + view::kWindow);
}

// An object with a shutdown of its own at `shutdown`, then let go.
void EndObject(char* self, std::size_t field, std::uintptr_t shutdown)
{
    void* const object = At<void*>(self, field);
    if (!object)
        return;
    This<void>(shutdown, object);
    Virtual<void>(At<void*>(self, field), 0x8);
    At<void*>(self, field) = nullptr;
}

// An object shut down with its slot 4, then let go.
void StopObject(char* self, std::size_t field)
{
    void* const object = At<void*>(self, field);
    if (!object)
        return;
    Virtual<void>(object, 0x10);
    Virtual<void>(At<void*>(self, field), 0x8);
    At<void*>(self, field) = nullptr;
}

// The game's 0x007F1320 (slot 5 of the window part, no arguments; `this`
// is the window at +0xC). Shutdown: the view's thirteen cheat codes are
// unregistered, every tool is taken off (slots 27 and 30), the animation
// recorder is put away, the panels go (ReleasePanels), the 0x68 commands
// of the table 0x00B0DD98 are taken from the service 0xEB903A32, the
// messages of the set +0x128 are unsubscribed, the globals of the view are
// cleared and the renderer's objects are shut down and let go; last the
// window base's own shutdown, whose answer is returned. Returns 1 at once
// when the window was not initialised. The command service is not checked
// for null, as in the original.
uint8_t __fastcall Shutdown(char* window, void*)
{
    char* const self = window - view::kWindow;
    if (!At<uint8_t>(self, view::kInitialised))
        return 1;
    At<uint8_t>(self, view::kByte15C) = 0;
    if (void* const object = At<void*>(self, view::kObject410))
        Virtual<void>(object, 0x10, uint32_t{0});
    Clear(At<void*>(self, view::kObject410));

    if (void* const cheats = Global<void*>(addr::SC4_CheatCodes)) {
        Virtual<void>(cheats, 0x18, static_cast<void*>(self ? window : nullptr), uint32_t{0});
        static const uint32_t kCodes[] = {
            0x8973A69C, 0xCA7BC5B5, 0xAA7A2746, 0x6A7A276E, 0x88F3D4BB, 0x88F3D4BC, 0x4A972F37,
            0x8AB730AE, 0x6976AFA0, 0x4A82253E, 0x4AC096C6, 0x4AC096CA, 0x0BFD8064,
        };
        for (const uint32_t code : kCodes)
            Virtual<void>(cheats, 0x10, code);
    }

    Virtual<void>(self, 0x6C, uint32_t{1});
    Virtual<void>(self, 0x78);
    Drop(At<void*>(self, view::kHeld120));
    if (void* const recorder = Virtual<void*>(window, 0x88, kRecorderId))
        Virtual<void>(window, 0x3C, recorder);
    EndObject(self, view::kObject184, addr::SC4_F47A780);
    EndObject(self, view::kObject188, addr::SC4_F47A780);
    This<void>(addr::SC4CoverageHelper_Shutdown, self + view::kCoverage);
    ReleasePanels(self, nullptr);
    EndObject(self, view::kLoadingPicture, addr::SC4UI_F777760);

    void* commands = nullptr;
    if (void* const framework = Cdecl<void*>(addr::RZGetFramework))
        Virtual<uint8_t>(framework, 0x14, uint32_t{0xEB903A32}, uint32_t{0xCB90253A}, &commands);
    Virtual<void>(commands, 0x28, std::uintptr_t{addr::cSC4View3DWin_Commands}, uint32_t{0x68});
    This<void>(addr::SC4MessageIds_Unsubscribe, self + view::kMessages,
               static_cast<void*>(self ? self + view::kTarget : nullptr), static_cast<void*>(nullptr));
    Cdecl<uint8_t>(addr::SC4_ClearView3D);

    Drop(At<void*>(self, view::kHeld150));
    EndObject(self, view::kObjectF8, addr::SC4_F4929F0);
    StopObject(self, view::kObjectE8);
    StopObject(self, view::kObjectF4);
    Drop(At<void*>(self, view::kHeld320));
    EndObject(self, view::kCameraControl, addr::cSC4CameraControl_ReleaseCamera);
    EndObject(self, view::kObjectEC, addr::SC4_ReturnTrue648F00);
    Drop(At<void*>(self, view::kHeld154));
    At<uint8_t>(self, view::kInitialised) = 0;
    const uint8_t done = This<uint8_t>(addr::SC4Win_F99D2FE, window);
    This<void>(addr::SC4RefPtr_Destruct562DD0, &commands);
    return done;
}

bool InstallCityView5(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[32] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F7EF200, reinterpret_cast<void*>(&ReleasePanels)},
        {addr::SC4UI_F7EF610, reinterpret_cast<void*>(&ToolBarMessage)},
        {addr::SC4UI_F7EF7A0, reinterpret_cast<void*>(&ClearCursorText)},
        {addr::SC4UI_F7EF820, reinterpret_cast<void*>(&LoadRenderProperties)},
        {addr::SC4UI_F7EFBD0, reinterpret_cast<void*>(&HoverMessage)},
        {addr::SC4UI_F7EFE60, reinterpret_cast<void*>(&CursorTextCopy)},
        {addr::SC4UI_F7EFF40, reinterpret_cast<void*>(&CollectBuildings)},
        {addr::SC4UI_F7F06B0, reinterpret_cast<void*>(&MakeBuildingMenu)},
        {addr::SC4UI_F7F0840, reinterpret_cast<void*>(&ShowRecorder)},
        {addr::SC4UI_F7F0A70, reinterpret_cast<void*>(&Construct)},
        {addr::SC4UI_F7F0FE0, reinterpret_cast<void*>(&GetObjectF8)},
        {addr::SC4UI_F7F0FF0, reinterpret_cast<void*>(&SetCostIndicator)},
        {addr::SC4UI_F7F1010, reinterpret_cast<void*>(&GetCoverageHelper)},
        {addr::SC4UI_F7F1020, reinterpret_cast<void*>(&SetLastSelectedBuilding)},
        {addr::SC4UI_F7F1040, reinterpret_cast<void*>(&SetLastSelectedLot)},
        {addr::SC4UI_F7F1060, reinterpret_cast<void*>(&GetLastSelectedBuilding)},
        {addr::SC4UI_F7F1080, reinterpret_cast<void*>(&GetLastSelectedLot)},
        {addr::SC4UI_F7F10A0, reinterpret_cast<void*>(&TargetQuery)},
        {addr::SC4UI_F7F10B0, reinterpret_cast<void*>(&TargetAddRef)},
        {addr::SC4UI_F7F10C0, reinterpret_cast<void*>(&TargetRelease)},
        {addr::SC4UI_F7F10D0, reinterpret_cast<void*>(&KeysQuery)},
        {addr::SC4UI_F7F10E0, reinterpret_cast<void*>(&KeysAddRef)},
        {addr::SC4UI_F7F10F0, reinterpret_cast<void*>(&KeysRelease)},
        {addr::SC4UI_F7F1100, reinterpret_cast<void*>(&WindowQuery)},
        {addr::SC4UI_F7F1110, reinterpret_cast<void*>(&WindowAddRef)},
        {addr::SC4UI_F7F1120, reinterpret_cast<void*>(&WindowRelease)},
        {addr::SC4UI_F7F1130, reinterpret_cast<void*>(&WindowDelete)},
        {addr::SC4UI_F7F1140, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4UI_F7F1320, reinterpret_cast<void*>(&Shutdown)},
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

const bool g_cityView5Registered = registry::Register({
    "ui.cityview.part5",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F7F1320,
    "Our city view object: made, shut down and unmade, its menu of buildings and small methods (docs/ui.md)",
    false,
    InstallCityView5,
});

} // namespace
} // namespace opensc4::ui
