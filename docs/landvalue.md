# The land value simulator (cSC4LandValueSimulator)

Reimplemented after pollution ([pollution.md](pollution.md)), power ([power.md](power.md)) and water ([water.md](water.md)), the same way: function by function on the original object under the parity harness ([parity.md](parity.md)), then our own object under the class ID. Build 641 addresses throughout. Code: [src/core/landvalue_sim.cpp](../src/core/landvalue_sim.cpp), [src/core/landvalue_object.cpp](../src/core/landvalue_object.cpp).

## The class

Class ID 0xE990C18E, interface 0xE7781D8C. Object size 0xA4. Factory 0x006BDEF0 (`SC4::operator_new(0xA4)`, then the constructor 0x006BDDF0). Code from 0x006BD4B0 to 0x006BF17C: 31 functions, about 8 KB, plus one helper elsewhere (0x005E5390, called only from here). Three interfaces:

| Vtable | Interface | Slots |
| --- | --- | --- |
| 0x00AB1670 | `cISC4LandValueSimulator` (primary) | 12 and the scalar deleting destructor (slot 12): QueryInterface 0x006BD4C0, AddRef 0x004F7BA0 (shared), Release 0x006BD8A0, Init 0x006BE3B0, Shutdown 0x006BDF70, GetLandValueType 0x006BD700, GetLandValue 0x006BD740, then the grid getters (0x006BDD80 and the shared one-line getters 0x0060FB80, 0x006AAC50), GetAverageLandValue 0x006BDD90, GetModeAltitude 0x006BDDA0 |
| 0x00AB1660 | `cIGZMessageTarget2` | DoMessage 0x006BF0D0 |
| 0x00AB1648 | `cIGZSerializable` | Write 0x006BD8D0, Read 0x006BD950, GetGZCLSID 0x006BD4B0 |

gzcom-dll's `cISC4LandValueSimulator.h` declares the slots in the game's order.

### Fields

| Offset | What |
| --- | --- |
| +0xC | initialised (Init ran) |
| +0x14, +0x18, +0x1C, +0x20 | references: the message server, the city, the terrain, a service (0x00B43D14) whose slot 12 (x, z) says whether a tile has something on it |
| +0x24 | the intrinsic land value grid (8-bit, cell size from the tuning, id 0x89D5BB39): land, waterfront and altitude |
| +0x28 | the total land value grid (id 0x89D5BB51): the intrinsic value plus the influences; `GetLandValue` reads it |
| +0x2C | the work grid (id 0x89D5BB60), where the step builds |
| +0x30 | the slope grid (float, cell size 8, id 0x49D5BB6F) |
| +0x34 | dirty: a building or the network changed (messages below) |
| +0x38 | the step's state (0 to 9) |
| +0x3C, +0x40 | cell iterators over the land value and the slope grids |
| +0x44, +0x48, +0x4C | a sum, a count, and "count every land cell" (when no cell has anything on it) |
| +0x50 | the altitude histogram (a vector of 32-bit counts; buckets of 32, +0x5C = 5) |
| +0x60 to +0x6C | the altitude scale: the pivot, the ranges below and above it, the value at the top |
| +0x70 | the influence the step is at (a map node) |
| +0x74 | the average land value (byte) |
| +0x78 | the average altitude of the cells with something on them |
| +0x7C to +0x82 | tuning bytes: cell size shift (2), land value of land (20), unused (120), the type thresholds (50, 75), the waterfront bonus and radius (100, 5) |
| +0x84, +0x88 | the altitude and influence curves (or null) |
| +0x8C | the influences: a map from an object (Service43D68's slot 32 of an id) to a factor |
| +0x98, +0x9C, +0xA0 | the lowest and highest land altitude, the mode altitude |

### Messages

| Message | What it does |
| --- | --- |
| 0x66956814 | the step with a budget of 5 |
| 0x6695681A | the step with the message's value |
| 0x66956816 MonthPassed | when dirty and the step is done (state 9): clears dirty, state 0 |
| 0x99EF1142, 0x99EF1143, 0x678E6E3A, 0x278E6003, 0x07699455 | dirty |

## The step

`Simulate` (0x006BE7B0) runs one state per call. States 1, 3, 4, 5 and 8 walk cells under a time budget (0x0068CC20, checked every 0x400 steps; their deadline steps are globals at 0x00B490E4 to 0x00B49104) and carry on where they stopped on the next call; the others finish in one call.

- 0: the cell iterator restarts.
- 1: the work grid: the land value of land (+0x7D, 20) where any tile corner of the cell is land (terrain slot 36, `BlockHasLand` 0x006BD5C0), else 0.
- 2: the waterfront: `SpreadFromValue` (0x005E5390) gives each cell within 5 steps (4-neighbour) of a cell holding 0 the bonus (100) less 20 per step. The lowest and highest altitudes start from the terrain's highest and lowest (slots 24 and 23); sums and histogram cleared.
- 3: per cell, its centre's altitude (grid slot 21, terrain slot 13); land cells move the lowest and highest. Land cells with something on them (`BlockHasLot` 0x006BD540; every land cell if none has) add to the average altitude and its histogram. The fullest bucket's middle is the mode altitude, kept within the lowest and highest. `UpdateAltitudeScale` (0x006BD750): the pivot is the lower of the mode and the average, then the ranges below and above it, and the value at the top (100, more for a city whose land rises over 100 above the pivot, at most 150).
- 5: the slope grid: per 8-tile cell, Service43D68's slot 36 object's slot 29 over its tiles.
- 4: with an altitude curve: the intrinsic grid is the work grid plus `AltitudeValue` (0x006BD810; 0 to 50 up to the pivot, 50 to the top value above it, through the curve), at most 255. Without one the intrinsic grid keeps its old values.
- 6: the work grid becomes a copy of the intrinsic grid.
- 7: one influence per call: `ApplyInfluence` (0x006BDA30) runs `AddInfluence` (0x006BD640) on each work cell with the influence object's grid (its slot 13, int16; averaged down or repeated up to the land value cell size): the value through the influence curve, times the factor, added, kept in 1 to 255. After the last, the total grid becomes a copy of the work grid.
- 8: the average land value over the cells counted in state 3, the monthly figure 0x6A663AC8, state 9, message 0x87CC75B3 (`PostUpdated` 0x006BD6B0).
- 9: done until the month restarts it (dirty only).

The map's order is that of its keys, which are object addresses. Under the harness one pass takes about two months of game time (the budgets are short), so a 7-day parity run sees only states 0 and 1; the runs below are 90 and 365 days.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.landvalue.interface` | slots 5-11 |
| `simulators.landvalue.step` | the step (0x006BE7B0) |
| `simulators.landvalue.helpers` | `BlockHasLot`, `BlockHasLand`, `AddInfluence`, `ApplyInfluence`, `PostUpdated`, `UpdateAltitudeScale`, `AltitudeValue`, `SpreadFromValue` (its distance grid comes from the game's STL allocator, as the game's, whose pool order is game state) |
| `simulators.landvalue.messages` | `DoMessage` (+0x4 slot 3) |
| `simulators.landvalue.tuning` | `LoadTuning` (0x006BE110): exemplar 0x6534284A/0xE7E2C2DB/0xE7E2C8D8, properties 0x47E2C200 (cell size), 0x47E2C300/301/310 (byte pairs), 0x47E2C320 (altitude curve), 0x47E2C330/331 (influence ids and factors), 0x47E2C332 (influence curve) |
| `simulators.landvalue.init`, `.shutdown` | `Init`, `Shutdown` |
| `simulators.landvalue.save`, `.load` | `Write`, `Read` (version 1) |
| `simulators.landvalue.object` | our own object under the class ID: constructor, destructor, identity, our copies of the three vtables |

Still the game's: the cell iterator, deadline, map (`SC4UintFloatMap::Index` 0x006A3170, `SC4MapIterator::Next` 0x005D17C0) and vector helpers (generic containers), GetGZCLSID.

The fingerprint's `lv` part: checksums of the intrinsic and slope grids, the average land value, the mode and average altitude (float bits) and the state; the total grid is the main line's `landvalue`.

## Save record

Version 1 (Write and Read, the cIGZSerializable part at +0x8): the intrinsic, total and slope grids (references), a byte (dirty, or the step not done), the average land value, the average altitude (float). Read sets dirty from the byte; Init then starts the step at state 0. `tools/re/sc4save.py` decodes it.

## Parity

Results below; run with `OPENSC4_PARITY_ENV` set so they keep apart from other batches.
