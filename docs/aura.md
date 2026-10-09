# The aura simulator (cSC4AuraSimulator)

The mayor rating map. Reimplemented the same way as pollution ([pollution.md](pollution.md)), power ([power.md](power.md)) and water ([water.md](water.md)): function by function on the original object under the parity harness ([parity.md](parity.md)), then our own object under the class ID. Build 641 addresses throughout.

## The class

Class ID 0x0990BDD8. Object size 0x160. Factory 0x0068D6E0 (`SC4::operator_new(0x160)`, then the constructor 0x0068D280). Code from about 0x0068B9C0 to 0x0068FC80. Three interfaces:

| Vtable | Interface | Slots |
| --- | --- | --- |
| 0x00AB07C8 | `cISC4AuraSimulator` (primary, gzcom-dll header) | 16 and the scalar deleting destructor (slot 16): QueryInterface 0x0068B9C0 (0xC773E880 and 1 at +0, 0x452294AA at +0x4, 0xE4FDA3D4 at +0x8), AddRef 0x004F7BA0 (shared), Release 0x0068BFB0, Init 0x0068E6B0, Shutdown 0x0068DEC0, then the getters, the two transient effects and BustStrike |
| 0x00AB07B8 | `cIGZMessageTarget2` | DoMessage 0x0068FA60 |
| 0x00AB07A0 | `cIGZSerializable` | Write 0x0068CC50, Read 0x0068E340, GetGZCLSID 0x0068D1A0 |

### Fields

| Offset | What |
| --- | --- |
| +0x18 | random generator (riots) |
| +0x30, +0x34, +0x38 | the city, the message server, service 0x00B43D68 (references) |
| +0x3C | grid size argument (4) |
| +0x40 | int8 aura grid (slots 5, 12; saved) |
| +0x44 | int8 effect grid at cell size, rebuilt each pass |
| +0x48 | int8 smoothed grid (+0x44 averaged per tract, smoothed over passes; -128 = none) |
| +0x4C | int8 transient grid (slots 6, 7, 13; decays each pass) |
| +0x50 | int8 landmark-style sources (property 0xCA5B9305) |
| +0x54 | int16 park sources (property 0x27812850) |
| +0x58 | int16 park map: +0x54 times the park department's funding (slot 14) |
| +0x5C, +0x60 | map of department (property 0xAA59670C) to int16 grid of its sources (property 0x2781284F), and its size |
| +0x68 | int16: copy of +0x6C after each pass (slot 15) |
| +0x6C | int16: the department grids, each times its department's funding |
| +0x70 | pass state (0 idle, 1-17) |
| +0x74 | the date the pass started |
| +0x78 | lot iterator (state 5) |
| +0x7C | state 3's place in the map (-1 = start) |
| +0x84 | mayor rating (slot 8) |
| +0x88 | rating offset (service 0x00B43D90, value 0xAA5B8407) |
| +0x94 | float[12]: each budget department's funding at the first pass |
| +0xC4 | vector of {x, z, 1}: tracts below +0x13C |
| +0xD0 | list of clusters {x, z, count} |
| +0xD4 | byte: refresh the department sources (set by an old save) |
| +0xD8 ... +0x100 | tuning curves: park, 4 pollution (vector +0xDC), two coverage grids, three service grids, funding, height |
| +0x104, +0x108 | smoothing (0.6) and transient decay (0.4) |
| +0x10C | int32[3]: bases of the effect table |
| +0x118, +0x128 | float[4] value and radius of the transient effect kinds (slot 6) |
| +0x138, +0x13C, +0x140, +0x144 | cluster distance, low threshold, cluster minimum, riot curve |
| +0x148, +0x154 | best and worst tract: x, z (cells, centre), value (int16) (slots 9, 10) |

The effect table at 0x00B0D150 is static: three 20-byte entries {id 0x1010/0x1020/0x1030, tuning index, budget department, base, value}. A lot's occupants carry maps of these ids to weights.

### Messages

| Message | What |
| --- | --- |
| 0x66956814 | the pass step with budget 5; DoMessage then returns 0 while a pass runs and has taken fewer days than its state number |
| 0x6695681A | the pass step with the message's budget (data 3) |
| 0x66956816 MonthPassed | starts a pass (state 1) when none runs |
| 0x99EF1142 / 0x99EF1143 | an occupant added / removed (0x0068ECF0), unless its exemplar has 0x029244DB |
| 0xC2C09554 | the rating offset again |
| 0xC53D10AA | the tuning again, for the tuning exemplar's group and instance |
| 0x26D31EC1 | with +0xD4 set: every lot with department sources removed and added again |

## The pass

`Simulate` (0x0068F190, `simulators.aura.step`) runs one state per call:

1. Only when initialised: the start date, then 2.
2. The park map: each park source times the park department's funding (budget department 0x8A4260A6), truncated.
3. One department grid per call into +0x6C (set by the first, added by the rest), each times its department's funding (1 for none); after the last, +0x68 becomes a copy.
4. The effect grid filled with -128; the lot iterator made or rewound; with lots, `UpdateEffectValues` (0x0068C000: the table's values reset to their bases, plus the funding curve at each department's change since the first pass) and 5, else the pass ends.
5. Each lot's aura (`OccupantAura` 0x0068CE00: the weighted average of its occupants' effect values, plus the height curve at its height below the terrain) over its rectangle, keeping the higher value. Cut into time slices (0x0068BF50 deadlines, checked every 0x400 steps).
6. The park map through the park curve (stays in 6 without that curve).
7. Each pollution grid through its curve.
8. - 12. Two coverage grids of service 0x00B43DA4 (float, zoned lots only), a uint8 grid of service 0x00B43D70 and two of 0x00B43DA8 (zoned lots only), each through its curve.
13. The landmark sources added.
14. The smoothed grid: the effect grid averaged over each tract plus the rating offset; a tract with a value moves (1 - 0.6) of the way, rounded away from zero.
15. `UpdateMayorRating` (0x0068DA80): the aura grid = smoothed + transient; best and worst tracts; low tracts listed; the rating weighted by the three population grids of 0x00B43DA4, sent to the monthly figures (0x0A5CBF37).
16. `UpdateLowClusters` (0x0068D4B0) and `MaybeRiot` (0x0068D030: a random threshold, a random cluster, the riot curve at its size; situation 0xCA380B06).
17. The transient grid decays; message 0x2A09B21A; state 0.

The grid walks of states 2, 3 and 6-13 (0x0068C270, 0x006BDA30, 0x006F8970, 0x0068C950, and 0x0068C550/0x0068C750 for zoned lots) average a finer source over the target's tract and repeat a coarser one.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.aura.interface` | slots 5-15 of `cISC4AuraSimulator` |
| `simulators.aura.step` | the pass (0x0068F190) with its grid walks and callbacks |
| `simulators.aura.helpers` | `OccupantAura`, `UpdateEffectValues`, `UpdateMayorRating`, `UpdateLowClusters`, `MaybeRiot`, `PostPassDone` (0x0068BA40) |
| `simulators.aura.buildings` | `OnOccupant` (0x0068ECF0): park, department and landmark sources spread around an occupant (0x005E4500, 0x005E4130); a network piece added makes transient effect 3 |
| `simulators.aura.messages` | `DoMessage` (+0x4 slot 3) |
| `simulators.aura.tuning` | `LoadTuning` (0x0068D770) |
| `simulators.aura.init`, `.shutdown` | `Init`, `Shutdown` |
| `simulators.aura.save`, `.load` | `Write` (version 9), `Read` (8 and 9) |
| `simulators.aura.object` | our own object under the class ID: constructor (0x0068D280), destructor (0x0068D110), identity, our copies of the three vtables |

Still the game's: the container helpers (map find, iterator steps, the row-vector assignment 0x0068E580, the map's tree erase), the script registration glue Init calls (0x0068C180, 0x0058DE80, the callbacks 0x0068BCA0 and 0x0068BD50), GetGZCLSID.

## Test cities

The pass starts on MonthPassed and takes about one state per game day, so a week covers half of it. `OPENSC4_TEST_AURA_PASS=<n>` starts one on the n-th day of a run when none runs; parity runs use it with windows of 25 days, which hold a whole pass.

The fingerprint's `aura` part: checksums of the grids +0x40 to +0x6C (the effect grid +0x44 left out while state 5 fills it, since the day can end anywhere in its time slices), the state, the rating and offset, the best and worst tracts.

## Save record

Version 9 (Write, the cIGZSerializable part at +0x8): references to the aura, park map, park sources, smoothed, transient and landmark source grids and the landmark map; the department grids (count, then key and reference each); +0x80, the rating, the best and worst tracts (x, z, value as uint16); the clusters (count, then x, z, count). Version 8 has one grid reference where the department grids are; Read then sets +0xD4. Decoded by `tools/re/sc4save.py`.

## Parity

Run with the aura pass started on day 1 (`OPENSC4_TEST_AURA_PASS=1`) and windows of 25 days, two jobs at a time (`OPENSC4_PARITY_JOBS=2`, own user folders under `OPENSC4_PARITY_ENV`). Results (2026-10-09; result folders under `%LOCALAPPDATA%\opensc4\parity\`):

| Run | Result |
| --- | --- |
| control, 7 days (`aura-control`) | 10 of 12 on the first pair; the two others differed only in the effect grid sampled mid state 5, which the fingerprint now leaves out |
| interface, step, helpers, buildings, messages (`aura1`) | 11 of 12 identical for 26 days; Rush Hour d0090 "partly": its first B run parted in the pollution grids on day 22 with the aura still identical, and all three retry pairs matched the original |
| everything, our object included (`aura2`) | **12 of 12**, aura records identical in all 12, our object made in every B run; every replaced function reached in all 12 |
| everything, `saves-plopped` (`aura2-plopped`) | 3 of 3 for 26 days |
| everything, `saves-mm2`, 90 days (`aura2-90`) | d0365 identical for 91 days; d0090 "partly": again only the first B run, parting on day 69 in pollution, power and water with its aura record identical; the three retry pairs matched the original; a fresh pair (`aura2-90-rerun`) was identical for 91 days, all five records identical |

Both odd outcomes are a first B run parting in other simulators while the aura stays identical, the pattern [parity.md](parity.md) describes as the harness's own noise (the original reached two outcomes on Big City d0000 in `aura2`).

Not reached in these runs: the department refresh (+0xD4, message 0x26D31EC1, set only by reading a version 8 record; all corpus saves are version 9), a riot (situation 0xCA380B06 needs a cluster of 16 low tracts).
