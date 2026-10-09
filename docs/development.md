# The building development simulator (cSC4BuildingDevelopmentSimulator)

The simulator behind growth: it knows every building, family, foundation and retaining-wall type and their exemplars, keeps the developers (one per developer type: residential, commercial and industrial by wealth) and runs their growth and decline queues, tracks construction resources, makes building and foundation occupants, and reacts to lots and terrain changing. Reimplemented after the residential simulator ([residential.md](residential.md)) the same way: written in four parts at once, then checked under the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/development.toml).

## The class

Class ID 0x8990BE7A. Object size 0xE4. Factory 0x0069A7A0 (`SC4::operator_new(0xE4)`, constructor 0x0069A000). Code 0x00695280 to 0x0069D400.

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AB0AF8 | `cISC4BuildingDevelopmentSimulator` (IID 0x07813EAE) | 60: QueryInterface 0x00695290, AddRef (shared 0x00695330), Release 0x006959B0, Init 0x0069D240, Shutdown 0x0069A210, the queries and operations (5-58), the deleting destructor 0x00699180 (59) |
| +0x4 | 0x00AB0AE8 | `cIGZMessageTarget` (IID 0xE98B2F57) | an empty DoMessage (shared) |
| +0x8 | 0x00AB0AD8 | `cIGZMessageTarget2` | DoMessage 0x0069CCE0 |
| +0xC | 0x00AB0AC0 | `cIGZSerializable` | Write 0x00697800, Read 0x0069ACB0, GetGZCLSID 0x00695280 |

gzcom-dll's `cISC4BuildingDevelopmentSimulator.h` lists the slots with several overloads in the wrong order. The vtable's real order, from the code: 6 makes a building at a cell (type, x, z, rotation), 7 at a position (type, position, rotation), 8 hands an occupant to the occupant manager (also slot 24); 10 is a demolition cost (int64 property 0x099AFACD), 12 lists the building types into an array; 22 is a foundation type's key; 42 and 43 are swapped; GetMaxCapacity (45) returns a double in ST0.

### Fields

`src/core/development_sim.h` names them. In short: +0x10/+0x11 loaded and initialised; +0x14 references; +0x18 to +0x38 the services (message server, city, occupant manager, resource manager, zone manager, lot manager, three more, the terrain); +0x3C to +0x44 the city's cell counts; +0x48 a bit grid; +0x4C a list of occupants; +0x50 a map of developer type to a stage byte; +0x5C the developers; +0x68, +0x6C two more services (the second flattens terrain under new buildings); +0x70 a `cSC4Demand`; +0x74 the queue state (0 idle, 1-4); +0x7C, +0x80 the growth and decline queues; +0x84, +0x90, +0xB0 trees; +0x9C a hash table of (developer, x, z, byte) to a number; +0xC0 to +0xD0 the tuning (capacity bounds 100 and 2000, a factor 0.5, a count 10, the stage multipliers); +0xDD to +0xE1 cheat flags (ignore power, ignore caps, coffee bar).

## The simulation

| Message | What happens |
| --- | --- |
| 0x66956810, 0x66956816 | when idle: the construction capacity from the region's populations times +0xC8, bounded by +0xC0 and +0xC4; every developer's slot 27; the queue state machine started |
| 0x66956814 | the state machine one step, then the day: the growth queue or the random pass, then the decline queue |
| 0x6695681A | the state machine one step (1 start, 2 wait for the developers' slot 28, 3 rebuild the growth queue, 4 rebuild the decline queue) |
| 0x278E6003 | lots in a changed rectangle: by kind, lots that still fit the ground are handed on, others demolished (cost charged to the budget) or dezoned |
| 0x99EF1142, 0x99EF1143 | qualifying occupants added to or removed from the list +0x4C |
| 0xE8BDA2E7 | an occupant's stage changed: its cells marked in the grid +0x48; a new highest stage for its developer type posts a notice |
| 0x230E27AC | the cheats (coffee bar, and two flags) |
| 0x0B99AE81 | a command: a building made at a position (slot 7) |

The save record is version 4 (Read takes only 4): the grid and demand references, the developers, the occupant list, the stage bytes, a process-wide map (0x00B485E0), the hash table, and three cheat bytes. `tools/re/save_schemas/development.py` decodes it exactly in all 15 corpus saves.

## What is ours

| Replacement | What is ours | File |
| --- | --- | --- |
| `simulators.development.queries` | slots 5, 10-21, 25, 27, 28, 30-34, 36-43, 45, 48-53, 55, 56, 58 | development_queries.cpp |
| `simulators.development.occupants` | slots 6-8, 22-24, 26, 29, 46, 47 | development_occupants.cpp |
| `simulators.development.messages` | DoMessage and everything it reaches; detours on three helpers other code calls too (0x00695260, and 0x00695F70 and 0x00696540, generic container functions the game shares with other classes) | development_messages.cpp |
| `simulators.development.init`, `.shutdown`, `.save`, `.load` | Init with the tuning, Shutdown, Write, Read | development_object.cpp |
| `simulators.development.object` | our own object: constructor, identity, our copies of the four vtables | development_object.cpp |

Still the game's: the slots shared with other classes (9, 35, 44, 54, 57), the member destruction our destructor calls (0x00698B30), two deeper helpers of Init (0x00695FD0, and the one-time table setup 0x0069C370), a few container helpers Read calls, the shared helper 0x00695390 (ours has a copy; other code still calls the game's), and generic container, string and property helpers. Where the game reads stack memory it never wrote, ours uses zeros.

## Test cities

In a week nothing grows on the test saves, so the building paths are first reached in 30-day runs. The fingerprint's `dev` part: the queue state, construction resources free and total (slots 48, 49), and the sizes of the occupant list, both queues, the trees, the stage map and the cell table. `OPENSC4_TEST_DEVELOPMENT_QUERIES=1` makes the queries no test city makes (the type lists, type and family checks, the stage multiplier, maximum capacities, a location check, demolition costs) each day and adds their checksum.

## Parity

All runs on 2026-10-09 with every `simulators.development.*` group on, two games at a time, other threads' games running too. Result folders under `%LOCALAPPDATA%\opensc4\parity\crime\`.

| Run | Result |
| --- | --- |
| 12 saves for a week (`dev-all1`) | first 1 of 12: the new save decoder returned pairs as tuples, which the comparison takes for references (heap addresses), so the original differed from itself; with lists, 11 of 12 and one B run off in the stopwatch counter; one B run crashed while loading (the known rare 0x0096DA20 crash), not repeated in 3 reruns |
| 30 days (`dev-30`) | 11 of 12 B runs crashed once buildings started growing: our hand-over of a new occupant to the occupant manager gave the game's early-pushed 0 to the occupant's slot 3 (which takes none) instead of to the manager's call, unbalancing the stack. Fixed; then an audit of every call's arguments in all four parts against the game's pushes and the callees' RET sizes found no other |
| 30 days after the fix (`dev-30c`) | 11 of 12; Rush Hour d0365 partly (one of four B runs parted on day 16 in the queue state; two reruns, `dev-rh365b-1`, `-2`, same) |
| 30 days with `OPENSC4_TEST_DEVELOPMENT_QUERIES=1` (`dev-q2`) | 11 of 12; one B run on Big City d0090 crashed inside the game's heap allocator on day 14; three reruns (`dev-bc90q-1` to `-3`) same. A first version of the switch also called the slots that take game objects (costs, location checks, maximum capacities) with made-up arguments and crashed the original too; those were taken out |
| `saves-plopped`, 30 days (`dev-pl`) | 3 of 3 |
| 90 days on `saves-mm2` (`dev-mm90`) | 2 of 2 |
| 30 days on main with the budget simulator merged (`dev-main`) | 9 of 12; Big City d0000 and Rush Hour d0090 partly (the original gives 2 and 4 outcomes there), Big City d1095 partly with a pollution difference on day 19 in 2 of 4 B runs: the original against itself (`ctl-bc1095`, 5 pairs) reaches the same outcome in 4 of 10 runs |

Our object was made in every B run; the development records were identical wherever a pair was the same. Not reached: building at a cell (slot 6) and plopping (slots 46, 47), foundation keys, the cheats, the two name queries, and filtered type lists.
