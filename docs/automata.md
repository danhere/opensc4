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

The road vehicles and the pedestrians share an automaton base (vtable 0x00A9C068); road vehicles, trains and emergency vehicles a vehicle base (0x00A9D488); fire truck, patrol car and ambulance an emergency base (0x00A981E0); aircraft and boats a base of their own (0x00A95AC0); the vehicle and pedestrian managers a manager base (0x00A9BD88); the aircraft and watercraft managers another (0x00A95880); attractor and generator a controller base (0x00A95D90). The automata are occupants (types 0xC7CC75F1 and 0x496E5783), which is why the occupant manager's fingerprint leaves them out ([occmgr.md](occmgr.md)).

## The code and how it splits

The code runs from 0x004FA700 to 0x00575600. In that range are 1,898 functions (Ghidra's list); 56 of them are the U-Drive control (0x00562C90 to 0x00566D80, the tools pieces T7 and T8, [tools.md](tools.md)) and 15 at the start belong to other code, which leaves about 1,830 functions of the automata. The counts below put each function with the class whose vtables hold it, and helpers with the class that calls them. Where a helper has callers in several classes, or none, the nearest neighbours decide. So they are close, not exact (about one in twelve was placed by its neighbours):

| Group | Functions | Bytes | Proposed parts |
| --- | ---: | ---: | --- |
| Registration and factories (0x005145A0 to 0x00514C30) | 33 | 1,551 | 1 |
| Controller manager | 76 | 15,809 | a1 (45, done), a2 (24, done) |
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

The +4 serializable vtable has four slots: QueryInterface (base `0x00517F30`; the adjustor `0x005183A0` for both derived), AddRef adjustor `0x005183B0`, Release adjustor `0x005183C0`, and GetGZCLSID (base `0x00517FD0`, attractor `0x00518230`, generator `0x005183D0`).

What stays the game's: the shared AddRef `0x008AE800`; the folded helpers `0x0040CE60`, `0x00477810`, `0x00648F00`; the stub `0x005D4A10`; the shared message helper `0x00602F90`; the adjustor thunks `0x005183A0`, `0x005183B0`, `0x005183C0`; the helpers the two classes call at their addresses (`SC4::stl_allocate` `0x0090CF54`, `SC4::Free` `0x0090CF63`, `SC4PairList::Destroy` `0x005650A0`, `SC4Curve::Init` `0x007D0410`, `SC4Random::Construct` `0x00913A5C`, `cRZRandom::Between` `0x009136CA`, `SC4::operator_new` `0x005E55E0`, `SC4::operator_delete` `0x005E5620`); the templates (`0x0051AB80`, `0x0051B1F0` .. `0x0051B310`), which are the script system's; and the drawing in the spawn path.

### Proposed sub-parts

- **a3a: the object and its queries.** The base and derived constructors and destructors, the two factories and the adjustor thunks' targets; QueryInterface, Release, the GetGZCLSID pair, the accessors and flag tests, `IsActive`, `IsFinished`, `IsSaturated`, `GetPosition`, the curve value, and the +4 serializable identity. Small, self-contained, and the foundation for the rest.
- **a3b: the behaviour.** Init and Shutdown, SetTemplate/SetActive and their bookkeeping, the automaton events, the tick, and the generator's spawn (`0x005198A0`, 2,222 bytes, the largest function of the part).

Tests and parity follow the family's method ([traffic-map.md](traffic-map.md)): the controllers run on frame time and the original does not repeat them run to run, so those parts compare inside one run rather than across the usual 7-day window.

