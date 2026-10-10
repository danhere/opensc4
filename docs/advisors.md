# The advisor system (cSC4AdvisorSystem)

The city's advisors: it loads the advisor scripts, creates the advisors they list, runs them every game day, routes events to the advisors that subscribed to them, and keeps the context advice and the script globals the advisors read. Reimplemented the same way as the simulators ([weather.md](weather.md), [parity.md](parity.md)): function by function on the original object under the parity harness, then our own object under the class ID. Build 641 addresses throughout; entries in `data/addresses/advisors.toml`.

## The class

Class ID 0x4A0DD616. Object size 0xA8. Factory 0x0041B220 (`SC4::operator_new(0xA8)`, then the constructor 0x0041A990, which also sets the instance pointer 0x00B207C0). Destructor 0x0041A0D0 (clears it). Six vtables:

| Vtable | At | Slots |
| --- | --- | --- |
| 0x00A82AC8 | +0 | `cISC4AdvisorSystem`, 29: QueryInterface 0x00418640 (0x4A0DD617 at +0, 0x452294AA at +0x4, 0xE98B2F57 at +0x8, 0xE4FDA3D4 at +0x10, others through the base at +0x14), AddRef and Release (shared, on the base), Init 0x0041B570, Shutdown 0x0041A3B0, AddAdvisor, RemoveAdvisor, GetAdvisor, three shared getters, DoLink 0x0041B080, SetGlobalString, SetGlobalNumber, SetLocalValue, PushEvent, SubscribeToEvent, UnsubscribeFromEvent, PushContextAdvice, PopContextAdvice, GetContextAdvice, GetAdvicePopupPriorityThreshold, GetDataTag, a shared slot, then five slots on the helper at +0x5C |
| 0x00A82AB4 | +0x4 | `cIGZMessageTarget2`: DoMessage 0x0041AAC0 |
| 0x00A82AA4 | +0x8 | a listener (IID 0xE98B2F57), slot 3 an empty stub |
| 0x00A82AA0 | +0xC | one slot: the deleting destructor |
| 0x00A82A88 | +0x10 | `cIGZSerializable`: Write 0x00418D10, Read 0x004198B0, GetGZCLSID 0x00418630 |
| 0x00A82A70 | +0x14 | the reference-counted base (count at +0x18); slot 3 the deleting destructor |

### Fields

| Offset | What |
| --- | --- |
| +0x1C, +0x20, +0x24 | advice popup thresholds by difficulty (100 each, then `tuning_constants.ADVICE_POPUP_PRIORITIES_BY_SKILL_LEVEL` low/medium/high) |
| +0x28 | loaded from a save (Read sets it; Init then keeps the saved advisors) |
| +0x29 | initialised |
| +0x30 | random generator |
| +0x48 | the last day run (day number) |
| +0x4C | map: advisor ID -> advisor (count at +0x50) |
| +0x58 | the script context (cISCLua, from 0x00B43CE8) |
| +0x5C | a helper object (slots 24-28) |
| +0x60 | reload the scripts on the next run |
| +0x64 | a service (0x00B43CDC) the advisor system registers its callback 0x00417AF0 with |
| +0x68 | map: event -> set of subscribed advisors (count at +0x6C) |
| +0x74 | vector: the context advice stack |
| +0x80 | vector: the two script data tags (with `gc` methods 0x00418B50) |
| +0x90 | stopwatch (on simulation time with `determinism.sim_timers`) |

### Messages (DoMessage)

| Message | What |
| --- | --- |
| 0x26D31EC1 | the city is ready: PostCheck, and the first run when no day has run yet; clears +0x28 |
| 0x26D31EC4 | event 0xAA31A210 to the subscribers, PostCheck, the first run when no day has run yet |
| 0xCA5501F2 | the run: on a new day the date globals (`g_year`, `g_month`, `g_day`, `g_date`, `g_year_count`; a script gc and a forced run on the 28th), otherwise only every 500 ms of a running simulation; then the scripts reloaded if +0x60, every advisor simulated, and PostCheck |
| 0x230E27AC | cheats: `DollyLlama` toggles the GUID display byte 0x00B20D50; `Adv` takes options (`guids on/off`, `trigger`, `print`, `refresh`, `reload`) |

Init (0x0041B570) takes the script context, registers the script types (0x00418940) and the two data tags, registers the callback, loads the scripts (Reload 0x0041A4F0: `_on_reload`, then the `advisors\` folders of the data and user folders as script group 0x4A5E8EF6), registers the cheats, creates every advisor in the script's `advisors` table (0x00419B10, each by its `class_id` under its `id`, 0x004199B0) unless loaded from a save, initialises them again (0x00419D60), loads the thresholds (0x0041B3A0) and subscribes to the three messages. Shutdown (0x0041A3B0) undoes it; RemoveAll (0x0041A2C0) removes and shuts down every advisor.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.advisors.interface` | the interface slots (adding, removing, finding advisors, events and subscriptions, script globals, context advice, thresholds, data tags), the helper slots, QueryInterface, PostCheck, SimulateAll and the advisor message (0x00418A80, inlined: it takes the advisor in ECX) |
| `simulators.advisors.lifecycle` | Init, Shutdown, RemoveAll, LoadThresholds |
| `simulators.advisors.run` | DoMessage (the run and the cheats), RegisterTypes, Reload, CreateAll, CreateAdvisor, Reconcile |
| `simulators.advisors.scripts` | DoLink (a link in advice text) and the data tags' gc callback (0x00418B50) |
| `simulators.advisors.save` | Write and Read (version 1: the advisor count, then each advisor as a reference and its ID) |
| `simulators.advisors.tokens` | the text-token callback (0x00417AF0) |
| `simulators.advisors.object` | our own object under the class ID: constructor, destructor, AddRef, Release, our copies of the six vtables |

Still the game's: the map, set, vector, string and date helpers, the shared getters, GetGZCLSID.

One call looks like it takes an argument and does not: slot 0xC4 of 0x00B43C94 takes none, and the value pushed before it belongs to the next call (0x004B7D70, `ret 4`). Reading it as an argument of the slot left the stack 4 bytes off and crashed every load. The constructor's generator (+0x30) is seeded under the original's call site (0x0041AA02) with `fixed_seed::SiteOverride`; from our code `determinism.fixed_seed` would count it under another site and seed it differently.

## Text tokens

The callback 0x00417AF0 (registered with the service at 0x00B43CDC) fills a token in advice text: it gets the token's name, the string to fill and the advisor system. Named tokens (strings at 0x00B20860):

| Token | Value |
| --- | --- |
| `link_id`, `sit_link_id` | `sc4://advice/%08x%08x` with the context advice's two IDs |
| `advisor` | the name of the context advice's advisor (a string resource, instance 0xC8) |
| `building_initial_cost`, `building_monthly_cost` | money, from the context advice's building |
| `mission_target`, `num_targets`, `cur_target_index`, `num_targets_remaining`, `mission_mayor_rating`, `mission_failure_mayor_rating`, `mission_money`, `mission_failure_money` | from the city's mission (city slot 0xC4) |

Any other name is run as script, `_dasd_=(name)`, and the result formatted: a number by the prefix (`S@` plain integer, `M@` money, `P@` percent, `D@` date, `T@` time, none the locale's number), a string as it is, a data-tag object as its string. A token nothing fills becomes `%UNKNOWN TOKEN --name--%`. A week's run fills about a hundred `tuning_constants.*` tokens at load; the Adv cheats' advice reaches `advisor`.

Three calls here pass arguments that were pushed earlier than the call before them: the resource lookup (slot 0x8C of the 0x00913C72 singleton) takes only the ID, and the key and a 0 pushed before it go to the string service's slot 0xC8; the time helpers 0x004178B0 and 0x00417870 take none, and the seconds pushed before them go to the locale's slot 0x28.

## Test cities

A loaded city skips CreateAll (+0x28 is set by Read). `OPENSC4_TEST_ADVISORS=<n>` removes every advisor on the n-th day of a run and creates them again from the script (RemoveAll, then CreateAll), and marks the scripts for a reload, which the next run message does (Reload, then Reconcile). `OPENSC4_TEST_ADVISOR_CHEATS=<n>` hands the advisor system the DollyLlama cheat twice, the Adv cheat in several spellings (every branch runs: guids on and off, trigger, print, refresh, reload) and a script link (`0000001d00000000...`, DoLink) on the n-th day.

The fingerprint's `adv` part: the advisor count, the event count, a hash of the own fields (thresholds, flags, generator state, last day, +0x60), hashes of the advisor IDs, the subscribed events, the context advice stack (GUID, session, flags of each) and its size, and the data tags. The service pointer at +0x64 differs run to run and is left out. Advisor records are not decoded by `tools/re/sc4save.py`; the batches compare the saves with `tools/re/savediff.py --pointers`.

## Parity

Everything on (`simulators.advisors.*`), two jobs at a time; results (2026-10-09) under `%LOCALAPPDATA%\opensc4\parity\`:

| Run | Result |
| --- | --- |
| 12 saves, 7 days (`advtok4-7`) | 11 of 12; Making Money d0365 partly: B's first run parted in the harness's tick count (`checks` 843/842) on day 6, its two retry pairs matched. 12 of 12 in the two batches before (`advtok2-7`, `advtok3-7`) |
| `saves-mm2`, 30 days (`advtok4-mm30`) | 2 of 2 |
| 12 saves, 7 days, advisors removed and created again on day 2 (`advtok4-drv`) | 11 of 12; Rush Hour d1095 partly: B's first run parted in the flammability walk a day in, its two retry pairs matched. 12 of 12 in the two batches before |
| 12 saves, 7 days, cheats and a link on day 1 (`advtok4-cht`) | 12 of 12 |

The advisor system's record was identical in every pair (`savediff.py --pointers`). Without the token callback everything was 12 of 12 (`advall2-*`).
