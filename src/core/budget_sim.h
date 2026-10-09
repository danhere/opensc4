// Shared between the budget simulator's files (docs/budget.md).
#pragma once

#include "core/game_access.h"

#include <cstddef>
#include <cstdint>

namespace opensc4::budget_sim {

// Fields of cSC4BudgetSimulator (offsets from the object's start; 0x854 bytes).
namespace field {
constexpr std::size_t kInitialised = 0xC;           // uint8
constexpr std::size_t kRefs = 0x10;
constexpr std::size_t kLoaded = 0x14;                // uint8: Read ran
constexpr std::size_t kClock = 0x18;                 // slot 24: this month's index
constexpr std::size_t kMessageServer = 0x1C;
constexpr std::size_t kTrade = 0x24;                 // slots 22, 23: a monthly expense and income
constexpr std::size_t kFunds = 0x28;                 // int64
constexpr std::size_t kFundsShown = 0x30;            // int64: a copy of the funds
constexpr std::size_t kField38 = 0x38;               // int64 (slot 41)
constexpr std::size_t kYearExpense = 0x40;           // int64: this year's expense so far
constexpr std::size_t kYearIncome = 0x48;            // int64: this year's income so far
constexpr std::size_t kDepartments = 0x54;           // map header: id -> cSC4DepartmentBudget* (+0x14)
constexpr std::size_t kGroups = 0x60;                // map header: the department groups
constexpr std::size_t kGroupRate = 0x70;             // float, per tax group (12 x 0x80 from +0x70)
constexpr std::size_t kGroupAssessed = 0xE0;         // int64, per tax group
constexpr std::size_t kTaxIncome = 0x670;            // int64
constexpr std::size_t kBonds = 0x780;                // list head: next +0, monthly payment +0x30 (int64)
constexpr std::size_t kMinFunds = 0x7A0;             // int64
constexpr std::size_t kBondIncrement = 0x7A8;        // int64
constexpr std::size_t kBondsAllowed = 0x7B0;         // int32
constexpr std::size_t kBondLimit = 0x7B8;            // int64
constexpr std::size_t kInterestRate = 0x7C0;         // float: yearly
constexpr std::size_t kLoanMonths = 0x7C4;           // uint32
constexpr std::size_t kMonthShares = 0x814;          // float*: this month's share of a year, by month
constexpr std::size_t kTotalMonthlyIncome = 0x830;   // int64
constexpr std::size_t kTotalYearlyIncome = 0x838;    // int64
constexpr std::size_t kTotalMonthlyExpense = 0x840;  // int64
constexpr std::size_t kTotalYearlyExpense = 0x848;   // int64
constexpr std::size_t kSmallWindow = 0x850;          // uint8
} // namespace field

constexpr uint32_t kTaxGroups = 12;
constexpr std::size_t kGroupSize = 0x80;

// cISC4BudgetSimulator slots used between the pieces (byte offsets).
namespace slot {
constexpr std::size_t kDepositFunds = 0x1C;
constexpr std::size_t kWithdrawFunds = 0x20;
constexpr std::size_t kYearIncomeSoFar = 0x28;
constexpr std::size_t kYearExpenseSoFar = 0x30;
constexpr std::size_t kMonthlyExpense = 0x38;
constexpr std::size_t kMonthlyIncome = 0x40;
constexpr std::size_t kGetGroupTaxIncome = 0x48;
constexpr std::size_t kGetDepartmentById = 0x70;
constexpr std::size_t kMonthlyPaymentForLoan = 0xC0;
} // namespace slot

struct Slot {
    int index;
    std::uintptr_t original; // the game's function, expected in the slot
    void* ours;
};

// Swaps every listed slot of a vtable or none: each must still hold the
// game's function.
bool SwapSlots(const char* what, std::uintptr_t address, const Slot* slots, std::size_t count);

void FirstCall(const char* name, bool& logged);

// The game's pow (_CIpow), for results identical to the bit.
double GamePow(double base, double exponent);

// In-order successor in a std::map (nodes: +0x4 parent, +0x8 left, +0xC
// right), as the game's inlined iterator increment.
inline char* MapNext(char* node)
{
    if (char* right = At<char*>(node, 0xC)) {
        while (char* left = At<char*>(right, 0x8))
            right = left;
        return right;
    }
    char* parent = At<char*>(node, 0x4);
    while (node == At<char*>(parent, 0xC)) {
        node = parent;
        parent = At<char*>(parent, 0x4);
    }
    if (At<char*>(node, 0xC) != parent)
        node = parent;
    return node;
}

} // namespace opensc4::budget_sim
