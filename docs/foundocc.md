# The foundation occupant (cSC4FoundationOccupant)

The slab under a building that stands on a slope: one object per foundation (2,510 in Big City, about 200 in Making Money). The building development simulator makes one when a building grows on uneven ground; the lot developer looks them up ([lot.md](lot.md)). It holds a foundation type, the greatest height, the position of the middle of its top, its size, a usage and an orientation, on top of the occupant base class ([occbase.md](occbase.md)). Reimplemented like the other occupant classes ([bldgocc.md](bldgocc.md)): one writer, an independent audit against the disassembly, the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/foundocc.toml).

## The class

Class ID 0x49C05C8F, IID 0xC8FCE101, occupant type 0x68FD0C69. 0x5C bytes from a pool (0x00B0A830; allocator 0x005D6220). Constructor 0x005D6440, factory 0x005D64C0 (it returns the occupant part, object + 4), deleting destructor 0x005D6500. Code 0x005D6130 to 0x005D6575. No class derives from it.

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AA29A0 | the foundation's | 13: the type, greatest height and usage (stubs folded with other classes), SetSize, GetOrientation, SetOrientation |
| +0x4 | 0x00AA2918 | `cISC4Occupant` | 34, most of them the base's; the class's own: GetBoundingBox |
| +0x8 | 0x00AA28FC | `cIGZSerializable` | Write 0x005D6250, Read 0x005D62D0 |
| +0xC, +0x20 | 0x00AA2898, 0x00AA2880 | the property and exemplar holders | shared code |

Fields: the base at +0x04; +0x38 the type; +0x3C the greatest height; +0x40 the position; +0x4C the size (width, height, depth); +0x58 usage; +0x59 orientation (0 to 3; the setter clamps, Read does not). The box is half the width and depth around the position and the whole height below it. QueryInterface answers the IID, not the class ID.

The save record is version 2, every foundation of the city in one record: the base's part, then the type, greatest height, position, size, usage and orientation. `tools/re/save_schemas/foundocc.py` decodes all 12,030 foundations of the twelve test saves.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.foundocc.slots` | 10 slots: QueryInterface and its thunks, SetSize, orientation, GetBoundingBox, GetGZCLSID |
| `simulators.foundocc.save` | Write and Read and their holder thunks |
| `simulators.foundocc.object` | our own foundation: detours on the constructor, the pool's allocator and the factory, the deleting destructor with the pool's free, copies of the five vtables taken once |

Still the game's: the base class, the folded getters and setters, the pool.

## Test cities

The fingerprint's `foundocc` part: the number of foundations (through the occupant manager's grid) and the sum and XOR of a hash per foundation over its type, height, position, size, usage, orientation and the base's saved state. `OPENSC4_TEST_FOUNDOCC=1` adds a checksum of the query slots on a third of them. Before the first batch the original was run three times against itself on Big City (`saves-plopped/p0090`) with zoning: the part and the decoded records agreed.

## Parity

Run on 2026-10-11 together with the landfill occupant ([landfill.md](landfill.md)), all groups of both on, two games at a time (`%LOCALAPPDATA%\opensc4\parity\crime\`):

| Run | Result |
| --- | --- |
| 12 saves for 14 days, land and landfill zoned and dezoned (`fl-zone14`) | 12 of 12 (Big City d0365 after the decoder change below) |
| `saves-plopped`, 30 days, zoning and landfill zoning (`fl-pl30`) | 3 of 3 |
| 12 saves for 30 days (`fl-all30`) | 12 of 12 |
| `saves-ports`, 30 days, zoning, growth asked and ports removed (`fl-ports30`) | 3 of 3 |
| 90 days on `saves-mm2` (`fl-mm90`) | 2 of 2, all 91 days |

**Big City d0365 and a building under construction.** In the zoning batch three of our four runs saved one building (at occupant manager cells 84,65) still under construction, where the original's four had it finished; the fingerprints were identical on every day. Reruns with only this class on gave it in one run of four, with only the landfill occupant in none, and fifteen single runs put it at none of five for the original, two of five with our object alone and one of five with our slots alone. That looked like ours. Then two runs of the original with one more test switch in the environment (`OPENSC4_TEST_BLDGOCC=2`) both saved it under construction, and four of five of ours did not. So it is the original's: construction runs by frames ([bldgocc.md](bldgocc.md)), and whether that building is finished when the city is saved depends on the run's timing. The building decoder compared the progress of finished buildings (1.0) against unfinished ones; it no longer compares construction progress at all (`tools/re/save_schemas/bldgocc.py`).

Reached: all 12 functions that log their first call (foundations read through the factory, made through the allocator and constructor for buildings grown on slopes, SetSize, the orientation, the box, Write, the deleting destructor). The audit compared every function instruction by instruction and found no difference in behaviour.
