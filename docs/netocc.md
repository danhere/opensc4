# The network occupant (cSC4NetworkOccupant)

One piece of a network in the city: a road, rail, highway, pipe, power pole or subway tile, a bridge or a tunnel piece. It holds the piece's flags, its rotation and variation, the heights of its four corners, which of its four edges connect and how, its demolition cost and a few properties in its property holder. The network simulator makes and changes them; traffic, the renderer and the save code read them. The occupant base class ([occbase.md](occbase.md)) is at the object + 4. Build 641 addresses throughout (data/addresses/netocc.toml); class map and slot list in `src/core/netocc_sim.h`.

This is the first piece: 43 small functions of the interface (state and queries). The pieces to come: life and save (constructor 0x0061BDD0, factory 0x0061C480, Write and Read), geometry and paths (path information, path connections, altitude and normal, neighbour connections, transforming a node). The drawing code stays out for the Renderer thread: SetQuadDraw, AddPart, GetModelInstances, MarkLightingUpdateNeeded, SetTranslucency, SetLit (the flag setters here call SetTranslucency, the game's).

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

`OPENSC4_TEST_NETOCC=<day>`: on that day every seventh network occupant (state hash a multiple of 7) goes through all the setters with a changed value, the answers are read back and hashed, and the value is restored (a property that was not there is removed with the holder's RemoveProperty; a retrofit property that was there and a path ID override property of value 0 are left alone). The sum of the read-backs and the count are added as ` set <checksum>,<count>` to the part from then on. The posted messages and the path information drop are the side effects the test cannot undo.

## Parity

Before the first run: the game's `ret N` of all 43 against our signatures (no mismatch), every direct call and jump to the 43 addresses scanned for a caller that reads ECX, EDX or EAX afterwards beyond the documented result (none), and an independent reading of every function against its disassembly (no difference; the leftovers the game has in EAX after the setters are not reproduced, and no caller reads them).

Original against the four groups for a week (2026-10-12), every pair on the first try: the 12 saves 12 of 12, `saves-networks` 2 of 2, `saves-plopped` 3 of 3, `saves-ports` 3 of 3, the fingerprint's `netocc` part included (504 to 3,866 network occupants a city).

With `OPENSC4_TEST_NETOCC=3` on the 12 saves and `saves-networks`: the `netocc` part with its `set` checksum is the same on both sides on every day of all 14 saves (52 to 562 occupants exercised a city). The rest of the city is not comparable in that leg: after the test the original does not repeat itself (four runs, four outcomes; the messages the setters post reach the prop developer), so only the part is judged.

What no city answers yes to, so only the setters' read-back reaches those branches: a retrofit, node edge types, a path ID override, a complex piece origin. `IsIntersection` said no for every occupant of every save, and `IsLit` too; their yes branches have not run with ours.
