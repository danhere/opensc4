# The prop manager (cSC4PropManager)

Two jobs. It is the catalogue of prop types and prop families, read from the game's exemplars when the first city is made. And it is the service that makes props and puts them into the city, or takes them out again: city props, lot props, street props and the props of a building. The prop developer decides where props go; the manager does the work once the place is chosen. Not to be confused with `cSC4PropDeveloper` ([prop.md](prop.md)). Reimplemented like the other classes: one writer with every virtual call's argument count checked against the callee's own code, then the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/propmgr.toml).

## The class

Class ID 0xA990BD10. Object size 0x28, made by the factory 0x004A28E0 (registered at 0x00460090). Code 0x004A1D20 to 0x004A44E0. One per city. The global 0x00B43D10 points to it.

## Pieces

Two groups, both installed at PostAppInit, both off by default. Together they hook 59 functions: 39 in part 1 (counted from the table in `InstallPropMgr1`) and 20 in part 2.

| Piece | Code | Group |
| --- | --- | --- |
| Identity and lifetime: the class ID, QueryInterface, the factory, Release, the deleting destructor, Shutdown, Write, Read, the clean-up 0x004A2A30, the types-loaded message (10 functions) | src/core/propmgr_1.cpp | `simulators.propmgr.part1` |
| The city, lot, street and building lists that need no exemplar: the three city and street queries, the five add calls, the lot and building queries by cell, SetUsage, the removals of one prop and of a list of props, RemoveBuildingProp (17) | src/core/propmgr_1.cpp | part1 |
| The prop filter's IsIncluded (1) | src/core/propmgr_1.cpp | part1 |
| Making a prop from a key and giving a prop its exemplar: CreateGenericProp, SetPropExemplar (2) | src/core/propmgr_1.cpp | part1 |
| The tables: the record insert, the lists of types and families, the key, validity, family and profile queries (9) | src/core/propmgr_1.cpp | part1 |
| CreateProp, the street, lot and building queries that filter or read a list, the six removals by area, lot, street and building, AddBuildingProp, the building prop list's Release, GetPropProfile, GetPropName, DoMessage (15) | src/core/propmgr_2.cpp | `simulators.propmgr.part2` |
| The exemplar loader: LoadPropExemplar, AddPropExemplar, the callback, LoadAllPropExemplars, and Init (5) | src/core/propmgr_2.cpp | part2 |
| The address entries | data/addresses/propmgr.toml | |
| The test | src/core/propmgr_autotest.cpp | `OPENSC4_TEST_PROPMGR` |

Part 1 covers 0x004A1D20 to 0x004A3040 and part 2 0x004A3040 to 0x004A44E0. Part 2 calls part 1's functions by address, as the original does. Each part is installed by looking up every entry and hooking it. If one entry or one hook fails, the part is not installed.

## The object

Three parts, each with its own vtable:

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00A8F928 | `cISC4PropManager` | 41 known, 0 to 40 (the next section; the source comments and the address file say 44) |
| +0x4 | 0x00A8F918 | `cIGZMessageTarget2` | 4: QueryInterface thunk 0x004A21F0, AddRef 0x0068D230, Release thunk 0x004A2710, DoMessage 0x004A34E0 (slot 3, called with this + 4) |
| +0x8 | 0x00A8F900 | `cIGZSerializable` | 6: thunk 0x004A2200, AddRef 0x0068D250, thunk 0x004A2720, Write 0x004A2430 (3), Read 0x004A2470 (4), GetGZCLSID 0x004A1D20 (5); Write and Read get this + 8 |

QueryInterface (0x004A1D30): 0x28326F3E gives the object, 0x00000001 and 0x452294AA the message target (+4), 0xE4FDA3D4 the serializable (+8). The answer is referenced. Any other ID is false and leaves the out pointer alone. The thunks jump to QueryInterface and Release with the object's own address.

| Offset | Field |
| --- | --- |
| +0x0C | initialised (byte; Init sets it, Shutdown clears it first) |
| +0x10 | reference count (starts at 0) |
| +0x14 | the occupant manager (global 0x00B43D0C, referenced; the address entry for it is still called SC4::NetworkService, an old mistake) |
| +0x18 | the resource manager (global 0x00B43CA8, referenced) |
| +0x1C | the prop filter (referenced; made by Init) |
| +0x20 | the lot manager (global 0x00B43D08, referenced) |
| +0x24 | an object of the service 0x00B43DC4 (referenced; message 0x06CDB65B replaces it) |

Release at a count of 1 or less runs Shutdown (slot 4) and then the deleting destructor with 1. Shutdown lets go of the references in the order +0x24, +0x20, +0x1C, +0x18, +0x14, takes the message target out of the four messages below and takes the command 0x0BEAF1A2 off the command service. The factory sets the base class's two vtables first and then its own, and clears the flag, the count and the five pointers.

The object is saved with a version word (1) and two references, the occupant manager and the lot manager. Read wants the version to be 1 and then fills the two fields (part offsets +0xC and +0x18, which are object +0x14 and +0x20). The tables are never saved: they are rebuilt from the exemplars.

### The prop filter

A counted object of 0x14 bytes, made by Init: the occupant filter base's constructor (0x005EA080 family), then its own vtables. The occupant manager calls its slot 3 (IsIncluded, 0x004A2210) for every occupant of a query.

| Offset | Field |
| --- | --- |
| +0x00 | vtable 0x00A8F8E4 (7 slots) |
| +0x04 | vtable 0x00A8F8CC (6 slots, thunks and shared code) |
| +0x08 | reference count |
| +0x0C | mode |
| +0x10 | the building (mode 3 only) |

IsIncluded is false for anything but an occupant of type 0xA823821E (a prop). Then by mode:

| Mode | What passes |
| --- | --- |
| 0 | every prop |
| 1 | props whose usage is city |
| 2 | usage lot |
| 3 | usage building; with a building set at +0x10, only a prop that is in that building's prop list (found by pointer) |
| 4 | usage street |

The usage numbers are the prop's own (`GetPropUsage`, slot 0x58). A mode of 0 passes without asking the prop for its interface. The queries set the mode before they ask the occupant manager and leave it set. The next query sets it again. Only the test puts it back.

### The four tables

All are hash tables of the library kind the game uses: +4 the bucket array's begin, +8 its end, +0x10 the element count. A node is the next pointer, then the key at +4, then the value at +8. The bucket is the key modulo the bucket count; a new node goes first in its bucket. The keys are instance IDs (numbers, not pointers).

| Global | Holds | Value |
| --- | --- | --- |
| 0x00B21760 | the prop types | the resource key, 3 dwords (type, group, instance); its count at 0x00B21770 goes into the types-loaded message |
| 0x00B21774 | the prop families | a vector of the types in the family (begin +8, end +0xC, capacity +0x10) |
| 0x00B2179C | the prop profiles | the profile, 28 bytes |
| 0x00B21788 | the prop names | a game string (read by GetPropName; nothing in this class's code writes it) |

Every prop type is its own family as far as the queries go: IsPropFamilyTypeValid and IsPropInFamily accept a type in place of a family.

The profile is seven dwords:

| Dword | Content | Property |
| --- | --- | --- |
| 0 | occupant class (0x2977AA47 plain prop, 0x2977AA48 terrain decal, 0x2977AA49 visual effect) | 0xA977A86B, default 0x2977AA47 |
| 1 to 3 | the size, three floats kept as raw dwords; zero when the property is absent | 0x27812810 |
| 4 | requester satisfaction | 0x49A1E05A |
| 5 | wealth (one byte) | 0xE9A316EB |
| 6 | crime incident ID | 0x8AA864D5 |

The class decides what CreateProp builds.

### A building's props

A building keeps its props as an attachment on its property holder, under the property 0xA8322B7F: a counted object of 0x14 bytes (vtable 0x00A8F9CC; QueryInterface 0x00811CB0, AddRef 0x004F7BA0, Release 0x004A3CE0). The vector of the props is at +4 (begin), +8 (end) and +0xC (capacity), and the count at +0x10. A building with no props has no attachment. AddBuildingProp makes the attachment when it is missing and the removals take it off again.

## The interface

The slots of vtable 0x00A8F928, with the call's arguments (the thiscall `ret N` of each is checked). The gzcom-dll header lists overloaded pairs in the other order, so the table names each slot by what it does. The source comments put it this way: in a pair, the slot that takes cell numbers (x, z) comes before the one that takes the lot or the building (GetLotPropsAt 23 and GetLotPropsOfLot 24; RemoveLotPropsAt 27 and RemoveLotPropsOfLot 28; GetBuildingPropsAt 34 and GetBuildingPropsOf 35; RemoveBuildingPropsAt 37 and RemoveBuildingPropsOf 38). The two city pairs differ in the rectangle: the lower slot takes metres, the higher cells (17 and 18, 21 and 22). In the street pair the rectangle comes first (30 and 31).

| Slot | Function | Address | Arguments | What it does |
| --- | --- | --- | --- | --- |
| 0 | QueryInterface | 0x004A1D30 | iid, out | see above |
| 1 | AddRef | | | the shared AddRef |
| 2 | Release | 0x004A2340 | | counts down; at 1 shuts down and deletes |
| 3 | Init | 0x004A4380 | | see "Loading" |
| 4 | Shutdown | 0x004A2370 | | gives the references back; true |
| 5 | GetAllPropTypes | 0x004A2BF0 | vector | empties the vector, then appends every key of the type table |
| 6 | GetAllPropFamilyTypes | 0x004A2CB0 | vector | the same for the family table |
| 7 | GetPropKeyFromType | 0x004A2D70 | type, key out (3 dwords) | the resource key; false for an unknown type |
| 8 | IsPropTypeValid | 0x004A2DD0 | type | in the type table |
| 9 | GetPropTypesByFamily | 0x004A2E90 | vector, family | copies the family's types into the vector and returns their number. For a type that is no family, the vector holds that one type and the result is 0. For neither, 0 and the vector is left alone |
| 10 | IsPropInFamily | 0x004A2F80 | type, family | the same ID, or the family's vector has the type |
| 11 | IsPropFamilyTypeValid | 0x004A2E10 | id | in the family table or in the type table |
| 12 | GetPropName | 0x004A4360 | type | the name table's operator[] (a type that is missing gets an empty record) |
| 13 | GetPropProfile | 0x004A3750 | type | the profile table's operator[] (a missing type gets a zeroed record) |
| 14 | GetPropProfilePtr | 0x004A2FF0 | type | the profile in the type's node, or null |
| 15 | CreateProp | 0x004A3040 | type, prop out | see below |
| 16 | CreateGenericProp | 0x004A24D0 | resource key, prop out | makes a prop straight from an exemplar |
| 17 | GetCityPropsInArea | 0x004A1DF0 | list, 4 floats (metres: x0, z0, x1, z1) | city props (filter mode 1) through the occupant manager's query by box (slot 0x90), the box spanning the whole height |
| 18 | GetCityPropsInRectangle | 0x004A1DB0 | list, 4 ints (city cells) | the same through the query by city cells (slot 0xA0), no limit |
| 19 | AddCityProp | 0x004A1F30 | occupant | an occupant with flag 1 already counts as added. Otherwise its Init and its insertion in the occupant manager must work; then it gets usage 1 |
| 20 | AddCityPropNew | 0x004A1E70 | type, position, orientation | makes a prop (slot 15), places it, initialises it, inserts it, gives it usage 1 |
| 21 | RemoveCityPropsInRectangle | 0x004A38F0 | 4 floats | slot 17 into a list, then the removal of the list (0x004A28A0) |
| 22 | RemoveCityPropsInArea | 0x004A3850 | 4 ints | slot 18 into a list, then the removal |
| 23 | GetLotPropsAt | 0x004A1FD0 | list, x, z | the lot at the cell (lot manager slot 6), then slot 24; 0 without either |
| 24 | GetLotPropsOfLot | 0x004A3240 | list, lot | the lot's cell rectangle (lot slot 14) to the query by city cells with mode 2; props outside the rectangle in metres (the far corner one cell on) are released and dropped, a NaN position too. False for a null lot |
| 25 | AddLotProp | 0x004A20C0 | lot, occupant | as slot 19 with usage 2; the lot is not used |
| 26 | AddLotPropNew | 0x004A2670 | type, position, orientation | 0x004A2010 with usage 2; returns the occupant or null |
| 27 | RemoveLotPropsAt | 0x004A3AF0 | x, z | slot 23 into a list, then the removal |
| 28 | RemoveLotPropsOfLot | 0x004A3A50 | lot | slot 24 into a list, then the removal |
| 29 | RemoveProp | 0x004A27B0 | occupant | 0x004A2690, then true |
| 30 | GetStreetPropsInRectangle | 0x004A1F90 | list, 4 ints | as slot 18 with mode 4 |
| 31 | GetStreetProps | 0x004A3150 | list, x, z | mode 4 query on the one cell; props whose own cell (city slot 0x17C) differs are released and dropped. Answers false whatever happens |
| 32 | AddStreetPropNew | 0x004A2120 | type, position, orientation | 0x004A2010 with usage 4 |
| 33 | RemoveStreetPropsFromTile | 0x004A3990 | x, z | slot 31 into a list; each prop taken out by 0x004A2690 and released. No result |
| 34 | GetBuildingPropsAt | 0x004A2140 | list, x, z | the first building (type 0x278128A0) the occupant manager has at the cell (slot 0x84), then slot 35; false without one |
| 35 | GetBuildingPropsOf | 0x004A3770 | list, building | the building's props appended to the list, each referenced. False without a holder or a prop list |
| 36 | AddBuildingProp | 0x004A3B90 | building, prop | see below |
| 37 | RemoveBuildingPropsAt | 0x004A21A0 | x, z | the building at the cell, then slot 38 |
| 38 | RemoveBuildingPropsOf | 0x004A3390 | building | each prop in the occupant manager (flag 1) is removed from it and each is shut down; the vector is emptied (not freed) and the attachment removed |
| 39 | RemoveBuildingProp | 0x004A27C0 | building, prop | takes one prop out of the list (out of the occupant manager when flag 1, shut down); the attachment goes when the list is empty. True when the prop was found |
| 40 | deleting destructor | 0x004A2310 | flags | the three vtables back to the class's own; the memory freed when bit 0 is set; returns the object |

The queries that fill a list refer to the game's list type: a head node (next, previous), nodes with the value at +8. The list holds one reference to each occupant. The caller lets them go and destroys the list. The removals make the list, fill it, remove the props with 0x004A28A0 (each removed from the city and released) and destroy it.

Other functions of the class, not slots:

| Function | Address | What it does |
| --- | --- | --- |
| SetUsage | 0x004A1F00 | (occupant, usage), manager not used: asks the occupant for its prop interface (0xE9793A65) and sets the usage |
| AddPropWithUsage | 0x004A2010 | (type, position, orientation, usage): makes, places, initialises and inserts a prop, gives it the usage; returns its occupant or null |
| RemoveProp | 0x004A2690 | a prop with flag 2 has its placeable object cleared and let go; a prop with flag 1 is removed from the occupant manager; then it is shut down |
| RemoveProps | 0x004A28A0 | RemoveProp then Release for every occupant of a list |
| SetPropExemplar | 0x004A2A70 | (prop, type or 0): for a type in the table (the given one or the prop's own) gives the exemplar at its resource key to the exemplar part of the prop's holder (interface 0x0AC2B5F7), if it has one, and sets the prop's size. True |
| InsertRecord | 0x004A2930 | puts a 16-byte key into a hash table of the type table's kind; called by the game's map operator 0x004A3470 |
| CleanUp | 0x004A2A30 | see "Loading" |
| PostTypesLoaded | 0x004A2730 | posts message 0x896FDC70 through the message server, the manager as both objects, the number of types as the value |

CreateProp: false for an invalid type. The class comes from the type's profile (0x2977AA47 when it is 0). The class factory is asked first (the framework's slot 3, interface 0xE9793A65). If it says no, the prop is built by hand: the terrain decal (0xA0 bytes, pool 0x005ED6E0, constructor 0x005EE360), the visual effect (0x94 bytes, 0x005ED9A0, 0x005F0210), or the plain prop (0x68 bytes, 0x005ECE60, 0x005EE050); the decal and the effect are used from their object + 4. The prop is referenced into the out pointer, gets its type (prop slot 8) and then SetPropExemplar. If that fails, the prop is released and the out pointer is left pointing at it. CreateGenericProp works the same from the exemplar's own class property, with no type; it gives the prop the exemplar and its size.

AddBuildingProp: a prop that is not yet flagged as in the occupant manager is initialised, inserted and set to usage 3 (SetUsage). Then the building's list is found, or made (0x14 bytes, vtable 0x00A8F9CC) and attached, and the prop appended unless it is there. Last, the prop's usage is set to 3 again through its prop interface. A building with no property holder counts as success (true) with nothing done. False without an occupant manager, or when the prop's Init or its insertion fails.

### Messages

The target is asked for four messages (Init does it with the message server's slot 0x14, Shutdown undoes it with slot 0x18). DoMessage is always true.

| Message | What it does |
| --- | --- |
| 0xA6B79602 | the message's two getters are called and nothing else |
| 0x06CDB65B | its data is an object of the service 0x00B43DC4: it replaces the one at +0x24 (the new one referenced, the old one released; the same object changes nothing) |
| 0x26D31EC1 | nothing (the city has been set up) |
| 0x0BEAF1A2 | the TextDecal command (the name is the string at 0x00A8F9D8, registered by Init with the command service). With more than two arguments it makes a text decal: text, x and z, then optionally a scale (1 when missing) and four colour numbers (1 each when missing; they are read only when there are more than seven arguments). The decal is made (0xBC bytes, pool 0x005F7D80, constructor 0x005F8300), added to the city with slot 19, put at x, the terrain height and z, and given the text, colour and scale |

## Loading

The exemplars are read once per process, not once per city. Init (slot 3) is the entry. It does nothing the second time for the same manager (flag +0xC). For the first manager of the process the flag 0x00B21758 is still clear: Init sets it, registers the clean-up 0x004A2A30 with the application (the service at 0x00B43C94, slot 0x114) and loads every prop exemplar. A later city in the same process finds the flag set and the tables as the first city left them. The clean-up, at the application's shutdown, takes itself off the service (slot 0x118), clears the flag and empties the type table.

Init runs when the city starts, after every replacement is installed. So with the groups on, ours fills the tables in a run. Every manager then registers the TextDecal command, asks the message server for the four messages and takes its references: the occupant manager, the resource manager, a new prop filter, the lot manager and, when the service 0x00B43DC4 gives one, its object.

The loading, in order:

1. LoadAllPropExemplars (0x004A4310) asks the resource manager (global 0x00B43CA8, slot 0x48) for the list of its exemplar resources (type 0x6534284A) and walks it with the callback 0x004A42A0. The walk is in the resource manager's own order.
2. The callback loads the exemplar by key (resource manager slot 0xC, interface 0xA52160F5). It keeps the exemplar when its property 0x10 (exemplar type) is 0x1E, a prop.
3. AddPropExemplar (0x004A4210) copies the key into the type table, under the key's instance (its third dword), through the game's map operator 0x004A3470.
4. LoadPropExemplar (0x004A3DB0) builds the profile (see the object section) and stores it in the profile table. A type that is already there has its record overwritten. Each family listed in property 0x27812870 gets the type appended to its vector, unless the type is already in it; a family that is new gets an empty vector first. A family's vector holds its types in the order the exemplars came. The name (property 0x20) is read into a string and thrown away.
5. PostTypesLoaded tells the world, with the number of types, that the tables are ready.

LoadPropExemplar has a third argument, whether the type is new. The only caller always passes 1. With 0 it would first take the type out of the profile table and out of every family's vector. The code is written and never runs.

## Left the game's

- The adjustor thunks 0x004A21F0, 0x004A2200 (jump to QueryInterface) and 0x004A2710, 0x004A2720 (jump to Release).
- The shared AddRefs 0x0068D230 and 0x0068D250.
- The table code that other classes use with their own tables: the type table's operator[] (0x004A3470, a record of 12 bytes; the others are 0x00698F90, 0x00699000, 0x0069D0E6), the family table's insert (0x004A3D10), the profile table's operator[] (0x0065FAD0), the name table's operator[] (0x004862F0), and 0x004A29B0 (the insert of a 0x24-byte record into a table of the 0x004A2930 kind, with a caller in another class, 0x0065FB41).
- The vector code shared with the occupant manager: push_back 0x0049FC30 and the destructor 0x0049FB10.
- The text decal's allocator 0x005F7D80 and constructor 0x005F8300, which only the TextDecal command uses. The terrain decal's (0x005ED6E0, 0x005EE360) and the visual effect's (0x005ED9A0, 0x005F0210) are ours in `simulators.propkinds.decal_object` and `.effect_object` ([propkinds.md](propkinds.md)); the plain prop's (0x005ECE60, 0x005EE050) are in `simulators.propocc.object`. The automata controller prop's and the burnt building's are in `simulators.propkinds.automata_object` and `.burnt_object`; the prop manager makes neither (no type in the tables of the test cities has their class). The other derived prop classes are another round.

## Oddities of the original, kept

- QueryInterface answers 0x00000001 (the base interface) with the message target part, not with the object.
- GetPropTypesByFamily returns 0 when the ID is a type (the vector then holds that one type).
- The prop filter's IsIncluded finds a building's prop list by pointer.
- AddLotProp ignores its lot argument.
- CreateGenericProp does not test the resource manager's answer. It tests the exemplar it left. The game leaves that local unset; ours starts it at null.
- The hash lookups divide by the bucket count with no check, so a table with no buckets is a divide error, as in the game.
- PostTypesLoaded crashes when the message cannot be allocated.
- GetStreetProps (slot 31) always answers false, even when it leaves a list. It only ever returns from its last line. The one caller that matters, RemoveStreetPropsFromTile, ignores the answer.
- RemoveStreetPropsFromTile returns nothing; the original leaves EAX as it is.
- AddBuildingProp does not release the building's list it got when the building already had one (the lookup took a reference). A new list is released after the building keeps it.
- RemoveBuildingPropsOf empties the vector but never frees it.
- The text decal gets one reference too many: nothing releases it after the city's AddCityProp took its own.
- The loader's name string (property 0x20) is read and thrown away.
- CreateProp, when SetPropExemplar fails, releases the prop and leaves the out pointer pointing at it.
- AddPropExemplar answers false.
- Read does not let go of the references the fields held.

### Two deliberate differences

- The size. The original copies as many dwords of property 0x27812810 into the profile's three-dword local as the property reports. A property with more than three would overrun the stack. Part 2 copies at most three.
- The wealth. The profile's wealth dword is one byte read from property 0xE9A316EB. In the original the three high bytes are what the stack held. Ours stores zero. Nothing reads them, and the test masks them (`& 0xFF`) on both sides.

## How it is tested

Existing switches that reach the class: the prop developer's tests ([prop.md](prop.md)), the bulldoze, zoning and dezoning switches ([demolition.md](demolition.md)). `OPENSC4_TEST_PROPMGR=<day>` (src/core/propmgr_autotest.cpp) calls the game's functions through the manager's vtable on that day (and SetUsage by address) and writes sub-hashes on `autotest: propmgr:` lines. Nothing in it calls our functions directly, so a run with the groups on and one with them off give comparable hashes. The fingerprint gets the token ` propmgr` with one hash of the sections.

The test has three levels:

| Switch | What runs |
| --- | --- |
| `OPENSC4_TEST_PROPMGR=<day>` | the read-only tier, six sections |
| plus `OPENSC4_TEST_PROPMGR_EDIT=1` | also the round trips for made props, the city and a lot (the city is left as found) |
| plus `OPENSC4_TEST_PROPMGR_EDIT=2` | also the round trips for a building |

Two debugging switches: `OPENSC4_TEST_PROPMGR_EDITPARTS=<mask>` picks which round trips run (bit 0 made props, bit 1 city, bit 2 lot, bit 3 building; default 7, with EDIT=2 15; bits 4 to 6 pick which of the three building rounds, none set means all), for finding which one disturbs something. `OPENSC4_TEST_PROPMGR_VERBOSE=1` writes a line per lot and per lot prop.

### The read-only tier

The test takes a sample of the city first: up to 200 lots, up to 800 buildings (the first of type 0x278128A0 the occupant manager has on each cell, walked cell by cell), up to 300 street cells (every cell whose x and z are multiples of 8), and three rectangles in cells: the middle half of the city, a strip of the four westmost columns, an 8 by 8 square in the middle.

| Section | What it does |
| --- | --- |
| object | QueryInterface with six IDs (the four it takes, the prop's ID and an unknown one): whether it answered, whether the out pointer is set, the part that came back as an offset, the change of the count before and after the release of the answer. AddRef and Release as changes of the count. The documented fields: the flag, the three global pointers, the five pointers non-null, the filter's mode as found |
| types | GetAllPropTypes (sorted, and the hash of the order the game returns them); called three times into one vector to show it is emptied each time. Then every type, or a fixed stride of at most 2,500: IsPropTypeValid, GetPropKeyFromType, GetPropProfile and GetPropProfilePtr (the two must agree; the wealth dword masked to its low byte), and for about 40 of them the name record (a game string's text and length, or the record's first dword when it is not a string). Six IDs that are not types (0, 1, 0xDEADBEEF, 0x12345678, 0xFFFFFFFF, 0x2977AA47) are asked IsPropTypeValid, IsPropFamilyTypeValid and GetPropKeyFromType |
| families | GetAllPropFamilyTypes (sorted and in order), then a stride of at most 400 families: IsPropFamilyTypeValid, GetPropTypesByFamily (the count it returns, the members in the order returned and sorted), IsPropInFamily for the first three members, the last, the family itself, an unknown ID and three types from the table that may not belong; then three types that are no family and one unknown ID |
| city | the three rectangles through GetCityPropsInRectangle (cells), GetCityPropsInArea (metres) and GetStreetPropsInRectangle, and GetStreetProps for the sampled street cells |
| lots | GetLotPropsOfLot and GetLotPropsAt (the lot's low corner) for every sampled lot, and GetLotPropsOfLot with a null lot |
| buildings | GetBuildingPropsOf and GetBuildingPropsAt (the cell the building was found on) for every sampled building |

Each query goes into a fresh list. Each occupant of the list is read (occupant type, prop type, usage, x and z), released, and the list destroyed as the game's callers do. A query's hash has the answer (the low byte), the count, the hash of the props sorted, the hash of the props in the order of the list, and the filter's mode after the call. The order of the list is hashed because it is the same in two runs of the original.

Hashed and logged:

- Hashed: everything above. Pointers are left out of every hash; every bool is masked to its low byte.
- Only logged: the first four name records (raw words), the number of props of prop type 0 that the lists held (see below), the number of calls, the per-section sub-hashes, and with VERBOSE the lot and prop lines.

Left out of the hashes on purpose:

- Prop heights. The height is not in the order or in the hash.
- Props of prop type 0 (none of the table's). They move from run to run in the original, x and z too: a lot's count changes as they cross its edge. In trial runs the original differed from itself on this in seven runs each on the MySim and the fire saves. They are counted for the log and skipped.

### The round trips

Before and after, the test counts the props of every usage (0 to 4) over the whole city and the props of the city kind (mode 1) and the street kind (mode 4) in each of the three rectangles. It counts through the occupant manager's query by city cells with the prop filter, and puts the filter's mode back. The two counts must be equal (the log says `restored yes` or `NO`; the answer is hashed).

| Part | What it does |
| --- | --- |
| made (bit 0) | up to three plain-class types, plus the first decal type and the first effect type: CreateProp, then the made prop's fields (the occupant, the occupant type, the class's vtable when it lies in the program's image, the prop type, the profile's class and size). SetUsage with 2 and 3, read back through the prop. The same type from its key through CreateGenericProp. Everything is released; nothing is inserted. An unknown type makes nothing |
| city (bit 1) | at an empty spot: the first cell, searched from a quarter of the city on, with no lot and no prop in its 3 by 3 neighbourhood. The first pass also wants the occupant manager's 64 m cell around it to be empty, because the flammability simulator recalculates where occupants come and go; the second pass takes the first cell without that. Then AddCityPropNew, read back by both city queries, RemoveCityPropsInRectangle (metres), and a read. Then CreateProp, AddCityProp, a read, RemoveCityPropsInArea (cells), and a read. So both add calls and both remove calls |
| lot (bit 2) | on the first sampled lot that has no props (both lot queries empty and answering true): AddLotPropNew, both lot queries, RemoveLotPropsAt, a read. Then CreateProp, AddLotProp, a read, RemoveLotPropsOfLot, a read |
| building (bit 3, EDIT=2 only) | on the first sampled building without props: CreateProp, AddBuildingProp, a read, then a removal, and a read, three times: RemoveBuildingProp; RemoveBuildingPropsOf; RemoveBuildingPropsAt (only if the building found on its cell is that building) |

A round trip with no spot, no lot without props or no building without props is skipped, logged, and the skip is hashed as a marker.

The props are placed at the middle of the cell (8 m from the corner) at the terrain height, with orientation 0. The types are the first plain-class types in the sorted table.

Why the building round trips are a separate level. In the original, the game's own AddBuildingProp and RemoveBuildingProp change the daily flammability hash of the city although the prop counts are restored (the sums of the grid are the same). With the test on, the daily fingerprint then is no longer that of a run without it. It was bisected per round trip, which is what EDITPARTS is for. So the building round trips stay behind EDIT=2, and the read-only tier and EDIT=1 leave the fingerprint alone.

What the test leaves out: Init, Shutdown, Release beyond what the test added, Write, Read, the deleting destructor, DoMessage, the exemplar loader functions and the clean-up. And the two getters that insert a record for an unknown type (GetPropName and GetPropProfile): they are only given types the table returned.

## What the test reaches

Of the 59 hooked functions, 34 ran by themselves in a city in trial runs (with the existing prop tests, a bulldoze, zoning and dezoning). With the test at level 1, 52 ran. At level 2, 57.

Two of the 59 were not reached at level 2: AddPropWithUsage (0x004A2010) and RemoveStreetPropsFromTile. AddPropWithUsage is the shared body behind the "new prop" add calls (slots 26 and 32); in the trial it showed no first call of its own although its callers ran, which is stated here as observed and not explained. RemoveStreetPropsFromTile is the street remover (slot 33); the test does not call it, and it runs in play. The test has no call for slots 29 (RemoveProp), 32 (AddStreetPropNew) and 33 (RemoveStreetPropsFromTile) either. The test also makes no call to Init, Shutdown, the destructor, Write, Read, DoMessage or the loader; those run only when the game itself reaches them.

## Trial results

2026-10-12, single runs of 7 days, not the parity harness's full retries. Not part of the parity results below.

Before the test existed: 72 runs, one save from each of seven corpora, with no test, with the existing prop tests, with a real bulldoze, and with zoning then dezoning on the three regular saves. Every run ended normally. Ours is equal to the original wherever the original is equal to itself. Where it is not, the cases are:

- The fire save from the first day in the `flam`, `prop` and `weather` parts.
- One number of the existing prop test's line on the last day of the plopped and ports saves: the original gave 125, 123 and 115 on one and 116, 124 and 126 on the other over six runs, and ours stays inside those sets.
- On the fire save, a flag that flips on day 2, 3 or 5 in the original's runs flipped on day 4 in one of ours. That is between the original's values, but with six samples it is not proven.

Acceptance matrix, final pass: seven saves, four switch sets (the test; the test with level 1; a real bulldoze on day 3 and then the test with level 1 on day 5; the test with level 2), the original twice and ours once per cell. 28 of 28 cells agree in the token and in every sub-hash. All 84 runs ended normally.

Unchanged-city check: the original with the test against the original with the test set to a day that never comes (leaving the switch out is not a fair comparison, because the environment's size shifts the heap layout and with it the outcome). The fingerprints are identical for the read-only tier on six of seven saves (the fire save differs from itself and was not repeated) and for level 1 on all seven.

## Parity

Batch `pm9` (2026-10-12): all groups pushed so far plus the two prop manager groups on side B (117 groups), 7 game days per save, verdicts by outcome sets as in [parity.md](parity.md).

| Leg | Saves | Test switches | Same on the first pair | Partly |
|---|---|---|---|---|
| plain | 12 | none | 10 | Big City d0000, d0365 |
| pm | 12 | `OPENSC4_TEST_PROPMGR=3 OPENSC4_TEST_PROPMGR_EDIT=1` | 9 | Big City d0000, Making Money d0090, Rush Hour d1095 |
| pmb | 12 | `OPENSC4_TEST_BULLDOZE=3 OPENSC4_TEST_PROPMGR=5 OPENSC4_TEST_PROPMGR_EDIT=1` | 9 | Big City d0000, Big City d0090, Rush Hour d0365 |
| plop | 3 (`saves-plopped`) | `OPENSC4_TEST_PROPMGR=3 OPENSC4_TEST_PROPMGR_EDIT=2` | 3 | none |

None of the 112 runs failed, crashed or stopped. The ` propmgr` token is equal on both sides in every run of all 27 saves of the three legs with the test, "restored yes" in every run, and the bulldoze cost is equal on both sides on all twelve saves of leg pmb. No fingerprint part describing props, lots, buildings or the occupant manager (`propocc`, `prop`, `occmgr`, `lot`, `bldgocc`) differs in any run. The parts that differ are `citylot`, `flam` and `weather`, all from the first simulated day, before any test runs.

What needed explaining: in 7 of the 56 B runs the `flam` part differed from every A run of that save in this batch and in batch dm5 (101 A runs), six of them the first pair's B run. None of dm5's 45 B runs (without the prop manager) did. A follow-up experiment (batches `fx1` and `fx2`: 30 one-pair legs each, no retries, 3 days, alternating Big City d0000 and Making Money d0090, B with and without the prop manager, same build) found the original deviating in 12 of 59 runs and ours in 12 of 60, to the same fixed values (Big City d0000: sum 9483/180, cells 31, where the usual is 8986/172, cells 29; Making Money d0090: 1167/20 where the usual is 1072/22). In 8 pairs both sides deviated together to the same value. Deviating runs loaded the city more slowly on average (median 11.1 s against 6.8 s), though not every one did. So these are alternative outcomes of the original on these saves, more frequent when the machine is busy while the city loads, and not a difference of the prop manager code (see "Flammability and weather from the first day" in [parity.md](parity.md)).

Not separable from these counts: with the prop manager in the B list ours deviated in 10 of 30 runs, without it in 2 of 30, while the original moved the same way between the two batches (8 of 29, then 4 of 30). A small extra exposure of our side (its exemplar loader runs at city load) cannot be excluded. It would lead to the same original outcome more often, not to a wrong result. Not repeated: the single deviating B runs on Big City d0365, Rush Hour d0365 and Rush Hour d1095.

The other differences: `citylot` on Big City d0000 and d0365 varies between the original's own runs (three values over twelve A runs on d0000). On Big City d0090 (leg pmb) one B run has identical fingerprints and differs only in the prop developer's record, where two references in its lists carry the neighbouring class IDs 0x2977AA47 and 0x2977AA49 the other way round; batch dm5 showed the same on Rush Hour d0365 without the prop manager.

## A note for maintainers

In `src/core/propmgr_2.cpp` the two constants that name the city-prop getters' slots are named the other way round from part 1 and the address file. Part 2 has `kGetCityPropsInRectangle = 0x44` and `kGetCityPropsInArea = 0x48`. Part 1, the address file and the test have slot 17 (0x44) as GetCityPropsInArea (metres) and slot 18 (0x48) as GetCityPropsInRectangle (cells). The behaviour is consistent: RemoveCityPropsInArea (slot 22, cells) calls 0x48, the cell getter, and RemoveCityPropsInRectangle (slot 21, metres) calls 0x44, the metre getter. Only the names are crossed. Do not rename one side without the other. The removal slots' names also run opposite to the getters' (RemoveCityPropsInArea takes cells, GetCityPropsInArea takes metres), a result of the header's order.
