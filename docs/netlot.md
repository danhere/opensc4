# The network lot manager (cSC4NetworkLotManager)

The city's desk for what stands beside a road. For every cell that has a piece of a surface network (every network type but pipe, power line and subway) and qualifies, the manager keeps one network lot: it ties the piece to one network lot configuration, a list of props and flora with their places on the cell (street lights, trees, bus shelters). The configurations are loaded once per city from exemplars and kept by the network piece ID they are for; the manager picks one that fits the cell by the zone and the wealth around it and the slope under it. It also keeps the potholes. It has no grid: every city size it needs it asks the city for (slots 93 and 94).

Three classes belong to this piece: the manager, the network lot and the network lot configuration. The last two have no class ID and no factory and are made only by the manager's code (the configuration also by the lot editor window). Together 62 functions (14.1 KB), of which two stay the game's: UpdatePieceStyle for good (a plugin patches it) and WriteLotConfiguration (not translated). Together with the construction crew (cSC4NetworkConstructionCrew, 43 functions), which decides when a new piece becomes usable and so when its lot appears, the piece is four classes and 105 functions. The rewrite is split in five parts:

| Part | What | Functions |
| --- | --- | --- |
| L1 | state and queries | 29 |
| L2 | life and save: factory, constructors, destructors, Init, Shutdown, Write, Read, loading of the configurations | 14 |
| L3 | the lots: messages, lot creation and removal, props, potholes, the styling of connected pieces | 12 ours; UpdatePieceStyle and WriteLotConfiguration stay the game's |
| C1 | the construction crew's state and life, with the `game.quiet.network` wrapper folded in | 23 |
| C2 | the construction crew's work | 20 |

L1, L2 and L3 are done: 55 functions are ours. L1 is 29, 23 as detours (`simulators.netlot.identity`, `.queries`, `.lot`) and 6 as swaps of one vtable slot each (`simulators.netlot.slots`, for code that other classes share). L2 is 14, all detours (`simulators.netlot.life`, `.save`, `.object`), with the flag group `simulators.netlot.own` for a manager that carries our copies of its vtables. L3 is 12, all detours (`simulators.netlot.messages` 3, `.lots` 7, `.pothole` 1, `.styles` 1). With that the lot manager is complete but for two functions: UpdatePieceStyle, which stays the game's for good, and WriteLotConfiguration. The six adjustor thunks of the range and the shared container code stay the game's too (see "What is left and why"). C1 and C2, the construction crew (23 and 20 functions), are to come, after its stopwatch is put on simulation time.

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
| 14 | 0x0060E810 | WriteLotConfiguration(configuration, segment) | the game's |
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

Fields: +0x4 a vector of lot configuration objects (0x34 bytes each, the same kind a lot configuration has: [lotconfig.md](lotconfig.md)), +0x10 the ID, +0x14 the name (a string of the game, 0x14 bytes), +0x28 the network piece ID, +0x2C four mask bytes and +0x30 the period (which cells of a run of pieces the configuration is for), +0x34 a bit vector of the 16 zone types, +0x48 one of the 4 wealths, +0x5C and +0x60 the least and greatest slope, +0x64 a mask over a value of the piece, +0x65 a rule about a flag of the piece, +0x68 the reference count.

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

The lots come in the hash table's order. Read makes each lot with its piece and the configuration's ID; Init, which runs after it, looks the configuration up by the ID (see "What the functions of L2 do"). The configurations themselves, the forced configuration and the waiting set are not saved. Write's code agrees with the decoder L1 made from it: nothing in `tools/re/save_schemas/netlot.py` changed with L2.

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

## What the functions of L2 do

| Function | Address | Group | What it does |
| --- | --- | --- | --- |
| the factory | 0x0060F4F0 | `object` | cdecl: 0x88 bytes from operator new and the constructor; null without memory. The object has a reference count of 0 |
| the constructor | 0x0060EE50 | `object` | the base classes' vtables at +4 and +8 (0x00A81174, 0x00A80784), then the class's three; the four hash tables (lot map, configuration map, pothole map, waiting set), each with 100 buckets asked of the table's constructor 0x00829BC0; no forced configuration; the cell iterator reset (step 1, the widest rectangle, cell 0, 0); count and initialised byte 0. It subscribes to nothing and asks no service |
| the destructor | 0x0060E480 | `object` | the three vtables again; the nodes and buckets of the waiting set, the pothole map, the configuration map (with its vectors) and the lot map freed; the forced configuration released. It does not call Shutdown and releases nothing a table holds |
| the deleting destructor | 0x0060EF20 | `object` | slot 15, `ret 4`: the destructor, and with flag bit 0 operator delete |
| Init | 0x00610120 | `life` | slot 3, once per object: see below |
| Shutdown | 0x0060E570 | `life` | slot 4, nothing unless initialised: the six unsubscriptions in Init's order; every lot released, but the lot map keeps its nodes; every configuration released and the configuration map cleared; every pothole's occupant gets its slot 8 with 1 and is released, and the pothole map is cleared. The forced configuration and the waiting set are not touched. AL 1 |
| LoadConfigurations | 0x006100D0 | `life` | the resource manager's list of the keys of the exemplar type (0x6534284A), walked with the callback and the manager as its context; nothing without a resource manager |
| the resource callback | 0x0060FB90 | `life` | cdecl (a key, the manager): the key's exemplar; when its exemplar type property (0x10) is 0x21, a configuration is made (0x6C bytes, the constructor), filled by LoadLotConfiguration and, when that says yes, added by AddConfiguration; a configuration that was not added is destroyed again |
| AddConfiguration | 0x0060E7A0 | `life` | `ret 4`: the configuration referenced and appended to the vector of its network piece ID in the configuration map (the map's operator[] makes the entry for the first of a piece) |
| LoadLotConfiguration | 0x0060F5F0 | `life` | `ret 8` (a property holder, a configuration): see the property table below. The lot editor calls it too (0x0049C5B4) |
| the configuration's constructor | 0x0060FA90 | `life` | the vtable, an empty object vector, ID 0, an empty name with eight bytes of storage, piece ID 0, period 4, the four mask bytes 0x0F, the zone types' vector 16 clear bits and the wealths' 4, slopes 0 and 64, piece mask 0x0F, rule 0, count 0 |
| the configuration's deleting destructor | 0x00610070 | `life` | slot 7, `ret 4`: the two bit vectors' words and the name's characters freed, the object vector destroyed (the lot configuration's 0x00479760), and with flag bit 0 operator delete |
| Write | 0x0060D6B0 | `save` | the record above; a lot's configuration ID is asked of its configuration (a lot without one would be a call through null: none exists outside the window between Read and Init). EAX 1 when the stream has no error |
| Read | 0x0060DAC0 | `save` | refuses any version but 2 (it does not ask the stream's state first). Per lot: 0x18 bytes from operator new built in place (the vtable 0x00AA78E0, piece, configuration and count cleared, the forced byte 0), one reference, the key, the piece (interface 0xA821EF94) into +0x4, the configuration's ID into +0xC, and the lot into the lot map under the key. Per pothole: the key, the occupant (interface 0x09820792), into the pothole map. Nothing is emptied first and no read's answer is looked at; EAX 1 when the stream has no error at the end |

The configuration's constructor and deleting destructor are in `simulators.netlot.life` with the loading that makes configurations; `simulators.netlot.object` is the manager's own object only, so that it pairs with `simulators.netlot.own`.

**Init**, in the game's order:

1. The initialised byte (+0x70) is set; a second call answers 1 and does nothing.
2. The part at +4 is subscribed at the message server (0x00B43CCC, not tested for null) to six messages, in this order:

| Message | What it is (DoMessage, L3) |
| --- | --- |
| 0x89D3E9F3 | a piece's usable flag changed |
| 0x99EF1143 | an occupant was removed |
| 0xA9B6C2D6 | a lot's objects are to be built |
| 0x07699455 | cells changed |
| 0xE8BDA2E7 | a lot's state changed |
| 0x69247DC8 | a pothole |

3. The cell iterator at +0x38 is set to the whole city: rectangle 0, 0, x count - 1, z count - 1, both counts asked of the city (slot 94 first, then slot 93); no size is a constant.
4. LoadConfigurations.
5. Every lot Read left in the lot map gets its configuration: the first configuration of the configuration map (table order, then vector order) whose ID is the one in the lot's +0xC is referenced and stored at the lot's +0x8.
6. **The missing-configuration case.** A lot whose ID no loaded configuration has (a plugin with network lot exemplars was removed, say): the game formats the text `Missing lot configuration 0x%08x` into a string and throws it away (the release build has nowhere to send it), collects the lot's cell key, and after the walk erases every collected key from the lot map. The erase frees the map's node only. The lot keeps the reference Read gave it and its network piece and can never be reached again: a leak of the original, which ours keeps (the lot and the piece's reference stay exactly as in the game). Ours makes the same allocations in the same order (the string's eight bytes, the formatter, the key vector's growth) and adds one line of its own to the log: `netlot: Init: <n> lots had no configuration of their saved ID and left the lot map`.

So the load order is Read (the lots, with IDs), then Init (the configurations from the resources, then the lots' pointers). A new city has no Read; Init then only subscribes and loads.

**LoadLotConfiguration** reads these properties of the exemplar, in this order:

| Property | Type | Becomes |
| --- | --- | --- |
| 0x88EDC789 | uint8 | the exemplar's version (1 when missing); not stored. Below 2 the exemplar is written back at the end |
| 0xC9A5A1BE | uint32 | +0x28 the network piece ID. Without it nothing is loaded and the answer is no |
| 0x00000020 | string | +0x14 the name |
| 0x00000021 | uint32 | +0x10 the ID |
| 0x88EDC793 | bytes | +0x34 the zone types: the bit of every listed type below 16 is set |
| 0x88EDC795 | bytes | +0x48 the wealths: the bit of every listed wealth below 4 is set |
| 0x49D55951 | bytes | +0x2C the four cell mask bytes, only when the property has exactly four |
| 0xCA81B8D4 | uint8 | +0x30 the period |
| 0xAA120972 | float | +0x5C the least slope |
| 0xAA120973 | float | +0x60 the greatest slope |
| 0x88EDC900 and following | uint32 arrays | +0x4 the objects, by the lot configuration's LoadObjects (0x00479D10, [lotconfig.md](lotconfig.md)) with the version and the detail 3; the vector is then cut to its exact size |
| 0xEC3BD470 | uint8 | +0x64 the mask over the piece's value |
| 0xCC3E4755 | uint8 | +0x65 the rule about the piece's flag (0 any, 1 needs, 2 forbids) |

A field whose property is missing keeps what the constructor left (the bit vectors are not cleared first, so a configuration without the zone or wealth property fits no zone or no wealth). After the properties the objects are moved into the cell (the lot configuration's ClampObjects with the rectangle 16, 16, 0, 0). For a version below 2 the manager's own WriteLotConfiguration (slot 14, the game's) writes the exemplar back in the current form to the segment that holds the key (type 0x6534284A, group 0x89AC5643, the ID).

Copied on purpose from the game and commented in the code: the message server and, for an old exemplar, the resource manager are used without a test for null; Read and the callback call slot 1 of what operator new gave without a test; Shutdown leaves the lot map's nodes pointing at released lots (the destructor frees the nodes without reading them); Read overwrites a lot that is already under a key without releasing it. Not reproduced, and the test stays away from it: Read's two counts share a stack word the game never sets, so a stream that ends right after the version makes the game build as many lots as that word says; ours reads 0 there.

## What the functions of L3 do

Twelve functions, each a detour at the game's address (`src/core/netlot_lots.cpp`). A call from one of them to another function of the class goes to the game's address or through the vtable, as the game calls it, so a group that is off, or a function on the skip list, is the game's.

| Function | Address | Group | Arguments (its own `ret N`) |
| --- | --- | --- | --- |
| DoMessage | 0x0060FC30 | `messages` | the message part's slot 3: a message (`ret 4`); `this` is the object + 4 |
| UpdateCell | 0x0060F980 | `messages` | x, z (`ret 8`) |
| ProcessPending | 0x0060FA10 | `messages` | none |
| UpdateLot | 0x0060E2D0 | `lots` | piece, x, z, zone type, wealth (`ret 0x14`) |
| CreateLot | 0x0060DCE0 | `lots` | piece, zone type, wealth (`ret 0xC`) |
| RemoveLot | 0x0060D8B0 | `lots` | piece (`ret 4`) |
| BuildLotObjects | 0x0060D410 | `lots` | lot (`ret 4`) |
| ObjectPosition | 0x0060C9B0 | `lots` | lot, object, three floats out, rotation out (`ret 0x10`) |
| CreateProp | 0x0060CF80 | `lots` | slot 8: lot, object index, prop type (`ret 0xC`) |
| RedevelopLot | 0x0060D5E0 | `lots` | slot 12: x, z, lot (`ret 0xC`) |
| Pothole | 0x0060DF50 | `pothole` | x, z, a byte: add or remove (`ret 0xC`) |
| UpdateConnectedPieces | 0x0060EF40 | `styles` | x, z (`ret 8`) |

### The six messages

The manager has no timer and no frame message; everything it does to lots starts in DoMessage. It always answers 1.

| Message | What DoMessage does |
| --- | --- |
| 0x89D3E9F3, a piece's usable flag changed | The object the message carries is asked for the network occupant interface (0xA821EF94); nothing when it refuses. For a piece that keeps lots (any flag of the surface networks, mask 0x1F4F, and not the flag 0x200000): UpdateCell for the piece's cell when the message's first data word is not 0, RemoveLot when it is 0. The piece is released |
| 0x99EF1143, an occupant was removed | The occupant (the first data word) is asked the same way; a piece that keeps lots gets RemoveLot; released |
| 0x07699455, cells changed | The rectangle the second data word points at, grown by one cell on every side: every cell of it inside the city goes into the waiting set (+0x74), row by row; then ProcessPending |
| 0xE8BDA2E7, a city lot's state changed | The object carried is asked for the lot interface (0xE818FCED). The lot's cell rectangle grown by one on every side goes to a cell iterator, and every cell the game's iterator function (0x0060CEE0, called at its address) gives that is inside the city goes into the waiting set; then ProcessPending; the lot is released |
| 0x69247DC8, a pothole | Pothole(first data word, second data word, third data word not 0) |
| 0xA9B6C2D6, a lot's objects are to be built | BuildLotObjects(the network lot in the first data word) |

"Inside the city" is a signed compare of x and z with the city's slots 93 and 94, asked once per message. The cell key put into the set is `((x & 0xFF) << 8) | z`.

Two things in the game's code are worth knowing and are kept as they are. For "cells changed" the game steps a cell iterator of its own on the stack with the stepping written out in place: every cell of the rectangle, x first. For "a lot's state changed" it calls the shared iterator function 0x0060CEE0 instead, which walks only the border of a rectangle (the first and the last row whole, the two end columns of the rows between) and begins at the iterator's current cell; the reset before it (0x0060C930) leaves that at (0, 0) and setting the rectangle does not move it. So what goes into the waiting set is row z = 0 from x = 0 to the rectangle's high x, then the two end columns of every row from 1 up, then the rectangle's last row whole: more cells than the lot's surroundings, and not the lot's first row unless the lot is at the map's top. Every such cell that has a surface piece gets UpdateCell. Ours calls the same function with the same iterator state, so it queues the same cells in the same order.

**ProcessPending** works the waiting set off: while it has a key, UpdateCell for the table's first key (high byte x, low byte z) and the key erased. UpdateCell can take keys out itself (see the flood).

**UpdateCell(x, z)**: the piece of a surface network the traffic network (the city's slot 40) has on the cell; nothing without one or when the piece has any flag of 0x80200000. Then the game's UpdatePieceStyle, **called at its address 0x0060F510**, which sets the piece's texture and style or, for a piece of the grouped network, calls UpdateConnectedPieces. Then, unless the piece has the network flag 0x40: the manager's own GetWealthForNetwork and GetZoneTypeForNetwork (in that order, through the vtable) and UpdateLot.

### How a lot is chosen, kept, chosen again or removed

**UpdateLot(piece, x, z, zone, wealth)** looks the cell up in the lot map.

- No lot: CreateLot, when the piece is usable (its slot 19).
- A lot whose forced byte is set stays, whatever changed.
- A lot whose configuration still fits stays: ConfigurationFits with the cell, the zone, the wealth and the piece's rotation and flip.
- Else the lot is chosen again: RemoveLot, then CreateLot, both for the piece the lot holds (referenced meanwhile). A lot without a piece is left alone.

**CreateLot(piece, zone, wealth)**:

1. The candidates are the configurations loaded for the piece's ID (the piece's slot 23; the configuration map). Without an entry and without a forced configuration there is no lot.
2. The piece's cell: its position through the city's slot 95.
3. A forced configuration (SetLotConfigurationForNextPiece) is taken as it is. Else every candidate, in the entry's order, is asked of ConfigurationFits; those that fit are collected in a vector of the function's own. With none there is no lot.
4. **The draw.** One number from the lot manager's static generator picks among those that fit: `SC4Random::Below(count)` (0x00913A06 on the generator at 0x00B44870, called at 0x0060DE59). No draw when a configuration is forced or nothing fits; a draw also when exactly one fits.
5. The lot: 0x18 bytes built in place (the vtable 0x00AA78E0), one reference for the map; it references its configuration and its piece. It is stored in the lot map under the cell key, and BuildLotObjects builds its objects.
6. A forced configuration is used up: the lot's forced byte is set, the manager's word cleared and its reference released.

**RemoveLot(piece)**: the piece's cell; the street props of the cell removed (the prop manager's slot 33) and the flora in the cell (the flora simulator's slot 20); then the lot under the cell's key is released (which releases its configuration and its piece) and the key erased. The props and the flora go even when the cell has no lot.

### How objects become props and flora

**BuildLotObjects(lot)** walks the objects of the lot's configuration (0x34 bytes each, [lotconfig.md](lotconfig.md)) by index:

- **A prop** (kind 1, and bit 0 of the flag byte at +1 clear): the candidate prop types of the object from the lot configuration manager (0x00B43D18, slot 27). With candidates, **one draw**, `SC4Random::Below(number of candidates)` on the same generator (called at 0x0060D4AC), and CreateProp (through the vtable, slot 8) with the lot, the index and the drawn type.
- **Flora** (kind 4): the candidates the same way. With candidates, **one draw** the same way (called at 0x0060D512), ObjectPosition, and the flora simulator's (0x00B43D80) slot 17 with the drawn type, the place and 1. The flora occupant gets the object's index as its lot tag.
- Any other kind, a prop with flag bit 0, or an object without candidates: nothing, and no draw.

At the end the traffic network (the global 0x00B43D54) is told of the lot's piece (its slot 36).

So the generator moves once per lot chosen among fitting configurations and once per object built, in the order the messages arrive. Its stream is shared by everything the manager does, so ours draws through the game's own function on the game's own generator object, in the same places, with the same bounds: `src/core/netlot_lots.cpp` has no generator code of its own.

**CreateProp(lot, index, type)**: ObjectPosition for the object; with a type of 0 nothing more. The prop manager (0x00B43D10) makes a street prop of the type at the place with the rotation (its slot 32). The prop's occupant part gets the object's ID (the word at +0x24) as its lot tag and, on a flipped piece, the flag 0x80. The answer is the occupant part or null. The props belong to the prop manager and are saved by it.

**ObjectPosition(lot, object, place out, rotation out)** turns an object's place on the 16 by 16 metre cell into a place in the city. The piece gives the cell it stands on (slot 59); the cell's corner is 16 x the cell and the far corner 16 more. With the piece's rotation (slot 24) and the object's place (px, pz):

| Rotation | x | z |
| --- | --- | --- |
| 0 | far x - px | far z - pz |
| 1 | pz + corner x | far z - px |
| 2 | px + corner x | pz + corner z |
| 3 | far x - pz | px + corner z |

The height is the piece's height at that point (slot 56) plus the object's own. The object's rotation is its orientation byte plus the piece's rotation, modulo 4. **The mirror**: on a flipped piece (slot 25) x is mirrored in the cell, x = (16 - 2 (x - corner x)) + x, and the rotation comes from a table of sixteen words at 0x00AA78A0 (index: piece rotation + 4 x orientation; rows 0 3 2 1, 3 2 1 0, 2 1 0 3, 1 0 3 2), read from the game's memory. The game computes all of it on the x87 stack at double precision and stores every result as a float; ours does the sums in doubles in the same operand order and rounds to float where the game stores one. The constants are 16.0 (0x00A8D45C) and 2^32 (0x00A80AA8, the game's way of making an unsigned number a float).

**RedevelopLot(x, z, lot)** (slot 12; it has no direct caller, callers through the vtable were not traced, and no coverage run reached it; the test calls it): the street props of the cell removed (the low bytes of x and z), the flora in the cell removed, BuildLotObjects.

### Potholes

**Pothole(x, z, add)** keeps the pothole map (+0x58, cell key to a prop occupant).

Add, unless the cell has one: a visual effect prop is made by its class ID 0x2977AA49 through the framework (interface 0x09820792); it is placed at the cell's position (the city's slot 97) on the terrain's height; when the occupant manager (0x00B43D0C) finds a piece of a surface network on the cell (its slot 32 with an occupant filter for the mask 0x1F4F), the height is the piece's instead and the place moves by a pair of signed bytes from a table at 0x00AA7910, picked by the piece's active sides, times 4.0; the effect is named `pothole_rail` when the piece's network flags are rail alone, else `pothole`; its occupant part goes into the occupant manager (slot 20), the effect is started, and the pothole map keeps it with the reference the framework gave.

Remove, when the cell has one: the map's node erased, the effect stopped, its occupant part taken out of the occupant manager (slot 21) and the effect released.

Nothing here draws a random number and nothing draws on the screen: the prop is made, placed and handed to the managers. No save of the corpus has a pothole and no coverage run reached the function; who sends the message was not looked for (nothing in this class does).

### The flood: one style for a connected run

**UpdateConnectedPieces(x, z)** is called by the game's UpdatePieceStyle for a usable piece that has the network flag 0x40. Bit n of a piece's network flags is network type n, and the edge value the flood reads is that of type 6, which is Avenue in gzcom-dll's `eNetworkType`: the two halves of an avenue get one texture and one kind of lot along the whole run, where every other network is styled cell by cell.

1. From the cell, the flood collects the group: every cell reached through the sides whose byte of the piece's type-6 edge value has one of its low three bits, as long as the cell has a piece of that network with the same slot-55 byte (intersections and plain pieces do not mix). Cells that fail are remembered so they are looked at once.
2. Around every piece of the group the zone type and the wealth are asked (the manager's own slots 9 and 10) and counted.
3. The group's zone type is the type of 1 to 15 counted most often, the lowest of equals (0 when none); its wealth the wealth of 1 to 3 counted most often, the lowest of equals (0 when none).
4. The texture is the word (wealth + 4 x zone type) of the table at 0x00AA7758, the style the byte (3 x zone type + wealth - 1, or 3 x zone type for wealth 0) of the table at 0x00AA7728.
5. Every usable piece of the group gets the texture (its slot 29, the under texture) and the style (its slot 27, the variation) and UpdateLot with the group's zone type and wealth; every cell of the group is taken out of the waiting set, so ProcessPending does not come back to it.

**Both tables are read from the game's memory at every call**, never from a copy: the plugin sc4-network-texture-test writes into the texture table in place.

**The neighbour arithmetic is the game's, copied with its edge cases.** A neighbour is computed in 16 bits and passed over when x or z is negative or *above* the city's size, so x equal to the size passes; and its key is built as `z | ((x & 0xFF) << 8)`. On a city smaller than 256 cells a side the cell just outside is then looked up and has no piece. On a city 256 cells wide a neighbour at x = 256 becomes x = 0 of the same row, and one at z = 256 becomes the cell (x | 1, 0): an avenue that runs off the east or south edge with its outward side marked as connected would be joined to whatever avenue stands at that cell on the far side. No such case was looked for in the corpus.

### Reference counts

Every count the twelve functions change, as in the game: DoMessage releases the piece (or the city lot) QueryInterface gave, once, on every path after a successful QueryInterface. UpdateLot holds the old lot's piece across RemoveLot and CreateLot. RemoveLot releases the lot once (the map's reference). CreateLot references the new lot once for the map, the configuration and the piece once each for the lot, and releases a forced configuration once after clearing the manager's word. Pothole keeps the framework's reference in the map and releases it on removal; the occupant filter, the occupant found and the piece asked of it are released before it returns. One thing is kept as compiled: CreateLot stores the new lot over whatever the map has under the key without releasing it; UpdateLot never calls it for a cell that has an entry, so nothing is lost in practice.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.netlot.identity` | 4 detours: the manager's QueryInterface, AddRef, Release, GetGZCLSID |
| `simulators.netlot.queries` | 12 detours: GetNetworkLot, GetNetworkLotConfigurationSet, GetNetworkLotConfigurationById, GetZoneTypeForNetwork, GetWealthForNetwork, IterateAllNetworkLots, ConfigurationFits, the two maps' operator[] and the node insert, the cell iterator's reset and rectangle |
| `simulators.netlot.lot` | 7 detours: the lot's QueryInterface, Release, object count, object array and deleting destructor; the configuration's AddRef and Release |
| `simulators.netlot.slots` | 6 vtable slots: the manager's SetLotConfigurationForNextPiece, the lot's GetNetworkOccupant, the configuration's GetObjectArray, GetID, GetName and GetNetworkPieceId |
| `simulators.netlot.life` | 8 detours: Init, Shutdown, LoadConfigurations, the resource callback, AddConfiguration, LoadLotConfiguration, the configuration's constructor and deleting destructor |
| `simulators.netlot.save` | 2 detours: Write and Read |
| `simulators.netlot.object` | 4 detours: the manager's factory, constructor, destructor and deleting destructor |
| `simulators.netlot.own` | no detour: with it and `object`, a manager carries our copies of the class's three vtables |
| `simulators.netlot.messages` | 3 detours: DoMessage, UpdateCell, ProcessPending |
| `simulators.netlot.lots` | 7 detours: UpdateLot, CreateLot, RemoveLot, BuildLotObjects, ObjectPosition, CreateProp, RedevelopLot |
| `simulators.netlot.pothole` | 1 detour: Pothole |
| `simulators.netlot.styles` | 1 detour: UpdateConnectedPieces |

All twelve are off by default. `OPENSC4_NETLOT_SKIP` (a list of function addresses, eight hex digits each) leaves single functions out, for bisecting; a slot is named by the address the game has in it. Each function logs `netlot: first <name>` at its first call.

**Folded code.** The linker gave six of these functions an address that other classes' vtables hold too, because the bytes are the same: SetLotConfigurationForNextPiece is also slot 10 of the city lot's vtable, and the getters are four-byte functions found in up to 150 vtables. A detour there would change all those classes. So `simulators.netlot.slots` stores our functions in the slots of this class's three vtables only (after checking that each slot still holds the game's address) and every other class keeps the game's code.

### Our own object

With `simulators.netlot.object` and `simulators.netlot.own` the constructor and the destructor store the addresses of our copies of the three vtables (16, 4 and 6 slots; `src/core/occ_vtables.h`, as the network manager and the network occupant classes do) instead of the game's tables. A copy is the game's table word for word, taken at the first construction, which is a city's and so comes after every replacement is in place; then the entries that hold a function of the `object` group directly are pointed at ours: slot 15 of the primary table, the deleting destructor (the log line `netlot own: vtables copied (16, 4 and 6 slots), 1 entries redirected`). Every other entry stays the game's address or adjustor thunk, which the other groups' detours still decide. `own` without `object` does nothing.

`simulators.netlot.slots` and `own` work in any combination, because `slots` changes the game's primary table in place at PostAppInit and the copy is taken later: with `slots` on, the copy's slot 13 holds our SetLotConfigurationForNextPiece; with it off (or the slot on the skip list), the game's folded code. The lot's and the configuration's vtables are not copied: Read and the configuration's constructor store the game's tables, which `slots` changes in place.

## What is left and why

- **UpdatePieceStyle 0x0060F510: the game's for good.** The plugin sc4-network-texture-test (`src/cSC4NetworkLotManagerHooks.cpp`) puts a jump in the middle of it, at 0x0060F580, that comes back at 0x0060F5E7 ([patch-census.md](patch-census.md)). A replacement would silently drop the plugin's texture choice. Our UpdateCell calls it at its address. The tables at 0x00AA7758 (texture) and 0x00AA7728 (style) are patched in place by another DLL (the same plugin writes 16 bytes at 0x00AA7768). Of our code only UpdateConnectedPieces reads them, from the game's memory at every call, never from a copy.
- **WriteLotConfiguration 0x0060E810 (slot 14): left the game's.** 1,560 bytes that build an exemplar from a configuration and write it to a segment. Its only callers are the lot editor and LoadLotConfiguration's rewrite of an exemplar below version 2; none of the 34 coverage runs reached it, so no test would show a slip, and the rule for a function of that size is to take it only when every instruction is certain. Ours calls it through the vtable where the game does.
- The six adjustor thunks 0x0060E510 to 0x0060E560: two instructions each, they jump to our functions.
- The lot's AddRef 0x004F7BA0 and the configuration's QueryInterface 0x0048C940: other classes' code too, and nothing of ours needs them changed.
- Shared container code in the class's range (cell iterators 0x0060C860, 0x0060C890, 0x0060C8D0, 0x0060CE90, 0x0060CEE0; the hash set of cell keys 0x0060D170 to 0x0060D270 and 0x0060E210; 0x0060D310; 0x0060EE30): other classes use it too. Our code calls the game's containers at their addresses wherever they allocate or free (the tree set's insert and erase, the hash tables' inserts and growth, the vector copy), so every node comes from the game's allocator.
- Called at their addresses by L2 and not this class's: the hash tables' constructor, clear, begin and erase by key (0x00829BC0, 0x008EBA60, 0x004B0390, 0x00710910), the clear of a table of vectors (0x00584660), the pointer vector's growing insert (0x0051CA60), the bit vector's resize and bit address (0x0060D310, 0x00477A40), the property getters (0x005FD3C0, 0x005FD480, 0x005FD4F0, 0x005FD570, 0x005FFB40), the string's formatter (0x0090F574), the stream's reference helpers (0x005D99A0, 0x005D9730), and the lot configuration's LoadObjects, vector copy, vector destructor and ClampObjects (0x00479D10, 0x00479670, 0x00479760, 0x00477BB0: ours in the lot configuration's groups).
- Nothing of L2 reads the tables at 0x00AA7758 and 0x00AA7728 or draws from the lot manager's static generator 0x00B44870 (only BuildLotObjects and CreateLot, L3, do).
- Called at their addresses by L3 and not this class's: the hash set of cell keys (find 0x0060D170, count 0x0060D1B0, erase at a node 0x0060D1F0, insert 0x0060D270, growing insert 0x0060E210, growth 0x00710DD0, erase by key 0x00710910), the tables' constructor, clear and begin, the cell iterators (0x0060CE90, 0x0060CEE0), the pointer vector's reserve and growing insert (0x0045D4F0, 0x0051CA60), the generator's Below (0x00913A06), the network manager's occupant filter constructor (0x0061F530) and the framework's COM (0x0090DDF1).
- C1 and C2, the construction crew, to come.

### Decisions taken (2026-10-13 and 2026-10-14)

- **The 256-cell packing of the cell key stays.** The key `(x << 8) | z` is the game's format, in its tables and in every save (see "The cell key and what it limits"). Ours keeps it and the limit is left to the bigger-maps work, which needs a new key and a new record version anyway.
- **The crew's stopwatch goes on simulation time.** Under `determinism.sim_timers`, on both sides of a parity run, as the other simulators' stopwatches do. This comes with the crew parts (C1 and C2).
- **The crew's static generator is seeded deterministically** before the crew parts are replaced, so that the two sides draw the same numbers.
- **Our crew Init reads and honours the sounds plugin's byte at 0x006071FC**, as the game's does.
- **The tables at 0x00AA7758 and 0x00AA7728 are read from the game's memory** at every use, never copied, because another DLL patches them in place (see UpdatePieceStyle above).

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
| `load` | the same configurations as `cfg`, read from their fields: how many, and the sum and exclusive or of a checksum of what the constructor and LoadLotConfiguration left in each (the ID, the name, the network piece ID, the four mask bytes, the period, the size and bits of the zone types' and the wealths' vectors, the two slopes as float bits, the piece mask, the rule byte, the number of objects) |
| `byid` | GetNetworkLotConfigurationById: the distinct IDs asked (the lots' and the set's), how many it knew (all), how many of two IDs that are not among them (none), and that the second argument's word came back untouched |

The calls change nothing: the two cell queries read the zone grid, the city's size, the lot manager's lot of each neighbour cell, three getters of that lot and the building development simulator's profile table; the set query references each configuration once, and the test releases exactly those.

`OPENSC4_TEST_NETLOT=<day>` drives, once, what changes something and puts it back, adds ` set <checksum>` to the part and writes one log line `autotest: netlot: ...` (also when it found no manager). It fills one set twice (the second call takes the branch that empties a set first; each configuration then holds two references, both given back), then calls SetLotConfigurationForNextPiece with the configuration of the least ID, with the same again, with null, with null again and with what was there before. After each call it notes whether the manager's word is what was set and how far the configuration's reference count is from where it began; every count ends where it began.

The same day the drive runs the calls of L2, adds ` life <checksum>` after ` set` and writes a second log line, `autotest: netlot: life: ...`:

- Init (slot 3) on the city's manager: it is initialised, so the answer is 1 and the four tables' counts are what they were.
- Write (the serializable part's slot 3) of the city's manager into a memory stream of the test's own, once into a stream without an error (answer 1) and once into one that reports one (answer 0). The stream answers no interface, so the game's WriteReference writes nothing for a reference and touches no object: the bytes are the record without its references (10 bytes, 6 per lot, 2 per pothole, in the tables' order), and the length and every byte are in the checksum. Left out, and said so in the log line, if a lot has no configuration (Write would call through null; no such lot exists after Init).
- Unless `OPENSC4_TEST_NETLOT_OBJECT=0`: a second manager through the factory. As constructed: the initialised byte, no forced configuration, the four tables' counts and bucket counts, the cell iterator, and that its three vtable pointers are the city's manager's. Its identity through the three parts. Shutdown on it (1, nothing done). Write of the empty object (10 bytes). Read from streams that build nothing: empty, version 1, version 3, version 2 with a count of 0 and no pothole count (0, six bytes taken), version 2 with both counts 0 (1, ten bytes), the same bytes in a stream already in error (0: Read does not ask first), an empty stream in error. Then Read of an 18-byte record with one lot (cell 0x34, 0x12, configuration ID 0xAABBCCDD) and one pothole: the answer 1, both maps' counts 1, GetNetworkLot finds the lot at its cell and nothing at the swapped cell, the lot has the game's lot vtable, no piece and no configuration, the saved ID, one reference and no forced byte, and the pothole map's node has the key and a null occupant (the stream gives no reference). The test releases the lot (which destroys it), and the last Release of the manager answers 0: the deleting destructor ran.

The same day the drive runs the calls of L3, adds ` lots <checksum>` after ` life` and writes a third log line, `autotest: netlot: lots: ...`. Two things always run, because they change nothing:

- **ObjectPosition** (at its address) for the first four objects of every lot that has a piece: the cell key, the object's index, the three floats' bits and the rotation, as a count, a sum and an exclusive or. The function only asks the piece and reads the object.
- **Messages that must be refused**, each answered 1 with the four tables' counts as before: a type the manager does not know; "an occupant was removed" whose occupant is the manager itself, and "a piece's usable flag changed" and "a lot's state changed" that carry the manager itself (it refuses the interface asked for); and a pothole removal on a cell that has no pothole.

Four more are each behind a switch of their own, off by default, because each leaves a trace in the city:

| Switch | What it does | What it leaves |
| --- | --- | --- |
| `OPENSC4_TEST_NETLOT_POTHOLES=1` | The pothole message for three cells with a surface piece (by their place in cell order) and for the first cell whose piece is rail alone: add; the map's count, the effect's name, started byte, occupant type, position bits and whether the occupant manager has it; add again (nothing); remove; the count back, the cell empty, the effect out of the occupant manager | Nothing of the manager's. But a visual effect prop went through the occupant manager (its insert and remove messages reach every subscriber), its effect was started through the effects manager (which may draw from the effects' generator), and the framework allocated and freed it |
| `OPENSC4_TEST_NETLOT_REDEVELOP=1` | RedevelopLot on every 50th lot in cell order (the first, the 51st, ...). The log has the lots and how many prop objects and flora objects their configurations asked BuildLotObjects for (not how many the managers then made: the prop manager's and the flora simulator's fingerprint parts show that) | The cells' street props and flora are removed and built again with new draws: the city is changed for good, the same on both sides as long as the draws are the same. This is the direct test of BuildLotObjects, ObjectPosition and CreateProp |
| `OPENSC4_TEST_NETLOT_CELLS=1` | The cells-changed message for the one-cell surroundings of three cells with a surface piece. DoMessage grows that by one more, so UpdateCell, the game's UpdatePieceStyle and UpdateLot with ConfigurationFits run on every piece in 5 by 5 cells. After each: the answer, the waiting set's count (0), the lot map's count | A piece's texture and style are set again from the zone and wealth around it as they are now, and a lot whose configuration no longer fits is chosen again (a draw) |
| `OPENSC4_TEST_NETLOT_STYLES=1` | UpdateConnectedPieces (at its address) on three cells that have a piece with the network flag 0x40. After each: the piece's style byte and under texture, the waiting set's count, the lot map's count. The log has how many such cells the city has | The texture and the style of every usable piece of each connected group are set again, and lots may be chosen again (draws) |

The log line also carries a checksum of the lot manager's generator before and after the drive (`generator X -> Y`). It is not in the fingerprint (the seed is the run's business); it moves exactly when something drew, so on the two sides of a pair with the same seed the second value must agree.

**Which field shows a lot chosen again.** The daily `netlot` part already holds every lot's state: in `lots` the second and third numbers (the sum and the exclusive or of the per-lot checksums, which have the configuration's ID and the objects) and the last (the object counts added up) move when a lot gets another configuration, and `iter` moves with them; the first number of `maps` and of `lots` moves when a lot appears or goes.

Not done, on purpose: Init on the second object (it would subscribe and load every configuration again), Shutdown on the city's manager, and Read of a stream that ends right after version 2 (the game would loop over an unset stack word).

Expected in the life line on a city: `Init again 1 (tables as before)`, and the written length is 10 + 6 x lots + 2 x potholes.

Expected on a city: the lot map's count, the cells with a lot and the lots IterateAllNetworkLots gives are the same number (the save's lot count at the load: 1,759 on Big City d0000, 245 on Making Money, 305 on Rush Hour d0000); no lot without a configuration and none forced (the count of lots whose piece is not the one the traffic network gives for the cell is only a number to compare: a cell can hold more than one piece); the second walk gives 3 and answers 0; `byid` reads n,n,0,1. The drive's log line on a city where nothing is forced: steps 1F, references +1, again +1, after null +0.

What runs without any switch from L2: at every city load the factory and the constructor (when the save brings no manager, the city's own creation makes one), Read (a saved city), Init with LoadConfigurations, the callback for every exemplar, and the configuration's constructor, LoadLotConfiguration and AddConfiguration for every network lot exemplar (543 on `saves-networks`; `load` and `cfg` show the result); at every save Write (the save record is compared); at every city close and at quit Shutdown, the last Release, the deleting destructor, the destructor and the configurations' deleting destructors. Only the drive reaches Init on an initialised manager, Write into an error stream, Shutdown on a manager that is not initialised, and Read's refusals.

What runs without any switch from L1 (from the coverage runs): the identity, GetNetworkLot, GetZoneTypeForNetwork, GetWealthForNetwork, ConfigurationFits, the maps and the iterator helpers (every load and every cell change), the lot's Release and destructor when a lot is removed. Only the test reaches GetNetworkLotConfigurationSet, GetNetworkLotConfigurationById, IterateAllNetworkLots, the lot's QueryInterface and its object count; only the drive reaches SetLotConfigurationForNextPiece on the manager.

What runs without any switch from L3 (from the coverage runs): DoMessage, ProcessPending, UpdateCell, UpdateLot and CreateLot with their draws whenever cells change beside a network (growth in a plain week is enough), BuildLotObjects, ObjectPosition and CreateProp when a lot is made, RemoveLot when a piece goes or a lot is chosen again. `OPENSC4_TEST_BULLDOZE` (pieces and buildings removed: RemoveLot, lots chosen again), `OPENSC4_TEST_ZONE_TOOL` and `OPENSC4_TEST_PLOP_TOOL` with their commits and the tutorial off (zones and lots beside roads change: the cells-changed and lot-state messages), and `OPENSC4_TEST_TERRAIN_EDIT` (the slope under a cell changes what fits) drive the same functions harder. Only the sub-switches reach Pothole, RedevelopLot and UpdateConnectedPieces on a city without the grouped network.

Unreached by any test, L3: a forced configuration in CreateLot (it needs SetLotConfigurationForNextPiece and then a new piece: the lot editor's path); the growing insert in CreateLot's candidate vector (the vector is reserved for all candidates first); a pothole on a cell whose piece is rail alone, on a city without rail; the usable-flag message with a real piece arrives only when a construction crew finishes a piece (`OPENSC4_TEST_QUIET=network` builds roads); UpdateConnectedPieces' far-edge neighbours.

Unreached by any test, L2: Init's missing-configuration case (it needs a save whose lot has an ID no loaded exemplar has: none in the corpus); LoadLotConfiguration's rewrite of an exemplar below version 2 (WriteLotConfiguration was hit in none of the 34 coverage runs, so no exemplar the corpus loads is that old), a configuration whose exemplar has no piece ID, the growing insert in AddConfiguration beyond what the shipped pieces need; Shutdown and Write with a pothole (no save has one); the configuration's constructor from the lot editor.

Unreached by any test, L1: the lot's object array for a lot without a configuration (the static empty vector; no such lot exists outside a load's Read to Init window), the configuration's GetName (the lot editor's), the node insert of the lot map for the pothole map (no save has a pothole), a manager Release that destroys (the quit).

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

Batch t1 (2026-10-13), parts L1 and L2: the original against every network occupant group, all of the network manager's groups and the eight groups of this class for a week. Before the first run: `ret N` of the 14 new functions against our signatures (no mismatch; Write's single exit read by hand, `ret 4`), the caller register scan and the EAX scan (no hit), and an independent reading of the 14 functions against the disassembly, the missing-configuration format string compared with the exe's (no difference in behaviour). A trial without `simulators.netlot.slots` and with `own` was the same on `saves-networks` too.

| Leg | Saves | Result |
|---|---|---|
| plain | the 12 saves | 12 of 12 the same on the first pair |
| plain | `saves-networks`, `saves-plopped`, `saves-ports` | 3 of 3 each, first pair |
| `OPENSC4_TEST_NETLOT=3` (with the network manager's and the underground pieces' tests) | the 12 saves | 12 of 12, first pair |
| the same | `saves-networks` | 3 of 3 |
| `OPENSC4_TEST_PLOP_TOOL=3` with its commit, tutorial off | the 12 saves | 12 of 12, first pair |
| the same | `saves-networks` | 3 of 3 |
| `OPENSC4_TEST_BULLDOZE=3` | the 12 saves | 11 of 12; Making Money d0365 partly (below) |
| the same | `saves-networks` | 2 of 3; t0000 partly (below) |

The class's save record was identical in all 66 pairs, and every city loaded its 543 configurations to the same `load` checksum on both sides (`load 543,3F683A74,36155B20`).

The two partly saves have the same fingerprints in every run of both sides and differ in a saved record of another class. t0000 after the bulldoze: one run of ours wrote the removed piece at 632,1576 that the original writes in about a quarter of its runs ([netmgr.md](netmgr.md), Parity, batch q1). Making Money d0365 after the bulldoze: in two of our four runs the prop developer's list has the prop types 0x2977AA47 and 0x2977AA49 in swapped order; ten more pairs of that save had the original's order on both sides. The same swap is on record in runs of the original after tool commits ([tools.md](tools.md), batch t1b1), so it is taken as the original's variation; here it was ours in 2 of 14 runs and the original's in none.

Ran with ours (first-call log lines over the batch): 42 of the 43 functions of the two parts; the configuration's GetName did not. Init's missing-configuration path, the rewrite of an old-version exemplar and the potholes in Write and Shutdown ran in no batch (no save has such a lot, exemplar or pothole).

Batch u1 (2026-10-14), parts L1 to L3: the original against every network occupant group, all of the network manager's groups and the twelve groups of this class for a week. Before the first run: `ret N` of the 12 new functions against our signatures (no mismatch), the caller register scan and the EAX scan (no hit), and an independent reading of the 12 functions against the disassembly: the three draws from the static generator at the game's three places under the game's conditions, the patched tables read from the game's memory with the game's index arithmetic, the jump table and float constants read from the exe (no difference in behaviour). The generator's state was the same on both sides before and after every leg that draws (it is reseeded under `determinism.fixed_seed`).

| Leg | Saves | Result |
|---|---|---|
| plain | the 12 saves | 12 of 12 the same on the first pair |
| plain | `saves-networks`, `saves-plopped`, `saves-ports` | 3 of 3 each, first pair |
| `OPENSC4_TEST_NETLOT=3` (with the network manager's and the underground pieces' tests) | the 12 saves; `saves-networks` | 12 of 12; 3 of 3 |
| `OPENSC4_TEST_BULLDOZE=3` | the 12 saves; `saves-networks` | 12 of 12; 3 of 3 |
| `OPENSC4_TEST_PLOP_TOOL=3` with its commit, tutorial off | the 12 saves; `saves-networks` | 11 of 12 (Big City d0365, below); 3 of 3 |
| with `OPENSC4_TEST_NETLOT_POTHOLES=1` | the 12 saves; `saves-networks` | 12 of 12; 3 of 3 |
| with `OPENSC4_TEST_NETLOT_REDEVELOP=1` | the 12 saves; `saves-networks` | 12 of 12; 3 of 3 |
| with `OPENSC4_TEST_NETLOT_CELLS=1` | the 12 saves; `saves-networks` | 12 of 12; 3 of 3 |
| with `OPENSC4_TEST_NETLOT_STYLES=1` | the 12 saves; `saves-networks` | 11 of 12 (Rush Hour d1095, below); 3 of 3 |
| `OPENSC4_TEST_ZONE_TOOL=3` with its commit, tutorial off | the 12 saves; `saves-networks` | 12 of 12; 2 of 3 (n0090, below) |

The rebuilt lots made the same props on both sides (Big City n0000: 36 lots, 52 to 62 props depending on the save's day), the potholes were added and taken away again (four cells, one of them rail), and the restyle flood ran three times on each city that has grouped pieces (43 such cells on the networks saves; the 12 saves have none).

The three partly saves:

- Big City d0365 with the lot tool and Rush Hour d1095 with the styles test: the first run of ours differed from the first day, before any test: the MySim part's checksum once and the advisor generator's once; the three pairs after it were the same. The open item of [parity.md](parity.md).
- `saves-networks` Big City n0090 after the zone tool: the fingerprints part on the last day in the terrain's flags only: bit 0x200 is set on a run of vertices along the city's edge row (z = 0) up to a point that differs from run to run. Twelve more pairs of that save: without this class's groups the original and ours gave one value 6 of 6; with them the original gave it 5 of 6 and a second one once (the bit on 23 vertices of that row), ours gave it 3 of 6 and a third one three times (the bit on the same vertices and two more). The same row, a different length: something that advances in real time after the zone tool's commit, likely the construction crew, whose progress runs on a real-time stopwatch until that is put on simulation time ("Decisions taken" above). Taken as the original's variation, with the numbers above.

Ran with ours (first-call log lines over the batch): 54 of the 55; the configuration's GetName did not (only the lot editor asks for a name).

Counting: the three classes have 62 functions in their range (the survey's count); 54 of them are ours, the 6 adjustor thunks and the 2 functions named above stay the game's; the 55th of ours is the configuration's GetID, a getter shared with other classes outside the range, replaced in this class's vtable only.
