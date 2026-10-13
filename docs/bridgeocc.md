# The bridge occupant (cSC4NetworkBridgeOccupant)

One piece of a bridge: one object per tile of the deck (48 in each of the corpus saves that have a bridge). It is a pre-built model network occupant, which is a network occupant ([netocc.md](netocc.md)), with one member of its own: a container of things attached to the piece's model instance, which the bridge starts for a piece that stands in deep water. Nothing of its own is saved. Build 641 addresses throughout (data/addresses/bridgeocc.toml). Reimplemented like the other occupant classes ([pipeocc.md](pipeocc.md), [netocc.md](netocc.md)): the bridge's own functions only, from the disassembly, tested through the parity harness ([parity.md](parity.md)).

## The class

Class ID 0x49CC1BCD, which is also the only IID the class answers itself. The occupant type is the network occupant's, 0xC772BF98: the bridge pieces are inside the network occupant test's walk (`netocc_autotest.cpp` takes every occupant of that type). 0x1F0 bytes from the pool at 0x00B0B2D0 (pages of 32 KB at address & 0xFFFF8000, 66 items each; table 0x00AA7180 while the pool lives and 0x00AA6EB8, with the same functions, from its exit handler on: free 0x00605120, page set-up 0x00604C60). Constructor 0x00605160 (one argument, non-zero for the most derived object), factory 0x00605450 (returns the occupant part, object + 4), destructor 0x006053F0, deleting destructor 0x00605390. A maker function (0x006242A0 on) picks one of three classes by class ID and for the bridge calls the pool allocation 0x00605040 and the constructor.

The class derives from the pre-built model network occupant (class ID 0x49C1A034, vtable 0x00AA7038, 0x1D8 bytes), which has its own document ([prebuiltocc.md](prebuiltocc.md)) and is called at its addresses here: its constructor 0x0061E1A0 (one argument, 0 from the bridge), destructor 0x00604CA0, QueryInterface 0x0061D060, slot 4 0x0061D0D0 and occupant Init 0x0061DB00. Its Write and Read are the bridge's too.

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AA7300 | the network occupant interface, 82 slots (81 of the network occupant's, then the pre-built class's slot 81) | the bridge's own: 0 QueryInterface 0x00604BC0, 4 Shutdown 0x00604C10, 62 CanBeLit (the shared stub 0x00648F00, always true; the parent has the always-false stub), 81 the model instance hook 0x00605070 (left the game's, see below) |
| +0x4 | 0x00AA7278 | `cISC4Occupant` | 3 OccupantInit 0x00604BF0 |
| +0x8 | 0x00AA725C | `cIGZSerializable` | 5 GetGZCLSID 0x00604BB0; Write 0x0061D8E0 and Read 0x0061D9B0 are the parent's |
| +0xC, +0x20, +0x38 | 0x00AA71F8, 0x00AA71E0, 0x00AA71DC | the property and exemplar holders, the model part's thunk | adjustor thunks |
| +0x1EC | 0x00AA7190 | the virtual base (table 0x00AA7448: -4, then the displacement 0x1B0) | 19 adjustor thunks, none the bridge's own |

Fields of its own: the member at +0x1D0 (0x18 bytes, up to the virtual base's displacement word at +0x1E8). The constructor also sets the flags 0x00C00000 in the network flags at +0x13C (the parent's constructor sets 0x00400000, immovable, before it); all 192 bridge pieces of the corpus saves have both.

**The member at +0x1D0.** A small object with its own vtable (0x00AA4060; constructor 0x005E8670, destructor 0x00513E00), also constructed at 0x0051429E, 0x0053C7A4, 0x0053C7AF and 0x0054CE1A by classes that were not looked at; no other code between 0x00600000 and 0x00625000 touches the bridge's. It keeps an owner (+4, with a reference), a tree of entries (+8, the head node from the game's STL allocator) and a count (+0xC). What the entries are was not analysed (things placed by the owner's transform: slot 5 builds matrices from it). The bridge calls its slots 0 (start), 1 (stop: empties the tree and releases the owner), 2 (set the owner), 3 (the owner), 5 (place the entries) and 6 (hand an argument to every entry that is not active).

## What each function does

| Function | Address | Ours | What |
| --- | --- | --- | --- |
| GetGZCLSID | 0x00604BB0 | `slots` | the class ID |
| QueryInterface | 0x00604BC0 | `slots` | the bridge IID: the object itself with one reference; the rest to the parent (0x0061D060) |
| OccupantInit | 0x00604BF0 | `slots` | the occupant part's slot 3: an object not initialised goes to the parent's occupant Init; an initialised one answers true |
| Shutdown | 0x00604C10 | `slots` | slot 4: an object not initialised answers true; else the member is stopped and loses its owner when it has one, then the parent's slot 4 |
| OnModelInstance | 0x00605070 | no (the game's) | slot 81, which the parent calls with the model instance it has just made: the member is stopped when it has an owner and given the instance as owner; with an instance it is started, and when the water level at the piece's position is more than 12 above the terrain's altitude there its slots 6 (with 0) and 5 are called |
| Constructor | 0x00605160 | `object` | the parent's constructor with 0, the vtables, the member's constructor, the two flags |
| Destroy | 0x006053F0 | `object` | the vtables, the member's destructor, the parent's destructor |
| DeletingDestructor | 0x00605390 | `object` | the destructor, the item back to its page (flag 1, object not null) |
| PoolFree, InitPoolPage, Allocate, Create | 0x00605120, 0x00604C60, 0x00605040, 0x00605450 | `object` | the pool's free and page set-up, the allocation (the maker function calls it with an argument it ignores) and the factory |

## The save record

The bridge has no Write or Read: the serializable part's slots 3 and 4 are the pre-built class's (0x0061D8E0, version 4; 0x0061D9B0, versions 3 and 4), so a bridge piece's record is that class's record under the bridge's class ID (GetGZCLSID is what the bridge overrides):

| Part | Fields |
| --- | --- |
| pre-built | u16 4 |
| network occupant | its whole record, version 8 (netocc.py decodes it) |
| pre-built | three floats (+0x178, +0x17C, +0x180), u32 (+0x1B0), u8 (+0x1B4), u32 (+0x19C) and, only when that is not zero, four floats (+0x1A0 to +0x1AC) |

Version 3 (read only) has six more uint32 after the three floats and one more at the end; Read drops them. The member at +0x1D0 is not saved.

`tools/re/save_schemas/bridgeocc.py` decodes all 192 bridge pieces of the parity corpora (`parity/saves*`), every byte of every record consumed: 48 in each of the two Big City saves of `parity/saves-networks` (its third save has none), 48 in `parity/saves-net14` and 48 in one of the two saves of `parity/saves-q1`. The 12 saves of `parity/saves` and the saves of `parity/saves-plopped` and `parity/saves-ports` have no bridge. All 192 are version 4 with the three floats, the word, the byte and the count zero. The decoder is the pre-built class's (`prebuilt_record` of `tools/re/save_schemas/prebuiltocc.py`) under the bridge's class ID.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.bridgeocc.slots` | 4 detours: GetGZCLSID, QueryInterface, the occupant part's Init, Shutdown |
| `simulators.bridgeocc.object` | 7 detours: the constructor, the destructor, the deleting destructor, the pool's free, page set-up and allocation, the factory |

`OPENSC4_BRIDGEOCC_SKIP` takes a list of function addresses (hex, eight digits) to leave out of a group, for bisecting.

## What is left, and why

- The parent class (the pre-built model network occupant, [prebuiltocc.md](prebuiltocc.md)) and the network occupant ([netocc.md](netocc.md)): other pieces of this work; called at the game's addresses.
- The member's class (vtable 0x00AA4060): not the bridge's; its constructor, destructor and slots are called at the game's addresses.
- The adjustor thunks 0x006051F0 to 0x00605380 (QueryInterface, Shutdown and the deleting destructor for each part, the virtual base's 19 slots): they jump to the functions above or to the network occupant's.
- CanBeLit (slot 62): the stub 0x00648F00 (`mov al, 1`), folded with many other classes.
- 0x00605490 (ret 8: the direction, 0 to 3, between the cells of two neighbouring network occupants, else -1): not the bridge's. Its two callers are in a function of the class that follows (0x00605D30), which loads its own `this` into ECX before each call.
- Between the bridge's functions and not the bridge's: 0x00604CA0 (the parent's destructor), 0x00604D10 to 0x00604E40 (the network occupant's stubs), 0x00604E50 (the parent's GetGZCLSID), 0x00604E80 to 0x00605010 (the parent's thunks), 0x00605020 (the parent's deleting destructor).
- Slot 81, OnModelInstance (0x00605070): the game's. The parent's model code calls it (at 0x0061EFED) with the model instance it has just made, and it decides (from the terrain's water level and altitude at the piece's position) whether the member's entries are started and placed, through the member's own functions. It is the one function of the bridge that belongs to drawing, which is fenced off for the renderer work, so it has no translation here, no hook and no address entry. The other functions of the bridge build or draw nothing.

## Test

The fingerprint's `bridgeocc` part (order 127): the number of bridge pieces (through the occupant manager's grid, occupant type 0xC772BF98 and a class ID of 0x49CC1BCD) and the sum and XOR of a hash per piece: the class ID of the serializable part, the occupant type, whether it is initialised, the two flags, CanBeLit, the parent's saved fields (the three floats, the word, the byte, the count and its four floats), the piece's cell, and QueryInterface through the interface and the occupant part for the bridge IID, the parent's, the network occupant's, 1 and one nobody answers (the reference it gives released). Then counters: initialised, both flags, can be lit, count not zero, answers to the bridge IID and to the parent's. Not mixed: the member (its owner is the model instance, which the view makes and drops), the parent's model references, pointers, reference counts.

`OPENSC4_TEST_BRIDGEOCC=<day>` adds ` set <checksum>,<count>` (printed with a count of 0 when the city has no bridge). One object made through the game's factory: GetGZCLSID, QueryInterface through six parts for ten IDs, the fields the constructors set, CanBeLit, Shutdown on the object as made (the branch that does nothing), the occupant part's Init on the object marked initialised (the branch that does nothing), then the last Release (the deleting destructor and the pool). And every third bridge piece of the city, chosen by its cell: the occupant part's Init (the branch that does nothing; only on an initialised piece) and QueryInterface through six parts for ten IDs. The class has no setter. Not called by the test: the Init of an object not initialised (it ends in the occupant message 0x99EF1140), Shutdown of an initialised object, slot 81 (the game's), the stand-alone allocation (the builder's path) and the pool's free slot, Write and Read (the parent's).

## Parity

Run on 2026-10-13 together with the other two classes of this step ([prebuiltocc.md](prebuiltocc.md), [tunnelocc.md](tunnelocc.md)) and the network occupant groups of the earlier steps ([netocc.md](netocc.md), [pipeocc.md](pipeocc.md)), the original against all of them for a week. Before the first run: `ret N` of all 44 replaced functions of the three classes against our signatures (no mismatch), the caller register scan and the EAX scan (no hit), every record of the three classes in the corpus saves decoded with every byte consumed (28 pre-built pieces and 96 bridge pieces in `saves-networks` Big City n0000 and n0090, 14 and 48 each; 2 tunnel entrances in `saves-networks/Rush Hour Tutorial/t0000`; none in the 12 saves, `saves-plopped` or `saves-ports`), and an independent reading of every function against its disassembly (no difference in behaviour).

Plain: the 12 saves 12 of 12 the same on the first pair, `saves-networks` 3 of 3 (n0000, n0090, t0000), `saves-plopped` 3 of 3, `saves-ports` 3 of 3. With `OPENSC4_TEST_PREBUILTOCC=3`, `OPENSC4_TEST_BRIDGEOCC=3` and `OPENSC4_TEST_TUNNELOCC=3` together: the 12 saves 12 of 12 and `saves-networks` 3 of 3 on the first pair, the three parts with their `set` checksums the same on both sides in every save. On n0000 the part reads `bridgeocc 48,... set 68E677DB,16`.

The audit found no difference.

Ran with ours (first-call log lines over the whole batch): Construct, Create, DeletingDestructor, Destroy, GetGZCLSID, OccupantInit, QueryInterface and Shutdown. Not run: PoolFree (0x00605120), InitPoolPage (0x00604C60) and Allocate (0x00605040). Slot 81 is the game's. Write and Read of a bridge piece are the pre-built class's ([prebuiltocc.md](prebuiltocc.md)), which ran.
