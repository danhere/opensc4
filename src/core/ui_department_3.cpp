// The budget panel's department dialog, the function between 0x007898A0 and
// 0x0078B120.
//
// The dialog (0x140 bytes, vtable 0x00AB5494, constructor 0x007862B0) shows,
// when its number at +0x20 is neither 1 (the ordinances) nor 2 (the neighbour
// deals) and its flag at +0x24 is clear, the departments of one budget group:
// a list of what the group earns and a list of what it costs, nine rows on
// show each. A department heads its line items; in the cost list a
// department's row has a funding slider, a marker of the highest funding
// allowed and a button that opens its buildings. This file holds the one
// function that fills that page (0x007898A0; the list's entry 0x00789DD0 is a
// label inside it). The command handler calls it after the scroll arrows, the
// window procedure after a slider has moved and the department's funding has
// been set.
//
// Replacement, off by default:
//
//   ui.department.part3: the page of a budget group's departments filled.
//
// Windows, texts, sliders, the budget simulator, its departments and their
// line items are the game's, called through their vtables; the dialog's other
// methods are in the neighbouring parts and in ui_windows_*.cpp.
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
constexpr std::size_t kBack = 0x10;         // the window the background strips and the row texts are on
constexpr std::size_t kGroup = 0x20;        // the budget group on show (1 and 2 are the other pages)
constexpr std::size_t kSliders = 0x2C;      // a vector of {slider's window ID, department budget}
constexpr std::size_t kSlidersEnd = 0x30;
constexpr std::size_t kSlidersCapacity = 0x34;
constexpr std::size_t kRowHeight = 0x88;
constexpr std::size_t kTotal = 0x90;        // int64: the total of the list being filled
constexpr std::size_t kFirstTop = 0x98;     // where the first list's rows begin
constexpr std::size_t kSecondTop = 0x9C;    // and the second list's
constexpr std::size_t kMultiplier = 0xB0;   // int64: 1, or more for amounts by year
constexpr std::size_t kIncomeScroll = 0x11C; // the first row on show of the list that pays
constexpr std::size_t kCostScroll = 0x120;  // and of the list that costs
constexpr std::size_t kIncomeRows = 0x124;  // how many rows the paying list has
constexpr std::size_t kCostRows = 0x128;    // how many the costing one
constexpr std::size_t kFilling = 0x12C;     // byte: set while this function runs
} // namespace dialog

constexpr uint32_t kTextIid = 0x212CDC1F;
constexpr uint32_t kSliderIid = 0x21325207;
constexpr uint32_t kValueParam = 0x4BFEE088;
constexpr uint32_t kHeadingFont = 0xEA85D306;
constexpr uint32_t kRowFont = 0xEA85D305;
constexpr uint32_t kFirstRowText = 0x0ABCDE01; // the row texts' window IDs count up from here, three a row
constexpr uint32_t kFirstSlider = 0x0ABCDF00;  // a cost row's slider
constexpr uint32_t kFirstMarker = 0x0ABCE200;  // its marker of the highest funding allowed
constexpr uint32_t kFirstButton = 0x0ABCE600;  // its button to the department's buildings
constexpr uint32_t kRowsShown = 9;

// The budget groups whose departments are left out while they have nothing
// to show.
constexpr uint32_t kGroupA = 0x6A357B96;
constexpr uint32_t kGroupB = 0xEA597195;
constexpr uint32_t kGroupC = 0x0A5A72D1;

// The purposes a department's buildings are funded under.
constexpr uint32_t kHealth = 0xCA565486;
constexpr uint32_t kSchools = 0xEA5654B6;
constexpr uint32_t kPolice = 0x0A567BAA;
constexpr uint32_t kFire = 0xEA567BC3;
constexpr uint32_t kPower = 0xCA58E540;

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

// A vector of references as the budget simulator fills it.
struct RefVector {
    void** begin;
    void** end;
    void** capacity;
};

// One budget group as the budget simulator's slot 29 lists it.
struct BudgetGroup {
    uint32_t id;
    GameString name;
    char rest[0x10];
};
static_assert(sizeof(BudgetGroup) == 0x28);

struct GroupVector {
    BudgetGroup* begin;
    BudgetGroup* end;
    BudgetGroup* capacity;
};

// One entry of the dialog's vector at +0x2C.
struct SliderEntry {
    uint32_t id;
    void* department;
};
static_assert(sizeof(SliderEntry) == 8);

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

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

// Eight bytes holding an empty text.
char* EmptyBuffer()
{
    char* const buffer = Cdecl<char*>(addr::SC4_stl_allocate, uint32_t{8});
    *buffer = 0;
    return buffer;
}

// The game's inlined empty string: eight bytes of buffer.
void MakeEmpty(GameString& text)
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

void DestroyString(GameString& text)
{
    char* const begin = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (begin)
        Free(begin);
}

uint32_t Count(const RefVector& list)
{
    return static_cast<uint32_t>((reinterpret_cast<char*>(list.end) - reinterpret_cast<char*>(list.begin)) >> 2);
}

// Every reference of the list dropped and the list emptied.
void ReleaseAll(RefVector& list)
{
    for (uint32_t i = 0; i < Count(list); ++i)
        Release(list.begin[i]);
    list.end = list.begin;
}

void* Back(char* self)
{
    return At<void*>(self, dialog::kBack);
}

void* Child(void* window, uint32_t id)
{
    return Virtual<void*>(window, 0x8C, id);
}

void SetText(void* window, GameString* text)
{
    Virtual<void>(window, 0x128, text);
}

void SetFlag(void* window, uint32_t flag, uint32_t on)
{
    Virtual<void>(window, 0x110, flag, on);
}

void SetBlank(void* window)
{
    GameString empty;
    MakeEmpty(empty);
    SetText(window, &empty);
    DestroyString(empty);
}

int64_t Scaled(char* self, int64_t amount)
{
    return static_cast<int64_t>(static_cast<uint64_t>(amount) * At<uint64_t>(self, dialog::kMultiplier));
}

void AddToTotal(char* self, int64_t amount)
{
    At<uint64_t>(self, dialog::kTotal) += static_cast<uint64_t>(Scaled(self, amount));
}

bool LeftOutWhenEmpty(char* self)
{
    const uint32_t group = At<uint32_t>(self, dialog::kGroup);
    return group == kGroupA || group == kGroupB || group == kGroupC;
}

// The text control with this ID on the background fetched into `holder`, and
// given a font when it is there.
void Style(void* back, uint32_t id, void*& holder, uint32_t font)
{
    if (Virtual<uint8_t>(back, 0x90, id, kTextIid, &holder)) {
        void* const fonts = Cdecl<void*>(addr::SC4_Singleton913C72);
        void* const control = holder;
        Virtual<void>(control, 0x48, Virtual<void*>(fonts, 0x14, font));
    }
}

// The same with a holder that is in use: it is emptied first.
void Restyle(char* self, uint32_t id, void*& holder, uint32_t font)
{
    void* const old = holder;
    void* const back = Back(self);
    if (old) {
        holder = nullptr;
        Release(old);
    }
    Style(back, id, holder, font);
}

// A department's row, the same in both lists but for the slot the amount
// comes from (16 its income, 15 its expenses): the name, the amount times the
// multiplier (asked for a second time for the total), and the third text
// hidden. Uses three window IDs; `style` holds the amount's text control
// afterwards.
void HeadingRow(char* self, void* department, std::size_t amountSlot, uint32_t& next, GameString& name,
                GameString& text, void*& style)
{
    Virtual<void>(department, 0x10, &name);
    SetText(Child(Back(self), next), &name);
    style = nullptr;
    Style(Back(self), next, style, kHeadingFont);
    ++next;

    Cdecl<void>(addr::SC4_FormatMoney, Scaled(self, Virtual<int64_t>(department, amountSlot)), &text);
    AddToTotal(self, Virtual<int64_t>(department, amountSlot));
    SetText(Child(Back(self), next), &text);
    Restyle(self, next, style, kHeadingFont);
    ++next;

    SetFlag(Child(Back(self), next), 1, 0);
    ++next;
}

// A line item's row, the same in both lists but for the slot the amount comes
// from (14 its income, 11 its current expenses): the name, the amount times
// the multiplier, and the item's second figure, shown when the item's display
// flag 2 is set. Uses three window IDs; `style` holds the amount's text
// control afterwards.
void LineRow(char* self, void* item, std::size_t amountSlot, uint32_t& next, GameString& name, GameString& text,
             void*& style)
{
    Virtual<void>(item, 0x18, &name);
    SetText(Child(Back(self), next), &name);
    style = nullptr;
    Style(Back(self), next, style, kRowFont);
    ++next;

    Cdecl<void>(addr::SC4_FormatMoney, Scaled(self, Virtual<int64_t>(item, amountSlot)), &text);
    SetText(Child(Back(self), next), &text);
    Restyle(self, next, style, kRowFont);
    ++next;

    Cdecl<void>(addr::cRZBaseString_Sprintf, &text, "%d", Virtual<int64_t>(item, 0x20));
    SetText(Child(Back(self), next), &text);
    const uint32_t flags = Virtual<uint32_t>(item, 0x44);
    SetFlag(Child(Back(self), next), 1, (flags & 2) ? uint32_t{1} : uint32_t{0});
    ++next;
}

// Three blank texts: the row after the last when a list has room left.
void BlankRow(char* self, uint32_t& next)
{
    for (int i = 0; i < 3; ++i) {
        void* const child = Child(Back(self), next);
        ++next;
        SetBlank(child);
    }
}

void ConstructEntry(SliderEntry* to, uint32_t id, void* department)
{
    to->id = id;
    to->department = department;
    if (department)
        AddRef(department);
}

// The game's inlined push_back on the vector at +0x2C.
void PushEntry(char* self, uint32_t id, void* department)
{
    SliderEntry* const end = At<SliderEntry*>(self, dialog::kSlidersEnd);
    if (end != At<SliderEntry*>(self, dialog::kSlidersCapacity)) {
        if (end)
            ConstructEntry(end, id, department);
        At<SliderEntry*>(self, dialog::kSlidersEnd) += 1;
        return;
    }

    const uint32_t size = static_cast<uint32_t>(
        (reinterpret_cast<char*>(end) - reinterpret_cast<char*>(At<SliderEntry*>(self, dialog::kSliders))) >> 3);
    const uint32_t capacity = (size < 1 ? uint32_t{1} : size) + size;
    SliderEntry* const memory =
        capacity != 0 ? Cdecl<SliderEntry*>(addr::SC4_stl_allocate, capacity * 8) : nullptr;
    SliderEntry* to = memory;
    for (const SliderEntry* from = At<SliderEntry*>(self, dialog::kSliders); from != end; ++from, ++to) {
        if (to)
            ConstructEntry(to, from->id, from->department);
    }
    if (to)
        ConstructEntry(to, id, department);
    ++to;

    SliderEntry* const oldEnd = At<SliderEntry*>(self, dialog::kSlidersEnd);
    for (SliderEntry* old = At<SliderEntry*>(self, dialog::kSliders); old != oldEnd; ++old) {
        if (old->department)
            Release(old->department);
    }
    if (At<SliderEntry*>(self, dialog::kSliders))
        Free(At<SliderEntry*>(self, dialog::kSliders));
    At<SliderEntry*>(self, dialog::kSliders) = memory;
    At<SliderEntry*>(self, dialog::kSlidersEnd) = to;
    At<SliderEntry*>(self, dialog::kSlidersCapacity) = memory + capacity;
}

// The department kept on the child window `id` of `window` as its parameter
// 0x4BFEE088 (the dialog's 0x007795A0, inlined in the original).
void KeepDepartment(void* window, uint32_t id, void* department)
{
    void* const child = Child(window, id);
    if (child) {
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x14});
        void* const variant = memory ? This<void*>(addr::SC4ScriptNumber_FromVoidPtr, memory, department) : nullptr;
        Virtual<void>(child, 0x1C8, kValueParam, variant);
    }
}

// A fraction as a slider takes it: times a hundred, cut to a whole number.
int32_t Percent(const float* fraction)
{
    return static_cast<int32_t>(static_cast<int64_t>(static_cast<double>(*fraction) * static_cast<double>(100.0f)));
}

// Whether the department has buildings of its own to fund: the purpose asked
// for goes by the department's ID; other departments have none.
uint8_t HasBuildings(void* department)
{
    uint32_t purpose;
    switch (Virtual<uint32_t>(department, 0xC)) {
    case 0x09188F42:
    case 0xAA538CB3:
        purpose = kHealth;
        break;
    case 0x09188F4C:
    case 0x4A538CC6:
        purpose = kSchools;
        break;
    case 0x28F55A9F:
        purpose = kFire;
        break;
    case 0xA2963983:
    case 0xA2963984:
        purpose = kPolice;
        break;
    case 0x8910BC8A:
        purpose = kPower;
        break;
    default:
        return 0;
    }
    return Virtual<uint8_t>(department, 0x84, purpose);
}

// The game's 0x007898A0 (it runs to 0x0078B119; the list's entry 0x00789DD0
// is inside it). `this` is the department dialog; `window` is the window the
// sliders, markers and buttons are on. Fills the page of the budget group at
// +0x20; returns 0 at once when a fill is already under way (the byte at
// +0x12C), else 1.
//
// The group's departments are asked for from the budget simulator (the first
// group's when the dialog's is not among the simulator's groups), and the
// vector at +0x2C is emptied. Then each list that has rows (first what pays,
// then what costs) is gone through department by department: a department
// with a line item of the list's type (1 pays, 0 costs) gets a heading row
// before the first such item, with its name and its total, and every such
// item that is to be shown (ID up to 100, or an amount other than 0; display
// flag 1) a row with its name, amount and second figure. Up to nine rows are
// filled from the list's scroll position on, on the background's texts
// (window IDs counting up from 0x0ABCDE03, three a row). A department's total
// counts towards the list's total whether its row is on show or not; the
// total goes to the text after the rows.
//
// Every row of the cost list also takes one window ID each from three runs on
// `window` and an entry {slider's ID, department} in the vector at +0x2C. On
// a heading row the slider gets its range (0 to the highest funding allowed,
// in percent) and the department's funding, the marker is moved to where the
// highest funding is, and the button is shown when the department has
// buildings to fund; a department with fixed funding has slider and marker
// hidden. On a line item's row all three are hidden. The button's window
// keeps the department either way.
//
// The two lists differ in small ways that are kept: the paying list counts a
// row that is not on show as shown and an item without display flag 1 as
// seen, the costing list does neither; the paying list moves its top by a row
// when the smaller of its row count and nine is even, the costing list when
// its row count is.
uint8_t __fastcall FillDepartments(char* self, void*, void* window)
{
    // Seven empty strings in the original; only three are used. The other
    // four are left as their buffers.
    RefVector items = {};
    char* const spareA = EmptyBuffer();
    uint32_t slider = kFirstSlider;
    uint32_t marker = kFirstMarker;
    uint32_t button = kFirstButton;
    GameString name;
    MakeEmpty(name);
    GameString line;
    MakeEmpty(line);
    char* const spareB = EmptyBuffer();
    char* const spareC = EmptyBuffer();
    char* const spareD = EmptyBuffer();
    GameString text;
    MakeEmpty(text);

    if (At<uint8_t>(self, dialog::kFilling) != 0) {
        DestroyString(text);
        Free(spareD);
        Free(spareC);
        Free(spareB);
        DestroyString(line);
        DestroyString(name);
        Free(spareA);
        if (items.begin)
            Free(items.begin);
        return 0;
    }
    At<uint8_t>(self, dialog::kFilling) = 1;

    GroupVector groups = {};
    Virtual<void>(Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0), 0x74, &groups);
    const uint32_t groupCount = static_cast<uint32_t>(
        static_cast<int32_t>(reinterpret_cast<char*>(groups.end) - reinterpret_cast<char*>(groups.begin)) / 0x28);
    uint32_t found = 0;
    if (groupCount != 0) {
        const uint32_t wanted = At<uint32_t>(self, dialog::kGroup);
        for (uint32_t i = 0; i < groupCount; ++i) {
            if (groups.begin[i].id == wanted) {
                found = i;
                break;
            }
        }
    }
    uint32_t incomeRows = At<uint32_t>(self, dialog::kIncomeRows);
    if (incomeRows > kRowsShown)
        incomeRows = kRowsShown;

    RefVector departments = {};
    // With no groups at all the original reads the ID through a null pointer.
    Virtual<void>(Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0), 0x7C, groups.begin[found].id, &departments);
    This<void*>(addr::SC4UI_F77C420, self + dialog::kSliders, At<void*>(self, dialog::kSliders),
                At<void*>(self, dialog::kSlidersEnd));

    uint32_t next = kFirstRowText;

    if (At<uint32_t>(self, dialog::kIncomeRows) != 0) {
        next = kFirstRowText + 2;
        At<uint64_t>(self, dialog::kTotal) = 0;
        uint32_t seen = 0;
        uint32_t shown = 0;
        for (uint32_t i = 0; i < Count(departments); ++i) {
            bool headed = false;
            Virtual<void>(departments.begin[i], 0x54, &items);
            for (uint32_t j = 0; j < Count(items); ++j) {
                if (Virtual<uint32_t>(items.begin[j], 0x10) != 1)
                    continue;
                if (!headed &&
                    (!LeftOutWhenEmpty(self) || Virtual<int64_t>(departments.begin[i], 0x40) != 0)) {
                    headed = true;
                    if (seen >= At<uint32_t>(self, dialog::kIncomeScroll) && shown < kRowsShown) {
                        void* style = nullptr;
                        HeadingRow(self, departments.begin[i], 0x40, next, name, text, style);
                        ++shown;
                        if (style)
                            Release(style);
                    } else {
                        AddToTotal(self, Virtual<int64_t>(departments.begin[i], 0x40));
                    }
                    ++seen;
                }
                if (Virtual<uint32_t>(items.begin[j], 0xC) > 100 && Virtual<int64_t>(items.begin[j], 0x38) == 0)
                    continue;
                if (Virtual<uint32_t>(items.begin[j], 0x44) & 1) {
                    if (seen >= At<uint32_t>(self, dialog::kIncomeScroll) && shown < kRowsShown) {
                        void* style = nullptr;
                        LineRow(self, items.begin[j], 0x38, next, line, text, style);
                        if (style)
                            Release(style);
                    }
                    // Counted as shown whether it was or not.
                    ++shown;
                }
                ++seen;
            }
            ReleaseAll(items);
        }
        if ((incomeRows & 1) == 0)
            At<int32_t>(self, dialog::kFirstTop) += At<int32_t>(self, dialog::kRowHeight);
        if (At<uint32_t>(self, dialog::kIncomeRows) >= kRowsShown && shown < kRowsShown)
            BlankRow(self, next);
        ++next;
        Cdecl<void>(addr::SC4_FormatMoney, At<int64_t>(self, dialog::kTotal), &text);
        SetText(Child(Back(self), next), &text);
        ++next;
    }

    if (At<uint32_t>(self, dialog::kCostRows) != 0) {
        next += 2;
        At<uint64_t>(self, dialog::kTotal) = 0;
        uint32_t seen = 0;
        uint32_t shown = 0;
        for (uint32_t i = 0; i < Count(departments); ++i) {
            bool headed = false;
            Virtual<void>(departments.begin[i], 0x54, &items);
            for (uint32_t j = 0; j < Count(items); ++j) {
                if (Virtual<uint32_t>(items.begin[j], 0x10) != 0)
                    continue;
                if (!headed &&
                    (!LeftOutWhenEmpty(self) || Virtual<int64_t>(departments.begin[i], 0x38) != 0)) {
                    headed = true;
                    if (seen >= At<uint32_t>(self, dialog::kCostScroll) && shown < kRowsShown) {
                        void* style = nullptr;
                        HeadingRow(self, departments.begin[i], 0x3C, next, name, text, style);

                        void* const kept = departments.begin[i];
                        if (kept)
                            AddRef(kept);
                        PushEntry(self, slider, kept);

                        // The original's holder is not cleared before the query.
                        void* control = nullptr;
                        if (Virtual<uint8_t>(Child(window, slider), 0x0, kSliderIid, &control)) {
                            Virtual<void>(control, 0x18, int32_t{0});
                            Virtual<void>(control, 0x20, Percent(Virtual<float*>(departments.begin[i], 0x30)));
                            Virtual<void>(control, 0x14, Percent(Virtual<float*>(departments.begin[i], 0x28)));
                            Release(control);
                            control = nullptr;
                        }

                        if (!Virtual<uint8_t>(departments.begin[i], 0x20)) {
                            void* const mark = Child(window, marker);
                            SetFlag(mark, 1, 1);
                            const int32_t top = Virtual<int32_t*>(mark, 0xC0)[1];
                            const double highest =
                                static_cast<double>(*Virtual<float*>(departments.begin[i], 0x30)) *
                                static_cast<double>(100.0f);
                            const double x = static_cast<double>(11000.0f) / highest;
                            const double moved = x + static_cast<double>(260.0f);
                            const double placed = moved - static_cast<double>(12.0f);
                            Virtual<void>(mark, 0xE0, static_cast<int32_t>(static_cast<int64_t>(placed)), top);
                            SetFlag(Child(window, slider), 1, 1);
                        } else {
                            SetFlag(Child(window, marker), 1, 0);
                            SetFlag(Child(window, slider), 1, 0);
                        }

                        // The original passes a dword whose low byte is this
                        // answer and whose other bytes are left over on its
                        // stack; the window reads the byte.
                        const uint8_t buildings = HasBuildings(departments.begin[i]);
                        SetFlag(Child(window, button), 1, buildings);
                        KeepDepartment(window, button, departments.begin[i]);

                        ++marker;
                        ++slider;
                        ++button;
                        ++shown;
                        if (kept)
                            Release(kept);
                        if (style)
                            Release(style);
                    } else {
                        AddToTotal(self, Virtual<int64_t>(departments.begin[i], 0x3C));
                    }
                    ++seen;
                }
                if (Virtual<uint32_t>(items.begin[j], 0xC) > 100 && Virtual<int64_t>(items.begin[j], 0x28) == 0)
                    continue;
                if (!(Virtual<uint32_t>(items.begin[j], 0x44) & 1))
                    continue;
                if (seen >= At<uint32_t>(self, dialog::kCostScroll) && shown < kRowsShown) {
                    if (line.body.begin != line.body.end) {
                        *line.body.begin = 0;
                        line.body.end = line.body.begin;
                    }
                    void* style = nullptr;
                    LineRow(self, items.begin[j], 0x2C, next, line, text, style);

                    void* const kept = departments.begin[i];
                    if (kept)
                        AddRef(kept);
                    PushEntry(self, slider, kept);

                    SetFlag(Child(window, marker), 1, 0);
                    SetFlag(Child(window, slider), 1, 0);
                    SetFlag(Child(window, button), 1, 0);
                    KeepDepartment(window, button, departments.begin[i]);

                    ++marker;
                    ++slider;
                    ++button;
                    ++shown;
                    if (kept)
                        Release(kept);
                    if (style)
                        Release(style);
                }
                ++seen;
            }
            ReleaseAll(items);
        }
        const uint32_t costRows = At<uint32_t>(self, dialog::kCostRows);
        if ((costRows & 1) == 0)
            At<int32_t>(self, dialog::kSecondTop) += At<int32_t>(self, dialog::kRowHeight);
        if (costRows >= kRowsShown && shown < kRowsShown) {
            BlankRow(self, next);
            SetFlag(Child(window, marker), 1, 0);
            SetFlag(Child(window, slider), 1, 0);
        }
        Cdecl<void>(addr::SC4_FormatMoney, At<int64_t>(self, dialog::kTotal), &text);
        ++next;
        SetText(Child(Back(self), next), &text);
    }

    ReleaseAll(departments);
    At<uint8_t>(self, dialog::kFilling) = 0;
    if (departments.begin)
        Free(departments.begin);

    for (BudgetGroup* group = groups.begin; group != groups.end; ++group)
        DestroyString(group->name);
    if (groups.begin)
        Free(groups.begin);

    DestroyString(text);
    Free(spareD);
    Free(spareC);
    Free(spareB);
    DestroyString(line);
    DestroyString(name);
    Free(spareA);
    if (items.begin)
        Free(items.begin);
    return 1;
}

bool InstallDepartment3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[2] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F7898A0, reinterpret_cast<void*>(&FillDepartments)},
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

const bool g_department3Registered = registry::Register({
    "ui.department.part3",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F7898A0,
    "Our department dialog of the budget panel, the page of a budget group's departments filled: rows, totals, "
    "funding sliders (docs/ui.md)",
    false,
    InstallDepartment3,
});

} // namespace
} // namespace opensc4::ui
