// The budget panel's department dialog, functions between 0x0077A480 and
// 0x0077BEC0.
//
// The dialog (0x140 bytes, vtable 0x00AB5494, constructor 0x007862B0; the
// budget panel makes one for each run and keeps it at +0x18 meanwhile) shows
// one of several pages on a background put together from picture strips: the
// ordinances in two lists (those that pay and those that cost, nine rows on
// show each, with a check box, a name and an amount), or a department's
// buildings with their funding. This file holds:
//
//   - the three background builders (0x0077A480, 0x0077A6F0, 0x0077A960):
//     strips stacked for the row counts asked for, the window sized to them
//     and centred in its parent;
//   - the funding rows refreshed (0x0077AB40);
//   - the ordinance rows filled (0x0077ADA0): names, check marks, amounts and
//     the two totals;
//   - the makers of a check box (0x0077B7B0) and a button (0x0077B960);
//   - one building's funding set (0x0077BC90) for the health, school, police,
//     fire and power departments;
//   - three copy helpers of the dialog's vectors (0x0077AC60, 0x0077ACA0,
//     0x0077ACE0).
//
// Replacement, off by default:
//
//   ui.windows.part3: everything above.
//
// Windows, buttons, texts, images and the simulators are the game's, called
// through their vtables or by address; the dialog's other methods are in the
// neighbouring parts. Two more copy helpers in this range (0x0077AC20,
// 0x0077AD20) have callers in unrelated classes and stay the game's.
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
constexpr std::size_t kBack = 0x10;          // the window the background strips and the row texts are on
constexpr std::size_t kFunding = 0x68;       // a vector of {ID, funding row object}
constexpr std::size_t kFundingEnd = 0x6C;
constexpr std::size_t kRows = 0x74;          // a vector of uint32: the ordinance index of each row on show
constexpr std::size_t kRowsEnd = 0x78;
constexpr std::size_t kRowsCapacity = 0x7C;
constexpr std::size_t kHeight = 0x80;        // the background's height so far
constexpr std::size_t kWidth = 0x84;
constexpr std::size_t kRowHeight = 0x88;
constexpr std::size_t kTotal = 0x90;         // int64: the total of the list being filled
constexpr std::size_t kFirstTop = 0x98;      // where the first list's rows begin
constexpr std::size_t kSecondTop = 0x9C;     // and the second list's
constexpr std::size_t kCount = 0xA0;         // how many ordinances are available
constexpr std::size_t kIds = 0xA4;           // their IDs
constexpr std::size_t kStates = 0xA8;        // two bytes for each; the second says it is on
constexpr std::size_t kMultiplier = 0xB0;    // int64: 1, or more for amounts by year
constexpr std::size_t kIncomeScroll = 0x11C; // the first paying ordinance on show
constexpr std::size_t kCostScroll = 0x120;   // the first costing one on show
constexpr std::size_t kIncomeRows = 0x124;   // how many paying ordinances there are
constexpr std::size_t kCostRows = 0x128;     // how many costing ones
constexpr std::size_t kDepartment = 0x134;   // the purpose ID of the department on show
} // namespace dialog

constexpr uint32_t kTextIid = 0x212CDC1F;
constexpr uint32_t kButtonIid = 0x8810;
constexpr uint32_t kUiGroup = 0x46A006B0;
constexpr uint32_t kFirstRowText = 0x0ABCDE01; // the row texts' window IDs count up from here
constexpr uint32_t kRowsShown = 9;

// The departments whose buildings take a funding percentage.
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

// The game's image holder (0x30 bytes); the image at +4.
struct ImageLoader {
    void* first;
    void* image;
    char rest[0x28];
};
static_assert(sizeof(ImageLoader) == 0x30);

// One funding row as the dialog's vector at +0x68 keeps it.
struct FundingEntry {
    uint32_t id;
    void* row;
};
static_assert(sizeof(FundingEntry) == 8);

// One pending change of a building's funding (the vector at +0x44).
struct FundingChange {
    void* building;
    uint32_t local;
    uint32_t second;
};
static_assert(sizeof(FundingChange) == 0xC);

// What the residential simulator tells of a hospital or school (made by
// 0x00414210: a string, then figures; the two funding levels at +0x2C and
// +0x34). The original's frame leaves 0x40 bytes for it.
struct BuildingFunding {
    char bytes[0x40];
};
constexpr std::size_t kLocalFunding = 0x2C;
constexpr std::size_t kSecondFunding = 0x34;

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

// One background strip (0x0077A390, in the part before this one): the picture
// `image` put on `parent` at (x, y); returns its height.
int32_t AddStrip(char* self, uint32_t image, int32_t x, int32_t y, void* parent)
{
    return This<int32_t>(addr::SC4UI_F77A390, self, image, x, y, parent);
}

// The strips of one list: a head, a strip for every two rows, a foot. Notes
// where the rows begin in the field at `top`.
void AddList(char* self, uint32_t rows, uint32_t head, uint32_t middle, uint32_t foot, std::size_t top)
{
    void* const back = At<void*>(self, dialog::kBack);
    const int32_t height = At<int32_t>(self, dialog::kHeight) +
                           AddStrip(self, head, 0, At<int32_t>(self, dialog::kHeight), back);
    rows >>= 1;
    At<int32_t>(self, dialog::kHeight) = height;
    At<int32_t>(self, top) = height;
    for (; rows != 0; --rows) {
        At<int32_t>(self, dialog::kHeight) +=
            AddStrip(self, middle, 0, At<int32_t>(self, dialog::kHeight), At<void*>(self, dialog::kBack));
    }
    At<int32_t>(self, dialog::kHeight) +=
        AddStrip(self, foot, 0, At<int32_t>(self, dialog::kHeight), At<void*>(self, dialog::kBack));
}

// The end of the three builders: the background sized like the window, and a
// call to a fresh object of class 0x4386D516 with the middle of the window
// twice.
void FinishBackground(char* self)
{
    Virtual<void>(At<void*>(self, dialog::kBack), 0xD4, At<int32_t>(self, dialog::kWidth),
                  At<int32_t>(self, dialog::kHeight));
    void* made = nullptr;
    void* const com = Cdecl<void*>(addr::RZGetCOM);
    if (Virtual<uint8_t>(com, 0xC, 0x4386D516u, 0x5386D516u, &made)) {
        const uint32_t y = At<uint32_t>(self, dialog::kHeight) >> 1;
        const uint32_t x = At<uint32_t>(self, dialog::kWidth) >> 1;
        Virtual<void>(made, 0x64, x, y, x, y);
    }
    if (made)
        Release(made);
}

// The background with two lists, as 0x0077A480 and 0x0077A6F0 build it from
// the eight pictures `image` to `image` + 7: top, the first list (left out
// with no rows), the second list likewise, bottom. `window` is then sized to
// the strips and centred in its parent.
void BuildTwoLists(char* self, void* window, uint32_t first, uint32_t second, uint32_t image)
{
    At<int32_t>(self, dialog::kHeight) +=
        AddStrip(self, image, 0, At<int32_t>(self, dialog::kHeight), At<void*>(self, dialog::kBack));
    if (first != 0)
        AddList(self, first, image + 1, image + 2, image + 3, dialog::kFirstTop);
    if (second != 0)
        AddList(self, second, image + 4, image + 5, image + 6, dialog::kSecondTop);
    At<int32_t>(self, dialog::kHeight) +=
        AddStrip(self, image + 7, 0, At<int32_t>(self, dialog::kHeight), At<void*>(self, dialog::kBack));

    Virtual<void>(window, 0xD4, At<int32_t>(self, dialog::kWidth), At<int32_t>(self, dialog::kHeight)); // SetSize
    void* const parent = Virtual<void*>(window, 0x2C);
    void* const parentAgain = Virtual<void*>(window, 0x2C);
    const int32_t parentHeight = Virtual<int32_t>(parent, 0xA8);
    const int32_t height = Virtual<int32_t>(window, 0xA8);
    const int32_t y = (parentHeight - height) / 2;
    const int32_t parentWidth = Virtual<int32_t>(parentAgain, 0xA4);
    const int32_t width = Virtual<int32_t>(window, 0xA4);
    Virtual<void>(window, 0xE0, (parentWidth - width) / 2, y); // GZWinMoveTo

    FinishBackground(self);
}

// The game's 0x0077A480. `this` is the dialog. The ordinance page's
// background (pictures 0x140155F0 to 0x140155F7) for `first` and `second`
// rows.
void __fastcall BuildOrdinanceBackground(char* self, void*, void* window, uint32_t first, uint32_t second)
{
    BuildTwoLists(self, window, first, second, 0x140155F0);
}

// The game's 0x0077A6F0. `this` is the dialog. The same with the pictures
// 0x140155D0 to 0x140155D7.
void __fastcall BuildDepartmentBackground(char* self, void*, void* window, uint32_t first, uint32_t second)
{
    BuildTwoLists(self, window, first, second, 0x140155D0);
}

// The game's 0x0077A960. `this` is the dialog. A background with one list
// (pictures 0x2BFEB0CB to 0x2BFEB0CF; the rows begin at +0x9C). The window's
// own height is asked for before its parent's here.
void __fastcall BuildSingleBackground(char* self, void*, void* window, uint32_t rows)
{
    At<int32_t>(self, dialog::kHeight) +=
        AddStrip(self, 0x2BFEB0CF, 0, At<int32_t>(self, dialog::kHeight), At<void*>(self, dialog::kBack));
    if (rows != 0)
        AddList(self, rows, 0x2BFEB0CB, 0x2BFEB0CC, 0x2BFEB0CD, dialog::kSecondTop);
    At<int32_t>(self, dialog::kHeight) +=
        AddStrip(self, 0x2BFEB0CE, 0, At<int32_t>(self, dialog::kHeight), At<void*>(self, dialog::kBack));

    Virtual<void>(window, 0xD4, At<int32_t>(self, dialog::kWidth), At<int32_t>(self, dialog::kHeight));
    void* const parent = Virtual<void*>(window, 0x2C);
    void* const parentAgain = Virtual<void*>(window, 0x2C);
    const int32_t height = Virtual<int32_t>(window, 0xA8);
    const int32_t parentHeight = Virtual<int32_t>(parent, 0xA8);
    const int32_t y = (parentHeight - height) / 2;
    const int32_t parentWidth = Virtual<int32_t>(parentAgain, 0xA4);
    const int32_t width = Virtual<int32_t>(window, 0xA4);
    Virtual<void>(window, 0xE0, (parentWidth - width) / 2, y);

    FinishBackground(self);
}

// The game's 0x0077AB40. `this` is the dialog. Brings every funding row up to
// date. A row with flag 2 takes its first value from its source (slot 12 to
// slot 11, slot 14 to slot 17) or, with no source value, is reset; a row with
// flag 4 and a source value recomputes (slot 35) and takes it again.
void __fastcall RefreshFunding(char* self)
{
    for (uint32_t i = 0;
         i < static_cast<uint32_t>((At<char*>(self, dialog::kFundingEnd) - At<char*>(self, dialog::kFunding)) >> 3);
         ++i) {
        void* const row = At<FundingEntry*>(self, dialog::kFunding)[i].row;
        if (row)
            AddRef(row);
        if (Virtual<uint32_t>(row, 0x90) & 2) {
            if (Virtual<uint32_t>(row, 0x30) != 0) {
                Virtual<void>(row, 0x2C, Virtual<uint32_t>(row, 0x30));
                Virtual<void>(row, 0x44, Virtual<uint32_t>(row, 0x38));
            } else {
                Virtual<void>(row, 0x88);
                Virtual<void>(row, 0x34, uint32_t{0});
                Virtual<void>(row, 0x3C, uint32_t{0});
            }
        }
        if ((Virtual<uint32_t>(row, 0x90) & 4) && Virtual<uint32_t>(row, 0x30) != 0) {
            Virtual<void>(row, 0x8C);
            Virtual<void>(row, 0x2C, Virtual<uint32_t>(row, 0x30));
            Virtual<void>(row, 0x44, Virtual<uint32_t>(row, 0x38));
        }
        Release(row);
    }
}

// The game's 0x0077AC60 (cdecl, four arguments; the last is an unused tag).
// Constructs `count` copies of `value` from `to` on; returns the end.
FundingEntry* __cdecl FundingFill(FundingEntry* to, uint32_t count, const FundingEntry* value, void*)
{
    for (; count != 0; --count, ++to) {
        if (!to)
            continue;
        to->id = value->id;
        to->row = value->row;
        if (to->row)
            AddRef(to->row);
    }
    return to;
}

void ConstructChange(FundingChange* to, const FundingChange* from)
{
    to->building = from->building;
    if (to->building)
        AddRef(to->building);
    to->local = from->local;
    to->second = from->second;
}

// The game's 0x0077ACA0 (cdecl, four arguments). Copy-constructs the pending
// changes of [first, last) from `to` on; returns the end.
FundingChange* __cdecl ChangesCopy(FundingChange* first, FundingChange* last, FundingChange* to, void*)
{
    for (; first != last; ++first, ++to) {
        if (to)
            ConstructChange(to, first);
    }
    return to;
}

// The game's 0x0077ACE0 (cdecl, four arguments). Constructs `count` copies of
// `value` from `to` on; returns the end.
FundingChange* __cdecl ChangesFill(FundingChange* to, uint32_t count, const FundingChange* value, void*)
{
    for (; count != 0; --count, ++to) {
        if (to)
            ConstructChange(to, value);
    }
    return to;
}

// Adds an ordinance's index to the rows on show.
void PushRow(char* self, uint32_t index)
{
    uint32_t* const end = At<uint32_t*>(self, dialog::kRowsEnd);
    if (end != At<uint32_t*>(self, dialog::kRowsCapacity)) {
        if (end)
            *end = index;
        At<uint32_t*>(self, dialog::kRowsEnd) += 1;
    } else {
        uint8_t tag = 0;
        This<void>(addr::SC4PtrVector_InsertN, self + dialog::kRows, end, &index, &tag, uint32_t{1}, uint32_t{1});
    }
}

int64_t Scaled(char* self, int64_t amount)
{
    return static_cast<int64_t>(At<uint64_t>(self, dialog::kMultiplier) * static_cast<uint64_t>(amount));
}

int64_t Negated(int64_t amount)
{
    return static_cast<int64_t>(uint64_t{0} - static_cast<uint64_t>(amount));
}

uint8_t IsOn(char* self, uint32_t index)
{
    return At<uint8_t*>(self, dialog::kStates)[index * 2 + 1];
}

void SetBlank(void* window)
{
    GameString empty;
    MakeEmpty(empty);
    Virtual<void>(window, 0x128, &empty);
    DestroyString(empty);
}

// With more rows than fit: the row after the last gets blank texts and its
// check box and button hidden, or, with all nine rows used, the last row's
// are shown.
void FinishRows(char* self, void* window, uint32_t shown, uint32_t& text, uint32_t box, uint32_t button)
{
    if (shown < kRowsShown) {
        SetBlank(Virtual<void*>(At<void*>(self, dialog::kBack), 0x8C, text));
        ++text;
        SetBlank(Virtual<void*>(At<void*>(self, dialog::kBack), 0x8C, text));
        ++text;
        Virtual<void>(Virtual<void*>(window, 0x8C, box), 0x110, uint32_t{1}, uint32_t{0});
        Virtual<void>(Virtual<void*>(window, 0x8C, button), 0x110, uint32_t{1}, uint32_t{0});
    } else {
        Virtual<void>(Virtual<void*>(window, 0x8C, box - 1), 0x110, uint32_t{1}, uint32_t{1});
        Virtual<void>(Virtual<void*>(window, 0x8C, button - 1), 0x110, uint32_t{1}, uint32_t{1});
    }
}

// The total of a list written to its text window.
void ShowTotal(char* self, uint32_t id, GameString* text)
{
    void* total = nullptr;
    if (Virtual<uint8_t>(Virtual<void*>(At<void*>(self, dialog::kBack), 0x8C, id), 0x0, kTextIid, &total)) {
        Virtual<void>(total, 0x10, text);
        Release(total);
    }
}

// The game's 0x0077ADA0. `this` is the dialog. Fills the ordinance page: the
// available ordinances are asked for again, and each list (first those that
// pay, then those that cost; left out when it has no rows) gets up to nine
// rows from its scroll position on: the name and the amount on the background
// (window IDs counting up from 0x0ABCDE03, two a row), the check box on
// `window` (IDs from 0x12C on, across both lists) set when the ordinance is
// on. An ordinance that is off shows no amount. The list's total (every
// ordinance that is on, shown or not, times the multiplier) goes to the text
// after the rows.
void __fastcall FillOrdinances(char* self, void*, void* window)
{
    void* const ordinances = Global<void*>(addr::SC4_Service43D90);
    uint32_t box = 0x12C;
    uint32_t button = 0x1F4;
    GameString text;
    MakeEmpty(text);
    Virtual<void>(ordinances, 0x30, At<uint32_t*>(self, dialog::kIds), &At<uint32_t>(self, dialog::kCount));
    At<uint32_t*>(self, dialog::kRowsEnd) = At<uint32_t*>(self, dialog::kRows);
    uint32_t next = kFirstRowText;
    // The original leaves both holders unset before the first query.
    void* checked = nullptr;
    void* unchecked = nullptr;

    if (At<uint32_t>(self, dialog::kIncomeRows) != 0) {
        At<uint64_t>(self, dialog::kTotal) = 0;
        next = kFirstRowText + 2;
        uint32_t shown = 0;
        uint32_t seen = 0;
        for (uint32_t i = 0; i < At<uint32_t>(self, dialog::kCount); ++i) {
            const int64_t income =
                Virtual<int64_t>(Virtual<void*>(ordinances, 0x50, At<uint32_t*>(self, dialog::kIds)[i]), 0x14);
            if (!Virtual<uint8_t>(Virtual<void*>(ordinances, 0x50, At<uint32_t*>(self, dialog::kIds)[i]), 0x58))
                continue;
            if (seen >= At<uint32_t>(self, dialog::kIncomeScroll) && shown < kRowsShown) {
                void* const ordinance = Virtual<void*>(ordinances, 0x50, At<uint32_t*>(self, dialog::kIds)[i]);
                void* const name = Virtual<void*>(At<void*>(self, dialog::kBack), 0x8C, next);
                ++next;
                Virtual<void>(name, 0x128, Virtual<void*>(ordinance, 0x1C));
                if (IsOn(self, i)) {
                    Cdecl<void>(addr::SC4_FormatMoney, Scaled(self, income), &text);
                    At<uint64_t>(self, dialog::kTotal) += static_cast<uint64_t>(Scaled(self, income));
                    if (Virtual<uint8_t>(Virtual<void*>(window, 0x8C, box), 0x0, kButtonIid, &checked)) {
                        Virtual<void>(checked, 0x24, uint32_t{1});
                        Release(checked);
                        checked = nullptr;
                    }
                } else {
                    This<void*>(addr::SC4String_EraseBody, &text.body, uint32_t{0}, 0xFFFFFFFFu);
                    if (Virtual<uint8_t>(Virtual<void*>(window, 0x8C, box), 0x0, kButtonIid, &unchecked)) {
                        Virtual<void>(unchecked, 0x24, uint32_t{0});
                        Release(unchecked);
                        unchecked = nullptr;
                    }
                }
                void* const amount = Virtual<void*>(At<void*>(self, dialog::kBack), 0x8C, next);
                ++next;
                Virtual<void>(amount, 0x128, &text);
                PushRow(self, i);
                ++box;
                ++button;
                At<int32_t>(self, dialog::kFirstTop) += At<int32_t>(self, dialog::kRowHeight);
            } else if (IsOn(self, i)) {
                At<uint64_t>(self, dialog::kTotal) += static_cast<uint64_t>(Scaled(self, income));
            }
            if (shown < kRowsShown && seen >= At<uint32_t>(self, dialog::kIncomeScroll))
                ++shown;
            ++seen;
        }
        if (At<uint32_t>(self, dialog::kIncomeRows) >= kRowsShown) {
            FinishRows(self, window, shown, next, box, button);
            if (shown < kRowsShown) {
                ++box;
                ++button;
            }
        }
        Cdecl<void>(addr::SC4_FormatMoney, At<int64_t>(self, dialog::kTotal), &text);
        ShowTotal(self, next + 1, &text);
        next += 2;
    }

    if (At<uint32_t>(self, dialog::kCostRows) != 0) {
        next += 2;
        At<uint64_t>(self, dialog::kTotal) = 0;
        uint32_t shown = 0;
        uint32_t seen = 0;
        for (uint32_t i = 0; i < At<uint32_t>(self, dialog::kCount); ++i) {
            const int64_t cost =
                Virtual<int64_t>(Virtual<void*>(ordinances, 0x50, At<uint32_t*>(self, dialog::kIds)[i]), 0x14);
            if (Virtual<uint8_t>(Virtual<void*>(ordinances, 0x50, At<uint32_t*>(self, dialog::kIds)[i]), 0x58))
                continue;
            if (seen >= At<uint32_t>(self, dialog::kCostScroll) && shown < kRowsShown) {
                void* label = nullptr;
                void* const name = Virtual<void*>(At<void*>(self, dialog::kBack), 0x8C, next);
                ++next;
                if (Virtual<uint8_t>(name, 0x0, kTextIid, &label)) {
                    void* const ordinance = Virtual<void*>(ordinances, 0x50, At<uint32_t*>(self, dialog::kIds)[i]);
                    Virtual<void>(label, 0x10, Virtual<void*>(ordinance, 0x1C));
                    Release(label);
                    label = nullptr;
                }
                // Here the total only counts a row whose check box answered.
                if (Virtual<uint8_t>(Virtual<void*>(window, 0x8C, box), 0x0, kButtonIid, &checked)) {
                    if (IsOn(self, i)) {
                        Cdecl<void>(addr::SC4_FormatMoney, Negated(Scaled(self, cost)), &text);
                        At<uint64_t>(self, dialog::kTotal) -= static_cast<uint64_t>(Scaled(self, cost));
                        Virtual<void>(checked, 0x24, uint32_t{1});
                    } else {
                        This<void*>(addr::SC4String_EraseBody, &text.body, uint32_t{0}, 0xFFFFFFFFu);
                        Virtual<void>(checked, 0x24, uint32_t{0});
                    }
                    Release(checked);
                    checked = nullptr;
                }
                void* const amount = Virtual<void*>(At<void*>(self, dialog::kBack), 0x8C, next);
                ++next;
                if (Virtual<uint8_t>(amount, 0x0, kTextIid, &label)) {
                    Virtual<void>(label, 0x10, &text);
                    Release(label);
                    label = nullptr;
                }
                PushRow(self, i);
                ++box;
                ++button;
                At<int32_t>(self, dialog::kSecondTop) += At<int32_t>(self, dialog::kRowHeight);
            } else if (IsOn(self, i)) {
                At<uint64_t>(self, dialog::kTotal) -= static_cast<uint64_t>(Scaled(self, cost));
            }
            if (shown < kRowsShown && seen >= At<uint32_t>(self, dialog::kCostScroll))
                ++shown;
            ++seen;
        }
        if (At<uint32_t>(self, dialog::kCostRows) >= kRowsShown)
            FinishRows(self, window, shown, next, box, button);
        Cdecl<void>(addr::SC4_FormatMoney, At<int64_t>(self, dialog::kTotal), &text);
        ShowTotal(self, next + 1, &text);
    }

    DestroyString(text);
}

// The game's 0x0077B7B0. `this` is the dialog (not used). Makes a check box
// (a control of kind 4 from the framework's maker, with the picture `image`
// of group 0x46A006B0, or 26 by 16 without one), gives it the window ID `id`,
// the state `checked` and the place (x, y), and adds it to `parent`. The
// sixth argument is not used.
void __fastcall MakeCheckBox(char*, void*, void* parent, uint32_t id, int32_t x, int32_t y, uint32_t image, uint32_t,
                             uint32_t checked)
{
    ImageLoader loader;
    This<void*>(addr::SC4ImageLoader_ConstructParts, &loader, uint32_t{0}, kUiGroup, image, uint32_t{1},
                uint32_t{0});
    GameString caption;
    MakeEmpty(caption);

    void* box;
    if (void* const picture = loader.image) {
        void* const maker = Cdecl<void*>(addr::SC4_Singleton913C72);
        void* const area = Virtual<void*>(picture, 0x30);
        box = Virtual<void*>(maker, 0x28, uint32_t{4}, id, &caption, picture, area, uint32_t{0});
    } else {
        void* const maker = Cdecl<void*>(addr::SC4_Singleton913C72);
        box = Virtual<void*>(maker, 0x28, uint32_t{4}, id, &caption, uint32_t{0}, uint32_t{0}, uint32_t{0});
        Virtual<void>(Virtual<void*>(box, 0xC), 0xD4, int32_t{0x1A}, int32_t{0x10});
    }

    if (box) {
        void* const window = Virtual<void*>(box, 0xC);
        Virtual<void>(box, 0x14, uint32_t{4}, uint32_t{0});
        Virtual<void>(Virtual<void*>(box, 0xC), 0x110, uint32_t{0x10000}, uint32_t{1});
        Virtual<void>(Virtual<void*>(box, 0xC), 0x110, uint32_t{0x20000}, uint32_t{1});
        Virtual<void>(Virtual<void*>(box, 0xC), 0x110, uint32_t{0x40000}, uint32_t{1});
        Virtual<void>(Virtual<void*>(box, 0xC), 0x110, uint32_t{4}, uint32_t{1});
        Virtual<void>(box, 0x14, uint32_t{4}, uint32_t{0});
        Virtual<void>(box, 0xB4, uint32_t{0}, 0x8A5C324Au);
        Virtual<void>(box, 0x24, checked);
        Virtual<void>(window, 0xE0, x, y);
        Virtual<void>(parent, 0x38, window);
        Release(box);
    }

    DestroyString(caption);
    This<void>(addr::SC4ImageLoader_Destruct, &loader);
}

// The game's 0x0077B960. `this` is the dialog (not used). Makes a button (a
// control of kind 2, with the picture `image` or 26 by 16 without one) with
// the window ID `id` at (x, y) and adds it to `parent` inside a holder window
// of its own (class 0xC2AFA76E, window ID `id` + 0x100), when that class can
// be made. With `toggle` set the button takes the font 0x4A809919 and is
// made a toggle. The sixth argument is not used; the picture's argument slot
// is where the original keeps the holder.
void __fastcall MakeButton(char*, void*, void* parent, uint32_t id, int32_t x, int32_t y, uint32_t image, uint32_t,
                           uint32_t toggle)
{
    ImageLoader loader;
    This<void*>(addr::SC4ImageLoader_ConstructParts, &loader, uint32_t{0}, kUiGroup, image, uint32_t{1},
                uint32_t{0});
    GameString caption;
    MakeEmpty(caption);

    void* const picture = loader.image;
    void* const maker = Cdecl<void*>(addr::SC4_Singleton913C72);
    void* button;
    if (picture) {
        void* const area = Virtual<void*>(picture, 0x30);
        button = Virtual<void*>(maker, 0x28, uint32_t{2}, id, &caption, picture, area, uint32_t{0});
        void* const window = Virtual<void*>(button, 0xC);
        Virtual<void>(window, 0xD8, Virtual<void*>(picture, 0x30));
    } else {
        button = Virtual<void*>(maker, 0x28, uint32_t{2}, id, &caption, uint32_t{0}, uint32_t{0}, uint32_t{0});
        Virtual<void>(Virtual<void*>(button, 0xC), 0xD4, int32_t{0x1A}, int32_t{0x10});
    }

    if (static_cast<uint8_t>(toggle)) {
        Virtual<void>(Virtual<void*>(button, 0xC), 0x110, uint32_t{0x10000}, uint32_t{0});
        Virtual<void>(Virtual<void*>(button, 0xC), 0x110, uint32_t{0x20000}, uint32_t{1});
        Virtual<void>(Virtual<void*>(button, 0xC), 0x110, uint32_t{0x40000}, uint32_t{1});
        Virtual<void>(Virtual<void*>(button, 0xC), 0x110, uint32_t{0x800}, uint32_t{0});
        void* const font = Virtual<void*>(Cdecl<void*>(addr::SC4_Singleton913C72), 0x14, 0x4A809919u);
        Virtual<void>(button, 0x4C, font);
    }

    void* const window = Virtual<void*>(button, 0xC);
    Virtual<void>(button, 0x14, uint32_t{4}, toggle);
    Virtual<void>(button, 0xB4, uint32_t{0}, 0x4A5C31D7u);
    uint32_t colour = Virtual<uint32_t>(Virtual<void*>(button, 0xC), 0x1B0, uint32_t{0x3F}, uint32_t{0x49},
                                        uint32_t{0x67});
    Virtual<void>(button, 0x5C, uint32_t{0}, colour);
    colour = Virtual<uint32_t>(Virtual<void*>(button, 0xC), 0x1B0, uint32_t{0x3F}, uint32_t{0x49}, uint32_t{0x67});
    Virtual<void>(button, 0x5C, uint32_t{2}, colour);
    colour = Virtual<uint32_t>(Virtual<void*>(button, 0xC), 0x1B0, uint32_t{0x66}, uint32_t{0x66}, uint32_t{0x66});
    Virtual<void>(button, 0x5C, uint32_t{1}, colour);
    Virtual<void>(Virtual<void*>(button, 0xC), 0x110, uint32_t{4}, uint32_t{1});
    Virtual<void>(window, 0xE0, x, y);

    void* holder = reinterpret_cast<void*>(static_cast<std::uintptr_t>(image));
    if (Virtual<uint8_t>(Cdecl<void*>(addr::RZGetCOM), 0xC, 0xC2AFA76Eu, 0xC2AFA76Fu, &holder)) {
        void* const outer = Virtual<void*>(holder, 0xC);
        Virtual<void>(outer, 0xD8, Virtual<void*>(window, 0xC0));
        Virtual<void>(outer, 0xE0, x, y);
        Virtual<void>(window, 0xE0, int32_t{0}, int32_t{0});
        Virtual<void>(outer, 0x110, uint32_t{0x10000}, uint32_t{0});
        Virtual<void>(outer, 0x110, uint32_t{0x20000}, uint32_t{0});
        Virtual<void>(outer, 0x110, uint32_t{0x40000}, uint32_t{0});
        Virtual<void>(outer, 0x110, uint32_t{0x800}, uint32_t{0});
        Virtual<void>(outer, 0x100, id + 0x100);
        Virtual<void>(holder, 0x14, uint32_t{0xFF}, uint32_t{0}, uint32_t{0xFF});
        Virtual<void>(holder, 0x30, uint32_t{0x40});
        Virtual<void>(parent, 0x38, outer);
        Virtual<void>(outer, 0x38, window);
        Release(holder);
    }
    Release(button);

    DestroyString(caption);
    This<void>(addr::SC4ImageLoader_Destruct, &loader);
}

// A percentage as the simulators take it: a hundredth, rounded to a float.
float Fraction(float percent)
{
    return static_cast<float>(static_cast<double>(percent) * static_cast<double>(0.01f));
}

// The health and school halves of 0x0077BC90: the residential simulator's
// record of the building is read (slot 27 for a hospital, 26 for a school),
// one of its two funding levels replaced and both written back (slot 19).
uint8_t SetCareFunding(void* building, uint8_t second, float percent, std::size_t read, uint32_t school)
{
    BuildingFunding record;
    This<void*>(addr::cSC4Advisors_F414210, &record);
    if (!Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DA4), read, building, &record)) {
        This<void>(addr::SC4String_Destroy, &record);
        return 0;
    }
    At<float>(&record, second ? kSecondFunding : kLocalFunding) = Fraction(percent);
    Virtual<void>(Global<void*>(addr::SC4_Service43DA4), 0x4C, building, &At<float>(&record, kLocalFunding),
                  &At<float>(&record, kSecondFunding), school);
    This<void>(addr::SC4String_Destroy, &record);
    return 1;
}

// The game's 0x0077BC90 (it runs to 0x0077BEB9; the list's entry 0x0077BD30
// is inside it). `this` is the dialog. Sets the funding of one building of
// the department on show to `percent` (0 to 100): a hospital's or school's
// first or second level, a police station's, a fire station's or a power
// plant's (these have no second level). Returns whether it was set.
uint8_t __fastcall SetBuildingFunding(char* self, void*, void* occupant, uint8_t second, float percent)
{
    void* building = nullptr;
    if (!Virtual<uint8_t>(occupant, 0x0, 0x26BB767Eu, &building)) {
        if (building)
            Release(building);
        return 0;
    }

    const uint32_t department = At<uint32_t>(self, dialog::kDepartment);
    if (department == kHealth || department == kSchools) {
        if (!building)
            return 0;
        const uint8_t done = department == kHealth ? SetCareFunding(building, second, percent, 0x6C, 0)
                                                   : SetCareFunding(building, second, percent, 0x68, 1);
        if (building)
            Release(building);
        return done;
    }
    if (department == kPolice || department == kPower || department == kFire) {
        if (!building)
            return 0;
        if (second) {
            Release(building);
            return 0;
        }
        float fraction = Fraction(percent);
        if (department == kPolice)
            Virtual<void>(Global<void*>(addr::SC4_PoliceSimulator), 0x20, building, &fraction);
        else if (department == kPower)
            Virtual<void>(Global<void*>(addr::SC4_Service43DA0), 0xCC, building, &fraction);
        else
            Virtual<void>(Global<void*>(addr::SC4_Service43D78), 0x24, building, &fraction);
        if (building)
            Release(building);
        return 1;
    }

    if (building)
        Release(building);
    return 0;
}

bool InstallWindows3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[12] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F77A480, reinterpret_cast<void*>(&BuildOrdinanceBackground)},
        {addr::SC4UI_F77A6F0, reinterpret_cast<void*>(&BuildDepartmentBackground)},
        {addr::SC4UI_F77A960, reinterpret_cast<void*>(&BuildSingleBackground)},
        {addr::SC4UI_F77AB40, reinterpret_cast<void*>(&RefreshFunding)},
        {addr::SC4UI_F77AC60, reinterpret_cast<void*>(&FundingFill)},
        {addr::SC4UI_F77ACA0, reinterpret_cast<void*>(&ChangesCopy)},
        {addr::SC4UI_F77ACE0, reinterpret_cast<void*>(&ChangesFill)},
        {addr::SC4UI_F77ADA0, reinterpret_cast<void*>(&FillOrdinances)},
        {addr::SC4UI_F77B7B0, reinterpret_cast<void*>(&MakeCheckBox)},
        {addr::SC4UI_F77B960, reinterpret_cast<void*>(&MakeButton)},
        {addr::SC4UI_F77BC90, reinterpret_cast<void*>(&SetBuildingFunding)},
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

const bool g_windows3Registered = registry::Register({
    "ui.windows.part3",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F77ADA0,
    "Our department dialog of the budget panel, third part: the backgrounds, the ordinance rows, the check box "
    "and button makers and a building's funding (docs/ui.md)",
    false,
    InstallWindows3,
});

} // namespace
} // namespace opensc4::ui
