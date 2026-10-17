# Automata: the cars, trains, pedestrians, aircraft and boats on screen

The automata are the moving things of a city: cars, buses, trains, emergency vehicles, pedestrians, aircraft and boats, the managers that make and remove them, the controllers that attract and generate them, the script system that tells them what to do, and the city situation manager behind emergencies. They ask the traffic network map ([traffic-map.md](traffic-map.md)) where they can go and move by frames, not by game days. Build 641 addresses throughout (data/addresses/automata.toml).

This page is the survey of the whole family and the record of the parts done so far. Part a1, the automata controller manager's bookkeeping and save, is the first.

## The family

All 30 classes are registered by one function, 0x00514A00, with factories 0x005145A0 to 0x005149F0:

| Kind | Classes (class ID) |
| --- | --- |
| Managers | cSC4VehicleManager (0xA98F9D3B), cSC4PedestrianManager (0xC98F9D17), cSC4AircraftManager (0xC98F9D4A), cSC4WatercraftManager (0xA98F9D28) |
| Road vehicles | cSC4Vehicle (0x896E714A), cSC4TrailerVehicle (0x4A5B69ED), cSC4Bus (0xAA9F4A3D), cSC4GetawayVan (0xEA863423), cSC4CarjackedVehicle (0xCBB32B0A), cSC4Tank (0xEBE0DCAD), cSC4MayorLimo (0x4C0DD224) |
| Emergency vehicles | cSC4FireTruck (0xAA222B83), cSC4PatrolCar (0x4A222B98), cSC4Ambulance (0x6C1ACDE1) |
| Trains | cSC4Train (0x8A41625A), cSC4TrainCar (0x4A416254), cSC4MassTransitTrain (0x4C22AE53) |
| Pedestrians | cSC4Pedestrian (0x896E75AF), cSC4PedestrianOneShot (0xAA09DC28), cSC4SimFireCrew (0x4A222E4D), cSC4RiotPolice (0x8A6E4FA4) |
| Aircraft and boats | cSC4Aircraft (0xC96E75DC), cSC4Helicopter (0x8BA98EDA), cSC4Airplane (0xCBEB4F62), cSC4Watercraft (0x096E75F5) |
| Controllers | cSC4AutomataControllerManager (0xCA14CA03), cSC4AutomataAttractor (0x6A1628BA), cSC4AutomataGenerator (0x4A1628C5) |
| Scripts and situations | cSC4AutomataScriptSystem (0x4A1B664E), cSC4CitySituationManager (0x0BB14381) |

The road vehicles and the pedestrians share an automaton base (vtable 0x00A9C068); road vehicles, trains and emergency vehicles a vehicle base (0x00A9D488); fire truck, patrol car and ambulance an emergency base (0x00A981E0); aircraft and boats a base of their own (0x00A95AC0); the vehicle and pedestrian managers a manager base (0x00A9BD88); the aircraft and watercraft managers another (0x00A95880); attractor and generator a controller base (0x00A95D90). The automata are occupants (types 0xC7CC75F1 and 0x496E5783), which is why the occupant manager's fingerprint leaves them out ([occmgr.md](occmgr.md)).

## The code and how it splits

The code runs from 0x004FA700 to 0x00575600. In that range are 1,898 functions (Ghidra's list); 56 of them are the U-Drive control (0x00562C90 to 0x00566D80, the tools pieces T7 and T8, [tools.md](tools.md)) and 15 at the start belong to other code, which leaves about 1,830 functions of the automata. The counts below put each function with the class whose vtables hold it, and helpers with the class that calls them. Where a helper has callers in several classes, or none, the nearest neighbours decide. So they are close, not exact (about one in twelve was placed by its neighbours):

| Group | Functions | Bytes | Proposed parts |
| --- | ---: | ---: | --- |
| Registration and factories (0x005145A0 to 0x00514C30) | 33 | 1,551 | 1 |
| Controller manager | 76 | 15,809 | a1 (45, done), a2 (24 queries and message handlers) |
| Attractor, generator and their base | 81 | 12,629 | 2 |
| Script system | 116 | 31,620 | 3 |
| City situation manager | 167 | 44,037 | 4 |
| Vehicle and pedestrian managers and their base | 96 | 20,337 | 3 |
| Aircraft and watercraft managers and their base | 222 | 56,293 | 6 |
| Automaton and vehicle bases, cSC4Vehicle | 318 | 70,504 | 8 |
| Other road vehicles (trailer, bus, van, carjacked, tank, limo) | 86 | 17,836 | 2 |
| Trains | 79 | 18,124 | 2 |
| Emergency vehicles | 101 | 19,149 | 3 |
| Pedestrians, one-shot, fire crew, riot police | 114 | 20,304 | 3 |
| Aircraft, helicopter, airplane | 188 | 79,376 | 5 |
| Watercraft | 75 | 37,045 | 2 |
| Path info (0x0053CF90 to 0x00542F00, cSC4PathInfoFactory) | 75 | 23,825 | 2 |

That is about 48 parts of 20 to 45 functions. What the parts after a1 will meet:

- **Frames.** The managers, the automata themselves, the attractors' automaton lists and the path following run on frame time, like the traffic network map. The original does not repeat them run to run, so the usual 7-day comparison will not hold for those parts; they need the comparison inside one run ([traffic-map.md](traffic-map.md), "The original does not repeat this class") or deterministic frames in the harness.
- **Drawing.** Model and animation calls inside the automata belong to the renderer and stay the game's.
- **Mods.** No NAM site is in the range. The only code patch in it is the Lua `print` replacement at 0x0051C50B, inside the script system's init ([patch-census.md](patch-census.md)): that function stays the game's. The UI scaling mod SC4UIScale has three probe hooks (0x004FBFE0, 0x00505370, 0x00528580, off by default) and a message popup style patch (0x0052CCEE, 0x0052CD01) in the aircraft manager, the road vehicles and the city situation manager; those parts must check them the way foreign_sites.cpp checks NAM's.
- **Out of scope here:** the automata controller prop (class 0x2A4017A0) is one of the derived props ([propkinds.md](propkinds.md)); MySim's agents are done ([mysim-agents.md](mysim-agents.md)).

## The controller manager

cSC4AutomataControllerManager (factory 0x00514840, constructor 0x0051AA50, 0x80 bytes, city slot 47 at city +0x1E0) keeps the city's automata controllers: attractors and generators made from templates of the script system for occupants, by the occupant's groups, moved and removed with them. It is saved with the city.

Its parts: +0 the manager interface (vtable 0x00A96058, 25 slots; the 26th word is the next vtable), +4 a cheat code target (0x00A96048), +8 a message target (0x00A96038), +0xC the serializable part (0x00A96020: Write 0x005174D0, Read 0x0051B520, GetGZCLSID 0x0051A300). Each of the gzcom-dll header's overloaded pairs sits in the vtable in the other order.

Fields ([automata_ctlmgr.h](../src/core/automata_ctlmgr.h)): +0x10 initialized, +0x11 loaded, +0x14 reference count; +0x18 the 24-hour clock (city slot 86), saved; +0x1C a grid of controller lists (a vector of list heads, the side in cells of 8 city cells, the shift, the size in metres); +0x34 to +0x43 the controller time, saved; +0x44 the script system (city slot 48); +0x58 the controller table (per occupant the list of its controllers); +0x6C the automaton table (per occupant the automata its controllers keep, written by the attractor code on frames).

## Part a1: bookkeeping and save

39 functions are ours, in two groups, off by default:

- `simulators.automata.ctlmgr` (35): the object (constructor, destructor, QueryInterface, Release), Init and Shutdown, the grid (set-up, cell range of a position and radius, the vector helpers it is built with), adding controllers (from a template, by template name, by occupant group ID or name, all of an occupant's), removing them (one, by name, by group, all of an occupant's), moving them, clearing at exit, and the template base's destructors.
- `simulators.automata.ctlsave` (4): Write, Read, GetGZCLSID and the automaton table insert Read uses.

Kept as the game's: the six `sub ecx,N; jmp` adjustor thunks (they land on our detours), AddRef 0x00695330 and slot 24 0x0051A310 (code folded with other classes), the value copy 0x00518A40 (shared with the attractor code), and part a2: the queries (GetController, GetRelevantControllersByWorldPos, the composite attraction effect and commotion), the two message handlers 0x005187F0 and 0x00519360, and the grid add, remove and move helpers they share with a1 (0x00517A90, 0x005171D0, 0x00517B70, 0x005163D0). No caller of a1 relies on a register the game's function leaves set; none needed a register thunk.

Kept as the game does it: QueryInterface with a null object writes null and calls through it; RemoveControllerByName (slot 8) never sets its removed flag and always answers 0; the cell loops of AddControllerFromTemplate go on after a find; the error texts keep the game's order of frees. One difference: where Read starts its counts and a flag byte from stack leftovers when a stream read fails at once, ours starts from 0 (the leftovers are never read on a good stream).

Floats: only the grid's cell range does float work, with the game's rounding by adding 1.5 x 2^23 (0x00A95D58); its fourth bound stays a double, as on the game's x87 stack. A NaN in the position and the radius at once could give another NaN payload; nothing else differs.

Reimplemented like the network parts: one writer with every call's argument count checked against the callee's `ret N`, then an independent audit of all 45 functions of the list against the disassembly (no translation defect found; it took the cells of automaton occupants out of the test's hash).

## Test

`OPENSC4_TEST_AUTOMATA=1` adds ` | auto n<occupants>/<controllers> c<sum> g<side>,<cells with lists>,<entries>,<sum>` to every day's fingerprint: the controller table and the grid, per occupant by type, cells and position (automata by type only: their cells and positions follow frames), per controller by kind and template (class ID, flags, radius, name), in sums so that the table's order does not count. The controller time (+0x34 to +0x43) and the automaton table go to the log only (`autotest: automata: ...`): both follow frames, and the first batch showed the time differing between two runs of the original from the fourth day on. `=2` also runs the game-address Write on day `OPENSC4_TEST_AUTOMATA_DAY` (default 3) into the test's own stream and adds ` w<answer>,<bytes>,<hash>`, the hash over the version, the occupant count and the per-occupant controller counts (the time bytes and the automaton table records are left out of it). A remove and add round trip was left out: removing sends 0xEA298D50 to the automata and releases them, so the city would not be the same after.

Reached in the trial (Rush Hour t0000, 7 days): 24 of the 39, among them Construct, Init, the grid, Read, Write, AddControllers, MoveController, RemoveControllers and the exit path. Not reached unattended: adding by template name and the by-name group variants (scripts only), RemoveController by controller, RemoveControllerByName, the error texts, the grid helpers' in-place branches (the grid is sized once), duplicate keys in Read.

## Parity

Batch b1 (snapshot net-a1-b1, env p4env-net-a1, B list = the network groups of part w2 plus both a1 groups, 127 groups; 7 days): plain 12 of 12 and saves-networks 3 of 3 identical, all on the first pair; with `OPENSC4_TEST_AUTOMATA=2` on saves-networks 3 of 3. On the 12 saves with the test, 6 differed because the original itself gave two outcomes there: the controller time was still in the token, and it follows frames. With it moved to the log (batch b2) the test leg is 12 of 12 identical, first pair.
