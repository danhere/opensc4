# The power pole occupant (cSC4PowerPoleOccupant)

The tower that carries a power line over the land: one object per tower, 2 to 10 of them in the test saves (4 in Making Money, 10 in Rush Hour, 2 in the networks save, none in Big City d0000). A tower holds its position, a transform, a list of records (one for each other tower its wires run to), the points those wires pass through and four corner vectors of its model; the occupant base class ([occbase.md](occbase.md)) starts at the object + 8. Build 641 addresses throughout (data/addresses/powerpoleocc.toml); class map and slot list in `src/core/powerpoleocc_sim.h`.

Most of the class builds and draws models: the tower's model instance, the wires' geometry and the highlight. That stays out for the Renderer thread, and what is mixed with it stays the game's (see "What is left"). Ours are the class's save record, its life (QueryInterface, Init, the message handler, the constructor, the destructors, the pool allocation and the factory) and the small state and queries (23 functions in four groups).

## The class

Class ID 0x09C05C6A, occupant type 0x2890D4DE, interface IID 0xA8C86142. 0x124 bytes from a pool of 32 KB pages (112 items a page; the pool object is at 0x00B0C0C8 with the current page in its +4, the free list at the page's +8 and the used count at +0xC; its table at 0x00AA9CA8 holds the free function 0x0064ABB0 and the page set-up 0x006497E0). Virtual inheritance: the model part is at +0x3C, the table pointer at +0x40 and the virtual base at +0x120.

| Offset | Vtable | What | Slots |
| --- | --- | --- | --- |
| +0x00 | 0x00AA9EB0 | the class's interface | 19: QueryInterface, AddRef and Release (shared thunks), the record count and target, the cell pair, the flag and byte getters and setters, Init, FindRecord, the wire points and the model list |
| +0x04 | 0x00AA9EA0 | the message target | 4: DoMessage 0x0064A140 |
| +0x08 | 0x00AA9E18 | the occupant part (the cSC4Occupant base starts here) | 34: Init, Shutdown, GetPosition, SetPosition, GetBoundingBox and SetHighlight are the class's own |
| +0x0C | 0x00AA9DFC | cIGZSerializable | Write 0x0064B010, Read 0x0064EA00, GetGZCLSID 0x0064DD50 |
| +0x10, +0x24 | 0x00AA9D98, 0x00AA9D80 | the property and exemplar holders | thunks and shared code |
| +0x3C | 0x00AA9D7C | the model part (constructor 0x00801CF0) | 1 |
| +0x120 | 0x00AA9D30 | the virtual base | 19 thunks |

Fields (the evidence is the constructor, Write, Read and the small functions):

| Offset | What |
| --- | --- |
| +0x2C | the occupant type (the base's +0x24) |
| +0x54, +0x55 | highlight bytes (SetHighlight 0x006491E0; saved, but the view drives them) |
| +0x64 | a float, saved (the interface's Init sets it to 1.0) |
| +0x68 | the 0x38-byte transform: two bytes, the 3 by 3 matrix at +0x6C (identity), translation +0x90, scale +0x9C |
| +0xA0 | the position: x, y, z (GetPosition) |
| +0xAC | the vector of records (0x2C bytes each): begin, +0xB0 end, +0xB4 capacity |
| +0xB8 | a uint32, saved after the records |
| +0xBC | a referenced object (the model resource); QueryInterface hands 0x26A68448 to it; Init and Read release it |
| +0xC0 | four int32, saved (the constructor: -1, -1, 0, 0); the last two are GetCellPair's |
| +0xD0, +0xD4 to +0xE0 | the bounds in cells, worked out by GetBoundingBox while the dirty byte at +0xD0 is set (the constructor sets it) |
| +0xE4 | a pointer to one of two tables of four triples (0x00B0C0D8 or 0x00B0C198), chosen by 0x00649B30 from the exemplar; its four triples are saved |
| +0xE8, +0xF4, +0x100 | three vectors of 0x18-byte corner records, saved |
| +0x10C | a float, saved (the box's low height) |
| +0x110 | a bool, saved |
| +0x111 | a byte: registered with the message server for 0x99EF1142 |
| +0x112 | a byte, saved (IsFlag112) |
| +0x114 | a referenced object: the highlight model, made by slot 12 (not saved) |
| +0x118 | a byte (slots 13 and 14; not saved) |

A record (0x2C bytes): +0x00 the other tower (a pointer), +0x04 a list header (a circular doubly linked list; each node is 12 bytes: next, previous, a pointer to an object), +0x08 and +0x09 two bools, +0x0C and +0x10 two int32 (a cell), +0x14 and +0x20 two vectors of vectors of three floats.

## The save record

Write is the cIGZSerializable part's slot 3 (this = object + 0xC), version 5; Read takes 4 and up (version 4 has the transform as nine floats). Schema: `tools/re/save_schemas/powerpoleocc.py`; it decodes all 434 objects of the 73 saves of the corpus that have towers (116 saves scanned: `saves` and `saves-networks`; 572 records, 1,486 list items, 14,500 point lists, 5,208 corner records), every one consuming its bytes exactly, all version 5.

| Field | Size | Notes |
| --- | --- | --- |
| version | uint16 | 5 |
| occupant base record | | version 4, removal counts, highlight, flags, type, 4 cell bytes, then the holder (version 2, properties, exemplar key) |
| transform | | a flags byte, then the matrix (flag 4), translation (1), scale (2) |
| position | 3 floats | |
| records | uint32 count, then each | the other tower (a reference to class 0x09C05C6A), the number of list items and each as a reference (class 0xC9C05C5D, the object + 8), two bools (uint8), two int32, then the two vectors of vectors (outer count, then each inner count and its triples) |
| +0xB8 | uint32 | |
| +0xC0 to +0xCC | 4 x int32 | |
| the table at +0xE4 | 4 triples | |
| three corner vectors | each a count, then 0x18-byte records | floats at +0, +4, +8, +0x10, +0x14, then bytes +0xE, +0xD, +0xC, +0xF |
| +0x10C | float | |
| +0x110 | bool (uint8) | |
| +0x54, +0x55 | 2 x uint8 | the highlight bytes |
| +0x64 | float | |
| +0x112 | uint8 | |

A reference is the address-ID of the object when saved and its class ID, or a single 0 for none.

## What is ours

| Group | What is ours |
| --- | --- |
| `simulators.powerpoleocc.save` | Write, Read, GetGZCLSID (3) |
| `simulators.powerpoleocc.life` | QueryInterface 0x0064A030, the occupant part's Init 0x00649FA0, the message handler 0x0064A0F0 and the message target's DoMessage 0x0064A140 (4) |
| `simulators.powerpoleocc.object` | the constructor 0x0064DB30, the destructor 0x0064DFC0, the deleting destructor 0x0064E380, the pool allocation 0x006499D0 and the factory 0x0064E340 (5) |
| `simulators.powerpoleocc.slots` | GetBoundingBox 0x00649A00, GetPosition 0x00649DA0, GetRecordCount 0x00649D80, GetRecordTarget 0x006499B0, GetCellPair 0x00649210, IsFlag112 0x00649230, GetByte118 0x00649240, SetByte118 0x00649250 and FindRecord 0x00649DD0 (9) |

All off by default. `OPENSC4_POWERPOLEOCC_SKIP` lists hex addresses (eight digits) to leave out of a group, for bisecting.

## What is left

| Function | Why |
| --- | --- |
| 0x0064AC70, 0x0064AD90 | update and make the tower's model instance (+0xBC), with the camera's rotation: drawing |
| 0x0064B750, 0x0064C800 | draw the wires and the tower's model (vertex data, draw calls 0x007D2990, lighting) |
| 0x0064AA40 (slot 12), 0x006491E0 (SetHighlight) | make and drive the highlight model (+0x114): view state |
| 0x00649D00 (slot 17) | the model list for the renderer: sets the model-state bytes |
| 0x0064BC80 (Shutdown) | tears down the model instance (+0x114), the corner vectors and the listener: mixed with the model |
| 0x0064BFD0, 0x0064C2C0, 0x0064C470, 0x0064C540 (SetPosition) | build the corner vectors from the exemplar and the transform: the tower model's geometry |
| 0x0064CE40 (Init), 0x0064D910 (SetExemplar) | call the model builder 0x0064AD90 |
| 0x0064C6C0 (slot 18), 0x0064BD80, 0x0064CC20 | the class statics: the model resource tables and their teardown |
| 0x0064BE50 (slot 5) | the points of a wire for the renderer: a spline sampled into an output vector |
| 0x0064D510, 0x0064E3E0, 0x0064DA00, 0x0064DAE0 (slot 6), 0x0064A560, 0x0064A650 (slot 9), 0x00649B30, 0x00649910 | non-virtual members that connect and drop wires: spline geometry (0x006493B0) and network service calls mixed with the records |
| 0x006493B0 | the spline between two towers' wire anchors (pure arithmetic, no class state): left with its callers |
| 0x0064B560 to 0x0064B6E0, 0x0064BC00, 0x0064CF40 to 0x0064D450, 0x0064E0E0 to 0x0064E200, 0x0064E920 to 0x0064E990 | the STL container helpers of the records, vectors and lists: the game's allocator and layout, called by our Read |
| 0x0064ABB0, 0x006497E0 | the pool's free and page set-up: a second pool table (0x00AA9D04) shares them |
| adjustor thunks 0x0064DD70 to 0x0064DFB0, 0x0064EFD0, stubs 0x0064DD60, 0x006491C0, 0x006491D0, 0x006C2040 | thunks and folded stubs |

## Test cities

The fingerprint's `powerpoleocc` part (order 121): the number of towers (through the occupant manager's grid), the sum and exclusive or of a hash of the saved fields per tower (no addresses), the sum of the pure queries' answers over every tower (`q`) and counters that say the data is there (towers with records, with list items, with point lists, with corners, with the flag byte, the byte at +0x118, the bool, and records that have another tower). `OPENSC4_TEST_POWERPOLEOCC=<day>` adds, on that day, the setter run on every tower (SetByte118 with another value, a value with high bits, 0, 0xFF, then the old one, each read back through the getter) and one object made through the game's factory (queried through every part, its constructor fields hashed, the setter, then released: the deleting destructor and the pool page); the read-back checksums go to ` set`. Not called by the test: the occupant part's Init (it registers the object for 0x99EF1142), the message handlers, FindRecord's found branch (it ends in the wire-point builder, slot 5).

## Parity

Run on 2026-10-12 together with the other two classes of this step ([pipeocc.md](pipeocc.md), [powerlineocc.md](powerlineocc.md), [powerpoleocc.md](powerpoleocc.md)) and the ten network occupant groups ([netocc.md](netocc.md)), the original against all of them for a week. Before the first run: `ret N` of every replaced function against our signatures, the register and EAX scans (clean), every byte of the class's records in the corpus saves decoded by the schema, and an independent reading of each function against its disassembly.

Plain: the 12 saves 11 the same on the first pair and one partly (Big City d0365: one of four runs with ours differed from the load on in the advisors' checksums only, every record identical; a run of the original did the same on another save earlier that day, see [netocc.md](netocc.md)), `saves-networks` 2 of 2, `saves-plopped` 3 of 3, `saves-ports` 3 of 3. With `OPENSC4_TEST_PIPEOCC=3`, `OPENSC4_TEST_POWERLINEOCC=3` and `OPENSC4_TEST_POWERPOLEOCC=3` together: the 12 saves 12 of 12 and `saves-networks` 2 of 2 on the first pair, the three `set` checksums the same on both sides in every save.

Ran with ours: every replaced function but the stand-alone allocation (0x006499D0, the builder's path; the factory's inline copy ran). The audit found GetPosition not leaving its argument in EAX (fixed, with GetCellPair's and SetByte118's leftovers). The first batch found one real difference, on the test's own tower in the cities that have none: the cell statics are 0 there before any tower's Init, the box divides 0 by 0, and the game's float-to-integer helper (0x009EEF04) makes 0 of the NaN where a plain cast made 0x80000000. GetBoundingBox now converts as the helper does; the leg was run again on the fixed build (the numbers above).

Not run with ours: Read for version 4, FindRecord's found branch (it ends in the wire-point builder), the occupant Init and the message handlers on the test's own object.
