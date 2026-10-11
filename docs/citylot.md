# The lot (cSC4Lot) and the business unit (cSC4BusinessUnit)

A lot is one parcel of the city: its rectangle and facing, its zone type, state and condition, its building and occupants, capacity, populations, jobs and travel populations, whether it has power and road access, its trips, and the business unit it belongs to. There is one object per lot (17,714 in the twelve test saves), made by the lot manager ([zone.md](zone.md)) and when a save is read, and nearly every simulator calls its slots. Reimplemented like the developers ([lot.md](lot.md), [zone.md](zone.md)): three writers by address range, every virtual call's argument count checked against the callee's own code, an audit of how the parts call each other and of the save path, then the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/citylot.toml). The files are called `citylot_*` because `lot_*` is the lot developer's.

## The classes

cSC4Lot: class ID 0xC9BD5D4A, IID 0xE818FCED, 0x8C bytes from a pool (0x00B0D3AC; Allocate 0x006BF430). Code 0x006BF180 to 0x006C3AF8. Three constructors: for a cell rectangle and facing (0x006C1E50, the lot manager's CreateLot), the default (0x006C2340, through the factory 0x006C2FC0 when a save is read) and the copy (0x006C2450: SetState and SetCondition send a snapshot of the lot with their messages). Destructor 0x006C25D0.

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AB19B0 | `cISC4Lot` | 70: identity, the property holder and traffic source parts (3, 4), Init, Shutdown, PostCityInit, the rectangle, altitude and slope, facing, the unit, road and job access, zone type, state (33, 34) and condition (35, 36), land value, the building and its type, the occupants, capacity, populations and travel populations, occupancy, jobs, power, locked vertices |
| +0x4 | 0x00AB1994 | `cIGZSerializable` | Write 0x006C0080, Read 0x006C35F0 |
| +0x8 | 0x00AB1930 | the property holder | 25 (its own properties; the deleting destructor is its slot 22) |
| +0x1C | 0x00AB1914 | `cISC4TrafficSource` | 6: the trips (slot 4 rebuilds them, 0x006C27E0), the cell rectangle |

QueryInterface gives only +0x0 and +0x4; the other two parts come from slots 3 and 4. cSC4BusinessUnit (class ID 0x4A232DA8; vtables 0x00AB18DC, 0x00AB18C4, 0x00AB1860) is a list of lots that share jobs.

Fields (src/core/citylot_sim.h has the list with its evidence): +0x20 the cell rectangle; +0x24 to +0x2C altitude and slopes; +0x30, +0x34 the lot configuration; +0x38 the day of the last state change, +0x3C the building type; +0x40 road access by purpose; +0x4C the unit; +0x54 the populations (a vector of maps), +0x60 the travel populations, +0x6C the travel jobs; +0x79 flags (needs power, has power, historical, vertices locked); +0x7E to +0x83 facing, state, condition, zone, wealth, the vacate reason; +0x84 the trip list; +0x88 a random byte.

The save record is version 8 (every lot of the city in one record): the configuration, flags, rectangle and cell, altitude and slopes, size, facing, state, condition, zone, wealth, the state's day, the building type, the vacate reason, the unit and the travel designate, the population maps, the travel map and jobs, the lot's properties, its trips and the random byte. `tools/re/save_schemas/citylot.py` decodes every lot and every business unit in all 29 corpus saves.

## What is ours

| Replacement | What is ours | File |
| --- | --- | --- |
| `simulators.citylot.slots` | 24 slots: the configuration object, flags, rectangle, centre, altitude and slope updates, location and size, job access, the zone type's update, the days since the state changed, land value, the building and its type, the occupants, capacity and available capacity, populations, travel populations and designates, jobs; the business unit's QueryInterface, Release, Add and Remove; two helpers the other parts call | citylot_object.cpp |
| `simulators.citylot.init` | Init, Shutdown, PostCityInit | citylot_object.cpp |
| `simulators.citylot.save` | Write, and the business unit's Write | citylot_object.cpp |
| `simulators.citylot.object` | our own lot: detours on the three constructors, the destructor with the pool's return, identity, copies of the four vtables taken once | citylot_object.cpp |
| `simulators.citylot.capacity` | 34 slots: the two part getters, the small getters and setters, road access (with the search of the neighbours and of transit stations), SetPopulation, AddTravelPopulation, occupancy; the traffic source's cell rectangle; the business unit's Read, factory and deleting destructor; the trip list's copy, push and erase | citylot_capacity.cpp |
| `simulators.citylot.state` | SetState, SetCondition, the trips rebuilt from the travel populations, the populations' resize | citylot_state.cpp |
| `simulators.citylot.load` | Read (versions up to 8) and the holder part's thunk to it | citylot_state.cpp |

Still the game's: the lot occupant filter (a small class of its own used by the occupants' query), the pool, the business unit's constructor, generic map, vector and list helpers other classes share. Copied on purpose from the game and commented in the code: occupancy divides by zero on a capacity map without entries; looking a population up inserts a zero entry; the vacate reason of a new lot is whatever the pool's memory held; fields of a new trip record the game never writes are zeros in ours.

## Travel jobs: the game's bug, and the fix

A lot tells the traffic simulator how many jobs it offers by wealth ($, $$, $$$) as the trip capacities 0, 1 and 2 of its holder. The game's 0x006BF6A0 rounds each of the three counts and sends it only when it is more than a tenth off the current value, but it always asks the current value of capacity 0, the $ jobs. So a $$ or $$$ count within a tenth of the $ count is never sent and stays at what it was, usually 0.

High-tech industry splits its jobs 10/80/10 (per the community fix, not read from the game files here; the split is the demand exemplar 6534284A-C7BB4816-00004401, property 0x47BB3F30, read by the demand simulator's slot 8), so its $$$ jobs equal its $ jobs on every lot and are never offered: the community's "IH Missing Jobs" problem. Their fix, a plugin that changes the split to 10/78/12, works because 12 is the first share far enough from 10. Other lots are hit the same way when two of their counts come close.

`fixes.travel_jobs` (on by default, [src/core/citylot_object.cpp](../src/core/citylot_object.cpp)) asks each capacity's own value, so every count is sent once it moves. It installs our 0x006BF6A0 by itself when `simulators.citylot.slots` is off, and turns the same switch on in it when that is on. `fixes.travel_jobs = original` keeps the game's behaviour; the parity script turns it off, since it changes the city.

Found by reading the code (2026-10-11); not yet run in the game.

## Test cities

The fingerprint's `citylot` part walks the lot manager's list: the lot count, how many are initialised, the count per state, the total population, and the sum and XOR of a hash over every lot's fields (not the vacate reason). `OPENSC4_TEST_CITYLOT=1` adds a checksum of the pure query slots on every 7th lot. The zoning and ports switches ([zone.md](zone.md), [ports.md](ports.md)) make and remove lots mid-run.

## Parity
All runs on 2026-10-10 with every `simulators.citylot.*` group on and `OPENSC4_TEST_CITYLOT=1`, two games at a time. Result folders under `%LOCALAPPDATA%\opensc4\parity\crime\`.

| Run | Result |
| --- | --- |
| 12 saves for 30 days (`cl-all30`) | 9 of 12. Big City d0090 and Rush Hour d0090 partly (the original 2 outcomes on each; on Rush Hour two of the four B runs are outcomes the original did not show in its four, one parting in the weather part, one with a lot grown differently on day 16, as that save does between runs of the original: [lot.md](lot.md), [zone.md](zone.md)). Big City d0000 different, see below |
| 12 saves for 14 days, zoned on day 2 and dezoned on day 8 (`cl-zone14`) | 12 of 12. One B run of Big City d0365 (second pair) ended without a word while the city loaded, no report; eight more loads of that save with our code went through (`one/clh1` to `clh8`) |
| `saves-ports`, 30 days, growth asked and ports removed (`cl-ports30`) | 2 of 3; Rush Hour q0000 partly (the original 2 outcomes, B 2, 1 shared) |
| `saves-plopped`, 30 days, zoning (`cl-pl30`) | 3 of 3 |
| 90 days on `saves-mm2` (`cl-mm90`) | 2 of 2, all 91 days |

Two things in the save are memory the game never sets, and the decoder leaves them out of the comparison:

- **A new trip record's +0x20, +0x22 and +0x28.** The lot makes a trip record for a travel population no trip has filled yet (0x006C27E0) and writes only part of it; the save has what the stack held (57944, 113, 1768432 in one run). Ours are zeros. Before this was masked nearly every save came out different with identical fingerprints.
- **The vacate reason of a lot not in state 4.** SetState writes it only on entering state 4; a lot made since has what the pool's memory held. The lots zoned in a run differed in it between two runs of the original (Rush Hour d0365: 30, 96, 22, 101 against zeros).

**Big City d0000, day 12.** In `cl-all30` the lots' checksum differed on one day (2457295; the day before and after identical, counts and population equal): CA532429 in the original's four runs, D0468DA1 in ours. The original reaches both: in `cl-zone14` its four runs split two and two on that day, and eight further runs of the original (six single runs with different environments and a heap shift, two in a control batch whose B side replaced nothing, `cl-ctl15`) all gave the value ours gives. `OPENSC4_TEST_CITYLOT_DUMP=<n>` logs every lot's fields at the n-th fingerprint for the next such case.

Reached (80 of the 86 functions that log their first call): Read, Write, Init, PostCityInit, Shutdown, road access with its neighbour search, SetState and SetCondition with their messages, the trips rebuilt, the populations, the business units read, written, joined and left; the zoning runs make lots through the cell constructor. Not reached: GetVacateReason, SetFacing, SetHistorical, road access through a transit station, the trip list's erase, the holder part's thunk to Read, and Read of versions before 8 (the corpus has none).
