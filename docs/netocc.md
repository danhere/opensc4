# The network occupant (cSC4NetworkOccupant)

One piece of a network in the city: a road, rail, highway, pipe, power pole or subway tile, a bridge or a tunnel piece. It holds the piece's flags, its rotation and variation, the heights of its four corners, which of its four edges connect and how, its demolition cost and a few properties in its property holder. The network simulator makes and changes them; traffic, the renderer and the save code read them. The occupant base class ([occbase.md](occbase.md)) is at the object + 4. Build 641 addresses throughout (data/addresses/netocc.toml); class map and slot list in `src/core/netocc_sim.h`.

The first piece is 43 small functions of the interface (state and queries). The second is the class's life and save: Write, Read, GetGZCLSID, both Inits, QueryInterface, the constructor, the destructors, the pool's free and page set-up and the factory (12 functions). The piece to come: geometry and paths (path information, path connections, altitude and normal, neighbour connections, transforming a node). The drawing code stays out for the Renderer thread: SetQuadDraw, AddPart, GetModelInstances, MarkLightingUpdateNeeded, SetTranslucency, SetLit (the flag setters here call SetTranslucency, the game's).

## The class

Class ID 0xC9C05C6E, occupant type 0xC772BF98, interface IID 0xA821EF94. Virtual inheritance: the primary interface (vtable 0x00AA8080, 74 slots) at +0, the cISC4Occupant part at +4 (0x00AA7FF8; the base starts there), the property holder at +0xC, more parts at +0x8, +0x20, +0x38 and a virtual base through the table at +0x3C. The bridge, tunnel and pipe classes put the same functions in their vtables, and some are called directly: each function is replaced by a detour at its address.

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
| +0x118 | a vector (begin, end, capacity) of 0x4C-byte path records: a uint32 and three corner-shaped records |
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
| path records | | count (uint32), then each: uint32, then three corner-shaped records |
| the box | 6 floats | as (low, high) pairs for x, y, z |
| flags (+0x13C) | uint32 | read: below version 6 converted from the old bits; bits 0xE000 cleared |
| path ID override (+0x164) | uint32 | version below 7 and not zero: two more floats follow (the old complex piece origin); below 8 the retrofit flag is set |
| resource key (+0x140) | 3 x uint32 | type, group, instance; zeros for none |
| demolition cost (+0x170) | int64 | |

After Read the model state (virtual base slots 0x3C and 0x40) is set when corner 0's last byte is not 0xFF or bit 1 of the state's first byte is set: that bit cleared, all four corners' last byte set to 0xFF, the state's float at +0x10 set to 255 times 1/255.

The derived classes write their own version first and then this whole record, then their own fields: the bridge (0x49CC1BCD) and the pre-built model (0x49C1A034) start with a uint16 of 4 and then this record; the pipe (0x49C05B9F) with two uint16 of 3. None has only the base's part, so no schema is written for them.

## What is ours

First piece, 43 functions (below). Second piece, 12 functions in `src/core/netocc_life.cpp`, each a detour at its address:

| Group | Functions |
| --- | --- |
| `simulators.netocc.save` (3) | Write 0x0061A190, Read 0x0061CC00, GetGZCLSID 0x00619040 |
| `simulators.netocc.life` (3) | the occupant part's Init 0x0061CB50, the interface's Init 0x00619050, QueryInterface 0x006192C0 |
| `simulators.netocc.object` (6) | constructor 0x0061BDD0, destructor 0x0061C170, deleting destructor 0x0061C4C0, the pool's free 0x0061BD40 and page set-up 0x00619D50, the factory 0x0061C480 |

Not ours: the adjustor thunks to QueryInterface (0x0061BFC0, 0x0061BFE0, 0x0061BFF0, 0x0061C010, 0x0061C030), the thunks to the occupant base's Shutdown (0x0061BFD0, 0x00619100), the thunk to the deleting destructor (0x0061C020), the helpers the code calls at their addresses (the edge store's Write and Read, the path vector's resize, the old flags' conversion, the statics' set-up 0x0061C690, the model part's constructor, destructor and QueryInterface) and slot 80 (0x0061A430). The vtables are the game's, not copies. The constructor does not make the dead stores of the float at 0x00B4525C that the game overwrites at once.

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

## Test

The fingerprint's `netocc` part walks every network occupant (through the occupant manager's grid, as the foundations are) and calls every read-only query of the piece through the interface vtable: all 32 flag bits but the lit bit with HasNetworkFlag, HasAnyNetworkFlag with four masks, the retrofit property, the node edge types, both path IDs, immovable, usable, placed, IsOfType for the 13 types, the piece ID, rotation, flip, variation, the corner heights both ways, the edge store for every type and the active, no-group and grouped edge counts, IsIntersection, the demolition cost, the passage ceiling, the one-way directions, the complex piece origin and the pylon flag. The count and the sum and XOR of a hash per object are printed with counters (` inter lit imm unus ow ceil retro node orig pyl placed pov var act`) so the reach of the test can be judged. The lit bit is only counted, not hashed.

The `netocc` part also mixes what the save holds and the queries do not reach: the transform, each corner's x, y, z and last two words, the words at +0xFC and +0x100, the edge store's raw entries, the path records, the box, whether the resource key object is set. Not mixed: the corners' four bytes and those of the path records (the lighting follows the view).

`OPENSC4_TEST_NETOCC=<day>`: on that day every seventh network occupant (state hash a multiple of 7) goes through all the setters with a changed value, the answers are read back and hashed, and the value is restored (a property that was not there is removed with the holder's RemoveProperty; a retrofit property that was there and a path ID override property of value 0 are left alone). The same day one made object goes through the class's life (the game's factory, a reference held, QueryInterface through six parts for eight IDs, GetGZCLSID, the fields the constructor sets, the occupant part's Init on an object marked initialised, then the last Release through the deleting destructor and the pool); its hash is added to the checksum. The interface's Init and the occupant part's first branch are not called: they end in the queued occupant message 0x99EF1140 with an object that is in no cell and no network. The sum of the read-backs and the count are added as ` set <checksum>,<count>` to the part from then on. The posted messages and the path information drop are the side effects the test cannot undo.

## Parity

Before the first run: the game's `ret N` of all 43 against our signatures (no mismatch), every direct call and jump to the 43 addresses scanned for a caller that reads ECX, EDX or EAX afterwards beyond the documented result (none), and an independent reading of every function against its disassembly (no difference; the leftovers the game has in EAX after the setters are not reproduced, and no caller reads them).

Original against the four groups for a week (2026-10-12), every pair on the first try: the 12 saves 12 of 12, `saves-networks` 2 of 2, `saves-plopped` 3 of 3, `saves-ports` 3 of 3, the fingerprint's `netocc` part included (504 to 3,866 network occupants a city).

With `OPENSC4_TEST_NETOCC=3` on the 12 saves and `saves-networks`: the `netocc` part with its `set` checksum is the same on both sides on every day of all 14 saves (52 to 562 occupants exercised a city). The rest of the city is not comparable in that leg: after the test the original does not repeat itself (four runs, four outcomes; the messages the setters post reach the prop developer), so only the part is judged.

What no city answers yes to, so only the setters' read-back reaches those branches: a retrofit, node edge types, a path ID override, a complex piece origin. `IsIntersection` said no for every occupant of every save, and `IsLit` too; their yes branches have not run with ours.

**Life and save (2026-10-12).** Before the first run: `ret N` of the 12 against our signatures, the register and EAX scans (clean), and an independent reading of each against its disassembly: eleven without a difference; Read differs only where the game goes on with uninitialised stack after the stream has already failed (ours goes on with 0) and, unprovable from the code alone, in the rounding of two floats for records older than version 7, which no save has (all 24,300 objects decoded from the 14 corpus saves are version 8, and the game runs its x87 at 53 bits, where our doubles are exact). The record's decoder (`tools/re/save_schemas/netocc.py`) consumes every object of those saves exactly, and the harness compares the record now.

Original against all seven groups for a week: the 12 saves 12 of 12, `saves-networks` 2 of 2, `saves-plopped` 3 of 3, `saves-ports` 3 of 3, every pair on the first try, the `netocc` part and the saved record included. Every city is loaded through our Read, constructor, factory and Init and saved through our Write. One run of the original died at startup on another thread before any city was loaded (the pair matched when run again). With `OPENSC4_TEST_NETOCC=3`, which now also makes an object with the factory, asks it for its interfaces, initializes and releases it: the `netocc` part with its `set` checksum the same on both sides on every day of the 14 saves.

A first version of the part also mixed in whether the object at +0x140 was there. It is fetched when a piece is first drawn, so the original gave five values for the part in eight runs of Big City; it is left out.

Not run with ours: Read for versions 4 to 7, the interface's Init on an object of the test's own making (it posts the occupant-added message for an object that is in no cell), and the destructor's paths for a piece with a path vector or a resource object.

## What is left

Geometry and paths: GetPathInfo, AddPathConnection, GetAltitude, GetNormal, ConnectsToNeighbor, TransformNode and the other slots of the interface that are not among the 43 (12, 23, 30 to 33, 37, 38, 44 to 50, 53, 54, 56 to 59, 61, 62, 65, 66, 74 to 79; 37, 38 and 44 are shared thunks). The drawing code for the Renderer thread. The model part's constructor and destructor (0x00801CF0, 0x00801810) and the statics' set-up (0x0061C690) stay the game's.
