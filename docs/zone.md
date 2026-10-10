# The zone developer and the lot manager (cSC4ZoneDeveloper)

One object with two faces. As the zone developer it turns a zoned area into parcels: it picks lot sizes from the growth developers' demand, lays streets along and into the area, cuts the area into pieces and the pieces into lots, and clears lots when land is dezoned. Its +0x4 part is the city's lot manager: the grid of lots by cell, the list of lots, the lots in and around a rectangle, whether a lot can be made somewhere, making and deleting lots. The lot manager runs all the time (the tract, lot and port developers call it); the zoning code runs when land is zoned. Reimplemented like the other developers ([tract.md](tract.md), [lot.md](lot.md)): five writers by address range, every virtual call's argument count checked against the callee's own code, an audit of how the parts call each other, then the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/zone.toml).

## The class

Class ID 0x498F9B01. Object size 0xFC. Constructor 0x007348D0, destructor 0x00734680, factory 0x00734AF0. Code 0x00729E70 to 0x00734B10. The city's slot 75 gives the object, slot 33 and the global 0x00B43D08 its lot manager part.

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AB3830 | `cISC4ZoneDeveloper` (IID 0xE818ED2B) | 11: QueryInterface, AddRef (shared), Release, Init 0x00734030, Shutdown 0x007335A0, HighlightParcels 0x00733F30, DoParcellization 0x00733FA0, SetOptions, ExistsNetworkOfType, IsCellWater, the deleting destructor |
| +0x4 | 0x00AB37C0 | the lot manager (IID 0x681905BD) | 28: the lot at a position, a cell (slot 6) or under an occupant; the lots in a rectangle (7) and beside one (8-10); a rectangle's edges (11, 12); a random lot; an iterator over the lots; the foundation height around a vertex, position or cell (17-19); CanCreateLot (20; 8 = yes, 1 water, 3 a network, 7 a cell a lot has); whether a configuration fits (21); making a lot or its preview (22); CreateLot (23), DeleteLot (24), the lots of a rectangle deleted (25) |
| +0x8 | 0x00AB37AC | `cIGZMessageTarget2` | DoMessage 0x007328D0 |
| +0xC | 0x00AB3794 | `cIGZSerializable` | Write 0x0072AB40, Read 0x00734380 |

src/core/zone_sim.h has every slot and the fields with their evidence. In short: +0x18 to +0x50 the services and the street tools; +0x54 the lot grid by cell, +0x60 the lot list with its iterators and count; +0x84 to +0x94 the lot width and depth, the depth's range and the street spacing a parcellization chose; +0x98 to +0x9C option bytes (+0x99 clear while only a preview is wanted); +0xA0 the zone type and +0xA4 the region of the operation in hand; +0xAC to +0xD0 the sides' scores and the directions the rows and lots start from; +0xD8, +0xE4 the preview's maps; +0xF0 a table of network pieces.

## What zoning does

The zone manager's PlaceZone ends in DoParcellization (slot 6) with the region and the zone type. For a residential, commercial or industrial type: the cells that can be zoned are taken (lots that do not fit the zone deleted on the way); ChooseLotSizes picks the growth developer with the highest desirability at the region's centre and a lot configuration whose capacity is nearest the demand; the region's sides are scored for the networks along them and directions chosen; streets are extended from dead ends and laid along edges where the region is more than two cells both ways; the region is cut into pieces by flood fill, and each piece into rows of back-to-back lots with cross streets (agriculture and other one-cell types get their own fill). Other zone types (dezoning among them) clear the lots under the region. HighlightParcels (slot 5) does the same as a preview, recording what it would make.

The save record is version 1: the grid's size, then one reference per cell (every cell of a lot refers to the same lot). `tools/re/save_schemas/zone.py` decodes it in every corpus save.

## What is ours

| Replacement | What is ours | File |
| --- | --- | --- |
| `simulators.zone.slots` | lot manager slots 5, 6, 8, 9, 11-14, 17-19, 26; SetOptions and IsCellWater; the configuration filter's test | zone_slots.cpp |
| `simulators.zone.helpers` | eleven helpers the other parts call: street drawing (0x0072A2A0), the region copy and trim, small tests; one (0x00729F90) takes its occupant in ESI and goes through a naked thunk | zone_slots.cpp |
| `simulators.zone.init`, `.shutdown`, `.messages`, `.save` | Init, Shutdown with every lot deleted, DoMessage, Write and Read | zone_object.cpp |
| `simulators.zone.object` | our own object: a detour on the constructor, the destructor, identity, copies of the four vtables | zone_object.cpp |
| `simulators.zone.cells` | lot manager slots 7, 10, 20 (CanCreateLot), 25; the cell-level workers: the cells of a region that can be zoned, the flood fill, one lot on a rectangle, a rectangle filled with one-cell-wide lots, the preview's outline | zone_cells.cpp |
| `simulators.zone.parcels` | CreateLot (23), DeleteLot (24), ExistsNetworkOfType; clearing a region's lots, cutting a region into pieces and a piece into lots with streets, the network queries of a cell, the network piece table | zone_parcels.cpp |
| `simulators.zone.streets` | lot manager slots 21 and 22; dead ends extended, streets along edges, the choice of directions, joining street dead ends to a new road, a street across a rail | zone_streets.cpp |
| `simulators.zone.parcellize` | HighlightParcels (5), DoParcellization (6), Parcellize, ChooseLotSizes, the lot iterator | zone_parcellize.cpp |

Functions one part calls in another are detoured by their owner, so each group works alone or with the others. Still the game's: two shared two-instruction getters (lot manager slots 16, 27), STL map, list and vector code the compiler placed in the range or shares with other classes, the flood of small template helpers. Copied on purpose from the game and commented in the code: stack slots the game reads without writing (zeros in ours), a choice between equally good lot configurations that follows heap addresses (the set is hashed by pointer), asymmetric bounds tests in the preview.

## Test cities

Nothing zones land unattended, so `OPENSC4_TEST_ZONING=<n>` (src/core/zoning_autotest.cpp) does it on the n-th day of a run through the zone manager's PlaceZone, with the arguments the port developers use when they dezone: four rectangles of free land (where the lot manager's CanCreateLot answers 8), low density residential 6 by 4, medium commercial 4 by 4, agriculture 8 by 8 and medium industry 4 by 6 cells, each searched from a different corner of the city. `OPENSC4_TEST_DEZONING=<n>` dezones the left half of each again. The original game's code does the rest (in Rush Hour all four are placed, 97 lots in all; in the dense Big City some are refused).

The fingerprint's `zone` part: the lot count, a checksum of the lot list in order (cell, zone type, state, flags, facing) and of the grid, the maps' sizes, the sizes the last parcellization chose, the option bytes. `OPENSC4_TEST_ZONE=1` adds a checksum of the lot manager's and zone developer's pure queries on a lattice of cells.

## Parity

All runs on 2026-10-10 with every `simulators.zone.*` group on and `OPENSC4_TEST_ZONE=1`, two games at a time. Result folders under `%LOCALAPPDATA%\opensc4\parity\crime\`. The first run of all with our code on (Rush Hour, land zoned on day 2 and dezoned on day 8) gave the original's lot counts after every step.

| Run | Result |
| --- | --- |
| 12 saves for 14 days, zoned on day 2 and dezoned on day 8 (`zn-zone14`) | 8 of 12. The other four are saves where the original itself reached 2 to 4 outcomes in its 4 runs; on Big City d0000, where no outcome was shared, the runs fall into the same variants on both sides (a stopwatch counter one off from day 6, the residential growth map a day late on day 13), only combined differently; lots, population and the zone part identical in all eight runs |
| 12 saves for 30 days, no zoning (`zn-all30`) | 10 of 12; Big City d0000 and Rush Hour d0090 partly (the original 3 and 4 outcomes) |
| `saves-ports`, 30 days, zoning, growth asked and ports removed (`zn-ports30`) | 3 of 3, all on the first pair |
| `saves-plopped`, 30 days, zoning (`zn-pl30`) | 3 of 3, all on the first pair |
| 90 days on `saves-mm2`, zoning (`zn-mm90`) | d0090 same, all 91 days; d0365 partly (the original 3 outcomes, B 3, 2 shared) |

Reached (52 of the 72 functions that log their first call): Init, Read, Write, Shutdown, the messages, the lot manager's queries, CanCreateLot, CreateLot and DeleteLot in every run; with zoning the whole parcellization: the cells that can be zoned, ChooseLotSizes, the choice of directions, dead ends extended and streets along edges, the cut into pieces and into lots, agriculture's fill, clearing lots on dezoning, a street across a rail. Not reached: HighlightParcels and the preview's records (the zoning tool's drag), joining dead ends to a new road, the plop-everything cheat, the lots of a rectangle deleted (slot 25).

The audit of how the parts call each other, with CutPieceIntoLots, Parcellize, ChooseLotSizes and the streets compared instruction by instruction, found nothing to change in the code. A last run with the zone and lot groups on together, zoning and dezoning on `saves-ports` (`zn-fin7`): 3 of 3.
