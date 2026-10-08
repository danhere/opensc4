# The power simulator (cSC4PowerSimulator)

The second simulator to be reimplemented, after pollution ([pollution.md](pollution.md)), the same way: function by function on the original object under the parity harness ([parity.md](parity.md)), then our own object under the class ID. Build 641 addresses throughout.

## The class

Class ID 0x0990C005. Object size 0x3C8. Factory 0x006F00D0 (`SC4::operator_new(0x3C8)`, then the constructor 0x006EF7B0). Code from 0x006EAA60 to about 0x006F0D50: 74 functions, about 25 KB, plus shared one-line getters elsewhere. Three interfaces:

| Vtable | Interface | Slots |
| --- | --- | --- |
| 0x00AB28B8 | `cISC4PowerSimulator` (primary) | 52: QueryInterface 0x006EABC0, AddRef 0x004F7BA0 (shared), Release 0x006EB190, Init 0x006F03B0, Shutdown 0x006F0110, then the getters; several slots are shared one-line getters (0x00407AC0, 0x0081F910, ...) or the shared "return 0" 0x00648EF0 (the strike slots) |
| 0x00AB288C | `cIGZSerializable` | Write 0x006EB6A0, Read 0x006F0880, GetGZCLSID 0x006EABB0 |
| 0x00AB28A4 | `cIGZMessageTarget2` | DoMessage 0x006EFB70 |

gzcom-dll's `cISC4PowerSimulator.h` declares overloads (`GetMonthlyConsumption`, `GetPowerPlantCount`, `GetPoweredGrid`) that MSVC compiles in reverse order, so code here calls the slots by number.

### Messages

| Message | Handler | What it is |
| --- | --- | --- |
| 0x66956814 | 0x006EE960 (3.6 KB) with 5 | the simulation's step |
| 0x6695681A | 0x006EE960 with the message's value | the same, a given amount |
| 0x66956816 MonthPassed | 0x006EC570 | the month's figures |
| 0x26D31EC1 CityInited | inline, then 0x006EB490 | drops dead entries from a list (+0x298), then a check that may set +0x334 |
| 0x278E6003 | 0x006EDDA0 | a network change in a cell rectangle (the message's two packed corners, clamped to the city) |
| 0x99EF1142 / 0x99EF1143 | 0x006ECC50 / 0x006EBF60 | a building added / removed |
| 0xCA1B151C | 0x006ED1C0 | the tuning |
| 0x07699455, 0xC2C09554, 0xE8BDA2E7, 0xEA418D8F, 0x6AC284F3 | inline | interface and disaster cases (an overlay redraw; the plants of type 5 at a disaster; a lot's flag; a funding change sets +0x348 = 4 and +0x334) |

## What is ours

| Replacement | What is ours | State |
| --- | --- | --- |
| `simulators.power.interface` | 44 of the 52 `cISC4PowerSimulator` slots: the getters (fields, the bit arrays, the consumption map), the building queries (`IsOccupantPowered`, `GetOccupantPowerConsumed`/`Produced`/`MaxPowerProduced`), the plant list and `FindPowerPlants`, the plants' efficiency and condition, the budget-item slots ([src/core/power_sim.cpp](../src/core/power_sim.cpp)) | parity below |
| `simulators.power.helpers` | `PlantOutput` (0x006EB1C0): a plant's capacity times its efficiency; the change-area helpers `CellRectAround` (0x006EAD70), `GrowRect` (0x006EAE20), `AddDirtyRect` (0x006EB230); `UpdateDemandTotal` (0x006EB490); `UpdatePlantUsage` (0x006EBC10) | parity below; `CellRectAround` and `UpdatePlantUsage` unreached |
| `simulators.power.month` | `OnMonthPassed` (0x006EC570), `ApplyFunding` (0x006EC3F0) | parity below |
| | slots 0-4 (identity, Init, Shutdown), 39/41/42 (the shared "return 0", left as is), the handlers, Read, Write | not started |

Notes from the getters:

- `IsPowered` and `IsWired` read per-cell bit arrays (+0x2D4, +0x2C8) through 0x00603DD0: the word is (width × z + x) / 32 but the bit is x mod 32, not the index's.
- Slots 45 and 46 return an `SC4Percentage` by value, so they take a hidden output pointer first: `(this, out, occupant)`, returning `out`.
- Efficiency (45): the curve at +0x378 at the plant's local funding × 100, × 0.01, times its condition (46: the curve in property 0x8A1E07EE at the age, property 0xC910B4CF). The default for both, and the chance of a strike, is the percentage at 0x00B4A5B0 (0).
- `FindPowerPlants` (30) returns the plant with the lowest local funding (below 2.0, with a line item in the department) and the one furthest through its life (age over the larger of properties 0xEA1CF221/0xEA1CF222, default 99), each with a reference; the game calls it a few times a run (the advisors).

### The month

`OnMonthPassed` (0x006EC570): +0x2AC from the service at 0x00B43D90 (slot 25, 0x0911E117); the month (+0x38) advances (mod 12) and the usage of the month a year ago (+0x90, [month][power type]) leaves the annual totals (+0x270) and is cleared (the game then adds the cleared figures back, a no-op kept as it is); the shortage ratio +0x64 = (production - need) / production as an integer division when need > production > 0, else 0; the dirty flags +0x338, +0x339; then `ApplyFunding` (0x006EC3F0) with the funding: the share at +0x2C0 moves by the curve at +0x3B8 (funding × 100) and is clamped to 0..1, and the objects of the vector at +0x29C (a copy shuffled by `std::random_shuffle` over the CRT's `rand()`, 0x006DB010) are switched (their slot 12) until the count at +0x2BC is that share of +0x2B8.

### What the test cities reach

The simulation step (0x006EE960) is a state machine (+0x34C). Its network work, the full recompute (0x006EE110), the change-area recompute (0x006ED6E0), the network-change handler (0x006EDDA0) and their helpers (0x006EDB40, 0x006EC8F0, 0x006EBD00, `CellRectAround`, `UpdatePlantUsage`), ran neither in three years of Making Money nor in 30 days of Big City d0090 or Rush Hour d0365 (`diagnostics.coverage`): it runs when the power network changes (lines, plants, the buildings on it). Checking it needs saves where the network changes during the run.

## Parity

The fingerprint's `power` part (2026-10-08): a checksum of the powered grid (slot 35), the monthly requirement, production, import, export, demand and consumption (slots 11-15 and 17; 16 is the per-cell overload), unpowered and powered consumers (21, 22), power plants (24), poles (26) and overworked, aged and distressed plants (27-29).

Control (the original against itself) with the power part: 12 of 12, 3 of 3 on `saves-plopped` for a week, 2 of 2 on `saves-mm2` for a year.

The getters (`simulators.power.interface` with `.helpers`): 12 of 12 and 3 of 3 for a week, 2 of 2 for a year. `FindPowerPlants` is also checked call by call with `OPENSC4_POWER_SELFCHECK=1` (`OPENSC4_TEST_FIND_POWER_PLANTS=1` adds a call after the load): the same plants and figures as the game's on every call (the test cities have one plant each).

The helpers and the month (`simulators.power.helpers`, `.month` with the interface): 12 of 12 and 3 of 3 for a week, 2 of 2 for a year (one run hit the rare load crash at 0x0096DA20, [parity.md](parity.md), and passed when run again). First calls in those runs: `UpdateDemandTotal` in nearly every run, `OnMonthPassed` and `ApplyFunding` in every run that reached a month's end, `AddDirtyRect` and `GrowRect` in the two year runs; `CellRectAround` and `UpdatePlantUsage` in none (see above).

