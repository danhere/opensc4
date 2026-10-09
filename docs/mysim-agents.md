# The MySim agents (cSC4MySim, cSC4MyStreetSim)

The player's own Sims who move into a city (the "My Sim" mode): each finds a house and a job, drives about, has a mood and shows advice balloons. They are owned by the MySim agent simulator ([mysim.md](mysim.md)) and feed nothing else in the city's simulation. Dan sees no use for the mode; it stays in the rewrite for completeness, with a switch to turn it off later (ROADMAP.md). Build 641 addresses throughout.

## The classes

| Class | Class ID | Constructor | Code |
| --- | --- | --- | --- |
| `cSC4MySim` | 0x4A1DBBBF | 0x00429890 (factory 0x0042A9F0) | 0x00424B00 to 0x0042E300 |
| `cSC4MySimDispatch` | 0xCBC14674 | 0x00433A60 | around 0x00433000 to 0x00434800 |
| `cSC4MyStreetSim` | 0x0BF606A5 | 0x0043D5F0 (factory 0x0043DFA0) | 0x0043A050 to 0x0043EBA0 |

`cSC4MySim` interfaces: +0 0x00A85218, +8 0x00A851C0, +0xC 0x00A851A4 (`cIGZSerializable`: Write 0x004251A0, Read 0x00425490), +0x10 0x00A85194 (`cIGZMessageTarget2`: DoMessage 0x0042D150), +0xB8 0x00A85080 (`cISC4MySim`, 69 slots in gzcom-dll's order once overloads are grouped: the two `GetJobTitle` are slots 27 (with an occupant) and 28; 4 MoveIn 0x0042D880, 5 FindNewResidence 0x0042A700, 10 GetSlot, 11 GetResidence, 12 GetJob, 20 IsActive). The +0 interface's slot 6 (0x0042DC90) is the update the advisor system runs every 500 ms (through 0x0040D820, slot 14 of +8).

The cISC4MySim methods take the +0xB8 part as `this`; offsets in src/core/mysim_agents.cpp are from there (state at +0x8, residence +0xC, job +0x28, dispatch object +0x154).

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.mysim_agents.interface` | 28 cISC4MySim methods: names and job titles, education and health text, the vehicle key, hiding the residence and location, the pylon, leaving for work and returning home, building changed, neighbour connection removed, distances, the night sound, the screen location, sounds, model buttons, balloons, U-Drive-It, the script context (slots 3, 15, 25-30, 33, 35, 36, 38, 39, 43, 49, 50, 54-56, 59-66, 68) |
| `simulators.mysim_agents.save` | Write and Read (version 0x11; Read takes 0xA and later) |
| `simulators.mysim_agents.messages` | DoMessage: trips and arrivals from the dispatch object (0x2BA06B99), balloons, the frame message 0x86AD10EE (Think every 2 ms of the stopwatch at +0x280) |
| `simulators.mysim_agents.update` | the update and Think (balloon advice entries round robin, script commands each new day) |
| `simulators.mysim_agents.events` | 15 cISC4MySim methods for city changes: ChangeResidence, TestForResidence, ChangeJob, DeleteFromGame, GetLocation, AnnounceMoveIn, DoScriptCommand, DoBuildingAdded, DoBuildingRemoved, DoFireAdded, DoPipeBurst, DoTornado, DoDisaster, DistanceToClosestSpecial, CreateDispatchTool |
| `simulators.mysim_agents.residence` | MoveIn (the move-in tool becomes the dispatch tool), FindNewResidence (a random house from the occupant manager's list, drawn from the MySim's own generator at +0xD8 of the cISC4MySim part, tried round the list) and SetResidence (on moving in: age, education and health from the city, the first trip) |

Replacement code reads the MySim stopwatches through `sim_timers::Claim` first, so `determinism.sim_timers` takes them over as it would for the original code.

## Testing

A MySim acts on real-time stopwatches, so runs with one repeat only with `determinism.sim_timers` ([experiments/determinism.md](experiments/determinism.md), "Simulation timers"). Saves with a MySim: `%LOCALAPPDATA%\opensc4\parity\saves-mysim`. `OPENSC4_TEST_MYSIM_MOVEIN=<day>` (autotest) moves a MySim in on that day: the agent simulator's MoveIn, then FindNewResidence and CancelMoveIn (MoveIn alone only opens the placement tool).

`OPENSC4_TEST_MYSIM_QUERIES=1` puts what the MySim panel and balloons ask every MySim each day (names, titles, education and health text, vehicle key, model buttons, current balloons, U-Drive-It) into the fingerprint. `OPENSC4_TEST_MYSIM_DRIVE=<day>` makes every MySim, on that day, leave for work, return home, play its night sound, hear that its home changed and a neighbour connection went, hear of a fire, a pipe burst, a tornado and two disasters near it and of its home added, test its home, run a script command, see its job's building replaced and change job. `OPENSC4_TEST_MYSIM_DELETE=<day>` makes the first MySim leave the game. GetLocation is not in the queries: for a MySim on the road it is the car's position, which moves on frame time.

## Parity

All four groups on, 2026-10-09: `saves-mysim` 4 of 4 for 30 days, with the queries and the drive on as well 4 of 4; the usual 12 saves for 7 days 11 of 12 and the twelfth (Big City d0365: one of three runs of ours ended with a different traffic simulator record, every fingerprint identical) same when repeated. Reached unattended: the interface methods above with the queries and drive switches, Write and Read, DoMessage, the update and Think.

With the events and residence groups as well (2026-10-09): `saves-mysim` with the queries, the drive (day 3) and the delete (day 20) 3 of 4 and the fourth partly (one run of ours of three began a day one tick early; the original alone shows such second outcomes in the parallel harness on other saves too); with a fifth group run one at a time, 4 runs of the original and 4 of ours from that save identical. The usual 12 with a MySim moving in on day 2 (`OPENSC4_TEST_MYSIM_MOVEIN=2`, so MoveIn, FindNewResidence and SetResidence run in every city): 11 of 12; on Rush Hour d0090 the original has two outcomes of its own after the move-in (the same configuration run twice gave both; seeds identical), and ours gave one of them.

**A moved-in MySim does not always repeat.** It travels by car, and cars move on frame time ([traffic-map.md](traffic-map.md)), so when it arrives can differ between runs of the original; its state (and the advice it gives) then differs from that day. Judge MySim runs with travel by outcome sets.
