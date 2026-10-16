# The network occupant (cSC4NetworkOccupant)

One piece of a network in the city: a road, rail, highway, pipe, power pole or subway tile, a bridge or a tunnel piece. It holds the piece's flags, its rotation and variation, the heights of its four corners, which of its four edges connect and how, its demolition cost and a few properties in its property holder. The network simulator makes and changes them; traffic, the renderer and the save code read them. The occupant base class ([occbase.md](occbase.md)) is at the object + 4. Build 641 addresses throughout (data/addresses/netocc.toml); class map and slot list in `src/core/netocc_sim.h`.

The first piece is 43 small functions of the interface (state and queries). The second is the class's life and save: Write, Read, GetGZCLSID, both Inits, QueryInterface, the constructor, the destructors, the pool's free and page set-up and the factory (12 functions). The third is geometry, paths and the edge store (17 functions: altitude and normal, the occupied cell, the position and boxes, neighbour connections, the path information and path connection table, and the edge store's Get, Set, Assign, Write and Read). The drawing code stays out for the Renderer thread: SetQuadDraw, AddPart (which is not only drawing: it appends the saved model triangle records at +0x118 and the box follows them, see the bridge parts in [nettool.md](nettool.md#the-bridges-parts-t10-and-t11)), GetModelInstances, MarkLightingUpdateNeeded, SetTranslucency, SetLit and the slots 76 to 79 (the flag setters here call SetTranslucency, the game's).

## The class

Class ID 0xC9C05C6E, occupant type 0xC772BF98, interface IID 0xA821EF94. Virtual inheritance: the primary interface (vtable 0x00AA8080, 81 slots: the 74 of the interface and the class's own 74 to 80) at +0, the cISC4Occupant part at +4 (0x00AA7FF8; the base starts there), the property holder at +0xC, more parts at +0x8, +0x20, +0x38 and a virtual base through the table at +0x3C. The bridge, tunnel and pipe classes put the same functions in their vtables, and some are called directly: each function is replaced by a detour at its address. The derived classes have their own documents: the pre-built model kind ([prebuiltocc.md](prebuiltocc.md)), the bridge ([bridgeocc.md](bridgeocc.md)) and the tunnel ([tunnelocc.md](tunnelocc.md)), which derive from it, and the pipe ([pipeocc.md](pipeocc.md)) and the subway piece ([subwayocc.md](subwayocc.md)), which derive from it by way of the class of the underground pieces ([pipebase.md](pipebase.md)).

| Offset | What |
| --- | --- |
| +0x04 | the occupant base (position in the occupant manager, removal counts, flags) |
| +0x0C | the property holder (retrofit, node edge types, path ID override, passage ceiling, one-way directions, complex piece origin) |
| +0xA0, +0xB8, +0xD0, +0xE8 | the four corner heights (floats, one every 0x18 bytes) |
| +0x100 | under texture (uint32) |
| +0x104 | rotation (bits 0 to 6) and flip (bit 7) |
| +0x105 | variation |
| +0x108 | the edge store: a small map from a network type (byte, 0 to 12) to four edge bytes (shift 8 x edge); +0x108 count of further entries, +0x10C first key, +0x110 first value, +0x114 pointer to the further entries |
| +0x13C | network flags (0x40 intersection candidate, 0x2000 path info, 0x4000 translucent, 0x400000 immovable, 0x10000000 usable, 0x20000000 lit, 0x40000000 support pylon) |
| +0x164 | path ID override |
| +0x170 | demolition cost (int64) |

An edge byte: low three bits the connection (non-zero = active), bit 2 the grouped bit.

### The layout the constructor shows

The object is 0x180 bytes, from a pool of 32 KB pages (85 items a page, `address & 0xFFFF8000` is the page; the pool object is at 0x00B0B394 with the current page in its +4, the free list at the page's +8 and the used count at +0xC; the pool's own table at 0x00AA7DA8 holds the free function 0x0061BD40 and the page set-up 0x00619D50). The constructor (one argument, non-zero for the most derived object) stores the abstract vtable 0x00AA7DB8 at +0, builds the occupant base at +4 (0x005E9220) and the model part at +0x38 (0x00801CF0, flag 0), then the final vtables: +0 0x00AA8080 (the interface, 74 slots and a slot 80 at 0x0061A430), +4 0x00AA7FF8 (the occupant part), +8 0x00AA7FDC (cIGZSerializable), +0xC 0x00AA7F78 (the property holder), +0x20 0x00AA7F60 (the exemplar holder), +0x38 0x00AA7F5C (the model part; slot 0 is a thunk to the deleting destructor) and, through the virtual base table at +0x3C (0x00AA81C4: -4, then the displacement 0x140), the virtual base at +0x17C (0x00AA7F10, a thunk table whose slots 15 and 16 get and set the model's 0x14-byte state at +0x50; its displacement word at +0x178 is zero). The derived classes call the constructor with 0 after setting the table themselves.

| Offset | What |
| --- | --- |
| +0x38 to +0x63 | the model part (its state at +0x50: bytes, a word, three words and a float) |
| +0x64 | the transform: two bytes, the 3 by 3 matrix at +0x68 (identity), translation at +0x8C, scale at +0x98 (1.0) |
| +0x9C | four corners of 0x18 bytes: x, y, z, four bytes (lighting), two words |
| +0xFC | a uint32 (zero), saved |
| +0x118 | a vector (begin, end, capacity) of 0x4C-byte model triangle records (a uint32 and three corner-shaped records), appended by AddPart (slot 31, 0x0061C800); saved, and they set the box at +0x124 (slot 80 for network pieces, slot 0x140, 0x0061DB50, for pre-built and bridge occupants) |
| +0x124 | the box: low x, y, z; at +0x130 high x, y, z (set by slot 80 from the Init) |
| +0x140 | a resource key object (type, group, instance), released by the destructor |
| +0x144 to +0x16F | zero from the constructor |

### The save record

Write is the cIGZSerializable part's slot 3 (this = object + 8), version 8; Read takes version 4 and up. Schema: `tools/re/save_schemas/netocc.py` (decodes all 14 saves of the parity corpus, 24,300 objects, each consuming its bytes exactly; all version 8). In saved order:

| Field | Size | Notes |
| --- | --- | --- |
| version | uint16 | 8 |
| occupant base record | | version 4, removal counts, highlight, flags, type, 4 cell bytes, then the holder (version 2, properties, exemplar key) |
| transform | | a flags byte, then the matrix (flag 4), translation (1), scale (2); version 4 has nine floats instead |
| four corners | 0x18 each | float x, y, z, float +0x10, float +0x14, bytes +0xE, +0xD, +0xC, +0xF |
| +0xFC | uint32 | |
| variation (+0x105) | uint8 | |
| under texture (+0x100) | uint32 | |
| rotation and flip (+0x104) | uint8 | |
| edge store | | version 2 (uint16), count (uint8), first key (uint8) and value (uint32), then count of key (uint8) and value (uint32) |
| model triangle records (+0x118) | | count (uint32), then each: uint32, then three corner-shaped records |
| the box | 6 floats | as (low, high) pairs for x, y, z |
| flags (+0x13C) | uint32 | read: below version 6 converted from the old bits; bits 0xE000 cleared |
| path ID override (+0x164) | uint32 | version below 7 and not zero: two more floats follow (the old complex piece origin); below 8 the retrofit flag is set |
| resource key (+0x140) | 3 x uint32 | type, group, instance; zeros for none |
| demolition cost (+0x170) | int64 | |

After Read the model state (virtual base slots 0x3C and 0x40) is set when corner 0's last byte is not 0xFF or bit 1 of the state's first byte is set: that bit cleared, all four corners' last byte set to 0xFF, the state's float at +0x10 set to 255 times 1/255.

The derived classes write their own version first and then this whole record, then their own fields: the bridge (0x49CC1BCD) and the pre-built model (0x49C1A034) start with a uint16 of 4 and then this record; the pipe (0x49C05B9F) with two uint16 of 3 (the pipe's, then the intermediate class's); the subway piece (0xCA16374F) the same with a 2 and a 3. None has only the base's part; each has a schema of its own (`tools/re/save_schemas/bridgeocc.py`, `prebuiltocc.py`, `tunnelocc.py`, `pipeocc.py`, `subwayocc.py`), which runs this record's decoder on the part inside.

## What is ours

First piece, 43 functions (below). Second piece, 12 functions in `src/core/netocc_life.cpp`, each a detour at its address:

| Group | Functions |
| --- | --- |
| `simulators.netocc.save` (3) | Write 0x0061A190, Read 0x0061CC00, GetGZCLSID 0x00619040 |
| `simulators.netocc.life` (3) | the occupant part's Init 0x0061CB50, the interface's Init 0x00619050, QueryInterface 0x006192C0 |
| `simulators.netocc.object` (6) | constructor 0x0061BDD0, destructor 0x0061C170, deleting destructor 0x0061C4C0, the pool's free 0x0061BD40 and page set-up 0x00619D50, the factory 0x0061C480 |
| `simulators.netocc.own` | no detour: the objects carry our copies of the class's seven vtables (only with `simulators.netocc.object`; see below) |

Third piece, 17 functions in `src/core/netocc_geometry.cpp`, each a detour at its address:

| Group | Functions |
| --- | --- |
| `simulators.netocc.geometry` (8) | ConnectsToNeighbor 0x00619320 (slot 47), GetAltitude 0x006197F0 (56), GetNormal 0x0061B6C0 (57), GetOccupiedCell 0x0061B010 (59), the occupant part's box query 0x00619360 (slot 10, `this` = object + 4), SetRotationAndFlip 0x00604DF0 (74), GetPosition 0x006193D0 (75; the occupant part's slot 8 forwards to it), ComputeBoundingBox 0x0061A430 (80) |
| `simulators.netocc.paths` (4) | GetPathInfo 0x0061B510 (49), ReleasePathInfo 0x00619680 (50), PathConnectionExists 0x006196A0 (53), AddPathConnection 0x00619790 (54) |
| `simulators.netocc.edgestore` (5) | the edge store's Get 0x00619B70, Set 0x00619A40, Assign 0x006199D0 (slot 44 jumps to it), Write 0x00619C10, Read 0x00619C90 |

Not ours: the adjustor thunks to QueryInterface (0x0061BFC0, 0x0061BFE0, 0x0061BFF0, 0x0061C010, 0x0061C030), the thunks to the occupant base's Shutdown (0x0061BFD0, 0x00619100), the thunk to the deleting destructor (0x0061C020), the helpers the code calls at their addresses (the edge store's Write and Read, the path vector's resize, the old flags' conversion, the statics' set-up 0x0061C690, the model part's constructor, destructor and QueryInterface) and slot 80 (0x0061A430, now the third piece's). The vtables are the game's, not copies. The constructor does not make the dead stores of the float at 0x00B4525C that the game overwrites at once.

### Our own object

`simulators.netocc.own` makes the objects carry our copies of the class's seven vtables instead of the game's: the interface's (0x00AA8080, 81 slots), the occupant part's (0x00AA7FF8, 34 slots), the cIGZSerializable part's (0x00AA7FDC, 6 slots), the property holder's (0x00AA7F78, 25 slots), the exemplar holder part's (0x00AA7F60, 5 slots), the model part's (0x00AA7F5C, 1 slot) and the virtual base's (0x00AA7F10, 19 slots), 171 slots in all. The counts are from the executable: each table ends where the next one the constructor stores begins, or at the virtual base table, padding or other data, and nothing refers to an address inside one. The virtual base table (0x00AA81C4) is data and stays the game's. The copies are taken once, the first time the constructor or the destructor wants them, so after every replacement is installed; each has the word before the game's table too. Every entry is what the game's table holds, a function's address or an adjustor thunk, so the other groups' detours decide what runs exactly as before. An entry that holds a function of the `object` group directly would be pointed at ours; this class has none (thunks reach the deleting destructor), so no entry is redirected. The group installs nothing: it only has an effect together with `simulators.netocc.object`, whose constructor and destructor store the tables, and it does not turn that group on. The log has one line when the copies are taken (`netocc own: vtables copied ...`, with the table sizes and the number of redirected entries). The interface's table has 81 slots: the interface's 74 and the class's own seven after them (74 to 80).

This is the sixth and last step of the network occupant split, done the same way for all eight classes with virtual inheritance ([prebuiltocc.md](prebuiltocc.md), [bridgeocc.md](bridgeocc.md), [tunnelocc.md](tunnelocc.md), [pipebase.md](pipebase.md), [pipeocc.md](pipeocc.md), [subwayocc.md](subwayocc.md), [powerpoleocc.md](powerpoleocc.md)), each behind its own `simulators.<key>.own`; the power line's objects already were our own through its `object` group ([powerlineocc.md](powerlineocc.md)). The copy is `VtableCopy` in `src/core/occ_vtables.h` (`tests/occ_vtables_test.cpp`). While a base class's constructor or destructor runs, an object has that class's tables, the game's or the copies by that class's own switch.

What was checked in the game before changing which tables an object has, for all 57 tables of the eight classes: each table's address appears in the executable exactly twice, in the class's constructor and in its destructor, both times as the value stored into the object (the pre-built class's destructor 0x00604CA0 is the one of those sixteen functions that is not ours); no code compares anything with a table's address and no data points at one; the word before each table is the last slot of the table before it, zero padding or a float, never a type descriptor. Our own tests compared the pointers with the game's addresses in five places (the pre-built, bridge, tunnel, pipe and subway tests); they now ask `occ_vtables::IsTable`, which says yes for the game's table and for a copy of it, so their checksums are what they were.

## The 43 functions

| Group | Functions |
| --- | --- |
| `simulators.netocc.flags` (13) | HasNetworkFlag, HasAnyNetworkFlag, SetNetworkFlag, ClearNetworkFlag, IsImmovable, SetImmovable, IsUsable, SetUsable, IsPlaced, IsOfType, IsIntersection, IsLit, IsSupportPylonPresent |
| `simulators.netocc.edges` (8) | SetEdgeType, GetActiveEdges, GetActiveEdgesForType, GetActiveEdgesForTypeNoGroup, GetGroupedEdges, GetVertexHeights, GetVertexHeight, SetVertexHeights |
| `simulators.netocc.pieces` (10) | GetRotation, GetFlip, GetRotationAndFlip, SetVariation, GetVariation, SetUnderTexture, SetPathIDOverride, GetPathIDOverride, GetCostOfDemolition, SetCostOfDemolition |
| `simulators.netocc.properties` (12) | CheckForRetrofit, SetRetrofitFlag, Get/SetConnectionNodeEdgeTypes, Get/SetConnectionPathIDOverride, Get/SetPassageCeiling, Get/SetOneWayDirections, Get/SetComplexPieceOrigin |

Code: `src/core/netocc_state.cpp`. `OPENSC4_NETOCC_SKIP` lists function addresses (hex) to leave out, for bisecting.

What the setters do besides storing:

- SetNetworkFlag / ClearNetworkFlag: the usable bit (0x10000000) changing posts message 0x89D3E9F3 (queued; for Set only on a placed piece); the translucent bit (0x4000) calls SetTranslucency(true, 0x80) / (false, 0xFF).
- SetVariation / SetUnderTexture: a changed value posts message 0x99EF1145 (queued).
- SetConnectionPathIDOverride: zero removes the property; always stores +0x164, calls slot 50 (ReleasePathInfo) and clears flag 0x2000.
- The property setters make a 0x14-byte script number (`SC4ScriptNumber`, vtable 0x00ACECD0) and give it to the holder; SetOneWayDirections and SetComplexPieceOrigin give a temporary one to an existing property instead.

Not hooked: the adjustor thunks 0x0061B480, 0x0061AF80, 0x00604E00 (this + 0x108, then a jump to the edge store's Set 0x00619A40, Get 0x00619B70, copy 0x006199D0; shared with other classes' vtables).

## Geometry, paths and the edge store

`this` of the interface functions is the object's start; the occupant part's slot 10 gets the object + 4. All are detours at the game's address.

**Geometry.**

- GetAltitude (slot 56, ret 8, answer in ST0): with `a` = x less corner 0's x and `b` = z less corner 0's z, `a <= b` (an unordered compare takes the other branch) gives `(a * (h2 - h1) + (h1 - h0) * b) / 16 + h0`, else `((h2 - h3) * b + (h3 - h0) * a) / 16 + h0`, `h0` to `h3` being the four corner heights and the 1/16 the float at 0x00B0B384. So the piece is two triangles, corners 0, 1, 2 and 0, 3, 2; the coordinates are world positions (corner 0 is the piece's lower corner, the others are 16 away). The game keeps every intermediate on the FPU at 53 bits; the code uses doubles.
- GetNormal (slot 57, ret 0xC): the gzcom-dll header (`float GetNormal(float, float)`) is wrong. The first argument is an out pointer to three words, then x and z. The vector is (`h0 - h3`, 16.0, `h0 - h1`) when `b > a` and (`h1 - h2`, 16.0, `h3 - h2`) otherwise; the answer is the out pointer.
- GetOccupiedCell (slot 59, ret 8): the cell of corner 0, each coordinate times 1/16 plus 12582912.0 stored as a float, its bits plus 0xB4C00000 (the floor trick of Read's origin).
- GetPosition (slot 75, ret 4; the occupant part's slot 8 at 0x00604E40 jumps to it): the average x and z of the four corners and the highest corner y (starts at -FLT_MAX; corner 0 counts when above 0x00AA7D74, which holds -FLT_MAX).
- The occupant part's slot 10 (0x00619360): the box at +0x124 (low) and +0x130 (high) copied out.
- ComputeBoundingBox (slot 80, ret 8): the position (the occupant part's slot 8, so slot 75 or a derived class's override) into the low pointer, copied to the high one, then widened by the four corners and by the three points of every model triangle record (+0x118). The interface's Init calls it with the object + 0x124 and + 0x130.
- ConnectsToNeighbor (slot 47, ret 8: the side, the network type): false unless the piece is of the type; else the type's edge value (slot 38) and the game's helper 0x00620EE0, which says whether the low two bits of byte `side` of the value are non-zero.
- SetRotationAndFlip (slot 74, ret 4): stores the byte at +0x104 (a plain setter, listed with the geometry).

**Paths.** The path information is the object at +0x140. GetPathInfo (slot 49) returns it, making it first when there is none: the resource key is {0x296678F7, group 0x69668828, instance}, the group 0xA966883F and the piece ID replaced by the path ID override when there is one, the instance whatever the network manager's slot 0x3C answers for that ID; the resource manager (global 0x00B45254) is asked for it as class 0xA9667793. With an override and a resource whose slot 0x40 says no, its slot 0x60 may give another one (slot 0x44 with 1). Its slot 0x64 (this) gives the path information; with an override that is made again by its slot 0x74 from the words at +0x168 and +0x16C and the x and z of corners 0 and 2, after the position was asked (not used). Then the information's slots 0x68 (this), 0xA0 (this), the eight words of the table at +0x144 are zeroed and slot 0x6C (this) is called. It is built lazily and the game asks for it when it draws a piece, so it follows the view. ReleasePathInfo (slot 50) releases and forgets it.

The table at +0x144 is eight words: four bytes for each of eight path types, indexed by `from` (0 to 3), with a bit for each `to` (0 to 3). **The header names slots 53 and 54 the wrong way round.** Slot 53 (0x006196A0) is the question (in the header AddPathConnection): without flag 0x2000 it first calls GetPathInfo, then with `from` and `to` both 0xFF counts the bits set in the type's four bytes, with only `from` 0xFF the bytes that have the bit of `to`, with only `to` 0xFF the bits in the byte of `from`, and with both given answers whether the bit is set. Slot 54 (0x00619790) is the add: with `from` not 0xFF it sets the bit of `to` in the type's byte for `from` and in the same byte of type 0 (the first row), and in any case sets flag 0x2000. The shift count is modulo 32 of the byte, the mask a byte.

**The edge store** (the sub-object at +0x108): Get (ret 4), Set (ret 8; a zero value removes a further entry, the last entry moves into its place, the array is freed when it was the only one; a first entry whose value is zero is taken by any other key), Assign (copies another store, ret 4; the interface's slot 44 jumps to it), Write (version 2, count, first key, first value, then each entry) and Read (version 2 only; the old array freed, the new one of count x 8 bytes; its padding bytes stay as the allocator gave them). The allocator is the game's (0x005E55E0, 0x005E5620). Other classes may hold a store too: the detours are at the functions' addresses.

### Slots 74 to 80

| Slot | Address | What | Ours |
| --- | --- | --- | --- |
| 74 | 0x00604DF0 | stores the rotation and flip byte | yes |
| 75 | 0x006193D0 | the position | yes |
| 76 | 0x0061AC70 | builds the piece's models (0x007D4530, 0x007D4070, 0x007D29F0, 0x007D41E0, 0x007D4280 on a model object, then 0x00619550) with their lighting | no, drawing |
| 77 | 0x0061C950 | looks a model up in (or adds one to) the hash map at 0x00B45278 through the resource service | no, the model cache |
| 78 | 0x0061C5F0 | releases every model of that map (0x008090E0) and clears it | no, the model cache |
| 79 | 0x0061BB60 | works out the corners' lighting bytes from the light object it is given (its slots 0x30 and 0x4C, normals through 0x005D6EA0) and sets flag 0x8000 | no, lighting |
| 80 | 0x0061A430 | the bounding box | yes |

IsPartOfTheSameStructure (slot 66, 0x006198A0) is left the game's: it asks both pieces for their complex piece origin into four stack words and compares them, and when a piece has none the call writes nothing and the words are whatever the stack held, so the answer for such pieces cannot be reproduced. It is called by the test only for pieces that have an origin, and only against themselves. Slot 65 is the shared stub 0x00648F10, TransformNode is not among these functions.

## Test

The fingerprint's `netocc` part walks every network occupant (through the occupant manager's grid, as the foundations are) and calls every read-only query of the piece through the interface vtable: all 32 flag bits but the lit bit with HasNetworkFlag, HasAnyNetworkFlag with four masks, the retrofit property, the node edge types, both path IDs, immovable, usable, placed, IsOfType for the 13 types, the piece ID, rotation, flip, variation, the corner heights both ways, the edge store for every type and the active, no-group and grouped edge counts, IsIntersection, the demolition cost, the passage ceiling, the one-way directions, the complex piece origin and the pylon flag. The count and the sum and XOR of a hash per object are printed with counters (` inter lit imm unus ow ceil retro node orig pyl placed pov var act`) so the reach of the test can be judged. The lit bit is only counted, not hashed.

The third piece adds (in `MixGeometry`): GetAltitude and GetNormal at the four corners, the middle, two points in each triangle and one on the diagonal, GetOccupiedCell, the occupant part's slots 8 and 10, slot 80 into local arrays, ConnectsToNeighbor for the four sides and 13 types, and IsPartOfTheSameStructure against itself for pieces that have a complex piece origin. Counters ` slope tilt cell wide conn same` follow the others. Not called in the fingerprint: GetPathInfo, ReleasePathInfo, PathConnectionExists (it builds the information) and AddPathConnection: the information is made when the game first needs it, which follows the view.

The `netocc` part also mixes what the save holds and the queries do not reach: the transform, each corner's x, y, z and last two words, the words at +0xFC and +0x100, the edge store's raw entries, the model triangle records, the box, whether the resource key object is set. Not mixed: the corners' four bytes and those of the model triangle records (the lighting follows the view).

`OPENSC4_TEST_NETOCC=<day>`: on that day every seventh network occupant (state hash a multiple of 7) goes through all the setters with a changed value, the answers are read back and hashed, and the value is restored (a property that was not there is removed with the holder's RemoveProperty; a retrofit property that was there and a path ID override property of value 0 are left alone). The same day one made object goes through the class's life (the game's factory, a reference held, QueryInterface through six parts for eight IDs, GetGZCLSID, the fields the constructor sets, the occupant part's Init on an object marked initialised, then the last Release through the deleting destructor and the pool); its hash is added to the checksum. The interface's Init and the occupant part's first branch are not called: they end in the queued occupant message 0x99EF1140 with an object that is in no cell and no network. The sum of the read-backs and the count are added as ` set <checksum>,<count>` to the part from then on. The third piece adds to the same day: SetRotationAndFlip with its read-back; for each exercised object `Paths`: GetPathInfo twice (an answer or not, the flag 0x2000, the eight table words, never the pointer), PathConnectionExists for path types 0 to 7 and `from`, `to` each 0xFF or 0 to 3, AddPathConnection (type 7, 3, 3, and one with `from` 0xFF) with the read-back in all four forms and the first row, then the table words and the flag are put back and the information is dropped if there was none before; and on the made object of the life test and a second one the edge store's Set, Get and Assign through slots 37, 38 and 44 (the first entry taken, replaced, emptied, taken by another key; entries added, replaced, removed from the end, from the middle and to empty; a zero for an absent key; Assign from a store with entries and from one without), Get for the keys 0 to 12 after each step. Write and Read of the store are the save's. Whether the path information was there before is not mixed. The posted messages and the path information drop are the side effects the test cannot undo.

## Parity

Before the first run: the game's `ret N` of all 43 against our signatures (no mismatch), every direct call and jump to the 43 addresses scanned for a caller that reads ECX, EDX or EAX afterwards beyond the documented result (none), and an independent reading of every function against its disassembly (no difference; the leftovers the game has in EAX after the setters are not reproduced, and no caller reads them).

Original against the four groups for a week (2026-10-12), every pair on the first try: the 12 saves 12 of 12, `saves-networks` 2 of 2, `saves-plopped` 3 of 3, `saves-ports` 3 of 3, the fingerprint's `netocc` part included (504 to 3,866 network occupants a city).

With `OPENSC4_TEST_NETOCC=3` on the 12 saves and `saves-networks`: the `netocc` part with its `set` checksum is the same on both sides on every day of all 14 saves (52 to 562 occupants exercised a city). The rest of the city is not comparable in that leg: after the test the original does not repeat itself (four runs, four outcomes; the messages the setters post reach the prop developer), so only the part is judged.

What no city answers yes to, so only the setters' read-back reaches those branches: a retrofit, node edge types, a path ID override, a complex piece origin. `IsIntersection` said no for every occupant of every save, and `IsLit` too; their yes branches have not run with ours. The network manager's retrofit test (`OPENSC4_TEST_NETMGR_RETROFIT=1`) makes some: it gives real pieces the retrofit property and runs the retrofit pass over them ([netmgr.md](netmgr.md)).

**Life and save (2026-10-12).** Before the first run: `ret N` of the 12 against our signatures, the register and EAX scans (clean), and an independent reading of each against its disassembly: eleven without a difference; Read differs only where the game goes on with uninitialised stack after the stream has already failed (ours goes on with 0) and, unprovable from the code alone, in the rounding of two floats for records older than version 7, which no save has (all 24,300 objects decoded from the 14 corpus saves are version 8, and the game runs its x87 at 53 bits, where our doubles are exact). The record's decoder (`tools/re/save_schemas/netocc.py`) consumes every object of those saves exactly, and the harness compares the record now.

Original against all seven groups for a week: the 12 saves 12 of 12, `saves-networks` 2 of 2, `saves-plopped` 3 of 3, `saves-ports` 3 of 3, every pair on the first try, the `netocc` part and the saved record included. Every city is loaded through our Read, constructor, factory and Init and saved through our Write. One run of the original died at startup on another thread before any city was loaded (the pair matched when run again). With `OPENSC4_TEST_NETOCC=3`, which now also makes an object with the factory, asks it for its interfaces, initializes and releases it: the `netocc` part with its `set` checksum the same on both sides on every day of the 14 saves.

A first version of the part also mixed in whether the object at +0x140 was there. It is fetched when a piece is first drawn, so the original gave five values for the part in eight runs of Big City; it is left out.

Not run with ours: Read for versions 4 to 7, the interface's Init on an object of the test's own making (it posts the occupant-added message for an object that is in no cell), and the destructor's paths for a piece with a path vector or a resource object.

**Geometry, paths and the edge store (2026-10-12).** Before the first run: `ret N` of the 17 against our signatures, the register and EAX scans (clean), and an independent reading of each against its disassembly, the float comparisons with their unordered branches included: no difference.

Original against all ten groups for a week: the 12 saves 12 of 12 (Making Money d1095 after two pairs: one run of the original differed in the advisors' checksums from the first day), `saves-networks` 2 of 2, `saves-plopped` 3 of 3, `saves-ports` 3 of 3, the rest on the first pair, the `netocc` part with the geometry queries included. With `OPENSC4_TEST_NETOCC=3` (path information fetched, path connections added, read back and put back, the edge store driven through every branch of its Set on two made objects): the `netocc` part with its `set` checksum the same on both sides on every day of the 14 saves.

Not run with ours: the edge store's Read for a version other than 2, and `IsPartOfTheSameStructure` is still the game's (it compares stack words a piece without an origin never wrote).

Our own objects (batch o1, 2026-10-13): the original against every network occupant group and the eight `.own` groups for a week, every class's copies in use in the runs that made or destroyed its objects (the `own: vtables copied` log line). Plain: the 12 saves 12 of 12 the same on the first pair, `saves-networks` 3 of 3, `saves-plopped` 3 of 3, `saves-ports` 3 of 3. With the test switches of all the network occupant classes but `OPENSC4_TEST_NETOCC`: `saves-networks` 3 of 3 and the 12 saves 11 of 12 on the first pair; on Making Money d0365 one run of ours in four gave other `adv` and `advisors` values from the first day on (two days before the tests run; every decoded record the same), and twelve more pairs of that save were all the same, so one run of ours in 16 against none of the original's 16: not explained and not reproduced.

## What is left

Of the interface: IsPartOfTheSameStructure (slot 66, see above), the shared stubs and thunks (12, 23, 30 to 33, 37, 38, 44 to 46, 48, 61, 62, 65; 37, 38 and 44 are the adjustor thunks to the edge store, which is ours now), TransformNode, and the drawing code for the Renderer thread: SetQuadDraw (30), AddPart (31), GetModelInstances (33), MarkLightingUpdateNeeded (46), SetTranslucency (58), SetLit (61), and slots 76 to 79 (models and lighting). The model part's constructor and destructor (0x00801CF0, 0x00801810) and the statics' set-up (0x0061C690) stay the game's.
