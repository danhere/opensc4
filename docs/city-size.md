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

- **Terrain.** The terrain classes are unnamed in the Ghidra project. None of them reads slots 93/94, and no 257 constant sits in terrain-looking code. The next thing to find is where the heightmap gets its size, probably from the city through its own interface.
- **Region view and thumbnails** never ran under the coverage meter. A coverage run that opens the region view is needed before the spike.
- The class guesses for unnamed functions (`~cSC4Foo` in the CSV) come from named neighbours and are hints only.

## Spike after the census

Make the region offer an 8 km (512x512) city: patch the `config.bmp` colour decode for one test region (for example, accept white, code 7, as index 3; the tile vector already stores log2 of the side), create a new city there, and record where it fails. Expect the occupant manager's world grid to fail first. Each failure is one entry on the list above. This runs inside the 32-bit game, so it also shows how far 512 gets before memory is the problem.

## The 8 km spike

Started 2026-10-11. The region data class is ours now (`region.data.*`, [region.md](region.md)), so the layout decode needs no patch: with `[improvements] big_cities = on` in `opensc4.ini` (and `region.data.*` on), our `DecodeLayout` reads a white square (all three channels at 255) as an 8 km tile, stored as log2 index 3. Off by default; with it off the decode is the game's.

The occupant manager is ours too ([occmgr.md](occmgr.md)), but still sized for 256 cells: a 192-cell world grid with the city at 64 to 127. A 512-cell city needs 128 world cells; with a 64-cell margin each side the grid is 256 cells, indices 0 to 255, just inside the one-byte rectangles. With `big_cities` on, Init sizes a new city's grid that way (192 for every standard size, at most 256), and the clamps, walks and slot 7 follow the grid's own columns, so a saved grid of any size reads back. Off, everything is the constant 192 as before.

To run: a test region whose `config.bmp` has one 8x8 white square, then create a city there and record each failure below.

Results: not run yet.

## Compatibility

- **Old saves:** unaffected. Standard sizes stay as they are.
- **Bigger cities in vanilla SC4:** will not open. They need a new region marker and a save flag so the original game refuses them cleanly instead of crashing.
- **Regions:** bigger tiles stay on the 1 km grid, so they mix with small, medium and large neighbours. Neighbour connections should work if they are made per edge cell (not checked); the region view and the thumbnails need the new size.
- **Plugins:** lots and buildings do not care about city size. DLL mods that hook fixed addresses in region or terrain code may; check during the census.
