# The occupant base class (cSC4Occupant)

What every occupant shares: buildings ([bldgocc.md](bldgocc.md)), props, flora, network pieces, base textures, automata. 57 occupant classes put its functions in their vtables. It holds the reference count, the property holder and exemplar holder parts, the occupant's type, the cells the occupant manager gave it ([occmgr.md](occmgr.md)), the removal counts, a state byte and a flags byte, and the placeable object. Reimplemented like the other classes: one writer with every virtual call's argument count checked against the callee's own code, an independent audit of every function against the disassembly, then the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/occbase.toml).

## The class

Class ID 0xA9BC9AB6, IID 0x26BB767E. 0x34 bytes. Constructor 0x005E9220 (every derived constructor calls it), destructor 0x005E8C40. Code 0x005E8970 to 0x005E95AD, and a few small functions elsewhere. Vtables of a plain base object: 0x00AA41D8 (`cISC4Occupant`, 34 slots), 0x00AA41BC (`cIGZSerializable`), 0x00AA4158 (the property holder), 0x00AA4140 (the exemplar holder).

| Offset | Field |
| --- | --- |
| +0x00 | `cISC4Occupant` |
| +0x04 | `cIGZSerializable` |
| +0x08 | the property holder: reference count at +0x0C, own properties |
| +0x1C | the exemplar holder, the exemplar at +0x20 |
| +0x24 | the occupant type |
| +0x28 | the occupant manager's cells, four bytes (0xFF first: none) |
| +0x2C, +0x2D | removal counts |
| +0x2E | state: initialized, read from a save, the highlight in the high half |
| +0x2F | flags (4, visible, from the constructor) |
| +0x30 | the placeable object |

Init marks the occupant initialized and sends message 0x99EF1140 (one read from a save gets its highlight set instead); Shutdown sends 0x99EF1141. PostOccupantMessage builds a message with the occupant, its visible flag, its type and one datum and sends it. The removal flags are two counters (set and unset count up and down); CanRemove tests them. An occupant's groups are a property of its exemplar. The base's part of a save record (inside each derived class's record) is version 4: the removal counts, the highlight, the flags without the two view bits, the type, the cells and the holder's properties.

## What is ours

The functions sit in 57 vtables, so each is replaced by a detour at its address, which covers every vtable and every direct call at once: 36 detours in five groups.

| Replacement | What is ours |
| --- | --- |
| `simulators.occbase.flags` | 15: IsInitialized, GetHighlight, the base's GetPosition and GetBoundingBox, the removal flags and CanRemove, SetVisibility, SetFlag, SetAllFlags, ClearFlag, IsFlagSet, GetFlags, the occupant manager's cells |
| `simulators.occbase.life` | 9: the constructor, destructor and deleting destructor, Init, Shutdown, QueryInterface, AddRef, Release, GetGZCLSID |
| `simulators.occbase.messages` | 5: PostOccupantMessage, SetHighlight, GetBoundingCityCells, the placeable object made and set |
| `simulators.occbase.groups` | 5: IsOccupantGroup, AddOccupantGroup, GetOccupantGroups, and two helpers in the class's range that other classes call (whether an exemplar has a group; an exemplar's list of them) |
| `simulators.occbase.save` | 2: the base's Write and Read |

Still the game's: the factory and the 120 small thunks that adjust `this` and jump on (a relative jump or call in their first five bytes; they all end in our detours), four stubs of three or four bytes, and the "false, one argument" stub behind slot 27, which the linker folded with 74 vtables of other classes. Copied on purpose from the game and commented in the code: several functions return only AL or whatever EAX held; GetBoundingCityCells returns the FPU status word (no caller reads it) and keeps x on the FPU where it rounds z to float; Write and Read use their argument slots as locals.

Tiny functions and registers: the game's link-time optimiser lets callers keep values in ECX and EDX across small callees. Every direct call and jump to the 36 addresses (190 sites) and the callers of the thunks that jump into them were scanned: none reads a register or the flags after the call beyond the documented result. Virtual callers cannot assume it.

## Test cities

The fingerprint's `occbase` part: the number of occupants (through the occupant manager's grid, each once, automata left out) and the sum and XOR of a hash per occupant over its type, cells, removal counts, the initialized and loaded bits, the flags that are saved, and its groups. Left out because the view or frames drive them: the highlight, flag bits 0x02 and 0x20 (the game leaves those two out of the save as well), the placeable object. `OPENSC4_TEST_OCCBASE=1` adds a checksum of the query functions on a seventh of the occupants; `=2` adds counts per flag bit.

Before the first batch the original was run three times against itself on Rush Hour d0090 and on Making Money d0090: the part and the decoded records agreed.

## Parity

All runs on 2026-10-11 with the five `simulators.occbase.*` groups on and `OPENSC4_TEST_OCCBASE=1`, two games at a time. Result folders under `%LOCALAPPDATA%\opensc4\parity\crime\`.

| Run | Result |
| --- | --- |
| 12 saves for 14 days, land and landfill zoned and dezoned (`ob-zone14`) | 11 of 12; Big City d0090 partly: two of the four B runs differ on one day (12 days in) in the lots' checksum, the variation that city has by itself ([citylot.md](citylot.md)) |
| 12 saves for 30 days (`ob-all30`) | 12 of 12 |
| `saves-ports`, 30 days, zoning, growth asked and ports removed (`ob-ports30`) | 3 of 3 |
| `saves-plopped`, 30 days, zoning (`ob-pl30`) | 3 of 3 |
| 90 days on `saves-mm2` (`ob-mm90`) | 2 of 2, all 91 days |

The batches ran with the "false, one argument" stub hooked as well; the audit showed it belongs to 74 foreign vtables, and it went back to the game. With that final build, single runs of Making Money d0090 with zoning: six of eight runs of ours gave the original's fingerprints, two gave another lots' checksum on the zoning day (the trip count of three lots). Ten runs of the original, the same way, gave that other value once: the trips of that day depend on what else the machine is doing (other threads' games were running), as on Big City ([lotconfig.md](lotconfig.md)).

Reached (31 of the 36 functions): the constructor and destructor through every kind of occupant made, read and removed, Init, Shutdown, QueryInterface, AddRef, Release, the flags, visibility, SetRemovalFlags and CanRemove, the manager's cells, PostOccupantMessage, SetHighlight, GetBoundingCityCells, the placeable object, the groups, the base's Write and Read. Not reached: the base's own GetPosition and GetBoundingBox (every derived class has its own), the deleting destructor and GetGZCLSID of a plain base object, UnsetRemovalFlags, SetPlaceableObject. The audit compared all of them instruction by instruction and found no difference in behaviour.
