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

Not yet playable as a standalone game. Roughly 15-20% of the way to a full rewrite; measured stats and per-area estimates are in [ROADMAP.md, "Progress"](ROADMAP.md#progress), refreshed weekly.

## Improvements over the original

Things OpenSC4 adds to the game. Classic behaviour stays: an addition that changes how the game plays is an option in `opensc4.ini`, off until you turn it on; one that does nothing until you ask for it is simply there.

| Improvement | How to get it | Details |
|---|---|---|
| Bulldozer modes: only trees (hold Ctrl), only roads and rails (hold Shift), everything but those (hold both), a diagonal strip (hold Alt); a line by the cursor says which | always there | [docs/bulldoze.md](docs/bulldoze.md) |
| De-zoning that keeps the roads inside the area (hold Shift) | always there | [docs/dezone.md](docs/dezone.md) |
| Growify: plopped buildings turned into growable lots of a zone, with the `growify <zone> <density>` cheat | always there | [docs/growify.md](docs/growify.md) |
| Auto save every few minutes of play, with the last few saves kept as copies | `game.autosave = ours` | [docs/autosave.md](docs/autosave.md) |
| Timelapse: a dated copy of the city at every save, rendered into a video | `game.timelapse = ours` | [docs/timelapse.md](docs/timelapse.md) |
| City date sync: a city loaded with an earlier date than the last one saved in its region moves forward to it | `game.date_sync = ours` | [docs/date-sync.md](docs/date-sync.md) |
| One seed per city, shown in the title and set with the `seed` cheat | `game.seed = ours` | [docs/seeds.md](docs/seeds.md) |
| D3D11 renderer: resolutions past 2048 px, windowed and borderless modes, a scaled interface | `renderer.d3d11 = ours` | [docs/renderer.md](docs/renderer.md), [docs/ui-scaling.md](docs/ui-scaling.md) |
| 8 km city tiles (an experiment: such a city cannot be saved yet) | `[improvements] big_cities = on` | [docs/city-size.md](docs/city-size.md) |

## Documents

| Document | Contents |
|---|---|
| [ROADMAP.md](ROADMAP.md) | Phases P0-P9 with progress checklists |
| [docs/architecture.md](docs/architecture.md) | Loader, hooking layer, replacement registry, ABI rules, subsystem map |
| [docs/decisions.md](docs/decisions.md) | Decision log: approach, language, license, name, platform |
| [docs/class-creation.md](docs/class-creation.md) | Which classes GZCOM creates by class ID vs. direct construction |
| [docs/renderer.md](docs/renderer.md) | P1: how the game gets its graphics driver, how ours is installed, what the game calls |
| [docs/pollution.md](docs/pollution.md) | P5: the pollution simulator's structure and our reimplementation of it |
| [docs/experiments/pollution-proxy.md](docs/experiments/pollution-proxy.md) | Experiment: a plugin-registered pass-through pollution simulator running a city |
| [docs/city-size.md](docs/city-size.md) | What limits city tile size and the plan for bigger tiles |
| [docs/research.md](docs/research.md) | Feasibility research, existing projects, legal context, community etiquette |
| [docs/dev-environment.md](docs/dev-environment.md) | Windows / CachyOS / macOS roles and toolchain |

## Why option B (incremental) over a rewrite

- Playable from day one; each subsystem ships value on its own.
- Keeps the plugin ecosystem (NAM 50, DLL mods, thousands of lots) working throughout.
- A from-scratch engine would first have to reverse-engineer ~64% of undocumented save and simulator state before anything worked.

## What original assets limit

Buildings are pre-rendered sprites baked at fixed angles (4 rotations x 5 zooms, day and night). With original assets OpenSC4 can deliver modern resolutions, UI scaling, stability, performance, smoother zoom and better terrain and water, but not free camera angles or relit buildings. Those need new art, like OpenTTD's OpenGFX.

## License

OpenSC4 is licensed under the [GNU Lesser General Public License v2.1 or later](LICENSE) (LGPL-2.1-or-later). Why this license: [docs/decisions.md](docs/decisions.md#license-lgpl-21-or-later).

Third-party code in `third_party/` keeps its own license (gzcom-dll: LGPL-2.1; MinHook: BSD-2-Clause).

OpenSC4 requires an owned copy of SimCity 4 Deluxe and ships none of its assets or code. It is a non-commercial fan project, not affiliated with or endorsed by Electronic Arts or Maxis. SimCity is a trademark of Electronic Arts Inc.
