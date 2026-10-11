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

The library every serialisable class writes and reads its record with. `saves.streams.part1` to `part3` replace it ([save_streams_1.cpp](../src/core/save_streams_1.cpp) to `_3.cpp`, off by default): 86 functions. It is the first piece of the save and load rewrite; the serializer, the save and load themselves and the file container are still the game's.

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

