# The tract developer (cSC4TractDeveloper)

The class that decides what grows where: for one growth developer (residential, commercial or industrial by wealth, or agriculture) and one tract, it fills existing buildings, finds lots, merges or splits them, picks a lot configuration and a building of the right style, and develops it. It also declines buildings, keeps the population of a tract's lots, and holds the building styles (the active styles, one style at a time or all at once, the years between changes). The building development simulator ([development.md](development.md)) calls it for every queued tract. Mods patch it mid-function (sc4-more-building-styles, the lot scoring test DLL; [patch-census.md](patch-census.md)); ours is the vanilla game. Build 641 addresses throughout (data/addresses/tract.toml).

Reimplemented like the growth developers ([growth.md](growth.md)): four parts written at once (Grow, lots, buildings, object), each part's calls audited against the game's pushes and RET sizes, and checked under the parity harness ([parity.md](parity.md)).

## The class

Class ID 0x2990C142. Object size 0x188. Constructor 0x0070C900, destructor 0x0070C750, factory 0x0070CAA0. Code 0x007045A0 to 0x0070D7D0 (118 functions). One object per city (the city's slot 80).

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AB3118 | `cISC4TractDeveloper` | 16: QueryInterface, AddRef (shared), Release, Init 0x00707780, Shutdown 0x0070B210, Grow 0x0070D130, Decline 0x00709200, UpdatePopulation 0x00706BC0, the style slots (8-12, 14; 13 is shared), the deleting destructor |
| +0x4 | 0x00AB3104 | `cIGZMessageTarget2` | DoMessage 0x007048E0 (message 0x66956817: the style year counter) |
| +0x8 | 0x00AB30EC | `cIGZSerializable` | Write 0x00704B60, Read 0x00707950, GetGZCLSID 0x007045A0 |

The four source files list the fields they use with their evidence. In short: +0x14 to +0x3C the services; +0x40 to +0x6C the tuning (exemplar 0x6534284A/0xE7E2C2DB/0xE8DA7677, properties 0x47E2C500 to 0x47E2C551); +0x70 the tuning styles, +0x7C the years between style changes; +0x80 to +0x117 the state of one Grow call (the growth developer, the tract, its demand and capacity, the lots of the tract, the configurations that pass, the candidates, the chosen configuration and building, the outcome cache per lot); +0x118 the active styles, +0x124 the current style, +0x128 years since the last change, +0x12C one style at a time; +0x130 the configuration filters; +0x13C the attempt record (date, tract, counters; +0x16C its list of attempts, +0x170 13 rows of 28 outcome counters); +0x17C the records kept for CheatDebugGrowth.

## What a Grow call does

1. Caches the growth developer's construction info, the tract's capacity and the demand.
2. Repeats passes until one decides it is done. Each pass collects the tract's lots and tries, in order: filling existing buildings with room (0x0070A3D0, ends Grow on success); the lot configurations the developer may use (0x00709650); a new building on one lot of the configuration's exact size (0x00708110), a bigger one over several lots (0x007097E0), smaller ones side by side on one lot (0x00708C00, not for agriculture). Candidates are scored (0x00705750), sorted, and developed in order (0x0070B440: choose a configuration and building of the current style and capacity range (0x00709BA0), then rebuild the lot, replace the lots or subdivide; 0x0070B640 for farms, which floods out over fields). Every outcome is counted and, when the building development simulator asks, recorded (0x0070AD20).
3. Returns the number of buildings grown.

Decline (slot 6) takes the tract's lots down by the developer's decline list and returns the capacity removed; UpdatePopulation (slot 7) sets the population of the tract's lots from the developer's population grid, with a per-tract random draw.

The save record is version 3 (Read takes 0 to 3; both get the object + 8): the current style, the years since the last change, then (version 2 on) one style at a time and the active styles, then (version 3) the years between changes. `tools/re/save_schemas/tract.py` decodes it in all 17 corpus saves.

## What is ours

| Replacement | What is ours | File |
| --- | --- | --- |
| `simulators.tract.grow` | Grow (slot 5) and its helpers: the passes, the sort of the candidates, the attempt record and outcome table, the CheatDebugGrowth records | tract_grow.cpp |
| `simulators.tract.lots` | filling existing buildings, the three lot searches, the lot check and its cache, fitting a configuration, the scoring, the record | tract_lots.cpp |
| `simulators.tract.buildings` | the lot configurations and their filters, the building choice (style, configuration, capacity range, building type, monopoly pieces), developing a lot (rebuild, replace, subdivide) and farms | tract_buildings.cpp |
| `simulators.tract.init`, `.shutdown`, `.save`, `.load`, `.messages`, `.styles`, `.decline` | Init with the tuning, Shutdown, Write, Read, DoMessage, the style slots, Decline and UpdatePopulation | tract_object.cpp |
| `simulators.tract.object` | our own object: a detour on the constructor, the destructor, identity, our copies of the three vtables | tract_object.cpp |

Functions another part calls are detoured by the part that owns them (33 in all, each called only from inside the class), so the parts work alone or together; no caller relies on registers across those calls. Still the game's: the shared slot 13, STL container and tree helpers other classes share, the random generators and the tract iterator. Copied on purpose from the game and commented in the code: unchecked reads in SetActiveStyles and Read, the uninitialised stack the game passes in a few places (ours passes zeros), a division by a configuration's width that crashes the game too when it is 0.

## Test cities

The fingerprint's `tract` part: the current style (slot 8), the style index, the years since the last change, one style at a time, the years between changes and the active styles. Growth itself shows in the population and lot counts every fingerprint has, and in the building development simulator's part.

## Parity

All runs on 2026-10-09, two games at a time, other threads' games running too. Result folders under `%LOCALAPPDATA%\opensc4\parity\crime\`.

| Run | Result |
| --- | --- |
| everything, 12 saves for 30 days (`tr-all30`) | 1 of 12: in the other 11 the game with our code stopped without a crash report at the first building choice. Every group alone and the pairs tried were identical on Rush Hour d0365 for 20 days, all but the buildings part too; the buildings part alone stopped. Traces found it: the game's hash-set insert (0x00624DD0) writes a 9-byte result (node, table, a bool) and ours gave it 8 bytes, so it overwrote our stack. Fixed, with the same latent bug in the flora and traffic code |
| everything after the fix, 12 saves for 30 days (`tr-all30b`) | 11 of 12, tract records identical. Rush Hour d0090 (where the original reaches 2 to 3 outcomes) differed on the first pairs, and its third B run crashed at 0x0096DA20, the rare game crash seen before in B runs ([parity.md](parity.md)); this time the stack went through our building choice into the game's exemplar style check (0x005E8F70). Three repeats of that save (`tr-rh90-1` to `-3`): no crash, verdicts same, partly (the original 3 outcomes, B 4, shared 1) and same |
| `saves-plopped`, 30 days (`tr-pl30`) | 3 of 3 |
| 90 days on `saves-mm2` (`tr-mm90`) | 2 of 2 |
| everything on main with the terrain renderer merged, 12 saves for a week (`tr-main7`) | 12 of 12, all on the first pair |

Our object was made in every B run. Each group alone, and the pairs grow with lots, object and shutdown, and lots with object, were also identical on Rush Hour d0365 for 20 days.
