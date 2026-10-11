# The fire occupant (cSC4FireOccupant)

A fire: one object on each burning building, lot object or tree. The fire disaster's instance makes them, heats them, steps their stage and takes them away ([disasters.md](disasters.md)); firefighters douse them. The object holds its target, a heat, a stage and the number of stages, how often it was hosed, and its flames; what the flames look like is effects and model work, which another thread owns and which stays the game's. Ours is the rest, on top of the occupant base class ([occbase.md](occbase.md)). Reimplemented like the other occupant classes: one writer, an independent audit against the disassembly, the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/fireocc.toml).

## The class

Class ID 0x29D25F1D, IID 0x69D25C60, occupant type 0x69D25C5F. 0x6C bytes from the game's operator new (no pool). Constructor 0x004711C0 (from the factory 0x004715E0 when a save is read, and from the fire instance), destructor 0x00471290. Code in pieces between 0x0046FDC0 and 0x004728F5. No class derives from it.

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00A88460 | the fire's | 15: SetTarget, SetStage, SetHosed; the getters and SetHeat, SetFamily are stubs folded with other classes |
| +0x4 | 0x00A883D8 | `cISC4Occupant` | 34, most of them the base's; the class's own: Init, Shutdown, GetPosition, GetBoundingBox |
| +0x8 | 0x00A883BC | `cIGZSerializable` | Write 0x00470130, Read 0x00471A70 |
| +0xC, +0x20 | 0x00A88358, 0x00A88344 | the property and exemplar holders | shared code |

Fields: the base at +0x04; +0x38, +0x3C two flame numbers (from the target's box through two curves); +0x40 the heat; +0x44 the stage (-1 at first); +0x48 the number of stages (the target's property 0x49BEDA31, else 2); +0x4C the hosed count; +0x50 an end time; +0x54 the target (referenced); +0x58 the flames (each 0x50 bytes: a kind, a quarter turn, a spot, a transform, its effect); +0x68 the family.

SetTarget takes the reference, reads the stage count, works out the two flame numbers, marks the target as burning (a property and a removal flag), and for a building names it to the automata controller manager. SetStage places the flames the first time and starts each flame's effect for the stage. Shutdown stops and releases the effects, takes the mark and the controllers off the target and lets it go. The position and box are the target's. CheckBurnOut (0x0046FE00) compares a time with the end time for the fire instance.

The save record is version 3: the base's part, heat, stage, stage count, the target and the flames. `tools/re/save_schemas/fireocc.py` decodes it (no corpus save has a fire; 40 saves of earlier fire runs decode to the last byte); the flames are read and not compared.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.fireocc.slots` | 13 slots: QueryInterface and its thunks, SetTarget, SetStage, SetHosed, Init, Shutdown, GetPosition, GetBoundingBox, GetGZCLSID; a detour on CheckBurnOut |
| `simulators.fireocc.save` | Write and Read with the flames' records, and their holder thunks |
| `simulators.fireocc.object` | our own fire: detours on the constructor and the factory, the destructor, copies of the five vtables taken once |

Still the game's: where the flames go (0x00472380, with the walk over the target's model 0x00471CF0), a flame's effect and transform (0x004710D0, 0x00470CA0), the view's two entry points (0x00470250, 0x00472790); ours calls them at their addresses where the game does. Also the folded stubs and the base class. Copied on purpose from the game and commented in the code: SetStage has no bounds test; the family of a fire read from a save is what the heap held; SetTarget, Shutdown and Read do not test the target for null.

## Test cities

No test city burns, so the fire switches of the disaster work start one ([disasters.md](disasters.md)): `OPENSC4_TEST_DISASTER=1` with `OPENSC4_TEST_DISASTER_TYPE=1`, the instance pushed by `OPENSC4_TEST_FIRE_PUSH=100`, the fire part called as tools do by `OPENSC4_TEST_FIRE_CALLS=2`, and `OPENSC4_TEST_TUTORIAL_OFF=1` (tutorial cities do not burn down). The fingerprint's `fireocc` part: the number of fires and the sum and XOR of a hash per fire over the two flame numbers, heat, stage, stage count, the target's type, the box and the base's saved state. Left out: the flames (the view's), the hosed count, the end time's value, the family. `OPENSC4_TEST_FIREOCC=1` adds a checksum of the query slots on half the fires.

With a fire under way the original does not always repeat itself (the disaster work found two or three outcomes on some saves), so verdicts go by outcome sets.

## Parity

All runs on 2026-10-11 with the three `simulators.fireocc.*` groups on and `OPENSC4_TEST_FIREOCC=1`, two games at a time (`%LOCALAPPDATA%\opensc4\parity\crime\`). The fire manager and instance were the game's in these runs.

| Run | Result |
| --- | --- |
| 12 saves for a week, a fire started on day 1, pushed by 100, the fire part's calls on day 2, tutorial off (`fi-fire7`) | 11 of 12; Big City d0000 partly (the original 3 outcomes; runs on both sides differ on the first day in the lots' checksum, as that city does under machine load, [lotconfig.md](lotconfig.md)) |
| `saves-plopped`, 14 days, the same fire switches (`fi-pl14`) | 3 of 3 |
| `saves-fire`: two Rush Hour saves taken by the original with three fires burning, a week, pushed by 100 (`fi-load7`) | f0002 same; f0003 partly: the original 3 outcomes in 4 runs, ours 3 with one shared; every run parts in the night switch of timed props, the weather or the flammability part, none in the fires |
| 12 saves for 14 days, no fire (`fi-all14`) | 4 of 12. The class does nothing in these runs (no fire, none of its functions called). Every miss is a stopwatch counter one off or Big City's and Making Money's lots' checksum, as in other batches of this day on a busy machine; the original gave 2 to 4 outcomes on five of the eight |

Reached: 14 of the 15 functions that log their first call: fires made by the instance (the constructor, Init, SetTarget, SetStage, CheckBurnOut, the position and box, Shutdown, the destructor) and fires read from a save and written again (the factory, Read with the flames' records, Write). Not reached: SetHosed (no fire engine arrives in these runs). The audit compared every function instruction by instruction and found no difference in behaviour.
