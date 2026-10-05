# Roadmap

The order is: tooling that reduces risk for everything else, then visible value that needs no simulator reverse engineering, then simulators from most isolated to most entangled, then the framework last.

Status key: `[ ]` not started, `[~]` in progress, `[x]` done.

## P0: Foundations

Output is a toolkit, not a game change. Everything later depends on it.

- [ ] Dev environment set up (see [docs/dev-environment.md](docs/dev-environment.md))
- [~] Toolchain: clang-cl + lld-link, CMake + Ninja, Windows reference build in CI. Skeleton builds and passes smoke tests on Windows (scoop + xwin); CI workflow exists but is paused (manual trigger only, never run green); still to do: re-enable and confirm CI, Linux/CachyOS cross-build
- [~] Ghidra project for Windows build 1.1.641, with Mac debug-symbol types ported (sc4-ghidra-symbols, version tracking / BinDiff). Rebuilt by [scripts/ghidra-setup.sh](scripts/ghidra-setup.sh): Function ID and the Mac type archives applied, 197 classes and about 1,600 functions named from the exe's own name table and gzcom-dll interfaces. Still to do: apply the Mac class layouts to Windows objects and name secondary vtables
- [~] Address database: names, addresses, signatures and calling conventions in one versioned source; `addresses.h` and hooks generated from it. Format, generator, byte checks and startup manifest done ([data/addresses-641.toml](data/addresses-641.toml), 11 entries); still to do: grow it as work needs addresses, and export the names Ghidra has into it
- [x] **Answer the class-creation question:** which game classes are created through GZCOM by class ID (can be swapped with no patching) and which with `new` (need detours)? This ratio decides how fragile option B is. **Answered** in [docs/class-creation.md](docs/class-creation.md): about 44% of instantiated classes (462 of ~1060) have GZCOM factories, including all 23 simulators; the rest need vtable swaps or detours. No outside code bypasses the simulators' vtables, and a pass-through pollution simulator registered by a plugin runs the city ([docs/experiments/pollution-proxy.md](docs/experiments/pollution-proxy.md)).
- [x] Launcher exe: start SC4 suspended, inject `core.dll`, resume. Verified with the Steam build (handles its DRM stub relaunch); Steam's Play button does not go through the launcher yet
- [x] `core.dll`: hook library (entry-only detours), GZCOM director, logging, crash dumps. Done: GZCOM director (gzcom-dll), logging, entry-only detours through MinHook with expected-byte checks (unit-tested, and tracing `cSC4App::LoadCity` in the game), minidumps for crashes in our code, and a hook guard that logs which hook a crash passed through (unit-tested, and checked in the game alongside the game's own report)
- [x] Unattended in-game test runs: `core.dll` loads a city in an isolated `-UserDir`, runs the simulation, quits without saving ([scripts/run-autotest.sh](scripts/run-autotest.sh))
- [x] Replacement registry with per-subsystem toggles and compare mode ([src/core/registry.h](src/core/registry.h)): modes original/ours/compare from `opensc4.ini` or `OPENSC4_REPLACE`, `subsystem.*` wildcards, applied only at startup phases, one manifest line per replacement. The pollution proxy and the LoadCity trace use it. Compare mode is unit-tested but has no game function to compare yet
- [x] Coverage meter: which original functions still execute. Replacement `diagnostics.coverage` with one-shot breakpoints on all 24,292 functions; a three-year test run executes 35.7% of them ([docs/dev-environment.md](docs/dev-environment.md), "Coverage")
- [~] Ecosystem patch census: what each popular DLL (NAM DLL, sc4fix, graphics-options, submenus, 0xC0000054 plugins) patches and where. Source-only pass done ([docs/patch-census.md](docs/patch-census.md)); still to do: confirm entry vs mid-function placement and function boundaries in Ghidra against the 641 exe
- [ ] Contact Null 45 (0xC0000054), memo33 and the NAM team before patching anything they patch

## P1: Renderer (first usable milestone)

- [ ] `cIGZGDriver` replacement registered through GZCOM (higher-version class ID; no code patching)
- [ ] Backend decision: D3D11 (recommended: best Wine/DXVK path) vs Vulkan
- [ ] Lift the hardware resolution cap (find the game-side cause of the >2048 px crash)
- [ ] Windowed / borderless modes, frame pacing
- [ ] Screenshot-diff harness: fixed test cities x 5 zooms x 4 rotations
- [ ] Coexists with or absorbs sc4-graphics-options

## P2: UI scaling and input

- [ ] Interim: driver-level UI scaling (render UI to an offscreen target and scale)
- [ ] Proper `cGZWin` layout scaling via detours
- [ ] Modern input: high-DPI mouse, wheel, rebinding

## P3: Resource manager / DBPF

- [ ] Our DBPF loader with exact plugin override precedence
- [ ] Faster loading, conflict and override diagnostics
- [ ] Coordinate with existing DBPF-loading DLLs

## P4: Save format and determinism (gate before any simulator work)

- [ ] **Prove the original game replays deterministically** (hook time sources, capture RNG, fixed sim-tick stepping). If it doesn't, parity becomes statistical, and that changes the second half of the plan. First data point: under real-time stepping, two identical runs of the same city diverge within two game months ([docs/experiments/pollution-proxy.md](docs/experiments/pollution-proxy.md)).
- [ ] Save parser / differ (offline tooling)
- [ ] In-memory state dumpers from Ghidra layouts
- [ ] Publish documented subfile schemas to the community

## P5: Isolated simulators

- [~] Pollution. A pass-through replacement registered through GZCOM already runs the city (experiment, replacement `simulators.pollution.proxy`)
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
