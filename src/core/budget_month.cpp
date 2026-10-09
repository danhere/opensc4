// The budget simulator's month, tax collection and messages (docs/budget.md).
#include "core/address_check.h"
#include "core/budget_sim.h"
#include "core/game.h"
#include "core/game_access.h"
#include "core/hook.h"
#include "core/log.h"
#include "core/registry.h"

#include <opensc4/addresses.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opensc4::budget_sim {
namespace {

using AllocateFn = void*(__cdecl*)(std::size_t size);
using FreeFn = void(__cdecl*)(void* p);
using ThisFn = void(__thiscall*)(void* self);
const auto Allocate = reinterpret_cast<AllocateFn>(addr::SC4_stl_allocate);
const auto Free = reinterpret_cast<FreeFn>(addr::SC4_Free);

// A message (cGZMessage, 0x2C bytes) of `type` from the simulator (slots 12
// and 33), through the message server referenced at +0x1C.
void PostFromSelf(void* self, uint32_t type)
{
    using MessageFn = void*(__thiscall*)(void* memory);
    void* const memory = reinterpret_cast<AllocateFn>(addr::RZ_operator_new)(0x2C);
    void* const message = memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
    Virtual<uint32_t>(message, 0x4);
    Virtual<uint32_t>(message, 0x14, type);
    Virtual<uint32_t>(message, 0x30, self);
    Virtual<uint32_t>(message, 0x84, self);
    Virtual<uint32_t>(At<void*>(self, field::kMessageServer), 0x10, message, 0u);
    Virtual<uint32_t>(message, 0x8);
}

void NotifyFunds(void* self)
{
    reinterpret_cast<ThisFn>(addr::cSC4BudgetSimulator_NotifyFunds)(self);
}

// The figure kept for the charts (the service at 0x00B43DC0, slot 12).
void Figure(uint32_t id, uint32_t value)
{
    Virtual<uint32_t>(Global<void*>(addr::SC4_MonthlyFigures), 0x30, id, value);
}

uint32_t Clamp32(int64_t value)
{
    return value > 0xFFFFFFFFll ? 0xFFFFFFFFu : static_cast<uint32_t>(value);
}

// A float through the round-to-nearest trick the game uses: add 1.5 x 2^23,
// store as float, take the bits less 0x4B400000.
int32_t RoundViaFloat(double value)
{
    const float sum = static_cast<float>(value + static_cast<double>(Global<float>(addr::Budget_RoundingMagic)));
    uint32_t bits;
    std::memcpy(&bits, &sum, sizeof bits);
    return static_cast<int32_t>(bits + 0xB4C00000u);
}

// --- 0x006900F0 (message 0x66956814): the 12 tax rates for the charts -------------

void __fastcall PublishTaxRates(void* self, void*)
{
    static bool logged = false;
    FirstCall("PublishTaxRates", logged);
    for (uint32_t group = 0; group < kTaxGroups; ++group) {
        const double rate = At<float>(self, field::kGroupRate + group * kGroupSize);
        Figure(group + 0xAABAAA9Fu, static_cast<uint32_t>(RoundViaFloat(rate * Global<float>(addr::Budget_Ten))));
    }
}

// --- 0x00690450, 0x00690490 (message 0x26D31EC4): starting funds and rates by
// the level from +0x18's slot 24 -----------------------------------------------------

void __fastcall SetStartingFunds(void* self, void*)
{
    const int32_t level = Virtual<int32_t>(At<void*>(self, field::kClock), 0x60);
    const int64_t funds = At<const int64_t*>(self, field::kStartingFunds)[level];
    At<int64_t>(self, field::kFunds) = funds;
    At<int64_t>(self, field::kFundsShown) = funds;
    NotifyFunds(self);
}

void __fastcall SetStartingTaxRates(void* self, void*)
{
    for (uint32_t group = 0; group < kTaxGroups; ++group) {
        const int32_t level = Virtual<int32_t>(At<void*>(self, field::kClock), 0x60);
        At<float>(self, field::kGroupRate + group * kGroupSize) = At<const float*>(self, field::kStartingRates)[level];
    }
}

// --- 0x00691E20: the month's taxes ------------------------------------------------

// Per lot with a building that pays taxes (lot slot 37): the tax group from
// the zone (lot slot 41) and the building's wealth (occupant slot 11, its
// first word) through the table at 0x00AB0840; the building's value
// (property 0xE91A0B5F) plus the land value curve (+0x804) at the lot's land
// value times its size, through the factor tables, the month's share, the
// rate and 1/1200. Each group's assessed value (+0xE0) and the month's tax
// by group (+0xE8) are kept; returns the total.
int64_t __fastcall CollectTaxes(void* self, void*)
{
    static bool logged = false;
    FirstCall("CollectTaxes", logged);
    char* const base = static_cast<char*>(self);
    for (uint32_t group = 0; group < kTaxGroups; ++group)
        At<int64_t>(self, field::kGroupAssessed + group * kGroupSize) = 0;
    double total = Global<double>(addr::SC4_DoubleZero);
    double zero = Global<double>(addr::SC4_DoubleZero);
    struct {
        double* begin;
        double* end;
        double* capacity;
    } byGroup{};
    using AssignFn = void(__thiscall*)(void* vector, uint32_t count, const double* value);
    reinterpret_cast<AssignFn>(addr::DoubleVector_Assign)(&byGroup, kTaxGroups, &zero);
    void* const landValues = Global<void*>(addr::SC4_Service43D88);
    auto** const iterator = Virtual<char**>(Global<void*>(addr::SC4_LotManager), 0x3C);
    if (iterator[1])
        iterator[0] = **reinterpret_cast<char***>(iterator[1]);
    using GetSint64Fn = uint8_t(__cdecl*)(void* holder, uint32_t id, int64_t* value);
    using CurveFn = double(__thiscall*)(void* curve, float x);
    const auto* const groupTable = reinterpret_cast<const int16_t*>(addr::Budget_TaxGroupTable);
    for (;;) {
        auto** const end = reinterpret_cast<char**>(iterator[1]);
        if (!end)
            break;
        char* const at = iterator[0];
        if (at == *end)
            break;
        iterator[0] = *reinterpret_cast<char**>(at);
        void* const lot = At<void*>(at, 0x8);
        void* const building = Virtual<void*>(lot, 0xC0);
        if (!building || !Virtual<uint8_t>(lot, 0x94))
            continue;
        const int32_t* const wealth = Virtual<const int32_t*>(building, 0x2C);
        const int32_t zone = Virtual<int32_t>(lot, 0xA4);
        const int16_t group = groupTable[zone + wealth[0] * 4];
        if (group == -1)
            continue;
        const float rate = Virtual<float>(self, 0x90, static_cast<int32_t>(group));
        int64_t value = 0;
        const float factor = At<float>(self, field::kGroupFactors + group * 4);
        reinterpret_cast<GetSint64Fn>(addr::SC4_GetPropertySint64)(
            Virtual<void*>(Virtual<void*>(building, 0xC), 0x18), 0xE91A0B5F, &value);
        uint8_t size[2] = {};
        Virtual<uint32_t>(lot, 0x58, &size[0], &size[1]);
        int32_t x = 0, z = 0;
        Virtual<uint32_t>(lot, 0x54, &x, &z);
        const uint8_t landValue = Virtual<uint8_t>(landValues, 0x18, x, z);
        const double curve = reinterpret_cast<CurveFn>(addr::SC4Curve_Evaluate)(base + field::kLandValueCurve,
                                                                                 static_cast<float>(landValue));
        const int32_t extra = RoundViaFloat(curve * size[0] * size[1]);
        const int64_t assessed = value + static_cast<int64_t>(static_cast<uint32_t>(extra));
        const float scale = At<const float*>(self, field::kWealthScale)[wealth[1] - zone];
        const int64_t worth = static_cast<int64_t>(static_cast<double>(assessed) * scale * factor);
        const int32_t month = Virtual<int32_t>(At<void*>(self, field::kClock), 0x60);
        double tax = static_cast<double>(worth) * At<const float*>(self, field::kMonthShares)[month];
        At<int64_t>(self, field::kGroupAssessed + group * kGroupSize) += worth;
        tax *= rate;
        tax *= Global<float>(addr::Budget_TaxScale);
        total = total + tax;
        byGroup.begin[group] = tax + byGroup.begin[group];
    }
    reinterpret_cast<ThisFn>(addr::SC4ListIterator_Release)(iterator);
    const int64_t result = static_cast<int64_t>(total);
    for (uint32_t group = 0; group < kTaxGroups; ++group)
        At<int64_t>(self, field::kGroupTax + group * kGroupSize) = static_cast<int64_t>(byGroup.begin[group]);
    if (byGroup.begin)
        Free(byGroup.begin);
    return result;
}

// --- 0x00692510 (MonthPassed, message 0x66956816) -------------------------------

// The game's cIGZString over an STL-style buffer.
struct GameString {
    const void* vtable;
    char* begin;
    char* end;
    char* capacity;
    uint32_t zero;
};

void __fastcall MonthPassed(void* self, void*)
{
    static bool logged = false;
    FirstCall("MonthPassed", logged);
    if (At<int64_t>(self, field::kFunds) != At<int64_t>(self, field::kFundsShown))
        At<int64_t>(self, field::kFunds) = At<int64_t>(self, field::kFundsShown);
    int32_t year = 0, month = 0, day = 0, other = 0;
    Virtual<uint32_t>(Global<void*>(addr::SC4_SimulatorService), 0x18, &year, &month, &day, &other, &other);
    const int32_t thisYear = year, thisMonth = month;
    int32_t index;
    if (month - 1 > 0) {
        index = month - 2;
    } else {
        index = 11;
        year = thisYear - 1;
    }

    // Income: taxes, +0x24's monthly income, the month end service's, the
    // departments'.
    using TaxFn = int64_t(__thiscall*)(void* self);
    const int64_t tax = reinterpret_cast<TaxFn>(addr::cSC4BudgetSimulator_CollectTaxes)(self);
    At<int64_t>(self, field::kYearTax) += tax;
    At<int64_t>(self, field::kTaxIncome) = tax;
    const int64_t tradeIncome = Virtual<int64_t>(At<void*>(self, field::kTrade), 0x5C);
    At<int64_t>(self, field::kTradeIncomeByMonth + 8 * index) = tradeIncome;
    At<int64_t>(self, field::kYearTradeIncome) += tradeIncome;
    void* const monthEnd = Global<void*>(addr::SC4_MonthEndService);
    int64_t income = Virtual<int64_t>(monthEnd, 0x80) + tradeIncome + tax;
    char* const departments = At<char*>(self, field::kDepartments);
    for (char* node = At<char*>(departments, 0x8); node != At<char*>(self, field::kDepartments); node = MapNext(node))
        income += Virtual<int64_t>(At<void*>(node, 0x14), 0x40);

    // Expense: +0x24's, the month end service's, the departments' (each
    // recalculated, slot 29, and its year's spending, slots 30 and 31), the
    // bonds.
    const int64_t tradeExpense = Virtual<int64_t>(At<void*>(self, field::kTrade), 0x58);
    At<int64_t>(self, field::kTradeExpenseByMonth + 8 * index) = tradeExpense;
    At<int64_t>(self, field::kYearTradeExpense) += tradeExpense;
    int64_t expense = Virtual<int64_t>(monthEnd, 0x84) + tradeExpense;
    for (char* node = At<char*>(At<char*>(self, field::kDepartments), 0x8); node != At<char*>(self, field::kDepartments);
         node = MapNext(node)) {
        void* const department = At<void*>(node, 0x14);
        Virtual<uint32_t>(department, 0x74);
        const int64_t spent = Virtual<int64_t>(department, 0x3C);
        const int64_t soFar = Virtual<int64_t>(department, 0x78);
        Virtual<uint32_t>(department, 0x7C, spent + soFar);
        expense += spent;
        GameString name{};
        name.begin = static_cast<char*>(Allocate(8));
        name.end = name.begin;
        name.capacity = name.begin + 8;
        name.begin[0] = 0;
        name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        name.zero = 0;
        Virtual<uint32_t>(department, 0x10, &name);
        name.vtable = reinterpret_cast<const void*>(addr::SC4String_Vtable);
        if (name.begin)
            Free(name.begin);
    }

    // Bonds: the payment taken off what is owed; a bond due this month is
    // repaid (message 0xCA3FBDCE), the others accrue interest on what is left.
    char* const bonds = At<char*>(self, field::kBonds);
    for (char* bond = At<char*>(bonds, 0x0); bond != At<char*>(self, field::kBonds);) {
        int32_t dy = At<int32_t>(bond, 0xC) - thisYear;
        int32_t dm = At<int32_t>(bond, 0x8) - thisMonth;
        if (dm < 0) {
            --dy;
            dm += 12;
        }
        const int64_t payment = At<int64_t>(bond, 0x30);
        At<int64_t>(self, field::kYearBondPayments) += payment;
        expense += payment;
        At<int64_t>(bond, 0x20) -= payment;
        At<int64_t>(bond, 0x28) += payment;
        if (dy == 0 && dm == 0) {
            At<int64_t>(self, field::kField38) -= At<int64_t>(bond, 0x10);
            char* const next = At<char*>(bond, 0x0);
            char* const previous = At<char*>(bond, 0x4);
            At<char*>(previous, 0x0) = next;
            At<char*>(next, 0x4) = previous;
            Free(bond);
            --At<int32_t>(self, field::kBondCount);
            if (At<void*>(self, field::kMessageServer))
                PostFromSelf(self, 0xCA3FBDCE);
            bond = next;
        } else {
            const int64_t owed = At<int64_t>(bond, 0x38);
            const double rate = static_cast<double>(At<float>(self, field::kInterestRate)) * Global<float>(addr::Budget_OneTwelfthFloat);
            const int64_t interest = static_cast<int64_t>(rate * static_cast<double>(owed));
            At<int64_t>(bond, 0x38) = interest - payment + owed;
            if (dy == 0 && dm == 1)
                At<int64_t>(bond, 0x30) = At<int64_t>(bond, 0x38);
            bond = At<char*>(bond, 0x0);
        }
    }

    // The funds, the year so far, the charts.
    Virtual<uint32_t>(self, 0x14, Virtual<int64_t>(self, 0x18) + (income - expense));
    At<int64_t>(self, field::kYearIncome) += income;
    At<int64_t>(self, field::kYearExpense) += expense;
    At<int64_t>(self, field::kTotalMonthlyIncome) = income;
    At<int64_t>(self, field::kTotalMonthlyExpense) = expense;
    const uint32_t incomeFigure = Clamp32(income);
    const uint32_t expenseFigure = Clamp32(expense);
    const uint32_t fundsFigure = static_cast<uint32_t>(Virtual<int64_t>(self, 0x18) / 1000);
    Figure(0x6A51178B, incomeFigure);
    Figure(0xCA5BA3FB, expenseFigure);
    Figure(0x4A5BA3D0, fundsFigure);
    At<int64_t>(self, field::kFundsShown) = At<int64_t>(self, field::kFunds);

    // The year's end (the month just past was December).
    if (index == 11) {
        for (const std::size_t offset : {field::kYearTax, field::kYearTradeIncome, std::size_t{0x770}, std::size_t{0x778},
                                         std::size_t{0x760}, std::size_t{0x758}, field::kYearIncome, field::kYearExpense})
            At<int64_t>(self, offset) = 0;
        for (char* node = At<char*>(At<char*>(self, field::kDepartments), 0x8);
             node != At<char*>(self, field::kDepartments); node = MapNext(node))
            Virtual<uint32_t>(At<void*>(node, 0x14), 0x7C, int64_t{0});
        At<int64_t>(self, field::kYearTradeExpense) = 0;
        At<int64_t>(self, 0x768) = 0;
        At<int64_t>(self, field::kYearBondPayments) = 0;
    }

    // Out of money: message 0xAA5CF1E3 (the game ends for the budget).
    if (At<int64_t>(self, field::kFunds) <= At<int64_t>(self, field::kMinFunds)) {
        using MessageFn = void*(__thiscall*)(void* memory);
        void* const memory = reinterpret_cast<AllocateFn>(addr::RZ_operator_new)(0x2C);
        void* const message = memory ? reinterpret_cast<MessageFn>(addr::cGZMessage_cGZMessage)(memory) : nullptr;
        Virtual<uint32_t>(message, 0x4);
        Virtual<uint32_t>(message, 0x14, 0xAA5CF1E3u);
        Virtual<uint32_t>(Global<void*>(addr::SC4_MessageServer), 0x10, message, 0u);
        Virtual<uint32_t>(message, 0x8);
    }
    (void)year;
}

using SelfFn = void(__fastcall*)(void*, void*);
using TaxesFn = int64_t(__fastcall*)(void*, void*);
SelfFn g_originalMonth = nullptr;
SelfFn g_originalPublish = nullptr;
SelfFn g_originalStartFunds = nullptr;
SelfFn g_originalStartRates = nullptr;
TaxesFn g_originalTaxes = nullptr;

bool InstallMonth(registry::Mode)
{
    const addr::Entry* month = addr_check::Find(addr::cSC4BudgetSimulator_MonthPassed);
    const addr::Entry* taxes = addr_check::Find(addr::cSC4BudgetSimulator_CollectTaxes);
    const addr::Entry* publish = addr_check::Find(addr::cSC4BudgetSimulator_PublishTaxRates);
    const addr::Entry* funds = addr_check::Find(addr::cSC4BudgetSimulator_SetStartingFunds);
    const addr::Entry* rates = addr_check::Find(addr::cSC4BudgetSimulator_SetStartingTaxRates);
    return game::IsBuild641() && month && taxes && publish && funds && rates &&
           hook::Install(*month, &MonthPassed, &g_originalMonth) &&
           hook::Install(*taxes, &CollectTaxes, &g_originalTaxes) &&
           hook::Install(*publish, &PublishTaxRates, &g_originalPublish) &&
           hook::Install(*funds, &SetStartingFunds, &g_originalStartFunds) &&
           hook::Install(*rates, &SetStartingTaxRates, &g_originalStartRates);
}

const bool g_registeredMonth = registry::Register({
    "simulators.budget.month",
    "simulators/budget",
    registry::Kind::Detour,
    registry::Phase::PostAppInit,
    addr::cSC4BudgetSimulator_MonthPassed,
    "Our budget month: tax collection over the lots, incomes and expenses, bonds, the year's end; the tax rate "
    "figures and the starting funds and rates (docs/budget.md)",
    false,
    InstallMonth,
});

// --- DoMessage (+0x4 slot 3, 0x00693450) -------------------------------------------

// Cheat codes (message 0x230E27AC, code at slot 9): 0xCBF31943 deposits
// 10,000 and may start a riot (disaster 0x42963812 at a random place) the
// more often it is used; 0x4AB87766 adds 1,000; 0x493F4FE6 sets the funds
// to its second word.
uint8_t Cheat(void* self, void* message)
{
    const uint32_t code = Virtual<uint32_t>(message, 0x24);
    using RandomFn = double(__thiscall*)(void* generator);
    const auto random = [] {
        return reinterpret_cast<RandomFn>(addr::SC4Random_Unit)(reinterpret_cast<void*>(addr::Budget_CheatRandom));
    };
    if (code == 0x493F4FE6) {
        const char* const text = Virtual<const char*>(Virtual<void*>(message, 0x3C), 0x14);
        alignas(4) char words[0x40];
        using TokenizerFn = void*(__thiscall*)(void* self, const char* text);
        using CountFn = int32_t(__thiscall*)(void* self);
        using Atoi64Fn = int64_t(__cdecl*)(const char* text);
        reinterpret_cast<TokenizerFn>(addr::SC4Tokenizer_SC4Tokenizer)(words, text);
        if (reinterpret_cast<CountFn>(addr::SC4Tokenizer_Count)(words) < 2) {
            reinterpret_cast<ThisFn>(addr::SC4Tokenizer_Destroy)(words);
            return 0;
        }
        void* const word = Virtual<void*>(words, 0x40, 1u);
        const int64_t funds = reinterpret_cast<Atoi64Fn>(addr::CRT_atoi64)(At<const char*>(word, 0x4));
        Virtual<uint32_t>(self, 0x14, funds);
        reinterpret_cast<ThisFn>(addr::SC4Tokenizer_Destroy)(words);
        return 1;
    }
    if (code == 0x4AB87766) {
        Virtual<uint32_t>(self, 0x14, Virtual<int64_t>(self, 0x18) + 1000);
        return 1;
    }
    if (code != 0xCBF31943)
        return 0;
    void* const city = Global<void*>(addr::SC4_City);
    if (!Virtual<uint8_t>(city, 0x58))
        return 1;
    Virtual<uint32_t>(self, 0x1C, int64_t{10000});
    const int32_t uses = ++At<int32_t>(self, field::kFundraisers);
    const double draw = random();
    const float one = Global<float>(addr::SC4_FloatOneA81228);
    const double threshold = one - one / (uses * static_cast<double>(Global<float>(addr::Budget_FundraiserStep)) + one);
    if (!(threshold > draw))
        return 1;
    void* const disasters = Global<void*>(addr::SC4_Service43D3C);
    void* const riot = Virtual<void*>(disasters, 0x1C, 0x42963812u);
    if (!riot)
        return 1;
    const float width = Virtual<float>(city, 0x164);
    const float depth = Virtual<float>(city, 0x168);
    const double spread = Global<double>(addr::Budget_Quarter);
    const double inset = static_cast<double>(Global<float>(addr::Budget_Eighth)) * Global<float>(addr::Budget_Three);
    const float x = static_cast<float>(random() * width * spread + width * inset);
    const int32_t z = static_cast<int32_t>(random() * depth * spread + depth * inset);
    if (Virtual<uint32_t>(riot, 0x18, static_cast<int32_t>(static_cast<int64_t>(x)), z))
        At<int32_t>(self, field::kFundraisers) = 0;
    return 1;
}

uint8_t __fastcall DoMessage(void* interface4, void*, void* message)
{
    static bool logged = false;
    FirstCall("DoMessage", logged);
    void* const self = static_cast<char*>(interface4) - 0x4;
    switch (Virtual<uint32_t>(message, 0x10)) { // GetType
    case 0x66956810: NotifyFunds(self); return 1;
    case 0x66956814: reinterpret_cast<ThisFn>(addr::cSC4BudgetSimulator_PublishTaxRates)(self); return 1;
    case 0x66956816: reinterpret_cast<ThisFn>(addr::cSC4BudgetSimulator_MonthPassed)(self); return 1;
    case 0x26D31EC4:
        reinterpret_cast<ThisFn>(addr::cSC4BudgetSimulator_SetStartingFunds)(self);
        reinterpret_cast<ThisFn>(addr::cSC4BudgetSimulator_SetStartingTaxRates)(self);
        return 1;
    case 0x29222F02: {
        const uint32_t which = Virtual<uint32_t>(message, 0x24);
        if (which == 0x892230F2 || which == 0xC92230EE) {
            At<uint8_t>(self, field::kSmallWindow) = which == 0x892230F2 ? 1 : 0;
            Virtual<uint32_t>(self, 0x60); // ShowBudgetWindow
        }
        return 1;
    }
    case 0x230E27AC: return Cheat(self, message);
    default: return 1;
    }
}

const Slot kMessageSlots[] = {{3, 0x00693450, reinterpret_cast<void*>(&DoMessage)}};

bool InstallMessages(registry::Mode)
{
    return SwapSlots("budget messages", addr::cSC4BudgetSimulator_vftable_4, kMessageSlots, 1);
}

const bool g_registeredMessages = registry::Register({
    "simulators.budget.messages",
    "simulators/budget",
    registry::Kind::Vtable,
    registry::Phase::PostAppInit,
    addr::cSC4BudgetSimulator_vftable_4,
    "Our budget DoMessage, the cheat codes included (docs/budget.md)",
    false,
    InstallMessages,
});

} // namespace
} // namespace opensc4::budget_sim
