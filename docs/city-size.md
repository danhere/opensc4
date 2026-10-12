# City size: what limits it and how we lift it

Status: research, 2026-10-08. Nothing here is implemented. Roadmap: "Beyond the original: bigger city tiles" in [ROADMAP.md](../ROADMAP.md). Timing (Dan, 2026-10-09): implemented with P8, not waiting for the 64-bit build; 8 km should fit in 32-bit memory.

## What the game allows

Three city sizes, all square: small 64x64 cells (1 km), medium 128x128 (2 km), large 256x256 (4 km). A cell is 16 m.

The region layout is `config.bmp`: one pixel per 1 km square, coloured by the size of the city that covers it (red 1 km, green 2 km, blue 4 km; a channel counts only at exactly 255). Any other colour leaves that square empty. Cities are aligned to the 1 km grid and must not overlap ([Simtropolis, "Config.bmp: How to Make it Yours"](https://community.simtropolis.com/omnibus/simcity-4/mapping-terraforming/configbmp-how-to-make-it-yours-r76/)). There is no colour or setting for a bigger city.

The modding community has never produced a bigger tile: the answer has always been that it needs changes to the exe ([Simtropolis, "Bigger city tiles"](https://community.simtropolis.com/forums/topic/24086-bigger-city-tiles/)). That is exactly what this project can do.

## What actually limits it

| Limit | Kind | Notes |
|---|---|---|
| Region layout knows three sizes | Hard, in the exe | The `config.bmp` reader and the region view. Nothing else offers a fourth size. |
| Code that assumes 64/128/256 | Hard, in the exe | Measured (see "Size census"): the region decode, the occupant manager's fixed world grid, the city's size type and the tables indexed by it. 169 functions read the size instead. |
| 32-bit address space | Hard, until P9 | At most 4 GB with the large-address flag. Dense large cities with big plugin folders already run out. Every doubling of a city's side is 4x the cells and roughly 4x the memory for grids, lots and props. |
| Simulation time | Soft | Grid simulators scale with area (4x per doubling). Traffic routing grows faster than area. Fine on a modern CPU for 2x; 4x needs measuring. |
| Renderer | Not a blocker | The D3D11 driver already runs past the old 2048 px cap. Draw distance and terrain detail are tuning, not limits. |
| Save format | Soft | Saves are DBPF files; records carry their own sizes. A bigger city needs no new container, only readers that take sizes from the save. |

## What our code tells us so far

- The city reports its own size: `cISC4City` slots 93 and 94 give cells in x and z ([data/addresses/core.toml](../data/addresses/core.toml), `SC4::City`). Our power simulator reads it the same way (`CityCellsX`, [src/core/power_sim.cpp](../src/core/power_sim.cpp)).
- The simulators we have read build their grids from that size, not from constants: pollution's eight `cSC4SimGridSint16` grids and its per-8x8-block vectors ([pollution.md](pollution.md), Init), power's bit arrays and 8x8 blocks ([power.md](power.md)), water's cell flags with a stride and height saved in the record ([water.md](water.md), version 4). Traffic's grids look the same, but are not checked yet.
- The save records we decode store grid dimensions with the data (water's stride and height, power's consumption-map rows and columns). They should load at any size.
- The renderer's camera works in metres per pixel and 16 m tiles ([ui-scaling.md](ui-scaling.md)); it has no city-size table.

So the simulation side looks size-agnostic. The census below confirms that the blockers are in the region, the occupant manager, the size type and per-size art; terrain is still open.

## Size census

Run on 2026-10-08 with [tools/ghidra/CitySizeCensus.java](../tools/ghidra/CitySizeCensus.java), a static scan that takes about 30 s, on `SimCity 4.exe` 1.1.641. The "Ran" column comes from a 2-minute coverage run of Making Money (`coverage-makingmoney-2.csv`, 2026-10-05). That run loads the city directly, so region code never shows as run. The script's CSV describes game code and stays local (`analysis/city-size-census.csv`).

```bash
# Windows Git Bash, after scripts/ghidra-setup.sh; copy the script into the local Ghidra scripts folder first
analyzeHeadless <projdir> SC4_1.1.641 -process "SimCity 4.exe" -noanalysis -readOnly \
  -scriptPath <scripts> -postScript CitySizeCensus.java <out.csv> [opensc4-coverage.csv]
```

| Category | Hits | Functions | Ran | What it is |
|---|---|---|---|---|
| size_read | 351 | 169 | 65 | Calls through vtable +0x174/+0x178 (`cISC4City` slots 93/94; other interfaces share the offsets) |
| near_read | 119 | 24 | 9 | 64/128/256/1024/4096, masks, shifts by 6/7/8 inside those functions |
| area | 34 | 24 | 10 | n x n x k constants (n = 64, 65, 128, 129, 256, 257) |
| imm257 | 38 | 31 | 16 | The immediate 257 anywhere |
| float | 27 | 15 | 8 | 1024.0, 2048.0, 4096.0 |
| table | 2 | 1 | 0 | Runs of 64,128,256 / 1,2,4 / 1024,2048,4096 in data (bit-mask tables skipped) |
| config_bmp | 6 | 3 | 0 | References to the `config.bmp` name |

I checked the raw hits by hand, and most are noise:

- 0x4101, 0x3331 and 0x4201 are occupant group IDs, not 129x129.
- 0x101 in 0x0063DDF0-0x0063EF85 is a tokenizer's number token.
- 0x2000 to 0x800000 in 0x00511A90 are path-flag bits printed in a debug string.
- The 0x40000 tests are occupant flags.
- The 1024.0f in 0x004E7A90 is a curve point (20, 1024).

### What does assume a size

| Where | What it assumes | Ran | Replaced in |
|---|---|---|---|
| Region `config.bmp` decode, 0x004A5640 (called by 0x004A6B00; an unnamed region class between `cSC4PropManager` and `cSC4RegionalCity`) | For each pixel, `code = (R==255) + 2*(G==255) + 4*(B==255)`. Only codes 1, 2 and 4 count (a power-of-two test). Each tile is stored as `{x, y, code >> 1}` in a vector at region +0x44, so the size is kept as log2 of the side in km. | no | P8 region view |
| Region tile matching, 0x004A6330 | Matches saved cities to tiles with `1 << index` km. Any index works here; only the decode limits it to three. | no | P8 region view |
| `cSC4OccupantManager` (0x0049D990-0x0049E1B0, `Init` 0x004A18E0) | A fixed world grid of 192 x 192 cells of 64 m (12,288 m). A standard cell becomes a world cell as `x / 4 + 64`, and `PositionToWorldCell` adds 4096 m. That leaves a 256-cell margin on each side of a city of at most 256 cells, so a 512-cell city overflows it. `Init` sizes its rows to 0xC0 whatever `SetCitySize` was given. | yes | P8 lots / buildings (one class; callers use its interface) |
| `cSC4City::GetCitySizeType` 0x00459390 (slot 87, +0x15C) | Size 64 gives 0, 128 gives 1, anything else gives 2. A 512 city reads as large. | yes | P8 (city object) |
| 0x005DDEC0, next to the lot base texture code | Tables indexed by size type: 0x00AA2BAC = {32, 64, 128} and 0x00AA2BB8 = {128, 256, 512}, half and twice the side. | no | P8 lots / buildings |
| 0x005DDDC0, 0x007770B0 | Resource instance IDs per size type: two per size, in table 0x00AB52A4 and in a local copy. This is per-size art, likely thumbnails or region images. A fourth size needs new entries, or reuses large. | no | P8 region view |
| 0x0074FEA0 | A 3-entry table at 0x00B4BCF8, filled at run time and indexed by size type. | yes | not identified yet |
| `cSC4CameraControl::UpdateProjection` 0x007CBE40, `cSC43DRender::Init` 0x007C6410 | 4096.0 m. The camera uses `max(extent, 4096)`, a lower bound that does no harm. The 3D render sets 4096.0 twice as an initial extent at +0x114 (+0x20, +0x24); still to check whether the city updates it. | yes | P1 renderer (the camera is already hooked) |
| `cSC4NeighborDeal` area, 0x006CE920 | Finds neighbour positions with `>> 6` and `& 0x3F`, on the 64-cell (1 km) region grid. Bigger tiles on the same grid are fine. | yes | no change |

### What reads the size instead

169 functions call slots 93/94, and 65 of them ran in the test city. They cover:

- power, water and pollution
- the zone, lot, prop and tract developers
- the network and lot managers
- neighbours, demand, flammability and traffic
- automata and the mini map

These adapt to the size by construction. The only risk is what they allocate next to the read, which the near_read list covers; none of its hits turned out to be a fixed size.

### Still open

- **Terrain.** The terrain classes are unnamed in the Ghidra project. None of them reads slots 93/94, and no 257 constant sits in terrain-looking code. The next thing to find is where the heightmap gets its size, probably from the city through its own interface. **Answered 2026-10-11** ([terrain.md](terrain.md)): the terrain is `cSTETerrain` (vtable 0x00AB3D18), and its Init does read the city's slots 93 and 94 (`CellCountX` / `CellCountZ`, the calls at 0x0074FEFC and 0x0074FF0A); every size in the object is derived from them (vertices = cells + 1, metres = cells x 16). No constant sized for 256 cells was found in its queries, setters, shaping code, Init, messages, save or load. To mind for big tiles: a load takes the vertex counts from the file and Init keeps the loaded array while overwriting the counts from the city (a save with another grid is overrun); the water distance rows hold one byte per vertex; the flood starts in a fixed window of vertices 20 to 40 (0x00747720, not yet ours).
- **Region view and thumbnails** never ran under the coverage meter. A coverage run that opens the region view is needed before the spike.
- The class guesses for unnamed functions (`~cSC4Foo` in the CSV) come from named neighbours and are hints only.

## Spike after the census

Make the region offer an 8 km (512x512) city: patch the `config.bmp` colour decode for one test region (for example, accept white, code 7, as index 3; the tile vector already stores log2 of the side), create a new city there, and record where it fails. Expect the occupant manager's world grid to fail first. Each failure is one entry on the list above. This runs inside the 32-bit game, so it also shows how far 512 gets before memory is the problem.

## The 8 km spike

Started 2026-10-11. The region data class is ours now (`region.data.*`, [region.md](region.md)), so the layout decode needs no patch: with `[improvements] big_cities = on` in `opensc4.ini` (and `region.data.*` on), our `DecodeLayout` reads a white square (all three channels at 255) as an 8 km tile, stored as log2 index 3. Off by default; with it off the decode is the game's.

The occupant manager is ours too ([occmgr.md](occmgr.md)), but still sized for 256 cells: a 192-cell world grid with the city at 64 to 127. A 512-cell city needs 128 world cells; with a 64-cell margin each side the grid is 256 cells, indices 0 to 255, just inside the one-byte rectangles. With `big_cities` on, Init sizes a new city's grid that way (192 for every standard size), capped at 255 because an occupant's first cell byte of 0xFF means "not in the manager"; so an 8 km city gets a 63-cell margin on the far side. Big cities need every `simulators.occmgr.*` group ours (the game's own slots know only 192), and stay off with a log line otherwise; the region decode follows the same switch. The clamps, walks and slot 7 always follow the grid's own columns, so a saved grid of any size is walked whole even with the setting off. `OPENSC4_TEST_OCCMGR` skips its comparison with the game's functions on a grid other than 192.

To run: a test region whose `config.bmp` has one 8x8 white square, then create a city there and record each failure below.

### Results

Run on 2026-10-11 with the debug build of `3d63f0c`, unattended, Steam 1.1.641, default 2 GB address space.

**Setup.** An isolated `-UserDir` with a region `Spike8km`: a 16x16 `config.bmp` with an 8x8 white square at 0,0 and a 4x4 blue square at 8,0 as the control, nothing else. `opensc4.ini`: `[improvements] big_cities = on`, `region.data.* = ours`. The run used test code that is not in the repo: a switch that opens the region, makes the regional city of an empty tile as the region screen does (0x007AEA00) and calls `cISC4App::RequestNewCity`; a log of every fatal exception in game code with the stack's code addresses; a memory line every 2 s. Each failure below was passed with a local change (not committed) so the next one could show.

**What worked.**

- The white square decodes as a tile: the region's tile list has `0,0 side 8 km (log 3)` next to `8,0 side 4 km (log 2)`, and the regional city made for it reports 8 x 8. Seen in the region's data, not on the region screen (the unattended run does not show it).
- With failures 1 and 2 passed, the city is made: `CellCountX/Z` 512 x 512, `GetCitySizeType` 2 (large). The empty city ran 19 game months at speed 2 and quit cleanly.
- The control (a new 4 km city on the blue tile, same switch) is made, runs and saves.

**Failures, in the order met.**

| # | When | Crash site | What it is | Passed by |
|---|---|---|---|---|
| 1 | Making the city | Write past the top of the stack at 0x007284E1 (the stack is wiped; neither the game nor we write a report) | `cSC4WeatherSimulator::FindWaterBodies` 0x007283D0: a 256 x 256 byte scratch on the stack. Our own version ([weather.md](weather.md), `simulators.weather.bodies`) has the same 256 x 256 array. | Our version with a bigger array |
| 2 | Making the city | Write past a heap block at 0x0071A966, called from `OnCityInited` 0x00720430 | `cSC4TrafficSimulator::OnCellsChanged` 0x0071A860 resets the 46-byte cell records at +0x58, indexed `(x << 8) \| z`: a table of 65,536 cells. Not one site: the traffic simulator and path finder pack a cell into 16 bits (`x << 8 \| z`) for the cell table, hash keys, edge and density keys and route bytes ([traffic.md](traffic.md); 30 or more places in `traffic_sim.cpp`). The same table failed again through `CellHasTraffic` 0x0070FA40 (read past the block), asked for a cell on the far edge. | Both clamped to cells below 256 (traffic then knows a quarter of the city) |
| 3 | Saving (the redraw before the pictures) | Return to a garbage address (0xC5ACD91C, a float); a hardware watchpoint on the return slot gave the writer, 0x00751F47 | `cSTETerrainView3D::LandRowThresholds` 0x00751EE0 writes one float per terrain row in view into a 257-float buffer on the stack of `LandRowVisibility` 0x007535A0 (frame 0x424). `WaterRowVisibility` 0x007539D0 has the same buffer. Any drawn row above 256 overwrites the frame. Our version (`renderer.terrain.rows`) has the same `float[257]`. | Our version with a bigger buffer |
| 4 | Saving, drawing the terrain | Read of a garbage list at 0x007598A7 (`STE::ClearRowLayers`), from `RebuildRowLayers` 0x0075ABA0, `DrawLandLayers`, `DrawLand`, `cSC43DRender::Draw`, `SC4CityFiles::MakePictures` 0x005DDEC0 | The terrain view's set-up 0x0075C0C0 sizes the per-row layer lists (0x00B4C7E0) and the dirty row bits (0x00B4C7EC) to a literal 0x100. The other arrays it makes there take their size from 0x00B4BC44. | Both grown to 1024 from the test |
| 5 | Saving (also a quick save) | Read past a heap block at 0x0060B0E4 | 0x0060ACD0 (5 KB, unnamed class with vtable 0x00AA75E8), called from 0x0060C0A0, from the map window's `ShowView` 0x007A49B0 (pushed by 0x007A61E0), from `MakePictures`. It packs cells as `(x * 0x100 + z) * 0x100 + 1 + n`: 8-bit coordinates again. | Not passed; the spike stopped here |

**Not reached.**

- No 8 km city was saved, so loading one is untested.
- The occupant manager's 192-cell world grid did not fail, against the expectation above: an empty city puts nothing beyond cell 255. It needs something built or planted out there, which needs traffic (2) and the save (5) first.
- Read in the code, not run: the region screen's terrain import (0x007AEDD0) takes a tile's side as `0x100 >> (2 - log)`, which is 0 for log 3; `GetCitySizeType` gives 2 for 512, so everything indexed by size type treats the city as large (`MakePictures` among them).

**Memory** (debug build, peak commit of the game process; 2,047 MB of address space, about 1,500 MB free before a city is open).

| Run | Peak commit | Address space left |
|---|---|---|
| Region open, no city | 133 MB | 1,506 MB |
| New 4 km city, 19 months, empty | 229 MB | 1,404 MB |
| New 8 km city, 19 months, empty | 293 MB | 1,328 MB |
| New 8 km city at failure 5 (full save, pictures being made) | 411 MB | 1,242 MB |

An empty 8 km city costs about 190 MB over the region screen against about 125 MB for an empty 4 km one. Memory is nowhere near the limit for an empty city; this says nothing yet about a built one.

**What it means for the plan.** Four of the five are 256-cell assumptions that the census did not find, because none reads the city size: two stack buffers, one literal array size, and the 8-bit cell packing in traffic and in the class behind the map picture. The census looked for 64/128/256 next to size reads; the next pass should look for 0x100 and 0x101 array sizes, `<< 8` cell keys and frames of 0x400 and 0x10000 bytes. Traffic is the large item: its cell keys need a wider format in the simulator, the path finder and the save record.

## Lifting the spike's failures

Work behind `[improvements] big_cities`; standard sizes keep the game's layouts. `BigCities()` (occmgr_sim.h) now also needs `simulators.weather.bodies` and every `simulators.traffic` group ours, since the game's own versions overrun at 512.

| # | Status | Owner |
|---|---|---|
| 1 | Done: our `FindWaterBodies` scratch keeps 256 x 256 for standard sizes and grows with the city. | weather (this work) |
| 2 | Part done: the cell record table (+0x58) is sized from the city and indexed through `CellRecord` ([traffic_cells.h](../src/core/traffic_cells.h)), stride 256 for standard sizes; keys, routes, cell tables and ferries are wide: see "Traffic cell keys" below. | traffic (this work) |
| 3 | Open: `renderer.terrain.rows` has the game's `float[257]` in `LandRowThresholds` and `WaterRowVisibility`; size it from the terrain's rows (cells + 1). | Renderer (terrain drawing) |
| 4 | Open: the terrain view's set-up 0x0075C0C0 sizes the row layer lists (0x00B4C7E0) and dirty row bits (0x00B4C7EC) to a literal 0x100; take the size from 0x00B4BC44 as its other arrays do. | Renderer (terrain drawing) |
| 5 | Open: 0x0060ACD0 (behind `MakePictures`' map picture) packs cells as `(x * 0x100 + z) * 0x100 + 1 + n`. Also on the save path: `MakePictures`' size-type tables ({32, 64, 128}, {128, 256, 512}) treat 512 as large, and the record streams refuse counts of 1,000,000 or more. | Save rewrite |
| - | Open, read not run: the region screen's terrain import 0x007AEDD0 takes a side as `0x100 >> (2 - log)`, 0 for log 3. | Region view |
| - | Nothing failed yet: the terrain simulator's heightmap size is still unchecked (census, "Still open"). | Lot and zone (terrain simulator) |

### Traffic cell keys

The game packs a cell in 8 bits a coordinate. `traffic_cells.h` holds the helpers: `CellKey` (`x << 8 | z`) and `TypedCellKey` (top byte, then the travel type at bit 16, then the cell). For a city past 256 cells a side (`g_wideCells`, set by Init) they take 10 bits a coordinate and the type at bit 20; the top byte stays put. Standard sizes keep the game's keys bit for bit.

Done:

- Edge density keys (+0x178): slot 12 (now takes whole cells when wide), slot 24's call, the occupant message, phase 6, the route recording, `CellTrafficPeak`, `OnCellsChanged`. Saved as a u32, so a big city's save carries the wide key.
- The path finder's node keys (start nodes, `AddNode`, `DecodeKey`) and `RecordRoute`'s previous cell.
- The water route finder's node cell (a u16 with padding in the game's node; the whole word now).
- The cell walks' deque items (`UpdatePendingRect`, `SpreadFromEdge`).
- The lot multimap's value (+0x1A0): two cell bytes and two of padding, a `CellKey` across the four when wide.
- **Route bytes** (`type, x, z`, jumps `type | 0x80, x, z`): a big city's cells take two bytes a coordinate, low first (`ReadRouteCell`, `PushRouteCell`): the route iterator, the path finder's encoding and the water routes. Trips keep their routes as byte vectors, so the lot record needs no change.
- **Damaged roads and rail crossings** (+0x7B8 to +0x800, slots 16 and 17, which now take whole cells when wide): a big city's are ordered sets of `CellKey` beside the game's (`WideSet`), which stay empty; saved as a uint32 count and uint32 keys.
- **The link table** (+0xC8): a big city's links are kept by `CellKey` in nodes of the game's layout with the linked cell whole after them (`WideLinks`, `FindLink`); saved as a uint32 key and the linked cell as two uint16.
- **Ferries**: the ferry index (+0xDC, +0xF0) and the links between entries keep their 16-bit keys, but a big city's key is an id per cell (`FerryKey`, given out when an entry is indexed) instead of `x << 8 | z`. The entries' cells (own, far, water, lot) stay as bytes in the game's entry and are kept whole beside it (`FerryCellX/Z`, `SetFerryCell`). A big city's save writes the id table before the entries and each entry's four cells as uint16 after its bytes.

Still 8 bits:

- **The cost record's switch cell** (+0x70C, +0x12/+0x13): written and saved only; nothing of ours reads it.
- **MySim's route**: the path finder's route deque has 3-byte cells (`FinishRoute`, read by `mysim_dispatch`), in the game's deque helpers. MySim stays off in a big city until it is widened.
- Slots 30 and 31 hand the ferry routes' bytes to the game's callers, which read them in the wide form only through our route iterator.

Big-city saves differ from the game's in these records (the 16-bit sets, the link table, the ferries, the density keys and the route bytes); standard saves stay byte for byte the game's.

## Compatibility

- **Record streams** ([save-format.md](save-format.md), "The record streams"): the vector and row readers refuse counts of 1,000,000 or more. A 512 by 512 grid has 262,144 cells, a 1024 by 1024 grid 1,048,576: over the limit if it is saved as one vector. The row writers only work for square grids.
- **Serializer** ([save-format.md](save-format.md), "The serializer and the segment streams"): a saved object over 250,000 bytes gets a checksum of its first 250,000 bytes only (it still loads); a class with 10,000,000 objects or more does not load.
- **Old saves:** unaffected. Standard sizes stay as they are.
- **Bigger cities in vanilla SC4:** will not open. They need a new region marker and a save flag so the original game refuses them cleanly instead of crashing.
- **Regions:** bigger tiles stay on the 1 km grid, so they mix with small, medium and large neighbours. Neighbour connections should work if they are made per edge cell (not checked); the region view and the thumbnails need the new size.
- **Plugins:** lots and buildings do not care about city size. DLL mods that hook fixed addresses in region or terrain code may; check during the census.
