# Saved cities (P4)

A saved city (`.sc4`) is a DBPF package ([resources.md](resources.md), "Package format"). Each record holds one serialised object; the record's type is the object's class ID, so the exe's own class-name table names it (for example `0xC9BD5D4A` is `cSC4Lot`, `0x8990C065` `cSC4PollutionSimulator`). A save written by the game holds about 150 records.

Tools: `opensc4-dbpf list|diff` for packages, [tools/re/savediff.py](../tools/re/savediff.py) for comparing two saves around moved addresses, and [tools/re/sc4save.py](../tools/re/sc4save.py), which decodes records whose layout is known (`sc4save.py <city.sc4> [type]`; one decoder module per simulator in [tools/re/save_schemas/](../tools/re/save_schemas/__init__.py)) and checks a schema against any number of saves (`sc4save.py --check <files or folders>`: every record must be consumed to the byte).

## Record header and references

Every record starts with a header from the save framework, then the class's own fields as its `cIGZSerializable::Write` produces them:

| Bytes | Field |
| --- | --- |
| 4 | record size in bytes, header included |
| 4 | CRC of the record |
| 4 | the object's address in memory when it was saved: its ID in this save |
| ... | the class's fields, usually starting with a 16-bit version |

Objects embedded in a record repeat this header. **References** between objects are written as 8 bytes: the target's address-ID and its class ID (`0x005D99A0`; zeros for none); lists of references as a 32-bit count followed by the references (`0x005DAFF0`). The referenced objects are saved as records of their own. Because IDs are memory addresses, two saves of the same city are never byte-identical ([experiments/determinism.md](experiments/determinism.md)).

Stream types follow `cIGZIStream`/`cIGZOStream` (little-endian; strings are a 32-bit length and the characters).

## The record streams (0x005D9430 to 0x005DBC10)

The library every serialisable class writes and reads its record with. `saves.streams.part1` to `part3` replace it ([save_streams_1.cpp](../src/core/save_streams_1.cpp) to `_3.cpp`, off by default): 86 functions. It is the first piece of the save and load rewrite.

**Four holders**, 0x10 bytes each, built on the caller's stack: +0 vtable, +4 the segment, +8 the record or stream, +0xC a 16-bit version (0xFFFF until something is open).

- The segment input and output streams (vtables 0x00AA2B0C and 0x00AA2B10) open one record of a save file's segment (`cISC4DBSegment`, interface 0x499899DA) by its key; the output stream takes the record's version and writes it first, the input stream reads it into +0xC. What the entries call `Close` (0x005D9C30, 0x005D9D50) are their destructors.
- The record reader and writer (vtables 0x00AA2AF4 and 0x00AA2B00) do the same on a plain `cIGZPersistDBSegment` and its serial record; only the application uses them.

**References.** `WriteReference` (0x005D99A0) asks the stream for the segment output stream interface (0x6999A8FB) and hands it the object's serialisable part (its slot 0x68); `ReadReference` (0x005D9730) does the reverse through 0x4999A8F5 with the interface wanted. On any other stream both answer false and move nothing. A reference list is a 32-bit count and the references; the reader takes up to 10,000,000 and appends a node for a reference that did not resolve too.

**Typed helpers** are all `cdecl (stream, value)` and return the stream, not a bool (several older address entries say bool; no caller reads the result). They never look at a stream call's result: every call is made whatever failed before.

| Value | Bytes in the record |
| --- | --- |
| bool | 1 |
| rectangle | four int32 |
| resource key | type, group, instance |
| date | the day number, uint32 |
| stopwatch | mode byte, elapsed uint64, running byte |
| random generator | two uint32, an int32, a float64 |
| the two cell iterators | 0x20 bytes of state field by field |
| vectors of 2 and 3 floats, box | 2, 3 and 6 float32 |
| transform | a flags byte, then only the parts it names: 9 floats of rotation (4), 3 of translation (1), the scale (2). An older form (0x005DA930, 0x005DAA80) always has all three and ends in a uint32 |
| tract walk | 8 fields, a count and that many uint16, three int32 |
| vectors of float, uint32, byte | a uint32 count and the values |
| byte rows, bit rows | row count, the first row's length, then the bytes (bit rows: one byte per bit) |
| curve | a count and the points as float pairs |

**Limits and oddities, all kept.**

- The vector readers refuse a count of 1,000,000 or more (the vector stays as it was), the row readers a row count or length of 1,000,000 or more. The tract walk and the curve have no limit.
- The row writers (0x005DADE0, 0x005DAC40) write as many values of each row as there are rows, whatever the row's length; the readers read length-many. The two only agree for square grids, which is all the game has. A city that is not square would not save its weather grids right ([city-size.md](city-size.md)).
- Readers keep their temporaries in argument slots, so a read that fails can leave a pointer's bytes in the value (the date, the generator's third field, a transform's flags and scale, a vector's count). The byte vector's reader starts its count from whatever the caller had in ECX; ours enters through a small assembly stub to pass that on.
- The float vector's reader moves each value through the x87, which quiets a signalling NaN; ours sets the bit by hand.

**Left the game's:** three container helpers shared with code that is not save code (the bit copy 0x005DAAE0, the bit row's copy constructor 0x005DB110, the bit row vector's destroy 0x005DB590) and a vector resize only the framework calls (0x005DB490).

**Checked** (2026-10-11). `OPENSC4_TEST_SAVE_STREAMS=<day>` ([save_autotest.cpp](../src/core/save_autotest.cpp), token `svstreams`) feeds every helper that works on a plain stream fixed data through streams of the test's own: the bytes each writer produces and the memory each reader leaves, whole, cut short and over the limit, 29 steps. And every parity run loads a city through the readers and saves it through the writers. Twelve saves, seven days, full saves, the original against all three groups: the test's token equal in all 120 runs; eight saves the same on the first or second pair, four that part on days and in ways the original parts from itself (the first pair's second run, or a save whose original runs give three outcomes in four); the 63 record types the tools decode identical wherever the days agree. A whole-file comparison around moved addresses (`savediff.py --pointers`) shows ours against the original differing in the records two runs of the original differ in (the traffic network map, the two pictures and two more frame-driven records on a week of Making Money). A fault planted in one writer (a box written as five floats) changed the token.

## The serializer and the segment streams (0x005D1520 to 0x005D4AB0)

What stands between an object's `Read` and `Write` and the file. `saves.serializer.part1` to `part3` replace it ([save_serializer_1.cpp](../src/core/save_serializer_1.cpp) to `_3.cpp`, off by default): 102 functions, the second piece of the rewrite. The framework's packed file underneath (0x0096Dxxx to 0x00972xxx) is ours on its read side (`resources.packed_file.*`, [resources.md](resources.md)); its write side (index and directory writing, compression) is still the game's.

**Three objects.**

- **The save segment** (`cSC4DBSegmentPackedFile`, 0x1DC bytes, constructor 0x005D3740): +0 the `cISC4DBSegment` interface (vtable 0x00AA2654, 14 slots), +4 and +8 the framework's packed file with its two vtables, +0x1CC the serializer, +0x1D0 a message target for progress, +0x1D8 the percentage done. Opening it (0x005D2110) opens the packed file and starts the serializer in mode 1 (read) or 2 (write); closing it (0x005D2170) saves every object, stops the serializer and closes the file, and returns false when the save step failed although the file is closed all the same. Its destructor runs the same save on a segment still open. Progress only rises, and a message (0xC9E30725) goes out at each new even percent.
- **The segment stream** (0x1C bytes; input vtable 0x00AA2280, interface 0x4999A8F5; output 0x00AA2310, 0x6999A8FB): what `Read` and `Write` get. A "good" byte, the record, the segment. Every typed call forwards to the record while the stream is good and makes the stream bad when the record refuses; after that nothing reaches the record. `GetError` is "not good". Slot 0x68 reads or writes a reference through the serializer, slot 0x70 a variant. The output stream's Open (0x005D2410, the entry named `cSC4COMSerializer::StreamOpen`) takes the existing record or makes a new one and seeks to its start or its end; it never cuts a record short. The output stream's `SetUint8` always answers false, though it writes the byte.
- **The serializer** (`cSC4COMSerializer`, 0x44 bytes, its interface 0x699899EA at +8 with vtable 0x00AA268C): a map from class ID to a map from object ID to object, the mode, a count of objects, and a buffer for checksums.

**Saving** (`SaveClassObjects`, 0x005D3DE0). Nothing is written when an object's `Write` runs first: `WriteReference` (0x005D4810) writes the target's address as its ID and its class ID (a null reference is one zero dword) and registers the target under its class (0x005D43F0). The first time a class is registered, its record in the file is deleted: that is how the old contents go. At the end the serializer walks a copy of the class map until every registered object has been written, objects registered during the walk included. Per class it opens the class's record to append; per object it writes two zero dwords, the ID, the object's `Write`, then goes back to fill in the size and a CRC-32 of at most 250,000 bytes of the object (0x00932FCF). A `Write` that fails or throws puts the record back to the object's start and the save goes on. Last comes the index record: version 1, the class count, the object count, then each class ID with its count.

**Loading** (`LoadClassObjects`, 0x005D38D0) reads the index, then each class's record: per object the size, the checksum, the ID; the object is found or made through the class factory (0x005D2DD0) and its `Read` runs. `ReadReference` (0x005D4880) reads an ID and class ID and hands back the object of that ID, making it if it is not there yet, so objects exist before their own record is read.

What goes wrong, and what the original does about it, all kept:

| Case | Outcome |
| --- | --- |
| A class's record does not open | the class is skipped, the load does not fail |
| A count of 10,000,000 or more, or a size beyond the record | the load fails for that class and the rest of its record is not read |
| Checksum mismatch | the same: the object and every later object of that class are lost |
| `Read` throws, or crashes | the object stays as far as it was read, the rest of the class's record is not read |
| A class nothing can make | that object is stepped over |
| An ID seen twice | `Read` runs again on the same object |
| `Read` consumed more or less than the size | the position is set right, silently |
| Fewer objects made than the index says | the load fails |

The "throws, or crashes" row is a property of the original's compiler: its catch of everything also takes an access violation. Ours runs `Read` and `Write` inside a structured handler for the same effect.

**Limits for bigger cities** ([city-size.md](city-size.md)): an object of more than 250,000 bytes is saved with a checksum of its first 250,000 only, and loads; a class of 10,000,000 objects or more does not load.

**Left the game's:** the tree iterator and two map inserts shared with the ordinance code, three folded getters and thunks, and four functions at the end of the range that are not save code.

With `diagnostics.prop_pox` on, which watches `StreamOpen` and `LoadClassObjects` with detours of its own, those two stay the game's.

**Checked** (2026-10-11). All six `saves.*` groups on against the original. Twelve saves, seven days, full saves: every city loads through our loader and saves through our saver; eight saves the same, four that part where the original parts from itself (in each the original alone gives two or three outcomes); the stream test's token equal in all 120 runs. A whole-file comparison of a week of Making Money shows ours differing from the original in the five records two runs of the original differ in, and in nothing else. Five saves damaged on purpose (scratch tool: a class's record stored uncompressed, then one object's checksum or size spoiled, among the props, the lots, the buildings and one more class): the original and ours lose the same objects and run the same for three days; the spoiled size among the lots leaves 200 of 433 lots on both sides. Nine saves written by ours load in the original and run as they do with ours (six the same, three partly through the first pair's second run, which a control with the original on both sides shows too). Not reached by any test: a `Read` or `Write` that throws.

## Saving and loading a city (cSC4App)

`cSC4App::SaveCity` (0x00455E20, interface slot 15) and `LoadCity` (0x004555F0, slot 13) with their helpers: the third piece. `saves.app` puts ours in place of the two ([save_app_1.cpp](../src/core/save_app_1.cpp), [save_app_2.cpp](../src/core/save_app_2.cpp)); `saves.app.part1` and `part2` replace the helpers: `SaveCity(fast)` (0x00454DA0, slot 16, which asks the city for its path and calls slot 15), the application's record written (0x004545D0) and read (0x00454620), and the city's file taken out of the resource manager and closed (0x00454A10) or opened and handed to it (0x00454B20). All off by default.

Other features wrap the two functions with detours of their own (`game.timelapse`, `game.seed`, `diagnostics.trace.loadcity`), and one address takes one detour. They reach ours through [save_app.h](../src/core/save_app.h): a wrapper hands over the pointer it would have called, and ours runs when `saves.app` is on. `saves.app` installs after them and puts its own detour only on an address none of them holds.

**Saving**, in order:

1. Four guards (a scope, a timer, modal input, system keys). No city: false.
2. The real path is made from the region's folder, the city's name and its serial number, whatever path was given. **The path argument is never where the file goes**; it only decides whether an old file there is deleted afterwards. The city is told the real path at once.
3. A temporary file name. A new save segment on it, opened for writing.
4. Messages 0x26C63343 and 0x26C63346 (segment, temporary name, real name, fast).
5. The version record, the game version record (the exe's four version numbers), then the application's record (type 0x49DD6F4C, group 0x49DD6F4E, instance 1): version 2, the city by reference, and the regional city's record. Writing the city's reference is what registers the city with the serializer, and from it everything the city refers to.
6. Messages 0x26C63344 and 0x26C63345 (the one auto save's copies and the date sync listen to).
7. The segment is asked to save every registered object, then closed.
8. The city's own open file is closed, the files are moved (below), the city's file is opened again and given back to the resource manager.
9. After a good save the neighbours' regional cities are written into their own files.

**The file moves.** T is the temporary file, R the real path, B is R with an underscore added, G the path given.

| Before | Steps | After | Result |
| --- | --- | --- | --- |
| no R | T to R | the new save at R | true |
| no R, the move fails | T removed | nothing at R | false |
| R there | an old B removed, R to B, T to R, B removed | the new save at R, no B | true |
| R there, an old B cannot be removed, or R cannot be moved | T removed | the old save at R | false |
| R there, T cannot be moved to R | T removed, B moved back to R | the old save at R if the move back works, else only at B | false |
| the segment did not write | T removed | untouched | false |

When G differs from R and a file is there, it is removed after the first, second, third and fifth case, the two failures among them: after a failed last move with a failed move back, the old city is only in B.

**Loading**: a city that is open is closed; a new segment on the file, opened for reading; the version must be 0x3E to 0x41; message 0x26C63340; the segment is given to the resource manager; every object is loaded; the application's record is read (version 2, the city by reference, the regional city, whose failure is not the load's failure); the city takes the regional city's serial number and the path; the screen is changed to the city's; messages 0x26C63341 and 0x26C63342; the city is initialised between the "loaded" messages 0x26D31EC0 and 0x26D31EC1 (the one the date sync listens to). A failed load sends no closing message, releases the city and closes the file. On every path the application's two caches are trimmed and the resource manager collects garbage.

**Checked** (2026-10-11). Every `saves.*` group on against the original. Twelve saves, seven days, full saves: every run loads through our LoadCity and saves through our SaveCity (the third row of the table: the city's file is there); eleven saves the same, one that parts on the first pair's second run on the first day, where that save parts with the original alone. A run of 200 days with `game.autosave`, `game.date_sync`, `game.timelapse` and `game.seed` on as well: ours runs inside the wrappers' detours, the auto saves go through ours and are copied, the date sync hears each save, the timelapse copies each file, and the city loads with its seed. Not reached: the failing rows of the table (a file that cannot be moved or removed) and a load that fails.

## cSC4PollutionSimulator (0x8990C065)

`Write` 0x006E3380 (writes version 4), `Read` 0x006E5270 (reads versions 2 to 4), `GetGZCLSID` 0x006E2CC0; the `cIGZSerializable` vtable is 0x00AB25E8, at object offset +0xC. Field names are ours except where a getter confirms them; offsets are the object's. (`sc4save.py` names fields by `Write`'s offsets, 0xC lower, so `avg_water` there is +0x104 here.)

| Version | Type | Object offset | Meaning |
| --- | --- | --- | --- |
| all | uint16 | | version |
| all | 4 references | +0x40 | grid objects (class 0x49B9E605 and others) |
| all | 4 references | +0x50 | grid objects |
| all | uint32 x2 | +0x60, +0x64 | the garbage simulation's tract cursor (x, z) |
| all | 4 reference lists | +0x68 to +0x74 | |
| all | uint32 | +0xFC | |
| all | uint32 | **+0x100** | **average air pollution** (air getter; 272 in the test city, as the game reports) |
| all | uint32 | **+0x104** | **average water pollution** (`GetAverageWaterValue`, 0x006E3010) |
| all | int32 x6 | +0x108 to +0x11C | |
| all | uint32 | +0x120 | |
| 2 | string | | read and dropped |
| all | uint16 | **+0x128** | **average garbage** (`GetAverageGarbageValue`, 0x006E3020) |
| all | uint32 x2, float, uint8, uint32 | +0x12C to +0x13C | monthly incinerator and waste-to-energy capacity, recycling center effect (float), garbage uncollected, total uncollected ([pollution.md](pollution.md)) |
| all | int64 x2 | +0x1C0, +0x1C8 | |
| 4 | uint32 | +0x1B8 | garbage abducted the previous month (`GetGarbageAbductedPrevMonth`, 0x006E4F20, returned zero-extended to 64 bits) |
| all | int64 x3, double, int64 x3 | +0x178 to +0x1AC | **last month's garbage**: produced, imported, exported (+0x178, +0x180, +0x188), recycled (double, +0x190), incinerated, converted to energy, landfilled (+0x198, +0x1A0, +0x1A8); the `Get...LastMonth` getters 0x006E4F30 to 0x006E4F90 return these as 64-bit values (the recycled one through `__ftol`) |
| all | int64 x3, double, int64 x3 | +0x140 to +0x174 | the same seven figures for the month in progress: the MonthPassed handler (0x006E7B60) copies them to +0x178 and clears them; `GarbagePutInLandfill` adds to +0x170 ([pollution.md](pollution.md)) |
| all | uint32, int32, uint32, uint32 | +0x1B4, +0x1B0, +0x1BC, +0x1D4 | +0x1BC active landfill tiles; +0x1D4 garbage tracts walked per call |
| all | struct (0x005DAB70) | +0x1D8 | int8, int8, uint8, int32 x5, count + uint16 values, int32 x3 |
| all | uint8 x4 | +0x208 | radiation, air, water, garbage changed (set by the setters) |
| 2 | uint32 | | read and dropped |
| all | float, int32 x2 | +0x228 to +0x230 | capacity scale, garbage supply, garbage capacity |
| all | reference | +0xD8 | |

Checked on 2026-10-06: the decoder consumes every pollution record exactly in 531 saves (the test region, the P4 test runs, and the six regions that ship with the game): 28 of version 4, 5 of version 3 and 498 of version 2 (cities the game has never re-saved). The three averages match what the live simulator reported for the same save.

## cSC4CrimeSimulator (0x8990C09A)

Version 6, `Write` 0x0069F600, `Read` 0x006A05E0, the `cIGZSerializable` part at +0xC. Layout in [crime.md](crime.md), "Save record"; decoded by `sc4save.py` (all 17 corpus saves).

## cSC4TutorialSystem (0xAA3836FB)

23 bytes in the tutorial cities' saves: the record header, uint16 version (2), uint8 active (1), uint32 (0), uint32 tutorial ID (Big City 0xCC02D3A3, Making Money 0x0BFDB71B, Rush Hour 0x2C12D0E9). With the active byte set to 0 the city loads as an ordinary city, without the tutorial's dialogs and locked tools; the loader does not reject the record although its CRC no longer matches.

## cSC4DemandSimulator (0x2990BFF2) and cSC4Demand (0x8990C12D)

Version 2 each, `Write` 0x006A2680 and 0x005D5080. Layouts in [demand.md](demand.md), "Save records"; decoded by `sc4save.py`. All of a city's cSC4Demand objects are in one entry, in an order that differs from run to run, so they are compared in ID order.

