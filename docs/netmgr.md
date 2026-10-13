# The network manager (cSC4NetworkManager)

The city's desk for the networks. It holds the thirteen network tools (one per network type: road, rail, highway, street, pipe, power line, avenue, subway, light rail, monorail, one-way road, dirt road, ground highway), the remap tables of the networks' ini resource, the list of the pieces that are managed globally, the queue of removals, the retrofit list and the construction crews. The tools, the lot code and the pieces ask it for lookups (a pipe at a cell, a subway station, the pieces of one intersection, a remapped texture or path ID); the demolition and lot code go through it to remove pieces and to make a lot's connection nodes. It has no grid and nothing sized from the city. The class has 70 functions (14.7 KB) and is reimplemented in three parts; this page has the first (state and queries, 22 functions) and what the survey found of the rest. Build 641 addresses throughout (data/addresses/netmgr.toml).

## The class

Class ID 0xC990BD46, IID 0xA9764C8C. Object size 0xBC. Constructor 0x00613330 (only reached by the jump from the factory 0x00613750, registered at 0x004603C5), destructor 0x00612000, deleting destructor 0x00612590. Code 0x00610530 to 0x006140FC, plus two small destructors at 0x006104D0 and 0x00610500. The city makes one per city unless the save brought one and keeps it at +0x200; the city's slot 38 and `SC4::GetNetworkManager` 0x00602680 give it.

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AA7AD0 | `cISC4NetworkManager` | 25, see the table below |
| +0x4 | 0x00AA7ABC | `cIGZMessageTarget2` | thunks 0x006120D0, 0x006C2270 (shared), 0x006120E0; DoMessage 0x00613950 |
| +0x8 | 0x00AA7AA4 | `cIGZSerializable` | thunks 0x006120F0, 0x006C2290 (shared), 0x00612100; Write 0x006111E0, Read 0x006124C0, GetGZCLSID 0x00610530 |

### The true slot order

gzcom-dll's `cISC4NetworkManager.h` is right up to slot 12 and one slot off from slot 13 on. Its `GetAllIntersectionPieces2` is not a virtual function in build 641: it is the helper 0x00612300, which takes exactly the header's five arguments and is called only by GetAllIntersectionPieces and by itself. So every name after it sits one slot too high in the header, and the last slot of the vtable, the deleting destructor, is not in the header at all. Calling slot 13 as the header's `GetAllIntersectionPieces2` reaches MapPreviewTexture with five arguments where it takes two. The labels the Ghidra project took from the header are shifted the same way for 0x00610AD0 to 0x00612590 (the function labelled MapShadowTexture, 0x00610B40, is MapPath; the one labelled AddToRetrofitList, 0x00612590, is the deleting destructor).

| Slot | Address | Function (arguments from its own `ret N`) | Header's slot |
| --- | --- | --- | --- |
| 0 | 0x00610750 | QueryInterface(iid, out) | 0 |
| 1 | 0x004664A0 | AddRef (code shared with other classes) | 1 |
| 2 | 0x006107D0 | Release | 2 |
| 3 | 0x00610C10 | Init | 3 |
| 4 | 0x00611EB0 | Shutdown | 4 |
| 5 | 0x006108D0 | GetNetworkTool(type, unique) | 5 |
| 6 | 0x00612110 | DestroyNetworkOccupants(list) | 6 |
| 7 | 0x00613450 | QueueRemovals(queue) | 7 |
| 8 | 0x006110B0 | RemoveOrReplaceOccupant(tool, occupant, out) | 8 |
| 9 | 0x00610540 | IsOccupantSubwayStation(occupant) | 9 |
| 10 | 0x00610550 | FindPipeOccupant(x, z, out) | 10 |
| 11 | 0x00610570 | FindPipeConnectionOccupant(x, z, out) | 11 |
| 12 | 0x00612780 | GetAllIntersectionPieces(piece, list, occupant manager, other) | 12 |
| 13 | 0x00610AD0 | MapPreviewTexture(id, rotation) | 14 |
| 14 | 0x00611D50 | MapShadowTexture(id, rotation, network flag, edge key) | 15 |
| 15 | 0x00610B40 | MapPath(id) | 16 |
| 16 | 0x006122B0 | RegisterConstructionCrew(crew) | 17 |
| 17 | 0x006112A0 | UnregisterConstructionCrew(crew) | 18 |
| 18 | 0x00610590 | GetWaterOccupantFilter | 19 |
| 19 | 0x006105C0 | GetSubwayOccupantFilter | 20 |
| 20 | 0x00611300 | CreateConnectionNode(out, type, point, a, b, c, flag) | 21 |
| 21 | 0x00611530 | AutoconnectLotNodes(lot, list) | 22 |
| 22 | 0x006107F0 | GetEnglishNetworkName(type) | 23 |
| 23 | 0x00612480 | AddToRetrofitList(piece) | 24 |
| 24 | 0x00612590 | the deleting destructor | none |

Two more things the header has wrong or open: FindPipeOccupant and FindPipeConnectionOccupant take a cell (x, then z) and the address of an out pointer, not a tool; and MapShadowTexture's third argument is a network flag (1 shifted by the network type), its fourth the edge key.

### Fields

`src/core/netmgr_sim.h` has the list with its evidence (the constructor 0x00613330 and the functions that use each field).

| Offset | What |
| --- | --- |
| +0x0C | reference count |
| +0x10 | initialised (Init sets it, Shutdown clears it) |
| +0x11 | a byte Init sets from a service; MapPath maps only when it is set (the ini section is "UK path remapping": left-hand traffic) |
| +0x12 | batching cell changes (DoMessage) |
| +0x14 | the dirty cell rectangle (min x, min z, max x, max z; empty is 0x7FFFFFFF twice and -1 twice) |
| +0x24 | vector of the globally managed pieces, referenced; this is the save record |
| +0x30 | the shadow edge table: a hash table (100 buckets at first) of 0x18-byte nodes: next, edge key, network type, base, mask, rotation byte |
| +0x44 | vector of 0x18-byte preview texture remap entries |
| +0x50 | vector of 0x18-byte shadow texture remap entries |
| +0x5C | vector of 0x10-byte path remap entries |
| +0x68, +0x74 | vector of occupants queued for removal (referenced) and the queueing byte |
| +0x78 | the retrofit list (a pointer to the head node; nodes of 12 bytes from the allocator cached at 0x00B45238), referenced |
| +0x7C | vector of construction crews, referenced |
| +0x88 | the 13 network tools by type |

A remap entry: a match word and a mask (an ID matches when ID and mask give the match word), a base and two more masks (the new ID is the base plus the ID's bits under the first less its bits under the second), a rotation byte (bit 7 mirrors) and a byte that says to go on with the next entries. A path entry: match, mask, base and one mask.

### The two filters

Slots 18 and 19 make occupant filters of 0xC bytes on the occupant filter base ([occfilters.md](occfilters.md)): the water filter (vtables 0x00AA7A54 and 0x00AA7A3C) and the subway filter (0x00AA7A88 and 0x00AA7A70). Which is which follows the header's order and the neighbourhood of their test functions: the water filter's (0x00648C40, 0x00648CC0) lie in the pipe tool's code, the subway filter's (0x00653C00, 0x00653CC0) in the subway tool's. The map window asks for them. Only their deleting destructors (0x006104D0, 0x00610500) lie in this class's code.

## What each function of the first part does

| Function | Address | Ours | What |
| --- | --- | --- | --- |
| QueryInterface | 0x00610750 | `identity` | IIDs 1 and 0xA9764C8C the object, 0x452294AA the part at +4, 0xE4FDA3D4 the part at +8, each with one reference |
| Release | 0x006107D0 | `identity` | above 1 the count goes down; else the deleting destructor (slot 24) with 1 |
| GetGZCLSID | 0x00610530 | `identity` | 0xC990BD46 |
| GetNetworkTool | 0x006108D0 | `queries` | null for a type of 13 or more. With `unique` 0 the manager's own tool of the type (no reference added). With `unique` set a NEW tool of the type as its constructor leaves it (reference count 0, not initialised): the general tool class (0x368 bytes, 0x0062C5F0, with the type) for ten types, the pipe tool (0x398 bytes, 0x00648EC0) for 4, the power line tool (0x3B8 bytes, 0x006503C0) for 5, the subway tool (0x398 bytes, 0x00653D60) for 7 |
| IsOccupantSubwayStation | 0x00610540 | `queries` | forwards to the subway tool's 0x00653B80: whether the occupant's byte array property 0xE90E25A1 holds a 6 |
| FindPipeOccupant, FindPipeConnectionOccupant | 0x00610550, 0x00610570 | `queries` | forward to 0x00648F70 and 0x00649080: of the occupants in the cell the last that answers IID 0x49C05B9F (0xEA0A5ACF for the second) is stored in the out pointer, referenced; the answer says whether there was one |
| GetAllIntersectionPieces | 0x00612780 | `queries` | the pieces of the intersection a piece belongs to, appended to the caller's list as occupant parts, each referenced. A null occupant manager is the city's, a null `other` the piece. Makes a network filter of the mask 0x1F4F (every type but pipe, power line and subway) for the walk, and does nothing when the piece is in the list already |
| the walk | 0x00612300 | `queries` | appends the piece, then for each of the four neighbour cells inside the city takes the first occupant the filter accepts; a piece there with the flag 0x100000 that is not in the list and says it is part of the same structure as `other` (slot 66) is walked in turn |
| MapPreviewTexture | 0x00610AD0 | `maps` | walks the entries of +0x44 in order: a matching entry rewrites the ID and turns the caller's rotation byte; the walk ends unless the entry says to go on (later entries then see the new ID) |
| MapShadowTexture | 0x00611D50 | `maps` | the same over +0x50; an entry with base 0 looks the edge key up in the table at +0x30 and takes the first node of that key whose network type's flag equals the kept bits (0x304: highway, light rail, monorail) of the flag argument: the node's base, mask and rotation; without such a node the entry does nothing |
| the equal range | 0x00611970 | `maps` | the first node of a key in the hash table and the node after its last (in the same chain, or the first of the next bucket that has one) |
| MapPath | 0x00610B40 | `maps` | with the byte at +0x11 set the first matching entry of +0x5C gives its base plus the ID's bits under its mask; else the ID |
| GetEnglishNetworkName | 0x006107F0 | `queries` | the type's pointer in the table at 0x00AA845C; no range test |
| GetWaterOccupantFilter, GetSubwayOccupantFilter | 0x00610590, 0x006105C0 | `queries` | a new filter, reference count 0 |
| the filters' deleting destructors | 0x006104D0, 0x00610500 | `queries` | the vtables, the base's destructor, the memory freed |
| RegisterConstructionCrew | 0x006122B0 | `queries` | appends the crew to +0x7C and references it |
| UnregisterConstructionCrew | 0x006112A0 | `queries` | takes the crew out of +0x7C and releases it; nothing for a crew that is not there |
| AddToRetrofitList | 0x00612480 | `queries` | references the piece and appends it to the list at +0x78; ProcessRetrofitList empties the list at the next city load message |
| the retrofit list's node allocation | 0x00611240 | `queries` | count times 12 bytes from the allocator cached at 0x00B45238 (also called by the constructor for the head node) |

Copied on purpose from the game and commented in the code: the walk hands the occupant manager an out word that still holds something else (the list's address the first time, the last occupant after that), because the game reuses its argument slots as locals; GetEnglishNetworkName reads beside the table for a type outside 0 to 12; Register and Unregister return what the last call left in EAX.

The walk's answer is not always a function of the city: slot 66 (0x006198A0, the network occupant's, left the game's: [netocc.md](netocc.md)) compares the complex piece origins of two pieces from four stack words, and a piece without an origin leaves its two words as the stack held them. The test below asks only where that cannot happen.

## The rest of the class (to come)

The other two parts hold the remaining 48 functions of the 70.

- **Life and save** (part 2, 20 functions, to come): factory, constructor, destructor, deleting destructor, Init (makes the 13 tools, subscribes to 11 messages), Shutdown, LoadRemapping 0x00613140 with its four ini section callbacks ("Texture remapping", "Shadow remapping", "Shadow edge remapping", "UK path remapping"; resource group 0x8A5971C5, instance 0x8A5993B9), Write and Read with a schema, and our own object.
- **Messages, removals, nodes and retrofit processing** (part 3, 24 functions, to come): DoMessage 0x00613950, ProcessRetrofitList 0x00612820, DestroyNetworkOccupants, QueueRemovals, RemoveOrReplaceOccupant, CreateConnectionNode, AutoconnectLotNodes.

### The save record

As far as the survey has it (Write and Read are part 2's, to come): the record's type is the class ID 0xC990BD46. Version 1 (uint16), a count (uint32), then that many references: the pieces of the vector at +0x24. Read refuses any other version. The three shipped tutorial cities have no such record (the city then makes the manager); saves made by this build have one, 18 bytes when no piece is managed globally.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.netmgr.identity` | QueryInterface, Release, GetGZCLSID (3 detours) |
| `simulators.netmgr.queries` | the subway station test and the two pipe lookups (forwards), GetNetworkTool, GetEnglishNetworkName, the two filter getters and the filters' deleting destructors, Register and UnregisterConstructionCrew, AddToRetrofitList with its node allocation, GetAllIntersectionPieces with its walk (15 detours) |
| `simulators.netmgr.maps` | MapPreviewTexture, MapShadowTexture with the equal range, MapPath (4 detours) |

All detours at the game's addresses (the thunks of the other vtable parts and the game's direct calls reach them), off by default; `OPENSC4_NETMGR_SKIP` takes a list of addresses to leave out. Each function logs `netmgr: first <name>` once.

Still the game's in the first part's range: the four adjustor thunks (0x006120D0, 0x006120E0, 0x006120F0, 0x00612100: they jump to our QueryInterface and Release), the AddRef stub 0x004664A0 and the thunks 0x006C2270 and 0x006C2290 (shared with other classes). Called at their addresses and not this class's: the tools' code behind the three forwards, the four tool constructors, the filters' test functions, the network filter's constructor 0x0061F530, the vector, list and search helpers.

## The test

The fingerprint's `netmgr` part (order 131) asks everything through the manager's vtable (the city's slot 38), every day:

- `st`: the bytes at +0x10, +0x11, +0x12, +0x74; `rect`: a checksum of the dirty rectangle; `mg`, `q`, `crews`, `retro`: the sizes of the vectors at +0x24, +0x68, +0x7C and the length of the retrofit list.
- `tools`: a bit per type for which GetNetworkTool (not making) gives a tool, and bits 13 to 15 for types 13, 14 and 0xFFFFFFFF, which must give none: 1FFF.
- `names`: the characters of the 13 English names. `id`: the class ID and QueryInterface through the three parts for four IDs the class answers and three it does not, each reference released (so Release counts down every day).
- `tab`: a checksum of the three remap vectors and the shadow edge table as they are, then their four sizes. `maps`: MapPreviewTexture, MapShadowTexture and MapPath over three IDs made from every table entry (one it takes plainly, one with every unmasked bit set, one it refuses) and five IDs no table should hold, with several rotations, the five network flags that matter and up to eight edge keys of the table plus two it should not hold.
- `occ`: the number of network pieces. `station`, `pipe`, `conn`: for how many of them IsOccupantSubwayStation, FindPipeOccupant and FindPipeConnectionOccupant (at the piece's cell) said yes. `isect a,s,e,f`: GetAllIntersectionPieces asked for `a` pieces and skipped for `s`, `e` list entries in all, `f` pieces with the flag 0x100000. Then the sum and the exclusive or of a checksum per piece (its cell, the three answers, and for the intersection the answer, the number of entries, the entries' cells without order, and that a second call adds nothing). The entries are released and the list freed.

GetAllIntersectionPieces is asked for every piece when no piece of the city has the flag 0x100000 (slot 66 is then never reached and each answer is the piece alone); otherwise only for pieces that have a complex piece origin, and only when every flagged piece has one. The rest are skipped, because their answer could depend on stack leftovers (above).

On a city without network pieces the walk fields are `occ 0 station 0 pipe 0 conn 0 isect 0,0,0,0 00000000,00000000`; the rest does not depend on the city's networks.

`OPENSC4_TEST_NETMGR=<day>` drives, once, what makes or changes something and puts it back, and adds ` set <checksum>` to the part and one log line `autotest: netmgr: ...`. The checksum mixes the results of the four items below (crews, both filters, the station count, the tools). The number of occupants of the city is only in the log line, not in the checksum: it is not the same in two runs of the original (82,730 and 82,729 on one save).

- five stand-in crews of the test's own registered and unregistered in an order that takes every branch (growing and in place, from the middle, the last, one that is not there), with the vector's size and each stand-in's reference count and place after every call;
- both filters made, referenced, asked for their interfaces and class ID, their occupant test run over every occupant of the city (how many each takes), and released, which destroys them;
- IsOccupantSubwayStation over every occupant of the city (the daily part only asks network pieces, which are never stations);
- GetNetworkTool in its making form for type 0: the tool is made, compared with the manager's own (another object, the same vtables), referenced and released. This is what the game's ProcessRetrofitList does with a road tool of its own. `OPENSC4_TEST_NETMGR_TOOLS=1` does it for all 13 types; the game itself never drops a tool of the other types without Init, so that is not on by default.

Not called by any test: AddToRetrofitList (nothing takes an entry off the list before the next retrofit pass, which only runs at a city load). Its node allocation 0x00611240 still runs with every city: the constructor calls it for the list's head node.

## Parity

Batch m1 (2026-10-13): the original against every network occupant group, their `.own` groups and the three groups of this part for a week, `determinism.dispatch_info` in the base list. Before the first run: `ret N` of the replaced functions against our signatures (no mismatch; MapPreviewTexture's read by hand, `ret 8`), the caller register scan and the EAX scan (no hit), and an independent reading of all 22 functions against the disassembly (no difference in behaviour).

Plain: `saves-networks` 3 of 3 the same on the first pair, `saves-plopped` 3 of 3, `saves-ports` 3 of 3, the 12 saves 11 of 12: on Big City d0000 the first run of ours had other `adv` and `advisors` values from the first day on (the advisor system's own generator state is in that checksum; nothing of this class touches it), the three pairs after it and 24 more pairs of that save (12 with these groups, 12 with only the groups of 2026-10-12) were all the same. The same thing happened once in the batch before this one, on another save ([netocc.md](netocc.md), Parity): twice in about 110 runs of ours, never in the original's, both times in a full batch and not in the repeats. Not explained.

With `OPENSC4_TEST_NETMGR=3` (and the underground pieces' switches): the 12 saves 12 of 12 and `saves-networks` 3 of 3 on the first pair, the part and its `set` checksum the same on both sides in every save. On `saves-networks` Big City n0000 the part reads `netmgr st 1000 rect 1A59D7F5 mg 62 q 0 crews 0 retro 0 tools 1FFF names 16B11744 id 85E3C59B tab 183A143E,3,110,16,16 maps A8B29554 occ 3866 station 0 pipe 651 conn 0 isect 3866,0,3866,0 2FDFAC1F,59ED38BD set 25352B6A`.

A first trial had the `set` checksum differ on two saves: the drive mixed the city's number of occupants, which is not the same in two runs (82,730 and 82,729); it is in the log line only now.

Ran with ours (first-call log lines over the batch): 21 of the 22 functions; AddToRetrofitList did not (the test does not call it and no piece of the corpus is retrofitted in a week).
