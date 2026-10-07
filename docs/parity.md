# Parity testing (P5)

How a replaced simulator is shown to behave like the original. With `determinism.fixed_seed`, `determinism.virtual_clock` and `determinism.fixed_ticks`, two runs of the original from the same save agree exactly: every daily fingerprint for a year, and the whole save apart from a noise floor ([experiments/determinism.md](experiments/determinism.md), third round). So a replacement can be compared with the original exactly, over short windows from many saves; long runs too, within the limits below.

## Running it

```bash
B="$LOCALAPPDATA/opensc4/build/debug"
scripts/run-parity.sh "$B" "$LOCALAPPDATA/opensc4/parity/proxy" 7 "$LOCALAPPDATA/opensc4/parity/saves" \
    OPENSC4_PARITY_B=simulators.pollution.proxy=ours
```

- `<saves dir>` holds one folder per city, named as the city in the test region, with saves of that city in it. For each save the harness runs the city twice through [scripts/run-save-test.sh](../scripts/run-save-test.sh) (`OPENSC4_TEST_START` puts the save in place of the city's file in the disposable region): once with replacement set A (`OPENSC4_PARITY_A`, default none: the original game) and once with set B (`OPENSC4_PARITY_B`). Both get the three determinism replacements and log a fingerprint every game day.
- After `<days>` game days each run saves and quits. A run's **outcome** is its daily fingerprints ([tools/re/fpdiff.py](../tools/re/fpdiff.py): population, funds, lot count, traffic, land value and pollution grid checksums, and the tick on which the day began) together with every saved record whose layout is decoded (`tools/re/sc4save.py`, field by field; CRCs and heap addresses are left out, references keep their class).
- If the two outcomes differ, the pair is run again, up to `OPENSC4_PARITY_RETRIES` more times (default 3; retries go to `<save>/try1`...). [tools/re/paritygroups.py](../tools/re/paritygroups.py) groups all runs of the save by outcome, and the save passes when **every outcome of B is also an outcome of A**. With determinism this should not be needed; it stays as a safety net and to show when a difference is systematic (the same outcome every time).
- **Both runs get the same environment**: the replacement lists go in an `opensc4.ini` per side (`a.ini`, `b.ini` in the output folder, installed by `run-save-test.sh` as `OPENSC4_TEST_INI`), not in `OPENSC4_REPLACE`. The game's outcome depends on its heap layout, and the C runtime copies each environment variable to the heap at startup: one extra variable (`OPENSC4_PARITY_B=...`, read by nothing) changed the original's outcome on Making Money d0090, reproducibly, while a variable of the same length under another name did not; padding the environment to the same total size still left 3 of 12 saves different, because the variables' individual lengths differed.
- Results go to `<output dir>/<city>/<save>/` (`a.log`, `b.log`, `a.sc4`, `b.sc4`, `fpdiff.txt` and `records.txt` for the first pair, `groups.txt`) and a `summary.txt`. Runs already done are reused, so an interrupted run can be resumed; delete the output folder to start over.
- A run with A = B is the control: it shows the noise of the method itself on those saves.
- One run takes about 20 s plus a 20 s pause (Steam relaunches the game when it starts right after the last run).

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

Along the way the proxy turned up a real bug: eight garbage getters return 64-bit values in EDX:EAX that gzcom-dll declares as `int32_t`, and the game reads all 64 bits; the proxy returned EAX with whatever EDX held (fixed; [experiments/pollution-proxy.md](experiments/pollution-proxy.md)). Its other differences turned out to be the original's own: timing-dependent outcomes that the proxy's small overhead, or the RNG tracer's, made more or less likely, and finally the environment difference above. One crash in a proxy run (0x0096DA20, Rush Hour d0365, during load) was never reproduced.

## Limits

- **Heap layout.** The outcome is deterministic for a given heap layout, not independent of it: something in the game processes objects in an order that depends on their addresses (the tract developer visits tracts in a different order in some runs; [experiments/determinism.md](experiments/determinism.md)). Identical environments keep the layout equal between A and B. A replacement that changes allocations in the game's own heaps could still shift it; the pass-through proxy, allocated by core.dll's C runtime, did not. Finding and neutralising the address-ordered processing would remove this limit, and is the next thing to do if a replacement shows a systematic difference that its own code does not explain.
- **Schedule.** `determinism.fixed_ticks` ends a day as soon as its work is done; real play on a fast machine spends waiting time on idle work. Parity holds for this schedule.
- **Whole saves** still differ from run to run in some records (automata, advisors, props, the traffic network map; [experiments/determinism.md](experiments/determinism.md)); the harness compares fingerprints and decoded records, which agree.

## Save corpus

`%LOCALAPPDATA%\opensc4\parity\saves` (game data, never committed): the three tutorial cities of the test region (Making Money: small town; Big City: dense downtown; Rush Hour: highways), each as found (`d0000`) and after 90, 365 and 1095 game days from there (`d0090`...), made with `run-save-test.sh` under fixed seeds and the virtual clock (before determinism round 3; they are only starting points).
