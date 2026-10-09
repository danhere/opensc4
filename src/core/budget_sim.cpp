// Our cSC4BudgetSimulator (build 641), reimplemented function by function
// on the original object (docs/budget.md). Each replacement has its own
// switch; calls between the pieces go through the game's vtables and
// addresses, so each piece is the game's or ours as configured.
#include "core/budget_sim.h"

#include "core/game.h"
#include "core/game_access.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace opensc4::budget_sim {

void FirstCall(const char* name, bool& logged)
{
    if (logged)
        return;
    logged = true;
    log::Write("budget: first %s", name);
}

bool SwapSlots(const char* what, std::uintptr_t address, const Slot* slots, std::size_t count)
{
    if (!game::IsBuild641())
        return false;
    auto* const vtable = reinterpret_cast<std::uintptr_t*>(address);
    int last = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (vtable[slots[i].index] != slots[i].original) {
            log::Write("%s: slot %d of %08X holds %08X, expected %08X; not installing", what, slots[i].index,
                       static_cast<unsigned>(address), static_cast<unsigned>(vtable[slots[i].index]),
                       static_cast<unsigned>(slots[i].original));
            return false;
        }
        if (slots[i].index > last)
            last = slots[i].index;
    }
    DWORD old = 0;
    if (!VirtualProtect(vtable, (last + 1) * sizeof(*vtable), PAGE_READWRITE, &old)) {
        log::Write("%s: VirtualProtect failed (%lu)", what, GetLastError());
        return false;
    }
    for (std::size_t i = 0; i < count; ++i)
        vtable[slots[i].index] = reinterpret_cast<std::uintptr_t>(slots[i].ours);
    VirtualProtect(vtable, (last + 1) * sizeof(*vtable), old, &old);
    log::Write("%s: %u slots of %08X are ours", what, static_cast<unsigned>(count), static_cast<unsigned>(address));
    return true;
}

// The game's pow (_CIpow, 0x009EFC60): base in ST1, exponent in ST0, the
// result in ST0; called directly so the result is the game's to the bit.
double GamePow(double base, double exponent)
{
    double result;
    const std::uintptr_t pow = addr::CRT_CIpow;
    __asm {
        fld base
        fld exponent
        call pow
        fstp result
    }
    return result;
}

namespace {

// 0x0068FE10: message 0x0772FAD4 (funds changed) through the message server
// referenced at +0x1C, when there is one.
void NotifyFunds(void* self)
{
    void* const server = At<void*>(self, field::kMessageServer);
    if (!server)
        return;
    using MessageFn = void*(__thiscall*)(void* memory);
    using AllocateFn = void*(__cdecl*)(std::size_t size);
    void* const memory = reinterpret_cast<AllocateFn>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    Virtual<uint32_t>(message, 0x4);
    Virtual<uint32_t>(message, 0x14, 0x0772FAD4u);
    Virtual<uint32_t>(message, 0x30, self);
    Virtual<uint32_t>(message, 0x84, self);
    Virtual<uint32_t>(server, 0x10, message, 0u);
    Virtual<uint32_t>(message, 0x8);
}

void SetFunds(void* self, int64_t funds)
{
    At<int64_t>(self, field::kFunds) = funds;
    At<int64_t>(self, field::kFundsShown) = funds;
    NotifyFunds(self);
}

// --- The interface: cISC4BudgetSimulator (vtable 0x00AB0908) ----------------

// 5 (0x00690140).
void __fastcall SetTotalFunds(void* self, void*, int64_t funds)
{
    if (At<int64_t>(self, field::kFunds) != funds)
        SetFunds(self, funds);
}

uint32_t __fastcall WithdrawFunds(void* self, void*, int64_t amount);

// 7 (0x00690170): a negative amount is withdrawn; the funds stop at the
// largest int64.
uint32_t __fastcall DepositFunds(void* self, void*, int64_t amount)
{
    static bool logged = false;
    FirstCall("DepositFunds", logged);
    if (amount < 0)
        return Virtual<uint32_t>(self, slot::kWithdrawFunds, -amount);
    if (amount == 0)
        return 1;
    const int64_t funds = At<int64_t>(self, field::kFunds);
    if (funds >= 0 && amount >= INT64_MAX - funds)
        At<int64_t>(self, field::kFunds) = INT64_MAX;
    else
        At<int64_t>(self, field::kFunds) = funds + amount;
    SetFunds(self, At<int64_t>(self, field::kFunds));
    return 1;
}

// 8 (0x00690200): a negative amount is deposited; more than the funds is
// refused.
uint32_t __fastcall WithdrawFunds(void* self, void*, int64_t amount)
{
    static bool logged = false;
    FirstCall("WithdrawFunds", logged);
    if (amount < 0)
        return Virtual<uint32_t>(self, slot::kDepositFunds, -amount);
    if (amount == 0)
        return 1;
    const int64_t funds = At<int64_t>(self, field::kFunds);
    if (funds < amount)
        return 0;
    SetFunds(self, funds - amount);
    return 1;
}

template <typename T, std::size_t Offset>
T __fastcall Getter(void* self, void*)
{
    return At<T>(self, Offset);
}

template <std::size_t Offset>
void __fastcall Int64Setter(void* self, void*, int64_t value)
{
    At<int64_t>(self, Offset) = value;
}

// The simulator service's date (0x00B43D5C slot 6): the month.
uint32_t Month()
{
    uint32_t month = 0;
    Virtual<uint32_t>(Global<void*>(addr::SC4_SimulatorService), 0x18, 0u, &month, 0u, 0u, 0u);
    return month;
}

// 11, 13 (0x00690270, 0x006902D0): this year so far plus this month's
// figure for every month left, this one included.
template <std::size_t MonthlySlot, std::size_t SoFarSlot>
int64_t __fastcall ProjectedYear(void* self, void*)
{
    const uint32_t month = Month();
    const int64_t monthly = Virtual<int64_t>(self, MonthlySlot);
    const int64_t months = static_cast<int32_t>(13 - month);
    return monthly * months + Virtual<int64_t>(self, SoFarSlot);
}

// The departments (the map at +0x54) in key order, each one's slot `Slot`.
template <std::size_t Slot>
int64_t SumDepartments(void* self)
{
    int64_t sum = 0;
    char* const header = At<char*>(self, field::kDepartments);
    for (char* node = At<char*>(header, 0x8); node != header; node = MapNext(node))
        sum += Virtual<int64_t>(At<void*>(node, 0x14), Slot);
    return sum;
}

// The bonds (the list at +0x780): their monthly payments (+0x30).
int64_t BondPayments(void* self)
{
    int64_t sum = 0;
    char* const head = At<char*>(self, field::kBonds);
    for (char* node = At<char*>(head, 0x0); node != head; node = At<char*>(node, 0x0))
        sum += At<int64_t>(node, 0x30);
    return sum;
}

// 14 (0x00690990): +0x24's slot 22, the service at 0x00B43D8C (month end service)'s slot 33,
// every department's expenses (slot 15), the bond payments.
int64_t __fastcall GetTotalMonthlyExpense(void* self, void*)
{
    int64_t sum = Virtual<int64_t>(At<void*>(self, field::kTrade), 0x58);
    sum += Virtual<int64_t>(Global<void*>(addr::SC4_MonthEndService), 0x84);
    sum += SumDepartments<0x3C>(self);
    return sum + BondPayments(self);
}

// 16 (0x00690A30): the tax income of the 12 groups (slot 18), +0x24's slot
// 23, the service at 0x00B43D8C (month end service)'s slot 32, every department's income (slot 16).
int64_t __fastcall GetTotalMonthlyIncome(void* self, void*)
{
    int64_t sum = 0;
    for (uint32_t group = 0; group < kTaxGroups; ++group)
        sum += Virtual<int64_t>(self, slot::kGetGroupTaxIncome, group);
    sum += Virtual<int64_t>(At<void*>(self, field::kTrade), 0x5C);
    sum += Virtual<int64_t>(Global<void*>(addr::SC4_MonthEndService), 0x80);
    return sum + SumDepartments<0x40>(self);
}

// 18 (0x006904C0): the group's assessed value (+0xE0) times this month's
// share (the table at +0x814 by the month from +0x18's slot 24), the rate
// and 0.000833 (1/1200), truncated.
int64_t __fastcall GetGroupTaxIncome(void* self, void*, uint32_t group)
{
    const int32_t month = Virtual<int32_t>(At<void*>(self, field::kClock), 0x60);
    const float share = At<const float*>(self, field::kMonthShares)[month];
    const char* const g = static_cast<char*>(self) + group * kGroupSize;
    double value = static_cast<double>(*reinterpret_cast<const int64_t*>(g + field::kGroupAssessed)) * share;
    value *= *reinterpret_cast<const float*>(g + field::kGroupRate);
    value *= Global<float>(addr::Budget_TaxScale);
    return static_cast<int64_t>(value);
}

// 24 (0x00690330): the large or the small budget window (+0x850).
void __fastcall ShowBudgetWindow(void* self, void*)
{
    using AllocateFn = void*(__cdecl*)(std::size_t size);
    using WindowFn = void*(__thiscall*)(void* memory);
    const auto operatorNew = reinterpret_cast<AllocateFn>(addr::SC4_operator_new);
    const bool small = At<uint8_t>(self, field::kSmallWindow) != 0;
    void* const memory = operatorNew(small ? 0x28 : 0x60);
    if (!memory)
        return;
    void* const window = reinterpret_cast<WindowFn>(small ? addr::BudgetWindow_Small : addr::BudgetWindow_Large)(memory);
    if (!window)
        return;
    Virtual<uint32_t>(window, 0x4);
    Virtual<uint32_t>(window, 0x8);
}

// 34, 35 (0x0068FD40, both): no funding by that key.
int64_t __fastcall GetFunding(void*, void*, uint32_t)
{
    return 0;
}

// 36 (0x0068FD50).
float __fastcall GetTaxRate(void* self, void*, int32_t group)
{
    if (group < static_cast<int32_t>(kTaxGroups))
        return At<float>(self, group * kGroupSize + field::kGroupRate);
    return 0.0f;
}

// 37 (0x0068FD70).
void __fastcall SetTaxRate(void* self, void*, int32_t group, float rate)
{
    if (group < static_cast<int32_t>(kTaxGroups))
        At<float>(self, group * kGroupSize + field::kGroupRate) = rate;
}

// 38 (0x0068FDE0).
int64_t __fastcall GetWeightedAssessedTaxValue(void* self, void*, int32_t group)
{
    if (group < static_cast<int32_t>(kTaxGroups))
        return At<int64_t>(self, group * kGroupSize + field::kGroupAssessed);
    return 0;
}

// 42 (0x0068FDA0): bonds allowed times the bond limit.
int64_t __fastcall GetCurrentBorrowingLimit(void* self, void*)
{
    return static_cast<int64_t>(At<int32_t>(self, field::kBondsAllowed)) * At<int64_t>(self, field::kBondLimit);
}

// 44 (0x0068FDD0).
int64_t __fastcall GetCurrentMaxOutstandingBondsLimit(void* self, void*)
{
    return At<int32_t>(self, field::kBondsAllowed);
}

// 45 (0x00690500).
int64_t __fastcall GetTotalMonthlyBondPayments(void* self, void*)
{
    return BondPayments(self);
}

// 48 (0x0068FEF0): the annuity payment: amount x r / (1 - (1 + r)^-n), r the
// yearly rate (+0x7C0) / 12, n the loan's months (+0x7C4, unsigned), x87 at
// 53 bits, truncated.
int64_t __fastcall GetMonthlyPaymentForLoan(void* self, void*, int64_t amount)
{
    const double rate = static_cast<double>(At<float>(self, field::kInterestRate)) * Global<double>(addr::Budget_OneTwelfth);
    const double interest = static_cast<double>(amount) * rate;
    const double growth = rate + 1.0;
    const double months = static_cast<double>(At<uint32_t>(self, field::kLoanMonths));
    const double factor = GamePow(growth, -months);
    return static_cast<int64_t>(interest / (1.0 - factor));
}

// 49 (0x0068FF50): the payment (slot 48) times the months.
int64_t __fastcall GetFullCostOfLoan(void* self, void*, int64_t amount)
{
    return Virtual<int64_t>(self, slot::kMonthlyPaymentForLoan, amount) *
           static_cast<int64_t>(At<uint32_t>(self, field::kLoanMonths));
}

// 57 (0x0068FF80) (item {department, line}): the department's line item
// (department slot 20).
void* __fastcall GetLineItemFromBudgetItem(void* self, void*, const uint32_t* item)
{
    void* const department = Virtual<void*>(self, slot::kGetDepartmentById, item[0]);
    return department ? Virtual<void*>(department, 0x50, item[1]) : nullptr;
}

#define OPENSC4_SLOT(index, original, fn) {index, original, reinterpret_cast<void*>(&fn)}

const Slot kSlots[] = {
    OPENSC4_SLOT(5, 0x00690140, SetTotalFunds),
    OPENSC4_SLOT(6, 0x0068FD10, (Getter<int64_t, field::kFunds>)),
    OPENSC4_SLOT(7, 0x00690170, DepositFunds),
    OPENSC4_SLOT(8, 0x00690200, WithdrawFunds),
    OPENSC4_SLOT(9, 0x0068FD20, (Getter<int64_t, field::kMinFunds>)),
    OPENSC4_SLOT(10, 0x006AAF00, (Getter<int64_t, field::kYearIncome>)),
    OPENSC4_SLOT(11, 0x00690270, (ProjectedYear<slot::kMonthlyIncome, slot::kYearIncomeSoFar>)),
    OPENSC4_SLOT(12, 0x006AAEF0, (Getter<int64_t, field::kYearExpense>)),
    OPENSC4_SLOT(13, 0x006902D0, (ProjectedYear<slot::kMonthlyExpense, slot::kYearExpenseSoFar>)),
    OPENSC4_SLOT(14, 0x00690990, GetTotalMonthlyExpense),
    OPENSC4_SLOT(15, 0x00694260, (Getter<int64_t, field::kTotalYearlyExpense>)),
    OPENSC4_SLOT(16, 0x00690A30, GetTotalMonthlyIncome),
    OPENSC4_SLOT(17, 0x00694270, (Getter<int64_t, field::kTotalYearlyIncome>)),
    OPENSC4_SLOT(18, 0x006904C0, GetGroupTaxIncome),
    OPENSC4_SLOT(19, 0x0068FD30, (Getter<int64_t, field::kTaxIncome>)),
    OPENSC4_SLOT(20, 0x00694280, Int64Setter<field::kTotalMonthlyExpense>),
    OPENSC4_SLOT(21, 0x006942A0, Int64Setter<field::kTotalYearlyExpense>),
    OPENSC4_SLOT(22, 0x006942C0, Int64Setter<field::kTotalMonthlyIncome>),
    OPENSC4_SLOT(23, 0x006942E0, Int64Setter<field::kTotalYearlyIncome>),
    OPENSC4_SLOT(24, 0x00690330, ShowBudgetWindow),
    OPENSC4_SLOT(34, 0x0068FD40, GetFunding),
    OPENSC4_SLOT(35, 0x0068FD40, GetFunding),
    OPENSC4_SLOT(36, 0x0068FD50, GetTaxRate),
    OPENSC4_SLOT(37, 0x0068FD70, SetTaxRate),
    OPENSC4_SLOT(38, 0x0068FDE0, GetWeightedAssessedTaxValue),
    OPENSC4_SLOT(39, 0x0068FD90, (Getter<int64_t, field::kBondIncrement>)),
    OPENSC4_SLOT(41, 0x006B9850, (Getter<int64_t, field::kField38>)),
    OPENSC4_SLOT(42, 0x0068FDA0, GetCurrentBorrowingLimit),
    OPENSC4_SLOT(43, 0x0068FDC0, (Getter<int64_t, field::kBondLimit>)),
    OPENSC4_SLOT(44, 0x0068FDD0, GetCurrentMaxOutstandingBondsLimit),
    OPENSC4_SLOT(45, 0x00690500, GetTotalMonthlyBondPayments),
    OPENSC4_SLOT(47, 0x00694250, (Getter<uint32_t, field::kLoanMonths>)),
    OPENSC4_SLOT(48, 0x0068FEF0, GetMonthlyPaymentForLoan),
    OPENSC4_SLOT(49, 0x0068FF50, GetFullCostOfLoan),
    OPENSC4_SLOT(57, 0x0068FF80, GetLineItemFromBudgetItem),
};

#undef OPENSC4_SLOT

bool InstallInterface(registry::Mode)
{
    return SwapSlots("budget interface", addr::cSC4BudgetSimulator_vftable, kSlots, sizeof(kSlots) / sizeof(kSlots[0]));
}

const bool g_registeredInterface = registry::Register({
    "simulators.budget.interface",
    "simulators/budget",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4BudgetSimulator_vftable,
    "Our cISC4BudgetSimulator funds, totals, taxes and loan figures on the original object (docs/budget.md)",
    false,
    InstallInterface,
});

} // namespace
} // namespace opensc4::budget_sim
