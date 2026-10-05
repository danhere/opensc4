# Experiment: Pass-Through Pollution Simulator

**Question.** Can a plugin replace a city simulator through GZCOM, with no code patching? Two things have to hold: (1) a class registered by a plugin director under the game's own class ID must win when the city asks for the simulator, and (2) no code may read the simulator's fields through the pointer it gets (a replacement has a different layout).

**Answer for `cSC4PollutionSimulator`: yes on both counts, as far as six and a half years of simulated time in one small city can show.** Dated 2026-10-05, Steam SimCity 4 Deluxe 1.1.641.

## Setup

- `core.dll` acts as a GZCOM director ([`src/core/director.cpp`](../../src/core/director.cpp)) and, when `OPENSC4_PROXY_POLLUTION=1`, registers a proxy class under `0x8990C065` (`cSC4PollutionSimulator`).
- The proxy ([`src/core/pollution_proxy.cpp`](../../src/core/pollution_proxy.cpp)) creates the original through the game's factory (0x006E5F00) and forwards every call of `cISC4PollutionSimulator` (68 methods) and `cIGZSerializable` (3) to it, counting calls. Other interfaces (`cIGZMessageTarget2`, and `0x89C498BE`, probably a shared simulator interface) are handed out as the original's own pointers.
- The proxy's object layout is unrelated to the original's (568 bytes, four vtables), so a field read by offset through the proxy pointer would read the wrong memory.
- Runs are unattended ([`src/core/autotest.cpp`](../../src/core/autotest.cpp), [`scripts/run-autotest.sh`](../../scripts/run-autotest.sh)): load "Making Money Tutorial" (Timbuktu, population 2,039) in an isolated `-UserDir` with no other plugins, run 120 s of real time, quit without saving. Results 1-5 come from runs at speed 0 (2008-11-03 to 2009-01-01 in game); result 6 from a run at speed 2.

## Results

1. **The plugin registration wins.** GZCOM served class `0x8990C065` from our director.
2. **A saved city creates its simulators through the save loader, not through the city's init code.** The loader asked GZCOM for `cIGZSerializable` (`0xE4FDA3D4`), called `Read`, and then queried that pointer for the simulator interface. The first version of the proxy forwarded `cIGZSerializable` to the original, and the city ended up holding the original; the proxy must implement `cIGZSerializable` itself. The city's own `GetClassObject(clsid, iid, &member)` calls (around 0x45AD40) only create simulators missing from the save.
3. **With `cIGZSerializable` proxied, `cISC4City::GetPollutionSimulator()` returns the proxy**, and the game ran normally on it: no crash, clean shutdown, `Shutdown` and final `Release` received.
4. **Calls through the proxy in 120 s:** `GetPollutionValue` 98,304; `GetPollutionGrid` 156; the `Get*Count` methods 19 each; the averages and worst-tract queries 5-15 each; `Init`, `Read`, `Shutdown` once each; `SetGarbageCapacity` and `GetGarbageSupply` twice.
5. **Values track the baseline within run-to-run noise.** Air/water/garbage averages matched a run without the proxy exactly through 2008-12-10 and differed by at most 1 afterwards. Two runs *without* the proxy differed by up to 3 over the same dates, so the game is not deterministic run to run under real-time stepping (an early data point for P4).

| Game date | Baseline A | Baseline B | Proxy |
|---|---|---|---|
| 2008-11-03 | 272/390/493 | 272/390/493 | 272/390/493 |
| 2008-12-03 | 274/392/505 | 274/392/505 | 273/391/505 |
| 2008-12-18 | 276/394/508 | 274/393/508 | 275/394/508 |
| 2009-01-01 | 278/396/517 | 275/394/517 | 277/397/517 |

6. **Long run at full speed:** 120 s at speed 2 covered 2008-11-03 to 2015-04-02 on the proxy, with about 3.8 million forwarded calls (`GetPollutionValue` 3,784,704; `GetPollutionGrid` 4,056; the monthly garbage and capacity queries 60-140 each) and a clean shutdown. Values stayed close to an unmodified run at the same dates (2011-03: 300/424/858 with the proxy, 299/422/856 without).

## Limits

- One small city, six and a half game years, no UI interaction (data views, query tool, disasters and radiation were not exercised). Code paths that only run there could still read fields by offset.
- The proxy forwards to the original, so it shows that callers go through the interface; it does not show that a from-scratch implementation would be accepted by the other two interfaces' users (the message target and `0x89C498BE` still point at the original).
- Exact equivalence can't be tested until P4 provides deterministic stepping.

## Practical notes

- **The user's normal install crashes when loading any city right now**, independently of OpenSC4: NAM 50 is installed without the NAM DLL, the exe is not large-address-aware (Steam restored the original exe), and loading `RuleOverrides` needs a 320 MB block (exception at 0x005B9FEB, allocation size 0x14000000). The isolated `-UserDir` avoids it.
- Steam's DRM stub occasionally relaunches the game through Steam (seen when a run started about 12 s after the previous game exited). The relaunched copy has no `core.dll`; `run-autotest.sh` detects this and asks for a retry.
- `cISC4Simulator::SetSimSpeed` takes the game's own numbering, 0 (slowest) to 2 (fastest); `cSC4Simulator` ignores other values (0x00702430). The first runs passed 3, stayed at speed 0 and covered about one game week per 15 s; at speed 2 the same city runs about ten game months per 15 s.

## Reproduce

```bash
B="$LOCALAPPDATA/opensc4/build/debug"
scripts/run-autotest.sh "$B" OPENSC4_TEST_SECONDS=120                              # baseline
scripts/run-autotest.sh "$B" OPENSC4_TEST_SECONDS=120 OPENSC4_PROXY_POLLUTION=1    # proxy
```
