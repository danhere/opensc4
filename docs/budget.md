# The budget simulator (cSC4BudgetSimulator)

Reimplemented after the ordinances ([ordinance.md](ordinance.md)), the same way: function by function on the original object under the parity harness ([parity.md](parity.md)), then our own object under the class ID. Build 641 addresses throughout ([data/addresses/budget.toml](../data/addresses/budget.toml)). Code: [budget_sim.cpp](../src/core/budget_sim.cpp) (funds, totals, taxes, loans), [budget_month.cpp](../src/core/budget_month.cpp) (the month, tax collection, messages and cheats), [budget_departments.cpp](../src/core/budget_departments.cpp) (departments, groups, bonds, loans), [budget_items.cpp](../src/core/budget_items.cpp) (budget item properties), [budget_init.cpp](../src/core/budget_init.cpp), [budget_save.cpp](../src/core/budget_save.cpp), [budget_object.cpp](../src/core/budget_object.cpp).

The department budgets (`cSC4DepartmentBudget`, class 0xE990BFFC) and their line items (`cSC4LineItem`, 0xAA313C9F) are separate classes the simulator owns; they are still the game's (a later step).

## The class

Class ID 0xE990BE01, interface 0x6772BD23. Object size 0x854. Factory 0x00694970 (`SC4::operator_new(0x854)`, constructor 0x00694650). Code from 0x0068FC80 to 0x00695260, about 110 functions with the container helpers.

| Vtable | Interface | Slots |
| --- | --- | --- |
| 0x00AB0908 | `cISC4BudgetSimulator` (primary) | 59 in gzcom-dll's order, the scalar deleting destructor (59), the class ID (60) |
| 0x00AB08F8 | `cIGZMessageTarget2` | DoMessage 0x00693450 |
| 0x00AB08E0 | `cIGZSerializable` | Write 0x00690530, Read 0x00694B00, GetGZCLSID 0x00694240 |

### Fields

| Offset | What |
| --- | --- |
| +0xC, +0x10, +0x14 | initialised, references, loaded (Read ran) |
| +0x18, +0x1C, +0x20, +0x24 | references: the city (slot 24 is the difficulty level), the message server, the land value service (0x00B43D88), the service at 0x00B43D90 (slots 22 and 23: a monthly expense and income) |
| +0x28, +0x30 | int64: the funds, and a copy the month restores them from |
| +0x38 | int64: the bonds' principal outstanding |
| +0x40, +0x48 | int64: this year's expense and income so far |
| +0x50 | int32: fundraiser cheats since the last riot |
| +0x54, +0x58 | the departments: map by ID to `cSC4DepartmentBudget*` |
| +0x60, +0x64 | the department groups: map by ID to a record (ID, name, expense, income, the set of department IDs) |
| +0x70 + 0x80 n | the 12 tax groups: rate (float), +0x74 (float), 12 int64 (+0x78), +0xD8, the assessed value (+0xE0), last month's tax (+0xE8) |
| +0x670 | int64: last month's tax income |
| +0x678 | group 8's rate is set from the level at Init |
| +0x680, +0x688 | int64: this year's +0x24 expense and income; by month at +0x690 and +0x6F0 (12 each) |
| +0x750 | int64: this year's taxes |
| +0x780, +0x784, +0x788 | the bonds (a list of 0x38-byte records: month and year due, principal, full cost, owed, paid, monthly payment, balance), their count, this year's payments |
| +0x790 | int64 vector: starting funds by level |
| +0x79C | float: a new group's +0x74 |
| +0x7A0, +0x7A8, +0x7B0, +0x7B8 | minimum funds (-100,000: below it the game ends), bond increment, bonds allowed, bond limit |
| +0x7C0, +0x7C4 | yearly interest rate, loan months |
| +0x7C8 | float[12]: tax group factors |
| +0x7F8, +0x814, +0x820 | float vectors: wealth scales, the tax scale by level, starting tax rates by level |
| +0x804 | `SC4Curve`: a lot's land value to its added assessed value |
| +0x830 to +0x848 | int64: total monthly and yearly income and expense |
| +0x850 | the small budget window |

## What it does

- **MonthPassed** (0x00692510, message 0x66956816): the funds restored from +0x30; the month's taxes (`CollectTaxes`); income: taxes, +0x24's income, the month end service's (0x00B43D8C slot 32), each department's (slot 16); expense: +0x24's, the month end service's (slot 33), each department's (recalculated, slot 15), each bond's payment. A bond due this month is repaid (message 0xCA3FBDCE), the others accrue interest. The funds change by income minus expense; the totals and the charts (0x6A51178B, 0xCA5BA3FB, 0x4A5BA3D0) are updated; after December the year's figures are cleared. Funds at or below the minimum send 0xAA5CF1E3 (the game ends for the budget).
- **CollectTaxes** (0x00691E20): per lot with a building that pays (lot slot 37): its tax group from the zone and the building's wealth (the table at 0x00AB0840); the building's value (property 0xE91A0B5F) plus the land value curve at the lot's land value times its size (rounded through the float trick 1.5 x 2^23); times the wealth scale and the group factor (truncated: the assessed value); times the level's tax scale, the rate and 1/1200 for the tax.
- **DoMessage** (0x00693450): 0x66956810 announces the funds, 0x66956814 publishes the tax rates to the charts, 0x66956816 the month, 0x26D31EC4 sets the starting funds and rates by the level, 0x29222F02 opens the small or large budget window, 0x230E27AC the cheat codes: 0xCBF31943 deposits 10,000 and may start a riot (disaster 0x42963812) the more often it is used, 0x4AB87766 adds 1,000, 0x493F4FE6 sets the funds.
- **IssueBond** (slot 40): in whole increments if the bond count, the bond limit and the borrowing limit allow; the payment is the annuity `amount x r / (1 - (1 + r)^-n)` with the game's own `pow` (0x009EFC60, called directly so results match to the bit).
- **Departments and groups** (slots 25-33): create (a `cSC4DepartmentBudget` of 0x68 bytes in a group), remove, find by ID or by a name prefix (lower-cased), each group's totals recounted (0x0A3E7624 on a change).
- **Budget items** (slots 50-58): an occupant's property arrays 0xEA54D283 to 0xEA54D287 (department, line, a third ID, cost, local funding in percent), read into 0x20-byte items, changed, or applied to the department lines.
- **Init** (0x00692FF0): the tuning (0x00692210, exemplar 6534284A-E7E2C2DB-0BFF6824), registration with the simulator service (phases 0, 6, 4), four messages, the three cheat codes, for a new city the starting funds and rates, the charts, the script table `budget` with `total_funds` and a deposit function.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.budget.interface` | 35 slots: funds, totals, taxes, rates, loan figures |
| `simulators.budget.month` | `MonthPassed`, `CollectTaxes`, the tax rate figures, the starting funds and rates |
| `simulators.budget.messages` | `DoMessage` with the cheat codes |
| `simulators.budget.departments` | slots 25-33, 40, 46 and the group record and recount helpers |
| `simulators.budget.items` | slots 50-56, 58 |
| `simulators.budget.init`, `.shutdown` | `Init` with the tuning load; `Shutdown` |
| `simulators.budget.save`, `.load` | `Write` (version 5), `Read` (versions 4 and 5) |
| `simulators.budget.object` | our own object under 0xE990BE01: constructor, destructor, identity, our copies of the vtables |

Still the game's: the container helpers (maps, sets, vectors, lists), the property helpers, the script callbacks (0x0068FFB0, 0x00690030), the department budgets and line items.

The fingerprint's `budget` part: the funds and their copy, the bonds owed, this year's expense and income, the tax income, the stored totals, this month's total expense and income (slots 14, 16), a checksum of the tax groups' rates and assessed values, the bonds (count, payments) and the departments (count, a checksum of their IDs, expenses and income). `OPENSC4_TEST_BUDGET_BOND=<n>` issues a bond of 25,000 on the n-th day and reads back the loans, groups and each group's departments (the test cities issue no bonds). `tools/re/save_schemas/budget.py` decodes the record.

## Parity

Everything ours (all ten `simulators.budget.*` switches, our object included) against the original, 2026-10-09, 2 parallel jobs in their own user folders, a bond issued on day 3 (`OPENSC4_TEST_BUDGET_BOND=3`) on both sides; result folders under `%LOCALAPPDATA%\opensc4\parity\`:

| Run | Result |
| --- | --- |
| 12 saves, 35 days (`bud-all35`) | 12 of 12 identical (Big City d0000 after 3 pairs, its outcomes all shared with the original's) |
| `saves-mm2`, 365 days (`bud-mm365`) | d0365 identical for all 366 days. d0090: one of the two B runs parted from the original on day 69 in the pollution grids (the budget part identical until then), the other matched; two more pairs (`bud-mm365-r1`, `-r2`) were identical for all 366 days, so ours matched in 3 of 4 runs |
| `saves-plopped`, 35 days (`bud-pl35`) | 3 of 3 identical |

Every function was reached (first-call log lines) but `CreateDepartmentBudget`, `RemoveDepartmentBudget`, the department lookup by name, `GroupRecord` (a new group), the budget item changes (slots 51-58), the cheats and the year's end beyond the runs' dates, and a bond falling due (120 months).
