// The query tool of the city view, second piece: 0x004C5730 to 0x004C7A6A.
// Replacement, off by default:
//
//   ui.tools.query2: 35 functions of the query tool
//     (cSC4ViewInputControlQuery: a view input control of 0xA0 bytes, vtable
//     0x00A90A88; docs/tools.md): Init, the destructor, the tip at the
//     pointer, two more map levels, the helpers that write a number, a
//     percentage or an amount of money into a text control or set up a
//     slider, the window runner's deleting destructor, the destructor of a
//     text entry, and fifteen entries of the filler table at 0x00A92488 with
//     six helpers of later fillers (fire and police stations, jails, the
//     airport and seaport lots, a school's numbers, a building's name).
//
// Entries of the table that are functions of this file (index, address): 0
// 0x004C70C0, 2 0x004C7200, 3 0x004C72E0, 4 0x004C7350, 7 0x004C7390, 8
// 0x004C73D0, 9 0x004C7410, 10 0x004C74F0, 11 0x004C7560, 13 0x004C75A0, 14
// 0x004C75F0, 15 0x004C7660, 17 0x004C67F0, 22 0x004C7910, 23 0x004C7980.
// Three of them change the city when a window reads its control back (fill
// 0): 0 renames the building, 2 and 9 set a fire or police station's
// funding.
//
// Left the game's: 0x004C56F0 (a tree's erase, folded, callers elsewhere),
// 0x004C5E20 (builds the route's line for display: drawing), 0x004C6830 (a
// window's paint hook that draws a bar, 0x004C5040: drawing), 0x004C6920
// (a hash's find, callers elsewhere), 0x004C69A0 (a tree's copy, a caller
// in the audio code), 0x004C6A60 (a log line; its arguments come in EBX
// and EDI).
//
// Time: Init gives the tool's timer its callback (0x004C4E70) but does not
// start it. The tip at the pointer (0x004C5BA0) reads where the mouse is
// (the window manager's slot 34): input, not city state. No random number
// is drawn, no generator made.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"
#include "core/tool_query.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::tool_query_2 {
namespace {

namespace base = tool_query::base;
namespace field = tool_query::field;
namespace window_runner = tool_query::window_runner;
namespace text_entry = tool_query::text_entry;

constexpr uint32_t kCursorFirst = 0x816D7F74;  // the cursor of kind 0
constexpr uint32_t kCursorSecond = 0xCB8D5B54; // the cursor of kind 1
constexpr uint32_t kCursorText = 0x2A456CB1;   // the tool's text at the pointer
constexpr uint32_t kWindowIid = 0x22BA0121;
constexpr uint32_t kProcedureIid = 0x5386D516; // what a window is asked for to get the tool's target
constexpr uint32_t kTextEdit = 0x231A1C57;
constexpr uint32_t kTextControl = 0x212CDC1F;
constexpr uint32_t kNumberControl = 0x21325207;
constexpr uint32_t kBuildingPart = 0x87DDA3A9; // an occupant's interface with the building's name
constexpr uint32_t kStationPart = 0xC9C00435;  // a protection simulator's part with the station numbers
constexpr uint32_t kHandlerProperty = 0x6A8CC9D6;
constexpr uint32_t kCostProperty = 0x2AA6E8AD;
constexpr uint32_t kExtraProperty = 0x0AB3F6DB;
constexpr uint32_t kSecondNumberProperty = 0x27812851;

// The messages Init registers the tool's target part for.
constexpr uint32_t kMessages[] = {0x0A456D95, 0x0A456D96, 0x26D31EC2, 0xA6B79602, 0x66956814, 0x69247DC7};

// The floats at 0x00A823A4 (0.01), 0x00A84D2C (0.5), 0x00A80AA0 (100),
// 0x00A90978 (1.5 times 2 to the 23rd: adding it and taking 0x4B400000
// from the bits rounds to a whole number) and 0x00A9095C (0.8); the double
// 2 to the 32nd is 0x00A80AA8's float.
constexpr float kHundredth = 0.01f;
constexpr float kHalf = 0.5f;
constexpr float kHundred = 100.0f;
constexpr float kRoundMagic = 12582912.0f;
constexpr uint32_t kRoundBias = 0xB4C00000;
constexpr float kJailShare = 0.8f;
constexpr double kTwoTo32 = 4294967296.0;

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

uint32_t Bits(float value)
{
    return At<uint32_t>(&value, 0);
}

// __ftol2: truncation toward zero, the low 32 bits.
int32_t Ftol(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// --- The game's string ---------------------------------------------------------------------

// SC4String as the game keeps one on the stack: the vtable 0x00A80810, an
// STL body (begin, end, end of the buffer) and a zero dword.
struct GameString {
    std::uintptr_t vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};
static_assert(sizeof(GameString) == 0x14);

// The game's inline construction of an empty string: an 8-byte buffer from
// the STL allocator holding a 0.
void MakeEmpty(GameString& text)
{
    text.begin = nullptr;
    text.end = nullptr;
    text.capacity = nullptr;
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    text.begin = buffer;
    text.end = buffer;
    text.capacity = buffer + 8;
    *buffer = 0;
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// The game's inline destructor: the vtable set again, the buffer freed.
void Destroy(GameString& text)
{
    text.vtable = addr::SC4String_Vtable;
    if (text.begin)
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(text.begin));
}

// --- The object ----------------------------------------------------------------------------

// The game's 0x004C5750 (called only by 0x004C5730): the object at +0x9C,
// the timer, the windows at +0x4C and +0x48 and the tip at +0x44 are let go
// (the fields stay), then the base's destructor. The vtables are not set
// again.
void __fastcall Destruct(char* self, void*)
{
    if (void* const line = At<void*>(self, field::kObject9C))
        Release(line);
    This<void>(addr::cRZTimer_Destruct, self + field::kTimer);
    if (void* const other = At<void*>(self, field::kPanelOther))
        Release(other);
    if (void* const panel = At<void*>(self, field::kPanel))
        Release(panel);
    if (void* const tip = At<void*>(self, field::kTip))
        Release(tip);
    This<void>(addr::SC4ViewInputControl_Destruct, self);
}

// The game's 0x004C5730 (slot 26, ret 4): the deleting destructor; returns
// this.
char* __fastcall DeletingDestruct(char* self, void*, uint32_t flags)
{
    Destruct(self, nullptr);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// What Init does with each of the two windows of kind 1 once the layout has
// given it: taken out of the layout window (slot 15) and made a child of the
// view's window (slot 14); its target (what the window answers for
// 0x5386D516, slot 7) the tool's panel target; the window's slot 23. The
// first is placed (slot 56) at 2 from the view window's right edge and 2
// from the top, and shown (slot 69); the second is placed at the first's
// slot 45 less its own width (slot 41) and the first's slot 44, and hidden
// (slot 70). The first is not checked when the second is placed.
void SetUpWindow(char* self, void* layout, void* target, bool second)
{
    const std::size_t at = second ? field::kPanelOther : field::kPanel;
    Virtual<void>(layout, 0x3C, At<void*>(self, at));
    void* const view = At<void*>(self, base::kView);
    Virtual<void>(Virtual<void*>(view, 0xC), 0x38, At<void*>(self, at));
    if (!second) {
        void* const viewWindow = Virtual<void*>(view, 0xC);
        void* const panel = At<void*>(self, field::kPanel);
        const uint32_t right = uint32_t{0xFFFFFFFE} - Virtual<uint32_t>(panel, 0xA4);
        Virtual<void>(panel, 0xE0, right + Virtual<uint32_t>(viewWindow, 0xA4), uint32_t{2});
    } else {
        void* const first = At<void*>(self, field::kPanel);
        void* const other = At<void*>(self, field::kPanelOther);
        const uint32_t along = Virtual<uint32_t>(first, 0xB0);
        const uint32_t own = Virtual<uint32_t>(other, 0xA4);
        Virtual<void>(other, 0xE0, Virtual<uint32_t>(first, 0xB4) - own, along);
    }
    void* procedure = nullptr;
    if (Virtual<uint8_t>(At<void*>(self, at), 0x0, kProcedureIid, &procedure))
        Virtual<void>(procedure, 0x1C, target);
    Virtual<void>(At<void*>(self, at), 0x5C);
    Virtual<void>(At<void*>(self, at), second ? 0x118 : 0x114);
    if (procedure)
        Release(procedure);
}

// The game's 0x004C57A0 (slot 3): Init. True, at once when inited. The ID
// and cursor of the kind; the base's Init; the target part registered with
// the message server (when there is one) for six messages; the timer given
// 0x004C4E70 with the tool (its slot 9; not started); +0x78 set; the view's
// slot 39 into +0x94. A new info tip (0x1B4 bytes, 0x00799DD0) replaces the
// one at +0x44 (with the references), gets the picture 0xA85F1A83 (slot
// 11), 0x0A52F5C6 (slot 10) and the box -40, -40, 40, 40 (slot 17); its
// window is hidden and made a child of the view's window. Kind 1 then loads
// the window 0x6BB92BCB of the layout {0, 0x96A006B0, 0xABB0120F} into the
// view's window; when it came, a panel target (0xC bytes, vtable
// 0x00A90980, count 0, the tool) is made and referenced, the layout's
// children 0x0BB0F5E7 and 0x6BB92BCA (slot 36) replace +0x48 and +0x4C and
// are set up (SetUpWindow), the layout window is taken out of the view's
// (slot 16) and the target and the layout window are let go. Nothing made
// is checked for null but the panel target; a failed tip calls through
// null, as in the game.
uint8_t __fastcall Init(char* self, void*)
{
    if (At<uint8_t>(self, base::kInited))
        return 1;
    if (At<uint32_t>(self, field::kKind) == 1) {
        At<uint32_t>(self, base::kId) = tool_query::kToolIdSecond;
        At<uint32_t>(self, base::kCursor) = kCursorSecond;
    } else {
        At<uint32_t>(self, base::kId) = tool_query::kToolIdFirst;
        At<uint32_t>(self, base::kCursor) = kCursorFirst;
    }
    This<uint8_t>(addr::SC4ViewInputControl_Init, self);
    if (void* const server = Global<void*>(addr::SC4_MessageServer)) {
        for (const uint32_t message : kMessages)
            Virtual<uint8_t>(server, 0x14, static_cast<void*>(self + field::kTarget), message);
    }
    Virtual<void>(self + field::kTimer, 0x24, std::uintptr_t{addr::SC4QueryTool_TimerPost}, static_cast<void*>(self));
    At<uint8_t>(self, field::kFlag78) = 1;
    At<uint8_t>(self, field::kFlag94) = Virtual<uint8_t>(At<void*>(self, base::kView), 0x9C);

    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x1B4});
    void* const tip = memory ? This<void*>(addr::SC4InfoTip_Construct, memory) : nullptr;
    void* const held = At<void*>(self, field::kTip);
    if (tip != held) {
        if (tip)
            AddRef(tip);
        At<void*>(self, field::kTip) = tip;
        if (held)
            Release(held);
    }
    Virtual<void>(At<void*>(self, field::kTip), 0x2C, uint32_t{0xA85F1A83});
    Virtual<void>(At<void*>(self, field::kTip), 0x28, uint32_t{0x0A52F5C6});
    int32_t box[4] = {-40, -40, 40, 40};
    Virtual<void>(At<void*>(self, field::kTip), 0x44, static_cast<int32_t*>(box));
    Virtual<void>(Virtual<void*>(At<void*>(self, field::kTip), 0xC), 0x118);
    void* const viewWindow = Virtual<void*>(At<void*>(self, base::kView), 0xC);
    Virtual<void>(viewWindow, 0x38, Virtual<void*>(At<void*>(self, field::kTip), 0xC));

    if (At<uint32_t>(self, field::kKind) != 1)
        return 1;
    uint32_t key[3] = {0, tool_query::kLayoutGroup, tool_query::kLayoutInstance};
    void* layout = nullptr;
    void* const parent = Virtual<void*>(At<void*>(self, base::kView), 0xC);
    Cdecl<uint8_t>(addr::SC4UI_LoadWindow, static_cast<uint32_t*>(key), parent, tool_query::kLayoutWindow, kWindowIid,
                   &layout);
    if (!layout)
        return 1;
    char* target = Cdecl<char*>(addr::SC4_operator_new, uint32_t{0xC});
    if (target) {
        At<std::uintptr_t>(target, 0) = addr::SC4QueryPanelTarget_Vtable;
        At<uint32_t>(target, tool_query::panel_target::kCount) = 0;
        At<char*>(target, tool_query::panel_target::kTool) = self;
        AddRef(target);
    }
    if (void* const old = At<void*>(self, field::kPanel)) {
        At<void*>(self, field::kPanel) = nullptr;
        Release(old);
    }
    Virtual<uint8_t>(layout, 0x90, tool_query::kPanelId, kWindowIid, &At<void*>(self, field::kPanel));
    if (void* const old = At<void*>(self, field::kPanelOther)) {
        At<void*>(self, field::kPanelOther) = nullptr;
        Release(old);
    }
    Virtual<uint8_t>(layout, 0x90, tool_query::kPanelOtherId, kWindowIid, &At<void*>(self, field::kPanelOther));
    if (At<void*>(self, field::kPanel))
        SetUpWindow(self, layout, target, false);
    if (At<void*>(self, field::kPanelOther))
        SetUpWindow(self, layout, target, true);
    Virtual<void>(Virtual<void*>(At<void*>(self, base::kView), 0xC), 0x40, layout);
    if (target)
        Release(target);
    Release(layout);
    return 1;
}

// The game's 0x004C5BA0 (ret 8; called by DoMessage 0x004D7160): the tip
// at the pointer for a title and a text (game strings; slot 7 their
// length). When both are missing or empty the view's text at the pointer is
// cleared (slot 41 with 0x2A456CB1) and the tip's window hidden. Else the
// tip gets both (slots 6 and 8); when its window is not showing (slot 71)
// it goes to where the mouse is over the view's window (the window
// manager's slot 34, real input; the game's out places are the argument
// slots, so a failed answer leaves the two pointers' values) and is shown.
void __fastcall ShowTip(char* self, void*, void* title, void* text)
{
    void* const tip = At<void*>(self, field::kTip);
    const bool any = (title && Virtual<uint32_t>(title, 0x1C) != 0) || (text && Virtual<uint32_t>(text, 0x1C) != 0);
    if (!any) {
        Virtual<void>(At<void*>(self, base::kView), 0xA4, kCursorText);
        Virtual<void>(Virtual<void*>(At<void*>(self, field::kTip), 0xC), 0x118);
        return;
    }
    Virtual<void>(tip, 0x18, title);
    Virtual<void>(At<void*>(self, field::kTip), 0x20, text);
    if (Virtual<uint8_t>(Virtual<void*>(At<void*>(self, field::kTip), 0xC), 0x11C))
        return;
    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    int32_t point[2] = {static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(title)),
                        static_cast<int32_t>(reinterpret_cast<std::uintptr_t>(text))};
    Virtual<uint8_t>(manager, 0x88, Virtual<void*>(At<void*>(self, base::kView), 0xC), &point[0], &point[1]);
    Virtual<void>(At<void*>(self, field::kTip), 0x3C, static_cast<int32_t*>(point));
    Virtual<void>(Virtual<void*>(At<void*>(self, field::kTip), 0xC), 0x114);
}

// --- Two more map levels ---------------------------------------------------------------------

// As the five of the first piece (0x004C4750 to 0x004C4A30): below the
// lower bound the number out is the value negated, from the upper bound on
// it is the value halved (towards zero), between them 0 with the answer 1.
uint32_t Band(int32_t value, int32_t low, int32_t high, int32_t* out)
{
    if (value < low) {
        *out = -value;
        return 2;
    }
    if (value >= high) {
        *out = value / 2;
        return 0;
    }
    *out = 0;
    return 1;
}

// The game's 0x004C5C90 (cdecl; only caller 0x004D4FD0, the object from
// 0x004D32D0 as for the first piece's five): kind 0. For each of the four
// curves at the object's +0x50 that is there, the pollution simulator's
// grid of that type (slot 5; the grid's slot 7, 16 bits taken unsigned) at
// the cell; types 0, 1 and 3 go through their curve and are added up, each
// sum truncated; type 2 is read and passed over. 2 below, 0 above.
uint32_t __cdecl LevelPollution(char* maps, int32_t x, int32_t z, int32_t* out)
{
    int32_t low = 0, high = 0;
    Virtual<void>(maps, 0x78, uint32_t{0}, &low, &high);
    int32_t sum = 0;
    for (uint32_t type = 0; type < 4; ++type) {
        if (!At<void**>(maps, 0x50)[type])
            continue;
        void* const grid = Virtual<void*>(Global<void*>(addr::SC4_PollutionSimulator), 0x14, type);
        const uint16_t value = Virtual<uint16_t>(grid, 0x1C, x, z);
        if (type == 2)
            continue;
        const double curved =
            This<double>(addr::SC4Curve_Evaluate, At<void**>(maps, 0x50)[type], static_cast<float>(value));
        sum = Ftol(curved + static_cast<double>(sum));
    }
    return Band(sum, low, high, out);
}

// The game's 0x004C5D70 (cdecl, the same caller): kind 1, the third curve
// at +0x50 and the pollution grid of type 2; 0 without the curve. 2 below, 0
// above.
uint32_t __cdecl LevelGarbage(char* maps, int32_t x, int32_t z, int32_t* out)
{
    int32_t low = 0, high = 0;
    Virtual<void>(maps, 0x78, uint32_t{1}, &low, &high);
    int32_t value = 0;
    if (At<void**>(maps, 0x50)[2]) {
        void* const grid = Virtual<void*>(Global<void*>(addr::SC4_PollutionSimulator), 0x14, uint32_t{2});
        const uint16_t cell = Virtual<uint16_t>(grid, 0x1C, x, z);
        value = Ftol(This<double>(addr::SC4Curve_Evaluate, At<void**>(maps, 0x50)[2], static_cast<float>(cell)));
    }
    return Band(value, low, high, out);
}

// --- Small classes ---------------------------------------------------------------------------

// The game's 0x004C68E0 (slot 3 of the window runner's vtable 0x00A90B6C,
// ret 4): the deleting destructor. The three vtables set again; the window
// at +0x10 let go once (the constructor took two references); the counted
// base's destructor; returns this. The object at +0x14 is not let go.
char* __fastcall RunnerDelete(char* self, void*, uint32_t flags)
{
    At<std::uintptr_t>(self, 0) = addr::SC4QueryWindowRunner_Vtable;
    At<std::uintptr_t>(self, window_runner::kProcedure) = addr::SC4QueryWindowRunner_ProcedureVtable;
    At<std::uintptr_t>(self, window_runner::kTarget) = addr::SC4QueryWindowRunner_TargetVtable;
    if (void* const window = At<void*>(self, window_runner::kWindow))
        Release(window);
    This<void>(addr::cRZBaseUnknown_Destruct, self);
    if (flags & 1)
        Cdecl<void>(addr::SC4_operator_delete, self);
    return self;
}

// The game's 0x004C6AF0 (13 calls in 0x004D5xxx): the destructor of a text
// entry (a key and a string): the string's vtable set again, its buffer
// freed.
void __fastcall DestroyTextEntry(char* self, void*)
{
    At<std::uintptr_t>(self, text_entry::kText) = addr::SC4String_Vtable;
    if (void* const buffer = At<void*>(self, text_entry::kText + 4))
        Cdecl<void>(addr::SC4_Free, buffer);
}

// --- Writing to a window's controls ------------------------------------------------------------

// The game's 0x004C6B10 (cdecl): the text of a text edit control (interface
// 0x231A1C57) into a game string: its length (slot 47) into *length and the
// string's slot 15 with it, the text (slot 45) through a string of the
// function's own, handed over by its data (the string's slot 4). False
// without the window or the child.
uint8_t __cdecl GetChildEdit(void* window, uint32_t id, void* text, uint32_t* length)
{
    void* control = nullptr;
    if (!window || !Virtual<uint8_t>(window, 0x94, id, kTextEdit, &control))
        return 0;
    GameString own;
    MakeEmpty(own);
    const uint32_t count = Virtual<uint32_t>(control, 0xBC);
    *length = count;
    Virtual<void>(text, 0x3C, count);
    Virtual<void>(control, 0xB4, &own);
    Virtual<void>(text, 0x10, This<const char*>(addr::cRZBaseString_Data, &own));
    Release(control);
    Destroy(own);
    return 1;
}

// What the three below share: the text goes to a text control's caption
// (interface 0x212CDC1F, slot 4); the string is destroyed. False without
// the window or the child.
uint8_t SetChildCaption(void* window, uint32_t id, GameString& text)
{
    uint8_t done = 0;
    void* control = nullptr;
    if (window && Virtual<uint8_t>(window, 0x94, id, kTextControl, &control)) {
        Virtual<void>(control, 0x10, &text);
        Release(control);
        done = 1;
    }
    Destroy(text);
    return done;
}

// The game's 0x004C6BF0 (cdecl; 33 calls): a number as text ("%d" at
// 0x00A8281C, the string's Sprintf 0x0090F574).
uint8_t __cdecl SetChildInt(void* window, uint32_t id, int32_t value)
{
    GameString text;
    MakeEmpty(text);
    Cdecl<void>(addr::cRZBaseString_Sprintf, &text, "%d", value);
    return SetChildCaption(window, id, text);
}

// The game's 0x004C6CA0 (cdecl; 11 calls): a percentage ("%d%%" at
// 0x00A90BB4).
uint8_t __cdecl SetChildPercent(void* window, uint32_t id, int32_t value)
{
    GameString text;
    MakeEmpty(text);
    Cdecl<void>(addr::cRZBaseString_Sprintf, &text, "%d%%", value);
    return SetChildCaption(window, id, text);
}

// The game's 0x004C6D50 (cdecl; 12 calls): an amount of money (64 bits) as
// the game writes money (0x005F9AB0).
uint8_t __cdecl SetChildMoney(void* window, uint32_t id, int64_t amount)
{
    GameString text;
    MakeEmpty(text);
    Cdecl<void>(addr::SC4_FormatMoney, amount, &text);
    return SetChildCaption(window, id, text);
}

// The game's 0x004C6E00 (cdecl; called by 0x004D3F40): the same into the
// control whose ID is 0x100 past the given one.
uint8_t __cdecl SetChildMoneyNext(void* window, uint32_t id, int64_t amount)
{
    return SetChildMoney(window, id + 0x100, amount);
}

// A window's value object kept as a property (the control's window, slot
// 3; slot 113 to find it): one found is given back its extra reference at
// once and used; a missing one is made (0x14 bytes, 0x009166D4) and
// referenced. A failed allocation calls through null, as in the game.
void* ValueOf(void* control, uint32_t property)
{
    void* value = nullptr;
    if (!Virtual<uint8_t>(Virtual<void*>(control, 0xC), 0x1C4, property, &value)) {
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x14});
        value = memory ? This<void*>(addr::SC4ScriptNumber_Construct, memory) : nullptr;
        AddRef(value);
    } else {
        Release(value);
    }
    return value;
}

void KeepValue(void* control, uint32_t property, void* value)
{
    Virtual<void>(Virtual<void*>(control, 0xC), 0x1C8, property, value);
}

// The game's 0x004C6E30 (cdecl, 11 dwords; 7 calls): a slider (a number
// control, interface 0x21325207). When `low` is below `high` they become
// its range (slots 6 and 8). Its value is set to `value` plus 1 and then to
// `value` (slot 5), its window's slot 23. Three values hang on its window
// as properties: 0x6A8CC9D6 the function to call when it moves (the
// value's slot 53), 0x2AA6E8AD the cost (slot 35, 64 bits), and when
// `ids` is not null, 0x0AB3F6DB a copy of `count` dwords from `ids` (slot
// 102; the callers pass tables of control IDs such as 0x00B096DC). The
// value of `about` is not read. Then the amount `value` hundredths of the
// cost, plus a half, truncated, goes as money into the control 0x100 past
// it (0x004C6D50). False without the window or the child.
uint8_t __cdecl SetChildSlider(void* window, uint32_t id, int32_t low, int32_t high, int32_t value, int64_t cost,
                               void*, std::uintptr_t handler, const uint32_t* ids, uint32_t count)
{
    if (!window)
        return 0;
    void* control = nullptr;
    if (!Virtual<uint8_t>(window, 0x94, id, kNumberControl, &control))
        return 0;
    if (low < high) {
        Virtual<void>(control, 0x18, low);
        Virtual<void>(control, 0x20, high);
    }
    Virtual<void>(control, 0x14, value + 1);
    Virtual<void>(control, 0x14, value);
    Virtual<void>(Virtual<void*>(control, 0xC), 0x5C);
    void* const call = ValueOf(control, kHandlerProperty);
    Virtual<void>(call, 0xD4, handler);
    KeepValue(control, kHandlerProperty, call);
    void* const price = ValueOf(control, kCostProperty);
    Virtual<void>(price, 0x8C, cost);
    KeepValue(control, kCostProperty, price);
    if (ids) {
        void* const extra = ValueOf(control, kExtraProperty);
        Virtual<void>(extra, 0x198, ids, count);
        KeepValue(control, kExtraProperty, extra);
    }
    Release(control);
    const double amount = static_cast<double>(value) * static_cast<double>(kHundredth) * static_cast<double>(cost) +
                          static_cast<double>(kHalf);
    return SetChildMoney(window, id + 0x100, static_cast<int64_t>(amount));
}

// --- The fillers ---------------------------------------------------------------------------

// The game's 0x004C70C0 (table index 0): the building's name in a text
// edit. The name comes from the occupant's part 0x87DDA3A9 (slot 19) into
// an empty string (made from the range at 0x00A80BF0, 0x00407FA0). With
// `fill` it goes to the control (0x004C3B90). Without it the control's text
// is read (0x004C6B10) and, when it differs from the name (0x00454BB0),
// given to the part (slot 18): the building is renamed. The answer is the
// write's or the read's. The occupant is not checked on that path.
uint8_t __cdecl FillName(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    const char* const empty = reinterpret_cast<const char*>(addr::MySim_ScriptEmpty);
    GameString name;
    name.begin = nullptr;
    name.end = nullptr;
    name.capacity = nullptr;
    uint32_t allocator = fill;
    This<void>(addr::SC4String_FromRange, &name.begin, empty, empty, &allocator);
    name.vtable = addr::SC4String_Vtable;
    name.zero = 0;
    if (occupant) {
        void* part = nullptr;
        if (Virtual<uint8_t>(occupant, 0x0, kBuildingPart, &part)) {
            Virtual<void>(part, 0x4C, &name);
            Release(part);
        }
    }
    uint8_t result = 0;
    if (static_cast<uint8_t>(fill)) {
        result = Cdecl<uint8_t>(addr::SC4QueryTool_SetChildEdit, window, id, static_cast<void*>(&name));
    } else {
        GameString typed;
        This<void*>(addr::SC4String_FromText, &typed, empty);
        uint32_t length = fill;
        result = GetChildEdit(window, id, &typed, &length);
        if (result && Cdecl<uint8_t>(addr::SC4UI_F454BB0, static_cast<void*>(&name), static_cast<void*>(&typed))) {
            void* part = nullptr;
            if (Virtual<uint8_t>(occupant, 0x0, kBuildingPart, &part)) {
                Virtual<void>(part, 0x48, &typed);
                Release(part);
            }
        }
        Destroy(typed);
    }
    Destroy(name);
    return result;
}

// Fillers 2 and 9: a station's funding on a slider. With `fill` the
// simulator's funding getter (the building, its cost out, the share out;
// the share 0 before, the cost not set) gives the share, times 100 rounded
// to the nearest whole number by the float trick, as the slider's value;
// the cost as its cost; the filler's own address as the function the
// slider calls. True. Without it the slider's number (unset without the
// window or the child) times 0.01 goes to the setter as the share: the
// station's funding changes. True.
uint8_t Funding(void* window, uint32_t id, uint32_t fill, void* about, std::uintptr_t service, std::size_t getter,
                std::size_t setter, std::uintptr_t self)
{
    void* const simulator = Global<void*>(service);
    if (static_cast<uint8_t>(fill)) {
        int32_t cost = 0;
        float share = 0.0f;
        Virtual<uint8_t>(simulator, getter, about, &cost, &share);
        const float shifted = static_cast<float>(static_cast<double>(share) * static_cast<double>(kHundred) +
                                                 static_cast<double>(kRoundMagic));
        const int32_t percent = static_cast<int32_t>(Bits(shifted) + kRoundBias);
        SetChildSlider(window, id, 1, 0, percent, static_cast<int64_t>(cost), about, self, nullptr, 0);
        return 1;
    }
    int32_t number = 0;
    void* control = nullptr;
    if (window && Virtual<uint8_t>(window, 0x94, id, kNumberControl, &control)) {
        number = Virtual<int32_t>(control, 0x10);
        Release(control);
    }
    float share = static_cast<float>(static_cast<double>(number) * static_cast<double>(kHundredth));
    Virtual<uint8_t>(simulator, setter, about, &share);
    return 1;
}

// The game's 0x004C7200 (table index 2): the fire simulator's funding
// (slots 8 and 9).
uint8_t __cdecl FillFireFunding(void* window, uint32_t id, uint32_t fill, void* about)
{
    return Funding(window, id, fill, about, addr::SC4_Service43D78, 0x20, 0x24, addr::SC4QueryTool_FillFireFunding);
}

// The game's 0x004C7410 (table index 9): the police simulator's funding
// (slots 7 and 8).
uint8_t __cdecl FillPoliceFunding(void* window, uint32_t id, uint32_t fill, void* about)
{
    return Funding(window, id, fill, about, addr::SC4_PoliceSimulator, 0x1C, 0x20,
                   addr::SC4QueryTool_FillPoliceFunding);
}

// Fillers 3 and 10: with `fill`, the simulator's part 0xC9C00435 (false
// when it does not answer) is asked for a station's three numbers (slot 7:
// the building, three out, not set before) and the third is written. True
// without `fill`.
uint8_t Station(void* window, uint32_t id, uint32_t fill, void* about, std::uintptr_t service)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    void* part = nullptr;
    if (!Virtual<uint8_t>(Global<void*>(service), 0x0, kStationPart, &part))
        return 0;
    uint32_t first = 0, second = 0, third = 0;
    Virtual<uint8_t>(part, 0x1C, about, &first, &second, &third);
    Release(part);
    return SetChildInt(window, id, static_cast<int32_t>(third));
}

// The game's 0x004C72E0 (table index 3): the fire simulator's.
uint8_t __cdecl FillFireStation(void* window, uint32_t id, uint32_t fill, void* about)
{
    return Station(window, id, fill, about, addr::SC4_Service43D78);
}

// The game's 0x004C74F0 (table index 10): the police simulator's.
uint8_t __cdecl FillPoliceStation(void* window, uint32_t id, uint32_t fill, void* about)
{
    return Station(window, id, fill, about, addr::SC4_PoliceSimulator);
}

// Fillers 4 and 11: with `fill`, the station's cost from the funding getter
// (no share asked) as money, the 32 bits taken signed. The game's out place
// is the `fill` argument's, so a getter that writes nothing leaves its
// dword. True without `fill`.
uint8_t Cost(void* window, uint32_t id, uint32_t fill, void* about, std::uintptr_t service, std::size_t getter)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    int32_t cost = static_cast<int32_t>(fill);
    Virtual<uint8_t>(Global<void*>(service), getter, about, &cost, static_cast<float*>(nullptr));
    return SetChildMoney(window, id, static_cast<int64_t>(cost));
}

// The game's 0x004C7350 (table index 4): the fire simulator's.
uint8_t __cdecl FillFireCost(void* window, uint32_t id, uint32_t fill, void* about)
{
    return Cost(window, id, fill, about, addr::SC4_Service43D78, 0x20);
}

// The game's 0x004C7560 (table index 11): the police simulator's.
uint8_t __cdecl FillPoliceCost(void* window, uint32_t id, uint32_t fill, void* about)
{
    return Cost(window, id, fill, about, addr::SC4_PoliceSimulator, 0x1C);
}

// A number of the police simulator for what is asked about (a slot with one
// argument) as text. True without `fill`.
uint8_t PoliceNumber(void* window, uint32_t id, uint32_t fill, void* about, std::size_t slot)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    return SetChildInt(window, id, Virtual<int32_t>(Global<void*>(addr::SC4_PoliceSimulator), slot, about));
}

// The game's 0x004C7390 (table index 7): the police simulator's slot 43
// (arrests at a station).
uint8_t __cdecl FillArrests(void* window, uint32_t id, uint32_t fill, void* about)
{
    return PoliceNumber(window, id, fill, about, 0xAC);
}

// The game's 0x004C73D0 (table index 8): the police simulator's slot 44
// (crimes at a station).
uint8_t __cdecl FillCrimes(void* window, uint32_t id, uint32_t fill, void* about)
{
    return PoliceNumber(window, id, fill, about, 0xB0);
}

// The game's 0x004C75A0 (table index 13): with `fill`, the jail's capacity
// (the police simulator's slot 28, taken signed) times 0.8, truncated.
uint8_t __cdecl FillJailShare(void* window, uint32_t id, uint32_t fill, void* about)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    const int32_t capacity = Virtual<int32_t>(Global<void*>(addr::SC4_PoliceSimulator), 0x70, about);
    return SetChildInt(window, id, Ftol(static_cast<double>(capacity) * static_cast<double>(kJailShare)));
}

// The game's 0x004C75F0 (table index 14): with `fill`, the jailed share
// (slot 36: a float through a place, the place the `fill` argument's)
// times the jail's capacity (slot 28, unsigned), truncated.
uint8_t __cdecl FillJailed(void* window, uint32_t id, uint32_t fill, void* about)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    uint32_t place = fill;
    const float share = *Virtual<float*>(Global<void*>(addr::SC4_PoliceSimulator), 0x90, &place);
    const int32_t capacity = Virtual<int32_t>(Global<void*>(addr::SC4_PoliceSimulator), 0x70, about);
    double whole = static_cast<double>(capacity);
    if (capacity < 0)
        whole += kTwoTo32;
    return SetChildInt(window, id, Ftol(whole * static_cast<double>(share)));
}

// The game's 0x004C7660 (table index 15): with `fill`, the jail's upkeep
// (slot 37, 64 bits) as money.
uint8_t __cdecl FillJailUpkeep(void* window, uint32_t id, uint32_t fill, void* about)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    return SetChildMoney(window, id, Virtual<int64_t>(Global<void*>(addr::SC4_PoliceSimulator), 0x94, about));
}

// The game's 0x004C67F0 (table index 17): the share of 0x004C5280 at the
// first cell of the box an occupant gives (its slot 11). False without the
// occupant, which is looked at before `fill`.
uint8_t __cdecl FillOccupantLotShareSecond(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!occupant)
        return 0;
    int32_t box[4];
    Virtual<void>(occupant, 0x2C, static_cast<int32_t*>(box));
    return Cdecl<uint8_t>(addr::SC4QueryTool_FillLotShareSecond, window, id, fill, box[0], box[1]);
}

// The game's 0x004C7910 (table index 22): with `fill`, the second number of
// the property 0x27812851 of the occupant's property holder (the holder's
// slot 9; the property's value, slot 5; its numbers, slot 105). On the way
// an empty 8-byte buffer is taken from the STL allocator and given back.
// False without the property; the occupant is not checked.
uint8_t __cdecl FillPropertySecond(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    void* const property = Virtual<void*>(Virtual<void*>(occupant, 0x18), 0x24, kSecondNumberProperty);
    if (!property)
        return 0;
    const uint32_t* const numbers = Virtual<const uint32_t*>(Virtual<void*>(property, 0x14), 0x1A4);
    char* const unused = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    *unused = 0;
    const uint8_t done = SetChildInt(window, id, static_cast<int32_t>(numbers[1]));
    Cdecl<void>(addr::SC4_Free, static_cast<void*>(unused));
    return done;
}

// The record the residential simulator's slot 26 fills (0x40 bytes: a game
// string, then numbers; the game sets only +0x10, +0x28, +0x2C and +0x34
// before the call, the rest are 0 here).
struct SchoolRecord {
    GameString text;
    uint32_t word[11]; // +0x14 to +0x3F
};
static_assert(sizeof(SchoolRecord) == 0x40);

// The game's 0x004C7980 (table index 23): with `fill`, the residential
// simulator (0x00B43DA4) is asked about the occupant as a school (slot 26)
// and the float at the record's +0x28 is the control's value (0x004C3CA0).
// An unused empty 8-byte buffer is taken and given back on the way. False
// when the simulator says no.
uint8_t __cdecl FillSchoolValue(void* window, uint32_t id, uint32_t fill, void* occupant)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    SchoolRecord record = {};
    MakeEmpty(record.text);
    char* const unused = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    *unused = 0;
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DA4), 0x68, occupant, &record)) {
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(unused));
        Destroy(record.text);
        return 0;
    }
    const uint8_t done = Cdecl<uint8_t>(addr::SC4QueryTool_SetChildValue, window, id, record.word[5]);
    Cdecl<void>(addr::SC4_Free, static_cast<void*>(unused));
    Destroy(record.text);
    return done;
}

// --- Helpers of the port fillers ----------------------------------------------------------------

// What the six share (cdecl: window, control ID, fill, cell x, cell z;
// each called by a filler at 0x004C9E60 to 0x004C9FA0 and by 0x004CC240):
// with `fill`, the lot at the cell (the lot manager's slot 6 with 0), false
// without one; the port developer the city's slot gives. True without
// `fill`.
void* PortLot(int32_t x, int32_t z)
{
    return Virtual<void*>(Global<void*>(addr::SC4_LotManager), 0x18, x, z, uint32_t{0});
}

// The port developer's maintenance cost of the lot (slot 14, 64 bits) as
// money.
uint8_t PortCost(void* window, uint32_t id, uint32_t fill, int32_t x, int32_t z, std::size_t citySlot)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    void* const lot = PortLot(x, z);
    if (!lot)
        return 0;
    void* const developer = Virtual<void*>(Global<void*>(addr::SC4_City), citySlot);
    return SetChildMoney(window, id, Virtual<int64_t>(developer, 0x38, lot));
}

// One of the two usage numbers of the lot (slot 12: the lot, two out) as
// text. The game's place for the one written is the `fill` argument's, so
// a developer that writes nothing leaves its dword; the other place is not
// set (0 here).
uint8_t PortUsage(void* window, uint32_t id, uint32_t fill, int32_t x, int32_t z, std::size_t citySlot, bool second)
{
    if (!static_cast<uint8_t>(fill))
        return 1;
    void* const lot = PortLot(x, z);
    if (!lot)
        return 0;
    void* const developer = Virtual<void*>(Global<void*>(addr::SC4_City), citySlot);
    uint32_t shown = fill;
    uint32_t other = 0;
    if (second)
        Virtual<uint8_t>(developer, 0x30, lot, &other, &shown);
    else
        Virtual<uint8_t>(developer, 0x30, lot, &shown, &other);
    return SetChildInt(window, id, static_cast<int32_t>(shown));
}

// The game's 0x004C7690: the airport's (the city's slot 77) cost.
uint8_t __cdecl FillAirportCost(void* window, uint32_t id, uint32_t fill, int32_t x, int32_t z)
{
    return PortCost(window, id, fill, x, z, 0x134);
}

// The game's 0x004C76F0: the airport's first usage number.
uint8_t __cdecl FillAirportUsageFirst(void* window, uint32_t id, uint32_t fill, int32_t x, int32_t z)
{
    return PortUsage(window, id, fill, x, z, 0x134, false);
}

// The game's 0x004C7760: the airport's second usage number.
uint8_t __cdecl FillAirportUsageSecond(void* window, uint32_t id, uint32_t fill, int32_t x, int32_t z)
{
    return PortUsage(window, id, fill, x, z, 0x134, true);
}

// The game's 0x004C77D0: the seaport's (the city's slot 76) first usage
// number.
uint8_t __cdecl FillSeaportUsageFirst(void* window, uint32_t id, uint32_t fill, int32_t x, int32_t z)
{
    return PortUsage(window, id, fill, x, z, 0x130, false);
}

// The game's 0x004C7840: the seaport's second usage number.
uint8_t __cdecl FillSeaportUsageSecond(void* window, uint32_t id, uint32_t fill, int32_t x, int32_t z)
{
    return PortUsage(window, id, fill, x, z, 0x130, true);
}

// The game's 0x004C78B0: the seaport's cost.
uint8_t __cdecl FillSeaportCost(void* window, uint32_t id, uint32_t fill, int32_t x, int32_t z)
{
    return PortCost(window, id, fill, x, z, 0x130);
}

// --- Registration ----------------------------------------------------------------------

bool InstallQuery2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[40] = {};
    static const struct {
        std::uintptr_t address;
        void* ours;
    } hooks[] = {
        {addr::SC4QueryTool_DeletingDestructor, reinterpret_cast<void*>(&DeletingDestruct)},
        {addr::SC4QueryTool_Destruct, reinterpret_cast<void*>(&Destruct)},
        {addr::SC4QueryTool_Init, reinterpret_cast<void*>(&Init)},
        {addr::SC4QueryTool_ShowTip, reinterpret_cast<void*>(&ShowTip)},
        {addr::SC4QueryTool_LevelPollution, reinterpret_cast<void*>(&LevelPollution)},
        {addr::SC4QueryTool_LevelGarbage, reinterpret_cast<void*>(&LevelGarbage)},
        {addr::SC4QueryTool_FillOccupantLotShareSecond, reinterpret_cast<void*>(&FillOccupantLotShareSecond)},
        {addr::SC4QueryWindowRunner_DeletingDestructor, reinterpret_cast<void*>(&RunnerDelete)},
        {addr::SC4QueryTool_DestroyTextEntry, reinterpret_cast<void*>(&DestroyTextEntry)},
        {addr::SC4QueryTool_GetChildEdit, reinterpret_cast<void*>(&GetChildEdit)},
        {addr::SC4QueryTool_SetChildInt, reinterpret_cast<void*>(&SetChildInt)},
        {addr::SC4QueryTool_SetChildPercent, reinterpret_cast<void*>(&SetChildPercent)},
        {addr::SC4QueryTool_SetChildMoney, reinterpret_cast<void*>(&SetChildMoney)},
        {addr::SC4QueryTool_SetChildMoneyNext, reinterpret_cast<void*>(&SetChildMoneyNext)},
        {addr::SC4QueryTool_SetChildSlider, reinterpret_cast<void*>(&SetChildSlider)},
        {addr::SC4QueryTool_FillName, reinterpret_cast<void*>(&FillName)},
        {addr::SC4QueryTool_FillFireFunding, reinterpret_cast<void*>(&FillFireFunding)},
        {addr::SC4QueryTool_FillFireStation, reinterpret_cast<void*>(&FillFireStation)},
        {addr::SC4QueryTool_FillFireCost, reinterpret_cast<void*>(&FillFireCost)},
        {addr::SC4QueryTool_FillArrests, reinterpret_cast<void*>(&FillArrests)},
        {addr::SC4QueryTool_FillCrimes, reinterpret_cast<void*>(&FillCrimes)},
        {addr::SC4QueryTool_FillPoliceFunding, reinterpret_cast<void*>(&FillPoliceFunding)},
        {addr::SC4QueryTool_FillPoliceStation, reinterpret_cast<void*>(&FillPoliceStation)},
        {addr::SC4QueryTool_FillPoliceCost, reinterpret_cast<void*>(&FillPoliceCost)},
        {addr::SC4QueryTool_FillJailShare, reinterpret_cast<void*>(&FillJailShare)},
        {addr::SC4QueryTool_FillJailed, reinterpret_cast<void*>(&FillJailed)},
        {addr::SC4QueryTool_FillJailUpkeep, reinterpret_cast<void*>(&FillJailUpkeep)},
        {addr::SC4QueryTool_FillAirportCost, reinterpret_cast<void*>(&FillAirportCost)},
        {addr::SC4QueryTool_FillAirportUsageFirst, reinterpret_cast<void*>(&FillAirportUsageFirst)},
        {addr::SC4QueryTool_FillAirportUsageSecond, reinterpret_cast<void*>(&FillAirportUsageSecond)},
        {addr::SC4QueryTool_FillSeaportUsageFirst, reinterpret_cast<void*>(&FillSeaportUsageFirst)},
        {addr::SC4QueryTool_FillSeaportUsageSecond, reinterpret_cast<void*>(&FillSeaportUsageSecond)},
        {addr::SC4QueryTool_FillSeaportCost, reinterpret_cast<void*>(&FillSeaportCost)},
        {addr::SC4QueryTool_FillPropertySecond, reinterpret_cast<void*>(&FillPropertySecond)},
        {addr::SC4QueryTool_FillSchoolValue, reinterpret_cast<void*>(&FillSchoolValue)},
    };
    constexpr std::size_t count = sizeof(hooks) / sizeof(hooks[0]);
    static_assert(count <= sizeof(originals) / sizeof(originals[0]));
    for (std::size_t i = 0; i < count; ++i) {
        const addr::Entry* found = addr_check::Find(hooks[i].address);
        if (!found || !hook::Install(*found, hooks[i].ours, &originals[i]))
            return false;
    }
    return true;
}

const bool g_registered = registry::Register({
    "ui.tools.query2",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4QueryTool_Init,
    "Our query tool, second piece: Init, the destructor and the functions its windows are filled with (docs/tools.md)",
    false,
    InstallQuery2,
});

} // namespace
} // namespace opensc4::tool_query_2
