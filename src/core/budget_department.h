// The department budgets (cSC4DepartmentBudget) and their line items
// (cSC4LineItem) the budget simulator owns (docs/budget.md, "Departments and
// line items").
#pragma once

#include "core/game_access.h"

#include <cstddef>
#include <cstdint>

namespace opensc4::budget_sim {

// cSC4DepartmentBudget (class 0xE990BFFC, 0x68 bytes): the primary interface
// at +0x0, cIGZSerializable at +0x4.
namespace dept {
constexpr std::size_t kSerializable = 0x4;
constexpr std::size_t kInitialised = 0x8;    // uint8
constexpr std::size_t kRefs = 0xC;           // uint32
constexpr std::size_t kId = 0x10;            // uint32
constexpr std::size_t kName = 0x14;          // string: vtable, buffer at +0x18 (begin, end, capacity), +0x24 zero
constexpr std::size_t kNameInstance = 0x28;  // uint32: localised text instance
constexpr std::size_t kNameGroup = 0x2C;     // uint32: localised text group
constexpr std::size_t kGroup = 0x30;         // uint32: budget group
constexpr std::size_t kFunding = 0x34;       // float: a fraction
constexpr std::size_t kMaxFunding = 0x38;    // float
constexpr std::size_t kSpending = 0x40;      // int64
constexpr std::size_t kFixed = 0x48;         // uint8
constexpr std::size_t kLines = 0x4C;         // map: line ID -> cSC4LineItem* (node +0x10 key, +0x14 value); count +0x50
constexpr std::size_t kLocal = 0x58;         // set of (holder, purpose) (node +0x10, +0x14); count +0x5C
constexpr std::size_t kSize = 0x68;
} // namespace dept

// cSC4LineItem (class 0xAA313C9F, 0x68 bytes): the primary interface at +0x0,
// cIGZSerializable at +0x4.
namespace line {
constexpr std::size_t kSerializable = 0x4;
constexpr std::size_t kOwned = 0x8;          // uint8: in a department
constexpr std::size_t kName = 0xC;           // string: vtable, buffer at +0x10 (begin, end, capacity), +0x1C zero
constexpr std::size_t kNameInstance = 0x20;  // uint32
constexpr std::size_t kNameGroup = 0x24;     // uint32: 0 = the name of the building exemplar `instance`
constexpr std::size_t kNamePending = 0x28;   // uint8: the name is made again from the two IDs when asked (after Read)
constexpr std::size_t kSecondary = 0x30;     // int64
constexpr std::size_t kFull = 0x38;          // int64: full expenses
constexpr std::size_t kCurrent = 0x40;       // int64: current expenses
constexpr std::size_t kIncome = 0x48;        // int64
constexpr std::size_t kRefs = 0x50;          // uint32
constexpr std::size_t kId = 0x54;            // uint32
constexpr std::size_t kFlags = 0x58;         // uint32: display flags
constexpr std::size_t kLocal = 0x5C;         // uint8: locally funded
constexpr std::size_t kDepartment = 0x60;    // cSC4DepartmentBudget* (referenced)
constexpr std::size_t kType = 0x64;          // uint32: 0 expense, 1 income
constexpr std::size_t kSize = 0x68;
} // namespace line

// cISC4DepartmentBudget and cISC4LineItem slots used between the pieces (byte
// offsets).
namespace dslot {
constexpr std::size_t kGetFunding = 0x28;
constexpr std::size_t kSetFunding = 0x2C;
constexpr std::size_t kGetMaxFunding = 0x30;
constexpr std::size_t kIdealFunding = 0x38;
constexpr std::size_t kCreateLineItem = 0x44;
constexpr std::size_t kSetLocalFullFunding = 0x64;
constexpr std::size_t kDeletingDestructor = 0x88;
} // namespace dslot

namespace lslot {
constexpr std::size_t kGetId = 0xC;
constexpr std::size_t kGetType = 0x10;
constexpr std::size_t kSetName = 0x1C;
constexpr std::size_t kGetFull = 0x28;
constexpr std::size_t kGetCurrent = 0x2C;
constexpr std::size_t kSetFull = 0x30;
constexpr std::size_t kAddToFull = 0x34;
constexpr std::size_t kGetIncome = 0x38;
constexpr std::size_t kIsLocal = 0x4C;
constexpr std::size_t kDeletingDestructor = 0x50;
} // namespace lslot

// The budget simulator's slots the departments call.
namespace bslot {
constexpr std::size_t kNeededFundingChanged = 0x80;
constexpr std::size_t kFundingChanged = 0x84;
constexpr std::size_t kGetBudgetItemInfo = 0xC8;
constexpr std::size_t kGetBudgetItemForPurpose = 0xCC;
constexpr std::size_t kChangeBudgetItemCost = 0xD4;
constexpr std::size_t kChangeBudgetItemLocalFunding = 0xD8;
} // namespace bslot

// The department's internal helpers, through their game addresses (ours when
// simulators.budget.department_helpers is on).
void AnnounceNeededFunding(void* department);

} // namespace opensc4::budget_sim
