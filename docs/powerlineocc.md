# The power line occupant (cSC4PowerLineOccupant)

The wire between two power poles: one object per pair of poles that the player has joined (19 in Making Money, 26 in Rush Hour, none in Big City). It holds the two poles it joins (referenced, with a flag byte), a position (three floats) and the cell of that position, and a preview byte the power line tool sets while the wire is still being placed. The power network's builder makes one for each pair (the function around 0x00651CD4 calls the allocator 0x0064A2E0 and the constructor 0x0064A270 and sets the poles with slot 4); the wire holds no model and draws nothing itself. It sits on top of the occupant base class ([occbase.md](occbase.md)) and derives from it directly, not from the network occupant ([netocc.md](netocc.md)). Reimplemented like the other occupant classes ([foundocc.md](foundocc.md)): one writer, an independent audit against the disassembly, the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/powerlineocc.toml).

## The class

Class ID 0xC9C05C5D, interface IID 0xC98F3B24 (the class ID and 0xA9764C8C are answered by the same object), occupant type 0x0990B2EA. 0x58 bytes from a pool (0x00B0C258; allocator 0x0064A2E0, 8 KB pages). Constructor 0x0064A270, factory 0x0064AB70 (a tail jump to the constructor: it returns the object, not the occupant part), deleting destructor 0x0064ABF0. No class derives from it.

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AA9C80 | `cISC4PowerLineOccupant` | 8: QueryInterface, AddRef, Release, GetConnectedPoles, SetConnectedPoles, GetCell, IsPreview, SetPreview |
| +0x4 | 0x00AA9BF8 | `cISC4Occupant` | 34, most of them the base's; the class's own: QueryInterface thunk, Shutdown, SetPosition, GetBoundingBox |
| +0x8 | 0x00AA9BDC | `cIGZSerializable` | Write 0x0064A310, Read 0x0064A3A0, GetGZCLSID 0x00649260 |
| +0xC, +0x20 | 0x00AA9B78, 0x00AA9B60 | the property and exemplar holders | shared code |

Fields: the base at +0x04; +0x38 the position (three floats; the constructor copies the float at 0x00B46778 into each, a placeholder value of the exe's image); +0x44 and +0x48 the cell (the x and z times 0.0625, truncated by SetPosition); +0x4C and +0x50 the two poles (referenced); +0x54 a flag byte (the third argument of SetConnectedPoles); +0x55 the preview byte (not saved). GetBoundingBox gives the position as both corners. Shutdown lets the poles go (SetConnectedPoles with zeros) before the base's Shutdown. QueryInterface answers the three IDs with the object itself (counted) and sends every other to the base.

The header gzcom-dll/cISC4PowerLineOccupant.h has the five methods in the right order and with the right argument counts; the asm gives two pole pointers and a byte as outputs of GetConnectedPoles (the header types the first as a wire) and no result worth the name (EAX is left over from a count or a release; ours keeps that, see the comments in the source).

The save record is version 2, every wire of the city in one record, each object preceded by its size: the base's part, the three position floats, the two cells (int32), the flag as a bool and the two poles as references (an address ID and the pole interface's class ID, or a single 0 for none). `tools/re/save_schemas/powerlineocc.py` decodes all 1,486 wires of the 69 saves of the corpus that have any (85 bytes each; the four Making Money saves of the first twelve have 19 each, the four Rush Hour ones 26). The two pole references of a wire carry the class ID 0x09C05C6A, the pole occupant's.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.powerlineocc.slots` | 11 slots: QueryInterface and its thunks, GetConnectedPoles, SetConnectedPoles, GetCell, IsPreview, SetPreview, GetGZCLSID |
| `simulators.powerlineocc.occupant` | Shutdown, SetPosition, GetBoundingBox (occupant part slots 4, 9, 10) |
| `simulators.powerlineocc.save` | Write and Read and their holder thunks |
| `simulators.powerlineocc.object` | our own wire: detours on the constructor, the pool's allocator and the factory, the deleting destructor with the pool's free, copies of the five vtables taken once |

Every slot is a slot swap: none is called by address from outside the class (the thunks jump to QueryInterface, Write and Read inside it) and no other class's vtable holds one. `OPENSC4_POWERLINEOCC_SKIP` takes a list of function addresses (hex, eight digits) to leave out for bisecting.

Still the game's: the base class, the folded stubs (AddRef and Release thunks, the exemplar holder's, GetPosition 0x005B2B00, the do-nothing stub 0x00648F10), the pool's page code.

## What is in the range and not ours

The range 0x00648F10 to 0x0064AC00 holds 60 function starts; 23 are the wire's (the 8 of the interface, 5 thunks, the constructor, allocator, Write, Read, SetPosition, GetBoundingBox, 2 holder thunks, the factory and the destructor). The other 37 belong to the pole occupant, which comes before the wire in the file, and to its neighbours. The evidence for the split: they use fields up to +0x118 and 0x2C-byte records at +0xAC (the wire's object ends at +0x58), their thunks subtract 0x64 and 0x118 from `this`, and their QueryInterface (0x0064A030) answers 0xA8C86142, the IID the wire's Read asks ReadReference for, and 0x09C05C6A, the class ID in the wire's pole references: the class before the wire is the pole occupant. None of them is in a wire vtable.

| Address | What | Why left |
| --- | --- | --- |
| 0x00648F10 | `ret 4` | the stub shared by 49 vtables (SetLotTag) |
| 0x00648F20 to 0x00648F70, 0x00649080, 0x006491C0 to 0x00649250, 0x006499B0, 0x00649D80 to 0x00649DD0 | small getters, setters and thunks of the pole and of its neighbour; 0x00648F40 returns the class ID 0x49C05B9F (not the pole's, which is 0x09C05C6A); 0x00648F70 and 0x00649080 look occupants up by interface | not the wire's |
| 0x006497E0, 0x006499D0, 0x0064ABB0 | pool pieces of other classes (page set-up for a pool of 0x7EAC-byte pages, an allocator on the pool 0x00B0C0C8, a free for 32 KB pages) | not the wire's pool (0x00B0C258) |
| 0x00649820, 0x0064A4D0 | the slope and intercept of a 2-D line from two points | called by the class before and from 0x0075A39E |
| 0x00649910, 0x00649A00, 0x00649B30, 0x00649D00, 0x00649FA0, 0x0064A030 to 0x0064A170, 0x0064A560, 0x0064A650, 0x0064AA40 | the class before: a bounding box over its 0x2C-byte records (0x00649A00), table choice by a type (0x00649B30), QueryInterface (0x0064A030), the tuning load (0x0064A170 sets six globals at 0x00B46740 to 0x00B46754 from properties of an exemplar), a lighting-style flag with an identity transform at the position (0x0064AA40) | not the wire's; 0x0064AA40 is view-side and left |
| 0x006493B0 | 1068 bytes of float work generating points on a curve (12-byte points, groups of four per loop turn) | geometry for drawing, called from 0x0064D792, 0x0064D87B, 0x0064E6F1, 0x0064E799 (code after the range); fenced for the Renderer thread |
| 0x0064A6C0 | the sag of a hanging wire between two points (a square root, a tuning ratio from 0x00B46750/0x00B46754/0x00B4674C, two output points and a count) | geometry for drawing (callers 0x0064D71E, 0x0064E675); fenced |
| fragments at 0x00649190, 0x00649E50 to 0x00649F10, 0x0064A870 | adjustor thunks and a half function that belong to the class before (the last is entered by a jump from 0x0064DF93) | not the wire's |

The wire's own functions touch no model, light, vertex buffer or texture. The preview byte is set by the tool and read by the drawing code of the other classes.

## Test cities

The fingerprint's `powerlineocc` part: the number of wires (through the occupant manager's grid), the sum and XOR of a hash per wire over the position and cells, whether each pole is there, the flag, the type, the base's saved state and the IDs of the holder's own properties (no pointers, no reference counts, not the preview byte), then counters (flag set, both poles, one pole, a cell other than 0,0, a position other than the constructor's, a cell that is what SetPosition makes of the position). `OPENSC4_TEST_POWERLINEOCC=1` adds a checksum of the query slots on a third of the wires. `OPENSC4_TEST_POWERLINEOCC=<day>` runs the setters on a third of the wires on that day, each value put back (the poles removed, swapped and set again with the test's counts held; the position moved and set again with the cells written back; the preview byte flipped and restored), and makes one wire through the game's factory: QueryInterface through the five parts, the fields the constructor sets, SetPosition and SetPreview, the poles of one existing wire set on it and let go by Shutdown, then the last Release through the deleting destructor. The read-backs are summed into a ` set` field. Init, the save functions and anything that posts a message are not called.

## Parity

Run on 2026-10-12 together with the other two classes of this step ([pipeocc.md](pipeocc.md), [powerlineocc.md](powerlineocc.md), [powerpoleocc.md](powerpoleocc.md)) and the ten network occupant groups ([netocc.md](netocc.md)), the original against all of them for a week. Before the first run: `ret N` of every replaced function against our signatures, the register and EAX scans (clean), every byte of the class's records in the corpus saves decoded by the schema, and an independent reading of each function against its disassembly.

Plain: the 12 saves 11 the same on the first pair and one partly (Big City d0365: one of four runs with ours differed from the load on in the advisors' checksums only, every record identical; a run of the original did the same on another save earlier that day, see [netocc.md](netocc.md)), `saves-networks` 2 of 2, `saves-plopped` 3 of 3, `saves-ports` 3 of 3. With `OPENSC4_TEST_PIPEOCC=3`, `OPENSC4_TEST_POWERLINEOCC=3` and `OPENSC4_TEST_POWERPOLEOCC=3` together: the 12 saves 12 of 12 and `saves-networks` 2 of 2 on the first pair, the three `set` checksums the same on both sides in every save.

Ran with ours: every replaced function but the stand-alone allocation (0x0064A2E0, the builder's path; the factory's inline copy ran). The audit found no difference; three notes on values outside any city's range (a signalling NaN position, a position beyond the int64 range, EAX of SetConnectedPoles when it has nothing to do) are left as they are.
