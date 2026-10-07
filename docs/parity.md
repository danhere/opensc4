# Parity testing (P5)

How a replaced simulator is shown to behave like the original. The game does not replay deterministically over long spans ([experiments/determinism.md](experiments/determinism.md)), but with fixed seeds and the virtual clock two runs from the same save usually agree exactly for a week of game time. So parity is tested in **short windows from many saves**, compared exactly; long runs will be compared statistically (not built yet).

## Running it

```bash
B="$LOCALAPPDATA/opensc4/build/debug"
scripts/run-parity.sh "$B" "$LOCALAPPDATA/opensc4/parity/proxy" 7 "$LOCALAPPDATA/opensc4/parity/saves" \
    OPENSC4_PARITY_B=simulators.pollution.proxy=ours
```

- `<saves dir>` holds one folder per city, named as the city in the test region, with saves of that city in it. For each save the harness runs the city twice through [scripts/run-save-test.sh](../scripts/run-save-test.sh) (`OPENSC4_TEST_START` puts the save in place of the city's file in the disposable region): once with replacement set A (`OPENSC4_PARITY_A`, default none: the original game) and once with set B (`OPENSC4_PARITY_B`). Both get `determinism.fixed_seed` and `determinism.virtual_clock` and log a fingerprint every game day.
- After `<days>` game days each run saves and quits. A run's **outcome** is its daily fingerprints ([tools/re/fpdiff.py](../tools/re/fpdiff.py): population, funds, lot count, traffic, land value and pollution grid checksums) together with every saved record whose layout is decoded (`tools/re/sc4save.py`, field by field; CRCs and heap addresses are left out, references keep their class).
- If the two outcomes differ, the pair is run again, up to `OPENSC4_PARITY_RETRIES` more times (default 3; retries go to `<save>/try1`...). [tools/re/paritygroups.py](../tools/re/paritygroups.py) groups all runs of the save by outcome, and the save passes when **every outcome of B is also an outcome of A**: B did nothing the original does not also do. "partly" means B shares some outcomes with A but not all; "different" means none.
- Results go to `<output dir>/<city>/<save>/` (`a.log`, `b.log`, `a.sc4`, `b.sc4`, `fpdiff.txt` and `records.txt` for the first pair, `groups.txt`) and a `summary.txt`. Runs already done are reused, so an interrupted run can be resumed; delete the output folder to start over.
- A run with A = B is the control: it shows the noise of the method itself on those saves.
- One run takes about 20 s plus a 20 s pause (Steam relaunches the game when it starts right after the last run).

## Sampling inside the simulator's tick

The autotest fingerprints and stops from a simulator agent (`cISC4Simulator::AddAgent`, `AgentTypeSimNewDay`), which the simulator calls from inside its tick when the day changes, and pauses there on the last day. The first version did both from the framework tick, on the first frame after the day number changed; by then the simulator had run however many further ticks that frame had room for, so the sample point and the saved moment moved from run to run. The first control (12 saves, 7 days) agreed on only 4 saves, mostly with one day's pollution checksum differing and the next agreeing again ("the pollution update landed on day 4 in some runs and day 5 in others" in the determinism experiment was this). Sampling in the tick: 9 of 12 agree.

## The original does not always agree with itself

The three saves that still differed in that control are all Rush Hour (the traffic-heavy city): funds from day 4 (`d0090`), a pollution grid from day 5 (`d0365`), and in `d0000` every fingerprint agrees but the saved pollution record does not (the 4,096-value block at +0x1D8, which nothing in the fingerprint reads). That is the game's own non-determinism (divergence starts in traffic, [experiments/determinism.md](experiments/determinism.md)) showing within a week, and it is sporadic: `d0000` agreed in the first control.

It is not limited to Rush Hour, and a clean control does not mark a save as safe. Big City `d0090` agreed in both controls, but repeated runs showed its day-5 air checksum taking one of two values in the original (CDD6CF94, or 92AE9957 in 3 of 9 runs). Rush Hour `d1095` gave three different outcomes in 15 runs of the original, with the mix shifting between sessions (outcome 0 in the earlier runs, outcome 3 in later ones). Hence the retries, and a verdict over outcome sets rather than single pairs.

## First subject: the pass-through pollution simulator

`simulators.pollution.proxy` forwards every call to the original ([experiments/pollution-proxy.md](experiments/pollution-proxy.md)), so it should be indistinguishable from it.

- **It found a forwarding bug.** On Rush Hour the proxy reproducibly reached an outcome the original never did (six of seven proxy runs alike), with funds differing from the first month boundary. Eight garbage getters return 64-bit values in EDX:EAX that gzcom-dll declares as `int32_t`, and the game reads all 64 bits; the proxy returned EAX with whatever EDX held. Fixed (details in the experiment doc).
- **After the fix, 9 of 12 saves pass** (7 days, up to 4 pairs each). Not passing: Big City `d0090` "partly" (the original gave one outcome in 4 runs, the proxy four different ones, one of them the original's), Rush Hour `d1095` "different" in that run (across all sessions the fixed proxy's outcomes were 0, 1 and 3, each twice; the original's 0, 2 and 3, and 1 never in 15 runs), and Rush Hour `d0365`, where a proxy run crashed during load (below).
- So the fixed proxy is not yet shown to be exact: its outcomes spread more widely than the original's on the noisy saves. The explanation that fits is that the remaining non-determinism (thread interleaving under real time) is sensitive to timing, and the proxy adds a little time per call (one more indirection, a log line on each method's first call). It is not proven; making it provable means removing the real-time dependency (serialising the worker thread, [experiments/determinism.md](experiments/determinism.md)).
- **Open: one crash.** One of about 24 runs with the fixed proxy crashed while the city loaded (game exception report, access violation at 0x0096DA20: `inc [eax+4]` with EAX = 0 in a constructor at 0x0096D9D1, called from 0x00970296), right after the proxy's first forwarded garbage calls. Not reproduced or explained.

## Save corpus

`%LOCALAPPDATA%\opensc4\parity\saves` (game data, never committed): the three tutorial cities of the test region (Making Money: small town; Big City: dense downtown; Rush Hour: highways), each as found (`d0000`) and after 90, 365 and 1095 game days from there (`d0090`...), made with `run-save-test.sh` under fixed seeds and the virtual clock.
