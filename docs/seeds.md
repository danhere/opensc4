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

Same random state is necessary, not sufficient. In normal play how much simulation runs per frame, and so the order of draws, follows real time ([experiments/determinism.md](experiments/determinism.md)): two players with the same save and seed start alike and drift apart within days to weeks of game time. The parity harness's determinism replacements remove that, but they also make the simulation run as fast as it can whatever the speed setting (10 game days in 2.4 s at turtle speed as at cheetah), so they are no way to play.

### Deterministic play

`[determinism] pace = realtime` keeps the harness's deterministic simulation and paces it to real time:

```ini
[replacements]
game.seed = ours
determinism.virtual_clock = ours
determinism.fixed_ticks = ours
determinism.agent_order = ours
determinism.zero_alloc = ours

[determinism]
pace = realtime
```

- **Slices are held back, not changed.** Each simulation slice still runs its fixed 60 ticks; before one starts, `fixed_ticks.cpp` checks a schedule and, if the game is ahead of it, returns without running it, so that frame does no simulation work. The simulator keeps no unit finer than the day (GetSimTime stays 0), so slices are spread over each day by how many the previous day took, at 2,000, 1,000 and 300 ms per game day for the three speeds (the unmodified game took 2.1 s per day at turtle speed on Making Money; cheetah is as fast as the machine allows).
- **The clock is virtual inside slices only.** Rendering, the interface and saving see real time, so animation and scrolling run at their normal speed; simulation code still sees the virtual clock, which advances only in slices.
- **The cycle clock is fixed.** The simulator recalibrates a milliseconds-per-cycle estimate at every slice (0x005D9370: a moving average of rdtsc against counter deltas, in floats), which its time-boxed work turns into deadlines. Its path to the virtual clock's 3 GHz depended on how many clock reads came between slices, so a different number of paused slices before the run moved a deadline by one check and the idle work with it. Paced, it is the value it converges to (1/3,000,000 ms per cycle).

- **Inside slices the clock catches up.** At the start of each slice the virtual clock moves up to the real time outside (never back). Some stopwatches span both sides: the camera's pause while scrolling (0x007E5B40) is started by the interface and ended by a stopwatch checked in the view update (0x007E5B70 from 0x007F6200), and while recording input on a separate frame clock the city stayed paused after the first keyboard scroll. With the catch-up, a paced Big City run with keyboard scrolling kept going (6 days, 2026-10-09).

Checked by running the same save at 0.5, 1 and 3 times the scheduled rate (`OPENSC4_PACE_SCALE`), seed 1234-5678, cheetah speed, 10 days:

| Save | 0.5x vs 1x | 3x vs 1x |
| --- | --- | --- |
| Big City d0090 | identical | identical |
| Rush Hour d0365 | identical | weather parts on day 10 (and stays apart, checked to day 15) |
| Making Money d1095 | identical | pollution grid parts on day 10 |

Repeat runs at the same rate are identical, so the 3x differences are systematic, not noise. The simulation's own clock reads inside slices are identical read for read between 1x and 3x (`OPENSC4_VCLOCK_TRACE`, Rush Hour d0365, 11 days), and holding slices during the simulator's hidden pauses changed nothing, so something outside the slices feeds the weather (and through the wind, pollution); the weather alone also reaches several outcomes in 35-day harness runs of the unmodified game ([weather.md](weather.md)). Not located yet. Before main gained the weather fingerprint field the same three saves were identical at 3x as well (2026-10-09 morning). Making Money d0365 parts on day 4 in the pollution grid between rates, as it does between repeat runs of the harness under load.

The speed setting is part of the history: it sets the frame rate cap the slice budget comes from, so the same save at turtle and at cheetah is not the same game. A replay has to repeat speed changes at the same slices, like any other input.

Not yet tried by hand: how play feels (the simulation runs in steps between held frames; at cheetah on a big city the schedule may be slower than the machine could go).

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
