# The terrain

The city's land and water: one object, `cSTETerrain` (interfaces `cISTETerrain : cISTETerrainMap` in gzcom-dll), that every simulator and the renderer ask for heights, slopes and water, and that the terraforming tools, disasters and the sea level change. Its picture on screen is another class, the terrain view (`cSTETerrainView3D`, [renderer.md](renderer.md), "The terrain"), which is not covered here.

## The class

- The object is 0x1E0 bytes, class ID 0xE98F9525, made only by its factory 0x0074D580 (constructor 0x0074C380, destructor 0x0074C740). Main vtable 0x00AB3D18, 98 slots; at +4 the message target (0x00AB3D08: DoMessage 0x0074A470); at +8 the serializable part (0x00AB3CF0: Write 0x00745300, Read 0x00745380, GetGZCLSID). Code about 0x00740060 to 0x00751000, 259 functions, 66 KB.
- Seven slots are small getters shared by code folding with unrelated classes (5, 7, 8, 29, 42, 54, 83); ours are swapped in this vtable only.
- The header's slot order is wrong in places: `GetWaterLevel(x, z)` is slot 30 and `GetWaterLevel(index)` slot 31; `CellWidth` (slot 6) returns a float.

The object, as far as the queries show it ([src/core/terrain_sim.h](../src/core/terrain_sim.h)):

| Offset | What |
|---|---|
| +0x0C | initialized (byte) |
| +0x18 | terrain type |
| +0x1C | the city |
| +0x24 | the water object; its +0x18 is the water record array |
| +0x28, +0x2C | cells along x and z; +0x30, +0x34 the same less one (clamps) |
| +0x38, +0x3C | vertices per row and rows (cells + 1); +0x5C vertex count |
| +0x50, +0x54 | the size in metres (cells x 16) |
| +0x6C | the vertex array, 36-byte records: +0 altitude, +4 normal, +0x22 a flags word (0x20 and 0x40 the cell's two triangles hidden, 0x1000 and 0x2000 the triangulation flipped, 0x4000 locked) |
| +0x70, +0x74 | least and greatest height |
| +0x7C | sea level |
| +0xC0, +0xC4 | texture updates being gathered, and their rectangle |
| +0xDD | the water surface shown |
| +0xE4 | the number of water vertices |
| +0xE8 | the view |

The water records are 36 bytes too, at the same index: +0 the depth (above 0 is water), +0x1C the sediment.

## The queries (`simulators.terrain.queries`)

54 slots that only read ([src/core/terrain_queries.cpp](../src/core/terrain_queries.cpp)): the grid's size and conversions (5 to 11), the altitude in every form (12 to 20, 25), the height limits (21 to 24), normals (26 to 28), sea and water levels and depths (29 to 33, 92), the land and water tests (34 to 39), the type, counts and sediment (42, 47, 53 to 55), the brush tables (58, 61 to 63), the triangulation, hidden and flag tests (70, 74 to 77, 82) and three display states (80, 83, 85). No code outside the class calls any of them directly, so there are no detours; the slots are swapped in the vtable after each is checked to hold the original.

These are among the most called functions in the game, and their float results feed everything, so they follow the game's x87 arithmetic statement by statement: double where the game keeps a value on the FPU stack, float where it stores and reloads. Things that are easy to get wrong:

- **Floor.** The cell of a position is not `floor(x / 16)` but the game's own idiom: `t - 0.5 + 12582912.0` stored as a float, 0x4B400000 taken off its bits, one added when `t` equals the result plus one.
- **What comes back in ST0.** `GetAltitude(x, z)` (slot 13), the checksum (20), the greatest surface level (18) and the two water levels (30, 31) return the unrounded double; the others a float. Callers exist that read slot 13 both ways.
- **No guards the game does not have.** `GetAltitudes`, `GetWaterDepths` and the water vertex count of a rectangle test no range; the lerped normal and lerped water level read past the row at the far edges; the face normal lets a NaN through. Ours do the same.

Slot 65 (`IsGoodWaveProducerLocation`) only reads too but is left with the wave producers: it uses the x87 sine and cosine, which our code cannot repeat bit for bit.

## The setters (`simulators.terrain.edit`)

24 slots that change the terrain's own state ([src/core/terrain_edit.cpp](../src/core/terrain_edit.cpp)), swapped in the vtable like the queries; none is called directly from outside the class.

| Slots | What they do |
|---|---|
| 49, 50, 51 | `SetAltitude` for a rectangle, an index and a vertex. A locked vertex (flag 0x4000) is left alone. By index the height is stored unclamped (the game compares it only with itself); a wet vertex keeps its water surface, a dry one gets water only when it ends below the sea. By vertex the height is raised or lowered towards the target and clamped to the allowed range. The rectangle form goes through slot 50 vertex by vertex, then sets the water and asks for a redisplay |
| 52 | `LockAltitude`: flag 0x4000 over a rectangle, no range test |
| 71, 72, 73, 81, 93, 94 | the hidden-triangle bits (0x20, 0x40), the cell flags, the two triangulation bits (0x1000, 0x2000); the view is told |
| 41 | `SetTerrainType`: message 0xC922EB91, the whole map refreshed |
| 45, 46 | `SetSeaLevel`, `ChangeSeaLevel`: refused outside -10.1 to 200.1 (the cities' own sea level, 250, is outside it, so in a normal city every step is refused); otherwise every water record is reset to sea less altitude and message 0x89171496 goes out |
| 48 | `ChangeTerrainLevel`: every unlocked vertex up or down by an amount, clamped; water follows |
| 43, 44 | `CreateFlatAreas`, `CreateShoreCliffs`: the loops and flags are ours; the flattening, spreading and smoothing they call are the brushes' machinery, still the game's |
| 78, 79 | texture updates gathered into one rectangle, refreshed at the end |
| 84, 86 | the two display states (message 0x6AB97795 for the water surface) |
| 87, 88 | long terrain operation begin and end (messages 0xEA3B0AF1, 0x6A3B0AFD) |
| 59, 60 | brush scrub begin and end: the scrubbed set emptied, the brush's sounds, the water object told at the end |

Calls into the view, the redisplay (slot 68) and the water object stay calls.

## The shaping machinery (`simulators.terrain.brush`, first part)

What flat areas, shore cliffs and the brushes share ([src/core/terrain_brush.cpp](../src/core/terrain_brush.cpp)): slot 57 `ApplyBrush`'s own code (the brush looked up, the body called, the applied rectangle redisplayed) and, by detours because the game's remaining code calls them directly, ten internal functions and five small callbacks:

| Address | What it does |
|---|---|
| 0x00749660 | flatten around a vertex: a flood fill over straight neighbours that are unflagged, above the sea and within a tolerance of the start's height; more than 401 gathered are all set to that height, fewer are let go |
| 0x00743210 | spread marks outward from marked vertices by a distance, with two callbacks deciding |
| 0x00745020 | smooth a rectangle: for each vertex and each of its 8 neighbours, the excess of the height difference over a limit (one for straight, one for diagonal) times a factor moves from the higher to the lower; locks, clamps, water follows |
| 0x007409B0, 0x00740A20, 0x00741DE0 | clear flags in a rectangle, the bounds of flagged vertices, flag vertices by a callback |
| 0x00740F70 | recompute the least and greatest height |
| 0x0074AD20, 0x0074AB60 | walk the shore line both ways from a vertex, gathering it into a deque |
| 0x00749FA0 | turn a gathered shore line into cliffs (with 0x00741680 and 0x00744220 inside it); the only random draws here, from the generator at 0x00B4BC78, in the game's order |

Two of the detours go through thunks that keep ECX, which callers at 0x0074215E, 0x0074290D, 0x00748E6E and 0x0074C31A rely on. With this group on, piece 2's flat areas and shore cliffs run on our code all the way down.

Still the game's under the brushes: the brush body 0x0074D730 and its per-layer routines (they use the x87 sine, cosine and arctangent), the brush loader, erosion and the flood.

## Init, messages, save and load, the object

Four more groups ([terrain_object.cpp](../src/core/terrain_object.cpp), [terrain_messages.cpp](../src/core/terrain_messages.cpp), [terrain_save.cpp](../src/core/terrain_save.cpp)), each switchable alone.

**`simulators.terrain.init`** (slots 3, 4, 40). Init: the brush table loaded once (the loader stays the game's); the city and the water object taken; the grid's size from the city's `CellCountX` and `CellCountZ` and everything derived from it; the rows of one byte per vertex (+0x190: the distance to water); the sea level from a global; the eight neighbour offsets (+0x84); the vertex array (36 bytes a vertex, zeroed) unless a load already made it; two vectors of 2 x cells; eight random phases (+0x1B4) drawn from the generator at 0x00B4BC78; ten message subscriptions, seven cheat commands, the simulator and timer registrations; and for a city with no saved terrain the water object's Init and 0x00748130 (the game's). Shutdown undoes it in mirror order. DoPostCityInit runs once: the zoom from the camera, the view made (the Renderer's class) and told, the whole map refreshed.

**`simulators.terrain.messages`** (DoMessage, and slot 89). The messages: 0x26D31EC1 post city init; 0x470BD214 erosion (kind 0 or 1, then a redisplay); 0x86AD10EE the timer (see below); 0xA6B79602 the zoom changed; 0x2A612643, which the terrain posts to itself to reconcile its edges with the neighbouring cities once no dialog is open; seven cheat commands 0x0C4A6AF0 to 6 (get and set altitude, by position and by cell, get and set sea level, apply a brush; one game bug kept: set altitude passes the x cell as both coordinates); two that do nothing. The timer tick counts to 2 and then starts the edge reconciling, steps the flood while one runs, expires the pending rectangles, and every 10,000,000 units of elapsed time with a changed rectangle refills the water distance map. `ReconcileEdgesWithNeighboringCities` is ours as orchestration; its workers and dialogs are calls.

**`simulators.terrain.save`**. The object's own record (type 0xE98F9525, version 2) holds only a reference to the view. The land is in headerless records of group 0xE98F9525: the header 0x8A91E7E3 (vertex counts, terrain type, least and greatest height, one more float), the altitudes 0xA9DD6FF4 instance 1 (a float per vertex), the flags 0x8A91E7E0 (a word per vertex), and four edge strips 0xA9DD6FF4 instances 0x64 to 0x67 for the neighbours. `tools/re/save_schemas/terrain.py` decodes them: 96 records in the 12 corpus saves, every byte read. `SaveObliterated` (slot 90) keeps only two flag bits on every vertex and saves.

**`simulators.terrain.object`**: our own object behind the game's factory: a detour on the constructor, copies of the three vtables taken at the first construction (so the other groups' swaps are in them), our QueryInterface, AddRef, Release, destructor.

For bigger city tiles ([city-size.md](city-size.md)): the size comes only from the city (`CellCountX` / `CellCountZ` in Init at 0x0074FEFC and 0x0074FF0A); none of Init, Shutdown, the messages, save and load or the object has a constant sized for 256 cells. Three things to mind there: a load takes its vertex counts from the file and Init then overwrites the counts from the city but keeps the loaded array, so a save whose grid is not the city's is overrun; the distance rows hold one byte per vertex (a distance, not a coordinate); indices are int32 times 36.

## Tests

The fingerprint part `terrain` reads the object's memory directly: the grid, the cell width, the sea level, the height limits, and checksums of every altitude, flags word, water depth and sediment, and normal, plus the water vertex count. Any change of the land between two runs shows there.

`OPENSC4_TEST_TERRAIN=1` compares, inside one run, every one of the 54 slots as the object has it with the game's own function at its address, bit for bit (floats by their bits, ST0 as a double, whole out-buffers): the getters; vertices and cells on every edge, a hashed part of the interior and outside on every side; about 4,600 positions a day on vertices, lines, diagonals and half cells; rectangles inside, empty, reversed and over the edge. It prints `q<agree>/<asked>`. It does not ask what would make the game itself read outside its arrays (the lerped slots on the far edges, rectangles outside the grid for the slots without a range test).

`OPENSC4_TEST_TERRAIN_EDIT=<n>` runs, on the n-th day, 46 calls of the setters on the live terrain in a fixed order at a place picked from the terrain itself: raise and lower by all three forms, under water, on locked vertices, hide and show, flags, gather and refresh, scrub begin and end, the display states; each call logs its answer and a checksum of the state. `OPENSC4_TEST_TERRAIN_SEA=<n>` does the whole-map ones: sea level refused and accepted, terrain level up and down, flat areas, shore cliffs. Both leave the land changed, so the `terrain` fingerprint part shows the result for the rest of the run.

`OPENSC4_TEST_TERRAIN_BRUSH=<n>` applies, on the n-th day, every brush kind at four places picked from the land (inland, shore, map edge, under water) at two strengths, three applications each, inside scrub begin and end as the terraform tool does, then flat areas and shore cliffs; each call logs its answer, the applied rectangle, a checksum of the whole map and the random generator's state.

The fingerprint part `terrainobj` prints the object's scalar state the `terrain` part does not cover (grid sizes, the type's float, the neighbour offsets, the vectors' sizes, the rows, the phases, the zoom, the brush table's size, the flooding byte). What the timer drives (the elapsed time, the tick count, the pending rectangles, the distance rows) follows real time and does not repeat between two runs of the original; it is printed only with `OPENSC4_TEST_TERRAIN_TIMERS=1`. `OPENSC4_TEST_TERRAIN_MESSAGES=<n>` sends the terrain, on the n-th day, the messages it handles in a fixed order with a state checksum after each (`OPENSC4_TEST_TERRAIN_RECONCILE=0` leaves out the edge reconciling, which can open a dialog).

## Checked

2026-10-11.

- The in-run comparison: about 110,000 questions per game day, 73 million over the batches below, none answered differently. That includes ten days with a volcano growing (`OPENSC4_TEST_DISASTER_TYPE=0`), where the altitude checksum changes every day.
- Parity, the original against `simulators.terrain.queries=ours` with the comparison on: the 12 saves over 7 days, 10 same; Big City d0000 partly (two outcomes each side, the first-day traffic noise of that save, [parity.md](parity.md); its terrain part is the same in all eight runs); Big City d1095 failed once, see below. The plopped city over 30 days: 3 of 3 same. The volcano runs do not repeat in the original either and are judged by the in-run comparison only.
- The failed run: our side crashed while the city was loading, before any game day, at 0x00975CC8 (a framework object's constructor at 0x00975CB5 storing its vtable 0x00AD8D88 through a bad pointer), on another thread than the main one. Four more runs of that save with ours loaded and ran; no query of ours is in that code. Counted with the game's rare load crashes ([parity.md](parity.md)).
- The setters (`simulators.terrain.edit` with the queries, the comparison on): the edit test's 46 calls and the whole-map test's 15 log the same answer and state after every call with the original (twice) and with ours, and the `terrain` part is the same afterwards. Parity with the edits on day 2: 12 of 12 saves the same over 7 days. With the whole-map test on day 2: 11 of 12 the same, Big City d0090 partly, the land and the per-call lines the same in all eight runs and the difference an advisor checksum on the last day. The original against itself with the same test gives 9 and 8 of 12 the same in two batches, with that same advisor value in one of its runs: after the sea is moved under a built city the original does not repeat, more so under load. The plopped city with both tests over 30 days: 2 of 3 the same, the third partly with three outcomes of the original alone.
- The shaping machinery (`simulators.terrain.brush` with the other two groups): the brush test's calls (191 on Big City and Rush Hour, 107 on Making Money, 208 with the whole-map test on the plopped city) log the same answer, map checksum and random state after every call, and the `terrain` part is the same afterwards, in every run of all 12 saves and the three plopped ones, original and ours. Whole-city parity after the brushes cannot be judged on the 12 saves: the original does not repeat itself there (two runs of the original part from the third day in the flora occupants' checksum, on every save); the plopped city came out 3 of 3 the same over 10 days. An independent reading against the disassembly found no difference, including in the shore cliff code whose random draws the test did not reach, and confirmed call site by call site that only the two thunked functions need ECX kept.
- Init, messages, save and load and the object (all seven terrain groups ours): every corpus city loads through our Read and Init, runs and saves through our Write, with our own object behind the factory. Plain parity over 7 days: 9 of 12 saves the same, 3 partly; in those three the runs differ in `checks`, `citylot` or, in the first run of ours on two saves, the first day's `flam`, never in the terrain's parts. The first two are the known noise of the original; the first-day `flam` value moves in the original too when only its heap layout is shifted (`diagnostics.heap_shift` with 300000 on Making Money d0000 gives another value; shifts up to 65536 do not), and our object and its log lines shift the layout: one of four single runs of ours had a different value, three the original's. The decoded terrain records are the same in every pair. With the messages test: the terrain's parts and the per-message lines are the same in every run of the 12 saves (one run of the original aside, whose terrain part differs from all others), and the plopped city with the edit test came out 3 of 3 over 30 days. An independent reading found no difference. Not run in the game, only read: a new city's path through Init (no saved terrain), the seven cheat commands, the edge reconciling and its dialogs, `SaveObliterated`.
- An independent reading of the 24 setters against the disassembly found no difference on any branch, including the ones the tests do not take. Slot 50's clamp in the game is dead code (the height is compared with itself), so ours has none.
- An independent reading of all 54 against the disassembly found no difference in results. It listed what our C++ cannot repeat and no caller was found to use: the upper bits of EAX where the game sets only AL (nine of the tests leave part of a count there on the out-of-range path), and ECX and EDX, which the game's small getters happen to leave untouched. These are virtual calls, so a caller cannot count on registers being kept.
- Resting on the build, not checked by a test: no fused multiply-add and no x87 code from the compiler (clang-cl's SSE2 default), and the FPU control word 0x027F wherever the game calls these (measured in the simulator tick only).

## Still the game's

The brush body and loader, erosion, the flood, Init, Shutdown, save and load, the object itself, and the drawing side (the view, lighting, redisplay, wave producers). They follow piece by piece.
