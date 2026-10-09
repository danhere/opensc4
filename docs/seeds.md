# Shareable seeds

One seed per city decides the game's random numbers: the same save (or the same empty region tile) with the same seed starts from the same random state, so a friend can be handed a city and a seed ("try 1F2E-3D4C on this one"). Replacement `game.seed`, [src/core/game_seed.cpp](../src/core/game_seed.cpp); it shares the seed hooks of `determinism.fixed_seed` ([src/core/fixed_seed.cpp](../src/core/fixed_seed.cpp)).

## Using it

```ini
[replacements]
game.seed = ours

[game]
seed = random      ; or 8 hex digits: 1F2E-3D4C (the dash is optional)
```

- `random` (the default) gives every city load a new seed; a fixed value gives every city that seed.
- The window title shows the loaded city's seed: `SimCity 4 - seed 1F2E-3D4C`.
- In the game, the cheat box (Ctrl+X) takes `seed` (shows the city's seed and the one the next load will use), `seed 1F2E-3D4C` (use this seed from the next city load or founding on) and `seed random`. The answer comes in a message box; we have no in-game interface of our own yet (ROADMAP P2).
- A seed takes effect when a city is loaded or founded, not in a running city: reseeding mid-game would make the "same scenario" depend on when the seed was typed.

## What it covers

The game draws from generator objects of two kinds: cRZRandom (a 32-bit LCG, seed 0x00913667) and cRZRandomSuper (a Mersenne twister, seed 0x00913977). A trace of every draw over 14 days of Making Money (`diagnostics.trace.rng`, `OPENSC4_RNG_DUMP_DAY=all`) shows where the numbers come from:

| Generator | Draws in 14 days | Drawn by (nearest named code where the function has no name) |
| --- | --- | --- |
| static 0x00B4BC78 | 22,325 | building development (0x0074B460, 0x0074B6A0) |
| static 0x00B22DF8 | 24,827 | 3D effects and particles (0x005B7580), per frame |
| heap, per city | 5,669 | the traffic network map and vehicles (0x004AE200, 0x004AFE50) |
| static 0x00B49AF0 | 1,354 | lot developer, base textures |
| static 0x00B4D530 | 723 | 0x00761E70 (between the police and pollution simulators; unnamed) |
| static 0x00B23078 | 472 | lot base texture occupants |
| stack, per tract | 469 | cSC4TractDeveloper::UpdatePopulation, seeded from the tract position |
| static 0x00B22C10 | 348 | 0x00583480, next to the disaster code |
| static 0x00B21D78 | 269 | vehicles (cSC4Vehicle::Create) |
| heap | 168 | a Lua C function (0x0040A810, called from luaD_precall): the advisors' and news scripts |
| static 0x00B49BD0 | 92 | pollution effects ([pollution.md](pollution.md)) |
| static 0x00B4A5C0, heap | 106, 18 | the prop developer; the traffic simulator |

Three kinds of seeding, three treatments:

- **Static generators** (37 of them in build 641, in the exe's own data, made by static initialisers and never freed). They are seeded once at startup and drawn by everything that happens before a city loads (the region view, other cities in the session), so their state at a city load was the session's history. `game.seed` records each one at its first seed call and, when a city starts loading (`cSC4App::LoadCity`, 0x004555F0) or is founded (the city constructor 0x00459F30, outside LoadCity), seeds it from `Mix(seed ^ its address)`.
- **Clock seeds** (seed 0xFFFFFFFF: time() ^ clock()), requested by objects as the city builds them (about 1,000 on a load). After the reseed they get `Mix(Mix(seed ^ call site) + n)`, n counting that site's requests from the load on, so neither other sites nor earlier cities move them. Two generators are seeded from the time of day by their own code rather than with 0xFFFFFFFF (the prop developer, made in cSC4PropDeveloper::Create; the traffic simulator's Init, 0x0071F1A5); they are treated the same way.
- **Seeds the game computes** (tract positions, constants) are the city's own and pass through.

So the random state after a load is a function of the save and the seed alone.

## Same seed, same game?

Same random state is necessary, not sufficient. In normal play how much simulation runs per frame, and so the order of draws, follows real time ([experiments/determinism.md](experiments/determinism.md)): two players with the same save and seed start alike and drift apart within days to weeks of game time. With the determinism replacements of the parity harness (`determinism.fixed_seed`, `virtual_clock`, `fixed_ticks`, `agent_order`, `zero_alloc`) a seed replays exactly, but those make the simulation run as fast as it can and the main thread's clock virtual, which is fine for tests, not for play. A play mode with the same guarantee (deterministic simulation, paced to real time) is the next step, and is what a recorded replay (timelapse) needs too.

## Checks

`[game] test_scramble = <n>` (or `OPENSC4_GAME_TEST_SCRAMBLE`) puts every static generator in a state made from n right after startup, as a long session before the first city would. With the parity harness ([parity.md](parity.md)), 12 saves, 7 days, two jobs:

| A | B | Result |
| --- | --- | --- |
| seed 1234-5678 | seed 1234-5678 after a scramble (7) | **12 of 12 same** (one save after a retry: the A side itself has two outcomes there in the traffic record, as under load before) |
| seed 1234-5678 | the same after another scramble (99), with the time-of-day sites covered | 11 of 12 same; Big City d0365 partly: identical fingerprints for all 8 days, a few traffic record counts apart, A two outcomes in four runs and B three |
| no `game.seed` | scramble (7) | 0 of 12 same: without the seed, what came before the load changes the city from the first day |
| seed 1234-5678 | seed 9ABC-DEF0 | 0 of 12 same: the seed decides the city's randomness |

In the game (real time, no determinism replacements, Making Money): the title showed `SimCity 4 - seed 1234-5678` after the load, and the cheat box took `seed` and `seed 0BAD-F00D`, both answered (2026-10-09; typed with synthesized keys after a click into the city view, which the cheat box needs for keyboard focus).

## Limits and notes

- With `simulators.traffic.init = ours` the traffic simulator's generator is seeded by our Init from core.dll, which the call-site check does not recognise; it stays seeded from the time of day in play (18 draws in 14 days).
- `diagnostics.trace.loadcity` hooks the same function as `game.seed`; only one of the two can be on.
- The cheat box matches a cheat on its first word and passes the whole text on, so `seed 1F2E-3D4C` arrives whole (checked in the game).
- The message box has only been seen in the log so far: unattended runs (`OPENSC4_TEST_CITY` set) write the answer there instead.
