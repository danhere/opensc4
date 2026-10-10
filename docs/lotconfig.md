# The lot configuration manager (cSC4LotConfigurationManager)

Holds every lot configuration of the game (what a lot of a given size, zone and wealth is made of: its building, props, textures and flora) by ID, by family and by size, and answers the developers' questions: which configurations fit a filter, a family or a size, which building a configuration takes, how large the objects of a family are. The tract, zone, lot and port developers and the lot itself ask it all the time ([tract.md](tract.md), [zone.md](zone.md), [lot.md](lot.md), [ports.md](ports.md), [citylot.md](citylot.md)). Reimplemented like the developers: one writer with every virtual call's argument count checked against the callee's own code, an independent audit of every function against the disassembly, then the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/lotconfig.toml).

## The class

Class ID 0x299062C0, IID 0xC8EDBEC0. Object size 0x2C. Constructor 0x0048A1B0 (reached only from the factory 0x0048A520), destructor 0x00489D70. Code 0x00489650 to 0x0048C050. One per city (the city's slot 37, global 0x00B43D18), made for a new city or read as a saved reference, initialised during the city's init.

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00A8EC58 | `cISC4LotConfigurationManager` | 29: Init, Shutdown, adding by key (5) and by object (6; gzcom-dll has these two the other way round), Remove, GetLotConfiguration, the sets by filter, family and size (9-11) and their ID forms (13-15), clearing a set, whether a configuration has a building family or type, filters registered and unregistered, the map and the family set, CreateGenericLotConfiguration, FixLotConfiguration, the largest footprint of building, flora and prop families, an object's ID array |
| +0x4 | 0x00A8EC48 | `cIGZMessageTarget2` | DoMessage 0x0048BC50 |
| +0x8 | 0x00A8EC30 | `cIGZSerializable` | Write 0x00489850, Read 0x004898A0 |

The configurations themselves are static, not the object's: a hash map from ID to configuration at 0x00B216C8 and a set of family IDs at 0x00B216DC, filled from the exemplars of group 0xA8FBD372 at the first Init of a game session (1,909 configurations and 282 families in the unmodded game) and again only when an option byte changes. The object holds three services (the building development simulator, the flora simulator, the prop manager) and a map of registered filters, each with a cached vector of the configurations it lets through. src/core/lotconfig_sim.h has every slot and field with its evidence.

The save record is version 1: the three service references. `tools/re/save_schemas/lotconfig.py` decodes it in every corpus save.

## What is ours

| Replacement | What is ours | File |
| --- | --- | --- |
| `simulators.lotconfig.load` | Init with the load of every configuration, Shutdown, adding by key and by object, Remove, CreateGenericLotConfiguration, FixLotConfiguration; detours on the load of one configuration by key (0x00489910), the save of one (0x00489700) and the cleanup the game registers (0x0048A240) | lotconfig_sim.cpp |
| `simulators.lotconfig.messages` | DoMessage and the update of a configuration it does | lotconfig_sim.cpp |
| `simulators.lotconfig.save` | Write and Read | lotconfig_sim.cpp |
| `simulators.lotconfig.object` | our own object: a detour on the constructor, the destructor, identity, copies of the three vtables | lotconfig_sim.cpp |
| `simulators.lotconfig.queries` | slots 8 to 15 and 18 to 21: the lookups, the sets by filter, family and size, the registered filters | lotconfig_queries.cpp |
| `simulators.lotconfig.footprints` | slots 16, 17 and 24 to 27 | lotconfig_queries.cpp |
| `simulators.lotconfig.filter` | the base filter's QueryInterface and deleting destructor | lotconfig_queries.cpp |

The developers pick from the sets by position, so the sets must come out node for node as the game's: the map is walked in bucket order and the game's own resize and insert functions are called in the game's order. Still the game's: cSC4LotConfiguration itself (called through its slots and helpers), generic sort code shared with other classes, the base filter's constructor (other classes' filters call it), the factory. Copied on purpose from the game and commented in the code: several slots return whatever EAX held (ours return 1; the callers found ignore it); CreateGenericLotConfiguration leaves one field of the new building object as stack leftovers (zero in ours); Read compares a version word the stream may leave unset.

## Test cities

The fingerprint's `lotconfig` part: which services are held, the counts of configurations, families and filters, a checksum of the map in bucket order with every configuration's full state (size, slopes, stage, capacities, flags, lists, every object), a checksum of the family set and the cached filter vectors. `OPENSC4_TEST_LOTCONFIG=1` adds a checksum of the queries each day (by family, by size, by filter with none and with the game's base filter, the footprints, the ID arrays); `=2` also registers a filter, asks through the cached path twice and unregisters it.

## Parity

All runs on 2026-10-10 with every `simulators.lotconfig.*` group on, two games at a time. Result folders under `%LOCALAPPDATA%\opensc4\parity\crime\`.

| Run | Result |
| --- | --- |
| 12 saves for 14 days, land zoned on day 2 and dezoned on day 8, queries and filters (`lcm-zone14`) | 11 of 11 compared. On Rush Hour d0365 the original's run crashed while the city loaded (a C++ exception on a worker thread, none of our code in that run); a pair of single runs of that save with the same switches was identical for all 15 days and in every decoded record (`one/lcrA`, `one/lcrB`) |
| 12 saves for 30 days, queries (`lcm-all30`) | 12 of 12, all on the first pair |
| `saves-ports`, 30 days, zoning, growth asked and ports removed (`lcm-ports30`) | 3 of 3, all on the first pair |
| `saves-plopped`, 30 days, zoning (`lcm-pl30`) | 3 of 3, all on the first pair |

Reached (27 of the 31 functions that log their first call): the constructor, Read, Init with the load of all 1,909 configurations, the lookups and the sets by filter, family and size, the registered filter's cached path, the footprints and ID arrays, the building family and type tests, DoMessage, Write, Shutdown, the cleanup at exit. Not reached: Remove, CreateGenericLotConfiguration (the plop-everything cheat), FixLotConfiguration and the save of one configuration (nothing in a city run calls them); the audit compared those instruction by instruction and found nothing to change.
