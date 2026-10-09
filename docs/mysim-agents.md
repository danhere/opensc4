# The MySim agents (cSC4MySim, cSC4MyStreetSim)

The player's own Sims who move into a city (the "My Sim" mode): each finds a house and a job, drives about, has a mood and shows advice balloons. They are owned by the MySim agent simulator ([mysim.md](mysim.md)) and feed nothing else in the city's simulation. Dan sees no use for the mode; it stays in the rewrite for completeness, with a switch to turn it off later (ROADMAP.md). Build 641 addresses throughout.

## The classes

| Class | Class ID | Constructor | Code |
| --- | --- | --- | --- |
| `cSC4MySim` | 0x4A1DBBBF | 0x00429890 (factory 0x0042A9F0) | 0x00424B00 to 0x0042E300 |
| `cSC4MySimDispatch` | 0xCBC14674 | 0x00433A60 | around 0x00433000 to 0x00434800 |
| `cSC4MyStreetSim` | 0x0BF606A5 | 0x0043D5F0 (factory 0x0043DFA0) | 0x0043A050 to 0x0043EBA0 |

`cSC4MySim` interfaces: +0 0x00A85218, +8 0x00A851C0, +0xC 0x00A851A4 (`cIGZSerializable`: Write 0x004251A0, Read 0x00425490), +0x10 0x00A85194 (`cIGZMessageTarget2`: DoMessage 0x0042D150), +0xB8 0x00A85080 (`cISC4MySim`, 69 slots in gzcom-dll's order: 4 MoveIn 0x0042D880, 5 FindNewResidence 0x0042A700, 10 GetSlot, 11 GetResidence, 12 GetJob, 20 IsActive).

## Testing

A MySim acts on real-time stopwatches, so runs with one repeat only with `determinism.sim_timers` ([experiments/determinism.md](experiments/determinism.md), "Simulation timers"). Saves with a MySim: `%LOCALAPPDATA%\opensc4\parity\saves-mysim`. `OPENSC4_TEST_MYSIM_MOVEIN=<day>` (autotest) moves a MySim in on that day: the agent simulator's MoveIn, then FindNewResidence and CancelMoveIn (MoveIn alone only opens the placement tool).
