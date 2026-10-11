// The budget panel's department dialog, first part of the rest: the page of
// one department's buildings (0x00785740 to 0x0078769F).
//
// The dialog (0x140 bytes, vtable 0x00AB5494) shows three kinds of page: the
// ordinances, the neighbour deals, and a department's funding rows. From a
// funding row the player can open a second dialog of the same class over the
// first, with kind 0 and the department's ID in the field at +0x24: the list
// of that department's buildings, each with its name, its use, one or two
// sliders for its funding (hospitals and schools have a second level:
// ambulances, school buses) and its monthly cost. This file holds that page:
//
//   0x00785740  the rows filled from the department's buildings (again after
//               a scroll or a slider's change; the first time it also notes
//               each building and its funding in the dialog's vector)
//   0x007862B0  the dialog's constructor
//   0x00786530  the second dialog made, run as a modal window and dropped
//   0x00786690  the page built: background, headings, nine rows of controls,
//               the scroll buttons, the two closing buttons; or the page that
//               only says the department has no buildings
//
// The list's entry 0x007864F0 is not a function: it is a label inside the
// constructor.
//
// Replacement, off by default:
//
//   ui.department.part1: the four functions above.
//
// Windows, texts, the control makers (ui_windows_2.cpp, ui_windows_3.cpp),
// the simulators asked for a building's figures and the vector's helpers are
// the game's or other groups', called through their vtables or by address.
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

// The dialog's fields, named as in ui_windows_2.cpp to ui_windows_6.cpp.
// Those files disagree on +0x10 (kBack, kPage, kWindow) and on +0x24 (kThird,
// kByBuilding); the offsets are what counts.
namespace dialog {
constexpr std::size_t kStarted = 0x08;       // byte: the page is built
constexpr std::size_t kPage = 0x10;          // the window the background strips and the texts are on
constexpr std::size_t kHeld = 0x1C;          // the second dialog while it runs
constexpr std::size_t kDepartmentId = 0x24;  // the third number given to the window's maker: a department's ID
constexpr std::size_t kSecond = 0x28;        // 1: amounts by year
constexpr std::size_t kBuildings = 0x44;     // vector of {building, float, float}: the funding as first shown
constexpr std::size_t kHeight = 0x80;
constexpr std::size_t kWidth = 0x84;
constexpr std::size_t kRowHeight = 0x88;
constexpr std::size_t kTotal = 0x90;         // int64: the sum of every building's cost
constexpr std::size_t kSecondTop = 0x9C;     // where the next row goes
constexpr std::size_t kMultiplier = 0xB0;    // int64, 1 from the constructor
constexpr std::size_t kIncomeHeading = 0xB8; // five game strings
constexpr std::size_t kIncomeAmount = 0xCC;
constexpr std::size_t kExpenseHeading = 0xE0;
constexpr std::size_t kExpenseAmount = 0xF4; // the heading of the cost column here
constexpr std::size_t kTotalLabel = 0x108;
constexpr std::size_t kIncomeScroll = 0x11C;
constexpr std::size_t kCostScroll = 0x120;   // the first building on show
constexpr std::size_t kIncomeRows = 0x124;
constexpr std::size_t kCostRows = 0x128;     // how many buildings the department has
constexpr std::size_t kFlag = 0x12C;         // byte, 0 from the constructor
constexpr std::size_t kList = 0x130;         // a list
constexpr std::size_t kDepartment = 0x134;   // the purpose ID of the department on show
constexpr std::size_t kSecondPurpose = 0x138; // the purpose ID of its second funding level, or 0
} // namespace dialog

constexpr uint32_t kWinGenIid = 0x5386D516;
constexpr uint32_t kOccupantIid = 0x26BB767E;
constexpr uint32_t kBuildingIid = 0x87DDA3A9;
constexpr uint32_t kSliderIid = 0x21325207;
constexpr uint32_t kTextGroup = 0x6A3FF01C;
constexpr uint32_t kHeadingFont = 0xEA85D304;
constexpr uint32_t kRowFont = 0xEA85D305;
constexpr uint32_t kFirstRowText = 0x0ABCDE06; // three text windows a row
constexpr uint32_t kFirstMark = 0x0ABCE200;    // two pictures a row
constexpr uint32_t kFirstSlider = 0x0ABCE400;  // a slider a row, and a second one 0x100 on
constexpr int32_t kRowsShown = 9;

// The departments whose buildings take a funding percentage.
constexpr uint32_t kHealth = 0xCA565486;
constexpr uint32_t kSchools = 0xEA5654B6;
constexpr uint32_t kPolice = 0x0A567BAA;
constexpr uint32_t kFire = 0xEA567BC3;
constexpr uint32_t kPower = 0xCA58E540;
constexpr uint32_t kAmbulances = 0xEA56549E;
constexpr uint32_t kSchoolBuses = 0x4A5654BA;

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

struct LocalizedString {
    GameString text;
    uint32_t rest[3];
};
static_assert(sizeof(LocalizedString) == 0x20);

// The game's image holder (0x30 bytes); the image at +4.
struct ImageLoader {
    void* first;
    void* image;
    char rest[0x28];
};
static_assert(sizeof(ImageLoader) == 0x30);

struct ListNode {
    ListNode* next;
    ListNode* previous;
    void* value;
};

// One building as the dialog's vector at +0x44 keeps it: the funding
// percentages it had when the page was first filled.
struct FundingChange {
    void* building;
    float local;
    float second;
};
static_assert(sizeof(FundingChange) == 0xC);

struct ChangeVector {
    FundingChange* begin;
    FundingChange* end;
    FundingChange* capacity;
};

// What the residential simulator tells of a hospital or school (made by
// 0x00414210): a string, a capacity at +0x24, and for each funding level a
// fraction and an amount. The original's frame leaves 0x40 bytes for it.
struct CareRecord {
    GameString name;
    char rest[0x2C];
};
static_assert(sizeof(CareRecord) == 0x40);
constexpr std::size_t kCareCapacity = 0x24;
constexpr std::size_t kCareUnset = 0x28;
constexpr std::size_t kLocalFunding = 0x2C;
constexpr std::size_t kLocalAmount = 0x30;
constexpr std::size_t kSecondFunding = 0x34;
constexpr std::size_t kSecondAmount = 0x38;

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

// The game's inlined reference holder: assign.
void Assign(void*& holder, void* object)
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

// The game's inlined reference holder: empty it.
void Clear(void*& holder)
{
    if (void* const object = holder) {
        holder = nullptr;
        Release(object);
    }
}

// The game's inlined string constructor.
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

void DestroyString(GameString& text)
{
    char* const begin = text.body.begin;
    text.vtable = addr::SC4String_Vtable;
    if (begin)
        Free(begin);
}

// The game's ftol: truncation.
int32_t Truncate(double value)
{
    return static_cast<int32_t>(static_cast<int64_t>(value));
}

// A fraction as the page shows it: times 100, rounded to a float.
float Percent(float fraction)
{
    return static_cast<float>(static_cast<double>(fraction) * static_cast<double>(100.0f));
}

// An amount's share, rounded half up.
int32_t Share(uint32_t amount, float fraction)
{
    return Truncate(static_cast<double>(amount) * static_cast<double>(fraction) + 0.5);
}

void* Child(void* window, uint32_t id)
{
    return Virtual<void*>(window, 0x8C, id);
}

void SetFlag(void* window, uint32_t flag, uint32_t on)
{
    Virtual<void>(window, 0x110, flag, on);
}

void ShowChild(void* window, uint32_t id, uint32_t show)
{
    Cdecl<uint8_t>(addr::SC4Win_ShowChild, window, id, show, uint32_t{0});
}

void SetChildText(void* window, uint32_t id, GameString* text)
{
    Cdecl<uint8_t>(addr::SC4Win_SetChildText, window, id, text);
}

// A localized text loaded and its characters given to a string's body. With
// no text object the original reads through a null pointer, as here.
void AssignLocalized(StringBody& target, uint32_t group, uint32_t instance)
{
    LocalizedString text;
    char* const made =
        This<char*>(addr::SC4LocalizedString_SC4LocalizedString, &text, group, instance, uint32_t{1});
    StringBody* const body = made ? reinterpret_cast<StringBody*>(made + 4) : nullptr;
    if (body != &target) {
        uint8_t tag;
        This<void>(addr::SC4String_AssignRange, &target, body->begin, body->end, &tag);
    }
    DestroyString(text.text);
}

// The same through the string's own assignment (0x00424380).
void AssignLocalizedWhole(GameString& target, uint32_t instance)
{
    LocalizedString text;
    void* const made =
        This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, kTextGroup, instance, uint32_t{1});
    This<void>(addr::cRZBaseString_Assign, &target, made);
    DestroyString(text.text);
}

// A localized text copied to a plain string and set as a window's text.
void SetLocalizedCopy(void* window, uint32_t group, uint32_t instance)
{
    LocalizedString source;
    char* const made =
        This<char*>(addr::SC4LocalizedString_SC4LocalizedString, &source, group, instance, uint32_t{1});
    const StringBody* const body = made ? reinterpret_cast<const StringBody*>(made + 4) : nullptr;
    GameString text;
    text.body.begin = nullptr;
    text.body.end = nullptr;
    text.body.capacity = nullptr;
    char* const begin = body->begin;
    uint32_t length = static_cast<uint32_t>(body->end - begin);
    if (!(length < 0xFFFFFFFFu))
        length = 0xFFFFFFFFu;
    uint8_t tag;
    This<void>(addr::cSC4Advice_F408420, &text.body, begin, begin + length, &tag);
    text.vtable = addr::SC4String_Vtable;
    text.zero = 0;
    Virtual<void>(window, 0x128, &text);
    DestroyString(text);
    DestroyString(source.text);
}

// The list of buildings emptied by the inlined code: every building let go,
// every node freed, the sentinel last.
void DestroyList(ListNode* list)
{
    ListNode* node = list->next;
    while (node != list) {
        ListNode* const gone = node;
        node = node->next;
        if (gone->value)
            Release(gone->value);
        Free(gone);
    }
    list->next = list;
    list->previous = list;
    Free(list);
}

// The makers of ui_windows_2.cpp and ui_windows_3.cpp, by address.
uint32_t AddText(char* self, void* parent, uint32_t id, uint32_t x, uint32_t y, void* caption, uint32_t align,
                 uint32_t font, uint32_t red, uint32_t green, uint32_t blue)
{
    return This<uint32_t>(addr::SC4UI_F779660, self, parent, id, x, y, caption, align, font, red, green, blue);
}

uint32_t AddSizedText(char* self, void* parent, uint32_t id, uint32_t x, uint32_t y, uint32_t width, void* caption,
                      uint32_t align, uint32_t font, uint32_t red, uint32_t green, uint32_t blue)
{
    return This<uint32_t>(addr::SC4UI_F779B80, self, parent, id, x, y, width, caption, align, font, red, green,
                          blue);
}

void MakeButton(char* self, void* parent, uint32_t id, uint32_t x, uint32_t y, uint32_t image, uint32_t toggle)
{
    This<void>(addr::SC4UI_F77B960, self, parent, id, x, y, image, uint32_t{0}, toggle);
}

void SetChildValue(char* self, void* window, uint32_t id, void* value)
{
    This<uint32_t>(addr::SC4UI_F7795A0, self, window, id, value);
}

// A hospital's or a school's figures: its use as "served/capacity", its two
// funding levels as percentages and what they cost together. `read` is the
// residential simulator's slot for the kind (27 a hospital, 26 a school).
// The original makes a hospital's record with 0x00414210 and a school's with
// the same code inlined.
void CareFigures(void* residential, void* building, std::size_t read, bool inlined, GameString& usage,
                 float& localPercent, float& secondPercent, int32_t& cost)
{
    CareRecord record{};
    if (inlined) {
        InitString(record.name);
        At<uint32_t>(&record, kCareUnset) = 0;
        At<uint32_t>(&record, kLocalFunding) = 0;
        At<uint32_t>(&record, kSecondFunding) = 0;
    } else {
        This<void*>(addr::cSC4Advisors_F414210, &record);
    }
    if (Virtual<uint8_t>(residential, read, building, &record)) {
        uint32_t served = 0;
        uint32_t other = 0;
        Virtual<void>(residential, 0x70, building, &served, &other);
        Cdecl<void>(addr::cRZBaseString_Sprintf, &usage, "%d/%d", At<uint32_t>(&record, kCareCapacity), served);
        localPercent = Percent(At<float>(&record, kLocalFunding));
        secondPercent = Percent(At<float>(&record, kSecondFunding));
        const int32_t second = Share(At<uint32_t>(&record, kSecondAmount), At<float>(&record, kSecondFunding));
        const int32_t local = Share(At<uint32_t>(&record, kLocalAmount), At<float>(&record, kLocalFunding));
        cost = second + local;
    }
    DestroyString(record.name);
}

// The building and its two percentages appended to the dialog's vector.
void NoteBuilding(char* self, void* occupant, float localPercent, float secondPercent)
{
    AddRef(occupant);
    FundingChange entry;
    entry.second = secondPercent;
    entry.local = localPercent;
    entry.building = occupant;
    ChangeVector& changes = At<ChangeVector>(self, dialog::kBuildings);
    FundingChange* const end = changes.end;
    if (end != changes.capacity) {
        if (end) {
            end->building = occupant;
            AddRef(occupant);
            end->local = localPercent;
            end->second = secondPercent;
        }
        changes.end = changes.end + 1;
    } else {
        uint8_t tag;
        This<void>(addr::SC4MySimAdviceVector_Insert, &changes, end, &entry, &tag, uint32_t{1}, uint32_t{1});
    }
    Release(occupant);
}

// The game's 0x00785740 (thiscall, ret 8). `this` is the dialog showing a
// department's buildings; `window` holds the rows' controls. Asks the
// department for its buildings, and for each one the simulator that runs it
// for its use, its funding and its cost (times 12 with amounts by year); the
// nine from the scroll position on fill the rows, the rows left over are
// hidden, and the sum of all goes to the total's text and the field at
// +0x90. With `first` set the dialog's vector of buildings is emptied and
// filled again with every building and its percentages.
void __fastcall FillBuildings(char* self, void*, void* window, uint8_t first)
{
    GameString amount;
    InitString(amount);
    uint32_t textId = kFirstRowText;
    uint32_t markId = kFirstMark;
    uint32_t sliderId = kFirstSlider;
    GameString name;
    InitString(name);
    GameString usage;
    InitString(usage);

    void* const budget = Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0);
    void* const department = Virtual<void*>(budget, 0x70, At<uint32_t>(self, dialog::kDepartmentId));
    ListNode* list = Cdecl<ListNode*>(addr::SC4_stl_allocate, uint32_t{0xC});
    list->next = list;
    list->previous = list;
    void* const residential = Global<void*>(addr::SC4_Service43DA4);
    void* const fire = Global<void*>(addr::SC4_Service43D78);
    void* const power = Global<void*>(addr::SC4_Service43DA0);
    void* const police = Global<void*>(addr::SC4_PoliceSimulator);
    Virtual<void>(department, 0x80, At<uint32_t>(self, dialog::kDepartment), &list);

    int32_t count = 0;
    for (const ListNode* node = list->next; node != list; node = node->next)
        ++count;
    At<int32_t>(self, dialog::kCostRows) = count;
    int32_t visible = count > kRowsShown ? kRowsShown : count;
    int32_t shown = 0;
    At<uint32_t>(self, dialog::kTotal) = 0;
    At<uint32_t>(self, dialog::kTotal + 4) = 0;
    uint32_t index = 0;
    if (first) {
        ChangeVector& changes = At<ChangeVector>(self, dialog::kBuildings);
        This<void>(addr::SC4MySimAdviceVector_EraseRange, &changes, changes.begin, changes.end);
    }

    for (ListNode* node = list->next; node != list; node = node->next, ++index) {
        void* const occupant = node->value;
        void* building = nullptr;
        int32_t cost = 0;
        float localPercent = 0.0f;
        float secondPercent = 0.0f;
        Virtual<uint8_t>(occupant, 0x0, kOccupantIid, &building);

        const uint32_t kind = At<uint32_t>(self, dialog::kDepartment);
        if (kind == kHealth) {
            if (building)
                CareFigures(residential, building, 0x6C, false, usage, localPercent, secondPercent, cost);
        } else if (kind == kSchools) {
            if (building)
                CareFigures(residential, building, 0x68, true, usage, localPercent, secondPercent, cost);
        } else if (kind == kPolice || kind == kFire) {
            if (building) {
                float factor = 0.0f;
                float held;
                void* const service = kind == kPolice ? police : fire;
                const float* const efficiency =
                    Virtual<const float*>(service, kind == kPolice ? 0x64 : 0x28, &held, building);
                Cdecl<void>(addr::cRZBaseString_Sprintf, &usage, "%2.0f%%",
                            static_cast<double>(*efficiency) * static_cast<double>(100.0f));
                Virtual<void>(service, kind == kPolice ? 0x1C : 0x20, building, &cost, &factor);
                localPercent = Percent(factor);
                cost = Truncate(static_cast<double>(cost) * static_cast<double>(factor) + 0.5);
            }
        } else if (kind == kPower) {
            if (building) {
                uint32_t capacity = 0;
                uint32_t produced = 0;
                float factor = 0.0f;
                Virtual<void>(Global<void*>(addr::SC4_Service43DA0), 0x84, building, &capacity, &produced);
                Cdecl<void>(addr::cRZBaseString_Sprintf, &usage, "%d/%d", produced, capacity);
                if (Virtual<uint8_t>(power, 0xC8, building, &factor)) {
                    localPercent = Percent(factor);
                    int64_t monthly = 0;
                    if (Virtual<uint8_t>(Global<void*>(addr::SC4_Service43DA0), 0xC4, building, &monthly))
                        cost = Truncate(static_cast<double>(monthly) * static_cast<double>(factor) + 0.5);
                }
            }
        }

        if (first)
            NoteBuilding(self, occupant, localPercent, secondPercent);
        if (At<uint32_t>(self, dialog::kSecond) == 1)
            cost *= 12;

        if (index >= At<uint32_t>(self, dialog::kCostScroll) && shown < visible) {
            // The building's name, or a stand-in.
            void* named = nullptr;
            uint32_t hasSecond = 0;
            bool gotName = false;
            if (Virtual<uint8_t>(occupant, 0x0, kBuildingIid, &named)) {
                gotName = Virtual<uint8_t>(named, 0x4C, &name) != 0;
                Release(named);
            }
            if (!gotName) {
                GameString missing;
                This<void*>(addr::SC4String_FromText, &missing, "Error: No name");
                uint8_t tag;
                This<void>(addr::SC4String_AssignRange, &name.body, missing.body.begin, missing.body.end, &tag);
                DestroyString(missing);
            }

            if (const uint32_t purpose = At<uint32_t>(self, dialog::kSecondPurpose)) {
                uint32_t item[8];
                item[7] = 0;
                if (Virtual<uint8_t>(budget, 0xCC, occupant, purpose, &item[0]))
                    hasSecond = 1;
            }

            ShowChild(window, textId, 1);
            ShowChild(window, sliderId, 1);
            ShowChild(window, sliderId + 0x100, hasSecond);
            ShowChild(window, markId, 1);
            ShowChild(window, markId + 1, hasSecond);
            markId += 2;
            SetChildValue(self, window, textId, occupant);
            SetChildValue(self, window, sliderId, occupant);
            SetChildValue(self, window, sliderId + 0x100, occupant);

            void* slider = nullptr;
            if (Virtual<uint8_t>(window, 0x94, sliderId, kSliderIid, &slider))
                Virtual<void>(slider, 0x14, Truncate(static_cast<double>(localPercent)));
            if (At<uint32_t>(self, dialog::kSecondPurpose) != 0) {
                Clear(slider);
                if (Virtual<uint8_t>(window, 0x94, sliderId + 0x100, kSliderIid, &slider))
                    Virtual<void>(slider, 0x14, Truncate(static_cast<double>(secondPercent)));
            }

            SetChildText(window, textId, &name);
            SetChildText(window, textId + 1, &usage);
            Cdecl<void>(addr::SC4_FormatMoney, static_cast<int64_t>(cost), &amount);
            SetChildText(window, textId + 2, &amount);
            textId += 3;
            ++sliderId;
            ++shown;
            if (slider)
                Release(slider);
        }

        const uint64_t total = static_cast<uint64_t>(At<int64_t>(self, dialog::kTotal));
        At<int64_t>(self, dialog::kTotal) =
            static_cast<int64_t>(total + static_cast<uint64_t>(static_cast<int64_t>(cost)));
        if (building)
            Release(building);
    }

    // The rows with no building.
    for (; shown < visible; --visible) {
        ShowChild(window, textId, 0);
        ShowChild(window, textId + 1, 0);
        ShowChild(window, textId + 2, 0);
        textId += 3;
        ShowChild(window, sliderId, 0);
        ShowChild(window, sliderId + 0x100, 0);
        ShowChild(window, markId, 0);
        ShowChild(window, markId + 1, 0);
        markId += 2;
        ++sliderId;
    }

    Cdecl<void>(addr::SC4_FormatMoney, At<int64_t>(self, dialog::kTotal), &amount);
    SetChildText(window, textId + 1, &amount);

    DestroyList(list);
    DestroyString(usage);
    DestroyString(name);
    DestroyString(amount);
}

// The game's 0x007862B0 (thiscall, no arguments; it runs to 0x0078652B, the
// list's entry 0x007864F0 is inside it). Makes the dialog in 0x140 bytes of
// fresh memory and returns it. The fields at +0x24, +0x8C, +0xAC and from
// +0x134 on are left as the memory had them.
char* __fastcall Construct(char* self, void*)
{
    At<std::uintptr_t>(self, 0x0) = addr::SC4UI_DepartmentDialogVtable;
    At<uint32_t>(self, 0x4) = 0;
    At<uint8_t>(self, dialog::kStarted) = 0;
    for (std::size_t offset = 0xC; offset <= 0x20; offset += 4)
        At<uint32_t>(self, offset) = 0;
    for (std::size_t offset = 0x28; offset <= 0x88; offset += 4)
        At<uint32_t>(self, offset) = 0;
    for (std::size_t offset = 0x90; offset <= 0xA8; offset += 4)
        At<uint32_t>(self, offset) = 0;
    At<uint32_t>(self, dialog::kMultiplier) = 1;
    At<uint32_t>(self, dialog::kMultiplier + 4) = 0;
    InitString(At<GameString>(self, dialog::kIncomeHeading));
    InitString(At<GameString>(self, dialog::kIncomeAmount));
    InitString(At<GameString>(self, dialog::kExpenseHeading));
    InitString(At<GameString>(self, dialog::kExpenseAmount));
    InitString(At<GameString>(self, dialog::kTotalLabel));
    At<uint32_t>(self, dialog::kIncomeScroll) = 0;
    At<uint32_t>(self, dialog::kCostScroll) = 0;
    At<uint32_t>(self, dialog::kIncomeRows) = 0;
    At<uint32_t>(self, dialog::kCostRows) = 0;
    At<uint8_t>(self, dialog::kFlag) = 0;

    ListNode*& list = At<ListNode*>(self, dialog::kList);
    list = nullptr;
    ListNode* const sentinel = This<ListNode*>(addr::SC4List_AllocateNodes1, &list, uint32_t{1}, uint32_t{0});
    sentinel->next = sentinel;
    sentinel->previous = sentinel;
    list = sentinel;

    AssignLocalized(At<GameString>(self, dialog::kTotalLabel).body, kTextGroup, 0x8A5796D1);
    return self;
}

// The game's 0x00786530 (thiscall, ret 4). `this` is a department dialog on
// show; `department` is the ID kept on the funding row that was pressed.
// Unless one is up already, a second dialog is made and held at +0x1C, its
// window made for kind 0 with this dialog's month or year setting and the
// department, and run as a modal window; then it is ended (0x0077A1A0) and
// dropped. The argument's stack slot is where the original keeps the window
// maker's interface.
void __fastcall OpenBuildings(char* self, void*, uint32_t department)
{
    void* window = nullptr;
    if (At<void*>(self, dialog::kHeld))
        return;

    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x140});
    void* const made = memory ? This<void*>(addr::SC4UI_F7862B0, memory) : nullptr;
    Assign(At<void*>(self, dialog::kHeld), made);

    if (This<uint8_t>(addr::SC4UI_F779E50, At<void*>(self, dialog::kHeld), &window, uint32_t{0},
                      At<uint32_t>(self, dialog::kSecond), department)) {
        Virtual<void>(window, 0x114);
        Virtual<void>(window, 0x110, uint32_t{0x800}, uint32_t{1});
        Virtual<void>(window, 0x5C);
        Virtual<void>(window, 0x110, uint32_t{0x800}, uint32_t{0});
        Virtual<void>(window, 0x110, uint32_t{0x100}, uint32_t{1});
        void* gen = nullptr;
        if (Virtual<uint8_t>(window, 0x0, kWinGenIid, &gen)) {
            Virtual<void>(gen, 0x18, uint32_t{0x1000}, uint32_t{1});
            Virtual<void>(gen, 0x18, uint32_t{0x2000}, uint32_t{1});
            Virtual<void>(gen, 0x18, uint32_t{0x4000}, uint32_t{1});
        }
        Virtual<void>(Cdecl<void*>(addr::SC4_GetWinManager), 0xA4, window); // modal: returns when the window closes
        Release(window);
        if (gen)
            Release(gen);
    }

    This<void>(addr::SC4UI_F77A1A0, At<void*>(self, dialog::kHeld));
    Clear(At<void*>(self, dialog::kHeld));
}

// The game's 0x00786690 (thiscall, ret 4; it runs to 0x0078769F). `this` is
// a department dialog of kind 0 whose field at +0x24 holds a department's
// ID; `parent` is its window. Works out the department's purpose and, for
// hospitals and schools, the purpose of the second funding level; with no
// building of that purpose the page only says so (0x0077BEC0). Otherwise:
// the background for up to nine rows, the title, four column headings, per
// row a name that is a button, two pictures, two sliders and two texts, the
// total's label and text, the two closing buttons with their captions, the
// two scroll buttons, and the rows filled (0x00785740).
void __fastcall BuildBuildingsPage(char* self, void*, void* parent)
{
    uint32_t markId = kFirstMark;
    GameString title;
    InitString(title);
    GameString nameHeading;
    InitString(nameHeading);
    GameString localHeading;
    InitString(localHeading);
    GameString secondHeading;
    InitString(secondHeading);
    GameString useHeading;
    InitString(useHeading);

    Cdecl<void*>(addr::SC4_Singleton913C72); // asked for and not used
    void* const budget = Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0);
    void* const department = Virtual<void*>(budget, 0x70, At<uint32_t>(self, dialog::kDepartmentId));
    ListNode* list = Cdecl<ListNode*>(addr::SC4_stl_allocate, uint32_t{0xC});
    list->next = list;
    list->previous = list;

    At<uint32_t>(self, dialog::kDepartment) = 0;
    At<uint32_t>(self, dialog::kSecondPurpose) = 0;
    switch (At<uint32_t>(self, dialog::kDepartmentId)) {
    case 0x28F55A9F:
        At<uint32_t>(self, dialog::kDepartment) = kFire;
        At<uint32_t>(self, dialog::kSecondPurpose) = 0;
        AssignLocalizedWhole(title, 0x4C04A33A);
        break;
    case 0x4A538CC6:
    case 0x09188F4C:
        At<uint32_t>(self, dialog::kDepartment) = kSchools;
        At<uint32_t>(self, dialog::kSecondPurpose) = kSchoolBuses;
        AssignLocalized(title.body, kTextGroup, 0x6C04A35E);
        AssignLocalized(secondHeading.body, kTextGroup, 0x0C04A37A);
        break;
    case 0x8910BC8A:
        At<uint32_t>(self, dialog::kDepartment) = kPower;
        At<uint32_t>(self, dialog::kSecondPurpose) = 0;
        AssignLocalizedWhole(title, 0x2C04A366);
        break;
    case 0xA2963983:
    case 0xA2963984:
        At<uint32_t>(self, dialog::kDepartment) = kPolice;
        At<uint32_t>(self, dialog::kSecondPurpose) = 0;
        AssignLocalizedWhole(title, 0x8C04A34B);
        break;
    case 0xAA538CB3:
    case 0x09188F42:
        At<uint32_t>(self, dialog::kDepartment) = kHealth;
        At<uint32_t>(self, dialog::kSecondPurpose) = kAmbulances;
        AssignLocalized(title.body, kTextGroup, 0x4C04A355);
        AssignLocalized(secondHeading.body, kTextGroup, 0x6C04A36E);
        break;
    default:
        break;
    }

    Virtual<void>(department, 0x80, At<uint32_t>(self, dialog::kDepartment), &list);
    int32_t count = 0;
    for (const ListNode* node = list->next; node != list; node = node->next)
        ++count;
    At<int32_t>(self, dialog::kCostRows) = count;

    // The sliders' top value and how far right that puts the pictures.
    const float top = Percent(*Virtual<const float*>(department, 0x30));
    const int32_t shift =
        Truncate(static_cast<double>(9000.0f) / static_cast<double>(top) - static_cast<double>(12.0f));

    ImageLoader loader;
    This<void*>(addr::SC4ImageLoader_ConstructParts, &loader, uint32_t{0x856DDBAC}, uint32_t{0x46A006B0},
                uint32_t{0x2BFEB0CC}, uint32_t{1}, uint32_t{0});
    At<int32_t>(self, dialog::kWidth) = Virtual<const int32_t*>(loader.image, 0x30)[2];
    At<int32_t>(self, dialog::kRowHeight) = Virtual<const int32_t*>(loader.image, 0x30)[3] / 2;

    if (At<uint32_t>(self, dialog::kCostRows) == 0) {
        LocalizedString nothing;
        This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &nothing, kTextGroup, 0x4A5CB4B6u, uint32_t{1});
        This<void>(addr::SC4UI_F77BEC0, self, parent, &title, &nothing);
        At<uint8_t>(self, dialog::kStarted) = 1;
        DestroyString(nothing.text);
        This<void>(addr::SC4ImageLoader_Destruct, &loader);
        This<void>(addr::SC4RefList_Clear, &list);
        if (list)
            Free(list);
        DestroyString(useHeading);
        DestroyString(secondHeading);
        DestroyString(localHeading);
        DestroyString(nameHeading);
        DestroyString(title);
        return;
    }

    int32_t rows = At<int32_t>(self, dialog::kCostRows);
    rows = rows > kRowsShown ? kRowsShown : rows;
    if (static_cast<uint32_t>(rows) > static_cast<uint32_t>(kRowsShown))
        rows = kRowsShown;

    This<void>(addr::SC4UI_F77A960, self, parent, rows);
    const int32_t listTop = At<int32_t>(self, dialog::kSecondTop);
    AddText(self, At<void*>(self, dialog::kPage), 0x0ABCDE00, 0x14, 8, &title, 0, 0x4A809916, 0x44, 0x55, 0x66);
    AssignLocalized(nameHeading.body, kTextGroup, 0xAC04A30F);
    AssignLocalized(localHeading.body, kTextGroup, 0x4C04A321);
    AssignLocalized(useHeading.body, kTextGroup, 0xCC04A330);

    AddText(self, At<void*>(self, dialog::kPage), 0x0ABCDE01, 0x15,
            At<uint32_t>(self, dialog::kSecondTop) - At<uint32_t>(self, dialog::kRowHeight), &nameHeading, 0,
            kHeadingFont, 0x44, 0x55, 0x66);
    AddSizedText(self, At<void*>(self, dialog::kPage), 0x0ABCDE02, 0x96,
                 At<uint32_t>(self, dialog::kSecondTop) - At<uint32_t>(self, dialog::kRowHeight), 0xBE,
                 &localHeading, 3, kHeadingFont, 0x44, 0x55, 0x66);
    AddSizedText(self, At<void*>(self, dialog::kPage), 0x0ABCDE03, 0xFF,
                 At<uint32_t>(self, dialog::kSecondTop) - At<uint32_t>(self, dialog::kRowHeight), 0xBE,
                 &secondHeading, 3, kHeadingFont, 0x44, 0x55, 0x66);
    AddSizedText(self, At<void*>(self, dialog::kPage), 0x0ABCDE04, 0x15E,
                 At<uint32_t>(self, dialog::kSecondTop) - At<uint32_t>(self, dialog::kRowHeight), 0xDC, &useHeading,
                 3, kHeadingFont, 0x44, 0x55, 0x66);
    AddSizedText(self, At<void*>(self, dialog::kPage), 0x0ABCDE05, 0x1D6,
                 At<uint32_t>(self, dialog::kSecondTop) - At<uint32_t>(self, dialog::kRowHeight), 0x87,
                 self + dialog::kExpenseAmount, 6, kHeadingFont, 0x44, 0x55, 0x66);

    uint32_t textId = kFirstRowText;
    At<uint32_t>(self, dialog::kTotal) = 0;
    At<uint32_t>(self, dialog::kTotal + 4) = 0;
    if (rows > 0) {
        const uint32_t localX = static_cast<uint32_t>(shift) + 0xC8;
        const uint32_t secondX = static_cast<uint32_t>(shift) + 0x131;
        const int32_t range = Truncate(static_cast<double>(top));
        int32_t row = 0;
        do {
            // The building's name: a button that is only its caption.
            GameString empty;
            InitString(empty);
            This<uint32_t>(addr::SC4UI_F779CA0, self, parent, textId, uint32_t{0x15},
                           At<uint32_t>(self, dialog::kSecondTop), uint32_t{0xB1}, &empty, uint32_t{1}, 0xAC0C7170u,
                           uint32_t{0x44}, uint32_t{0x55}, uint32_t{0x66});
            ++textId;
            DestroyString(empty);

            // A picture and a slider for each funding level.
            This<void>(addr::SC4UI_F77A250, self, 0x140155C8u, localX, At<uint32_t>(self, dialog::kSecondTop), parent,
                       markId);
            ++markId;
            This<void>(addr::SC4UI_F7794E0, self, parent, static_cast<uint32_t>(row) + kFirstSlider, uint32_t{0xC8},
                       At<uint32_t>(self, dialog::kSecondTop), uint32_t{0x5A}, uint32_t{1}, uint32_t{0}, range,
                       uint32_t{0});
            This<void>(addr::SC4UI_F77A250, self, 0x140155C8u, secondX, At<uint32_t>(self, dialog::kSecondTop),
                       parent, markId);
            ++markId;
            This<void>(addr::SC4UI_F7794E0, self, parent, static_cast<uint32_t>(row) + kFirstSlider + 0x100,
                       uint32_t{0x131}, At<uint32_t>(self, dialog::kSecondTop), uint32_t{0x5A}, uint32_t{1},
                       uint32_t{0}, range, uint32_t{0});

            // Its use and its cost.
            InitString(empty);
            AddSizedText(self, At<void*>(self, dialog::kPage), textId, 0x190, At<uint32_t>(self, dialog::kSecondTop),
                         0x78, &empty, 3, kRowFont, 0x44, 0x55, 0x66);
            ++textId;
            DestroyString(empty);
            InitString(empty);
            AddSizedText(self, At<void*>(self, dialog::kPage), textId, 0x208, At<uint32_t>(self, dialog::kSecondTop),
                         0x55, &empty, 6, kRowFont, 0x44, 0x55, 0x66);
            ++textId;
            DestroyString(empty);

            At<uint32_t>(self, dialog::kSecondTop) += At<uint32_t>(self, dialog::kRowHeight);
            ++row;
        } while (row < rows);
    }
    if (!(rows & 1))
        At<uint32_t>(self, dialog::kSecondTop) += At<uint32_t>(self, dialog::kRowHeight);

    // The total's label and its text, in red.
    AddText(self, At<void*>(self, dialog::kPage), textId, 0x159, At<uint32_t>(self, dialog::kSecondTop),
            self + dialog::kTotalLabel, 0, kHeadingFont, 0x44, 0x55, 0x66);
    ++textId;
    {
        GameString empty;
        InitString(empty);
        AddSizedText(self, At<void*>(self, dialog::kPage), textId, 0x208, At<uint32_t>(self, dialog::kSecondTop),
                     0x55, &empty, 6, kHeadingFont, 0x77, 0, 0);
        DestroyString(empty);
    }

    // The two closing buttons and their captions.
    MakeButton(self, parent, 0x67, 0xE, At<uint32_t>(self, dialog::kHeight) - 0x28, 0x144161EB, 1);
    Virtual<void>(Child(parent, 0x67), 0xD4, uint32_t{0xB4}, uint32_t{0x1E});
    Virtual<void>(Child(parent, 0x167), 0xD4, uint32_t{0xB4}, uint32_t{0x1E});
    SetLocalizedCopy(Child(parent, 0x67), 0x0A554AE8, 0x2A6CD8FD);
    Virtual<void>(Child(parent, 0x67), 0x158, parent);

    {
        const uint32_t y = At<uint32_t>(self, dialog::kHeight) - 0x28;
        const uint32_t x = At<uint32_t>(self, dialog::kWidth) - 0xC3;
        MakeButton(self, parent, 0x6D, x, y, 0x144161EB, 1);
    }
    Virtual<void>(Child(parent, 0x6D), 0xD4, uint32_t{0xB4}, uint32_t{0x1E});
    Virtual<void>(Child(parent, 0x16D), 0xD4, uint32_t{0xB4}, uint32_t{0x1E});
    SetLocalizedCopy(Child(parent, 0x6D), 0x6A231EAA, 0x6A56B0BB);
    Virtual<void>(Child(parent, 0x6D), 0x158, parent);

    // The scroll buttons: up is off to begin with, down too when every
    // building fits.
    MakeButton(self, parent, 0x453, At<uint32_t>(self, dialog::kWidth) - 0x21, static_cast<uint32_t>(listTop) + 4,
               0x140155CB, 0);
    Virtual<void>(Child(parent, 0x453), 0x158, parent);
    MakeButton(self, parent, 0x454, At<uint32_t>(self, dialog::kWidth) - 0x21,
               static_cast<uint32_t>(listTop) + At<uint32_t>(self, dialog::kRowHeight) * 8 + 4, 0x140155CC, 0);
    Virtual<void>(Child(parent, 0x454), 0x158, parent);
    SetFlag(Child(parent, 0x453), 1, 0);
    if (At<uint32_t>(self, dialog::kCostRows) <= static_cast<uint32_t>(kRowsShown))
        SetFlag(Child(parent, 0x454), 1, 0);

    FillBuildings(self, nullptr, parent, 1);
    At<uint8_t>(self, dialog::kStarted) = 1;
    This<void>(addr::SC4ImageLoader_Destruct, &loader);
    DestroyList(list);
    DestroyString(useHeading);
    DestroyString(secondHeading);
    DestroyString(localHeading);
    DestroyString(nameHeading);
    DestroyString(title);
}

bool InstallDepartment1(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[4] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F785740, reinterpret_cast<void*>(&FillBuildings)},
        {addr::SC4UI_F7862B0, reinterpret_cast<void*>(&Construct)},
        {addr::SC4UI_F786530, reinterpret_cast<void*>(&OpenBuildings)},
        {addr::SC4UI_F786690, reinterpret_cast<void*>(&BuildBuildingsPage)},
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

const bool g_department1Registered = registry::Register({
    "ui.department.part1",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F786690,
    "Our department dialog, the page of one department's buildings: the dialog's constructor, the second dialog "
    "run over the first, the page built and its rows filled (docs/ui.md)",
    false,
    InstallDepartment1,
});

} // namespace
} // namespace opensc4::ui
