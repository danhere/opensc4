# The traffic network map (cSC4TrafficNetworkMap)

The game has no transit simulator of its own: buses, trains, subways, ferries, transit switches and strikes are all part of the traffic simulator ([traffic.md](traffic.md)). The transit piece that was still the game's code is the traffic network map, the automata's view of the transport networks: a grid with one record per network occupant on each city cell, the traffic signals of intersections, the lanes of multi-way pieces and the objects waiting on cells. Cars, trains and pedestrians ask it where they can go and what the signal in front of them shows. It is saved with the city. Build 641 addresses throughout; [trafficmap.h](../src/core/trafficmap.h) has the field and record layouts.

## The class

Class ID 0x6A0F82B2, interface 0xCA0F81B9 (`cISC4TrafficNetworkMap`, not in gzcom-dll). Object size 0xA90. Factory 0x004B42E0 (`SC4::operator_new(0xA90)`, constructor 0x004B3EB0), destructor 0x004B3A30. Code from 0x004ADF50 to 0x004B5C20. Four interfaces:

| Vtable | Interface | Slots |
| --- | --- | --- |
| 0x00A8FED8 | `cISC4TrafficNetworkMap` | 52: QueryInterface 0x004AE350, AddRef (shared), Release 0x004AFAA0, Init 0x004AE4E0, Shutdown 0x004B4000, the queries and signal functions below, the deleting destructor (45, 0x004B3FE0) |
| 0x00A8FEC8 | `cIGZUnknown` at +0x4 | 3 |
| 0x00A8FEB8 | `cIGZMessageTarget2` at +0x8 | DoMessage 0x004B0C10 |
| 0x00A8FEA0 | `cIGZSerializable` at +0xC | Write 0x004B1CE0, Read 0x004B4DE0, GetGZCLSID 0x004B3AB0 |

### Fields

| Offset | Field |
| --- | --- |
| +0x10 | initialised |
| +0x14 | reference count |
| +0x18 | the grid: a list of cell records per cell (width x height; row z at z << shift) |
| +0x1C, +0x20, +0x24 | width, height (the city's cells), shift (width = 1 << shift) |
| +0x25 | set by Read; Init and slot 50 then re-register tunnels and rebuild |
| +0x28 | signals: hash table of intersections keyed by occupant (0x70-byte nodes) |
| +0x3C | signal objects to refresh on the next signal tick |
| +0x40 | lanes: hash table keyed by occupant (0x48-byte nodes), 4 ways of a vector and a round-robin index |
| +0x5C | the view changed (message 0x69247DC7) |
| +0x60 | the view rectangle in cells (x0, z0, x1, z1), 5 cells around the view |
| +0x70 | a view point, copied on message 0xA6B79612 |
| +0x7C | the map's own random generator (0x9C8 bytes, seeded from the clock: 0x00913B74 with -1) |
| +0xA44, +0xA48 | the view (0x00B43DD8) and the occupant manager (0x00B43D0C, referenced) |
| +0xA4C, +0xA50 | two occupant filters made by Init (vtables 0x00A8FE14 and 0x00A8FE48) |
| +0xA54 | paused (messages 0xCAD61E00 and 0xEAD61E16) |
| +0xA58, +0xA5C | the signal tick's walk over the grid |
| +0xA60 to +0xA6C | two blink timers and their states |
| +0xA70 | objects waiting on cells: hash table keyed by x << 16 \| z |
| +0xA84 to +0xA8C | the cell update's walk and its 1-second timer |

A **cell record** (0x58 bytes, from the pool at 0x00B09588) holds its occupant (referenced), 12 vectors of 16-byte lane entries by travel type (slots 19-24), a vector of 12-byte path entries (20, 22), a list of automata on it (30, 31), three flag words and the approach and turn traffic of its four sides. Slot 48 makes one per cell the occupant covers. A **signal** has four approaches (kind 0 none, 1 stop sign where a street meets, 2 traffic light, 3 rail crossing; green, amber, the green and red times and a timer), the props that show it and the automata waiting at it.

## What drives it

Init registers the map with the framework service 0x86AD10EC (slot 5, code 0xC0). That service sends message 0x86AD10EE every frame with the elapsed time in milliseconds, and DoMessage passes it to slot 51 (Update): the signal tick (32) while a view is active, the approach and turn traffic of the view rectangle when the view changed, and every 1000 ms of frame time the cell update (0x004AE6D0, which calls each occupant's slot 50 within a 5 ms stopwatch budget). The automata call the queries as they move. So the map runs on **frame time**, not game time.

## Our code

Every function of the class is ours but the shared AddRef and getters (slots 1, 17, 35) and the game's STL instantiations (hash insert, erase and clear, vector growth), which our code calls. Each group is its own replacement:

| Id | What |
| --- | --- |
| `simulators.trafficmap.interface` | QueryInterface, Release and the parts' thunks; the queries: cell records (13, 14, 16), occupants at a cell, position or height (7-12), the ground height (18), neighbour records (15), lane and path entries (19-24), occupants near a rectangle or an occupant (25, 26), automata counts, add and remove (27, 30, 31), what an occupant or cell allows (28, 29, 34), lane ways (40-42), snapping a point or a move onto the network (43, 44) |
| `simulators.trafficmap.signals` | slots 5, 6 (a weighted random order), 32 (the signal tick), 33 (what an automaton meets at a signal), 36-39 (refresh, toggle, automata waiting), 46 (the view rectangle), 51 (Update), and their helpers: setting up and applying approaches (0x004AFC20, 0x004AE9E0, 0x004AE850), one signal's tick (0x004AFB80), the green wave over connected signals (0x004B1110), approach and turn traffic (0x004AFCE0, 0x004AEC70, 0x004AEE70, 0x004AFE50), the signal props (0x004B2640, 0x004B2830), the cell update (0x004AE6D0) |
| `simulators.trafficmap.init` | Init and Shutdown with their clean-up helpers |
| `simulators.trafficmap.occupants` | slots 47-50: cell objects, adding and removing an occupant (records, signal, lanes), the rebuild |
| `simulators.trafficmap.messages` | DoMessage |
| `simulators.trafficmap.save`, `.load` | Write and Read |
| `simulators.trafficmap.object` | our own object under the class ID: factory, constructor, destructor, identity; turn it on with the others |

The game copies uninitialised stack bytes into some fields: Read builds signals and lanes on its stack and a version 6 record leaves a signal's +0x58 byte to whatever was there; slot 48 leaves the round-robin index of an empty lane way unset. Our Read copies the original's stack area before building the same values (so it copies the same leftovers); elsewhere ours writes 0. The save decoder and the fingerprint leave out the values nothing reads (an empty way's index, a kind-0 approach's times, padding).

## The save record

Version 7 (6 in the oldest saves; Read takes 3 to 7). `tools/re/save_schemas/trafficmap.py` decodes it; the hash tables are written in bucket order of pointer keys, so the decoder sorts them by content.

1. Header, u16 version.
2. u32 cells (width x height), u32 records; each record in cell order: u32 cell, reference to the occupant, the lane vectors (u32 n; n x {u32 index, u32 m, m x {u32 key, f32}}), the automata (u32 count, references), u8 0, u32 flags x 3, 4 x {f32 approach traffic, 4 x u16 turn traffic}, u16 0, from version 5 the paths (u32 n, n x {s32 key, f32, u16}).
3. u32 signals; each: reference to the occupant, from version 7 u8 +0x58 and the waiting automata (count, references), 4 approaches {u32 kind, u8 green, f32 green time, f32 red time, f32 timer}.
4. From version 2: the cell objects (u32 count; u32 key, count, references).
5. The view rectangle (4 x s32), the blink timers and states (f32, u8, f32, u8).
6. From version 6: the lanes (u32 count; reference, 4 x {u32 n, n x u32, u16 index}).

## Parity

The fingerprint's `tm` part has by default only what does not depend on frames: the number of cell records and a hash of their cells, and the sizes of the signal, lane and cell-object tables. `OPENSC4_TEST_TRAFFICMAP=1` adds the rest (a hash of the cell records' contents in cell order, order-free sums of per-node hashes of the tables, the walks and blink states); `OPENSC4_TEST_TRAFFICMAP_RANDOM=1` also the map's random generator. The save decoder likewise keeps by default only the record's frame-free fields (the records' cells and occupants, the signals' and lanes' occupants, the lanes' values, the cell objects, the view rectangle); `OPENSC4_TRAFFICMAP_FULL=1` decodes everything. The checks below used the full forms. With the defaults, every parity run compares the frame-free part: control 12 of 12 and every `simulators.trafficmap.*` on 12 of 12 for 7 days (2026-10-09).

**The original does not repeat this class.** Because the map runs on frame time, and the harness's frames are not deterministic (the virtual clock advances per clock read, and frame-level code reads it a varying number of times: the frame times seen were 21 to 31 ms), the original against itself gives different signal timers, lane entries, automata lists and draws from the map's generator after a few game days, on every save, while every other part of the city stays identical (control, 7 days, 12 saves, 2026-10-09). So the usual 7-day comparison cannot pass for this class with either side. What is checked instead:

Dan's call (2026-10-09): check exactly what does not depend on frames, and the rest by its effect on the city. All on 2026-10-09, with every `simulators.trafficmap.*` on:

- **Load and save (0 game days)**: Read, Init, the rebuild, the signals' set-up, Write and the frames during the load. Control (original against itself): identical apart from the generator. Ours: **12 of 12**, fingerprints and the map's record identical.
- **Load, cycle and save**: `OPENSC4_TEST_TRAFFICMAP_CYCLE=40` takes 40 occupants off the map at load and puts them back (slots 49 and 48: records, signals, lanes). Control 12 of 12; ours **12 of 12**.
- **7 days, 12 saves**: everything outside the map (every other fingerprint part, every other decoded record) identical between ours and the original on every day, as in the control. The one record that differs, the traffic simulator's on Big City d0000, differs in the control too. The original's side and ours give the same fingerprints outside the map day by day across the two batches. Big City d0090 once hit the harness's known rare load crash (0x0096DA20, [parity.md](parity.md)); ten more runs of it were clean.
- **A year on `saves-mm2`**: d0365 identical outside the map on all 366 days, as in the control. d0090 parts on the same day in the control too, so the original itself does not repeat that year (perhaps through this map: its cell update calls each occupant's slot 50 on frame time).

Reached in these runs: Read, Write, Init, Shutdown, DoMessage, Update, the view rectangle, the rebuild, adding and removing occupants, signal set-up and `PickApproaches`, what an automaton meets at a signal, the automata's add and remove, lane and path entries, neighbour records, occupants near a rectangle. Not reached: the signal tick (32; no view is active in the unattended runs) and with it the green wave, the waiting lists (38, 39), toggling (37), the cell objects (47), the height and snapping queries (18, 43, 44).

Exact parity over days needs deterministic frames in the harness (a fixed frame clock and a fixed number of frames per game tick); that would cover the automata too.
