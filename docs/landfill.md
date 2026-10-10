# The landfill developer (cSC4LandfillDeveloper)

Keeps the city's landfill zones: their tiles' capacity and use, the monthly upkeep and what zoning a landfill costs, and what happens when landfill is zoned, dezoned or its ground changes. The pollution simulator ([pollution.md](pollution.md)) fills the tiles. Reimplemented like the growth and tract developers: one writer, an audit against the game's code, then the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/landfill.toml).

## The class

Class ID 0xEA09D4AE. Object size 0x50. Constructor 0x006B9780, factory 0x006B98E0. Code 0x006B8B10 to 0x006BA120 (cSC4LandfillZone follows; it stays the game's and is called at its addresses). The city's slot 78 gives the object. It has no destructor of its own: Release shuts it down, destroys the zone list and deletes it.

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AB157C | the primary | 13: QueryInterface, AddRef, Release 0x006B9D20, Init 0x006B9D70, Shutdown 0x006B94B0, GetEfficiency, GetSimulatorType, GetMaintenanceCosts, GetCurrentBudget, GetZoningCost 0x006B9030, GetLandfillUsage, GetLandfillTileCount, GetLandfillZoneFromCell |
| +0x4 | 0x00AB156C | `cIGZMessageTarget2` | DoMessage 0x006B9EA0 |
| +0x8 | 0x00AB1554 | `cIGZSerializable` | Write 0x006B9880, Read 0x006B9900 |

Fields (evidence in the header of landfill_sim.cpp): +0x18 to +0x28 the services; +0x2C the department budget (0x6A42A8F5); +0x30 the list of zones; +0x38 the upkeep (int64, written to line item 0x1E); +0x40 capacity, +0x44 use. The tuning (exemplar 0x6534284A/0x67CD5FA1/0x6A2806AF) is in globals at 0x00B0D2A4 to 0x00B0D2C0.

The save record is version 3: the zone count, then every zone inline (its rectangle, a byte per cell, its tiles with capacity and use, a flag and a reference). `tools/re/save_schemas/landfill.py` decodes it in every corpus save (Big City has one or two zones, the other cities none).

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.landfill.sim` | primary slots 3 to 12 and what they reach: the tuning, the terrain measure for the zoning cost, the usage and upkeep |
| `simulators.landfill.messages` | DoMessage: the month (upkeep by the department's funding), zoning and dezoning, tiles and ground changing, the city loaded |
| `simulators.landfill.save` | Write and Read |
| `simulators.landfill.object` | our own object: a detour on the constructor, identity, copies of the three vtables |

Still the game's: the factory, shared helpers (cSC4LandfillZone is ours too, see below). Copied on purpose from the game: removing a zone that is not in the list unlinks and frees the list's head; Read empties the list without releasing the zones; the department is used without a null test; GetLandfillZoneFromCell leaves out a zone's far edges though the other tests include them.

## Test cities

The fingerprint's `landfill` part: the zone count, capacity, use, upkeep and a hash of every zone and tile. `OPENSC4_TEST_LANDFILL=1` adds the queries no city run makes (the zoning cost of a few regions, the zone at a few cells, efficiency and budget). Big City and `saves-plopped` have landfill zones; zoning and dezoning landfill are not reached unattended.

## Parity

Run together with the airport and seaport developers; the table is in [ports.md](ports.md#parity). In short (2026-10-09): 10 of 12 for a week and 8 of 12 for 35 days on the usual saves, the others on saves where the original itself varied that evening; 3 of 3 on `saves-plopped` for 35 days and 3 of 3 on `saves-ports` for 30 days (all Big City saves have landfill zones), landfill records identical wherever a pair was the same. Reached: Init, the city loaded, the monthly upkeep with zones, the usage, saving and loading zones and tiles, the queries. Not reached: zoning and dezoning landfill, ground changes under a zone, the zoning cost outside the query switch.

## The zone (cSC4LandfillZone)

One object per landfill zone, made by the developer when landfill is zoned or a save is read. Not a GZCOM class: 0x70 bytes, vtable 0x00AB1610 (5 slots, three of them shared code), constructors 0x006BBB60 (plain, for Read) and 0x006BBD60 (from a region), destructor 0x006BBBC0. Code 0x006BA120 to 0x006BD4B0, 50 functions, all ours (src/core/landfillzone_sim.cpp, landfillzone_cells.cpp, fields and evidence in landfillzone_sim.h; data/addresses/landfillzone.toml). A tile is 0x20 bytes (vtable 0x00AB15DC): its grid place, active, has garbage, capacity and use.

What it does: keeps the zone's cells (a rectangle with a bit per cell) and a grid of tiles over them; levels the ground when landfill is zoned (through the lot developer); puts a base texture on every cell and a landfill occupant on a tile once it holds garbage (one of several pictures, drawn with the C library's rand); finds a cell with road access each month (a zone without it is inactive and gets a marker prop); counts capacity, use and upkeep for the developer; runs the seagulls and the two automata controllers; clears cells when landfill is dezoned or the ground under it changes.

| Replacement | What is ours |
| --- | --- |
| `simulators.landfillzone.sim` | Release, the deleting destructors (zone and tile), and what the developer calls: Init, Shutdown, the counts (active tiles, use, free capacity, upkeep, empty), the capacity update, the effects, the month |
| `simulators.landfillzone.tiles` | AddRegion, ClearCell, the cells' refresh, OnTilesChanged |
| `simulators.landfillzone.save` | Write and Read (inline in the developer's record, version 4; Read takes 3 too) |
| `simulators.landfillzone.object` | our own zone: detours on both constructors and the destructor, our copy of the vtable |

The game's 0x006BABC0 reads ECX after calling GetUsage, so that detour is a naked thunk. Copied on purpose from the game and commented in the code: the tile grid is one short each way (x1 - x0 by z1 - z0); AddRegion puts old tiles back by their old grid place in the new width, so they shift when the rectangle grows west or north; the refresh clears a cell without deactivating its tiles; a missing exemplar leaves a null object that is used all the same.

Nothing zones landfill unattended, so `OPENSC4_TEST_LANDFILL_ZONING=<n>` zones a 6 by 6 square of free land next to a network as landfill (zone type 0xE) on the n-th day through the zone manager, and `OPENSC4_TEST_LANDFILL_DEZONING=<n>` dezones its left half. The fingerprint's `landfillzone` part has what the `landfill` part lacks: each zone's cells, counters, road access cell, which effects it holds, and the occupants on its cells; `OPENSC4_TEST_LANDFILLZONE=1` adds the zone's queries.

Parity on 2026-10-10 with the four groups on, both switches and the queries, two games at a time (`%LOCALAPPDATA%\opensc4\parity\crime\`):

| Run | Result |
| --- | --- |
| 12 saves for 14 days, landfill zoned on day 2 and dezoned on day 8 (`lfz-zone14`) | 10 of 12. Big City d0000 and d0090 partly: two of the four B runs differ on one day (12 days in) in the lots' checksum alone, the one-day variation the original shows on that city by itself ([citylot.md](citylot.md)); the lot code was the game's in these runs |
| `saves-plopped`, 30 days, zoned and dezoned (`lfz-pl30`) | 3 of 3, all on the first pair |
| `saves-ports`, 30 days (`lfz-ports30`) | 3 of 3, all on the first pair |

Reached (43 of the 50 functions): both constructors, Read and Write with tiles, Init, the month with the road access search, the counts, the region constructor with the ground levelled and base textures placed, ClearCell with tiles deactivated and occupants removed, the effects, Shutdown and the destructors. Not reached: AddRegion on an existing zone, OnTilesChanged and its per-cell check (ground changes under a zone), the marker prop of a zone without road access, two tile vector helpers.

