# The retaining wall occupant (cSC4LotRetainingWallOccupant)

The wall around a lot whose ground was levelled into a slope: one object per lot that has walls (1,367 in Big City, 156 in Rush Hour). It holds a wall type, its lot, which of the four sides have a wall, a hash per side, a position and a size, on top of the occupant base class ([occbase.md](occbase.md)). The lot developer has walls made and removed through a helper of this class's file ([lot.md](lot.md)). Reimplemented like the other occupant classes ([foundocc.md](foundocc.md)): one writer, an independent audit against the disassembly, the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/wallocc.toml).

## The class

Class ID 0x49C05C9F, IID 0x295D1162, occupant type 0x895D1169. 0x6C bytes from a pool (0x00B0A8AC; allocator 0x005E2A50). Constructor 0x005E2F40, factory 0x005E3330, deleting destructor 0x005E3370. Code 0x005E28C0 to 0x005E33F6. No class derives from it.

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AA3080 | the wall's | 13: SetSize, SetLot, the edge hashes (9, 10); the lot, edge flags and wall type getters and setters are stubs folded with other classes |
| +0x4 | 0x00AA2FF8 | `cISC4Occupant` | 34, most of them the base's; the class's own: Shutdown, SetPosition, GetBoundingBox |
| +0x8 | 0x00AA2FDC | `cIGZSerializable` | Write 0x005E2AB0, Read 0x005E2B40 |
| +0xC, +0x20 | 0x00AA2F78, 0x00AA2F64 | the property and exemplar holders | shared code |

Fields: the base at +0x04; +0x38 the wall type; +0x3C the lot (referenced); +0x40 the edge flags; +0x44 four edge hashes; +0x54 the position; +0x60 the size.

Three functions of the file are not methods. ComputeEdgeHashes (0x005E2CE0) walks a lot's four sides: for each cell along a side it takes the neighbouring lot and the terrain's height, and where the lot stands higher than its surroundings the side gets a hash of the height and the place. The make-or-remove helper (0x005E3070) makes a wall for a lot that needs one (the hashes, a wall type drawn from the lot configuration's list with the file's own generator at 0x00B230C8, position and size from the lot's rectangle and the highest drop) and inserts it in the occupant manager, or finds and removes a lot's wall.

The save record is version 2, every wall of the city in one record (104 bytes each): the base's part, the type, edge flags, the four hashes, position, size and the lot's reference. `tools/re/save_schemas/wallocc.py` decodes them in every corpus save.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.wallocc.slots` | 13 slots: QueryInterface and its thunks, SetSize, SetLot, the edge hashes, Shutdown, SetPosition, GetBoundingBox, GetGZCLSID |
| `simulators.wallocc.helpers` | detours on ComputeEdgeHashes and the make-or-remove helper (with the type's draw) |
| `simulators.wallocc.save` | Write and Read and their holder thunks |
| `simulators.wallocc.object` | our own wall: detours on the constructor, the pool's allocator and the factory, the deleting destructor with the pool's free, copies of the five vtables taken once |

Still the game's: the base class, the folded stubs, the pool, and the drawing: the walls' geometry is built by code from 0x005E3400 on (another thread's). That code also sets and clears the edge flags by whether a side's geometry could be built, so the flags follow the view: they are left out of the fingerprint and masked in the decoder (in Rush Hour d0365, 94 of 184 walls have a hash on a side whose flag is clear).

## Test cities

The fingerprint's `wallocc` part: the number of walls (through the occupant manager's grid) and the sum and XOR of a hash per wall over its type, its lot's cell, the four hashes, position, size and the base's saved state. `OPENSC4_TEST_WALLOCC=1` adds a checksum of the query slots and of ComputeEdgeHashes on a third of the walls. Before the first batch the original was run three times against itself on Rush Hour d0090 and Big City d0365 with zoning: the part and the decoded records agreed.

## Parity

Run on 2026-10-11 together with the construction occupant ([constrocc.md](constrocc.md)), all groups of both on, two games at a time (`%LOCALAPPDATA%\opensc4\parity\crime\`):

| Run | Result |
| --- | --- |
| 12 saves for 14 days, land and landfill zoned and dezoned (`wc-zone14`) | 12 of 12 |
| 12 saves for 30 days (`wc-all30`) | 11 of 11 compared. Big City d0090: the first pair differed on one day (12 days in) in the lots' checksum, as that city does by itself ([citylot.md](citylot.md)); in the second pair the original's run crashed while the city loaded (0x00975CC8, a write to read-only data on another thread; none of our code in that run), and our second run matched the original's first on all 31 days |
| `saves-ports`, 30 days, zoning, growth asked and ports removed (`wc-ports30`) | 3 of 3 |
| `saves-plopped`, 30 days, zoning (`wc-pl30`) | 3 of 3 |
| 90 days on `saves-mm2`, zoning (`wc-mm90`) | 2 of 2, all 91 days |

Reached: all 18 functions that log their first call (walls read through the factory, made and removed through the helper with the type's draw, the setters, ComputeEdgeHashes, Write, Shutdown, the deleting destructor). The audit compared every function instruction by instruction and found no difference in behaviour.
