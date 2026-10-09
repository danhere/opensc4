# The demand simulator (cSC4DemandSimulator)

The RCI demand: reimplemented after pollution, power, water, traffic, land value, aura and crime, the same way: function by function on the original object under the parity harness ([parity.md](parity.md)), then our own object under the class ID. Build 641 addresses throughout. Code: [src/core/demand_sim.h](../src/core/demand_sim.h), [demand_demand.cpp](../src/core/demand_demand.cpp) (the demand objects), [demand_sim.cpp](../src/core/demand_sim.cpp) (the interface and the save record).

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

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.demand.demands` | every slot of both cSC4Demand vtables (36 + 6), so every demand object runs our code |
| `simulators.demand.interface` | slots 5-11 of `cISC4DemandSimulator` |
| `simulators.demand.save`, `.load` | Write, Read (version 2) |

`UpdateOccupantEffects` (slot 7) calls the occupant demand helper (0x006A5920) through its address. `GetNeutralTaxRate` returns the curve's result unrounded (in ST0), as the game's does: its callers compute with it before storing it.

## Save records

`cSC4DemandSimulator`, version 2 (Write and Read, the `cIGZSerializable` part at +8): versions before 2 have four more words first, which Read skips. Then the four floats at +0xA0, the demands (count, then ID and a reference each) and the counted values of +0x28 (count, then ID and float each). Read sets +0xC and puts the references in the map as they come.

`cSC4Demand`, version 2: the ID, then nine floats (supply, demand, the two additions, +0x24, +0x28, min, max, active: the minimum before the maximum), the byte +0x38, the economy and tax modifiers and the cap (a float; version 1 has it as a percentage byte). All the city's demands are in one entry of the save, in an order the save framework picks (it differs from run to run), so `sc4save.py` compares them in ID order.

## Test cities

The fingerprint's `demand` part: the step's state, the four saved floats, each demand's fields under its key, the two maps of counted values (the walk's map only outside state 1: the walk is cut into time slices, so a day can end anywhere in it), the neutral tax rate (slot 10) and the jobs of every demand (slot 9).

## Parity

Results folders under `%LOCALAPPDATA%\opensc4\parity\`.

| Run | Result |
| --- | --- |
| the demand objects, the interface, Write and Read on Big City d0090 (`dem-smoke`) | identical for a week; the control run (`dem-ctl`) showed the walk's map differing mid-walk between two runs of the original, hence the fingerprint rule above |
| the same on the 12 saves for a week (`dem-m1-7`, two games at a time, with other threads' batches running on the machine) | 10 of 12 on the first pair. Big City d0000 and d0365 "partly": every run's fingerprints identical and every demand record identical; the runs part in the pollution and traffic save records only, where the original also reached two outcomes by itself in the same batch (the time-sliced passes of those simulators) |
