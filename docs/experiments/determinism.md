# Does the game replay deterministically? (P4, 2026-10-06)

ROADMAP P4's first item: if two runs of the same city from the same save do not end in the same state, exact parity tests for replaced simulators are impossible and parity becomes statistical. This experiment compares the **saved city** after identical runs, record by record.

## Method

- [scripts/run-save-test.sh](../../scripts/run-save-test.sh) `<build> <out.sc4> <days>`: loads the Making Money Tutorial, runs the simulation at speed 2 until its day number has advanced by `<days>` (`OPENSC4_TEST_DAYS`), pauses, saves the city in place (`OPENSC4_TEST_SAVE=1`) and copies the file out. It runs in a disposable user folder (`%LOCALAPPDATA%\opensc4\p4env`) refreshed from the test region before every run: saving rewrites the city and six neighbours. (`cISC4App::SaveCity(path, fastSave)`, slot 15, 0x00455E20, given a path outside the region, still saved over the city's own file and its neighbours; not understood. The first attempt did this to the test region itself; it was restored from the play folder's copy, made before, and checked identical.)
- All runs stopped on the same day number with simulation time 0, so they cover exactly the same simulated span.
- [tools/re/savediff.py](../../tools/re/savediff.py) `--pointers a b`: saves are never byte-identical, because records store their objects' heap addresses (as IDs and references) and a CRC per object, and heap addresses differ on every run. The tool matches address-like values between the two saves, one to one and consistently across the whole file, and skips the per-object CRCs (the word before a matched address, after an equal size word); what is left is reported per record. `opensc4-dbpf diff` gives the raw record-level comparison.
- Record types are the serialised objects' class IDs, named from the exe's own class-name table.

## Results

| Runs (each pair: same save, same settings) | Records differing beyond moved addresses | Size changes |
| --- | --- | --- |
| 0 days (load, pause, save), fixed seeds | 7 | none |
| 30 days, fixed seeds | 14 | 2: traffic network map, thumbnail |
| 365 days, fixed seeds | 44 | 17, incl. lots, buildings, flora, lot developer |
| 365 days, fixed seeds, virtual clock | 44 | 18, incl. lots, buildings, flora |
| 365 days, fixed seeds, virtual clock, `-CPUcount:1` | 43 | 17 |
| 1 day, fixed seeds, virtual clock | 11 | 2: traffic network map, thumbnail |
| 7 days, fixed seeds, virtual clock | 9 | 2: traffic network map, thumbnail |

- **Zero days:** 122 of 148 records differ byte-wise, 115 of them only by moved addresses and CRCs. The other 7 (`cSC4Demand`, `cSC4Advice`, `cSC4MySimAdvice`, `cSC4MySimBalloonAdvice`, `cSC4AutomataControllerManager`, `cSC4AutomataGenerator`, `cSC4TrafficNetworkMap`) are the noise floor of this method: some look like containers ordered by address (same values, different order), and `cSC4Demand` has fields that hold pointer-like values in one save and float defaults in the other, which could be uninitialised memory written to the file. Not confirmed either way.
- **The game is not deterministic**, even with every random seed fixed (`determinism.fixed_seed`), a virtual clock on the main thread, and one CPU. After a year the two cities have developed differently: `cSC4Lot`, `cSC4BuildingOccupant`, `cSC4FloraOccupant`, `cSC4LotDeveloper`, `cSC4PropDeveloper` and others differ in size.
- **Divergence starts in traffic.** After one and seven days, development is still identical; what differs beyond the noise floor is `cSC4TrafficNetworkMap` (size), `cSC4Vehicle`, the automata records, the region thumbnail, and one byte in each advisor record.

## What was ruled out, and what is left

- **Random seeds:** fixed; not sufficient.
- **The simulator's per-frame time budget** (0x00703E70 runs simulation ticks until a `QueryPerformanceCounter` stopwatch passes 15-33 ms): `determinism.virtual_clock` gives the main thread a counter that advances 1 ms per call, so every frame runs the same number of ticks. Not sufficient on its own. (A year then runs in 4.3 s instead of 21 s. The game's save hangs under the virtual clock, so the autotest returns the main thread to real time first.)
- **Threads:** `-CPUcount:1` made no difference. Other threads still see real time.
- **Left to test:** other time sources (`clock()`, `time()`, `GetLocalTime`, `GetSystemTimeAsFileTime`, stopwatches in `rdtsc` mode, which an import hook cannot see; the automata's animation time); the traffic simulator's own scheduling; and heap addresses: if the simulation processes objects in address order (the zero-day saves show address-ordered containers), the outcome can depend on where the heap put things, which differs on every run. Controlling that means a deterministic allocator behind the game's `operator new`.

## Second round: daily fingerprints (2026-10-06)

The save diff turned out too noisy to bisect with: the address-matching heuristic left 7 to 20 records unexplained for setups that should behave alike. So the autotest got a **daily fingerprint** (`OPENSC4_TEST_FINGERPRINT=1`): every game day it logs population, funds, lot count and checksums of the traffic (congestion, trip length, commercial, air-polluting), land value and pollution grids, read through the game's interfaces. Numbers, so heap layout does not matter. [tools/re/fpdiff.py](../../tools/re/fpdiff.py) compares two logs and names the first differing day. 60-day pairs, fixed seeds and virtual clock throughout:

| Pair | First day population or lot count differs | After 60 days (pop/lots) |
| --- | --- | --- |
| main-thread virtual clock | 9 | 2060/430 vs 2060/432 |
| plus `srand` pinned | 27 | 2060/432 vs 2060/432 |
| virtual clock on every thread | 29 | 2060/433 vs 2060/432 |

What this round established:

- **Random numbers are not the cause.** Seeding is identical: all ~1,070 generator seeds are set on one thread in the same order, by the same callers, until the runs have already diverged (then the counts differ because different objects get created). The C runtime's `rand()`, which simulation code calls directly (0x006BCFC0 five times, 0x0049CC05 most of all) besides Lua's `math.random`, gives **identical call sequences over 20 days** (8,448 calls, same caller and thread each time, traced with `OPENSC4_RAND_TRACE`). `srand` is never called, so it is not seeded from the clock either.
- **Threads.** The game starts five threads (`diagnostics.trace.threads`): two before the app initialises, one from its own `CreateThread` (0x0086ABC0, probably audio), and one during city load whose routine (0x0048E8F0 -> 0x0048E460) takes rectangular jobs from a locked queue and works on them under a stopwatch budget, like the main simulator loop. Its owner (vtable 0x00A8ED5C, four slots) is not named in the exe. Giving every thread its own fixed-step clock made the first divergence later but did not remove it; the order in which this worker and the main thread interleave is still up to Windows.
- **Divergence is sporadic**: the first population difference came on day 9, 27 and 29 in three pairs, and grid checksums sometimes differ for a day and agree again (the pollution update landed on day 4 in some runs and day 5 in others; that may be the fingerprint's sampling point, taken on the first frame after the day changes, rather than the simulation). Every pair so far was **identical for at least the first 8 game days**. (Later: it was the sampling point, and the fingerprint now comes from inside the simulator's tick. Over 12 saves of three cities, the traffic-heavy Rush Hour city diverged within a week in 3 of 4 saves even so; see [../parity.md](../parity.md).)
- Not tested: heap-address ordering (needs a deterministic allocator behind the game's `operator new`, per thread), and serialising the worker thread with the main thread.

## Conclusion of the bounded round

Exact whole-game determinism is not within reach of a few hooks. The remaining sources (thread interleaving, possibly address-ordered processing) need deep control of the game's threads or memory, with no guarantee. What does hold, with `determinism.fixed_seed` and `determinism.virtual_clock`: two runs from the same save agree exactly for at least a week of game time. That suggests short-window parity for P5: start from a save, run a few game days with the original and with a replacement, compare fingerprints (and the replaced simulator's own state) exactly, and repeat from many saves; plus statistical comparison over long runs.

Tooling from this experiment: `opensc4-dbpf diff`, `tools/re/savediff.py`, `scripts/run-save-test.sh`, `OPENSC4_TEST_DAYS`/`OPENSC4_TEST_SAVE`/`OPENSC4_TEST_FINGERPRINT` in the autotest, `tools/re/fpdiff.py`, `determinism.virtual_clock` (`OPENSC4_VCLOCK_ALL_THREADS`), `determinism.fixed_seed` (`OPENSC4_SEED_LOG`, `OPENSC4_RAND_TRACE`, pinned `srand`), `diagnostics.trace.threads`. Saves are kept in `%LOCALAPPDATA%\opensc4\analysis\p4`.
