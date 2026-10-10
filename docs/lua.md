# Script bindings (Lua 5.0)

The game embeds Lua 5.0 for the advisors, the news ticker, rewards and the tutorials. This page is about the game's side of it: what the game publishes to scripts and what scripts can call. The Lua runtime itself (0x008B96F0 to 0x008BF160, apart from the game's thread table at 0x008BC700 and its tag library at 0x008BE930) is a third-party library and stays as it is.

The pieces, in the order they are being rewritten under the parity harness ([parity.md](parity.md)):

| Piece | Code | State |
| --- | --- | --- |
| The script variables (`game.g_population` and 294 more) | 16 functions, about 28 KB, 0x00411BC0 to 0x00422A00 | ours (`scripts.globals.*`) |
| The native script functions (`game.trend_value`, `ordinance_is_on`, ...) | 36 natives and 5 helpers, about 9 KB, 0x00409690 to 0x0040B8D0 | ours (`scripts.natives.*`) |
| The advisor base class (the advice engine every advisor shares) and the place object | 101 functions, about 17 KB, 0x0040CE00 to 0x00411800 | ours (`scripts.advisors.base1`, `base2`, `base3`) |
| The advisor classes' own functions (factories, constructors, Init and Shutdown, save and load, message handlers) | 81 functions, about 10 KB | ours (`scripts.advisors.classes1`, `classes2`) |
| The advice class (cSC4Advice, 0x00407A90 to 0x00409690) and the city situation advice (the Rush Hour missions, 0x00422FF0 to 0x00424B00) | 95 functions; 40 more are generic string and container code or folded with other classes and stay the game's | ours (`scripts.advisors.advice1`, `advice2`) |
| The advice window's text builder (0x0041D400 to 0x00420D30) | 62 functions, about 14 KB; interface code (it builds the HTML of the advice list), not script bindings | the game's, with the interface |
| The script context (cISCLua) and the two classes under it, the Lua thread and the Lua engine | 275 functions, about 20 KB, 0x0087FFF0 to 0x00881260 and 0x008B68D0 to 0x008B96F0 | ours (`scripts.context.part1`, `part2`, `part3`) |
| The call closure of the data registry (0x00881260 to 0x00881640), the thread table (0x008BC700 to 0x008BD630), the tag library (0x008BE930 to 0x008BF160), the native function table's dispatcher (0x008B5B70) | | the game's |

The advisor system that owns the script context is already ours ([advisors.md](advisors.md)).

## The script variables

Scripts read the city through numbers in the table `game`. The advisors put them there: every advisor has an update function that asks the simulators for figures and hands each to the advisor system (0x00B207C0) by name:

- slot 13 (+0x34), (name, double): a number;
- slot 14 (+0x38), (name, double, place): a number with a place, for the `l_` variables ("lowest", "highest": the worst funded school, the most congested road). The place is an object (class 0x6A26D049) stored under `<name>_subject`;
- slot 12 (+0x30), (name, object): a string or data object.

There are 295 names: 229 `g_` (city figures), 29 `ga_` (averages and grades) and 37 `l_` (figures with a place). The population variables are in the city planning advisor's function (0x00412530, 68 variables): `g_city_r_population` and the like are the demand simulator's figures by wealth, `g_population` and the workforce figures come from the same service.

| Function | Variables | Group |
| --- | --- | --- |
| 0x00412530, 0x00411BC0 | mayor rating, population and jobs by type, demand, zones, neighbours, parks | `scripts.globals.city` |
| 0x00413A10 | funds, income and expenses, tax rates, bonds, neighbour deals | `scripts.globals.city` |
| 0x00415480, 0x00414290 | health and education: funding, city averages, best and worst tracts; hospitals, clinics, schools, museums, libraries with the worst funded and worst graded of each | `scripts.globals.education` |
| 0x004166B0, 0x00416030 | fire, police and health strike chances, stations and jails, coverage | `scripts.globals.safety` |
| 0x004169E0, 0x0041D250, 0x00422770 | the safety, transport and utilities advisors' message handlers (strikes, jail breaks, disasters and their causes, zots, outages), which publish a few variables and events of their own | `scripts.globals.safety` |
| 0x0041C1A0, 0x0041B8D0 | transport: funding, station use, congestion, ports and airports, neighbour connections; station and tile counts | `scripts.globals.transport` |
| 0x004220A0, 0x00421030, 0x00421AB0 | power, water, garbage and pollution; plants and their worst funded and oldest; neighbour deals on offer | `scripts.globals.utilities` |

Things in the original that our code repeats:

- **Two variables are stack garbage.** 0x00415480 publishes `g_num_treated_patients` and `g_health_capacity` from two slots of its frame that nothing writes (they belong to a results structure the function sets up and never has filled). What they hold is whatever the code that ran before left on the stack. Our entry reads the same two stack addresses before our own frame covers them, so the values match while the code around is the game's; with the neighbouring publishers ours too, `g_health_capacity` came out different in the runs with the monthly cycle driven (3 in the original, an address-like number in ours). The test fingerprint logs these two and leaves them out of its hash.
- Several variables are published twice in one pass (`g_elevated_station_count`, `ga_rail_congestion`, the elevated and monorail station figures).
- The transport pass keeps the place of the busiest road connection when it goes on to the avenues, so `l_avenue_connection_congestion_h` can carry a road's place.
- The education pass looks up the worst funded and worst graded building after it has released its references to them.
- `g_num_educational_buildings` and `g_num_schools` are the same count.

A lesson from the first parity run: the service calls that fill a results structure on the caller's stack write 0x40 bytes; a 0x38-byte structure in our code let them overwrite our frame, and the game exited silently while loading. Structures the game fills on the stack must be given the size the original frame leaves for them.

## The native functions

A static initialiser (0x0040B8D0) builds a vector of name and function pairs (0x00B207CC); the advisor system registers them with the script context, in `game` or as globals. Each native gets the Lua state, finds the script context through the object that owns the state (QueryInterface 0xEA10C4A2) and works through the context's methods.

| Natives | What |
| --- | --- |
| `trend_value`, `trend_count`, `trend_slope` | the monthly figures' history (service 0x00B43DC0); the slope is `atan(x) * 63.66` |
| `ordinance_is_on`, `ordinance_is_available`, `ordinance_set_on`, `ordinance_set_off`, `ordinance_enact_fee`, `ordinance_repeal_fee`, `ordinance_monthly_cost`, `ordinance_name`, `ordinance_description` | the ordinance simulator |
| `random_chance`, `_combine_guids`, `difficulty_level`, `is_tutorial` | a draw from the advisor system's generator, an exclusive or, the city's difficulty, the tutorial flag |
| `reward_instance_count`, `get_reward_building_state`, `unlock_reward_building` | reward buildings |
| `trigger_event`, `event_feedback`, `expire_advice`, `retire_advice`, `advice_subject`, `advice_info`, `event_building_distance`, `event_disaster_distance`, `building_is_group`, `nd_cancel` | events and the advice being evaluated |
| `mysim_dispatch_tool`, `mysim_distance_to_closest_building`, `mysim_distance_to_closest_landmark_or_reward`, `mysim_local_school_grade`, `mysim_local_hospital_grade` | the MySim advisor |
| `camera_jump`, `camera_zoom` | the view |

`print` and `_dofile_from_resource` are in the same table and are not in this range.

## The advisor base class

Every advisor is one class (0xB8 bytes; 0xC0 with the part the eight city advisors add) with four vtables: the object at +0, the advisor interface at +8 (the pointer the advisor system holds), the serialisable part at +0xC and the message target at +0x10.

| Field | What |
| --- | --- |
| +0x14 | Init's argument (which entry of the script's `advisors` table) |
| +0x18, +0x38 | caption and name (localised strings) |
| +0x58, +0x5C | a tick count and "now" (the simulator's day) |
| +0x60, +0x68 | the script's id and advice type |
| +0x64 | the cursor of the trigger check |
| +0x6C | triggers: a multimap from event type to record index |
| +0x78 | the active advices: a map from advice to record index |
| +0x84 | the records, 16 bytes each: the advice, a time it may next show, a time it ends, a stamp |
| +0x90 to +0xA4 | the selected head and the two lists of head indices (from the script's `advisor_heads`) |
| +0xB0 | queued events (type, object) |

Init reads the advisor's entry and reconciles its records with the script's advices of its type. Each step checks the triggers of a thirtieth of the records (plus one) from the cursor, then walks the active advices: one whose time is up or whose condition no longer holds is ended (message 0x8A09FA65), one that newly holds is started (0x8A09FA64, and 0x8A09FA6C when it has a follow-up). Queued events run every record whose trigger matches. The object saves its fields, its records and its event queue (version 3).

The range also holds the place object the `l_` variables carry (class 0x6A26D049, 0x28 bytes: an optional position, two numbers and an object) and a good deal of container code. Ten container and string helpers in it are shared by the whole game through code folding (tree successor and rebalancing, a string append with about 160 callers); those stay the game's, as container helpers do everywhere in this project. Seven four-byte getters cannot be hooked.

One function has a convention of its own: 0x0040DA10 (post a message from an object) takes the object in ECX and three stack arguments that the caller removes. Our replacement and our calls to it go through small thunks.

## The advisor classes

| Advisor | Class ID | Size | What it adds to the base |
| --- | --- | --- | --- |
| City planning | 0x2A5F877D | 0xDC | counts of buildings, parks and recreation and a per-group building count map, kept from the occupant messages and saved (version 3) |
| City situations | 0x4BE372CD | 0xC0 | advices that follow a condition: started while it holds, ended when it stops |
| Finance | 0xAA5292D7 | 0xC4 | an update request when buildings change |
| Health and education | 0x6A5F8755 | 0xC4 | ten building-group keys, filled at Init from a table of 19 group numbers |
| Safety | 0xEA19E825 | 0xC4 | sixteen notifications (strikes, jail breaks, disasters) |
| Transport | 0x4A3AD3E1 | 0xD0 | a set of zots |
| Utilities and neighbour deals | 0xCA2C2A2F | 0xC4 | a text token callback for deal texts, and its own event pass: a deal event goes to the one advice of the deal's type, or a copy of that advice is made for it |

The neighbour deal advice (class 0x6A3848DB, an advice with a deal type read from the script field `nd_type`) and a few helpers live in the same range (daylight saving dates, a percent-decoding string copy). Eleven functions there are shared by the rest of the game through code folding and stay the game's.

## The advice classes

An advice (cSC4Advice, 0xC8 bytes) is one entry of the script's `advices` table: Init opens the entry by its index, reads the fields (type, mood, priority, frequency, timeout, title, message, effects, command) and compiles the trigger into a script global named `_atrg_` plus the advice's ID. Its vtables are at +0 (main, 31 slots), +4 (reference counting) and +0xC (save and load); flags are at +0x70, the entry's index at +0x88, the ID at +0x8C and a session number at +0xB0.

The city situation advice (cISC4CitySituationAdvice in gzcom-dll, 0x19C bytes, main vtable 0x00A845B8 with 56 slots, the advice at +4) is a Rush Hour mission. Its Init keeps a reference to the script entry and reads the automata, target and sequence lists; a second pass (slot 55) reads about 40 more fields: the success condition, distances and times, the coverage rectangle and cells, the evade list, success and failure aura, effect and money. Most getters read the script entry again on every call and fall back to a field.

Things the original does that ours keeps: the mission's Init never pops the context advice it pushed on the way that succeeds; a numeric `target_sequence` is added to the target list instead of the sequence; the constructor leaves a dozen fields unset that the getters return when there is no script entry.

## The script context

Three classes, one on top of the other; gzcom-dll's `cIGZLua5Thread.h`, `cIGZLua5.h` and `cISCLua.h` match their vtables slot for slot.

| Class | Size | What it is |
| --- | --- | --- |
| Lua thread (cGZLua5Thread) | 0x1C | one Lua state. The thread interface (97 slots, vtable 0x00AC8DD0) is at +8; the thread's ID is at +0xC, the `lua_State` at +0x10, the owning engine at +0x14. The four bytes before each `lua_State` point back at the thread interface, which is how a native finds its context. |
| Lua engine (cGZLua5) | 0x40 | the main state, a thread itself. Its own interface (15 slots, 0x00AC8F80) at +0x1C opens and closes Lua, creates, resumes and destroys threads and holds the error reporter (+0x3C); the thread table is at +0x20. |
| Script context (class 0x4B2811AA) | 0x58 | the engine plus the game's cISCLua interface (78 slots, 0x00AC4148) at +0x40 and a notification target at +0x44. Nearly all of its code is forwarding: a jump per slot to the thread's method, and a second jump per interface. Its own additions are `getn`, references, and the bridge to the game's data registry. |

Nearly every thread method is one Lua call inside a precision switch: the game runs the x87 at 24 bit, Lua needs 53, so the method reads the setting (0x00892A0A), sets 53 bit when it was 24 (0x008929E0), makes the call and puts 24 bit back. Ours keeps the switch in place and does no arithmetic of its own outside it.

Things the original does that ours keeps: the methods the interface declares as returning nothing return whatever the last call left (Lua's result when no switch happened, the precision setter's otherwise); `NewTag` reads its result at stack index 1, so it is right only on an empty stack; the default error reporter builds its whole report (a banner, the thread, the error type and the message) and then frees it without showing it anywhere; the negative-index conversion in the registry entry reader is off by one.

## Tests

- The fingerprint's `lua` part reads all 295 names back from the `game` table through the script context and hashes their types and values (for an `l_` name also whether its place is there); `OPENSC4_TEST_LUA_DUMP=1` logs every value each day. Scratch tools compare two logs name by name.
- `OPENSC4_TEST_LUA_SCRIPT=<file>` runs a chunk in the script context on a run day (`OPENSC4_TEST_LUA_SCRIPT_DAY`, default 1) and logs and hashes the string it leaves in `_test_result`. `tests/scripts/natives.lua` calls 23 natives with fixed arguments (48 calls). It leaves out the ones that need a current advice or a MySim, and the camera and dialog ones.
- `OPENSC4_TEST_LUA_ADVICE=1` makes one of the city's advices the advisor system's context advice while the chunk runs, as it is while the game runs an advice's trigger: the first advice that has a subject, over the advisors in ID order, else the first advice. `tests/scripts/advice.lua` then calls the natives that work on the current advice: `advice_subject`, `event_building_distance`, `event_disaster_distance`, `building_is_group`, `nd_cancel`, the five `mysim_` ones, `event_feedback`, `expire_advice` and `retire_advice`.
- The fingerprint's `adv` part now ends with `advisors`: how many, their records, and a hash of each advisor's counters and of every record's three numbers (15 advisors and 2,815 records in Big City); `OPENSC4_TEST_ADVISOR_DUMP=1` logs one line per advisor.
- A 35-day run of Big City with the coverage meter reaches all 16 publishing functions and seven natives on its own (`trend_slope`, `trend_value`, `trend_count`, `ordinance_is_on`, `random_chance`, `reward_instance_count`, `_combine_guids`).

## Parity

All seven groups on, 12 saves, 7 days (2026-10-10; folders `lu5-*`, `lu6-*` under `%LOCALAPPDATA%\opensc4\parity\`):

| Leg | Same | Rest |
| --- | --- | --- |
| plain, every variable logged | 12 | |
| the monthly cycle and the advisors driven (`OPENSC4_TEST_RESIDENTIAL_MONTH=2`, `OPENSC4_TEST_ADVISORS=2`) | 12 | |
| the natives' script on day 1 | 12 | |
| the monthly cycle driven, the natives' script on day 4 | 12 | |

The script's result string was the same on both sides in every save (it differs from save to save). In the control (the original against itself) the `lua` part was 12 of 12. Before the two garbage variables were taken out of the hash, the month leg differed on all 12 saves in `g_health_capacity` alone; every other variable matched day by day.

Not reached by any test: the natives that need a current advice or a MySim (`advice_subject`, `event_building_distance`, `event_disaster_distance`, `building_is_group`, `nd_cancel`, `advice_info`, the five `mysim_` ones), `camera_jump` and `camera_zoom`, and inside the message handlers whatever no test city triggers (strikes, jail breaks, outages).

With the advisor base class on too (ten groups; `lu9-*`): 12 of 12 plain, 12 of 12 with the monthly cycle and the advisors driven (every advisor removed and created again on day 2, so Init runs from the script), 12 of 12 with the natives' script. The base's Write runs at the end of every run and its Read at every load; the saves and decoded records match.

With the advisor classes on too (twelve groups; `lu10-*`, `lu13-*`): 12 of 12 plain, with the monthly cycle and the advisors recreated (constructors, destructors, Init and Shutdown of every class run), and with the natives' script. The first run of the second part crashed every city while loading: a flag pushed early for a three-argument call had been given to the call before it, which takes none. An audit of every call's argument bytes against its callee followed. It checked 930 of 950 virtual calls and all 221 direct calls in the advisor files (the rest have a receiver it could not identify) and found three more places where two neighbouring calls had their arguments split wrongly, each harmless only because the pair cleaned the stack between them. All three are fixed (`lu14-*`: 12 of 12 plain and monthly). The advisors' fingerprint now also hashes each record's advice ID and flags.

With the advice classes on too (fourteen groups; `lu15-*`): 12 of 12 in the three legs. The coverage run reaches 20 of the 28 functions of the advice and 23 of the 67 of the mission advice: the constructors, Init, the field readers, save and load. No test starts a mission, so the mission's getters and texts are compared by reading only.

The natives on a current advice (`lu16-*`, fourteen groups): 12 of 12 with the script on day 1; with the monthly cycle driven and the script on day 4, 11 same and one partly (Making Money d0090: the first B run differed from day 0, before the script ran, and the two repeats matched; the known first-run noise). Every city has an advice with a subject, and the result strings match on both sides. The advice's advisor is never a MySim in the test cities, so the `mysim_` natives are compared on their nil path only; `advice_info`, `camera_jump` and `camera_zoom` stay untested.

With the script context on too (seventeen groups; `lu17-*` the thread methods alone, `lu18-*` and `lu19-*` all three parts): 12 of 12 plain, monthly with the advisors recreated, with the natives' script and with the advice script. In the first run of the natives' leg one B run died while loading at 0x0096DA20 (the rare load crash in the game's framework code, [parity.md](parity.md)) and one first B run differed in the advisors' hash; the leg run again was 12 of 12. The coverage run reaches 118 of the 275 functions. Not reached: the constructors, Open and the class factories (they run at startup, before replacements are installed), the tag methods, threads other than the main one, the error paths and the default error reporter, and the data registry bridge. In the registry's value exchange the original passes a 32-bit argument of which it set only the low byte; ours passes the byte zero-extended.
