# The network lot manager (cSC4NetworkLotManager)

The city's desk for what stands beside a road. For every cell that has a piece of a surface network (every network type but pipe, power line and subway) and qualifies, the manager keeps one network lot: it ties the piece to one network lot configuration, a list of props and flora with their places on the cell (street lights, trees, bus shelters). The configurations are loaded once per city from exemplars and kept by the network piece ID they are for; the manager picks one that fits the cell by the zone and the wealth around it and the slope under it. It also keeps the potholes. It has no grid: every city size it needs it asks the city for (slots 93 and 94).

Three classes belong to this piece: the manager, the network lot and the network lot configuration. The last two have no class ID and no factory and are made only by the manager's code (the configuration also by the lot editor window). Together 62 functions (14.1 KB), of which one stays the game's for good. Together with the construction crew (cSC4NetworkConstructionCrew, 43 functions), which decides when a new piece becomes usable and so when its lot appears, the piece is four classes and 105 functions. The rewrite is split in five parts:

| Part | What | Functions |
| --- | --- | --- |
| L1 | state and queries (this page's code) | 29 |
| L2 | life and save: factory, constructors, destructors, Init, Shutdown, Write, Read, loading of the configurations | 14 |
| L3 | the lots: messages, lot creation and removal, props, potholes | 13 |
| C1 | the construction crew's state and life, with the `game.quiet.network` wrapper folded in | 23 |
| C2 | the construction crew's work | 20 |

L1 is done: 29 functions are ours, 23 as detours (`simulators.netlot.identity`, `.queries`, `.lot`) and 6 as swaps of one vtable slot each (`simulators.netlot.slots`, for code that other classes share). The six adjustor thunks of the range and the shared container code stay the game's (see "What is left and why").

## The classes

### The manager

Class ID 0xE9A6CD3B, IID 0x89A5BA3F. Object size 0x88. Factory 0x0060F4F0 (registered at 0x00460443), constructor 0x0060EE50, destructor 0x0060E480, deleting destructor 0x0060EF20. Code 0x0060C720 to 0x006104CE. One per city: the city makes it (0x0045AC5C) unless the save brought one, keeps it at +0x150 and gives it by its slot 42. There is no global pointer.

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AA7958 | `cISC4NetworkLotManager` | 16, see the table below |
| +0x4 | 0x00AA7948 | `cIGZMessageTarget2` | thunks 0x0060E510, 0x0060E520, 0x0060E530; DoMessage 0x0060FC30 |
| +0x8 | 0x00AA7930 | `cIGZSerializable` | thunks 0x0060E540, 0x0060E550, 0x0060E560; Write 0x0060D6B0, Read 0x0060DAC0, GetGZCLSID 0x0060C720 |

gzcom-dll's `cISC4NetworkLotManager.h` has the slots in the right order (each checked by its argument count and by what it does), so the interface can be used as the header declares it. One signature misleads: `GetNetworkLotConfigurationById(id, out)` never reads or writes its second argument; it only answers whether a configuration with the ID exists.

| Slot | Address | Function (arguments from its own `ret N`) | Part |
| --- | --- | --- | --- |
| 0 | 0x0060C760 | QueryInterface(iid, out) | L1 |
| 1 | 0x0060C730 | AddRef | L1 |
| 2 | 0x0060C740 | Release | L1 |
| 3 | 0x00610120 | Init | L2 |
| 4 | 0x0060E570 | Shutdown | L2 |
| 5 | 0x0060D650 | GetNetworkLot(x, z) | L1 |
| 6 | 0x0060DBF0 | GetNetworkLotConfigurationSet(set) | L1 |
| 7 | 0x0060D9A0 | GetNetworkLotConfigurationById(id, unused) | L1 |
| 8 | 0x0060CF80 | CreateProp(lot, index, type) | L3 |
| 9 | 0x0060D030 | GetZoneTypeForNetwork(x, z) | L1 |
| 10 | 0x0060CBB0 | GetWealthForNetwork(x, z) | L1 |
| 11 | 0x0060D820 | IterateAllNetworkLots(callback, context) | L1 |
| 12 | 0x0060D5E0 | RedevelopLot(x, z, lot) | L3 |
| 13 | 0x0060CB80 | SetLotConfigurationForNextPiece(configuration); folded code, see below | L1 |
| 14 | 0x0060E810 | WriteLotConfiguration(configuration, segment) | L3 |
| 15 | 0x0060EF20 | the deleting destructor | L2 |

Fields (`src/core/netlot_sim.h` has the list with its evidence):

| Offset | What |
| --- | --- |
| +0x0C | the lot map: a hash table from the cell key to the lot (one reference each). The table is 0x14 bytes (buckets at +4 and +8, the count at +0x10), made with 100 buckets; a node is 0xC bytes: next, the uint16 key, the lot |
| +0x20 | the configuration map: the same table from a network piece ID to a vector of configurations (one reference each); a node is 0x14 bytes |
| +0x34 | the configuration forced for the next piece (referenced), or null |
| +0x38 | a cell iterator (0x20 bytes); Init sets its rectangle to the city |
| +0x58 | the pothole map: as the lot map, from the cell key to a prop occupant |
| +0x6C | reference count |
| +0x70 | initialised |
| +0x74 | the set of cells waiting for an update (a hash set of cell keys) |

### The cell key and what it limits

A cell is packed into 16 bits, `(x << 8) | z`. That is the key of the lot map, the pothole map and the waiting set, the cell in the save record, and what IterateAllNetworkLots unpacks for its callback. GetNetworkLot builds it as `((x & 0xFF) << 8) | z` cut to 16 bits, and nothing checks x or z against 256. The key is in the game's tables, which its own code reads, and in every save, so ours keeps it exactly. It is a uint16 in the save record too, so the format itself carries the limit. It is a limit of the format: whatever the city's slots 93 and 94 say, everything this class does only works for a city of at most 256 cells a side (the largest city the game has); on a larger one two cells 256 apart would share a lot. A larger city needs a new key and a new record version.

### The network lot

0x18 bytes, IID 0x89A5BA66, one vtable (0x00AA78E0, 7 slots; the header `cISC4NetworkLot.h` has the right order). It has no constructor function: Read (0x0060DB12) and CreateLot (0x0060DE9D) build it in place.

| Slot | Address | Function |
| --- | --- | --- |
| 0 | 0x0060C800 | QueryInterface(iid, out): IIDs 1 and 0x89A5BA66 |
| 1 | 0x004F7BA0 | AddRef (code shared with 73 other vtables) |
| 2 | 0x0060C7E0 | Release |
| 3 | 0x0060CE80 | GetNetworkOccupant (folded code, about 150 vtables) |
| 4 | 0x0060CE50 | GetLotConfigurationObjectCount |
| 5 | 0x00610020 | GetLotConfigurationObjectArray |
| 6 | 0x0060D130 | the deleting destructor |

Fields: +0x4 the network piece (referenced), +0x8 the configuration (referenced; null between Read and Init), +0xC the configuration's ID as read from the save (only Read writes it), +0x10 the reference count, +0x14 a byte: the configuration was forced (such a lot is never chosen again).

### The network lot configuration

0x6C bytes, one vtable (0x00AA7998, 8 slots; the header `cISC4NetworkLotConfiguration.h` has the right order). Constructor 0x0060FA90, deleting destructor 0x00610070.

| Slot | Address | Function |
| --- | --- | --- |
| 0 | 0x0048C940 | QueryInterface (shared code: IID 1 only) |
| 1 | 0x0060C830 | AddRef |
| 2 | 0x0060C840 | Release |
| 3 | 0x0060FB50 | GetObjectArray (folded code, 44 vtables) |
| 4 | 0x0040CE60 | GetID (shared code) |
| 5 | 0x0060FB60 | GetName(string) (folded with one other vtable) |
| 6 | 0x0060FB80 | GetNetworkPieceId (folded code, 13 vtables) |
| 7 | 0x00610070 | the deleting destructor |

Fields: +0x4 a vector of lot configuration objects (0x34 bytes each, the same kind a lot configuration has: [lotconfig.md](lotconfig.md)), +0x10 the ID, +0x14 the name, +0x28 the network piece ID, +0x2C four mask bytes and +0x30 the period (which cells of a run of pieces the configuration is for), +0x34 a bit vector of the 16 zone types, +0x48 one of the 4 wealths, +0x5C and +0x60 the least and greatest slope, +0x64 a mask over a value of the piece, +0x65 a rule about a flag of the piece, +0x68 the reference count.

## The save record

Type = the class ID. Write 0x0060D6B0, Read 0x0060DAC0 (both L2; the format was read from Write for the decoder `tools/re/save_schemas/netlot.py`):

| Field | Size |
| --- | --- |
| version, 2 (Read refuses any other) | uint16 |
| the number of lots | uint32 |
| per lot: the cell key | uint16 |
| per lot: a reference to the lot's network piece | 8 bytes (4 for none) |
| per lot: the ID of the lot's configuration | uint32 |
| the number of potholes | uint32 |
| per pothole: the cell key | uint16 |
| per pothole: a reference to the occupant | 8 bytes |

The lots come in the hash table's order. Read makes each lot with its piece and the configuration's ID; Init, which runs after it, looks the configuration up by the ID. The configurations themselves, the forced configuration and the waiting set are not saved.

All 21 records of the corpus decode with every byte used (`python3 tools/re/sc4save.py --check`): Big City 1,759 / 1,765 / 1,771 / 1,773 lots (d0000 to d1095), Making Money 245 in all four, Rush Hour 305 / 306 / 306 / 307, `saves-networks` 1,792 / 1,792 / 314, `saves-plopped` 1,731 in all three, `saves-ports` 1,731 / 1,731 / 306. No save has a pothole or a lot without a piece; every key is a distinct cell and no x or z is above 127. The pieces are of class 0xC9C05C6E, and in `saves-networks` also of the pre-built model and bridge classes.

## What the functions of L1 do

**Identity.** QueryInterface answers IIDs 1 and 0x89A5BA3F with the object, 0x452294AA with the part at +4 and 0xE4FDA3D4 with the part at +8, each referenced. AddRef and Release count at +0x6C; a Release at 1 or below calls the deleting destructor. GetGZCLSID is the class ID.

**GetNetworkLot(x, z)** looks the cell key up in the lot map and gives the lot or null, not referenced.

**GetNetworkLotConfigurationSet(set)** fills the caller's tree set of pointers with every configuration, each referenced for the caller. A set that is not empty is emptied first, and what it held is not released. The game's only caller is the lot editor window (0x0049A530), which makes an empty set, walks it and releases each entry.

**GetNetworkLotConfigurationById(id, unused)** asks every configuration for its ID until one matches.

**GetZoneTypeForNetwork(x, z)** is the zone a network cell counts as. The cell's own zone when it has one. Else the best of the four neighbour cells by a rank table (0x00AA76E8, the lowest rank wins; an unzoned cell ranks last), and when that leaves no zone or one of two weak types (7 and 14), the four diagonal cells are looked at too. It reads the zone manager's grid directly.

**GetWealthForNetwork(x, z)** is the wealth (0 to 3) a network cell counts as: the commonest wealth of the lots on the four neighbour cells, a tie going to the higher wealth; the diagonals only when no neighbour has a lot. A lot counts with its occupant wealth, or, when it is in its livable state and that wealth is 0, with the wealth of its building's profile.

Both read the neighbour offsets from the tables at 0x00AA7858 and 0x00AA7878 and compare against the city's size (slots 93 and 94). Ours reads the three tables from the game's memory at every call, so a plugin that changes them changes ours too.

**IterateAllNetworkLots(callback, context)** calls a cdecl callback with (x, z, lot, context) for every lot in the table's order until it answers false, and answers the last answer.

**SetLotConfigurationForNextPiece(configuration)** stores the configuration the next new lot must take (the new one referenced, the old one released). CreateLot uses it once and marks the lot as forced.

**ConfigurationFits** (0x0060CD30; CreateLot and UpdateLot call it) says whether a configuration fits a cell: the cell's place in the configuration's pattern, the zone, the wealth, the piece's value and flag, and the slope of the cell, which must lie between the configuration's two slopes, both ends included.

**The maps.** 0x0060DA50 is operator[] of the lot map and of the pothole map (it puts in an empty entry for a new key); 0x0060E3C0 the same for the configuration map, with its node insert 0x0060E250.

**The cell iterator.** 0x0060C930 resets an iterator and 0x0060C960 sets its rectangle; DoMessage uses them for the cells around a change.

**The lot.** QueryInterface, Release (the deleting destructor releases the configuration and the piece), the number of objects of its configuration and the objects themselves (the game's static empty vector for a lot without a configuration), and its piece.

**The configuration.** AddRef and Release, and four getters: the objects, the ID, the name (copied into the caller's string) and the network piece ID.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.netlot.identity` | 4 detours: the manager's QueryInterface, AddRef, Release, GetGZCLSID |
| `simulators.netlot.queries` | 12 detours: GetNetworkLot, GetNetworkLotConfigurationSet, GetNetworkLotConfigurationById, GetZoneTypeForNetwork, GetWealthForNetwork, IterateAllNetworkLots, ConfigurationFits, the two maps' operator[] and the node insert, the cell iterator's reset and rectangle |
| `simulators.netlot.lot` | 7 detours: the lot's QueryInterface, Release, object count, object array and deleting destructor; the configuration's AddRef and Release |
| `simulators.netlot.slots` | 6 vtable slots: the manager's SetLotConfigurationForNextPiece, the lot's GetNetworkOccupant, the configuration's GetObjectArray, GetID, GetName and GetNetworkPieceId |

All four are off by default. `OPENSC4_NETLOT_SKIP` (a list of function addresses, eight hex digits each) leaves single functions out, for bisecting; a slot is named by the address the game has in it. Each function logs `netlot: first <name>` at its first call.

**Folded code.** The linker gave six of these functions an address that other classes' vtables hold too, because the bytes are the same: SetLotConfigurationForNextPiece is also slot 10 of the city lot's vtable, and the getters are four-byte functions found in up to 150 vtables. A detour there would change all those classes. So `simulators.netlot.slots` stores our functions in the slots of this class's three vtables only (after checking that each slot still holds the game's address) and every other class keeps the game's code.

## What is left and why

- **UpdatePieceStyle 0x0060F510: the game's for good.** The plugin sc4-network-texture-test (`src/cSC4NetworkLotManagerHooks.cpp`) puts a jump in the middle of it, at 0x0060F580, that comes back at 0x0060F5E7 ([patch-census.md](patch-census.md)). A replacement would silently drop the plugin's texture choice. Our UpdateCell (L3) will call it at its address. The tables at 0x00AA7758 (texture) and 0x00AA7728 (style) are patched in place by another DLL (the same plugin writes 16 bytes at 0x00AA7768). Nothing in L1 reads them; the later parts (L3 and the crew) must read both from the game's memory at every use, never from a copy.
- The six adjustor thunks 0x0060E510 to 0x0060E560: two instructions each, they jump to our functions.
- The lot's AddRef 0x004F7BA0 and the configuration's QueryInterface 0x0048C940: other classes' code too, and nothing of ours needs them changed.
- Shared container code in the class's range (cell iterators 0x0060C860, 0x0060C890, 0x0060C8D0, 0x0060CE90, 0x0060CEE0; the hash set of cell keys 0x0060D170 to 0x0060D270 and 0x0060E210; 0x0060D310; 0x0060EE30): other classes use it too. Our code calls the game's containers at their addresses wherever they allocate or free (the tree set's insert and erase, the hash tables' inserts and growth, the vector copy), so every node comes from the game's allocator.
- L2, to come: the factory, the constructor, the destructors, Init, Shutdown, Write, Read, the loading of the configurations (LoadConfigurations with its callback, AddConfiguration, LoadLotConfiguration, the configuration's constructor and destructor).
- L3, to come: DoMessage, UpdateCell, ProcessPending, UpdateLot, CreateLot, RemoveLot, BuildLotObjects, ObjectPosition, CreateProp, RedevelopLot, Pothole, UpdateConnectedPieces, WriteLotConfiguration.
- C1 and C2, the construction crew, to come after the manager.

## The test

The `netlot` part of the daily fingerprint (`src/core/netlot_autotest.cpp`) reaches the manager as the game does, by the city's slot 42, and calls everything through the vtables, so it runs the game's functions or ours. It holds no address and no reference count.

| Field | What |
| --- | --- |
| `st` | initialised; a configuration is forced for the next piece |
| `id` | the class ID of the serializable part and QueryInterface through the three parts for seven IDs (the answer and where in the object it lies), each reference released through the part it came from |
| `maps` | the counts of the lot map, the configuration map (piece IDs), the pothole map and the waiting set |
| `lots` | GetNetworkLot over every cell of the city, z then x, bounds from the city: the cells with a lot; the sum and the exclusive or of a checksum per lot (the cell, its configuration's ID, its object count, its objects' fields, whether it has a piece, the forced byte, its QueryInterface answers for four IDs); the lots that are forced, that have no configuration, whose piece is not the piece the traffic network has on the cell; the object counts added up |
| `net`, `zone`, `wealth` | the cells with a piece of a surface network (asked of the traffic network as UpdateCell asks), and GetZoneTypeForNetwork and GetWealthForNetwork on each: the sum of the answers and the sum and exclusive or of (cell, answer) |
| `off` | the same two calls on every 7th cell that has no piece: the number of cells, then both triples |
| `iter` | IterateAllNetworkLots with a callback of the test's own: the lots, the sum and exclusive or of (x, z, configuration ID), the answer; then a second walk that stops after 3: the lots and the answer |
| `cfg` | GetNetworkLotConfigurationSet into a set built the way the lot editor builds one: the set's count, the entries walked, the sum and exclusive or of each configuration's ID, piece ID and objects; every entry released once, the set taken down as the game does |
| `byid` | GetNetworkLotConfigurationById: the distinct IDs asked (the lots' and the set's), how many it knew (all), how many of two IDs that are not among them (none), and that the second argument's word came back untouched |

The calls change nothing: the two cell queries read the zone grid, the city's size, the lot manager's lot of each neighbour cell, three getters of that lot and the building development simulator's profile table; the set query references each configuration once, and the test releases exactly those.

`OPENSC4_TEST_NETLOT=<day>` drives, once, what changes something and puts it back, adds ` set <checksum>` to the part and writes one log line `autotest: netlot: ...` (also when it found no manager). It fills one set twice (the second call takes the branch that empties a set first; each configuration then holds two references, both given back), then calls SetLotConfigurationForNextPiece with the configuration of the least ID, with the same again, with null, with null again and with what was there before. After each call it notes whether the manager's word is what was set and how far the configuration's reference count is from where it began; every count ends where it began.

Expected on a city: the lot map's count, the cells with a lot and the lots IterateAllNetworkLots gives are the same number (the save's lot count at the load: 1,759 on Big City d0000, 245 on Making Money, 305 on Rush Hour d0000); no lot without a configuration and none forced (the count of lots whose piece is not the one the traffic network gives for the cell is only a number to compare: a cell can hold more than one piece); the second walk gives 3 and answers 0; `byid` reads n,n,0,1. The drive's log line on a city where nothing is forced: steps 1F, references +1, again +1, after null +0.

What runs without any switch (from the coverage runs): the identity, GetNetworkLot, GetZoneTypeForNetwork, GetWealthForNetwork, ConfigurationFits, the maps and the iterator helpers (every load and every cell change), the lot's Release and destructor when a lot is removed. Only the test reaches GetNetworkLotConfigurationSet, GetNetworkLotConfigurationById, IterateAllNetworkLots, the lot's QueryInterface and its object count; only the drive reaches SetLotConfigurationForNextPiece on the manager.

Unreached by any test: the lot's object array for a lot without a configuration (the static empty vector; no such lot exists outside a load's Read to Init window), the configuration's GetName (the lot editor's), the node insert of the lot map for the pothole map (no save has a pothole), a manager Release that destroys (the quit).

## Parity

Batch s1 (2026-10-13), part L1: the original against every network occupant group, the network manager's groups and the four groups of this part for a week, `determinism.dispatch_info` in the base list. Before the first run: `ret N` of the 23 detoured functions against our signatures (no mismatch), the caller register scan and the EAX scan (no hit), every record of the class in the corpus decoded with every byte consumed (21 records, 245 to 1,792 lots, no pothole), and an independent reading of the 23 functions and the 6 swapped slots against the disassembly, the rank and offset tables read from the exe (no difference in behaviour; each swap writes one word of this class's own vtable, the other classes that share the code keep the game's).

| Leg | Saves | Result |
|---|---|---|
| plain | the 12 saves | 12 of 12 the same on the first pair |
| plain | `saves-networks`, `saves-plopped`, `saves-ports` | 3 of 3 each, first pair |
| `OPENSC4_TEST_NETLOT=3` (with the network manager's and the underground pieces' tests) | the 12 saves | 12 of 12, first pair |
| the same | `saves-networks` | 3 of 3 |
| `OPENSC4_TEST_BULLDOZE=3` (the middle half of the city bulldozed) | the 12 saves | 12 of 12, first pair |
| the same | `saves-networks` | 3 of 3 |
| `OPENSC4_TEST_PLOP_TOOL=3` with its commit, tutorial off | the 12 saves | 12 of 12, first pair |
| the same | `saves-networks` | 3 of 3 |

The class's save record was identical in all 66 pairs, the bulldozed cities included (lots removed and chosen again by the game's part L3 code through our maps and queries). On `saves-networks` Big City n0000 the part reads `netlot st 10 id 7322030C maps 1792,259,0,0 lots 1792,835FE7F6,AC5EE58C,0,0,0,3979 net 3380 zone 7468,8C94D0AB,732B62BB wealth 3545,8DB1343E,5EA42B0C off 1855,3266,0CC70F84,186DB828,1338,47F029A8,C15839EC iter 1792,48018379,DB9C64B1,1,3,0 cfg 543,543,055E8D5A,DD1B904C byid 543,543,0,1 set C4029468`.

Ran with ours (first-call log lines over the batch): 28 of the 29; the configuration's GetName did not (only the lot editor asks for a name).
