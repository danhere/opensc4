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

The road vehicles and the pedestrians share an automaton base (vtable 0x00A9C068); road vehicles, trains and emergency vehicles a vehicle base (0x00A9D488); fire truck, patrol car and ambulance an emergency base (0x00A981E0); aircraft and boats a base of their own (0x00A95AC0); the vehicle and pedestrian managers a manager base (0x00A9BD88), which derives from the base all four managers share (0x00A95880, constructor 0x005125E0; part v1 below); attractor and generator a controller base (0x00A95D90). The automata are occupants (types 0xC7CC75F1 and 0x496E5783), which is why the occupant manager's fingerprint leaves them out ([occmgr.md](occmgr.md)).

## The code and how it splits

The code runs from 0x004FA700 to 0x00575600. In that range are 1,898 functions (Ghidra's list); 56 of them are the U-Drive control (0x00562C90 to 0x00566D80, the tools pieces T7 and T8, [tools.md](tools.md)) and 15 at the start belong to other code, which leaves about 1,830 functions of the automata. The counts below put each function with the class whose vtables hold it, and helpers with the class that calls them. Where a helper has callers in several classes, or none, the nearest neighbours decide. So they are close, not exact (about one in twelve was placed by its neighbours):

| Group | Functions | Bytes | Proposed parts |
| --- | ---: | ---: | --- |
| Registration and factories (0x005145A0 to 0x00514C30) | 33 | 1,551 | 1 |
| Controller manager | 76 | 15,809 | a1 (45, done), a2 (24, done) |
| Attractor, generator and their base | 81 | 12,629 | 2 |
| Script system | 116 | 31,620 | s1 (34, done), s2, s3 (part s below counts 125) |
| City situation manager | 167 | 44,037 | 4 |
| Vehicle and pedestrian managers and their base | 96 | 20,337 | v1 (32, done), v2 (25, done), v3 |
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
- **v3 (29): creation, dispatch and frames.**
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

Kept as the game's: the six adjustor thunks and the two plain jumps above; the common base's functions (constructor, destructor 0x00512780, QueryInterface 0x00505480, AddRef, Release), called at their addresses; and the 54 functions of v2 and v3 (v2 is now ours, below).

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

Kept as the game's: the common base's functions (Init, Shutdown, DoMessage and its +4 slot 3, Write, Read, AddAutomaton, RemoveAutomaton), the tuning load 0x00512CF0, the record copy 0x0054ED10 and the vehicle and pedestrian builders 0x005628F0 and 0x00545F20, all called at their addresses; and the 29 functions of v3. No NAM or SC4UIScale site is among the 25, and none needed keeping for stack leftovers or x87 values held across a call.

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
  - **s3a (15, 9,964 bytes): the loader and the template readers.**
    - LoadScripts 0x00523B10 and the readers it calls first: 0x0051DAA0, 0x0051EEE0, 0x0051F5C0 (1,158 bytes), 0x0051FA60.
    - The attractor and generator templates: 0x00523500 (1,546) and 0x00522F30 (1,488), with their parts 0x0051C5D0, 0x0051CB40, 0x0051CBF0, 0x0051CD20, 0x0051F010, 0x0051EE10, 0x0051F2A0.
    - The behaviours 0x00522860 (1,733).
  - **s3b (11, 8,861 bytes): the groups and the vehicle kinds.**
    - The groups: 0x005216A0, 0x00520DF0 (1,316), 0x00521C70 (1,527), 0x00521800 (795), 0x0051FB90, 0x00520520.
    - The vehicle kinds' tuning: 0x0051DC20 (1,465), 0x0051E1E0, 0x0051E4A0, 0x0051E6B0 (1,520), 0x0051ECA0.

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
