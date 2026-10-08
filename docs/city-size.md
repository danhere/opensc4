# City size: what limits it and how we lift it

Status: research, 2026-10-08. Nothing here is implemented. Roadmap: "Beyond the original: bigger city tiles" in [ROADMAP.md](../ROADMAP.md).

## What the game allows

Three city sizes, all square: small 64x64 cells (1 km), medium 128x128 (2 km), large 256x256 (4 km). A cell is 16 m.

The region layout is `config.bmp`: one pixel per 1 km square, coloured by the size of the city that covers it (red 1 km, green 2 km, blue 4 km; a channel counts only at exactly 255). Any other colour leaves that square empty. Cities are aligned to the 1 km grid and must not overlap ([Simtropolis, "Config.bmp: How to Make it Yours"](https://community.simtropolis.com/omnibus/simcity-4/mapping-terraforming/configbmp-how-to-make-it-yours-r76/)). There is no colour or setting for a bigger city.

The modding community has never produced a bigger tile: the answer has always been that it needs changes to the exe ([Simtropolis, "Bigger city tiles"](https://community.simtropolis.com/forums/topic/24086-bigger-city-tiles/)). That is exactly what this project can do.

## What actually limits it

| Limit | Kind | Notes |
|---|---|---|
| Region layout knows three sizes | Hard, in the exe | The `config.bmp` reader and the region view. Nothing else offers a fourth size. |
| Code that assumes 64/128/256 | Unknown, in the exe | Not measured yet (see "Size census"). Most of the simulators we have read do not assume it (below). |
| 32-bit address space | Hard, until P9 | At most 4 GB with the large-address flag. Dense large cities with big plugin folders already run out. Every doubling of a city's side is 4x the cells and roughly 4x the memory for grids, lots and props. |
| Simulation time | Soft | Grid simulators scale with area (4x per doubling). Traffic routing grows faster than area. Fine on a modern CPU for 2x; 4x needs measuring. |
| Renderer | Not a blocker | The D3D11 driver already runs past the old 2048 px cap. Draw distance and terrain detail are tuning, not limits. |
| Save format | Soft | Saves are DBPF files; records carry their own sizes. A bigger city needs no new container, only readers that take sizes from the save. |

## What our code tells us so far

- The city reports its own size: `cISC4City` slots 93 and 94 give cells in x and z ([data/addresses-641.toml](../data/addresses-641.toml), `SC4::City`). Our power simulator reads it the same way (`CityCellsX`, [src/core/power_sim.cpp](../src/core/power_sim.cpp)).
- The simulators we have read build their grids from that size, not from constants: pollution's eight `cSC4SimGridSint16` grids and its per-8x8-block vectors ([pollution.md](pollution.md), Init), power's bit arrays and 8x8 blocks ([power.md](power.md)), water's cell flags with a stride and height saved in the record ([water.md](water.md), version 4). Traffic's grids look the same, but are not checked yet.
- The save records we decode store grid dimensions with the data (water's stride and height, power's consumption-map rows and columns). They should load at any size.
- The renderer's camera works in metres per pixel and 16 m tiles ([ui-scaling.md](ui-scaling.md)); it has no city-size table.

So the simulation side looks size-agnostic. The likely blockers are in the parts we have not read yet: the region, terrain, the lot and network managers, and the city thumbnails.

## Size census (next step)

A static scan of `SimCity 4.exe` 1.1.641 in the Ghidra project, ideally as a script under `tools/ghidra` so it can be rerun:

1. Immediates 64, 128, 256, 257 (heightmap points per side), 1024, 4096 and the matching shifts (6, 7, 8) and masks (0x3F, 0x7F, 0xFF) near city-size reads (slots 93/94, `this+0x174`-style cached sizes).
2. Floats 1024.0, 2048.0, 4096.0 (city size in metres) and 16.0 used next to them.
3. Fixed-size arrays and `new` calls whose size is a multiple of 256x256 or 257x257.
4. The `config.bmp` reader and region tile code: how it turns colours into sizes and where it stores them.
5. Every hit gets a class name, and its coverage-meter status (does it run in the test cities).

The output is a list in this document: which code assumes a size, and which phase (P5 to P8) replaces it.

## Spike after the census

Make the region offer an 8 km (512x512) city: patch the `config.bmp` colour decode for one test region, create a new city there, and record where it fails. Each failure is one entry on the list above. This runs inside the 32-bit game, so it also shows how far 512 gets before memory is the problem.

## Compatibility

- **Old saves:** unaffected. Standard sizes stay as they are.
- **Bigger cities in vanilla SC4:** will not open. They need a new region marker and a save flag so the original game refuses them cleanly instead of crashing.
- **Regions:** bigger tiles stay on the 1 km grid, so they mix with small, medium and large neighbours. Neighbour connections should work if they are made per edge cell (not checked); the region view and the thumbnails need the new size.
- **Plugins:** lots and buildings do not care about city size. DLL mods that hook fixed addresses in region or terrain code may; check during the census.
