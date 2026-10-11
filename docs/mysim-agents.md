# The MySim agents (cSC4MySim, cSC4MyStreetSim)

The player's own Sims who move into a city (the "My Sim" mode): each finds a house and a job, drives about, has a mood and shows advice balloons. They are owned by the MySim agent simulator ([mysim.md](mysim.md)) and feed nothing else in the city's simulation. Dan sees no use for the mode; it stays in the rewrite for completeness, with a switch to turn it off later (ROADMAP.md). Build 641 addresses throughout.

## The classes

| Class | Class ID | Constructor | Code |
| --- | --- | --- | --- |
| `cSC4MySim` | 0x4A1DBBBF | 0x00429890 (factory 0x0042A9F0) | 0x00424B00 to 0x0042E300 |
| `cSC4MySimDispatch` | 0xCBC14674 | 0x00433A60 | around 0x00433000 to 0x00434800 |
| `cSC4MyStreetSim` | 0x0BF606A5 | 0x0043D5F0 (factory 0x0043DFA0) | 0x0043D0F0 to 0x0043E740 |
| `SC4MicrophoneTool` (the street sim's tool) | 0xCC23D564 (the tool's ID) | 0x0043E760 | 0x0043E740 to 0x0043EB90 |
| `cSC4MySimBalloonAdvice` | 0x6B70EFAD | 0x0042FA70 (factory 0x0042FB10) | 0x0042F6D0 to 0x00430250 |
| `SC4MySimBalloon` (kept at `cSC4MySim` + 0x238) | none | 0x00430B60 (made by 0x00430680) | 0x00430250 to 0x004311B0 |

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
| `simulators.mysim_agents.script` | UpdateHomeAndJob (~40 globals in the MySim's script table: education and health, the home's condition, power, water, road, crime, police and fire coverage and funding, pollution, desirability, traffic around the lot, the job's title, building, city and condition, employment), PublishBasics (state, age, wealth, generation, retired, zodiac, sex, last local disaster, home and job locations, name), UpdateDistances (the nearest building of each kind within 20 cells, DistanceToClosest's map) |
| `simulators.mysim_agents.trip` | OnCommand: the trip globals (in transit, arrived, destination, means of travel, congestion, time ratio, waiting, pollution, crime, mayor rating, EQ and HQ on the way, the distress and wealth of the buildings around, a landmark and its funding, disasters nearby) |
| `simulators.mysim_agents.agent` | the advisor and agent side: QueryInterface, the balloon target (check, get, make, follow), the dispatch flag, above water, trip time, the advisor's Run, MonthPassed, the name, Init, Shutdown, ProcessAdviceEvents |
| `simulators.mysim_agents.ui` | what the panel asks to change: UpdateResidence, the local school and hospital grades, the car and pedestrian models |
| `simulators.mysim_agents.object` | the factory, constructor (its generator seeded under the original's call site, `fixed_seed::SiteOverride`) and destructors |
| `simulators.mysim_agents.helpers` | the object's own helpers: CanTravel (a trip to work within the working hours left, or home), ShowAdvice, the balloon queues (PopBalloon, NextBalloon, QueueBalloon, ClearBalloons), ClearResidence, DropAdvice, OnBalloonTimeout, TryGoToWork, LoadLuaContext, AtBalloonPlace, DistanceTo, OccupantCondition, ResetAdvisor |
| `simulators.mysim_agents.loading` | LoadAdvice (the balloon entries rebuilt from the advisor's advice and the advice still waiting; the first time also the dispatch object, the balloon, the message notifications and the script tables `mysims`, `age` and `name`) and LoadTuning (the `MYSIM_BALLOON_*` and `MYSIM_TRIP_*` tuning constants) |
| `simulators.mysim_agents.misc` | the rest of the object's own methods: FindNewJob, OnStranded, RefreshLocation (a route from home to work asked of the path finder), OnScriptEvent (the script's idle animation offset), ArriveHome, CancelMoveIn, PostChanged, PlaceGhost, DefaultPedestrianKey and the script commands (leave, died, fired, a new generation, richer) |
| `simulators.mysim_agents.dispatch_state` | cSC4MySimDispatch (src/core/mysim_dispatch.cpp), the object that takes a MySim from place to place: the state, flag and status setters and the events they post to the MySim (0x2BA06B99), Stop, HideLocation, the vehicle, the model lookups, the route reset, and the vtable's arrived and abort slots |
| `simulators.mysim_agents.place` | SC4MySimPlace, a place a MySim is at or goes to (occupant, position, cell, neighbour connection): construct, copy, assign, compare, read, write, cell and lot rectangle, distance between two places |
| `simulators.mysim_agents.dispatch_trip` | starting a trip (the places still to go, a deque at +0x160), home, destination, model keys, Clear, Reset and Shutdown |
| `simulators.mysim_agents.dispatch_route` | the route (a deque of 3-byte cells: x, z, kind; the segment in hand runs from +0x30 to +0x2C): advancing to the next segment, stops, copying a segment, snapping a place to a road cell, a destination outside the city through a neighbour connection |
| `simulators.mysim_agents.dispatch_vehicle` | making the vehicle (or walker) automaton for a segment's model and launching it on the route, and the location marker over what moves the MySim |
| `simulators.mysim_agents.dispatch_legs` | GoTo (a new target: the route planned again while travelling, else queued) and the vtable's next-leg slot (a car parked with the traffic simulator on arrival; from home to work and back) |
| `simulators.mysim_agents.dispatch_step` | Step, the trip state machine: stopped (the next place queued or home, travelling to it), travelling (the route view's search, then segment by segment with the vehicle), none (back home). With flag 0x800, which only a save can set, the game's Step reads a target rectangle from stack slots that call never wrote; ours uses the last one computed |
| `simulators.mysim_agents.dispatch_segment` | EndSegment, what happens when a segment of the route ends: walking to and from the car (snapped to a road, the walker launched), transit (the stop for the line found near the route, then a vehicle of the line there), arriving at a stop or the target, or on to the next kind of segment; the status, state and flags set at the end |
| `simulators.mysim_agents.dispatch_save` | Write and Read of the dispatch object (version 1: the fields, the four places, marker, vehicle, owner, clock, route cells, model map and places still to go) |
| `simulators.mysim_agents.dispatch_messages` | DoMessage: frames step it, 0x44778231 adds 5 to its clock, a reset after a load (0x26D31EC1), automaton events (at the car, arrived, path updated, moved), abort, the transit messages |
| `simulators.mysim_agents.dispatch_object` | the constructor (the game's allocation order kept), destructors and QueryInterface (any other IID goes to the base's with IID 1, so it also succeeds) |
| `simulators.mysim_agents.dispatch_connections` | ValidConnection (a neighbour connection with a link of one of nine kinds) and FindConnection (the game's builds an empty list and never fills it, so it never finds one; kept as is) |
| `simulators.mysim_agents.street` | cSC4MyStreetSim (src/core/mysim_street_1.cpp, 16 functions): QueryInterface, Init, Write, Read, GetGZCLSID, the advisor turns (slots 6 to 8), Stop, HideLocation and ShowLocation of the cISC4MySim part, MakeTool, the deleting destructor, the constructor, UpdateGlobals and the class factory |
| `simulators.mysim_agents.street_tool` | the rest of cSC4MyStreetSim and its tool (src/core/mysim_street_2.cpp, 9 functions): DoMessage, Follow, and the Microphone Tool's Init, constructor, SetStreetSim, Shutdown, OnMouseDownL, deleting destructor and OnMouseMove |
| `simulators.mysim_agents.balloon_advice` | cSC4MySimBalloonAdvice (src/core/mysim_balloon_advice.cpp, 22 functions): the advice class's methods, its Init from the script table, its constructor, destructors and factory, and the helpers of the icon vector (SC4ImageLoader copy, assign, destroy; the vector's copy, insert, assign and erase) |
| `simulators.mysim_agents.balloon` | SC4MySimBalloon (src/core/mysim_balloon.cpp, 12 functions): QueryInterface, StartSound, StopSound, DropTripObject, the heartbeat post, Sync, Create, the constructor, the destructors, UpdateQueue and DoMessage |
| `simulators.mysim_agents.leftovers` | the small functions of src/core/mysim_leftovers.cpp (12 functions, listed below) |

What is not ours is listed under "Left the game's" below. The group `ui.mysim_panel.sim` ([ui.md](ui.md)) carries the cISC4MySim code the panel calls: the script helpers 0x00425B80 and 0x00425BD0 (register convention), the functors 0x00424F30 and 0x00425100, the tool makers 0x004268C0 and 0x00426970, Find 0x004293B0, ShowResidence 0x00429590, the picture buffer 0x0042BDB0 and ShowLocation 0x0042C820. The few pieces of these classes still the game's are in that list.

Replacement code reads the MySim stopwatches through `sim_timers::Claim` first, so `determinism.sim_timers` takes them over as it would for the original code.

## The street sim (cSC4MyStreetSim)

The game's Microphone Tool. Its button (ID 0xEBF9E342, next to "U Drive It") is tipped "Click on any Sim to see what they think about local conditions": the player clicks a car or a pedestrian and the street sim follows it, publishing to a script what is around it, which the advice system turns into a speech balloon. It is the advisor with ID 0x1E in every city, so it exists in every test save (the advisor system's slot 7 gives it; QueryInterface 0x0BF61276 gives the interface at +0x2B8). Nothing in an unattended run starts it: only the button makes the tool and only a click on a car or pedestrian follows.

The object is 0x2C8 bytes: a `cSC4MySim` (0x2B8 bytes) with 0x10 more, the small interface at +0x2B8 included. It has six vtables:

| Offset | Vtable | Part |
| --- | --- | --- |
| +0 | 0x00A85ED0 | the agent (slots 6 to 8 are the advisor system's turns) |
| +8 | 0x00A85E78 | the advisor part (Init is slot 3) |
| +0xC | 0x00A85E5C | `cIGZSerializable` |
| +0x10 | 0x00A85E4C | `cIGZMessageTarget2` (DoMessage 0x0043DFD0) |
| +0xB8 | 0x00A85D38 | `cISC4MySim` (HideLocation at vtable offset 0x8C, ShowLocation at 0x88) |
| +0x2B8 | 0x00A85D20 | the street interface: slot 3 Follow(automaton), slot 4 Stop(), slot 5 MakeTool() |

Fields added to the `cSC4MySim` ones, from the object's start:

| Offset | Field |
| --- | --- |
| +0xC0 | state: 0 idle, 6 on foot, 4 or 5 for a vehicle |
| +0xF8 | the followed automaton (counted) |
| +0x12C | the overlay key 0x0BFA331E |
| +0x150 | the overlay marker handle, -1 for none |
| +0x164 | the script table reference, +0x1F0 the script context |
| +0x210 | byte: following |
| +0x240 | diameter of the look-around square in cells |
| +0x280 | the stopwatch that paces the frame message |
| +0x2BC | means of travel: 9 none, 0 foot, 1 car, 2 bus, 3 train, 7 elevated train, 8 monorail |
| +0x2C0 | the landmark name, an empty counted string made by the constructor |

Class ID 0x0BF606A5 for GetGZCLSID; QueryInterface answers 0x0BF6069F with the object and 0x0BF61276 with the interface, anything else is the base's. The constructor runs the base's, stores the six vtables, sets the overlay key and the marker to -1 and makes the empty landmark name. The factory (0x0043DFA0) returns the interface at +0x2B8. Init is the base's, then loads the script context if the script table reference is still 0.

**Follow** (0x0043E0E0, the interface's slot 3, one argument). Stops the earlier follow first. It does nothing more when the automaton or the script context is null (the result is 1 all the same). Otherwise it keeps the automaton, asks it for its base class (IID 0x8B99EE7C) and tells it a byte (slot 0x268 with 0xFF) and the flag 0x2000000, adds the street sim's message target as the automaton's callback, and writes the script variables: `wealth` 2, `trip_in_transit` 1, and zero for `trip_at_location`, `trip_at_transit_switch`, `trip_idle`, `trip_approaching_location`, `trip_on_bus`, `trip_on_foot`, `trip_on_subway`, `trip_in_car`, `trip_on_train`, `trip_on_elevated_train` and `trip_on_monorail`. Then the occupant group decides:

| Occupant group | Means of travel (+0x2BC) | Script variable set | State (+0xC0) |
| --- | --- | --- | --- |
| 0x4001 pedestrian | 0 | `trip_on_foot` (and `trip_idle` when the automaton's slot 0xEC says 7) | 6 |
| 0x420B car | 1 | `trip_in_car` | 4 or 5 |
| 0x4200 bus | 2 | `trip_on_bus` | 4 or 5 |
| 0x4201 or 0x4215 train | 3 | `trip_on_train` | 4 or 5 |
| 0x422C or 0x422D elevated train | 7 | `trip_on_elevated_train` | 4 or 5 |
| 0x422E or 0x422F monorail | 8 | `trip_on_monorail` | 4 or 5 |

A vehicle takes one draw from the advisor system's generator (service 0x00B43D34, its slot 9, the generator at the system's +0x30; cRZRandom::Advance 0x00913696): the state is 5 when the draw is below 0x7FFFFFFF, else 4. A pedestrian takes none. Then the `state` variable is set, UpdateGlobals runs, the marker goes on the automaton (the overlay manager's Add, kind 3) and the following byte is set.

**Stop** (0x0043D3A0, the interface's slot 4). Takes the callback off the automaton, lets go of it, takes the marker off the overlay manager and clears the state and the following byte. Returns 1.

**HideLocation and ShowLocation** (0x0043D450, 0x0043D4F0; on the cISC4MySim part, the object + 0xB8). HideLocation does nothing without an automaton; with one it calls Stop, puts the Microphone Tool away if it is the view's current control (ID 0xCC23D564; the view's slot 0x68 with 0), and gives the automaton back to the object, so that ShowLocation (Follow on the kept automaton again) can resume. For a vehicle that resumed follow is another draw. MakeTool (0x0043D520, slot 5) makes a tool (0x30 bytes), gives it the street sim and makes it the view's tool (kind 2).

**The advisor turns** (slots 6, 7 and 8, 0x0043D330, 0x0043D360, 0x0043D390) do something only while an automaton is followed: Think(0), ProcessAdviceEvents and MonthPassed of the base. Write and Read are the `cSC4MySim` record: the base's, and the result is the stream's slot 0x48 inverted.

**DoMessage** (0x0043DFD0, the message target at +0x10; every path ends in the `cSC4MySim` handler 0x0042D150):

| Message | What happens |
| --- | --- |
| 0x09D92FC3 (automaton event) | with the message's value 2, 3 or 5: Stop. Other values do nothing |
| 0x4BC42C48 | when the message's object is the street interface: UpdateGlobals, then Think(1) |
| 0x86AD10EE (every frame) | only when the state is not 0, the simulator is paused (`cSC4Simulator::IsAnyPaused`, slot 0x48: one of the three pause counters is above 0) and the stopwatch at +0x280 shows more than 2 ms: UpdateGlobals, Think(1), restart the stopwatch |

So the street sim updates itself on frames only while the game is paused. The stopwatch is claimed for `determinism.sim_timers` like the other MySim stopwatches.

**UpdateGlobals** (0x0043D6C0, `this` is the object's start). Does nothing without a script context. Otherwise it reads the cell of the followed automaton (a place made from its occupant) and publishes, into the script table at +0x164:

| Variable | Source |
| --- | --- |
| `trip_traffic_congestion` | the traffic simulator's grid (service 0x00B43DA8), byte at the cell |
| `trip_time_ratio` | the traffic simulator's slot 0x60 for the cell and the means of travel at +0x2BC, truncated |
| `trip_wait_length` | always 0 |
| `trip_pollution_air`, `_water`, `_garbage` | the pollution simulator's three grids (words) |
| `trip_crime` | the crime grid (service 0x00B43D70) |
| `trip_mayor_rating` | the mayor rating grid (service 0x00B43D60) |
| `trip_eq`, `trip_hq` | the education grids of the grades service (0x00B43DA4), truncated floats |
| `trip_distress1_p`, `_2_p`, `_3_p` | the share (percent) of the buildings in the square with distress exactly 2, exactly 3 and 4 or more |
| `trip_wealth_low_p`, `_med_p`, `_high_p` | the share of the buildings with wealth exactly 1, exactly 2 and 3 or more |
| `trip_landmark_nearby`, `trip_landmark_name`, `trip_landmark_funding_p` | a building in the square with property 0x150A (the first found), its name, and its funding from the budget simulator (property 0xAA59670C, times 100) |
| `trip_fire_nearby`, `trip_riot_nearby`, `trip_any_disaster_nearby` | the disaster manager's disasters (service 0x00B43D3C) inside the square: a fire (class 0x621CDA33), a riot (0xCA380B06), any |

The square is centred on the cell and as wide as the diameter at +0x240. The buildings come from the building finder (service 0x00B43DB0, IID 0x681905BD). A percent is the count times 100 over the number of buildings plus the epsilon at `MySim_TripShareEpsilon`, truncated. This is the same set as the trip part of `cSC4MySim`'s OnCommand, but always all of it, with no state tests. Called with no followed automaton (+0xF8 null) the game's crashes; the tests never do that.

### The Microphone Tool

A `SC4ViewInputControl` of 0x30 bytes (vtable 0x00A85F40, ID 0xCC23D564 at +0xC, the street interface at +0x28, the hovered automaton at +0x2C). Init sets the idle cursor (0xEBF9C962); the constructor 0x0043E760; SetStreetSim 0x0043E790 (calls Init, keeps a counted reference to the interface, false when Init fails); Shutdown 0x0043E7E0; the deleting destructor 0x0043E870.

OnMouseMove (0x0043E8B0) picks what is under the cursor. First through the view (slot 0x94): a car, bus, train or pedestrian (groups 0x420B, 0x4200, 0x4201, 0x4215, 0x4001) whose automaton flag 1 is clear becomes the hovered automaton. If that gives nothing, the 3D point under the cursor (view slot 0x90) is searched in a box 6 by 14 by 6 around it for the first pedestrian (group 0x4001) with that flag clear. The cursor shows a hit (0xEBF9C971) or not. Elevated trains and monorails are in neither test, so Follow's branches for them cannot be reached from the tool's pick (bus and train can).

OnMouseDownL (0x0043E810) does something only when the tool is the view's current control: if a hovered automaton exists and its exemplar does NOT carry the bool property 0x1BC42C49, it calls Follow. The dispatch sets that property on the vehicles it makes for a MySim, so the tool refuses a MySim's own vehicle; only the property's presence is tested, not its value.

## The balloons

**The balloon advice** (`cSC4MySimBalloonAdvice`, class 0x6B70EFAD, 0x104 bytes). A `cSC4Advice` (0xC0 bytes, ours in [advisors.md](advisors.md)) plus a second interface at +0xC0, the balloon part (vtable 0x00A855E0, 15 slots; its `this` is the object + 0xC0). The object's main vtable is 0x00A85650, the counted base at +4 (0x00A85634) and the serializable part at +0xC (0x00A8561C). Fields of the part, from the object's start:

| Offset | Field |
| --- | --- |
| +0xC4 | the icon vector (begin, end, capacity) of 0x30-byte `SC4ImageLoader` holders |
| +0xD0 | the message text (a localised string, 0x20 bytes) |
| +0xF4 | the part's flags (bit 1, 2 and 4 are tested) |
| +0xF8, +0xFC | the male and the female sound key |
| +0x100 | the sound chance |

The part answers the number of icons (slot 7), an icon's picture (8) and instance ID (9), the sound key (10, male for a non-zero argument), SetPartFlags (13; setting bit 2 while the advice has its flag 0x10 asks the advice to reload its texts) and HasPartFlags (14). IsTriggered (main slot 8) is false while the part's flag 4 is set, and otherwise defers to the base class when the part's flag 2 or the advice's own flag 2 is set. QueryInterface 0x6B70E6D9 gives the part, 0x2B722C71 the advice.

Init (0x0042FE60, main slot 3) reads entry `index` of the script's `advices` table after the base's Init: `balloon_message` (the text key, joined to the base's text vector so it loads with the advice's texts), up to nine icons `balloon_icon1` to `balloon_icon9` (a number or string naming an image of type 0x856DDBAC, group 0xAB7E5421, magenta as the transparent colour), `balloon_sound` (both keys), `balloon_sound_m` and `balloon_sound_f`, and `balloon_sound_chance`. It succeeds when the base did and there is an icon or a message text. The constructor draws no random numbers.

**The balloon object** (`SC4MySimBalloon`, 0x58 bytes, at `cSC4MySim` + 0x238). It shows the advice a MySim speaks as a balloon over its head. Three interfaces: +0 (vtable 0x00A8576C, IID 0x22E85D8E; the window procedure 0x00430A40 is its slot 3), +4 (0x00A8575C, IID 0x452294AA, the message target, DoMessage 0x00430F70) and +8 (0x00A85744, the counted base, reference count at +0xC). Fields:

| Offset | Field |
| --- | --- |
| +0x10 | state: 5 hidden, 4 waiting for the next advice, 1 advice shown but not yet spoken, 3 spoken, 2 taken down |
| +0x14 | the current balloon advice (its part, counted) |
| +0x18 | the MySim's `cISC4MySim` part (counted) |
| +0x1C | the window generator (class 0x4386D516, counted) |
| +0x20 | vector of counted icon pictures |
| +0x2C | byte: the queue gave an advice |
| +0x30 | stopwatch (0x18 bytes) |
| +0x48, +0x4C, +0x50 | delays in milliseconds: 300, 2500, 300 |
| +0x54 | byte: the window is made |
| +0x55 | byte: the sound plays |

It runs on a heartbeat message (0xCB7CB509) queued to its own message target through the message server, at the message server's pace: the heartbeat is queued again at once, the state follows the MySim (Sync 0x004305F0: an active MySim wakes a hidden balloon to state 4 and starts the clock; an inactive one hides it to state 5 and stops the clock), and when the balloon is not hidden the window is put over the MySim's screen position and the state moves on by the clock: in state 1, after the first delay (300 ms), the sound starts and the state becomes 3; in state 3, after the shown delay (2500 ms), the window is hidden and the state becomes 2; in state 2, after the first delay again, the sound is stopped and the state becomes 4; in state 4, after the last delay (300 ms), the queue update runs (0x00430CE0) and, when it gave icons, the sound is stopped, the window shown, the state set to 1 and the byte at +0x2C set. Each step restarts the clock.

The queue update takes the next advice off the MySim's balloon queues: it drops the current advice and icons, asks the MySim for the next event advice (cISC4MySim slot 0xF4), and when that has no icon the next ordinary one (slot 0xF0), keeps the advice and its icons, and sizes the window to the largest icon. True when there are icons.

The sound (StartSound 0x00430360): for the current advice, one draw from the advisor system's generator (a number from 2 to 100); when the advice's chance is greater than the draw, there is a sound key for the MySim's sex and the MySim can speak (slot 0xE0), the sound service plays the key for the MySim and is told the MySim's screen position. StopSound (0x004304C0) stops it when it plays. DropTripObject (0x00430530, called by the MySim's Shutdown) lets go of the advice, the MySim and the window.

The balloon's stopwatch at +0x30 is read and started only from this code, which is not in one of the owner ranges of `determinism.sim_timers`, so it stays on the ordinary (virtual) clock, as the original's does. Ours does the same on purpose and never claims it: a claim would put the balloon on simulation time, which the original does not. Its state at a given game day therefore depends on real time.

## The small functions (`mysim_leftovers.cpp`)

Callers of the tiny ones keep ECX and EDX across the call, so ours sit behind thunks that keep both. All build-641 addresses:

| Function | What it does |
| --- | --- |
| `cSC4MySim::SetSubject` 0x00425A80 | register convention (script in ESI, name in EAX, subject on the stack): makes the script global a location object (class 0x6A26D049) holding the subject, the script's current value when there is one, else a new object from the class factory |
| `cSC4MySim::ResidenceScreenPoint` 0x00425030 | register convention: the residence's bounds turned into view positions; gives the middle and the left edge less 5 of the corner that is higher up |
| the script callback for a MySim's name 0x00424E30 | asks the context whether the script holds a name at the value and, when writing, tells the script to go on; always true |
| `cSC4MySimDispatch::ScriptEvent` 0x00432860 | stores a value in the model map's node for a kind and, when asked, hands it to the vehicle's automaton (interface 0xA9B40F05, slot 0x4C) |
| `cSC4MySimDispatch::HasFlag` 0x00431260 | the flags at +0x48 anded with a mask |
| place getters 0x00431290, 0x004312A0, 0x004312B0 | the dispatch object + 0xAC (target), + 0x84 (home), + 0xD4 (work) |
| `SC4MySimPlace::SetWork` 0x004311E0 | the "work" byte at +0x1D |
| `cSC4MySimDispatch::Create` 0x00434620 | the class factory (class 0xCBC14674): 0x18C bytes, the constructor with a null owner, the pointer to the serializable part (+0xC) |
| grade texts 0x00437A30, 0x00437A80 | cdecl (float, out string): the localised text of group 0xEA231E96, instance by the value (below 50, below 100, else; NaN counts as the top) |

## Left the game's

The balloon: the picture composition 0x00430800 and the window procedure 0x00430A40 that paints (drawing, the renderer's). Shared or folded code with callers elsewhere: 0x004302A0, 0x004302B0, 0x004302C0, 0x004302D0, 0x004302E0, 0x00430310, 0x004309F0, 0x00430A90, 0x00431180 and 0x0043D230. The register-convention helper 0x0043D1F0 (the script's integer setter): ours writes the same calls inline and the game's own code still calls it. 0x00437AD0, a generic float compare with a caller outside the MySim code. Adjustor and jump thunks (0x0042F6E0, 0x0042F6F0, 0x0042F840 to 0x0042F870, 0x0042F9D0 to 0x0042FA60, 0x00430C30, 0x00430C40, 0x00430C50). The STL container code in the dispatch and tools ranges, and what the base classes, the string, script and image helpers do.

## Oddities of the original, kept

- UpdateGlobals' landmark property record is a 0x20-byte structure in the game's stack frame of which only the last dword is cleared; ours clears all of it.
- HideLocation sets the kept automaton again after Stop through an inlined smart pointer assignment whose address comparison in front of it never holds; ours does the same steps.
- The constructor stores a temporary vtable at +0x2B8 between the base constructor and the final stores; nothing can see it and ours leaves it out.
- OnMouseMove drops the old hovered automaton and then looks at the picked one again, which is still empty, so nothing is followed from that look; ours keeps the order.
- `SetSubject` faults on a failed script lookup and releases an earlier reference that is null at that point; ours faults the same way.

## A fix to earlier code

The cISC4MySim slots at vtable offsets 0xF0 and 0xF4 (slots 60 and 61, game functions 0x004275E0 and 0x004275F0, GetNextBalloonAdvice and GetNextBalloonEventAdvice) return the advice pointer that NextBalloon (0x00427540) gives. Ours (`simulators.mysim_agents.interface`) were declared void and now return it; the balloon's queue update (0x00430CE0) uses the result.

## Testing

A MySim acts on real-time stopwatches, so runs with one repeat only with `determinism.sim_timers` ([experiments/determinism.md](experiments/determinism.md), "Simulation timers"). Saves with a MySim: `%LOCALAPPDATA%\opensc4\parity\saves-mysim`. `OPENSC4_TEST_MYSIM_MOVEIN=<day>` (autotest) moves a MySim in on that day: the agent simulator's MoveIn, then FindNewResidence and CancelMoveIn (MoveIn alone only opens the placement tool).

`OPENSC4_TEST_MYSIM_QUERIES=1` puts what the MySim panel and balloons ask every MySim each day (names, titles, education and health text, vehicle key, model buttons, current balloons, U-Drive-It) into the fingerprint. `OPENSC4_TEST_MYSIM_DRIVE=<day>` makes every MySim, on that day, leave for work, return home, play its night sound, hear that its home changed and a neighbour connection went, hear of a fire, a pipe burst, a tornado and two disasters near it and of its home added, test its home, run a script command, see its job's building replaced and change job. `OPENSC4_TEST_MYSIM_DELETE=<day>` makes the first MySim leave the game. `OPENSC4_TEST_MYSIM_COMMANDS=<day>` sends every MySim the script commands for richer, fired and a new generation, and the first one the one for dying. GetLocation is not in the queries: for a MySim on the road it is the car's position, which moves on frame time.

**The street sim, the balloons and the small functions** have two switches of their own (src/core/mysim_autotest.cpp). Both call the game's addresses only, never our functions, so the same test runs with the replacements on or off.

`OPENSC4_TEST_STREET_SIM=<day>` (token ` street`). The test finds the street sim through the advisor system (advisor 0x1E, QueryInterface 0x0BF61276) and checks that it is idle. It then makes a car and a pedestrian the way the dispatch's MakeVehicle (0x00435170) does: the model for the status (walker or car) and its key from the first MySim place's dispatch object, then the model service's make call at that MySim's home position (the dispatch's position fields), without the property the dispatch sets and without launching them, so they send no events. The callback the make call added is taken off again, as a failed launch does.

It takes the position and the model key from the first MySim place's dispatch. When that place has no model key (nobody moved in) or its position is invalid (negative; `OPENSC4_TEST_MYSIM_COMMANDS` resets it to -16, -16), the test makes no vehicle: it logs "not made" or "dispatch has no position ... nothing made", hashes a marker and still does the steps that need none. The reason: the original game exits silently when the street sim is told to follow a vehicle built from an empty key or at such a position (found when the first batches failed on both sides). So on the twelve regular saves nothing is made and the street test compares only the no-vehicle part; the full sequence (car, pedestrian, tool click) runs on the four saves of `saves-mysim`. Do not combine the street test with `OPENSC4_TEST_MYSIM_COMMANDS` if street coverage is wanted: it runs, but makes nothing.

Per automaton, 13 steps, each hashed:

1. Follow
2. HideLocation (the cISC4MySim override: stops, keeps the automaton)
3. ShowLocation (follows again)
4. the automaton event 0x09D92FC3 with value 2 (stops)
5. to 12. for the values 3, 5, 4 and 7: Follow, then the event (3 and 5 stop it, 4 and 7 do not)
13. Stop

With `OPENSC4_TEST_STREET_TURNS=1` the car's run gets one more step after the first Follow: the advisor turns (slots 6 and 7: Think and the advice events) run once while it is followed. It is off by default because the turns queue balloon advice for the street sim picked by a draw that differs between runs of the original, so the MySim panel questions of `OPENSC4_TEST_MYSIM_QUERIES` differ for the street sim afterwards.

The tool part: MakeTool, a check that the view's current control has ID 0xCC23D564, then a car the test marked with the property 0x1BC42C49 is put in the tool's hovered field and OnMouseDownL is called (refused: the state stays 0), then a car without the property (followed: the state is set), then HideLocation (the tool must be put away: the view's control is cleared) and Stop. The test never calls the tool's OnMouseMove: what is under the cursor is frame driven.

Each step's hash is the call's low result byte, the state (+0xC0), the means of travel (+0x2BC), the following byte, whether an automaton is held, whether a marker is set, whether the advisor system's generator moved since the last step, and, once a follow has published, every script variable the street sim writes (36 names: numbers as bit patterns, the landmark name as text). The generator's own values are only logged: their starting state differs between runs of the original (the balloon sounds draw from it), so only whether it moved is hashed. The marker's value, the stopwatch and pointers are never hashed. The token hashes the car, the pedestrian and the tool parts together. Cleanup: the made automata are flagged for removal as the game does for a vehicle that was never launched, and the game removes them when its automata manager next runs; the next day the test logs how many occupants stand near the point (in the trial: 2 made, 4 near right after, 0 the next day). The `ms` fingerprint part also gained the street sim: where a place holds it, its saved fields are hashed like a MySim's, and the sixth place's hash replaces the text ` other` it had before.

`OPENSC4_TEST_MYSIM_BALLOONS=<day>` (token ` balloons`). For every MySim place (and the street sim when it is not among them): the balloon advice of the advisor's list, read through QueryInterface 0x6B70E6D9 (icon count, each icon's instance ID and whether it is loaded, an index past the end, both sound keys, the flag tests for 1, 2, 4 and 0xFF, the chance, the message text); the balloon object at +0x238 (whether it exists, its three delays, the window byte, whether it belongs to this MySim, the answers to QueryInterface for 0x22E85D8E, 0x452294AA and a made-up ID) and Sync, whose result and the states are only logged (they depend on real time). The small functions: the three place getters (as offsets from the object), HasFlag for eight masks (logged), and the two grade texts for eight values (0, -5, 49.9, 50, 99.9, 100, 250 and NaN). Last, a balloon advice made by its class factory and let go by its deleting destructor, with its plain fields and what its part and QueryInterface answer.

**Not reached by these tests**: the street sim's paused per-frame branch (the test would have to pause the simulator, which costs real seconds), the tool's mouse-move pick, the bus, train, elevated train and monorail branches of Follow, the balloon's queue update and sound (they need a live balloon target), and the street sim's MonthPassed.

**Trial results so far** (single runs, Big City m0010, 7 days): three runs of the original and three with our code agree in every step hash and both tokens; with the older MySim switches on as well, both sides agree.

## Parity

All four groups on, 2026-10-09: `saves-mysim` 4 of 4 for 30 days, with the queries and the drive on as well 4 of 4; the usual 12 saves for 7 days 11 of 12 and the twelfth (Big City d0365: one of three runs of ours ended with a different traffic simulator record, every fingerprint identical) same when repeated. Reached unattended: the interface methods above with the queries and drive switches, Write and Read, DoMessage, the update and Think.

With the events and residence groups as well (2026-10-09): `saves-mysim` with the queries, the drive (day 3) and the delete (day 20) 3 of 4 and the fourth partly (one run of ours of three began a day one tick early; the original alone shows such second outcomes in the parallel harness on other saves too); with a fifth group run one at a time, 4 runs of the original and 4 of ours from that save identical. The usual 12 with a MySim moving in on day 2 (`OPENSC4_TEST_MYSIM_MOVEIN=2`, so MoveIn, FindNewResidence and SetResidence run in every city): 11 of 12; on Rush Hour d0090 the original has two outcomes of its own after the move-in (the same configuration run twice gave both; seeds identical), and ours gave one of them.

With the script and trip groups as well (2026-10-09): `saves-mysim` 4 of 4 for 30 days with the queries, the drive and the delete; the usual 12 with a MySim moving in 12 of 12 (Big City d0000 on a rerun: the first pair's original run crashed in the game's own code while loading, 0x00975CC8). The script's globals are not in the fingerprint; what the script does with them is (the balloons in the queries), and so are the home, job and title texts and education and health.

With the agent, ui and object groups as well (all eleven, 2026-10-09): `saves-mysim` 4 of 4 for 30 days with the queries, the drive and the delete; the usual 12 with a MySim moving in 12 of 12. The drive does not make a balloon target: the original crashes doing it unattended.

With the helpers as well (twelve groups, 2026-10-09): `saves-mysim` 4 of 4, the usual 12 with a MySim moving in 12 of 12. Reached with the drive and delete (coverage run of the original): all helpers but QueueBalloon, OnBalloonTimeout, TryGoToWork and AtBalloonPlace, which need a balloon target.

With LoadAdvice and LoadTuning as well (thirteen groups, 2026-10-09): `saves-mysim` 4 of 4, the usual 12 with a MySim moving in 12 of 12. Both run on every load.

With the misc group as well (fourteen groups, 2026-10-09): `saves-mysim` 4 of 4, and 4 of 4 with the script commands on day 5 as well; the usual 12 with a MySim moving in 11 of 12, the twelfth (Rush Hour d0090) partly with one run of ours giving the original's other outcome there (both seen from the original in earlier batches). Not reached: OnStranded (no route), OnScriptEvent.

With the dispatch state and place groups as well (sixteen groups, 2026-10-09): `saves-mysim` 4 of 4, with the script commands on day 25 3 of 4 and the fourth partly (one run of ours one tick short on the commands day, every record and later day identical); the usual 12 with a MySim moving in 12 of 12. The dispatch object's deque and map helpers stay the game's.

With the dispatch trip group as well (seventeen groups, 2026-10-09): `saves-mysim` 4 of 4, with the script commands 3 of 4 and one partly (one run of ours a tick short on one day, every record identical; the original shows such one-tick differences of its own in a run's first pair); the usual 12 with a MySim moving in 12 of 12.

With the dispatch route group as well (eighteen groups, 2026-10-09): `saves-mysim` 3 of 4 and one partly, with the script commands 4 of 4, the usual 12 with a MySim moving in 11 of 12 and one partly; both partlies are the first pair's run of ours one tick short on one day with every record identical (the same counts, 1042/1041 and 843/842, appear in other batches with unrelated replacements).

With the dispatch vehicle and legs groups as well (twenty groups, 2026-10-09): `saves-mysim` 4 of 4, with the script commands 4 of 4, the usual 12 with a MySim moving in 12 of 12.

**Registers the game expects kept.** The game was built with whole-program optimization, and some callers keep using ECX or EDX after calling a small function that the compiler knew would leave them alone: Step (0x0043699A) calls RefreshMarker right after SetStatus without setting ECX again, MakeVehicle (0x00435226) pushes EDX after ModelService. A C++ replacement may change both, so these two are hooked through thunks that keep them. Each new group's call sites are checked for such uses.

With Step as well (twenty-one groups, 2026-10-09): `saves-mysim` 3 of 4 and one partly (the first pair's run of ours one tick short on one day, every record identical), with the script commands 4 of 4, the usual 12 with a MySim moving in 12 of 12.

With EndSegment and the dispatch's Write and Read as well (twenty-three groups, 2026-10-09): `saves-mysim` 4 of 4, with the script commands 4 of 4, the usual 12 with a MySim moving in 12 of 12. With EndSegment alone, one run of ours with the script commands ended in the game's own crash at 0x00975CC8 (a constructor writing through a bad pointer, called from framework code; the same crash the unmodified game had once before); that save on its own then matched.

With the dispatch's DoMessage, object and connection groups as well (twenty-six groups, 2026-10-09): `saves-mysim` 3 of 4 and one partly (the first pair's run of ours one tick short on one day, every record identical), with the script commands 4 of 4, the usual 12 with a MySim moving in 12 of 12. The parity verdict does not decode MySim records, so the saves were compared directly (`tools/re/savediff.py --pointers`): in all four MySim saves the MySim and street sim records (with the dispatch objects inside) differ only by moved addresses.

**A moved-in MySim does not always repeat.** It travels by car, and cars move on frame time ([traffic-map.md](traffic-map.md)), so when it arrives can differ between runs of the original; its state (and the advice it gives) then differs from that day. Judge MySim runs with travel by outcome sets.

### Street sim, balloons and small functions

Parity with the street sim, balloon and leftover groups ours (113 groups on side B; verdicts by outcome sets as in [parity.md](parity.md)). Two earlier batches failed on both sides because of the test, not the code (the two causes above); after the two guards no run failed.

| Batch, leg | Saves, days | Result |
|---|---|---|
| `ms9`, plain (no test switches) | 12 regular, 7 | 9 same; Big City d0000 and Making Money d0365 partly (known noise: traffic numbers on the second day on Big City d0000; on Making Money d0365 the original has two outcomes of its own and the first difference is one count in `checks`); Rush Hour d1095: the original exited while loading the city (the rare load exit, parity.md), ours loaded and finished |
| `ms10`, street (`OPENSC4_TEST_STREET_SIM=3 OPENSC4_TEST_MYSIM_BALLOONS=4`) | 12 regular, 7 | 10 same; Big City d0365 and Making Money d0000 partly (traffic and lot numbers; on Making Money d0000 the original has two outcomes); none failed. ` street` and ` balloons` tokens equal on both sides on all twelve (street D3039427 everywhere: nothing made; balloons C2FE7956 on Big City, 841B6BEA on the other two cities) |
| `ms11`, ms (street, balloons and `OPENSC4_TEST_MYSIM_QUERIES=1`) | `saves-mysim` (4), 30 | Making Money m0010 same (31 of 31 days); Big City m0010, Big City m0060 and Making Money m0060 partly; none failed |
| `ms11`, msc (ms plus `OPENSC4_TEST_MYSIM_COMMANDS=1`) | `saves-mysim` (4), 30 | Big City m0010 and m0060 same (31 of 31); Making Money m0010 and m0060 partly; none failed; street token 433739CC on every save and side (nothing made), balloons tokens as in ms |

In leg ms every run of both sides has the same ` street` and ` balloons` token on every save, and a car and a walker were made and followed with identical step hashes on both sides (street tokens: Big City m0010 1F5752DA, with car 8517BC87, walker 9A77840E and tool BBCD062D; Big City m0060 02315C5C; Making Money m0010 40C60AF9; Making Money m0060 1CEF50B1; balloons B7D9C5D8 on Big City, CEA840E8 on Making Money). The `ms` part (the MySims' and the street sim's saved fields) is identical across all runs of each save. Where the partly saves part: mostly on the second day, before either test runs, in `flam` (Big City) or `adv` (Making Money), parts in which the original's own runs differ from each other on these saves; later differences are in `lv` (day 21), `checks`, `citylot` and `growth`.

**Open point.** In four of the partly cases one or two runs of ours have a value that none of that save's three original runs had: the `adv` part from the second day (Making Money m0060 in leg ms; Making Money m0010 in leg msc; Big City m0060 in leg ms, there together with the first `q` hash on days 2 to 4), and `lv` on day 21 (`st 2` against `st 3`; Making Money m0060 in leg msc, while in leg ms the original's own runs differ in that same field on that save). The `adv` part varies between the original's own runs on these saves too (the balloons take draws from the advisor system's generator at a pace set by a real-time stopwatch, in the original and in ours), and three saves ran 30 days with every part identical on both sides with the balloon code ours, so this reads as the saves' known noise. Three original runs per save are too few to show it, so it is not proven.
