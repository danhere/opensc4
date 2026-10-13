# The pre-built-model network occupant

A network piece that is drawn from a model made beforehand (found through the piece's exemplar) and not from the network's own textured quads: the pieces of the bridge approaches, and the base class of the bridge and tunnel occupants. The class has no name in the exe; `cSC4PrebuiltNetworkOccupant` is ours. It is a network occupant ([netocc.md](netocc.md)) that also keeps where the model stands from the piece's position, an optional support pylon (a second model, its angle and where it stands), and the two planes of its surface, which answer the height and the normal at a point. Few cities have one: 14 in each of the four corpus saves that have any (`saves-networks`, `saves-net14`, `saves-q1`), none in the twelve tutorial saves. Build 641 addresses throughout (data/addresses/prebuiltocc.toml). Reimplemented like the other occupant classes ([pipeocc.md](pipeocc.md), [netocc.md](netocc.md)): the class's own functions only, from the disassembly, tested through the parity harness ([parity.md](parity.md)).

## The class

Class ID 0x49C1A034, which is also the only IID the class answers itself; every other ID goes to the network occupant's QueryInterface. The occupant type is the network occupant's, 0xC772BF98, so the objects are inside the network occupant test's walk. 0x1D8 bytes from the pool at 0x00B0B3A8 (pages of 32 KB at address & 0xFFFF8000, 69 items each; table 0x00AA81EC: free 0x0061E7E0, page set-up 0x0061D090). Constructor 0x0061E1A0 (one argument, non-zero for the most derived object; the bridge's 0x00605160 and the tunnel's 0x006476F0 call it with 0), factory 0x0061E820 (returns the occupant part, object + 4), destructor 0x00604CA0, deleting destructor 0x00605020. Code 0x0061D040 to 0x0061F4B8, with the stubs and adjustor thunks 0x00604E50 to 0x00605020.

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AA7038 | the network occupant interface, 82 slots | the class's own: 0 QueryInterface, 4 Shutdown, 33 GetModelInstances, 49 GetPathInfo, 56 GetAltitude, 57 GetNormal, 58 SetTranslucency, 61 SetLit, 65 TransformNode, 73 IsSupportPylonPresent, 79, 80 the bounding box |
| +0x4 | 0x00AA6FB0 | `cISC4Occupant` | 3 OccupantInit 0x0061DB00, 18 SetVisibility 0x0061F430, 30 SetAllFlags 0x0061F460 |
| +0x8 | 0x00AA6F94 | `cIGZSerializable` | Write 0x0061D8E0, Read 0x0061D9B0, GetGZCLSID 0x00604E50 |
| +0xC, +0x20, +0x38 | 0x00AA6F30, 0x00AA6F18, 0x00AA6F14 | the property and exemplar holders, the model part's thunk | adjustor thunks |
| +0x1D4 | 0x00AA6EC8 | the virtual base (table 0x00AA81FC: -4, then the displacement 0x198) | 19 adjustor thunks; slot 12 reaches the class's draw 0x0061E860 |

Fields (the network occupant's are in [netocc.md](netocc.md)):

| Offset | Field |
| --- | --- |
| +0x178 | three floats: where the model stands from the piece's position (saved; the set-up and Read set it, the constructor does not) |
| +0x184, +0x190 | the two planes of the surface, three floats each: x slope, z slope, height |
| +0x19C | the support pylon's model, 0 for none (saved) |
| +0x1A0 | the pylon's angle (saved with a pylon; the constructor leaves it) |
| +0x1A4 | three floats: where the pylon stands (saved with a pylon) |
| +0x1B0, +0x1B4 | a piece ID and a rotation and flip byte that stand in for the object's own in the effects' look-up (saved; 0 in every corpus object) |
| +0x1B8, +0x1BC | two effect IDs (-1 none, -2 asked for): the view's |
| +0x1C0, +0x1C1 | the zoom GetModelInstances last saw and the detail preference the draw last saw: the view's |
| +0x1C4, +0x1C8 | the model instance and the pylon's model instance |
| +0x1D0, +0x1D4 | the virtual base's displacement word (0) and its vtable |

The planes. The piece's surface is two triangles of its four corners, split along one diagonal or, with network flag 0x01000000, along the other. `UpdatePlanes` turns the corner heights h0 to h3 into slopes per world unit (differences over 16) and a height at the piece's position (the middle of the diagonal's two heights less the position's y, which is the highest corner's). A point (a, b) from the position lies on the first plane when a - b (or, with the flag, a + b) is below zero, else on the second.

## What each function does

| Function | Address | Ours | What |
| --- | --- | --- | --- |
| QueryInterface | 0x0061D060 | `slots` | the class IID: the object itself with one reference; the rest to the network occupant (0x006192C0) |
| IsSupportPylonPresent | 0x0061D040 | `slots` | 1 with a pylon model, else the network occupant's answer |
| SetSupportPylon | 0x0061D340 | `slots` | a maker's setter: the model, the angle, the offset |
| SetUp | 0x0061D120 | `slots` | a maker's set-up: the exemplar, the rotation and flip, the model's offset, the box (slot 80), and a property 0x4835C6A6 (true) on the object when the exemplar has no property 0x6A845768 |
| Shutdown | 0x0061D0D0 | `slots` | an initialised object releases its two model instances and goes on to the occupant base's Shutdown |
| OccupantInit | 0x0061DB00 | `slots` | the network occupant's Init, the model word at +0x4C (0x10 off, 0x40 on), SetLit with what the object at 0x00B43DDC answers, the planes |
| UpdatePlanes | 0x0061D230 | `geometry` | the six plane floats from the corners, the position and the flag |
| TransformNode | 0x0061D660 | `geometry` | a vector's x and z turned by the rotation and flip (and moved by +0x168 and +0x16C with a path ID override), the plane's height under it added to its y |
| GetAltitude | 0x0061D790 | `geometry` | the plane's height at a world point |
| GetNormal | 0x0061D840 | `geometry` | (-x slope, 1, -z slope) of the plane at a world point |
| GetPathInfo | 0x0061D380 | `geometry` | the path information from the path resource the exemplar's key or the path ID override names |
| Write, Read | 0x0061D8E0, 0x0061D9B0 | `save` | version 4 (Read also 3), the network occupant's record, the class's fields |
| Constructor | 0x0061E1A0 | `object` | the network occupant's constructor with 0, the vtables, the fields, the immovable flag |
| DeletingDestructor | 0x00605020 | `object` | the destructor (the game's), then the free |
| Free, PoolFree, InitPoolPage, Allocate, Create | 0x0061E300, 0x0061E7E0, 0x0061D090, 0x0061D200, 0x0061E820 | `object` | the free the deleting destructor calls, the pool's free and page set-up, the allocation a maker calls and the factory |

## The save record

Version 4, then the network occupant's whole record (version 8), then the class's fields. Read also takes version 3, which has seven more uint32 that it drops.

| Part | Fields |
| --- | --- |
| class | u16 4 (or 3) |
| network occupant | its record (netocc.py decodes it) |
| class | three floats (+0x178); version 3 only: six uint32, dropped; u32 the piece override (+0x1B0); u8 its rotation and flip (+0x1B4); u32 the pylon's model (+0x19C); with a pylon: float the angle (+0x1A0), three floats the offset (+0x1A4); version 3 only: one uint32, dropped |

`tools/re/save_schemas/prebuiltocc.py` decodes all 56 objects of the corpus (four saves of the `parity/saves*` folders have the record, 14 objects each: both saves of `saves-networks`, one of `saves-net14` and one of `saves-q1`; the saves of `saves`, `saves-plopped` and `saves-ports` have none), every byte of every record consumed. All are version 4 with a network occupant record of version 8; 20 have a pylon, 8 the flag 0x01000000; the model's offset is zero and the piece override 0 in all. `prebuilt_record(r)` decodes the part from the class's version on, for the bridge's and the tunnel's decoders.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.prebuiltocc.slots` | 6 detours: QueryInterface, the pylon's query and setter, the set-up, Shutdown, the occupant part's Init |
| `simulators.prebuiltocc.geometry` | 5 detours: the planes, TransformNode, GetAltitude, GetNormal, GetPathInfo |
| `simulators.prebuiltocc.save` | 2 detours: Write and Read |
| `simulators.prebuiltocc.object` | 7 detours: the constructor, the deleting destructor, the free, the pool's free, page set-up and allocation, the factory |

`OPENSC4_PREBUILTOCC_SKIP` takes a list of function addresses (hex, eight digits) to leave out of a group, for bisecting.

## What is left, and why

Drawing, which stays the game's for the renderer work:

- GetModelInstances 0x0061E8A0 (slot 33, 2,946 bytes): makes the model instance and the pylon's (0x00801B00, 0x00614A30, 0x00614890), sets their transforms and alpha and keeps them at +0x1C4 and +0x1C8.
- SetTranslucency 0x0061D540 (slot 58): the base's (0x0061B750), then the two model instances' render state (their slots 0x3C and 0x40) and the view object at 0x00B43DD0.
- The bounding box 0x0061DB50 (slot 80, 1,585 bytes): with an exemplar it asks slot 33 for up to ten model instances and joins their boxes (without one it is the network occupant's 0x0061A430).
- The draw 0x0061E860 (the virtual base's slot 12 behind the thunk 0x00604FB0): the effects' refresh when the detail preference changed, then the drawable's draw 0x0061A850.
- The effects' refresh 0x0061E340 (1,177 bytes): by the detail preference (0x00B43CC4) and the occupant's flags it stops and starts two effects through the object the city's slot 0x150 answers, at heights it asks the terrain for, and keeps their IDs at +0x1B8 and +0x1BC.
- 0x0061F430 (the occupant part's slot 18, SetVisibility), 0x0061F460 (its slot 30, SetAllFlags), 0x0061F480 (slot 79) and 0x0061F4A0 (slot 61, SetLit): each is the base's function and then the effects' refresh; the bases of the last two are the network occupant's lighting, also the game's.

Not drawing, and left:

- The destructor 0x00604CA0: the bridge's and the tunnel's deleting destructors call it too, and it is a tail jump into the network occupant's destructor; it is called at its address.
- GetGZCLSID 0x00604E50: a six-byte stub among the shared stubs.
- The adjustor thunks 0x00604E60 to 0x00605010 (the save thunks, the QueryInterface, Shutdown and deleting destructor thunks, the virtual base's 19 slots).
- 0x0061D030 (a thunk to the network occupant's Read) is the network occupant's; 0x00604C60, 0x00605040 and 0x00605120 are the bridge pool's ([bridgeocc.md](bridgeocc.md)).

## Test

The fingerprint's `prebuiltocc` part (order 126): the number of objects (through the occupant manager's grid, occupant type 0xC772BF98 and a class ID of 0x49C1A034) and the sum and XOR of a hash per object: the saved fields (the model's offset, the piece override and its byte, the pylon's model and with one its angle and offset), the six plane floats, the pylon's query, flag 0x01000000, GetAltitude and GetNormal at seven points around the position, TransformNode of three vectors (for a rotation of 0 to 3: above that the game's function works on stack slots it never set), the class ID of the serializable part, the occupant type, and QueryInterface through the interface and the occupant part for the class IID, 0xA9764C8C, the network occupant IID, 1 and one nobody answers. Then counters (pylon, other diagonal, sloped, a model offset, a piece override, points on the first plane, turned pieces). Not mixed: the effect IDs, the two seen bytes, the model instances, the path information (they follow the view), pointers, reference counts.

`OPENSC4_TEST_PREBUILTOCC=<day>` adds ` set <checksum>,<count>` (also for a city without such objects). One object made through the game's factory: QueryInterface through six parts for ten IDs, the fields the constructor sets, the pylon setter and query, GetPathInfo without an exemplar, then on corners the test stores in it the planes, GetAltitude, GetNormal and TransformNode for both diagonals, the rotations 0 to 3 plain and flipped, without and with a path ID override; Shutdown not initialised and the occupant part's Init on the object marked initialised (the branches that do nothing); the last Release. And on every second object of the city (by the cell of its corner 0): the pylon setter with other values, read back and put back; the planes worked out again and compared with what the object had; GetPathInfo twice, the path table read and put back. Not called: the set-up (it needs an exemplar and asks for the model instances), the Inits and Shutdown that post messages, Write and Read (the saves of the parity runs go through them in a city that has such pieces).

## Parity

Run on 2026-10-13 together with the other two classes of this step ([bridgeocc.md](bridgeocc.md), [tunnelocc.md](tunnelocc.md)) and the network occupant groups of the earlier steps ([netocc.md](netocc.md), [pipeocc.md](pipeocc.md)), the original against all of them for a week. Before the first run: `ret N` of all 44 replaced functions of the three classes against our signatures (no mismatch), the caller register scan and the EAX scan (no hit), every record of the three classes in the corpus saves decoded with every byte consumed (28 pre-built pieces and 96 bridge pieces in `saves-networks` Big City n0000 and n0090, 14 and 48 each; 2 tunnel entrances in `saves-networks/Rush Hour Tutorial/t0000`; none in the 12 saves, `saves-plopped` or `saves-ports`), and an independent reading of every function against its disassembly (no difference in behaviour).

Plain: the 12 saves 12 of 12 the same on the first pair, `saves-networks` 3 of 3 (n0000, n0090, t0000), `saves-plopped` 3 of 3, `saves-ports` 3 of 3. With `OPENSC4_TEST_PREBUILTOCC=3`, `OPENSC4_TEST_BRIDGEOCC=3` and `OPENSC4_TEST_TUNNELOCC=3` together: the 12 saves 12 of 12 and `saves-networks` 3 of 3 on the first pair, the three parts with their `set` checksums the same on both sides in every save. On n0000 the part reads `prebuiltocc 14,... pyl 5 diag 2 slope 10 ... set E9985E36,7`.

The audit found one difference that does not matter: the occupant part's Init (0x0061DB00) returns the upper bytes of EAX differently from the game, which sets only AL, and every caller reads only AL.

Ran with ours (first-call log lines over the whole batch): Construct, Create, DeletingDestructor, Free, GetAltitude, GetNormal, GetPathInfo, IsSupportPylonPresent, OccupantInit, QueryInterface, Read, SetSupportPylon, Shutdown, TransformNode, UpdatePlanes and Write. Not run: SetUp (0x0061D120, called when the network tool places a piece), PoolFree (0x0061E7E0), InitPoolPage (0x0061D090; the first page is made by the static initialiser before the detours are in) and Allocate (0x0061D200, the builder's path).
