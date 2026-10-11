# The construction occupant (cSC4ConstructionOccupant)

The construction site that stands on a lot while its building goes up: scaffolding, a crane, props and dust. One object per building under construction (a handful at a time), made by the lot developer's StartConstruction ([lot.md](lot.md)). Nearly all of the class is drawing and animation, which another thread owns and which stays the game's here; what is ours is the part that decides when the building is finished, the save record and the object.

## The class

Class ID 0xA97F909E, IID 0x496E7636, occupant type 0x096E763C. 0x1A0 bytes from a pool (0x00B0A808, 32 KB pages; allocator 0x005CA8A0). Constructor 0x005D00F0, factory 0x005D0BC0, deleting destructor 0x005D0C00. Code 0x005CA7A0 to 0x005D1511, about 27 KB, most of it geometry, textures, the crane, props and effects. Eight vtables: its own interface (0x00AA2250: SetLot), the message target (0x00AA2240), the occupant (0x00AA21B8), serializable (0x00AA219C: Write 0x005CD250, Read 0x005CE770), the two holders, and a model part with a virtual base (0x00AA20D0 at +0x19C), which is what the renderer holds. Underneath is the occupant base class ([occbase.md](occbase.md)). Build 641 addresses throughout (data/addresses/constrocc.toml; fields and vtables with their evidence in src/core/constrocc_sim.h).

## The schedule

Construction progress goes by game days, not frames. SetLot (0x005D0890) takes the lot and its building, has the drawing code set the site up, and lays out a schedule from the building's profile and its number of floors: days for the first stage, a pause per floor, days for the last stage, and what is left divided over the floors. On the simulator's daily message a step (0x005D0680) compares the day with the next deadline and moves through the stages: the groundwork, floor after floor, the finish. The last stage takes the site out of the occupant manager, sets the lot to its built state and shuts the site down. Only what the site looks like in between (and the building's completion percentage, [bldgocc.md](bldgocc.md)) follows frames.

The save record is version 5 (Read takes 4 too): the base's part, the lot and the building, the transform and two boxes, the props and effects, then the schedule (stage, days, floors, heights). `tools/re/save_schemas/constrocc.py` decodes every site in the corpus saves (118 objects in 18 records); what the drawing's setup makes with the class's random generator (the prop and effect lists, the crane) and the frame-driven fields are masked.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.constrocc.slots` | QueryInterface and its six thunks (the virtual base's too), SetLot with the schedule, GetPosition, GetBoundingBox, GetGZCLSID; a detour on the daily step |
| `simulators.constrocc.save` | Write and Read and their holder thunks |
| `simulators.constrocc.object` | our own site: detours on the constructor, the pool's allocator and the factory, the deleting destructor, copies of the eight vtables taken once |

SetLot and the step call the drawing helpers at their addresses in the game's order. Still the game's: Init, Shutdown, DoMessage, the frame animation and every drawing function. Copied on purpose from the game and commented in the code: the days of a stage are the smaller of a rounded value and 1 (so 0 or 1); Read reuses a loop counter's stack slot for two flags.

## Test cities

The fingerprint's `constrocc` part: the number of sites and the sum and XOR of a hash per site over its stage, days, floors, heights, boxes and the base's saved state. `OPENSC4_TEST_CONSTROCC=1` adds a checksum of the query slots on half the sites. Before the first batch the original was run three times against itself on Rush Hour d0090 and Big City d0365 with zoning: the part and the decoded records agreed. The zoning runs have up to four sites at a time.

## Parity

Run together with the retaining wall occupant; the table is in [wallocc.md](wallocc.md#parity): 12 of 12 saves with zoning, 11 of 11 compared over 30 days, 3 of 3 on `saves-ports` and `saves-plopped`, 2 of 2 over 90 days. Reached: 11 of the 12 functions (sites made through the factory, SetLot with the schedule, the step through every stage to the finished building, Read, Write, the queries, the deleting destructor). Not reached: the allocator as a function of its own (StartConstruction's fallback when the class factory gives nothing). The audit compared every function instruction by instruction, the virtual base's handling included, and found no difference in behaviour.
