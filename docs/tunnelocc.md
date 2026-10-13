# The tunnel occupant (cSC4NetworkTunnelOccupant)

One entrance of a tunnel (a portal): a tunnel is two of these objects, each holding a pointer to the other. It is a pre-built-model network occupant, which is a network occupant ([netocc.md](netocc.md)), with that one pointer of its own. Build 641 addresses throughout (data/addresses/tunnelocc.toml). Reimplemented like the other occupant classes ([pipeocc.md](pipeocc.md), [netocc.md](netocc.md)): the tunnel's own functions only, from the disassembly, tested through the parity harness ([parity.md](parity.md)).

## The class

Class ID 0x8A4BD52B, which is also the IID the class answers to. The occupant type is the network occupant's, 0xC772BF98: tunnel entrances are inside the network occupant test's walk (`netocc_autotest.cpp` takes every occupant of that type). 0x1E0 bytes from the pool at 0x00B0B570 (pages of 32 KB at address & 0xFFFF8000, 68 items each; table 0x00AA9188: free 0x0061E7E0, which is the pre-built class's, page set-up 0x006471F0; the first page is set up by the static initialiser 0x00A73DE0 at start-up). Constructor 0x006476F0 (one argument, non-zero for the most derived object), factory 0x006477F0 (returns the occupant part, object + 4), destructor 0x00647260, deleting destructor 0x00647790. Code 0x00647170 to 0x00647830, most of the function starts in it adjustor thunks.

The class derives from the pre-built-model network occupant (class ID 0x49C1A034, vtable 0x00AA7038, constructor 0x0061E1A0, 0x1D8 bytes), which derives from the network occupant and is also the base of the bridge occupant. That class has its own document ([prebuiltocc.md](prebuiltocc.md)); the tunnel's code calls it at the game's addresses:

- its constructor 0x0061E1A0 takes one argument (0 when a derived class calls it) and calls the network occupant's constructor with 0; it stores its own vtables through the virtual base table at +0x3C (in a tunnel that is the tunnel's table, so its virtual base vtable lands at +0x1DC and its shift, 8, at +0x1D8; the tunnel's constructor replaces both), sets the flag 0x00400000 and its fields from +0x168 to +0x1C8 (not the floats at +0x178 to +0x180 and at +0x1A0);
- its destructor 0x00604CA0 releases the objects at +0x1C8 and +0x1C4 and ends in the network occupant's destructor;
- QueryInterface 0x0061D060 answers its own IID and hands the rest to the network occupant's; slot 4 is 0x0061D0D0, the occupant part's Init 0x0061DB00, Write 0x0061D8E0 and Read 0x0061D9B0.

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AA9308 | the network occupant interface, 82 slots | the tunnel's own: 0 QueryInterface 0x00647170, 4 0x006474F0 |
| +0x4 | 0x00AA9280 | `cISC4Occupant` | 3 OccupantInit 0x006471C0; 4 the thunk 0x006476C0 to slot 4 of the interface |
| +0x8 | 0x00AA9264 | `cIGZSerializable` | Write 0x00647580, Read 0x00647610, GetGZCLSID 0x006472D0 |
| +0xC, +0x20, +0x38 | 0x00AA9200, 0x00AA91E8, 0x00AA91E4 | the property and exemplar holders, the model part's thunk | adjustor thunks |
| +0x1DC | 0x00AA9198 | the virtual base (table 0x00AA9450: -4, then the displacement 0x1A0) | 19 adjustor thunks |

The tunnel's own fields: the other entrance at +0x1D0 (a tunnel occupant, referenced; null until the network tool links the two entrances) and the virtual base's displacement word at +0x1D8 (zero). Nothing in the class's code touches +0x1CC or +0x1D4.

## What each function does

| Function | Address | Ours | What |
| --- | --- | --- | --- |
| QueryInterface | 0x00647170 | `slots` | the tunnel IID: the object itself with one reference; the rest to the parent (0x0061D060) |
| GetGZCLSID | 0x006472D0 | `slots` | the class ID; also called by the class registration at 0x0046075A |
| OccupantInit | 0x006471C0 | `slots` | the occupant part's slot 3: an initialised object answers 1; otherwise the parent's occupant Init, then the network flag 0x4000 (translucent) cleared through slot 11 |
| Shutdown | 0x006474F0 | `slots` | slot 4: an object not initialised answers 1; otherwise the other entrance is let go and the parent's slot 4 follows |
| SetOtherEnd | 0x00647530 | `slots` | the other entrance: a new pointer is referenced and stored, the old one released; the network tool calls it for both entrances of a new tunnel (0x00628AFE, 0x00628B08) |
| Write, Read | 0x00647580, 0x00647610 | `save` | version 2, the parent's record, the other entrance as a flag byte and a reference |
| Constructor | 0x006476F0 | `object` | the parent's constructor with 0, the vtables, the other entrance null, flag 0x4000 cleared and 0x10C00000 set |
| Destroy | 0x00647260 | `object` | the vtables, the other entrance released, the parent's destructor |
| DeletingDestructor | 0x00647790 | `object` | the destructor, the item back to its page (released at 0 used, opened again at 0x40000033) |
| InitPoolPage, Allocate, Create | 0x006471F0, 0x00647230, 0x006477F0 | `object` | the pool's page set-up, the allocation (the network tool calls it at 0x006242BA with a size it ignores, then the constructor) and the factory |

## The save record

Version 2 of the tunnel, then version 4 of the parent, the network occupant's whole record (version 8), the parent's fields and last the tunnel's:

| Part | Fields |
| --- | --- |
| tunnel | u16 2 |
| parent | u16 4 (Read takes 3 too), the network occupant's record (netocc.py decodes it), three floats (+0x178), u32 +0x1B0, u8 +0x1B4, u32 +0x19C and, when that is not zero, four floats (+0x1A0); version 3 has six more words after the three floats and one at the end, which Read drops |
| tunnel | u8: 1 when there is another entrance that answers the serializable IID, then the reference to it (its address-ID and the class ID 0x8A4BD52B); 0 and nothing otherwise |

`tools/re/save_schemas/tunnelocc.py` decodes the two tunnel entrances of the one corpus save that has a tunnel, `parity/saves-networks/Rush Hour Tutorial/t0000.sc4` (one road tunnel: record 8A4BD52B/299B2D1B/00000000, 564 bytes, two objects of 282 bytes), every byte consumed. Both have the byte 1 and a reference whose address-ID is the other object's ID (0x069F0018 and 0x069F01F8, 0x1E0 apart: neighbours in the pool), the parent's fields all zero and the network flags 0x10C8A001. No other save of `parity/saves` (12), `parity/saves-networks` (n0000, n0090), `parity/saves-plopped` (3) and `parity/saves-ports` (3) has a record of the class, or the class ID's bytes anywhere in a record. The parent's part of the record is decoded by the pre-built class's module (`prebuilt_record` of `tools/re/save_schemas/prebuiltocc.py`), which also decodes the pre-built records those saves have (n0000 and n0090: 14 objects each, every byte consumed).

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.tunnelocc.slots` | 5 detours: QueryInterface, GetGZCLSID, the occupant part's Init, slot 4, SetOtherEnd |
| `simulators.tunnelocc.save` | 2 detours: Write and Read |
| `simulators.tunnelocc.object` | 6 detours: the constructor, the destructor, the deleting destructor, the pool's page set-up and allocation, the factory |
| `simulators.tunnelocc.own` | no detour: the objects carry our copies of the class's seven vtables (only with `simulators.tunnelocc.object`; see below) |

`OPENSC4_TUNNELOCC_SKIP` takes a list of function addresses (hex, eight digits) to leave out of a group, for bisecting. The deleting destructor calls the destructor, and the factory the constructor, at the game's addresses, so a skipped one is the game's there too.

### Our own object

`simulators.tunnelocc.own` makes the objects carry our copies of the class's seven vtables instead of the game's: the interface's (0x00AA9308, 82 slots), the occupant part's (0x00AA9280, 34 slots), the cIGZSerializable part's (0x00AA9264, 6 slots), the property holder's (0x00AA9200, 25 slots), the exemplar holder part's (0x00AA91E8, 5 slots), the model part's (0x00AA91E4, 1 slot) and the virtual base's (0x00AA9198, 19 slots), 172 slots in all. The counts are from the executable: each table ends where the next one the constructor stores begins, or at the virtual base table, padding or other data, and nothing refers to an address inside one. The virtual base table (0x00AA9450) is data and stays the game's. The copies are taken once, the first time the constructor or the destructor wants them, so after every replacement is installed; each has the word before the game's table too. Every entry is what the game's table holds, a function's address or an adjustor thunk, so the other groups' detours decide what runs exactly as before. An entry that holds a function of the `object` group directly would be pointed at ours; this class has none (thunks reach the deleting destructor), so no entry is redirected. The group installs nothing: it only has an effect together with `simulators.tunnelocc.object`, whose constructor and destructor store the tables, and it does not turn that group on. The log has one line when the copies are taken (`tunnelocc own: vtables copied ...`, with the table sizes and the number of redirected entries). What was checked in the game for all eight classes is in [netocc.md](netocc.md), "Our own object".

## What is left, and why

- The parent class (0x0061D040 to 0x0061F4A0 and its destructor 0x00604CA0) and the pool's free slot 0x0061E7E0, which is in its range.
- The adjustor thunks (QueryInterface, AddRef and Release thunks for each part, the virtual base's 19 slots, the save, slot 4 and deleting destructor thunks) and the jumps 0x006471A0 and 0x006471B0 (AddRef and Release): they jump to the functions above or to the base's.
- 0x00647570 (`mov eax, [ecx + 0x1D0]`, seven bytes): for a tunnel it answers the other entrance, but it is called for other classes' objects too (0x004ADFBA, 0x004AF83C, 0x0062AF07, 0x0063A8BC, 0x0071414B): a folded stub.
- 0x00647830 and 0x00647850: not the tunnel's. They work on a record of the network tool (the first ORs the 13 words from +0x18 into +0x4C, the second clears +0xC and +0x18 to +0x50 and sets +0xB8 to -1) and are called from the tool's code (0x00624FE0 to 0x006589BB).
- Drawing: none of the tunnel's own functions draws or builds a model.

## Test

The fingerprint's `tunnelocc` part (order 128): the number of tunnel entrances (through the occupant manager's grid, occupant type 0xC772BF98 and a class ID of 0x8A4BD52B) and the sum and XOR of a hash per entrance: whether it has another entrance, whether that one is a tunnel that points back and how many cells away it is, its own cells, the network flags 0x10C04000, the parent's saved fields, the class ID of the serializable part, the occupant type, and QueryInterface through the interface and the occupant part for the tunnel IID, the parent's, the network occupant's, 0xA9764C8C, 1, the bridge's and one nobody answers (each reference released). Then counters: entrances with another entrance (`link`), entrances whose other entrance points back (`mut`), usable, translucent. Not mixed: pointers, reference counts, the parent's objects and model state. On a city without a tunnel the part is `tunnelocc 0,00000000,00000000 link 0 mut 0 use 0 trans 0`; on `t0000` it counts 2 entrances, 2 linked and 2 mutual (`tunnelocc 2,AC2C5386,0BCB5378 link 2 mut 2 use 2 trans 0`).

`OPENSC4_TEST_TUNNELOCC=<day>` adds ` set <made>,<city>,<count>` and one log line (`autotest: tunnelocc: ...`), also when the city has no tunnel:

- every tunnel entrance of the city, in the order of its cells: SetOtherEnd with null, with the old pointer and with the old pointer again, the field read back after each and the other entrance's reference count compared with before (the test holds a reference of its own meanwhile, and the old pointer is back at the end); then the occupant part's Init, which only answers 1 on an initialised object;
- made objects, which are in no cell: one from the factory, QueryInterface through six parts for eleven IDs, GetGZCLSID, the fields the constructors set; a second one made as the network tool does (the allocation, then the constructor), the setter both ways, with null and with the same pointer, pointers and reference counts read back after each call; the second object's last reference is the first one's pointer, so it dies inside the setter, through the deleting destructor and the destructor, which releases its own other entrance; the occupant part's Init on an object marked initialised and slot 4 on one that is not (through the interface and the occupant part's thunk); Read's refusal of versions 1 and 3 and of an empty stream, through the serializable part and the holder's thunk, with a stream of the test's own (as `save_autotest.cpp`'s); then 68 more objects from the factory, which makes the pool take a second page (the page set-up), all released again (`OPENSC4_TEST_TUNNELOCC_POOL=0` leaves this part out); then the last Release.

Reached by the test on any city: QueryInterface, GetGZCLSID, OccupantInit (the initialised branch), slot 4 (the branch of an object not initialised), SetOtherEnd (every branch), Read (the version refusal only), the constructor, the destructor (with and without another entrance), the deleting destructor (each page count branch), the page set-up, the allocation and the factory. Not reached by the test: OccupantInit of an object not initialised and slot 4 of an initialised one (they end in the occupant messages 0x99EF1140 and 0x99EF1141, whose listeners would meet an occupant that is in no cell), and Write and Read of a whole record: no autotest makes the game's memory stream, and the record holds the property holder's, the network occupant's and the parent's parts, whose Reads end in the model's state. Those are for the game itself to run on a save with a tunnel: loading `t0000` runs the factory, Read with the byte 1 (the save has linked entrances only, so not the byte 0) and the occupant part's Init; saving it Write; leaving the city slot 4 and the destructors. The parity runs of `t0000` did this (see Parity below). Each function logs its first call (`tunnelocc: first <name>`), which shows what a run reached.

## Parity

Run on 2026-10-13 together with the other two classes of this step ([prebuiltocc.md](prebuiltocc.md), [bridgeocc.md](bridgeocc.md)) and the network occupant groups of the earlier steps ([netocc.md](netocc.md), [pipeocc.md](pipeocc.md)), the original against all of them for a week. Before the first run: `ret N` of all 44 replaced functions of the three classes against our signatures (no mismatch), the caller register scan and the EAX scan (no hit), every record of the three classes in the corpus saves decoded with every byte consumed (28 pre-built pieces and 96 bridge pieces in `saves-networks` Big City n0000 and n0090, 14 and 48 each; 2 tunnel entrances in `saves-networks/Rush Hour Tutorial/t0000`; none in the 12 saves, `saves-plopped` or `saves-ports`), and an independent reading of every function against its disassembly (no difference in behaviour).

Plain: the 12 saves 12 of 12 the same on the first pair, `saves-networks` 3 of 3 (n0000, n0090, t0000), `saves-plopped` 3 of 3, `saves-ports` 3 of 3. With `OPENSC4_TEST_PREBUILTOCC=3`, `OPENSC4_TEST_BRIDGEOCC=3` and `OPENSC4_TEST_TUNNELOCC=3` together: the 12 saves 12 of 12 and `saves-networks` 3 of 3 on the first pair, the three parts with their `set` checksums the same on both sides in every save. On t0000 the part reads `tunnelocc 2,AC2C5386,0BCB5378 link 2 mut 2 use 2 trans 0 set 92913BDE,7159204A,2`.

The save: `saves-networks/Rush Hour Tutorial/t0000.sc4` was made on 2026-10-13 by driving the original game's own interface (terrain raised into steep spires, one straight road drag through the base): one road tunnel, two entrances that reference each other. Earlier attempts on Big City had only levelled the hill.

The audit found no difference.

Ran with ours (first-call log lines over the whole batch): all 13 replaced functions (Allocate, Construct, Create, DeletingDestructor, Destroy, GetGZCLSID, InitPoolPage, OccupantInit, QueryInterface, Read, SetOtherEnd, Shutdown and Write). Write and the full Read ran only on t0000.

Our own objects (batch o1, 2026-10-13): the original against every network occupant group and the eight `.own` groups for a week, every class's copies in use in the runs that made or destroyed its objects (the `own: vtables copied` log line). Plain: the 12 saves 12 of 12 the same on the first pair, `saves-networks` 3 of 3, `saves-plopped` 3 of 3, `saves-ports` 3 of 3. With the test switches of all the network occupant classes but `OPENSC4_TEST_NETOCC`: `saves-networks` 3 of 3 and the 12 saves 11 of 12 on the first pair; on Making Money d0365 one run of ours in four gave other `adv` and `advisors` values from the first day on (two days before the tests run; every decoded record the same), and twelve more pairs of that save were all the same, so one run of ours in 16 against none of the original's 16: not explained and not reproduced.
