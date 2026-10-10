# Disasters (cSC4DisasterLayer and the disaster managers)

The city's disaster layer, its seven disaster managers and their disaster instances. Reimplemented piece by piece on the original objects under the parity harness ([parity.md](parity.md)). Build 641 addresses throughout; entries in `data/addresses/disasters.toml`. Code from about 0x00575600 to 0x00580290 (about 44 KB).

## The classes

| Class | ID | Size | Factory | What |
| --- | --- | --- | --- | --- |
| cSC4DisasterLayer | 0x61F6ABF5 | 0xD8 | 0x00578B70 | the managers, the count of disasters under way, their locations, their cost |
| cSC4DisasterManagerVolcano | 0xA9B5AB9F | 0x28 | 0x0057FE00 | disaster 0xC4C90997 |
| cSC4DisasterManagerFire | 0x69C1718E | 0x2C | 0x00579AE0 | disaster 0x621CDA33 |
| cSC4DisasterManagerEarthquake | 0x0A086BD3 | 0x24 | 0x00575670 | disaster 0x42963812 |
| cSC4DisasterManagerPipeBurst | 0x4A132FDF | 0x28 | 0x0057C0E0 | disaster 0x4A1332E8 |
| cSC4DisasterManagerRiot | 0x4A38206C | 0x30 | 0x0057DAA0 | disaster 0xCA380B06 |
| cSC4DisasterManagerEffect | 0x6A7B3E9B | 0x28 | 0x00577200 | disaster 0x6A7B4397 |
| cSC4DisasterManagerToxic | 0xEBFB0402 | 0x28 | 0x0057EDF0 | disaster 0xEBFB0404 |
| cSC4DisasterInstanceFire | 0x69C1718F | 0x100 | 0x0057BB60 | |
| cSC4DisasterInstanceEarthquake | 0xAA086D86 | 0x120 | 0x005763F0 | |
| cSC4DisasterInstancePipeBurst | 0x0A133012 | 0x38 | 0x0057D000 | |
| cSC4DisasterInstanceVolcano | 0x29B5AB89 | 0x54 | 0x0057FF00 | |
| cSC4DisasterInstanceRiot | 0x4A38206D | 0x78 | 0x0057DFA0 | |
| cSC4DisasterInstanceToxic, Effect | (plain `new`) | 0x40 | | |

The city creates the layer (its slot at +0x1C4, interface 0x29B02A2C); the layer's Init creates the seven managers from the table at 0x00A9E3F4 ({disaster type, manager class}).

## The layer

Three vtables: the reference-counted base at +0 (0x00A9E3DC), `cISC4DisasterLayer` at +8 (0x00A9E390, 19 slots) and the message target at +0xC (0x00A9E380, DoMessage 0x00578E10). Not saved.

| Offset | What |
| --- | --- |
| +0x10, +0x1C | maps: key -> stopwatch, the response timers (messages with data 1 of 1 or 5; of 0 or 6) |
| +0x28 | stopwatch: since the first disaster began |
| +0x40 | count of disable requests (EnableDisasters; enabled at 0) |
| +0x44 | initialised |
| +0x48 | disasters under way |
| +0x50 ... +0x9C | the cost (what GetDisasterCost returns): buildings destroyed, total value, then count and value for four purposes (flags 1, 2, 4, 0x20 of the building type) |
| +0xA0 | vector of {name, value}: destroyed buildings whose type has flags 0x1800400 |
| +0xAC, +0xB0 | the response timers' share of the disaster's time |
| +0xB8 | map: disaster type -> manager |
| +0xC8 | the location the camera goes to next |
| +0xCC | vector of locations {disaster, x, z, zoom, occupant}, 0x14 bytes each |

- **A disaster begins** (slot 8, 0x00578FC0): the count goes up; at 1 the simulator service's slot 0x2C and the city's slot 0x1A0 are called and the layer subscribes to five messages, with a fresh cost and timers; message 0xCA1B1E64 (count, cell, disaster ID, the disaster) is posted.
- **A disaster ends** (slot 9, 0x00577E90; it takes no argument, though gzcom-dll declares one): the count goes down; at 0 the city's slot 0x19C and the simulator service's slot 0x38, the messages dropped, the timers' shares computed, message 0xAA1B2002 posted.
- **Messages while a disaster is under way**: 0x99EF1143 (an occupant destroyed) adds its value (property 0x8A1C3E72) to the cost; 0xE94671EC starts and 0x894671F6, 0xE94671EB, 0xE94671ED stop the stopwatch of the message's key.
- **Locations**: a disaster registers the cells it affects (slot 13), with an occupant if it has one (the cell then follows the occupant); with the "go to disasters" option (0x00B43CD8 + 0xF08) the camera goes to a new location (slot 11, GoToNextDisaster), zooming in if the view is further out.

## The managers

A manager is the base (constructor 0x00578160: the reference-counted object, `cISC4DisasterManager` at +8, the message target at +0xC) with its own Init, GetID, start methods (slot 6 by cell, slot 7 anywhere) and message handler (main vtable slot 6). The base keeps the list of instances (+0x14); while the list is being walked (+0x18), adds and removes wait in two more lists (+0x1C, +0x20).

### Toxic spill

One instance (0x40 bytes), made on first use, holds a list of toxic occupants (cSC4ToxicOccupant, 0x5C bytes), one per toxic cell, each with its time left. A start at a cell (or the `ToxicSpill x z time` command, 0x0BFB41BA) adds 120 to the cell's time or makes the occupant and registers it with the layer. The tick (message 0x86AD10EE, at most once a second by the instance's stopwatch) takes 1 off each cell's time, or 4 per responder at the cell; at 0 the occupant is removed, and when none is left the instance shuts down (the layer's count goes down).

### Pipe burst

The manager keeps a pipe filter (an occupant filter that accepts a pipe, type 0xC772BF98, whose cell the plumbing simulator's slot 0x2C accepts). A start at a cell takes the accepted pipe there; a start anywhere draws a random cell (a generator seeded from the clock) and walks outwards in a spiral to the first accepted pipe. Either makes an instance (0x38 bytes) with a first burst at the pipe.

A burst is {position, cell, time, fuse, effect}: its effect is `burst_pipe_aboveground` or `burst_pipe_underground` by the view mode (message 0xC6CDBF1A remakes them), its fuse a random time in the tuning's range (exemplar 0x6534284A/0x29B6C670/3, properties 0x4A08C600 and 0x4A08C601, in seconds). The tick (message 0x86AD10EE with the time passed) adds to each burst's time; past its fuse the pipe explodes (`pipe_explodes`): its connected neighbours inside the city are queued, the pipe is demolished (0x00B43D00's slot 0x20), a crater is made (the city's slot 0x150 object, slot 0xE4), and message 0xEA1F0DCD is posted. Three ticks later the queued cells get bursts of their own. A pipe removed by anything else (messages 0x99EF1142, 0x99EF1143) loses its burst. When no burst and no queued cell is left the instance tells the layer it ended and shuts down.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.disasters.layer` | every layer method but GetDisasterCost (4 bytes): 27 functions |
| `simulators.disasters.managers` | the manager base: QueryInterface, Init, Shutdown, the instance list, the active count, the message forwarding |
| `simulators.disasters.toxic` | the toxic manager and instance |
| `simulators.disasters.pipeburst` | the pipe burst manager, its pipe filter and the instance (21 functions) |

Still the game's: the objects (constructors, destructors), the other managers and instances, container and stopwatch helpers.

## Test cities

No test city has a disaster. `OPENSC4_TEST_DISASTER=<day>` with `OPENSC4_TEST_DISASTER_TYPE=<n>` (0 volcano, 1 fire, 2 earthquake, 3 pipe burst, 4 riot, 5 effect, 6 toxic) starts one through its manager on that day of a run, at the first cell that takes it, walking out from the centre. The camera jumping to the disaster makes the traffic record's view-dependent fields vary run to run with the original (on Big City d0000 one field took the values 10, 11, 12, 13 and 19 in the original's own runs), so the switch turns the "go to disasters" option off unless `OPENSC4_TEST_DISASTER_CAMERA=1`. `OPENSC4_TEST_DISASTER_END=<day>` shuts the chosen type's instances down on that day (a toxic spill lasts two minutes of real time, longer than a week's run), so a disaster's end also runs. `OPENSC4_TEST_PIPE_FUSE=<ms>` sets the pipe burst's fuses after it starts, so pipes explode and the bursts spread within the run; where the walk finds no accepted pipe the switch falls back to the manager's own choice (slot 7).

The fingerprint's `dis` part: the disable count, disasters under way, the cost's building count and total, the locations (count and a hash of their cells and zooms) and each manager's instance count (in type order: earthquake, pipe burst, fire, effect, volcano, riot, toxic).

With a disaster under way the original does not always repeat itself: in the control (the original against itself, 12 saves, 7 days) fire and earthquake each gave two or three outcomes on two to five saves. Verdicts go by outcome sets.

## Parity

Layer, manager base and toxic on, 12 saves, 7 days, two jobs at a time, on a machine that other threads' batches were loading (2026-10-10; folders `dism2-*` under `%LOCALAPPDATA%\opensc4\parity\`). "Partly" means some of B's runs matched the original's and some did not, on saves where the original's own runs also part:

| Leg | Same | Rest |
| --- | --- | --- |
| no disaster | 9 | 2 partly, 1 failed to start; the off runs differ in the traffic record's view-dependent fields only, fingerprints identical (12 of 12 on a quiet machine with the layer alone, `disl1-7`) |
| toxic spill on day 1 | 12 | |
| toxic, ended on day 3 | 10 | 2 partly |
| fire on day 1 | 11 | 1 partly |
| fire, ended on day 3 | 11 | 1 partly |
| earthquake on day 1 | 6 | 5 partly; Big City d0000 gave six outcomes from six runs (three each side) |
| earthquake, ended on day 3 | 9 | 3 partly |
| fire, camera jumping | 11 | 1 partly |

No run crashed, and no save had the original agree with itself while ours agreed with itself on something else. The earthquake shakes on frame time; in the control (`disc1-t2`) the original against itself was 10 of 12 on a quieter machine.

With the pipe burst on too (`disp1-*`, `disp2-fuse`; same machine, same day):

| Leg | Same | Rest |
| --- | --- | --- |
| no disaster | 10 | 2 partly |
| pipe burst on day 1 (a pipe found on the four Rush Hour saves only) | 12 | |
| pipe burst, ended on day 3 | 8 | 4 partly (two in the harness's tick count, on saves with no pipe) |
| pipe burst with 2-second fuses, the manager's own choice where the walk finds no pipe (8 saves with a burst) | 10 | 1 partly (the original gave three outcomes), 1 failed |

With short fuses the pipes explode and the bursts spread: the layer's cost rises the same way day by day on both sides (0, 220, 260, 410 on Rush Hour d0365), and the manager's random choice of pipe matched on the four saves that used it. The failed run was a B run that crashed in the game's code at 0x0080105D while the city loaded, before any disaster, with none of our code on the stack; three repeats of that save loaded and matched (`disp2-rep1` to `rep3`), as did about 80 other B runs of this code. It is listed with the other rare load crashes in [parity.md](parity.md).
