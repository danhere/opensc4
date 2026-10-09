# Roadmap

The order is: tooling that reduces risk for everything else, then visible value that needs no simulator reverse engineering, then simulators from most isolated to most entangled, then the framework last.

Status key: `[ ]` not started, `[~]` in progress, `[x]` done.

## P0: Foundations

Output is a toolkit, not a game change. Everything later depends on it.

- [~] Dev environment set up (see [docs/dev-environment.md](docs/dev-environment.md)). Windows done (scoop toolchain, Ghidra pipeline, isolated in-game test runs); CachyOS not yet
- [~] Toolchain: clang-cl + lld-link, CMake + Ninja, Windows reference build in CI. Skeleton builds and passes smoke tests on Windows (scoop + xwin); CI workflow exists but is paused (manual trigger only, never run green); still to do: re-enable and confirm CI, Linux/CachyOS cross-build
- [~] Ghidra project for Windows build 1.1.641, with Mac debug-symbol types ported (sc4-ghidra-symbols, version tracking / BinDiff). Rebuilt by [scripts/ghidra-setup.sh](scripts/ghidra-setup.sh): Function ID and the Mac type archives applied, 197 classes and about 1,600 functions named from the exe's own name table and gzcom-dll interfaces. Still to do: apply the Mac class layouts to Windows objects and name secondary vtables
- [~] Address database: names, addresses, signatures and calling conventions in one versioned source; `addresses.h` and hooks generated from it. Format, generator, byte checks and startup manifest done ([data/addresses-641.toml](data/addresses-641.toml), 11 entries); still to do: grow it as work needs addresses, and export the names Ghidra has into it
- [x] **Answer the class-creation question:** which game classes are created through GZCOM by class ID (can be swapped with no patching) and which with `new` (need detours)? This ratio decides how fragile option B is. **Answered** in [docs/class-creation.md](docs/class-creation.md): about 44% of instantiated classes (462 of ~1060) have GZCOM factories, including all 23 simulators; the rest need vtable swaps or detours. No outside code bypasses the simulators' vtables, and a pass-through pollution simulator registered by a plugin runs the city ([docs/experiments/pollution-proxy.md](docs/experiments/pollution-proxy.md)).
- [x] Launcher exe: start SC4 suspended, inject `core.dll`, resume. Verified with the Steam build (handles its DRM stub relaunch); Steam's Play button does not go through the launcher yet
- [x] `core.dll`: hook library (entry-only detours), GZCOM director, logging, crash dumps. Done: GZCOM director (gzcom-dll), logging, entry-only detours through MinHook with expected-byte checks (unit-tested, and tracing `cSC4App::LoadCity` in the game), minidumps for crashes in our code, and a hook guard that logs which hook a crash passed through (unit-tested, and checked in the game alongside the game's own report)
- [x] Unattended in-game test runs: `core.dll` loads a city in an isolated `-UserDir`, runs the simulation, quits without saving ([scripts/run-autotest.sh](scripts/run-autotest.sh))
- [x] Replacement registry with per-subsystem toggles and compare mode ([src/core/registry.h](src/core/registry.h)): modes original/ours/compare from `opensc4.ini` or `OPENSC4_REPLACE`, `subsystem.*` wildcards, applied only at startup phases, one manifest line per replacement. The pollution proxy and the LoadCity trace use it. Compare mode is unit-tested but has no game function to compare yet
- [x] Coverage meter: which original functions still execute. Replacement `diagnostics.coverage` with one-shot breakpoints on all 34,556 functions (including about 10,400 that Ghidra's auto-analysis had left undefined); a six-year test run executes 32.6% of them ([docs/dev-environment.md](docs/dev-environment.md), "Coverage")
- [x] Ecosystem patch census: what each popular DLL (NAM DLL, sc4fix, graphics-options, submenus, 0xC0000054 plugins) patches and where. Source pass and Ghidra placement check done ([docs/patch-census.md](docs/patch-census.md), section 9): only 3 of 232 code sites are at a function entry. The 27 sites that were in code Ghidra had left out of functions are all mid-function now. Still to do: transcribe UI Modernization's remaining immediates
- [ ] Contact Null 45 (0xC0000054), memo33 and the NAM team before patching anything they patch

## P1: Renderer (first usable milestone)

- [x] Pass-through `cIGZGDriver` in front of the game's driver, with a call log. Installed with `cIGZGraphicSystem2::SetGDriver` at `PreAppInit`, because the graphics system never instantiates new driver class IDs; no class ID taken, no code patching ([docs/renderer.md](docs/renderer.md))
- [x] Backend decision: D3D11 first (best Wine/DXVK path), behind an internal render interface so a Vulkan backend can follow (2026-10-05)
- [~] D3D11 driver (`renderer.d3d11`, selected whenever on; [docs/renderer.md](docs/renderer.md), "D3D11 driver"): renders the city, with clouds too; matches the stock driver in all 20 screenshots, once it reported the same capabilities the game configures itself from (`Graphics Rules.sgr`). Full-screen modes change the display mode like the stock drivers (desktop-size full screen is borderless). Menus, the region view and UI interaction (clicks, tooltips, wheel, edge scrolling, dragging, zoning) checked unattended with synthesized input; that found and fixed swapped buffer-region arguments, which broke scrolling. What every driver call means is documented from both stock drivers. Still to do: a longer hands-on play session
- [x] Lift the hardware resolution cap. The >2048 px crash is in the stock DirectX driver (its device setup fails and it then writes through a null pointer); the D3D11 driver runs 3440x1440 ([docs/renderer.md](docs/renderer.md), "Resolutions")
- [x] Windowed / borderless modes, frame pacing. Full-screen modes change the display mode, desktop-size full screen is borderless; the window shows at most one frame per refresh without slowing the game's loop or simulation; settings in `opensc4.ini` `[renderer]` ([docs/renderer.md](docs/renderer.md), "Frame pacing"). Measured: the game redraws a running city only about 15 times a second at speed 2, and its loop never sleeps (a CPU core busy even when paused, as with the stock driver); both are game-loop matters, left for later
- [x] Screenshot-diff harness: fixed test cities x 5 zooms x 4 rotations. Three tutorial cities (small town, dense downtown, highways) with fixed random seeds, one command for all ([scripts/run-shots-all.sh](scripts/run-shots-all.sh)); animation still runs on real time, so comparisons use a small tolerance. The D3D11 driver matches stock in all 60 shots ([docs/renderer.md](docs/renderer.md), "Screenshot harness")
- [~] Coexists with or absorbs sc4-graphics-options. Absorbed with the D3D11 driver: driver choice, resolution and window mode without the command line (`opensc4.ini` `[renderer]`), borderless full screen, 32-bit colour. Not renderer work: its intro and pause-on-focus-loss switches. Its "force draw on scroll" render properties are untested with the D3D11 driver. Running both together not tried

## P2: UI scaling and input

- [x] Interim: driver-level render scale (`[renderer] scale`; the game runs at the screen size divided by it and the D3D11 driver draws at full size; [docs/ui-scaling.md](docs/ui-scaling.md)). Interface images drawn with sharp bilinear filtering. Mouse input checked unattended
- [~] Proper scaling: interface enlarged, city at native size. Done without `cGZWin` layout detours: the city camera's projection is wrapped so the city keeps its full-resolution size (the default; `scale_city = yes` enlarges it too). **Tabled by Dan (2026-10-06):** crisp text and a native-size region view. Both are pictures the game composes on the CPU at game-pixel size, so they need the game's 2D drawing layer to work at full resolution (weeks; findings and two possible routes in [docs/ui-scaling.md](docs/ui-scaling.md), "Text and the region view")
- [ ] Modern input: high-DPI mouse, wheel, rebinding

## P3: Resource manager / DBPF

- [x] Survey ([docs/resources.md](docs/resources.md)): segment order and override rules measured in the running game (`diagnostics.dbpf_survey`); on the vanilla game building all segments takes under 40 ms, so loading speed only matters with large plugin folders
- [x] Our DBPF reader (format, index, compression) as a tested library ([src/dbpf](src/dbpf), `opensc4-dbpf` tool); byte-identical to the game's own reading for all 111,838 records of the clean install, and reads every saved city in the test region. Also the base for P4, since saved cities are DBPF files
- [ ] Our DBPF loader with exact plugin override precedence (top level: first segment wins; inside a folder: last file wins, top-level files before subfolders, alphabetical)
- [ ] Faster loading, conflict and override diagnostics (pays off with mods; absorbs sc4-dbpf-loading)
- [ ] Coordinate with existing DBPF-loading DLLs

## P4: Save format and determinism (gate before any simulator work)

- [x] **Does the original game replay deterministically?** Answered (2026-10-06, [docs/experiments/determinism.md](docs/experiments/determinism.md)): **no**, not as a whole. Two runs from the same save with fixed random seeds (`determinism.fixed_seed`), a fixed-step clock on every thread (`determinism.virtual_clock`) and one CPU still end in different cities after a year. Random numbers are ruled out (seeding and `rand()` sequences identical); left are thread interleaving (a city-load worker thread with its own time budget) or address-ordered processing. With those controls, runs **stay identical for at least the first 8 game days** (first population difference on day 9, 27 and 29 in three pairs).
  - **Decision (Dan, 2026-10-06):** parity testing uses short windows: from many saves, a few game days with the original and with a replacement, compared exactly (daily fingerprints, the replaced simulator's own state), plus statistical comparison over long runs. **Tabled, not dropped:** finding the root cause of the divergence (next steps in the experiment doc: serialise or trace the worker thread, a deterministic per-thread allocator for address order).
  - **Root cause found (2026-10-07, Dan un-tabled it):** real time reached the simulation through six paths (ticks per frame, the simulation clock's pacing, idle-time budgets, rdtsc in simulation code, our own seed replacement, time()-based seeds), not the worker thread. With `determinism.fixed_ticks` added and the other two replacements extended, runs agree in every daily fingerprint for a year. The outcome still depends on heap layout, so compared runs need identical environments.
- [~] Save parser / differ (offline tooling): record-level diff (`opensc4-dbpf diff`) and an address-aware diff that separates moved heap addresses from real differences ([tools/re/savediff.py](tools/re/savediff.py)); record types named from the exe's class table. First schema: `cSC4PollutionSimulator`, decoded exactly in 531 saves across versions 2-4 ([docs/save-format.md](docs/save-format.md), [tools/re/sc4save.py](tools/re/sc4save.py))
- [ ] In-memory state dumpers from Ghidra layouts
- [ ] Publish documented subfile schemas to the community

## P5: Isolated simulators

Parity method (from P4): short-window exact comparison against the original from many saves, plus statistical comparison over long runs.

- [x] Parity harness (docs/parity.md): `scripts/run-parity.sh`, a week from each of 12 saves, compared exactly. With the determinism replacements (docs/experiments/determinism.md, third round) the original agrees with itself on all 12, and runs agree for a year. Limit: the outcome depends on heap layout (both runs get identical environments)
- [~] Pollution. A pass-through replacement registered through GZCOM already runs the city (experiment, replacement `simulators.pollution.proxy`); under the harness it passes all 12 saves exactly, after fixing a 64-bit return bug. Reimplementation in increments on the original object ([docs/pollution.md](docs/pollution.md)): the getters, setters and queries (66 of 71 interface slots, `simulators.pollution.interface`) and the simulation itself, the tract walk (`simulators.pollution.tracts`), are ours, with its helpers for garbage disposal and the building lists; they match the original on all 12 parity saves and for a year from two; and the handler for buildings added and removed (pollution footprints); the MonthPassed handler, the end-of-pass averages the worst tracts and pollution effects and the save (Write); the building-list and monthly helpers, checked on a second corpus with garbage and water buildings; the monthly aging, the recycling effect and the load (Read), the tuning load, Init and Shutdown; and our own object under the game's class ID (`simulators.pollution.object`), so with every replacement on, the city's pollution simulator is ours
- [x] Power ([docs/power.md](docs/power.md)): every function the game runs is replaceable (`simulators.power.*`), and `simulators.power.object` is our own object under the class ID. Parity with everything on: 12 of 12 saves and 3 of 3 for a week, 2 of 2 for 90 days, save records identical; the network is woken and edited by test switches (`OPENSC4_TEST_POWER_WAKE`, `OPENSC4_TEST_POWER_MESSAGES`), since the test cities never edit it. Four rare paths are not reached yet (the plant disaster case, `CreateBitArrays`, `OccupantCell`, `FindConnectedOccupant`).
- [x] Water ([docs/water.md](docs/water.md)): every function the game runs is replaceable (`simulators.water.*`) but generic container helpers, and `simulators.water.object` is our own object. Parity with everything on: 12 of 12 and 3 of 3 for a week, 2 of 2 for 90 days, save records identical; woken by `OPENSC4_TEST_WATER_WAKE`. Not reached yet: `FindOtherSources`, `ObjectCell`, `Serve`, `OnPipe`.
- [~] Traffic ([docs/traffic.md](docs/traffic.md)), on vanilla, with the functions NAM patches left as the game's: 24 of 43 interface slots, the step, the sub-step, the month (funding, strike check), DoMessage and 15 of the 17 phase functions are ours (`simulators.traffic.*`). Parity with all on: 12 of 12, 3 of 3, 2 of 2 for 90 days. Still the game's: phases 17 and 18 (trip routing), Init, Shutdown, CityInited, Read, Write, the remaining slots.
- Order from here (Dan, 2026-10-08): (1) a coverage test that reaches the paths no test city runs (network edits, a new city, the disaster cases), (2) traffic, (3) fire and police, (4) land value, (5) the remaining simulators, (6) back to the renderer and UI.
- [ ] Crime
- [ ] Coverage sims (health, education, fire, police)
- [ ] Land value

## P6: Economy simulators

- [ ] Demand / RCI
- [ ] Budget
- [ ] Population
- [ ] Lua 5.0 bindings

## P7: Networks and traffic (only together with the NAM team)

- [ ] RUL0/1/2 engine and network tools, either implemented jointly or preserving the NAM DLL's hook points
- [ ] Traffic simulator

## P8: Remaining world systems

- [ ] Lots / buildings / growth
- [ ] Terrain
- [ ] Region view
- [ ] Audio

## P9: Framework and host mode (exe independence)

- [ ] Own GZCOM framework implementation (scion as reference)
- [ ] Host mode: our exe maps the original exe's sections, then stops needing them
- [ ] Address shim for ecosystem DLLs that hook fixed addresses
- [ ] **Exe independence:** the coverage meter shows zero original code executed in the regression suite

## Beyond the original: bigger city tiles

Asked by Dan (2026-10-08). Findings and plan: [docs/city-size.md](docs/city-size.md).

**Timing (Dan, 2026-10-09):** bigger cities land with P8, when the occupant manager, terrain and region view are replaced anyway; until then every replacement takes sizes from the city, never constants. 8 km (512 cells) does not need the 64-bit build; 16 km and up likely does (P9).

- [x] Research: what limits city size (three sizes in the region layout, the 32-bit address space, simulation time; not the renderer or the save format), and what our simulators assume (they read the size from the city)
- [x] Size census (`tools/ghidra/CitySizeCensus.java`): found the region `config.bmp` decode (three sizes, stored as log2 km), the occupant manager's fixed 192x192 world grid (256-cell margin, max 256-cell city), `cSC4City::GetCitySizeType` and the per-size tables and art it indexes; 169 functions read the size instead
- [ ] Find where terrain gets its heightmap size (terrain classes unnamed, not seen by the census)
- [ ] Coverage run that opens the region view (region code never ran under the meter)
- [ ] Spike: an 8 km (512x512) city in a test region, recording where the 32-bit game fails (expect the occupant manager first)
- [ ] Size-aware replacements for the census list: occupant manager world grid and size type (P8 lots/buildings), region decode, per-size art and tables (P8 region view), render initial extent (P1)
- [ ] Keep every replacement size-agnostic (sizes from the city or the save, never constants); add a check to new simulator work
- [ ] Region layout and region view with a fourth size, and a save flag so vanilla SC4 refuses bigger cities cleanly
- [ ] 64-bit build after exe independence (P9), removing the memory ceiling
- [ ] Measure simulation time at 512 and 1024 cells per side
