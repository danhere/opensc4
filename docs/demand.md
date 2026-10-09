# The demand simulator (cSC4DemandSimulator)

The RCI demand: reimplemented after pollution, power, water, traffic, land value, aura and crime, the same way: function by function on the original object under the parity harness ([parity.md](parity.md)), then our own object under the class ID. Build 641 addresses throughout. Code: [src/core/demand_sim.h](../src/core/demand_sim.h), [demand_demand.cpp](../src/core/demand_demand.cpp) (the demand objects), [demand_sim.cpp](../src/core/demand_sim.cpp) (the interface and the save record), [demand_step.cpp](../src/core/demand_step.cpp) (the step, the passes, the messages), [demand_region.cpp](../src/core/demand_region.cpp) (the sources), [demand_month.cpp](../src/core/demand_month.cpp) (the month), [demand_init.cpp](../src/core/demand_init.cpp) (the tuning, Init, Shutdown), [demand_object.cpp](../src/core/demand_object.cpp) (our object).

**Nothing about demand is fixed in the code.** Every demand, its caps (the active demand's range, the cap a demand puts on another) and the links between demands come from the game's demand exemplars, and the simulator's own numbers from its tuning exemplar; ours reads them the same way. So mods that change them, the Colossus Addon Mod (CAM) with its extra growth stages among them, keep working.

## The class

Class ID 0x2990BFF2, interface 0x47967E16. Object size 0xDC. Factory 0x006A6F30 (`SC4::operator_new(0xDC)` and the constructor inline). Code from 0x006A2300 to 0x006A96B0: about 90 functions, 29 KB, many of them the STL containers' helpers. Three interfaces:

| Vtable | Interface | Slots |
| --- | --- | --- |
| 0x00AB0E48 | `cISC4DemandSimulator` (primary) | QueryInterface 0x006A2310, AddRef 0x004F7BA0 (shared), Release 0x006BD8A0 (shared), Init 0x006A8C20, Shutdown 0x006A3A20, GetSimulatorType 0x006A2390, GetDemand 0x006A2960, UpdateOccupantEffects 0x006A6C90, CalculateJobsPerUnitOfDemand 0x006A23A0, GetJobsBySensus 0x006A3E60, GetNeutralTaxRate 0x006A3DD0, GetLocalPopulationSummary 0x006A5F80, the scalar deleting destructor (12), 13 (0x006A52D0: adds a demand object, replacing one with its ID) |
| 0x00AB0E38 | `cIGZMessageTarget2` | DoMessage 0x006A9470 |
| 0x00AB0E20 | `cIGZSerializable` | Write 0x006A2680, Read 0x006A3C50, GetGZCLSID 0x006A2300 |

gzcom-dll has `cISC4DemandSimulator.h` and `cISC4Demand.h`, in the game's slot order.

### Fields

| Offset | What |
| --- | --- |
| +0xC, +0xD | loaded from a save (set by Read), initialised |
| +0x10 | reference count |
| +0x14 | the step's state (0 to 3) |
| +0x18 | the cell walk of state 1 (0x20 bytes: state byte, x, z, step, the rectangle) |
| +0x1C | `map<uint32, float>`: the values the cell walk counts (24 IDs, below) |
| +0x28 | `map<uint32, float>`: the same values from the last finished walk (saved) |
| +0x34 to +0x4C | references: the city, the message server, services 0x00B43D68 (lot types and zones), 0x00B43D08 (lots), 0x00B43D8C (civic buildings, neighbours), 0x00B43D90 (ordinances), 0x00B43DA4 (people grids) |
| +0x50 | `map<uint32, cISC4Demand*>`: the demands by ID (173 in the tutorial cities) |
| +0x5C | `multimap<uint32, effect>`: the effects of one demand on others, from the demand exemplars |
| +0x68 | 12 weights (tuning 0x8A63F2A1, 100 each by default) |
| +0x98, +0x9C | lists: the neighbouring cities, the neighbour connections |
| +0xA0 to +0xAC | four floats, saved: economy additions for offices (+0xA0) and industry (+0xA4), the civic (+0xA8) and connection (+0xAC) demand of the last month |
| +0xB0, +0xB4 | curves (tuning 0x4A1F38B5, 0x4A1F38B6): the neutral tax rate from the population, the tax effect |
| +0xB8 | the tax history in days (tuning 0x4A1F38B7, 90) |
| +0xBC | a vector of 13 floats (tuning 0x4A1F38C5): the weights of the civic building counts |
| +0xC8 to +0xD8 | tuning 0x4A1F38C6 to 0x4A1F38CA (1, 2, 20, 0.1, 20) |

The demand IDs follow a pattern: the low digits are a group (0x1010 R$, 0x1020 R$$, 0x1030 R$$$; 0x3110, 0x3120, 0x3130 CS$ to CS$$$; 0x3320, 0x3330 CO$$, CO$$$; 0x4100 agriculture, 0x4200 dirty, 0x4300 manufacturing, 0x4400 high tech), +1 its population or jobs, 0x10000 and 0x20000 the regional and the city's own figures (0x21010 is the R$ demand the RCI bars show).

## cSC4Demand

Class ID 0x8990C12D, interface 0x0797A4E8, 0x54 bytes, factory 0x005D52E0. One per demand. Two vtables: 0x00AA2758 (`cISC4Demand`, 36 slots) and 0x00AA273C (`cIGZSerializable` at +4).

| Offset | What |
| --- | --- |
| +0x8, +0x9 | loaded, initialised |
| +0xC | reference count |
| +0x10 | ID |
| +0x14, +0x18 | supply, demand |
| +0x1C, +0x20 | this cycle's additions to supply and demand (`AddToSupplyValue`, `AddToDemandValue`) |
| +0x24, +0x28 | saved, nothing else touches them |
| +0x2C, +0x30 | the active demand's maximum and minimum (from the exemplar; ±8.5e37 by default) |
| +0x34 | the active demand |
| +0x38 | a byte, saved, 1 by default |
| +0x3C, +0x40 | the economy and tax modifiers |
| +0x44 | the cap (a fraction) |
| +0x48 | the region use: 1 supply, 2 demand (exemplar property 0x47BB3F60) |
| +0x4C, +0x50 | references: the city, the message server |

The slots are gzcom-dll's; several are one-line functions the exe shares with other classes. The two that compute:

- `EndOfCycle` (slot 30, 0x005D4CF0): the cycle's additions into supply and demand, then slot 35 without smoothing; whether anything was added.
- slot 35 (0x005D4E60, `Update(bool smooth)`): the target is demand less supply within [min, max], plus the smaller of (economy + tax) × max and (economy + tax) × demand, within [min, max] again and at most max × (1 − cap). Without smoothing the active demand becomes the target. Smoothed, it moves toward it by at most 7% of the range, snapping to 0 when a step would cross from one side of 0 to the other while the target is on the far side. Then message 0x426840A0 (kMsgTypeDemandChanged) with the ID and the active demand in thousandths of its range (positive: of max, negative: of min), rounded to nearest.

`SimulationBegin` (slot 5) computes the active demand first unless loaded, then sends the same message with the value truncated.

## Messages

| Message | What happens |
| --- | --- |
| 0x6695681A | the step with the message's time budget |
| 0x66956814 | the step with 5, then `PublishActive` (0x006A7600): the passes and the twelve RCI groups' active demand as figures 0x2A67A399 ... 0x8A67A3CC |
| 0x66956816 MonthPassed | `MonthEffects`, `MonthJobs`, and a step waiting in state 3 back to 0 |
| 0x66956810 | every demand's SimulationBegin |
| 0x26C63343 | the passes, twice |
| 0xE8BDA2E7 | an occupant's state changed: when it becomes or stops being functional (states 2 and 3) its type's demand effects (`OccupantDemand`, the global map at 0x00B49008) and its jobs (`OccupantJobs`) are added or taken off, unless its exemplar has 0x27812844 |
| 0x8A0D15DA, 0xEA0D163E | the neighbours (`Neighbors` with false) |
| 0xCA1B151C | the tuning reloaded |

## The step

`Step` (0x006A7020) runs one state a call (+0x14):

- 0: `RegionDemand` (0x006A6070): for each of the twelve RCI groups, the tax rate over the last 90 days (figures 0xAABAAA9F ... 0xAABAAAAA, the history through figure slot 7) against the neutral rate (slot 10, at least 0.1) through curve +0xB4, plus the group's ordinance factor less 1, as its demand's tax modifier; the economy modifier from the service at 0x00B43C94 (slot 34's object, slot 16), plus +0xA0 for offices and +0xA4 for industry. The 24 counted values of +0x1C cleared; the cell walk restarted.
- 1: the cell walk, under the time budget (`SC4Deadline`, checked every 0x400 steps; adaptive step at 0x00B49024): `CountCell` (0x006A5A00) a cell: a functional, zoned lot's residents by wealth (+0x1001 + 16 × wealth) and weighted by the cell's value (+0x2110 + 16 × wealth + a level of 1 to 4), or its jobs (its key + 1). At the end each counted value that moved since the last walk goes to its demand's supply (region use 1) or demand (2) and is kept at +0x28.
- 2: the month's figures: population and jobs by wealth, the jobs of commerce and industry, the workers without jobs, and the average of twelve demands weighted by +0x68 (figure 0x8A63F30C).
- 3: waits for MonthPassed.

`Propagate` (0x006A4E70) applies each demand's effects on the others (the multimap at +0x5C, from the demand exemplars): new supply or demand times a factor added to another's supply or demand (0x47BB3F30-32), another's cap from this one's demand over supply (0x47BB3F41), or this one as the sum of others (0x47BB3F50); then each demand's EndOfCycle. Repeated until a pass adds nothing above 0.01.

`Neighbors` (0x006A5CB0) moves the change in the neighbouring cities' figures (city slot 17, every neighbour but this city) to the regional demands (ID + 0x10000).

## The month

- `MonthEffects` (0x006A3ED0): +0xAC from the capacity of some kinds of buildings (the service at +0x44, slot 16; kinds 1, 2, 3, 8, 10 and 12 through the service at 0x00B43DA8, slot 21, with 3) times +0xD0; +0xA8 from six counts of civic buildings by kind (unique IDs gathered in sets, plus list lengths) through ln, weighted by +0xBC, times 1/ln 2. Both plus two city counts (city slots 76 and 77) times tuning. The change goes to the supply of 0x4800 and 0x4900 (+0xAC) and 0x3800 (+0xA8). The logarithms are x87 `FYL2X`, whose result is extended precision before the multiply; ours runs the same instructions (inline assembly) to match it bit for bit.
- `MonthJobs` (0x006A8FF0): for each lot type not updated in the last 0x41 days with at least 10 places open, the neighbour connections in order of their offer (the game's own sort over a map of offers, so ties keep its order) each take up to 1,000 places while 10 are left; `ShareWorkers` (0x006A68E0) and `ShareJobs` (0x006A6AF0) carry the workers and jobs over to the demands.

## Tuning

Nothing about demand is fixed in code.

- The demands: Init hands the resource manager a callback (`ForEachExemplar`, 0x006A7840) for every exemplar; those of exemplar type 3 go to `ConfigureDemand` (0x006A5350): the demand with its ID (property 0x21, else the exemplar's instance) is made if new (`CreateDemand`, 0x006A5210, class 0x8990C12D); unless the city was loaded, its starting supply and demand (0x47BB3F20, 0x47BB3F21); its range (0x47BB3F10 minimum, 0x47BB3F11 maximum); its effects on other demands (0x47BB3F31, 30, 32 as (target, factor in hundredths) pairs; 0x47BB3F41 and 50 as targets), numbered by a global counter (0x00B49020); its region use (0x47BB3F60).
- The occupant effects (`LoadTuning` 0x006A8790, `OccupantTuning` 0x006A8420): for every lot or building type the service at +0x3C knows, pairs (demand, amount) from its exemplar: 0x27812841 (demand; amounts replaced by the floats of 0x27812843) and 0x27812840 (supply; 0x27812842), kept for demands that exist, into the global map at 0x00B49008.
- The simulator's tuning exemplar 6534284A-E7E2C2DB-4A1F3CE6: curves 0x4A1F38B5 (+0xB0) and 0x4A1F38B6 (+0xB4), 0x4A1F38B7 (+0xB8, 90), 0x4A1F38C5 (13 floats, +0xBC), 0x4A1F38C6 to CA (+0xC8 to +0xD8), 0x8A63F2A1 (12 weights, +0x68, 100 each by default).

Init (0x006A8C20) also makes the cell walk over the city, references its services, declares the twelve active-demand figures, registers for the messages and as a simulator ("cSC4DemandSimulator", kinds 0, 4, 6, 10), and runs the passes once. A city loaded from a save starts the step at 0, a new one at 3. Shutdown (0x006A3A20) undoes it; where it frees the curve at +0xB4 it clears +0xB0 (a slip in the game, kept).

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.demand.demands` | every slot of both cSC4Demand vtables (36 + 6), so every demand object runs our code |
| `simulators.demand.interface` | slots 5-11 of `cISC4DemandSimulator` |
| `simulators.demand.save`, `.load` | Write, Read (version 2) |
| `simulators.demand.step` | the step (0x006A7020) and `ClearCounts` (0x006A37A0) |
| `simulators.demand.passes` | `Propagate`, `PublishActive`, `BeginAll` (0x006A27E0), MonthPassed (0x006A9450) |
| `simulators.demand.messages` | DoMessage (+4 slot 3), `OccupantDemand` (0x006A5920), `OccupantJobs` (0x006A2C80) |
| `simulators.demand.sources` | `RegionDemand`, `CountCell`, `Neighbors` |
| `simulators.demand.month` | `MonthEffects`, `MonthJobs`, `ShareWorkers`, `ShareJobs` |
| `simulators.demand.tuning` | `ForEachExemplar`, `ConfigureDemand`, `CreateDemand`, `OccupantTuning`, `LoadTuning`, slot 13 (`AddDemand`) |
| `simulators.demand.init`, `.shutdown` | Init, Shutdown |
| `simulators.demand.object` | our own object under the class ID: constructor (0x006A35E0), destructor (0x006A3020), identity, copies of the three vtables |

Still the game's: the STL helpers (map inserts and erases, list and set helpers, the sort `MonthJobs` uses), the curve evaluation, and the global effect maps' registration (0x006A7CA0).

`UpdateOccupantEffects` (slot 7) calls the occupant demand helper (0x006A5920) through its address. `GetNeutralTaxRate` returns the curve's result unrounded (in ST0), as the game's does: its callers compute with it before storing it.

## Save records

`cSC4DemandSimulator`, version 2 (Write and Read, the `cIGZSerializable` part at +8): versions before 2 have four more words first, which Read skips. Then the four floats at +0xA0, the demands (count, then ID and a reference each) and the counted values of +0x28 (count, then ID and float each). Read sets +0xC and puts the references in the map as they come.

`cSC4Demand`, version 2: the ID, then nine floats (supply, demand, the two additions, +0x24, +0x28, min, max, active: the minimum before the maximum), the byte +0x38, the economy and tax modifiers and the cap (a float; version 1 has it as a percentage byte). All the city's demands are in one entry of the save, in an order the save framework picks (it differs from run to run), so `sc4save.py` compares them in ID order.

## Test cities

The fingerprint's `demand` part: the step's state, the four saved floats, each demand's fields under its key, the two maps of counted values (the walk's map only outside state 1: the walk is cut into time slices, so a day can end anywhere in it), the neutral tax rate (slot 10) and the jobs of every demand (slot 9).

Two autotest switches bring in what a week's run would miss: `OPENSC4_TEST_DEMAND_MONTH=<n>` calls MonthPassed every n days (the monthly passes, and a waiting step restarted), `OPENSC4_TEST_DEMAND_MESSAGES=<n>` sends SimulationBegin, the neighbours message and the passes on day n. The cell walk takes most of a month in the tutorial cities, so the step's state 2 (the month's figures) is reached only in the 35-day runs.

## Parity

Results folders under `%LOCALAPPDATA%\opensc4\parity\`.

| Run | Result |
| --- | --- |
| the demand objects, the interface, Write and Read on Big City d0090 (`dem-smoke`) | identical for a week; the control run (`dem-ctl`) showed the walk's map differing mid-walk between two runs of the original, hence the fingerprint rule above |
| the same on the 12 saves for a week (`dem-m1-7`, two games at a time, with other threads' batches running on the machine) | 10 of 12 on the first pair. Big City d0000 and d0365 "partly": every run's fingerprints identical and every demand record identical; the runs part in the pollution and traffic save records only, where the original also reached two outcomes by itself in the same batch (the time-sliced passes of those simulators) |
| the step, passes, messages, sources and month added, with `OPENSC4_TEST_DEMAND_MONTH=2 OPENSC4_TEST_DEMAND_MESSAGES=1` (`dem-m2-7`) | 12 of 12; Big City d0000 and d0365 after retries, where the original gave two and three outcomes itself |
| the same for 35 days (`dem-m2-35`), the cell walk finishing on 6 saves | 11 of 12, 36 of 36 days identical on each. Big City d0090 "partly": the original reached three outcomes in four runs (its counted residents differ by a few after the walk ends), ours two, one of them the original's most common |
| everything, our object included, with the two switches (`dem-all-7`) | 11 of 12, our object made in every B run. Big City d0000 "partly": fingerprints and demand records identical in every run, the runs part in the traffic record only (as in `dem-m1-7`) |
| everything for 90 days on `saves-mm2` (`dem-all-mm90`) | 2 of 2, 91 of 91 days identical |
| everything on `saves-plopped` with the two switches (`dem-all-pl7`) | 3 of 3 |

Not reached in the corpus: a new city (the starting supply and demand of the exemplars, Init's not-loaded path), the occupant message (0xE8BDA2E7: a building switching between functional and not), the tuning reload (0xCA1B151C).

