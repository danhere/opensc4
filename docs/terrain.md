# The terrain

The city's land and water: one object, `cSTETerrain` (interfaces `cISTETerrain : cISTETerrainMap` in gzcom-dll), that every simulator and the renderer ask for heights, slopes and water, and that the terraforming tools, disasters and the sea level change. Its picture on screen is another class, the terrain view (`cSTETerrainView3D`, [renderer.md](renderer.md), "The terrain"), which is not covered here.

## The class

- Main vtable 0x00AB3D18, 98 slots; the +4 and +8 parts have only QueryInterface and Release thunks (0x0074C700 to 0x0074C730). Code about 0x00740060 to 0x00751000, 259 functions, 66 KB.
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

## Tests

The fingerprint part `terrain` reads the object's memory directly: the grid, the cell width, the sea level, the height limits, and checksums of every altitude, flags word, water depth and sediment, and normal, plus the water vertex count. Any change of the land between two runs shows there.

`OPENSC4_TEST_TERRAIN=1` compares, inside one run, every one of the 54 slots as the object has it with the game's own function at its address, bit for bit (floats by their bits, ST0 as a double, whole out-buffers): the getters; vertices and cells on every edge, a hashed part of the interior and outside on every side; about 4,600 positions a day on vertices, lines, diagonals and half cells; rectangles inside, empty, reversed and over the edge. It prints `q<agree>/<asked>`. It does not ask what would make the game itself read outside its arrays (the lerped slots on the far edges, rectangles outside the grid for the slots without a range test).

## Checked

2026-10-11.

- The in-run comparison: about 110,000 questions per game day, 73 million over the batches below, none answered differently. That includes ten days with a volcano growing (`OPENSC4_TEST_DISASTER_TYPE=0`), where the altitude checksum changes every day.
- Parity, the original against `simulators.terrain.queries=ours` with the comparison on: the 12 saves over 7 days, 10 same; Big City d0000 partly (two outcomes each side, the first-day traffic noise of that save, [parity.md](parity.md); its terrain part is the same in all eight runs); Big City d1095 failed once, see below. The plopped city over 30 days: 3 of 3 same. The volcano runs do not repeat in the original either and are judged by the in-run comparison only.
- The failed run: our side crashed while the city was loading, before any game day, at 0x00975CC8 (a framework object's constructor at 0x00975CB5 storing its vtable 0x00AD8D88 through a bad pointer), on another thread than the main one. Four more runs of that save with ours loaded and ran; no query of ours is in that code. Counted with the game's rare load crashes ([parity.md](parity.md)).
- An independent reading of all 54 against the disassembly found no difference in results. It listed what our C++ cannot repeat and no caller was found to use: the upper bits of EAX where the game sets only AL (nine of the tests leave part of a count there on the out-of-range path), and ECX and EDX, which the game's small getters happen to leave untouched. These are virtual calls, so a caller cannot count on registers being kept.
- Resting on the build, not checked by a test: no fused multiply-add and no x87 code from the compiler (clang-cl's SSE2 default), and the FPU control word 0x027F wherever the game calls these (measured in the simulator tick only).

## Still the game's

Everything that changes the terrain (the altitude setters, the brushes, erosion, the flood, sea level), Init, Shutdown, save and load, the object itself, and the drawing side (the view, lighting, redisplay, wave producers). They follow piece by piece.
