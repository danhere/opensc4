# The zone manager (cSC4ZoneManager)

Keeps the zone type of every cell of the city and does the zoning itself: PlaceZone checks a zoning or dezoning request, prices it, takes the money, sets the cells, has the lots in the way demolished and hands the region to the zone developer ([zone.md](zone.md)) to be cut into lots. It also holds what the zones look like and how large they may be. Nearly every developer and simulator asks it for a cell's zone. Reimplemented like the other classes: one writer with every virtual call's argument count checked against the callee's own code, an independent audit of every function against the disassembly, then the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/zonemgr.toml).

## The class

Class ID 0x298F9B2D, IID 0x67652433. Object size 0x380. Constructor 0x004DCE50 (from the factory 0x004DD3E0), destructor 0x004DCC70. Code 0x004DC1A0 to 0x004DE2E0. One per city (the city's slot 36, global 0x00B43D14).

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00A92E00 | `cISC4ZoneManager` | 22: Init 0x004DE170, Shutdown, the grid (5), the counts of zoned and undeveloped cells (6-8), GetDevelopmentFailureCount (9), PlaceZone 0x004DD780 (11), GetZoneType (12), IsRCIZone, a zone's texture, drag colour, display alpha (16-18), smallest and largest size (19, 20) |
| +0x4 | 0x00A92DF0 | `cIGZMessageTarget2` | DoMessage 0x004DCA70 |
| +0x8 | 0x00A92DD8 | `cIGZSerializable` | Write 0x004DC7A0, Read 0x004DD400 |

Fields (src/core/zonemgr_sim.h has the list with its evidence): +0x18 to +0x2C the services; +0x30 a random generator nothing draws from; +0x48 the zone grid (a signed byte per cell); +0x4C the cells per zone type, +0x8C and +0xCC two more counts per type, +0x10C four counters; +0x11C and +0x15C the smallest and largest size per type; +0x19C the steepest slope per type; +0x1E0 and +0x260 the cost of zoning and dezoning a cell per type; +0x2E0 textures, +0x320 colours, +0x360 the display alpha; +0x364 a map from (zone type, reason) to a count of development failures, +0x370 a second such map that is only saved and read. The tuning comes from one exemplar at Init.

The save record is version 1: the grid's reference, the counts, sizes, costs, textures, colours, the alpha, the two maps and five service references. `tools/re/save_schemas/zonemgr.py` decodes it in every corpus save.

## PlaceZone

Ten arguments: the region (a rectangle with a bit per cell; cells that are refused are cleared in the caller's region), the zone type (0 dezones), place (0 only tests and prices), skip the funds test, custom zoning (the slope test, whether a building may be removed), skip the size test, whole lots (a lot reaching out of the rectangle keeps its cell), a cost output, an error output (1 funds, 2 landfill on water, 3 too small, 4 too large, 5 landfill blocked) and a set that receives every lot to be demolished.

In order: the funds are read; the size is tested; then every cell, x outer and z inner: a network on the cell (rail costs its demolition when dezoning), water, the slope, the lot on the cell with the whole-lot test and whether its zone, configuration and building let it go, the lot added to the demolition list; the cell is recorded and its cost added. After the walk the demolition is priced, the lots go into the caller's set, landfill adds the landfill developer's cost, and the total is compared with the funds. If nothing failed and place is set: the money is withdrawn, the grid and the counts are written, the lots demolished, the zone developer's DoParcellization called with a copy of the region, and message 0x07699455 sent. An error inside the walk stops the walk, but the pricing after it still runs.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.zonemgr.slots` | slots 6 to 9 and 12 to 20: the counts, the failure count, GetZoneType, IsRCIZone, textures, colours, the display alpha, sizes |
| `simulators.zonemgr.place` | PlaceZone (src/core/zonemgr_place.cpp) |
| `simulators.zonemgr.init` | Init with the tuning and the grid, Shutdown |
| `simulators.zonemgr.messages` | DoMessage (a network piece placed: the cells under it dezoned) |
| `simulators.zonemgr.save` | Write and Read |
| `simulators.zonemgr.object` | our own object: a detour on the constructor, the destructor, identity, copies of the three vtables |

Still the game's: the factory, three slots that are code shared with other classes, the zoned message's destructors. The class's map and vector template code is written inline. Copied on purpose from the game and commented in the code: asking for a failure count inserts a zero entry for a key that is not there (and the map is saved); Read's locals are unset before the first read (zeros in ours) and one set serves both maps.

## Test cities

The fingerprint's `zonemgr` part: the services held, the loaded flag, the 16 counts of zoned cells, checksums of the other counts and of the whole grid, the alpha, both maps and the tuning. `OPENSC4_TEST_ZONEMGR=1` adds each day a checksum of the query slots and of PlaceZone in its test-only form (place = 0) on 8 by 8 rectangles across the city, a zone type by turns and dezoning, with custom zoning, whole lots, the funds test and the lot set by turns, and a rectangle too small and one too large; `=2` also asks for every failure count (which fills the saved map on both sides); `=3` also sets the display alpha to another value and back. Real zoning comes from the zoning switches ([zone.md](zone.md), [landfill.md](landfill.md)).

## Parity

All runs on 2026-10-10 with every `simulators.zonemgr.*` group on, land zoned on day 2 and dezoned on day 8, landfill zoned on day 3 and dezoned on day 9 unless said otherwise, two games at a time. Result folders under `%LOCALAPPDATA%\opensc4\parity\crime\`. Other threads' games ran on the machine during some of these.

| Run | Result |
| --- | --- |
| 12 saves for 14 days, zoning and landfill zoning, queries (`zm-zone14`) | 10 of 12; Big City d0000 and d0090 partly (the original 3 outcomes on each in its 4 runs, 2 and 1 of them shared) |
| 12 saves for 30 days, no zoning, queries with the failure counts and the alpha set (`zm-all30`) | 11 of 12; Big City d0365 partly: the first B run parted on the first day in the lots' checksum only, as Big City does under load ([lotconfig.md](lotconfig.md)); its three retries same |
| `saves-ports`, 30 days, zoning, growth asked and ports removed (`zm-ports30`) | 3 of 3, all on the first pair |
| `saves-plopped`, 30 days, zoning, failure counts (`zm-pl30`, `zm2-pl30`) | 3 of 3 on the second batch, all on the first pair. In the first, p0090 had no verdict: the original's run ended while the city loaded (no code of ours in it) |
| 90 days on `saves-mm2`, zoning (`zm2-mm90`) | 2 of 2, all 91 days |
| Big City d0090 again, 14 days, zoning (`zm2-bc90`) | same on the third pair (the original 3 outcomes, B 2, both shared) |
| Big City d0000 again with Rush Hour d1095, 14 days, zoning (`zm2-bc0`) | Rush Hour partly (one B run one off in a stopwatch counter). Big City d0000 different by the rule, and not by the zone manager: the original gave 4 outcomes in 4 runs (one with the first-day value it shows under load, the others apart in counters), ours one outcome 4 times, which differs from the original's three quiet runs on one day only (12 days in) in the lots' checksum: the one-day variation the original has on that save by itself ([citylot.md](citylot.md)). Zone grid, counts and zone manager record identical in all eight runs |

The second batches ran with the final build (after the audit's fix).

Reached: all 19 functions that log their first call: Init with the tuning, Read, Write, Shutdown, every query slot, PlaceZone placing (residential, commercial, agriculture, industry, landfill, dezoning, the ports' ground) and testing (sizes, funds, slopes, whole lots, the lot set), DoMessage, the failure counts, the display alpha. Not reached inside PlaceZone: placing with a real cost withdrawn and placing with custom zoning (the test switches place with the funds test skipped; the test-only form covers the pricing). The audit compared every branch instruction by instruction and found one difference, in Read after a failed stream read (the two maps share one set of locals in the game), fixed.
