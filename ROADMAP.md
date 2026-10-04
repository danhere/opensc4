# Roadmap

The order is: tooling that reduces risk for everything else, then visible value that needs no simulator reverse engineering, then simulators from most isolated to most entangled, then the framework last.

Status key: `[ ]` not started, `[~]` in progress, `[x]` done.

## P0: Foundations

Output is a toolkit, not a game change. Everything later depends on it.

- [ ] Dev environment set up (see [docs/dev-environment.md](docs/dev-environment.md))
- [ ] Toolchain: clang-cl + lld-link, CMake + Ninja, Windows reference build in CI
- [ ] Ghidra project for Windows build 1.1.641, with Mac debug-symbol types ported (sc4-ghidra-symbols, version tracking / BinDiff)
- [ ] Address database: names, addresses, signatures and calling conventions in one versioned source; `addresses.h` and hooks generated from it
- [ ] **Answer the class-creation question:** which game classes are created through GZCOM by class ID (can be swapped with no patching) and which with `new` (need detours)? This ratio decides how fragile option B is.
- [ ] Launcher exe: start SC4 suspended, inject `core.dll`, resume
- [ ] `core.dll`: hook library (entry-only detours), GZCOM director, logging, crash dumps
- [ ] Replacement registry with per-subsystem toggles and compare mode
- [ ] Coverage meter: which original functions still execute
- [ ] Ecosystem patch census: what each popular DLL (NAM DLL, sc4fix, graphics-options, submenus, 0xC0000054 plugins) patches and where
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

- [ ] **Prove the original game replays deterministically** (hook time sources, capture RNG, fixed sim-tick stepping). If it doesn't, parity becomes statistical, and that changes the second half of the plan.
- [ ] Save parser / differ (offline tooling)
- [ ] In-memory state dumpers from Ghidra layouts
- [ ] Publish documented subfile schemas to the community

## P5: Isolated simulators

- [ ] Pollution
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
