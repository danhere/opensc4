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
| Rush Hour d1095, 365 days, 2 runs (with agent order, zero-filled allocations, all-thread clocks) | diverged within days 3 to 37 | fingerprints identical every day; whole save: 9 records (automata, vehicles, advisors, the traffic network map, 4 bytes of props), 6 KB |

"Identical" means every daily fingerprint (population, funds, lots, traffic, land value and pollution grids, and the tick on which each day began) and the decoded pollution record. Whole saves (`savediff.py --pointers`) still differ in some records:

- Zero days (load, save), 2 runs: 10 records (props, flora, network occupants, automata generator and controller, demand, advice, MySim advice, traffic network map). This is the method's floor: address-ordered containers or values the address matching misses; not investigated further.
- 365 days: 8 records: props and the traffic network map (both in the floor), automata controller and vehicles (cars are animated per frame), advisors (1 to 27 bytes each), and an untyped record 0x8A2482B9 (group 0x4A2482BB, instance 4; 16 bytes longer in one run).
- 90 days: 23 records, 13 beyond the floor: automata attractor, vehicles and vehicle manager, seven advisor records (1 to 8 bytes), and, not explained, cSC4BuildingOccupant (the last object in the record: one float, 0.6408 vs 0.6220, and two words that look like a saved generator state), cSC4ConstructionOccupant (scattered bytes), cSC4OccupantManager (8 bytes longer) and the untyped 0x8A2482B9.

**Heap layout.** Runs agreed when everything about the process was the same, but the outcome depended on the heap layout: one extra environment variable, read by nothing (the C runtime copies each variable to the heap at startup), changed the original game's outcome on Making Money d0090 reproducibly (day 7 began on check 745 instead of 435), while a variable of the same length under another name did not. `diagnostics.heap_shift` (`OPENSC4_HEAP_SHIFT=<bytes>`: that many bytes from the game's malloc at startup, kept) turned this into a lever: shifts of a few hundred bytes to a few kilobytes flipped the outcome for some sizes and not others. Comparing runs read by read with `OPENSC4_VCLOCK_TRACE` (every main-thread clock read inside a simulation slice, plus `determinism.fixed_ticks`' slice checks, with callers) found two causes:

1. **Agents are dispatched in address order.** cSC4Simulator keeps one map of agents per agent type, keyed by the agent pointer compared as a number (insert 0x00704060, from `AddAgent` 0x00704360), and walks it with a persistent cursor (0x007038B0; the idle dispatcher 0x00702DF0). Which simulator acts first within a tick, and which gets a turn when a slice ends mid-round, followed the heap. `determinism.agent_order` hands `AddAgent` a forwarding object per agent from one block, in registration order, so map order is registration order (both `RemoveAgent` overloads translate; the message server, by contrast, keeps its targets in lists in registration order and needed nothing).
2. **Reads of memory nobody wrote.** With agent order fixed, some shifts still made the traffic pathfinder (0x006D91B0) do a different amount of work from identical inputs (same trip sources in the same order, same query fields). `determinism.zero_alloc` makes the game's allocators (operator new 0x005E55E0, the STL allocator 0x0090CF54, malloc 0x009F066E) return zero-filled memory, so such reads see zeros in every run.

With both, ten heap shifts (and a second control) gave clock-read traces identical read for read over 7 days, and the environment variable no longer changed anything. Each fix alone left differences (zero_alloc alone: identical fingerprints but traces parting on the first day for three shifts). Zero-filling changes what the game reads where it reads uninitialised memory, so runs with it are not byte-for-byte the stock game; both sides of a parity test use it.

**The worker thread.** With both fixes, Rush Hour d0000 still began one day a tick later in 2 of 8 runs; with the RNG and clock tracing on (which slows the main thread) all runs agreed, on the other value. The city-load worker (0x0048E460) takes jobs under a stopwatch budget on real time, so how much it had done when the main thread used its results depended on speed. With a virtual counter per thread (now the default of `determinism.virtual_clock`; `OPENSC4_VCLOCK_ALL_THREADS=0` turns it off) 8 of 8 runs agreed, and so did 4 runs with tracing: the result no longer depends on how fast either thread runs.

The tools: `diagnostics.heap_shift`, `OPENSC4_VCLOCK_TRACE`, `diagnostics.trace.trips` (`OPENSC4_TRIP_TRACE`: trip sources the traffic simulator takes from its queue; `OPENSC4_PATH_TRACE`: the pathfinder's decisions).

Open:

- One run of about 70 with all replacements quit silently during city load (Making Money d1095 with a 1,072-byte heap shift; no exception report, no crash dump, log ending at an ordinary point); three repeats ran normally.
- Which code reads uninitialised memory (zero_alloc hides it; finding the reads would tell whether the stock game's results are affected by heap garbage).
- Whether the building, construction and occupant-manager differences at 90 days are simulation state or per-frame animation state. They do not show in any fingerprint, and the 365-day pair does not have them.
- The tract developer visits tracts in a different order in some runs (same 996 position seeds, in another order); each tract seeds its own generator, so this has not changed results, but processing order could matter where tracts share state. A container ordered by heap address would explain it.
- Automata (cars, pedestrians) are driven by frame time and differ from run to run; they appear not to feed the simulation, since fingerprints agree for a year.
- `determinism.fixed_ticks` gives a day less idle work than real play on a fast machine (no waiting ticks), so parity results hold for this schedule. OPENSC4_FIXED_TICKS only moves frame boundaries between ticks; a replacement could be checked with a few values to show frame-level work does not matter to it.

## A year of `saves-plopped/p0000` (2026-10-07, retracted finding)

The Big City save with garbage and water buildings (`saves-plopped/p0000`, [../parity.md](../parity.md)) does not have one outcome over a year, even with the original code: of 28 year-long runs of the unmodified game under the determinism replacements, 17 ended with one set of daily fingerprints, 10 with a second (first different on day 164, 2458662) and 1 with a third. Runs with a replacement land on the same outcomes.

An earlier version of this section blamed an uninitialised stack read: runs of the original code with a pattern written into unused stack after `RollUsedCapacity` (0x006E3C80), or with one extra call after it, ended differently from a run without. That came from too few repeats per variant. Repeating the same pattern twice gave both outcomes, and so did the runs without any pattern. Tagging the pattern words and searching memory for them found copies only in what looks like picture data. Tracing the random draws showed the draw order differs between two runs of the unmodified game from the first day. Nothing points at a stack read: this save has some residual nondeterminism, not yet located, that the third round and the heap-layout fixes did not remove. For this save a year can only be judged by outcome sets ([../parity.md](../parity.md), `tools/re/paritygroups.py`), and every replacement checked so far gives an outcome the original also gives.

## Simulation timers (2026-10-09)

A city with an active MySim (the player's own Sim, [../mysim-agents.md](../mysim-agents.md)) did not repeat, even with every fix above: two runs of the original from the same save saved different MySim records. The MySim agents (`cSC4MySim`, `cSC4MySimDispatch`, `cSC4MyStreetSim`) act on real-time stopwatches (GZStopwatch): the message handler (0x0042D150) does its work only when the stopwatch at +0x280 shows more than 2 ms since last time, and the advice balloons expire by another (0x00552BD0, every 500 ms). A third is not in MySim code: the advisor system (`cSC4AdvisorSystem`, constructor 0x0041A990) runs every advisor when its stopwatch (+0x8C) shows 500 ms (0x0041B01A), and each MySim is an advisor; its turn (slot 14 of its +8 interface, 0x0040D820, then the MySim's update 0x0042DC90) moves a cursor through the MySim's list (0x0040F460). That cursor differs between two runs in every city, active MySim or not, since every city has MySim places. Under the virtual clock a stopwatch reads a counter that advances with every clock read anywhere, so how often these ran followed frame-level work.

`determinism.sim_timers` (src/core/sim_timers.cpp) gives those stopwatches simulation time. Thunks on the stopwatch methods that read the clock (ElapsedMilliseconds 0x00890198, through `determinism.fixed_ticks`' hook, and 0x00890279, 0x00890377, 0x0089045A, 0x00890501, the restart 0x0089058F, Start 0x008905C4, Stop 0x008905F0) take over a stopwatch the first time code in the MySim or advisor-system ranges (or the balloon timer) calls one of them on the main thread. From then on its clock reads return a counter that advances 10 ms per simulation tick (the slice checks of `determinism.fixed_ticks`) and nothing else. Every other stopwatch is untouched. It is in the parity harness's base list.

The fingerprint's `ms` part has each MySim's plain saved fields (its Write 0x004251A0 and the agent base's 0x0040E2E0, without references and strings), with its two stopwatches as stored.

Control runs (original against itself, `scripts/run-parity.sh`):

| Corpus | Result |
| --- | --- |
| `saves-mysim` (Big City and Making Money with a MySim moved in, 10 and 60 days in; 30 days), both sides with it | 4 of 4 same |
| the same, both sides without it | 0 of 4 |
| the usual 12 saves (7 days), `saves-plopped` (7 days), `saves-mm2` (90 days), one side with it | every fingerprint part but `ms` identical on every day of every save; `ms` differs, and the side without it has up to 3 outcomes of its own |
| with it in the harness's base list, both sides: `saves-mysim` (30 days), the usual 12 (7 days), `saves-plopped` (7 days), `saves-mm2` (90 days) | 4 of 4, 12 of 12, 3 of 3, 2 of 2 |

With a MySim, the traffic network map and frame-level random draws (automata, particles) differ from the first day as without one ([../traffic-map.md](../traffic-map.md)); the MySim's car moves on frame time.

## The cycle clock in parity runs (2026-10-12)

Under load from other games on the machine, a save could give an outcome that differed from the usual one only in the lots' checksums, a day in. It first looked like a traffic parity gap: on Making Money d1095 with every traffic replacement on, 6 of 17 runs gave it and none of 9 runs of the original. With the load made equal (four games at once, 24 runs each) the original gave it as often: 8 of 24, against 6 of 24 with the traffic replacements, with the same fingerprints.

In those runs the traffic simulator's lot walk (+0x90C in its record) had stopped at another lot (61, 64, 100 or 104), and its counters were one or two off: a time-boxed phase had got a little further or less far in a step. A trace of every clock read (`OPENSC4_VCLOCK_TRACE`) showed the same in other simulators: the demand step (0x006A7020) and the land value walk (0x006BE7B0) took 223, 224 or 226 deadline checks in one call on the first day in three runs, two of them with the same outcome.

The cause is the cycle clock's estimate (0x005D9370), which the simulator makes again at every slice: a moving average of cycles per millisecond, which every deadline (0x0068BF50) is made with. It starts from a value measured in real time before the virtual clock takes over, and moves towards the virtual clock's 3 GHz along a path that depends on how many clock reads came between slices while the city loaded, which load changes (4,100 to 8,800 reads before the first day in four traced runs). Until it has settled, a deadline of 4 ms is a slightly different number of checks from run to run.

`determinism.fixed_ticks` already held the estimate at the value it converges to for paced play ([../seeds.md](../seeds.md)); it now does so whenever it is on, so in every parity run (`OPENSC4_FREE_CYCLES=1` leaves the game's estimate alone). With it, under the same load: none of 24 runs of the original and none of 24 with the traffic replacements gave a lot-walk outcome (14 of 48 before). Results made before this change are not comparable with new ones run for run: the first days' deadlines differ.

What was left on those saves was a second pair of outcomes that the original gave as often as any replacement: the next section.

## The sound code and rand() (2026-10-12)

Big City saves and Making Money d1095 had two outcomes, from the first day on, in the flammability part of the fingerprint (the grid's checksum, the walk's sum and count, the hot cells) and in the weather record, with everything else the same. The original gave both as often as any replacement: under load (four games at once) Making Money d1095 split 18 to 6 in 24 runs of the original and Big City d1095 8 to 8 in 16.

It was not the clock: the two kinds read the virtual clock in the same order, read for read, for thousands of reads back into the first day. A dump of the flammability grid on the first day (`OPENSC4_TEST_FLAM_DUMP=<n>`: every cell's raw value, grid value and the water simulator's answer at the n-th fingerprint) showed the same raw values, multiplier and water answers in both kinds, and 37 cells of 6,267 with another grid value: the walk had been over other cells. The flammability and weather simulators walk the city with the same iterator (0x0049CB10, 0x0049D1C0), whose restart picks its starting place, and whose shuffle its order, with the C runtime's `rand()`.

`rand()` has one sequence per thread and only twelve callers in the exe. On the main thread the simulation's callers (that iterator's 0x0049CBE0 and 0x0049D1C0, the shuffles 0x005CF610, 0x006C68C0 and 0x006DB010, the landfill's pictures 0x006BCFC0, Lua's `math.random`) share it with the sound code: 0x008D2CFD, 0x008D3442 and 0x008D546F in GZSound draw when a sound starts, and the audio event handler 0x004EE560 draws a wait of one to four seconds on a real-time stopwatch. How many sounds had started when a walk restarted depends on timing. A trace of every call (`OPENSC4_RAND_TRACE`) had the two kinds agree for 4,341 calls; then one had four to nine draws by the sound code ahead of the iterator's, the other none.

`determinism.fixed_seed` now gives the sound code's draws a sequence of their own (the same generator on its own state; our audio code asks for it through `fixed_seed::SoundRand`), so the simulation's sequence no longer depends on what was heard. `OPENSC4_SHARED_RAND=1` leaves the game's sharing. Under the same load, original code: Making Money d1095 24 of 24 runs one outcome, Big City d1095 16 of 16; with the switch back on in the same build, 18 to 6 and 8 to 8. With every traffic replacement on, Making Money d1095: 23 of 24 the original's outcome; the other differed from the load on in one checksum of the fingerprint's MySim part and nothing else (not looked into).

The usual batch for a week, original against every traffic replacement, after both changes: 12 of 12 on the first pair, `saves-plopped` 3 of 3 (p0090 after three pairs, the original with two outcomes), `saves-ports` 3 of 3. `saves-networks` still varied with the original against itself (n0000: three outcomes in four runs): the next section.

As with the cycle clock, results made before this change are not comparable run for run: the walks start elsewhere.

## Nightfall and the lot developer's shuffle (2026-10-12)

On `saves-networks/n0000` the runs differed in the order of two of the simulation's `rand()` callers on the first day: at call 5,474 of the main thread either the flammability walk ended and restarted (0x0049D1C0 from its step 0x006AF8D0) or the lot developer shuffled its buildings first (0x006C68C0 from its DoMessage 0x006CD940), some 3,400 draws, with the restart after them. The sound code was not in it, and the two kinds read the virtual clock alike on that day, read for read. (`OPENSC4_RAND_TRACE` writes the next two code addresses up the stack with each call, which is how the callers' callers were read.)

A first guess was construction still going on in a save taken a day after its networks were drawn. It was wrong: the lots' states do not change in the 30 days after (92 empty, 3,422 built, 8 in state 4 on every day), and the city run on varied as much. Original code, 20 runs each under four games at once: n0000 11 and 9, the same city 14 days on 12 and 8, n0090 (90 days on) 13, 5 and 2.

The shuffle is the view's day and night. When the view's clock turns to night or back, the lot developer gets message 0x44778234 and shuffles every building, for the order their lights change in, then steps through them on the frame timer (0x86AD10EE). The view's clock follows real time, so where in a run the turn comes does too, and its 3,400 draws come out of the sequence the simulation's walks use. The unattended corpora were saved far from a turn; this save was taken after 25 minutes of drawing by a script with the clock running, and a turn falls inside a week's run.

The shuffle's draws now come from the same separate sequence as the sound code's (`determinism.fixed_seed`, one more range in its list). With it, the same 20 runs each, by fingerprints: n0000 one outcome, 14 days on one outcome, n0090 17, 2 and 1 (one run differed from the load on in a checksum of the MySim part, two from the first day in a token of the traffic map's part; not looked into).

The saves these runs wrote still differed, with the same fingerprints on every day: counted as the parity harness counts (fingerprints and decoded records), n0000 gave five outcomes in those 20 runs and the city 14 days on four, where Big City d1095 and Making Money d1095 gave one. Two things differed, both the state of the view at the moment of the save and not of the simulation:

- The lot developer's record keeps how far the lights' stepping has got (+0x8C: 40, 50 or 60 in three runs), which follows the frame timer. `determinism.lights_at_once` (src/core/lights_at_once.cpp; off by default, in `run-parity.sh`'s list, never for play) replaces the shuffle with the game's own finish (0x006CBE50), which changes every building at once and ends the stepping; a city saved in the middle of the stepping is finished when the autotest has loaded it. With it n0000 gave two outcomes in 20 runs, 16 and 4.
- Those four had no train. The city has a rail line and a station since the networks were drawn, and a train (cSC4Train, class 0x8A41625A, one of the automata registered at 0x00514A00) was in the save or not. The occupant manager's decoder already left the four automaton classes out of its cell lists (vehicles, pedestrians; they move by frames); the train is left out with them. Counted so, the same 20 runs: one outcome.

Original against every traffic replacement for a week with both: n0000 the same on the first pair, n0090 the same after two pairs (the original two outcomes), the 12 saves 12 of 12 and `saves-plopped` 3 of 3, all on the first pair.

n0090 still differed from run to run in its records, eight outcomes in 20 runs with two fingerprints: the prop developer steps its timed props the same way after a turn (0x006F0EC0: +0xA34 props every 200 ms, +0xA2C the next prop, +0xA38 the milliseconds, all saved; 845, 1,014 or 1,183 props done at the save). Nothing of its own starts that stepping that could be replaced without taking a function our own prop developer detours, so `determinism.lights_at_once` finishes it from outside with the game's function that switches every timed prop at once (0x006F10E0) and clears the milliseconds: when the buildings' shuffle fires, when the autotest has loaded the city, and at every daily fingerprint, the last of which comes just before the save. (It acts only in runs that take fingerprints, and a turn between the last fingerprint and the save would escape it.) With it the same 20 runs gave two outcomes, 13 and 7, records and fingerprints alike; n0000 one outcome in 20; original against every traffic replacement for a week: both `saves-networks` saves, the 12 and `saves-plopped` the same on the first pair (one run of the 12 died at startup on another thread, before a city was loaded, and matched when run again).

The two outcomes left on n0090 differed in whether the view had turned to night by the first day's fingerprint (the prop developer's night flag), and the pollution simulator's record with it: the turn itself came at another point of the run.

The view's clock is cSC424HourClock (class 0xA7E7F929, a system service, at 0x00B43DBC). Its tick (0x00445820, on the part at +0xC) comes with every frame: unless paused, the milliseconds of its stopwatch (+0x50 of the object) times the magnification (100) and 0.001 are seconds of the day; above 60 they are added (at most 1,200 a call), messages 0x44778230 and 0x44778231 follow an hour or five minutes passed, 0x00445680 posts the turn 0x44778234 when the hour has crossed between day and night, and the stopwatch restarts. A day of the view is 864 seconds of that stopwatch.

Two things made the turn move. The stopwatch read the virtual clock, which counts clock reads, so its milliseconds followed how much frame work a run had done; `determinism.sim_timers` can count simulation ticks for it instead (10 ms a tick). That alone made it worse (three outcomes in 20 runs): unpaced, a run has a frame only every 170 to 330 ticks, each tick's seconds went in at the next frame, and a frame more or less before a day's end moved the turn by a day. So with `determinism.lights_at_once` the clock of a loaded city is put on simulation ticks and its tick is made at the start of each simulation day, by the autotest, and left out when a frame asks. The turn then comes on the same day of every run (on n0090's third).

With that, original code, 20 runs each under four games at once, records included: n0090 one outcome, n0000 one outcome. Original against every traffic replacement for a week: both `saves-networks` saves and `saves-plopped` the same on the first pair, the 12 saves 12 of 12 (Big City d0090 after two pairs: one run of the original differed from the load on in one checksum of the fingerprint's MySim part and nothing else, as one traffic run of 24 had on Making Money d1095 earlier; seen twice now, once on each side, and not looked into).
