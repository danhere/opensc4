# The pipe occupant (cSC4PipeOccupant)

One tile of the city's water pipe network: one object per pipe piece (483 in Big City, 53 in Making Money, 100 in Rush Hour). It is a network occupant ([netocc.md](netocc.md)) with one word of its own, whether the pipe is watered, which the water simulator sets ([water.md](water.md), `SC4Pipe::SetWatered`) and which picks the tint the piece is drawn with. Build 641 addresses throughout (data/addresses/pipeocc.toml). Reimplemented like the other occupant classes ([foundocc.md](foundocc.md), [netocc.md](netocc.md)): the pipe's own functions only, from the disassembly, tested through the parity harness ([parity.md](parity.md)).

## The class

Class ID 0x49C05B9F, which is also the IID the class answers to (with 0xA9764C8C: the object itself). The occupant type is the network occupant's, 0xC772BF98: the pipes are inside the network occupant test's walk (`netocc_autotest.cpp` takes every occupant of that type); all 27,545 pipes of the corpus saves have it. 0x280 bytes from the pool at 0x00B0BEC0 (pages of 64 KB at address & 0xFFFF0000, 102 items each; table 0x00AA9960: free 0x00648970, page set-up 0x00648580). Constructor 0x006488F0 (one argument, non-zero for the most derived object), factory 0x006489B0 (returns the occupant part, object + 4), deleting destructor 0x006489F0. Code 0x00648400 to 0x00648F10 (62 function starts, most of them adjustor thunks).

The class derives from an intermediate class, not from the network occupant directly. That class (vtable 0x00AAAD40, constructor 0x0065C330, destructor 0x0065C0C0, code about 0x00652000 to 0x0065D000) is also the base of a second class (vtable 0x00AAA3C8) and is not part of this work. What the pipe's code shows of it:

- its constructor takes one argument (0 when a derived class calls it) and calls the network occupant's constructor with 0; it stores its own five vtables and its virtual base displacement (0x238 in its own object, 0x240 from the pipe's), and zeroes its fields from +0x178 to +0x270 (not +0x26C);
- its fields are at +0x178 up to +0x270: a word at +0x17C, six vectors of 0x18-byte records (the network occupant's corner layout) at +0x180, +0x18C, +0x198, +0x1A4, +0x1B4 and +0x1C0, a 4 by 4 matrix at +0x1FC, words at +0x23C and +0x240 (a position in the network's grid, which it asks the network manager about), bytes at +0x248 to +0x24A, floats from +0x24C to +0x25C, a dirty byte at +0x260 (the pipe's Read sets it to 1), bytes +0x261 and +0x262, words at +0x264 and +0x268 and at +0x26C the descriptor the pipe's slot 101 answers;
- its Init 0x0065C6B0 takes three arguments (the description handed to the pipe's Init, the class ID and the descriptor), its occupant Init is 0x0065C070, QueryInterface 0x0065A080, AddRef 0x0065A860, Release 0x0065A870; the refresh the pipe's SetWatered ends in is its slot 100 (0x0065AF10), which asks the network manager about the cell and, when the piece is not yet marked, computes its box (interface slot 80) and calls the model registry;
- its Write 0x0065B140 and Read 0x0065E510 write and read its own version (3), the network occupant's whole record and then those fields.

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AA97B8 | the network occupant interface, 106 slots (81 of the network occupant's, then the intermediate class's) | the pipe's own: 0 QueryInterface 0x006484D0, 3 Init 0x00648BF0, 101 the descriptor 0x006484B0, 103 the tint 0x006488A0, 105 the default flags 0x006484C0 |
| +0x4 | 0x00AA9730 | `cISC4Occupant` | 3 OccupantInit 0x00648BC0 |
| +0x8 | 0x00AA9714 | `cIGZSerializable` | Write 0x00648800, Read 0x00648840, GetGZCLSID 0x00648F40 (shared) |
| +0xC, +0x20, +0x38 | 0x00AA96B0, 0x00AA9698, 0x00AA9694 | the property and exemplar holders, the model part's thunk | adjustor thunks |
| +0x27C | 0x00AA9648 | the virtual base (table 0x00AA9970: -4, then the displacement 0x240) | slot 17 piece key 0x00648500 behind the thunk 0x006487B0 |

## What each function does

| Function | Address | Ours | What |
| --- | --- | --- | --- |
| QueryInterface | 0x006484D0 | `slots` | the pipe IID and 0xA9764C8C: the object itself with one reference; the rest to the intermediate class (0x0065A080) |
| GetDescriptor | 0x006484B0 | `slots` | slot 101: the address 0x00B0BED0 |
| GetDefaultFlags | 0x006484C0 | `slots` | slot 105: 0x06000000 |
| GetPieceKey | 0x00648500 | `slots` | virtual base slot 17: the word at +0xFC when any of the flags 0x06000000 is set, else the class ID |
| SetWatered | 0x00648540 | `slots` | the water simulator's setter: when the byte differs from (word == 1) the word becomes 0 or 1 and the refresh (slot 100) runs with 0 |
| GetTint | 0x006488A0 | `slots` | slot 103: the three words of the record at 0x00B0BE88 indexed by the watered word (0.8 0.23 0.23 dry, 1 1 1 watered) |
| InitStatics | 0x00648A90 | `init` | once: five distances between table points as floats, and the ratios of them in the statics at 0x00B46708 |
| OccupantInit | 0x00648BC0 | `init` | the statics, and for an object not initialised the intermediate class's occupant Init and 0x0065AE80 |
| Init | 0x00648BF0 | `init` | the statics, and for an object not initialised the intermediate class's Init (handed the argument, the class ID, the descriptor), then the word zero |
| Write, Read | 0x00648800, 0x00648840 | `save` | version 3, the intermediate class's record, the watered word |
| Constructor | 0x006488F0 | `object` | the intermediate class's constructor with 0, the vtables, the word zero |
| DeletingDestructor | 0x006489F0 | `object` | the vtables, the intermediate class's destructor, the item back to its page |
| PoolFree, InitPoolPage, Allocate, Create | 0x00648970, 0x00648580, 0x006487D0, 0x006489B0 | `object` | the pool's free and page set-up, the allocation (the maker class 0x00648D10 calls it with an argument it ignores) and the factory |

## The save record

Version 3 of the pipe, then version 3 of the intermediate class, the network occupant's whole record (version 8), the intermediate class's fields and last the pipe's word:

| Part | Fields |
| --- | --- |
| pipe | u16 3 |
| intermediate | u16 3, the network occupant's record (netocc.py decodes it), bytes +0x50 and +0x51, float +0x60, word +0x17C, six vectors (count, then 0x18-byte corner records each), 16 floats (+0x1FC), two signed words (+0x23C, +0x240), three bytes, four floats (+0x24C), a float (+0x25C), words +0x264 and +0x268 |
| pipe | u32 the watered word (+0x270) |

`tools/re/save_schemas/pipeocc.py` decodes all 27,545 pipes of the 116 saves in the parity corpora (`parity/saves*`; 3,516 pipes in the 14 saves of `parity/saves` and `parity/saves-networks`), every byte of every record consumed. The network occupant's decoder wants a whole object with its own header; it is run on the record with a header made for it, in two passes (the first finds its length). In the corpus the word is 1 in every pipe of Making Money and Rush Hour and 0 in every pipe of Big City.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.pipeocc.slots` | 6 detours: QueryInterface, slots 101 and 105, the piece key, SetWatered, the tint |
| `simulators.pipeocc.init` | 3 detours: the statics, the occupant part's Init, the interface's Init |
| `simulators.pipeocc.save` | 2 detours: Write and Read |
| `simulators.pipeocc.object` | 6 detours: the constructor, the deleting destructor, the pool's free, page set-up and allocation, the factory |

`OPENSC4_PIPEOCC_SKIP` takes a list of function addresses (hex, eight digits) to leave out of a group, for bisecting.

## What is left, and why

- The intermediate class (0x00652000 to 0x0065D000), its slots 21 (IsPlaced 0x00648470) and 82 (0x00648460), which sit inside the pipe's address range but are in its vtable 0x00AAAD40 and a second class's: the code is the intermediate class's.
- The adjustor thunks (QueryInterface, AddRef and Release thunks for each part, the virtual base's 19 slots, the save and deleting destructor thunks): they jump to the functions above.
- GetGZCLSID 0x00648F40 (`mov eax, 0x49C05B9F`): it is after the class's range and in the vtables of two maker classes too.
- 0x00648C40, 0x00648CC0 (referenced from the vtable around 0x00AA7A60), 0x00648D10 (a maker class's slot 6, vtable 0x00AA9AD8: it fills a pipe from a description with the pool allocation and the constructor), 0x00648E90 and 0x00648EC0 (other classes), 0x006483C0 and 0x00648440 (the class before): not the pipe's.
- The stubs 0x00648EF0, 0x00648F00 and 0x00648F10: folded with many other classes.
- Drawing: none of the pipe's own functions draws. The tint (slot 103) is read by the renderer; it is a pure table lookup and is ours.

## Test

The fingerprint's `pipeocc` part (order 121): the number of pipes (through the occupant manager's grid, occupant type 0xC772BF98 and a class ID of 0x49C05B9F) and the sum and XOR of a hash per pipe: the watered word, the tint, whether slot 101 answers the descriptor and whether the intermediate class stored it at +0x26C, the default flags, the piece key, IsPlaced, the class ID of the serializable part and the occupant type, and for one pipe in five QueryInterface through the interface and the occupant part for the pipe IID, 0xA9764C8C, the network occupant IID, 1 and one nobody answers (the reference it gives released). Then counters (watered, placed, descriptor stored, key is the class ID or the word, tint) and `st`, a checksum of the statics at 0x00B46708. Not mixed: the intermediate class's dirty byte and other fields, pointers, reference counts.

`OPENSC4_TEST_PIPEOCC=<day>` adds ` set <checksum>,<count>`: SetWatered with the other value and then the old one on every seventh pipe (chosen by its grid position) whose word is 0 or 1 (the word and the tint read back after each), and one object made through the game's factory: QueryInterface through six parts for nine IDs, the fields the constructor sets, the tint and the answers, and both Inits on an object marked initialised (the branch that does nothing), then the last Release. Not called: the Inits of an object not initialised (they end in the occupant message 0x99EF1140).

## Parity

Run on 2026-10-12 together with the other two classes of this step ([pipeocc.md](pipeocc.md), [powerlineocc.md](powerlineocc.md), [powerpoleocc.md](powerpoleocc.md)) and the ten network occupant groups ([netocc.md](netocc.md)), the original against all of them for a week. Before the first run: `ret N` of every replaced function against our signatures, the register and EAX scans (clean), every byte of the class's records in the corpus saves decoded by the schema, and an independent reading of each function against its disassembly.

Plain: the 12 saves 11 the same on the first pair and one partly (Big City d0365: one of four runs with ours differed from the load on in the advisors' checksums only, every record identical; a run of the original did the same on another save earlier that day, see [netocc.md](netocc.md)), `saves-networks` 2 of 2, `saves-plopped` 3 of 3, `saves-ports` 3 of 3. With `OPENSC4_TEST_PIPEOCC=3`, `OPENSC4_TEST_POWERLINEOCC=3` and `OPENSC4_TEST_POWERPOLEOCC=3` together: the 12 saves 12 of 12 and `saves-networks` 2 of 2 on the first pair, the three `set` checksums the same on both sides in every save.

Ran with ours: every replaced function but the stand-alone allocation (0x006487D0, the builder's path; the factory's inline copy ran) and the pool's free slot (0x00648970). The audit found no difference.
