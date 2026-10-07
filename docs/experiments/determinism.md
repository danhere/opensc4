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

(Superseded: the third round below found the causes; with them fixed, runs agree for a year.)

Exact whole-game determinism is not within reach of a few hooks. The remaining sources (thread interleaving, possibly address-ordered processing) need deep control of the game's threads or memory, with no guarantee. What does hold, with `determinism.fixed_seed` and `determinism.virtual_clock`: two runs from the same save agree exactly for at least a week of game time. That suggests short-window parity for P5: start from a save, run a few game days with the original and with a replacement, compare fingerprints (and the replaced simulator's own state) exactly, and repeat from many saves; plus statistical comparison over long runs.

Tooling from this experiment: `opensc4-dbpf diff`, `tools/re/savediff.py`, `scripts/run-save-test.sh`, `OPENSC4_TEST_DAYS`/`OPENSC4_TEST_SAVE`/`OPENSC4_TEST_FINGERPRINT` in the autotest, `tools/re/fpdiff.py`, `determinism.virtual_clock` (`OPENSC4_VCLOCK_ALL_THREADS`), `determinism.fixed_seed` (`OPENSC4_SEED_LOG`, `OPENSC4_RAND_TRACE`, pinned `srand`), `diagnostics.trace.threads`. Saves are kept in `%LOCALAPPDATA%\opensc4\analysis\p4`.

## Third round: the causes (2026-10-07)

Dan un-tabled the root cause after the parity harness found the pass-through pollution simulator reaching outcomes the original "never" reached: the original reached them too once the game ran a little slower (the RNG tracer's overhead was enough). The outcome depended on real time. This round found how real time got in, one path at a time, each confirmed by repeat runs of the same save.

**Tools.** `diagnostics.trace.rng` (src/core/rng_trace.cpp) hashes every draw from the game's generators per thread (cRZRandom::Advance 0x00913696, cRZRandom::NextFloat 0x0091372E, cRZRandomSuper::Next 0x009139B9) into the daily fingerprint, and `OPENSC4_RNG_DUMP_DAY` (a day, 0 or `all`) dumps each draw with its caller, the next code addresses up the stack and the generator object. `OPENSC4_VCLOCK_CALLERS` counts the main thread's clock reads per caller and day. Comparing two runs draw by draw, after dropping callers whose counts differ between two runs that agree (frame-level work: particles, automata, interface) and every generator such a caller uses, leaves the simulation's own draws, which must match one for one; the first mismatch names the code. (The hooks are register-preserving thunks: a plain C++ detour on `Advance`, which changes only EAX and EDX, crashed the game in a caller that keeps `this` in ECX across the call.)

What made runs differ, in the order found:

1. **Ticks per frame.** `cSC4Simulator::RunTimeSlice` (0x00703E70) runs ticks until a stopwatch passes 1000 / fps cap minus the time spent outside the simulation since the last slice (rendering, interface, messages). Under the virtual clock that time is the number of clock reads frame-level code made, which varied (city load alone: 120,814 to 160,819 reads), so the split of ticks into frames varied.
2. **The simulation clock waits for time.** The clock step in the tick (0x00702A80: minutes at +0xB8, hours at +0xBC, then the day) waits until its pacing stopwatch (simulator +0x48) shows the minimum time per step (+0x80); meanwhile the other tick work runs on. How much work a day got depended on elapsed time.
3. **Idle time.** The tick's idle dispatcher (0x00702DF0) sends SimIdle (0x6695681A) to eight simulators round robin, each with a third of the slice budget, while the slice stopwatch allows; the budget came from 1.
4. **The CPU time stamp counter.** Seven `rdtsc` instructions in simulation code bypass any clock hook: deadline helpers that bound how much work a call may do (0x0068BF50 sets now + ms, 0x0068BF80 checks; about 25 simulator functions call them), a cycles-per-millisecond estimate made every slice (0x005D9370) that turns those milliseconds into cycles, and three more (0x0070E2B0, 0x00720430). The GZStopwatch class has an rdtsc mode too (mode 0), but no stopwatch the game reads uses it (`OPENSC4_STOPWATCH_SURVEY`).
5. **Our own fixed seeds.** `determinism.fixed_seed` replaced every seed with the next number of one global sequence. Simulation code seeds local generators from positions (cSC4TractDeveloper::UpdatePopulation 0x00706BC0 seeds one on the stack from ((x << 16) + z) << 4), so the replacement made them depend on how many generators frame-level code had created before. Found as a stack generator (0x001AFC18) whose values in one run were the other run's shifted by 22 draws.
6. **The wall clock.** With the game's own seeds passed through, seeds it computes from time() appeared (0x005CED80 seeds from time() directly; the traffic simulator's Init and the prop developer do the same).

The fixes:

- `determinism.fixed_ticks` (src/core/fixed_ticks.cpp): the slice stopwatch says "expired" after OPENSC4_FIXED_TICKS checks (default 60), the pacing stopwatch always says enough time has passed, and the time-since-last-slice stopwatch reads 0 inside a slice, so the budget is a constant 1000 / cap. No clock is read for any of them. A day now ends as soon as its work is done, with no waiting ticks: the schedule a machine with no spare time would get, not what a fast machine does at full speed (it waits and spends the wait on idle work). Both sides of a parity test get the same schedule, which is what matters there.
- `determinism.virtual_clock` now also traps the seven rdtsc sites (int3; a vectored handler returns the virtual counter scaled to 3 GHz, advancing 0.1 ms per read), and freezes the wall clock (GetSystemTimeAsFileTime and GetLocalTime in the exe's imports return 2026-01-01 00:00 UTC, `OPENSC4_VCLOCK_WALL`; clock()'s start at 0x00BAD6B0 is moved to the same instant).
- `determinism.fixed_seed` replaces every seed only until the director's PostAppInit (startup, where the order is fixed and static initialisers seed from the local time); after that it keeps the game's own seeds and replaces only clock requests (0xFFFFFFFF), counting them per call site.

Results (repeat runs of one save, everything above on):

| Save, days, runs | Before this round | After |
| --- | --- | --- |
| Rush Hour d1095, 7 days, 6 runs | 3 outcomes in 15 runs | identical: fingerprints, day boundaries to the tick, saved records |
| Making Money d0000, 30 days, 4 runs | diverged day 19 (1 in 3), then day 25 (all) during the round | identical |
| Rush Hour d1095, 60 days, 3 runs | diverged day 37 | identical |
| Big City d0090, 60 days, 3 runs | (not run) | identical |
| Making Money d0000, 90 days, 3 runs | population diverged days 9 to 29 (60-day pairs, second round) | identical |
| Making Money d0000, 365 days, 2 runs | saves differed in 44 records, lots, buildings and flora among them (first round) | fingerprints identical every day; whole save: 8 records differ beyond moved addresses, fewer than the zero-day noise floor below |

"Identical" means every daily fingerprint (population, funds, lots, traffic, land value and pollution grids, and the tick on which each day began) and the decoded pollution record. Whole saves (`savediff.py --pointers`) still differ in some records:

- Zero days (load, save), 2 runs: 10 records (props, flora, network occupants, automata generator and controller, demand, advice, MySim advice, traffic network map). This is the method's floor: address-ordered containers or values the address matching misses; not investigated further.
- 365 days: 8 records: props and the traffic network map (both in the floor), automata controller and vehicles (cars are animated per frame), advisors (1 to 27 bytes each), and an untyped record 0x8A2482B9 (group 0x4A2482BB, instance 4; 16 bytes longer in one run).
- 90 days: 23 records, 13 beyond the floor: automata attractor, vehicles and vehicle manager, seven advisor records (1 to 8 bytes), and, not explained, cSC4BuildingOccupant (the last object in the record: one float, 0.6408 vs 0.6220, and two words that look like a saved generator state), cSC4ConstructionOccupant (scattered bytes), cSC4OccupantManager (8 bytes longer) and the untyped 0x8A2482B9.

**Heap layout.** Runs agree when everything about the process is the same, but the outcome depends on the heap layout: one extra environment variable, read by nothing (the C runtime copies each variable to the heap at startup), changed the original game's outcome on Making Money d0090 reproducibly (day 7 began on check 745 instead of 435), while a variable of the same length under another name did not. So something processes objects in address order; the tract developer's varying visiting order (below) is one sign. The parity harness therefore gives both runs identical environments ([../parity.md](../parity.md)).

Open:

- Which code processes objects in heap-address order, so that the outcome no longer depends on the layout.
- Whether the building, construction and occupant-manager differences at 90 days are simulation state or per-frame animation state. They do not show in any fingerprint, and the 365-day pair does not have them.
- The tract developer visits tracts in a different order in some runs (same 996 position seeds, in another order); each tract seeds its own generator, so this has not changed results, but processing order could matter where tracts share state. A container ordered by heap address would explain it.
- Automata (cars, pedestrians) are driven by frame time and differ from run to run; they appear not to feed the simulation, since fingerprints agree for a year.
- `determinism.fixed_ticks` gives a day less idle work than real play on a fast machine (no waiting ticks), so parity results hold for this schedule. OPENSC4_FIXED_TICKS only moves frame boundaries between ticks; a replacement could be checked with a few values to show frame-level work does not matter to it.
