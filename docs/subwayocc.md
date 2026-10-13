# The subway occupant (cSC4SubwayOccupant)

One underground tile of the city's subway: one object per subway piece (16 in Rush Hour, none in Big City and Making Money). It is a network occupant ([netocc.md](netocc.md)) by way of the intermediate class of the underground pieces, which it shares with the water pipe ([pipeocc.md](pipeocc.md), [pipebase.md](pipebase.md)), with four corner records of its own: the surface the height query answers from, which the paths of the piece are laid on. The game's ID table has no name for the class; `cSC4SubwayOccupant` is ours. Build 641 addresses throughout (data/addresses/subwayocc.toml). Reimplemented like the other occupant classes ([pipeocc.md](pipeocc.md), [tunnelocc.md](tunnelocc.md)): the subway's own functions only, from the disassembly, tested through the parity harness ([parity.md](parity.md)).

## The class

Class ID 0xCA16374F, which is also the only IID the class adds (0xEA0A5ACF is another class's, the one before the pipe: GetGZCLSID 0x00648040, QueryInterface 0x00648080, factory 0x00648440). The occupant type is the network occupant's, 0xC772BF98: subway pieces are inside the network occupant test's walk (`netocc_autotest.cpp` takes every occupant of that type). 0x2D8 bytes from the pool at 0x00B0CDB8 (pages of 64 KB at address & 0xFFFF0000, 90 items each; the pool's table has the free 0x00653520 and the page set-up 0x00652E00, in two copies, 0x00AAA578 in the exe's data and 0x00AAA590, which the static initialiser 0x00A73F10 stores at start-up after it has set up the first page). Constructor 0x006530B0 (one argument, non-zero for the most derived object), factory 0x00653560 (returns the occupant part, object + 4; the class registration pushes it at 0x004606B3), deleting destructor 0x006535A0. Code 0x00652CD0 to 0x006539F5, 47 function starts: the 18 functions below, 28 adjustor thunks and one jump.

The class derives from the intermediate class of the underground pieces (vtable 0x00AAAD40, constructor 0x0065C330, destructor 0x0065C0C0; [pipebase.md](pipebase.md)), which derives from the network occupant. The subway's code calls it at the game's addresses and uses these of its fields:

- the class word at +0x264 (its Init stores the class ID it is given; slot 82 answers it), the descriptor at +0x26C (its occupant Init stores slot 101's answer), the dirty byte at +0x260 (the subway's Read sets it to 1) and the byte at +0x262 (set when the piece's geometry is made);
- the vector of four corner records at +0x1B4 (the floor; saved) and the vector at +0x1E4 (view geometry, not saved; made with the geometry by slot 87);
- slot 86 (0x0065ADD0, one argument: makes the geometry) and slot 99 (0x0065A200, one argument: for a subway piece with the key 0x4B00 it looks at the neighbours in the network and answers 0, 1 or 2; for every other object 0).

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AAA3C8 | the network occupant interface, 107 slots (81 of the network occupant's, the intermediate class's from 81 on, and one more) | the subway's own: 0 QueryInterface 0x00652D00, 3 Init 0x006537A0, 49 GetPathInfo 0x006537E0, 56 the height 0x00652D70, 101 the descriptor 0x00652CD0, 105 the default flags 0x00652CE0, 106 the corner records 0x00653130 |
| +0x4 | 0x00AAA340 | `cISC4Occupant` | 3 OccupantInit 0x00653770 |
| +0x8 | 0x00AAA324 | `cIGZSerializable` | Write 0x00653010, Read 0x00653040, GetGZCLSID 0x00653D90 (shared) |
| +0xC, +0x20, +0x38 | 0x00AAA2C0, 0x00AAA2A8, 0x00AAA2A4 | the property and exemplar holders, the model part's thunk | adjustor thunks |
| +0x2D4 | 0x00AAA258 | the virtual base (table 0x00AAA588: -4, then the displacement 0x298) | slot 17 piece key 0x00652D30 behind the thunk 0x00652FC0 |

The subway's own fields: four corner records of 0x18 bytes at +0x270 (x, y and z floats, four bytes, two more floats: the network occupant's corner layout), corner 0 at (x0, z0), corner 1 at (x0, z0 + 16), corner 2 at (x0 + 16, z0 + 16) and corner 3 at (x0 + 16, z0); and the virtual base's displacement word at +0x2D0 (zero). The constructor leaves the corner records as the pool's memory holds them: slot 106 makes them, and only GetPathInfo calls it.

## What each function does

| Function | Address | Ours | What |
| --- | --- | --- | --- |
| QueryInterface | 0x00652D00 | `slots` | the subway IID: the object itself with one reference; the rest to the intermediate class (0x0065A080) |
| GetDescriptor | 0x00652CD0 | `slots` | slot 101: the address 0x00B0CDC8 |
| GetDefaultFlags | 0x00652CE0 | `slots` | slot 105: 0x07000000 |
| GetPieceKey | 0x00652D30 | `slots` | virtual base slot 17: the word at +0xFC when any of the flags 0x06000000 is set, else the class ID |
| GetAltitude | 0x00652D70 | `slots` | slot 56: the height at a point (x, z) on the surface of the four corner records, less 0.5. With u = x - x0 and v = z - z0: when u is not greater than v, h0 + (h1 - h0) v / 16 + (h2 - h1) u / 16; otherwise (and when one of them is not a number) h0 + (h3 - h0) u / 16 + (h2 - h3) v / 16 |
| UpdateCorners | 0x00653130 | `paths` | slot 106: the corner records. A piece whose key (+0xFC) is neither 0x300 nor 0x4B00 gets the four records of the vector at +0x1B4. For those two keys slot 99 is asked; when the vector at +0x1E4 has fewer than eight records the same four are taken; otherwise its records 4, 5, 7 and 6 go to the corners r, r + 1, r + 2 and r + 3 (modulo 4; r is 4 less the rotation), and when slot 99 answered 0 each of those heights is lowered by twice a difference of heights in that vector (record 4 less 0, 5 less 1, 7 less 11, 6 less 10) |
| GetPathInfo | 0x006537E0 | `paths` | slot 49: the path information (+0x140), made when there is none: slot 86 when the geometry is not made, slot 106, the path resource {0x296678F7, 0xA966883F, the piece ID with 0x06000000 or the path ID override}, its slot 0x64 for this piece; then every point of every path gets the height slot 56 answers as its y and every path its length (the sum of the distances between its points); then the path connection table is cleared and filled again as the network occupant does |
| InitStatics | 0x00653640 | `init` | once: five distances between table points as floats, and the ratios of them in the statics at 0x00B467F8 (the pipe's code with other tables) |
| OccupantInit | 0x00653770 | `init` | the statics, and for an object not initialised the intermediate class's occupant Init and 0x0065AE80 |
| Init | 0x006537A0 | `init` | the statics, and for an object not initialised the intermediate class's Init (handed the argument, the class ID, the descriptor) |
| Write, Read | 0x00653010, 0x00653040 | `init` | version 2 and the intermediate class's record; Read takes version 2 only and sets the dirty byte when the record was good |
| Constructor | 0x006530B0 | `object` | the intermediate class's constructor with 0, the vtables |
| DeletingDestructor | 0x006535A0 | `object` | the vtables, the intermediate class's destructor, the item back to its page (released at 0 used, opened again at 0x40000043) |
| PoolFree, InitPoolPage, Allocate, Create | 0x00653520, 0x00652E00, 0x00652FE0, 0x00653560 | `object` | the pool's free and page set-up, the allocation (the maker class 0x00653A00 calls it with a size it ignores, then the constructor) and the factory |

The floats: the game computes on the x87 at 53-bit precision, so the heights, distances and ratios are worked out in double in the game's operand order and stored as floats where the game stores floats; the height is returned in ST0.

## The save record

Version 2 of the subway, then version 3 of the intermediate class, the network occupant's whole record (version 8) and the intermediate class's fields. The subway saves no field of its own: the corner records are made again with the path information.

| Part | Fields |
| --- | --- |
| subway | u16 2 |
| intermediate | u16 3, the network occupant's record (netocc.py decodes it), bytes +0x50 and +0x51, float +0x60, word +0x17C, six vectors (count, then 0x18-byte corner records each), 16 floats (+0x1FC), two signed words (+0x23C, +0x240), three bytes, four floats (+0x24C), a float (+0x25C), words +0x264 and +0x268 |

`tools/re/save_schemas/subwayocc.py` decodes every subway piece of the corpus saves with every byte consumed: 64 objects in `parity/saves` (the four Rush Hour saves of the 12, 16 each), 16 in `parity/saves-networks` (t0000), none in `parity/saves-plopped` (Big City only); `parity/saves-ports` has 16 more (q0000). Each of those saves holds one record of 11,328 bytes: 14 objects of 696 bytes with the piece key 0x4B00 and two side walls, and 2 of 792 bytes, the ends of the line, with the key 0x300 and three side walls. The pieces are one straight line in the cells x 7, z 99 to 114 of the network's grid. In all of them the class word is 0xCA16374F, the network flags are 0x10002080 (so the piece key the virtual base answers is the class ID), the rotation is 0 or 2 and the saved resource key of the path information is zero: no piece had path information when the saves were made.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.subwayocc.slots` | 5 detours: QueryInterface, slots 101 and 105, the piece key, the height |
| `simulators.subwayocc.paths` | 2 detours: the corner records (slot 106) and GetPathInfo |
| `simulators.subwayocc.init` | 5 detours: the statics, the occupant part's Init, the interface's Init, Write and Read |
| `simulators.subwayocc.object` | 6 detours: the constructor, the deleting destructor, the pool's free, page set-up and allocation, the factory |

`OPENSC4_SUBWAYOCC_SKIP` takes a list of function addresses (hex, eight digits) to leave out of a group, for bisecting. The factory calls the constructor, and the Inits the statics, at the game's addresses, so a skipped one is the game's there too. Each function logs its first call (`subwayocc: first <name>`).

## What is left, and why

- The intermediate class (0x00659B00 to 0x0065E800 and 0x00648460, 0x00648470, 0x00657730), which has its own document; its slot 4 is reached through the jump 0x00652CF0, which the pipe's vtable has too.
- The adjustor thunks 0x00652E40 to 0x00652FD0 (QueryInterface, AddRef and Release thunks for each part, the virtual base's 19 slots, the deleting destructor thunks) and the save thunks 0x00653090 and 0x006530A0: they jump to the functions above or to the bases'.
- GetGZCLSID 0x00653D90 (`mov eax, 0xCA16374F`): it is after the class's range, in the vtables of two other classes too (0x00AAA188 slot 38, 0x00AAA5C0 slot 81) and called by the class registration (0x004606B8).
- Not the subway's: 0x00652BD0 (slot 44 of the 0x250-byte class's vtable 0x00AAA188), 0x00652C30 and 0x00652CB0 (that class's constructor and factory), 0x00653A00 (slot 82 of the maker class, vtable 0x00AAA5C0: it fills a subway piece from a description with the pool allocation and the constructor), 0x00653B80, 0x00653C00 and 0x00653CC0 (the table at 0x00AA7A94), 0x00653D60 (the maker class's constructor), 0x00653DA0 (an exit function) and 0x00653DC0 (three other classes' vtables).
- Drawing: none of the subway's own functions draws, loads a texture or builds a model. GetPathInfo calls slot 86, which does (it is the intermediate class's); slot 106 and the height only compute the surface the paths are laid on.

## Test

The fingerprint's `subwayocc` part (order 130): the number of subway pieces (through the occupant manager's grid, occupant type 0xC772BF98 and a class ID of 0xCA16374F) and the sum and XOR of a hash per piece: the piece key word, the class word (slot 82), the rotation and the flip, whether slot 101 answers the descriptor and whether the intermediate class stored it at +0x26C, the default flags, slot 102, the virtual base's piece key, IsPlaced, the class ID of the serializable part, the occupant type, the saved vector at +0x1B4 (its count and records) and QueryInterface through the interface and the occupant part for the subway IID, the intermediate class's, the network occupant's, 0xA9764C8C, 1, the pipe's, 0xEA0A5ACF and one nobody answers (each reference released). Then counters (`k300`, `k4b`: pieces with those keys; placed; descriptor stored; `keyc`, `keyo`: the piece key is the class ID or the word) and `st`, a checksum of the statics at 0x00B467F8. Not mixed, because they follow the view: the corner records, whether a piece has path information, the geometry byte and the unsaved vector; and pointers and reference counts. The height (slot 56) is asked daily at eight points of every piece by the network occupant's part. On a city without a subway the part is `subwayocc 0,00000000,00000000 k300 0 k4b 0 placed 0 stored 0 keyc 0 keyo 0 st ...`.

`OPENSC4_TEST_SUBWAYOCC=<day>` adds ` set <made>,<city>,<count>` and one log line (`autotest: subwayocc: ...`), also when the city has no subway:

- every subway piece of the city, in the order of its cell: the virtual base's piece key with the flags 0x06000000 cleared and with 0x02000000 set (the flags word written and put back); then GetPathInfo twice. A piece that has no path information gets it made (slot 86 for the geometry, slot 106, the resource, the heights and lengths); the corner records, the height at ten points, every path's key, points and length, how many points hold the height slot 56 answers for them and the path connection table are mixed; slot 106 is called once more and the corner records mixed again. Then the table is put back and, for a piece that had none, the path information dropped, the corner records put back and the flag 0x2000 cleared if it was clear. What slot 86 did to the piece stays (its geometry is made, as the game does the first time a loaded piece is asked for its path information). `OPENSC4_TEST_SUBWAYOCC_PATHS=0` leaves the path part out;
- made objects, which are in no cell: one from the factory, QueryInterface through six parts for twelve IDs, GetGZCLSID, the fields the constructors set; the piece key both ways; the height on a tile of the test's own, in both triangles, on the diagonal and outside; slot 106 with the two vectors pointed at tables of the test's own (a plain key; the key 0x300 with the second vector empty; twelve records with the rotations 0 to 3, the flip bit and both keys: turned and lowered), the vectors put back; GetPathInfo with a pointer in place (it answers it); both Inits on the object marked initialised; Read's refusal of versions 1 and 3 and of an empty stream, of a record whose intermediate version is 4 and of one that ends after the subway's version, through the serializable part and the holder's thunk, with a stream of the test's own (as `save_autotest.cpp`'s); a second object made as the maker class does (the allocation, then the constructor) and released; then 90 more objects from the factory, which makes the pool take a second page (the page set-up), all released again (`OPENSC4_TEST_SUBWAYOCC_POOL=0` leaves this part out); then the last Release.

Reached by the test on any city: QueryInterface, slots 101 and 105, the piece key (both branches), the height (both triangles), slot 106 (the plain copy, the copy for a short second vector, the turned and lowered branch), GetPathInfo (only the answer of information that is there), the statics, both Inits (the initialised branch), Read (the refusals and the call of the intermediate class's Read that refuses), the constructor, the deleting destructor (each page count branch), the page set-up, the allocation and the factory. Only on a city with subway pieces (Rush Hour): GetPathInfo's making of the information, slot 106 on a piece in the network (with slot 99's other answers, so also the turned branch that is not lowered when a piece has it), and, by the game itself, the factory, Read of a whole record and the occupant part's Init at load, Write at save and the deleting destructor on leaving the city. Not reached: the Inits of an object not initialised (they end in the occupant message 0x99EF1140, whose listeners would meet an occupant that is in no cell; the game runs the occupant part's at load), the pool's free slot 0x00653520 (nothing in the exe calls it by address; the deleting destructor has its own copy), and Write and Read of a whole record on a made object: no autotest makes the game's memory stream, and the record holds the property holder's, the network occupant's and the intermediate class's parts, whose Reads end in the model's state.

## Parity

Run on 2026-10-13 (batch b1) together with its parent class ([pipebase.md](pipebase.md)) and every network occupant group of the earlier steps ([netocc.md](netocc.md), [pipeocc.md](pipeocc.md), [powerlineocc.md](powerlineocc.md), [powerpoleocc.md](powerpoleocc.md), [prebuiltocc.md](prebuiltocc.md), [bridgeocc.md](bridgeocc.md), [tunnelocc.md](tunnelocc.md)), the original against all of them for a week, with `determinism.dispatch_info` in the base list. Before the first run: `ret N` of all 55 replaced functions of the two classes against our signatures (no mismatch), the caller register scan and the EAX scan (no hit), and a second writer's reading of every function of the first writer's draft against the disassembly (no difference in behaviour), and every subway record of the corpus decoded with every byte consumed (16 pieces in each Rush Hour save: 64 in the 12 saves, 16 in `saves-networks`, 16 in `saves-ports`, none in `saves-plopped`).

Plain: the 12 saves 12 of 12 the same on the first pair, `saves-networks` 3 of 3 (n0000, n0090, t0000), `saves-plopped` 3 of 3, `saves-ports` 3 of 3. With `OPENSC4_TEST_PIPEBASE=3` and `OPENSC4_TEST_SUBWAYOCC=3` (and the three switches of the step before) together: the 12 saves 12 of 12 and `saves-networks` 3 of 3 on the first pair, the parts and their `set` checksums the same on both sides in every save. On every Rush Hour save the part reads `subwayocc 16,D6C44FA4,4473EA08 k300 2 k4b 14 placed 16 stored 16 keyc 16 keyo 0 st 663C4868`, with `set A22C8071,9611B852,16` after the drive on t0000. All 16 pieces already had path information when the drive ran, so the drive made none.

Ran with ours (first-call log lines over the whole batch): 17 of the 18 replaced functions; the one that did not is the pool's free function 0x00653520, which nothing calls by address (only the two pool tables point at it). The factory, Read and OccupantInit ran at each Rush Hour load and Write at each save.
