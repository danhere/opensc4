# The prop occupant (cSC4PropOccupant)

One object per prop of the city: the things on lots and along networks (benches, cars, people, signs), four fifths of a city's occupants (69,251 in Big City). A prop knows its type, box, orientation, whether it shows (its state), when it shows (hours of the day, a range of dates, the view, the light of its cell), a chance, a usage and the lot it belongs to. The prop developer places, removes and switches them ([prop.md](prop.md)); the occupant base class is underneath ([occbase.md](occbase.md)). Reimplemented like the other classes: one writer with every virtual call's argument count checked against the callee's own code, an independent audit of every function against the disassembly, then the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/propocc.toml).

## The class

Class ID 0x2977AA47, IID 0xE9793A65, occupant type 0xA823821E. 0x68 bytes from a pool (0x00B0A8E4; allocator 0x005ECE60). Constructor 0x005EE050, factory 0x005EEF50, deleting destructor 0x005EEF90. Code 0x005EC960 to 0x005EF4E0, mixed with other classes'. Six classes derive from it (the terrain decal, the burnt building, the visual effect and three more); they hold or call most of its functions and stay the game's.

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AA4900 | `cISC4PropOccupant` | 24: the prop type (7, 8), orientation, SetState and GetState (11, 12), the hours of the day (13, 14), SetSimulatorDateRange, TickSimulatorDate, the chance, the timer bits and state (19-21), the usage |
| +0x4 | 0x00AA48F0 | `cIGZMessageTarget2` | DoMessage 0x005ECFB0 |
| +0x8 | 0x00AA4868 | `cISC4Occupant` | 34, most of them the base's; the class's own: Shutdown, GetPosition, SetPosition, GetLotTag |
| +0xC | 0x00AA484C | `cIGZSerializable` | Write 0x005ECE90, Read 0x005EF360 |
| +0x10, +0x24 | 0x00AA47E8, 0x00AA47D0 | the property and exemplar holders | shared code |

Fields: the base at +0x08; +0x3C and +0x48 the box; +0x54 the date range (allocated while there is one: interval, duration, start and end date); +0x58 the lot tag; +0x5C a 16-bit index into a table of prop type IDs the whole game session shares (0x00B231A4); +0x5E, +0x5F the hours in tenths; +0x60 the timer bits (1 the hours, 2 the dates, 4 the view, 8 the cell's light); +0x61 orientation; +0x62 the chance (100); +0x63 the state; +0x64 the usage.

A timed prop shows when all four timer bits are set, and then only if one draw from a generator all props share (0x00B231B0) is below its chance. SetState tells the model and posts message 0xE9C86DDB. The dates follow the simulator's date (TickSimulatorDate); the three other bits follow the 24-hour clock, the view and the lights, which run by frames.

The save record is version 6, every prop of the city in one record: the base's part, the type's ID, the box, orientation, state, the hours, a flag and the date range if there is one, the chance, usage, lot tag and timer bits. `tools/re/save_schemas/propocc.py` decodes all 311,930 props of the twelve test saves.

## What is ours

The derived classes share the functions, so most are replaced by detours at their addresses (as in [occbase.md](occbase.md)).

| Replacement | What is ours |
| --- | --- |
| `simulators.propocc.slots` | 13 detours and 2 slot swaps: QueryInterface, the prop type with the type table, orientation, GetState, the usage, the chance, the box's size, insertion in the occupant manager, Shutdown, GetPosition, SetPosition, GetLotTag, GetGZCLSID |
| `simulators.propocc.timers` | 9 detours: SetState, the timer bits and state, the hours, SetSimulatorDateRange, TickSimulatorDate, DoMessage |
| `simulators.propocc.save` | Write and Read |
| `simulators.propocc.object` | our own prop: detours on the constructor, the pool's allocator, the factory and the deleting destructor, copies of the six vtables taken once (a derived class's constructor overwrites them with its own, as it does the game's) |

Still the game's: the small thunks that adjust `this` (they jump into our detours), stubs folded with other classes, the base and holder code, the pool. Copied on purpose from the game and commented in the code: the deleting destructor does not free the date range (only Shutdown does); Read's locals are unset before the first read (zeros in ours).

## The save code and prop pox

"Prop pox" is the game's known corruption of large cities' saves, believed to sit in how props are saved and read back. Nothing is fixed here: ours copies the game exactly. What reading Write, Read and the parts they call turned up, for whoever takes it on (not reproduced):

1. The base's Read and Write ignore the property holder's result. The holder's Read (0x005FF420) returns early on a wrong version or a failed property read without consuming the 12-byte exemplar key that follows; the base reports success as long as the stream has no error, and the rest of the prop is then read out of step. The holder's Write (0x005FE070) skips the key the same way if its property write fails.
2. The holder's Write writes an exemplar (property 0x6A871B82) as a separate resource into the save in the middle of the prop record.
3. The type index is 16 bits into an append-only table shared by the session: after 65,536 different prop types the index wraps and Write saves another prop's type.
4. Read never tests the record's version: anything is read as version 6; only "above 5" decides whether the timer byte is read.
5. A prop's record changes size over time: the 17 bytes of dates are there only while it has a date range, and the holder's properties vary.
6. Read overwrites a date range pointer without freeing the old one, and hands the stream a null address if the allocation fails.

Write and Read agree on every field's width.

## Test cities

The fingerprint's `propocc` part: the number of props (through the occupant manager's grid, each once, derived classes left out) and the sum and XOR of a hash per prop over its type, orientation, hours, date range, the dates' timer bit, chance, usage, lot tag, cells, and its state where only the simulation sets it. Left out because frames drive them: the state of timed and night props, the three other timer bits, two base flags, the position of the box (its size is kept). The decoder masks the same. `OPENSC4_TEST_PROPOCC=1` adds a checksum of the query slots on an eleventh of the props; `=2` adds counts of the frame-driven fields.

Before the first batch the original was run three times against itself on Rush Hour d0090 and Big City d0090: the part and the decoded records agreed.

## Parity

All runs on 2026-10-11 with the four `simulators.propocc.*` groups on and `OPENSC4_TEST_PROPOCC=1`, two games at a time, while other threads' games ran on the machine. Result folders under `%LOCALAPPDATA%\opensc4\parity\crime\`.

| Run | Result |
| --- | --- |
| 12 saves for 30 days (`po-all30`) | 8 of 12; Big City d0000 and d0090, Making Money d0365 and Rush Hour d0090 partly |
| 12 saves for 14 days, land and landfill zoned and dezoned (`po-zone14`) | 8 of 12; Big City d0090 and d0365, Making Money d0000 and Rush Hour d0000 partly |
| `saves-ports`, 30 days, zoning, growth asked and ports removed (`po-ports30`) | 3 of 3 |
| `saves-plopped`, 30 days, zoning (`po-pl30`) | 3 of 3 |
| 90 days on `saves-mm2` (`po-mm90`) | 2 of 2, all 91 days |

More saves came out partly than for the classes before, so every run of the eight was looked at. On six of them the original itself gave 2 to 4 outcomes in its 4 runs. In every one of the 64 runs, the original's and ours, the first difference from the original's first run is one of four things, all seen between runs of the original before: a stopwatch counter one or two off ([parity.md](parity.md)), the lots' checksum on Big City on its first day (the value it shows under machine load, [lotconfig.md](lotconfig.md)) or about ten days in ([citylot.md](citylot.md)), and the flammability part of Rush Hour d0090 on its eighth day. The prop part never differed on a day when the rest agreed.

Reached (28 of the 31 functions that log their first call): the constructor through the factory for props placed in a run and props read from a save, Write, the type table, SetState with its message, the timer bits and the draw, the hours and the dates with TickSimulatorDate, DoMessage, the position and box, Shutdown and the deleting destructor. Not reached: the allocator as a function of its own (the factory has its own copy; only derived classes call it), and the two helpers the terrain code and the decal use to put a prop in the occupant manager. The audit compared every function instruction by instruction, with all 60 direct call sites, and found no difference in behaviour. One assumption is not measured: SetPosition and the hours are computed in doubles, which equals the game where the FPU runs at 53-bit precision, as it does in the simulator's tick ([pollution.md](pollution.md)); SetPosition is also reached from frame-driven code of the derived classes.
