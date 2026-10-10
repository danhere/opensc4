# Parity testing (P5)

How a replaced simulator is shown to behave like the original. With `determinism.fixed_seed`, `determinism.virtual_clock`, `determinism.fixed_ticks`, `determinism.agent_order`, `determinism.zero_alloc` and `determinism.sim_timers` (the MySim and advisor timers on simulation time), two runs of the original from the same save agree exactly, whatever the heap layout: every daily fingerprint for a year, and the whole save apart from a noise floor ([experiments/determinism.md](experiments/determinism.md), third round). So a replacement can be compared with the original exactly, over short windows from many saves; long runs too, within the limits below.

## Running it

```bash
B="$LOCALAPPDATA/opensc4/build/debug"
scripts/run-parity.sh "$B" "$LOCALAPPDATA/opensc4/parity/proxy" 7 "$LOCALAPPDATA/opensc4/parity/saves" \
    OPENSC4_PARITY_B=simulators.pollution.proxy=ours
```

- `<saves dir>` holds one folder per city, named as the city in the test region, with saves of that city in it. For each save the harness runs the city twice through [scripts/run-save-test.sh](../scripts/run-save-test.sh) (`OPENSC4_TEST_START` puts the save in place of the city's file in the disposable region): once with replacement set A (`OPENSC4_PARITY_A`, default none: the original game) and once with set B (`OPENSC4_PARITY_B`). Both get the five determinism replacements and log a fingerprint every game day.
- After `<days>` game days each run saves and quits. A run's **outcome** is its daily fingerprints ([tools/re/fpdiff.py](../tools/re/fpdiff.py): population, funds, lot count, traffic, land value and pollution grid checksums, the pollution simulator's worst tracts and the state of the generator that places its effects (read by nothing else, so otherwise invisible to the comparison), and the tick on which the day began) together with every saved record whose layout is decoded (`tools/re/sc4save.py`, field by field: the pollution and power simulators' records; CRCs and heap addresses are left out, references keep their class).
- If the two outcomes differ, the pair is run again, up to `OPENSC4_PARITY_RETRIES` more times (default 3; retries go to `<save>/try1`...). [tools/re/paritygroups.py](../tools/re/paritygroups.py) groups all runs of the save by outcome, and the save passes when **every outcome of B is also an outcome of A**. With determinism this should not be needed; it stays as a safety net and to show when a difference is systematic (the same outcome every time).
- **Both runs get the same environment**: the replacement lists go in an `opensc4.ini` per side (`a.ini`, `b.ini` in the output folder, installed by `run-save-test.sh` as `OPENSC4_TEST_INI`), not in `OPENSC4_REPLACE`. The game's outcome depends on its heap layout, and the C runtime copies each environment variable to the heap at startup: one extra variable (`OPENSC4_PARITY_B=...`, read by nothing) changed the original's outcome on Making Money d0090, reproducibly, while a variable of the same length under another name did not; padding the environment to the same total size still left 3 of 12 saves different, because the variables' individual lengths differed.
- Results go to `<output dir>/<city>/<save>/` (`a.log`, `b.log`, `a.sc4`, `b.sc4`, `fpdiff.txt` and `records.txt` for the first pair, `groups.txt`) and a `summary.txt`. Runs already done are reused, so an interrupted run can be resumed; delete the output folder to start over.
- A run with A = B is the control: it shows the noise of the method itself on those saves.
- Runs use `OPENSC4_TEST_QUICK=1` unless told otherwise (`OPENSC4_TEST_QUICK=0`): 1 s on the region view instead of 5, and the game's fast save, which leaves out only the region view's thumbnails of the city (records 0x8A2482B9 and 0x2026960B; ~0.4 s instead of ~11). Checked on 2026-10-09: a control with it and one without gave identical fingerprints and identical decoded records on all 12 saves. With one job each start waits `OPENSC4_PARITY_PAUSE` seconds (default 20; Steam once relaunched the game when it started right after the last run); with several jobs the default is 0, and no relaunch was seen in over 100 starts.
- **Parallel runs** (`OPENSC4_PARITY_JOBS=<n>`): n saves at a time, each job in its own user folder (`%LOCALAPPDATA%\opensc4\p4env\job<k>`, via `OPENSC4_SAVE_ENV`) with both runs of a pair in the same job. Adds `harness.multi_instance` to both sides ([src/core/multi_instance.cpp](../src/core/multi_instance.cpp): per-process names for the exe's named mutexes and events, other processes' windows hidden from FindWindow; it logs every such call, so the log names whatever single-instance check the game makes) and sets `OPENSC4_PARALLEL=1`, so `run-autotest.sh` skips its "already running" check and its watchdog kills its own game by pid. Determinism no longer depends on timing or heap layout, so parallel runs should give the same outcomes; check with a control (A = B) at 1 and at n jobs. The game's only single-instance check is a mutex named "SimCity 4 Mutex" (it calls no FindWindow). Control A = B, 7 days, 12 saves, on 2026-10-09 (20 logical CPUs, 32 GB):

  | Jobs | Result | Wall time |
  |---|---|---|
  | 1 | 12 of 12 same | 1,119 s |
  | 2 | 12 of 12 same | 551 s |
  | 4 | 12 of 12 same | 327 s |

  No retries, timeouts or Steam relaunches; each copy used at most about 400 MB. Four jobs fall short of 4x because job starts are staggered by 15 s.

  Jobs take the next unclaimed save from a shared queue (`<output dir>/.claims`), so one slow save (retries) does not leave the other job idle.

### Where the time goes

Measured from the core.dll logs of ~260 runs on 2026-10-09 (2 jobs, other threads' games running at the same time), per game start, before these changes:

| Step | Median | Share |
|---|---|---|
| Pause before the start | 20 s | 40-50% |
| Save (full save, with region thumbnails) | 10.8 s | 20-28% |
| Fixed wait on the region view | 5.0 s | 10-14% |
| Boot to the region view | 3-5 s | 8-15% |
| Load the city | 1 s | 2-4% |
| 7 game days (90: 2.5 s, 365: 6.6 s) | 1.4 s | 3-17% |
| Quit | 1 s | 3-10% |

Installing hooks one at a time also cost ~40 ms each (MinHook suspends the process's other threads for each, and finding them is slow on a busy machine): 138 hooks with the whole traffic simulator on added 5 s to every start. The registry now enables each phase's hooks together (`hook::BeginBatch`/`EndBatch`); PostAppInit to the first tick went from 5.35 s to 0.1 s. A control on the three noisiest saves, three rounds with batching and three without, gave the same noise in both.

Control A = B, 7 days, 12 saves, 2 jobs, same afternoon: 858 s before, 171 s with quick runs and no pause. Under heavier load from other threads the same batch took 375 s, mostly in retries: the original itself gave more than one outcome on Big City d0000 and Making Money d0090/d0365 in some batches, before and after these changes (also seen by other threads under load), so read "partly" verdicts on those saves against a repeat.

## Sampling inside the simulator's tick

The autotest fingerprints and stops from a simulator agent (`cISC4Simulator::AddAgent`, `AgentTypeSimNewDay`), which the simulator calls from inside its tick when the day changes, and pauses there on the last day. The first version did both from the framework tick, on the first frame after the day number changed; by then the simulator had run however many further ticks that frame had room for, so the sample point and the saved moment moved from run to run (the first control agreed on 4 of 12 saves; sampling in the tick: 9 of 12).

## History of the results

12 saves (three tutorial cities, each as found and after 90, 365 and 1095 days), 7 game days each:

| Harness state | Control (original vs original) | Pass-through pollution proxy |
| --- | --- | --- |
| fixed seeds + virtual clock, sampled per frame | 4 of 12 agree | not run |
| sampled in the tick | 9 of 12 | 8 of 12 (pairs), 9 of 12 after a forwarding bug fix and outcome-set verdicts |
| + determinism round 3 (fixed ticks, rdtsc, seeds, wall clock) | **12 of 12, all on the first pair** | 11 of 12; Making Money d0090 different, the same way every time |
| + replacement lists in opensc4.ini (identical environments) | | **12 of 12, all on the first pair** |
| + agent order, zero-filled allocations, virtual clocks on all threads | **12 of 12, first pair**; with B's heap shifted by 1,072 bytes: 11 of 12, the twelfth an unexplained silent exit during load (not reproduced in 3 repeats) | **12 of 12, all on the first pair** |

Our first reimplementation increment, the pollution simulator's getters and setters (`simulators.pollution.interface`), passes 12 of 12 on the first pair; with B's heap shifted, 10 of 12 and the two others identical when repeated ([pollution.md](pollution.md)).

Along the way the proxy turned up a real bug: eight garbage getters return 64-bit values in EDX:EAX that gzcom-dll declares as `int32_t`, and the game reads all 64 bits; the proxy returned EAX with whatever EDX held (fixed; [experiments/pollution-proxy.md](experiments/pollution-proxy.md)). Its other differences turned out to be the original's own: timing-dependent outcomes that the proxy's small overhead, or the RNG tracer's, made more or less likely, and finally the environment difference above. One crash in a proxy run (0x0096DA20, Rush Hour d0365, during load) was never reproduced. It came back once (2026-10-07) in a run with our Init (`init7`, Big City d0000, during load; three repeats loaded fine): a constructor at 0x0096D9D1, called from 0x009701FD in framework code, gets a null owner and increments its count. A third came on 2026-10-08 with only power code replaced (`pwr-f-pl7`, `saves-plopped/p0365`, during load; the rerun passed). All three were B runs (with more replacements than A), never an A run, among several hundred pairs: not yet explained, worth a look if it keeps to B. On 2026-10-08 two A runs crashed as well, in the water batches (`wat-sv7`, Big City d0000, at 0x0075FEEE in 0x0075FEE0 just after the run started; `wat-obj7`, Rush Hour d1095, at 0x005A62A2 during load): A runs have none of our simulator code, and both saves passed twice more on rerun. So the game itself crashes now and then under the harness, on either side; a run that ends without a save should be checked for a new `SimCity Exception Report` (its name carries the virtual clock's date, 2025.12.31 19.00.00, not the real one). On 2026-10-09 0x0096DA20 came once more in a B run during the simulation, not the load (`tr-all30b`, Rush Hour d0090, with the tract developer ours): the report's stack went from our building choice into the game's exemplar style check (0x005E8F70), so the crash is in resource loading wherever it is asked for; three repeats passed ([tract.md](tract.md)). On 2026-10-10 a B run with the disaster layer, managers, toxic spill and pipe burst ours crashed at 0x0080105D while Big City d0090 loaded (`disp2-fuse`, third pair), with none of our code on the stack ([disasters.md](disasters.md)). The same night 0x0096DA20 came in an A run for the first time (`disv1-7`, Big City d0365, second pair, during load): the original, with none of our simulator code, so that crash is the game's own under the harness and not tied to the B side. And 0x005A62A2, seen in an A run on 2026-10-08, came in a B run (`disr1-e4`, Making Money d1095, during load). On 2026-10-10 it came on the same save and day again with the lot developer ours and no code of ours on the stack (`lot-all30`); six single runs with and six without the lot groups passed ([lot.md](lot.md)). Also seen from 2026-10-09 on: the first B run of a save parting alone from its second day in the flammability part (then weather and pollution), its retries agreeing, in batches of unrelated replacements and in a control whose B side replaced nothing (`pt-b-ctl`); never the A run. Unexplained; judge such a save by its retries.

## Limits

- **Heap layout** no longer matters for the cases tested: with `determinism.agent_order` and `determinism.zero_alloc`, shifting the game's heap by up to a few kilobytes changes nothing (ten shifts over 7 days, clock reads identical; 11 of 12 saves in the shifted control). The harness still gives both runs identical environments, which costs nothing. A replacement that reads uninitialised memory of its own, or orders things by address, would bring the problem back; the shifted control (`OPENSC4_PARITY_B=diagnostics.heap_shift=ours OPENSC4_HEAP_SHIFT=<bytes>`) is the check.
- **Schedule.** `determinism.fixed_ticks` ends a day as soon as its work is done; real play on a fast machine spends waiting time on idle work. Parity holds for this schedule.
- **Whole saves** still differ from run to run in some records (automata, advisors, props, the traffic network map; [experiments/determinism.md](experiments/determinism.md)); the harness compares fingerprints and decoded records, which agree.

## Coverage

The test cities never edit their networks, so parity alone would not reach the code a network edit, a disaster or a rare message runs. Autotest switches bring those events in, identically on both sides of a pair:

| Switch | What it does |
| --- | --- |
| `OPENSC4_TEST_POWER_WAKE=<n>`, `OPENSC4_TEST_WATER_WAKE=<n>` | on day n, wake the power or water step (+0x14), as message 0x6AC284F3 from the network tools would |
| `OPENSC4_TEST_CRIME_MONTH=<n>` | every n days, restart the crime simulator's monthly pass (state +0x90 from 6 to 0), as MonthPassed does ([crime.md](crime.md)) |
| `OPENSC4_TEST_DEMAND_MONTH=<n>` | every n days, the demand simulator's MonthPassed ([demand.md](demand.md)) |
| `OPENSC4_TEST_DEMAND_MESSAGES=<n>` | on day n, SimulationBegin, the neighbours message and the passes to the demand simulator |
| `OPENSC4_TEST_COMMERCIAL=<n>` | on day n, "removed" then "added" (0x99EF1143, 0x99EF1142, kind 0x278128A0) for every building on a lot (at most 400), to the commercial simulator, so its proximity-map cycle runs ([commercial-industrial.md](commercial-industrial.md)) |
| `OPENSC4_TEST_RESIDENTIAL_MONTH=<n>` | every n days, the month message (0x66956816) to the residential simulator, which restarts its cycle; with 2, a 30-day run covers it about twice ([residential.md](residential.md)) |
| `OPENSC4_TEST_RESIDENTIAL_QUERIES=1` | each day, the residential queries only the UI makes, into the fingerprint ([residential.md](residential.md)) |
| `OPENSC4_TEST_DEVELOPMENT_QUERIES=1` | each day, the building development queries no test city makes (type lists, type and family checks, developer wealth, the stage multiplier), into the fingerprint ([development.md](development.md)) |
| `OPENSC4_TEST_GROWTH_QUERIES=1` | each day, per growth developer, the queries no test city makes (desirability estimates, subjective thresholds, abandonment thresholds, the desirability threshold), into the fingerprint ([growth.md](growth.md)) |
| `OPENSC4_TEST_PLOP_PORTS=<n>`, `OPENSC4_TEST_GROW_PORTS=<n>`, `OPENSC4_TEST_REMOVE_PORTS=<n>` | on the n-th day: an airport and a seaport built with the game's own calls; growth asked of the ports; the first port of each developer removed ([ports.md](ports.md)) |
| `OPENSC4_TEST_AIRPORT=1`, `OPENSC4_TEST_SEAPORT=1`, `OPENSC4_TEST_LANDFILL=1` | each day, the queries of those developers no test city makes, into the fingerprint ([ports.md](ports.md), [landfill.md](landfill.md)) |
| `OPENSC4_TEST_PROP=1` (2: the frame-driven state too) | each day, the prop developer's queries no test city makes, into the fingerprint ([prop.md](prop.md)) |
| `OPENSC4_TEST_LOT=1` (2: the timer-driven night step too) | each day, the lot developer's queries no test city makes, into the fingerprint ([lot.md](lot.md)) |
| `OPENSC4_TEST_CRIME_QUERIES=1` | each day, the crime queries only the UI and the automata make, into the fingerprint ([crime.md](crime.md)) |
| `OPENSC4_TEST_MYSIM_QUERIES=1` | each day, what the MySim panel and balloons ask every MySim, into the fingerprint ([mysim-agents.md](mysim-agents.md)) |
| `OPENSC4_TEST_MYSIM_DRIVE=<day>` | on that day every MySim leaves for work, returns home and hears of building and connection changes, fires and disasters ([mysim-agents.md](mysim-agents.md)) |
| `OPENSC4_TEST_MYSIM_DELETE=<day>` | the first MySim leaves the game on that day ([mysim-agents.md](mysim-agents.md)) |
| `OPENSC4_TEST_MYSIM_COMMANDS=<day>` | on that day every MySim gets the script commands for richer, fired and a new generation, and the first one for dying ([mysim-agents.md](mysim-agents.md)) |
| `OPENSC4_TEST_MYSIM_MOVEIN=<day>` | a MySim moves in on that day ([mysim-agents.md](mysim-agents.md)) |
| `OPENSC4_TEST_FLORA_MONTH=<n>` | every n days, the flora simulator's monthly pass (the date into +0x40, then 0x006B2CB0), as MonthPassed does ([flora.md](flora.md)) |
| `OPENSC4_TEST_NEIGHBORS_MONTH=<n>` | every n days, the neighbors simulator's monthly pass (0x006D0E50: commodity statistics, then the deals), as MonthPassed does ([neighbors.md](neighbors.md)) |
| `OPENSC4_TEST_POWER_MESSAGES=<n>` | on day n, a network change (0x278E6003) and a changed cell rectangle (0x07699455) over the whole city, to power's DoMessage |
| `OPENSC4_TEST_OCCUPANT_CYCLE=<n>` | on day n, "removed" then "added" (0x99EF1143, 0x99EF1142) for an existing pipe and pole, to the water and power simulators; and power's plant disaster message (0xC2C09554 with 0xE0D0722E) |
| `OPENSC4_TEST_FIND_POWER_PLANTS=1`, `OPENSC4_TEST_CAPACITY_SCALE=1`, `OPENSC4_TEST_GARBAGE_BUILDINGS=1`, `OPENSC4_TEST_RADIATION=x,z,amount,radius` | one call each after the load: power's FindPowerPlants, pollution's capacity scale handler, GetGarbageBuildings, AddRadiationToCity |

Building real network pieces from the harness did not work: the game's network tool (`cISC4NetworkManager::GetNetworkTool`, then `PlaceNetworkOccupants`, or `DrawNetworkLine` and `PlaceAllSegments`) needs an initialised tool of its own (the shared one has no terrain at +0x44 until the player picks it, and placing with it crashes), and even then every drag came out at cost 0 and built nothing, in the tutorial cities and in one with the tutorial off. Making it build would mean working out the network tool, which waits for the NAM team (ROADMAP P7); the messages above reach the simulators' handlers instead.

With all of them (power and water woken on day 1, the network messages on day 2, the occupant cycle on day 3, the three one-off calls) and every pollution, power and water replacement on, our three objects included: 12 of 12, 3 of 3 on `saves-plopped` for a week, 2 of 2 on `saves-mm2` for 90 days, all three save records identical (2026-10-08). Reached now that were not before: water's `OnPipe`, power's plant disaster case, `RecomputeArea` and the network change handler in every run, GetGarbageBuildings. Still not reached: power's `CreateBitArrays` (a new city), `OccupantCell` and `FindConnectedOccupant` (consumer lots short of power), water's `FindOtherSources` (a pump whose source kind searches), `ObjectCell` and `Serve` (consumer lots short of water, lots of type 5).

## Save corpus

`%LOCALAPPDATA%\opensc4\parity\saves-plopped` adds three saves of a Big City Tutorial with garbage and water buildings, which the main corpus lacks (it has one such building, in Rush Hour): a water treatment plant, a recycling center, a waste-to-energy plant and a landfill zone, placed through the game's own UI by a script (2026-10-07), then the city as built (`p0000`) and 90 and 365 days on. The steps: the tutorial is switched off by clearing the flag in its `cSC4TutorialSystem` record ([save-format.md](save-format.md)); `run-save-test.sh` with `OPENSC4_TEST_START_PAUSED=1` loads the city paused; the buildings are placed (a blocking area first bulldozed and left to clear for a few game days); the game is resumed from its speed buttons and the run saves at its last day. The base game's menus have no plain incinerator, so the list at +0x6C stays empty.

`saves-recycle` adds two more recycling centers to `saves-plopped/p0365` the same way (three in all; 2026-10-08): `r0000` as built and `r0090` 90 days on. It is noisier than the others: the original against itself reaches three outcomes in six year-long runs, first parting about 72 days in, so use it for short windows and per-call checks.

`saves-ports` (2026-10-09) has an airport and a seaport, which no other save has: built unattended by `OPENSC4_TEST_PLOP_PORTS` with the original code only ([ports.md](ports.md)); Big City from `saves-plopped/p0000` two days after (`q0000`) and 90 days on (`q0090`), Rush Hour from d0365 (`q0000`).

`%LOCALAPPDATA%\opensc4\parity\saves` (game data, never committed): the three tutorial cities of the test region (Making Money: small town; Big City: dense downtown; Rush Hour: highways), each as found (`d0000`) and after 90, 365 and 1095 game days from there (`d0090`...), made with `run-save-test.sh` under fixed seeds and the virtual clock (before determinism round 3; they are only starting points).
