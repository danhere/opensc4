# The lot developer (cSC4LotDeveloper)

Builds and unbuilds lots: it starts a construction on a lot (the building, its foundation, the base textures, props and flora of the lot's configuration), levels the ground and adds retaining walls, changes and abandons buildings, demolishes and explodes lots, keeps the lots' zots (no power, no water, no road access), their business units and travel designations, and steps the buildings' night lights. The tract developer ([tract.md](tract.md)) and the port developers ([ports.md](ports.md)) hand it the lots they make. Reimplemented like the other developers: four writers by address range, every virtual call's argument count checked against the callee's own code, an audit of how the parts call each other, then the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/lot.toml).

## The class

Class ID 0xA990BFE0. Object size 0x108. Constructor 0x006C8650, destructor 0x006C7280, factory 0x006C8DF0. Code 0x006C3B00 to 0x006CDEED. One per city (the city's slot 79, global 0x00B43D20).

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AB1B98 | `cISC4LotDeveloper` (IID 0x4818E5D2) | 66: QueryInterface, AddRef (shared), Release, Init 0x006CB9A0, Shutdown 0x006C7380, then the operations and queries (5-64), the deleting destructor |
| +0x4 | 0x00AB1B88 | `cIGZMessageTarget2` | DoMessage 0x006CD940 |
| +0x8 | 0x00AB1B70 | `cIGZSerializable` | Write 0x006C66F0, Read 0x006CD120, GetGZCLSID 0x006C3B00 |

Fields (src/core/lot_sim.h has the list with its evidence): +0x14 to +0x58 the services; +0x40 to +0x48 the terrain's size; +0x5C to +0x60 flags (force empty base textures, what the lot update takes, two set by messages); +0x64 a deque of the building occupants and +0x8C to +0xA8 the night lights' step through it; +0xAC to +0xBC the lot update's state; +0xC0 to +0xEC the tuning (flatness tolerance, the slope above which a building gets a foundation, the levelling limit, a cost curve); +0xFC the boundary textures.

The save record is version 1: the terrain's size, the list of buildings, the night step's position and two bytes. `tools/re/save_schemas/lot.py` decodes it in every corpus save.

## What is ours

| Replacement | What is ours | File |
| --- | --- | --- |
| `simulators.lot.slots` | 20 slots: abandoning and reoccupying a building, foundations of lots and buildings, base texture occupants, levelling, the position for a building; eight helpers the other parts call (lot condition, the land value counter, turning a lot to the road, debris) | lot_object.cpp |
| `simulators.lot.init`, `.shutdown`, `.messages`, `.save` | Init with the tuning, Shutdown, DoMessage, Write and Read | lot_object.cpp |
| `simulators.lot.object` | our own object: a detour on the constructor, the destructor, identity, copies of the three vtables | lot_object.cpp |
| `simulators.lot.terrain` | 20 slots: ChangeBuilding, the altitude, flatness, range and slope queries of areas and lots, the development cost, the position of a lot configuration's object, the boundary texture, foundations by configuration, lot props, travel populations | lot_terrain.cpp |
| `simulators.lot.build` | 9 slots and 17 helpers: ending a demolition, business units, anchor buildings, the first building, foundations by cell and position, the normal base texture; the day and night deque, zots and the power, water and access checks, a lot's occupants after the ground moved, network pieces, flora removal | lot_build.cpp |
| `simulators.lot.construct` | 9 slots and 5 helpers: StartConstruction, ConstructDefaultLot, starting a demolition, ExplodeLot, the empty base texture, a lot's elements, travel designates; the night lights' steps, power in a rectangle, a lot's flora, the lot update's step | lot_construct.cpp |

Functions one part calls in another are detoured by their owner (30 in all), so each group works alone or with the others. One of them (0x006C3B10) takes its arguments in registers and goes through a naked thunk. Still the game's: the shared AddRef, a deleting destructor forty filter classes share, the deque, list, tree and shuffle code the compiler placed in the range, the retaining walls' helper, the occupants' allocators and constructors. Copied on purpose from the game and commented in the code: null dereferences the game also has, stack slots the game reads without writing (zeros in ours), LoadTuning loading one property into the night step's millisecond counter.

## Test cities

The fingerprint's `lot` part: the flags, the building count, the lot update's state, the boundary textures' count and a hash of the terrain size. `OPENSC4_TEST_LOT=1` adds queries no city run makes (altitudes at a lattice of vertices, and for the lots at 16 cells whether each has a foundation and where its building stands); `OPENSC4_TEST_LOT=2` the timer-driven night step. Construction and decline run in every 30-day test; `saves-ports` with `OPENSC4_TEST_GROW_PORTS` and `OPENSC4_TEST_REMOVE_PORTS` adds lots built and changed by the port developers.

## Parity

All runs on 2026-10-10 with every `simulators.lot.*` group on and `OPENSC4_TEST_LOT=1`, two games at a time. Result folders under `%LOCALAPPDATA%\opensc4\parity\crime\`.

| Run | Result |
| --- | --- |
| 12 saves for 30 days (`lot-all30`) | 9 of 12 on the first pair. Big City d0000 partly (the original reached 4 outcomes in 4 runs) and d0365 partly (one B run off in the stopwatch counter). On Rush Hour d0090 the B run hit the rare game crash at 0x0096DA20 on its 19th day, the same save and day as the one in the tract developer's runs ([tract.md](tract.md)); this time no code of ours was on the stack. Six single runs of that save with the lot groups on and six without (`one/rh90-*`): no crash |
| `saves-ports`, 30 days, with growth asked and the ports removed (`lot-ports30`) | 3 of 3, all on the first pair |
| `saves-plopped`, 30 days (`lot-pl30`) | 3 of 3, all on the first pair |
| 90 days on `saves-mm2` (`lot-mm90`) | d0090 same on the second pair (the original reached 2 outcomes); d0365 partly: the first B run parted on its second day in the flammability part, the three retries same |
| 12 saves for a week after the audit's fix (`lot-fix7`) | 10 of 12; Big City d0090 and Making Money d0000 partly, each with only the first B run off from the second day in the flammability part, the three retries same |

The audit of how the parts call each other found one mismatch before the PR: 0x006C3C50 returns the lot's slot 11 result and StartDemolishLot passes it on; our copy returned nothing. Fixed.

A first B run parting alone in the flammability part, with the retries agreeing, has now shown up in the ports, prop and lot batches alike, and in a control whose B side had no replacement at all (`pt-b-ctl`, Rush Hour d1095): it comes from the harness or that part, not from the code under test ([parity.md](parity.md)).

Reached in these runs (83 of the 104 functions that log their first call): Init with the tuning, the messages, construction of grown lots (StartConstruction, the base textures, props, flora, network pieces, foundations, levelling and retaining walls), the altitude and slope queries, the lot update with the power, water and access zots, demolition, reoccupying a building, business units and travel designates, saving and loading. Not reached: the night lights' steps (they follow a timer), ChangeBuilding, AbandonBuilding, ExplodeLot and its debris (disasters), ConstructDefaultLot, the special lot of the CoffeeBar cheat.
