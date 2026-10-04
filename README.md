# OpenSC4

An open-source, incremental reimplementation of SimCity 4 Deluxe, in the spirit of OpenRCT2 and OpenTTD.

OpenSC4 runs inside the original game and replaces its subsystems one at a time, until the original executable is no longer needed. The game is playable at every step.

**Requires a legitimate copy of SimCity 4 Deluxe (Steam, GOG or EA, build 1.1.641).** OpenSC4 never ships game assets or binaries.

## Status

Planning. Nothing is built yet. See [ROADMAP.md](ROADMAP.md) for progress.

## Documents

| Document | Contents |
|---|---|
| [ROADMAP.md](ROADMAP.md) | Phases P0-P9 with progress checklists |
| [docs/architecture.md](docs/architecture.md) | Loader, hooking layer, replacement registry, ABI rules, subsystem map |
| [docs/decisions.md](docs/decisions.md) | Decision log: approach, language, license, name, platform |
| [docs/research.md](docs/research.md) | Feasibility research, existing projects, legal context, community etiquette |
| [docs/dev-environment.md](docs/dev-environment.md) | Windows / CachyOS / macOS roles and toolchain |

## Why option B (incremental) over a rewrite

- Playable from day one; each subsystem ships value on its own.
- Keeps the plugin ecosystem (NAM 50, DLL mods, thousands of lots) working throughout.
- A from-scratch engine would first have to reverse-engineer ~64% of undocumented save and simulator state before anything worked.

## What original assets limit

Buildings are pre-rendered sprites baked at fixed angles (4 rotations x 5 zooms, day and night). With original assets OpenSC4 can deliver modern resolutions, UI scaling, stability, performance, smoother zoom and better terrain and water, but not free camera angles or relit buildings. Those need new art, like OpenTTD's OpenGFX.
