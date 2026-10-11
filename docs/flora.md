# The flora simulator (cSC4FloraSimulator)

Reimplemented after crime ([crime.md](crime.md)) the same way: function by function on the original object under the parity harness ([parity.md](parity.md)), then our own object under the class ID. Build 641 addresses throughout.

## The class

Class ID 0xC990C034. Object size 0xB8. Factory 0x006B2940 (`SC4::operator_new(0xB8)`, then the constructor 0x006B1880). Code from about 0x006AFBD0 to 0x006B3790. Three interfaces:

| Vtable | Interface | Slots |
| --- | --- | --- |
| 0x00AB13C0 | `cISC4FloraSimulator` (primary, IID 0x06C66177) | 26: QueryInterface 0x006AFC90, AddRef 0x004F7BA0 (shared), Release 0x006B0480, Init 0x006B2960, Shutdown 0x006AFDA0, the types (5-14), the place (15, 16), occupants (17-23), SaveObliterated (24), the deleting destructor (25, 0x006B1990) |
| 0x00AB13AC | `cIGZMessageTarget2` | DoMessage 0x006B2F70 |
| 0x00AB1394 | `cIGZSerializable` | Write 0x006AFEE0, Read 0x006AFFE0, GetGZCLSID 0x006AFBD0 |

gzcom-dll has `cISC4FloraSimulator.h` and `cISC4FloraOccupant.h`; the slots are in its order, with each pair of overloads the other way round (as the compiler lays them out).

### Fields

| Offset | Field |
| --- | --- |
| +0xC | initialised |
| +0x10 | reference count (stays at 1 when the last reference goes) |
| +0x14 to +0x24 | references: the occupant manager (0x00B43D0C), the service at 0x00B43DAC (a place's column in the suitability tables, slot 19), the simulator service, the terrain, the lot manager |
| +0x28 | the occupant filter (vtables 0x00AB1374 and 0x00AB135C, base 0x005EA080; its test 0x006AFD10: flora, occupant types 0x278128A0, 0x6A11BD2B, 0xA823821E, and props whose kind has a bit of 0x1F4F) |
| +0x2C | flora on (the cheat "Flora off" clears it and demolishes every tree) |
| +0x2D | trees spread (off when the city's slot 22 says so, unless +0x2E) |
| +0x2E, +0x2F | the cheats "Flora propogation" and "Flora preserve" (trees under terrain changes are kept) |
| +0x30, +0x34 | the city's size in meters; +0x38, +0x3C its size in cells |
| +0x40 | the date of the month's pass |
| +0x44, +0x48 | tracts a month (tuning), then cells / tracts / 4 |
| +0x4C | seeding tries a month (tuning, scaled by the city's size: x size x size >> 20) |
| +0x50 | loaded from a save (Init then leaves the tract walk alone) |
| +0x54 | the tract walk's rectangle iterator (2 by 2 cells a tract) |
| +0x88 | the simulator's `cRZRandom` |
| +0xA0 | the slope scale (16 / tuning value) |
| +0xA4 | the dying trees (a list) |
| +0xA8, +0xAC | the death timer and the deaths due |
| +0xB0, +0xB4 | the death rate (1 / tuning value) and deaths a step |

The flora types are global: a map by type (0x00B49050), the types in load order (vector 0x00B49044), a map by family to its types (0x00B4905C). The first Init loads them (flag 0x00B4903C) and registers `FreeTypes` (0x006B1070) with the app (0x00B43C94, slot 69).

### Type records

0x178 bytes, reference counted (+0x170), one per flora exemplar (type property 0x10 = 0xF in group 0x6534284A):

| Offset | Field |
| --- | --- |
| +0 | the type (property 0x21) |
| +4 | the next stage (0x2A0348BA); two trees of one chain close by merge into the stage after the later one |
| +8 | the type its seeds grow as: its own, or the first stage of its chain |
| +0xC | grows in water (0xEA346888 present) |
| +0x10 to +0x44 | 14 floats (0x083DC35B): maturity age (+0x10), spreading age (+0x14), seeding interval, seeds and reach (+0x18 to +0x20), the ages of the last stages (+0x24, +0x28), steepness (+0x2C), the suitability threshold (+0x30), the seed chance (+0x34), the box's radius and height (+0x40, +0x44) |
| +0x48, +0x4C | the radius and half height merging looks within (0x2A0348BB, 0x2A0348BC) |
| +0x50 | 16 by 16 suitability bytes (0x083DC487, x 255): row by slope class, column by the place's column |
| +0x150 | 0x083DE16F |
| +0x154 | its families (0xA8F149C5) |
| +0x160 | 0x49CAC341 (64 bits) |
| +0x168 | the exemplar |
| +0x16C | flags: bit 0 not seeded at random (0xE9D117C3), bits 1 and 2 by 0x6A37EBB6 |

Init also adds a record of type 0 with the defaults (`DefaultData` 0x006B0220), each time it runs; the second time the insert fails and the record is never freed, as in the game.

### Messages

| Message | What happens |
| --- | --- |
| 0x66956814 | the daily step (0x006B16D0): the death timer; whole deaths take dying trees off (0x006B15F0) |
| 0x66956816 MonthPassed | the date into +0x40, the monthly pass (0x006B2CB0) |
| 0x99EF1142 / 0x99EF1143 | an occupant added (trees under a new building or network piece demolished, 0x006B04F0) or removed (a tree leaves the dying list, 0x006B1190); not for exemplars with 0x029244DB |
| 0x278E6003 | terrain or networks changed over a cell rectangle: trees back to the ground (0x006B1200), or removed when off by 0.001 or more unless +0x2F (0x006B1AB0) |
| 0x26D31EC4 / 0x26D31EC5 | trees stop spreading (unless +0x2E) / spread |
| 0xAB99A64B | the "PlaceFlora" command: a tree of a type at x, z (slot 17) |
| 0xC53D10AA | resources reloaded: the types again (when the instance is a known type), the tuning (instance 0x1A2FDB6A) |
| 0x230E27AC | cheat codes: "Flora" (0x095BDC9C) with on, off, preserve [off], propogation [off] or blast (seeds scattered around random places); another code, or "Flora" alone, runs on into the 0xC53D10AA code with the message's values, as in the game |

## Growth

The monthly pass ages the trees of the next +0x48 tracts of the walk (`Age` 0x006B0A80): dying or dead trees start over as seedlings; the others move through their stages by age (0x006AFBE0, against +0x10, +0x24, +0x28), die where the place's suitability is below +0x30, and mature ones spread seeds (+0x1C of them within +0x20 meters, every +0x18 days). Then +0x4C seeding tries: a random type (not bit 0 of +0x16C) at a random place. A seed (`Seed` 0x006B0980) becomes a tree when the place takes it (slot 15), it is suitable enough (`Suitability` 0x006B0900) and a draw passes +0x34.

Slot 15 (`LocationIsOKForNewFlora`) wants land (or a water tree), ground less steep than +0x2C, the tree's box inside the city, and no other flora's box (through the filter) in the way. Slot 17 (`AddNewFloraOccupant` by type) takes out the flora in its merge box that `MergedType` (0x006B0DA0) joins with the new tree, moves the tree to the average place and its type on, until nothing more merges; then it makes a `cSC4FloraOccupant` (pool 0x00472960, constructor 0x00472B30) with a random orientation and today's date, on the ground.

## Tuning

`LoadTuning` (0x006B00F0, Init and message 0xC53D10AA), exemplar 6534284A-7A4A8458-1A2FDB6A: 0xC9DBDD00 (+0xB0 = 1 / value), 0xC9DBDD01 (+0xB4), 0x8761ABD0 (+0x44), 0x085489C7 (+0x4C), 0xC9DBDD02 (+0xA0 = 16 / value).

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.flora.interface` | slots 7-14, 16, 18-23 and the slope class (0x006B02E0) |
| `simulators.flora.growth` | the daily step, dying trees, the monthly pass, `Age`, `Suitability`, `Seed` (the stage function 0x006AFBE0 inlined) |
| `simulators.flora.messages` | DoMessage (+4 slot 3) with the cheat code, and its handlers (0x006B1200, 0x006B1AB0, 0x006B04F0, 0x006B1190) |
| `simulators.flora.types` | slots 5 and 6, `LoadTypes` and its callback, `LoadTuning`, `DefaultData`, the message after a load (0x006B05E0), `FreeTypes`, the type map's clear and erase (0x006B08C0, 0x006B04B0) |
| `simulators.flora.placement` | slots 15 and 17, `MergedType` |
| `simulators.flora.init`, `.shutdown` | Init (with `LoadOccupants` 0x006B0660), Shutdown |
| `simulators.flora.save`, `.load` | Write, Read (version 2), SaveObliterated (slot 24) |
| `simulators.flora.helpers` | the on/off switch (0x006B19E0), the occupant filter's test (0x006AFD10) |
| `simulators.flora.object` | our own object under the class ID: constructor, destructor, identity, our copies of the three vtables |

Still the game's: the record release 0x00472C70 (outside the flora code; ours does the same), the generic containers, strings and streams (the flora occupant class is ours too, see below).

## Test cities

The monthly pass runs once a month, so a week's run often has none. `OPENSC4_TEST_FLORA_MONTH=<n>` runs it every n days (the date into +0x40, then 0x006B2CB0), as MonthPassed does.

The fingerprint's `flora` part: the number of flora occupants (occupant manager slot 39, type 0x74758926) and a checksum of each one's type, birth date, last seeding date, state, orientation and position.

## Save record

Version 2 (Write and Read, the `cIGZSerializable` part at +8): the four flags +0x2C to +0x2F, the size in meters (floats) and cells, +0x40 to +0x4C, +0x50, the tract walk, the random generator, the dying list (references), the death timer and deaths (floats). `tools/re/sc4save.py` decodes it.

## Parity

All runs with `OPENSC4_TEST_FLORA_MONTH=2` but the 90-day one (2026-10-09, two games at a time, other threads' games running too). Result folders under `%LOCALAPPDATA%\opensc4\parity\crime\`.

| Run | Result |
| --- | --- |
| the interface alone (`f-iface7`) | 11 of 12; Big City d0000 partly, the original with three outcomes in four runs there; flora records identical |
| interface, growth, messages, types and placement (`f-all7`) | 11 of 12; Making Money d0090 partly, one B run differing only in the tick count; flora records identical |
| everything, our object included, after rebasing onto main with the aura simulator (`f-obj7`) | 11 of 12; Big City d0000 partly again (traffic and pollution records, the original with two outcomes); the rerun with retries (`f-obj-bc0`) passed, Rush Hour d1095 too |
| everything on `saves-plopped` (`f-obj-pl7`) | 3 of 3, our object made in every B run |
| everything for 90 days on `saves-mm2`, the real monthly passes (`f-obj-mm90`) | d0365 same, 91 of 91 days; d0090 partly: the original gives two outcomes, and one of four B runs a third, which first parts from the original in pollution on day 9 and in flora only on day 29 (trees follow the buildings); flora records identical in the first pair |

Reached in the runs (the functions that log their first call): all but `MergedType` (no two trees merged in a week), the cheat code, `LoadOccupants`, `SaveObliterated` and `SetEnabled`, which nothing in a test city calls.

## The occupant (cSC4FloraOccupant)

One object per tree or plant. Class ID 0xA9C05C85, IID 0xC83DD057, occupant type 0x74758926; 0x5C bytes from a pool (0x00B07FC8; allocator 0x00472960). Constructor 0x00472B30, factory 0x00472C30, deleting destructor 0x00472D30. Code 0x00472900 to 0x00472DBD. No class derives from it. Underneath is the occupant base class ([occbase.md](occbase.md)). Ours in src/core/floraocc_sim.cpp (fields and vtables with their evidence in floraocc_sim.h; data/addresses/floraocc.toml).

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00A88608 | `cISC4FloraOccupant` | 15: the type, birth date, last seeding date and state (eight getters and setters that are stubs folded with other classes), GetOrientation, SetOrientation, SetStaticFloraData |
| +0x4 | 0x00A88580 | `cISC4Occupant` | 34, most of them the base's; the class's own: GetBoundingBox, GetLotTag |
| +0x8 | 0x00A88564 | `cIGZSerializable` | Write 0x00472990, Read 0x00472A20 |
| +0xC, +0x20 | 0x00A88500, 0x00A884E8 | the property and exemplar holders | shared code |

Fields: the base at +0x04; +0x38 the flora type; +0x3C the lot tag; +0x40 the position; +0x4C the birth date, +0x50 the last seeding date; +0x54 the type's record (fetched from the simulator the first time the box is asked for; counted); +0x58 state, +0x59 orientation. The box is the position with the type's radius around it and its height above.

The save record is version 3, every tree of the city in one record (78 bytes each without properties): the base's part, then the type, position, the two dates, state, orientation and lot tag. `tools/re/save_schemas/floraocc.py` decodes all 25,838 trees of the twelve test saves.

| Replacement | What is ours |
| --- | --- |
| `simulators.floraocc.slots` | 11 slots: QueryInterface and its thunks, orientation, SetStaticFloraData, GetBoundingBox, GetLotTag, GetGZCLSID; a detour on the release of a type record (0x00472C70) |
| `simulators.floraocc.save` | Write and Read and their holder thunks |
| `simulators.floraocc.object` | our own tree: detours on the constructor, the pool's allocator and the factory, the deleting destructor with the pool's free, copies of the five vtables taken once |

Still the game's: the base class, the eight folded getters and setters, the pool. Copied on purpose from the game and commented in the code: the constructor never sets the lot tag, so a new tree no lot planted has what the pool's memory held (zero in a fresh page, a dead tree's tag in a reused place), and it is saved; when the base's part fails, Write and Read skip the class's fields and still report success while the stream has no error.

The fingerprint's `floraocc` part: the number of trees (through the occupant manager's grid) and the sum and XOR of a hash per tree over what the `flora` part does not have: the lot tag, the type's record with its radius and height, the exemplar, the manager's cells, the base's saved state. `OPENSC4_TEST_FLORAOCC=1` adds a checksum of the query slots on a thirteenth of the trees. Before the first batch the original was run three times against itself on Rush Hour d0090 and Making Money d0090: the part and the decoded records agreed.

Parity on 2026-10-11 with the three groups on and the query switch, two games at a time (`%LOCALAPPDATA%\opensc4\parity\crime\`):

| Run | Result |
| --- | --- |
| 12 saves for 30 days (`fo-all30`) | 10 of 12; Big City d0000 and d0090 partly: runs on both sides differ on one day (12 days in) in the lots' checksum alone, the variation that city has by itself ([citylot.md](citylot.md)) |
| 12 saves for 14 days, land and landfill zoned and dezoned (`fo-zone14`) | 12 of 12 |
| `saves-ports`, 30 days, zoning, growth asked and ports removed (`fo-ports30`) | 3 of 3 |
| `saves-plopped`, 30 days, zoning (`fo-pl30`) | 3 of 3 |
| 90 days on `saves-mm2` (`fo-mm90`) | 2 of 2, all 91 days |

Reached: all 14 functions that log their first call (trees made through the allocator and constructor and through the factory, Read, Write, the box with the type's record fetched, the lot tag, orientation, the record's release, the deleting destructor). The audit compared every function instruction by instruction and found no difference in behaviour.
