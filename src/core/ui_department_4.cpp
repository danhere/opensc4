// The budget panel's department dialog: its command handler and its window
// procedure (0x0078B120 to 0x0078C200).
//
// The dialog (0x140 bytes, vtable 0x00AB5494, made by 0x007862B0) shows one
// of three kinds of page, by the number at +0x20: 1 the ordinances, 2 the
// neighbour deals, anything else a department's funding (by building when
// +0x24 is set, by department otherwise). This file holds:
//
//   - OnCommand (0x0078B120): what a button's click does, by the button's
//     window ID: the page's OK, cancel and close buttons, the two lists'
//     scroll arrows, an ordinance's check box, an ordinance's description
//     (a small page made on the spot), a building's row (the view goes
//     there), a department's row (that department's own dialog opens);
//   - WindowProc (0x0078BCA0, vtable slot 4): the start and stop messages
//     passed on, the keys that close the dialog, and the three notices of
//     message type 3: a click (OnCommand), a slider moved on the deals page,
//     a funding bar moved.
//
// The list of this part names two more starts, 0x0078B5F0 and 0x0078BE90:
// both are labels inside these two functions, reached by jumps only.
//
// Replacement, off by default:
//
//   ui.department.part4: the two functions above.
//
// Windows, sliders, the ordinance simulator, deals, department budgets and
// the view's camera are the game's, called through their vtables; the
// dialog's other methods are in the neighbouring parts and called by
// address.
//
// Field names follow ui_windows_3.cpp to _5.cpp. The vector at +0x2C is
// "kFirstList" and "kPairsA" there; here it is used and named for what it
// holds. ui_windows_6.cpp has the names of +0x80 and +0x84 the other way
// round.
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

namespace dialog {
constexpr std::size_t kPane = 0x14;         // the description page's outer window
constexpr std::size_t kInner = 0x18;        // the description page's inner window
constexpr std::size_t kPageKind = 0x20;     // 1 ordinances, 2 neighbour deals, else funding
constexpr std::size_t kByBuilding = 0x24;   // nonzero: the funding is kept by building
constexpr std::size_t kDepartments = 0x2C;  // vector of {funding bar's ID, department budget}
constexpr std::size_t kRecords = 0x50;      // vector of 0x18-byte records: a slider's ID, its deal at +0x10
constexpr std::size_t kRows = 0x74;         // vector of dwords: the ordinance's index of each row
constexpr std::size_t kHeight = 0x80;
constexpr std::size_t kWidth = 0x84;
constexpr std::size_t kCount = 0xA0;        // the ordinances that are available
constexpr std::size_t kIds = 0xA4;          // their IDs
constexpr std::size_t kStates = 0xA8;       // two bytes each: on when the page was built, on now
constexpr std::size_t kFirstScroll = 0x11C; // the first row on show of the first list
constexpr std::size_t kSecondScroll = 0x120;
constexpr std::size_t kFirstRows = 0x124;   // the rows of the first list
constexpr std::size_t kSecondRows = 0x128;
} // namespace dialog

constexpr uint32_t kCloseResult = 0x42B7C353;
constexpr uint32_t kTextGroup = 0x6A3FF01C;
constexpr uint32_t kWinGenClass = 0x4386D516;
constexpr uint32_t kWinGenIid = 0x5386D516;
constexpr uint32_t kRowsShown = 9;
constexpr std::size_t kRecordSize = 0x18;
constexpr std::size_t kRecordDeal = 0x10;

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

// The game's image holder (0x30 bytes); the image at +4.
struct ImageLoader {
    std::uintptr_t vtable;
    void* image;
    char rest[0x28];
};
static_assert(sizeof(ImageLoader) == 0x30);

// One entry of the vector at +0x2C.
struct DepartmentEntry {
    uint32_t id;
    void* department;
};
static_assert(sizeof(DepartmentEntry) == 8);

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

// The game's inlined reference holder: empty it.
void Clear(void*& holder)
{
    if (void* const object = holder) {
        holder = nullptr;
        Release(object);
    }
}

void* Child(void* parent, uint32_t id)
{
    return Virtual<void*>(parent, 0x8C, id);
}

// A child window's flag 1 (shown) set or cleared.
void Show(void* window, uint32_t id, uint32_t shown)
{
    Virtual<void>(Child(window, id), 0x110, uint32_t{1}, shown);
}

// The dialog's 0x00779850: the window closed with a result.
void CloseWindow(char* self, void* window)
{
    This<uint8_t>(addr::SC4UI_F779850, static_cast<void*>(self), window, kCloseResult);
}

// The dialog's 0x00779600: the value kept on a child window.
void* ChildValue(char* self, void* window, uint32_t id)
{
    return This<void*>(addr::SC4UI_F779600, static_cast<void*>(self), window, id);
}

// The dialog's label maker (0x00779660).
void Label(char* self, void* window, uint32_t id, uint32_t x, uint32_t y, void* text, uint32_t align, uint32_t font,
           uint32_t red, uint32_t green, uint32_t blue)
{
    This<uint32_t>(addr::SC4UI_F779660, static_cast<void*>(self), window, id, x, y, text, align, font, red, green,
                   blue);
}

// The dialog's button maker (0x0077B960).
void Button(char* self, void* window, uint32_t id, uint32_t x, uint32_t y, uint32_t picture, uint32_t a, uint32_t b)
{
    This<void>(addr::SC4UI_F77B960, static_cast<void*>(self), window, id, x, y, picture, a, b);
}

// The description page taken off the window and let go, when there is one.
void DropPane(char* self, void* window)
{
    if (void* const pane = At<void*>(self, dialog::kPane)) {
        Virtual<void>(window, 0x3C, pane);
        Clear(At<void*>(self, dialog::kPane));
    }
}

// The page filled again after a scroll arrow of the first list: the
// ordinances, the deals, or the funding page built again.
void RefillAfterFirst(char* self, void* window)
{
    const uint32_t kind = At<uint32_t>(self, dialog::kPageKind);
    if (kind == 1)
        This<void>(addr::SC4UI_F77ADA0, static_cast<void*>(self), window);
    else if (kind == 2)
        This<void>(addr::SC4UI_F781C90, static_cast<void*>(self), window);
    else
        This<void>(addr::SC4UI_F7898A0, static_cast<void*>(self), window);
}

// The same after an arrow of the second list. Only here does a funding page
// kept by building go to its own refill.
void RefillAfterSecond(char* self, void* window)
{
    const uint32_t kind = At<uint32_t>(self, dialog::kPageKind);
    if (kind == 1)
        This<void>(addr::SC4UI_F77ADA0, static_cast<void*>(self), window);
    else if (kind == 2)
        This<void>(addr::SC4UI_F781C90, static_cast<void*>(self), window);
    else if (At<uint32_t>(self, dialog::kByBuilding) != 0)
        This<void>(addr::SC4UI_F785740, static_cast<void*>(self), window, uint32_t{0});
    else
        This<void>(addr::SC4UI_F7898A0, static_cast<void*>(self), window);
}

// A list scrolled down two rows while more than nine are left below its
// first; the down arrow hidden at the end, the up arrow shown.
void ScrollDown(char* self, void* window, std::size_t scroll, std::size_t rows, uint32_t upId, uint32_t downId)
{
    const uint32_t first = At<uint32_t>(self, scroll);
    const uint32_t count = At<uint32_t>(self, rows);
    if (first + kRowsShown < count)
        At<uint32_t>(self, scroll) = first + 2;
    if (At<uint32_t>(self, scroll) + kRowsShown >= count)
        Show(window, downId, 0);
    Show(window, upId, 1);
}

// A list scrolled up two rows; the up arrow hidden at the top, the down
// arrow shown while rows are left below.
void ScrollUp(char* self, void* window, std::size_t scroll, std::size_t rows, uint32_t upId, uint32_t downId)
{
    const uint32_t first = At<uint32_t>(self, scroll);
    if (first > 0)
        At<uint32_t>(self, scroll) = first - 2;
    if (At<uint32_t>(self, scroll) == 0)
        Show(window, upId, 0);
    if (At<uint32_t>(self, scroll) + kRowsShown < At<uint32_t>(self, rows))
        Show(window, downId, 1);
}

// The description of the ordinance on row `id` - 0x1F4: a page of its own
// (outer window kept at +0x14, inner at +0x18) put on the dialog's window at
// the height of the row's button, with the ordinance's name and description
// and two buttons, 0x384 over the whole page and 0x68 in its corner. The
// original uses the inner window's maker for the background picture whether
// or not it was made; so does this.
void ShowDescription(char* self, void* window, uint32_t id)
{
    DropPane(self, window);
    void*& pane = At<void*>(self, dialog::kPane);

    const uint32_t row = At<uint32_t*>(self, dialog::kRows)[id - 0x1F4];
    void* holder; // not set before the call in the original either
    if (!Virtual<uint8_t>(Child(window, id + 0x100), 0x0, 0xC2AFA76Fu, &holder))
        return;
    uint32_t top = Virtual<uint32_t>(Virtual<void*>(holder, 0xC), 0xB0);
    void* const ordinances = Global<void*>(addr::SC4_Service43D90);

    void* outerGen = nullptr;
    void* com = Cdecl<void*>(addr::RZGetCOM);
    if (Virtual<uint8_t>(com, 0xC, kWinGenClass, kWinGenIid, This<void**>(addr::SC4RefPtr_Reset, &outerGen))) {
        This<void*>(addr::SC4RefPtr_Assign, &pane, Virtual<void*>(outerGen, 0xC));
        Virtual<void>(outerGen, 0x18, uint32_t{1}, uint32_t{0});
        Virtual<void>(outerGen, 0x18, uint32_t{2}, uint32_t{0});
        Virtual<void>(outerGen, 0x18, uint32_t{0x10000}, uint32_t{0});
        Virtual<void>(outerGen, 0x18, uint32_t{0x20}, uint32_t{0});
        Virtual<void>(outerGen, 0x18, uint32_t{0x40}, uint32_t{0});
        Virtual<void>(outerGen, 0x18, uint32_t{0x1000}, uint32_t{1});
        Virtual<void>(outerGen, 0x18, uint32_t{0x2000}, uint32_t{1});
        Virtual<void>(outerGen, 0x18, uint32_t{0x4000}, uint32_t{1});
        Virtual<void>(pane, 0x110, uint32_t{0x8000}, uint32_t{0});
        Virtual<void>(pane, 0x110, uint32_t{0x10000}, uint32_t{0});
        Virtual<void>(pane, 0x110, uint32_t{0x200000}, uint32_t{0});
        Virtual<void>(pane, 0x110, uint32_t{0x800}, uint32_t{1});
        Virtual<void>(pane, 0x5C);
        Virtual<void>(pane, 0x110, uint32_t{0x800}, uint32_t{0});
        Virtual<void>(pane, 0x100, uint32_t{0x0423278D});
        Virtual<void>(window, 0x38, pane);

        void* innerGen = nullptr;
        com = Cdecl<void*>(addr::RZGetCOM);
        void*& inner = At<void*>(self, dialog::kInner);
        if (Virtual<uint8_t>(com, 0xC, kWinGenClass, kWinGenIid, This<void**>(addr::SC4RefPtr_Reset, &innerGen))) {
            This<void*>(addr::SC4RefPtr_Assign, &inner, Virtual<void*>(innerGen, 0xC));
            Virtual<void>(innerGen, 0x18, uint32_t{1}, uint32_t{0});
            Virtual<void>(innerGen, 0x18, uint32_t{2}, uint32_t{0});
            Virtual<void>(innerGen, 0x18, uint32_t{0x10000}, uint32_t{0});
            Virtual<void>(innerGen, 0x18, uint32_t{0x20}, uint32_t{0});
            Virtual<void>(innerGen, 0x18, uint32_t{0x40}, uint32_t{0});
            Virtual<void>(innerGen, 0x18, uint32_t{0x1000}, uint32_t{1});
            Virtual<void>(innerGen, 0x18, uint32_t{0x2000}, uint32_t{0});
            Virtual<void>(innerGen, 0x18, uint32_t{0x4000}, uint32_t{1});
            Virtual<void>(inner, 0x110, uint32_t{4}, uint32_t{1});
            Virtual<void>(inner, 0x110, uint32_t{0x8000}, uint32_t{0});
            Virtual<void>(inner, 0x110, uint32_t{0x100}, uint32_t{1});
            Virtual<void>(inner, 0x110, uint32_t{0x10000}, uint32_t{1});
            Virtual<void>(inner, 0x110, uint32_t{0x200000}, uint32_t{1});
            Virtual<void>(inner, 0x110, uint32_t{0x800}, uint32_t{0});
            Virtual<void>(inner, 0x100, uint32_t{0x0423278F});
            Virtual<void>(pane, 0x38, inner);
        }

        ImageLoader loader;
        This<void*>(addr::SC4ImageLoader_ConstructParts, &loader, uint32_t{0x856DDBAC}, uint32_t{0x46A006B0},
                    uint32_t{0x144161E4}, uint32_t{1}, uint32_t{0});
        Virtual<void>(innerGen, 0x34, uint32_t{2}, loader.image);

        const uint32_t width = At<uint32_t>(self, dialog::kWidth) - 0x3C;
        Virtual<void>(pane, 0xD4, width, uint32_t{0x7D});
        Virtual<void>(inner, 0xD4, width, uint32_t{0x7D});
        const uint32_t lowest = At<uint32_t>(self, dialog::kHeight) - 0x7D;
        if (top > lowest)
            top = lowest;
        Virtual<void>(pane, 0xE0, uint32_t{0x1E}, top);
        Virtual<void>(inner, 0xE0, uint32_t{0}, uint32_t{0});

        void* ordinance = Virtual<void*>(ordinances, 0x50, At<uint32_t*>(self, dialog::kIds)[row]);
        void* parent = inner;
        void* text = Virtual<void*>(ordinance, 0x1C);
        Label(self, parent, 0x0ABCE000, 0xA, 5, text, 0, 0xEA85D307, 0x44, 0x55, 0x66);
        ordinance = Virtual<void*>(ordinances, 0x50, At<uint32_t*>(self, dialog::kIds)[row]);
        parent = inner;
        text = Virtual<void*>(ordinance, 0x20);
        Label(self, parent, 0x0ABCE001, 0xF, 0x19, text, 0x63, 0xEA85D308, 0x44, 0x55, 0x66);

        Button(self, pane, 0x384, 0, 0, 0x14416242, 0, 0);
        Button(self, pane, 0x68, width - 0x1F, 0xB, 0x140155B4, 0, 0);
        Virtual<void>(Child(pane, 0x384), 0xD4, width, uint32_t{0x7D});
        Virtual<void>(Child(pane, 0x484), 0xD4, width, uint32_t{0x7D});
        Virtual<void>(Child(pane, 0x384), 0x158, window);
        Virtual<void>(Child(pane, 0x68), 0x158, window);

        This<void>(addr::SC4ImageLoader_Destruct, &loader);
        if (innerGen)
            Release(innerGen);
    }
    Release(holder);
    if (outerGen)
        Release(outerGen);
}

// The game's 0x0078B120 (ret 8; it runs to 0x0078BC05, with a jump table at
// 0x0078BC08 and a byte table at 0x0078BC28; the list's entry 0x0078B5F0 is a
// label inside it). `this` is the dialog. Acts on the click of the button
// with window ID `id` on `window`, the dialog's window; true when the click
// closed the dialog without anything else done.
uint8_t __fastcall OnCommand(char* self, void*, void* window, uint32_t id)
{
    switch (id) {
    case 0x67:
    case 0xCC:
    case 0x385:
    case kCloseResult:
        CloseWindow(self, window);
        return 1;

    case 0x68:
    case 0x384:
        // The description page's two buttons: the page goes.
        Virtual<void>(window, 0x3C, At<void*>(self, dialog::kPane));
        Clear(At<void*>(self, dialog::kPane));
        return 0;

    case 0x6D:
        This<void>(addr::SC4UI_F77C5E0, static_cast<void*>(self), window);
        CloseWindow(self, window);
        return 1;

    case 0xCD: {
        // The ordinances' OK: every ordinance whose state was changed is
        // switched in the simulator.
        void* const ordinances = Global<void*>(addr::SC4_Service43D90);
        for (uint32_t i = 0; i < At<uint32_t>(self, dialog::kCount); ++i) {
            const uint8_t* const state = At<uint8_t*>(self, dialog::kStates) + i * 2;
            const uint8_t before = state[0];
            const uint32_t now = state[1];
            if (now != before)
                Virtual<void>(ordinances, 0x34, At<uint32_t*>(self, dialog::kIds)[i], now);
        }
        DropPane(self, window);
        CloseWindow(self, window);
        return 0;
    }

    case 0xCE:
        DropPane(self, window);
        CloseWindow(self, window);
        return 0;

    case 0xCF:
        This<void>(addr::SC4UI_F77AB40, static_cast<void*>(self));
        DropPane(self, window);
        CloseWindow(self, window);
        return 0;

    case 0x451:
        ScrollUp(self, window, dialog::kFirstScroll, dialog::kFirstRows, 0x451, 0x452);
        RefillAfterFirst(self, window);
        return 0;

    case 0x452:
        ScrollDown(self, window, dialog::kFirstScroll, dialog::kFirstRows, 0x451, 0x452);
        RefillAfterFirst(self, window);
        return 0;

    case 0x453:
        ScrollUp(self, window, dialog::kSecondScroll, dialog::kSecondRows, 0x453, 0x454);
        RefillAfterSecond(self, window);
        return 0;

    case 0x454:
        ScrollDown(self, window, dialog::kSecondScroll, dialog::kSecondRows, 0x453, 0x454);
        RefillAfterSecond(self, window);
        return 0;

    default:
        break;
    }

    // A department's row: its own dialog.
    if (id >= 0x0ABCE600 && id < 0x0ABCE700) {
        void* const department = ChildValue(self, window, id);
        if (!department)
            return 0;
        This<void>(addr::SC4UI_F786530, static_cast<void*>(self), Virtual<uint32_t>(department, 0xC));
        return 0;
    }

    // A building's row: the view goes to the building.
    if (At<uint32_t>(self, dialog::kByBuilding) != 0 && id >= 0x0ABCDE00 && id < 0x0ABCDF00) {
        void* const object = ChildValue(self, window, id);
        if (!object)
            return 0;
        void* occupant = nullptr;
        if (Virtual<uint8_t>(object, 0x0, 0x26BB767Eu, This<void**>(addr::SC4RefPtr_Reset, &occupant))) {
            const uint32_t fill = Global<uint32_t>(addr::SC4UI_DepartmentDialogPositionFill);
            uint32_t position[3] = {fill, fill, fill};
            Virtual<void>(occupant, 0x20, &position[0]);
            void* const camera = Virtual<void*>(Global<void*>(addr::SC4_View43DD0), 0x40);
            This<void>(addr::cSC4CameraControl_SetCenter, camera, &position[0]);
        }
        if (occupant)
            Release(occupant);
        return 0;
    }

    // An ordinance's check box: its state kept, the page filled again.
    if (id >= 0x12C && id <= 0x1F3) {
        const uint32_t row = At<uint32_t*>(self, dialog::kRows)[id - 0x12C];
        void* button = window; // the original reuses the argument's slot
        if (Virtual<uint8_t>(Child(window, id), 0x0, uint32_t{0x8810}, &button)) {
            At<uint8_t*>(self, dialog::kStates)[row * 2 + 1] = Virtual<uint8_t>(button, 0x28);
            Release(button);
        }
        This<void>(addr::SC4UI_F77ADA0, static_cast<void*>(self), window);
        return 0;
    }

    if (id >= 0x1F4 && id <= 0x2BB)
        ShowDescription(self, window, id);
    return 0;
}

// A slider's one label set from a text resource.
void SetOnlyLabel(void* slider, uint32_t instance)
{
    LocalizedString text;
    void* const made =
        This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, kTextGroup, instance, uint32_t{1});
    GameString label;
    void* const copy = This<void*>(addr::SC4String_Substring, &label, made, uint32_t{0}, uint32_t{0xFFFFFFFF});
    Virtual<void>(slider, 0x6C, copy, uint32_t{0});
    This<void>(addr::SC4String_Destroy, &label);
    This<void>(addr::SC4String_Destroy, &text);
}

// A deal's slider was moved to `position`. At 0 a deal that is on is called
// off and the slider gets its one "none" label; otherwise the deal's amount
// becomes `position` steps, counted in the deal's larger step when the
// amount would pass what it carries now, and the deal is made if it was not
// on. A deal with flag 0x100 is left alone.
void DealSliderMoved(void* slider, void* deal, int32_t position)
{
    if (position == 0) {
        if (!(Virtual<uint8_t>(deal, 0x90) & 2))
            return;
        Virtual<void>(deal, 0x88);
        Virtual<void>(slider, 0x60, position);
        SetOnlyLabel(slider, 0xCA5793C0);
        Virtual<void>(slider, 0x70, int32_t{0}, int32_t{1});
        Virtual<void>(deal, 0x94, uint32_t{1}, uint32_t{1});
        return;
    }

    if (Virtual<uint8_t>(deal, 0x9C, uint32_t{0x100}))
        return;
    if (!(Virtual<uint8_t>(deal, 0x90) & 2))
        Virtual<void>(deal, 0x8C);
    if (Virtual<uint32_t>(deal, 0x48) != 0) {
        const uint32_t wanted = Virtual<uint32_t>(deal, 0x40) * static_cast<uint32_t>(position);
        if (wanted > Virtual<uint32_t>(deal, 0x28)) {
            const int32_t large = Virtual<int32_t>(deal, 0x48);
            const int32_t inLarge = Virtual<int32_t>(deal, 0x28) / large;
            const int32_t small = Virtual<int32_t>(deal, 0x40);
            const int32_t inSmall = Virtual<int32_t>(deal, 0x28) / small;
            const uint32_t steps = static_cast<uint32_t>(inLarge) - static_cast<uint32_t>(inSmall) +
                                   static_cast<uint32_t>(position);
            Virtual<void>(deal, 0x2C, Virtual<uint32_t>(deal, 0x48) * steps);
            Virtual<void>(slider, 0x60, int32_t{0});
            SetOnlyLabel(slider, 0x2A5794A0);
            Virtual<void>(deal, 0x44, Virtual<uint32_t>(deal, 0x48));
            return;
        }
    }
    Virtual<void>(deal, 0x2C, Virtual<uint32_t>(deal, 0x40) * static_cast<uint32_t>(position));
    Virtual<void>(slider, 0x60, int32_t{0});
    SetOnlyLabel(slider, 0x2A5794A0);
}

// The number of 0x18-byte records of the vector at +0x50.
uint32_t RecordCount(char* self)
{
    const int32_t bytes =
        static_cast<int32_t>(At<char*>(self, dialog::kRecords + 4) - At<char*>(self, dialog::kRecords));
    return static_cast<uint32_t>(bytes / static_cast<int32_t>(kRecordSize));
}

// The number of 8-byte entries of the vector at +0x2C.
uint32_t DepartmentCount(char* self)
{
    const int32_t bytes =
        static_cast<int32_t>(At<char*>(self, dialog::kDepartments + 4) - At<char*>(self, dialog::kDepartments));
    return static_cast<uint32_t>(bytes >> 3);
}

// Notice 0xE8711255: the slider `id` of the deals page was moved. Every
// record of that slider has its deal changed, then the page is filled again.
void OnDealSlider(char* self, void* window, uint32_t id)
{
    if (RecordCount(self) != 0) {
        uint32_t index = 0;
        std::size_t offset = 0;
        do {
            if (At<uint32_t>(At<char*>(self, dialog::kRecords) + offset, 0) == id) {
                void* slider = nullptr;
                void* const child = Child(window, id);
                if (Virtual<uint8_t>(child, 0x0, 0x412CE496u, This<void**>(addr::SC4RefPtr_Reset, &slider))) {
                    const int32_t position = Virtual<int32_t>(slider, 0x78);
                    void* const deal = At<void*>(At<char*>(self, dialog::kRecords) + offset, kRecordDeal);
                    DealSliderMoved(slider, deal, position);
                    Clear(slider);
                }
                if (slider)
                    Release(slider);
            }
            ++index;
            offset += kRecordSize;
        } while (index < RecordCount(self));
    }
    This<void>(addr::SC4UI_F781C90, static_cast<void*>(self), window);
}

// Notice 0x887113A4: the funding bar `id` was moved. By building, the
// building kept on the bar's window takes the bar's value as its first
// (IDs below 0x0ABCE500) or second funding level; by department, every
// department of that bar takes a hundredth of it. The page is then filled
// again. A byte keeps the refill's own changes of the bars from coming back
// in here.
void OnFundingBar(char* self, void* window, uint32_t id)
{
    void* bar = nullptr;
    if (Virtual<uint8_t>(window, 0x94, id, 0x21325207u, This<void**>(addr::SC4RefPtr_Reset, &bar)) &&
        !Global<uint8_t>(addr::SC4UI_DepartmentDialogBusy)) {
        const uint32_t byBuilding = At<uint32_t>(self, dialog::kByBuilding);
        Global<uint8_t>(addr::SC4UI_DepartmentDialogBusy) = 1;
        if (byBuilding != 0) {
            if (void* const building = ChildValue(self, window, id)) {
                const float percent = static_cast<float>(Virtual<int32_t>(bar, 0x10));
                const uint32_t second = id < 0x0ABCE500 ? 0u : 1u;
                This<uint8_t>(addr::SC4UI_F77BC90, static_cast<void*>(self), building, second, percent);
                This<void>(addr::SC4UI_F785740, static_cast<void*>(self), window, uint32_t{0});
            }
        } else {
            for (uint32_t i = 0; i < DepartmentCount(self); ++i) {
                const DepartmentEntry* const entry = At<DepartmentEntry*>(self, dialog::kDepartments) + i;
                if (entry->id != id)
                    continue;
                void* const department = entry->department;
                const int32_t value = Virtual<int32_t>(bar, 0x10);
                float fraction = static_cast<float>(static_cast<double>(value) * static_cast<double>(0.01f));
                Virtual<void>(department, 0x2C, &fraction, uint32_t{0});
            }
            This<void>(addr::SC4UI_F7898A0, static_cast<void*>(self), window);
        }
        Global<uint8_t>(addr::SC4UI_DepartmentDialogBusy) = 0;
    }
    if (bar)
        Release(bar);
}

// The game's 0x0078BCA0 (ret 0x14; vtable slot 4; the list's entry
// 0x0078BE90 is a label inside it). `this` is the dialog. The window's
// messages: the start (0xA2BF8AD0) and stop (0xA2BF8AD1) passed on to
// 0x007876B0 and 0x0077E440, a key (5) that closes the dialog (escape, or
// enter or F4 with modifier 4), and the notices (3) of a click, a deal's
// slider and a funding bar. The last argument is not used.
uint8_t __fastcall WindowProc(char* self, void*, void* window, uint32_t message, uint32_t a, uint32_t b, uint32_t)
{
    if (message == 0xA2BF8AD1)
        return This<uint8_t>(addr::SC4UI_F77E440, static_cast<void*>(self), window);
    if (message == 0xA2BF8AD0)
        return This<uint8_t>(addr::SC4UI_F7876B0, static_cast<void*>(self), window);

    if (message == 5) {
        if (a == 0xD || a == 0x73) {
            if (!(static_cast<uint8_t>(b) & 4))
                return 0;
        } else if (a != 0x1B) {
            return 0;
        }
        DropPane(self, window);
        CloseWindow(self, window);
        return 1;
    }

    if (message == 3) {
        if (a == 0x287259F6)
            return OnCommand(self, nullptr, window, b);
        if (a == 0x887113A4)
            OnFundingBar(self, window, b);
        else if (a == 0xE8711255)
            OnDealSlider(self, window, b);
    }
    return 0;
}

bool InstallDepartment4(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[2] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F78B120, reinterpret_cast<void*>(&OnCommand)},
        {addr::SC4UI_F78BCA0, reinterpret_cast<void*>(&WindowProc)},
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

const bool g_department4Registered = registry::Register({
    "ui.department.part4",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F78B120,
    "Our department dialog, its clicks and messages: the buttons' commands, the keys that close it, a deal's "
    "slider and a funding bar moved (docs/ui.md)",
    false,
    InstallDepartment4,
});

} // namespace
} // namespace opensc4::ui
