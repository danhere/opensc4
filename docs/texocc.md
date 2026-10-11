# The lot base texture occupant (cSC4LotBaseTextureOccupant)

The ground under a lot: the pavement, grass or dirt textures its cells show. One object per lot that has them, and one per landfill cell (3,646 in Big City with its landfill, 517 in Rush Hour). The lot developer makes and updates them when a lot is built, emptied or changed ([lot.md](lot.md)); the landfill zone places one on every cell ([landfill.md](landfill.md)). It holds a box, a usage and a list of texture entries, on top of the occupant base class ([occbase.md](occbase.md)). What it looks like is drawn by a model class of its own, which another thread owns and which stays the game's; ours is the rest. Reimplemented like the other occupant classes ([foundocc.md](foundocc.md), [constrocc.md](constrocc.md)): one writer, an independent audit against the disassembly, the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/texocc.toml).

## The class

Class ID 0xC97F987C, IID 0x097F6D98, occupant type 0x497F6D9D. 0x60 bytes from a pool (0x00B0A86C; allocator 0x005DECF0). Constructor 0x005E05B0, factory 0x005E0820, deleting destructor 0x005E0860. Code 0x005DEA60 to 0x005E11F8, interleaved with its model class and container code. No class derives from it.

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AA2E18 | the base texture's | 12: GetUsage, SetUsage, GetCellCoverage, IsCellCovered, SetTextureSpecification, UpdateVertexAltitudes, LoadModels, UnloadModels |
| +0x4 | 0x00AA2D90 | `cISC4Occupant` | 34, most of them the base's; the class's own: Shutdown, GetPosition, SetAllFlags |
| +0x8 | 0x00AA2D74 | `cIGZSerializable` | Write 0x005DF1E0, Read 0x005E10B0 |
| +0xC, +0x20 | 0x00AA2D10, 0x00AA2CFC | the property and exemplar holders | shared code |

Fields: the base at +0x04; +0x38 and +0x44 the box (two corners); +0x50 the usage (1 a landfill cell, 2 a lot); +0x54 a vector of entries of 16 bytes: the texture's ID, the cell (x, z), orientation, mirroring, a colour, an alpha (0xFF covers the whole cell), a draw mode and which of the texture's pictures is used.

SetTextureSpecification takes a list of entries. A first fill pushes them; a list with the same cells as before changes the entries in place and has the models updated; anything else unloads the models, replaces the list and loads them again. An entry's picture is drawn from the class's own generator (0x00B23078) when its texture has more than one. Then the box is worked out again: the cells' extent, and for the height either the lot's level or the lowest and highest terrain height under the cells. UpdateVertexAltitudes does the box again after the ground moved and has the occupant manager move the occupant. GetCellCoverage gives the cells the entries cover completely.

The save record is version 2, every base texture of the city in one record: the base's part, the box, the usage and the entries. `tools/re/save_schemas/texocc.py` decodes them in every corpus save.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.texocc.slots` | 14 slots: QueryInterface and its thunks, the usage, GetCellCoverage, IsCellCovered, SetTextureSpecification with the box, UpdateVertexAltitudes, Shutdown, GetPosition, GetGZCLSID |
| `simulators.texocc.save` | Write and Read and their holder thunks |
| `simulators.texocc.object` | our own object: detours on the constructor, the pool's allocator and the factory, the deleting destructor with the pool's free, copies of the five vtables taken once |

Still the game's: the model class and everything that loads, unloads or updates models (LoadModels, UnloadModels, the update at 0x005DF640, SetAllFlags), called through the vtable or at their addresses in the game's order; the folded stubs; the base class; the pool. Copied on purpose from the game and commented in the code: the box reads the first entry without checking that there is one; the last two bytes of an entry are never set.

## Test cities

The fingerprint's `texocc` part: the number of base textures (through the occupant manager's grid), the number of entries, and the sum and XOR of a hash per occupant over its box, usage, every entry's saved bytes and the base's saved state. `OPENSC4_TEST_TEXOCC=1` adds a checksum of the query slots on a third of them. Before the first batch the original was run three times against itself on Rush Hour d0090 and Big City (`saves-plopped/p0090`) with land and landfill zoned: the part and the decoded records agreed, the picture bytes included.

Those runs showed something else: the prop occupants' part differed between runs of the original on Big City (on some days two props trade their state). The prop fingerprint no longer mixes any prop's state ([propocc.md](propocc.md)).

## Parity

All runs on 2026-10-11 with the three `simulators.texocc.*` groups on and `OPENSC4_TEST_TEXOCC=1`, two games at a time (`%LOCALAPPDATA%\opensc4\parity\crime\`).

| Run | Result |
| --- | --- |
| 12 saves for 14 days, land and landfill zoned and dezoned (`tx-zone14`) | 11 of 12; Rush Hour d0090 partly (the original 2 outcomes; one B run apart in the flammability part on its eighth day, as that save does between runs of the original, [bldgocc.md](bldgocc.md)) |
| `saves-plopped`, 30 days, zoning and landfill zoning (`tx-pl30`) | 3 of 3 |
| 12 saves for 30 days (`tx-all30`) | 8 of 12; Big City d0000 and d0365, Rush Hour d0000 and d0090 partly |
| `saves-ports`, 30 days, zoning, growth asked and ports removed (`tx-ports30`) | 3 of 3 |
| 90 days on `saves-mm2`, zoning (`tx-mm90`) | 2 of 2, all 91 days |

On three of the four partly saves of the 30-day batch the original itself gave 3 or 4 outcomes in its 4 runs. Every run of the five partly saves was looked at: each parts from the original's first run in a way the original parts from itself: a stopwatch counter one off, Big City's lots' checksum (on its first day under load, or 9 to 14 days in), Rush Hour d0090's weather, flammability or growth 16 to 19 days in (where the base textures differ with the lots, between runs of the original too), and once the flammability part from the first day (a value the original showed in an earlier batch, `pt-b-seaport`).

Reached: the constructor through the factory for textures read from a save and made in a run, SetTextureSpecification on all three paths, the usage, UpdateVertexAltitudes, Shutdown and the deleting destructor (dezoning), GetPosition, the coverage queries, Write. Not reached: the allocator as a function of its own (the callers' fallback when the class factory gives nothing). The audit compared every function instruction by instruction, the box's arithmetic included, and found no difference in behaviour.
