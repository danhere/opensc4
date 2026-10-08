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
| `simulators.power.helpers` | `PlantOutput` (0x006EB1C0): a plant's capacity times its efficiency; the change-area helpers `CellRectAround` (0x006EAD70), `GrowRect` (0x006EAE20), `AddDirtyRect` (0x006EB230); `UpdateDemandTotal` (0x006EB490); `UpdatePlantUsage` (0x006EBC10); the wired-bit leaves `FillWiredRect` (0x006EAFD0), `ClearWiredRect` (0x006EB020), `OccupantCell` (0x006EB280), `OnNetworkCellChanged` (0x006EB350), `CreateBitArrays` (0x006EAED0); `PublishChanges` (0x006EBD00): the changed cells into the supply grid and a message per 8x8 block; `FindConnectedOccupant` (0x006EDB40); `AgePlant` (0x006EC8F0): a plant's wear and its end (demolished, radiation); `RecomputeArea` (0x006ED6E0): the wired bits of a changed rectangle; `DistributePower` (0x006EE110): the network flooded from a plant's cell, the plants reached, the consumers served, 8x8 blocks powered while the supply lasts | parity below |
| `simulators.power.month` | `OnMonthPassed` (0x006EC570), `ApplyFunding` (0x006EC3F0) | parity below |
| `simulators.power.step` | the simulation step (0x006EE960), see "The step" | parity below |
| `simulators.power.buildings` | `OnOccupantInserted` (0x006ECC50) and `OnOccupantRemoved` (0x006EBF60), messages 0x99EF1142/3 | parity below |
| `simulators.power.network` | `OnNetworkChanged` (0x006EDDA0), message 0x278E6003 | parity below |
| `simulators.power.tuning` | `LoadTuning` (0x006ED1C0), message 0xCA1B151C | parity below |
| `simulators.power.messages` | `DoMessage` (+0x4 slot 3, 0x006EFB70): each message to its handler by address; CityInited, the cell-rectangle message, the plant disaster, 0xE8BDA2E7 and 0xEA418D8F handled in it | parity below |
| `simulators.power.init`, `.shutdown` | `Init` (slot 3, 0x006F03B0), `Shutdown` (slot 4, 0x006F0110) | parity below |
| `simulators.power.save`, `.load` | `Write` (0x006EB6A0), `Read` (0x006F0880), the cIGZSerializable part at +0x8 | parity below |
| `simulators.power.object` | our own object under the class ID: factory, constructor (0x006EF7B0), destructor (0x006EFAB0), QueryInterface, AddRef, Release, our copies of the three vtables | parity below |
| | 39/41/42 (the shared "return 0", left as is), GetGZCLSID | the game's |

Notes from the getters:

- `IsPowered` and `IsWired` read per-cell bit arrays (+0x2D4, +0x2C8) through 0x00603DD0: the word is (width × z + x) / 32 but the bit is x mod 32, not the index's.
- Slots 45 and 46 return an `SC4Percentage` by value, so they take a hidden output pointer first: `(this, out, occupant)`, returning `out`.
- Efficiency (45): the curve at +0x378 at the plant's local funding × 100, × 0.01, times its condition (46: the curve in property 0x8A1E07EE at the age, property 0xC910B4CF). The default for both, and the chance of a strike, is the percentage at 0x00B4A5B0 (0).
- `FindPowerPlants` (30) returns the plant with the lowest local funding (below 2.0, with a line item in the department) and the one furthest through its life (age over the larger of properties 0xEA1CF221/0xEA1CF222, default 99), each with a reference; the game calls it a few times a run (the advisors).

### The month

`OnMonthPassed` (0x006EC570): +0x2AC from the service at 0x00B43D90 (slot 25, 0x0911E117); the month (+0x38) advances (mod 12) and the usage of the month a year ago (+0x90, [month][power type]) leaves the annual totals (+0x270) and is cleared (the game then adds the cleared figures back, a no-op kept as it is); the shortage ratio +0x64 = (production - need) / production as an integer division when need > production > 0, else 0; the dirty flags +0x338, +0x339; then `ApplyFunding` (0x006EC3F0) with the funding: the share at +0x2C0 moves by the curve at +0x3B8 (funding × 100) and is clamped to 0..1, and the objects of the vector at +0x29C (a copy shuffled by `std::random_shuffle` over the CRT's `rand()`, 0x006DB010) are switched (their slot 12) until the count at +0x2BC is that share of +0x2B8.

### The step

The simulation step (0x006EE960, message 0x66956814 with 5, 0x6695681A with an amount) does nothing until the network is awake (+0x14); a pending change rectangle (+0x33A) is recomputed first. Then one state per call (+0x34C, jump table 0x006EF79C):

- 0: the month advances (+0x38) and that month's usage a year ago leaves the annual totals; consumers of types 0x4773FFE0 and 0x4773FFE1 are told (slot 21, 0) and type-4 lots switched off (slot 12, 0); this month's figures (+0x3C to +0x4C, +0x6C, +0x70), the plants' states and the powered-cells bits being built (+0x2D0) are cleared.
- 1: power from each plant not reached yet (`DistributePower` from its cell); each plant's usage (used × rate 0xC910B4CE, truncated) is totalled from +0x78 into the department's line (slot 20, then 12); its cost (0x2A58D350 × the curve 0x0911E2E7 at its age) updates its budget item when it changed; after a month's end (+0x339) the plants age (`AgePlant`).
- 2: consumers still short of power get their unserved lots switched on and powered from their cell; the bits being built become the powered bits (+0x2D0 and +0x2D4 swap), the changes are published, and message 0x6A07C2EE goes out if any block changed.
- 3: the month's figures to the monthly figures service (0xAA11F7CE ...; the percentage powered 0xAA11FB59 = (1 − unpowered / consumers) × 100), usage by type (0xAA120D5E ...), the shortage ratio +0x64 (an integer division, so 0 or the default), the unpowered count, and each type-4 lot's powered state (slot 14); then message 0xEA418D8F.
- 4: waits for a change (+0x338), then starts over at 0.

### Our object

`simulators.power.object` registers our factory under class ID 0x0990C005 (as `simulators.pollution.object` does for pollution, [pollution.md](pollution.md)): operator new(0x3C8) and our constructor, which sets exactly the fields the game's (0x006EF7B0) sets, through the game's allocators in the same order (the plant-list sentinel, the object at +0x318 through 0x00913A5C, an empty string at +0x350, five empty curves); our destructor frees what the game's (0x006EFAB0) frees; QueryInterface (0xE9E7BFF5 and 1 at +0, cIGZMessageTarget2 0x452294AA at +0x4, cIGZSerializable 0xE4FDA3D4 at +0x8), AddRef and Release (the count stays at 1 when the last reference goes: Shutdown, then the deleting destructor, slot 52). The three vtables are our copies of the game's, taken when the first object is made, after the other replacements have patched them; with all of `simulators.power.*` on, every power function the game runs is ours except GetGZCLSID and the shared "return 0" slots.

### Save record

Write and Read (the cIGZSerializable part at +0x8): version 6, in this order: the department (reference), +0x38, +0x3C, +0x40, +0x64 (float; a whole percentage below version 6), +0x68 to +0x74, +0x78 (int64), +0x50 to +0x60, the supply grid (reference), the consumption map (rows, columns, int16 values), the usage history (12 × 10) and annual totals (10), the plant count and counts by type, the plant list (state, reference each), the network-piece count, +0x2BC and the pieces, the bit-array word count and the four bit arrays (+0x2C4, +0x2C8, +0x2D4, +0x2D0), flags +0x2D8, +0x330 (cleared after reading), +0x334, +0x338, +0x339, and the state. Read sets +0xD, so Init does not make the bit arrays again. `tools/re/sc4save.py` decodes it (all 2,040 power records in the test region and the parity corpora read to the last byte), and the parity harness compares it field by field.

### What the test cities reach

The simulation step (0x006EE960) is a state machine (+0x34C). Its network work, the full recompute (0x006EE110), the change-area recompute (0x006ED6E0), the network-change handler (0x006EDDA0) and their helpers (0x006EDB40, 0x006EC8F0, 0x006EBD00, `CellRectAround`, `UpdatePlantUsage`), ran neither in three years of Making Money nor in 30 days of Big City d0090 or Rush Hour d0365 (`diagnostics.coverage`): the step does nothing until message 0x6AC284F3 sets +0x14, and that message comes from the network tools (0x004AA6D0 and others): the power network is recomputed only after the player edits it, so in an unedited city power's figures stay as they were loaded, for a year. `OPENSC4_TEST_POWER_WAKE=<n>` sets +0x14 on the n-th day of a run (from the new-day agent, so at the same moment in both runs), as the message would: the step then runs its whole cycle on the city's real network. Two runs of the original agree with it (Making Money: powered buildings 327 to 336, demand 1140 to 1228).

## Parity

The fingerprint's `power` part (2026-10-08): a checksum of the powered grid (slot 35), the monthly requirement, production, import, export, demand and consumption (slots 11-15 and 17; 16 is the per-cell overload), unpowered and powered consumers (21, 22), power plants (24), poles (26) and overworked, aged and distressed plants (27-29).

Control (the original against itself) with the power part: 12 of 12, 3 of 3 on `saves-plopped` for a week, 2 of 2 on `saves-mm2` for a year.

The getters (`simulators.power.interface` with `.helpers`): 12 of 12 and 3 of 3 for a week, 2 of 2 for a year. `FindPowerPlants` is also checked call by call with `OPENSC4_POWER_SELFCHECK=1` (`OPENSC4_TEST_FIND_POWER_PLANTS=1` adds a call after the load): the same plants and figures as the game's on every call (the test cities have one plant each).

The helpers and the month (`simulators.power.helpers`, `.month` with the interface): 12 of 12 and 3 of 3 for a week, 2 of 2 for a year (one run hit the rare load crash at 0x0096DA20, [parity.md](parity.md), and passed when run again). First calls in those runs: `UpdateDemandTotal` in nearly every run, `OnMonthPassed` and `ApplyFunding` in every run that reached a month's end, `AddDirtyRect` and `GrowRect` in the two year runs; `CellRectAround` and `UpdatePlantUsage` in none (see above).

With the wake-up (`OPENSC4_TEST_POWER_WAKE=1`, the interface, helpers and month ours): 12 of 12 and 3 of 3 for a week, 2 of 2 for 90 days; `UpdatePlantUsage` now ran in 15 runs and `CellRectAround` in 2.

The network helpers (`.helpers` complete, with the interface and month, `OPENSC4_TEST_POWER_WAKE=1`): 12 of 12 and 3 of 3 for a week, 2 of 2 for 90 days. `DistributePower`, `PublishChanges` and `AgePlant` ran in 15 runs, `RecomputeArea` in 2; `CreateBitArrays`, `OccupantCell`, `OnNetworkCellChanged` and `FindConnectedOccupant` in none (they need a building added or a network edit), so their parity is not shown yet.

The step (`simulators.power.step` with the interface, helpers and month, wake-up on day 1): 12 of 12, 3 of 3 for a week, 2 of 2 for 90 days; `Simulate` ran in 22 runs. (Two Rush Hour saves first showed as different: the power record comparison was added while that batch ran, and its first version compared the plant references' heap addresses. Fixed and judged again on the same saves: one outcome over all six runs of each.)

Everything (`simulators.power.*` but `.object`, wake-up on day 1): 12 of 12, 3 of 3 for a week, 2 of 2 for 90 days, save records identical. With `.object` as well: 12 of 12, 3 of 3, 2 of 2; our object made in all 17 runs of our side. First calls in those runs: Init, LoadTuning, Read, Write, Shutdown and `Simulate` in every run, `OnOccupantInserted` in 24 and `OnOccupantRemoved` in 28 of 34.

With the network messages as well (`OPENSC4_TEST_POWER_MESSAGES=2`: on the second day the autotest hands the power simulator's DoMessage a network change, 0x278E6003, and a changed cell rectangle, 0x07699455, both over the whole city, as a network edit would; everything and `.object` on, wake-up on day 1): 12 of 12, 3 of 3 for a week, 2 of 2 for 90 days. `OnNetworkChanged`, the cell-rectangle case, `OnNetworkCellChanged`, `RecomputeArea` and `CellRectAround` ran in all 17 runs of our side, CityInited and 0xEA418D8F in all, 0xE8BDA2E7 in 7. Never reached by any test run: the plant disaster case (0xC2C09554 with 0xE0D0722E), `CreateBitArrays` (new cities only: a loaded city's bit arrays come from its save), `OccupantCell` and `FindConnectedOccupant`.
