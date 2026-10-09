# The simulator (cSC4Simulator): clock and scheduler

The object every other simulator hangs off: the simulation date, the pauses and the speed, and the scheduler that hands time to the other simulators (its agents). Reimplemented the same way as the simulators ([aura.md](aura.md), [civic.md](civic.md)): function by function on the original object under the parity harness ([parity.md](parity.md)), then our own object under the class ID. Build 641 addresses throughout.

Kept out on purpose: the frame's time slice (`RunTimeSlice`, 0x00703E70) and adding and removing agents (`AddAgent` 0x00704360, both `RemoveAgent` overloads, with the map insert 0x00704060). The determinism replacements hook these (`determinism.fixed_ticks`, `determinism.agent_order`; [experiments/determinism.md](experiments/determinism.md)), and frame timing stays as it is for now.

## The class

Class ID 0x2990C1E5. Object size 0x1E0. Factory 0x00704340 (`SC4::operator_new(0x1E0)`, then the constructor 0x00704120). Code from 0x00702020 to 0x007045A0. Five interfaces:

| Vtable | Part | Slots |
| --- | --- | --- |
| 0x00AB2FB8 | `cISC4Simulator` (+0, IID 0x8695664E) | 34: identity, Init 0x00703250, Shutdown 0x00702950, then the slots below |
| 0x00AB2FA0 | `cIGZSerializable` (+0x4) | Write 0x00702290, Read 0x00702340, GetGZCLSID 0x00702030 |
| 0x00AB2F6C | the system service (+0x8, `cRZBaseSystemService`, ID 0x46956659, priority -2,000,000) | Init and Shutdown thunks, OnTick = RunTimeSlice, the base's own slots |
| 0x00AB2F54 | the base's second interface (+0xC) | the deleting destructor in slot 3 |
| 0x00AB2F44 | `cIGZMessageTarget2` (+0x24) | DoMessage 0x00703540 |

The references are at +0x28; every part's Release ends in the +0xC part's slot 3. Interfaces other than the two above come from the base's QueryInterface (0x0091DD67).

### The interface

| Slots | What |
| --- | --- |
| 5-8 | the start date (1 January 2000), the date's parts, the date object (+0xF0), the day number |
| 9-18 | pause, hidden pause and emergency pause (counts at +0x30, +0x34, +0x38), their resumes and queries; the first pause and the last resume of each kind post 0xAA7FB7E0, 0x4A7FB7E2 or 0x4A7FB807; an emergency pause also pauses and stops speed changes (+0x44) |
| 19-24 | AddAgent, RemoveAgent (two), RemoveAllAgents (two), EnumerateAgentsByName |
| 25, 26 | an agent's enabled flag by name |
| 27-31 | speed (+0x3C), sim time (+0x40), time per tick (+0x78) |
| 32 | the animation time dilation: the tuning's values by pause and speed |
| 33 | city established: each agent enabled by its flags for before or after |

### The scheduler

Agents are kept in 11 maps by type (+0x114, red-black trees keyed by the agent pointer, or by registration order under `determinism.agent_order`), each with a cursor (+0x1B0). A node holds the agent, its enabled flag, its name and its flags. Types 0 and 1 hold the clock while any are queued (type 0 leaves after its turn), 2 to 7 are the units of time (minute, hour, day, week, month, year), 10 the idle agents. An agent's message is 0x66956810 + type with the day number, 0 and the type's data (+0xB0: the minute, hour, day of year, week, month, year).

Each tick of the slice runs:

- **The clock (0x00702A80).** Nothing moves while agents of types 0 or 1 are queued. A minute passes once every minute agent had its turn in the current round; an hour after 60 minutes once the hour agents had theirs; a day after 24 hours once the day agents had theirs and, when tomorrow starts a week, month or year, that unit's agents too. A unit with no agents passes at once. The paced unit (+0x7C: 2, 3 or 4) also waits for the step watch (+0x48) to pass the time per step (+0x80: 5000, 1000 or 100 ms by speed, from the tuning); `determinism.fixed_ticks` makes that watch always pass. The day posts 0x66956814 (NewDay), then 0x66956815 (week), 0x66956816 (MonthPassed) and 0x66956817 (year) when they begin. The units passed this tick are flagged at +0xDC.
- **The agents (0x007038B0).** The units passed get their agents' round in full (0x00702F10, each agent again until it answers true), then agents one at a time from the cursors while the slice lasts (+0x198 against +0x78): types 0 and 1 first, then types 2 to 9 that still have a round going.
- **The idle agents (0x00702DF0)**, type 10, from their cursor while the slice lasts, each with a budget of the time left, at most a third of the tick's (message 0x6695681A).

### Fields

| Offset | What |
| --- | --- |
| +0x2C, +0x2D, +0x2E | initialised; a saved byte; city established |
| +0x30 ... +0x44 | pause counts, speed (3 = none yet), sim time, speed changeable |
| +0x48, +0x60, +0x198 | stopwatches: the step, another, the slice |
| +0x78 ... +0x88 | time per tick, paced unit, time per step, frame rate cap (30, 20, 15 by speed), the cycles estimate |
| +0xB0, +0xDC | each agent type's data and passed flag |
| +0xE8 | the date (`cRZDate`, its `cIGZDate` at +0xF0, day number at +0xEC) |
| +0xF8 | the message server |
| +0xFC, +0x108 | the times per step by speed; the animation dilations (a float vector) |
| +0x114, +0x1B0 | the agent maps and their cursors |

### Messages and cheats

| Message | What |
| --- | --- |
| 0x26D31EC1 DoPostCityInit | the hidden pause Init set is resumed |
| 0x8BAC4B43 | the date command's query: year, month, day of month, day of week, day of year and day number as script numbers |
| 0x230E27AC | a cheat: 0x0AC07E24 sets the frame rate cap, 0x8A78BEFF the date (and steps the clock) |

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.core.interface` | slots 5-18 and 22-33 (all but AddAgent, both RemoveAgent overloads and EnumerateAgentsByName) |
| `simulators.core.clock` | the clock step (0x00702A80), the message post (0x007020E0), the speed (0x00702430) |
| `simulators.core.agents` | the tick's agents (0x007038B0), a type's round (0x00702F10), the idle agents (0x00702DF0), a message to an agent (0x007024C0) |
| `simulators.core.init` | Init, Shutdown, the tuning (0x007025B0) |
| `simulators.core.messages` | DoMessage (+0x24 slot 3) and the cheats (0x00703420) |
| `simulators.core.save` | Write (version 4) and Read (versions up to 4) |
| `simulators.core.object` | our own object under the class ID: constructor, destructor, identity, our copies of the five vtables; the service part is made and destroyed by the game's base class functions |

Still the game's: RunTimeSlice, AddAgent, RemoveAgent and its helpers, EnumerateAgentsByName, the map node free (0x00703850) and the container, date, stopwatch, string and tokenizer helpers, GetGZCLSID.

## Test cities

`OPENSC4_TEST_SIMULATOR=<n>` calls, on the n-th day, the slots the test cities never call, with values that leave the simulator as it was: each pause with its resume and the queries between, the dates, the first day agent's enabled flag read and set again, the established flag flipped and back, the sim time and time per tick set to themselves, the animation dilation. The cheats and the date query are not driven.

The fingerprint's `sim` part: the pause counts, speed, sim time and flags, the clock's hour and minute, the agent types' data and passed flags, per agent type the count, the cursor's place and the enabled flags, the tick, pacing, step and frame rate settings, the step times and the dilations. `tools/re/save_schemas/simulator.py` decodes the record.

## Parity

With everything on, our object included, and `OPENSC4_TEST_SIMULATOR=2`, two jobs at a time. Results (2026-10-09; folders under `%LOCALAPPDATA%\opensc4\parity\`):

| Run | Result |
| --- | --- |
| 7 days (`core3`) | **12 of 12**, every pair identical on the first run; simulator records identical; every replaced function reached in every B run but the cheats and the date query |
| `saves-plopped`, 7 days (`core3-plopped`) | 3 of 3 |
| `saves-mm2`, 90 days (`core3-90`) | 2 of 2, identical for 91 days |

