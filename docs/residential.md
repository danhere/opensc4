# The residential simulator (cSC4ResidentialSimulator)

Despite its name, this is the game's education and health simulator: schools and hospitals, their coverage and funding, strikes, education (EQ) and health (HQ) quality per cell and for the city, life expectancy, and the population grids they work from. Reimplemented after the commercial and industrial simulators ([commercial-industrial.md](commercial-industrial.md)) the same way: function by function on the original object under the parity harness ([parity.md](parity.md)), then our own object under the class ID. Build 641 addresses throughout (data/addresses/residential.toml).

## The class

Class ID 0x4990C013. Object size 0x3D0. Factory 0x00700E60 (`SC4::operator_new(0x3D0)`, constructor 0x00700510). Code 0x006F6E50 to 0x00700E80.

| Vtable | Interface | Slots |
| --- | --- | --- |
| 0x00AB2C10 | `cISC4ResidentialSimulator` (IID 0x077AC1EE) | 61: QueryInterface 0x006F6E60, AddRef (shared), Release 0x006F7DD0, Init 0x006FEAD0, Shutdown 0x006F97F0, the queries (5-59), the deleting destructor 0x00700330 (60) |
| 0x00AB2BFC | `cIGZMessageTarget2` | DoMessage 0x00700350 |
| 0x00AB2BE4 | `cIGZSerializable` | Write 0x006F8C40, Read 0x007009A0, GetGZCLSID 0x006F6E50 |

gzcom-dll's `cISC4ResidentialSimulator.h` has the slots in this order, with a few differences: the ratings, strike chances and workforce percentages (10-13, 30, 31) return an `SC4Percentage` through a hidden pointer; GetAutoBudget and SetAutoBudget (56, 57) take an occupant; slots 17, 18, 39-41 and 59 are shared stubs (they return 0 or nothing).

### Fields

| Offset | Field |
| --- | --- |
| +0xC, +0xD | initialised; the grid sizes set (Init, Read) |
| +0x10 | reference count |
| +0x14, +0x18, +0x1C, +0x20 | references: the message server, the city, the network service, the building development simulator |
| +0x24 | the zone grid (0x00B43D74's slot 6 for 0x1000) |
| +0x28-+0x30 | the building development simulator's grids 0x1010, 0x1020, 0x1030 (its slot 32) |
| +0x34 | a flag handed to the proximity maps (1) |
| +0x38-+0x40 | the proximity maps by wealth (bit grids, slot 5) |
| +0x44, +0x78 | the cell size of the population and other grids, and of the EQ and HQ grids |
| +0x48-+0x50 | the population grids by wealth (uint16) |
| +0x54, +0x58, +0x64, +0x70, +0x74 | working grids (a bit grid, vectors of 7 and 3 bit grids, two float grids) |
| +0x7C-+0x84, +0x88-+0x90 | the EQ and HQ grids by wealth (float) |
| +0x94 | the step of the simulation cycle (0-15) |
| +0xA0 | a month passed or a building changed: restart the cycle |
| +0xA4 | population |
| +0xB8 | a random generator |
| +0xD0 | the budget simulator |
| +0xD4, +0x134 | the health and school blocks (curves, factors, strike flag +0x44 and day +0x48, the message posted when a strike ends +0x5C) |
| +0x120, +0x124, +0x180, +0x184 | the four budget items (health and schools) |
| +0x194, +0x198 | the health and school service records (from the building exemplars) |
| +0x19C-+0x1A4, +0x1C0 | building lists; +0x1A8 and +0x1B4 vectors of lists, the schools and hospitals by type |
| +0x264 | nine curves |
| +0x328, +0x338 | a cell iterator over a rect (the city) |
| +0x348 | int64, the position of the timed cell walk |
| +0x3C8, +0x3C9 | the city-wide auto budgets for schools and hospitals |

## The simulation

DoMessage (0x00700350) runs one step of the cycle on 0x66956814 (with a budget of 5), and on the month (0x66956816) restarts it when it is resting at 10 (or marks +0xA0). Building occupants added or removed (0x99EF1142, 0x99EF1143, kind 0x278128A0) update their health and school budget items and the type lists and mark +0xA0.

| State | What it does |
| --- | --- |
| 0 | strikes, the service records reloaded, new buildings sorted into their type lists |
| 13 -> 1, 14 -> 2, 15 -> 3 | the three proximity maps: mark, timed spread, copy |
| 11 -> 4 | the timed walk over every cell: EQ, HQ, population, age |
| 12 | the monthly figures |
| 5-8 | school and hospital coverage, auto funding, the EQ and HQ spread |
| 9 | the city-wide figures |
| 10 | at rest until the next month |

Init (0x006FEAD0) takes the services, registers with the simulator service (0, 10, 6, 4) and the monthly figures, loads the tuning (0x006FBAB0, exemplar 6534284A-E7E2C2DB-69189C84) and the service records before the first run, makes every grid a save did not give, and creates the four budget items. The save record is version 7 (Read takes 1 to 7); `tools/re/save_schemas/residential.py` decodes it, and it decodes exactly in all 15 corpus saves.

## What is ours

| Replacement | What is ours | File |
| --- | --- | --- |
| `simulators.residential.queries` | slots 5-16, 29-38, 42-44, 46, 48, 50-57 and their helpers | residential_queries.cpp |
| `simulators.residential.occupants` | slots 19-28, 45, 47, 49, 58 and their helpers | residential_occupants.cpp |
| `simulators.residential.messages` | DoMessage, the cycle (0x006FF520) and everything it reaches; detours on two of its helpers that other code calls too (0x006F8970, and 0x006FCBD0, which the traffic simulator's ferry routes also use) | residential_messages.cpp |
| `simulators.residential.init`, `.shutdown` | Init with the tuning, Shutdown | residential_object.cpp |
| `simulators.residential.save`, `.load` | Write, Read | residential_object.cpp |
| `simulators.residential.object` | our own object: constructor, identity, our copies of the three vtables | residential_object.cpp |

Still the game's: the shared stubs, the member destruction our object's destructor calls (0x006FE840), and the generic grid, curve, list, vector, string, property and stream helpers.

The code was written in four parts at once; each part keeps its own copies of the small internal helpers it needs. Where the game reads stack memory it never wrote (a building query that fails, the min/max cell search with no populated cell, wealth outside 1-3), ours uses zeros.

## Test cities

The cycle rests at state 10 between months, so a week's run from a save reaches only its first maps. `OPENSC4_TEST_RESIDENTIAL_MONTH=<n>` hands the simulator the month message every n days; with n = 2 the whole cycle runs about twice in 30 days. `OPENSC4_TEST_RESIDENTIAL_QUERIES=1` makes the queries only the UI makes each day (ratings, building counts, the average grids, ages and EQ/HQ by wealth, auto budgets, and coverage, capacity and ideal funding of the first school and hospital) and adds their checksum to the fingerprint. The fingerprint's `res` part: population, the state and its floats, both strikes, global EQ and HQ, the walk position, the iterator, and checksums of the population, EQ and HQ grids' cells.

## Parity

All runs on 2026-10-09 with every `simulators.residential.*` group on, two games at a time, other threads' games running too. Result folders under `%LOCALAPPDATA%\opensc4\parity\crime\`.

| Run | Result |
| --- | --- |
| 12 saves for a week (`res-all1`) | 12 of 12, but the cycle rested at state 10 throughout |
| a week with `OPENSC4_TEST_RESIDENTIAL_MONTH=2` (`res-mon1`) | 12 of 12; the cycle reaches only its first maps in a week |
| 30 days with the month switch (`res-30`) | 12 of 12 (Rush Hour d0090 on its second pair, the original giving two outcomes there); every state of the cycle ran, twice; residential records identical in all 12 pairs |
| `saves-plopped`, 30 days with the switch (`res-pl`) | 3 of 3 |
| 90 days on `saves-mm2`, the real months (`res-mm90`) | 2 of 2 |
| 30 days with the month and UI-queries switches (`res-q`) | 11 of 12; Making Money d0090 partly (one of four B runs parted on day 5 in the harness's own counter, the other three identical to the original); two reruns of that save (`res-q-mm90-1`, `-2`) same |

Our object was made in every B run. Not reached: the strike ends (no test city goes on strike), the setters only the UI uses (funding percentages, the auto budgets), and GetHQGrids.
