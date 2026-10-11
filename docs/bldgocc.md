# The building occupant (cSC4BuildingOccupant)

One object per building of the city: what a lot's building is to the occupant manager ([occmgr.md](occmgr.md)) and to everything that asks about buildings. It is thin: the building's type, its box, orientation, whether its lights are on and how far its construction has got, on top of the occupant base class every kind of occupant shares. Reimplemented like the other classes: one writer with every virtual call's argument count checked against the callee's own code, an independent audit of every function against the disassembly, then the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/bldgocc.toml).

## The class

Class ID 0xA9BD882D, IID 0x87DDA3A9, occupant type 0x278128A0. 0x5C bytes from a pool (0x00B07F1C; allocator 0x00458A60). Constructor 0x00458E90 (from the factory 0x00458F40 when a save is read, and from the building development simulator's AddNewBuildingOccupant), deleting destructor 0x00458F80. Code 0x004587C0 to 0x004592FB. The base class cSC4Occupant (constructor 0x005E9220, code around 0x005E8xxx) is every occupant's and stays the game's.

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00A872C0 | `cISC4BuildingOccupant` | 20: the building type (shared stubs), SetBoundingBox, completion (9, 10), GetBuildingProfile, GetBuildingName, GetExemplarName, orientation (14, 15), IsLit, SetLit, SetName, GetName |
| +0x4 | 0x00A87238 | `cISC4Occupant` | 34, most of them the base's; the class's own: Shutdown, IsInitialized, GetPosition, SetPosition, GetBoundingBox, PostOccupantMessage, GetHighlight |
| +0x8 | 0x00A8721C | `cIGZSerializable` | Write 0x00458A90, Read 0x00458B10 |
| +0xC | 0x00A871B8 | the property holder | 25, shared holder code; the deleting destructor is its slot 22 |
| +0x20 | 0x00A871A0 | the exemplar holder | SetPropertyExemplar |

Fields: +0x04 to +0x37 are the base's (reference count, properties, exemplar, type, the occupant manager's cells, state and flags); the class's own: +0x38 the building type, +0x3C and +0x48 the box (two corners), +0x54 orientation, +0x55 lit, +0x58 completion (1.0 when built).

The position is not stored: GetPosition gives the middle of the box's base, SetPosition moves the box. SetLit sends message 0x09D8E0B6 when the state changes; PostOccupantMessage adds a second message (0x8B8B2CED or EE) for the occupant manager's insert and remove messages. The names and the profile come from the building development simulator by building type.

The save record is version 2, every building of the city in one record: the base's part (flags, type, cells), the exemplar holder's part (the building's own properties and its exemplar's key), then the type, lit, the box, orientation and completion. `tools/re/save_schemas/bldgocc.py` decodes every building in every corpus save (16,518 in the twelve test saves).

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.bldgocc.slots` | 26 slots over the five vtables: QueryInterface and its thunks, the box and position, completion, orientation, IsLit and SetLit, the names and profile, PostOccupantMessage, SetPropertyExemplar, GetGZCLSID |
| `simulators.bldgocc.save` | Write and Read and their holder thunks |
| `simulators.bldgocc.object` | our own building: detours on the constructor, the pool's allocator and the factory, the deleting destructor with the pool's free, copies of the five vtables taken once |

Still the game's: the base class (called at its addresses), shared stubs, the pool's own functions, map, vector and profile helpers other classes call. Copied on purpose from the game and commented in the code: SetLit uses a new message without a null test; several setters return whatever EAX held; Read takes the version and the completion into stack slots that start as pointers.

## Test cities

The fingerprint's `bldgocc` part: the number of buildings (found through the occupant manager's grid) and the sum and XOR of a hash per building over its type, box, orientation, cells, state, flags, properties and name. Left out because they follow frames, not game days: lit (the night lights' timed step, [lot.md](lot.md)), completion while a building is under construction, the highlight. The decoder masks the same two fields. `OPENSC4_TEST_BLDGOCC=1` adds a checksum of the query slots on a fifth of the buildings (chosen by hash, so the choice does not follow the walk's order); `=2` adds the frame-driven fields for a look at how they vary.

Before the first batch three runs of the original on Rush Hour d0090 were compared with each other: the part and the decoded record agreed in all three (after the occupant manager's lesson, [occmgr.md](occmgr.md)).

## Parity

All runs on 2026-10-11 with the three `simulators.bldgocc.*` groups on and `OPENSC4_TEST_BLDGOCC=1`, two games at a time. Result folders under `%LOCALAPPDATA%\opensc4\parity\crime\`.

| Run | Result |
| --- | --- |
| 12 saves for 30 days, with the lot developer's night step asked (`bo-all30`) | 10 of 12. Big City d0000 partly (the original 3 outcomes). Rush Hour d0090 different by the rule: the original gave one outcome four times, ours three, each apart from it in a part where that save varies between runs of the original (the zone list's checksum on day 16, a stopwatch counter, the flammability part on day 8); the same values are among the original's own outcomes in the occupant manager's batch of that save (`om3-rh30`) |
| 12 saves for 14 days, land and landfill zoned and dezoned (`bo-zone14`) | 11 of 12; Rush Hour d1095 partly (one B run one off in a stopwatch counter, its retries same) |
| `saves-ports`, 30 days, zoning, growth asked and ports removed (`bo-ports30`) | 3 of 3, all on the first pair |
| `saves-plopped`, 30 days, zoning (`bo-pl30`) | 3 of 3, all on the first pair |
| 90 days on `saves-mm2` (`bo-mm90`) | d0365 same, all 91 days. d0090 partly: the city branches on its 68th day. The original took one branch once and another three times; three of our runs took a third (8,100 residents that day instead of 8,085). The original took that third branch as well in earlier batches (three of its runs in `lot-mm90` and `lv-re90`) |

Reached: construction of new buildings through the allocator and constructor, Read through the factory, Write, the box, position and orientation setters, SetCompletionPercent, SetLit, PostOccupantMessage, Shutdown and the deleting destructor (demolitions), the names and profile through the query switch. Not reached: SetName (a player naming a building) and the three fallbacks for a missing development simulator. The audit compared every function instruction by instruction, the pool's arithmetic included, and found no difference in behaviour.
