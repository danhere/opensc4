# The occupant manager (cSC4OccupantManager)

The city's index of every occupant: buildings, props, flora, network pieces, lot base textures, automata. Everything that asks "what is at this place" goes through it, tens of thousands of times a game day, and the order in which it answers is game state: callers take the first match or act in the order given. Reimplemented like the other classes: one writer with every virtual call's argument count checked against the callee's own code, an independent audit of every slot against the disassembly, then the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/occmgr.toml; the global 0x00B43D0C is named `SC4::NetworkService` there by an old mistake).

## The class

Class ID 0x098F964D, IID 0xC6BB8334. Object size 0x9C. Constructor 0x004A0540 (from the factory 0x004A18A0), destructor 0x004A0620. Code 0x0049D8B0 to 0x004A1D20. One per city (the city's slot 34, global 0x00B43D0C).

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00A8F7F0 | `cISC4OccupantManager` | 50: Init, Shutdown, SetCitySize, the conversions between positions, world cells, occupant manager cells and standard city cells (6-19), InsertOccupant (20), RemoveOccupant (21), RemoveOccupants (22, 23), MoveOccupant (24), IsCellEmpty, GetBoundingCells, FindOccupant, the first occupant at a position, cell or cells (28-35), the occupants of a box or of cells (36-41), the same as iterations with a callback (42-47), ReleaseOccupantList |
| +0x4 | 0x00A8F7D4 | `cIGZSerializable` | Write 0x0049ECF0, Read 0x004A1A20 |
| +0x8 | 0x00A8F7C4 | `cIGZMessageTarget2` | DoMessage (shared code, does nothing) |

In gzcom-dll's header each overloaded pair is the other way round: of two slots with one name the first takes a filter object, the second an occupant type.

## The index

A fixed grid of 192 by 192 cells of 64 m; the city itself lies at cells 64 to 127 (a large city is 64 cells wide), the rest is margin. A cell is empty or a list of entries. An entry is the occupant's pointer, with its lowest bit set when the occupant lies in that one cell only; an occupant spanning several cells has an entry in each and keeps its rectangle of cells as four bytes of its own. A new entry goes to the front of a cell's list (after a load the lists are in saved order); an emptied cell's list is freed. The manager holds a reference to every occupant.

The queries walk cells x then z ascending and each list front to back. Where several cells are asked, an occupant in more than one of them is reported once, at the first cell met, by a set of occupants seen (asked before the filter and the box test). The first-occupant slots do not use the set. The iterations collect one cell under the lock and call back after it; a callback that returns false ends the walk after its cell.

The save record is version 1: the city's size, four cell counts, then the whole grid, every cell with its entries as references in the list's order. `tools/re/save_schemas/occmgr.py` decodes it in every corpus save.

## What is ours

| Replacement | What is ours | File |
| --- | --- | --- |
| `simulators.occmgr.cells` | slots 6 to 19 and 26: the conversions and GetBoundingCells | occmgr_sim.cpp |
| `simulators.occmgr.edit` | SetCitySize, InsertOccupant, RemoveOccupant, RemoveOccupants, MoveOccupant, FindOccupant | occmgr_edit.cpp |
| `simulators.occmgr.queries` | slots 25 and 28 to 48: IsCellEmpty, the first-occupant, get and iterate families, ReleaseOccupantList | occmgr_queries.cpp |
| `simulators.occmgr.init` | Init, Shutdown | occmgr_sim.cpp |
| `simulators.occmgr.save` | Write and Read | occmgr_sim.cpp |
| `simulators.occmgr.object` | our own object: a detour on the constructor, the destructor, identity, copies of the three vtables | occmgr_sim.cpp |

Still the game's: the set of occupants seen (template code only this class uses, called at its addresses so its allocations stay the same), container helpers shared with other classes, the factory. Copied on purpose from the game and commented in the code: slot 12 (occupant manager cell to world cell) ignores its x and returns nonsense; two first-occupant slots index the grid without a range test; MoveOccupant uses the old cells without a null test; the small conversion slots return whatever EAX held (ours return the same values); slot 19 returns the FPU status word, which cannot be had (ours returns 1; no caller found tests it); Read keeps its two counters across a failed read.

**Sizes.** The roadmap wants bigger city tiles, and this class is where the size is most fixed: 192 cells a side as a constant (Init, every clamp, the full scans, slots 7, 25, 26 and 30), the city's origin at cell 64, the 64 m cell and 16 m city cell as shifts and constants, and an occupant's rectangle in bytes, which caps any grid at 255 cells. The save record carries its own column and row counts, and Read follows them. With `[improvements] big_cities = on` the grid is sized from the city instead ([city-size.md](city-size.md), "The 8 km spike"); off, it is 192 as above.

## Test cities

The fingerprint's `occmgr` part: the grid's dimensions, the cells with lists, the entries and occupants, and an order-sensitive checksum of the whole index every day (per cell, per entry: the one-cell bit, the occupant's type, its cells and its position). Automata (occupant types 0xC7CC75F1 and 0x496E5783, the four classes registered at 0x00514A00: vehicles, pedestrians, animals) are left out of the counts and the checksum, and out of the decoded save record: they move by frames, not game days, and their number and cells differ between two runs of the original. A prop's position inside its cells is left out as well (some props move by frames; their cells agree).

For the same reason the answers of the queries cannot be compared between two runs. `OPENSC4_TEST_OCCMGR=1` compares them inside one run: each day the same few hundred questions (the conversions on a lattice of cells and positions, far outside the grid included; the first-occupant slots; the get and iterate slots by box, world cells and city cells with limits, missing corners, early stops, filters and types; FindOccupant) go to the manager's slots and to the game's own functions at their addresses, one after the other on the same index, and the fingerprint prints how many days the two checksums agreed (`q14/14`). `OPENSC4_TEST_OCCMGR_TYPES=1` logs the entries per occupant type.

## Parity

All runs on 2026-10-10 and 11 with every `simulators.occmgr.*` group on and `OPENSC4_TEST_OCCMGR=1`, two games at a time. Result folders under `%LOCALAPPDATA%\opensc4\parity\crime\`.

The first batches came out mostly "partly", the original with three or four outcomes on saves that are otherwise quiet: the new fingerprint part and the decoded record had things in them that the original does not repeat. Found one after another with `OPENSC4_TEST_OCCMGR_TYPES` and by comparing the original's own saves: the automata of type 0xC7CC75F1, the answers of the queries (hence the comparison inside one run), a cell counted when it holds only automata, two more automaton classes in the save, the position of props (Rush Hour d0090), and the fourth automaton class (type 0x496E5783). The table has the runs after those were taken out.

| Run | Result |
| --- | --- |
| 12 saves for 14 days, land and landfill zoned and dezoned (`om2-zone14`, `om3-rh14`) | 11 of 12. Rush Hour d0090 same in a rerun with the final fingerprint. Big City d0000 different by the rule, as in the zone manager's runs ([zonemgr.md](zonemgr.md)): the original gave the outcome it shows under load once and another three times, ours a third four times, one day (12 days in) off the original's in the lots' checksum, the variation that save has by itself ([citylot.md](citylot.md)); the occupant index identical on every day of all eight runs |
| 12 saves for 30 days (`om2-all30`, `om4-rh30`) | 11 of 12; Making Money d0365 partly (one B run two off in a stopwatch counter from the first day, its retries same). Rush Hour d0090 same in a rerun with the final fingerprint (the original 3 outcomes, B 2, both shared) |
| `saves-ports`, 30 days, zoning, growth asked and ports removed (`om-ports30`) | 3 of 3 |
| `saves-plopped`, 30 days, zoning (`om-pl30`) | 3 of 3 |
| 90 days on `saves-mm2`, zoning (`om2-mm90`) | 2 of 2, all 91 days |

In every B run of every batch the manager's slots and the game's own functions agreed on every day's questions (no "queries differ" line in any log). A run with our code took 29 s where the original took 27 s (Rush Hour, 12 days, debug build).

Reached (45 of the 48 functions that log their first call): Init, Read, Write, Shutdown, InsertOccupant, RemoveOccupant, RemoveOccupants, MoveOccupant, FindOccupant, every conversion and every query family. Not reached: SetCitySize (a new city only), the by-type forms of the first-occupant slot and of RemoveOccupants. The audit compared all 50 slots instruction by instruction: identical but for slot 19's return value (above) and Read's counters after a failed read, fixed.
