# Saved cities (P4)

A saved city (`.sc4`) is a DBPF package ([resources.md](resources.md), "Package format"). Each record holds one serialised object; the record's type is the object's class ID, so the exe's own class-name table names it (for example `0xC9BD5D4A` is `cSC4Lot`, `0x8990C065` `cSC4PollutionSimulator`). A save written by the game holds about 150 records.

Tools: `opensc4-dbpf list|diff` for packages, [tools/re/savediff.py](../tools/re/savediff.py) for comparing two saves around moved addresses, and [tools/re/sc4save.py](../tools/re/sc4save.py), which decodes records whose layout is known (`sc4save.py <city.sc4> [type]`) and checks a schema against any number of saves (`sc4save.py --check <files or folders>`: every record must be consumed to the byte).

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

## cSC4PollutionSimulator (0x8990C065)

`Write` 0x006E3380 (writes version 4), `Read` 0x006E5270 (reads versions 2 to 4), `GetGZCLSID` 0x006E2CC0; the `cIGZSerializable` vtable is 0x00AB25E8, at object offset +0xC. Field names are ours except where a getter confirms them; offsets are the object's. (`sc4save.py` names fields by `Write`'s offsets, 0xC lower, so `avg_water` there is +0x104 here.)

| Version | Type | Object offset | Meaning |
| --- | --- | --- | --- |
| all | uint16 | | version |
| all | 4 references | +0x40 | grid objects (class 0x49B9E605 and others) |
| all | 4 references | +0x50 | grid objects |
| all | uint32 x2 | +0x60, +0x64 | |
| all | 4 reference lists | +0x68 to +0x74 | |
| all | uint32 | +0xFC | |
| all | uint32 | **+0x100** | **average air pollution** (air getter; 272 in the test city, as the game reports) |
| all | uint32 | **+0x104** | **average water pollution** (`GetAverageWaterValue`, 0x006E3010) |
| all | int32 x6 | +0x108 to +0x11C | |
| all | uint32 | +0x120 | |
| 2 | string | | read and dropped |
| all | uint16 | **+0x128** | **average garbage** (`GetAverageGarbageValue`, 0x006E3020) |
| all | uint32 x2, float, uint8, uint32 | +0x12C to +0x13C | |
| all | int64 x2 | +0x1C0, +0x1C8 | |
| 4 | uint32 | +0x1B8 | |
| all | int64 x3, double, int64 x3, int64 x3, double, int64 x3 | +0x178 to +0x1AC, +0x140 to +0x174 | 64-bit values, meaning not known yet |
| all | uint32, int32, uint32, uint32 | +0x1B4, +0x1B0, +0x1BC, +0x1D4 | |
| all | struct (0x005DAB70) | +0x1D8 | int8, int8, uint8, int32 x5, count + uint16 values, int32 x3 |
| all | uint8 x4 | +0x208 | flags |
| 2 | uint32 | | read and dropped |
| all | float, int32 x2 | +0x228 to +0x230 | |
| all | reference | +0xD8 | |

Checked on 2026-10-06: the decoder consumes every pollution record exactly in 531 saves (the test region, the P4 test runs, and the six regions that ship with the game): 28 of version 4, 5 of version 3 and 498 of version 2 (cities the game has never re-saved). The three averages match what the live simulator reported for the same save.
