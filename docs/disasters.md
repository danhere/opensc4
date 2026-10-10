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

### Volcano

One volcano at a time. The manager's start (at a cell, or at a random one) makes the instance (0x54 bytes; its instance interface is its first vtable, the volcano interface 0x29B5AB88 is at +4), gives it the place, initialises it and puts it in state 3. The instance is a state machine (the +4 interface's slot 8):

| State | What |
| --- | --- |
| 1 | set by Init (the layer is told the disaster began; the place, clamped to the city, is registered) |
| 3 | the `volcano_control` effect starts; if the tuning allows building, the terrain's volcano builder (0x178 bytes, 0x0075E820) is made and the state goes to 5 |
| 5 | the builder starts at the place with the tuning's height |
| 7 | the effect reported its end (message 0xCA493C1D): back to 5 while building, else 8 |
| 8 | the builder is finished, the layer told the disaster ended, and the instance takes itself off the manager's list |

The tick (message 0x86AD10EE) adds the time passed; while building in state 5 it sets the builder's two rates from two tuning curves at time / duration; past the duration the builder is finished and the state goes to 3 again (or 8 when the effect is over). Tuning: exemplar 0x6534284A/0x29B6C670/1.

### Riot

The manager's start at a cell makes a riot instance (0x78 bytes; saved with the city) while fewer than the tuning's most are active, and adds a first rioter. A rioter is an automaton (type 0x4107) placed at a random spot in the cell of the nearest lot (within 8 rings) that takes residents, commerce or industry, unless the police cover there is too good (the police grid's value through the instance's curve, above 1) or the tuning's most rioters are out. Each rioter keeps {automaton, time, level, top level, lot}.

The tick (message 0x86AD10EE, at most once per millisecond of the instance's stopwatch) adds to each rioter's time 1 less the curve at its cell, so good cover holds it back or runs it down; its level is time / level time. A new level sets the automaton's `Riot<n>` animation; a new top level spawns the tuning's count of rioters for that level around it and posts the `riot_leader_<level>` news. A rioter whose automaton is gone, whose time ran below 0 or who passed the last level is removed. The lots in the 7 by 7 cells around a rioter are handed to the manager, which keeps them in a set and marks them (their slot 0xC0 object, slot 0x34 with 2; unmarked when the last riot goes). The layer is told the riot began when it has its first rioter and ended when the last has gone.

Tuning: exemplar 0x6534284A/0x29B6C670/0x4A38206D (6 levels, level time 30, spawn counts 0 1 0 1 0 0 and a curve 0, 0 up to cover 64, 1 at 65, 3 at 255 by default).

The instance's Read (0x0057E870) first resizes the rioter list to the saved count, with a rioter whose automaton, time and levels are whatever is on the stack, and then appends the saved rioters, which reads as twice the saved count with the first half garbage. That is from reading the code, not from a test (no save with rioters was loaded); Read is left the game's.

### Fire

The fire manager (0x2C bytes) has a fire part (interface 0x49B43F57 at +0x24) that other simulators and tools call, and one instance (0x100 bytes), made when the manager is initialised, that holds every fire in the city. The manager's start at a cell is the part's start with no chance draw and not forced. The instance has three interfaces: the fire interface (0x49B43F58, +8) with the work, the instance interface (+0xC) the layer and manager use, and the message target (+0x10).

A fire is a fire occupant (cSC4FireOccupant, 0x6C bytes, the game's) placed on a burning building (type 0x278128A0), lot object (0xA823821E) or flora (0x74758926). It has a heat, a stage and a group; fires started by one call share a group, and a group has one location with the layer, which passes to another fire of the group when its holder goes out.

- **Starting** (fire interface slot 6: occupant, forced, group, jump): only the three types, only with a flammability (property 0x29244DB5) above 0. Unless forced, nothing starts while the simulator service reports its slot 15 without its slot 17, and the fire cover over the occupant's cells (a grid of service 0x00B43D78, through the third tuning curve, rounded) must not be above 0. An occupant already burning (property 0x69DBEBE1) only gets 1 more heat, and only when forced. Otherwise a fire occupant is made and listed, the group's first fire registers the location, and message 0x49E3DB1D is posted. A forced fire starts at the first stage's limit (half of it for a one-stage occupant).
- **By cell** (slot 7: x, z, by chance, forced, group): with "by chance" a draw in [0, 100] must not exceed the fourth curve at the cell's value in a grid of service 0x00B43D7C; then a start on every burnable occupant in the cell.
- **The step** (the update, slot 4, on tick 0x86AD10EE and message 0x66956813): up to five occupants from the burned queue are destroyed; then, once every +0x80 milliseconds (1000) of the instance's stopwatch, every fire steps: a fire with an end time is removed when it has passed; otherwise its heat goes up by 1, or down by a draw up to the cover's cooling (the third curve at the fire cover, plus 4 per responder); below 0 the fire gets an end time up to 4 seconds on; the stage follows the heat through five limits; past the occupant's last stage the fire burns it down (unless the city's slot 0x148 object holds that off). A building's lot is shown in its two last looks (the lot's slot 0x90 with 5 or 6). Then, with a chance of 1 over the stage's odds, the fire spreads: a cell drawn inside the fire's bounds grown by the stage's radius and the wind (times the tuning's factor) gets a by-chance start in the fire's group.
- **What a fire leaves** (0x005796E0): an effect by type; flora is removed with a `fire_flora_scorch` mark; a building's lot is demolished (service 0x00B43D00's slot 6 with the lot's cells, flags 0x300 and the occupant's rubble property 0x6A43150F) and may leave pollution (properties 0xAA5C04C8 and 0xAA5C04C5); anything without a lot is just removed.
- **The state**: with the first fire the layer is told a disaster began, the tick is taken and messages 0xA9C7CAB6 (state 1) and 0x49D15B72 are posted; when the last fire has gone, state 3, the locations withdrawn, the layer told, the tick given back. Each update posts 0x49D15B73 with the counts of new fires.
- **The part's other calls**: douse a cell (its fires lose that much heat), burn a cell down (its occupants are queued as burned), burn an area (within the inner radius burned down, within the outer set alight), ignite an occupant, an owner value.

Tuning: exemplar 0x6534284A/0x29B6C670/0xC9B6B73D (stage lengths, spread odds and radii by stage, the wind factor, the location's zoom, four curves); without it five stages of 10, odds of 10, radii 0 3 5 7 9.

### Earthquake

One earthquake at a time. The manager's start at a cell makes the instance (0x120 bytes; no start anywhere), starts it and puts it straight into the quake phase.

The start loads the tuning at first use (exemplar 0x6534284A/0x29B6C670/2: four curves and six numbers), clamps the epicentre to the city and makes the fault: a direction (a base angle drawn once per game session in [0, pi], plus a draw within the tuning's spread, from a static generator at 0x00B22AE8) and two halves that leave the epicentre in opposite directions. Each half is a cubic Bezier curve of four control points: the epicentre, then three points each a third of the city's width further along, pushed sideways by a draw within the tuning's jitter (the first pair shares one draw, mirrored). It then plays `QuakeStart`, walks the fault once in coarse steps leaving a news item at every point (the preview), registers the epicentre's cell and tells the layer.

| Phase | What |
| --- | --- |
| 1 rumble | the strength (0.1 at first) rises by 1e-7 per unit of time up to 1; a `QuakerShaker` effect at the epicentre every tuning interval. The manager's start skips this phase (the strength stays 0.1) |
| 2 quake | the two amplitude scales are two curves at the strength; two `Quaker` effects; sounds. Each tick the fault advances by time passed x the tuning's speed, in whole steps of 8: per step the next point of each half (until it leaves the city) and, per half, a terrain request (the city's slot 0x150 object, slot 0xE4, kinds 0x11 and 0x12) from the previous point to this one, with two amplitudes: two more curves at the distance walked, times the scales. The quake is over when both halves have left the city or an amplitude falls below 0.001 (first) or 0.002 (second) |
| 3 after | 3,000,000 units of time later the instance ends: the layer is told, the instance leaves the manager's list and shuts down (news items withdrawn, effects released) |

The tick is message 0x86AD10EE with the time passed (the same service as the volcano's, on frame time).

### Effect

A disaster that is only a visual effect a tool started (the caller is a view tool at 0x005803D0). The manager has no start by cell; its effect part (interface 0xCA7B3F3C at +0x24) takes an effect, a position and a disaster type, makes an instance (0x2C bytes) around the effect, and can move or stop the first instance. The instance tells the effect to report to it (message 0x2A7B582D when it is over, 0x2A7B582E with a new cell), starts it, puts itself on the manager's list and tells the layer a disaster began; its location is the position's cell, registered with the layer. When the effect reports it is over the instance shuts down: the effect is stopped if it still runs, the layer is told and the location withdrawn.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.disasters.layer` | every layer method but GetDisasterCost (4 bytes): 27 functions |
| `simulators.disasters.managers` | the manager base: QueryInterface, Init, Shutdown, the instance list, the active count, the message forwarding |
| `simulators.disasters.toxic` | the toxic manager and instance |
| `simulators.disasters.volcano` | the volcano manager and instance (24 functions); the terrain's volcano builder stays the game's |
| `simulators.disasters.riot` | the riot manager and instance (18 functions) but the instance's Read |
| `simulators.disasters.pipeburst` | the pipe burst manager, its pipe filter and the instance (21 functions) |
| `simulators.disasters.earthquake` | the earthquake manager's start and the instance (20 functions) |
| `simulators.disasters.fire` | the fire manager and instance (46 functions); the fire occupant stays the game's |
| `simulators.disasters.effect` | the effect manager's QueryInterface and effect part, and the instance (13 functions; its QueryInterface is also the earthquake instance's) |

Still the game's: the objects (constructors, destructors), the riot instance's Read, the fire occupant, container and stopwatch helpers.

## Test cities

No test city has a disaster. `OPENSC4_TEST_DISASTER=<day>` with `OPENSC4_TEST_DISASTER_TYPE=<n>` (0 volcano, 1 fire, 2 earthquake, 3 pipe burst, 4 riot, 5 effect, 6 toxic) starts one through its manager on that day of a run, at the first cell that takes it, walking out from the centre. The camera jumping to the disaster makes the traffic record's view-dependent fields vary run to run with the original (on Big City d0000 one field took the values 10, 11, 12, 13 and 19 in the original's own runs), so the switch turns the "go to disasters" option off unless `OPENSC4_TEST_DISASTER_CAMERA=1`. `OPENSC4_TEST_DISASTER_END=<day>` shuts the chosen type's instances down on that day (a toxic spill lasts two minutes of real time, longer than a week's run), so a disaster's end also runs. `OPENSC4_TEST_PIPE_FUSE=<ms>` sets the pipe burst's fuses after it starts, so pipes explode and the bursts spread within the run; where the walk finds no accepted pipe the switch falls back to the manager's own choice (slot 7). `OPENSC4_TEST_RIOT_PUSH=<n>` adds n to every rioter's time each day and runs the riot's tick with its stopwatch gate open: under the harness's clock the riot's own tick comes about once a week, so without the push no rioter changes level.

`OPENSC4_TEST_QUAKE_PUSH=<n>` takes each earthquake off the tick service and runs its tick once every run day with n as the time passed (the quake's own tick comes on frame time, so unpushed runs differ in how far the fault got). For the effect disaster (type 5), which has no start by cell, the switch does what the game's tool does: it makes an effect (`OPENSC4_TEST_DISASTER_EFFECT`, default `QuakeStart`), gives it to the manager's effect part at the city's centre and then moves it 8 cells through the part's SetLocation. `OPENSC4_TEST_FIRE_CALLS=<day>` calls the fire manager's fire part as tools and other simulators do: an area burned around the centre (inner radius 1, outer 4), the first three fires doused by 3, one cell burned down, a forced start, and the owner and count queries (their results are logged). `OPENSC4_TEST_FIRE_PUSH=<n>` runs the fire instance's update n times every run day with its next-step time cleared, so every call steps every fire: on its own the step comes about once a week under the harness's clock. `OPENSC4_TEST_TUTORIAL_OFF=1` clears the tutorial system's flag (+0x1F of its interface, what IsTutorial returns) on the first run day: every test city is a tutorial city, and while a tutorial is on a fire past its last stage neither burns its occupant down nor spreads.

The fingerprint's `dis` part: the disable count, disasters under way, the cost's building count and total, the locations (count and a hash of their cells and zooms) and each manager's instance count (in type order: earthquake, pipe burst, fire, effect, volcano, riot, toxic), then `riot`: the rioters, and their times and levels added up; `fires` (with a fire instance): the fires, their heats and stages added up, the burned queue, the count destroyed, the groups and a hash of the instance's random generator (so every draw the fires made shows); and per earthquake `quake`: the phase, a hash of the fault (strength, epicentre, direction, control points), a hash of its progress and the number of news items.

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

With the volcano on too (`disv1-*`): a volcano makes every run its own outcome, the original's as much as ours (three outcomes from three runs on every save; the terrain grows on frame time, and what it destroys follows), so whole-city verdicts say nothing. What can be compared is the fingerprint's `dis` part, day by day, across all six runs of a save:

| Leg | The `dis` part |
| --- | --- |
| volcano on day 1 | the same in all six runs on 8 saves; on the four Big City saves it differs among the original's own runs too (the cost of what fell) |
| volcano, ended on day 3 | the same in all six runs on 9 saves; on two the original's own runs differ; on Big City d0090 one of ours counted 63 buildings destroyed where the other five runs counted 60 |

So the state machine's course (two disasters counted while the effect and the building overlap, three locations then two, the end) matches wherever the original repeats it, and no run crashed in about 70 with the volcano on. That is all this harness can show for the volcano.

With the riot on too (`disr1-*`, `disr3-push`):

| Leg | Same | Rest |
| --- | --- | --- |
| no disaster | 11 | Big City d0000: ours differed from the original in the traffic record's view-dependent field only (10 against 19, 12, 19), fingerprints identical; the original takes those values too in other batches |
| riot on day 1 | 11 | 1 partly |
| riot, ended on day 3 | 8 | 3 partly, 1 failed |
| riot pushed by 40 a day (`OPENSC4_TEST_RIOT_PUSH`) | 11 | 1 partly (Big City d0000) |

Unpushed, a riot is one rioter whose time goes from 1 to 2 in a week. Pushed, the rioters go from 2 to 5 as levels are reached, their locations are registered and one leaves, the same on both sides day by day. The riot's Write runs at the end of every such run (the saves match). The failed run was a B run that crashed in the game's code at 0x005A62A2 while Making Money d1095 loaded, an address [parity.md](parity.md) already has from an A run.

With the earthquake on too (`dq1-*`, `dq2-push`):

| Leg | Same | Rest |
| --- | --- | --- |
| no disaster | 12 | |
| earthquake on day 1 | 9 | 1 partly, 2 where ours shared no outcome with the original, whose own three runs gave three and two outcomes; the `dis` histories that occur (the cost by day) are the same few on both sides |
| earthquake, ended on day 3 | 10 | 2 partly |
| earthquake pushed by 2,000,000 a day (`OPENSC4_TEST_QUAKE_PUSH`) | 8 | on the four Big City saves every run was its own outcome, the original's too, but the `dis` part was the same in all six runs |

Unpushed, the fault advances on frame time and the quake is not over within the week. Pushed, it runs its course in three days on both sides, the same day by day in every run of every save: the fault's hash and its 19 news items, the progress on days 1 and 2, the after phase on day 3, then the end (the location withdrawn, the layer's count back to 0), and on Big City the cost (10 buildings and 1220, then 15 and 1810). On the eight Making Money and Rush Hour saves the whole city matched for the week, saves included, with the terrain shaken on both sides. The rumble phase is not reached: the manager's start skips it and nothing else in the game starts an earthquake.

With the effect disaster on too (`dq2-fx`, `dq2-fxe`): 12 of 12 with an effect disaster started on day 1 (one instance, two locations: the start and the move), and 12 of 12 with it ended on day 3. The effect's own end message (0x2A7B582D) did not come within the week with the `QuakeStart` effect, so that path and the move message (0x2A7B582E) are unreached.

With the fire on too (`df1-*` to `df6-*`), every leg with a fire started on day 1:

| Leg | Same | Rest |
| --- | --- | --- |
| fire | 11 | 1 partly (Big City d0365: the original gave one outcome, ours two, one of them the original's) |
| fire calls on day 2 | 12 | |
| fire calls, ended on day 4 | 12 | |
| pushed by 20 a day | 12 | |
| pushed by 20, fire calls | 12 | |
| pushed by 300 | 12 | |
| pushed by 300, fire calls | 12 | |
| tutorial off, pushed by 100 | 12 | |
| tutorial off, pushed by 100, fire calls | 12 | |
| tutorial off, pushed by 10 | 12 | |
| tutorial off, pushed by 10, fire calls | 12 | |

Unpushed, a fire makes one step in the week. With the tutorial on, a pushed fire's heat rises by 1 a step and stops at its last stage; the area call sets up to 13 more fires alight in two more groups, the douse takes heat off, and the burned queue empties five an update (15, then 10 queued; 5, 10, 20 destroyed), with the cost counted the same on both sides. With the tutorial off the fires run their course: at 10 steps a day the first fire changes stage on day 3 and burns its occupant down on day 6, the groups end one by one and the disaster ends. The instance's random generator, drawn from on every step for the spread, has the same state on both sides at the end of every day in every save. Not seen: a spread that starts a new fire (the draws are made, but no day ended with more fires than the calls had started), the two view messages' effect on a fire occupant, and Ignite.
