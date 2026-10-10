# Script bindings (Lua 5.0)

The game embeds Lua 5.0 for the advisors, the news ticker, rewards and the tutorials. This page is about the game's side of it: what the game publishes to scripts and what scripts can call. The Lua runtime itself (0x008B6C30 to 0x008BF160) is a third-party library and stays as it is.

The pieces, in the order they are being rewritten under the parity harness ([parity.md](parity.md)):

| Piece | Code | State |
| --- | --- | --- |
| The script variables (`game.g_population` and 294 more) | 16 functions, about 28 KB, 0x00411BC0 to 0x00422A00 | ours (`scripts.globals.*`) |
| The native script functions (`game.trend_value`, `ordinance_is_on`, ...) | 36 natives and 5 helpers, about 9 KB, 0x00409690 to 0x0040B8D0 | ours (`scripts.natives.*`) |
| The advisor base class (the advice engine every advisor shares) and the place object | 101 functions, about 17 KB, 0x0040CE00 to 0x00411800 | ours (`scripts.advisors.base1`, `base2`, `base3`) |
| The advisor classes' own functions (factories, Init and Shutdown, save and load, message handlers) | 81 functions, about 10 KB | in progress |
| The script context and server (cISCLua and the classes behind it) | 0x008B0xxx | the game's |

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

## Tests

- The fingerprint's `lua` part reads all 295 names back from the `game` table through the script context and hashes their types and values (for an `l_` name also whether its place is there); `OPENSC4_TEST_LUA_DUMP=1` logs every value each day. Scratch tools compare two logs name by name.
- `OPENSC4_TEST_LUA_SCRIPT=<file>` runs a chunk in the script context on a run day (`OPENSC4_TEST_LUA_SCRIPT_DAY`, default 1) and logs and hashes the string it leaves in `_test_result`. `tests/scripts/natives.lua` calls 23 natives with fixed arguments (48 calls). It leaves out the ones that need a current advice or a MySim, and the camera and dialog ones.
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
