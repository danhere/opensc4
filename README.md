# OpenSC4

An open-source, incremental reimplementation of SimCity 4 Deluxe, in the spirit of OpenRCT2 and OpenTTD.

OpenSC4 runs inside the original game and replaces its subsystems one at a time, until the original executable is no longer needed. The game is playable at every step.

**Requires a legitimate copy of SimCity 4 Deluxe (Steam, GOG or EA, build 1.1.641).** OpenSC4 never ships game assets or binaries.

## Status

Early development. Working today, inside SC4 Deluxe build 1.1.641:

- **In-process mod:** a launcher injects `core.dll` (hooks, logging, crash dumps, per-subsystem replacement toggles).
- **D3D11 renderer:** matches the stock driver in all 60 harness screenshots, runs past the old 2048 px cap, windowed and borderless modes.
- **DBPF reader:** byte-identical to the game's own reading of every record in a clean install.
- **Determinism and parity harness:** runs from the same save agree for a year; replacement simulators are compared exactly against the original.

Not yet playable as a standalone game. See [ROADMAP.md](ROADMAP.md) for progress.

## Documents

| Document | Contents |
|---|---|
| [ROADMAP.md](ROADMAP.md) | Phases P0-P9 with progress checklists |
| [docs/architecture.md](docs/architecture.md) | Loader, hooking layer, replacement registry, ABI rules, subsystem map |
| [docs/decisions.md](docs/decisions.md) | Decision log: approach, language, license, name, platform |
| [docs/class-creation.md](docs/class-creation.md) | Which classes GZCOM creates by class ID vs. direct construction |
| [docs/renderer.md](docs/renderer.md) | P1: how the game gets its graphics driver, how ours is installed, what the game calls |
| [docs/experiments/pollution-proxy.md](docs/experiments/pollution-proxy.md) | Experiment: a plugin-registered pass-through pollution simulator running a city |
| [docs/research.md](docs/research.md) | Feasibility research, existing projects, legal context, community etiquette |
| [docs/dev-environment.md](docs/dev-environment.md) | Windows / CachyOS / macOS roles and toolchain |

## Why option B (incremental) over a rewrite

- Playable from day one; each subsystem ships value on its own.
- Keeps the plugin ecosystem (NAM 50, DLL mods, thousands of lots) working throughout.
- A from-scratch engine would first have to reverse-engineer ~64% of undocumented save and simulator state before anything worked.

## What original assets limit

Buildings are pre-rendered sprites baked at fixed angles (4 rotations x 5 zooms, day and night). With original assets OpenSC4 can deliver modern resolutions, UI scaling, stability, performance, smoother zoom and better terrain and water, but not free camera angles or relit buildings. Those need new art, like OpenTTD's OpenGFX.
