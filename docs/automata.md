# Automata: the cars, trains, pedestrians, aircraft and boats on screen

The automata are the moving things of a city: cars, buses, trains, emergency vehicles, pedestrians, aircraft and boats, the managers that make and remove them, the controllers that attract and generate them, the script system that tells them what to do, and the city situation manager behind emergencies. They ask the traffic network map ([traffic-map.md](traffic-map.md)) where they can go and move by frames, not by game days. Build 641 addresses throughout (data/addresses/automata.toml).

This page is the survey of the whole family and the record of the parts done so far. Parts a1 and a2 make the automata controller manager ours.

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

Every automaton class derives from an automata core (vtable 0x00A95AC0, constructor 0x005140A0; part vb below); the road vehicles and the pedestrians from an automaton base on it (vtable 0x00A9C068); the other road vehicles, trains and emergency vehicles from cSC4Vehicle (0x00A9D488); fire truck, patrol car and ambulance from an emergency base (0x00A981E0); aircraft and boats from the core directly; the vehicle and pedestrian managers a manager base (0x00A9BD88), which derives from the base all four managers share (0x00A95880, constructor 0x005125E0; part v1 below); attractor and generator a controller base (0x00A95D90). The automata are occupants (types 0xC7CC75F1 and 0x496E5783), which is why the occupant manager's fingerprint leaves them out ([occmgr.md](occmgr.md)).

## The code and how it splits

The code runs from 0x004FA700 to 0x00575600. In that range are 1,898 functions (Ghidra's list); 56 of them are the U-Drive control (0x00562C90 to 0x00566D80, the tools pieces T7 and T8, [tools.md](tools.md)) and 15 at the start belong to other code, which leaves about 1,830 functions of the automata. The counts below put each function with the class whose vtables hold it, and helpers with the class that calls them. Where a helper has callers in several classes, or none, the nearest neighbours decide. So they are close, not exact (about one in twelve was placed by its neighbours):

| Group | Functions | Bytes | Proposed parts |
| --- | ---: | ---: | --- |
| Registration and factories (0x005145A0 to 0x00514C30) | 33 | 1,551 | 1 |
| Controller manager | 76 | 15,809 | a1 (45, done), a2 (24, done) |
| Attractor, generator and their base | 81 | 12,629 | 2 |
| Script system | 116 | 31,620 | s1 (34, done), s2, s3 (part s below counts 125) |
| City situation manager | 167 | 44,037 | c1 (41, done), c2 to c5 (part c below counts 164) |
| Vehicle and pedestrian managers and their base | 96 | 20,337 | v1 (32, done), v2 (25, done), v3 (29, done) |
| Aircraft and watercraft managers and their base | 222 | 56,293 | aw1 (36, done), aw2 (25, done), aw3 to aw5 (part aw below counts 172) |
| Automaton and vehicle bases, cSC4Vehicle | 318 | 70,504 | vb1 (36, done), vb2 (30, done), vb3 (13, done), vb4 (22, done), vb5 (10, done), vb6 (12, done), vb7 (14, done), vb8 (1, done) |
| Other road vehicles (trailer, bus, van, carjacked, tank, limo) | 86 | 17,836 | ro1 (44, done), ro2 (15), ro3 (4) (part ro below counts 103) |
| Trains | 79 | 18,124 | 2 |
| Emergency vehicles | 101 | 19,149 | 3 |
| Pedestrians, one-shot, fire crew, riot police | 114 | 20,304 | 3 |
| Aircraft, helicopter, airplane | 188 | 79,376 | 5 |
| Watercraft | 75 | 37,045 | 2 |
| Path info (0x0053CF90 to 0x00542F00, cSC4PathInfoFactory) | 75 | 23,825 | 2 |

That is about 48 parts of 20 to 45 functions. What the parts after a1 will meet:

- **Frames.** The managers, the automata themselves, the attractors' automaton lists and the path following run on frame time, like the traffic network map. The original does not repeat them run to run, so the usual 7-day comparison will not hold for those parts; they need the comparison inside one run ([traffic-map.md](traffic-map.md), "The original does not repeat this class") or deterministic frames in the harness.
- **Drawing.** Model and animation calls inside the automata belong to the renderer and stay the game's.
- **Mods.** No NAM site is in the range. The only code patch in it is the Lua `print` replacement at 0x0051C50B, inside the script system's loading of the scripts (0x0051C4C0, which Init reaches through 0x00523B10; [patch-census.md](patch-census.md)): that function stays the game's. The UI scaling mod SC4UIScale has three probe hooks (0x004FBFE0, 0x00505370, 0x00528580, off by default) and a message popup style patch (0x0052CCEE, 0x0052CD01) in the aircraft manager, the road vehicles and the city situation manager; those parts must check them the way foreign_sites.cpp checks NAM's.
- **Out of scope here:** the automata controller prop (class 0x2A4017A0) is one of the derived props ([propkinds.md](propkinds.md)); MySim's agents are done ([mysim-agents.md](mysim-agents.md)).

## The controller manager

cSC4AutomataControllerManager (factory 0x00514840, constructor 0x0051AA50, 0x80 bytes, city slot 47 at city +0x1E0) keeps the city's automata controllers: attractors and generators made from templates of the script system for occupants, by the occupant's groups, moved and removed with them. It is saved with the city.

Its parts: +0 the manager interface (vtable 0x00A96058, 25 slots; the 26th word is the next vtable), +4 a cheat code target (0x00A96048), +8 a message target (0x00A96038), +0xC the serializable part (0x00A96020: Write 0x005174D0, Read 0x0051B520, GetGZCLSID 0x0051A300). Each of the gzcom-dll header's overloaded pairs sits in the vtable in the other order.

Fields ([automata_ctlmgr.h](../src/core/automata_ctlmgr.h)): +0x10 initialized, +0x11 loaded, +0x14 reference count; +0x18 the 24-hour clock (city slot 86), saved; +0x1C a grid of controller lists (a vector of list heads, the side in cells of 8 city cells, the shift, the size in metres); +0x34 to +0x43 the controller time, saved; +0x44 the script system (city slot 48); +0x58 the controller table (per occupant the list of its controllers); +0x6C the automaton table (per occupant the automata its controllers keep, written by the attractor code on frames).

## Part a1: bookkeeping and save

39 functions are ours, in two groups, off by default:

- `simulators.automata.ctlmgr` (35): the object (constructor, destructor, QueryInterface, Release), Init and Shutdown, the grid (set-up, cell range of a position and radius, the vector helpers it is built with), adding controllers (from a template, by template name, by occupant group ID or name, all of an occupant's), removing them (one, by name, by group, all of an occupant's), moving them, clearing at exit, and the template base's destructors.
- `simulators.automata.ctlsave` (4): Write, Read, GetGZCLSID and the automaton table insert Read uses.

Kept as the game's: the six `sub ecx,N; jmp` adjustor thunks (they land on our detours), AddRef 0x00695330 and slot 24 0x0051A310 (code folded with other classes), the value copy 0x00518A40 (shared with the attractor code), and the 24 functions of part a2 (below), which a1's code calls at their game addresses. No caller of a1 relies on a register the game's function leaves set; none needed a register thunk.

Kept as the game does it: QueryInterface with a null object writes null and calls through it; RemoveControllerByName (slot 8) never sets its removed flag and always answers 0; the cell loops of AddControllerFromTemplate go on after a find; the error texts keep the game's order of frees. One difference: where Read starts its counts and a flag byte from stack leftovers when a stream read fails at once, ours starts from 0 (the leftovers are never read on a good stream).

Floats: only the grid's cell range does float work, with the game's rounding by adding 1.5 x 2^23 (0x00A95D58); its fourth bound stays a double, as on the game's x87 stack. A NaN in the position and the radius at once could give another NaN payload; nothing else differs.

Reimplemented like the network parts: one writer with every call's argument count checked against the callee's `ret N`, then an independent audit of all 45 functions of the list against the disassembly (no translation defect found; it took the cells of automaton occupants out of the test's hash).

## Test

`OPENSC4_TEST_AUTOMATA=1` adds ` | auto n<occupants>/<controllers> c<sum> g<side>,<cells with lists>,<entries>,<sum>` to every day's fingerprint: the controller table and the grid, per occupant by type, cells and position (automata by type only: their cells and positions follow frames), per controller by kind and template (class ID, flags, radius, name), in sums so that the table's order does not count. The controller time (+0x34 to +0x43) and the automaton table go to the log only (`autotest: automata: ...`): both follow frames, and the first batch showed the time differing between two runs of the original from the fourth day on. `=2` also runs the game-address Write on day `OPENSC4_TEST_AUTOMATA_DAY` (default 3) into the test's own stream and adds ` w<answer>,<bytes>,<hash>`, the hash over the version, the occupant count and the per-occupant controller counts (the time bytes and the automaton table records are left out of it). A remove and add round trip was left out: removing sends 0xEA298D50 to the automata and releases them, so the city would not be the same after.

Reached in the trial (Rush Hour t0000, 7 days): 24 of the 39, among them Construct, Init, the grid, Read, Write, AddControllers, MoveController, RemoveControllers and the exit path. Not reached unattended: adding by template name and the by-name group variants (scripts only), RemoveController by controller, RemoveControllerByName, the error texts, the grid helpers' in-place branches (the grid is sized once), duplicate keys in Read.

## Parity

Batch b1 (snapshot net-a1-b1, env p4env-net-a1, B list = the network groups of part w2 plus both a1 groups, 127 groups; 7 days): plain 12 of 12 and saves-networks 3 of 3 identical, all on the first pair; with `OPENSC4_TEST_AUTOMATA=2` on saves-networks 3 of 3. On the 12 saves with the test, 6 differed because the original itself gave two outcomes there: the controller time was still in the token, and it follows frames. With it moved to the log (batch b2) the test leg is 12 of 12 identical, first pair.

## Part a2: queries, grid and messages

The other 24 functions of the class are ours, in two more groups, off by default, in [automata_ctlquery.cpp](../src/core/automata_ctlquery.cpp):

- `simulators.automata.ctlquery` (15): the queries GetControllerNear (slot 17, a position as three floats, a name and a radius), GetControllerByTemplate (18), GetControllerByName (19), GetRelevantControllersByWorldPos (20), ReleaseControllerList (21), GetCompositeAttractionEffect (22) and GetCompositeAttractionCommotion (23, which answers a float in ST0, not a controller as the gzcom-dll header has it); the grid's add 0x00517A90, remove 0x005171D0 and move 0x00517B70 with their helpers (a controller's cell range 0x00515290, the cell at a point 0x00515660); AddControllersFromGroup 0x005163D0; and two controller methods only the composite queries call (the attraction strength 0x00515180, IsRelevantTo 0x00515DA0).
- `simulators.automata.ctlmsg` (9): the cheat code target 0x005187F0 (`Controllers off` / `on` clears or sets +0x55, which stops the queries and the update), the message handler 0x00519360 (a chain of compares, no jump table: occupant inserted and removed, after a load, the frame message 0x86AD10EE, the hour, automaton events, camera and view messages, the occupant group event), and its helpers: the frame update 0x00518B80, the templates after a load 0x00518660, the flagged moves 0x00518950, the move notification 0x005177F0, and the controller time (hour and weekday) 0x00514FC0, 0x00515100, 0x00515D50.

None of the 24 has callers outside the class, so nothing on the list stayed the game's. The handler also calls tControllerTime::AddHours 0x00515080, which is not on the list and which the generator controller shares (0x00515200, 0x00515E60), so it stays the game's. No caller relies on a register; no register thunks.

Kept as the game does it: GridAdd, GridMove and the cell lookup index a cell (z << shift) + x, while a1's radius test (the game's 0x0051AD20) uses (x << shift) + z; the grid is square, so that test only looks at the mirrored cells. Also kept: ResolveTemplates loops for ever on a null controller in a list; GridAdd and GridMove loop for ever if a cell range reaches 255, because their counters are bytes; the composite effect writes its unit vector over the stack vector that the next controller's calls start from (the audit found that ours had first kept it apart). The clock-seeded draw of the group event (0xEBFF18A3) is made under `fixed_seed::SiteOverride` at the game's return address. Stack leftovers that are never read decide nothing, so ours starts them at 0: the upper two bytes of the time pairs that Update and the hour message hand on, the two vectors the cell range hands to the controller's slot 21 (which fills them before it reads them), and the composite queries' value slot (read only when slot 23 has written it).

Floats: 53-bit doubles, every compare with the asm's handling of unordered values, a1's cell rounding. Values the game answers in ST0 (the attraction strength, the curve, the distance, the generator's random float) are taken as doubles, unrounded. The composite effect adds its x and y increments unrounded and rounds z to a float first, as the asm does.

Reimplemented like a1: one writer, every call's argument count checked against the callee's `ret N`, then an independent audit of all 24 against the disassembly (one defect, the unit vector above, fixed before any batch).

### Test

`OPENSC4_TEST_AUTOMATA=2` now also queries on the test day, at the game's addresses (so the same test runs on both sides): up to 16 (occupant, controller) pairs, automata and props left out, sorted by values; for each, GetController by template, by name, with no template, and near the occupant's position (radius 64). The answers, by kind and template, go into the token as ` q<pairs>,<sum>`. GetRelevantControllersByWorldPos (released through ReleaseControllerList), the composite effect and the commotion read frame-driven state (the controller flags, the controller time, the attractors' automata), so they go to the log only (`autotest: automata: query ...`). The test adds, removes or moves nothing and sends no message.

Reached in the trial (Rush Hour t0000, 7 days, identical): 18 of the 24; over batch b1's 30 B runs, 23 of the 24 (the commotion in one run only, on saves-networks). Not reached unattended: the cheat target 0x005187F0 (the `Controllers off` / `on` cheat).

### Parity

Batch b1 (snapshot net-a2-b1, env p4env-net-a2, B list = a1's plus both a2 groups; 7 days): plain 12 of 12, saves-networks 3 of 3, with `OPENSC4_TEST_AUTOMATA=2` 12 of 12 and 3 of 3, all identical on the first pair.

## Part a3: the attractor, generator and their base

The next part of the survey's list: 81 functions, 12,629 bytes, two proposed sub-parts. Nothing is ours yet; this section is the survey the work rests on. Build 641 addresses throughout.

The two controllers the manager (a1, a2) holds are `cSC4AutomataAttractor` (class ID `0x6A1628BA`, factory `0x00514860`, constructor `0x005181F0`, `0x6C` bytes) and `cSC4AutomataGenerator` (class ID `0x4A1628C5`, factory `0x00514880`, constructor `0x005182B0`, `0xA0` bytes). Both derive from the base controller `cSC4AutomataController` (no class ID of its own; the gzcom-dll header's `cISC4AutomataController`, interface `0x6A1857F8`).

| Part | Attractor | Generator | Base |
| --- | --- | --- | --- |
| +0x00 vtable | `0x00A95E48`, 42 slots | `0x00A95F00`, 42 slots | `0x00A95D90`, 42 slots |
| +0x04 serializable vtable | `0x00A95E30`, 4 slots | `0x00A95EE8`, 4 slots | `0x00A95D74`, 4 slots |

The base controller's constructor `0x00517D40` fixes the layout, shared by both derived classes:

| Offset | Field |
| --- | --- |
| +0x00 | the controller vtable |
| +0x04 | the serializable interface (its own vtable) |
| +0x08 | initialized byte |
| +0x0C | occupant (referenced) |
| +0x10 | controller template (referenced) |
| +0x14 / +0x18 / +0x1C | position x, z, y (floats; the game's zero `0x00B227CC`) |
| +0x20 | reference count |
| +0x24 .. +0x27 | four bytes, zero |
| +0x28 | a base with the vtable `0x00A80810` (the saved curve's date range) |
| +0x2C / +0x30 / +0x34 | a byte vector (the name); 8 bytes from `SC4::stl_allocate` (`0x0090CF54`) |
| +0x40 | flags (2 from the constructor) |
| +0x44 / +0x48 | the list of automata: head and count |
| +0x4C | the automata script system (city slot 48, referenced) |
| +0x50 / +0x54 / +0x58 | floats: the saturation limit, an accumulator used by the tick, a third |
| +0x5C | attractor only: its `SC4Curve`, initialized by `0x007D0410` with `this = object + 0x5C` |
| +0x60 | generator only: its `cRZRandom`, seeded by `SC4Random::Construct` (`0x00913A5C`) with `this = object + 0x60` and seed `-1` |
| +0x78, +0x7C, +0x80, +0x84, +0x88, +0x8C, +0x90, +0x94, +0x98 | generator fields, zeroed by its constructor |

The base vtable's slots (base / attractor / generator; a blank cell means the base's slot is inherited):

| Slot | Base | Attractor | Generator | What it is |
| --- | --- | --- | --- | --- |
| 0 | `0x00514F60` | `0x005151F0` (thunk) | `0x005151F0` (thunk) | QueryInterface (interface `0x6A1857F8`, else the serializable part at +4) |
| 1 | `0x008AE800` | | | AddRef (shared, `+0x20`) |
| 2 | `0x00517F10` | | | Release (`+0x20`, deleting destructor at slot 0x9C) |
| 3 | `0x00516850` | `0x00516FB0` | `0x00516FE0` | Init |
| 4 | `0x005169F0` | | | Shutdown |
| 5 | `0x005D4A10` (stub) | `0x00518220` | `0x00518390` | GetGZCLSID |
| 6 | `0x00517F70` | | | GetOccupant (`+0x0C`) |
| 7 | `0x00517DF0` | | | GetTemplate (`+0x10` + 4, else this + 0x28) |
| 8 | `0x0040CE60` (folded) | | | GetClassID or a shared getter |
| 9 | `0x00517E00` | | `0x00518310` | the template's curve value (+0x20, or +0xA8) |
| 10 | `0x00517E20` | | | active? (flags 1, 2, 4 and not 0x400) |
| 11 | `0x00515A40` | | | position (the occupant's, unless flag 0x20) |
| 12 | `0x00517E60` | | | TestFlag (`+0x40`) |
| 13 | `0x00517E70` | | | SetFlag (`+0x40`) |
| 14 | `0x00517F40` | | | SetOccupant (`+0x0C`) |
| 15 | `0x00517430` | | `0x00517460` | SetTemplate (`+0x10`) |
| 16 | `0x005D4A10` (stub) | `0x00518A00` | `0x00518A20` | the template's own set-up |
| 17 | `0x00602F90` | | | shared message helper |
| 18 | `0x00516AA0` | | `0x00517240` | SetActive and its bookkeeping |
| 19 | `0x00517EF0` | | | SetLimit (`+0x54`) |
| 20 | `0x00516EC0` | | | spot selection |
| 21 | `0x00515BD0` | | | cell bounds |
| 22 | `0x00516C40` | | | position from the occupant's box |
| 23 | `0x00515A80` | | | a cell test |
| 24 | `0x006F7DC0` | | `0x00515200` | the time passed (generator adds hours) |
| 25 | `0x00516B40` | | | the template's occupant lists |
| 26 | `0x00514E30` | | | reset the tick's accumulators |
| 27 | `0x00515870` | | | a frame/step handler |
| 28 | `0x0051BB60` | | | an automaton event (kind 4) |
| 29 | `0x0051BCA0` | `0x0051BFB0` | | an automaton event, second form |
| 30 | `0x0051BCE0` | `0x0051C030` | | an automaton event, third form |
| 31 | `0x0051BD50` | | | an automaton leaves |
| 32 | `0x0051BAB0` | | | pick a spot for an automaton |
| 33 | `0x00517ED0` | | | finished? (count `+0x48` against the template's `+0x54`) |
| 34 | `0x00477810` | | | shared |
| 35 | `0x00517EA0` | | `0x00518350` | saturated? (generator also tests `+0x8C` against `+0x88`) |
| 36 | `0x00648F00` | | | shared |
| 37 | `0x00518330` | | | calls slot 0x30 with flag 1 |
| 38 | `0x00514D90` | | `0x00515E60` | the tick |
| 39 | `0x00519340` | `0x005192C0` | | deleting destructor |
| 40 | `0x005183A0` (adjustor) | | `0x00518340` | QueryInterface adjustor / SetLimit (`+0x8C`) |
| 41 | `0x005183B0` (adjustor) | | `0x005198A0` | AddRef adjustor / the generator's spawn |

The +4 serializable vtable has six slots (part a3a corrected the survey's four): QueryInterface (base `0x00517F30`; the adjustor `0x005183A0` for both derived), AddRef adjustor `0x005183B0`, Release adjustor `0x005183C0`, Write (base `0x00517FD0`, attractor `0x00518230`, generator `0x005183D0`), Read, and GetGZCLSID (base `0x00517F00`; the derived classes reuse their slot 5, `0x00518220` and `0x00518390`).

What stays the game's: the shared AddRef `0x008AE800`; the folded helpers `0x0040CE60`, `0x00477810`, `0x00648F00`; the stub `0x005D4A10`; the shared message helper `0x00602F90`; the adjustor thunks `0x005183A0`, `0x005183B0`, `0x005183C0`; the helpers the two classes call at their addresses (`SC4::stl_allocate` `0x0090CF54`, `SC4::Free` `0x0090CF63`, `SC4PairList::Destroy` `0x005650A0`, `SC4Curve::Init` `0x007D0410`, `SC4Random::Construct` `0x00913A5C`, `cRZRandom::Between` `0x009136CA`, `SC4::operator_new` `0x005E55E0`, `SC4::operator_delete` `0x005E5620`); the templates (`0x0051AB80`, `0x0051B1F0` .. `0x0051B310`), which are the script system's; and the drawing in the spawn path.

### Proposed sub-parts

- **a3a: the object and its queries.** The base and derived constructors and destructors, the two factories; QueryInterface, Release, the GetGZCLSID pair, the accessors and flag tests, `IsActive`, `IsFinished`, `IsSaturated`, `GetPosition`, the curve value and the +4 serializable identity. Small, self-contained, and the foundation for the rest.
- **a3b: the behaviour.** Init and Shutdown, SetTemplate/SetActive and their bookkeeping, the automaton events, the tick, and the generator's spawn (`0x005198A0`, 2,222 bytes, the largest function of the part).

Tests and parity follow the family's method ([traffic-map.md](traffic-map.md)): the controllers run on frame time and the original does not repeat them run to run, so those parts compare inside one run rather than across the usual 7-day window.

## Part a3a: the object and its queries

32 functions are ours, in one group, off by default: `simulators.automata.controller` (src/core/automata_controller.cpp, the layout in src/core/automata_controller.h). The object: the base constructor 0x00517D40 with the layout above and the two derived constructors, the destructor bodies 0x00517F80 and 0x005192E0, the deleting destructors 0x005192C0 and 0x00519340, the two factories 0x00514860 and 0x00514880, QueryInterface 0x00514F60 and Release 0x00517F10. The queries: the GetGZCLSID pair, GetOccupant, GetTemplate, the two GetTemplateCurve, TestFlag, SetFlag, SetOccupant, the two SetTemplate, IsActive, GetPosition, the two IsSaturated, IsFinished and the generator's 0x00518330 and 0x00518340. Also CurveValue 0x00514C70 (not a slot: +0x28 times 1000 above the game's zero, else -1.0, answered unrounded in ST0) and the +4 serializable part's identity: its QueryInterface 0x00517F30 and the base GetGZCLSID 0x00517F00 (0x4A18672B).

Kept as the game's: the shared AddRef 0x008AE800; the folded helpers 0x0040CE60, 0x00477810, 0x00648F00 and the stub 0x005D4A10; the adjustor thunks 0x005183A0, 0x005183B0 and 0x005183C0 and the plain jump 0x005151F0 (they reach our detours at their entries); SC4PairList::Destroy, SC4Curve::Init, SC4Random::Construct, cRZRandom::Between and the allocators, all called at their addresses; and the save, a later part: Read and the three Write functions (0x00517FD0, 0x00518230, 0x005183D0), whose address entries are in place.

Oddities kept: the base constructor writes 0x00A80784 to +4 before the serializable vtable, as the game does; QueryInterface's null branches write a null and then call through the object (the address is held in a volatile local, as in the a1 manager, so the compiler keeps the call); the destructors free the name's buffer and the attractor's curve but do not run the string's or the curve's own destructor; the deleting destructors run Shutdown (slot 4) before the body; the generator's SetTemplate takes its limit and a fresh random only for a template whose class ID is its own; the generator's IsSaturated tests +0x8C against +0x88 unsigned after the two float tests fail.

GetOccupant 0x00517F70 is 4 bytes (`MOV EAX,[ECX+0xC]; RET`), too short for the 5-byte entry hook; its entry lists the two bytes of `CC` padding after it, so the patch stays inside the function. Its own `expect` in [data/addresses/automata.toml](../data/addresses/automata.toml) says so.

The `simulators.automata.controller` group has no fingerprint test of its own yet. It is reached and compared through the existing `OPENSC4_TEST_AUTOMATA` test, which drives the manager's queries (parts a1, a2) through these objects: the trial (Rush Hour t0000, 7 days) calls the constructors, both factories, QueryInterface, Release, the GetGZCLSID pair, both SetTemplate, Destroy and both deleting destructors; the pure read-only queries (GetOccupant, GetTemplate, the curves, TestFlag, IsActive, GetPosition, IsSaturated, IsFinished, the generator's 0x00518330 and 0x00518340) are reached by the manager's and the script system's paths in a longer run.

Parity (2026-10-17, `OPENSC4_TEST_AUTOMATA=2`, `simulators.automata.controller=ours` against the original, all the same): Making Money Tutorial d0000 1 day (2 of 2 common days) and 4 days (5 of 5), the 12-save 7-day batch (12 of 12), and Rush Hour t0000 7 days (8 of 8). CurveValue is reached (Making Money d0090, d0365); the serializable QueryInterface and GetGZCLSID are not reached by any run, since the harness does not save.


## Part a3b: the behaviour

19 functions are ours, in their own group, off by default: `simulators.automata.ctlbehaviour` (src/core/automata_controller.cpp, `kBehaviour`), so it can be enabled apart from a3a's `simulators.automata.controller`. They are Init (base 0x00516850, attractor 0x00516FB0, generator 0x00516FE0), Shutdown (base 0x005169F0, generator 0x00517000) and SetActive (base 0x00516AA0, generator 0x00517240). Also the base slots the runs reach: spot selection 0x00516EC0, cell bounds 0x00515BD0, the cell test 0x00515A80, the template's occupant lists 0x00516B40 and the frame/step handler 0x00515870. Then the automaton events: 28 0x0051BB60, the attractor's 29 0x0051BFB0, 30 0x0051BCE0, an automaton leaving 0x0051BD50 and picking a spot 0x0051BAB0. Last, the tick: base 0x00514D90 and generator 0x00515E60.

The layout's +0x14/+0x18/+0x1C is x, the height, z (a3a's x, z, y named the same offsets; `field::kHeight` and `field::kZ` now), as Init's terrain-height store shows.

Kept as the game's, because no parity run reaches them (checked with `diagnostics.coverage`), with address entries in place: the position from the box 0x00516C40, resetting the accumulators 0x00514E30, the base event 29 0x0051BCA0, the attractor's event 30 0x0051C030, SetLimit 0x00517EF0, the time-passed stub 0x006F7DC0, and the generator's spawn 0x005198A0. The spawn is the next piece of this part once a run reaches it: flag 4 (the frame/step handler's camera-view test) gates it, and the harness's runs never set it.

Review fixes (checked against the asm): the frame/step handler's five view points are the position, the low corner, high x with the low corner's height and z, the high corner, and low x with the high corner's height and z. Its flag's top three bytes are stack leftovers, but SetFlag 0x00517E70 tests only the low byte, so 0 or 1 is exact. The generator's tick returns before the interval reset when its clock pair is not reached, so the accumulator keeps counting. The first pass reset it on every tick, and parity did not catch that, since no run spawns. SC4StlList::Clear and PushBack take the list field's address, not its head.

Parity (2026-10-18, `OPENSC4_TEST_AUTOMATA=2`, both controller groups ours against the original, all the same): Making Money Tutorial d0000 1 day (2 of 2) and 4 days (5 of 5), the 12-save 7-day batch, and Rush Hour t0000 7 days (8 of 8). `ctlbehaviour` alone on Making Money d0000 1 day was also the same (2 of 2). The tick repeats run to run, so no in-run comparison was needed. In the batch, Making Money d0000 and d1095 each needed a second pair for the original's own first-run advisor outcome (docs/parity.md).

## Part a3c: the save and the rest of the behaviour

12 functions are ours, in two more groups, off by default (src/core/automata_controller.cpp, `kSerial` and `kSpawn`), so a3a's and a3b's groups stay as they were:

- `simulators.automata.ctlserial` (6): the +4 serializable part's Write (base 0x00517FD0, attractor 0x00518230, generator 0x005183D0) and Read (base 0x005180E0, attractor 0x00518270, generator 0x00518450).
- `simulators.automata.ctlspawn` (6): the generator's spawn 0x005198A0 (slot 41), the position from the occupant's lot 0x00516C40 (slot 22), resetting the tick's accumulators 0x00514E30 (26), the base automaton event 29 0x0051BCA0 (the generator's), the attractor's event 30 0x0051C030 and SetLimit 0x00517EF0 (19).

Kept as the game's: the time-passed stub 0x006F7DC0 (slot 24 of the base and the attractor), `RET 8`, three bytes, shared with other classes' vtables; it has an address entry saying so. The spawn's helpers stay the game's and are called at their addresses: the cell list 0x00518DC0, the class pick 0x00516010, the automaton manager lookup 0x0050CCA0, the point turn 0x005D77A0, the segment test 0x00514E70, the random draws (0x00913761, 0x009136B1, 0x0091372E) and the record constructor 0x00414210. So do the curve's save functions 0x005DB190 and 0x005DB5D0. With a3a (32) and a3b (19), 63 of the controllers' functions are ours. The rest of the survey's 81 are the shared, folded and adjustor functions and the script system's templates listed under "Part a3".

What the save does. Write saves a version (5), the occupant as a reference, the template's name (or an empty string it makes on its stack), the position, +0x50, +0x54, +0x3C, the flags and the automaton count, then a reference per automaton of the list. The attractor adds version 1 and its curve. The generator writes version 4 first, then the base, then its two clock pairs and +0x90, +0x94. Read mirrors it, with these quirks kept:

- It reads the name into the controller's own name (+0x28), not a template. The manager finds the template by that name after a load (part a2's ResolveTemplates). So a controller that has been read writes an empty name until its template is set again.
- It appends a node per automaton reference to the list it already has, and reads the count straight into +0x48.
- The versions go into the stack slot of the stream argument. The generator answers 0 unless that slot's low word is 4. Ours starts the slot from the stream pointer the same way, so a failed read compares the same word.

The spawn makes automata at the generator:

- How many: by the template's +0x8C. Kind 0 is the curve value times +0x88, at least 1. Kind 1 is a building's value from the simulator at city slot 0x110, or a value of its lot. Kind 2 is a count from that simulator. The count is rounded with the game's 1.5 x 2^23 trick and capped at 25.
- Where:
  - With flag 0x80 or 0x800, a random cell of 0x00518DC0's list. The point is pulled to between the template's +0x20 and +0xA8 from the box edge, and kept only if the traffic network takes it.
  - Otherwise, a random angle and radius around the controller.
- Then the class, the manager and the automaton (manager slot 0x3C). The automaton is appended, referenced and counted, told the controller manager, and given a spot (PickSpot) or none.

Three details from the seeds and the audit:

- The position from the box seeds a cRZRandom on its stack that it never draws from. Ours seeds one too, under `fixed_seed::SiteOverride(0x00516C51)`, so the clock seed counts at the game's site.
- **A fix to part a3a.** The generator's constructor now seeds its cRZRandom under `SiteOverride(0x005182CF)`. From our detour, the clock seed was counted under a different call site than the original's, so our generators had other random sequences. Nothing compared drew from them until the spawn.
- The spawn's record at local 0xF0 is 0x40 bytes, which is what the game's frame allows; the simulator fills fields up to +0x3B. The audit caught ours at 0x38.

Floats as in part a3b: 53-bit doubles, a float wherever the game stores one, a double wherever it keeps ST0 (the random values, the distance, the sums before their store). Reimplemented like the earlier parts: one writer, then an independent audit of all 12 against the disassembly. The audit found the record size above, and the zeros now come from 0x00B227CC as the game reads them (the same value as before). Both were fixed before any batch.

### Test

`OPENSC4_TEST_AUTOMATA=3` does everything `=2` does, plus the following on the test day, all at the game's addresses (so the same test runs on both sides).

For up to 8 attractors and 8 generators of the controller table:

- The serializable part's QueryInterface and GetGZCLSID. QueryInterface's AddRef is taken back.
- Write into the test's own stream.
- A round trip: a new controller of the same class, made by its factory, reads those bytes and writes them again. That must give the same bytes with an empty name, and the new controller's own name must be the template's.
- On the new controller: SetLimit, SetTemplate, ResetAccumulators, and for a generator a Read with version 0, which must answer 0. Then it is released.
- The position from the box of the original controller.

The fixed part of the bytes goes into the token as ` s<controllers>,<checksum>,<state>`: the versions, the name, the position, +0x3C, the curve and +0x90. The state checksum covers +0x50, +0x54, the flags, the clock pairs, +0x94 and the count.

Then the spawn of up to 4 initialized generators, chosen so that different count kinds and cell flags come first, is called directly (curve 1.0). The tick only spawns with flag 4, the camera-view flag, which the harness never sets. The test records:

- What the spawn made, as ` p<generators>,<made>,<checksum>`: each new automaton's type and position (through its occupant interface; the generator's list holds automaton interfaces).
- With the first new automaton: the generator's events 29 (kinds 4 and 0) and 30, and an attractor's events 29 and 30 with kind 0. The attractor is the first whose template keeps automata, if there is one.

The spawned automata stay in the city, so from that day on a run with `=3` is not the one without it (alike on both sides).

Reached (our first-call log lines, in the batch below): all 12. Read runs on every load and in the test, Write in the test. Branches reached and not:

- The spawn reaches the count kinds 0 and 1, the cell branch (flag 0x80, network kind 0xC49), and the box position from inside the spawn (flag 0x10).
- Read's list loop runs: Rush Hour's generators save 3 automata.
- Not reached: count kind 2 and flag 0x800 (network kind 2); no corpus generator has them.
- Not reached: the remove branch of the attractor's event 30. No corpus attractor template keeps automata (+0x54 is 0), so event 29 adds nothing for 30 to take out.

### Parity

2026-10-19, B = the four controller groups (`controller`, `ctlbehaviour`, `ctlserial`, `ctlspawn`) against the original, env p4env-a3c, snapshot a3c-r1. All the same, on the first pair:

| Run | Days | Test | Result |
| --- | ---: | --- | --- |
| Making Money Tutorial d0000 | 4 | `OPENSC4_TEST_AUTOMATA=3` | 5 of 5 days |
| Rush Hour Tutorial t0000 | 7 | `=3` | 8 of 8 days |
| Big City Tutorial d0000 | 7 | `=3` | 8 of 8 days |
| The 12-save batch | 7 | `=2` | 12 of 12 saves |
| The 12-save batch | 7 | `=3` | 12 of 12 saves |

Before the batch, an original-against-ours run of Making Money d0000 with `=3` and the test on day 1 matched line for line in the log, including the spawned automata's places. The `s` state checksum (tick-driven fields) repeated, so it stays in the token.


## Part v1: the vehicle and pedestrian managers, the object and its queries

### Survey

cSC4VehicleManager (class ID 0xA98F9D3B, factory 0x005145A0, constructor 0x0055FB50, 0xAE8 bytes, city slot 43) and cSC4PedestrianManager (0xC98F9D17, factory 0x005145D0, constructor 0x00545500, 0xAE0 bytes, city slot 44) keep the city's road vehicles and pedestrians. Both derive from a base of their own (constructor 0x00554360, vtable 0x00A9BD88; `cSC4VehPedManagerBase`, the name is ours). That base derives from the base all four automata managers share (constructor 0x005125E0, vtable 0x00A95880; the aircraft manager's 0x004FBB40 and the watercraft manager's 0x00574860 call it too). The common base holds most of cISC4AutomataManager: the gzcom-dll header's slots 0 to 37 line up (Init 3, Shutdown 4, AddAutomaton 7, RemoveAutomaton 8, CreateAutomaton 15, the two Dispatch 17 and 18, SetMaxCount 23, HandleTuningParametersChanged 24, SetCameraFollow 34). Its code belongs with the aircraft and watercraft group of the survey's table.

Four parts, each with its own vtable:

| Part | Vehicle | Pedestrian | Base | What it is |
| --- | --- | --- | --- | --- |
| +0x00 | 0x00A9D1E0 | 0x00A9A470 | 0x00A9BD88 | the manager, 90 slots |
| +0x04 | 0x00A9D1CC | 0x00A9A460 | 0x00A9BD78 | 4 slots; the vehicle manager's slot 3, 0x0055B3F0, wraps the common base's 0x0050DD30 |
| +0x08 | 0x00A9D1B4 | 0x00A9A448 | 0x00A9BD60 | serializable: Write 0x00553570, Read 0x00554410, GetGZCLSID (the factories answer this part) |
| +0x0C | 0x00A9D1A4 | 0x00A9A438 | 0x00A9BD50 | message target: 0x00562620, 0x00545450, the common base's 0x00506970 |

The layout is in [automata_vehmgr.h](../src/core/automata_vehmgr.h). The base's own fields:

- +0xA94, the kind list: pairs (kind, 0). The vehicle manager's kinds are 1, 2, 3, 5, 4, 7, 6, 8, 9, 10; the pedestrian manager's are 1, 7, 8. +0xA98 is its first node.
- +0xAA8, a vector of 0x1C-byte records.
- +0xAB8, the record list: 0x40-byte nodes, each with two int rectangles and a referenced object.
- +0xAC0, a stopwatch.

The derived constructors set the occupant group +0xA44 (0x4002, 0x4001), the automaton class +0xA48 (cSC4Vehicle, cSC4Pedestrian) and the default class +0xA8C (1, 2).

The group is found by the vtables of the two classes and the base, less the common base's slots, plus the helpers only they call: 94 functions, 19,898 bytes. The survey's 96 and 20,337 had two neighbours placed with it. Six of the 94 are `sub ecx,N; jmp` adjustor thunks and two are plain jumps (0x0054D250, 0x0054D780); those stay the game's. The other 86 go in three parts:

- **v1 (32, done): the object and its queries.** Below.
- **v2 (25, done): life cycle, tuning, messages and the save.** Below.
  - Init (base 0x0054DBA0, vehicle 0x0055B7E0, pedestrian 0x00544820).
  - Shutdown (0x00552D00, 0x0055B880, 0x00544860), with 0x00552A20 and 0x00562D60.
  - HandleTuningParametersChanged (0x0055B410, 0x00544410) and the tuning set-up of slot 58 (0x0055C9D0, 0x005450A0).
  - The +4 slot 0x0055B3F0.
  - The message targets: 0x00562620 (with 0x00561500, 1,039 bytes) and 0x00545450 (with 0x00544F30 and 0x005448D0).
  - Write 0x00553570 with 0x00552A80, and Read 0x00554410 with 0x00553E90.
  - AddAutomaton (0x00562C10, 0x00546990) and RemoveAutomaton 0x0054D690.
- **v3 (29, done): creation, dispatch and frames.** Below.
  - GetRandomCreationPoint 0x00553080 and the Dispatch pair 0x00551730 and 0x0054DBF0.
  - Slots 52, 53 (0x0055B480, 0x00544300), 55, 56, 63 (0x00553300, 0x0055B600), 69 to 72, 76 to 81, 83, 84 and 89.
  - Their helpers 0x0054DD50, 0x0054E690, 0x0054DB50, 0x00552BD0, 0x00555350 and 0x00555400.
  - The largest are 0x005554E0 (1,241 bytes), 0x0054EF00 (1,206), 0x00552000 (1,022) and 0x0054F3C0 (1,005). The part is about 13,000 bytes, so it may need two pieces.
  - These run on frames, so the family's frame caveat applies (top of this page).

Mods: no NAM site is in the group, and neither is any of SC4UIScale's addresses. SC4UIScale's probe 0x00505370 is the common base's CreateAutomaton (slot 15 of both managers), which stays the game's.

### What is ours

32 functions in one group, off by default: `simulators.automata.vehmgr` ([automata_vehmgr.cpp](../src/core/automata_vehmgr.cpp)).

- **The object (12):**
  - The factories 0x005145A0 and 0x005145D0.
  - The constructors 0x00554360, 0x0055FB50 and 0x00545500.
  - The destructor body 0x00553860, the deleting destructor 0x00545670 (slot 38, shared by both classes) and the record list's clear 0x00551FC0.
  - QueryInterface 0x0055B630 and 0x005444C0; GetGZCLSID 0x0055FE20 and 0x00545630.
- **The kind tables (8):**
  - Slot 85, the kind's static SC4Curve: base 0x0054D560, vehicle 0x0055B440, pedestrian 0x00544440.
  - Slot 86, that curve's float at +0x10: 0x0054D5C0, 0x0055B460, 0x00544460.
  - Slot 73, a class by kind (0x0054D3C0), and slot 74, an occupant group by kind (0x0054D420).
- **The other queries (12):**
  - Slot 87, a scale. The base's 0x0054D630 answers 1 - x * 0.01, clamped to 0..0.5; the pedestrian manager's 0x00544480 answers x * 0.02 + 1. Both answer 1.0 unless +0xA3D is set.
  - Slot 88 (0x0054D4A0): a 4x4 of words times the scale, transposed for a negative scale.
  - Slot 68 (0x0054D6C0): an automaton's class by its groups.
  - Slot 75 (0x0054DF40): the kind's curve at a city count, times the 24-hour clock's hour / 3600.
  - Slot 41 (0x0054DFB0): whether an object's position is inside the manager's rectangle grown by 48.
  - Slot 82 (0x0054D260): the neighbour cell test.
  - The record list's hit test 0x00551630, the automaton test 0x0054D150 and the three group tests 0x0054D1A0, 0x0054D210 and 0x0054D230.

Kept as the game's: the six adjustor thunks and the two plain jumps above; the common base's functions (constructor, destructor 0x00512780, QueryInterface 0x00505480, AddRef, Release), called at their addresses; and the 54 functions of v2 and v3 (both now ours, below).

Oddities kept:

- QueryInterface writes the object and calls AddRef through it without a test.
- The constructors test the pair inside each new list node for null before writing it.
- The destructor frees the record vector's buffer and the record list's head without destroying either.
- 0x0054D150 asks the automaton for its occupant and type twice.
- Slot 82 ignores its first argument.
- Slot 88 transposes for a negative or NaN scale.

One difference: slot 75 reads a float that the 24-hour clock writes into a local. Ours starts that local at 0.

Floats are handled as in the earlier parts: 53-bit doubles. Values the game leaves in ST0 are answered unrounded (slots 87 and 75). A float is stored wherever the game stores one (slot 88's factor, slot 41's rectangle, slot 75's count).

Reimplemented like the earlier parts: one writer, then an independent audit of all 32 against the disassembly. The audit found no defect in the code. One address description had slot 82's last test the wrong way round; that is fixed. The audit could not see every caller of the bool answers. In the game, QueryInterface and slot 41 leave junk in the upper bytes of EAX, where ours leaves zeros. Every caller the audit found reads AL only.

### Test

`OPENSC4_TEST_AUTOMATA_VEHMGR=1`, with `OPENSC4_TEST_AUTOMATA=2` or more, queries both managers on the test day at the game's addresses, so the same test runs on both sides (header of [automata_autotest.cpp](../src/core/automata_autotest.cpp)). It is read-only.

Into the fingerprint, per manager:

- QueryInterface for four interfaces, on the object and through the serializable part's adjustor (each AddRef is given back), and GetGZCLSID.
- Slots 73, 74, 85, 86, 87 and 88 for kinds 0 to 12.
- Slot 82 in four directions and the record list's hits, at 12 x 12 cells spread over the city (the sizes come from the city).
- The group tests over 0x4000..0x4240.

To the log only:

- Slot 75, which reads the clock.
- Slots 68 and 41 and 0x0054D150 over the automata of the manager's list, because which automata there are follows frames.

Reached (our first-call log lines, trial Making Money d0000, 4 days): all 32. The constructors, factories and QueryInterface run at the load, the destructors at the exit, and the rest by the test. In the trial the log-only values were also the same on both sides.

### Parity

2026-10-19, B = `simulators.automata.vehmgr` against the original (the switch list includes `determinism.dispatch_info=ours`), env p4env-v1, snapshot v1-r1, with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_VEHMGR=1`. All the same, on the first pair:

| Run | Days | Result |
| --- | ---: | --- |
| Making Money Tutorial d0000 | 4 | 5 of 5 days |
| Rush Hour Tutorial t0000 | 7 | 8 of 8 days |
| The 12-save batch | 7 | 12 of 12 saves |

Every B run of the three legs carried the ` v` token and reached all 32 functions (our first-call log lines).


## Part v2: life cycle, tuning, messages, the save, adding and removing

### What is ours

25 functions in four more groups, off by default, so part v1's group stays as it was ([automata_vehmgr.cpp](../src/core/automata_vehmgr.cpp), "Part v2"):

- **`simulators.automata.vehmgr.life` (13):**
  - Init: base 0x0054DBA0, vehicle 0x0055B7E0, pedestrian 0x00544820.
  - Shutdown: 0x00552D00, 0x0055B880, 0x00544860, with the record list's release 0x00552A20 and the overlay's stop 0x00562D60.
  - HandleTuningParametersChanged (slot 24): 0x0055B410, 0x00544410.
  - The tuning set-up (slot 58): 0x0055C9D0, 0x005450A0.
  - The vehicle manager's +4 slot 3, 0x0055B3F0.
- **`simulators.automata.vehmgr.messages` (5):** the message targets 0x00562620 and 0x00545450, the vehicle manager's controller update 0x00561500, the pedestrian manager's gathering 0x00544F30 and the pedestrian's send 0x005448D0.
- **`simulators.automata.vehmgr.serial` (4):** Write 0x00553570 with the record list's 0x00552A80, Read 0x00554410 with 0x00553E90.
- **`simulators.automata.vehmgr.add` (3):** AddAutomaton 0x00562C10 and 0x00546990, RemoveAutomaton 0x0054D690.

What they do:

- **Init**, once (+0x34): the common base's Init (0x005066B0), +0xA9C and +0xAA0 back to -1, city slot 0x114's object kept at +0xA88, the stopwatch started. The derived Inits add the message target (+0xC) to the message server, the vehicle manager for seven types (0xC53D10AA, 0xA6B79601, 0xA6B79611, 0xC9DA96E8, 0xC9DA96E9, 0x44778234, 0x0772FAD4), the pedestrian manager for 0xA9E3B6E2, then run slots 24 and 58.
- **Shutdown** undoes it: +0xA88 released, the +0xAA8 vector's objects released and the vector emptied, the record list's objects given back (to the controller manager's slot 0x38 first when the node's +0x38 is set) and the list cleared, then the common base's Shutdown 0x005110A0. The vehicle manager first takes a static route line (0x00B22970) off the view, unprepares and releases it, and stops and releases its +0xAE0 object.
- **Slot 24** sets the most automata (slot 23) from the tuning's word (0x00B21BC8, 0x00B21BDC) and +0xA3C from its byte. **Slot 58** sets the most automata again: that word times a float of the app object 0x00B43CC4, rounded the game's way (plus 1.5 x 2^23, the float's bits less 0x4B400000). +0xA3C then comes from a byte of the app object less one, and slot 49 gets the camera view's zoom and -1.
- **The vehicle manager's messages:**
  - 0xA6B79601 and 0xA6B79611 stop the camera follow (slot 34) when the followed automaton answers 0 and the follow is not locked.
  - 0x0772FAD4 runs the controller update: for the occupants of city slot 0x110's two lists, a flag from a property of their slot 0x18 part in city slot 0xD0's tables (set, and above 0x00B22974) goes to SetActive of the occupant's controllers, one by template and four by name.
  - 0x44778234, 0xC9DA96E8 and 0xC9DA96E9 tell every automaton (slot 0x2C0) a pair of flags, the first by the clock 0x00B43DBC.
  - 0xC53D10AA with 0xA998D30B reloads the tuning (0x00512CF0) and runs slot 24 of all four automata managers, the city situation manager's slot 0x7C and the cells object's slot 0x14.
  - Every message then goes to the common base's DoMessage 0x00506970, whose answer is the answer.
- **The pedestrian manager's 0xA9E3B6E2:** for the message's occupant, each pedestrian closer than a distance (the message's slot 0x38) and not in group 0x4000 is sent there (0x005448D0), and a sound message 0xA9E3B6E3 with the count goes out. 0x230E27AC is answered 0.
- **The save.** Write puts version 3, the common base's Write (0x0050CB80), +0xA8C, the current kind (+0xA98's node; 0 at the head) and the record list (version 1, the count, per node +8, +0xC, +0x10, the two rectangles, the object as a reference, +0x38 as a bool, +0x3C). Read takes versions 2 and 3, the common base's Read (0x005108E0), drops the saved +0xA8C, finds the saved kind's node (the first node for 0, the head when not found), and with version 3 appends the record list's nodes (copied by 0x0054ED10).
- **AddAutomaton** with none makes one while the count (+0x48) is below the most (+0x3C): a block from the class's pool (0x00B0A0F8, 0x00B0A018), built by 0x005628F0 or 0x00545F20, then the common base's AddAutomaton 0x0050F790. **RemoveAutomaton** is the common base's 0x0050C990, then slot 69.

Part v1's +0xA98 is the current kind's node rather than the first kind's (v1's constructors set it to the first). The field keeps its v1 name.

Kept as the game's: the common base's functions (Init, Shutdown, DoMessage and its +4 slot 3, Write, Read, AddAutomaton, RemoveAutomaton), the tuning load 0x00512CF0, the record copy 0x0054ED10 and the vehicle and pedestrian builders 0x005628F0 and 0x00545F20, all called at their addresses; and the 29 functions of v3 (now ours, below). No NAM or SC4UIScale site is among the 25, and none needed keeping for stack leftovers or x87 values held across a call.

Oddities kept:

- Read drops the +0xA8C it reads, and turns a saved kind of 0 (what Write saves for the head) into the first kind's node.
- The record list's read zeroes a record's object and then tests it for a release.
- The base Shutdown erases the vector's records from begin to end, which moves the none after the end down.
- The common base's Shutdown runs even when +0x34 is clear.
- The controller update asks each occupant for its property part twice and fetches the node allocator for every list.
- The pedestrian message tests its occupant, still null, for a release before asking for it, and asks the message's type twice.
- The versions are read into the stack slot of the stream argument, as in part a3c; ours starts that slot from the stream's address the same way.

Return values. EAX's top bytes follow the game's wherever they come from a call: RemoveAutomaton, the full AddAutomaton, Read's version tests, the pedestrian messages, 0x005448D0 and the +4 slot. The Inits, Shutdowns, tuning functions, the controller update and the gathering answer 1 where the game leaves call leftovers above AL; their callers read AL.

Stack leftovers the game would read, none in the runs:

- Read's saved kind and the record list's count, when the stream fails before them. Ours start at 0.
- The top three bytes of the controller update's flag, which goes to SetActive as a dword. Both SetActive functions, and what they pass it to (SetFlag 0x00517E70, 0x00515770), read its low byte. Ours sends 0 or 1.

Floats as in part v1. The tuning's word goes to a double unsigned; the product and the sum with 1.5 x 2^23 are rounded to doubles, then the sum to a float. The pedestrian distance is the square root of the squared differences summed in z, y, x order, compared with the distance as a float; a NaN is not closer.

One mistake the first trial caught: the occupant's slot 0x18 takes no argument (it answers its +8 part). The property ID the game pushes before that call is the tables' second argument. The first version passed it to slot 0x18, which unbalanced the stack and ended the game at the first controller update. Every other vtable call's argument count was then checked against the callee's `ret`.

### Test

`OPENSC4_TEST_AUTOMATA_VEHMGR=2` does everything `=1` does, plus the following on the test day, at the game's addresses (header of [automata_autotest.cpp](../src/core/automata_autotest.cpp)). Per manager:

- Write into the test's own stream. Into the fingerprint as ` x<fixed>,<flags>` (vehicle) and ` <fixed>,<flags>` (pedestrian): the versions, the most automata, the two bools, +0xA8C, the kind and the record list. The automata count and the common base's table follow frames and go to the log.
- A round trip into a new manager from the class's factory, held by one reference. It reads the bytes (the automata count zeroed in its copy, since the references read back as nothing) and writes them again. The bytes must come back with its own +0xA8C. The pedestrian manager's do; the vehicle manager's differ inside the common base's table, whose order follows the hash table. Both sides alike.
- On that new manager:
  - reads of version 0, of version 2 (no record list) and of a record list of version 9;
  - AddAutomaton with none while full (its count set to the most for the call);
  - RemoveAutomaton with none, flags 0 and 1;
  - a message of a type nobody handles, the vehicle manager's 0xC9DA96E8, and the vehicle manager's +4 slot 3;
  - then the release (Shutdown, not initialized, and the deleting destructor).
- The pedestrian manager's gathering on the city's manager around its first pedestrian with distance 0, and the pedestrian's send on that pedestrian to its own position. The send changes the pedestrian's mode, frame-driven state, alike on both sides. The vehicle manager's overlay stop on a zeroed object.

The answers' AL and the round trip's result are the flags.

Reached (our first-call log lines, trial Making Money d0000, 4 days): all 25.

- By the game: the Inits, the tuning functions, Read and the record list's read at the load; the Shutdowns and the record list's release at the exit; both message targets, the controller update, the +4 slot, AddAutomaton and RemoveAutomaton during the run.
- By the test: Write and the record list's write, the gathering, the send and the overlay stop. The overlay stop takes only its early return; the vehicle manager's +0xAE0 stays empty in every run.
- Not reached: AddAutomaton's branch that makes an automaton (it has a first-call line of its own; no run logs it).

### Parity

2026-10-19, B = `simulators.automata.vehmgr` and the four part v2 groups against the original (the switch list includes `determinism.dispatch_info=ours`; two jobs, `harness.multi_instance=ours` on both sides), env p4env-v2, snapshot v2-r1, with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_VEHMGR=2`. All the same, on the first pair:

| Run | Days | Result |
| --- | ---: | --- |
| Making Money Tutorial d0000 | 4 | 5 of 5 days |
| Rush Hour Tutorial t0000 | 7 | 8 of 8 days |
| The 12-save batch | 7 | 12 of 12 saves |

Every B run carried the ` x` token. All 25 functions were reached in 12 of the 14 B runs. In Big City d0000 and d1095 the pedestrian manager has no pedestrian on the test day, so the gathering and the send (and part v1's slot 68) are not called there.


## Part v3: creation, dispatch and frames

### What is ours

29 functions in two more groups, off by default, so the groups of parts v1 and v2 stay as they were ([automata_vehmgr.cpp](../src/core/automata_vehmgr.cpp), "Part v3"):

- **`simulators.automata.vehmgr.create` (11):**
  - GetRandomCreationPoint (slot 16) 0x00553080.
  - The two Dispatch: slot 17 0x00551730 (a path for an automaton) and slot 18 0x0054DBF0 (a new automaton, then slot 17).
  - The record add (slot 70) 0x005538D0.
  - The occupant's automaton (slot 71) 0x0054F8A0, with the network position 0x0054DD50.
  - The kind matrix (slot 72) 0x0054DDB0.
  - The automaton's kind set 0x0054E690.
  - The three spawns: slots 79, 80 and 89 (0x0054EF00, 0x0054F3C0, 0x0054FA10).
- **`simulators.automata.vehmgr.frame` (18):**
  - The frame update (slot 63): base 0x00553300, vehicle 0x0055B600, with the record list's update 0x00552BD0.
  - The occupant's removal (slot 52) 0x0054ED90 and arrival (slot 53): vehicle 0x0055B480, pedestrian 0x00544300.
  - The records' removal (slot 55) 0x005516C0 with the record copy 0x0054DB50.
  - The occupants' refresh (slot 56) 0x0054F7B0 and the automaton's forgetting (slot 69) 0x0054FBC0.
  - The kind sweep (slot 76) 0x00552000, the cell balance (slot 77) 0x005554E0 with the tuning map's insert 0x00555350 and lookup 0x00555400, the cell spawn (slot 78) 0x00551A50, the cell cull (slot 81) 0x005539A0, the lot sweep (slot 83) 0x00553370 and the records' update (slot 84) 0x00552DE0.

What they do:

- **Every frame** (slot 63, with the frame's time): the record list (+0xAB8) is aged at most every 500 ms of the stopwatch +0xAC0. A record past its time is given back (to the controller manager's slot 0x38 when held), released and freed; the others follow their object's cell. Then, while the manager may have automata (+0x3C above 0, +0x4D and 0x00B21B9C clear), the kind sweep (with +0x4C set) and the records' update (with records). Then the common base's slot 63; the vehicle manager adds slot 64.
- **The kind sweep** adds the time to each kind's clock, picks the next kind of the tuning mask 0x00B21D5C and, when the day or night (0x00B43DC4's slot 0x58) suits the kind and its slot 42 value is not near 0, walks the cells object's rectangle from where it stopped (+0xA9C, +0xAA0) for 5 ms of its own stopwatch:
  - kinds 1, 2, 3 and 7: the cell balance (77) on each cell's network occupant;
  - kinds 4, 5, 6, 9 and 10: the cell spawn (78) with the kind's rate (slot 75);
  - kind 8: the lot sweep (83).
- **The cell balance** keeps each class's density. The kinds' 4x4 matrices (slot 72) are summed and set against the target of 0x00B43DA8's slot 0x5C through a tuning record per occupant key (slot 91; the map 0x00B21D60, filled with the tuning). Above it, the cell cull (81) by a draw; below it, a spawn (80) at a place drawn by weight.
- **The cell spawn** ages the cell's 4x4 kind table (the cells object's slots 0x54 and 0x60) and spawns (79) on its open sides.
- **The spawns** (79, 80, 89):
  - build the automaton's key from the class, a drawn lane and the sides;
  - check the cell's tables and keep away from busy cells, and from the camera's automaton (slot 80);
  - create the automaton (slot 15, the game's) at the point slot 25 gives;
  - give it its kind (0x0054E690) and its occupant (automaton slot 0x20C), or remove it again.
- **The records** come from slot 53 (an occupant's building finished: the IDs of its property 0xE9AEE3DA / 0xE9AEE3DC, and for vehicles a drawn record 0x4206) through slot 70. The records' update (84) spends each record whose timer ran out. The network points near its object (the cells object's slot 0x68) get automata by slot 71 or by Dispatch (18), until the record's count is spent or five tries failed; between tries it waits 1000.
- **GetRandomCreationPoint** draws a network point near a position (the cells object's slot 0x64 within a radius clamped to the city's size). With flags 0x22000 it is the common base's 0x00506DC0.

Kept as the game's, called at their addresses or through the vtable:

- The common base's CreateAutomaton (slot 15, 0x00505370, SC4UIScale's probe site).
- The common base's slot 46 (0x00510690), slots 19, 20 and 25, its GetRandomCreationPoint and its slots 52, 55, 56 and 63.
- The base's jump 0x0054D250 and the record rectangles 0x0054EC60.
- The cells object, the path object and the automata.

No NAM or SC4UIScale site is among the 29. None holds an x87 value across a call, apart from the C runtime's ftol (C++'s truncation).

Oddities kept:

- Slot 52 reads the occupant's rectangle only when there is an occupant (otherwise stack leftovers, below).
- The pedestrian manager's slot 53 never calls the base's.
- GetRandomCreationPoint passes 0 as the common base's seventh argument and leaks the network points when the cells object answers false; slot 84 does the same.
- Slot 17 leaks the path object when the automaton is not an occupant's. It answers the automaton even when slot 19 fails.
- Slot 89 leaves the automaton it made in the list when the automaton is not an occupant's.
- The record list's update tests the object it has just cleared.
- Slot 76 sends the first kind's clock to slot 77, whatever the kind.
- Slot 72's sign flag keeps the upper bytes of its first argument.
- Slots 79 and 80 ignore their second argument; slot 80 its sixth too.

Stack leftovers the game would read, none logged in the runs (each has a first-call line of its own):

- Slot 52's rectangle without an occupant. Ours is 0.
- The four floats of a new tuning map record (0x00555400 with a key the tuning did not load). Ours are 0.
- The cell balance's records past the last when the drawn weight is not spent. Ours stops.
- The upper bytes of the byte counters slot 78 passes as dwords. Ours are 0.

Floats as in parts v1 and v2. The cell of a position is the game's floor: the coordinate times 1/16, less 0.5, plus 1.5 x 2^23 rounded to a float, its bits less 0x4B400000, one more when that is the scaled value. FST keeps the unrounded value on the x87 stack; ours keeps the double the same way (slot 76's kind value, slot 77's row sums, slot 80's distance, slot 84's timer test).

Return values. EAX's top bytes follow the game's where they come from a call or a pointer: slots 52, 53 (vehicle), 55, 63, 69 (which RemoveAutomaton reads), 71, the Dispatch pair, and 16 with flags 0x22000. The others answer AL alone. Slots 56, 72, 76 to 78, 80, 81, 0x00552BD0 and 0x0054E690 answer nothing their callers read.

Reimplemented like the earlier parts: one writer, then an independent audit of all 29 against the disassembly, with every vtable call's argument count checked against the callee's `ret` (the managers' and the road automata's vtables; elsewhere against the stack offsets after the call). The audit found one defect: the vehicle manager's slot 53 asked the occupant for its slot 0xC once too often. That is fixed. It also had the cell balance's records capped at 11, where the game's frame holds 12; now 12 (at most nine kinds come there). The first batch then found a second one the audit had missed: slot 72's second factor is the absolute value of its fourth argument (the stack slot the game reads after the flag), not of the scale. With the test's sweep that changed which automata were made, and six of the twelve saves differed in the saved city (an automaton occupant in a cell), A and B each alike over two pairs. A bisect by `OPENSC4_AUTOMATA_SKIP` found it; it is fixed.

### Test

`OPENSC4_TEST_AUTOMATA_VEHMGR=3` does everything `=2` does, plus the following on the test day, at the game's addresses (header of [automata_autotest.cpp](../src/core/automata_autotest.cpp)).

Into the fingerprint as ` y<fixed>,<flags>` (vehicle) and ` <fixed>,<flags>` (pedestrian):

- On a new manager of the class: three records by slot 70 (none without an object) with two building occupants of the controller table, slot 55 taking them out again in two steps (0x0054DB50 moves the rest down), hashed after each step with the occupants as indices.
- Slots 69 and 52 on its empty list, slot 52 with a network occupant (the cells' occupants, the interface 0xA821EF94 the spawns and slots 52 and 72 take).
- Slot 72's zero kinds and its slot 88 kinds.
- Slot 53 on another new manager with a building's cISC4BuildingOccupant (0x87DDA3A9), what the game's completion message 0x2977AE2F carries: its records.
- The record copy on records of the test's own.
- For the vehicle manager, the tuning map's insert and lookup on a map of the test's own, and the lookup of every key of the game's map (read-only).

To the log only, on the city's managers (which automata there are, the cells' contents and the manager's generator follow frames):

- GetRandomCreationPoint at four places spread over the city, with the generator (+0x50) seeded for the calls and put back after.
- 0x0054DD50 and slot 72 at two network occupants.
- AddAutomaton with none while there is room, and the automaton it made removed again (slot 8 with 1). This is the branch part v2 never reached; it runs on both sides alike.
- The frame work the harness's saves leave off:
  - In these saves +0x4C (the manager's own generation) is clear, so slot 63 never sweeps, slot 46 makes no automaton for the spawns, and there are no records.
  - With +0x4C set for the duration and the generator seeded, the test runs the kind sweep through each kind of the list.
  - The harness's stopwatch counts its reads, so a call walks a few cells; the test calls again until the kind's walk is through, at most 40000 calls.
  - Then records as slot 53 makes them are spent by slot 84; what is left is released and cleared, and +0x4C is put back.

These add automata to the city and take some away. From that day on the run is not the one without the test (on both sides alike).

Reached (our first-call log lines, trial Making Money d0000, 4 days): 26 of 29.

- By the game alone: the frame update (both), the record list's update, slot 56, slot 69.
- By the test: the rest. With the test the managers made automata (vehicles 107 to 116), and Dispatch 17, slot 18, slot 71 and the kind set ran.
- Not reached: slot 79 (no cell spawn place had both a table value and an open side), slot 89 (no record of 0x00B43D70 at an automata cell) and, in Making Money, the pedestrian manager's slot 53 with a property (no building there has 0xE9AEE3DC; it is called with another building and answers without records).

In the parity runs below, 28 of the 29 were reached in at least one B run (each run's first-call lines): all but slot 79. The pedestrian manager's slot 53 with records, slot 89 and the Dispatch pair ran in some saves and not others. No stack-leftover line was logged in any run.

### Parity

2026-10-20, B = `simulators.automata.vehmgr` and the six groups of parts v2 and v3 against the original (the switch list includes `determinism.dispatch_info=ours`; two jobs, `harness.multi_instance=ours` on both sides), env p4env-v3, snapshot v3-r4, with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_VEHMGR=3`. All the same:

| Run | Days | Result |
| --- | ---: | --- |
| Making Money Tutorial d0000 | 4 | 5 of 5 days |
| Rush Hour Tutorial t0000 | 7 | 8 of 8 days |
| The 12-save batch | 7 | 12 of 12 saves |

Every B run carried the ` y` token.


## Part aw: the aircraft and watercraft managers and the common base

### Survey

cSC4AircraftManager (class ID 0xC98F9D4A, factory 0x00514600, constructor 0x004FBB40, 0xA9C bytes, city slot 45) and cSC4WatercraftManager (0xA98F9D28, factory 0x00514630, constructor 0x00574860, 0xAD4 bytes, city slot 46) keep the city's aircraft and boats. Both derive directly from the base all four automata managers share (constructor 0x005125E0, vtable 0x00A95880, GetGZCLSID 0x498F9CB5; "cSC4AutomataManager", the name is ours), which part v1's vehicle and pedestrian base derives from too. The layout is in [automata_awmgr.h](../src/core/automata_awmgr.h).

Four parts, each with its own vtable:

| Part | Aircraft | Watercraft | Common base | What it is |
| --- | --- | --- | --- | --- |
| +0x00 | 0x00A94850 | 0x00A9DF38 | 0x00A95880 | the manager, 68 slots |
| +0x04 | 0x00A94840 | 0x00A9DF28 | 0x00A95870 | 4 slots; slot 3 is the common base's 0x0050DD30, the aircraft manager's 0x005DEB40 |
| +0x08 | 0x00A94828 | 0x00A9DF10 | 0x00A95858 | serializable: Write 0x004FB5B0 / 0x0056FA70 / 0x0050CB80, Read 0x004FBE70 / 0x005749E0 / 0x005108E0, GetGZCLSID |
| +0x0C | 0x00A94818 | 0x00A9DF00 | 0x00A95848 | message target: 0x004FC710, 0x00575500, 0x00506970 |

The gzcom-dll cISC4AutomataManager header's slots line up from Init at slot 3 (each overloaded pair in the other order), so slot 6 is IsActive (+0x4C), 9 to 11 Pause, Resume and IsPaused (+0x4D), 13 IsDrivingMode, 14 GetMinZoom, 28 and 29 GetExemplarData and its map, 30 CanCreateGroup, 35 IsCameraFollowUpdating, 36 and 37 GetSourceBuildingCount and its group form. The aircraft manager overrides slots 0, 3, 4, 7, 17, 18, 24 and 38; the watercraft manager slots 0, 3, 4, 7, 8, 15, 16, 24, 38 and 63.

The group is found by the three classes' vtables, less nothing shared outside the four managers, plus the helpers only they (or part v1's group) call: 172 functions, 46,000 bytes. The survey's 222 and 56,293 had neighbours placed with it (among them the class at vtable 0x00A95990, 0x00513E00 to 0x00514340). Two callees were left to their own parts: 0x00523F60 (an aircraft automaton's constructor, which the aircraft manager's AddAutomaton calls) and the script system's 0x0051C380, 0x0051C3B0 and 0x0051CD90. Of the 172:

- **Stay the game's (19).**
  - The fifteen `sub ecx,N; jmp` adjustor thunks: 0x004FBC60 to 0x004FBCE0 (nine, six of them shared by all four managers), 0x00512750, 0x00512760, 0x00512770, 0x00574920, 0x00574930, 0x00574940.
  - The shared stubs 0x004FC900 (slots 51 and 52) and 0x005053E0 (slot 18, `xor eax,eax; ret 0x1C`).
  - SC4UIScale's two probe sites in the group: the common base's CreateAutomaton 0x00505370 (slot 15) and the aircraft manager's message helper 0x004FBFE0. Its popup style patch (0x0052CCEE, 0x0052CD01) is in the city situation manager's 0x0052CC50, not here. No NAM site is in the group.
- **aw1 (36, done): the object and its queries.** Below. With the two factories, which the survey counted with the registration.
- **aw2 (25, done): the common base's life cycle, tuning, save and messages.** Below ("Part aw2").
  - Init 0x005066B0, Shutdown 0x005110A0, Activate (slot 5) 0x0050A6B0, SetMaxCount (23) 0x0050AA60, ReleaseAutomatonSet (33) 0x0050CDE0.
  - DoMessage 0x00506970 and the +4 slot 3 0x0050DD30 with 0x00542EE0 and 0x00542F50.
  - Write 0x0050CB80, Read 0x005108E0 with 0x0050FEB0.
  - The tuning load 0x00512CF0 (2,777 bytes) with 0x00512860, 0x00512940, 0x00510C80, 0x00510D20; slot 39 0x00513910; TrackAutomataGroupInstances (31) 0x00513C40 with 0x00510F10, 0x00510450, 0x00512030; slot 66 0x00514520 with 0x00512C80 and 0x005120E0.
- **aw3 (36, about 12,200 bytes, done): the common base's automata, creation, dispatch and frames.** Below ("Part aw3").
  - AddAutomaton 0x0050F790, RemoveAutomaton 0x0050C990, GetRandomCreationPoint 0x00506DC0.
  - TranslateDispatchLocation and ChaseAutomaton (19 to 22): 0x00507120, 0x00506F50, 0x00511260, 0x005072E0.
  - The exemplar choices (25 to 27): 0x0050BE90, 0x0050C0D0, 0x0050C1D0. GetInstancesOfGroup (32) 0x00510040. SetCameraFollow (34) 0x0050A930.
  - Slots 40 to 43, 45 to 50, 53 to 57, 59 to 65 and 67: 0x00506600, 0x00507AC0, 0x0050C8B0, 0x00507B80, 0x00511990, 0x00510690, 0x0050A740, 0x00506CD0, 0x00505350, 0x0050A5D0, 0x0050C6A0, 0x00506D50, 0x0050C7E0, 0x0050F2C0, 0x0050A670, 0x0050F690, 0x0050A520, 0x005073D0, 0x00507430, the frame update 0x0050A7B0 with 0x00507BF0, 0x00507470 (1,606 bytes), 0x0050F930, 0x0050F9C0 (1,245).
- **aw4 (35, about 9,500 bytes, done): the aircraft manager's behaviour and the watercraft manager's life cycle, save and messages.** Below ("Part aw4").
  - Aircraft (11): AddAutomaton 0x004FA6D0, Init 0x004FA9E0, HandleTuningParametersChanged 0x004FAA50, slots 17 and 18 (0x004FAAF0, 0x004FC440), Write 0x004FB5B0, Shutdown 0x004FB7D0, Read 0x004FBE70, DoMessage 0x004FC710 with 0x004FB8D0 and 0x004FB2B0.
  - Watercraft (24): Init 0x00567B90, Shutdown 0x005744A0, the tuning 0x00567BF0, AddAutomaton 0x0056F7C0, RemoveAutomaton 0x0056F800, Write 0x0056FA70, Read 0x005749E0 (1,917 bytes), DoMessage 0x00575500 with 0x00574520 and 0x00575170, and their helpers 0x00574340, 0x00574230, 0x005732F0, 0x005734E0, 0x00573520, 0x00574830, 0x00574260, 0x0056C850, 0x00570D10, 0x00570D60, 0x0056D7F0, 0x0056D6E0, 0x0056C910, 0x0056F740.
- **aw5 (23, about 11,000 bytes, done): the watercraft manager's creation, routes and frames.** Below ("Part aw5"). CreateAutomaton (15) 0x00568F30, GetRandomCreationPoint (16) 0x0056C990 (1,321 bytes), the frame update (63) 0x005736A0 (2,941), and 0x00566DA0, 0x00566E60, 0x00567C90, 0x00567CE0, 0x005684C0, 0x00568CE0, 0x00568FD0, 0x0056C940, 0x0056D5F0, 0x0056F510, 0x0056F670, 0x0056FD60, 0x00570CC0, 0x00570DF0, 0x005710A0 (1,417), 0x00571630, 0x00572360, 0x005723D0, 0x00573230, 0x00573580.

aw3 and aw5 run on frames, so the family's frame caveat applies (top of this page). The common base's functions are also the vehicle and pedestrian managers', so every aw part changes all four managers.

### Part aw1: what is ours

36 functions in one group, off by default: `simulators.automata.awmgr` ([automata_awmgr.cpp](../src/core/automata_awmgr.cpp)).

- **The object (22):**
  - The factories 0x00514600 and 0x00514630.
  - The constructors 0x005125E0 (common base), 0x004FBB40 and 0x00574860.
  - The destructor bodies 0x00512780 (common base) and 0x00574970 (watercraft), the deleting destructors (slot 38) 0x00512CD0, 0x004FBCF0 and 0x00574950, and the container clears they call: the aircraft map's 0x004FB780 with its erase 0x004FB270, the watercraft tables' 0x0056D670 and 0x005743D0.
  - QueryInterface 0x00505480, 0x004FA740 and 0x00566E30; AddRef 0x00505510 and Release 0x00507B50 (all four managers'); GetGZCLSID 0x00512740, 0x004FBC50 and 0x00574910.
- **The queries (14), slots of all four managers:** IsActive 0x004FBBE0, Pause 0x004FBBF0, Resume 0x004FBC00, IsPaused 0x004FBC10, IsDrivingMode 0x00506940 (the view service's view is the U-Drive's, 0x2B709676), GetMinZoom 0x004FBC20, the five-argument Dispatch stub 0x005053F0, GetExemplarData 0x004FBC30 and its map 0x004FBC40, CanCreateGroup 0x0050C300, IsCameraFollowUpdating 0x00505400 (the byte 0x00B21B94), GetSourceBuildingCount 0x0050BE30 (the +0xA64 table's count, or for 0x1402 and 0x1406 the object at 0x00B43D28), its group form 0x00505410 and the exemplar find of slot 44, 0x00507B00.

IsActive and IsPaused are 4 bytes long; their address entries list the two bytes of `CC` padding after them, as part a3a's GetOccupant does, so the 5-byte entry hook stays inside the function.

Kept as the game's: the adjustor thunks, the two stubs and the two SC4UIScale probe sites above; the containers' helpers (the exemplar map's erase 0x0050F1E0, the exemplar records' range 0x00510650, the hash table clears 0x008EBA60 and 0x00829D20, the watercraft records' range 0x0056F740, the group's class 0x00505330); SC4Random::ConstructSeeded, the hash set constructor, the map and hash table finds, SC4PairList::Destroy, SC4RefVector::Destroy and the allocators, all called at their addresses.

Oddities kept:

- QueryInterface's branches for 1, the serializable part and the message target write a null and still call AddRef through a null object; its cISC4AutomataManager branch never tests the object.
- Release at a count of 1 (or 0) shuts down and deletes without counting down.
- The common base's constructor writes the vtables of its own bases (0x00A8298C, 0x00A80784, 0x00A81174) before its own.
- The destructors free the containers' storage but never destroy the generator at +0x50. The aircraft and watercraft managers' deleting destructors do not set the vtables back.
- Slot 44 does not read the key of an empty vector.

The generator at +0x50 is seeded from the clock by SC4Random::ConstructSeeded; ours seeds it under `fixed_seed::SiteOverride(0x00512666)`, so the clock seed counts at the game's call site, as part a3c's generator does.

Return values. EAX's top bytes follow the game's where they come from a call or a value: QueryInterface (AddRef's answer, or what its compares leave of the IID), Release, IsDrivingMode (the view's ID), CanCreateGroup and GetMinZoom (zero), the source building counts and the destructor bodies (what their last call leaves). IsActive, IsPaused and IsCameraFollowUpdating answer AL; the game leaves its caller's EAX above it.

No floats, no stack leftovers that are read.

### Test

`OPENSC4_TEST_AUTOMATA_AWMGR=1`, with `OPENSC4_TEST_AUTOMATA=2` or more, queries the city's four managers on the test day at the game's addresses, so the same test runs on both sides (header of [automata_autotest.cpp](../src/core/automata_autotest.cpp)). Into the fingerprint as ` A<aircraft>,<watercraft>,<vehicle>,<pedestrian>,<fresh>,<flags>`:

- QueryInterface for the four class IDs and six interfaces, on the object and through the +4, +8 and +0xC parts (each AddRef given back), AddRef and Release, GetGZCLSID, and the queries above.
- Every exemplar record found by its own key and a key not there; every key of the exemplar map through CanCreateGroup; every key of the source building table through slot 36, and slot 37 over groups 0x4000 to 0x4240.
- A new aircraft and a new watercraft manager from the factories, with nodes of the test's own in their containers, released so that the destructors' clears walk them; a bare common base built and deleted by its slot 38 (which no class reaches otherwise).

To the log only: the reference and automata counts, and the source building counts that come from the object at 0x00B43D28. The city's managers are not changed.

Reached (our first-call log lines, trial Making Money d0000, 4 days, original and ours alike): all 36. The constructors, factories, QueryInterface, AddRef, Release, IsDrivingMode, IsActive, CanCreateGroup and GetSourceBuildingCount by the game at the load; the rest by the test.

### Parity

2026-10-20, B = `simulators.automata.awmgr` against the original (the switch list includes `determinism.dispatch_info=ours`; two jobs, `harness.multi_instance=ours` on both sides), env p4env-aw1, snapshot aw1-r1, with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_AWMGR=1`. All the same:

| Run | Days | Result |
| --- | ---: | --- |
| Making Money Tutorial d0000 | 4 | 5 of 5 days |
| Rush Hour Tutorial t0000 | 7 | 8 of 8 days |
| The 12-save batch | 7 | 12 of 12 saves |

In the 12-save leg, three saves needed a second leg:

- Rush Hour d0000's first B run had other `adv` and `advisors` values from the first day on. That is the first-run advisor outcome of [parity.md](parity.md); its three repeats were the same as A.
- In Rush Hour d0090 and Making Money d0000, the B game ended during the load: no crash dump, no crash line, and other games were running on the machine.

A second leg of those three saves was the same on the first pair. Every completed B run carried the ` A` token and reached all 36 functions (our first-call log lines).

## Part aw2: the common base's life cycle, tuning, messages and save

### What is ours

25 functions in four more groups, off by default, so part aw1's group stays as it was ([automata_awmgr_life.cpp](../src/core/automata_awmgr_life.cpp)):

- **`simulators.automata.awmgr.life` (13):**
  - Init 0x005066B0 and Shutdown 0x005110A0.
  - Activate (slot 5) 0x0050A6B0, SetMaxCount (23) 0x0050AA60 and ReleaseAutomatonSet (33) 0x0050CDE0.
  - TrackAutomataGroupInstances (31) 0x00513C40, with the group instance table's erase 0x00510F10, its node release 0x00510450 and its insert 0x00512030.
  - Slot 39 0x00513910 (the cheats' registration and the tuning load, the first time).
  - Slot 66 0x00514520 (the exemplar data), with the record range copy 0x00512C80 and the record copy 0x005120E0.
- **`simulators.automata.awmgr.tuning` (5):** the tuning load 0x00512CF0 with the kind curves' filler 0x00512860 and insert 0x00510C80, and the tuning map's filler 0x00512940 and insert 0x00510D20.
- **`simulators.automata.awmgr.messages` (4):** DoMessage 0x00506970, the cheat target (the +4 part's slot 3) 0x0050DD30, and the route line object it makes: constructor 0x00542F50 and Prepare 0x00542EE0.
- **`simulators.automata.awmgr.serial` (3):** Write 0x0050CB80, Read 0x005108E0 and its body 0x0050FEB0.

Part v2's vehicle and pedestrian code calls Init, Shutdown, DoMessage, the +4 slot 3, Write, Read and the tuning load at their game addresses, so with these groups on they reach ours.

What they do:

- **Init**, once (+0x34):
  - slot 39;
  - the message server for fourteen types (0xA6B79602, 0xA6B79612, 0x06CDB65A, 0x26D31EC1, 0x26D31EC2, 0x99EF1142, 0x99EF1143, 0x2977AE2F, 0x4A7FB807, 0xEA298D50, 0xC9F775BB, 0xCAD61E00, 0xEAD61E16, 0xEBFF18A3);
  - the frame service 0x86AD10EC and the cheat codes (the +4 part);
  - the cells object, the script system and the controller manager kept (city slots 0xA0, 0xC0, 0xBC);
  - slots 65 and 66.
- **Shutdown** undoes it with slots 45 and 40. It releases the exemplar records' objects (+0x30) but keeps the records.
- **Slot 39** registers five cheats the first time (counter 0x00B21B88; 0xE97FCBC8 and 0xE97FCBC9 show and hide a route line, 0x0B70C96D, 0x6BB354C7 and 0xCC478B79), then runs the tuning load.
- **The tuning load** reads the automata tuning exemplar (0x6534284A, 0xA998D30B, 1) into the statics 0x00B21BC8 to 0x00B21D5C, the kind curves (table 0x00B21D44) and the tuning map (0x00B21D60, the four floats and the curve of each key that part v3's cell balance reads).
- **SetMaxCount** clamps to 0 to 5000. While the count is above it, it removes automata neither flagged 0x200000 nor at 0xFF (slot 62). +0x40 is the C runtime's truncation of the count times 0x00A955A0.
- **TrackAutomataGroupInstances** keeps, per group, a hash set of the list's automata in the group (referenced); untracking releases them through slot 33.
- **DoMessage** sends the sixteen message types to slots 45, 48, 50, 51, 53 to 59 and 63, and sets the scale switch +0xA3D and the camera hold +0xA4C. A camera move past the zoom's distance (0x00A95518) keeps the camera's point (+0xA30) and runs slot 48.
- **The save.** Write saves version 6, the most, the bools +0x4C and +0xA3D, the count, a reference per automaton, and the source building table's (key, count) pairs. Read takes versions up to 6; from version 5 it reads the table; +0xA78 tells a version below 6.

Kept as the game's:

- the SC4UIScale probe sites (0x00505370, 0x004FBFE0) and the exemplar callback 0x00513A60;
- the hash set and hash map helpers, the curve, string, property, stream and tokenizer helpers, SC4RenderState::Init and the allocators, all called at their addresses.

No NAM or SC4UIScale site is among the 25.

Oddities kept:

- Read frees the list's old nodes without releasing their automata, and appends what it reads without counting it; the count comes from the stream.
- Read reads the version, the references and the table's count into the stack slot of its stream argument. Ours keeps that slot's value the same way, so a failed count read leaves the last reference (or the stream's address) there.
- A failed read of a table record keeps the record slots' earlier values. The key's starts as the reference counter. The value's is a stack leftover in the game; ours starts at 0.
- Write walks the table bucket by bucket, finding the next bucket from the node's key.
- TrackAutomataGroupInstances answers 1 for a group the exemplar map does not hold and for a group already tracked.
- ReleaseAutomatonSet leaves the set's buckets.
- Slot 66 copies the exemplar vector onto its own end before it destroys it, so the record copy never runs from the game.
- The cheat target allocates its route line without a null test.
- DoMessage answers 1 for every message.
- The tuning load releases the exemplar's resource halfway and reads the rest after. Its first two masks become all ones at a zero byte; the kind mask never does.

Floats are handled as in the earlier parts: 53-bit doubles. The tuning load's three computed floats (0x00B21CF8, 0x00B21C8C, 0x00B21D40) take a word unsigned, as the game's FILD with 2^32 added does, with one product or quotient rounded to a float. DoMessage compares the unrounded squared distance the asm's way: not above, or unordered, keeps the old point. Return values: AL, as the callers read it.

Reimplemented like the earlier parts: one writer, then an independent audit of all 25 against the disassembly. The audit found one defect: Read's table count and records started from other stack values than the game's when a stream read fails. Ours now keeps the slots the way the game does. The test drive never fails a read, so no run would have shown it.

### Test

`OPENSC4_TEST_AUTOMATA_AWMGR=2` does everything `=1` does, plus the following, into the fingerprint as ` B<aircraft>,<watercraft>,<vehicle>,<pedestrian>,<fresh>,<cheats>,<flags>` (header of [automata_autotest.cpp](../src/core/automata_autotest.cpp)):

- Per city manager:
  - the common base's Write (the automata count, which follows frames, to the log);
  - TrackAutomataGroupInstances for the first two untracked groups of the exemplar map, tracked and untracked again (the sets' sizes to the log), and for a group the map does not hold.
- On a new aircraft manager:
  - the round trip of the first manager's bytes, which must come back the same;
  - reads of versions 7, 4 and 5;
  - SetMaxCount, Activate and ReleaseAutomatonSet;
  - thirteen messages of the test's own;
  - then the release.
- On test-owned data: the group instance table's insert and erase (two keys in one bucket, so the erase reaches 0x00510450) and the exemplar record copy.
- The cheat target: another message, another cheat, 0xCC478B79 three ways (its flag 0x00B09968 put back after), and the route line's show and hide.

Reached (our first-call log lines, trial Making Money d0000, 4 days): all 25. Init, the tuning load and its helpers, slot 39, slot 66 with its range copy, Activate, SetMaxCount, TrackAutomataGroupInstances, DoMessage and Read run by the game at the load or during the run; Shutdown at the exit; the rest by the test (the record copy and the node release only by it).

### Parity

2026-10-20, B = `simulators.automata.awmgr` and the four part aw2 groups against the original (the switch list includes `determinism.dispatch_info=ours`; `OPENSC4_MUTE=1` on both sides; two jobs, `harness.multi_instance=ours`), env p4env-aw2, snapshot aw2-r1, with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_AWMGR=2`. All the same:

| Run | Days | Result |
| --- | ---: | --- |
| Making Money Tutorial d0000 | 4 | 5 of 5 days |
| Rush Hour Tutorial t0000 | 7 | 8 of 8 days |
| The 12-save batch | 7 | 12 of 12 saves |

Big City d0090 and Rush Hour d0090 needed more pairs: the original itself had two outcomes there, and B had the same two. Every B run carried the ` B` token and reached all 25 functions.


## Part aw3: the common base's automata, creation, dispatch and frames

### What is ours

36 functions in three more groups, off by default ([automata_awmgr_auto.cpp](../src/core/automata_awmgr_auto.cpp)). The vehicle and pedestrian managers keep most of these slots in their vtables, and parts v2 and v3 call AddAutomaton, RemoveAutomaton, GetRandomCreationPoint and slots 55, 56 and 63 at their game addresses, so all four managers reach these detours when the groups are on.

- **`simulators.automata.awmgr.create` (14):**
  - AddAutomaton (slot 7) 0x0050F790 and RemoveAutomaton (8) 0x0050C990.
  - GetRandomCreationPoint (16) 0x00506DC0.
  - The exemplar choices: slot 25 0x0050BE90 (by group, bit and cell), 26 0x0050C0D0 (by masks), 27 0x0050C1D0 (by bit and value).
  - GetInstancesOfGroup (32) 0x00510040.
  - Slot 40 0x00506600 (slot 39's undoing), 43 0x00507B80 (a message posted), 45 0x00511990 (every automaton removed), 46 0x00510690 (an automaton made), 47 0x0050A740 (room by priority), 65 0x0050F930 (the source building table filled) and 67 0x0050F9C0 (an exemplar record read).
- **`simulators.automata.awmgr.dispatch` (12):** TranslateDispatchLocation's two forms (19 0x00507120, a point; 20 0x00506F50, cells), ChaseAutomaton (21) 0x00511260 and its pair form (22) 0x005072E0, slot 41 0x00507AC0 (in view), 42 0x0050C8B0 (the kind rate), 53 0x0050C6A0, 54 0x00506D50 and 55 0x0050C7E0 (occupants' source buildings), 59 0x0050F690 (an automaton tracked), 61 0x005073D0 and 62 0x00507430 (shown and hidden).
- **`simulators.automata.awmgr.frame` (10):** SetCameraFollow (34) 0x0050A930, slots 48 0x00506CD0 (the view rectangle), 49 0x00505350, 50 0x0050A5D0 (zoom and filter), 56 0x0050F2C0 (after a load), 57 0x0050A670, 60 0x0050A520 (active by zoom), the frame update (63) 0x0050A7B0 with its rate 0x00507BF0, and the camera follow's update (64) 0x00507470.

What they do:

- **The list.** AddAutomaton references the automaton, runs its slot 3, appends it and puts it in the set of every tracked group (+0xA50) its occupant is in; then slot 61 and its activity by the automata filter of 0x00B43DC4 or +0x4C. RemoveAutomaton with its byte takes it out of the sets and the list, ends the camera follow for the followed one and releases it; without the byte it only marks it (its slots 102, 89, 33 and its +0x18C part).
- **Creation.** Slot 46 refuses at priority 0 while inactive, and above the most (+0x3C) unless slot 47 made room, the flags have 0x80 or the priority is 0xFF; the priority becomes 0xFE when the city's slot 0xC4 object holds the group and is below its most. The record (slot 44 of the start, else slot 25), its automata group and the group's class (else +0xA48) are made by the COM, then given the record's model, groups (property 0xAA1DD396) and flags. GetRandomCreationPoint draws a distance between the least and the most (at least the least, at most the city's span) and a direction made a unit; the height is the terrain's without flag 0x40000.
- **The exemplar choices** draw from the manager's generator (+0x50) among the group's records of the exemplar map whose masks fit; slot 25 also by the cells object's masks at a cell, slot 27 by the record's +0x38.
- **The chase.** Slot 21 gathers the group's automata (slot 32) and lets the nearest within the radius, or all of them, chase the target through slot 22, which puts the target into the chaser's controller (+0x28) and gives the target the chaser's message target.
- **The source building table** (+0xA64, part aw2's save) gets its classes from the script system (slot 65) and the exemplar records (slot 67); slots 53 and 54 count a building's groups up, slot 55 counts an occupant's down, slot 56 recounts them after an old save.
- **Frames.** The rate 0x00507BF0 sets 0x00B09964 by the simulation speed (or the U-Drive), 0 while paused. The frame update removes automata by their state (at once or not), runs their slot 72 and removes those out of view. Slots 48, 50 and 60 follow the view: its rectangle, the zoom against +0xA3C and the activity.
- **The camera follow.** SetCameraFollow keeps the automaton (0x00B21B8C), centres the camera on it and runs slot 64, which moves the camera's centre and centre offset after it each frame, pushed by the screen edges it is near.

Kept as the game's: the SC4UIScale probe site CreateAutomaton (slot 15, 0x00505370); the hash set, map, tree, vector and list helpers, the random draws, the camera's helpers (0x007F7E50, 0x007F66C0, 0x007F79B0 and the camera control's), the property getters, the script number, the group templates' queries, the automaton's 0x00510360 and the allocators, all called at their addresses. Slot 46 and GetRandomCreationPoint, which part v3 left to the game for the vehicle managers, are ours here. No NAM or SC4UIScale site is among the 36.

Oddities kept:

- AddAutomaton tests the node's +8 for null, never the node.
- GetRandomCreationPoint's distance is ((r x most) - least) + least. The direction's x is used unrounded, y and z after their rounding to floats. y is 0 without flag 0x40000, else an unscaled draw in [0, 1).
- Slot 25 tests its cell for -1 and up (signed) and, without a cell filter or a bit, draws straight from the map's vector. Slot 27's value 0 takes every record.
- Slot 21 with all within the radius answers 1 when any chased, not slot 22's answers. Its distances must be strictly below the radius's square (rounded to a float); of equal nearest ones the first wins. Slot 22 answers 1 once the controller is there, whether the message target's query worked or not.
- Slot 46 passes the caller's whole word of its priority byte to the new automaton's slot 64.
- Slot 47 answers 0 when the count is not below the share (+0x40).
- Slot 56 keeps one set of IDs for all the occupants it recounts, so each occupant's groups are counted again for every occupant after it.
- The frame update takes the automata filter and never uses it.
- Slot 67's first three masks become all ones at a zero byte; the fourth shifts by the byte itself (the others by the byte less one).
- The camera update reads its zoom share from a table of six on the stack by the zoom. A zoom outside 0..5 would read other stack slots in the game; ours logs a first-call line and takes 0. None was logged.
- Slots 59 and 65 answer nothing; slot 56 answers slot 48's answer.

Floats: 53-bit doubles, floats stored where the game stores them, FST's unrounded value kept as a double. The cell of a point (slot 20) and the camera's zoom level are the game's floor ((a - 0.5) + 1.5 x 2^23 as a float, its bits less 0x4B400000, plus one when that is a); the camera's pixel counts round to nearest the same way. The kind rate reads the clock's two words unsigned (FILD plus 2^32). Compares keep the asm's handling of equal and unordered values. Return values: AL for the bools; slot 32 0 or 1 in EAX; pointers and calls' answers in full.

Reimplemented like the earlier parts: one writer, then an independent audit of all 36 against the disassembly. The audit found no defect. Two of its notes were taken for exactness although neither can be seen: slot 54's compare is now the asm's own (it differed only for a NaN threshold), and slot 56's occupant loop ends at the list head as read after the occupant manager's call. Before the audit, the writer's own review caught one misreading: `test ah,5 / jp` jumps for equal values too, so those compares (GetRandomCreationPoint, the chase, the camera's blend) are strict.

### Test

`OPENSC4_TEST_AUTOMATA_AWMGR=3` does everything `=2` does, plus the following, into the fingerprint as ` D<calls>,<choices>,<points>,<sources>,<camera>,<flags>` (header of [automata_autotest.cpp](../src/core/automata_autotest.cpp)):

- **On a bare common base** (0x005125E0, the base's vtable), lent the city's vehicle manager's cells object and script system, with fake automata of the test's own. Every slot the base calls on a fake goes into the hash with its value arguments; a pointer argument counts as the fake it is. The fakes' occupants give a property holder without properties, which the automata filter of 0x00B43DC4 asks for.
  - Two tracked groups; AddAutomaton with none and with three; slot 59.
  - GetInstancesOfGroup for a tracked group, a list group and with a fake flagged 1, each set released by slot 33.
  - The chase: slot 22's six refusals, then slot 21 for the nearest and for all.
  - Slots 61 and 62 in each state; slot 47 under two most counts; slots 57, 50, 60, 48 and 49.
  - Slots 19 and 20 for each kind of location.
  - Slot 65 and the source building counts of slots 53, 54 and 55 with a building occupant of the city.
  - Slots 39 and 40 (the counter stays above 0); slot 43 with a message nobody takes; slot 46 refusing twice.
  - The frame update through its branches, and paused.
  - SetCameraFollow and three camera updates as the fake moves (the camera's centre and the follow's globals into the token; the camera put back after), then the follow ended.
  - Slot 56 after a load and for an old save; slot 45; the base deleted.
- **On the city's four managers**, the generator seeded for the calls and put back: the exemplar choices for every group of the exemplar map, the default and a group not there (as record indexes), and GetRandomCreationPoint at four places. To the log only: the kind rates (they read the clock) and slot 41.
- **Slot 46 making one** on the city's vehicle manager (priority 0xFF): its class into the token, then the automaton shut down and released.

The flags: 1 the automata filter is there (always, in the harness), 0x20 no network occupant in the controller table (so slot 55's network branch does not run), others for a missing view or building, a cheat object or a followed automaton (which skip parts), or a fake called at a slot it does not know (none).

The fakes are put at an address that is 0 modulo a hash set's bucket count (flag 0x100 when the count is not known). The tracked groups' sets and GetInstancesOfGroup's are keyed by the fakes' addresses, and the base calls the fakes in a set's order (the chase, slot 33's releases), so before this ` D`'s first field followed where the DLL was loaded: two or three values per city on both sides.

Trial of the fix (2026-10-22, TurboToaster, the part aw5 trial's env and switches, `OPENSC4_TEST_AUTOMATA_AWMGR=5`, 26 pairs: Rush Hour t0000 seven, Making Money d0000, Rush Hour d0090 and Big City d1095 three each, the 12-save batch): ` D` took one value per save in all 52 runs, the same on both sides, flags never 0x100 (193 buckets). The fakes' address changed run to run. Rush Hour t0000 gave 7CAD6A06 in all 14 runs. Every save was the same day by day except one Rush Hour d0090 pair, whose first difference was part aw1's ` A` (its known two outcomes).

Reached (our first-call log lines, trial Making Money d0000, 4 days): all 36. By the game alone, 17, several of them only with empty lists: GetInstancesOfGroup, slots 40, 45, 48, 49, 50, 54, 56, 57, 60, 62, 65, 67, SetCameraFollow, the rate, the frame update and the camera update. The rest by the test.

### Parity

2026-10-20, B = `simulators.automata.awmgr`, the four part aw2 groups and the three part aw3 groups against the original (the switch list includes `determinism.dispatch_info=ours`; `OPENSC4_MUTE=1` on both sides; two jobs, `harness.multi_instance=ours`), env p4env-aw3, snapshot aw3-r1, with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_AWMGR=3`. All the same:

| Run | Days | Result |
| --- | ---: | --- |
| Making Money Tutorial d0000 | 4 | 5 of 5 days |
| Rush Hour Tutorial t0000 | 7 | 8 of 8 days |
| The 12-save batch | 7 | 12 of 12 saves |

Rush Hour t0000, Big City d1095 and Rush Hour d0090 needed more pairs: the original itself had two outcomes there, and each of B's outcomes was one of them. Every B run (18) carried the aw3 token (then ` C`; renamed ` D` when part c1 took ` C`) and reached all 36 functions. In two saves the controller table had a network occupant (flags 1), so slot 55's network branch ran there too.

## Part aw4: the aircraft manager's behaviour and the watercraft manager's life cycle, save and messages

### What is ours

35 functions in four more groups, off by default. These are the two managers' own slots: each calls the common base's function of its slot at the game's address, so with parts aw2 and aw3 on as well the whole chain is ours.

- **`simulators.automata.awmgr.air` (11), in [automata_awmgr_air.cpp](../src/core/automata_awmgr_air.cpp):**
  - AddAutomaton (slot 7) 0x004FA6D0, Init (3) 0x004FA9E0, Shutdown (4) 0x004FB7D0.
  - HandleTuningParametersChanged (24) 0x004FAA50.
  - The two dispatch slots 17 (0x004FAAF0) and 18 (0x004FC440).
  - Write 0x004FB5B0 and Read 0x004FBE70 (the serializable part's slots 3 and 4).
  - DoMessage 0x004FC710 (the message target's slot 3), with the occupant removed handler 0x004FB8D0 and its list find 0x004FB2B0.
- **`simulators.automata.awmgr.water.life` (5), in [automata_awmgr_water.cpp](../src/core/automata_awmgr_water.cpp):** Init 0x00567B90, Shutdown 0x005744A0, the tuning (24) 0x00567BF0, AddAutomaton 0x0056F7C0, RemoveAutomaton (8) 0x0056F800.
- **`simulators.automata.awmgr.water.serial` (8):**
  - Write 0x0056FA70 and Read 0x005749E0.
  - The pair insert 0x0056C850 and the route insert without a resize 0x00574260.
  - The record copies 0x00570D10 (into new storage) and 0x00570D60 (assigned), the ferry route's assignment 0x0056D7F0, and the records' destroy 0x0056F740 (which part aw1 called at its address).
- **`simulators.automata.awmgr.water.messages` (11):**
  - DoMessage 0x00575500, the occupant's addition 0x00575170 and its removal 0x00574520.
  - The route table's erase 0x00574340 and node release 0x00574230, the route value's destructor 0x005732F0, the records' clear 0x005734E0, the route pair's constructor 0x00573520 and the route insert with its resize 0x00574830.
  - The pair tables' erase 0x0056D6E0 and node release 0x0056C910.

A helper that two groups call belongs to the group of its first user, in the order life, serial, messages.

What they do:

- **The aircraft manager.**
  - **Init** runs once. It calls the common base's Init, keeps an occupant filter of its own at +0xA98, puts the generator's address (+0x50) into 0x00B21AF4 and runs slot 24.
  - **Shutdown** releases the map's occupants and aircraft (+0xA88), frees the list of records at +0xA94 and releases the filter. Then it calls the common base's Shutdown and clears 0x00B21AF4.
  - **The tuning** sets the most automata from 0x00B21BC8 and the least zoom from property 0x4998C91A, less one.
  - **AddAutomaton** without an automaton makes one: a helicopter (0x0053C6E0) when the draw is below 0x7FFFFFFF, else the 0x200-byte aircraft of 0x00523F60.
  - **Slots 17 and 18** send an aircraft 200 above the terrain at a dispatch location (slot 19), with a rate at +0x1E4 from the police simulator or the service 0x00B43D78, then its slot 52.
    - Slot 17 takes the aircraft it is given.
    - Slot 18 finds the aircraft in the map whose occupant is on the source's lot.
  - **The save** is version 5. After the common base's part come the map's references and the list's points and bytes.
  - **DoMessage** sends an occupant inserted to SC4UIScale's 0x004FBFE0 and an occupant removed to 0x004FB8D0, then always calls the common base's DoMessage. 0x004FB8D0 erases the occupant's map entry, and unlinks its list record of the byte property 0x8BFB1072 at its position.
- **The watercraft manager.**
  - **Init** runs once. It calls the common base's Init, subscribes to message 0x2C06BA82 (which sets +0xA88), puts the generator's address into 0x00B22A78, then runs slot 45 after a load, slot 23 with 500 and slot 24.
  - **Shutdown** undoes Init, and clears the three tables and the reference vector with part aw1's clears.
  - **The tuning** sets the most automata from 0x00B21BF0 and the least zoom from property 0x4998C91B, less one.
  - **AddAutomaton** without an automaton makes a boat (0xDC8 bytes, 0x0056D9A0).
  - **RemoveAutomaton** takes the automaton out of the place that holds it, by kind or by group, then calls the common base's RemoveAutomaton. The places are the route records (kinds 0x4400 and 0x4402), the second pair table (groups 0x4416, 0x4411, 0x440F and 0x440A) and the reference vector (group 0x4418).
  - **The tables.**
    - The routes (+0xA8C) are keyed by a dock's lot. Each holds a vector of 0x3C-byte records (an automaton and a ferry route in use) and the dock occupant at +0x1C.
    - The first pair table (+0xAA0) maps a lot to an occupant of the type 0x8C392D39.
    - The second pair table (+0xAB4) holds the occupants of ten terminal types.
  - **The occupant handlers.** The occupant's addition fills these tables by the occupant's type. A route is made only when the dock search (0x00568CE0, part aw5) answers. The occupant's removal empties them; a route's automata are stopped (slot 52) before its records go.
  - **The save** is version 6: the common base's part, then the three tables.

Kept as the game's:

- SC4UIScale's probe site 0x004FBFE0 (the aircraft manager's occupant inserted handler).
- The common base's functions.
- The helicopter's, the other aircraft's and the boat's constructors.
- Part aw5's lot query 0x00567C90, dock search 0x00568CE0, record vector copy 0x00572360 and record insert 0x00573580.
- Part aw1's table clears.
- The map, tree, hash table, vector, reference and stream helpers, the property getter and the allocators, all called at their addresses.

No NAM site is among the 35.

Oddities kept:

- **The aircraft manager.**
  - Init references the filter without testing the allocation. Shutdown releases the map's objects and the filter without null tests, and frees the list's nodes without destroying their records.
  - Slot 17 picks the police simulator or the service by whether the aircraft's occupant is in the group 0x4301. Slot 18 picks by the police simulator's own test of the source.
  - Slot 18 never reads its first argument, and takes the first map entry, in key order, whose occupant is on the lot.
  - DoMessage answers 1 for an occupant inserted whatever its type.
  - Write and Read answer 1 when the stream has no error, not whether the common base's part worked.
  - Read takes only version 5 as its own. A later version answers AL 0; an earlier one goes to the common base's body alone.
  - The occupant removed handler releases the occupant (the map's reference) and then still reads its property and its position.
  - The list find's tolerance test is strict: |a - b| < 0.0001, and false when unordered.
- **The watercraft manager.**
  - Read keeps no reference of those it reads: the tables take references of their own.
  - Write saves neither a record's automaton nor the route value's +8 and +0xC.
  - Write saves a record's ferry route word +0xC as 0, 1, or -1 for any other value. Read keeps the slot's earlier value for anything but 0 and 1, so a 2 comes back as the record before it.
  - RemoveAutomaton drops the first place found and releases the vector's last element after the shift, although that element is still in the vector.
  - The occupant's addition of the type 0x8C392D39 runs the dock search and never reads its answer.
  - DoMessage null-tests the occupant of an addition but not of a removal.
- **Both.** The common base's DoMessage does not test the occupant of 0x99EF1142 or 0x99EF1143, so the managers' own null tests never help for those messages.

Floats: 53-bit doubles. The dispatch height is the terrain's answer plus the float 200, one sum rounded to the float the game stores. The list find compares the unrounded difference.

Stack leftovers:

- Both Reads read a word version into a dword slot, so the upper half is the caller's stack in the game and 0 in ours.
- The aircraft list records' byte slot, and the watercraft records' ferry route slots, are read into before anything sets them. Both carry over from one record to the next, as in the game; the first time they are leftovers in the game and 0 in ours.
- The counts are read into one slot, so a failed count read keeps the earlier count, as in the game.

Return values: AL for Init, Shutdown and the tunings. EAX 0 or 1 for Write and Read. The common base's EAX where a slot ends in its call. The dispatch slots answer the aircraft or 0.

Reimplemented with two writers (the aircraft and the watercraft halves) and an independent audit of each against the disassembly. Neither audit found a defect.

### Test

`OPENSC4_TEST_AUTOMATA_AWMGR=4` does everything `=3` does, plus the following, into the fingerprint as ` E<aircraft>,<watercraft>,<flags>` (header of [automata_autotest.cpp](../src/core/automata_autotest.cpp)). It uses part aw3's fake automata, with two more slots in a vtable of its own (10, the point it is sent to; 67, a kind block) and a property holder that can answer a type and the byte property 0x8BFB1072.

- **A new aircraft manager from its factory:**
  - Init twice and slot 24.
  - A Write; a Read of those bytes with two list records (Write must give them back); reads of versions 6 and 4, and of a stream cut inside a record.
  - Map entries of the test's own: fakes, and the first building occupant of the controller table.
  - DoMessage: an occupant inserted and removed of the type 0x029244DB; inserted of another type (0x004FBFE0, which finds no property and ends); removed twice (its entry and list record, then nothing); a type no one handles.
  - Slot 17: no automaton, a refused location, a point, no object, and the building with the occupant in the group 0x4301 and not.
  - Slot 18: a point, no object, and the building with a refused location, then a good one, then after its entry is removed.
  - The list find on nodes of the test's own.
  - AddAutomaton with a fake, then with none until both a helicopter and the other aircraft are made, each removed again.
  - Shutdown twice, then the release.
- **The city's watercraft manager:** a Write; its tables' sizes go into the token and the bytes to the log.
- **A new watercraft manager:**
  - Init twice and slot 24.
  - A route of the test's own with three records (ferry route words 0, 1 and 2), a second pair table node and the reference vector.
  - A Write, a Read of those bytes and a Write again; reads of versions 7 and 5.
  - RemoveAutomaton by each kind and group (the vector's middle, then its end), and with none.
  - The occupant handlers without an occupant, called at their addresses.
  - DoMessage with 0x2C06BA82, and with occupants added and removed of the terminal, dock and pair types.
  - On tables of the test's own with one bucket: the erases of a node behind another, which reach the node releases. On a value of the test's own: the route pair's constructor and the records' clear.
  - AddAutomaton with a fake, then with none: the boat is left to Shutdown, because the game's RemoveAutomaton reads a block that a new boat does not have.
  - Shutdown twice, then the release.

The generator globals 0x00B21AF4 and 0x00B22A78 are put back after each part, and the city's managers are not changed. The flags bits are:

- no building occupant;
- a missing manager or missing bytes;
- a round trip that did not come back the same;
- a fake called at a slot it does not know.

The occupant's addition does not reach its dock branch with a fake, because the dock search asks the occupant for an interface it does not have. The route pair's constructor and the records' clear are reached directly instead.

Three things the first trials found, which the test now avoids:

- The common base's DoMessage reads the occupant of 0x99EF1142 without a null test.
- The watercraft manager's RemoveAutomaton reads its slot 67's block on a new boat that does not have one.
- A version 5 stream that ends after the automata count gives the original two outcomes. The common base's body reads the source building table's count from the failed stream into a slot that holds the stream's address. It then adds a record whose value is a stack leftover (part aw2's oddity), which is sometimes 1. The test's stream now carries a count of 0.

Part aw4's fakes have a copy of the fakes' vtable with slots 10 and 67 filled. Part aw3's keep their traps there, so its ` D` token is not changed: the base calls slot 10 on its fakes.

Reached (our first-call log lines, trial Making Money d0000, 4 days): all 35.

### Parity

2026-10-21, on the branch with main merged in. B was these groups, all on, against the original:

- `simulators.automata.awmgr`;
- the four part aw2 groups and the three part aw3 groups;
- the four part aw4 groups.

The switch list includes `determinism.dispatch_info=ours`, with `OPENSC4_MUTE=1` on both sides and two jobs (`harness.multi_instance=ours`). Env p4env-aw4, snapshot aw4-r3, with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_AWMGR=4`.

| Run | Days | Result |
| --- | ---: | --- |
| Making Money Tutorial d0000 | 4 | same (5 of 5 days; the original had two outcomes) |
| Rush Hour Tutorial t0000 | 7 | 8 of 8 days |
| The 12-save batch | 7 | 10 same; 2 partly, each difference one the original shows too |

Every one of the 13 saves gave a single ` E` value in all its A and B runs, with flags 0, and all 24 B runs reached all 35 functions. Every other difference was one of three kinds:

- **Part aw3's ` D` token.** Its first field (the fakes' calls) takes one of two values per city on both sides, as part aw3's own runs showed: Making Money 4848E1FD or E75BEE79, Big City 5C81BE01 or 503D8DAD. These account for Making Money d0000 (the 4-day leg), Big City d0000, Making Money d0090 and Making Money d1095, which were the same after two pairs.
- **Big City d0090 (partly).** Besides the ` D` values, the vehicle manager's field of part aw1's ` A` token took two values, C59EB66B and 8B190717. Both came from the original.
- **Big City d0365 (partly).** Besides the ` D` values, one B run had other `advisors` values from the first day. That is the first-run advisor outcome of [parity.md](parity.md), which shows before the test day.

An earlier run (r1, stopped) showed the original giving two values of ` E` on Making Money d0000 and Rush Hour t0000. That was the test's version 5 stream (above), not our code; after the fix (r2), Making Money and Rush Hour were the same on the first pair.

## Part aw5: the watercraft manager's creation, routes and frames

### What is ours

23 functions in three more groups, off by default. With them and parts aw1 to aw4 on, the watercraft manager is ours, except for its kept helpers and the common base's SC4UIScale probe site.

- **`simulators.automata.awmgr.water.create` (6), in [automata_awmgr_water_route.cpp](../src/core/automata_awmgr_water_route.cpp):**
  - CreateAutomaton (slot 15) 0x00568F30 and GetRandomCreationPoint (16) 0x0056C990.
  - The lot at an occupant 0x00567C90, a lot's water body 0x00567CE0, the dock search 0x00568CE0 and the open water test 0x00568FD0. Part aw4 called the first and the third at their addresses.
- **`simulators.automata.awmgr.water.routes` (7), in the same file:**
  - A terminal's boat 0x005710A0, with the path group test 0x00566DA0 and the boat's path 0x0056F510.
  - A route record's two legs 0x0056FD60 and its new ferry 0x00570DF0.
  - The boat's two legs 0x0056F670 (`cSC4Watercraft::SetFerryRoute`: its `this` is the boat) and a leg's constructor 0x0056C940.
- **`simulators.automata.awmgr.water.frame` (10), in [automata_awmgr_water_frame.cpp](../src/core/automata_awmgr_water_frame.cpp):**
  - The frame update (slot 63) 0x005736A0.
  - The crossing rank 0x00566E60, the boat's step direction 0x005684C0 and its path cut 0x0056D5F0.
  - The edge spawn 0x005723D0 and its edge cells 0x00571630.
  - The route record vector's fill 0x00570CC0, copy constructor 0x00572360, erase 0x00573230 and growing insert 0x00573580. Part aw4 called the copy and the insert at their addresses.

What they do:

- **Creation.**
  - **CreateAutomaton** refuses (null) when a boat is near the point (0x00568C10) or the city's slot 0x18C refuses its cell. Otherwise it calls the common base's CreateAutomaton 0x00505370 with the seventh argument 0.
  - **GetRandomCreationPoint** needs the group (slot 30). When there is a lot at the position with a route (its dock) or a pair of the first pair table, it tries the cell twice the dock's facing out from the dock. Otherwise it walks a spiral over the city's cells for open water of the lot's water body, else the first open water cell. The city's slot 0x188 must take the cell. The point is the cell's centre at the sea level.
  - **The dock search** reads the occupant's dock interface (0xE9793A65): its orientation picks a facing, and its model's box an extent. The point moves half the extent along the facing.
  - **Open water** is the 0x00B43CF0 service's answer for the cell, with no automaton of the list nearer to its centre than its radius.
- **Routes.**
  - **A terminal's boat** (0x005710A0) makes a boat of the terminal's kind beside the dock. A ferry kind (the path groups 0x440F, 0x440A, 0x4411) is given a wait (30000 x (1 + a draw)) and a water path (0x00B43DA8's slot 0x84) to the first object of the 0x00B43D8C service's list 9 that has one. Other boats wait 60000.
  - **A route record's legs** (0x0056FD60): the first from the route's dock, the second from the destination route's dock. When the ferry route walks, the second comes from its first open water step and the one after. **A new ferry** (0x00570DF0) makes a boat of the kind 0x4400 or 0x4402, places it at the leg its byte names, gives it both legs and flips the byte.
- **Frames.** The frame update:
  - Every 60000 it lets go of the routes' boats that no record holds.
  - Out of the U-Drive, it cuts the path of one of two boats whose paths cross. It cuts the boat of the higher crossing rank, the second on a tie. Two boats of rank 0 stop the first.
  - It runs the edge spawn. While the edge spawn's boats are 5 or fewer and the first pair table is not empty, every 10000 it draws for a boat at the next map edge (0.1, or 0.05 in the U-Drive, where a second draw above 0.5 makes the kind 0x4417).
  - After message 0x2C06BA82 it fits the routes' records to the city's ferry routes (the city's slot 0x114).
  - Every 35000 it gives the records without a boat a new ferry.
  - It plans the terminals without a boat (0x4411, 0x4419 or 0x4416 by their type), then calls the common base's slot 63.

Kept as the game's:

- the common base's CreateAutomaton 0x00505370 (SC4UIScale's probe site), frame rate and slot 63;
- the boat near a point 0x00568C10, and the boat's leg assignment 0x0056D920, copy 0x0056D8B0 and begin 0x0056F3F0;
- part aw4's record helpers;
- the ferry route's copy constructor and list clear;
- the spiral walk, the route iterator, the hash table, vector, reference and byte vector helpers, the random draw and the allocators, all called at their addresses.

No NAM site, and no SC4UIScale site other than 0x00505370, is among the 23.

Oddities kept:

- CreateAutomaton writes the point's z cell over its point argument's slot and hands the common base a seventh argument of 0 whatever the caller gave.
- GetRandomCreationPoint reads neither its most nor its flags and makes no draw. Its seventh argument takes the dock's facing when not null. The spiral's least distance applies only without a lot.
- The dock search's interface query writes into the occupant's argument slot. Its orientation indexes a table of four on the stack. Another value would read other stack slots in the game; ours logs a first-call line and takes 0. Every game caller passes the turned flag 0, so the turned offset never runs.
- 0x00567CE0 answers AX 0 without a lot; the game keeps EAX's top half. Its one caller compares the word.
- The frame update's crossing check skips each path's last point. Two boats of rank 0 stop the first boat again at every shared cell.
- The step direction takes an index below the count less two, unsigned, so a path of one point takes any index and reads past the path. The path cut tests its index signed, then unsigned.
- The record fill advances its destination even when it is null, so a count above 1 at a null place writes at 0x3C.
- The edge spawn's timer starts at 10001 and is reset only when a boat is made. Its count test is unsigned. The edge cells compute the middle cell's point and never use it.
- The ferry route sweep erases every record when the city has no ferry routes.
- The record vectors divide by 0x3C signed. The growing insert grows to size + max(size, count), the max unsigned.

Floats: 53-bit doubles, floats stored where the game stores them, the products added before a store kept unrounded or rounded per site as the asm does. The timers wait while strictly below their periods (equal and unordered run); the draws go on unless strictly above. The game's constants are read where it reads them.

Stack leftovers:

- The legs 0x00570DF0 makes on its stack, and the frame update's two ferry routes, keep the bytes 0x0056FD60 does not write: +0x28, +0x2C and +0x30 on some paths, and everything past 0x0056C940's 0x28 bytes. These reach the boat's +0x218 through 0x0056F670. In the game they are the stack's; in ours, 0. The frame update keeps them from one list entry to the next, as the game does.
- The by-value copies' padding is 0 in ours.
- 0x005710A0's first object's x cell slot is a leftover in the game (0 in ours); its z slot starts as the wait's bits, as in the game.
- Argument slots the game reuses after their last read are locals in ours. No caller reads them back.

Return values: the bools AL as the game sets it. The frame update answers the common base's slot 63's EAX, which no caller reads. The crossing rank, the step direction and the path group test answer EAX in full; the path cut AL over the index or over its copy's answer; the vector helpers their pointers.

Random: the terminal's boat and the edge spawn draw from the manager's generator (0x00B22A78) at the game's address. None of the 23 seeds a generator.

Reimplemented with two writers (creation and routes; frames) and an independent audit of each against the disassembly, instruction by instruction and against our object's stack layout. Neither audit found a defect. On the audits' notes, 0x0056F670's entry became `cSC4Watercraft::SetFerryRoute`, and the comments now name the dock search's orientation and the copies' padding.

### Test

`OPENSC4_TEST_AUTOMATA_AWMGR=5` does everything `=4` does, plus the following, into the fingerprint as ` W<create>,<routes>,<frame>,<flags>` (header of [automata_autotest.cpp](../src/core/automata_autotest.cpp)).

It uses fake boats: part aw4's fakes in a 0x400-byte block, with slots 16, 17 (the radius), 35 and 59. The boats' legs cover the fakes' state slot at +0x1F0, so their state is kept in a side table. Their occupants can answer a dock interface of the test's own: an orientation, a model and its box.

- **The leaf helpers on data of the test's own:**
  - the leg's constructor;
  - the crossing rank and the path group test for each group;
  - the step direction along a path of the eight unit steps and a longer one, and the path cut;
  - the record vector's copy, fill, erase and growing insert (empty, inside a full vector with one and three, at the end);
  - the city's edge cells for sides 0 to 3, 5 and -1.
- **The city's watercraft manager, queries only:** the lot and water body at the controller table's first building, the dock search (none, without the interface, the building, the interface at four orientations), open water at four cells, and the creation point at three places for three groups.
- **A new watercraft manager from the factory**, its generator seeded, its vtable a copy whose slot 15 answers fake boats (or none), so that no game boat is made:
  - two routes, a pair and a terminal of the test's own;
  - CreateAutomaton refused near a listed boat and outside the city;
  - the creation point at the building's lot with a route of it (erased again: the lot's heap address would set the table's order);
  - the boat's path and legs;
  - a record's legs, a new ferry and a terminal's boat, each also with a refused creation;
  - the edge spawn until a boat is made or 40 calls;
  - the frame update with two listed boats whose paths cross and both timers due, then with the city's ferry routes asked.
  - Then the game's vtable back, Shutdown and the release.

The globals the part changes are put back. The flags: no building, no city watercraft manager, no new manager, a route not made, and a fake boat or a made one called at a slot it does not know.

What the trials found:

- The record fill at a null place with a count of 2 writes its second record at 0x3C, as the game does (the first trial ended in the game). The test fills one there.
- The frame update's answer is the common base's slot 63's EAX: the game leaves 3 there and part aw3's frame group another value. No caller reads it; the test leaves it out, as part aw3's does.
- The route keyed by the building's lot made the route table's order follow the heap, so the routes' and frame's hashes took two values per side. The test erases that route after use.

Reached (our first-call log lines, trial Making Money d0000, 4 days): all 23. The frame update and the edge spawn are the game's on every frame; the rest are reached by the test.

### Parity

2026-10-21, B was these groups, all on, against the original:

- `simulators.automata.awmgr`;
- the four part aw2 groups, the three part aw3 groups and the four part aw4 groups;
- the three part aw5 groups.

The switch list includes `determinism.dispatch_info=ours`, with `OPENSC4_MUTE=1` on both sides and two jobs (`harness.multi_instance=ours`). Env p4env-aw5, snapshot aw5-r1, with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_AWMGR=5`.

| Run | Days | Result |
| --- | ---: | --- |
| Making Money Tutorial d0000 | 4 | 5 of 5 days |
| Rush Hour Tutorial t0000 | 7 | 8 of 8 days |
| The 12-save batch | 7 | 11 same (Big City d0090 after three pairs, Rush Hour d1095 on a second leg); Rush Hour d0090 partly, each difference one the original shows too |

All 27 B runs reached all 23 functions and carried the ` W` token, with flags 0. Its routes and frame fields take one of two values per run, on both sides: 50B695CD,0421E12D or 61F645AF,E92A3039.

- Both sides gave both values on Rush Hour d0090 and d1095.
- Some saves showed only one value in their runs: Making Money d1095 and Rush Hour d0000 the second, the others the first.

That is the test's own two outcomes, like the ones the lot's route gave before it was erased (above). Routes A and B are keyed by fake boats 7 and 6, which were statics of the DLL, and a key's bucket is its address modulo the route table's bucket count (193), so the table's order, and with it the frame's, followed where the DLL was loaded.

Fixed 2026-10-22: the test makes its manager first and puts the boats at an address that is 0 modulo that count (flag 0x40 when the count is not known). Trial on TurboToaster, same env and switches, 39 pairs (Rush Hour d0090 and d1095 four legs each, Making Money d0000, Rush Hour t0000 seven pairs, the 12-save batch): ` W` took one value per save in all 78 runs, the same on both sides, flags 0, routes and frame 50B695CD,0421E12D (Rush Hour t0000 50B695CD,4A7E20CA). The boats' address changed run to run. Every first difference was another test's known two-outcome token (aw1 ` A`, aw3 ` D`). Part aw3's ` D` on Rush Hour t0000 gave 58C58226 in two of seven runs of ours, a value the original did not give there.

In the first leg, Rush Hour d0090 and d1095 differed in tokens that have two outcomes on the original: part aw3's ` D` (4848E1FD, E75BEE79 or ECD4CDDD, its three known values), part aw1's ` A` (the vehicle field, 5B07DDF3 or 919E9993, both from the original on d0090) and the ` W` above. In three pairs no single run matched as a whole line, because each token took its value independently. Every token value B gave was one the original gave on the same save or the same city. A second leg of the two saves (up to five pairs) found Rush Hour d1095 the same after two pairs. Rush Hour d0090 came out partly: four outcomes on each side, two of them shared.

## Part s: the script system

### Survey

cSC4AutomataScriptSystem (class ID 0x4A1B664E, factory 0x005148A0, constructor 0x005207A0, 0xB8 bytes, city slot 48, the city's GetAutomataScriptSystem at slot 0xC0) loads the automata Lua scripts and keeps what they define: the occupant groups, the controller templates, the drive commands and the track building groups. The layout is in [automata_scripts.h](../src/core/automata_scripts.h).

Four parts, each with its own vtable:

| Part | Vtable | What it is |
| --- | --- | --- |
| +0x00 | 0x00A96BA8 | cISC4AutomataScriptSystem, 19 slots. Each of the gzcom-dll header's overloaded pairs sits in the other order: slot 6 is GetAutomataGroup by ID, 7 by name, 11 GetOccupantGroup by ID, 12 by name. |
| +0x04 | 0x00A96B98 | the adjustors and a shared stub; Init adds this part to the message server |
| +0x08 | 0x00A96B88 | the adjustors and 0x0051C4B0, an ID test (0x87163754) |
| +0x0C | 0x00A96B70 | serializable: Write and Read are shared stubs, GetGZCLSID 0x00520050 |

The fields:

- +0x10 initialized, +0x14 the reference count, +0x18 an object Shutdown shuts down and releases, +0x1C a 0x2C-byte record that starts with an SC4String.
- Five hash maps: +0x4C name to occupant group, +0x60 name to automata group, +0x74 ID to occupant group, +0x8C name to controller template, +0xA4 key to drive command.
- Two lists: +0x88 the groups without an ID (the special filter groups), +0xA0 the track building groups.

The string maps hash with h * 5 + the signed char. The occupant group template (constructor 0x005206B0, 0x84 bytes, vtable 0x00A96C0C) derives from the occupant filter base (0x005EA080). It holds the parent group (+0xC), the ID (+0x10), an automaton class (+0x14) and a percentage (+0x2C, 1.0).

The class is found by its vtables, the constructor, the destructor and the group template's code, plus the helpers only they call: 83 functions. With the rest of the range 0x0051C2D0 to 0x00523F20 and the templates part a3 left to it (0x0051AB80, 0x0051B1F0 to 0x0051B310), that makes 125 functions and 32,001 bytes. The survey's 116 and 31,620 placed a few shared helpers elsewhere. Of the 125:

- **Stay the game's (21).**
  - The nine `sub ecx,N; jmp` adjustor thunks: 0x005200C0 to 0x00520110, 0x00521670, 0x00521680, 0x00521690.
  - Twelve shared helpers with callers outside the class, mostly STL hash map code: 0x004FC900, 0x0051C880, 0x0051C950, 0x0051C9B0, the string map's find 0x0051C9F0, 0x0051CA60, 0x0051CE70, 0x0051FC30, the insert 0x0051FDC0, 0x0051FE90, 0x0051FF00 and 0x00520970.
- **s1 (34, done): the object, the life cycle, the queries and the occupant groups.** Below.
- **s2 (43, 7,048 bytes, done): the controller templates, the Lua callbacks and the script data structures.** See "Part s2" below.
  - The templates: 0x0051AB80, 0x0051B1F0, 0x0051B240, 0x0051B280, 0x0051B2A0, 0x0051B2F0, 0x0051B310, the template factory by class 0x0051BDF0 and CreateControllerTemplate (slot 16) 0x00520AC0.
  - The seven Lua functions Init registers: create_automaton 0x0051D220, create_controller 0x0051D3C0, attach_controller 0x0051D510, attach_controller_group 0x0051D640, detach_controller 0x0051D750, detach_controller_group 0x0051D860 and get_source_building_count 0x0051C7D0.
  - Helpers the managers and the U-Drive call: 0x0051CD90, 0x0051D040, 0x0051D0D0, 0x0051D120, 0x005204D0, 0x0051FD30.
  - The behaviour records that the loading builds (the record, its vector and the std::sort over them): 0x00521350, 0x00521470, 0x00521550, 0x005215A0, 0x00521620, 0x00521B20, 0x00521B70, 0x00521BB0, 0x00522270, 0x00522350, 0x005223C0, 0x00522450, 0x00522540, 0x00522590, 0x005226C0, 0x00522720, 0x00522800, 0x005205D0, 0x00520940, 0x0051C8B0 and 0x0050BD40.
- **s3 (27, 19,089 bytes): the loading of the scripts**, in two pieces. 0x0051C4C0 stays the game's: it holds the Lua print patch site 0x0051C50B. The Lua VM's own functions are called at their addresses, not reimplemented.
  - **s3a (15, 9,964 bytes, done): the loader and the template readers.** See "Part s3a" below.
    - LoadScripts 0x00523B10 and the readers it calls first: 0x0051DAA0, 0x0051EEE0, 0x0051F5C0 (1,158 bytes), 0x0051FA60.
    - The attractor and generator templates: 0x00523500 (1,546) and 0x00522F30 (1,488), with their parts 0x0051C5D0, 0x0051CB40, 0x0051CBF0, 0x0051CD20, 0x0051F010, 0x0051EE10, 0x0051F2A0.
    - The behaviours 0x00522860 (1,733).
  - **s3b (11, 8,861 bytes, done): the group and drive parameter readers.** See "Part s3b" below.
    - The groups: 0x005216A0, 0x00520DF0 (1,316), 0x00521C70 (1,527), 0x00521800 (795), 0x0051FB90, 0x00520520, the models 0x0051ECA0.
    - The drive parameters: 0x0051DC20 (1,465), 0x0051E1E0, 0x0051E4A0, 0x0051E6B0 (1,520).

Mods: no NAM site and no SC4UIScale address is in the class. The only patch site is 0x0051C50B in 0x0051C4C0 (s3, kept).

### What is ours

34 functions in four groups, off by default ([automata_scripts.cpp](../src/core/automata_scripts.cpp)):

- **`simulators.automata.scripts.object` (7):** the factory 0x005148A0, the constructor 0x005207A0, the destructor body 0x0051FF70, QueryInterface 0x0051C2E0, Release 0x00521320, the serializable part's GetGZCLSID 0x00520050 and the +8 part's ID test 0x0051C4B0.
- **`simulators.automata.scripts.life` (5):**
  - Init 0x00523C80 with its default groups 0x0051D9B0.
  - Shutdown 0x005208A0 with the release of the templates 0x00520120 and of the groups 0x005201D0.
- **`simulators.automata.scripts.queries` (12):**
  - The map getters, slots 5, 8, 9, 10, 15 and 18: 0x00520060, 0x00520080, 0x00520090, 0x005200A0, 0x00520070, 0x005200B0.
  - The groups by ID and by name: slots 6, 7, 11 and 12 (0x0051C2D0, 0x0051CF60, 0x0051CFF0, 0x0051CFC0).
  - The drive command, slot 14 (0x0051D1D0).
  - The controller template by name, slot 17 (0x0051CF90).
- **`simulators.automata.scripts.groups` (10):**
  - CreateOccupantGroup, slot 13 (0x00520BD0).
  - The group template's constructor 0x005206B0, destructor body 0x00520410, deleting destructor 0x00520780 and record map clear 0x0051FC80.
  - Matches, its slot 3 (0x0051C3D0).
  - The parent chain queries that the managers call: IsInGroup 0x0051C380, GetClass 0x0051C3B0 and Depth 0x0051C4A0.
  - The name lookup 0x0051D970.

What they do:

- **Init**, once:
  - It adds the +4 part to the message server and makes the two default automata groups, "vehicle" (0x4002, cSC4Vehicle) and "pedestrian" (0x4001, cSC4Pedestrian).
  - It loads the scripts (0x00523B10, the game's) and registers the Lua table `automata` and its seven functions with the framework's script interface.
- **Shutdown** undoes Init. It destroys the controller templates and releases the groups of the three group maps and of the special filter list.
- **CreateOccupantGroup** puts a new group into the name map. A name already there is refused: the message is formatted and dropped, the group is destroyed and the answer is null. With the automata flag the group also goes into the automata group map. Then it goes into the ID map or, with ID 0, onto the special filter list. Each map or list holds one reference.
- **Matches** tests whether an occupant's group list holds the group's ID. For a percentage below 1.0 it also draws from a cRZRandom that is seeded from the clock. Ours seeds under `fixed_seed::SiteOverride(0x0051C461)`, so the seed counts at the game's site.

Kept as the game's, called at their addresses:

- The adjustor thunks, which reach our QueryInterface and Release, and the shared AddRef 0x00695330.
- The group base (0x005EA080, 0x005E95C0), the group's AddRef and Release (0x004BDF00, 0x007CB920) and its slots 4 and 5 (0x004FC900).
- The hash map helpers and the allocators.
- The scripts' loading, the Lua callbacks and the script registration functions (0x0068C180, 0x0058DE80, 0x0058DF10).

Oddities kept:

- QueryInterface's null branches write a null and still call AddRef through the object.
- Release at a count of 1 (or 0) deletes without counting down.
- CreateOccupantGroup writes the ID into the new group without a null test, and ignores the automata map's insert answer.
- AddDefaultGroups writes the class into the group it gets back without a test.
- Matches never destroys its cRZRandom.

Floats: only Matches. The percentage is compared with 1.0 and with the draw (cRZRandom::NextFloat's ST0, taken as a double) as the asm does. A NaN percentage matches without a draw, and a NaN draw does not match.

Reimplemented like the earlier parts: one writer, every call's argument count checked against the callee's `ret`, then an independent audit of all 34 against the disassembly. It found no defect; IsInGroup now reads its own ID before any test, as the asm does (ours had tested for null first), and some wording was fixed.

### Test

`OPENSC4_TEST_AUTOMATA_SCRIPTS=1`, with `OPENSC4_TEST_AUTOMATA=2` or more, queries the city's script system on the test day at the game's addresses, so the same test runs on both sides (header of [automata_autotest.cpp](../src/core/automata_autotest.cpp)). It goes into the fingerprint as ` S<identity>,<maps>,<groups>,<matches>,<fresh>`:

- QueryInterface for six interfaces, on the object and through the three adjustors (each AddRef is given back), GetGZCLSID and the ID test.
- The map getters; every name, ID, drive command and template through its lookup, plus keys that are not there; the two lists. These are summed, so the maps' order does not count.
- Per group: its fields and the group queries (Depth, GetClass, IsInGroup, FindName).
- Matches of up to 16 groups for up to 8 occupants of the controller table (no automata or props). For groups below 100 percent it seeds from the clock, the same on both sides.
- A fresh script system from the factory, with one reference:
  - three groups made by CreateOccupantGroup (with an ID and the automata flag, without an ID, and with an ID and the first as parent) and the first name again, which is refused;
  - the lookups, the group queries, and Matches of a 50-percent group so that it draws;
  - then ReleaseTemplates and ReleaseGroups, which run the groups' destructors, and its Release (Shutdown, not initialized, and the destructor body).

The city's script system is not changed.

### Parity

2026-10-19, B = the four `simulators.automata.scripts` groups against the original (the switch list includes `determinism.dispatch_info=ours`; two jobs, `harness.multi_instance=ours` on both sides), env p4env-s1, snapshot s1-r1, with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_SCRIPTS=1`. All the same, on the first pair:

| Run | Days | Result |
| --- | ---: | --- |
| Making Money Tutorial d0000 | 4 | 5 of 5 days |
| Rush Hour Tutorial t0000 | 7 | 8 of 8 days |
| The 12-save batch | 7 | 12 of 12 saves |

After the audit's change to IsInGroup (snapshot s1-r2) the batch was run again: Making Money 5 of 5, Rush Hour 8 of 8, and 11 of the 12 saves the same. In the twelfth, Rush Hour d0365, the B game ended at the end of its load: same seeds as r1 up to there, no further log line and no output. Three reruns of that save with the same build were the same, 8 of 8 days each.

All 34 functions were reached in every one of the 14 B runs of r1 (our first-call log lines), and every B run carried the ` S` token:

- By the game: the factory, the constructor, QueryInterface, Release, Init, the default groups, CreateOccupantGroup and the group constructor at the load; the queries and IsInGroup during the run.
- By the test: the rest, including the destructors, Shutdown and the two releases on the fresh object.

Not compared: Shutdown's initialized branch (the table unregistered, the +4 part taken off the message server, +0x18 released). The fresh object is never initialized, and the city's Shutdown, if it runs at the exit, comes after the last fingerprint; its first-call line is already taken by the test's Release.


## Part s2: the templates, the Lua functions, the helpers and the behaviour records

### What is ours

All 43 functions of s2's list, in four more groups, off by default, so part s1's groups stay as they were ([automata_scripts_2.cpp](../src/core/automata_scripts_2.cpp)):

- **`simulators.automata.scripts.templates` (9):**
  - The constructors: the template base 0x0051AB80, the attractor template 0x0051B1F0 (0x98 bytes, vtable 0x00A96134, with an SC4Curve at +0x88) and the generator template 0x0051B240 (0xB4 bytes, vtable 0x00A96138).
  - Their destructor bodies 0x0051B2A0 and 0x0051B310 and deleting destructors 0x0051B280 and 0x0051B2F0.
  - The factory by class 0x0051BDF0 and CreateControllerTemplate (slot 16) 0x00520AC0.
- **`simulators.automata.scripts.lua` (7):** the Lua functions create_automaton 0x0051D220, create_controller 0x0051D3C0, attach_controller 0x0051D510, attach_controller_group 0x0051D640, detach_controller 0x0051D750, detach_controller_group 0x0051D860 and get_source_building_count 0x0051C7D0.
- **`simulators.automata.scripts.helpers` (6):**
  - A group's cached ID list 0x0051CD90, which the managers call.
  - Its record lookup 0x005204D0, with the three-string record's constructor 0x0051D040, destructor 0x0051D0D0 and assignment 0x0051FD30. The U-Drive calls them.
  - The Lua value to ID 0x0051D120.
- **`simulators.automata.scripts.records` (21):** the behaviour record (0x7C bytes) and the code the loading runs on its vector.
  - The constructor 0x0050BD40, the copy 0x005205D0 and the assignment 0x00521470.
  - The vector's insert into a full buffer 0x00521350, and the uninitialized copy 0x00520940.
  - std::sort over the records: 0x00522800, the introsort loop 0x00522720, the median 0x0051C8B0 and the partition 0x00522270.
  - The insertion sorts 0x005226C0, 0x00522540, 0x00521B70, 0x005223C0 and 0x00521550, with copy and copy_backward 0x00521B20 and 0x00521620.
  - The heap fallback: partial_sort 0x00522590, make_heap 0x00522350, sort_heap 0x00522450, adjust_heap 0x00521BB0 and push_heap 0x005215A0.

What they do:

- **The templates.** CreateControllerTemplate asks the factory for a template of the class (attractor 0x6A1628BA or generator 0x4A1628C5) and puts it into the template map. A name already there is refused: the message is formatted and dropped and the template destroyed. The factory sets the class, the name and the template's curve, the points (0, 1) and (24, 1). Any other class gets the refusal message and null.
- **The Lua functions** live in the advisors' script context as `sc4game.automata` (`automata` there is a different table). Each takes the script system from its binding, then reads its arguments:
  - create_automaton makes an automaton of a class at a point (0x0050CDA0) and starts it.
  - create_controller asks the controller manager (part a1) for a controller of a template at a point (its slot 5).
  - attach_controller uses the same slot 5 with an occupant. attach_controller_group uses slot 10, detach_controller slot 8 and detach_controller_group slot 12.
  - get_source_building_count asks four services in turn.
  - The first three push the object they got back; get_source_building_count pushes a count.
- **The behaviour records** are sorted by a comparator inlined at every compare site: a is before b when its float (+4) is below b's, or else when its uint32 (+0xC) is above b's. That is not a strict weak order in general; the game sorts with it as it is.
- **By-value records.** The insertions, the partition and the heap functions take a record by value and destroy it before they return, as the MSVC ABI has the callee do. The game copy-constructs each one into the argument area. Ours copy-constructs it into a local and passes its bytes, so each record is still made once and destroyed once.

Kept as the game's, called at their addresses:

- The record's destructor 0x0050BDE0.
- The vectors' copy and assignment: 0x0051FE90 and 0x00520970 (shared, now with address entries), 0x00462F00 and 0x00462C10.
- The range destroy 0x00518B20 and the two InsertN functions.
- SC4Curve::Init and SetPoints, the string helpers and strtoul.
- 0x0050CDA0 and the controller manager's slots.

Oddities kept:

- The factory writes the class into a null template when the allocation fails.
- create_automaton reads a ninth argument it never uses.
- The four functions with an occupant (attach and detach) test and release an occupant slot they have just zeroed.
- The record copy and assignment copy field by field, so the padding is left alone.
- The three-string record's constructor leaves +0x3C alone, though its assignment copies it.
- GroupCacheIds hands over the IDs only when the +0x18 part is still empty.

Stack slots the game reuses as locals:

- get_source_building_count reads its key into its binding argument's slot. So a key that does not read stays the binding's address, and ours starts its local from that address the same way.
- 0x0051D120 reads a number into its own argument's slot, and a failed read answers 0.
- create_automaton's ninth-argument bool and the attach and detach functions' occupant are argument slots that nothing reads afterwards; ours keep them in locals.

Floats: the comparator's float compares only. An unordered pair is not "below", so the uint32 compare decides it.

Reimplemented like the earlier parts: one writer, every call's argument count checked against the callee's `ret`, then an independent audit of all 43 against the disassembly.

### Test

Two tests reach part s2:

- **`OPENSC4_TEST_AUTOMATA_SCRIPTS=2`** does everything `=1` does and adds ` T<templates>,<helpers>,<records>` after the ` S` token:
  - the city's templates;
  - on a fresh script system: CreateControllerTemplate for both classes, a taken name and a class with no template;
  - the cached ID lists and the record lookups (every key byte) of the city's groups;
  - std::sort of 40 records of the test's own, keyed so that the comparator is a strict weak order. Above 16 records it partitions;
  - partial_sort of 40 with a middle of 20, which runs the heap functions;
  - the full vector's insert in the middle and at the end.
- **`OPENSC4_TEST_LUA_SCRIPT=<path>/tests/scripts/automata.lua`** calls the seven `sc4game.automata` functions with fixed arguments (see the script). It records only whether each call worked and whether it pushed nil, 0 or nonzero, since the pushed objects are pointers. Its result goes into the ` | lua` part's script hash. The occupant arguments are nil, so the attach and detach functions stop after reading their arguments. With these arguments create_controller and create_automaton get null back from the game's functions, on both sides.

Reached in the trial (Making Money d0000, 4 days, both tests): all 43, with all 34 of s1 in the same run. By the game at the load: the templates, the Lua value to ID, get_source_building_count, the record constructor, copy, assignment, the vector insert and the insertion sorts. By the tests: the rest. Original and ours gave the same fingerprint on every day of the trial.

Reimplementation notes from the audit: no defect. Sort (0x00522800) now forwards the word in its first argument's slot as the empty predicate, as the game does; its only caller pushes two arguments, and the callees ignore the predicate. Some wording was also fixed.

### Parity

2026-10-19, B = all eight `simulators.automata.scripts` groups (s1 and s2) against the original (the switch list includes `determinism.dispatch_info=ours`; two jobs, `harness.multi_instance=ours` on both sides), env p4env-s2. Both tests ran: `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_SCRIPTS=2` and the Lua chunk.

| Batch | Making Money d0000, 4 days | Rush Hour t0000, 7 days | The 12-save batch, 7 days |
| --- | --- | --- | --- |
| r2 (snapshot s2-r2) | 5 of 5 | 8 of 8 | 11 of 12; Making Money d0000 partly the same |
| r3 (s2-r3, the final build) | 5 of 5 | 8 of 8 | 11 of 12; Big City d1095 failed on the original's side |

- **Making Money d0000 in r2.** The first B run had other `adv` and `advisors` values from the test day on, and the rest of the line was the same. The second B run had A's values, as did both A runs. This looks like the first-run advisor outcome in [parity.md](parity.md) ("The advisor system's generator"). In r3 the save was the same in the batch and in three more reruns, 8 of 8 days each.
- **Big City d1095 in r3.** The original's game ended during its load, with no output. Two reruns were the same, 8 of 8 days each.

All 77 functions of s1 and s2 were reached in every B run of r2 (14) and r3 (our first-call log lines).

The Lua chunk ran in every run, and every B run carried the ` S` and ` T` tokens. A batch r1 had the chunk's path broken, so its functions were not called; it was otherwise the same (5 of 5, 8 of 8, 12 of 12).


## Part s3a: the loading of the scripts

### What is ours

The 15 functions of s3a's list, in two more groups, off by default ([automata_scripts_3.cpp](../src/core/automata_scripts_3.cpp)):

- **`simulators.automata.scripts.loader` (5):** LoadScripts 0x00523B10, and four readers it runs: the templates' groups 0x0051DAA0, the groups' parents and members 0x0051F5C0, the drive commands 0x0051FA60 and the track buildings 0x0051EEE0.
- **`simulators.automata.scripts.readers` (10):**
  - The attractor templates 0x00523500 and the generator templates 0x00522F30.
  - Their parts: the flags 0x0051C5D0, the calendar 0x0051CB40, the sounds 0x0051CBF0, the ignored states 0x0051CD20, the time of day curve 0x0051F010 (with its point insert 0x0051EE10), the triggers 0x0051F2A0 and the behaviours 0x00522860.

What the loading does:

- **LoadScripts** (from Init) gets the loader's Lua state from 0x0051C4C0 (the game's: it holds the Lua print patch) and runs the script group 0x4A5E8F3F (the automata `_config.lua`). Then the readers run in this order: attractors, generators, the automata and occupant groups (0x00521C70, 0x005216A0, part s3b), the templates' groups, the groups. The answer is false at the first that fails. The drive commands and the track buildings come last. The Lua state is shut down and released. A stopwatch around it is read and dropped. A failed script group formats its message, drops it and answers true.
- **A template** is one entry of the global table `attractor` or `generator`. Its name gets a template (slot 17, else slot 16). The table's fields are read at the index of a table walk made over the entry for that alone:
  - Both readers: `radius`, `time_of_day` (a curve), `calendar` (a bit mask), `behavior` (the behaviour records, sorted), `effect_trigger` and `deactivate_trigger` (ID lists), `lifetime`, `delay`, nine flags, `sfx`, `ignore_states`, `exclusive_radius` and `max_count`.
  - The attractor adds `strength`, a curve spread over the radius.
  - The generator adds the count kind (`population_pct`, `occupancy_pct` or `count`), `rate`, `rate_scale`, `event`, `random_chance`, `lifetime_count`, `group_count` and `priority` (a byte, at most 255, 25 when absent).
- **The groups' pass** resolves each group's two Lua references: the parent's table gives the parent through its `__group_handle` userdata, the group's own table has that field cleared, and both references are dropped. The member names become IDs. **The templates' pass** turns the automata group names into IDs.
- **The drive commands** come from an exemplar (0xA2E3D533, 0x6A231EAA, 0x0C0EAAD0) read by a COM reader: 13 properties of up to 8 values, each value mapping to its property's index.
- **The track buildings** are the global table `track_buildings`: numbers, or strings read as hex.

Kept as the game's, called at their addresses:

- 0x0051C4C0, for the patch site.
- The Lua state's own functions and the field readers (`SC4Script::GetField`, `SC4ScriptField::*`, `SC4ScriptValue::*`, the table walk).
- The point and pointer vector inserts, SC4Curve::SetPoints, the behaviour record functions (part s2), the stopwatch, the resource manager and the COM objects.

Oddities kept:

- A vector is emptied by erasing [end, end), which leaves the end at the begin.
- A template of the other reader's class gets the refusal message, formatted and dropped, and is then read anyway.
- After one entry fails, the later entries are still made and named and get their max_count (and the attractor its radius and strength curve, the generator its radius and the fields after the count kind), but the parts in between are skipped.
- The attractor's strength table of one value (or none) gives two points that read its first value even when there is none.
- `random_chance` is 1.0 when absent and a behaviour's `percentage` 1.0. A behaviour's `radius` is FLT_MAX when absent.

Floats: the strength curve's step is the radius over the count less one, divided in double and stored to a float. The running x is a double, as in the x87 register (53-bit), and is stored to a float per point. Lua numbers the game stores as floats are rounded at the store. The time of day curve inserts each point before the first larger x, where an unordered compare is not larger.

Reimplemented like the earlier parts: one writer (a frame annotator, net-tools\s3\frame.py, tracked the stack offsets of the largest functions), every call's argument count checked against the callee's `ret`, then an independent audit of all 15 against the disassembly. It found one defect, fixed before the final batch: a single-number `turn` of a behaviour's `random_walk` goes to the record's +0x58 (ours had written +0x54; the annotator had miscounted one call's arguments there). No corpus script has one, so the first batch could not show it.

### Test

`OPENSC4_TEST_AUTOMATA_SCRIPTS=3` does everything `=2` does and adds ` L<city>,<fresh>,<load>`:

- A hash of the city's script system as loaded: every template (fields, curves, ID lists and vectors, behaviour records), every group's loaded fields, the drive commands and the track buildings, summed by name.
- The same hash of a fresh script system from the factory, into which LoadScripts loads the scripts on the test day, and that load's answer.

The fresh system's templates and groups are then released, and the object too. The two hashes are equal when the fresh load reads what the city's load read. In the trial they were equal on both sides: 219 templates, 222 groups, 25 drive commands.

Reached in the trial (Making Money d0000, 3 days): all 15, with all 77 of s1 and s2 in the same run, so 92 first-call lines.

### Parity

2026-10-19, env p4env-s3, both tests on: `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_SCRIPTS=3` and the Lua chunk.

B = all ten `simulators.automata.scripts` groups (s1, s2, s3a) against the original. The switch list includes `determinism.dispatch_info=ours`. Two jobs ran, with `harness.multi_instance=ours` on both sides.

| Batch | Making Money d0000, 4 days | Rush Hour t0000, 7 days | The 12-save batch, 7 days |
| --- | --- | --- | --- |
| r1 (snapshot s3-r1, before the audit's fix) | 5 of 5 | 8 of 8 | 12 of 12 |
| r2 (s3-r2, the final build, with main merged; `OPENSC4_MUTE=1` on both sides) | 5 of 5 | 8 of 8 | 11 of 12; Big City d0000 partly the same |

In r2, Big City d0000's first B run had other `adv` and `advisors` values from the first day on, and the rest of the line was the same. The second B run had A's values. This is the first-run advisor outcome of [parity.md](parity.md) ("The advisor system's generator"). Two reruns of that save, muted, were the same, 8 of 8 days each.

All 92 functions of s1, s2 and s3a were reached in every one of the 14 B runs of both batches. Every run's ` L` token had the same city and fresh hash, 25C2EAB1.

## Part s3b: the group and drive parameter readers

### What is ours

The 11 functions of s3b's list, in two more groups, off by default ([automata_scripts_4.cpp](../src/core/automata_scripts_4.cpp)):

- **`simulators.automata.scripts.groupreaders` (7):** the occupant groups 0x005216A0, the automata groups 0x00521C70, the group reader 0x00520DF0, the models 0x0051ECA0, the key effects 0x00521800, the three-string record's copy 0x0051FB90 and the record map's insert 0x00520520.
- **`simulators.automata.scripts.driveparams` (4):** the player's driving 0x0051DC20, the airplane 0x0051E1E0, the helicopter 0x0051E4A0 and the watercraft 0x0051E6B0.

Two names from part s3a were wrong and are corrected: 0x00521C70 reads the global table `automata_group` (ReadAutomataGroups, not ReadGroups) and 0x005216A0 reads `occupant_group` (ReadOccupantGroups, not ReadVehicleKinds).

What the readers do:

- **A group** (0x00520DF0, for both tables) is the group of its name, or a new one (slot 0x34) with `group_id` (a number truncated through __ftol2, else a hex string). Then it reads `percentage` (1.0 when absent), `controllers` (templates by name into the list +0x30), `models` (three hex IDs per entry into +0x48), `anims` (name to a number or a hex string, +0x34), `_parent` (a Lua reference, +0x4E) and `class_id` (+0x14, 0 when absent). The group's table gets `__group_handle` (the group as userdata) and the group a reference to its table (+0x4C). Lists and maps whose field is absent are emptied.
- **An automata group** also goes into the automata group map (+0x60, with a reference when it went in) and reads:
  - `trailers` (names, +0x54) and `kph` (one or two numbers, +0x58 and +0x5C);
  - the drive parameters (0xA0 bytes at +0x60): the first of `airplane` (18 floats), `helicopter` (13) and `WatercraftDriveParams` (40);
  - `playerdrive` (0x94 bytes at +0x64: two ints, 33 floats and `OffRoadTurnAngleRange`);
  - `csi_image` and `source_building` as hex (+0x68, +0x6C);
  - `key_effects`: per table entry a record of `fx`, `bone`, `controller` and the flags `hold` 1, `one_shot` 2, `hard_transition` 4, `override` 8 (and 0x10 for a non-empty fx, 0x20 for a controller). It goes under the byte `cmd` into the record map (+0x70) when its fx is not empty, unless the key is there.

Oddities kept: the models reader reads three strings of an entry whatever its length; the drive readers check the walk's index before every field (it is never -1, so they never stop early); `OffRoadTurnAngleRange` is read even after such a stop.

Reimplemented like s3a: a frame annotator (net-tools\s3b\frame.py, fixed to count only a callee's own arguments), the field tables generated from the disassembly, then an independent audit of all 11 against the disassembly. It found no behavioural difference, only four cosmetic ones (an unreachable null test in the record copy, the order of two independent stores, the context read from the walk rather than the system, an ignored return value).

### Test

`OPENSC4_TEST_AUTOMATA_SCRIPTS=3`'s group hash now covers what s3b writes as well: the controller templates, the models, the trailers, the anims and key effects (summed), the speeds, the drive parameter and driving blocks (raw: the allocations are zeroed under `determinism.zero_alloc`), the image and the source building.

Reached in the trial (Making Money d0000, 5 days): 10 of the 11 (with all 92 of s1, s2 and s3a: 102 first-call lines). The models reader is not reached: no script group has `models`. The ` L` token was L775668C0,775668C0,1 on both sides.

### Parity

2026-10-20, env p4env-s3b, `OPENSC4_MUTE=1` on both sides, both tests on (`OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_SCRIPTS=3` and the Lua chunk). B = all twelve `simulators.automata.scripts` groups (s1, s2, s3a, s3b) against the original. The switch list includes `determinism.dispatch_info=ours`. Two jobs ran, with `harness.multi_instance=ours` on both sides. Snapshot s3b-p1, the final build.

| Making Money d0000, 4 days | Rush Hour t0000, 7 days | The 12-save batch, 7 days |
| --- | --- | --- |
| 5 of 5 | 8 of 8 | 11 of 12; Big City d0090 partly the same |

Big City d0090's first B run had other `adv` and `advisors` values from the second day on, and the rest of the line was the same. Its two reruns had A's values, 8 of 8 days each: the first-run advisor outcome of [parity.md](parity.md) ("The advisor system's generator").

All 102 reached functions were reached in every one of the 16 B runs, and every run's ` L` token was L775668C0,775668C0,1.

### The script system is finished

Of the class's 125 functions, 103 are ours (s1 34, s2 43, s3a 15, s3b 11) and 22 stay the game's:

- The nine adjustor thunks: 0x005200C0 to 0x00520110, 0x00521670, 0x00521680, 0x00521690.
- Twelve shared helpers with callers outside the class: 0x004FC900, 0x0051C880, 0x0051C950, 0x0051C9B0, 0x0051C9F0, 0x0051CA60, 0x0051CE70, 0x0051FC30, 0x0051FDC0, 0x0051FE90, 0x0051FF00 and 0x00520970.
- 0x0051C4C0, the loader's Lua state, for the Lua print patch site 0x0051C50B.

The Lua VM and the script field readers (`SC4Script*`) are outside the class and stay the game's, called at their addresses.


## Part c: the city situation manager

### Survey

cSC4CitySituationManager (class ID 0x0BB14381, factory 0x005148C0, constructor 0x00529FD0, 0xB00 bytes, city slot 49, the city's GetCitySituationManager at slot 0xC4) runs the Rush Hour missions: the city situation advices of the advisors' scripts ([lua.md](lua.md)), their targets and automata, the CSI (the situation's on-screen icons, through the overlay manager), the countdown, the joyrides of the U-Drive, and the success counts of the missions, which it saves. The layout is in [automata_situations.h](../src/core/automata_situations.h).

Five parts, each with its own vtable:

| Part | Vtable | What it is |
| --- | --- | --- |
| +0x00 | 0x00A97E58 | cISC4CitySituationManager, 40 slots. The gzcom-dll header has 34; its two IsSituationAvailable sit in the other order (slot 19 takes an ID, 0x00525CB0; slot 20 an occupant, 0x00525BC0). Slot 34 is the deleting destructor, 35 to 39 are 0x005252D0 (the joyride's start and stop), 0x0052E750 (start a situation), 0x005276A0, 0x005294C0 (end the situation) and 0x00529ED0. |
| +0x04 | 0x00A97E44 | the message target: DoMessage 0x0052EB80 |
| +0x08 | 0x00A97E34 | the adjustors and the shared 0x005DEB40 |
| +0x0C | 0x00A97E1C | serializable: Write 0x00527040, Read 0x00529010, GetGZCLSID 0x005274E0 |
| +0x10 | 0x00A97E08 | a window procedure, 0x0052CAB0: the answers of the situation's message popups |

The fields that matter for part c1:

- +0x14 initialized, +0x18 the reference count.
- +0x9F0 the CSI's visibility, +0x9F4 the active situation, +0xA48 the status (4 when none runs).
- +0xA4C the active automaton, +0xA50 the current target, +0xA5C and +0xA60 the target count and index.
- +0xA8C, +0xA90, +0xA94 the situation's time, the countdown and the time limit.
- +0xA34 the success map: mission ID to a uint16 count. It is the only state the save holds, with the CSI's visibility.
- +0xA2C the overlay manager, +0xA08 the tracked automata, +0xAC4 a map of objects the situation controls, +0xA04 a list of icons.

The class is found by its vtables, the constructor and the helpers only they call. In the range 0x00524560 to 0x0052F130, with the factory and three helpers outside it that only the class calls (0x0054A5F0, 0x0054A700, 0x00554EA0), that makes 164 functions and 43,815 bytes. The survey's 167 and 44,037 counted a few neighbours; Ghidra's list also holds a padding byte at 0x0052542F, which is no function. Of the 164:

- **Stay the game's (21).**
  - The twelve `sub ecx,N; jmp` adjustor thunks: 0x005275A0 to 0x00527610 and 0x00527650 to 0x00527680.
  - Five shared helpers with callers outside the class: SC4Script::RegisterVariable 0x00524970, 0x00525E80, 0x00525FB0, the referenced hash set's clear 0x00526050 and SC4RefVector::Erase 0x00527180.
  - The two functions with SC4UIScale sites, as in parts aw1 and aw2: 0x00528580 (a probe hook, off by default) and 0x0052CC50 (the message popup's style IDs at 0x0052CCEE and 0x0052CD01). The helpers only 0x0052CC50 calls, 0x0054A5F0 and 0x0054A700, stay with it.
- **c1 (41, 5,461 bytes, done): the object, the life cycle, the queries and the save.** See "Part c1" below.
- **c2 (33, 7,557 bytes, done): the Lua functions and the starts.** See "Part c2" below.
  - The eight Lua callbacks Init registers: 0x00524560, 0x00524760, 0x005247E0, 0x00524830, 0x00524880, 0x00524910, 0x00525550 and 0x0052DB50.
  - Slots 19 to 25, 35, 37 and 39: 0x00525CB0, 0x00525BC0, 0x0052A8B0, 0x00525DC0, 0x0052AB00, 0x00524E20, 0x0052A7A0, 0x005252D0, 0x005276A0 and 0x00529ED0.
  - Their helpers: 0x005245F0, 0x00525640, 0x005292F0, 0x00528360, 0x00526380, 0x00526610, 0x005291C0, 0x00524A50, 0x00528ED0, 0x005245A0, 0x00524A00, 0x00526EF0, 0x00526A20 (1,083 bytes), 0x005256E0 and 0x00524D80.
- **c3 (28, 8,796 bytes, done): the messages and the popups' answers.** See "Part c3" below. DoMessage 0x0052EB80 and its handlers 0x0052E300, 0x0052E670, 0x0052E710, 0x0052E910, 0x0052C020, 0x0052A7D0, 0x0052A380, 0x0052A650, 0x0052A830, 0x0052A1E0, 0x00527920, 0x00525AF0, 0x00527300, 0x00526440, 0x00524DF0, 0x00524CC0, 0x00525330, 0x00524580, 0x0052C190, 0x00526720, 0x00525430, 0x00525130, 0x00525100 and 0x00524D10; the window procedure 0x0052CAB0 with 0x0052BF00 and 0x0052BFB0.
- **c4 (30, 6,760 bytes, done): the targets and the situation's smaller steps.** See "Part c4" below.
  - The targets: 0x0052C8F0, 0x00528C70, 0x00529B80, 0x0052BC50, 0x0052BD30, 0x00524C40, 0x00524CF0, 0x00524660, 0x00526180, 0x00529560, 0x00526960, 0x00525A50, 0x00525AA0, 0x0052C0E0 and 0x0052BBC0.
  - The steps: 0x00525800, 0x005254D0, 0x00525070, 0x005278E0, 0x005260C0, 0x00526510, 0x00526250, 0x005271D0, 0x00524E90, 0x00524F00, 0x00529AF0, 0x005278A0, the end of a situation (slot 38) 0x005294C0, and the slot 4 of the two occupant filters of the class, 0x00524AB0 (vtable 0x00A97D90) and 0x00526650 (vtable 0x00A97DE0).
- **c5 (11, 8,915 bytes, done): the running situation.** See "Part c5" below. The start (slot 36) 0x0052E750, 0x0052C4E0, 0x0052B450 (1,883 bytes), 0x0052B200, 0x0052AD00 (1,252), 0x00529680, 0x005299C0, 0x00528140, 0x00527D30, 0x00527EA0 and 0x00554EA0 (1,083).

The calls cross these lines (for example 0x0052AD00 calls 0x00528580, and slot 38 calls 0x00527800 of c1), so each part calls the others at their addresses, as the script system's parts do.

Mods: no NAM site is in the class. SC4UIScale's probe 0x00528580 and its popup style patch in 0x0052CC50 are its only sites; both functions stay the game's.

## Part c1: the object, the life cycle, the queries and the save

### What is ours

41 functions in four groups, off by default ([automata_situations.cpp](../src/core/automata_situations.cpp)):

- **`simulators.automata.situations.object` (8):** the factory 0x005148C0, the constructor 0x00529FD0, the destructor body 0x00527320, the deleting destructor (slot 34) 0x00527AA0, QueryInterface 0x005246B0, AddRef 0x00524750, Release 0x00525520 and the serializable part's GetGZCLSID 0x005274E0.
- **`simulators.automata.situations.life` (8):**
  - Init 0x0052DC40, with the success map's operator[] 0x00527110 and insert 0x005264A0.
  - Shutdown 0x00527AC0, with the release of the controlled objects 0x00527800, of the tracked automata 0x00526E60 and of the list 0x005263D0, and the list's clear 0x00525EC0.
- **`simulators.automata.situations.queries` (22):** slots 5 to 18 (0x00526320, 0x00527620, 0x005274F0, 0x00527630, 0x00527640, 0x00527510, 0x00527520, 0x00527530, 0x00527550, 0x00527560, 0x00527570, 0x005290F0, 0x00524B70, 0x00524BB0) and 26 to 33 (0x00524BF0, 0x005245C0, 0x00527690, 0x00524C20, 0x00527500, 0x005245D0, 0x00527580, 0x005266E0).
- **`simulators.automata.situations.save` (3):** Write 0x00527040, Read 0x00529010 and the success map's insert 0x00525F10.

What they do:

- **Init**, once:
  - With the app's cheats switch on (its slot 0x54) it asks the cheat codes for notifications and registers the cheat `csitrigger`.
  - It registers its message target for ten messages and with the service 0x86AD10EC.
  - It keeps the overlay manager (the dispatch service's slot 22), four services, the lot manager, the city's slot 71 and the script context.
  - It registers the Lua table `sitmgr`: get_success_count, activate_situation, get_current_target, get_active_auto, get_target_name, show_CSI, num_targets_remaining, and the variables num_targets and cur_target_index (the callbacks are part c2).
  - It runs HandleTuningParametersChanged, shows or hides the CSI, and gives each tutorial that the preferences hold as done (+0x1334, ended by a zero) a success count of 1.
- **Shutdown** undoes Init. It drops the references, tells the controlled objects to stop (their slot 4 with 1) and releases them, empties the services' vector, takes back the registrations, ends the active situation (slot 38) and releases the tracked automata and the list.
- **The queries** read the fields above. GetCurrentTimeRemainingSeconds is the time limit less the situation's time. GetCurrentTargetName asks the target for its name through the interface 0x87DDA3A9. GetSituationAura and GetSituationMoney ask the situation (slots 49 and 51 when the status is 2, else 46 and 48). SetMaxCSI writes the global byte 0x00B21D34 and the overlay manager's layer 4. ResetTutorials clears the success map, zeroes the preferences' done tutorials and saves the preferences.
- **The save** is the version 2 (a word), the success map (the count, then per entry the ID and the count) and the CSI's visibility. Read takes versions up to 2; the visibility is read from version 2 on.

Kept as the game's, called at their addresses:

- The adjustor thunks.
- The hash map helpers: SC4Uint32HashSet's construct, clear and resize, SC4HashTable::Begin, the clears 0x00526050 and 0x007D1B40.
- The vector helpers: SC4RefPtrVector::InsertN, SC4RefVector::Erase and Destroy.
- The script registrations, the overlay manager's functions, and the rest of the class.

Oddities kept:

- QueryInterface's null branches write a null and call AddRef through the object; an unknown ID leaves the answer alone.
- Release at a count of 1 (or 0) deletes without counting down.
- Init calls the app's slot 0x54 without a null test at its start, but tests the app at its end.
- Read reads the version into its stream argument's slot. A failed read in its loop keeps the previous entry's ID or count.
- Write pushes each count with the high half of the part's address above it; the stream reads the word.
- GetMissionSuccessCount answers in AX, with the high half of the bucket vector's address above it.
- The destructor body leaves the cRZDate (+0x1C) and the SC4Random (+0x28) alone; the list clear does not zero what it releases.

Floats: GetCurrentTimeRemainingSeconds subtracts on the x87 stack. Ours answers a double in ST0, so the difference is not rounded to a float. CountdownIsInProgress is true for a countdown above 0.0; a NaN is not above it.

Reimplemented like the earlier parts: one writer, every call's argument count checked against the callee's `ret`, then an independent audit of all 41 against the disassembly.

The audit found no behavioural difference. Its notes, all cosmetic or on failure paths:

- Read zeroes its count, ID and count locals before the first read, where the game's are uninitialized. A failed read of the count or of the first entry therefore differs only in what garbage the game would use.
- Some padding and scratch bytes are zeroed where the game leaves stack leftovers: the pairs, the insert's result and InsertN's allocator byte. The callees do not read them.
- The destructor body and ResetTutorials end in tail calls whose EAX ours does not pass on. Read's refusal of a version above 2 returns 0 in all of EAX, not only in AL.
- GetCurrentTimeRemainingSeconds is the same bit for bit under the game's 53-bit precision control.

### Test

`OPENSC4_TEST_AUTOMATA_SITUATIONS=1`, with `OPENSC4_TEST_AUTOMATA=2` or more, calls the city's situation manager on the test day at the game's addresses, so the same test runs on both sides (header of [automata_autotest.cpp](../src/core/automata_autotest.cpp)). It goes into the fingerprint as ` C<identity>,<queries>,<save>,<fresh>,<flags>`:

- QueryInterface for six interfaces on the object and through the four adjustors, AddRef and Release, GetGZCLSID.
- The getters, the target's name, the aura and money, slot 5 for every mission ID. The pointers count only as null or not. The tracked automata count and the reference count go to the log only.
- Write into a stream of the test's own.
- A fresh manager from the factory, with one reference:
  - Read of the city's bytes and Write again, which must give the same bytes; reads of version 3, of version 1 and of a cut stream.
  - The success map's operator[] and insert, directly.
  - The getters over fields the test sets, with the target a building of the controller table (so slot 16 asks it for its name); the aura and money with a situation of the test's own, at the statuses 2 and 4.
  - Slots 26 to 31; the most CSIs is a global, put back after.
  - The tracked automata, the object map and the list, filled with objects of the test's own and emptied by 0x00526E60, 0x00527800, 0x005263D0 and 0x00525EC0.
  - ResetTutorials, when the preferences hold no done tutorial (the harness's never do). It saves the preferences into the run's user folder.
  - Its Release: Shutdown (not initialized), the deleting destructor and the destructor body.

The city's manager is not changed.

### Parity

2026-10-20, env p4env-c1, `OPENSC4_MUTE=1` on both sides, with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_SITUATIONS=1`. B is the four `simulators.automata.situations` groups against the original. The switch list includes `determinism.dispatch_info=ours`. Two jobs ran, with `harness.multi_instance=ours` on both sides. Snapshot c1-r1, the final build (the audit changed nothing). All the same, on the first pair:

| Making Money d0000, 4 days | Rush Hour t0000, 7 days | The 12-save batch, 7 days |
| --- | --- | --- |
| 5 of 5 | 8 of 8 | 12 of 12 |

All 41 functions were reached in every one of the 14 B runs (our first-call log lines). Every run carried the same ` C` token per city:

- C783025EC,8FC3B85A,2A3F2853,64BE6A38,7 for Making Money;
- C783025EC,892F3FB0,DF5AAD0B,64BE6A38,7 for Big City and Rush Hour.

What reached them:

- **The game:** the factory, the constructor, QueryInterface, AddRef, Read, the insert and Release at the load; Init, HandleTuningParametersChanged and SetMaxCSI; during the run slot 5, the CSI's visibility, the active situation and the list clear.
- **The test:** the rest, among them Write, the destructors and Shutdown.

Not compared: Shutdown's initialized branch. The fresh manager is never initialized, and the city's Shutdown runs at the exit if at all; its helpers are called directly by the test. Init's tutorial loop is not compared either: the harness's preferences hold no done tutorial, and the test calls 0x00527110 directly.

## Part c2: the Lua callbacks and the starts

### What is ours

33 functions in two groups, off by default ([automata_situations_2.cpp](../src/core/automata_situations_2.cpp)):

- **`simulators.automata.situations.lua` (8):** the callbacks of the Lua table `sitmgr`: get_success_count 0x00524760, get_current_target 0x005247E0, get_active_auto 0x00524830, show_CSI 0x00524880, num_targets_remaining 0x00524910, activate_situation 0x00525550, get_target_name 0x0052DB50 and the accessor of the variables num_targets and cur_target_index 0x00524560.
- **`simulators.automata.situations.starts` (25):**
  - Slots 19 to 25: IsSituationAvailable by ID 0x00525CB0 and for an occupant 0x00525BC0, InitiateSituationByGuid 0x0052A8B0, InitiateSituationForOccupant 0x00525DC0, InitiateSituationByAutomataGroup 0x0052AB00, StartJoyride 0x00524E20, TreatAutomatonAsGroup 0x0052A7A0.
  - Slots 35, 37 and 39: the U-Drive switch 0x005252D0, the drive of a started situation 0x005276A0 and the stop 0x00529ED0.
  - Their helpers: the candidate filter 0x005245F0, the gathering by the services 0x00525640, the automaton the services make 0x005292F0, its site 0x00528360 with the site filter's constructor 0x00526610 and the random pick 0x00526380, the CSI of an automaton on a list entry 0x005291C0 with the priority compare 0x00524A50 and the icon 0x00528ED0, the inherited template 0x005245A0, a holder 0x00524A00, the time limit 0x00526EF0, the distance to the target 0x00526A20, the driver's position 0x005256E0 and a message of the situation 0x00524D80.

What they do:

- **The callbacks** (cdecl: a1, the arguments, the results, the binding) get the manager through the binding (its slot 8 with a1, then slot 18) and push one number (SC4ScriptNumber):
  - get_success_count: slot 5 for argument 0 as an ID, as a word;
  - get_current_target and get_active_auto: slots 9 and 8, with no null test of the manager;
  - show_CSI: answers slot 30, then sets slot 29 from argument 0;
  - num_targets_remaining: slot 12, or 1 without a manager;
  - activate_situation: posts the message 0xAC1045EF (the ID, argument 1 as a bool, 0) and answers true, with a manager and an ID not 0;
  - get_target_name: slot 16's text.
- **IsSituationAvailable** walks the advisor service's situation advices (0x00B43D34's slot 7 with 0x1D, a std::map, through the interface 0x6BEB1E8A):
  - by ID, whether an advice's automata groups (its slot 12) hold the ID;
  - for an occupant, whether an advice's slot 53 answers true for the occupant's 0xA9B40F05 interface.
- **InitiateSituationByGuid** takes the advice by GUID. Its candidates are the occupants of its automata groups (or of the one group given) that the four services (+0x9F8) know, filtered by 0x005245F0 (flag 2, none of 1, 0x8000000, 0x10000000, the type neither 0x34 nor 0x14). The first that starts wins: slot 36 then slot 37, or slot 22 when asked. Else an automaton the services make near a random occupant of the group's class (0x005292F0, 0x00528360) is tried, else slot 36 runs with no occupant.
- **InitiateSituationForOccupant** starts slot 36 with the advice, or with the first list entry (+0xA04, walked backwards) that holds the occupant or answers true for it.
- **InitiateSituationByAutomataGroup:** a list entry that answers true for the group (slot 54) starts its own situation by its GUID (slot 21). Else, when asked, a joyride (slot 24) starts for a candidate or a made automaton.
- **StartJoyride** and slot 35 with 1 set +0xA98, send 0xABBBCA64 (StartJoyride only) and start the U-Drive (0x00506850, which does nothing while 0x00B21D74 is set). Slot 35 with 0 leaves the U-Drive's view.
- **Slot 37**, after a start:
  - the status 3 and the situation's script (0x00525430);
  - with the situation's flag 2, the car service's groups handed to the automaton's controller;
  - the time limit from 0x00526EF0, the situation's script given the distance (0x00526A20) and the automaton's speed;
  - the message 0xABBBCA64 with the situation, and slot 35 with 1.
- **Slot 39** drops the driver (+0xA9C) and +0xAA0 and zeroes the time limit. It takes the active automaton's CSI off the situation's list entry, then calls slot 38, and shows or hides the CSI layer.
- **0x005291C0** (slot 25's) shows the automaton's CSI on the first free list entry that answers true for it, below the most CSIs. A tracked automaton takes the entry of a lower-priority one instead.

The list entries (+0xA04): +0 next, +4 previous, +8 the entry (its slots 53 and 54 the relevance tests, slot 3 its advice), +0xC the automaton shown, +0x10 its overlay ID.

Oddities kept:

- show_CSI and activate_situation read their bool into their binding argument's slot. show_CSI writes the byte 1 there first, and hands the whole word to slot 29.
- get_success_count reads argument 0 with the arguments' slot 6; the others use slot 5.
- The accessor answers slot 26's EAX with AL 1, and on reading passes slot 25's EAX on.
- 0x005292F0 never frees the vector its services' slot 15 may fill. It keeps looking at later groups after a find, and a null answer then drops the find. It answers its pick after releasing it, as 0x00528360 does.
- 0x00526A20 answers its distances unrounded (a double in ST0) and its fallback (FLT_MAX) rounded. For the type 2 the x edge is rounded to a float and the z edge is not.
- 0x005256E0 copies the first float of the driver's position through the x87, so a signalling NaN comes out quiet.
- The +0x28 random of a new manager is seeded with -1, which means from the clock: its picks differ from run to run.

Reimplemented like the earlier parts: one writer, every call's argument count checked against the callee's `ret`, then an independent audit of all 33 against the disassembly. The audit found one difference, which is fixed: after slot 54 answers true, 0x0052AB00 reads the entry again and goes on to the joyride when it is null. Its other notes are cosmetic: locals the game leaves uninitialized are zeroed, argument slots the game reuses are locals, and the high bytes of EAX differ where callers read only AL.

### Test

`OPENSC4_TEST_AUTOMATA_SITUATIONS=2` adds ` D<lua>,<available>,<starts>,<helpers>,<flags>` to the fingerprint (header of [automata_autotest.cpp](../src/core/automata_autotest.cpp)). It runs on the same day as part c1's test, on a fresh manager whose vtable is a copy with slots 24, 36 and 38 the test's. No situation starts and no message is sent; the game's globals are read and never written. The functions get only objects of the test's own, as arguments or in the fresh manager's fields, and each object hashes its calls. The fresh manager's random is reseeded with a fixed seed.

- The eight callbacks, with and without a manager. activate_situation only runs with an ID of 0 or no manager, so it posts nothing.
- Slots 19 and 20 for every automata group and GUID of the real advices (up to 8 and 4), 0, an unknown ID, two buildings and null.
- Slots 21 to 23 with two services of the test's own: the real advices by GUID and an unknown one, candidates that start or not, slot 22's path, made automata, the list entries.
- The helpers directly: the candidate filter over its flags, the gathering, the made automaton and its site (also by +0xAF8), the random pick, the priority compare, the holder, the inherited template, the icon (never with a template, so no icon is added), slot 25 with 0x005291C0 (the automaton tracked, so no icon either), the driver's position, the distance (the city's edge, a cell, straight, five lot rectangles), the time limit with a script context of the test's own, slot 39, slot 37 without a situation and slot 35 off.

StartJoyride (0x00524E20) and 0x00524D80 are not called: they send 0xABBBCA64 to the city view, which can open the mission popup or pause the game, and the joyride starts the U-Drive. A real start would need slot 36 (part c5). Part c5's test calls both, with a message server and a U-Drive view of its own (see "Part c5").

Flags: 1 ran, 2 GUIDs found, 4 groups found, 0x10 a field left set (an error). `OPENSC4_TEST_C2_TRACE=1` logs the helpers' hash step by step.

### Parity

2026-10-20, env p4env-c2, `OPENSC4_MUTE=1` on both sides, with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_SITUATIONS=2`. B is the six `simulators.automata.situations` groups (parts c1 and c2) against the original. The switch list includes `determinism.dispatch_info=ours`. Two jobs ran, with `harness.multi_instance=ours` on both sides. Snapshot c2-r2, the final build (with the audit's fix). All the same, on the first pair:

| Making Money d0000, 4 days | Rush Hour t0000, 7 days | The 12-save batch, 7 days |
| --- | --- | --- |
| 5 of 5 | 8 of 8 | 12 of 12 |

In every one of the 14 B runs, 31 of the 33 functions were reached (our first-call log lines). Each run's ` D` token was the same on both sides. The tokens differ between cities only in the real advices found: Making Money d0000 and d1095, Big City d0365 and d1095 and Making Money d0090 and d0365 find advices (flags 7), the Rush Hour saves and the early Big City saves none (flags 1).

Not reached: StartJoyride 0x00524E20 and 0x00524D80 (see "Test"). Not compared:

- The bodies of slots 24 and 37 with a situation, and slot 35 with 1.
- activate_situation's message.
- 0x00528ED0's Add and Remove of an icon.

Slot 36 (the start) and slot 38 (the end) were the test's own, so the real start of a situation was not run; it belongs to part c5.

## Part c3: the messages, the CSI list and the popup

### What is ours

28 functions in three groups, off by default ([automata_situations_3.cpp](../src/core/automata_situations_3.cpp)):

- **`simulators.automata.situations.messages` (15):**
  - DoMessage 0x0052EB80 (the +4 part's slot 3) and the frame 0x0052E300.
  - The events: an occupant removed 0x0052E670, the automata events 9 (0x0052E710) and 11 and 12 (0x0052C020), an occupant added 0x0052A7D0, an advice gone 0x00524DF0.
  - The small steps: the drive aborted 0x00524CC0, IsFinished 0x00524580, the target check 0x00525130, the target's occupant 0x00525100, the message post 0x00524D10.
  - The success 0x0052C190, the failure 0x00526720 and the script 0x00525430 (the RunScript part c2 called at its address).
- **`simulators.automata.situations.list` (8):** an advice's list entry added 0x0052A380 and removed 0x0052A650, an automaton untracked 0x0052A830, the candidate 0x0052A1E0, and the list's sort 0x00527920, merge 0x00525AF0, construction 0x00527300 and insert 0x00526440.
- **`simulators.automata.situations.popup` (5):** the popup shown 0x0052E910 and closed 0x00525330, its window procedure 0x0052CAB0 (the +0x10 part's slot), the dismiss 0x0052BF00 and the joyride button 0x0052BFB0.

What they do:

- **DoMessage** answers fifteen messages; the handlers get the manager (this - 4):

  | Message | What it does |
  | --- | --- |
  | 0x230E27AC | The cheat csitrigger (data 1 0x8C4136DB): its second word, hex, posted as 0xAC1045EF. |
  | 0x0BB324BD | Slot 39, unless the situation succeeded or failed. |
  | 0x09D92FC3 | By data 2: 2 aborts the drive of the active automaton, 9 rechecks the target, 11 (and 12 with data 3 0x14) an automaton destroyed. |
  | 0x26D31EC1 | After a load (+0x9F1), every advice of the advisor service put on the list. |
  | 0x2C16BDE7, 0xEBB67EF7 | The popup closed and shown again (0xEBB67EF7 with an advice and data 1). |
  | 0x6BB350E4 | An advice gone: slot 35 with 0, slot 39 for the active one. |
  | 0x86AD10EE | The frame, dt in data 3. |
  | 0x8A09FA64, 0x8A09FA65 | An advice's list entry added or removed. |
  | 0x99EF1142, 0x99EF1143 | An occupant added (tracked) or removed. |
  | 0xAC1045EF | Slot 21 with data 1, 2 and 3. |
  | 0xCC0D905F | The driver (+0xA9C), with the situation's flag 8. |
  | 0x8A09FA67 | Nothing. |

  Other messages answer false.
- **The frame:**
  - The joyride's end (+0xA98): the services' slot 9, the countdown 4000, +0xAA0 stopped, and the U-Drive camera effect (0x00B43D1C's slot 7) placed at the U-Drive's position.
  - Else the countdown (+0xA90; at its end the services' slot 10).
  - Else the drive's time (+0xA88). At its end: 0x0052C190 for the status 1 or 0x00526720 for 2, then 0xEBB67EF7.
  - The driver is dropped when its slot 5 is false.
  - While a situation runs: the warning 0x6C32871F to the automaton 30 seconds before the time limit, the target update 0x0052C8F0 (part c4), and the target check. Every 5000 ms the target check hands the services that handle the situation's groups their slot 21 with a radius and the target's occupant or the automaton.
- **The success:**
  - The GUID's success count goes up (at most 0xFFFF). With the flag 0x40 the GUID is added to the preferences' done tutorials, which are saved.
  - The camera effect is named by the situation. The money (0x00B43D60) and the budget's reward follow, then the script (+0xADC).
  - The advisor service is told and the list entry removed.
- **The failure:** the camera effect, the money and the budget's loss at the automaton or the target, then the script (+0xAE0) in any case.
- **The list:**
  - An added advice is inserted at the front and the list sorted (STLport's list sort and merge, by the advice's slot 3's slot 7, an entry without an advice first).
  - An entry showing no automaton gets a candidate of its automata groups that is not tracked (0x0052A1E0), under the most CSIs. At the most, a later entry's CSI is taken instead. Without a candidate, a tracked automaton the advice answers true for is taken from a later entry.
  - Removing an entry hands candidates to the entries without one.
- **The popup:**
  - The window 0x96A006B0 (by the status: 0x0BBC06B6, 0x4BBC080F or 0x2BB16D50) is loaded and filled by 0x0052CC50 (the game's, an SC4UIScale site) under a modal guard, and kept in +0xA28. The link of the data (its property 0x2BE8FBF3) is opened.
  - The window procedure answers the buttons 1 (continue or dismiss), 6 (the drive, slot 37) and 7 (the joyride, slot 24) and the link, and closes the popup (0x00525330).

Oddities kept:

- DoMessage hands slot 21 the bool of 0xAC1045EF in the low byte of the message pointer.
- 0x0052A380 passes the advice to the sort as its comparator, which the sort and the merge never read.
- 0x0052A1E0's candidate set is released by 0x00B43D44's slot 33, not by the manager's services.
- At the most CSIs, 0x0052A380 gives up the candidate it found and takes over a lower entry's CSI instead.
- The window procedure answers the link's result inverted, and tests the status 1 or 2 inline where the button calls 0x00524580.
- Several QueryInterface out pointers and the target check's subject live in argument slots; ours are locals.

Floats: the countdown, the drive's time and the target check's clock are compared as the x87 register holds them (unrounded) and stored rounded. The target check's radius is compared unrounded and kept rounded.

Reimplemented like the earlier parts: one writer, every call's argument count checked against the callee's `ret` (with the pushes that a call without arguments leaves to the next one), then an independent audit of all 28 against the disassembly. The audit found no behavioural difference. Its notes are cosmetic: locals the game leaves uninitialized (the pad bytes of the camera transforms, the money and reward outputs, a property out word, the new success pair's high word) are zeroed, and argument slots the game reuses are locals.

### Test

`OPENSC4_TEST_AUTOMATA_SITUATIONS=3` adds ` E<messages>,<frame>,<handlers>,<list>,<flags>` to the fingerprint (header of [automata_autotest.cpp](../src/core/automata_autotest.cpp)). It runs on part c2's day, on a fresh manager whose vtable is a copy with slots 24, 35, 36 and 38 the test's, with a reseeded random. Only objects of the test's own are handed to the functions; the game's globals are read and never written.

- DoMessage for every message that posts nothing and opens no popup:
  - an unknown one, 0x8A09FA67, the cheat with no ID or an ID of 0;
  - the end (none, finished, running), the automata events 2, 9, 11, 12 and 5, the load without +0x9F1, an advice gone, the frame;
  - occupants added and removed, a start by an unknown GUID, the driver.
- The frame: the joyride's end (only when the U-Drive's camera would not be asked; flag 2), the countdown, the driver, the drive's time, the warning, the target update at the statuses 4 and 3, and the target check (the automaton, the target's occupant, a radius from the city's width and one of 24).
- The handlers directly:
  - 0x0052E670, 0x0052E710, 0x0052C020 and 0x0052A7D0, the first three over the kinds 1, 3 and 4;
  - 0x00524DF0, 0x00524CC0 and 0x00524580 over the statuses, 0x00525130 and 0x00525100;
  - 0x0052C190's early returns, 0x00526720 with the rewards' slots false, 0x00525430;
  - 0x0052BF00 without a situation, 0x0052BFB0, 0x0052CAB0 for 0xA2BF8AD5, 0xA2BF8AD6 and two messages it refuses.
- The list: advices added and removed by message, the untracking, a take-over, the most CSIs (flag 4), 0x0052A1E0, and a list of the test's own made, filled, sorted and merged.

The tracked set's walk order follows the fakes' addresses, which move with the DLL's load address, so where it holds several fakes the calls on them are summed instead of hashed in order. Flags: 1 ran, 2 the joyride's end ran, 4 the most-CSIs step ran, 0x10 a field left set (an error). `OPENSC4_TEST_C3_TRACE=1` logs each step's hash; 2 also logs the fakes' calls in the list part.

Not called by the test:

- 0x0052E910, 0x00525330 and 0x00524D10: they open or close a real popup, or post a message.
- 0x0052C190's body: it tells the advisor service.
- DoMessage's 0x2C16BDE7 and 0xEBB67EF7, the frame's end of a drive, and the window procedure's buttons and link: they reach these.
- The load with +0x9F1 set: it hands the real advices to the list.

### Parity

2026-10-20, env p4env-c3, `OPENSC4_MUTE=1` on both sides, with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_SITUATIONS=3`. B is the nine `simulators.automata.situations` groups (parts c1, c2 and c3) against the original. The switch list includes `determinism.dispatch_info=ours`. Two jobs ran, with `harness.multi_instance=ours` on both sides. Snapshot c3-r1, the final build (the audit changed nothing). All the same, on the first pair:

| Making Money d0000, 4 days | Rush Hour t0000, 7 days | The 12-save batch, 7 days |
| --- | --- | --- |
| 5 of 5 | 8 of 8 | 12 of 12 |

In every one of the 14 B runs, 25 of the 28 functions were reached (our first-call log lines). Every run, on both sides, carried the same token, E FF4CF095,3814C9A3,B254D375,EE1F3D29,5. The flags are 5 everywhere: the joyride's end was skipped, because the U-Drive's camera would have been asked.

Not reached: 0x0052E910 (the popup shown), 0x00525330 (the popup closed) and 0x00524D10 (the message post). No mission popup opens in these runs, and the test does not call them (see "Test"). Not compared:

- 0x0052C190's body. It is reached through the test's early returns only.
- The frame's end of a drive and its joyride's end.
- DoMessage's 0x2C16BDE7, 0xEBB67EF7 and 0x26D31EC1 with +0x9F1 set.
- The window procedure's buttons and link.
- The rewards and camera effects of 0x0052C190 and 0x00526720.

## Part c4: the targets and the situation's smaller steps

### What is ours

30 functions in two groups, off by default ([automata_situations_4.cpp](../src/core/automata_situations_4.cpp)):

- **`simulators.automata.situations.targets` (15):**
  - The target update 0x0052C8F0 (each frame while a situation runs) and its checks by kind: the distance 0x0052BC50 (kind 1), the city's edge 0x00528C70 (kind 2), the cells 0x00529B80 (kind 5), and the escort 0x0052BD30 (the situation's flag 2).
  - The status 0x00524C40, the time up 0x00524CF0, a target reached 0x0052C0E0, the next target 0x0052BBC0, the overlay's intensity 0x00524660.
  - The kind 5's cells: the target placed 0x00529560, the nearest cell 0x00526960, the cell set's count 0x00525A50, erase 0x00526180 and bucket walk 0x00525AA0.
- **`simulators.automata.situations.steps` (15):**
  - The end of a situation (slot 38) 0x005294C0, the scripts' references 0x00525800 and 0x005254D0, the active automaton 0x00525070.
  - The target IDs 0x00529AF0, the target script 0x00524F00, a cell filter 0x00524E90 (stdcall, 8 arguments).
  - The slot 4 of the two occupant filters: 0x00524AB0 (vtable 0x00A97D90) and 0x00526650 (vtable 0x00A97DE0, on SC4OccupantGroupFilter).
  - The cell set's insert 0x005278E0, unique insert 0x005260C0, resize 0x00526510 and copy 0x005271D0; the cell map's insert 0x00526250; the referenced set's insert 0x005278A0.

What they do:

- **The target update** answers the status (4 without a situation or an automaton):
  - With the situation's flag 2, the escort first.
  - At the status 3 without a check_success script (+0xAE4 at -2), by the situation's kind: 1 with a target, 2 or 5.
  - With a check_success script, the script is called with the situation's table (with the situation's flag 0x20, +0xAF8's slot 68 first). The number it answers, truncated by __ftol2, is the status.
  - At the status 3 the situation's time (+0xA8C) grows by dt / 1000. A time limit (the manager's slot 13) of at least 1.0 that the time reaches ends it (0x00524CF0: the status 2).
  - Every step sets the status through 0x00524C40. For the statuses 1 and 2 it sets the drive's time (+0xA88): the situation's slot 11 (at least 1.0) when the automaton has the flag 0x8000000, else 1.0.
- **The kinds:**
  - 1: the distance to the target (0x00526A20) less the situation's slot 52. Within the situation's slot 7 the clock +0xA7C runs; at slot 10's seconds the status 1. At the status 1 the next target (0x0052BBC0), 3 when one starts.
  - 2: in a cell within 2 of the city's edge, when the point 32 units along the automaton's direction (slot 15) leaves the city: the status 1 when the automaton has the flag 0x2000, or when a link of 0x00B43D8C's slot 16 list is within 2 cells.
  - 5: each cell of the set +0xAA8 within the situation's radius (slot 52) is reached. Its object (the map +0xAC4) is told to stop and dropped, the camera effect mission_selection_yellow_shrink (the water one for the situation's slot 36 kind 6) is made over the cell, the cell is erased and +0xABC counted. At slot 44's goal (or +0xAC0's) the status 1, else the next cell is placed (0x00529560).
  - The escort: an occupant of the type 0x34 of the situation's groups that the automaton controls, within the situation's radius (slot 20), runs the clock +0xA80; at slot 21's seconds the status 2.
- **The overlay** (+0xA2C, +0xA30) is given the distance as an intensity from 0.2 to 0.5 (0x00524660, then 0x0046BF00, the game's).
- **A target reached:** without a check_success script, the kind 1 fails (2) and the kind 3 goes on to the next target (none left: 1). With one, a target whose slot 22 answers 0x5005 or 0x5006 starts the next (0x0052AD00, part c5).
- **The next target** (with the situation's slot 17): the next of slot 16's targets (+0xA60), started by 0x0052AD00. The automaton is told 0x2C22D924 and 0xAC083D7D is sent (0x00524D80).
- **The cell target** (0x00529560) goes to the cell +0xAD8's center, at the terrain's height plus the water's depth: the target's slot 9, or a new prop occupant automata controller handed to 0x00528580.
- **The scripts:** the situation's table's on_success, on_failure, check_success, find_target, create_target, init and get_time_limit are referenced into +0xADC to +0xAF4. get_time_limit falls back on the global table base_citysituation_advice. Without a situation they are unreferenced and set to -2.
- **The end** (slot 38): the controlled objects released, no active automaton and no target, the referenced set +0xA68 cleared, the status 4, the scripts dropped, 0xABBBCA64 sent, the situation and +0xAF8 dropped.
- **The active automaton** (0x00525070) takes the manager's +4 part as a listener (slots 39 and 40) and gets the flag 0x4000.

Oddities kept:

- 0x00529560 asks 0x00526960 for the cell of +0xAA8 nearest +0xAD8, then drops the answer and places the target at +0xAD8 itself.
- 0x00524F00 answers its object after releasing it.
- 0x00526960 squares the 16-bit differences into a signed 32-bit sum; 0x00528C70 sums its squared cell distances unsigned.
- 0x00529B80 hands the situation's slot 45 a stack slot as its out pointer and never reads it.
- 0x00525FB0, the referenced set's unique insert, stays the game's (it has callers outside the class).

Floats: the clocks (+0xA7C, +0xA80, +0xA8C) are stored rounded; the seconds compared are the unrounded sums times 0.001, rounded. The distances and radii are rounded to floats where the game stores them, and the situation's floats are compared as the x87 holds them.

Reimplemented like the earlier parts: one writer, every call's argument count checked against the callee's `ret`, then an independent audit of all 30 against the disassembly. The audit found no behavioural difference. Its notes are cosmetic:

- 0x00528C70 and 0x00529B80 test the overlay's fields again before 0x0046BF00, where the game reads them again without a test (0x00526A20 does not change them).
- 0x00528C70's link cells and 0x0052BD30's set head are zeroed, where the game leaves them uninitialized.
- 0x00529B80's slot 45 gets a zeroed local of its own.

### Test

`OPENSC4_TEST_AUTOMATA_SITUATIONS=4` adds ` F<targets>,<steps>,<sets>,<filters>,<flags>` to the fingerprint (header of [automata_autotest.cpp](../src/core/automata_autotest.cpp)). It runs on part c3's day, on a fresh manager like part c3's (slots 24, 35, 36 and 38 the test's, the random reseeded). Only objects of the test's own are handed to the functions; the game's globals are read and never written.

- The status over the statuses (the drive's time with and without the flag 0x8000000, a NaN), the time up, the overlay's intensity over ten distances.
- Cell sets, a cell map and a referenced set of the test's own: inserts with shared buckets and duplicates, counts, a resize, a copy, erases, walks by 0x00525AA0.
- The manager's cells: the nearest cell, and the target placed (always on a fake target) at cells of the city's middle and edge.
- The next target (never when one would start), a target reached by the kinds and with a script.
- The kinds' checks directly:
  - the distance's clock;
  - the escort: in, at the edge, out, another automaton, another type, none;
  - the city's edge: the middle, heading out with the flag 0x2000 and with the links, heading in;
  - the cells: reached, counted, placed below the goal, the status 1 at it.
- The update by the kinds, with the escort, the check_success script's numbers (2, 3.7, -1, 5e9, 1e20, -0.5, NaN), the time limit.
- The scripts' references (found, missing, the base table's, refused), the active automaton, the target IDs, the target script (the real framework's service is asked for), the cell filter (the real water query), the end without a situation, the two occupant filters.

Not called by the test:

- 0x005294C0 with a situation: it sends 0xABBBCA64 to the city view (0x00524D80).
- 0x0052BBC0 and 0x0052C0E0 where a next target would start (0x0052AD00, part c5).
- 0x00529560 without a target: it makes a prop controller for 0x00528580.
- 0x00529B80 over a cell holding an object: it makes a camera effect.
- 0x0046BF00 through the overlay: the fresh manager has no overlay manager.

Flags: 1 ran, 2 the city's cells used, 0x10 a field left set (an error). `OPENSC4_TEST_C4_TRACE=1` logs each step's hash; 2 also logs the fakes' calls.

### Parity

2026-10-20, env p4env-c4, `OPENSC4_MUTE=1` on both sides, with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_SITUATIONS=4`. B is the eleven `simulators.automata.situations` groups (parts c1 to c4) against the original. The switch list includes `determinism.dispatch_info=ours`. Two jobs ran, with `harness.multi_instance=ours` on both sides. Snapshot c4-r1, the final build (the audit changed nothing). All the same:

| Making Money d0000, 4 days | Rush Hour t0000, 7 days | The 12-save batch, 7 days |
| --- | --- | --- |
| 5 of 5 | 8 of 8 | 12 of 12 |

Two saves of the batch, Making Money d0000 and Rush Hour d0365, were the same on the second pair. On the first, the original side gave two outcomes and B one, and they shared it.

All 30 functions were reached in every one of the 14 B runs (our first-call log lines). Each run's ` F` token was the same on both sides. The tokens differ between cities only in the targets part, which uses the city's grid, water and links:

- F97AF8D3,FF186641,23C8B4E5,F7BE54F0,3 for Big City;
- 467950CB,... for Making Money;
- F2E9857C,... for Rush Hour.

The flags are 3 everywhere (the city's cells used).

Not compared (see "Test"):

- 0x005294C0's body with a situation.
- The start of a next target in 0x0052BBC0 and 0x0052C0E0.
- 0x00529560's new controller.
- 0x00529B80's objects and camera effects.
- The overlay's 0x0046BF00.

## Part c5: the running situation

### What is ours

11 functions in one group, off by default ([automata_situations_5.cpp](../src/core/automata_situations_5.cpp)):

- **`simulators.automata.situations.running` (11):**
  - The start of a situation (slot 36) 0x0052E750.
  - The cell set by kind 0x0052C4E0, with its makers: the zone region 0x0052B450 (kinds 1, 5 and 6) and its region fill 0x00527EA0, the lot 0x005299C0 with 0x00528140, and the network flood 0x00529680 with 0x00527D30.
  - The targets: by the services or the find_target script 0x0052B200, by kind 0x0052AD00, the occupant of the nearest link 0x00554EA0.

What they do:

- **The start** (slot 36, ret 0xC: the situation, the automaton, whether to post). Nothing when the situation is the active one. Otherwise:
  - The old situation is ended (slot 38) and the new one referenced; none answers 0.
  - The status is 4, the drive's time 0 and +0xA99 0. With the situation's flag 0x20, +0xAF8 gets the city's slot 71's slot 12 of +0xAFC.
  - The automaton becomes the active one (0x00525070). Without one, the status is 5 and 0xEBB67EF7 is posted with 0.
  - The scripts are referenced (0x00525800). Then by the situation's kind (slot 4): 2 starts at once; 5 starts with cells (0x0052C4E0); the others count their targets (slot 17's slot 16, else 1), set the index to 0 and make the first. That is 0x0052B200 with the flag 0x100; otherwise 0x0052AD00, and 0x0052B200 when that fails and the flag 1 is set. A target starts the situation.
  - Started and asked to post: 0xEBB67EF7 is posted (0x00524D10) with the automaton. Otherwise the status is 5 and the message carries 0.
- **The cells** (0x0052C4E0) empty the set +0xAA8 and fill it by the situation's cell kind (slot 36):
  - 1, 5 and 6: 0x0052B450.
  - 2: 0x005299C0.
  - 3: 0x00529680.
  - 4: the rectangle of slot 40 (two uint16 halves each).
  - 7: the offsets of slot 41's list from the city's middle.

  +0xAC0 is then the count and +0xABC 0. With cells and the effects service (0x00B43D1C), each cell gets a marker effect (mission_selection_yellow, the water one for the kind 6) over its square into the map +0xAC4. +0xAD8 is set to the middle of the cells, where the target goes (0x00529560). The answer is whether there are cells.
- **The zone region** (0x0052B450). A spiral walk from the automaton's cell covers the box of the situation's slot 9 radius (below 1.0: the city's slot 91 times its width) and skips the box of slot 8. Each cell of the situation's zone type (for the kind 1 slot 37's type, else 0) is the seed of a region:
  - kind 6: 0x00527EA0, a recursive fill through the cell filter 0x00524E90;
  - kinds 1 and 5: a second spiral over the city. It keeps cells of the type that are not water and, for the kind 5, hold no network occupant; at each other cell it shrinks its bounds toward the seed.

  The first region whose count is from slot 42 to slot 43 (at most the city's cells; the least cut to the most) becomes +0xAA8.
- **The lot** (0x005299C0). A site of the situation's class (slot 38, 0x00528360), its lot (the lot manager's slot 14) and the lot's cells column by column, a column cut at slot 43. Then 0x00528140 adds whole rows (or columns, along the longer side) after the lot and then before it, up to slot 42's goal.
- **The flood** (0x00529680). The network service is asked for an occupant of the situation's type (slot 39) in a box around the automaton. The box is the automaton's position plus or minus the squared slot 9, with the filter 0x00A97D94 at the squared slot 8. One is taken at random; from its cell 0x00527D30 floods the cells of the network map whose occupants match the type, in four directions from a random one, for a count between slots 42 and 43 (8 to 50).
- **The target by the services** (0x0052B200, ret 4). The find_target script (+0xAEC) is asked first. Else, for each service (+0x9F8) and target ID, the service's slot 16 places the target near the automaton between the radii and its slot 15 makes it. The target is the made one's slot 5. With the argument it becomes the target (0x00528580), and +0xA64 is set.
- **The target by kind** (0x0052AD00, ret 4). First, with the flag 0x20 and 0x193A among the target IDs, the +0xAF8 site. Else by the kind:
  - 1 and 3: a candidate of the services' groups (0x00525640, 0x005245F0) at a distance from the squared slot 8 (the first target only) to the squared slot 9, measured at the automaton's height. With none, one at random of the network service's occupants in the slot 9 box that pass the group filter (0x00526610) and are not in +0xA68.
  - 2: the occupant on the link nearest by path (0x00554EA0), its slot 48.
  - 4: the create_target script (+0xAE8).

  With the argument the target is set (0x00528580).
- **The nearest link** (0x00554EA0, on the automaton's 0x8B99EE7C interface). The links of the 0x00B43D8C service's lists 3, 0xC, 8, 2 and 0xA, each found by the path finder (0x00B43DA8's slot 6) from the automaton's cell. The first network occupant in the cell of the shortest path is the answer.

Oddities kept (each a candidate for a later optional fix, see "The situation manager is finished"):

- 0x0052AD00's first loop asks the referenced set +0xA68 whether it holds the result so far (still null), not the candidate. A candidate already used is not skipped there (the network loop does check the occupant).
- 0x0052C4E0's middle takes the least z twice: its "greatest z" keeps the minimum, so +0xAD8 sits at the least z.
- 0x00529680's search box is the automaton's position plus or minus the squared radius (slot 9 squared), not the radius.
- Slot 36 started but not asked to post sets the status 5 and posts 0xEBB67EF7 with 0, while it answers that it started.
- 0x005299C0 cuts a column at slot 43 but never resets the count, so each later column gets one cell.
- 0x0052AD00 answers its target after releasing it; so do 0x0052B200 and 0x00554EA0.

Reimplemented like the earlier parts: one writer, every call's argument count checked against the callee's `ret`, then an independent audit of all 11 against the disassembly. 0x0052B450 was audited last, by hand from its stack frame. Every local, argument order and clamp matches, as do both walks (the second built by hand as the game does, with no start call) and the set's lower bound over SC4HashTable's primes for 100. The audit found no behavioural difference. Its notes are cosmetic:

- The squares are computed in double, where the game uses the x87 at 53 bits (the same values).
- Points and spiral walks are zeroed where the game leaves them uninitialized (they are written before use).
- 0x0052C4E0's early return is 0 in all of EAX; the game sets only AL.

### Test

`OPENSC4_TEST_AUTOMATA_SITUATIONS=5` adds ` G<cells>,<fills>,<targets>,<starts>,<flags>` to the fingerprint (header of [automata_autotest.cpp](../src/core/automata_autotest.cpp)). It runs on part c4's day, on a fresh manager like part c4's (slots 24, 35, 36 and 38 the test's, the random reseeded), with objects of the test's own. The city's zones, terrain, network map, network service, links and path finder are asked as the game asks them, all read only.

- The cells by kind (0x0052C4E0): no situation, none, the rectangle (and an empty one), the effects refused and missing, the offsets with one twice. Then the zone regions of 0x0052B450:
  - three zone types;
  - the least above the most;
  - the radius below 1.0 (the whole city);
  - the kind 5 and the kind 6;
  - the near box covering all.

  Then the lot of 0x005299C0 (wide and tall, with and without the column limit, no lot) and the network flood of 0x00529680 (two types, none near). Each with its markers and the target placed.
- 0x00527EA0 over a set and a cell object of the test's own (seeds that pass and fail, the limit, outside the width).
- 0x00527D30 and 0x00528140 directly: the five directions, the edges, a count of 0, rows and columns, the city's last row, a goal of 0 and one already reached.
- 0x0052B200: no situation, no place, none made, made, as the target already set, the script, no services.
- 0x0052AD00 by kind:
  - the +0xAF8 site, by the flag 0x20 and 0x193A;
  - candidates at four distances, the near radius dropped for a later target, not a candidate, as the target already set;
  - the network's occupants (near the network and far away);
  - the nearest link with and without the interface;
  - the script.

  And 0x00554EA0 directly at three places.
- Slot 36 for every kind and flag: the active one, no automaton, the kind 2 posted and not, the kind 5 with and without cells, the flags 0x100, none, 1, the kind 4, and the flag 0x20 with a second situation over the first.
- StartJoyride (0x00524E20, part c2) with and without an automaton, and 0x00524D80.
- The next target started: 0x0052BBC0 (none, one, past the last) and 0x0052C0E0 (the kind 3, and a script with a target of 0x5005).

The network cell nearest the city's middle (the network map's slot 13, as 0x00527D30 asks it) is where the floods and links start; flag 8 says one was found.

Where a function would post a message, start the U-Drive, make a marker or ask a lot, the global it uses is the test's own for that call only and is put back after it:

- the message server 0x00B43CCC (slot 36's 0xEBB67EF7, the joyride's and the next target's 0xABBBCA64 and 0xAC083D7D);
- the U-Drive's view 0x00B21D74 (set, so 0x00506850 does nothing);
- the effects service 0x00B43D1C;
- the lot manager 0x00B43D08 (0x005299C0's lot).

Dan approved these test-only swaps (2026-10-21). The target setter 0x00528580 is never reached with a new target. Where a target is set, the one found is the one already set, so 0x00528580 returns at once. The pointers the messages carry are hashed as the fakes' indices.

Not called by the test:

- 0x00528580 with a new target (it makes the target's marker and a prop controller, and has an SC4UIScale site).
- 0x00528360's own search (by the camera's view); only its +0xAF8 path.

Flags: 1 ran, 2 the city's cells used, 4 the joyride ran, 8 a network cell found, 0x10 a field left set (an error). `OPENSC4_TEST_C5_TRACE=1` logs each step's hash; 2 also logs the fakes' calls and the messages.

### Parity

2026-10-22, env p4env-c5, `OPENSC4_MUTE=1` on both sides, with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_SITUATIONS=5`. B is all twelve `simulators.automata.situations` groups (parts c1 to c5) against the original. The switch list includes `determinism.dispatch_info=ours`. Two jobs ran, with `harness.multi_instance=ours` on both sides. Snapshot c5-p1, the final build. No retries:

| Making Money d0000, 4 days | Rush Hour t0000, 7 days | The 12-save batch, 7 days |
| --- | --- | --- |
| 5 of 5 | 8 of 8 | 11 of 12 on the first pair |

The one difference, Making Money d1095, was in part c2's ` D` token only (its availability hash on day 4); the saved city's records were identical. Run alone with retries it was the same, 8 of 8, on its first pair.

All 11 functions were reached in every one of the 14 B runs (our first-call log lines). Each run's ` G` token was the same on both sides. It depends only on the city, since the test's fakes and the city's networks set it:

- G5C093DDE,7096E4E2,31A2CDAA,0102AD5D,F for Big City;
- G1F2E2D9A,AA0FC347,5EB3FAD5,4AAF69A1,F for Making Money;
- G70623F15,756615D3,6FFEA55A,23A39CEA,F for Rush Hour.

The flags are F everywhere: a network cell was found near each middle. In Rush Hour the nearest link (0x00554EA0) finds no link occupant and answers null; it runs all the same.

Not compared (see "Test"): 0x00528580 with a new target, and 0x00528360's own search.

## The situation manager is finished

Parts c1 to c5 make 143 of the class's 164 functions ours, in twelve groups, all off by default:

| Part | Functions | Groups |
| --- | --- | --- |
| c1 | 41 | object, life, queries, save |
| c2 | 33 | lua, starts |
| c3 | 28 | messages, list, popup |
| c4 | 30 | targets, steps |
| c5 | 11 | running |

All are `simulators.automata.situations.<group>`. The 21 that stay the game's are listed in the survey:

- the twelve adjustor thunks;
- five helpers with callers outside the class;
- 0x00528580 and 0x0052CC50 (SC4UIScale's sites), with the two helpers only 0x0052CC50 calls.

Each part's test reaches all of its functions. The parity of all twelve groups together is in "Part c5", "Parity".

Candidates for a later optional fix. Each is kept faithful now; a fix would be a separate switch, off by default, since it changes what the missions do:

- 0x0052AD00: ask +0xA68 for the candidate, not the null result, so the first loop skips a used target as the second does.
- 0x0052C4E0: take the greatest z for the middle (+0xAD8), so the kind 5's target sits in the middle of its cells.
- 0x00529680: search the box of the radius, not of the squared radius.
- Slot 36: a start that is not posted keeps the status 4 rather than 5.
- 0x005299C0: reset the column count, so each column of the lot is cut at the limit.
- Part c4's oddities: 0x00529560 drops 0x00526960's answer, and 0x00526960 squares into a signed sum.

## Part vb: the automaton base and cSC4Vehicle

### Survey

Three levels of classes are in play ([automata_vehicle.h](../src/core/automata_vehicle.h) has the layout):

- The automata core (constructor 0x005140A0, vtable 0x00A95AC0, 129 slots; "cSC4AutomatonCore", the name is ours). Every automaton class derives from it; the aircraft and the boats directly. Its code lies with the aircraft group (0x004FA700 to 0x00514340) and is not part of vb.
- The automaton base (constructor 0x005544D0, vtable 0x00A9C068, 176 slots; "cSC4AutomatonBase", the name is ours). The pedestrians and the road vehicles derive from it. It ends at +0x254.
- cSC4Vehicle (class ID 0x896E714A, factory 0x005148E0, constructor 0x005628F0, 0x2B8 bytes from a pool of its own at 0x00B0A0F8, 192 slots). The trailer, bus, getaway van, carjacked vehicle, tank, limo, the trains and the emergency vehicles derive from it. The "vehicle base 0x00A9D488" of the family table is cSC4Vehicle itself.

Six parts, each with its own vtable:

| Part | Core | Base | cSC4Vehicle | What it is |
| --- | --- | --- | --- | --- |
| +0x00 | 0x00A95AC0 | 0x00A9C068 | 0x00A9D488 | the automaton: slots 3 to 69 are the gzcom-dll cISC4Automaton header's 0 to 66 (Init 3, Shutdown 4, SetDestination 10, the flag test 31 and so on), the class's own after |
| +0x04 | 0x00A95A38 | 0x00A9BFE0 | 0x00A9D400 | the occupant (cISC4Occupant, 34 slots) |
| +0x08 | 0x00A95A1C | 0x00A9BFC4 | 0x00A9D3E4 | serializable, 7 slots: Write 3 (base 0x00551DE0, vehicle 0x0055F640), Read 4 (0x00554D20, 0x0055BCF0), GetGZCLSID 5 (0x00532240) |
| +0x0C | 0x00A959B8 | 0x00A9BF60 | 0x00A9D380 | 25 slots; 22 the deleting destructor, 23 and 24 Write and Read through +4 |
| +0x20 | 0x00A959A0 | 0x00A9BF4C | 0x00A9D368 | 5 slots (6 in cSC4Vehicle) |
| +0x38 | 0x00A95990 | 0x00A9BF3C | 0x00A9D358 | 4 slots |

The base overrides the core's slots 0, 3, 4, 10, 72, 93, 109, 110, 111 and adds 129 to 175; cSC4Vehicle overrides 0, 3, 4, 60, 65, 72, 93, 109, 110, 115, 116, 120, 121, 123, 124, 125, 129, 131, 132, 135, 140, 151, 158, 174 and adds 176 to 191. The core's AddRef and Release keep the count at +0x10 (the occupant part's); Release at 0 calls +0xC's slot 22, the deleting destructor.

Fields beyond the core's (the core's +0x40 position, +0x7C radius, +0x84 automata flags and +0x1A0 smoothed value are the ones this part touches): the base's +0x1E4 state, +0x1E8 a byte, +0x1EC the occupant it is on (referenced), +0x1F0 its network part (referenced; vtable 0x00A99E70 on a road), +0x1F4 to +0x1F7 the sides and the network kind, +0x200 a float, +0x204 a deque of 0xC-byte elements, +0x22C a route deque; cSC4Vehicle's +0x258 and +0x25C two objects held through a virtual base, +0x264 and +0x270 two points, +0x27C a deque of 8-byte elements.

The group is found by the two classes' vtables less the core's slots, plus the functions that write those vtables and the helpers only they call, plus the destructor bodies the derived classes share (0x00504E30, 0x00504E70) and six helpers cSC4Vehicle shares with the trains: 163 functions, 45,178 bytes. The survey's 318 and 70,504 had the trailer's and the trains' code (0x00555A30 to 0x0055B3E0) placed with it by neighbours; those belong to their own groups. Of the 163:

- **Stay the game's (25).**
  - Twenty `sub ecx,N; jmp` adjustor thunks: 0x005545C0 to 0x00554630, 0x005552E0, 0x005553F0 (base); 0x00505080, 0x005050B0, 0x005050C0, 0x005050E0, 0x005050F0, 0x00524520, 0x00524530, 0x00562A60, 0x00562A70, 0x00562A80 (cSC4Vehicle).
  - Two plain jumps into the core: 0x0055B6C0 (slot 60) and 0x0055B740 (slot 120).
  - Three accessors folded with other classes' code: 0x00504F70 (slot 136, +0x1F4; cSC4City::GetTerrain), 0x00505100 (slot 137, +0x1EC; cSC4City::GetCitySituationManager) and 0x00520080 (slot 143, +0x4C; four other classes).
- **vb1 (36, 3,155 bytes, done): the object and its queries.** Below.
- **vb2 (30, 4,317 bytes, done): life cycle, save and the setters.** Below.
  - Init (slot 3) 0x0054D790 and 0x0055B9B0; Shutdown (4) 0x00555310 and 0x005602F0 with 0x00554640, 0x00554280, 0x0055BE00 (the renderer 0x00B43DD0) and 0x0055AC20 (shared with the trains).
  - Write and Read (+8 slots 3 and 4): 0x00551DE0, 0x00554D20, 0x0055F640, 0x0055BCF0, with the +0x204 deque's push 0x00554230 and 0x00553780 and the managers' record list push 0x00554160 (both called by vb4's code).
  - The setters: 0x00504FC0 (154), 0x00505110 (178), 0x00505140 (180), 0x0053CC00 (158), 0x0055B690 (cSC4Vehicle's 158, a random float from 0x00B21D78), 0x0055B750 (184, a random byte), 0x0054DA00 (159) and 0x0054DA50 (160, the speeds), 0x0054EB20 (149), 0x0054D820 (135), 0x0054D830 (172), 0x0055BE70 (124, the vehicle manager's slot 69) and 0x0055CA60.
  - The debug texts (slot 93): 0x00553CF0 and 0x00562AC0 (static strings, `State: %d`).
- **vb3 (13, about 6,800 bytes, done): the base's movement on frames.** SetDestination (10) 0x0054E5B0; slots 129 to 134 (0x0054FDC0, 0x00550190, 0x0054E920, 0x00550760, 0x005504E0, 0x00550B60), 142 0x00554AB0, 151 0x00551300, 157 0x005510D0, 170 0x0054E790; helpers 0x0054E6F0 and 0x00551F70.
- **vb4 (22, about 6,500 bytes, done): the base's states on frames.** Slots 72 0x0054E050, 109 0x00550090, 110 0x005546E0 (with 0x0054E380), 111 0x005535D0 (with 0x00552B80, 0x0054ECC0), 145 0x005559D0, 147 0x00551480, 148 0x00551C20, 161 to 169 (0x0054E1A0, 0x0054E410, 0x005500D0, 0x0054E3D0, 0x00553AA0 with 0x00553720 and the deque step 0x00504C40, 0x00554870, 0x00550F40, 0x005527E0, 0x00552520), 173 0x0054D860.
- **vb5 (10, about 5,900 bytes, done): cSC4Vehicle's movement.** Slots 72 0x0055BA20, 129 0x0055CAF0, 131 0x0055D210, 132 0x0055D5A0, 151 0x0055D9D0, 121 0x0055C600, 109 0x0055CA90, 135 0x0055BE90, 125 0x0055DF50, 65 0x0055C8C0 (HandleCollision: a sound, a message).
- **vb6 (12, about 7,500 bytes, done): cSC4Vehicle's lanes and turns.** Slots 176 0x0055DBE0, 177 0x0055DD10, 188 0x0055C480 with 0x0055C350 and 0x0055BB10 (shared with the trains), 185 0x0055E1F0, 186 0x0055E550, 187 0x0055ED10 (1,760 bytes) with 0x0055EA20, 0x0055BF70, 0x0055C030, 191 0x0055DD50 (a model matrix: SC4Matrix::FromDirection, the renderer).
- **vb7 (14, about 6,500 bytes, done): cSC4Vehicle's queues.** Slots 182 0x0055FE60 with 0x0055CFD0, 181 0x00561910 with the +0x27C deque's 0x0055FA90, 0x0055FAF0, 0x005614B0, 0x00560250, and 0x0055F930 (entering the next occupant, slot 115's), 189 0x00561D20 with 0x00561A30 (the wall and cell tests), 190 0x005620E0 (the collisions: not a drawing function), 110 0x0055B6D0, and the trains' shared 0x0055F3F0 (the damage) and 0x0055F7B0 (an impact).
- **vb8 (1, 4,279 bytes, done): cSC4Vehicle's frame.** Slot 115 0x005603F0, the vehicle's frame (it calls vb7's 0x0055F930, 0x0055F3F0 and 0x0055F7B0), split off from vb7 for its size.

vb3 to vb8 run on frames, so the family's frame caveat applies (top of this page): the comparison of those parts is inside one run, or values from automata the test makes itself.

Drawing: the model, its animation and its matrix calls (0x0055DD50, 0x0055BE00 through the view 0x00B43DD0) belong to the renderer and are called at their addresses. Sounds and messages go out through 0x00602640 and SC4::PostMessage (0x0055C8C0, 0x0055D5A0, 0x00550760).

Mods: no NAM site and none of SC4UIScale's addresses is in the group. SC4UIScale's probe 0x00505370 is the managers' CreateAutomaton (parts v and aw), which makes these automata; it stays the game's there.

### What is ours

138 functions in one group, off by default: `simulators.automata.vehicle` ([automata_vehicle.cpp](../src/core/automata_vehicle.cpp)): vb1's 36 (kObject), vb2's 30 (kLife), vb3's 13 (kMove), vb4's 22 (kStates), vb5's 10 (kVehicleMove), vb6's 12 (kVehicleLanes), vb7's 14 (kVehicleQueues) and vb8's 1 (kVehicleFrame).

#### vb1

- **The object (16):**
  - The factory 0x005148E0 with the vehicle pool's allocate 0x0055B970 and free 0x0055F600.
  - The constructors 0x005544D0 (base) and 0x005628F0 (cSC4Vehicle; the vehicle manager's AddAutomaton calls it too).
  - The destructor bodies 0x00504E30 and 0x00504E70, the deleting destructors 0x005552F0 and 0x00562A90, and the deques' destruction 0x00504D90 and 0x00504CF0 with their range releases 0x00504CB0 and 0x00504C70.
  - QueryInterface 0x0054D7F0 (base, 0x8B99EE7C) and 0x0055B660 (cSC4Vehicle); GetGZCLSID 0x00532240.
- **The accessors (12):** slots 138 0x00504F80 (+0x200), 144 0x00504F90 (the route deque), 150 0x00504FA0 (the flag 8), 153 0x00504FB0 (+0x1E8), 156 0x00504FD0 (the +0x204 deque), 175 0x00504FE0 (the byte 0x00B21D28), the base's 140 0x00546020 (the position) and cSC4Vehicle's 140 0x00504FF0 (+0x264), 179 0x00505000 (+0x270), 183 0x00505010 (the +0x27C deque), 174 0x0055BF60 (the city's vehicle manager), and the +0x27C deque's back 0x0055F900.
- **The queries (8):**
  - Slot 146 0x0054DAB0: whether the point of slot 143 is within 16 x the byte 0x00B21CB8 of the position.
  - Slot 155 0x0054EA80: the unit vector from the position to a point; (0, 0, 1) at 1e-5 or less.
  - 0x0054EA30: the held test (the byte, the states 7 and 8, the flag 0x200000, the occupant's group 2).
  - Slot 171 0x0054D8D0: the side (of four) closest to a direction, optionally only the sides the network answers for; true at a dot product of 0.6 or more.
  - Slot 152 0x0054EB90: the network part's test, then the next occupant's for the other side.
  - 0x0050AAF0 (on a manager) and cSC4Vehicle's slot 123 0x005573C0: the automaton the vehicle runs into.
  - 0x0054FC30: the nearest automaton in x and z on a network occupant.

Kept as the game's: the 25 above; the core (constructor 0x005140A0, destructor 0x00513E60, QueryInterface 0x00505D80, AddRef, Release, the smoothed value 0x004FA960); the deques' own code (0x008EB800, 0x0049EB90, 0x006DA8D0, SC4RouteDeque), the pool's (SC4EdgePool::Grow, SC4Pool::ReleaseBlock, SC4Pool::ReopenBlock), the generator (cRZRandomSuper::Between) and the allocators, all called at their addresses.

GetPositionAddress 0x00546020 is 4 bytes long; its address entry lists the byte of `CC` padding after it, so the 5-byte entry hook stays inside, as part a3a's GetOccupant does.

Oddities kept:

- The vehicle's QueryInterface leaves the out pointer as it was for other interfaces until the base's clears it.
- The base's deleting destructor frees without a null test; the destructor bodies do not set the vtables back.
- 0x0050AAF0 keeps its best distance in its first argument's stack slot, which it starts at 0: only automata within the margin count, and ties go to the later one.
- 0x0054FC30 measures in x and z only (the y difference is the float SC4::FloatZero) and, when the key is the automaton's own occupant, starts at its own node.
- Slot 155 squares the unrounded z difference times its float copy.
- Slot 171 keeps its best dot product as a float (stored into an argument's slot).

Stack leftovers the game passes on, where ours passes zeros: slot 171's side to the network's slot 0xD4, and slot 152's +0x1F5 to the network part's slot 0x90 and +0x1F4 to 0x00B43D54's slot 0x3C, are bytes with three bytes of the caller's stack above them. The network part on roads (vtable 0x00A99E70, slot 0x90 0x0053D060) and 0x00B43D54's slot 0x3C (0x004AF7C0) read the byte only.

Return values: the game sets only AL in QueryInterface, slots 146 and 153, 0x0054EA30 and slot 152 (and leaves its caller's EAX above it); ours zero-extends. Every caller the audit found reads AL only.

Floats: 53-bit doubles, as the game's x87 runs. The distances keep the asm's order ((dz x dz + dy x dy) + dx x dx); floats are stored where the game stores them (the best distances, the drawn time, slot 155's factor). Compares keep the asm's handling of unordered values.

Reimplemented like the earlier parts: one writer, then an independent audit of all 36 against the disassembly, every vtable call's argument count checked against the callee's `ret`. The audit found no defect in the 36 functions. It found one in the test: the fingerprint read the word at +0x1E8, of which only the byte is written; that is fixed. The audit could not see the class of every object the queries call (slot 171's network, the occupant's slot 0x58); the argument counts are the game's. In a city the network part is of vtable 0x00A99E70, whose slot 0x90 (0x0053D060) is `ret 0x14` and reads the third argument's byte only.

#### vb2

- **The life cycle (9):**
  - Init (slot 3) 0x0054D790 (base: the core's Init 0x005101D0, then slots 105 and 134 unless the automata flags 0x4000 and 0x2000 are on) and 0x0055B9B0 (cSC4Vehicle: the flag 8 from the byte 0x00B0A0A8, slot 158, the base's Init, slot 176 when 0x00B43DDC's slot 9 or the byte 0x00B2296C says so). Both answer 1 at once when the occupant part says initialised.
  - Shutdown (slot 4) 0x00555310 (base: slot 132 and 0x00554640 when initialised, then the core's Shutdown 0x0050BA50) and 0x005602F0 (cSC4Vehicle: the base's, +0x258 and +0x25C released through their virtual base, the +0x27C deque's objects released and the deque emptied, the models).
  - 0x00554640 (the +0x204 deque's objects cleared and released, then the deque emptied), the two deques' clear 0x00554280 and 0x0055AC20 (the latter shared with the trains), 0x0055BE00 (the four models +0x2A4 taken off the view 0x00B43DD0 through its slot 0x74 and released) and 0x0055CA60 (the derived classes' operator delete: a 0x2B8-byte block to the vehicle pool, any other to SC4::operator_delete).
- **The save (4):** Write and Read (+8 slots 3 and 4, this = object + 8) 0x00551DE0 and 0x00554D20 (base: version 2, the core's, the references +0xB0 and +0x1EC, +0x1E4, the byte +0x1E8, +0x1F4, +0x1FC, the route's 3-byte cells) and 0x0055F640 and 0x0055BCF0 (cSC4Vehicle: version 2, the base's, the +0x27C deque's elements, the point +0x264, a second point, the float +0x260).
- **The pushes (3):** the +0x204 deque's push_back 0x00554230 and its slow path 0x00553780 (a new block, room in the map through 0x008EC5B0); the managers' record list push 0x00554160 (a 0x40-byte node, the record's rectangles by cSC4VehPedManagerBase::RecordRect).
- **The setters (12):** slots 154 0x00504FC0 (+0x1E8); cSC4Vehicle's 178 0x00505110 and 180 0x00505140 (+0x264, +0x270); the base's 158 0x0053CC00 (+0x200 zero) and cSC4Vehicle's 0x0055B690 (+0x200 drawn between the floats 0x00B21CF0 and 0x00B21CF4); 184 0x0055B750 (the flags 0x0201180A, +0x1E8 drawn below 50); 159 0x0054DA00 and 160 0x0054DA50 (SetSpeed to 0 or to the default speed, at 1.5 or 0.3 over slot 125's answer, 0.1 when that is not above 0); 149 0x0054EB20 (the flag 8, +0xB0 from slot 148, slot 124); 135 0x0054D820 (Remove(false)); 172 0x0054D830 (slot 173, Remove(false)); cSC4Vehicle's 124 0x0055BE70 (the vehicle manager's slot 69).
- **The debug texts (2):** slot 93 0x00553CF0 (base: "layer: %d  lifespan: %d  %s %s %s" with +0x1E4, slot 153, "edge: %d-%d", "route: %d, %d" and the core's text) and 0x00562AC0 (cSC4Vehicle: the base's text and " model:0x%8x 0x%8x 0x%8x" with the first model key), each into a function-local static string (0x00B22924, 0x00B22988) made on first use, its destructor given to SC4::atexit.

Kept as the game's for vb2: the core's Init, Shutdown, Write, Read and debug text (0x005101D0, 0x0050BA50, 0x0050AC00, 0x0050DFD0, 0x0050F0A0); the deque map's reserve 0x008EC5B0; SC4Stream's references and vectors, SC4CellDeque::Clear and SC4RouteDeque::PushBackSlow; the string code (cRZBaseString::Sprintf and Data, SC4String::AssignRange and AppendRange); the generator (cRZRandom::Range, SC4Random::Below); the renderer (the view 0x00B43DD0, 0x00B43DDC); the managers' CopyRecord and RecordRect; all called at their addresses.

vb2's oddities kept:

- cSC4Vehicle's Write writes the core's default point 0x00B09A04 where +0x270 would be; its Read reads the +0x27C deque's elements and the second point and drops them (the object its loop would release is a local it sets to null). After a load the deque and +0x270 are as the constructor left them.
- The base's Read takes +0x1E4 and +0x1FC through one local word: a failed read of the second leaves the first's value in +0x1FC.
- cSC4Vehicle's Write calls slot 0xEC of each +0x27C element's object without a null test.
- Slot 149 compares its argument's byte with the flag but sets the flag with the whole word.
- The base's Shutdown releases the +0x204 deque only when initialised; cSC4Vehicle's releases +0x27C always.

Locals the game leaves as stack garbage before a read that may fail (the version words, Read's words and cells, cSC4Vehicle's Write's two key words) and the allocator byte the debug texts pass to SC4String are zeros in ours; they differ only on a failed stream read.

Return values: the game sets only AL in both Inits, the four Writes and Reads and slots 172 and 184; ours zero-extends. cSC4Vehicle's Shutdown has EAX's upper bytes zero in the game too (0x0055BE00 leaves EAX 0); the base's Shutdown passes the core's EAX on, as slots 124, 135 and 149 pass their last call's. The void functions (the clears, 0x0055BE00, 0x0055CA60, the setters) leave EAX otherwise than the game; no caller the audit found reads it. One case: with 0x005602F0 skipped through OPENSC4_AUTOMATA_SKIP and our 0x0055BE00 kept, the game's 0x005602F0 no longer gets EAX 0 from it (its AL is still right).

Floats: slots 159 and 160 divide 1.0 by slot 125's answer and multiply by the factor in doubles, stored as a float; FCOM's below, equal and unordered all take 0.1. 0x0055B690 passes the two floats as doubles to cRZRandom::Range and stores its answer as a float.

Reimplemented the same way: one writer, then an independent audit of all 30 against the disassembly, every call's argument count checked against the callee's `ret`. The audit found no defect in the 30 functions. It corrected the address database: the +0x204 push's caller is slot 166 (0x00554870), not 165, and slot 149's argument is a word. The classes it could not see (the stream, the view's slot 0x74, the model's slot 0, 0x00B43DDC's slot 9, the +0x27C element's slot 0xEC): the game's own code reads its locals relative to ESP right after each call, which fixes their argument counts, and ours passes the same.

#### vb3

The base's movement on frames. cSC4Vehicle overrides slots 129, 131, 132 and 151 (vb5), so on the city's vehicles those four of ours run only where the vehicle's own code calls the base's; the pedestrians and the base's other slots run them directly.

- **SetDestination (slot 10) 0x0054E5B0:** the automata flag 4 cleared, the destination +0x4C set, the heading +0x58 its unit vector and slot 16 (null, heading); at a length not above 0 the flag 4 set and slot 102 (4, 0) instead.
- **The moves (2):**
  - Slot 129 0x0054FDC0 (frame seconds): the step (slot 125 x the smoothed speed, slot 34, x the seconds); with slot 88, an empty route and +0x204 deque and the flag 0x20, slot 79 and true. Else slot 161 (vb4), then the room ahead (slot 151; below 16 SetSpeed to +0xB0's speed at 0.5) caps the step; a step not above 0 sets the flag 0x20 and, when minus the radius is above it and the flag 0x40 is off, Remove(false) and false. Else slot 133 along the path into the destination, kept inside the city (city slots 89 and 90 less 0.5), its height from the occupant (slot 56) unless the network part is flat (its slot 16), slot 155 and slot 16; within 0.001 in x and z, slot 16 (destination, null) and the flag 0x20 instead.
  - Slot 130 0x00550190 (frame seconds): straight to the destination (reset to the position when a coordinate is below 0); arrived (the flags 0x20 and 4, slot 102 (4, 0), true) when the distance or step is not above 0 or the move reaches the distance (0 with the flag 8 and a vehicle within 1 by slot 123). Else false outside the city in x or z; slot 155; unless the flag 0x20000 is on, the traffic network map's slot 44 (position, point, 2 with slot 141 else 0xC49): Remove(false) when it says no, else the height from the map (slot 18); slot 16.
- **The path (3):**
  - Slot 133 0x005504E0 (step, from, out, index, path, leftover): out walks the path (+0x1F8 by default) from the index (+0x1FC) by the step, each next point offset by slot 157; true when the step ends inside a segment; past the last point false, leftover the rest and out moved by the whole step along the last segment.
  - Slot 157 0x005510D0 (point): the point moved sideways by +0x200 (plus slot 56's object's +0x24's +0x4C when +0xB4 has elements; nothing below 1e-4) across the path's first-to-last direction: its angle by 0x007F82F0 (FPATAN of the negated x and z), FSIN and FCOS, the side by the direction's dot product with (1/sqrt 2, 0, 1/sqrt 2).
  - Slot 151 0x00551300: the room ahead, the smaller of 16 or the path's last point's distance less the radius (0 below 1; with the flag 0x10) and 16 or +0xB0's point's (its slot 66) less the radius and 3 (with the flag 8).
- **The network (4):**
  - Slot 131 0x0054E920 (occupant, key, exact): the occupant's network part (slot 49), its path for the key (slot 3) or, with exact, one found from the position (slot 5); with two points or more, slot 132 and the traffic network map's slot 19 word for the key set to 0.
  - Slot 132 0x00550760 (occupant, groups, path, keep): +0x1EC, +0x1F0 (the part), +0x1F4, +0x1F8 and +0x1FC; without keep, slot 16 and SetDestination at the path's first two points (offset by slot 157, heights by the occupant). Into a tunnel (0x8A4BD52B by QueryInterface, after the occupant's slot 8 with 0x80000) slot 33 (0, 0.8), out of one slot 33 (1, 0.1), each with the sound object (0x00602640) slot 5 or 6 with 0xE when slot 57 (0x10) finds one. The traffic network map forgets the automaton on the old occupant (slot 31) and takes it on the new one (slot 30, after slot 160); with the flag 8, +0xB0 = slot 148 (SC4RefPtr::Assign).
  - Slot 134 0x00550B60 (Init calls it): false at the core's default point; the map's occupant at the position's cell (city slot 95, map slot 11) and its part; the best side of +0xA4 (slot 171) when slot 122 answers, or when +0x98 is set and +0xA4 is longer than 1e-5; the part's path from the position (slot 5 with the vehicle manager's slot 68); slot 132; +0x1FC the part's index of the position (slot 19); slot 16 with the path's first segment's direction, else the whole path's, else the heading.
  - Slot 142 0x00554AB0 (now): 0x00554640, slot 90 (1), the flag 0x2000 cleared; with now, true. Else the route's first cell popped and, of the map's occupants at it (slot 14), the first whose part finds a path towards the next cell's side (slot 9 with the automata's generator): slot 132, slot 16 at the path's first point (with the route now empty, the part's end point, slot 22, and the flag 4).
- **The side weights (2):** slot 170 0x0054E790 (network, weights): +0xA4 normalised, its best side and, for each of four rows the network answers for (its slot 0xD4), a share of the opposite side's weight (at least 100, times the absolute +0xA0 x 0.01) moved to the side, the rest not below 0. 0x0054E6F0 (slot 167's helper): the vehicle manager's weights (slots 42 and 72), slot 170 and the map's slot 6 with the table's row for the side.
- **The route's pop_front 0x00551F70** (3-byte cells; at a block's end the block freed by SC4::Free).

Kept as the game's for vb3: the core's slots they call (16, 33, 34, 56, 57, 79, 88, 90, 102, 122, 125, 141), vb4's slots 148 and 161, cSC4Vehicle's overrides, the traffic network map (city slot 40) and the network parts, the vehicle manager's slots, 0x007F82F0, the sound object 0x00602640 and its slots, SC4::Free and SC4RefPtr::Assign; all called at their addresses.

vb3's oddities kept:

- SetDestination and slot 130 take the y difference as the destination's y less itself: 0 unless it is infinite or NaN.
- Slot 170 moves a share of the larger of the opposite weight and 100, normalises +0xA4 without a zero test, and passes the network argument's own stack slot as BestDirection's out byte: the side word it then passes to the network's slot 0xD4 has the pointer's upper three bytes.
- Slot 133 compares the index unsigned and, past the path's end, moves out by the whole step along the last segment while answering false; it leaves leftover alone when the step ends inside the path.
- Slot 157 does not test +0x1F8 for null; slot 129 calls city slot 40 and slot 174 and drops the answers.

Stack garbage where ours passes zeros: the byte arguments with three bytes of the game's stack above them (slot 131's key byte 1 to the part's slot 5 and the map's slot 19, the side to the parts' slots 5 and 9, slot 170's row to slot 0xD4; every callee reads only the byte), and the locals the callees always write before a read (0x0054E6F0's weight table, slot 134's cells and out word, slot 142's out word).

Return values: the game sets only AL in slots 129, 130, 131, 133, 134, 142 and 170; ours zero-extends. Slot 151 answers a float in ST0. SetDestination and 0x0054E6F0 pass their last call's EAX on; slots 132 and 157 are void (no caller the audit found reads EAX).

Floats: 53-bit doubles, as the game's x87 runs; ours rounds to a float exactly where the game stores one, keeps the x87's unrounded values elsewhere (slot 157 keeps 1/sqrt 2 unrounded, the direction's z as a float and its product unrounded), and adds sums of squares in the game's order. FSIN and FCOS are the x87's own (inline asm); the angle is the game's 0x007F82F0.

Reimplemented the same way: one writer, then an independent audit of all 13 against the disassembly, every call's argument count checked against the callee's `ret` (the base's and cSC4Vehicle's vtables, the traffic network map 0x00A8FED8, the vehicle manager base 0x00A9BD88, the road network part 0x00A99E70, the network occupant 0x00AA8080). The audit found one defect: slot 131 passed the out word where the game passes the key's byte 1 as the map's slot 19's fourth argument (that slot builds its lane key from the byte); fixed. Everything else matched, including the compiler's interleaved pushes (slot 129's 0.5 pushed before +0xB0's slot 34 belongs to SetSpeed; slot 142's five pushes before the manager's slot 68 belong to the part's slot 9).

#### vb4

The base's states on frames. The core keeps a stack of states at +0xB4..+0xB8 (pointers; the last is the current one, its +4 the type, +0x10 a timer); the base answers four types itself: 0xD (waiting), 0x10 (parked), 0x15 and 0x1E (driving). cSC4Vehicle overrides slots 72, 109 and 110 (vb5, vb7), so on the city's vehicles those three of ours run where its code calls the base's.

- **The frame (slot 72) 0x0054E050 (a word, frame seconds):** the smoothed value's elapsed time (+0x1AC) on by the seconds x 0.001 while below its duration (+0x1A8); at the duration the target (+0x1A4) becomes the value and both are zero. Slots 83 and 103; with slot 86 (the flag 2) and not slot 109: unless slot 87 (the flag 4), the move (slot 129 with an occupant, a network part and a path and without the flag 0x2000, else slot 130), whose AL is the answer; with slot 87, not gone and not slot 165, Remove(false); slot 105. Then slot 102 (0, 0). Else true.
- **The state frames (3):** slot 109 0x00550090 (tail calls: slot 163 for the current type 0xD, 164 for 0x1E, else the core's 0x00508AA0); slot 163 0x005500D0 (the waiting state's timer down by the core's rate 0x00505AB0 x the seconds while the flag 0x20 is on, true while above 0; at its end slot 160, the flags 0x20 and 0x10 cleared and the traffic network map's slot 33 (occupant, automaton, groups, a wait), slot 53 without an answer); slot 164 0x0054E3D0 (slot 129 while the smoothed value is above 0, then the core's 0x00508AA0; true).
- **Entering and leaving (6):** slot 110 0x005546E0: for 0x10 a record on the vehicle manager's record list (+0xAB8; vb2's push with 0, 0, 5 and the occupant part), the entries object 0x00585110 (0x00B22C30, made on first use and registered with the view) and an entry at the position (0x0054E380 builds it: 0x5C bytes, then 0x00584250), the flag 0x40 cleared, the route and the +0x204 deque emptied, the core's 0x00511450, with an occupant answering its slot 9 for 0x403C1004 the state's +0x78 from it (when its slot 8 answers 0x80000), +0x228 zero and +0x8C -1, then the core's 0x00511450 again; for 0x1E SetSpeed (0, 0.5); else the core's. Slot 111 0x005535D0: for 0xD and 0x1E slot 160 and the flags 0x10 and 0x20 cleared (0x1E also the core's 0x0050E3F0), true; for 0x10 (unless the flag 1: the flag 0x2000 cleared and slot 134) the records +0x64 and +0x68 erased (0x00552B80), +0x74's slot 4 (0) and release, the entry removed (0x00584B30) and the entries let go (0x00583860), then the core's; for 0x15 slot 134 unless the flag 1, true; else the core's. The record list's erase 0x00552B80 (the node holding the record: cleared by 0x0054ECC0, unlinked, its +0x34 released, freed) and clear 0x0054ECC0 (stdcall: with the byte +0x30 and an object +0x2C the city's slot 47's slot 14; the object released; +0x34 zero).
- **The route (slot 145) 0x005559D0 (route, next, now):** false for an empty route; the route copied (SC4RouteDeque's assignment 0x00532430); with next, false when slot 142 is; slot 160 and true.
- **Around the automaton (4):** slot 147 0x00551480 (occupant, depth): on the occupant (its own, slot 137, or with the flag 0x2000 the map's at its cell) every other automaton the map has on it (slot 16) with the flag 2, of its own occupant type (the occupant part's slot 7) and not with the flag 0x40, gone, slot 57 (0x10) or the flag 0x200000: Remove(false) and slot 33 (0, 0x00B21C84 x 0.25); then while the depth is not below 0, slot 147 on the map's four neighbours (slot 15) with the depth less one. Slot 148 0x00551C20: the automaton ahead, from the steps (two or more) by 0x0054FC30, else from the occupant and then 0x00B43D54's neighbour on the side +0x1F4 with the opposite side. Slot 161 0x0054E1A0: with the flag 8, +0xB0 = slot 148 when +0xB0 is gone or the flag 0x10 is on; the flag 0x800, or the byte +0x33 without 4 and the flag 0x200000: the flag 0x10 cleared, slot 160 without +0xB0; not while waiting (slot 59 0xD): a wait of 1.7 when slot 141 is off and the network part's slot 13 says so for the kind, else the map's slot 33, whose answer sets the flag 0x10, pushes the state 0xD (slot 52 with 0x00B21C74 x 1000) at 1 with the flag 0x20, and stops (slot 159); else slot 159 with the flag 0x10 and a wait below 0.7, slot 160 otherwise. Slot 162 0x0054E410: along the automata ahead (slot 48, up to 50) with the flags 0x10 and 0x20, those without the flag 1 counted, the last vehicle (0xC7CC75F1) or 0x496E5783 among them kept; with a count above 2, false when the vehicle manager's weight for the kept one's groups (slots 72 and 42; the row byte 1, the column byte 0) x its slot 75 x 10, rounded (less 0.5, plus 1.5 x 2^23 as a float; one more when exactly the next integer), is below the count.
- **The steps ahead (8):** the +0x204 deque's elements are the steps (+0 an occupant, referenced; +4 its key, kind << 24 | entry << 8 | side; +8 the last step). Slot 165 0x00553AA0: slot 166; the first step's occupant released and the step popped (0x00553720; Remove(false) and false when it was the last or none is left); from the path's last point each step's key by slot 169 (true when any is found, the point then the end of that key's path); +0x1E8 counted down; slot 131 to the next step, and at the last step slot 79 with slot 88, else Remove(false). Slot 166 0x00554870: an empty deque given the occupant and +0x1F4 (0x00554230); then while it holds fewer than slot 175's byte plus one steps, the last one (not marked last, with an occupant) extended by slot 168 (with the flag 0x40) or 167 into a new step pushed at the back; one that cannot be is marked last and the answer is false. Slot 167 0x00550F40: the map's neighbour (slot 15) of the step's occupant, the side order from 0x0054E6F0, the first side the neighbour's slot 53 answers for, else among the sides to try and when not held (0x0054EA30) a path from the network part's slot 9, marked last. Slot 168 0x005527E0: along the route: the first cell's list (map slot 14), the cell popped (inline, as 0x00551F70); at the route's end the map's neighbour (slot 15 with 1) marked last and its first side from the key's that answers slot 53; else the first occupant of the cell's list answering slot 53 towards the next cell (1 at a lower z, 3 at a higher, else 0 at a lower x, else 2). Slot 169 0x00552520 (a deque iterator by value, point, out key, out distance): the network part's keys from the point (slot 6 with the kind, the entry and side bytes and 32 with the flag 0x40, else 4); one key: its path's first point's distance; more: the least of each key's distance plus, unless the step is the last, slot 169's for the next step from the key's path's end, plus 0x00B43D54's slot 27 x 0x00B21D2C (not below 0, from FLT_MAX); the vector freed. The deque's pop_front 0x00553720 and iterator step 0x00504C40.
- **Entering from a key (slot 173) 0x0054D860 (occupant, key):** the occupant's network part's path (slot 9 with the key's kind, the key shifted down 8, 0xFF, the generator, the occupant argument's own slot as the out word) and slot 132 (occupant, the out word, path, true).

Kept as the game's for vb4: the core's slots they call (5, 13, 19, 30, 31, 33, 36, 48, 52, 53, 57, 59, 61, 62, 67, 79, 83, 85 to 89, 102, 103, 105, 136, 137, 140, 141, 175) and functions (0x00508AA0, 0x00511450, 0x0050E3F0, the rate 0x00505AB0, the smoothed value 0x004FA960); vb3's slots 129 to 132, 134 and 142; vb1 to vb3's functions it calls by address (0x0054FC30, 0x0054EA30, 0x0054E6F0, 0x00554160, 0x00554230, 0x00553780, 0x00554280); cSC4Vehicle's overrides; the entries object (0x00585110, 0x00583860, 0x00584250, 0x00584B30); SC4RouteDeque's assignment, SC4CellDeque::Clear and Begin, SC4RefPtr::Assign and AssignFrom, SC4::Free; the traffic network map, the network parts, the occupants, the vehicle manager, 0x00B43D54 and the city's slot 47 object; all called at their addresses.

vb4's oddities kept:

- Slot 110 calls the core's 0x00511450 twice for the state 0x10.
- Slot 147 passes the occupant argument's own stack slot, its low byte the side, to the map's slot 15; slots 167 and 168 pass sides whose upper bytes are a pointer's (the neighbour, the automaton, the step argument).
- Slot 162 indexes the 4 x 4 weight table by the groups' bytes without a bound: past it the game reads its own stack (ours past its table). Sides are 0 to 3, so the city's automata stay inside.
- Slot 163 hands the map's slot 33 a wait word holding the object's address (the stack slot ECX was pushed into); slot 173 uses the occupant argument's slot as the out word.
- Slot 167 calls the network part's slot 9 without a null test; slot 169 takes a path's first point without testing the path, and skips a key whose next step has no key.
- Slot 169 adds the squares in two orders: (dy^2 + dz^2) + dx^2 for one key, (dx^2 + dy^2) + dz^2 for more.
- 0x00552B80 releases the node's +0x34 after 0x0054ECC0 has cleared it (the same word, the record's +0x2C): never an object there. Slot 111 releases the state's +0x74 without clearing it.
- Slot 162 keeps the last of the vehicle and 0x496E5783 automata it passes, not the first; its rounding is in effect a ceiling, an exact integer staying itself.
- Slot 163 reads the current state without testing the stack for one.
- Slot 168 marks the out step last before it asks for the neighbour at the route's end, so it stays marked when the answer is false; slot 166 ends at a last step without an occupant with false, without marking it.
- Slot 169 stores each key's distance in its point argument's stack slot.
- Slot 166 keeps one step local over its loop: the key stays from the first push or the last extension (stack garbage in the game when the deque was not empty at the start; no callee reads it).

Stack garbage the game passes where ours passes zeros: slot 148's side byte to 0x00B43D54's slot 0x3C, slot 168's opposite side, slot 169's entry and side words to the part's slot 6 (all bytes with three bytes of stack above them; every callee reads the byte), the entry's three bytes after +0x3C (slot 110), and the locals callees write before a read (slot 162's table, slot 165's key and distance, slot 166's key).

Return values: the game sets only AL in slots 72, 145, 162 to 169, 173, 0x0054ECC0 and 0x00552B80; ours zero-extends. Slot 109 and slots 110 and 111 where they end in the core's pass that call's EAX on; slot 111's true for 0xD, 0x1E and 0x15 is AL only.

Floats: 53-bit doubles, as the game's x87 runs; floats are stored where the game stores them (the smoothed value's elapsed time, the timer, slot 169's distances), compares use the unrounded values the game compares (slots 72 and 163 compare the sum, not the stored float), and keep the asm's handling of unordered values.

Reimplemented the same way: one writer, then an independent audit of all 22 against the disassembly, every call's argument count checked against the callee's `ret`. The audit found no defect in the 22 functions; its notes on the game are the oddities above. It could not see the classes of the city's slot 47 object, the state's +0x74 object, and whether 0x0054E6F0 and the part's slot 6 always fill their outs (ours starts them at zero, the game's are stack garbage); the game's own code reads its locals relative to ESP right after each call, which fixes the argument counts, and ours passes the same. While writing the test one slip was the test's: the core's slot 89 takes one word (0x00511450's state 0x10 case pushes it early), which a stub without arguments did not pop.

#### vb5

cSC4Vehicle's movement: its overrides of the base's frame, state frame, move along a path, path and occupant entry and room ahead, and its own steering, speed factor, drop and collision. The vehicle moves a front point (+0x264, the core's default point while unset) along the path and keeps the destination (+0x4C) half its radius behind it along a direction (+0x270) that it averages with the last one, (2 d + last) / 3, while that is set; +0x260 is the leftover of the walk, +0x254 a timer and the core's +0x94 the speed of the last move.

- **The frame (slot 72) 0x0055BA20 (a word, frame seconds):** slot 191 (seconds) with the automata flag 0x80000; the base's frame 0x0054E050 at its address (vb4's), whose AL is the answer; +0x254, unless within 1e-4 of 0, moved towards 0 by the seconds x 0.001: from below at 0 or past it zero and slot 176 (0, 0), from above (1, 0); slot 128.
- **The state frame (slot 109) 0x0055CA90 (frame seconds):** the base's 0x00550090 (vb4's); when it answers AL 0 with a current state, slot 185 for the state 0xE and 186 for 0x15 (seconds), whose EAX is the answer; else the base's EAX.
- **The move (slot 129) 0x0055CAF0 (frame seconds):** the step (slot 125 x the smoothed speed x the seconds); with slot 88, an empty route and the flag 0x20, slot 79 and true (unlike the base's, the +0x204 deque is not tested). Else slot 161, the room ahead (slot 151; below 16 with +0xB0, SetSpeed to +0xB0's speed at 0.5) caps the step; at a step not above 0 the flag 0x20 is set and, when minus the radius is above the step and the flag 0x40 is off, Remove(false) and false. Else the front point (the position when unset) is walked along the path by slot 133 into a new point (the index +0x1FC, the leftover +0x260): when that fails the flag 4 and slot 102 (4, 0); else the reached segment's unit vector (at 1e-5 or less +0x270, or (0, 0, 1) when that is unset), averaged with +0x270 when set; the destination half the radius behind the new point and slot 16 (destination, direction); +0x270 the direction, +0x264 the new point; the flag 0x20 cleared. In every case but the early one +0x94 is the distance the position moved over the seconds.
- **The path (slot 131) 0x0055D210 (occupant, key, exact):** the base's 0x0054E920 (vb3's), whose AL is the answer; with it true, a path and without the flag 0x2000: with +0x260 above 0, +0x260 capped by the room ahead (slot 151) and the path's first point walked by it (slot 133 into the front point); the direction of the segment reached, or of the path's last segment when the walk ran out (at 1e-5 or less +0x270), averaged with +0x270 when set; the destination and slot 16 as in slot 129; +0x270 the direction; the flag 0x20 cleared. Without +0x260 the network part's index of the front point on the path (part slot 19), its answer dropped.
- **Entering an occupant (slot 132) 0x0055D5A0 (occupant, groups, path, keep):** as the base's 0x00550760 (the occupant and its network part referenced into +0x1EC and +0x1F0, the groups, the path, +0x1FC 0; the tunnel's fade and sounds; the traffic network map's slots 31 and 30; +0xB0 from slot 148 with the flag 8), but without slot 160 before the map's slot 30, and with the front point: unless keep (the flag 4 cleared), the path's first two points offset sideways (slot 157), +0x264 the first, and slot 16 with the point half the radius behind it along their unit vector (no SetDestination and no heights from the occupant).
- **The room ahead (slot 151) 0x0055D9D0, a float:** from the front point: 16, or with a path and the flag 0x10 the distance to the path's last point (0 below 1.0); 16, or with +0xB0 and the flag 8 the distance to +0xB0's back (its position less its heading, slot 15, times its radius, slot 17) less 1, or to its position less 3 when that is nearer, less half the radius. The smaller (ties and unordered the second).
- **The steering (slot 121) 0x0055C600 (controls, frame seconds):** false without the template's movement entry (slot 67's +0x64, else 0x0046B4A0 on its +0xC). The controls' +0x28 moves by the entry's rate (+0x14; +0x10 when braking: moving forward with the pedal +0x7C not below 0, or backward with +0x80 not below 0) x the seconds x 0.001: up with +0x7C not below 0, down with +0x80, else 0. The turn is +0x2C, or with the flag 0x2000 +0x24 to +0x28 by +0x94 over +8 (clamped to 0..1). The controls' +0x5C less the seconds' share of the turn with +0x88 not below 0, else (when below 0) eased up to 0 by +0x30; plus it with +0x84 not below 0, else (when above 0) eased down to 0; clamped to +-(+0x20) with the flag 0x2000, else to +-pi/2. Then the core's slot 121 0x00509070 (controls, seconds), whose EAX is the answer.
- **The speed factor (slot 125) 0x0055DF50, a float:** the core's rate (0x00505AB0); with an occupant: unless the flag 0x1000 or the state 0x10, times the group table 0x00B21CA8 for the occupant's first group of 0 to 12 (its slot 22), or its third when the vehicle is off the ground (the occupant's group 0xC, or its group 2 inside the city, city slot 98, more than 5 above the terrain's slot 13), which then answers; off the ground or in the state 0x10 it answers as it is. Then times 0x00B21C88 x 0.6 when the bytes +0x1F4 and +0x1F5 match, times 0x00B21C88 when +0x1F5 is not +0x1F4's next side and the path has more than 3 points. Without the flag 0x40 the slope: the asin (the exe's 0x009F03B0) of the heading's y x 0x00B21CFC / 0x00B21CF8, at most 0x00B21CFC and below 0 as 0; uphill 1 less it, downhill 1 plus it, times the factor, unrounded in ST0.
- **The drop (slot 135) 0x0055BE90:** without the flag 1, with the flag 2, inside the city (city slot 98) and more than 5 off (above or below, the difference's absolute value) the higher of the terrain's slots 13 and 30 at the position: slot 52 (0xE, 0, -1.0, 0, 0, 0, 0); else the base's 0x0054D820 (Remove(false)). Answers that call's EAX.
- **HandleCollision (slot 65) 0x0055C8C0 (hit, a word not read):** with hit the message 0x49E64802 (SC4::PostMessage with five zeros) and, unless in the state 0x14, the heading turned by half a random unit vector (0x0080B730 on a clock-seeded cRZRandom of the stack; ours seeds it under `fixed_seed::SiteOverride` at the game's return address 0x0055C907) and normalised, the flag 0x10000000 set and slot 52 (0x14, 0, 5000.0, 1, 0, 0, 0); false when that answers false. Else true.

Kept as the game's for vb5: the core's slots they call (7, 13, 15 to 17, 30, 31, 33, 34, 36, 52, 57, 59, 67, 79, 88, 102, 128, 136, 148, 150) and functions (slot 121 0x00509070, the rate 0x00505AB0, the smoothed value 0x004FA960, 0x0046B4A0); vb3's and vb4's slots and functions they call (the base's frame, state frame and path at their addresses; slots 133, 157, 161); cSC4Vehicle's slots 176, 185, 186 and 191 (vb6); 0x0080B730, SC4Random's constructor, SC4::PostMessage, the exe's asin, SC4RefPtr::Assign, the sound object 0x00602640; the traffic network map, the network parts, the occupants, the terrain and the city; all called at their addresses.

vb5's oddities kept:

- Slot 129's early way out tests only the route, not the +0x204 deque the base's tests, and does not set +0x94.
- Slot 131, without +0x260, asks the network part for the front point's index and drops the answer.
- Slot 132 does not call slot 160 before the map's slot 30 (the base's does), and with keep leaves the front point as it was.
- The direction's average rounds x and y to floats before the third and keeps z unrounded; slot 131's unit vector goes through a float inverse, slot 129's does not; slot 131's y component stays unrounded when the walk ran out.
- Slot 151 measures +0xB0's back with the heading times the radius unrounded in x and rounded in y and z.
- Slot 125's off-the-ground case answers before the turn and slope factors; its slope uses 0x00B21CFC both as the factor and the limit.
- Slot 121 reads the smoothed value a second time unless the vehicle moves forward with +0x7C not below 0, and its easings compare the unrounded sum with 0 before storing it.
- HandleCollision leaves its generator on the stack without a destructor call and does not read its second argument.

Return values: the game sets only AL in slots 72, 129, 131 and 65 and where slot 121 answers false; ours zero-extends. Slots 109, 121 (past the template), 135 pass the callee's EAX on. Slots 125 and 151 answer in ST0 (125 unrounded where the game multiplies last).

Floats: 53-bit doubles, as the game's x87 runs; floats are stored where the game stores them (the step, the room ahead, the unit vectors, the halves, the controls), compares use the unrounded values the game compares, and keep the asm's handling of unordered values. Copies of whole points (the front point, the direction, the reading-point init 0x00B22978) are word copies, as the game's MOVs.

Reimplemented the same way: one writer, then an independent audit of all 10 against the disassembly, every call's argument count checked against the callee's `ret`. The audit found no defect in the 10; its notes on the game are the oddities above, and two more: slot 135 takes the absolute value of the height difference, so a vehicle more than 5 below the ground drops too; slot 72 branches on its compare after an FMUL, relying on the FPU leaving the condition codes alone. The averaging's first product uses the unrounded 1 x 1/3 where the others use its float, the same because the 1 is exact. It could check the argument counts on the other objects' vtables (the network part, the map, the occupants, the city, the terrain, the sound service, +0xB0) only against the game's pushes, which ours matches; ours runs the game's own asin 0x009F03B0.

#### vb6

cSC4Vehicle's lanes and turns: the lights and the shown flag, keeping a point to the occupant's cells, the falling and placed states, driving off the network with its throttle, its path and floor searches, and the models. Names are ours.

- **The lights (slot 176) 0x0055DBE0 (on, later):** nothing when +0x258 is already set as asked. Later: a time from cRZRandom::Range (0.2, 15) on the automata's generator 0x00B21D78 into +0x254, negative for off; slot 72 counts it down and calls slot 176 (0 or 1, 0) at 0. Else on: +0x258 and +0x25C (held through a virtual base) each released and made again by the core's slot 99 (1, the field); off: both taken off the core's models (0x0050B400, the vector +0x110..+0x114) and released.
- **Shown (slot 177) 0x0055DD10 (shown):** when its byte differs from the automata flag 0x80000's test, the flag set to the whole word; turned off, the models released (vb2's 0x0055BE00). AL 1.
- **Keeping to the occupant (slot 188) 0x0055C480 (from, to), shared with the trains:** when to is more than 1e-5 from from, the occupant at to's cell (city slot 95 for the cell, city slot 33's slot 6 for the occupant) gives its cells (slot 14, four ints), city slot 96 their corners, and 0x0055C350 runs on the four edges in turn with the circle around from through to; to ends at the crossing nearest the circle (the best starts at FLT_MAX).
- **An edge (0x0055C350, eight words):** the edge from (x0, the centre's y, z0) to (x1, the centre's y, z1) against the sphere (0x0055BB10); of the points found (the second root first when there are two) each one whose distance from the centre is below the best becomes it and is copied out.
- **The segment and the sphere (0x0055BB10, stdcall, six words):** the roots t of from + t (to - from) on the sphere, a, b and c as the x87 forms them (a and b stored as floats, b x b with b unrounded once); a negative or unordered discriminant answers 0. The larger t, in 0..1, into the first point, the smaller into the second (and into the first when it is the only one). AL the count.
- **Falling (slot 185, the state 0xE) 0x0055E1F0 (frame seconds):** the gravity 0x00B22980 (-0.000833) set once under the guard 0x00B22984; nothing with the automata flag 1. While the state's +0x34 is above 0: it less the seconds, the heading's y tilted by seconds x 0.01 x +0x2C within +0x30 +- 0.05 (+0x2C turned at the edge), normalised, slot 16 (no point, the heading). Else the position's y moved by seconds x +0x28; a timer +0x38 above 0 counted down, at 0 the effect +0x3C let go (its slot 4, released) and Remove(false); then +0x28 gains the gravity x seconds; outside the city (slot 98) Remove(false); at or below the higher of the terrain's slots 13 and 30 with the timer run out: +0x28 0.000278 and, above the ground (on water), a splash (0x00B43D1C's slot 7 with the key at 0x00B0A0D4 into +0x3C, its slots 8 with a placement block and 3), a fade (slot 33, 0 and a draw of SC4Random::Unit) and the timer the draw x 1000; on the ground the timer 0 and Remove(false). Slot 16 with the point. AL 1.
- **Placed (slot 186, the state 0x15) 0x0055E550 (frame seconds):** nothing with the flag 1. At the state's +0x28 exactly 0: SetSpeed (0.0042, 1), the heading normalised into +0x2C and +0x38, +0x38 turned by -pi/2 about y (FCOS and FSIN of the double 0x00A8EDD8) and turned back when it points away from +8's target (slot 6, its point slot 8). Below 1000: the speed x the seconds, when above 0, as a step along +0x38 (scaled to radius x 1.5 x seconds / 1000) plus the kept heading scaled to it, the y 0x00B43D54's slot 18 there; the position set and slot 16 with it and the unit vector towards it (the heading only when that is not a NaN); +0x94 0. From 1000: the heading back from +0x2C, slot 16 (no point), PopState when 0x00B43DA8's slot 25 (1, the cell) is above 3 or +8's slot 23 (the position, 0) answers false. +0x28 gains the seconds. AL 1.
- **Driving off the network (slot 187) 0x0055ED10 (a word, controls, frame seconds, other, a word):** nothing at 0 seconds. The heading turned about y by the controls' wheel +0x5C; the front point (+0x264; the position plus half the radius along the heading when unset) moved along it by the seconds and the controls' rear point +0x44 by their velocity +0x50; the unit vector from the rear to the front (the heading at 1e-5 or less), the rear moved along it and put on the floor (0x0055C030). Slot 189 (vb7's) with the front and the point the radius ahead of the rear: on a hit the front at the hit point (y the rear's) on the floor and the direction slid along the wall (less 1.1 x its share of the wall's normal, flat): below 0.9 long the speed 0, else the smoothed speed x its length; the controls' +0x28 0, the smoothed speed set at once to it when it differs, +0x2C it, +0x50 the direction, the rear the radius behind the front. Else the front the radius ahead of the rear (y the rear's) on the floor, the direction from the rear to it, the rear behind it and +0x50 eased towards the direction by other's +0x38. Slot 16 with the middle of the two and the direction. AL 1.
- **The throttle (0x0055EA20, controls, entry, reverse, limited, frame seconds), a float in ST0:** the controls' speed +0x2C plus +0x28 x the seconds, clamped to the entry's +0xC..+8 when backing (below 0 with the byte +0x228) or starting from 0 with reverse, else to 0..+8 (both scaled by +0x18 unless limited); with both pedals +0x7C and +0x80 at -1 eased towards 0 by seconds x 0.001 x +0x1C without crossing it; stored into +0x2C. Steeper than 0.2 (the heading's y) and moving the slope's way: x (4 - |y|) / 4 when limited, else 0 past 0.8 or x (2 - |y|) / 2; then plus |y| y x -0.0003 x seconds. 0 when the occupant part's slot 22 (0x4202) answers and the sign changed from +0x2C, or below 0 without +0x228. The smoothed speed set at once to it when it differs. Answers it.
- **The path by keys (0x0055BF70, stdcall: part, keys, point, key, index):** for each key of the vector, the part's path (slot 3, key, 0) and the point's index on it (slot 19, which may move the point): the first that leaves the point within 4 in y answers; else the point is put back. Null when none.
- **The floor (0x0055C030, controls, found, point, out):** found 0; null outside the city. The cell by x / 16 rounded with the float 1.5 x 2^23 (one more when x / 16 is exactly that); the ground from city slot 84's slot 13. Over the cell's occupants (0x00B43D54's slot 14; nodes +0 the occupant, +0x54 the next) with the flag 0x1F4F (slot 9), a network part (slot 49) with a path from the position (its slot 5 with 1, 0xFF, 0xFF): their height (slot 56) when the part's slot 20 (point, 1, 0) answers -1, or, without the flag 0x40341004, when nearer the point's y than what slot 20 left; the lowest below the point's y + 4 wins. A winner with the flag 0x80000 is held in the controls' +0x78 (SC4RefPtr::Assign), found 1; with 0x40341004 found 1; else the ground. With none, +0x228 clear, the ground more than 2 above the point and an occupant held in +0x78: its height there, found 1, and it answers. out: the point with that y. Answers the occupant.
- **The models (slot 191) 0x0055DD50 (a word):** the current one of +0x2A4..+0x2B0 (the byte +0x2B4; from the resource key +0x11C, SC4::GetKeyedResource, when missing) given a placement block (the bytes 2 and 5, the heading's matrix from SC4Matrix::FromDirection, the position, 1.0) through its virtual base's slot 10, and the view 0x00B43DD0 told (slot 28 when just made, else 30, with the model's slot 0); then each from the current one backwards given (slot 18) 1 and a fade from +0x1B0's smoothed value (at most 1) less a third each (not below 0). The byte moves on by one of 4.

Kept as the game's for vb6: the core's slots they call (13, 16, 30, 31, 33, 34, 36, 53, 56, 99) and functions (0x0050B400, the smoothed value 0x004FA960), vb2's 0x0055BE00, vb7's slot 189, cRZRandom::Range, SC4Random::Unit, SC4RefPtr::Assign, SC4::GetKeyedResource, SC4Matrix::FromDirection; the models, the view, the effects 0x00B43D1C, 0x00B43D54, 0x00B43DA8, the city, the terrain, the occupants and their network parts; all called at their addresses.

vb6's oddities kept:

- Slot 186's last branch reads the state's +8 and adds the seconds to its +0x28 after a PopState that may have freed it.
- The placement blocks of slots 185 and 191 leave their bytes 2 and 3 as the game's stack (ours 0).
- Slot 187 hands its own seconds argument's stack slot to 0x0055C030 as the found byte (ours a word of its own; the seconds are not read after), and does not read its first and last arguments; slot 191 does not read its argument.
- The throttle's easing branches on an FCOM taken before an FLD and two FMULs (the FPU keeps C0..C3), as slot 72 does.
- 0x0055C030 reads the held occupant's height at the point slot 20 left in the last loop pass, and answers a winner without either flag with the ground's y.
- Slot 186's turn keeps its last element unrounded, slot 187's rounds it; both build their zeros as 0 x sin and 0 x cos.

Return values: the game sets only AL in slots 177, 185, 186, 187 and 0x0055BB10; ours zero-extends. 0x0055EA20 answers in ST0. Slots 176, 188, 191 and 0x0055C350 answer nothing.

Reimplemented the same way: one writer, then an independent audit of all 12 against the disassembly, every call's argument count checked against the callee's `ret`. The audit found no defect; its notes are the oddities above. The first trial found one of ours the audit missed: four single-word copies (the front point's words in slot 187, the position's in slot 186) written as three-word copies; fixed before parity.

#### vb7

- **The occupants it has been on (slot 181) 0x00561910 (deque, count):** nothing with the automata flag 0x2000 (slot 31), without an occupant (+0x1EC) or when the count's byte is 0. Else an 8-byte element of the occupant (SC4RefPtr::AssignFrom into a null local) and the base's word +0x1F4; when the deque is not empty and its last element (0x0055F900) holds the same occupant, that element's object is cleared and released and the element popped (0x0055FA90); the element pushed (0x005614B0); then, while the deque holds more than the count (its size counted as vb2's Write counts it, unsigned), the front element's object cleared and released and the element popped (0x0055FAF0). The local's reference given back.
- **Onto the next occupant (0x0055F930, direction):** 0x00B43D54's neighbour (slot 15: the occupant +0x1EC, the side byte +0x1F4, 0, kind 1); none answers false. Of the occupant it points at (not tested for null) the network part (slot 49, not tested); slot 171 (that occupant, the direction, the side byte as the out byte, 1). The part's keys (slot 6 with an out vector, kind 1, the front point +0x264, the entry (side + 2) mod 4, the side slot 171 left, 4.0) and the first key's path (slot 3, key, 0); without a key or a path, the keys again with the side 0xFF, the first one's path, and that vector freed (SC4::Free). With a path: slot 132 (occupant, key, path, 1) and true. The first vector freed last. AL only.
- **The 8-byte deque's pop_back (0x0055FA90):** inside the finish's block the current back by 8; at the block's start the block freed (SC4::Free, when not null) and the finish on the node before, at that block's last element. The element's object released (a tail jump; the field is not cleared). EAX the release's answer, or 0 without an object.
- **Its pop_front (0x0055FAF0):** the front element's object released (not cleared); before the block's last element the current on by 8; at it the block freed and the start at the next node's block. EAX the new current.
- **Its push_back (0x005614B0, element):** at the block's last place 0x00560250; else the element copied to the finish (its object referenced) when the finish's current is not null, and the current moves on by 8 either way.
- **Its push at a full block (0x00560250, element):** a referenced copy of the element; SC4CellDeque::ReserveMapAtBack (1, 0) when fewer than two map slots are left after the finish's node; a new 0x80-byte block (SC4::stl_allocate) at the next node; the element built at the finish (when not null); the finish on the new block; the copy released.

- **The point behind (slot 182) 0x0055FE60 (point, out, distance, trail):** the point the distance behind the point along where the vehicle came from. The city's traffic network (city slot 40) is asked for and dropped. With a path (+0x1F8): its segments from the last backwards, each from its earlier point to its later one (the first later point the path's last), against the sphere of the distance around the point (0x0055CFD0, least 0.1, into out); the first that answers gives AL. Then the trail (a deque of 8-byte elements, an occupant and a key, of the +0x27C kind) from its back to its front: each occupant's network part (slot 49) and its path (slot 3, key, 0) walked the same way, the later point carried over from the path before (its first point), so a path's last point is never a segment's earlier one; a null occupant, part or path ends the walk. Else AL 0 and out the point less the distance along the unit vector of the path's segment at +0x1FC (when the path is there, the index is at least 0 and below the count less 1 and the segment is at least 1e-5 long, unordered not), or along the heading; x keeps the product unrounded. AL only.
- **The crossing behind (0x0055CFD0, from, to, centre, radius, out, least; this not read):** AL 0 when from and to are within 1e-4 on each axis (unordered go on). The unit vector u from from to to (1 / length as a float, no zero test); the segment against the sphere (0x0055BB10: the far crossing into out, the near one into a local starting as 0x00B22978's word). With two crossings the near one first: taken when more than 1e-5 from the centre and u . the unit vector from it to the centre is at least least (out = it, AL 1). With none AL 0. Else (one crossing, or the near one failed) the same test on out: AL 1 when it holds, else 0. AL only.

- **Entering a state (slot 110, over the base's 0x005546E0) 0x0055B6D0 (state):** the type 0x10: the state's +0x30 zero; with a template (slot 67) its entry (+0x64, else SC4AutomatonTemplate::FindInherited64 on its parent +0xC): the entry's +8 (the top speed) into the state's +0x34, 1.0 without an entry (without a template +0x34 is left alone). Then the base's slot 110 (vb4's), its EAX.
- **The damage (0x0055F3F0, controls, entry), shared with the trains:** called by slot 115 (0x005603F0) after it adds to the damage, and by 0x0055F7B0. The controls are the state 0x10's (slot 56); their damage +0x60 an unsigned word, against the entry's +0x5C. Below half of it nothing. Below it (injured): 0x00B43D1C's slot 7 with the name at 0x00B0A0DC ("udriveit_injured") gives an effect. At or past it, or unordered (crashed): city slot 95 (x, y) for a cell never read; SC4::ApplyTrafficEvent on the vehicle manager (slot 174) with the occupant part (+4), 2 and 30000 into the controls' +0x68 (the record vb4's slot 111 erases); SetAutomataFlag (0x8000000, 1); slot 102 (0xB, 1); slot 7 with the name at 0x00B0A0E0 ("udriveit_crash"). With an effect: the controls' old one at +0x74 stopped (its slot 4 with 0) and released, the new one held there, placed (slot 8 with a block: the bytes 1 and 1, the identity, the position, 1.0) and started (slot 3 with 0); the local reference released.
- **An impact (0x0055F7B0, dx, dy, dz, push, controls, entry), shared with the trains:** called by slot 115 (twice) and the trains' 0x005591A0. q = push / the entry's +0x48 (the mass), as a float. With the vector longer than 0.01 (0x00B0A0B4) or q above 2.0: the damage +0x60 plus (length - 0.01) x 2000.0 (0x00B0A0B8), times q as well unless q is below 5.0 (0x00A9D170), by __ftol2. Then 0x0055F3F0 (controls, entry) in every case. The speed: the vector's share along the heading x q plus the smoothed speed (0x004FA960), 0 when the sum is below 0, at most the smoothed speed; the smoothed speed set at once to it when it differs (or is unordered); the controls' speed +0x2C it and, at exactly 0, their +0x28 (the pedal) zero.

- **The wall test (slot 189) 0x00561D20 (from, to, hit, normal), this not read:** where the segment from from to to (x and z only) first enters a cell a car may not drive into (0x00561A30, with city slot 33's object and the city's size in cells, slots 93 and 94, each read from the city pointer afresh). The cells by x / 16 rounded as 0x0055C030 rounds them. 0 when both points are in one cell, when from's cell is not free (asked with from's x and z) or when to's cell is. Else a walk over the cells: each pass starts both t at FLT_MAX; while not at to's column the next x edge (16 x (cell + 1) going up, 16 x cell going down) gives its t (an unrounded quotient by the float x extent, stored as a float) and the z there; while not at to's row the next z edge gives its t and an x. The strictly nearer t moves the cell on along x (the z edge on a tie or when unordered), and 0x00561A30 is asked for the new cell with the last crossing computed. Free: the next pass, and 0 on reaching to's cell. Not free: AL 1 with hit and normal (two floats each, x and z): through an x edge the hit (the crossing's x, from's z + the float t x the z extent) and the normal (minus the step, 0); through a z edge the hit (from's x + the float t x the x extent, the crossing's z) and the normal (0, minus the step); going up the hit's coordinate on that axis less 0.1. AL only.
- **The cell test (0x00561A30, cdecl: cell x, cell z, lots, cells x, cells z, x, z), slot 189's only:** 0 outside the city's cells (negative cells signed, the rest unsigned) or under water (the terrain 0x00B43CF4's slot 34). The cell's power poles (the occupant manager 0x00B43D0C's slot 41 with the cell as both corners, the type 0x2890D4DE and no limit, into an SC4List of 0x00414270): each occupant asked for the pole interface 0xA8C86142, whose cell (its slot 8) equal to the cell makes it 0; each occupant and each pole released; the list destroyed (0x00413FE0) on every later way out. Then only the first of the cell's network occupants (0x00B43D54's slot 14, node +0): with the flags 0x1F4F (slot 9) it answers 1 without a support pylon (slot 73), else 0 when its altitude at the point (slot 56, stored as a float) is above the terrain's height there (slot 13, unrounded) + 1, 1 when not or unordered. Without one, a landfill occupant at the cell (the manager's slot 33, type 0x6A11BD2B) makes it 0. Else the lot at the cell (lots' slot 6 (x, z, 0)): 1 when none or its state (slot 33) is not 2 to 4; 0 when its zone type (slot 31) is not 7 (agriculture); a farm answers 1 without a building (slot 48) and 0 only when its building's occupant (slot 3) has a bounding box (slot 10, into a record of 0x00466E10) more than 10 across its diagonal (1 when unordered). The landfill's reference (if any) released before the answer. AL only.

- **Collisions (slot 190) 0x005620E0 (push out, share out):** not a drawing function. The vehicle's velocity is the heading scaled to the smoothed speed (0x004FA960), or the heading as it is when its square is not above 0. The occupant manager 0x00B43D0C's slot 37 puts up to 5 occupants of the vehicle type 0xC7CC75F1 into a list (a sentinel from the allocator 0x00B207E4, fetched from the framework when the cache is null). It searches the occupant part's box (its slot 10) grown by 24 (0x00A9D354) each way. When the list has entries it builds our rectangle (0x0050A310 with no offset and scale 1) and its area (the half sizes x 4). Each occupant other than our own part is tested in turn:
  - its box (slot 10) must cross ours (SC4::BoxesIntersect) by more than 1, in x or else in z;
  - it must answer the automaton interface 0xA9B40F05 (when it does not, it is skipped and not released);
  - unless that automaton has the flag 1: its rectangle, then we must be able to meet it (slot 73) and the rectangles must overlap (0x00506450).

  Each one that passes counts. Its velocity (slot 15 into three words of 0x00B22978, scaled to its slot 38 speed when the square is above 0) less ours gives a difference, its length, and the share (their area / ours). When the length is above the best so far (which starts at -FLT_MAX) or the share is 2 or more: the difference into push, the share into share, and the best becomes the length. When the share is below 2, its slot 65 is called with us and the middle of the boxes' overlap (the vb5 HandleCollision on a vehicle). In the state 0x10 (slot 57), the sound object (0x00602640) gets slot 6 and then slot 5, each with 7. Each occupant that answered the interface is released. The manager's slot 48 takes the list whenever slot 37 answered true (even with the list empty). Then the list is emptied (SC4List::Clear2) and its sentinel freed (the allocator's slot 4). AL the count.

Kept as the game's for vb7: the core's slot 31, slots 132 and 171, 0x0055F900 (vb1's, at its address), SC4RefPtr::AssignFrom, SC4CellDeque::ReserveMapAtBack, SC4::stl_allocate, SC4::Free, 0x00B43D54, the occupants and their network parts; city slot 40, the occupants' slot 49, the network parts' slot 3, 0x0055BB10 and 0x0055CFD0 (from slot 182),; the base's slot 110 (0x005546E0), SC4AutomatonTemplate::FindInherited64, SC4::ApplyTrafficEvent, SC4Matrix::FromDirection, the smoothed value 0x004FA960, 0x0055F3F0 from 0x0055F7B0; the vehicle's slots 30, 67, 102 and 174, the city's slot 95, the effects 0x00B43D1C and their effect's slots; the occupant manager's slots 33 and 41, the power poles' interface and slot 8, the network occupants' slots 9, 56 and 73, the terrain's slots 13 and 34, the city's slots 33, 93 and 94, the lots object's slot 6, the lot's slots 31, 33 and 48, the building's slot 3 and its occupant's slot 10, SC4List's constructor 0x00414270 and destructor 0x00413FE0, the record constructor 0x00466E10; slot 189 calls 0x00561A30 at its address; the occupant manager's slots 37 and 48, the allocator and the framework, SC4::ReleaseRef, SC4List::Clear2, SC4::BoxesIntersect, the rectangle's 0x0050A310 and 0x00506450, the core's smoothed value 0x004FA960 and slots 15, 31, 38, 57 and 73, the other vehicle's slot 65 and the sound object 0x00602640,; all called at their addresses or slots.

vb7's oddities kept:

- 0x0055F930 hands 0x00B43D54's slot 15 the byte +0x1F4 and the network part's slot 6 the side and the entry as words with three bytes of the game's stack above each byte (ours 0).
- 0x0055F930 tests neither the neighbour's occupant nor its network part for null.
- Slot 181 compares the last element with the occupant only (not the word): a repeat of the same occupant is moved to the back with the new word.
- Slot 181 tests the occupant before the count byte, and the last element against +0x1EC read again after the AssignFrom.
- 0x005614B0 moves the finish on by 8 even when its current is null (the element not built).
- 0x0055FA90 and 0x0055FAF0 release the popped element's object without clearing it; slot 181 clears it first, so its pops release nothing.
- Slot 182 asks the city for its traffic network (slot 40) and drops it.
- Slot 182 reads the path's last point before its count, from before the begin when the path is empty; with fewer than 2 points its index starts at 2^32 - 1 or - 2 and the game counts down doing nothing until it is below the count (ours starts there: the same calls).
- Slot 182's walk starts the trail iterator at the start and jumps to the finish (a reverse walk), and keeps a dead copy of the element's iterator in its frame.
- Slot 182's fallback tests the index against the count less 1 unsigned: an empty path with an index at 0 or more reads the points past its end.
- 0x0055CFD0 stores its 1 / length into its own `from` argument's stack slot; on AL 0 after a crossing, out keeps what 0x0055BB10 wrote there (slot 182 then overwrites it or tries the next segment).
- 0x0055CFD0 receives the vehicle in ECX and does not read it.
- Slot 110 reads the state's type without a null test (vb4's slot 110 tests it).
- 0x0055F3F0's crash asks city slot 95 for the cell of the position's x and y (not z), into its own two argument slots, and never reads it.
- 0x0055F3F0's crash lets go of its effect local before asking for the crash effect; it is always null there.
- 0x0055F3F0 makes the heading's matrix (SC4Matrix::FromDirection) and never reads it: the effect is placed with the identity. The block's bytes 2 and 3 are the game's stack (ours 0).
- 0x0055F7B0 adds damage when q alone is above 2.0, with (length - 0.01) then negative: a short hard impact lowers the damage (and the unsigned sum may wrap through __ftol2's low 32 bits).
- 0x0055F7B0 branches on an FCOMP of q and 5.0 taken before an FSUB and an FMUL (the FPU keeps C0..C3), as the throttle does.
- 0x0055F7B0 tests the unrounded sum against 0 but keeps the rounded float; the cap at the smoothed speed is taken against the unrounded smoothed value.
- 0x0055F7B0 and 0x0055F3F0 write their own argument slots (q over push, the cells over the controls and entry); ours use locals.
- Slot 189 does not read its this.
- Slot 189's z edge crossing takes its x as from's x + t x the z extent (the x extent was meant); when both edges are computed in a pass, the crossing handed to 0x00561A30 is the z edge's, even when the step is along x, and an x step's hit then takes that x as its own (its z is recomputed from the x edge's float t).
- The hit is pulled 0.1 back only when the step is up (+1); going down it sits on the edge.
- 0x00561A30 looks only at the first of the cell's network occupants, and a network occupant with the flags 0x1F4F but no support pylon answers 1 without the landfill and lot tests.
- 0x00561A30's pole cells are two stack words never set before the first pole's slot 8 (ours 0); the found byte and the altitude are kept in its argument slots.
- 0x00561A30 asks the lot's state (slot 33) twice.
- The rectangles are stack in the game. When the automaton has no model (slot 82 null), 0x0050A310 leaves the byte +0 and the axis +0x1C/+0x20 as they were: the first time that is stack garbage (ours 0), after that the previous occupant's. Our area still comes from the half sizes, zeroed in that case.
- Push and share are overwritten for a share of 2 or more whatever the length. Such a large occupant (twice our area or more) is not told through slot 65. The best length still follows it, even when the length is lower.
- The z square of the difference is the unrounded difference times its stored float (FST then FMUL). x and y use the floats. With the other's velocity scaled, its x and y go unrounded into the difference.
- The interface's reference is released through the occupant (slot 2 on the occupant pointer, not on the interface).
- The sound object is fetched twice (0x00602640) for its slots 6 and 5.
- AL only in the game (the upper bytes come from the allocator's free); ours zero-extends.

Return values: the game sets only AL in 0x0055F930; ours zero-extends. 0x0055FA90 passes the release's EAX on (0 without an object); 0x0055FAF0 answers the new current. Slot 181, 0x005614B0 and 0x00560250 answer nothing. The game sets only AL in slots 182, 189 and 190 and 0x0055CFD0, 0x00561A30; ours zero-extends. Slot 110 passes the base's EAX on; 0x0055F3F0 and 0x0055F7B0 answer nothing.

Reimplemented the same way, five writers in parallel (one per group of slots), then an independent audit of all 14 against the disassembly, every call's argument count checked against the callee's `ret` (0x00554400, ApplyTrafficEvent, is a thunk to the ret 0xC 0x00554040, though a `ret` scan of the thunk says 4). The audit found no defect; its one note (a comment of slot 189's) is fixed.

#### vb8

- **The driven frame (slot 115) 0x005603F0 (frame seconds), the U-Drive car's frame:** the controls are the current state (slot 56) and the entry is the template's (slot 67: its +0x64, else SC4AutomatonTemplate::FindInherited64 on its parent +0xC). Without either the answer is AL 0. When the damage +0x60 (unsigned) is above the entry's most (+0x5C) the answer is AL 1 at once.
  - Whether the key +0x80 was up (at or below 0) is read before the keys' frame 0x00508C50 runs. That frame moves the controls' 13 key timers from +0x7C on by the seconds and calls slot 121 and the occupant manager's slot 17; its AL 0 (with the automata flag 1) answers 0. Before it, the sound 0x12 goes to the event system (0x00602640): slot 5 when the smoothed speed is below 0, else slot 6.
  - The floor under the front point +0x264 (0x0055C030). The car is off the network when, with the controls' +0x8C at -1, there is no floor occupant, or otherwise when the floor was not found (its found byte 0). A change goes to slot 102 (0xA, off) and the byte +0x228. Then the throttle (0x0055EA20: the key pressed this frame and off; limited on a floor occupant), its answer dropped.
  - The frame runs in ceil(seconds / 33) steps (the float magic on seconds x 1/33 + 0.5, one less when that is exact). Each is seconds / steps long and covers the smoothed speed x that.
  - On a floor occupant (and not off): the sounds 5 and 0x10 stopped (slot 6), the haze stopped (slot 45 with the name "haze", an SC4String from the text 0x00B0A0D8 points to), and the heading turned about y by the wheel +0x5C. With the flag 0x2000 or the front point unset, it finds a path:
    - the occupant's network part (slot 49);
    - slot 171 (the occupant, the turned heading, a side byte, 1);
    - the part's keys (slot 6: kind 1, the front, the entry 0xFF, the side, 16.0) and the nearest path (0x0055BF70);
    - without one, the keys again with the side 0xFF, copied over the first list (SC4Uint32Vector::CopyAssign; the new list freed) and tried once more;
    - then slot 132 (occupant, key, path, 1) and, with a path, its index into +0x1FC; both lists freed.

    The flag 0x2000 is then cleared.
  - Each step: slot 133 along the path by the step to the next point, else onto the next occupant (0x0055F930), else Remove (slot 13, false) and AL 1 with nothing more.
    - The direction is the path's segment at +0x1FC when it is above 1e-5 long, averaged (2 d + +0x270) / 3 unless +0x270 is unset. For a shorter segment it is +0x270, or (0, 0, 1) when that is unset.
    - +0x4C becomes the next point. Slot 16 gets the point half the radius behind it and the direction. +0x270 becomes the direction and the front the next point, and the flag 0x20 is cleared.
    - The controls' rear +0x44 is set the radius behind the next point, and +0x50 and +0x38 become the direction.
    - Then the collisions (slot 190): on one, 0x0055F7B0 (the push, the share, the controls, the entry) and no more steps.
  - After the steps, the turn onto another path: when the wheel is past the entry's +0x34 to the right, or past minus it to the left with a path. Slot 171 runs on the own occupant with the turned heading, then the network part's path (slot 5: 1, the byte +0x1F5, the side, the front, a key out, 0). A new path (not the current one) whose point (slot 19 on a copy of the front) stays within 4 in y gives slot 132 (occupant, key, path, 1), +0x1FC its index and +0x4C the point.
  - Then the lane change, when moving (slot 38 above 0) on a path with the byte +0xE6 or +0xE7 set. A point 5 to that side of the heading (added to the heading, not to the position) goes to the network part's slot 39 (+0x1F4's word, the position, it, 0.8, a point out, a key out, an index out). The path for its answer (slot 3) is used when its point (slot 19, its answer dropped) stays within 4 of the position's y: slot 132 (the floor occupant, the key, null, 1) and +0x1FC the index.
  - Off the network:
    - the flag 0x2000 is set, and slot 132 (the floor occupant or null, 0, null, 1) runs when that is not the own occupant;
    - with an own occupant and +0x228, the sound 5 (slot 5);
    - without one and moving (the smoothed speed not 0, or unordered), the sounds 0x10 and 5 (slot 5) and the haze started (slot 44, the name, 0, 1); else the sound 0x10 stopped and the haze stopped;
    - each step: slot 187 (the step's seconds as a word, the controls, the step, the entry, the floor occupant), whose AL becomes the answer, then the collisions as above.
  - At the end:
    - the controls' effect +0x74 is placed at the position (its slot 8 with 0x0055F3F0's block);
    - the sound source **+0x70 gets the position's x and z, -atan2(heading x, heading z), its +0x28 plus the position's y, and the speed x the heading's x and z x 1000;
    - when the disaster service 0x00B43D3C's slot 10 says a disaster runs, its disaster 0xC4C90997 (slot 7, held for the call) is asked, as 0xA9B5AB9E (slot 5), for the level at the position's cell (x and z / 16, rounded as 0x0055C030 rounds them). A level above 0, x 3 and rounded by the float magic, is added to the damage and 0x0055F3F0 runs;
    - +0x94 becomes the distance moved over the seconds.

    AL is the keys' frame's, or the last slot 187's.

Kept as the game's for vb8: the keys' frame 0x00508C50 (the core's), the event system 0x00602640 and its slots 5 and 6, SC4String::FromText and Destroy, SC4Uint32Vector::CopyAssign, SC4::Free, SC4AutomatonTemplate::FindInherited64, the smoothed value 0x004FA960; vb6's 0x0055C030, 0x0055EA20 and 0x0055BF70 and vb7's 0x0055F930, 0x0055F3F0 and 0x0055F7B0 (ours, called at their addresses); the vehicle's slots 13, 16, 30, 31, 38, 44, 45, 56, 67, 102, 132, 133, 171, 187 and 190; the network parts' slots 3, 5, 6, 19 and 39; the effect, the sound source, the disaster service and its objects.

vb8's oddities kept:

- The key +0x80 is read before the keys' frame and again after it, so the throttle's reverse means "pressed this frame and off".
- The +0x8C test picks which floor answer counts: at -1 the floor occupant, otherwise (and when unordered) the found byte.
- The step count is a ceiling made with the float magic (+0.5, then one less when exact). 0 seconds gives 0 steps and seconds / 0 is NaN, so +0x94 becomes NaN.
- The turn keeps the cosine unrounded on the x87 (vb6's turn rounds it); the rows are otherwise vb6's.
- The direction's average doubles x through a float and y and z unrounded. z's third is unrounded; x's and y's go through a float. Slots 129 and 131 round differently.
- The keys' second list is copied over the first (CopyAssign) instead of swapped, and the copy, not the original, is searched; the new list is freed at once.
- The lane change's point is the heading plus 5 to the side, not the position plus it.
- The lane change drops its path's index answer (slot 19) and takes the index from slot 39; slot 132 then gets the floor occupant and a null path.
- The turn's slot 171 and slot 5, and the first block's slot 6, get the side (and the +0x1F5 byte) as words whose upper three bytes are the game's stack (earlier floats). Ours passes 0 above the bytes.
- Slot 171 writes 0xFF into the side first, so the game's own 0xFF preset is redundant.
- The throttle's two bytes have the upper bytes of ECX and of an earlier stack word above them; ours zero-extends them.
- Slot 44 is a jump to +0x18C's slot 12 (rt.py reads it as ret 8); the game pushes three words and the callee pops 0xC.
- Off the network the answer is the last step's slot 187 AL, not the keys' frame's.
- The sound source's speed is the smoothed speed read before the steps, not the one at the end.
- AL only in the game; ours zero-extends.

Reimplemented by hand from the Ghidra listing (no translator), then an independent audit against the disassembly. The audit found two branches inverted by a misread JP: the +0x8C floor test and the sound 0x12's slot. Both were fixed before parity; the rest matched.

### Test

`OPENSC4_TEST_AUTOMATA_VEHICLE=1`, with `OPENSC4_TEST_AUTOMATA=2` or more, drives the base and cSC4Vehicle on the test day at the game's addresses, so the same test runs on both sides (header of [automata_autotest.cpp](../src/core/automata_autotest.cpp)). Vehicles run on frames, so everything in the fingerprint comes from objects the test makes and removes itself. The automata's generator (0x00B21D78) is saved and seeded first and put back after: the drawn times are the same on both sides, and the game's own draws do not change.

Into the fingerprint as ` V<object>,<accessors>,<queries>,<nearest>,<flags>,<setters>,<save>,<texts>,<life>,<move>,<frame>,<enter>,<states>,<ahead>,<steps>,<vmove>,<vframe>` (vb2's four after the flags, then vb3's three, vb4's three and vb5's two; the flags 4 when vb3's part ran, 8 when vb4's did, 0x80 when vb5's did):

- Four vehicles from the factory, each held by one reference: their fields after construction; a base object built and deleted (0x005544D0, 0x005552F0); QueryInterface for six interfaces at both addresses and through the occupant part (each AddRef given back) and GetGZCLSID.
- The accessors (addresses as offsets; the manager compared with city slot 43; the deque's back with its expected address).
- Slot 146 at points around its distance, slot 155 at points near and far, 0x0054EA30 over its byte, state, flag and an occupant of the test's, slot 171 for nine directions with and without a network of the test's, slot 152 with a network part of the test's answering yes and no.
- 0x0050AAF0 on a manager of the test's whose list holds the four vehicles at set positions, radii and flags, for five margins.
- vb2's setters on the first vehicle: slot 154 with three values, 178 and 180, the base's and cSC4Vehicle's 158 (twice: the generator is seeded), 184 (the flags put back), 159 and 160 with and without the automata flag 0x80 (the smoothed value's four words: its elapsed time stays 0 on the test's vehicles), 149 with five values (the flags, +0xB0 set or not), 124, and the operator delete 0x0055CA60 with null, a pool block (the page's count) and another size.
- 25 elements pushed on the +0x204 deque (three blocks; objects of the test's that count their references), walked and released by 0x00554640; 12 more pushed and the deque emptied by 0x00554280. Three records pushed on a record list of the test's (0x00554160).
- Write then Read: the first vehicle's fields and three +0x27C elements (objects of the test's answering slot 0xEC) written by 0x0055F640 into a memory stream, the bytes after the core's part into the fingerprint (the core's own part, which 0x0050AC00 writes alone into a second stream to find its length, to the log only); the stream given 50 route cells (two blocks) and read into the second vehicle by 0x0055BCF0, and twice with a wrong version (cSC4Vehicle's, the base's); the second vehicle's fields, route and empty +0x27C deque; the second vehicle written again and the base's part compared with what was read.
- The debug texts of both classes on both vehicles, with and without a model key.
- Init and Shutdown: the third vehicle by cSC4Vehicle's (Init twice), the fourth by the base's, each given +0x204 and +0x27C elements and the third objects at +0x258 and +0x25C held through a virtual base; their answers (AL), flags, fields, deques and the reference counts after Shutdown. Then slot 172 on the fourth and slot 135 on the third.
- Then each vehicle's last reference is released: the deleting destructor through +0xC's slot 22, the deques, the pool.

To the log only: the core's part of the save (its size and a hash), the sizes read and written, the second vehicle's debug text, and the Init and Shutdown answers (AL); for up to 24 of the city's vehicles, slots 146, 150, 138, 0x0054EA30, slot 123 with margins 0 and 2 and 0x0054FC30 on the vehicle's occupant; and the first one's network part's vtable.

Reached (our first-call log lines): all 36 in every B run of Making Money and Rush Hour; 34 in the four Big City runs, where slot 123 (0x005573C0) and 0x0054FC30 are never called (the test calls them on the city's vehicles only, and Big City has none). The factory, the pool's allocate, both constructors, QueryInterface, slot 150, 0x0054FC30, slot 140, slot 175, 0x0054EA30 and slot 174 run by the game at the load; the rest by the test. In the trial (Making Money d0000, 4 days) the token and the city log were the same on both sides.

vb2 reached: all 30 in every B run of vb2-r1 (Making Money, Rush Hour and Big City alike: the test makes its own vehicles). Both Reads (0x0055BCF0, 0x00554D20), both Inits and cSC4Vehicle's slot 158 (0x0055B690) run by the game at the load; the rest by the test. In the trial the token and the log were the same on both sides.

vb3 adds `,<move>,<frame>,<enter>` to the token. Its calls run on a base object of the test's (0x005544D0) whose vtable is a copy of the base's with slot 174 answering a vehicle manager of the test's, and with the city global 0x00B43CEC pointing at a city of the test's while they run: its traffic network map (slots 6, 11, 14, 18, 19, 30, 31, 44), a size of 2048 x 1024 and cells of 16; any other city slot is counted into the flags (0x200). So nothing of the real city is read, and the values do not follow frames. Two of the object's fields are held for the test: its byte +0x33's low bits are cleared (with bit 4 the core's slot 16 would index it in the occupant manager 0x00B43D0C) and the automata flag 0x100000 stays on (else slot 16 would put it on the view's update list); both are put back, with +0x98 and its references cleared, before the object is deleted.

- move: SetDestination for six targets; slot 157 on two paths for five offsets; slot 133 along a five-point path for seven steps and five start indices, at offsets 0 and 1.25, and on the object's own path and index; slot 151 at three places with the flags 0x10 and 8 and an automaton ahead of the test's; 50 route cells pushed and 49 popped (0x00551F70); slot 170 over three side masks and four directions (with +0x98, +0xA0 and the flag 0x4000 varied) and with no network; 0x0054E6F0 for four sides.
- enter: slot 132 into a lane, a tunnel (QueryInterface 0x8A4BD52B), an occupant without a network part, a part without a path, and none; slot 131 to four occupants with a path and without, exact or not; slot 134 at the default point and over five occupants and three +0xA4 cases, then with a one-point path; slot 142 over six route cells with a cell list of three occupants.
- frame: slot 129 for 12 frames of 0.25 s, flat or not, with an automaton ahead or not; its early way out; steps of 0 and -2 (stopped, removed); slot 130 towards five goals with the automata flag 0x20000 on and off and the map's slot 44 answering yes and no, four frames of 0.5 s each.

The fakes mix their arguments (the bytes the game passes with stack above them as bytes) and count their references into the hashes.

vb3 reached: all 13 in every B run of vb3-r2. Before the test day the game itself reaches up to nine of them on the city's own automata (slot 170, 0x0054E6F0, slots 131, 132, 133, 157, 151, 129 and 134 in Rush Hour t0000, fewer in the others, none in four Big City and Making Money d0000 runs); SetDestination, slot 130, slot 142 and 0x00551F70 only by the test in the days before. In the trial (Making Money d0000, 4 days) the token was the same on both sides.

Not covered by the fingerprint: the tunnel's sound call (slot 57 finds nothing on the test's object), slot 132 with the flag 8 (slot 148 is vb4's), the real map's slot 6 (the fake answers it: the real one reads the live map).

vb4 adds `,<states>,<ahead>,<steps>` to the token (the flags 8 when its part ran). Its calls run on a second base object of the test's, held as vb3's is (+0x98, the byte +0x33's low bits, the flag 0x100000; its state stack +0xB4..+0xBC put back before it is deleted) and referenced once (the record slot 110 pushes holds its occupant part and lets it go again; the count is 1 at the end). Its vtable is the base's with the core's slots the vb4 code calls (13, 19, 52, 53, 61, 62, 67, 73, 79, 83, 86 to 89, 102, 103, 105, 131, 132, 134, 141, 142, 159, 160) and the vb4 and vb3 slots driven at their own addresses (109, 129, 130, 148, 163 to 166) answering a probe of the test's, which mixes each call and its words (the test's pointers as indices) and answers what the case sets; slot 174 answers a vehicle manager of the test's with slot 75 and a record list at +0xAB8. The city global points at vb3's city of the test's (its map now also answering slots 15, 16 and 33, the city its slot 47), 0x00B43D54 at a service of the test's (slots 0x3C, 0x40, 0x6C) while the calls run, and 0x00B09964 is set to 2 for slot 163. The states the test makes are 0x240-byte blocks whose own slot 0 is the probe's; the core's 0x00508AA0, 0x00511450 and 0x0050E3F0 run on them at the game's addresses (the types are ones whose cases call only the probe's slots and the core's SetFlag, CheckFlag and fade). The entries object 0x00B22C30 is the game's: slot 110 adds an entry and slot 111 removes it in the same call sequence; its handles are not mixed.

- states: 0x0054E380 on an entry of the test's; slot 72 over four smoothed values (below, at and past the duration) and the probe's answers for slots 86, 87, 109, 129, 130 and 165, with and without an occupant, network part and path and the flag 0x2000 (96 calls); slot 109 with no state and with four current ones (0xD, 0x1E, 0x30 and the first of two); slot 163 over five timers, the flags 0x20 and 0x80 and the map's wait answer; slot 164 with and without a speed over three timers (the core's frame counting one out); slots 110 and 111 for null, 0x1E, 5, 0xD, 0x15 and 7 with and without the flag 1, and for 0x10 without an occupant, on a lane and on a tunnel lane answering its slot 9 (the second record and +0x74 set before the last leave); 0x00552B80 over three records and one not on the list, 0x0054ECC0 with the byte +0x30 (the city's slot 47); slot 145 with an empty route and a 45-cell one, with and without slot 142.
- ahead: slot 147 on an occupant with eight automata of the test's (one of each case) and its neighbours at depths 1, 0 and -1, from the object's own occupant and from the map's at its cell (the flag 0x2000), and with none; slot 148 over three steps and over one with the occupant, a bare occupant and none, the neighbour on or off, slot 73 yes and no; slot 161 over three automata ahead (none, one, one gone), seven flag sets and five ways (the byte +0x33's 4, the part's slot 13, slot 141, the state 0xD, the map's three answers and two waits); slot 162 over chains of 1, 2, 3, 5, 9 and 55 (the cap of 50), three occupant types and two states (one weight exactly an integer), and without a manager; slot 173 over five occupants and two keys.
- steps: 0x00504C40 over 24 steps and 0x00553720 over 25 (three blocks, objects of the test's that count their references); slot 169 over nine key counts (0, 1, 3 on two steps) with and without the flag 0x40, from the first step (the second's answer added) and the last; slot 167 over three neighbours, two side masks, three tries (held at 4) and three ways (slot 141, no neighbour); slot 168 on an empty route, one cell (the neighbour), a cell ahead in each direction with and without a cell list, and 46 pops of 50 cells; slot 166 with slots 167 and 168 the test's (fails, last steps, no occupant, a last step already) with and without the flag 0x40, then the game's once; slot 165 without a manager, an occupant or with the flag 0x2000, with no steps and over five step cases with slot 88 on and off.

To the log only: the tuning floats 0x00B21C84, 0x00B21C74 and 0x00B21D2C, slot 175's byte (the steps wanted), the fake city's traps, the reference counts left, the object's count and whether records are left.

vb4 reached: all 22 in every B run of vb4-r1 (the test drives each at its address). Before the test day the game itself reaches up to 13 of them on the city's own automata (Rush Hour t0000: slots 72, 109, 110, 111, 148, 161, 163, 165, 166, 167 and 169, the deque's pop_front and step); slot 148 in every run, also Big City and Making Money d0000. 0x0054E380, 0x00552B80, 0x0054ECC0 and slots 145, 147, 162, 164, 168 and 173 only by the test in the days before. In three trials (Making Money d0000, 4 days) the token was the same on both sides.

Not covered by the fingerprint: slot 72's, 165's and 166's callees at their real addresses (the probe answers them; each is driven at its own address); slot 110's entry as the game's entries object keeps it, and its handle; slot 162 with groups past the table (the game reads its stack); slot 110's occupant answering slot 9 but not slot 8; the pointer bytes slots 147, 167 and 168 pass above their sides (the fakes mix the low byte).

vb5 adds `,<vmove>,<vframe>` to the token (the flags 0x80 when its part ran; 0x800 when the factory gave no vehicle, 0x1000 when the fake city trapped). Its calls run on a cSC4Vehicle from the factory, referenced once and held as vb4's object is (+0x98, the byte +0x33's low bits, the flag 0x100000, the state stack, and its +0x254 to +0x27C fields put back before its last reference goes). Its vtable is a copy of cSC4Vehicle's 192 slots with the core's and the vehicle's other slots the vb5 code calls answering vb4's probe (13, 33, 52, 53, 57, 67, 79, 83, 85 to 88, 102, 103, 105, 112 to 120, 128, 148, 160, 161, 163 to 165, 176, 185, 186, 191; slot 16 mixes its point and heading, slot 17 answers the object's radius) and the slots driven at their own addresses (109, 129, 130) the probe's too; slots 121, 125, 131, 132 and 151 and the base's 133 and 157 stay the vehicle's, so slot 129 runs the real speed factor and room ahead, and slot 131 the real slot 132. The probe's answers now cover 192 slots. The city global points at vb3's city of the test's (its slot 98 now answering inside x 1500 and z 900), the terrain global 0x00B43CF4 at a terrain of the test's (slots 13 and 30), 0x00B43D54 at vb4's service and 0x00B09964 is 2 while the calls run; for HandleCollision the message server 0x00B43CCC points at one of the test's that counts the posts (the message object is the game's, made and let go by 0x005F9800). The controls slot 121 is given are a 0x240-byte block whose words +0x15C to +0x18C are -1, so the core's slot 121 after it takes its no-sound way.

- vframe: slot 151 from three front points, with and without a path, three automata ahead (none, two of the test's with position, heading, radius and speed) and the flags 0x10 and 8; slot 125 without an occupant and over six group masks, four flag sets (0x1000, 0x40, 0x80) and five heading slopes, the state 0x10 every seventh case, the position in and out of the city at four heights, the side bytes and three paths; slot 135 over four flag sets, three x and four heights; slot 109 with no state and the states 0xD, 0x1E, 0xE, 0x15 and 0x30 with the probe answering 0 and 1 (the core's 0x00508AA0 runs for 0xE, 0x15 and 0x30); slot 72 over seven timers (inside 1e-4, below and above 0, crossing 0), the flag 0x80000 and the probe's answers for slots 86, 87 and 109 (28 calls); slot 121 with no template, a template with the entry, one finding it through its parent (0x0046B4A0) and one without, three speeds, the flag 0x2000 and four +0x94 values, over 16 pedal and wheel cases (the whole controls block mixed).
- vmove: slot 132 into a lane, a five-point path, an occupant without a network part, none and the lane again, with the path given and not, keep and not (flag 8 on, so slot 148 is the probe's); slot 131 to three occupants over four +0x260 values, three flag sets and exact or not, with an automaton ahead in two of three cases and +0x270 unset in one of four; slot 129 over three speeds, four flag sets and four ways (slot 88's way out, a route cell, two automata ahead), each from the path's start (slot 132 with keep) with the front point and the direction unset or set and the index 0 or 3, three frames of 0.5 s each; HandleCollision with hit 0 and 1, in the state 0x14 and 7, slot 52 answering 0 and 1.

To the log only: the collision's turned heading (the draw is the game's clock-seeded generator; it was the same on both sides in the trial), the fake city's traps, the reference counts left, the posts and the object's count.

vb5 reached: all 10 in every B run of vb5-r1 (the test drives each at its address). Before the test day the game itself reaches up to seven of them on the city's own vehicles (Making Money d0090 and later, Rush Hour: slots 72, 109, 125, 129, 131, 132 and 151); slot 125 in every run (Big City and Making Money d0000 too). Slots 121, 135 and 65 only by the test in the days before. In the trial (Making Money d0000, 4 days) the token, the collision's heading included, was the same on both sides.

Not covered by the fingerprint: the heading HandleCollision turns (the log only); slot 132's tunnel sound (slot 57 answers 0); slot 72's callees at their real addresses (the probe answers them); slot 125 with the city's own tuning table (the game's, read on both sides); the core's slot 121 on controls that would play its sounds.

vb6 adds `,<vlanes>,<vdrive>` to the token (the flags 0x2000 when its part ran; 0x4000 when the factory gave no vehicle, 0x8000 when the fake city trapped). Its calls run on a cSC4Vehicle from the factory held as vb5's is (also its +0x2A4..+0x2B4, +0x11C and +0x1B0 put back), its vtable vb5's copy with the vehicle's slots under test (176, 177, 185 to 188, 191) its own again and slots 56 (the current state), 99 (a model) and 189 (the wall test: it writes the test's hit point and normal) the probe's; its occupant part's vtable (+4) a copy with slot 22 the probe's. The city global points at vb3's city of the test's (now also answering slots 33, 84 and 96), the terrain at vb5's, 0x00B43D54 at a ground of the test's (slots 14 and 18), 0x00B43D1C at effects of the test's, 0x00B43DA8 at a counter of the test's and the view 0x00B43DD0 at a view of the test's while the calls run. The models and lights are objects of the test's held through a virtual base; the floors are occupants of the test's with network parts.

- vlanes: 0x0055BB10 over six points as both ends, three centres and five radii (one a NaN); 0x0055C350 over four edges, four radii and three starting bests; slot 188 from two points to six (one inside 1e-5, one on a cell without an occupant); slot 176 with the lights there or not, on 0, 1 and 0x100, later 0, 1 and 0x200; slot 191 over seven model masks and three fades (the current one missing twice, its key not a model's); slot 177 with the flag on and off and 0, 1 and 0x100 (the models let go); 0x0055EA20 over four speeds, three accelerations, +0x228, reverse, limited 0, 0x100 and 1, three slopes, the pedals and slot 22 (432 calls); 0x0055BF70 over five key sets and two heights.
- vdrive: slot 185 tilting (24 cases) and falling at two x, three heights and three timers, two frames each (the splash on water, the removal on the ground, the timer counting the effect out); slot 186's first frame with a target or none on either side and two slopes, its moves at three speeds with and without +0x38 and the kept heading (the NaN way among them), and its end with the counter at 2 and 5 and slot 23 0 and 1; 0x0055C030 over six floor lists, four heights and +0x228 (in and out of the city, out given or not); slot 187 with slot 189 answering 0 and 1, the front set or not, floors or none, over six wheel, heading and speed cases (one at 0 seconds).

To the log only: the fake city's traps, whether the core's model vector was empty, the references left and each section's hash.

vb6 reached: all 12 in every B run of vb6-r1 (the test drives each at its address). Before the test day the game itself reaches only slot 176 (Making Money and Rush Hour, not Big City); the rest only by the test. In the trial (Making Money d0000, 4 days) the token was the same on both sides.

Not covered by the fingerprint: slot 176's models as the core's slot 99 makes them (the probe answers it); the real view, effects and resources (the test's answer them; slot 191's resource lookup fails on the key's type); slot 185's second test of the flag 1 (Remove is the probe's, so it never sets it); slot 187's slot 189 at its real address (vb7's).

vb7 adds `,<vqueue>,<vhit>` to the token (the flags 0x10000 when its part ran; 0x20000 when the factory gave no vehicle, 0x40000 when the fake city trapped). Its calls run on a cSC4Vehicle from the factory (its states, +0x1EC..+0x1F4, +0x1F8, the byte +0x33 and the flags put back; the +0x27C deque emptied), its vtable vb6's copy with the vehicle's slots under test (110, 181, 182, 189, 190) its own again and slots 132 and 171 the probe's (slot 171 mixes its arguments, not the side's stack address, and writes the side 3). Slot 110 runs inside vb4's state setup on its object (slot 67 answers the test's template once, then null, so the base's state 0x10 path sees none as vb4's does); its hash goes into vqueue. While the vqueue calls run the city global points at vb3's city of the test's, 0x00B43D54 at neighbours of the test's (slot 15) and 0x00B43D1C at vb6's effects (the effect with an AddRef); the walls swap the city, the terrain, the occupant manager 0x00B43D0C and 0x00B43D54 for their own, the collisions the occupant manager (its lists made from the game's node allocator, which the game's SC4List::Clear2 frees). The occupants, their network parts and the paths are the test's.

- vqueue: the +0x27C deque's push_back over 40 elements (six with a null object; across blocks, so 0x00560250 runs), 9 pop_backs, 20 pop_fronts, 12 more pushes and the emptying; slot 181 over eleven steps (a count of 0, the same occupant twice, the flag 0x2000, no occupant, a count byte of 3 in 0x103, counts 2 and 1) and across blocks (20 kept, then 2); 0x0055CFD0 over four segments (one shorter than 1e-4 on every axis), four centres, three radii and four leasts (one a NaN); slot 182 with no path, a two- and a three-point path and a zero-length segment, five points and two distances, the index 0, -1 or past the end, and no trail, a trail of three and one of four whose front occupant has no network part; 0x0055F930 over seven cases (no neighbour, empty key lists, a first key without a path and the second list's key); 0x0055F3F0's injured way at five damages with and without an old effect; 0x0055F7B0 over six vectors, four pushes, two masses and two speeds (the smoothed target off by one every third call), and once into the injured way; slot 110 on the state 0x10 with the template's entry, its parent's, neither and no template, and on three other types.
- vhit: slot 189 over 16 walks (8 worked by hand; power poles, a landfill, a lot, a diagonal with equal crossings, a long walk down in z, the city three cells wide, a high network occupant, a negative from) and 0x00561A30 directly over about 30 cells (the bounds, water, the lot states and zones, a building box of 10 and just over, the landfill with and without an object, the network occupants, the poles in the cell, elsewhere and refusing the interface); slot 190 over 16 cases (slot 37 refusing, an empty list, our own part only, boxes apart or crossing by 0.5, the interface refused, the flag 1, rectangles apart, CanMeet 0, a share below 2 and of 2 or more, a crossing in z only, two and five others in two orders, the same velocity). Each also logs its answer against a hand-worked one (the log only; all matched in the trial).

To the log only: the traps of the fakes, the references left and each section's hash.

vb7 reached: all 14 in every B run of vb7-r1 (the test drives each at its address). Before the test day the game itself reaches slot 181 with the deque's 0x0055FAF0 and 0x005614B0 and slot 110 (Rush Hour t0000), slot 110 alone (Making Money d0090), none elsewhere; the rest only by the test. In the trial (Making Money d0000, 4 days) the token was the same on both sides.

Not covered by the fingerprint (vb7): 0x0055F3F0's crash (ApplyTrafficEvent writes the city's vehicle manager's record list), so neither a damage at or past the entry's most nor 0x0055F7B0's unsigned wrap of a negative sum (both lead there); slot 182 on a path of fewer than 2 points (the game's index counts down from 2^32) or an empty one; slot 190's sound (slot 57 answers 0) and its rectangles without models; slots 132 and 171 under 0x0055F930 at their real addresses (the probe answers them); the base's state 0x10 path after slot 110 with a template there (the template is answered once).

vb8 adds `,<vframe8>` to the token. Its flags: 0x80000 when its part ran, 0x100000 when the factory gave no vehicle, 0x200000 when the fake city trapped, 0x400000 when there was no event system and it was skipped. Slot 115 is driven at its address on a cSC4Vehicle from the factory. Its states, +0x1EC..+0x1FF, the byte +0x33, the flags and both vtables are put back after.

Its vtable is vb7's copy with these slots the test's:
- 44 and 45, the haze (the name's text is mixed, not its stack address);
- 121, the probe's, under the keys' frame;
- 171, vb7's without the side byte the game hands it;
- 187, the probe's AL;
- 190, a scripted collision: push (0.5, 0, -0.25) and share 1.5 on the chosen call.

Its occupant part's slot 22 is vb6's probe. While it runs, these globals point at the test's:
- the city: vb3's, with vb6's terrain as its ground;
- 0x00B43D54: a cell list with one floor of vb6's kind and no neighbour, so 0x0055F930 answers false;
- 0x00B43D1C: vb6's effects;
- the occupant manager 0x00B43D0C: one answering the keys' frame's slot 17;
- the disaster service 0x00B43D3C.

The floor's and +0x1F0's network parts, the paths, the effect, the sound source, the template and the entry are the test's too. The sounds go to the game's event system, and every sound id slot 115 starts (0x12, 5, 0x10) is stopped after.

- vframe8: 14 frames over fixed seconds (0, 16, 33, 50, 99, 400). They cover:
  - the damage above the entry's most, and the keys' frame refusing (the flag 1);
  - on a floor occupant (+0x8C at -1, or found with +0x8C NaN): the flag 0x2000 with the front unset (the first key list, and an empty one copied over); steps along a three-point path with +0x270 set or unset, and a zero-length segment; steps running off the path (Remove);
  - the turn both ways past the wheel's most, and the lane change both ways;
  - collisions on the first and second step into the impact (the injured way once);
  - the disaster's level at 0, 2.5 and 3, and a disaster that does not answer the interface;
  - off the network (no floor; a floor not found with +0x8C at 0), with and without an own occupant, moving or stopped, and slot 187 answering 0 and 1.

  Each frame mixes AL, the position, +0x4C, the front, +0x270, +0x1FC, +0x260, +0x94, the flags, the controls' +0x28..+0x5C, the key timers, the damage and +0x228, the effect held, the sound source and the smoothed value. At the end the fakes' calls and references are mixed in. The frame seconds are the test's own, so nothing in the hash comes from the game's clock.

To the log only: each frame's AL, flags, index, +0x94, damage, collision count and running hash; the traps and the references left.

vb8 reached: slot 115 in every B run of vb8-r1 (the test drives it at its address). The game itself never reaches it before the test day (U-Drive only); only the test does. In the trial (Making Money d0000, 4 days) the token was the same on both sides.

Not covered by the fingerprint (vb8):
- the damage reaching the entry's most inside the frame (0x0055F3F0's crash needs the city's vehicle manager);
- slot 133, slot 187 and the collisions with the game's own objects (slots 187 and 190 are the test's here; vb6 and vb7 cover them);
- a held floor (0x80000, SC4RefPtr::Assign);
- the real effect, haze and sound source;
- NaN steps and wheels (the x87's NaN payloads would differ from ours in the hash).

Not covered by the fingerprint (vb1): slot 152's branch through the next occupant (with no occupant 0x00B43D54's slot 0x3C answers null at once), 0x0054FC30 and slot 123 (the log only), a tie in 0x0050AAF0.

### Parity

2026-10-22, B = `simulators.automata.vehicle` against the original (the switch list includes `determinism.dispatch_info=ours`; two jobs, `harness.multi_instance=ours` on both sides, `OPENSC4_MUTE=1` on both), env p4env-vb1, snapshot vb1-r2, with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_VEHICLE=1`. All the same, on the first pair:

| Run | Days | Result |
| --- | ---: | --- |
| Making Money Tutorial d0000 | 4 | 5 of 5 days |
| Rush Hour Tutorial t0000 | 7 | 8 of 8 days |
| The 12-save batch | 7 | 12 of 12 saves |

Every B run carried the ` V` token. A first batch (vb1-r1) was stopped before it finished, when the audit found the test's uninitialised bytes; vb1-r2 has the fixed test.

vb2, 2026-10-22, the same lists and setup, env p4env-vb2, snapshot vb2-r1 (66 detours), with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_VEHICLE=1`. All the same, on the first pair:

| Run | Days | Result |
| --- | ---: | --- |
| Making Money Tutorial d0000 | 4 | 5 of 5 days |
| Rush Hour Tutorial t0000 | 7 | 8 of 8 days |
| The 12-save batch | 7 | 12 of 12 saves |

Every B run carried the ` V` token with vb2's four hashes, equal to the A run's, and the first-call lines of all 30 vb2 functions (64 of the 66 in Big City, as in vb1). The audit's corrections after vb2-r1 are to the address database's descriptions only.

vb3, 2026-10-22, the same lists and setup, env p4env-vb3, snapshot vb3-r2 (79 detours), with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_VEHICLE=1`. All the same, on the first pair:

| Run | Days | Result |
| --- | ---: | --- |
| Making Money Tutorial d0000 | 4 | 5 of 5 days |
| Rush Hour Tutorial t0000 | 7 | 8 of 8 days |
| The 12-save batch | 7 | 12 of 12 saves |

Every B run carried the ` V` token with vb3's three hashes, equal to the A run's, and the first-call lines of all 13 vb3 functions. The first batch (vb3-r1) had the same Making Money and Rush Hour results and 8 of 12 in the batch: the test's fake map then passed slot 6 on to the real map, which reads its own state, so the move hash followed frames on both sides (A against A too); the fake now answers slot 6 itself.

vb4, 2026-10-22, the same lists and setup, env p4env-vb4, snapshot vb4-r1 (101 detours), with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_VEHICLE=1`. All the same, on the first pair:

| Run | Days | Result |
| --- | ---: | --- |
| Making Money Tutorial d0000 | 4 | 5 of 5 days |
| Rush Hour Tutorial t0000 | 7 | 8 of 8 days |
| The 12-save batch | 7 | 12 of 12 saves |

Every B run carried the ` V` token with vb4's three hashes, equal to the A run's, and the first-call lines of all 22 vb4 functions. After r1 only comments and this page changed.

vb5, 2026-10-23, the same lists and setup, env p4env-vb5, snapshot vb5-r1 (111 detours), with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_VEHICLE=1`. All the same:

| Run | Days | Result |
| --- | ---: | --- |
| Making Money Tutorial d0000 | 4 | 5 of 5 days |
| Rush Hour Tutorial t0000 | 7 | 8 of 8 days |
| The 12-save batch | 7 | 12 of 12 saves |

All on the first pair but Rush Hour d0365 in the batch: there B's first run had an outcome of its own from the first day (A's two runs and B's second shared the other); rerun alone it was the same on the first pair. That save has more than one outcome run to run on the original too ([citylot.md](citylot.md), the vacate reason). Every B run carried the ` V` token with vb5's two hashes, equal to the A run's, and the first-call lines of all 10 vb5 functions.

vb6, 2026-10-23, the same lists and setup, env p4env-vb6, snapshot vb6-r1 (123 detours), with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_VEHICLE=1`. All the same, on the first pair:

| Run | Days | Result |
| --- | ---: | --- |
| Making Money Tutorial d0000 | 4 | 5 of 5 days |
| Rush Hour Tutorial t0000 | 7 | 8 of 8 days |
| The 12-save batch | 7 | 12 of 12 saves |

Every B run carried the ` V` token with vb6's two hashes, equal to the A run's, and the first-call lines of all 12 vb6 functions.

vb7, 2026-10-23, the same lists and setup, env p4env-vb7, snapshot vb7-r1 (137 detours), with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_VEHICLE=1`. All the same:

| Run | Days | Result |
| --- | ---: | --- |
| Making Money Tutorial d0000 | 4 | 5 of 5 days |
| Rush Hour Tutorial t0000 | 7 | 8 of 8 days |
| The 12-save batch | 7 | 12 of 12 saves |

Every B run carried the ` V` token with vb7's two hashes, equal to the A run's, and the first-call lines of all 14 vb7 functions.

vb8, 2026-10-23, the same lists and setup, env p4env-vb8, snapshot vb8-r1 (138 detours), with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_VEHICLE=1`. All the same:

| Run | Days | Result |
| --- | ---: | --- |
| Making Money Tutorial d0000 | 4 | 5 of 5 days |
| Rush Hour Tutorial t0000 | 7 | 8 of 8 days |
| The 12-save batch | 7 | 12 of 12 saves |

Every B run carried the ` V` token with vb8's hash, equal to the A run's, and the first-call line of slot 115.

## AD1: the automata core's models

The first piece of the automata drawing plan (the read-only survey of how the automata are drawn: Ghidra, build 641). The core (vtable 0x00A95AC0) keeps its models in the vector +0x110..+0x118 (model instances, each held through a virtual base, the drawable), their keys in +0xC0 (12-byte resource keys, vb2's vector) and a start key at +0x11C (+0x124 nonzero when set). The flag 1 of the byte +0x33 says the automaton is in the occupant manager (0x00B43D0C), the flag 2 that its models are in the view (0x00B43DD0, cSC43DRender; its slots 0x70 InsertModel, 0x74 RemoveModel, 0x78 MoveModel and 0x7C ChangeModel stay the game's, called through its vtable).

### What is ours

10 functions, in the same group `simulators.automata.vehicle` (list kCoreModels in [automata_vehicle.cpp](../src/core/automata_vehicle.cpp); the group now has 148): the core's slots 70 (in and out of the occupant manager, 0x00507E60), 71 (the models in and out of the view, 0x00507F70), 74 (the level of detail: model slot 8 with the zoom and rotation, 0x00509660), 97 (CreateModels, 0x0050E8B0), 98 (ReleaseModels, 0x0050D3C0), 99 (a model by index, 0x0050EB20), 100 (the view after a move, 0x00508610) and 120 (the light frame, 0x00508D70); the model from a key 0x0050E4D0 and the removal of one model 0x0050B400. The survey's model-make, model-release, view and lod rows of the core, less those of AD2 and AD3 and the ones below.

Left the game's: the factory SC4::GetKeyedResource (0x007FEDE0) and the model instances' and drawables' own code; the matrix of one model 0x005094A0 (AD2's transforms); the camera's ZoomAndRotation, the property readers, SC4PtrVector::InsertN, SC4KeyVector::PushBack, SC4EffectPartB::Attach, the smoothed value 0x004FA960; the core's slots 9, 31, 83, 102 and 106 (called through the vtable).

Not in AD1 (gaps of the model life still the game's): the swaps 0x0050E620 (the model of a key replaced, the billboard falling back to the 3D model) with slots 19 and 20 that call it, and SC4Automaton::SetStart 0x00510360 (the start key added and made, called by aw3's code), and the state 0x13's LeaveState 0x0050E3F0 (AD2's one-shot animation).

Oddities kept:

- Slot 71's way in answers AL 0 even when every model went in (only the way out and the early ways answer 1); the first refusal of InsertModel ends it with the flag 2 left clear and the models already in left there.
- Slot 71 rounds the smoothed fade +0x1B0 to a float and gives it as the alpha below 1 ((1, alpha)); at 1 or above, or NaN, (0, 1).
- CreateModels answers AL 0 at once on the first key the factory refuses (no slot 83 or 106 then), keeping the models made before it; with models already there it answers 0 after slots 83 and 106. The radius is half the first model's z extent of a box that starts as (1, 1, 1), (0, 0, 0), compared with 32 before it is rounded to a float.
- CreateModels gives each model alpha (1, 0) and render flags 3; the model from a key (0x0050E4D0) gives render flags 1 and no alpha, and passes the null-tested virtual base to 0x005094A0 where CreateModels passes the drawable from slot 0.
- 0x0050E4D0 keeps the first key it is given as the start key (+0x128 gets its third word again).
- Slot 99 writes the out only when the property is there (null too); the other indices are the property ID itself.
- 0x0050B400 always answers AL 0; it releases a matched model without a null test, and takes the models out of the view and back around the erase only when there were models before and after.
- Slots 100 and 120 call the first model's drawable without a null test (a null first model is the game's crash too); slot 100 leaves EAX as the x87 status word, ours as it is.
- Stack the game reuses: 0x0050E4D0's rotation out is its key argument's stack slot, InsertN's allocator argument is a byte local (CreateModels: its answer byte); ours pass zeroed locals (both callees always write or never read them).

### Test

The ` V` token gains `,<cmodels>` (23 fields). Its flags: 0x1000000 when the part ran, 0x2000000 when its game-model half ran, 0x4000000 when the factory gave no vehicle, 0x8000000 when no city vehicle had a model key. On a cSC4Vehicle from the factory (its vtable a copy with the core's slots 70, 71, 74, 97 to 100 and 120 at the core's addresses and slots 9, 83, 102 and 106 the test's; its occupant part's slots 6, 29, 31 and 32 the test's, the flags kept in +0x33), with a test view and occupant manager in the two globals: four test models (one null entry among them) in and out of the manager and the view, moved, changed, the timer's event, the light frame, the level of detail, refusals of the view and the manager, no view and no manager, a removal and the release; then the game's models from the first city vehicle's first model key: CreateModels with no key, with a key the factory refuses and with the real one (and the test's property 0xCC21420D listing 0, 1, 5 and 3: index 1 the real key, 3 a refused key, 2 absent), into the view, moved, the level of detail, by index and by key, removed and released. Per step it mixes the answer's AL, the flags, the counts, the box +0x170, the timer, the radius and the test objects' call hash (the game's drawables by the order first seen); log lines "ad1 step N" and "ad1 cmodels".

### Parity

2026-10-23, the same lists and setup as part vb (B = `simulators.automata.vehicle`, the switch list includes `determinism.dispatch_info=ours`, two jobs, `OPENSC4_MUTE=1`), env p4env-ad1, snapshot ad1-r3 (148 detours), with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_VEHICLE=1`. All the same, on the first pair:

| Run | Days | Result |
| --- | ---: | --- |
| Making Money Tutorial d0000 | 4 | 5 of 5 days |
| Rush Hour Tutorial t0000 | 7 | 8 of 8 days |
| The 12-save batch | 7 | 12 of 12 saves |

Every B run carried the ` V` token with ad1's hash, equal to the A run's. The game itself reaches slots 70, 98 and 100, 0x0050B400 and CreateModels before the test day in every run (slots 71 and 74 and 0x0050E4D0 in some, slot 120 in two); the test reaches all 10 but slot 99 in the four Big City saves, where no city vehicle has a model key (flag 0x8000000: the game-model half is skipped). The first trials (ad1-r1, r2) hashed the EAX of slots 100 and 74, which return nothing (A and B differed there only), and missed the game-model half (the vehicle's own start key, which the factory refuses, made CreateModels fail at once); the audit against the disassembly found no difference.

Not covered: the game's InsertN growth while the view is refused part way, slot 99's property with a key list of other sizes, NaN fades.

## AD2: the core's swaps, start, animation, fades and placement

The second piece of the automata drawing plan, after AD1 (the core's models). A model swap replaces one of the core's models (+0x110) by one made from another key, writing that key over the keys' entry (+0xC0) of the same index; the start key (+0x11C, +0x124 set, +0x128 the third word last used) is what slot 19 swaps from. A one-shot (slot 22) is the state 0x13 holding an extra model in its record (+0x28, with the frame answer +0x2C and the play mode +0x30), faded in by slot 119 and removed on leaving the state (slot 111). The fade +0x1B0 is (value, target, duration, elapsed), its value read through the smoothed value 0x004FA960.

### What is ours

14 functions, in the same group `simulators.automata.vehicle` (list kCoreModels2 in [automata_vehicle.cpp](../src/core/automata_vehicle.cpp); the group now has 162): the swap 0x0050E620 and the core's slots 19 (the first model swapped for the start key plus an offset, 0x00510240) and 20 (for a key, 0x00510210); SC4Automaton::SetStart 0x00510360; the core's slot 111, LeaveState 0x0050E3F0 (all its states, the one-shot's included); slots 103 (the animation, 0x005089F0), 22 (the one-shot, 0x005102A0) and 119 (its frame, 0x00508F80); slots 33 (a fade, 0x0050B360) and 83 (the fade's frame, 0x005091C0); slots 8 (the position, 0x00507D90), 14 (the heading, 0x0050AF10), 16 (the placement: position and heading into the models' transforms with the city and cell tests, 0x005080C0) and 75 (a transform onto every model, 0x00509440).

Left the game's: the matrix of one model 0x005094A0 (PlaceModel) and SC4Matrix::FromDirection 0x0080B830; the factory, the model instances' and drawables' own code; SC4PtrVector::Insert / InsertN, SC4Float3Vector::Insert, the smoothed value 0x004FA960; the state 0x10's attachments 0x0050D870 (called on leaving it); the city's, the lot manager's and the traffic network map's queries; every core slot called through the vtable.

Oddities kept:

- The swap does nothing when the key's third word equals +0x128 (the last third word used), whatever the model; it writes the key over the keys' entry of the old model's index, and when the old model is not among the models that index is their count (the entry past the last model, past the keys' end when there are as many keys as models). A 0x29A5D1EC (billboard) key the factory refuses is tried again as 0x5AD0E817 (S3D). The attachments are bound again only when they were bound to the first model, and then to the new first model.
- Slot 19 adds its offset to +0x124's third word (not +0x128's) and swaps only with the start key set; slot 20 only for a key whose third word is set. Both answer AL 1 whatever happened.
- SetStart pushes the key onto the keys even when it is not kept as the start (a start already set, or the key's third word 0), and answers AL 1 without a model while the occupant part's slot 5 answers 0.
- LeaveState zeroes 13 bytes of the state 0x10's record (+0x205..+0x211), puts the record's +0x20 into the automata flags but 0x0600C001, and answers the record's object's slot 30 (full EAX) when there is one, else 1; AL 0 for no record.
- Slot 103 is held only when the simulator's slot 17 answers 0 and its slot 15 1; the last-frame event (slot 102 (1, 0)) needs the list +0x188 not empty and calls the first model without a null test.
- Slot 22 pushes the state before making the model: a key the factory refuses answers AL 0 with the state pushed and the record's former model let go.
- Slot 119 asks the drawable for five words into an uninitialised local (ours zeroed); it clamps against 0x00A81228 but writes the literal 1; only play modes other than 2 and 3 pop the state at the last frame.
- Slot 33 takes an unordered target as new and copies the arguments' bits; the alphas are set at once only for a duration below 0.001. Slot 83 compares the elapsed time unrounded and stores it rounded.
- Slot 8 calls each model without a null test; slot 14 hands a null model's null to PlaceModel (both the game's crash too). Slot 75 answers whether there are models.
- Slot 16 takes the heading only without a NaN y and with any part at least 0.00001 in size (infinities pass); a point outside the city answers slot 13 (0) and returns before slots 107, 104 and 100, even after the heading was taken. The cells use the float magic (0x00A952F4, the same 1.5 x 2^23 as cSC4Vehicle::RoundMagic, which ours reads); a cell refusal answers slot 13 (0) but still counts as moved. Slot 100 gets the two bytes (moved, turned) from dwords whose upper bytes are stack junk in the game (slot 100 reads bytes).

### Test

The ` V` token gains `,<cmodels2>` (24 fields). Its flags: 0x10000000 when the part ran, 0x20000000 when its game-model half ran (0x4000000 again for no vehicle). On a cSC4Vehicle from the factory, put back byte for byte at the end (its vtable a copy with the core's slots 8, 14, 19, 20, 22, 33, 70, 71, 75, 83, 103, 111 and 119 at the core's addresses and slots 13, 16, 52, 53, 56, 57, 62, 67, 100, 102, 104, 106 and 107 the test's; its occupant part's slots 5, 6, 29, 31 and 32 the test's), with a test view, occupant manager and simulator in the three globals, the test's model and key storage and a test state record: on four test models the position and heading (in both and not, a zero heading), the transforms, the placement (slot 16 by address: both, neither, a tiny, a NaN-y and an infinite heading, points below 0 and past the city, the lot and network cell tests and the automata flag 1), the fades to their end, the animation (the last frame, held, the list empty), the one-shot (refused, a refused key, its frames by mode and alpha, the flag 1), leaving the states 0x13, 0x10, 0x14, another and none, SetStart and the swaps with keys the factory refuses; then the game's models from the first city vehicle's first model key: SetStart, into the manager and the view, the swaps (the same third word, a billboard key made as S3D in the first model's place, one pushed at the end), the animation, fades, position, heading and placement, a one-shot and leaving it, out and released. Per step it mixes the answer's AL, the flags, the counts, the fade, the timer, the position and heading, the automata flags, the start key, the record's model and the test objects' call hash; log lines "ad2 step N" and "ad2 cmodels2".

### Parity

2026-10-23, the same lists and setup as AD1 (B = `simulators.automata.vehicle`, two jobs, `OPENSC4_MUTE=1`, `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_VEHICLE=1`), env p4env-ad2, snapshot ad2-t2 (162 detours). All the same, on the first pair:

| Run | Days | Result |
| --- | ---: | --- |
| Making Money Tutorial d0000 | 4 | 5 of 5 days |
| Rush Hour Tutorial t0000 | 7 | 8 of 8 days |
| The 12-save batch | 7 | 12 of 12 saves |

Every B run carried the ` V` token with ad2's hash, equal to the A run's, and reached all 14. The game itself reaches LeaveState, slots 33, 83, 8 and 16 before the test day in every run (slot 103 in most, the swap, slot 19 and SetStart in some); the rest only the test. An earlier snapshot without slot 16 (ad2-t1, 161 detours) gave the same results. Two audits against the disassembly (the 13 first, then slot 16) found no behavioural difference.

Not covered: SetStart's growing insert (the keys full) and the swap's InsertN growth (the test's storage never fills), a swap's key written past the keys' capacity, slot 22 with a record whose former model is the new one.

## AD3: the query tool's highlight model

The third piece of the automata drawing plan. While the query tool points at an automaton, it gets a highlight model: a second instance of its first model (made from its first key), tinted green and translucent, lifted toward the camera by the model's height and moved with it on frames. The core keeps it at +0xCC with its effect part (SC4EffectPartB, 0x40 bytes) at +0xD0. The tank has two more, for its models 1 and 2 (the turret and barrel, made from keys 1 and 2): at +0x2B8 with part +0x2BC, and at +0x2FC with part +0x300.

### What is ours

10 functions, in the same group `simulators.automata.vehicle` (list kCoreHighlight in [automata_vehicle.cpp](../src/core/automata_vehicle.cpp); the group now has 172):

- The core's slots 126 (make, 0x00514340), 127 (remove, 0x00508D00) and 128 (the frame, 0x0050AFD0).
- The tank's copies: slots 192 to 194 (0x0054CF60, 0x0054A950, 0x0054C2E0) for +0x2B8, and slots 195 to 197 (0x0054CBA0, 0x0054A9C0, 0x0054C5B0) for +0x2FC.
- The effect part's assignment 0x00513FF0.

The three variants share one body each (MakeHighlight, RemoveHighlight, UpdateHighlight), with a Highlight record holding the offsets, the key and model index, the make and remove slots, and the lift factor.

Left the game's:

- The factory (SC4::GetKeyedResource) and the model instances' and drawables' own code.
- The effect part's constructor 0x00803060, destructor, Attach and Start, and its vector assignments 0x00513810, SC4Vector12::Assign, 0x00511F00 and SC4IntVector::Assign.
- The box constructor 0x00466E10, and the view's, the camera's and the occupant manager's slots.
- The query tool and the U-Drive object, and every automaton slot called through the vtable.

Oddities kept:

- Every function but the tank's make leaves the automaton alone when its occupant part's slot 7 answers 0x496E5783 and then, asked a second time, 0x897F7EC2.
- The make releases the old highlight twice over; the second release is always of a null.
- The make takes the transform of the core's first model with no test that there is one.
- The tank's make goes ahead with only two keys. The second highlight then reads key 2 past the keys' end, and takes model 2's transform with no count test. Its frame follows model 2 with more than one model, which is past the models when there are two.
- The make gives the drawable its own transform unchanged: no change flag, count 0.
- The frame removes the highlight only when the byte 0x00B09968 is clear or the service 0x00B43D44's slot 13 answers false. When the tool moves away the highlight stays and keeps being moved. It is made only while there is none, when the tool's U-Drive object (+0x30 of 0x00B21D74) answers an occupant whose cell (+0x26C, +0x270) is the automaton's own (the occupant manager's slot 17 from +0x40, +0x48). The core asks that object's slot 57 with 0x10; the tank asks slot 56 with no argument.
- The lift is the followed model's height (box y extent) times 1.5 (0x00A880D4) for the core, 1.15 (0x00A9B7EC) for the tank's first and 1.25 (0x00A9B7F0) for its second, along the camera's +0x54..+0x5C. The x and y offsets are rounded to floats; z is not.
- The effect part assignment still assigns its vectors when given itself. Make never detaches the part from the old highlight before releasing it; the reassignment drops it.

### Test

The ` V` token gains `,<chighlight>` (25 fields). Its flags: 0x40000000 when the part ran, 0x80000000 when it found no city vehicle with models and keys, view or camera.

The test runs on a 0x340-byte copy of the first city vehicle that has models and keys (three or more keys preferred). The real vehicle is not touched. The copy's setup:

- Its vtable is a copy: the core's slots 126 to 128 and the tank's 192 to 197 point at their addresses, and slot 5 is the test's.
- Its models vector holds three entries, all the real first model.
- Fresh effect parts sit at +0xD0, +0x2BC and +0x300.

While it runs, the service 0x00B43D44, the U-Drive object 0x00B21D74 and the byte 0x00B09968 are the test's. The view, camera and occupant manager stay the game's.

It first assigns an effect part to another and to itself. Then, for each of the three variants:

1. Made, moved, and made again (refused once for the tank).
2. Removed by the service, then by the byte.
3. Under the query tool: away, no tool, tool inactive, over it (made), then moved.
4. With the U-Drive object unset.
5. For the tank, with one key and with one model.
6. Removed.

Per step it mixes the highlights' presence, draw properties and transform (slot 9), whether each part is on its highlight, and the test objects' call hash. The log lines are "ad3 step N" and "ad3 chighlight".

### Parity

2026-10-23, the same lists and setup as AD2 (B = `simulators.automata.vehicle`, two jobs, `OPENSC4_MUTE=1`, `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_VEHICLE=1`), env p4env-ad3, snapshot ad3-t1 (172 detours). All the same, on the first pair:

| Run | Days | Result |
| --- | ---: | --- |
| Making Money Tutorial d0000 | 4 | 5 of 5 days |
| Rush Hour Tutorial t0000 | 7 | 8 of 8 days |
| The 12-save batch | 7 | 12 of 12 saves |

Every B run carried the ` V` token, equal to the A run's. In the Making Money and Rush Hour runs it reached all 10. The game itself reaches the core's slots 126 to 128 and the assignment before the test day (slot 128 in all but Making Money d0000); the tank's six only the test (the tutorials have no tanks). The Big City saves have no city vehicle with models and keys, so there the part did not run (flag 0x80000000) and only slot 127 was reached, by the game. An independent audit against the disassembly found one difference before parity, fixed: the tank's two frames lift by their own factors (1.15 at 0x00A9B7EC, 1.25 at 0x00A9B7F0), not the core's 1.5.

Not covered: the make with no models (the game's read of a null first model), the tank's second highlight with exactly two keys or models (the reads past them), and the game's own query tool and U-Drive object (the test's stand in for them).

## Part ro: the other road vehicles

### Survey

Six classes derive from cSC4Vehicle and keep its layout up to +0x2B8 ([automata_roadveh.h](../src/core/automata_roadveh.h) has theirs). The read-only survey (%LOCALAPPDATA%\opensc4\analysis\roadveh-survey) found them by their vtables against cSC4Vehicle's, the constructors that write those vtables, and the factories registered by 0x00514A00:

| Class | ID | Factory | Constructor | Size | Main vtable |
| --- | --- | --- | --- | --- | --- |
| cSC4TrailerVehicle | 0x4A5B69ED | 0x00514970 | 0x00556DB0 | 0x2E4, SC4::operator_new | 0x00A9C490 (199 slots) |
| cSC4Bus | 0xAA9F4A3D | 0x005146C0 | 0x00524360 | 0x2B8, vehicle pool | 0x00A974D8 |
| cSC4GetawayVan | 0xEA863423 | 0x005146A0 | 0x00533BA0 | 0x2B8 | 0x00A98B60 |
| cSC4CarjackedVehicle | 0xCBB32B0A | 0x005146E0 | 0x005244C0 | 0x2B8 | 0x00A97910 |
| cSC4Tank | 0xEBE0DCAD | 0x00514700 | 0x0054CD90 | 0x37C | 0x00A9B9B8 (198 slots) |
| cSC4MayorLimo | 0x4C0DD224 | 0x00514720 | 0x0053CEC0 | 0x2C4 | 0x00A999E0 |

What each class overrides of cSC4Vehicle:

- The trailer: slots 0, 3, 4, 66, 72, 73, 95 and 132, and it adds slots 192 to 198. It keeps a vector of words and a vector of the cars it pulls, a point, an occupant and a word.
- The bus: 164 and 172.
- The van and the carjacked vehicle: 101.
- The limo: Init (it reads the property 0xEC15654A) and slot 121.
- The tank: 3, 4, 72, 93, 110, 115, 121 and 127, and it adds slots 192 to 197 (part AD3's highlights). It also has turret and barrel models, a shot effect and effect attachments.
- Each class has its own +8 Write, Read and GetGZCLSID. These are shared in places:
  - The bus's Write and Read (version 2) with cSC4TrainCar.
  - The carjacked vehicle's (version 1) with the limo.
  - The van's are version 3, the tank's version 2 with the model indices.
- Only the trailer overrides QueryInterface. The other five do not answer their own class IDs.

103 functions, 18,141 bytes. Of them:

- **Already ours (6):** the tank's slots 192 to 197 (AD3).
- **Stay the game's (34):**
  - 25 adjustor thunks.
  - Seven plain jumps: 0x005240C0, 0x0053CD00, 0x0053CD10, 0x0053CD20, 0x0054A740, 0x0054A770, and 0x00555A50 (the trailer's slot 176, shared with the trains).
  - Two folds: 0x006F7DC0 and 0x005050C0.
- **ro1 (44, 3,707 bytes, done):** the objects, the save, the life cycle and the queries. Below.
- **ro2 (15, 5,249 bytes):** the frame code of the trailer, the bus, the van, the carjacked vehicle and the limo.
  - The trailer: 72 0x00555B50, 73 0x00556260, 95 0x00555B10, 132 0x00555A90, 192 0x00556380, 195 0x00556A30, 196 0x00556460, 197 0x00555C70, 198 0x005560D0.
  - The bus: 164 0x00524180, 172 0x005240D0.
  - The carjacked vehicle: 101 0x00524440. The van: 101 0x005339B0. The limo: 121 0x0053CD30.
  - The tank: 127 0x0054A750 (both highlights off).
- **ro3 (4, 6,322 bytes):** the tank on frames: 110 0x0054A780, 115 0x0054AAE0, 121 0x0054B030, 72 0x0054B7C0.

No helper is called only by the group beyond the two destructor bodies. No NAM site and none of SC4UIScale's addresses is in it.

### What is ours (ro1)

44 functions in a new group, off by default: `simulators.automata.roadveh` ([automata_roadveh.cpp](../src/core/automata_roadveh.cpp)).

- **The objects (25):**
  - The six factories: 0x00514970, 0x005146C0, 0x005146A0, 0x005146E0, 0x00514700, 0x00514720.
  - The six constructors: 0x00556DB0, 0x00524360, 0x00533BA0, 0x005244C0, 0x0054CD90, 0x0053CEC0.
  - The trailer's and the tank's destructor bodies: 0x00556F40, 0x0054CEC0.
  - The four deleting destructors: 0x00524410 (shared by the bus, the van and the carjacked vehicle), 0x0053CF60, 0x0054CE90, 0x00556F20.
  - The trailer's QueryInterface 0x00555A60.
  - The six GetGZCLSIDs: 0x00556E50, 0x005243B0, 0x00533BF0, 0x00524510, 0x0054CE30, 0x0053CF30.
- **The save (10):** Write and Read of:
  - the bus, 0x00524160 and 0x00557010;
  - the carjacked vehicle and the limo, 0x0053CE00 (missing from Ghidra's function list) and 0x00524490;
  - the van, 0x00533B00 and 0x00533B40;
  - the tank, 0x0054AA30 and 0x0054A8E0;
  - the trailer, 0x005562A0 and 0x00556C30.
- **The life cycle (5):**
  - Init of the limo 0x0053CE20, the tank 0x0054C880 and the trailer 0x00555A30.
  - Shutdown of the tank 0x0054A850 and the trailer 0x005563C0.
- **The queries (4):** the trailer's slot 66 0x00555AE0 (the rear point: the last car's, else the core's 0x00505A40), its slots 193 and 194 (the counts 0x00556ED0 and 0x00556EE0), and the tank's debug text 0x0054CAA0.

Left the game's: cSC4Vehicle's code (part vb), the jumps, the thunks and the folds above. Also these, all called at their addresses:

- the core's rear point 0x00505A40;
- the stream helpers and SC4PtrVector::InsertN;
- the effect parts (SC4EffectPartB);
- the attachments container (cSC4NetworkBridgeOccupant's, vtable 0x00AA4060);
- the model assignment 0x004201D0 and the string code.

Oddities kept:

- **The trailer's constructor** does not set +0x2E0.
- **The limo's Init** reads its property even when already initialised, and drops cSC4Vehicle's answer.
- **The tank's Read:**
  - It answers by the stream's error alone (1 when there is none), not by cSC4Vehicle's Read.
  - It takes versions 0 to 2, and reads the two indices only from version 2 on.
- **The tank's Write** writes 0xFF for a model not found in the core's model vector, and the last match otherwise.
- **The trailer's Read:**
  - Both counts go into one local.
  - Each word goes into the local the car loop zeroes before each reference. So a failed word read pushes the word before it again. A zero word is never pushed.
- **The trailer's Shutdown and destructor:** Shutdown stops and releases the cars but leaves the words. The destructor frees both vectors without releasing the cars.
- **The tank's debug text** does not test the shot for null.
- **Version words in the argument's slot.** The bus's, the carjacked vehicle's, the limo's and the tank's Reads read their version word into the stream argument's own stack slot. A failed read then compares the low word of the stream's address; ours does the same.
- **AL only in the game:** the van's, the tank's and the trailer's Writes, the van's and the trailer's Reads, the Inits of the limo and the tank, and the trailer's Shutdown. Ours zero-extends.
- **Float truncation:** the limo's first float is truncated as __ftol2 does (the low 32 bits of the 64-bit result).

Two unobservable orders were left as written; the audit noted them:

- The tank's Init reads the barrel index once, where the game reads it again after the turret's assignment.
- The tank's debug text calls the shot's slot 5 before reading its floats, where the game calls it after. Nothing in between writes them.

### Test (ro1)

The ` V` token gains `,<robject>,<rsave>,<rlife>,<rflags>` (29 fields). rflags: 1 objects made, 2 the save ran, 4 the limo's property was set, 8 the life cycle ran, 0x10 a factory failed. All on objects of the test's, with the generator seeded as for vb (header of [automata_autotest.cpp](../src/core/automata_autotest.cpp)). The log lines are "ro1 tank shot" and "ro1 robject".

- **robject:** one object of each class from its factory: its fields, GetGZCLSID, and QueryInterface for four IDs (the trailer's also at its address). Then the trailer's slot 66 without cars and with three cars of the test's, and its two counts.
- **rsave, writing:** each class written by its Write and by cSC4Vehicle's. Mixed in are the version word, whether the rest begins with cSC4Vehicle's bytes, and the bytes after them. The tank is written twice: once with models of the test's in its model vector (indices 2 and 1), once with none found.
- **rsave, reading:** each class read back into a new object, with its own version and one more. The tank's is read with 2, 1, 0 and 3, and one byte short. The trailer's is also cut after its first word. Reads cut inside cSC4Vehicle's part are left out: the game's own Reads there loop on counts left as stack garbage. The bus's, the carjacked vehicle's and the tank's Reads also run on an empty stream.
- **rlife:**
  - The trailer's Init and Shutdown, with cars and an occupant of the test's.
  - The limo's Init five times. Its +0xC vtable is a copy whose GetProperty answers a property of the test's for 0xEC15654A: absent, then 1 to 4 floats.
  - The tank's Init twice, its debug text with a shot of the test's, and Shutdown.
  - Three more tanks with the byte +0x3C and indices that do not hold: equal, past the count, and 0.
- **Not reached by the test:** the tank's Init taking saved indices that hold. That needs a real model vector.

### Parity (ro1)

2026-10-23. B = `simulators.automata.vehicle` and `simulators.automata.roadveh` against the original; the base list includes `determinism.dispatch_info=ours`. Two jobs, `OPENSC4_MUTE=1`, with `OPENSC4_TEST_AUTOMATA=2 OPENSC4_TEST_AUTOMATA_VEHICLE=1`. Env p4env-ro1, snapshot ro1-t2 (the 12-save leg rerun as ro1-t3 from a copy of it):

| Run | Days | Result |
| --- | ---: | --- |
| Making Money Tutorial d0000 | 4 | 5 of 5 days, first pair |
| Rush Hour Tutorial t0000 | 7 | 8 of 8 days, first pair |
| The 12-save batch | 7 | 12 of 12 saves, all on the first pair |

Every B run carried the ` V` token, equal to the A run's, and reached all 44. The game itself reaches the trailer's factory, constructor, QueryInterface, Read and Init before the test day in Rush Hour t0000, which has trailers in its save. Everything else is reached by the test.

- **ro1-t1 not counted:** its snapshot came from the first trial build. That build's test cut streams inside cSC4Vehicle's part, and both sides crashed there. The exception report has cSC4Vehicle's Read (0x0055BD28) and the bus's (0x0055703B) on the stack, and an allocation of a garbage size failing at 0x005B9FEB. No effects switch was on.
- **ro1-t2's 12-save leg not counted:** the PC slept during it, so the leg was rerun as ro1-t3.
- **The audit:** an independent check against the disassembly found no defect in the 44. It corrected one comment.
