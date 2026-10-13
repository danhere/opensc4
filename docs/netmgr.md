# The network manager (cSC4NetworkManager)

The city's desk for the networks. It holds the thirteen network tools (one per network type: road, rail, highway, street, pipe, power line, avenue, subway, light rail, monorail, one-way road, dirt road, ground highway), the remap tables of the networks' ini resource, the list of the pieces that are managed globally, the queue of removals, the retrofit list and the construction crews. The tools, the lot code and the pieces ask it for lookups (a pipe at a cell, a subway station, the pieces of one intersection, a remapped texture or path ID); the demolition and lot code go through it to remove pieces and to make a lot's connection nodes. It has no grid and nothing sized from the city. The class has 70 functions (14.7 KB) and is reimplemented in three parts; this page has the first (state and queries, 22 functions), the second (life and save, 20 functions), the first half of the third (messages and removals, 18 functions; PieceOffTerrain is written inside DoMessage) and what is left for its second half. With all three that is 60 of the 70 functions ours. Build 641 addresses throughout (data/addresses/netmgr.toml).

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
| +0x11 | left-hand traffic: Init sets it when the current language's road driving side (the language service's utility, `GetLanguageRoadDrivingSide`) has a low byte of 0; MapPath maps only when it is set (the ini section is "UK path remapping") |
| +0x12 | batching cell changes: set by the message 0xEA3B0AF1, cleared by the flush |
| +0x14 | the dirty cell rectangle (min x, min z, max x, max z; empty is 0x7FFFFFFF twice and -1 twice): the cells changed since the batch began, or of the one change outside a batch |
| +0x24 | vector of the globally managed pieces, referenced; this is the save record |
| +0x30 | the shadow edge table: a hash table (100 buckets at first) of 0x18-byte nodes: next, edge key, network type, base, mask, rotation byte |
| +0x44 | vector of 0x18-byte preview texture remap entries |
| +0x50 | vector of 0x18-byte shadow texture remap entries |
| +0x5C | vector of 0x10-byte path remap entries |
| +0x68, +0x74 | vector of occupants queued for removal (referenced) and the queueing byte |
| +0x78 | the retrofit list (a pointer to the head node; nodes of 12 bytes from the allocator cached at 0x00B45238), referenced |
| +0x7C | vector of construction crews, referenced |
| +0x88 | the 13 network tools by type |

The constructor never writes +0x13, the first word of the table at +0x30 and +0x75 to +0x77; they stay as operator new left them.

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

## What each function of the second part does

| Function | Address | Ours | What |
| --- | --- | --- | --- |
| the factory | 0x00613750 | `object` | cdecl, no arguments: 0xBC bytes from operator new, then the constructor (the game jumps to it); the object with a reference count of 0, or null |
| the constructor | 0x00613330 | `object` | the base classes' vtables at +4 and +8, then the class's three; every field cleared in the order of the table above; the shadow edge table made with 100 buckets (0x00829BC0); the retrofit list's head node from 0x00611240, pointed at itself; the dirty rectangle emptied; last LoadRemapping. It subscribes to nothing and asks no service but the resource manager |
| the destructor | 0x00612000 | `object` | the three vtables, Shutdown (called directly), the thirteen tools released from the last to the first, then the memory let go in this order: the crews' vector, the retrofit list (0x00611AA0), the queue, the path, shadow and preview vectors, the table's nodes (0x008EBA60) and buckets, the vector at +0x24. The queued occupants and the crews themselves are not released |
| the deleting destructor | 0x00612590 | `object` | slot 24: the destructor, and with flag 1 operator delete |
| Init | 0x00610C10 | `life` | slot 3, once per object (+0x10): the byte at +0x11 from the language service (class 0x441E5070, slot 38 with 0, then the utility's slot 25: 1 when the answer's low byte is 0; neither pointer is tested for null). Unless there is no message server: the part at +4 subscribed to eleven messages in this order: 0xA6B79602, 0x278E6003, 0xEA3B0AF1, 0x6A3B0AFD, 0x6AB97795, 0x26D31EC2, 0x26D31EC1, 0x99EF1142, 0x99EF1143, 0xC9DA96E8, 0xC9DA96E9; +0x12 cleared; the thirteen tools made in the order of their types (each allocated and constructed, then referenced and stored at +0x88 + 4 x type). AL 1 |
| Shutdown | 0x00611EB0 | `life` | slot 4, nothing unless initialised: +0x10 cleared; the pieces of +0x24 released and the vector emptied; the pieces of the retrofit list released and the list cleared; the thirteen tools released and their slots nulled (ReleaseTools); the part at +4 taken off the eleven messages (Init's order, but 0x26D31EC1 before 0x26D31EC2). The queue and the crews are not touched. AL 1 |
| LoadRemapping | 0x00613140 | `life` | empties the three remap vectors and the shadow edge table, opens the ini resource and runs its four sections (below); called by the constructor only |
| the four section callbacks | 0x00612F70, 0x00612EB0, 0x00613030, 0x006130E0 | `life` | cdecl (key string, value string, the manager), one call per line of a section; each scans the line's value and appends an entry (below) |
| the shadow edge append | 0x006125B0 | `life` | (type, key, base, mask, rotation): the table at +0x30 resized for one more (0x004F0940) and the value inserted |
| the table's insert | 0x00611890 | `life` | an insert that keeps equal keys: a new 0x18-byte node goes right behind the first node of its key, or at the head of the bucket (key modulo the bucket count) |
| the path append | 0x00612610 | `life` | (match, mask, base, mask): a 0x10-byte entry at the end of +0x5C |
| ReleaseTools | 0x00610880 | `life` | cdecl (first, last, the address of a pointer): assigns the pointer to every slot of the range, the new one referenced and the old one released; Shutdown and DoMessage (0x26D31EC2) call it with the thirteen tool slots and a null |
| ShutdownCrews | 0x00610B80 | `life` | while +0x7C holds a crew, the first crew's slot 5 (the crew unregisters itself); DoMessage (0x26D31EC2) is the only caller |
| the retrofit list's clear and destroy | 0x00611A20, 0x00611AA0 | `life` | every node but the head node back to the allocator cached at 0x00B45238 and the head node pointed at itself; the destroy also gives the head node back |
| Write | 0x006111E0 | `save` | the record below; 1 when the stream has no error |
| Read | 0x006124C0 | `save` | refuses a stream in error and any version but 1; appends each reference it reads to +0x24 and keeps the reference the read gave (no AddRef of its own); a reference that cannot be read ends it with 0 and the pieces read so far stay; the vector is not emptied first |

Copied on purpose from the game and commented in the code: Read's version word is its stream argument's stack slot, so a stream that gives no version leaves the low word of the stream's address there; Init does not test the language service for null; ShutdownCrews loops for ever on a crew that does not unregister. Not reproduced, and nothing reads them: the last two bytes of a 0x18-byte remap entry and the three bytes after a table node's rotation byte are what the game's stack held (zero in ours); the name buffer of the shadow edge callback is the 64 bytes under the game's return address, so a name of exactly 64 characters would write its end mark into the return address (ours has room).

### The remap resource

Type 0, group 0x8A5971C5, instance 0x8A5993B9: an ini text, opened with `SC4::OpenResourceRecord` 0x005F9670 and read by the game's ini reader (0x0091CF46, `ForEachEntry` 0x0091D91C, 0x0091CDE3), which calls a callback for every `key = value` line of a section (lines that begin with `;` are comments). The key is not looked at; the value is scanned with sscanf. The sections are read in this order:

| Section | Value | Becomes |
| --- | --- | --- |
| `Texture remapping` | `match, mask, base, keep, rotation, mirror, go on, drop`: four hexadecimal words, three decimal numbers, one hexadecimal word (` %lx , %lx , %lx , %lx , %d , %d , %d , %lx`). At least the first four; the rest are 0 when the line ends before them | a 0x18-byte entry of +0x44: +0 match, +4 mask, +8 base, +0xC keep mask, +0x10 drop mask, +0x14 the rotation byte (the rotation's low two bits, plus 0x80 when mirror is not 0), +0x15 1 when go on is not 0 |
| `Shadow remapping` | the same | the same in +0x50 |
| `Shadow edge remapping` | `name, key, base, mask, rotation, mirror`: a name of up to 64 characters that ends at a comma or blank, three hexadecimal words, two decimal numbers (` %64[^, ] , %lx , %lx , %lx , %d , %d`). At least the first four | the name is looked up without regard to case among the thirteen English network names; a line with another name is left out. A node of the table at +0x30: key, the name's network type, base, mask, the rotation byte as above |
| `UK path remapping` | `match, mask, base, mask`: four hexadecimal words, all needed | a 0x10-byte entry of +0x5C |

A line with fewer values than needed is left out without a message. In the shipped game the tables hold 3, 110 and 16 entries and 16 edges (the `tab` field of the test).

### The save record

The record's type is the class ID 0xC990BD46. After the save framework's header (size, CRC, address-ID): uint16 version 1, uint32 count, then count references (8 bytes each: address-ID and class ID): the pieces of the vector at +0x24 in the vector's order. Nothing else of the manager is saved. `tools/re/save_schemas/netmgr.py` decodes it.

Checked with `tools/re/sc4save.py --check`, every record decoded to its last byte:

| Corpus | Records | Counts seen |
| --- | --- | --- |
| `saves` (12 files) | 9 | 0 in all nine (18 bytes); the three d0000 files, the shipped tutorial cities, have no such record (the city then makes the manager) |
| `saves-networks` (3) | 3 | 62 in Big City n0000 and n0090 (514 bytes; 48 references of class 0x49CC1BCD and 14 of 0x49C1A034), 2 in Rush Hour t0000 (34 bytes; class 0x8A4BD52B) |
| `saves-plopped` (3) | 3 | 0 |
| `saves-ports` (3) | 3 | 0 |

## What each function of the third part's first half does

Messages and removals (`src/core/netmgr_messages.cpp`).

| Function | Address | Ours | What |
| --- | --- | --- | --- |
| DoMessage | 0x00613950 | `messages` | slot 3 of the message target at +4, `ret 4`, 1,964 bytes: eleven messages, see the table below. AL 1 for every message, known or not |
| PieceOffTerrain | 0x006105F0 | inside our DoMessage | register arguments (the cell's x in ECX, z in EAX, the piece in EDI, the terrain in ESI) and one caller, DoMessage's 0x00613BEA, so it is not hooked: our DoMessage has its own copy. Whether a piece no longer sits on the ground: at the four corners of its cell, (x, z), (x + 16, z), (x, z + 16), (x + 16, z + 16) in metres, the piece's height (slot 56, stored as a float) less the terrain's (slot 13); below 0 or above 0.01 at any corner is yes, and the first such corner ends it. A difference that is not a number counts as on the ground |
| the occupant callback | 0x00610BB0 | `messages` | cdecl (an occupant, a context that is not read); DoMessage hands it to the occupant manager for 0x6AB97795. A piece with the flag 0x40000 whose occupant has the flag 4 set posts its "changed" message 0x99EF1145. AL 1 |
| RefreshAllPieces | 0x00612690 | `messages` | `ret 8` (two values; `this` is not read): nothing when they are equal. Else every occupant of the network type (the occupant manager's slot 37 into a list) that is a piece is told that its lighting needs an update (slot 46, the game's: drawing state) |
| DestroyNetworkOccupants | 0x00612110 | `removals` | slot 6, `ret 4` (the address of a list of occupants). While removals are queued (+0x74) every occupant is referenced and appended to +0x68, nothing else. Otherwise three passes with the masks at 0x00AA7B40 (0x1F4F the surface networks, 0x10 pipes, 0x80 subways) and the tools of the types at 0x00AA7B34 (0, 4, 7): the pieces of the list whose flags meet the mask are collected, each referenced, and when there are any the manager's tool of the type runs Init, slot 50 (`DestroyOccupantsAndResolve`) with the collection and Shutdown. The demolition utility calls it with the pieces a removal took ([demolition.md](demolition.md)); so does our own DoMessage and the lot developer |
| QueueRemovals | 0x00613450 | `removals` | slot 7, `ret 4` (a byte): the queueing switch. Switching on only sets +0x74. Switching off with occupants queued: the queue is sorted by address, an occupant queued more than once is released for each time beyond the first, the rest go into a list that DestroyNetworkOccupants (not queueing now) and the occupant manager's slot 48 get; the queue is emptied. DemolishRegion switches it on before it finishes its lots and off after it told the manager of the removed occupants, so the pieces of one drag go to the tools in one call |
| RemoveOrReplaceOccupant | 0x006110B0 | `removals` | slot 8, `ret 0xC` (a tool, an occupant, the address of a byte); the tool's slot 50 calls it per piece (0x0062B213). 0 for no occupant, for one whose flag bit 0 is clear and for one that is no piece. A piece without the flag 0x100000 is removed from the occupant manager (slot 21) and that answer is the answer. A piece with the flag (part of an intersection of several cells): the tool's entry for the cell (0x00633220 with (z << 16) + x), the removal, then the tool's byte +0x50 set and its slot 72 called with the entry: the byte is set to 1 and the answer is 1 |
| the sort and its loop | 0x006132E0, 0x00612E00 | `removals` | cdecl (first, last): an introsort of unsigned words, ascending: the loop takes the median of three, partitions (0x00611840, shared) and calls itself for the upper part while a range is longer than 16; then one insertion pass (0x0048A460, shared). Only QueueRemovals calls it |
| the vector of point vectors | 0x00613920, 0x006138B0, 0x00613770, 0x00613640, 0x006135E0, 0x00611BD0, 0x00611C00, 0x00611C30, 0x00611C60, 0x00611CB0 | `messages` | resize with an empty value, resize, insert n, the growing insert, erase, and five loops (copy construct a range, assign a value to a range, construct n copies, assign a range forward and backward) over elements of 0xC bytes that are vectors of 8-byte points. Only the flush calls the first, with 13 on an empty vector, so in the game only the growing insert ever runs; the other branches are reached by the test alone |

Left the game's and called at their addresses: the partition 0x00611840 (also called from 0x0047EAA8 and 0x0048ACA8) and the point vector's assignment 0x00611B00 (eighteen callers in other classes): shared template code. The tools' code: 0x006588D0, 0x00633220 and the tools' slots.

### The messages

The message's type is its slot 4; "data 1" is its slots 9 and 10, "data 2" 14 and 15, "data 4" 24.

| Message | What DoMessage does |
| --- | --- |
| 0x99EF1142, an occupant was inserted (data 1 the occupant) | an occupant that is a network piece and says it is managed globally (slot 5) is appended to +0x24, which keeps the reference |
| 0x99EF1143, an occupant was removed | such a piece is looked for in +0x24; when found its reference is released, the last entry takes its place and the vector shrinks by one |
| 0xC9DA96E8, 0xC9DA96E9 | every piece of +0x24 that can be lit (slot 62) is unlit, lit (slot 61, the game's: drawing state) |
| 0x26D31EC1 (a city was loaded) | ProcessRetrofitList 0x00612820 (the game's); then, when the 24 hour clock (0x00B43DBC, slot 28) says it is night, the lit pass |
| 0x26D31EC2 | ShutdownCrews, then the thirteen tools released and their slots nulled (ReleaseTools) |
| 0xEA3B0AF1, a long terrain operation begins | the dirty rectangle emptied, +0x12 set |
| 0x278E6003, cells changed (data 1 the x range, data 2 the z range, each (low + 1) << 16 or high; data 4 the reason) | the cells are cut to the city (the city's slots 93 and 94: nothing assumes a size) and merged into the dirty rectangle (batching) or stored as it. Unless the reason is 5: the pieces of the cells (the occupant manager's slot 40 with a network filter of the mask 0x1FFF) that PieceOffTerrain says are off the ground go to DestroyNetworkOccupants and the occupant manager's slot 48. Reason 4 keeps every piece; every reason but 3 keeps pipes and subways; reason 6 keeps pieces with the flag 0x80000. Then, unless batching, the flush |
| 0x6A3B0AFD, the operation ends: the flush | with a rectangle that is not empty: the pipes and subways in it (filter mask 0x90) are collected, every piece's cell goes into the point vector of each type it is of (thirteen vectors), and when there are pipes the pipe tool runs Init, 0x006588D0 (the pipes' points, the pipes, 4) and Shutdown; the same for subways with 7. The rectangle is emptied. With or without a rectangle +0x12 is cleared |
| 0x6AB97795 (the terrain's "show water" changed) | the occupant manager's slot 45 over every occupant of the network type with the callback 0x00610BB0 |
| 0xA6B79602 (the view changed; data 1 and data 2 each point at something) | when the words at +4 of the two differ, RefreshAllPieces with them |
| any other | nothing |

Copied on purpose from the game and commented in the code: the filter DoMessage makes, the clock, the tool the flush gets and the occupant manager are used without a test for null; the callback uses the piece without looking at QueryInterface's answer; QueueRemovals would release through a null pointer for a null first entry; the flush gets the manager from `SC4::GetNetworkManager` and not from its own `this`; the references of the pieces handed to a tool (DestroyNetworkOccupants' collection, the flush's two lists) are not given back by the manager. The comparison object of the sort is empty, and the game pushes whatever its own first argument is in its place: ours does the same. Not reproduced, and nothing reads them: the three bytes after the byte of RemoveOrReplaceOccupant's record and the words of a cell before slot 59 writes them are what the game's stack held (zero in ours).

## The rest of the class (to come)

Ten functions of the 70 are still the game's. Three are the second half of the third part: ProcessRetrofitList 0x00612820 (1,498 bytes; DoMessage calls it at its address), CreateConnectionNode 0x00611300 (slot 20) and AutoconnectLotNodes 0x00611530 (slot 21). They call the second part's ReleaseTools, ShutdownCrews and the retrofit list's clear at their addresses. The other seven are left on purpose: the two template helpers shared with other classes (the partition 0x00611840 and the point vector's assignment 0x00611B00), the four adjustor thunks, and PieceOffTerrain's original 0x006105F0, which ours does not use.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.netmgr.identity` | QueryInterface, Release, GetGZCLSID (3 detours) |
| `simulators.netmgr.queries` | the subway station test and the two pipe lookups (forwards), GetNetworkTool, GetEnglishNetworkName, the two filter getters and the filters' deleting destructors, Register and UnregisterConstructionCrew, AddToRetrofitList with its node allocation, GetAllIntersectionPieces with its walk (15 detours) |
| `simulators.netmgr.maps` | MapPreviewTexture, MapShadowTexture with the equal range, MapPath (4 detours) |
| `simulators.netmgr.life` | Init, Shutdown, LoadRemapping, the four section callbacks, the shadow edge and path appends, the table's insert, ReleaseTools, ShutdownCrews, the retrofit list's clear and destroy (14 detours) |
| `simulators.netmgr.save` | Write and Read (2 detours) |
| `simulators.netmgr.object` | the factory, the constructor, the destructor and the deleting destructor (4 detours) |
| `simulators.netmgr.messages` | DoMessage (PieceOffTerrain inside it), the occupant callback, RefreshAllPieces and the ten functions of the vector of point vectors (13 detours) |
| `simulators.netmgr.removals` | DestroyNetworkOccupants, QueueRemovals, RemoveOrReplaceOccupant and the sort with its loop (5 detours) |
| `simulators.netmgr.own` | no detour: with it and `object`, a manager carries our copies of the class's three vtables |

All detours at the game's addresses (the thunks of the other vtable parts and the game's direct calls reach them), off by default; `OPENSC4_NETMGR_SKIP` takes a list of addresses to leave out. Each function logs `netmgr: first <name>` once. Every call from one of our functions to another of the class goes through the game's address, so a group that is off or a skipped function is the game's.

### Our own object

With `simulators.netmgr.object` and `simulators.netmgr.own` the constructor and the destructor store the addresses of our copies of the three vtables (25, 4 and 6 slots; `src/core/occ_vtables.h`, as the network occupant classes do) instead of the game's tables. A copy is the game's table word for word, taken at the first construction, after every replacement is in place; then the entries that hold a function of the `object` group directly are pointed at ours: slot 24 of the primary table, the deleting destructor (the log line `netmgr own: vtables copied (25, 4 and 6 slots), 1 entries redirected`). Every other entry stays the game's address or adjustor thunk, which the other groups' detours still decide. `own` without `object` does nothing.

Still the game's in the first two parts' range: the four adjustor thunks (0x006120D0, 0x006120E0, 0x006120F0, 0x00612100: they jump to our QueryInterface and Release), the AddRef stub 0x004664A0 and the thunks 0x006C2270 and 0x006C2290 (shared with other classes). Called at their addresses and not this class's: the tools' code behind the three forwards, the four tool constructors, the filters' test functions, the network filter's constructor 0x0061F530, the vector, list and search helpers; for the second part the string and the ini reader, the resource record's opener, the hash table's constructor, clear and resize (0x00829BC0, 0x008EBA60, 0x004F0940: shared with other classes), the vectors' growing inserts (0x0051CA60, 0x00638C60, 0x00627100), the stream's reference helpers (0x005D99A0, 0x005D9730) and the C library's sscanf and compare (0x009EF536, 0x00A04487); for the third part's first half the reference holder and the list, vector and search helpers (0x004F20C0, 0x00414270, 0x00413FE0, 0x00467B50, 0x004F5A00, 0x00427640, 0x005650A0, 0x004467A0, 0x007CED60, 0x004638B0, 0x007EBD30, 0x008BC520), the network filter's other constructor 0x0061F510, the two shared sorts of small ranges (0x0048A460, 0x0048A4B0), and the two shared helpers of the class's own range named above (0x00611840, 0x00611B00).

## The test

The fingerprint's `netmgr` part (order 131) asks everything through the manager's vtable (the city's slot 38), every day:

- `st`: the bytes at +0x10, +0x11, +0x12, +0x74; `rect`: a checksum of the dirty rectangle; `mg`, `q`, `crews`, `retro`: the sizes of the vectors at +0x24, +0x68, +0x7C and the length of the retrofit list.
- `tools`: a bit per type for which GetNetworkTool (not making) gives a tool, and bits 13 to 15 for types 13, 14 and 0xFFFFFFFF, which must give none: 1FFF.
- `names`: the characters of the 13 English names. `id`: the class ID and QueryInterface through the three parts for four IDs the class answers and three it does not, each reference released (so Release counts down every day).
- `tab`: a checksum of the three remap vectors and the shadow edge table as they are, then their four sizes. `maps`: MapPreviewTexture, MapShadowTexture and MapPath over three IDs made from every table entry (one it takes plainly, one with every unmasked bit set, one it refuses) and five IDs no table should hold, with several rotations, the five network flags that matter and up to eight edge keys of the table plus two it should not hold.
- `saved o,s` (at the end of the part): the pieces of the vector at +0x24, the save record's, in the order they are stored: each piece's cell and the class ID of its serializable part, as a checksum that depends on the order (`o`) and a sum that does not (`s`); `811C9DC5,00000000` when there are none.
- `occ`: the number of network pieces. `station`, `pipe`, `conn`: for how many of them IsOccupantSubwayStation, FindPipeOccupant and FindPipeConnectionOccupant (at the piece's cell) said yes. `isect a,s,e,f`: GetAllIntersectionPieces asked for `a` pieces and skipped for `s`, `e` list entries in all, `f` pieces with the flag 0x100000. Then the sum and the exclusive or of a checksum per piece (its cell, the three answers, and for the intersection the answer, the number of entries, the entries' cells without order, and that a second call adds nothing). The entries are released and the list freed.

GetAllIntersectionPieces is asked for every piece when no piece of the city has the flag 0x100000 (slot 66 is then never reached and each answer is the piece alone); otherwise only for pieces that have a complex piece origin, and only when every flagged piece has one. The rest are skipped, because their answer could depend on stack leftovers (above).

On a city without network pieces the walk fields are `occ 0 station 0 pipe 0 conn 0 isect 0,0,0,0 00000000,00000000 saved 811C9DC5,00000000`; the rest does not depend on the city's networks.

`OPENSC4_TEST_NETMGR=<day>` drives, once, what makes or changes something and puts it back, and adds ` set <checksum>` to the part and one log line `autotest: netmgr: ...`. The checksum mixes the results of the four items below (crews, both filters, the station count, the tools). The number of occupants of the city is only in the log line, not in the checksum: it is not the same in two runs of the original (82,730 and 82,729 on one save).

- five stand-in crews of the test's own registered and unregistered in an order that takes every branch (growing and in place, from the middle, the last, one that is not there), with the vector's size and each stand-in's reference count and place after every call;
- both filters made, referenced, asked for their interfaces and class ID, their occupant test run over every occupant of the city (how many each takes), and released, which destroys them;
- IsOccupantSubwayStation over every occupant of the city (the daily part only asks network pieces, which are never stations);
- GetNetworkTool in its making form for type 0: the tool is made, compared with the manager's own (another object, the same vtables), referenced and released. This is what the game's ProcessRetrofitList does with a road tool of its own. `OPENSC4_TEST_NETMGR_TOOLS=1` does it for all 13 types; the game itself never drops a tool of the other types without Init, so that is not on by default.

The same day also drives the second part and adds ` life <checksum>` after ` set <checksum>` and a second log line `autotest: netmgr: life: ...`:

- Init on the city's manager: it is initialised, so the answer is 1 and nothing happens;
- Write of the city's manager into a stream of the test's own, once into a stream without an error (answer 1) and once into one that reports one (answer 0). Such a stream answers no interface, so the game's `WriteReference` writes nothing for a piece and does not touch it: the bytes are the version and the count (six bytes);
- unless `OPENSC4_TEST_NETMGR_OBJECT=0`: a second manager made by the factory and destroyed again. As constructed: its state bytes, the empty rectangle, the empty vectors and list, no tools, the table's bucket count, the same vtable pointers and the same remap tables as the city's manager (so LoadRemapping ran a second time and gave the same); its identity through the three parts; Shutdown on it (nothing, 1); Write (six bytes with a count of 0); Read with seven streams that end before a reference is stored (empty, versions 2 and 0, version 1 without a count, a count of 0, a count of 2 on a stream no reference can be read from, a stream that is in error already); the last Release, which runs the deleting destructor. Safe by the disassembly: the constructor subscribes to nothing, and the destructor of a manager that was never initialised frees its own memory only. Init is not called on it.

The same day drives the third part's first half and adds ` msg <checksum>` after ` life <checksum>` and a third log line `autotest: netmgr: msg: ...`. Only what the disassembly shows to change nothing lasting is called; the state (the batching and queueing bytes, the dirty rectangle, the sizes of +0x24 and +0x68) is read after each step, and the rectangle is put back as it was at the end. Nothing goes through the message server: the messages are objects of the game's own message class handed straight to the manager's message target.

- the template code on memory of the test's own (`helpers` on the log line): the sort over ranges of 0, 1, 2, 16, 17, 40 and 300 words with equal ones among them; a vector of point vectors resized to 13, filled, resized to 5 and to 9, then inserts at the start, before the last two and without room, and an erase in the middle, the contents mixed after each. This is the only thing that reaches the erase, the in-place inserts and four of the five loops with ours or with the game's;
- a message of a type the manager does not know;
- inserted and removed with no occupant, with an occupant that is no network piece, with a piece that is not managed globally;
- the last piece of +0x24 (where there is one: `saves-networks`) removed and inserted again: the vector and the reference counts are as before;
- 0xA6B79602 with two equal states (nothing). With `OPENSC4_TEST_NETMGR_REFRESH=1` also with two that differ (RefreshAllPieces over every piece) and 0x6AB97795 (the callback over every network occupant): both only touch drawing state and post "changed" messages, so they are off unless asked for;
- a batch: begin, two cell changes of reason 5 (the rectangle grows, one of them cut to the city), the rectangle emptied by hand, end; and begin and end with nothing between;
- one cell change of reason 4 outside a batch, on the first cell with a network piece (by z, then x, of the first 64) where the flush's filter finds no pipe and no subway: the pass finds the pieces and keeps them all, DestroyNetworkOccupants and the occupant manager get an empty list, the flush finds nothing to rebuild and empties the rectangle. The number of pieces on the cell before and after is mixed;
- QueueRemovals on, DestroyNetworkOccupants with an empty list, on again, off, DestroyNetworkOccupants with the empty list again; RemoveOrReplaceOccupant with no occupant.

Not sent by the test: 0x26D31EC1 and 0x26D31EC2 (the retrofit pass; the tools let go), the lighting pair (the lit flag follows the game's day and night and the `netocc` part counts it), cell changes of the reasons that remove pieces. The whole step is left out, and the log line says so, when the manager is batching or queueing on that day.

Real removals come from the switches that exist for them. The table is read from the code of those tests and of the senders, not from a run: the first-call log lines (`netmgr: first <name>`) of a batch say what was reached.

| Switches | What the game then calls |
| --- | --- |
| none | DoMessage for every occupant inserted or removed, the lighting pair with the game's day and night, 0x26D31EC1 at the load, 0x26D31EC2; the batch and cell-change messages whenever the simulation changes terrain (lots being built), with the flush and the growing insert; QueueRemovals and DestroyNetworkOccupants whenever the simulation demolishes |
| `OPENSC4_TEST_BULLDOZE=<day>`, alone and with `OPENSC4_TEST_BULLDOZE_MODE=2` (networks only) | DemolishRegion over the middle half of the city: QueueRemovals on, DestroyNetworkOccupants queueing, QueueRemovals off with a queue (the sort with real addresses), the three passes with the tools, and from the tools' slot 50 RemoveOrReplaceOccupant for every piece; on `saves-networks` roads, rails, pipes and the subway |
| `OPENSC4_TEST_BULLDOZE_TOOL=<day> OPENSC4_TEST_BULLDOZE_TOOL_COMMIT=1` | the same through the tool for a 4 by 4 block; network pieces only where the block has some |
| `OPENSC4_TEST_TERRAIN_SEA=<day>` and `OPENSC4_TEST_TERRAIN_EDIT=<day>` | the terrain's batch and cell-change messages over land that has networks: PieceOffTerrain, removals by DoMessage, and on `saves-networks` the flush with pipes and subways (0x006588D0). The edit's hide and show of the water sends 0x6AB97795 (the callback) |
| `OPENSC4_TEST_SMALL_TOOLS=<day> OPENSC4_TEST_SMALL_TOOLS_COMMIT=1` | the level tool's cell change on four free cells: the pass and the flush with nothing found |
| `OPENSC4_TEST_CITY_ZOOM=<day>` | the view's 0xA6B79602 for every zoom and rotation: RefreshAllPieces when the word at +4 changed |

What runs without any switch: the factory, the constructor, LoadRemapping with its callbacks and inserts and Init at every city load; Shutdown, ReleaseTools, the list's clear and destroy and both destructors when the city is closed (the quit); ShutdownCrews and ReleaseTools from DoMessage's 0x26D31EC2. Read runs at the load of a save that has the record (every save this build made; with references only on `saves-networks`), Write only at a save.

Not called by any test: Shutdown on the city's manager and Init on a second one; Read with a reference that resolves (a load of `saves-networks` does that); AddToRetrofitList (nothing takes an entry off the list before the next retrofit pass, which only runs at a city load). Its node allocation 0x00611240 still runs with every city: the constructor calls it for the list's head node.

## Parity

Batch m1 (2026-10-13): the original against every network occupant group, their `.own` groups and the three groups of this part for a week, `determinism.dispatch_info` in the base list. Before the first run: `ret N` of the replaced functions against our signatures (no mismatch; MapPreviewTexture's read by hand, `ret 8`), the caller register scan and the EAX scan (no hit), and an independent reading of all 22 functions against the disassembly (no difference in behaviour).

Plain: `saves-networks` 3 of 3 the same on the first pair, `saves-plopped` 3 of 3, `saves-ports` 3 of 3, the 12 saves 11 of 12: on Big City d0000 the first run of ours had other `adv` and `advisors` values from the first day on (the advisor system's own generator state is in that checksum; nothing of this class touches it), the three pairs after it and 24 more pairs of that save (12 with these groups, 12 with only the groups of 2026-10-12) were all the same. The same thing happened once in the batch before this one, on another save ([netocc.md](netocc.md), Parity): twice in about 110 runs of ours, never in the original's, both times in a full batch and not in the repeats. Not explained.

With `OPENSC4_TEST_NETMGR=3` (and the underground pieces' switches): the 12 saves 12 of 12 and `saves-networks` 3 of 3 on the first pair, the part and its `set` checksum the same on both sides in every save. On `saves-networks` Big City n0000 the part reads `netmgr st 1000 rect 1A59D7F5 mg 62 q 0 crews 0 retro 0 tools 1FFF names 16B11744 id 85E3C59B tab 183A143E,3,110,16,16 maps A8B29554 occ 3866 station 0 pipe 651 conn 0 isect 3866,0,3866,0 2FDFAC1F,59ED38BD set 25352B6A`.

A first trial had the `set` checksum differ on two saves: the drive mixed the city's number of occupants, which is not the same in two runs (82,730 and 82,729); it is in the log line only now.

Ran with ours (first-call log lines over the batch): 21 of the 22 functions; AddToRetrofitList did not (the test does not call it and no piece of the corpus is retrofitted in a week).

Batch p0 (2026-10-13), the second part with the first: the original against every network occupant group and all seven groups of this class (`identity`, `queries`, `maps`, `life`, `save`, `object`, `own`) for a week. Before the first run: `ret N` of the 20 new functions against our signatures (no mismatch), the caller register scan and the EAX scan (no hit), every record of the class in the corpus decoded with every byte consumed (18 records: 62 references in `saves-networks` n0000 and n0090, 2 in t0000, none elsewhere; the three d0000 saves have no record), and an independent reading of all 20 functions against the disassembly, the remap file's format strings and section names compared with the exe's byte for byte (no difference in behaviour).

Plain: the 12 saves 12 of 12 the same on the first pair, `saves-networks` 3 of 3, `saves-plopped` 3 of 3, `saves-ports` 3 of 3. With `OPENSC4_TEST_NETMGR=3`: the 12 saves 12 of 12 and `saves-networks` 3 of 3 on the first pair, the part with its `saved`, `set` and `life` fields the same on both sides in every save, and the class's save record identical in all 36 pairs. On `saves-networks` Big City n0000 the part ends `saved D675B803,2EDECF03 set 25352B6A life 20CC0568`.

Ran with ours (first-call log lines over the batch): 41 of the 42 functions of the two parts; AddToRetrofitList did not. The manager carried our vtable copies in every run (`netmgr own: vtables copied (25, 4 and 6 slots), 1 entries redirected`).

Batch q1 (2026-10-13), part 3a with the first two parts: the original against every network occupant group and all nine groups of this class for a week. Before the first run: `ret N` of the 18 new functions against our signatures (no mismatch), the caller register scan (one hit, IntroSortLoop's call of itself, inside the replaced function) and the EAX scan (no hit), the wrapper files and the patch-site list searched for anything in the class's range (nothing), and an independent reading of the 18 functions and the inlined PieceOffTerrain against the disassembly (no difference in behaviour; 0x006588D0 frees the vector it is handed by value, so ours does not).

The six legs of the earlier parts, every pair on the first try: the 12 saves 12 of 12 plain and 12 of 12 with `OPENSC4_TEST_NETMGR=3`, `saves-networks` 3 of 3 and 3 of 3, `saves-plopped` 3 of 3, `saves-ports` 3 of 3.

Legs that make the game remove and re-check pieces, up to four pairs each:

| Leg | Saves | Result |
|---|---|---|
| `OPENSC4_TEST_BULLDOZE=3` (the middle half of the city bulldozed) | the 12 saves | 12 of 12 the same on the first pair |
| `OPENSC4_TEST_BULLDOZE=3` | `saves-networks` | n0000 and n0090 the same; t0000 partly (below) |
| `OPENSC4_TEST_BULLDOZE=3 OPENSC4_TEST_BULLDOZE_MODE=2` (networks only) | `saves-networks` | 3 of 3 |
| `OPENSC4_TEST_TERRAIN_SEA=3` | `saves-networks` | 3 of 3 |
| `OPENSC4_TEST_TERRAIN_EDIT=3` | `saves-networks` | 3 of 3 |
| `OPENSC4_TEST_CITY_ZOOM=3` | `saves-networks` | n0090 and t0000 the same; n0000 partly (below) |

t0000 after the bulldoze: the fingerprints are the same in every run of both sides. What differs is the saved file: in some runs one or two pieces that were removed (the placed bit clear) are still referenced when the city is saved and so are written, 466 or 467 network pieces in place of 465. The original does this by itself: over 18 runs of each side (the batch, a bisection over five group lists and eight more pairs) it wrote a stray piece in 5 runs (at 632,1576 four times, at 488,1544 once) and ours in 3 (632,1576 once with the lists before this part; 488,1560 once; 488,1544 and 616,1576 together once). The removal order follows the pieces' addresses (the queue is sorted by pointer), which differ from run to run.

n0000 after the zoom: the `netocc` part changes from the fifth day on and the original's four runs gave two values (two runs each); ours gave the more common one three times and a third value once. The original does not repeat after the view moves.

Ran with ours (first-call log lines over the batch): 59 of the 60 functions of the class that are ours; AddToRetrofitList did not. The helpers that the game never reaches (the point lists' erase and in-place inserts) ran from the test.
