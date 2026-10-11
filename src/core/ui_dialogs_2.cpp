// The city view's budget panel (0x1A0 bytes, constructor 0x007658A0, kept by
// the city view's interface object at +0x2E8), four of its functions
// (0x00765C30 to 0x00767270):
//
//   - the loan dialog opened: its texts and rows filled, the spinner set to
//     the bond increment, the dialog centred and run as a modal window;
//   - the loan dialog refreshed while it is open (rows scrolled, a loan
//     taken);
//   - the tax income by tax type, twelve texts and three sums;
//   - the panel made the notification target of its controls.
//
// Replacement, off by default:
//
//   ui.dialogs.part2: the four functions above.
//
// Windows, texts and the spinner are the game's, called through their
// vtables; the budget simulator is called through its interface.
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

namespace panel {
constexpr std::size_t kWindow = 0x10;     // slot 3 gives the panel's window
constexpr std::size_t kBudget = 0x14;     // the budget simulator
constexpr std::size_t kLoans = 0x1C;      // vector of Loan
constexpr std::size_t kFirstRow = 0x180;  // the first loan on show
constexpr std::size_t kLoanCount = 0x184;
constexpr std::size_t kLoanCount2 = 0x188;
constexpr std::size_t kTaken = 0x18C;     // borrowed while the dialog is open
constexpr std::size_t kAmount = 0x190;    // the spinner's amount
constexpr std::size_t kYearly = 0x198;    // amounts shown by the year
} // namespace panel

constexpr uint32_t kSpinnerIid = 0x612CE0C3;
constexpr uint32_t kButtonIid = 0x8810;
constexpr uint32_t kTextGroup = 0x6A3FF01C;
constexpr uint32_t kTermFormat = 0xEA569D4B; // the text of a term in years and months

constexpr uint32_t kDialog = 0xCA4C332D;
constexpr uint32_t kSpinner = 0x8A4C34D4;
constexpr uint32_t kAccept = 0xAA4C33E6;
constexpr uint32_t kRowsUp = 0x2A4C33A6;
constexpr uint32_t kRowsDown = 0xAA4C339C;
constexpr uint32_t kPaymentsTotal = 0x6A4C3494;
constexpr uint32_t kBorrowed = 0xCA4C349D;

// The four texts of the first loan row; the other three rows follow by one.
constexpr uint32_t kRowTerm = 0x4A4C33AD;
constexpr uint32_t kRowTotal = 0x8A4C33D0;
constexpr uint32_t kRowRemaining = 0x0A4C3405;
constexpr uint32_t kRowPayment = 0xCA4C3426;
constexpr uint32_t kRows = 4;

// cISC4BudgetSimulator::LoanInfo.
struct Loan {
    uint32_t months;
    uint32_t years;
    int64_t total;
    int64_t remaining;
    int64_t payment;
};
static_assert(sizeof(Loan) == 0x20);

struct LoanVector {
    Loan* begin;
    Loan* end;
    Loan* capacity;
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

// A string from the text resources: a string first, 0x20 bytes.
struct LocalizedString {
    GameString text;
    uint32_t rest[3];
};
static_assert(sizeof(LocalizedString) == 0x20);

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

// The game's inlined string constructor: empty, with an 8-byte buffer.
void EmptyString(GameString& text)
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

void* LoadText(LocalizedString& text, uint32_t instance)
{
    return This<void*>(addr::SC4LocalizedString_SC4LocalizedString, &text, kTextGroup, instance, uint32_t{1});
}

// The runtime's 64-bit multiply (0x009F0D10).
int64_t Times(int64_t value, uint32_t factor)
{
    return static_cast<int64_t>(static_cast<uint64_t>(value) * factor);
}

// The parent of the panel's window: the controls are looked up from it.
void* Parent(char* self)
{
    return Virtual<void*>(Virtual<void*>(At<void*>(self, panel::kWindow), 0xC), 0x2C);
}

void* Child(void* parent, uint32_t id)
{
    return Virtual<void*>(parent, 0x8C, id);
}

// A child's text set to an amount of money (the panel's 0x00765800).
void SetMoney(char* self, uint32_t id, int64_t amount)
{
    This<void>(addr::SC4UI_F765800, static_cast<void*>(self), id, amount);
}

// The budget simulator taken from the city when the panel has none.
void TakeBudget(char* self)
{
    if (!At<void*>(self, panel::kBudget))
        Assign(At<void*>(self, panel::kBudget), Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0));
}

// The sixteen texts of the four loan rows set to `text`.
void SetRowTexts(void* parent, GameString& text)
{
    for (uint32_t row = 0; row < kRows; ++row) {
        Virtual<uint8_t>(Child(parent, kRowTerm + row), 0x128, &text);
        Virtual<uint8_t>(Child(parent, kRowTotal + row), 0x128, &text);
        Virtual<uint8_t>(Child(parent, kRowRemaining + row), 0x128, &text);
        Virtual<uint8_t>(Child(parent, kRowPayment + row), 0x128, &text);
    }
}

// One loan row filled: the term as text, the amount, what is left and the
// payment by the month or the year.
void FillRow(char* self, void* parent, uint32_t id, const Loan* loan, uint32_t factor)
{
    GameString term;
    EmptyString(term);
    LocalizedString format;
    void* const loaded = LoadText(format, kTermFormat);
    const uint32_t months = loan->months;
    const uint32_t years = loan->years;
    Cdecl<void>(addr::cRZBaseString_Sprintf, &term, Virtual<const char*>(loaded, 0x14), years, months);
    DestroyString(format.text);
    Virtual<uint8_t>(Child(parent, id + 0xBFFFFFDDu), 0x128, &term);
    SetMoney(self, id, loan->total);
    SetMoney(self, id + 0x80000035u, loan->remaining);
    SetMoney(self, id + 0x40000056u, Times(loan->payment, factor));
    DestroyString(term);
}

// The rows on show filled from the panel's loans; returns the sum of all
// payments, in 32 bits as the original adds it.
int32_t FillRows(char* self, void* parent, uint32_t factor)
{
    uint32_t sum = 0;
    uint32_t index = 0;
    if (At<uint32_t>(self, panel::kLoanCount) > 0) {
        uint32_t id = kRowTotal;
        do {
            if (index >= At<uint32_t>(self, panel::kFirstRow) && id - kRowTotal < kRows) {
                FillRow(self, parent, id, At<Loan*>(self, panel::kLoans) + index, factor);
                ++id;
            }
            const Loan* const loan = At<Loan*>(self, panel::kLoans) + index;
            sum += static_cast<uint32_t>(loan->payment) * factor;
            ++index;
        } while (index < At<uint32_t>(self, panel::kLoanCount));
    }
    return static_cast<int32_t>(sum);
}

// The button that scrolls the rows down shown when more than four loans
// remain below the first row.
void ShowRowsDown(char* self, void* parent)
{
    const uint32_t count = At<uint32_t>(self, panel::kLoanCount);
    const uint32_t first = At<uint32_t>(self, panel::kFirstRow);
    if (first < count - 4 && count > 4)
        Virtual<uint32_t>(Child(parent, kRowsDown), 0x110, uint32_t{1}, uint32_t{1});
    else
        Virtual<uint32_t>(Child(parent, kRowsDown), 0x110, uint32_t{1}, uint32_t{0});
}

// With as many loans as the city may have, the spinner is emptied and the
// accept button disabled. `budget` is asked afresh from the city.
void StopAtMostLoans(char* self, void* parent)
{
    void* const budget = Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0);
    const LoanVector& loans = At<LoanVector>(self, panel::kLoans);
    const int32_t count = static_cast<int32_t>((reinterpret_cast<intptr_t>(loans.end)
        - reinterpret_cast<intptr_t>(loans.begin)) >> 5);
    const int64_t most = Virtual<int64_t>(budget, 0xB0);
    if (static_cast<int64_t>(static_cast<uint32_t>(count)) != most)
        return;
    void* spinner = nullptr;
    if (Virtual<uint8_t>(parent, 0x94, kSpinner, kSpinnerIid, &spinner)) {
        Virtual<void>(spinner, 0x28, uint32_t{0}, uint32_t{0});
        Virtual<void>(spinner, 0x24, uint32_t{0});
        Virtual<uint32_t>(Child(parent, kAccept), 0x110, uint32_t{2}, uint32_t{0});
    }
    if (spinner)
        Release(spinner);
}

// The game's 0x00765C30. `this` is the panel. Opens the loan dialog: fills
// it from the budget simulator, centres it in its parent and runs it as a
// modal window; when the modal loop ends with the dialog still visible it is
// closed here.
void __fastcall OpenLoanDialog(char* self, void*)
{
    uint32_t factor = 1;
    GameString empty;
    EmptyString(empty);
    void* const parent = Parent(self);
    const bool noBudget = !At<void*>(self, panel::kBudget);
    At<uint32_t>(self, panel::kFirstRow) = 0;
    At<uint32_t>(self, panel::kTaken) = 0;
    At<uint32_t>(self, panel::kAmount) = 0;
    if (noBudget)
        Assign(At<void*>(self, panel::kBudget), Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0));

    // The heading of the payment column: by the month or by the year.
    {
        const bool yearly = At<uint32_t>(self, panel::kYearly) != 0;
        void* const heading = Child(parent, 0x0A4C34BE);
        LocalizedString text;
        void* const loaded = LoadText(text, yearly ? 0x4A569CE4 : 0x8A569BD2);
        Virtual<uint8_t>(heading, 0x128, loaded);
        DestroyString(text.text);
        if (yearly)
            factor = 12;
    }

    SetRowTexts(parent, empty);

    void* spinner = nullptr;
    if (Virtual<uint8_t>(parent, 0x94, kSpinner, kSpinnerIid, &spinner)) {
        void* budget = At<void*>(self, panel::kBudget);
        const int64_t borrowed = Virtual<int64_t>(budget, 0xA4);
        const int64_t limit = Virtual<int64_t>(budget, 0xA8);
        if (limit - borrowed != 0) {
            budget = At<void*>(self, panel::kBudget);
            const int64_t borrowedNow = Virtual<int64_t>(budget, 0xA4);
            const int64_t room = Virtual<int64_t>(budget, 0xA8) - borrowedNow;
            const int64_t bondLimit = Virtual<int64_t>(budget, 0xAC);
            if (room < bondLimit) {
                budget = At<void*>(self, panel::kBudget);
                void* const target = spinner;
                const uint32_t low = static_cast<uint32_t>(Virtual<int64_t>(budget, 0xA4));
                const uint32_t most = static_cast<uint32_t>(Virtual<int64_t>(budget, 0xA8)) - low;
                const uint32_t step = static_cast<uint32_t>(Virtual<int64_t>(budget, 0x9C));
                Virtual<void>(target, 0x28, step, most);
            } else {
                budget = At<void*>(self, panel::kBudget);
                void* const target = spinner;
                const uint32_t most = static_cast<uint32_t>(Virtual<int64_t>(budget, 0xAC));
                const uint32_t step = static_cast<uint32_t>(Virtual<int64_t>(budget, 0x9C));
                Virtual<void>(target, 0x28, step, most);
            }
            void* const target = spinner;
            const uint32_t step = static_cast<uint32_t>(Virtual<int64_t>(At<void*>(self, panel::kBudget), 0x9C));
            Virtual<void>(target, 0x24, step);
            Virtual<uint32_t>(Child(parent, kAccept), 0x110, uint32_t{2}, uint32_t{1});
        } else {
            Virtual<void>(spinner, 0x28, uint32_t{0}, uint32_t{0});
            Virtual<void>(spinner, 0x24, uint32_t{0});
            Virtual<uint32_t>(Child(parent, kAccept), 0x110, uint32_t{2}, uint32_t{0});
        }
    }

    SetMoney(self, 0xEA4C34A6, Virtual<int64_t>(At<void*>(self, panel::kBudget), 0xA8));
    SetMoney(self, kBorrowed, Virtual<int64_t>(At<void*>(self, panel::kBudget), 0xA4));

    // The payment for one bond increment; by the year the amount asked about
    // is twelve times the increment, in 32 bits.
    {
        uint32_t amount = static_cast<uint32_t>(Virtual<int64_t>(At<void*>(self, panel::kBudget), 0x9C));
        void* const budget = At<void*>(self, panel::kBudget);
        At<uint32_t>(self, panel::kAmount) = amount;
        if (At<uint32_t>(self, panel::kYearly) != 0)
            amount *= 12;
        SetMoney(self, 0xCA4C34E2, Virtual<int64_t>(budget, 0xC0, static_cast<int64_t>(amount)));
    }

    // The term of a new loan.
    GameString term;
    EmptyString(term);
    {
        const uint32_t years = Virtual<uint32_t>(At<void*>(self, panel::kBudget), 0xBC) / 12;
        const uint32_t months = Virtual<uint32_t>(At<void*>(self, panel::kBudget), 0xBC) - years * 12;
        LocalizedString format;
        void* const loaded = LoadText(format, kTermFormat);
        Cdecl<void>(addr::cRZBaseString_Sprintf, &term, Virtual<const char*>(loaded, 0x14), years, months);
        DestroyString(format.text);
    }
    Virtual<uint8_t>(Child(parent, 0x0A4C34DB), 0x128, &term);

    // The city's loans.
    LoanVector* const loans = &At<LoanVector>(self, panel::kLoans);
    This<void>(addr::LoanVector_Erase, loans, loans->begin, loans->end);
    Virtual<uint8_t>(At<void*>(self, panel::kBudget), 0xB8, loans);
    const int32_t count = static_cast<int32_t>((reinterpret_cast<intptr_t>(loans->end)
        - reinterpret_cast<intptr_t>(loans->begin)) >> 5);
    At<int32_t>(self, panel::kLoanCount) = count;
    At<int32_t>(self, panel::kLoanCount2) = count;
    const int32_t payments = FillRows(self, parent, factor);
    SetMoney(self, kPaymentsTotal, static_cast<int64_t>(payments));
    Virtual<uint32_t>(Child(parent, kRowsUp), 0x110, uint32_t{1}, uint32_t{0});
    ShowRowsDown(self, parent);

    // The dialog shown, brought forward and centred in its parent. It is not
    // checked for null, as in the original.
    void* const dialog = Child(parent, kDialog);
    if (dialog)
        AddRef(dialog);
    Virtual<uint32_t>(dialog, 0x110, uint32_t{1}, uint32_t{1});
    Virtual<uint32_t>(dialog, 0x110, uint32_t{0x800}, uint32_t{1});
    Virtual<uint8_t>(dialog, 0x5C);
    Virtual<uint32_t>(dialog, 0x110, uint32_t{0x800}, uint32_t{0});
    Virtual<int32_t>(Virtual<void*>(dialog, 0x2C), 0xA4);
    {
        void* const heightParent = Virtual<void*>(dialog, 0x2C);
        void* const widthParent = Virtual<void*>(dialog, 0x2C);
        const int32_t height = Virtual<int32_t>(dialog, 0xA8);
        const int32_t y = (Virtual<int32_t>(heightParent, 0xA8) - height) / 2;
        const int32_t width = Virtual<int32_t>(dialog, 0xA4);
        const int32_t x = (Virtual<int32_t>(widthParent, 0xA4) - width) / 2;
        Virtual<uint8_t>(dialog, 0xE0, x, y);
    }

    Virtual<int64_t>(Virtual<void*>(Global<void*>(addr::SC4_City), 0xD0), 0xB0);
    StopAtMostLoans(self, parent);

    // The modal run.
    Virtual<void>(Global<void*>(addr::SC4_SimulatorService), 0x28);
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x28, dialog, uint32_t{1});
    Virtual<int32_t>(Cdecl<void*>(addr::SC4_GetWinManager), 0xA4, dialog);
    if (Virtual<uint8_t>(dialog, 0x10C, uint32_t{1}))
        This<void>(addr::SC4UI_F764CF0, static_cast<void*>(self));
    {
        void* const service = Global<void*>(addr::SC4_Service43DC4);
        void* const manager = Cdecl<void*>(addr::SC4_GetWinManager);
        Virtual<void>(manager, 0x94, Virtual<void*>(service, 0xC));
    }
    Virtual<void>(Global<void*>(addr::SC4_Service43CE0), 0x18, dialog);
    Release(dialog);

    DestroyString(term);
    if (spinner)
        Release(spinner);
    DestroyString(empty);
}

// The game's 0x00766680. `this` is the panel. Fills the open loan dialog
// again: the rows from the first one on show, the spinner's range less what
// was borrowed since the dialog opened, the two scroll buttons.
void __fastcall RefreshLoanDialog(char* self, void*)
{
    uint32_t factor = 1;
    GameString empty;
    EmptyString(empty);
    void* const parent = Parent(self);
    TakeBudget(self);
    if (At<uint32_t>(self, panel::kYearly) != 0)
        factor = 12;

    SetRowTexts(parent, empty);

    void* spinner = nullptr;
    if (Virtual<uint8_t>(parent, 0x94, kSpinner, kSpinnerIid, &spinner)) {
        void* budget = At<void*>(self, panel::kBudget);
        const int64_t limit = Virtual<int64_t>(budget, 0xA8);
        const int64_t left = limit - static_cast<int64_t>(At<uint32_t>(self, panel::kTaken));
        const int64_t borrowed = Virtual<int64_t>(budget, 0xA4);
        if (left - borrowed != 0) {
            void* const target = spinner;
            budget = At<void*>(self, panel::kBudget);
            const uint32_t low = static_cast<uint32_t>(Virtual<int64_t>(budget, 0xA4));
            const uint32_t most = static_cast<uint32_t>(Virtual<int64_t>(budget, 0xA8)) - low
                - At<uint32_t>(self, panel::kTaken);
            const uint32_t step = static_cast<uint32_t>(Virtual<int64_t>(budget, 0x9C));
            Virtual<void>(target, 0x28, step, most);
            Virtual<void>(spinner, 0x24, At<uint32_t>(self, panel::kAmount));
            Virtual<uint32_t>(Child(parent, kAccept), 0x110, uint32_t{2}, uint32_t{1});
        } else {
            Virtual<void>(spinner, 0x28, uint32_t{0}, uint32_t{0});
            Virtual<void>(spinner, 0x24, uint32_t{0});
            Virtual<uint32_t>(Child(parent, kAccept), 0x110, uint32_t{2}, uint32_t{0});
        }
    }

    {
        const int64_t borrowed = Virtual<int64_t>(At<void*>(self, panel::kBudget), 0xA4);
        SetMoney(self, kBorrowed, borrowed + static_cast<int64_t>(At<uint32_t>(self, panel::kTaken)));
    }

    const LoanVector& loans = At<LoanVector>(self, panel::kLoans);
    At<int32_t>(self, panel::kLoanCount) = static_cast<int32_t>((reinterpret_cast<intptr_t>(loans.end)
        - reinterpret_cast<intptr_t>(loans.begin)) >> 5);
    const int32_t payments = FillRows(self, parent, factor);
    SetMoney(self, kPaymentsTotal, static_cast<int64_t>(payments));

    if (At<uint32_t>(self, panel::kFirstRow) != 0)
        Virtual<uint32_t>(Child(parent, kRowsUp), 0x110, uint32_t{1}, uint32_t{1});
    else
        Virtual<uint32_t>(Child(parent, kRowsUp), 0x110, uint32_t{1}, uint32_t{0});
    ShowRowsDown(self, parent);
    StopAtMostLoans(self, parent);

    if (spinner)
        Release(spinner);
    DestroyString(empty);
}

// The game's 0x00766D00. `this` is the panel. Shows the tax income of the
// twelve tax types (texts 0x300 to 0x30B) and the sums of types 0 to 2, 3 to
// 7 and 8 to 11 (0x500 to 0x502), by the month or the year. The amounts are
// taken and multiplied in 32 bits and shown unsigned, as in the original.
void __fastcall ShowTaxIncome(char* self, void*)
{
    const uint32_t factor = At<uint32_t>(self, panel::kYearly) != 0 ? 12 : 1;
    static const struct {
        uint32_t first;
        uint32_t last;
        uint32_t sumText;
    } groups[] = {{0, 2, 0x500}, {3, 7, 0x501}, {8, 11, 0x502}};
    for (const auto& group : groups) {
        uint32_t sum = 0;
        for (uint32_t type = group.first; type <= group.last; ++type) {
            const uint32_t income = Virtual<uint32_t>(At<void*>(self, panel::kBudget), 0x48, type) * factor;
            SetMoney(self, 0x300 + type, static_cast<int64_t>(income));
            sum += income;
        }
        SetMoney(self, group.sumText, static_cast<int64_t>(sum));
    }
}

// The game's 0x00766E10. `this` is the panel. Called once by the city view
// after the constructor: hides the loan dialog and three other windows, makes
// the panel's window the notification target of its controls, sets the four
// month and year buttons (monthly on) and shows the spinner's amount.
void __fastcall Attach(char* self, void*)
{
    void* const target = Virtual<void*>(At<void*>(self, panel::kWindow), 0xC);
    void* const parent = Parent(self);
    void* const first = Child(parent, 0xAA3AC001);
    void* const second = Child(parent, 0xAA3AC002);
    Virtual<uint32_t>(first, 0x110, uint32_t{1}, uint32_t{0});
    Virtual<uint32_t>(Child(parent, 0xAA3AC000), 0x110, uint32_t{1}, uint32_t{0});
    Virtual<uint32_t>(Child(parent, 0xAA3AC002), 0x110, uint32_t{1}, uint32_t{0});
    Virtual<uint32_t>(Child(parent, kDialog), 0x110, uint32_t{1}, uint32_t{0});

    static const uint32_t controls[] = {
        0xAA3AC40E, 0xAA3AC50E, 0x4A8879CB, 0xAA3AC40F, 0xAA3AC50F, 0x4A8879DB,
        0x4A8879CC, 0x4A8879CD, 0x4A8879DC, 0x4A8879DD, 0xAA3AC400, 0xAA3AC401,
        0xAA3AC402, 0xAA3AC403, 0xAA3AC404, 0xAA3AC405, 0xAA3AC406, 0xAA3AC407,
        0xAA3AC408, 0xAA3AC409, 0xAA3AC40A, 0xAA3AC40B, 0xAA3AC40C, 0xAA3AC40D,
        0xAA3AC500, 0xAA3AC501, 0xAA3AC502, 0xAA3AC503, 0xAA3AC504, 0xAA3AC505,
        0xAA3AC506, 0xAA3AC507, 0xAA3AC508, 0xAA3AC509, 0xAA3AC50A, 0xAA3AC50B,
        0xAA3AC50C, 0xAA3AC50D, 0x6A5BB0FB, 0x4A5BB1B5, 0xAA4C353B, 0xCA4C352F,
    };
    for (const uint32_t id : controls)
        Cdecl<uint8_t>(addr::SC4UI_AttachChild, parent, id, target);
    // The tax rate controls, children of the second window.
    for (uint32_t id = 0x100; id < 0x10C; ++id) {
        Cdecl<uint8_t>(addr::SC4UI_AttachChild, second, id + 0x100, target);
        Cdecl<uint8_t>(addr::SC4UI_AttachChild, second, id, target);
    }
    static const uint32_t dialogControls[] = {0x0A4C3354, kAccept, kSpinner, kRowsUp, kRowsDown};
    for (const uint32_t id : dialogControls)
        Cdecl<uint8_t>(addr::SC4UI_AttachChild, parent, id, target);

    // The month and year buttons of the two pages. A button that is missing
    // is not checked for, as in the original.
    void* button = nullptr;
    Virtual<uint8_t>(parent, 0x94, uint32_t{0x4A8879CC}, kButtonIid, &button);
    Virtual<void>(button, 0x18);
    if (void* const held = button) {
        button = nullptr;
        Release(held);
    }
    Virtual<uint8_t>(parent, 0x94, uint32_t{0x4A8879CD}, kButtonIid, &button);
    Virtual<void>(button, 0x1C);
    if (void* const held = button) {
        button = nullptr;
        Release(held);
    }
    Virtual<uint8_t>(parent, 0x94, uint32_t{0x4A8879DC}, kButtonIid, &button);
    Virtual<void>(button, 0x18);
    if (void* const held = button) {
        button = nullptr;
        Release(held);
    }
    Virtual<uint8_t>(parent, 0x94, uint32_t{0x4A8879DD}, kButtonIid, &button);
    Virtual<void>(button, 0x1C);

    At<uint32_t>(self, panel::kYearly) = 0;
    void* spinner = nullptr;
    if (Virtual<uint8_t>(parent, 0x94, kSpinner, kSpinnerIid, &spinner))
        SetMoney(self, 0x0A4C3409, static_cast<int64_t>(Virtual<int32_t>(spinner, 0x20)));
    if (spinner)
        Release(spinner);
    if (button)
        Release(button);
}

bool InstallDialogs2(registry::Mode)
{
    if (!game::IsBuild641())
        return false;
    static void* originals[4] = {};
    static const std::pair<std::uintptr_t, void*> hooks[] = {
        {addr::SC4UI_F765C30, reinterpret_cast<void*>(&OpenLoanDialog)},
        {addr::SC4UI_F766680, reinterpret_cast<void*>(&RefreshLoanDialog)},
        {addr::SC4UI_F766D00, reinterpret_cast<void*>(&ShowTaxIncome)},
        {addr::SC4UI_F766E10, reinterpret_cast<void*>(&Attach)},
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

const bool g_dialogs2Registered = registry::Register({
    "ui.dialogs.part2",
    "ui",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::SC4UI_F765C30,
    "Our budget panel: the loan dialog opened and refreshed, the tax income texts, the controls attached (docs/ui.md)",
    false,
    InstallDialogs2,
});

} // namespace
} // namespace opensc4::ui
