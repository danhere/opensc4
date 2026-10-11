// The budget panel's department dialog: its start (0x007876B0 to 0x007898A0,
// one function).
//
// The dialog (0x140 bytes, vtable 0x00AB5494, constructor 0x007862B0) gets
// window message 0xA2BF8AD0 once its window is up, and its window procedure
// then calls this function with the window. It builds the page for the
// dialog's kind (+0x20):
//
//   - 1: the ordinances (0x0077C660);
//   - 2: the neighbour deals (0x0077E600);
//   - any other number is a budget group. With +0x24 set the page is built by
//     0x00786690 (the group's buildings with their own funding). Otherwise
//     the page is built here: the group's departments from the budget
//     simulator, the ones that pay in a first list and the ones that cost in
//     a second, up to nine rows each. A department gets a heading row with
//     its name and total and, in the second list, an information button, a
//     funding control and a mark for the most it may get; under it a row for
//     each line item on show. Each list ends with a total, and two buttons
//     and four scroll arrows go under the lists. With nothing to list the
//     page is a single message (0x0077BEC0).
//
// Replacement, off by default:
//
//   ui.department.part2: the start.
//
// Windows, controls, pictures, texts and the budget simulator are the game's,
// called through their vtables or by address; the page builders and control
// makers it calls are in ui_windows_2.cpp to ui_windows_6.cpp and
// ui_department_1.cpp. The list's entry 0x007880F0 is a label inside the
// function, not a start.
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
constexpr std::size_t kStarted = 0x08;       // byte: the page is built
constexpr std::size_t kBack = 0x10;          // the window the background strips and the row texts are on
constexpr std::size_t kKind = 0x20;          // 1 ordinances, 2 neighbour deals, else a budget group
constexpr std::size_t kByBuilding = 0x24;    // nonzero: the page lists buildings (0x00786690)
constexpr std::size_t kByYear = 0x28;        // nonzero: amounts by year
constexpr std::size_t kControls = 0x2C;      // vector of {funding control's window ID, department}
constexpr std::size_t kControlsEnd = 0x30;
constexpr std::size_t kShares = 0x38;        // vector of {float funding share, department}
constexpr std::size_t kSharesEnd = 0x3C;
constexpr std::size_t kSharesCapacity = 0x40;
constexpr std::size_t kHeight = 0x80;        // the background's height
constexpr std::size_t kWidth = 0x84;
constexpr std::size_t kRowHeight = 0x88;
constexpr std::size_t kTotal = 0x90;         // int64: the total of the list being filled
constexpr std::size_t kFirstTop = 0x98;      // where the next row of the first list goes
constexpr std::size_t kSecondTop = 0x9C;     // and of the second list
constexpr std::size_t kMultiplier = 0xB0;    // int64: 1, or 12 for amounts by year
constexpr std::size_t kIncomeHeading = 0xB8; // game strings from here on
constexpr std::size_t kIncomeAmount = 0xCC;
constexpr std::size_t kCostHeading = 0xE0;
constexpr std::size_t kCostAmount = 0xF4;
constexpr std::size_t kTotalLabel = 0x108;
constexpr std::size_t kIncomeRows = 0x124;   // rows wanted in the first list, headings included
constexpr std::size_t kCostRows = 0x128;     // and in the second
} // namespace dialog

constexpr uint32_t kWinGenClass = 0x4386D516;
constexpr uint32_t kWinGenIid = 0x5386D516;
constexpr uint32_t kTextGroup = 0x6A3FF01C;
constexpr uint32_t kUiGroup = 0x46A006B0;
constexpr uint32_t kPngType = 0x856DDBAC;
constexpr uint32_t kValueParam = 0x4BFEE088;

constexpr uint32_t kPageTitleStyle = 0x4A809916;
constexpr uint32_t kTitleStyle = 0xEA85D304;
constexpr uint32_t kItemStyle = 0xEA85D305;
constexpr uint32_t kHeadingStyle = 0xEA85D306;

constexpr uint32_t kMaxRows = 9;

// The purposes whose buildings take a funding of their own.
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

// A string from the text resources: 0x20 bytes, a string first.
struct LocalizedText {
    GameString text;
    uint32_t spare[3];
};
static_assert(sizeof(LocalizedText) == 0x20);

// The holder of an image found by key.
struct ImageLoader {
    std::uintptr_t vtable;
    void* image;
    uint32_t key[3];
    GameString name;
    uint32_t mode;
    uint32_t flag;
};
static_assert(sizeof(ImageLoader) == 0x30);

struct PointerVector {
    void** begin;
    void** end;
    void** capacity;
};

// One budget group as the budget simulator lists them.
struct BudgetGroup {
    uint32_t id;
    GameString name;
    uint32_t rest[4];
};
static_assert(sizeof(BudgetGroup) == 0x28);

struct GroupVector {
    BudgetGroup* begin;
    BudgetGroup* end;
    BudgetGroup* capacity;
};

// An entry of the vectors at +0x2C and +0x38: a number and a department.
struct DepartmentEntry {
    uint32_t number;
    void* department;
};
static_assert(sizeof(DepartmentEntry) == 8);

// The window IDs the rows count up.
struct Ids {
    uint32_t text = 0x0ABCDE01;    // on the background: names and amounts
    uint32_t control = 0x0ABCDF00; // the funding controls
    uint32_t button = 0x0ABCE600;  // the information buttons
    uint32_t mark = 0x0ABCE200;    // the marks of the most a department may get
};

// What the function keeps on its stack from start to end.
struct Scratch {
    GameString money;
    PointerVector items;
    GameString title;
    GameString departmentName;
    GameString incomeItemName;
    GameString costItemName;
    GameString spareA;
    GameString spareB;
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

void Free(void* memory)
{
    Cdecl<void>(addr::SC4_Free, memory);
}

// The game's inlined string constructor.
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

void DestroyString(GameString& text)
{
    char* const begin = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (begin)
        Free(begin);
}

void* MakeText(LocalizedText& text, uint32_t group, uint32_t instance)
{
    return This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, group, instance, uint32_t{1});
}

// A string made from the buffer of a text resource, as the original inlines
// it.
void CopyText(GameString& text, void* made)
{
    uint8_t tag = 0;
    void* const source = made ? static_cast<void*>(static_cast<char*>(made) + 4) : nullptr;
    This<void>(addr::SC4String_CopyBuffer, &text.body, source, uint32_t{0}, uint32_t{0xFFFFFFFF}, &tag);
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
}

// One of the dialog's own strings set from a text resource.
void SetOwnText(char* self, std::size_t offset, uint32_t instance)
{
    LocalizedText text;
    void* const made = MakeText(text, kTextGroup, instance);
    This<void>(addr::cRZBaseString_Assign, self + offset, made);
    DestroyString(text.text);
}

uint32_t Count(const PointerVector& vector)
{
    return static_cast<uint32_t>(static_cast<int32_t>(reinterpret_cast<char*>(vector.end) -
                                                      reinterpret_cast<char*>(vector.begin)) >>
                                 2);
}

// Every object of the vector released and the vector emptied.
void ReleaseAll(PointerVector& vector)
{
    for (uint32_t i = 0; i < Count(vector); ++i)
        Release(vector.begin[i]);
    vector.end = vector.begin;
}

// The three budget groups whose departments are left out when they have
// nothing to show.
bool IsSparseGroup(uint32_t group)
{
    return group == 0x6A357B96 || group == 0xEA597195 || group == 0x0A5A72D1;
}

int64_t Scaled(char* self, int64_t amount)
{
    return static_cast<int64_t>(static_cast<uint64_t>(amount) * At<uint64_t>(self, dialog::kMultiplier));
}

void AddToTotal(char* self, int64_t amount)
{
    At<uint64_t>(self, dialog::kTotal) += static_cast<uint64_t>(Scaled(self, amount));
}

// The x87 conversion the game calls (truncation).
int32_t Truncate(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// The dialog's 0x00779660: a text made at (x, y) on `parent`.
void AddText(char* self, void* parent, uint32_t id, int32_t x, int32_t y, void* text, uint32_t align, uint32_t style,
             uint32_t red, uint32_t green, uint32_t blue)
{
    This<void>(addr::SC4UI_F779660, self, parent, id, x, y, text, align, style, red, green, blue);
}

// The dialog's 0x0077B960: a button made at (x, y) on `parent`.
void AddButton(char* self, void* parent, uint32_t id, int32_t x, int32_t y, uint32_t image, uint32_t toggle)
{
    This<void>(addr::SC4UI_F77B960, self, parent, id, x, y, image, uint32_t{0}, toggle);
}

void* Child(void* window, uint32_t id)
{
    return Virtual<void*>(window, 0x8C, id);
}

// A child window shown or hidden.
void Show(void* window, uint32_t id, uint32_t shown)
{
    Virtual<void>(Child(window, id), 0x110, uint32_t{1}, shown);
}

// A line item counts as a row when its flag 1 is set and its ID is no more
// than 100 or its amount (slot `amount`) is not zero.
bool IsRow(void* item, std::size_t amount)
{
    if (Virtual<uint32_t>(item, 0xC) > 0x64 && Virtual<int64_t>(item, amount) == 0)
        return false;
    return (Virtual<uint32_t>(item, 0x44) & 1) != 0;
}

// The first pass: every department of the group gets an entry {funding
// share, department} in the vector at +0x38, and the rows of both lists are
// counted. A department counts once in the first list when it has a paying
// line item (type 1) and once in the second when it has any other; in the
// three sparse groups only if its income, or its ideal funding, is not zero.
// A line item with flag 1 counts in its list unless its ID is above 100 and
// its amount zero.
void CountRows(char* self, PointerVector& departments, PointerVector& items)
{
    for (uint32_t i = 0; i < Count(departments); ++i) {
        uint32_t paying = 0;
        uint32_t costing = 0;
        Virtual<void>(departments.begin[i], 0x54, &items); // GetAllLineItems

        DepartmentEntry share;
        share.department = nullptr;
        share.number = At<uint32_t>(Virtual<void*>(departments.begin[i], 0x28), 0); // the funding share's bits
        if (void* const department = departments.begin[i]) {
            AddRef(department);
            share.department = department;
        }
        DepartmentEntry* const end = At<DepartmentEntry*>(self, dialog::kSharesEnd);
        if (end != At<DepartmentEntry*>(self, dialog::kSharesCapacity)) {
            if (end) {
                end->number = share.number;
                end->department = share.department;
                if (share.department)
                    AddRef(share.department);
            }
            At<DepartmentEntry*>(self, dialog::kSharesEnd) += 1;
        } else {
            uint8_t tag = 0;
            This<void>(addr::SC4UI_F77D7E0, self + dialog::kShares, end, &share, &tag, uint32_t{1}, uint32_t{1});
        }

        for (uint32_t j = 0; j < Count(items); ++j) {
            if (Virtual<uint32_t>(items.begin[j], 0x10) == 1) {
                if (!paying) {
                    paying = 1;
                    if (!IsSparseGroup(At<uint32_t>(self, dialog::kKind)) ||
                        Virtual<int64_t>(departments.begin[i], 0x40) != 0)
                        ++At<uint32_t>(self, dialog::kIncomeRows);
                }
                if (Virtual<uint32_t>(items.begin[j], 0x44) & 1) {
                    if (Virtual<uint32_t>(items.begin[j], 0xC) <= 0x64 ||
                        Virtual<int64_t>(items.begin[j], 0x38) != 0)
                        ++At<uint32_t>(self, dialog::kIncomeRows);
                }
            } else {
                if (!costing) {
                    costing = 1;
                    if (!IsSparseGroup(At<uint32_t>(self, dialog::kKind)) ||
                        Virtual<int64_t>(departments.begin[i], 0x38) != 0)
                        ++At<uint32_t>(self, dialog::kCostRows);
                }
                if (Virtual<uint32_t>(items.begin[j], 0x44) & 1) {
                    if (Virtual<uint32_t>(items.begin[j], 0xC) <= 0x64 ||
                        Virtual<int64_t>(items.begin[j], 0x28) != 0)
                        ++At<uint32_t>(self, dialog::kCostRows);
                }
            }
        }
        ReleaseAll(items);
        if (share.department)
            Release(share.department);
    }
}

// A text made blank, added at (0x102, y) and hidden: the third column of a
// heading row.
void AddBlank(char* self, Ids& ids, int32_t y)
{
    GameString blank;
    InitString(blank);
    AddText(self, At<void*>(self, dialog::kBack), ids.text, 0x102, y, &blank, 0, kItemStyle, 0x44, 0x55, 0x66);
    DestroyString(blank);
    void* const made = Child(At<void*>(self, dialog::kBack), ids.text);
    ++ids.text;
    Virtual<void>(made, 0x110, uint32_t{1}, uint32_t{0});
}

// A line item's second figure printed into the third column, shown when the
// item's flag 2 is set. Both lists put it at the second list's height.
void AddSecondFigure(char* self, void* item, Ids& ids, GameString& money)
{
    const int64_t figure = Virtual<int64_t>(item, 0x20);
    Cdecl<void>(addr::cRZBaseString_Sprintf, &money, "%d", figure);
    AddText(self, At<void*>(self, dialog::kBack), ids.text, 0x102, At<int32_t>(self, dialog::kSecondTop), &money, 0,
            kItemStyle, 0x44, 0x55, 0x66);
    const uint32_t flags = Virtual<uint32_t>(item, 0x44);
    void* const made = Child(At<void*>(self, dialog::kBack), ids.text);
    ++ids.text;
    Virtual<void>(made, 0x110, uint32_t{1}, (flags & 2) ? uint32_t{1} : uint32_t{0});
}

// The first list: the departments with paying line items (type 1).
void AddIncomeRows(char* self, PointerVector& departments, Scratch& s, Ids& ids)
{
    uint32_t shown = 0;
    for (uint32_t i = 0; i < Count(departments); ++i) {
        uint32_t headed = 0;
        Virtual<void>(departments.begin[i], 0x54, &s.items);
        for (uint32_t j = 0; j < Count(s.items); ++j) {
            if (Virtual<uint32_t>(s.items.begin[j], 0x10) != 1)
                continue;
            if (!headed) {
                if (shown >= kMaxRows) {
                    // Past the ninth row the department's income is added for
                    // each of its paying items.
                    AddToTotal(self, Virtual<int64_t>(departments.begin[i], 0x40));
                } else if (!IsSparseGroup(At<uint32_t>(self, dialog::kKind)) ||
                           Virtual<int64_t>(departments.begin[i], 0x40) != 0) {
                    headed = 1;
                    Virtual<void>(departments.begin[i], 0x10, &s.departmentName);
                    AddText(self, At<void*>(self, dialog::kBack), ids.text, 0x30,
                            At<int32_t>(self, dialog::kFirstTop), &s.departmentName, 0, kHeadingStyle, 0x44, 0x55,
                            0x66);
                    ++ids.text;
                    Cdecl<void>(addr::SC4_FormatMoney, Scaled(self, Virtual<int64_t>(departments.begin[i], 0x40)),
                                &s.money);
                    AddToTotal(self, Virtual<int64_t>(departments.begin[i], 0x40));
                    AddText(self, At<void*>(self, dialog::kBack), ids.text, At<int32_t>(self, dialog::kWidth) - 0x26,
                            At<int32_t>(self, dialog::kFirstTop), &s.money, 6, kHeadingStyle, 0x00, 0x66, 0x00);
                    ++ids.text;
                    AddBlank(self, ids, At<int32_t>(self, dialog::kSecondTop));
                    At<int32_t>(self, dialog::kFirstTop) += At<int32_t>(self, dialog::kRowHeight);
                    ++shown;
                }
            }
            if (shown >= kMaxRows)
                continue;
            void* const item = s.items.begin[j];
            if (!IsRow(item, 0x38))
                continue;
            Virtual<void>(item, 0x18, &s.incomeItemName);
            AddText(self, At<void*>(self, dialog::kBack), ids.text, 0x30, At<int32_t>(self, dialog::kFirstTop),
                    &s.incomeItemName, 0, kItemStyle, 0x44, 0x55, 0x66);
            ++ids.text;
            Cdecl<void>(addr::SC4_FormatMoney, Scaled(self, Virtual<int64_t>(item, 0x38)), &s.money);
            AddText(self, At<void*>(self, dialog::kBack), ids.text, At<int32_t>(self, dialog::kWidth) - 0x26,
                    At<int32_t>(self, dialog::kFirstTop), &s.money, 6, kItemStyle, 0x00, 0x66, 0x00);
            ++ids.text;
            AddSecondFigure(self, item, ids, s.money);
            At<int32_t>(self, dialog::kFirstTop) += At<int32_t>(self, dialog::kRowHeight);
            ++shown;
        }
        ReleaseAll(s.items);
    }
}

// The mark of the most a department may get (picture 0x140155C8 on the
// dialog's window) and its funding control: a number control from 0 to that
// most, set to the department's share, both in hundredths.
void AddFundingControl(char* self, void* window, void* department, Ids& ids)
{
    const float most = At<float>(Virtual<void*>(department, 0x30), 0);
    const double x = static_cast<double>(11000.0f) / (static_cast<double>(most) * static_cast<double>(100.0f)) +
                     static_cast<double>(260.0f) - static_cast<double>(12.0f);
    This<void>(addr::SC4UI_F77A250, self, uint32_t{0x140155C8}, Truncate(x), At<int32_t>(self, dialog::kSecondTop),
               window, ids.mark);

    void* const share = Virtual<void*>(department, 0x28);
    void* const limit = Virtual<void*>(department, 0x30);
    const int32_t value = Truncate(static_cast<double>(At<float>(share, 0)) * static_cast<double>(100.0f));
    const int32_t highest = Truncate(static_cast<double>(At<float>(limit, 0)) * static_cast<double>(100.0f));
    This<void>(addr::SC4UI_F7794E0, self, window, ids.control, int32_t{0x104}, At<int32_t>(self, dialog::kSecondTop),
               int32_t{0x6E}, uint32_t{1}, uint32_t{0}, highest, value);
}

// The purpose whose buildings a department funds one by one, or 0.
uint32_t PurposeOf(uint32_t department)
{
    switch (department) {
    case 0x09188F42:
    case 0xAA538CB3:
        return kHealth;
    case 0x09188F4C:
    case 0x4A538CC6:
        return kSchools;
    case 0x28F55A9F:
        return kFire;
    case 0x8910BC8A:
        return kPower;
    case 0xA2963983:
    case 0xA2963984:
        return kPolice;
    default:
        return 0;
    }
}

// A department's heading row in the second list.
void AddCostHeading(char* self, void* window, void* department, Scratch& s, Ids& ids)
{
    Virtual<void>(department, 0x10, &s.departmentName);
    AddButton(self, window, ids.button, 0x12, At<int32_t>(self, dialog::kSecondTop), 0x140155B7, 0);

    // The department kept on the button, as 0x007795A0 does it.
    if (void* const button = Child(window, ids.button)) {
        void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x14});
        void* const variant = memory ? This<void*>(addr::SC4ScriptNumber_FromVoidPtr, memory, department) : nullptr;
        Virtual<void>(button, 0x1C8, kValueParam, variant);
    }

    AddText(self, At<void*>(self, dialog::kBack), ids.text, 0x30, At<int32_t>(self, dialog::kSecondTop),
            &s.departmentName, 0, kHeadingStyle, 0x44, 0x55, 0x66);
    ++ids.text;
    Cdecl<void>(addr::SC4_FormatMoney, Scaled(self, Virtual<int64_t>(department, 0x3C)), &s.money);
    AddToTotal(self, Virtual<int64_t>(department, 0x3C));
    AddText(self, At<void*>(self, dialog::kBack), ids.text, At<int32_t>(self, dialog::kWidth) - 0x26,
            At<int32_t>(self, dialog::kSecondTop), &s.money, 6, kHeadingStyle, 0x77, 0x00, 0x00);
    ++ids.text;
    AddBlank(self, ids, At<int32_t>(self, dialog::kSecondTop));

    AddFundingControl(self, window, department, ids);
    DepartmentEntry entry;
    entry.department = nullptr;
    entry.number = ids.control;
    if (department) {
        AddRef(department);
        entry.department = department;
    }
    This<void>(addr::SC4UI_F77E550, self + dialog::kControls, &entry);

    // A department with fixed funding shows neither the mark nor the control.
    const uint32_t adjustable = Virtual<uint8_t>(department, 0x20) ? 0 : 1;
    Show(window, ids.mark, adjustable);
    Show(window, ids.control, adjustable);

    // The button shows when the department has buildings funded one by one.
    uint8_t local = 0;
    if (const uint32_t purpose = PurposeOf(Virtual<uint32_t>(department, 0xC)))
        local = Virtual<uint8_t>(department, 0x84, purpose);
    Show(window, ids.button, local);

    ++ids.mark;
    ++ids.control;
    ++ids.button;
    At<int32_t>(self, dialog::kSecondTop) += At<int32_t>(self, dialog::kRowHeight);
    if (entry.department)
        Release(entry.department);
}

// A line item's row in the second list. Its button, mark and control are made
// like a heading's and hidden.
void AddCostItem(char* self, void* window, void* department, void* item, Scratch& s, Ids& ids)
{
    Virtual<void>(item, 0x18, &s.costItemName);
    AddButton(self, window, ids.button, 0x12, At<int32_t>(self, dialog::kSecondTop), 0x140155B7, 0);
    This<uint32_t>(addr::SC4UI_F7795A0, self, window, ids.button, department);

    AddText(self, At<void*>(self, dialog::kBack), ids.text, 0x30, At<int32_t>(self, dialog::kSecondTop),
            &s.costItemName, 0, kItemStyle, 0x44, 0x55, 0x66);
    ++ids.text;
    Cdecl<void>(addr::SC4_FormatMoney, Scaled(self, Virtual<int64_t>(item, 0x2C)), &s.money);
    AddText(self, At<void*>(self, dialog::kBack), ids.text, At<int32_t>(self, dialog::kWidth) - 0x26,
            At<int32_t>(self, dialog::kSecondTop), &s.money, 6, kItemStyle, 0x77, 0x00, 0x00);
    ++ids.text;
    AddSecondFigure(self, item, ids, s.money);

    AddFundingControl(self, window, department, ids);
    DepartmentEntry entry;
    entry.department = nullptr;
    entry.number = ids.control;
    if (department) {
        AddRef(department);
        entry.department = department;
    }
    This<void>(addr::SC4UI_F77E550, self + dialog::kControls, &entry);

    Show(window, ids.mark, 0);
    Show(window, ids.control, 0);
    Show(window, ids.button, 0);

    ++ids.mark;
    ++ids.control;
    ++ids.button;
    At<int32_t>(self, dialog::kSecondTop) += At<int32_t>(self, dialog::kRowHeight);
    if (entry.department)
        Release(entry.department);
}

// The second list: the departments with costing line items (type 0).
void AddCostRows(char* self, void* window, PointerVector& departments, Scratch& s, Ids& ids)
{
    uint32_t shown = 0;
    for (uint32_t i = 0; i < Count(departments); ++i) {
        uint32_t headed = 0;
        Virtual<void>(departments.begin[i], 0x54, &s.items);
        for (uint32_t j = 0; j < Count(s.items); ++j) {
            if (Virtual<uint32_t>(s.items.begin[j], 0x10) != 0)
                continue;
            if (!headed) {
                headed = 1;
                if (shown >= kMaxRows) {
                    AddToTotal(self, Virtual<int64_t>(departments.begin[i], 0x3C));
                } else if (!IsSparseGroup(At<uint32_t>(self, dialog::kKind)) ||
                           Virtual<int64_t>(departments.begin[i], 0x38) != 0) {
                    AddCostHeading(self, window, departments.begin[i], s, ids);
                    ++shown;
                }
            }
            if (shown >= kMaxRows)
                continue;
            void* const item = s.items.begin[j];
            if (!IsRow(item, 0x28))
                continue;
            AddCostItem(self, window, departments.begin[i], item, s, ids);
            ++shown;
        }
        ReleaseAll(s.items);
    }
}

// One of the two buttons under the lists: made by 0x0077B960, it and its
// holder (id + 0x100) sized 180 by 30, captioned, and tied to the window.
void AddBottomButton(char* self, void* window, uint32_t id, int32_t x, uint32_t group, uint32_t instance)
{
    AddButton(self, window, id, x, At<int32_t>(self, dialog::kHeight) - 0x28, 0x144161EB, 1);
    Virtual<void>(Child(window, id), 0xD4, int32_t{0xB4}, int32_t{0x1E});
    Virtual<void>(Child(window, id + 0x100), 0xD4, int32_t{0xB4}, int32_t{0x1E});

    void* const button = Child(window, id);
    LocalizedText text;
    void* const made = MakeText(text, group, instance);
    GameString caption;
    CopyText(caption, made);
    Virtual<void>(button, 0x128, &caption);
    DestroyString(caption);
    DestroyString(text.text);

    Virtual<void>(Child(window, id), 0x158, window);
}

// One of the four arrows beside the lists.
void AddArrow(char* self, void* window, uint32_t id, int32_t y, uint32_t picture)
{
    AddButton(self, window, id, At<int32_t>(self, dialog::kWidth) - 0x21, y, picture, 0);
    Virtual<void>(Child(window, id), 0x158, window);
}

void InitScratch(Scratch& s)
{
    InitString(s.money);
    s.items.begin = nullptr;
    s.items.end = nullptr;
    s.items.capacity = nullptr;
    InitString(s.title);
    InitString(s.departmentName);
    InitString(s.incomeItemName);
    InitString(s.costItemName);
    InitString(s.spareA);
    InitString(s.spareB);
}

// The scratch strings destroyed as the original inlines it.
void DestroyScratch(Scratch& s)
{
    Free(s.spareB.body.begin);
    Free(s.spareA.body.begin);
    DestroyString(s.costItemName);
    DestroyString(s.incomeItemName);
    DestroyString(s.departmentName);
    DestroyString(s.title);
    if (s.items.begin)
        Free(s.items.begin);
    DestroyString(s.money);
}

// The same through the string's destructor, as the buildings page's exit has
// it.
void DestroyScratchByCall(Scratch& s)
{
    This<void>(addr::SC4String_Destroy, &s.spareB);
    This<void>(addr::SC4String_Destroy, &s.spareA);
    This<void>(addr::SC4String_Destroy, &s.costItemName);
    This<void>(addr::SC4String_Destroy, &s.incomeItemName);
    This<void>(addr::SC4String_Destroy, &s.departmentName);
    This<void>(addr::SC4String_Destroy, &s.title);
    if (s.items.begin)
        Free(s.items.begin);
    This<void>(addr::SC4String_Destroy, &s.money);
}

// The background's width and the row height taken from a picture of the
// interface group: the picture's width, and half its height.
void MeasureFrom(char* self, ImageLoader& loader, uint32_t instance)
{
    This<void*>(addr::SC4ImageLoader_ConstructParts, &loader, kPngType, kUiGroup, instance, uint32_t{1},
                uint32_t{0});
    At<int32_t>(self, dialog::kWidth) = At<int32_t>(Virtual<void*>(loader.image, 0x30), 0x8);
    At<int32_t>(self, dialog::kRowHeight) = At<int32_t>(Virtual<void*>(loader.image, 0x30), 0xC) / 2;
}

void DestroyGroups(GroupVector& groups)
{
    for (BudgetGroup* group = groups.begin; group != groups.end; ++group)
        DestroyString(group->name);
    if (groups.begin)
        Free(groups.begin);
}

// The page of a budget group's departments (the function's last and largest
// part).
void BuildDepartmentsPage(char* self, void* window, Scratch& s)
{
    // The group's name is the page's title. With no group of the dialog's
    // kind the first group is taken; with no groups at all the original reads
    // through a null vector, and so does this.
    GroupVector groups = {nullptr, nullptr, nullptr};
    Virtual<void>(Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0), 0x74, &groups); // GetAllGroups
    const uint32_t groupCount =
        static_cast<uint32_t>(static_cast<int32_t>(reinterpret_cast<char*>(groups.end) -
                                                   reinterpret_cast<char*>(groups.begin)) /
                              0x28);
    uint32_t index = 0;
    for (uint32_t i = 0; i < groupCount; ++i) {
        if (groups.begin[i].id == At<uint32_t>(self, dialog::kKind)) {
            index = i;
            break;
        }
    }
    const std::uintptr_t nameAt = reinterpret_cast<std::uintptr_t>(groups.begin) + index * 0x28 + 4;
    StringBody* const name = nameAt ? reinterpret_cast<StringBody*>(nameAt + 4) : nullptr;
    if (name != &s.title.body) {
        uint8_t tag = 0;
        This<void>(addr::SC4String_AssignRange, &s.title.body, name->begin, name->end, &tag);
    }

    PointerVector departments = {nullptr, nullptr, nullptr};
    Virtual<void>(Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0), 0x7C, groups.begin[index].id,
                  &departments); // GetDepartmentBudgetsInGroup
    This<void>(addr::SC4UI_F77C420, self + dialog::kShares, At<void*>(self, dialog::kShares),
               At<void*>(self, dialog::kSharesEnd));

    CountRows(self, departments, s.items);

    ImageLoader loader;
    MeasureFrom(self, loader, 0x140155D2);

    uint32_t incomeRows = At<uint32_t>(self, dialog::kIncomeRows);
    if (incomeRows == 0 && At<uint32_t>(self, dialog::kCostRows) == 0) {
        LocalizedText nothing;
        MakeText(nothing, kTextGroup, 0x4A5CB4B6);
        This<void>(addr::SC4UI_F77BEC0, self, window, &s.title, &nothing);
        ReleaseAll(departments);
        At<uint8_t>(self, dialog::kStarted) = 1;
        DestroyString(nothing.text);
        This<void>(addr::SC4ImageLoader_Destruct, &loader);
        if (departments.begin)
            Free(departments.begin);
        DestroyGroups(groups);
        DestroyScratch(s);
        return;
    }

    uint32_t costRows = At<uint32_t>(self, dialog::kCostRows);
    if (incomeRows > kMaxRows)
        incomeRows = kMaxRows;
    if (costRows > kMaxRows)
        costRows = kMaxRows;
    This<void>(addr::SC4UI_F77A6F0, self, window, incomeRows, costRows);
    const int32_t firstTop = At<int32_t>(self, dialog::kFirstTop);
    const int32_t secondTop = At<int32_t>(self, dialog::kSecondTop);
    This<void>(addr::SC4UI_F77C420, self + dialog::kControls, At<void*>(self, dialog::kControls),
               At<void*>(self, dialog::kControlsEnd));
    AddText(self, At<void*>(self, dialog::kBack), 0x0ABCDE00, 0x14, 8, &s.title, 0, kPageTitleStyle, 0x44, 0x55,
            0x66);

    Ids ids;
    if (At<uint32_t>(self, dialog::kIncomeRows) != 0) {
        AddText(self, At<void*>(self, dialog::kBack), 0x0ABCDE01, 0x12,
                At<int32_t>(self, dialog::kFirstTop) - At<int32_t>(self, dialog::kRowHeight),
                self + dialog::kIncomeHeading, 0, kTitleStyle, 0x44, 0x55, 0x66);
        AddText(self, At<void*>(self, dialog::kBack), 0x0ABCDE02, At<int32_t>(self, dialog::kWidth) - 0x26,
                At<int32_t>(self, dialog::kFirstTop) - At<int32_t>(self, dialog::kRowHeight),
                self + dialog::kIncomeAmount, 6, kTitleStyle, 0x44, 0x55, 0x66);
        At<uint64_t>(self, dialog::kTotal) = 0;
        ids.text = 0x0ABCDE03;
        AddIncomeRows(self, departments, s, ids);

        if (!(incomeRows & 1))
            At<int32_t>(self, dialog::kFirstTop) += At<int32_t>(self, dialog::kRowHeight);
        AddText(self, At<void*>(self, dialog::kBack), ids.text, 0xFA, At<int32_t>(self, dialog::kFirstTop),
                self + dialog::kTotalLabel, 0, kTitleStyle, 0x44, 0x55, 0x66);
        ++ids.text;
        Cdecl<void>(addr::SC4_FormatMoney, At<int64_t>(self, dialog::kTotal), &s.money);
        AddText(self, At<void*>(self, dialog::kBack), ids.text, At<int32_t>(self, dialog::kWidth) - 0x26,
                At<int32_t>(self, dialog::kFirstTop), &s.money, 6, kTitleStyle, 0x00, 0x66, 0x00);
        ++ids.text;
    }

    if (At<uint32_t>(self, dialog::kCostRows) != 0) {
        AddText(self, At<void*>(self, dialog::kBack), ids.text, 0x12,
                At<int32_t>(self, dialog::kSecondTop) - At<int32_t>(self, dialog::kRowHeight),
                self + dialog::kCostHeading, 0, kTitleStyle, 0x44, 0x55, 0x66);
        ++ids.text;
        AddText(self, At<void*>(self, dialog::kBack), ids.text, At<int32_t>(self, dialog::kWidth) - 0x26,
                At<int32_t>(self, dialog::kSecondTop) - At<int32_t>(self, dialog::kRowHeight),
                self + dialog::kCostAmount, 6, kTitleStyle, 0x44, 0x55, 0x66);
        ++ids.text;
        At<uint64_t>(self, dialog::kTotal) = 0;
        AddCostRows(self, window, departments, s, ids);

        if (!(costRows & 1))
            At<int32_t>(self, dialog::kSecondTop) += At<int32_t>(self, dialog::kRowHeight);
        AddText(self, At<void*>(self, dialog::kBack), ids.text, 0xFA, At<int32_t>(self, dialog::kSecondTop),
                self + dialog::kTotalLabel, 0, kTitleStyle, 0x44, 0x55, 0x66);
        Cdecl<void>(addr::SC4_FormatMoney, At<int64_t>(self, dialog::kTotal), &s.money);
        ++ids.text;
        AddText(self, At<void*>(self, dialog::kBack), ids.text, At<int32_t>(self, dialog::kWidth) - 0x26,
                At<int32_t>(self, dialog::kSecondTop), &s.money, 6, kTitleStyle, 0x77, 0x00, 0x00);
    }

    AddBottomButton(self, window, 0x67, 0xE, 0x0A554AE8, 0x2A6CD8FD);
    AddBottomButton(self, window, 0x6D, At<int32_t>(self, dialog::kWidth) - 0xC3, 0x6A231EAA, 0x6A56B0BB);

    AddArrow(self, window, 0x451, firstTop + 4, 0x140155CB);
    AddArrow(self, window, 0x452, firstTop + At<int32_t>(self, dialog::kRowHeight) * 8 + 4, 0x140155CC);
    AddArrow(self, window, 0x453, secondTop + 4, 0x140155CB);
    AddArrow(self, window, 0x454, secondTop + At<int32_t>(self, dialog::kRowHeight) * 8 + 4, 0x140155CC);

    Show(window, 0x451, 0);
    if (At<uint32_t>(self, dialog::kIncomeRows) <= kMaxRows)
        Show(window, 0x452, 0);
    Show(window, 0x453, 0);
    if (At<uint32_t>(self, dialog::kCostRows) <= kMaxRows)
        Show(window, 0x454, 0);

    ReleaseAll(departments);
    At<uint8_t>(self, dialog::kStarted) = 1;
    This<void>(addr::SC4ImageLoader_Destruct, &loader);
    if (departments.begin)
        Free(departments.begin);
    DestroyGroups(groups);
    DestroyScratch(s);
}

// The game's 0x007876B0 (thiscall, ret 4; it runs to 0x00789895). `this` is
// the department dialog, `window` the dialog's window. Builds the page once:
// false without a window, true at once when the page is built already.
//
// The simulator is paused (the simulator service's slot 10), the four column
// headings are read for amounts by month or by year (+0x28) and the
// multiplier set to 1 or 12. A plain window (class 0x4386D516) is made for
// the background, kept at +0x10 and added to `window` with ID 0x0423278E.
// Then the page of the dialog's kind is built; see the top of the file.
uint8_t __fastcall Start(char* self, void*, void* window)
{
    if (!window)
        return 0;
    if (At<uint8_t>(self, dialog::kStarted))
        return 1;

    Scratch s;
    InitScratch(s);

    Virtual<void>(Global<void*>(addr::SC4_SimulatorService), 0x28); // HiddenPause
    Cdecl<void*>(addr::SC4_Singleton913C72);

    if (At<uint32_t>(self, dialog::kByYear) == 0) {
        SetOwnText(self, dialog::kIncomeHeading, 0xAA5690B0);
        SetOwnText(self, dialog::kIncomeAmount, 0xCA569B75);
        SetOwnText(self, dialog::kCostHeading, 0xCA569897);
        SetOwnText(self, dialog::kCostAmount, 0xCA569B75);
        At<uint32_t>(self, dialog::kMultiplier) = 1;
    } else {
        SetOwnText(self, dialog::kIncomeHeading, 0xAA569DCD);
        SetOwnText(self, dialog::kIncomeAmount, 0x0A569C81);
        SetOwnText(self, dialog::kCostHeading, 0xEA569C2F);
        SetOwnText(self, dialog::kCostAmount, 0x0A569C81);
        At<uint32_t>(self, dialog::kMultiplier) = 0xC;
    }
    At<uint32_t>(self, dialog::kMultiplier + 4) = 0;

    void* gen = nullptr;
    void* const com = Cdecl<void*>(addr::RZGetCOM);
    if (Virtual<uint8_t>(com, 0xC, kWinGenClass, kWinGenIid, &gen)) {
        This<void*>(addr::SC4RefPtr_Assign, self + dialog::kBack, Virtual<void*>(gen, 0xC));
        Virtual<void>(gen, 0x18, uint32_t{1}, uint32_t{0});
        Virtual<void>(gen, 0x18, uint32_t{2}, uint32_t{0});
        Virtual<void>(gen, 0x18, uint32_t{0x10000}, uint32_t{0});
        Virtual<void>(gen, 0x18, uint32_t{0x20}, uint32_t{0});
        Virtual<void>(gen, 0x18, uint32_t{0x40}, uint32_t{0});
        Virtual<void>(gen, 0x18, uint32_t{0x1000}, uint32_t{1});
        Virtual<void>(gen, 0x18, uint32_t{0x2000}, uint32_t{1});
        Virtual<void>(gen, 0x18, uint32_t{0x4000}, uint32_t{1});
        Virtual<void>(At<void*>(self, dialog::kBack), 0x110, uint32_t{0x100}, uint32_t{1});
        Virtual<void>(At<void*>(self, dialog::kBack), 0x110, uint32_t{0x10000}, uint32_t{1});
        Virtual<void>(At<void*>(self, dialog::kBack), 0x110, uint32_t{0x200000}, uint32_t{0});
        Virtual<void>(At<void*>(self, dialog::kBack), 0x110, uint32_t{0x800}, uint32_t{0});
        Virtual<void>(At<void*>(self, dialog::kBack), 0x110, uint32_t{0x8000}, uint32_t{0});
        Virtual<void>(At<void*>(self, dialog::kBack), 0x110, uint32_t{4}, uint32_t{1});
        Virtual<void>(At<void*>(self, dialog::kBack), 0x100, 0x0423278Eu);
        Virtual<void>(window, 0x38, At<void*>(self, dialog::kBack));
    }
    if (gen)
        Release(gen);

    const uint32_t kind = At<uint32_t>(self, dialog::kKind);
    if (kind == 1 || kind == 2) {
        ImageLoader loader;
        MeasureFrom(self, loader, kind == 1 ? 0x140155F2 : 0x140155D2);
        This<void>(kind == 1 ? addr::SC4UI_F77C660 : addr::SC4UI_F77E600, self, window);
        At<uint8_t>(self, dialog::kStarted) = 1;
        This<void>(addr::SC4ImageLoader_Destruct, &loader);
        DestroyScratch(s);
        return 1;
    }

    if (At<uint32_t>(self, dialog::kByBuilding) != 0) {
        // The buildings page; the original does not set the started byte
        // here.
        This<void>(addr::SC4UI_F786690, self, window);
        DestroyScratchByCall(s);
        return 1;
    }

    BuildDepartmentsPage(self, window, s);
    return 1;
}

bool InstallDepartment2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[2] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F7876B0, reinterpret_cast<void*>(&Start)},
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

const bool g_department2Registered = registry::Register({
    "ui.department.part2",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F7876B0,
    "Our department dialog of the budget panel, its start: the page built for the dialog's kind, and the page of a "
    "budget group's departments with their line items (docs/ui.md)",
    false,
    InstallDepartment2,
});

} // namespace
} // namespace opensc4::ui
