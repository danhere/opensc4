// The budget panel of the city view, third part (0x00767270 to 0x007693E0),
// and the first function of the class after it.
//
// The panel (0x1A0 bytes; vtables 0x00AB4A40, 0x00AB4A2C at +8, 0x00AB4A1C at
// +0xC; the city view's interface object keeps it at +0x2E8) shows the city's
// funds, income and expenses by month or by year on two pages, a tax rate
// window with twelve spinners and edit fields, a loan list, and opens a
// dialog for each department. This file holds:
//
//   - ReadBudget (0x00767270): the figures read from the budget simulator and
//     two services into the panel's fields;
//   - Refresh (0x00767990): the figures written to the text windows;
//   - the tax window shown (0x00768130) and hidden (0x007680D0), a department
//     dialog run (0x00768460), the page shown (0x00768670);
//   - the message handler (0x00768620) and the window procedure (0x00768750);
//   - QueryInterface of the next class (0x007693E0, vtable 0x00AB4A78).
//
// Replacement, off by default:
//
//   ui.dialogs.part3: everything above.
//
// Windows, texts, spinners and the department dialog are the game's, called
// through their vtables or by address; the panel's other methods are in the
// neighbouring parts and are called by address.
#include "core/address_check.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>

namespace opensc4::ui {
namespace {

namespace panel {
constexpr std::size_t kWinProc = 0x08;
constexpr std::size_t kTarget = 0x0C;       // the message target part
constexpr std::size_t kWindow = 0x10;       // the window's cIGZWinGen
constexpr std::size_t kBudget = 0x14;       // the budget simulator
constexpr std::size_t kDepartment = 0x18;   // the department dialog while it runs
constexpr std::size_t kLoans = 0x1C;        // a vector of 0x20-byte loan records
constexpr std::size_t kFunds = 0x28;        // int64 from here on
constexpr std::size_t kIncome = 0x30;       // by month, by year at +8
constexpr std::size_t kExpense = 0x40;      // by month, by year at +8
constexpr std::size_t kTaxFirst = 0x50;     // tax types 0 to 2: month, year
constexpr std::size_t kTaxSecond = 0x60;    // tax types 3 to 7
constexpr std::size_t kTaxThird = 0x70;     // tax types 8 to 11
constexpr std::size_t kDealsIn = 0x80;      // the deals of the service at 0x00B43D90 that pay: month, year
constexpr std::size_t kDealsOut = 0x90;     // and those that cost
constexpr std::size_t kOtherIn = 0xA0;      // the month end service's slot 32, by month
constexpr std::size_t kOtherOut = 0xA8;     // its slot 33, by month
constexpr std::size_t kOtherInYear = 0xB0;
constexpr std::size_t kOtherOutYear = 0xB8;
constexpr std::size_t kGroupA = 0xC0;       // group 0x0A5A72D1, second amount: month, year
constexpr std::size_t kGroupB = 0xD0;       // group 0xAA369059, second amount
constexpr std::size_t kGroupC = 0xE0;       // group 0xAA369059, first amount
constexpr std::size_t kGroupD = 0xF0;       // group 0x4A357B40
constexpr std::size_t kGroupE = 0x100;      // group 0x6A357B7F
constexpr std::size_t kGroupF = 0x110;      // group 0x4A357EAF
constexpr std::size_t kGroupG = 0x120;      // group 0x6A357B96
constexpr std::size_t kGroupH = 0x130;      // group 0xEA357ECB or 0xEA597195
constexpr std::size_t kBonds = 0x140;       // the bond payments: month, year
constexpr std::size_t kRates = 0x150;       // twelve floats: the tax rates in tenths of a percent
constexpr std::size_t kLoanScroll = 0x180;  // the first loan row on show
constexpr std::size_t kLoanCount = 0x184;
constexpr std::size_t kLoanTotal = 0x18C;
constexpr std::size_t kLoanAmount = 0x190;  // the amount in the loan spinner
constexpr std::size_t kPage = 0x194;        // byte: 0 the short page, 1 the long one
constexpr std::size_t kYearly = 0x198;      // 0 amounts by month, 1 by year
constexpr std::size_t kRegistered = 0x19C;  // byte: registered for its messages
constexpr std::size_t kPageShown = 0x19D;   // byte
constexpr std::size_t kDirty = 0x19E;       // byte: a budget message came since the last refresh
} // namespace panel

constexpr uint32_t kTextIid = 0x212CDC1F;
constexpr uint32_t kSpinnerIid = 0x612CE0C3;
constexpr uint32_t kEditIid = 0x231A1C57;
constexpr uint32_t kButtonIid = 0x8810;
constexpr uint32_t kWinGenIid = 0x5386D516;

constexpr uint32_t kShortPage = 0xAA3AC000;
constexpr uint32_t kLongPage = 0xAA3AC001;
constexpr uint32_t kTaxWindow = 0xAA3AC002;

// A window message as window procedures get it.
struct WinMessage {
    uint32_t type;
    uint32_t a;
    uint32_t b;
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

// One budget group as the budget simulator's slot 29 lists them.
struct BudgetGroup {
    uint32_t id;
    GameString name;
    int64_t first;
    int64_t second;
};
static_assert(sizeof(BudgetGroup) == 0x28);

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

// The game's inlined reference holder: empty it.
void Clear(void*& holder)
{
    if (void* const object = holder) {
        holder = nullptr;
        Release(object);
    }
}

// The game's inlined reference holder: assign.
void Assign(void*& holder, void* object)
{
    void* const old = holder;
    if (object != old) {
        if (object)
            AddRef(object);
        holder = object;
        if (old)
            Release(old);
    }
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
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(begin));
}

// The runtime's 64-bit multiply (0x009F0D10) by twelve.
int64_t Year(int64_t month)
{
    return static_cast<int64_t>(static_cast<uint64_t>(month) * 12u);
}

// a - b + c as the original adds it up.
int64_t Net(int64_t a, int64_t b, int64_t c)
{
    return static_cast<int64_t>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b) + static_cast<uint64_t>(c));
}

// A value times ten, rounded to a whole number the way the original does it:
// 1.5 * 2^23 is added, the sum stored as a float, and the bias taken off its
// bits.
uint32_t Tenths(double timesTen)
{
    const float biased = static_cast<float>(timesTen + 12582912.0);
    uint32_t bits;
    std::memcpy(&bits, &biased, sizeof(bits));
    return bits + 0xB4C00000u;
}

// The window the panel's child windows are looked up in.
void* Root(char* self)
{
    void* const window = Virtual<void*>(At<void*>(self, panel::kWindow), 0xC);
    return Virtual<void*>(window, 0x2C);
}

// The panel's 0x00765800: a child window's text set to an amount of money.
void SetMoney(char* self, uint32_t id, int64_t amount)
{
    This<void>(addr::SC4UI_F765800, self, id, amount);
}

// The panel's 0x00764D50: "whole.tenth" written to a string.
void FormatRate(char* self, uint32_t whole, uint32_t tenth, GameString* text)
{
    This<void>(addr::SC4UI_F764D50, self, whole, tenth, text);
}

// The window manager told which window has the keyboard, by way of the
// service at 0x00B43DC4.
void RestoreFocus()
{
    void* const service = Global<void*>(addr::SC4_Service43DC4);
    void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
    Virtual<void>(manager, 0x94, Virtual<void*>(service, 0xC));
}

// The game's 0x00767270. `this` is the panel. Reads everything the panel
// shows into its fields, each amount by month and by year. Without a budget
// simulator every field is cleared.
void __fastcall ReadBudget(char* self, void*)
{
    if (!At<void*>(self, panel::kBudget))
        Assign(At<void*>(self, panel::kBudget), Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0));

    void* budget = At<void*>(self, panel::kBudget);
    if (!budget) {
        for (std::size_t at = panel::kFunds; at < panel::kRates; at += 8)
            At<int64_t>(self, at) = 0;
        return;
    }

    At<int64_t>(self, panel::kFunds) = Virtual<int64_t>(budget, 0x18);
    budget = At<void*>(self, panel::kBudget);
    At<int64_t>(self, panel::kIncome) = Virtual<int64_t>(budget, 0x40);
    budget = At<void*>(self, panel::kBudget);
    At<int64_t>(self, panel::kIncome + 8) = Virtual<int64_t>(budget, 0x2C);
    budget = At<void*>(self, panel::kBudget);
    At<int64_t>(self, panel::kExpense) = Virtual<int64_t>(budget, 0x38);
    budget = At<void*>(self, panel::kBudget);
    At<int64_t>(self, panel::kExpense + 8) = Virtual<int64_t>(budget, 0x34);

    budget = At<void*>(self, panel::kBudget);
    uint64_t sum = static_cast<uint64_t>(Virtual<int64_t>(budget, 0x48, int32_t{2}));
    sum += static_cast<uint64_t>(Virtual<int64_t>(budget, 0x48, int32_t{1}));
    sum += static_cast<uint64_t>(Virtual<int64_t>(budget, 0x48, int32_t{0}));
    At<int64_t>(self, panel::kTaxFirst) = static_cast<int64_t>(sum);
    At<int64_t>(self, panel::kTaxFirst + 8) = Year(static_cast<int64_t>(sum));

    budget = At<void*>(self, panel::kBudget);
    sum = static_cast<uint64_t>(Virtual<int64_t>(budget, 0x48, int32_t{5}));
    sum += static_cast<uint64_t>(Virtual<int64_t>(budget, 0x48, int32_t{4}));
    sum += static_cast<uint64_t>(Virtual<int64_t>(budget, 0x48, int32_t{3}));
    sum += static_cast<uint64_t>(Virtual<int64_t>(budget, 0x48, int32_t{7}));
    sum += static_cast<uint64_t>(Virtual<int64_t>(budget, 0x48, int32_t{6}));
    At<int64_t>(self, panel::kTaxSecond) = static_cast<int64_t>(sum);
    At<int64_t>(self, panel::kTaxSecond + 8) = Year(static_cast<int64_t>(sum));

    budget = At<void*>(self, panel::kBudget);
    sum = static_cast<uint64_t>(Virtual<int64_t>(budget, 0x48, int32_t{8}));
    sum += static_cast<uint64_t>(Virtual<int64_t>(budget, 0x48, int32_t{0xA}));
    sum += static_cast<uint64_t>(Virtual<int64_t>(budget, 0x48, int32_t{0xB}));
    sum += static_cast<uint64_t>(Virtual<int64_t>(budget, 0x48, int32_t{9}));
    At<int64_t>(self, panel::kTaxThird) = static_cast<int64_t>(sum);
    At<int64_t>(self, panel::kTaxThird + 8) = Year(static_cast<int64_t>(sum));

    // The deals of the service at 0x00B43D90: the active ones, split by sign.
    At<int64_t>(self, panel::kDealsIn) = 0;
    At<int64_t>(self, panel::kDealsOut) = 0;
    void* const deals = Global<void*>(addr::SC4_Service43D90);
    uint32_t count = Virtual<uint32_t>(deals, 0x2C);
    uint32_t* const ids = Cdecl<uint32_t*>(addr::SC4_operator_new, count << 2);
    Virtual<void>(deals, 0x30, ids, &count);
    for (uint32_t i = 0; i < count; ++i) {
        void* const deal = Virtual<void*>(deals, 0x50, ids[i]);
        const int32_t amount = Virtual<int32_t>(deal, 0x14);
        if (Virtual<uint8_t>(deals, 0x20, ids[i])) {
            if (amount >= 0) {
                At<int64_t>(self, panel::kDealsIn) += static_cast<int64_t>(amount);
            } else {
                const int32_t cost = static_cast<int32_t>(0u - static_cast<uint32_t>(amount));
                At<int64_t>(self, panel::kDealsOut) += static_cast<int64_t>(cost);
            }
        }
    }
    if (ids)
        Cdecl<void>(addr::SC4_operator_delete, static_cast<void*>(ids));
    At<int64_t>(self, panel::kDealsIn + 8) = Year(At<int64_t>(self, panel::kDealsIn));
    At<int64_t>(self, panel::kDealsOut + 8) = Year(At<int64_t>(self, panel::kDealsOut));

    int64_t other = Virtual<int64_t>(Global<void*>(addr::SC4_MonthEndService), 0x80);
    At<int64_t>(self, panel::kOtherIn) = other;
    At<int64_t>(self, panel::kOtherInYear) = Year(other);
    other = Virtual<int64_t>(Global<void*>(addr::SC4_MonthEndService), 0x84);
    At<int64_t>(self, panel::kOtherOut) = other;
    At<int64_t>(self, panel::kOtherOutYear) = Year(other);

    // The budget groups; the simulator fills the vector.
    BudgetGroup* groups[3] = {nullptr, nullptr, nullptr};
    Virtual<void>(At<void*>(self, panel::kBudget), 0x74, &groups[0]);
    for (BudgetGroup* group = groups[0]; group != groups[1]; ++group) {
        std::size_t field = 0;
        switch (group->id) {
        case 0x4A357B40:
            field = panel::kGroupD;
            break;
        case 0x0A5A72D1:
            At<int64_t>(self, panel::kGroupA) = group->second;
            At<int64_t>(self, panel::kGroupA + 8) = Year(group->second);
            break;
        case 0x4A357EAF:
            field = panel::kGroupF;
            break;
        case 0x6A357B7F:
            field = panel::kGroupE;
            break;
        case 0x6A357B96:
            field = panel::kGroupG;
            break;
        case 0xEA357ECB:
        case 0xEA597195:
            field = panel::kGroupH;
            break;
        case 0xAA369059:
            At<int64_t>(self, panel::kGroupB) = group->second;
            At<int64_t>(self, panel::kGroupB + 8) = Year(group->second);
            field = panel::kGroupC;
            break;
        default:
            break;
        }
        if (field) {
            At<int64_t>(self, field) = group->first;
            At<int64_t>(self, field + 8) = Year(group->first);
        }
    }

    const int64_t bonds = Virtual<int64_t>(At<void*>(self, panel::kBudget), 0xB4);
    At<int64_t>(self, panel::kBonds) = bonds;
    At<int64_t>(self, panel::kBonds + 8) = Year(bonds);

    for (BudgetGroup* group = groups[0]; group != groups[1]; ++group)
        DestroyString(group->name);
    if (groups[0])
        Cdecl<void>(addr::SC4_Free, static_cast<void*>(groups[0]));
}

// A text window of `root` given a colour, when it is there. The reference is
// left in `text`, as in the original.
void SetColour(void* root, uint32_t id, void*& text, uint32_t colour)
{
    if (Virtual<uint8_t>(root, 0x94, id, kTextIid, &text))
        Virtual<void>(text, 0x24, uint32_t{0}, colour);
}

// The long page's rows from the tax income down, by month (`year` 0) or by
// year (8). The two amounts of the month end service lie apart.
void SetRows(char* self, std::size_t year)
{
    SetMoney(self, 0xAA3AC112, At<int64_t>(self, panel::kTaxFirst + year));
    SetMoney(self, 0xAA3AC113, At<int64_t>(self, panel::kTaxSecond + year));
    SetMoney(self, 0xAA3AC114, At<int64_t>(self, panel::kTaxThird + year));
    SetMoney(self, 0xAA3AC115, At<int64_t>(self, panel::kDealsIn + year));
    SetMoney(self, 0xAA3AC11F, At<int64_t>(self, panel::kDealsOut + year));
    SetMoney(self, 0xAA3AC120, At<int64_t>(self, year ? panel::kOtherInYear : panel::kOtherIn));
    SetMoney(self, 0xAA3AC121, At<int64_t>(self, year ? panel::kOtherOutYear : panel::kOtherOut));
    SetMoney(self, 0xAA3AC122, At<int64_t>(self, panel::kGroupA + year));
    SetMoney(self, 0xAA3AC123, At<int64_t>(self, panel::kGroupB + year));
    SetMoney(self, 0xAA3AC124, At<int64_t>(self, panel::kGroupC + year));
    SetMoney(self, 0xAA3AC125, At<int64_t>(self, panel::kGroupD + year));
    SetMoney(self, 0xAA3AC126, At<int64_t>(self, panel::kGroupE + year));
    SetMoney(self, 0xAA3AC127, At<int64_t>(self, panel::kGroupF + year));
    SetMoney(self, 0xAA3AC128, At<int64_t>(self, panel::kGroupG + year));
    SetMoney(self, 0xAA3AC129, At<int64_t>(self, panel::kGroupH + year));
    SetMoney(self, 0xAA3AC12A, At<int64_t>(self, panel::kBonds + year));
}

// The game's 0x00767990. `this` is the panel. Reads the budget and writes the
// amounts of the page on show to its text windows. By month, the funds and
// the month's balance are also coloured: one colour below zero, another from
// zero up. By year nothing is coloured.
void __fastcall Refresh(char* self, void*)
{
    void* const root = Root(self);
    ReadBudget(self, nullptr);
    void* colours = Cdecl<void*>(addr::SC4_Singleton913C1A);
    const uint32_t below = Virtual<uint32_t>(colours, 0x88, uint32_t{0x77}, uint32_t{0}, uint32_t{0});
    colours = Cdecl<void*>(addr::SC4_Singleton913C1A);
    const uint32_t plain = Virtual<uint32_t>(colours, 0x88, uint32_t{0x44}, uint32_t{0x55}, uint32_t{0x66});

    if (At<uint32_t>(self, panel::kYearly)) {
        const bool longPage = At<uint8_t>(self, panel::kPage) != 0;
        SetMoney(self, longPage ? 0xAA3AC10E : 0xAA3AC100, At<int64_t>(self, panel::kFunds));
        SetMoney(self, longPage ? 0xAA3AC10F : 0xAA3AC101, At<int64_t>(self, panel::kIncome + 8));
        SetMoney(self, longPage ? 0xAA3AC110 : 0xAA3AC102, At<int64_t>(self, panel::kExpense + 8));
        SetMoney(self, longPage ? 0xAA3AC111 : 0xAA3AC103,
                 Net(At<int64_t>(self, panel::kFunds), At<int64_t>(self, panel::kExpense + 8),
                     At<int64_t>(self, panel::kIncome + 8)));
        if (longPage)
            SetRows(self, 8);
        return;
    }

    SetMoney(self, 0xAA3AC100, At<int64_t>(self, panel::kFunds));
    SetMoney(self, 0xAA3AC10E, At<int64_t>(self, panel::kFunds));
    uint32_t colour = At<int64_t>(self, panel::kFunds) >= 0 ? plain : below;
    void* text = nullptr;
    if (!At<uint8_t>(self, panel::kPage)) {
        SetColour(root, 0xAA3AC100, text, colour);
        Clear(text);
        SetColour(root, 0xAA3AC107, text, colour);
        SetMoney(self, 0xAA3AC101, At<int64_t>(self, panel::kIncome));
        SetMoney(self, 0xAA3AC102, At<int64_t>(self, panel::kExpense));
        SetMoney(self, 0xAA3AC103,
                 Net(At<int64_t>(self, panel::kFunds), At<int64_t>(self, panel::kExpense),
                     At<int64_t>(self, panel::kIncome)));
    } else {
        SetColour(root, 0xAA3AC10E, text, colour);
        Clear(text);
        SetColour(root, 0xAA3AC10D, text, colour);
        SetMoney(self, 0xAA3AC10F, At<int64_t>(self, panel::kIncome));
        SetMoney(self, 0xAA3AC110, At<int64_t>(self, panel::kExpense));
        SetMoney(self, 0xAA3AC111,
                 Net(At<int64_t>(self, panel::kFunds), At<int64_t>(self, panel::kExpense),
                     At<int64_t>(self, panel::kIncome)));
    }

    const int64_t balance =
        Net(At<int64_t>(self, panel::kFunds), At<int64_t>(self, panel::kExpense), At<int64_t>(self, panel::kIncome));
    colour = balance >= 0 ? plain : below;
    if (!At<uint8_t>(self, panel::kPage)) {
        Clear(text);
        SetColour(root, 0xAA3AC103, text, colour);
        Clear(text);
        SetColour(root, 0xAA3AC104, text, colour);
    } else {
        Clear(text);
        SetColour(root, 0xAA3AC111, text, colour);
        Clear(text);
        SetColour(root, 0xAA3AC10A, text, colour);
        SetRows(self, 0);
    }
    if (text)
        Release(text);
}

// The game's 0x007680D0. `this` is the panel. Takes the tax window off the
// screen: hidden, its modal state ended, the simulation let go on, and the
// panel refreshed.
void __fastcall HideTaxWindow(char* self, void*)
{
    void* const window = Virtual<void*>(Root(self), 0x8C, kTaxWindow);
    Virtual<void>(window, 0x110, uint32_t{1}, uint32_t{0});
    Virtual<void>(Cdecl<void*>(addr::SC4_GetWinManager), 0x24, uint32_t{0xF}, window, uint32_t{0});
    Virtual<void>(Global<void*>(addr::SC4_SimulatorService), 0x34);
    Refresh(self, nullptr);
}

// The game's 0x00768130. `this` is the panel. Shows the tax window as a modal
// one: each of the twelve spinners (0x200 on) is set to its tax rate in
// tenths, the rate kept at +0x150 and written to the edit field (0x100 on),
// the simulation is held, and the window manager's modal loop runs. If the
// window is still visible afterwards the panel's 0x00764850 runs and the
// window is hidden.
void __fastcall ShowTaxWindow(char* self, void*)
{
    void* field = nullptr;
    void* spinner = nullptr;
    GameString text;
    InitString(text);

    void* const root = Root(self);
    void* const window = Virtual<void*>(root, 0x8C, kTaxWindow);
    if (!At<void*>(self, panel::kBudget))
        Assign(At<void*>(self, panel::kBudget), Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0));

    for (uint32_t i = 0; i < 12; ++i) {
        Clear(spinner);
        if (!Virtual<uint8_t>(window, 0x94, i + 0x200, kSpinnerIid, &spinner))
            continue;
        const double rate = Virtual<double>(At<void*>(self, panel::kBudget), 0x90, i);
        const uint32_t tenths = Tenths(rate * 10.0);
        At<float>(self, panel::kRates + i * 4) = static_cast<float>(static_cast<double>(tenths));
        Virtual<void>(spinner, 0x24, tenths);
        FormatRate(self, tenths / 10, tenths % 10, &text);
        Clear(field);
        if (Virtual<uint8_t>(root, 0x94, i + 0x100, kEditIid, &field)) {
            Virtual<void>(field, 0xC0, &text, uint32_t{0});
            Virtual<void>(field, 0x130, uint32_t{0x8000});
            Virtual<void>(field, 0x130, uint32_t{0x400});
        }
    }

    This<void>(addr::SC4UI_F766D00, self);
    Virtual<void>(window, 0x110, uint32_t{1}, uint32_t{1});
    Virtual<void>(window, 0x110, uint32_t{0x800}, uint32_t{1});
    Virtual<void>(window, 0x5C);
    Virtual<void>(window, 0x110, uint32_t{0x800}, uint32_t{0});
    Virtual<void>(Global<void*>(addr::SC4_SimulatorService), 0x28);
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x28, window, uint32_t{1});
    Virtual<void>(Cdecl<void*>(addr::SC4_GetWinManager), 0xA4, window); // modal: returns when the window closes

    if (Virtual<uint8_t>(Virtual<void*>(root, 0x8C, kTaxWindow), 0x10C, uint32_t{1})) {
        This<void>(addr::SC4UI_F764850, self);
        HideTaxWindow(self, nullptr);
    }
    RestoreFocus();
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x18, window);

    DestroyString(text);
    if (field)
        Release(field);
    if (spinner)
        Release(spinner);
}

// The game's 0x00768460. `this` is the panel. Runs the dialog of a
// department (or budget group) as a modal window, unless one is up already:
// the dialog object is made, loads its window for `which` and the panel's
// month or year setting, the window manager's modal loop runs, and the dialog
// is finished (0x0077A1A0) and dropped. The panel is refreshed afterwards.
void __fastcall OpenDepartment(char* self, void*, uint32_t which)
{
    void* window = nullptr;
    if (At<void*>(self, panel::kDepartment))
        return;

    void* const memory = Cdecl<void*>(addr::SC4_operator_new, uint32_t{0x140});
    void* const dialog = memory ? This<void*>(addr::SC4UI_F7862B0, memory) : nullptr;
    Assign(At<void*>(self, panel::kDepartment), dialog);

    if (This<uint8_t>(addr::SC4UI_F779E50, At<void*>(self, panel::kDepartment), &window, which,
                      At<uint32_t>(self, panel::kYearly), uint32_t{0})) {
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
        Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x28, window, uint32_t{1});
        Virtual<void>(Cdecl<void*>(addr::SC4_GetWinManager), 0xA4, window); // modal
        RestoreFocus();
        Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x18, window);
        Release(window);
        if (gen)
            Release(gen);
    }

    This<void>(addr::SC4UI_F77A1A0, At<void*>(self, panel::kDepartment));
    Clear(At<void*>(self, panel::kDepartment));
    Refresh(self, nullptr);
}

// The game's 0x00768620 (its second exit is at 0x00768660). `this` is the
// message target part (+0xC; slot 3 of vtable 0x00AB4A1C). Messages
// 0x08DAC3E7 and 0x08DAC3E8 mark the panel as out of date; 0x86AD10EE
// refreshes it when it is.
uint8_t __fastcall DoMessage(char* part, void*, void* message)
{
    char* const self = part - panel::kTarget;
    const uint32_t type = Virtual<uint32_t>(message, 0x10);
    if (type < 0x08DAC3E7)
        return 1;
    if (type <= 0x08DAC3E8) {
        At<uint8_t>(self, panel::kDirty) = 1;
        return 1;
    }
    if (type == 0x86AD10EE && At<uint8_t>(self, panel::kDirty)) {
        At<uint8_t>(self, panel::kDirty) = 0;
        Refresh(self, nullptr);
    }
    return 1;
}

// The game's 0x00768670. `this` is the panel (slot 6). Shows the page chosen
// at +0x194 and hides the other one.
void __fastcall ShowPage(char* self, void*)
{
    void* const root = Root(self);
    uint32_t shown;
    if (At<uint8_t>(self, panel::kPage)) {
        Virtual<void>(Virtual<void*>(root, 0x8C, kShortPage), 0x110, uint32_t{1}, uint32_t{0});
        shown = kLongPage;
    } else {
        Virtual<void>(Virtual<void*>(root, 0x8C, kLongPage), 0x110, uint32_t{1}, uint32_t{0});
        shown = kShortPage;
    }
    void* const page = Virtual<void*>(root, 0x8C, shown);
    Virtual<void>(page, 0x110, uint32_t{1}, uint32_t{1});
    Virtual<void>(page, 0x110, uint32_t{0x800}, uint32_t{0});
    Refresh(self, nullptr);
    RestoreFocus();
    At<uint8_t>(self, panel::kPageShown) = 1;
}

// One of the four month and year buttons checked (slot 6) or not (slot 7).
// The lookup's result is not tested, as in the original.
void SetButton(char* self, void*& button, uint32_t id, bool checked)
{
    void* const root = Root(self);
    void** const out = This<void**>(addr::SC4RefPtr_Reset, &button);
    Virtual<uint8_t>(root, 0x94, id, kButtonIid, out);
    Virtual<void>(button, checked ? std::size_t{0x18} : std::size_t{0x1C});
}

// The month (0) or year (1) setting taken from its buttons.
void SetYearly(char* self, uint32_t yearly)
{
    void* button = nullptr;
    SetButton(self, button, 0x4A8879CC, yearly == 0);
    SetButton(self, button, 0x4A8879CD, yearly != 0);
    SetButton(self, button, 0x4A8879DC, yearly == 0);
    SetButton(self, button, 0x4A8879DD, yearly != 0);
    At<uint32_t>(self, panel::kYearly) = yearly;
    Refresh(self, nullptr);
    if (button)
        Release(button);
}

// The loan button: a loan of the amount in the spinner joins the panel's
// list.
void TakeLoan(char* self)
{
    void* budget = Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0);
    const uint32_t cost =
        Virtual<uint32_t>(budget, 0xC4, static_cast<int64_t>(At<uint32_t>(self, panel::kLoanAmount)));
    uint32_t record[8];
    record[2] = At<uint32_t>(self, panel::kLoanAmount);
    record[3] = 0;
    budget = Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0);
    uint32_t months = Virtual<uint32_t>(budget, 0xBC);
    record[7] = 0;
    record[6] = cost / months;
    budget = Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0);
    months = Virtual<uint32_t>(budget, 0xBC);
    record[0] = months % 12;
    budget = Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0);
    months = Virtual<uint32_t>(budget, 0xBC);
    record[1] = months / 12;
    record[4] = At<uint32_t>(self, panel::kLoanAmount);
    record[5] = 0;
    This<void>(addr::SC4Block32Vector_PushBack, self + panel::kLoans, &record[0]);
    const uint32_t amount = At<uint32_t>(self, panel::kLoanAmount);
    const uint32_t total = At<uint32_t>(self, panel::kLoanTotal);
    const uint32_t loans = At<uint32_t>(self, panel::kLoanCount);
    At<uint32_t>(self, panel::kLoanTotal) = total + amount;
    At<uint32_t>(self, panel::kLoanCount) = loans + 1;
    This<void>(addr::SC4UI_F766680, self);
    This<void>(addr::SC4UI_F764BE0, self);
    This<void>(addr::SC4UI_F764CF0, self);
}

// A button of the panel pressed.
void OnButton(char* self, uint32_t id)
{
    switch (id) {
    case 0x2A4C33A6: // the loan list up
        if (At<uint32_t>(self, panel::kLoanScroll) > 0)
            At<uint32_t>(self, panel::kLoanScroll) -= 2;
        This<void>(addr::SC4UI_F766680, self);
        break;
    case 0xAA4C339C: // the loan list down
        if (At<uint32_t>(self, panel::kLoanScroll) < At<uint32_t>(self, panel::kLoanCount) - 4)
            At<uint32_t>(self, panel::kLoanScroll) += 2;
        This<void>(addr::SC4UI_F766680, self);
        break;
    case 0x0A4C3354:
        This<void>(addr::SC4UI_F764CF0, self);
        break;
    case 0x4A5BB1B5:
    case 0x6A5BB0FB:
        Virtual<void>(self, 0x1C);
        break;
    case 0x4A8879CB:
    case 0xAA3AC40E:
    case 0xAA3AC50E:
        At<uint8_t>(self, panel::kPage) = 1;
        Virtual<void>(self, 0x18);
        break;
    case 0x4A8879DB:
    case 0xAA3AC40F:
    case 0xAA3AC50F:
        At<uint8_t>(self, panel::kPage) = 0;
        Virtual<void>(self, 0x18);
        break;
    case 0x4A8879CC:
    case 0x4A8879DC:
        SetYearly(self, 0);
        break;
    case 0x4A8879CD:
    case 0x4A8879DD:
        SetYearly(self, 1);
        break;
    case 0xAA3AC400:
    case 0xAA3AC500:
        ShowTaxWindow(self, nullptr);
        break;
    case 0xAA3AC401:
    case 0xAA3AC409:
    case 0xAA3AC501:
    case 0xAA3AC509:
        OpenDepartment(self, nullptr, 1);
        break;
    case 0xAA3AC402:
    case 0xAA3AC40A:
    case 0xAA3AC502:
    case 0xAA3AC50A:
        OpenDepartment(self, nullptr, 2);
        break;
    case 0xAA3AC403:
    case 0xAA3AC503:
        OpenDepartment(self, nullptr, 0x0A5A72D1);
        break;
    case 0xAA3AC404:
    case 0xAA3AC405:
    case 0xAA3AC504:
    case 0xAA3AC505:
        OpenDepartment(self, nullptr, 0xAA369059);
        break;
    case 0xAA3AC406:
    case 0xAA3AC506:
        OpenDepartment(self, nullptr, 0x4A357B40);
        break;
    case 0xAA3AC407:
    case 0xAA3AC507:
        OpenDepartment(self, nullptr, 0x6A357B7F);
        break;
    case 0xAA3AC408:
    case 0xAA3AC508:
        OpenDepartment(self, nullptr, 0x4A357EAF);
        break;
    case 0xAA3AC40B:
    case 0xAA3AC50B:
        OpenDepartment(self, nullptr, 0x6A357B96);
        break;
    case 0xAA3AC40C:
    case 0xAA3AC50C:
        OpenDepartment(self, nullptr, 0xEA597195);
        break;
    case 0xAA3AC40D:
    case 0xAA3AC50D:
        This<void>(addr::SC4UI_F765C30, self);
        break;
    case 0xAA4C33E6:
        TakeLoan(self);
        break;
    case 0xAA4C353B:
        HideTaxWindow(self, nullptr);
        break;
    case 0xCA4C352F:
        This<void>(addr::SC4UI_F764850, self);
        HideTaxWindow(self, nullptr);
        break;
    default:
        break;
    }
}

// A spinner moved (message 0x682876C9): the loan spinner shows the amount
// and its payment; a tax spinner (0x200 to 0x20C, one more than there are)
// writes its rate to the edit field and sets it in the budget simulator.
void OnSpinner(char* self, const WinMessage* message)
{
    GameString text;
    This<void*>(addr::SC4String_SC4String, &text);
    void** const budget = &At<void*>(self, panel::kBudget);
    if (!*budget)
        This<void>(addr::SC4RefPtr_Assign, budget, Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0));

    if (message->a == 0x8A4C34D4) {
        SetMoney(self, 0x0A4C3409, static_cast<int64_t>(message->b));
        int64_t payment;
        if (At<uint32_t>(self, panel::kYearly) == 0) {
            void* const simulator = Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0);
            payment = Virtual<int64_t>(simulator, 0xC0, static_cast<int64_t>(message->b));
        } else {
            void* const simulator = Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0);
            payment = Year(Virtual<int64_t>(simulator, 0xC0, static_cast<int64_t>(message->b)));
        }
        SetMoney(self, 0xCA4C34E2, payment);
        At<uint32_t>(self, panel::kLoanAmount) = message->b;
    }

    const uint32_t id = message->a;
    if (id >= 0x200 && id <= 0x20C) {
        const uint32_t value = message->b;
        const uint32_t index = id - 0x200;
        FormatRate(self, value / 10, value % 10, &text);
        void* field = nullptr;
        void* const root = Root(self);
        void** const out = This<void**>(addr::SC4RefPtr_Reset, &field);
        if (Virtual<uint8_t>(root, 0x94, index + 0x100, kEditIid, out)) {
            Virtual<void>(field, 0xC0, &text, uint32_t{0});
            Virtual<void>(field, 0x70);
        }
        const float rate = static_cast<float>(static_cast<double>(message->b) * static_cast<double>(0.1f));
        Virtual<void>(*budget, 0x94, index, rate);
        This<void>(addr::SC4UI_F766D00, self);
        if (field)
            Release(field);
    }
    This<void>(addr::SC4String_Destroy, &text);
}

// Return pressed in an edit field of the tax window (message 0x9B1552D5 with
// key 0xD): the next field, round the twelve, gets its text selected and the
// keyboard.
void OnReturnKey(char* self, const WinMessage* message)
{
    void* const window = Virtual<void*>(Root(self), 0x8C, kTaxWindow);
    void* field = nullptr;
    This<void>(addr::SC4RefPtr_Assign, &field, reinterpret_cast<void*>(static_cast<std::uintptr_t>(message->a)));
    const int32_t id = Virtual<int32_t>(Virtual<void*>(field, 0x14), 0xFC);
    const int32_t next = (id - 0xFF) % 12;
    Clear(field);
    if (Virtual<uint8_t>(window, 0x94, next + 0x100, kEditIid, &field)) {
        void* const edit = field;
        const uint32_t length = Virtual<uint32_t>(edit, 0xBC);
        Virtual<void>(edit, 0x12C, uint32_t{0}, length);
        Virtual<void>(field, 0x70);
        void* const focus = field;
        void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
        Virtual<void>(manager, 0x94, Virtual<void*>(focus, 0x14));
    }
    if (field)
        Release(field);
}

// An edit field of the tax window done with (message 0x9B1552D9): its text
// read as a rate. A number goes to the field's spinner in tenths; anything
// else puts the simulator's rate back. The spinner's value, which it keeps
// within its range, is written back to the field. The field's index is not
// checked: the original's test can never fail.
void OnEditDone(char* self, const WinMessage* message)
{
    void* const edit = reinterpret_cast<void*>(static_cast<std::uintptr_t>(message->a));
    const int32_t index = Virtual<int32_t>(Virtual<void*>(edit, 0x14), 0xFC) - 0x100;
    void* const window = Virtual<void*>(Root(self), 0x8C, kTaxWindow);
    GameString text;
    This<void*>(addr::SC4String_SC4String, &text);
    Virtual<void>(edit, 0xB4, &text);
    This<void>(addr::cRZBaseString_Trim, &text);

    void* spinner = nullptr;
    if (Virtual<uint8_t>(window, 0x94, index + 0x200, kSpinnerIid, &spinner)) {
        const char* const chars = This<const char*>(addr::cRZBaseString_Data, &text);
        char* end;
        const float typed = static_cast<float>(Cdecl<double>(addr::CRT_strtod, chars, &end));
        const uint32_t length = This<uint32_t>(addr::cRZBaseString_Strlen, &text);
        double timesTen;
        if (chars + length != end) {
            const int32_t group = reinterpret_cast<const int32_t*>(addr::SC4UI_BudgetTaxGroups)[index];
            timesTen = Virtual<double>(At<void*>(self, panel::kBudget), 0x90, group) * 10.0;
        } else {
            timesTen = static_cast<double>(typed) * 10.0;
        }
        Virtual<void>(spinner, 0x24, Tenths(timesTen));
        const int32_t value = Virtual<int32_t>(spinner, 0x20);
        const int32_t whole = value / 10;
        FormatRate(self, static_cast<uint32_t>(whole), static_cast<uint32_t>(value - whole * 10), &text);
        Virtual<void>(edit, 0xC0, &text, uint32_t{0});
        Virtual<void>(edit, 0x70);
    }
    if (spinner)
        Release(spinner);
    DestroyString(text);
}

// The game's 0x00768750 (it ends at 0x007693D3 with its jump tables). `this`
// is the window procedure part (+8; slot 3 of vtable 0x00AB4A2C). Buttons,
// spinners and the tax window's edit fields; 0xA2BF8AD5 and 0xA2BF8AD6 (the
// window is up, or gone) register the panel for its messages and take it off
// again. Answers true only to message 5 while the window manager's slot 42
// says so, and to a button the tutorial holds back.
uint8_t __fastcall WinMessageHandler(char* part, void*, void* window, const WinMessage* message)
{
    char* const self = part - panel::kWinProc;
    if (window != Virtual<void*>(At<void*>(self, panel::kWindow), 0xC))
        return 0;

    switch (message->type) {
    case 0x682876C9:
        OnSpinner(self, message);
        return 0;
    case 5:
        return Virtual<uint8_t>(Virtual<void*>(window, 0x18), 0xA8) ? 1 : 0;
    case 3:
        if (!Cdecl<uint8_t>(addr::SC4Tutorial_FilterWinMessage, message))
            return 1;
        if (message->a == 0x287259F6)
            OnButton(self, message->b);
        return 0;
    case 0x9B1552D5:
        if (message->b == 0xD)
            OnReturnKey(self, message);
        return 0;
    case 0x9B1552D9:
        OnEditDone(self, message);
        return 0;
    case 0xA2BF8AD5:
        This<void>(addr::SC4UI_F7648D0, self, uint32_t{1});
        return 0;
    case 0xA2BF8AD6:
        if (At<uint8_t>(self, panel::kRegistered))
            This<void>(addr::SC4UI_F7648D0, self, uint32_t{0});
        return 0;
    default:
        return 0;
    }
}

// The game's 0x007693E0. `this` is an object of the class after the panel
// (slot 0 of vtable 0x00AB4A78). QueryInterface: it answers to 0x22E85D8E
// only.
uint8_t __fastcall NextQueryInterface(char* self, void*, uint32_t iid, void** out)
{
    if (iid != 0x22E85D8E)
        return 0;
    *out = self;
    Virtual<uint32_t>(self, 0x4);
    return 1;
}

bool InstallDialogs3(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[12] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F767270, reinterpret_cast<void*>(&ReadBudget)},
        {addr::SC4UI_F767990, reinterpret_cast<void*>(&Refresh)},
        {addr::SC4UI_F7680D0, reinterpret_cast<void*>(&HideTaxWindow)},
        {addr::SC4UI_F768130, reinterpret_cast<void*>(&ShowTaxWindow)},
        {addr::SC4UI_F768460, reinterpret_cast<void*>(&OpenDepartment)},
        {addr::SC4UI_F768620, reinterpret_cast<void*>(&DoMessage)},
        {addr::SC4UI_F768670, reinterpret_cast<void*>(&ShowPage)},
        {addr::SC4UI_F768750, reinterpret_cast<void*>(&WinMessageHandler)},
        {addr::SC4UI_F7693E0, reinterpret_cast<void*>(&NextQueryInterface)},
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

const bool g_dialogs3Registered = registry::Register({
    "ui.dialogs.part3",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F768750,
    "Our budget panel, third part: the figures read and shown, the tax window, the department dialogs "
    "and the window procedure (docs/ui.md)",
    false,
    InstallDialogs3,
});

} // namespace
} // namespace opensc4::ui
