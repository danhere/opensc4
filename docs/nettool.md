# The network tool (cSC4NetworkTool)

The tool a player's drag of a network goes through: it turns the dragged cells into solved cells (a piece ID, a rotation and a flip per cell), fits the heights to the terrain, picks bridges and tunnels, asks the player about intersections, previews the result, works out the cost and places the pieces (handing them to a construction crew where the type has one). The network manager keeps one tool per network type and hands them to the network view's input controls, to its own lot and removal code and to the demolition. Nothing of the tool is saved. With it come the RUL engine that loads the networks' rule files at every city load, a vertex height constraint satisfier, a world cache of cell infos, a tool UI object and three tools of their own (pipe, subway, power line). The whole piece is 612 functions (196.5 KB); the tool proper is about 250. It is reimplemented in parts (below); this page has the first, the state and queries (46 functions, 43 of them ours), the second, the world cache and the cell infos (25 functions, 23 of them ours), the third, the tool's life and its preview drawable (27 functions, 23 of them ours), the vertex height constraint satisfier (s1: 46 functions, 45 of them ours), the RUL engine's load, statics and RUL0 store (r1: 29 functions, all ours), the type rows' rule files (r2: 35 functions, all ours), the RUL parsers' first part (r3: 45 functions, 44 of them ours), their second part (r4: 23 functions, all ours), and their third part (r5: 25 functions, all ours), which completes the RUL engine but its two guarded functions; the previews and highlights (t4: 23 functions, 20 of them ours), the tile solver (t8: 26 functions, 25 of them ours), the heights (t9: 20 functions, 19 of them ours), the occupants, tunnels and cost (t7: 23 functions, all ours), placing (t6: 34 functions, all ours), the bridges (t10: 27 functions, 16 of them ours; t11: 20 functions, all ours), the drag (t5: 17 functions, 15 of them ours), the puzzle and resolve part (t12: 6 functions, 4 of them ours), the underground tools (u1: 34 functions, 32 of them ours) and the power line tool (w1 and w2: 43 functions, 40 of them ours). With them 572 of the 612 functions are ours. Build 641 addresses throughout (the address entries of these parts come from the specs `p6/nettool1.spec`, `p6/nettool2.spec`, `t3/nettool3.spec`, `t3/nettool_s1.spec`, `t3/nettool_r1.spec`, `t3/nettool_r2.spec`, `t3/nettool_r3.spec`, `t3/nettool_r4.spec`, `t3/nettool_r5.spec`, `t3/nettool_t4.spec`, `t3/nettool_t8.spec`, `t3/nettool_t9.spec`, `t3/nettool_t7.spec`, `t3/nettool_t6.spec`, `t10/nettool_t10.spec`, `t10/nettool_t11.spec`, `t5/nettool_t5.spec`, `t12/nettool_t12.spec`, `u1b/nettool_u1.spec`, `u1b/nettool_u1b.spec`, `w1/nettool_w1.spec` and `w2/nettool_w2.spec` in the tools folder, with the survey's files).

## The class

The general tool is 0x368 bytes; constructor 0x0062C5F0 (ret 4: the network type), deleting destructor 0x0062CD30 (slot 51). No class ID of its own and no serializable part. The network manager's Init (0x00610C10) makes the 13 tools, one per type, and keeps them at its +0x88 + 4 x type; GetNetworkTool (0x006108D0) gives the kept one with `unique` 0 and a new, uninitialised one with `unique` set ([netmgr.md](netmgr.md)). Init (slot 3) runs once per object until Shutdown (slot 4); the manager's callers run Init, their calls and Shutdown around each use.

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AA8748 | `cISC4NetworkTool` + `cSC4NetworkTool` | 81, see the table below |
| +0x4 | 0x00AA8738 | `cIGZMessageTarget2` | thunks 0x0062C9A0, 0x005200D0 (shared), 0x0062C9B0; DoMessage 0x00624480 |

QueryInterface (0x00623290) answers IIDs 1 and 0x0A74CA15 (the tool's own) with the object and 0x452294AA with the part at +4.

The classes derived from it (verified constructors and vtable differences; inferred roles):

| Class | Size | Constructor | Vtables (+0, +4) | Slots of its own |
| --- | --- | --- | --- | --- |
| the underground tools' base | not made by itself (fields to +0x388 at least) | 0x00657FD0 (type) | 0x00AAA950, 0x00AAA93C | 3, 4, 16, 17, 46, 51, 54 to 65, 68 to 71, 73 to 80 |
| the pipe tool | 0x398 | 0x00648EC0 | 0x00AA99A8 (83), 0x00AA9994 | the base's, slot 81 (folded with the pipe occupant's class ID getter), 82 |
| the subway tool | 0x398 | 0x00653D60 | 0x00AAA5C0 (83), 0x00AAA5AC | the base's, 81 (folded), 82 |
| the power line tool | 0x3B8 | 0x006503C0 | 0x00AA9F30 (83), 0x00AA9F20 | 3, 4, 10, 16, 20, 51, 52, 53, 81, 82 |

### The true slot order

gzcom-dll's `cISC4NetworkTool.h` has slots 0 to 50 in the right order (checked by each function's `ret N`), with one wrong count: UpdateOnZoneChange (slot 10) is `ret 0x14`, five arguments, where the header has none. Slot 51 is the deleting destructor, which the headers leave out; `cSC4NetworkTool.h`'s 29 methods are slots 52 to 80. Two more header comments are wrong: ForceReload sets the byte +0x53 (not a no-op), and GetBridgeAltitudeOverride gives the float at +0x204 in ST0 (the header has it as a void no-op). "=" marks code the tool shares with other classes.

| Slot | Address | Function (arguments from its own `ret N`) | Part |
| --- | --- | --- | --- |
| 0 | 0x00623290 | QueryInterface(iid, out) | t1 |
| 1 | =0x00695330 | AddRef | shared |
| 2 | 0x00623270 | Release | t1 |
| 3, 4, 5 | 0x006269C0, 0x00626D30, 0x00626F90 | Init, Shutdown, Reset | t3 |
| 6 | 0x00623B20 | GetUITool(out) | t3 |
| 7, 8 | 0x00629210, 0x00624FD0 | PlaceNetworkOccupants(8), PlaceNetworkOccupantById(5) | t6 |
| 9 | 0x006248D0 | OccupantExists(x, z, mask, out) | t1 |
| 10 | 0x0062C900 | UpdateOnZoneChange(5 words, none read) | t1 |
| 11 | 0x0062C8C0 | ForceReload | t1 |
| 12 | =0x00648F10 | SetTraceFinal (`ret 4`, nothing) | shared |
| 13, 14 | 0x0062C8E0, 0x0062C8F0 | ClearFailureReason, GetFailureState | t1 |
| 15 | 0x0062CA40 | GetFailureReasonString(text) | t1 |
| 16, 17 | 0x00625580, 0x00625F10 | ClearOccupantHighlights, ClearCellPreviews | t3 |
| 18 | 0x0063AF40 | DrawNetworkLine(start, end, cache, type) | guarded |
| 19 | 0x0063BDC0 | PlaceAllSegments(4) | t6 |
| 20 | 0x00625640 | GetCostOfSolution | t7 |
| 21 | 0x0062C8D0 | EnablePreviewUpdate(byte) | t1 |
| 22 | 0x0062CA20 | SetAnchorPoints(points) | t1, left |
| 23, 24 | 0x0062CA30, 0x0062CA10 | GetDraggedCells(out), SetValidStepCount(n) | t1 |
| 25 to 27 | =0x00407AB0, =0x00407AC0, =0x00809240 | GetValidStepCount, GetStraightStepCount, GetTotalStepCount | shared |
| 28 to 30 | 0x0062C9C0, 0x0062C9E0, 0x00626E80 | IsRegularStep(i), GetLastValidPoint(out), GetPossibleBridges(out) | t1 |
| 31, 32 | =0x00623B70, =0x00623B80 | GetPreferredBridge, SetPreferredBridge(id) | t1 (folded) |
| 33, 34 | 0x00623B90, 0x00623BA0 | GetBridgeAltitudeOverride, SetBridgeAltitudeOverride(float) | t1 |
| 35 to 41 | 0x00624AF0, 0x00624130, 0x00624180, 0x00624150, 0x006241B0, 0x00624B20, 0x006241D0 | GetBridgeName(text, id), GetMajorBridge, GetMajorBridgeAltitude(out), GetMajorBridgeReferenceAltitude(out), IsMajorBridgeLandBridge, GetBridgeBitmapKey(id, out, out), GetPossibleNeighborConnection(out) | t1 |
| 42 to 45 | 0x0062C910, 0x0062C930, 0x0062C940, 0x0062C950 | ViewInputControlId, CursorId, DirectionalCursorBase, PenWidth(out, out) | t1 |
| 46 | =0x00648F00 | StopPickAtWater (true) | shared |
| 47, 48 | 0x0062C920, 0x0062CAD0 | GetToolViewId, GetPlacementParams(out) | t1 |
| 49 | 0x0062D450 | QueryUserForPotentialIntersections | t12 |
| 50 | 0x0062A5C0 | DestroyOccupantsAndResolve(list) | t12 |
| 51 | 0x0062CD30 | the deleting destructor(flags) | t3 |
| 52 | 0x0062BF60 | GetFailureReasonString(reason, text) | t1 |
| 53 | =0x0062C980 | DoNetworkSpecificPlacement(2) | t1 (folded) |
| 54, 55 | 0x00624210, 0x006258C0 | CreateNetworkOccupant(4), InsertOccupantsForSolvedCells(3) | t7 |
| 56, 57 | 0x00644B20, 0x00644530 | CanTilesBeSupportedOnTerrain(type), UpdateTerrainHeights(out) | t9 |
| 58, 59, 60 | 0x006246A0, 0x006299A0, 0x00624920 | AddCellPreview(2), UpdateOccupantHighlights(1), DestroyHighlightedOccupants | t4 |
| 61 to 64 | =0x00648F00 x3, =0x00648EF0 | AllowBridges, AllowTunnels, IsStoppedByWater (true), ShouldAutoConnect (false) | shared |
| 65 | 0x00638680 | TestForAutoConnectOccupant(x, z) | t5 |
| 66 | 0x00638710 | IdentifyStartingConditions(count out) | t5 |
| 67 | 0x006392F0 | CheckForReservedTilesAt(x, z, type), one cell | t5 |
| 68 | 0x00639170 | CheckForReservedTiles(count out, type, skip), one step, calls slot 67 per cell | t5 |
| 69 | 0x00638990 | CheckForGoodTerrain(count out) | t5 |
| 70 | =0x00407990 | StartPlacing (nothing) | shared |
| 71, 72 | 0x00633250, 0x006278A0 | CreateCellInfosForCurrentPlacementTiles(1), InsertNetworkOccupant(3) | t2, t7 |
| 73 | 0x006444F0 | GetNetworkVertexHt(1) | t9 |
| 74 | 0x0062C990 | GetNetworkHeightForTerrainHeight(float) | t1 |
| 75 | =0x00407990 | PropagateSolvedCellsForSmoothing (nothing) | shared |
| 76, 77 | 0x006360C0, 0x00637B90 | InsertRangeConstraintsForCell(1), ApplyPlaneConstraintToRegularCells(1) | t9 |
| 78, 79 | =0x00648F10 | InsertNetworkSpecificConstraintsForCell, ReloadNetworkSpecificInfo (`ret 4`, nothing) | shared |
| 80 | 0x00623CE0 | SetQuadTrianglesToDraw(2) | t4 |

The names of slots 52 to 80 are the header's; the argument counts are the code's.

### Fields

`src/core/nettool_sim.h` has the full list with its evidence (the constructor, Init, Reset, Shutdown and the functions named). The ones this part reads or writes:

| Offset | What |
| --- | --- |
| +0x14 | reference count (the survey had +0xC; Release reads +0x14, the constructor clears it) |
| +0x18 | initialised (Init sets it once, Shutdown clears it) |
| +0x48 | the occupant manager (Init references 0x00B43D0C; Shutdown releases it) |
| +0x53 | ForceReload's byte |
| +0x54 | the drag's steps: a vector of 0xC-byte steps (first cell, last cell, a word that is 0 for a regular step) |
| +0x60 | the dragged cells: a vector of 8-byte points |
| +0x70, +0x78 | the step count (inferred) and the valid step count |
| +0xD8 | the major bridges: a vector the getters read from its end (the last element's ID at end - 0x1C, altitude - 0x18, reference altitude - 0x14, land bridge byte - 0x10); GetCostOfSolution walks it in steps of 0x28, so the survey's "0x1C stride" is the getters' offset, not the element size |
| +0x10C | the possible neighbour connections: the getter copies the first element's +0x8, +0xC, +0x10 and byte +0x14 |
| +0x1F4 | the possible bridges: a vector of uint32 |
| +0x200, +0x204 | the preferred bridge (-1) and the bridge altitude override (-1.0f): the constructor and Reset set them |
| +0x20C | the failure reason: 0 or 0xF00000nn (placement code writes it); the constructor does NOT write it, Reset clears it through slot 13 |
| +0x234 | EnablePreviewUpdate's byte (1) |
| +0x240, +0x244 | the city's cells in x and z (Init) |
| +0x290 | the placement parameters (0xAC bytes, from the type row's +0x50 by Init); their byte +0x25 is the object's +0x2B5, the diagonal flag NAM's patches test |
| +0x358 | the type's row of the type table (Init; null before the first Init) |
| +0x35C | the network type |
| +0x360, +0x364 | the mask of the type's class and a network occupant filter of it (Init; the survey's list stopped at +0x35C) |

The type table at 0x00B452C8 has 13 rows of 0x114 bytes, filled by the RUL engine's load. This part reads a row's +0x8 (view input control ID), +0x10 (tool view ID), +0x14 (cursor), +0x18 (directional cursor base), +0x1C and +0x20 (pen width), +0x30 (a vector of 0x178-byte bridge records: +0x148 and +0x14C the bitmap's group and instance, +0x158 the name, a game string whose characters' begin at +0x15C GetBridgeName hands on) and +0x50 (the placement parameters).

## The NAM guard

The NAM DLL and sc4fix patch inside nine functions of the piece; they stay the game's for good. None is in the first part.

| Site | Who and what | Function | Part |
| --- | --- | --- | --- |
| 0x006099C4 | NAM FlexPieces: an immediate 2 made 0 | 0x006099A0 | t12 |
| 0x0062CEDC | NAM FlexPieces: a jump out, back at 0x0062CF42; uses the game's frame | 0x0062CD50 | t12 |
| 0x00634D79 | NAM Rul2Engine: its own RUL2 pass, back at 0x00635282 | 0x00634D70 | t8 |
| 0x006366C5 | NAM NetworkSlopes: a jump out, back at 0x00636907 | 0x00636690 | t9 |
| 0x00637F80 | NAM diagonal streets: a conditional jump made unconditional (tests +0x2B5) | 0x00637EB0 | t5 |
| 0x0063AFF2 | NAM diagonal streets: the same (tests +0x2B5) | 0x0063AF40 (DrawNetworkLine) | t5 |
| 0x0063D316 | NAM Rul2Engine: the RUL2 line callback's insert skipped | 0x0063D1E0 | r3 |
| 0x006459BC | NAM ferry bridge height: the operand of `fadd [0x00AA90B0]` pointed at a DLL float | 0x00645980 | ui1 |
| 0x0065EE3E, 0x0065EE66 | sc4fix PuzzlePieceTE (OpenSC4's fixes.puzzle_piece also detours the entry) | 0x0065ED80 | s1 |

What NAM needs beyond those bytes, and every part keeps:

- NAM calls the game at fixed addresses in the piece: 0x00633220 (a cell's entry), 0x00647A20 (the world cache's GetCell), 0x00623E60 (GetFirstNetworkTypeFromFlags, this part), 0x00636000, 0x00636030, 0x00636360, 0x0065FF60. A detour there is fine (NAM then calls ours) if it keeps the function's convention, its arguments and the registers its game callers rely on. One thing is checked by reading only: our replacements leave the processor flags as their own last instruction leaves them, not as the game's last compare did. No compiled caller in the exe reads the flags across a call; whether NAM's hand-written stubs do could not be checked (NAM is not installed in parity runs, and its binary is not on the test machine).
- NAM reads and writes by layout: the tool object (+0x1C, +0x54, +0x60, +0x98, +0xCC, +0x124, +0x188, +0x240, +0x244, +0x2B5), the 0xBC-byte cell info (+0x8 flags, +0xC occupant, +0x18 the words by type, +0x4C edge flags, +0x53, +0x56, +0xA8 vertices, +0xB8 the solved cell's index), the 0xC-byte solved cells, the type table's rows (+0xF0 to +0x110 among others), the RUL0 rule objects (0xFC bytes, edited in place by FlexPieces) and the RUL2 multimap at 0x00B466D0. So every part reads and writes these in the game's memory with the game's layouts, and nothing is cached privately across a call into a guarded function.
- NAM is not installed on the test machine, so parity runs the guarded functions without the stubs: the rule above is a compatibility rule that parity cannot show.

## The parts

The survey's split (`p6/survey.txt`, section 5), in the order planned: state and queries, life, the owned objects, the RUL engine (separate, all at load), then solving and placing.

| Part | Functions | Bytes | Guarded | What |
| --- | --- | --- | --- | --- |
| t1 state and queries | 46 | 1,749 | none | this page |
| t2 cache and cells | 25 | 4,834 | none | the world cache, the cell info lookup and making (NAM calls 0x00647A20 and 0x00633220), slot 71; [below](#what-the-second-part-covers) |
| t3 life and previews | 27 | 5,715 | none | constructor, destructors, Init, Shutdown, Reset, DoMessage, GetUITool, the two Clear slots, the preview drawable but its three drawing functions; [below](#what-the-third-part-covers) |
| s1 satisfier | 46 | 8,850 | 0x0065ED80 | the vertex height constraint satisfier; [below](#the-satisfier-part-s1) |
| r1 to r5 the RUL engine | 157 | 47,007 | 0x0063D1E0 | the load, the network ini, the rule records and the three parsers; r1 (29: the load, the statics, the RUL0 store) [below](#the-rul-engine-part-r1); r2 (35: the rule files) [below](#the-rule-files-part-r2); r3 (44: parsers, maps) [below](#the-rul-parsers-part-r3); r4 (23: the RUL0 line, bridge and check type parsers, exemplars, tile verifiers) [below](#the-rul-parsers-second-part-r4); r5 (25: RUL0/RUL1 loads, Ordering, the bridges) [below](#the-rul-parsers-third-part-r5) |
| t4 previews and highlights | 23 | 6,671 | none | AddCellPreview, UpdateOccupantHighlights, DestroyHighlighted, SetQuadTrianglesToDraw; 20 ours, [below](#the-previews-and-highlights-part-t4) |
| t5 drag | 17 | 9,520 | 0x0063AF40, 0x00637EB0 | the drag path builder 0x00639790 and the start, reserved and terrain slots; 15 ours, the 2 guarded stay the game's, [below](#the-drag-part-t5) |
| t6 place | 34 | 11,952 | none | PlaceAllSegments, the segment placer, PlaceNetworkOccupants(ById), MarkInvalidSteps 0x00624BF0; all ours, [below](#placing-part-t6) |
| t7 occupants | 23 | 11,634 | none | slots 54, 55, 72, tunnels, the cost (slot 20); all ours, [below](#the-occupants-tunnels-and-cost-part-t7) |
| t8 solve | 26 | 11,195 | 0x00634D70 | the tile solver 0x00635CB0; 25 ours, [below](#the-tile-solver-part-t8) |
| t9 heights | 20 | 11,290 | 0x00636690 | the height pass and its constraints; 19 ours, [below](#the-heights-part-t9) |
| t10 bridges | 27 | 15,643 | none | the major bridges' cells and occupants, the cable sort; 16 ours, 11 stay the game's (model geometry), [below](#the-bridges-parts-t10-and-t11) |
| t11 bridge picking | 20 | 7,640 | none | where a bridge goes, which record, how high, its cells; all ours, [below](#the-bridges-parts-t10-and-t11) |
| t12 puzzle and resolve | 6 | 11,445 | 0x0062CD50, 0x006099A0 | slots 49 and 50, the RUL0 rule match and the store's lookup; 4 ours, the 2 guarded stay the game's, [below](#puzzle-and-resolve-part-t12) |
| u1 underground | 34 | 8,442 | none | the underground base, the pipe and subway tools |
| w1 power | 43 | 14,679 | none | the power line tool; 40 ours (w1 and w2), [below](#the-power-line-tool-part-w1) |
| ui1 tool UI | 38 | 8,232 | 0x00645980 | the tool UI object (real-time clock and key reads) |

## What the first part covers

All in `src/core/nettool_state.cpp`, three groups, off by default. `OPENSC4_NETTOOL_SKIP` (function addresses, eight hex digits) leaves any of them out; each logs `nettool: first <name>` on its first call.

| Function | Address | Group | What it does |
| --- | --- | --- | --- |
| QueryInterface | 0x00623290 | `state` | IIDs 1 and 0x0A74CA15 the object, 0x452294AA the part at +4, referenced through slot 1; AL 1, else AL 0 over the ID less 0x452294AA |
| Release | 0x00623270 | `state` | above 1 the count at +0x14 goes down and is the answer; else slot 51 with 1 and 0 |
| ForceReload | 0x0062C8C0 | `state` | the byte +0x53 set |
| ClearFailureReason, GetFailureState | 0x0062C8E0, 0x0062C8F0 | `state` | +0x20C cleared; 1 when it is not 0 |
| GetFailureReasonString | 0x0062CA40 | `state` | a game string on the stack (8 bytes from the STL allocator, vtable 0x00A80810), slot 52 with the reason, then the caller's string's slot 3 with the characters; the characters freed |
| GetFailureReasonString(reason, text) | 0x0062BF60 | `state` | slot 52, `this` not read: the string emptied, the LTEXT of group 0x2A592FD1 and the reason (0x00603040), else "## Missing failure reason ##" (0x00AA8714) through a temporary string |
| EnablePreviewUpdate | 0x0062C8D0 | `state` | the byte stored at +0x234 |
| GetDraggedCells | 0x0062CA30 | `state` | the cells at +0x60 assigned to the caller's vector (0x00611B00) |
| SetValidStepCount | 0x0062CA10 | `state` | +0x78 set, then MarkInvalidSteps 0x00624BF0 (the survey's "tail jump to 0x00611B00" is slot 22's; the header's name fits) |
| IsRegularStep, GetLastValidPoint | 0x0062C9C0, 0x0062C9E0 | `state` | a step's third word is 0; the dragged cell indexed by the last valid step's second word (no bounds in either) |
| GetPossibleBridges | 0x00626E80 | `state` | false for none, else the vector at +0x1F4 assigned to the caller's (0x00462C10) |
| GetBridgeAltitudeOverride, SetBridgeAltitudeOverride | 0x00623B90, 0x00623BA0 | `state` | the float at +0x204 in ST0; its four bytes stored |
| CopyPlacementParams | 0x00626860 | `state` | the 0xAC-byte parameters assigned member by member (vectors through 0x00462C10 and 0x00464430) |
| GetPlacementParams | 0x0062CAD0 | `state` | the tool's +0x290 copied out through 0x00626860 |
| GetNetworkHeightForTerrainHeight | 0x0062C990 | `state` | the float back (the general and the power line tool) |
| ViewInputControlId, GetToolViewId, CursorId, DirectionalCursorBase, PenWidth | 0x0062C910 to 0x0062C950 | `queries` | the type row's +0x8, +0x10, +0x14, +0x18, +0x1C and +0x20 (the row not tested for null) |
| GetBridgeName, GetBridgeBitmapKey | 0x00624AF0, 0x00624B20 | `queries` | the bridge record's name (the characters of the game string at +0x158) to the string's slot 4; its group and instance when both are set |
| GetMajorBridge and its three getters | 0x00624130, 0x00624180, 0x00624150, 0x006241B0 | `queries` | -1 or false with no major bridge, else the words and the byte before the vector's end |
| GetPossibleNeighborConnection | 0x006241D0 | `queries` | false for none, else the first one's three words and byte |
| OccupantExists | 0x006248D0 | `queries` | the cell's entry (0x00633220, which makes one when the cache has none) tested; its word for the tool's type |
| GetNetworkOccupantAt | 0x00624B70 | `queries` | inside the tool's city size: the occupant manager's first occupant of the cell through a filter, asked for the network occupant interface |
| DirectionOffsets, DirectionStep | 0x00620D80, 0x00620DA0 | `queries` | the direction step tables 0x00B0B3D8 and 0x00B0B3F4 (28 signed bytes each) |
| CopyWord | 0x00620DD0 | `queries` | one word copied (the world cache's only) |
| EdgeByte, EdgeConnects | 0x00620DE0, 0x00620EC0 | `queries` | a side's byte of a word of four; whether its low two bits are set |
| GetFirstNetworkTypeFromFlags | 0x00623E60 | `queries` | the first type 0 to 11 whose flag (table 0x00AA835C) meets the flags, else 12 |
| GetLowestNetworkTypeFromFlags | 0x00623EC0 | `queries` | the flags' lowest bit written over the argument, then 0x00623E60 |
| GetPreferredBridge, SetPreferredBridge | 0x00623B70, 0x00623B80 | `slots` | +0x200; folded code (the city's GetNetworkManager; 19 other vtables) |
| DoNetworkSpecificPlacement | 0x0062C980 | `slots` | false; "xor al, al; ret 8" in about 70 vtables |
| UpdateOnZoneChange | 0x0062C900 | `slots` | nothing, five arguments; three bytes, too short for a detour |

The `slots` group swaps those four in every vtable of the tool family that holds the game's address: the general tool, the underground base, the pipe and the subway tool, and for slots 31 and 32 the power line tool (it has its own 10 and 53). The other classes' vtables keep the game's code. A detour of this part is the subclasses' code too where their vtables keep the general tool's address.

Registers: GetFirstNetworkTypeFromFlags, its lowest-flag form and the two edge byte helpers go through naked thunks that keep EDX and leave ECX as the game's do (the flags; the index times 8), because the slot 50 code at 0x0062B8E9 pushes EDX right after a call of 0x00623E60. NAM's call of 0x00623E60 keeps its cdecl convention.

### What stays the game's

- The adjustor thunks of the part at +4: 0x0062C9A0 and 0x0062C9B0 (they jump to our QueryInterface and Release).
- SetAnchorPoints 0x0062CA20 (`add ecx, 0x118; jmp 0x00611B00`): the shape of an adjustor thunk, into the point vector's assignment, shared template code with eighteen callers that is the game's everywhere.
- Called at their addresses and not this part's: the cell entry 0x00633220 (t2), MarkInvalidSteps 0x00624BF0 (t6), the vector assignments 0x00611B00, 0x00462C10 and 0x00464430, the localized text 0x00603040, the string helpers 0x00408480 and 0x00408A70, the STL allocator and free.

## What the second part covers

All in `src/core/nettool_cache.cpp`, two groups, off by default: `simulators.nettool.cache` (13 detours) and `simulators.nettool.cells` (10 detours). The same skip list (`OPENSC4_NETTOOL_SKIP`) and first-call lines (`nettool: first <name>`) as the first part. Every call between these functions goes to the game's address, so each detour stands alone and the skip list bisects them. None of the nine guarded functions is in this part, and none of its functions calls one (0x0062CD50, guarded, is a caller of the cache's Reset, not a callee).

### The cell info

0xBC bytes, the world cache's unit: what the tool knows of one cell while it solves a drag. The layout from the makers (0x00633030 from a cell's network occupant, 0x00632EF0 for a network type, slot 71 for the placement), the constructor 0x00647850, the copy 0x006478A0 and the assignment 0x00632D50; the names in brackets are NAM's (`NetworkStubs.h`, `cSC4NetworkCellInfo`):

| Offset | What |
| --- | --- |
| +0x00, +0x04 | x and z |
| +0x08 | network flags [networkTypeFlags]: the occupant's slot 0x30 & 0x1FFF, or the type's flag from 0x00AA8980; slot 71 ORs 0x40000 for a step of kind 1 and 0x80000 for kind 2 |
| +0x0C | the network occupant, referenced (copy and assignment take a reference, the clear of a table releases it) [networkOccupant] |
| +0x10 | byte: in the current placement (slot 71 sets it) |
| +0x14 | the index of the step that has the cell (slot 71); -1 from an occupant, 0 from 0x00632EF0 |
| +0x18 | 13 edge words, one per network type: the occupant's slot 0x98 for each type its slot 0x58 says it is [edgesPerNetwork] |
| +0x4C | the 13 words ORed (0x00647830) [edgeFlagsCombined] |
| +0x50 | byte: the occupant's slot 0x114 (inferred: its one-way directions; the RUL0 marking compares it with a rule's rotation byte) |
| +0x51 | byte: made from an existing occupant (OccupantExists tests it) |
| +0x52 | byte: the occupant has the flag 0x100000 |
| +0x53 | byte: immovable: the occupant's slot 0x44, or a step whose kind is not 0 [isImmovable] |
| +0x54, +0x57 | bytes, always 0 |
| +0x55 | byte: the step's kind is 3 |
| +0x56 | byte: the occupant has the flag 0x200000 [isNetworkLot] |
| +0x58 | the four corners' heights (floats: the occupant's slot 0x88 or the terrain's altitude) |
| +0x68 | four (low, high) pairs of floats, one per corner: -FLT_MAX, FLT_MAX from an occupant; the height less and plus a margin in slot 71; water raises the low |
| +0x88 | four more pairs: the same start, the height twice in slot 71, or 0xFDFFFFFF, 0x7DFFFFFF for a new cell; water raises the low |
| +0xA8 | the four corners' vertex indices NW, SW, SE, NE (stride the tool's +0x248) [vertices] |
| +0xB8 | the index into the solved cells, -1 [idxInCellsBuffer] |

The bytes +0x11 to +0x13 are padding: no function copies them.

### The world cache

cSC4NetworkWorldCache, 0x28 bytes at the tool's +0x1C (constructor 0x00648000, from the tool's constructor; destructor 0x00647AE0, from the tool's). Two hash tables of cell infos keyed by (z << 16) + x: the cells at +0x0, a copy at +0x14. A table: +0x4 the bucket vector (100 buckets to start, from 0x00829BC0; it grows by 0x004F0940), +0x10 the count. A node: 0xC4 bytes from the STL allocator: the next node, the key, the cell info. GetCell looks only in the cells' table.

| Function | Address | Group | What it does |
| --- | --- | --- | --- |
| Construct | 0x00648000 | `cache` | both tables' bucket vectors and counts cleared, 100 buckets each |
| Destruct | 0x00647AE0 | `cache` | the copy cleared and its buckets freed, then the cells |
| Reset | 0x00647B20 | `cache` | both tables cleared (the tool's Reset, the placement code, the underground tools' rebuild) |
| ClearTable | 0x006479B0 | `cache` | every node freed after its cell's occupant is released, every bucket null, the count 0 |
| GetCell | 0x00647A20 | `cache` | the cell info of a key or 0; NAM calls it (below) |
| InsertNode | 0x00647A60 | `cache` | the table grown for one more, a node with the key and a copy of the cell info first in its bucket |
| FindOrInsert | 0x00647BB0 | `cache` | the cell info of a key, inserted as a default cell info when missing |
| FindOrInsertByValue | 0x00647E10 | `cache` | the same with the key by value (what the makers call) |
| ClearCellIndices | 0x00647B40 | `cache` | every cell's +0xB8 set to -1 |
| AssignTable | 0x00647CC0 | `cache` | a table made a copy of another (buckets reserved and filled with nulls, the chains copied in order) |
| RestoreCells, SaveCells | 0x00647E20, 0x00647E40 | `cache` | the copy into the cells, the cells into the copy (the drag code 0x0063B830) |
| CollectCells | 0x00647E60 | `cache` | every cell appended by copy to a vector of 0xBC-byte cell infos (the underground tools' slot 70, 0x00658710) |

### The cell infos and the tool's code that makes them

| Function | Address | Group | What it does |
| --- | --- | --- | --- |
| CombineEdges | 0x00647830 | `cells` | the 13 edge words ORed into +0x4C |
| the constructor | 0x00647850 | `cells` | the occupant, the edge words, +0x4C and +0x50 cleared, +0xB8 -1; nothing else |
| the copy | 0x006478A0 | `cells` | every field (the occupant referenced; the edge words through CopyWord 0x00620DD0, part t1) |
| the assignment | 0x00632D50 | `cells` | every field; the occupant as a reference (the new one referenced, the old released) |
| the cell entry | 0x00633220 | `cells` | the cache's cell info, else a new one from the cell's occupant; NAM calls it (below) |
| MakeCellInfo | 0x00633030 | `cells` | the cell's network occupant through 0x00624B70 (the tool's filter); none: 0; else a cell info from its types, edges, flags, bytes and heights, written into the cache |
| slot 71 CreateCellInfosForCurrentPlacementTiles | 0x00633250 | `cells` | every dragged cell of every valid step: its cell info (cached, made, or new from the terrain where there is no occupant), marked with the type's flag, the step and its kind, the height margins (the parameters' +0x14 for a known cell, +0x34 for a new one) and the water's height at each corner |
| MarkRuleCells | 0x006097E0 | `cells` | a RUL0 rule's cells at an offset: each cell info tested against the rule's edge patterns by type (the one-way type 10 also by its directions against the rule's rotation byte, table 0x00AA752C); a record for each that does not fit (0x00629170) |
| NeighborIsClear, NeighborEdges | 0x00623F40, 0x00623EE0 | `cells` | the two slots of the neighbour walker (vtable 0x00AA8700, made by 0x00627080 in part t4: a cell, the tool, a type, a mask): whether the cell one step in a direction has none of the mask's networks, and its edge word for the type |

### How NAM's two entries are kept

NAM's DLL calls GetCell 0x00647A20 (with the cache) and the cell entry 0x00633220 (with the tool, `GetCellInfo` in its source) from compiled C++, both `__thiscall` with one argument through function pointers; its RUL2 engine and its NetworkSlopes hook read the cell infos they give by layout. Both detours keep the convention and `ret 4` and go through naked thunks:

- GetCell leaves what the game's leaves: EAX the cell info (the node + 8) or 0, ECX the cache (the game's never writes it), EDX the node or 0, and the flags of the game's last instruction (`test edx, edx` when found, `xor eax, eax` when not).
- The cell entry, when the cache has the cell, leaves ECX the cache (the tool + 0x1C) and EDX the node, as the game's does after its GetCell, and the flags of its `test eax, eax`. When the cache has not, the thunk calls the maker 0x00633030 itself, last, at its address, so whatever the maker leaves comes back as in the game (the game's maker ends with one or two Release calls of occupants, so that was never a fixed state).
- The register scan of every game caller (76 of 0x00633220, 10 of 0x00647A20, both branches followed) found none that reads ECX or EDX before writing it and no call that takes a stale ECX as `this`. The same scan over the callers of every other function of the part found nothing either; CombineEdges (ECX the cell, EDX the cell + 0x4C) and the constructor (ECX and EDX 0) keep the game's registers through thunks anyway.
- The cell infos and the cache stay where the game keeps them, in its layout: nodes of 0xC4 bytes and vectors of 0xBC-byte cell infos from the game's STL allocator, the buckets through the game's own vector and hash helpers, temporaries on the stack as the game has them. Nothing is copied or cached by our code, and nothing here calls a guarded function.

### What stays the game's

- 0x00632EF0 (ret 0xC: x, z, a network type; called by 0x006371E0 and 0x006384E0 of part t9): it builds a cell info for a type on its stack and writes it into the cache, but never sets the second pairs (+0x88 to +0xA4), so eight words of the cache are its stack's leftovers. A replacement cannot write the same words, and leaving it the game's does not keep the game's bytes either: they are what its callers' earlier calls (the cell entry 0x00633220 and its maker, ours in this part) left at that depth. No reader of those pairs is known: slot 71 sets them before it compares the water, and slot 76 reads only the first pairs (+0x68).
- 0x00629170 (ret 0x18; called only by MarkRuleCells): it appends a 0x14-byte record (the cell, a pattern, a type and two bytes) to the tool's vector +0xF0 from a copy on its stack whose last two bytes it never writes; the vector gets its stack's leftovers there. MarkRuleCells calls it at its address.
- FindOrInsert (ours) builds its default cell info with the constructor's fields set and the other words 0, where the game's has its stack's leftovers (which the copy writes into the node). Every game caller of 0x00647E10 either assigns a whole cell info over the answer at once (0x00632EF0, 0x00633030, slot 71, 0x00657DA0) or asks for keys it inserted just before (0x006588D0 at 0x00658CED and 0x00658EBC), so the leftovers are never read.

The cache's life (constructor, destructor, clear) runs at every load. Slot 71, ClearCellIndices, SaveCells and RestoreCells are called by the segment placer 0x0063B830 (at 0x0063BA6E, 0x0063BA5D and around them), which PlaceAllSegments runs for each segment: the crew test's drags (`OPENSC4_TEST_CREW`) reach them, though no coverage run has measured it yet. CollectCells is called only by the underground tools' slot 70 (StartPlacing, 0x00658710), which no test drives. MarkRuleCells is reached only by the network view's puzzle piece controls: no test reaches it.

## What the third part covers

All in `src/core/nettool_life.cpp`, two groups, off by default: `simulators.nettool.life` (14 detours) and `simulators.nettool.preview` (9 detours). The same skip list (`OPENSC4_NETTOOL_SKIP`) and first-call lines (`nettool: first <name>`) as the other parts; every call between these functions goes to the game's address. None of the nine guarded functions is in this part, NAM's DLL calls none of it, and none of it calls a guarded function.

### The tool's life

| Function | Address | What it does |
| --- | --- | --- |
| the constructor | 0x0062C5F0 | ret 4 (the type): the two vtables (the part at +4 first gets 0x00A81174), the world cache (0x00648000), the three hash sets with 100 buckets, the two satisfiers (0x00660810), the mask of the other surface networks, the bridge settings, the two trees' 0x14-byte head nodes, the preview update byte, the type; every other word it writes is 0. The failure reason +0x20C, the byte +0x51 and the words +0x6C to +0x80 are not written. EAX this. The pipe, subway, power line and underground constructors call it at its address |
| Init (slot 3) | 0x006269C0 | once until Shutdown: the type row; on the first Init after a resource load (the byte 0x00B452A8), the app's slot 0x114 gets the callback 0x006232F0 that clears the byte again and the RUL engine loads (0x006253D0); slot 79 with the type's word of 0x00AA8428; a new preview drawable assigned as a reference, its render state set up (0x0061FD10, the game's) and registered with the render service 0x00B43DD0 (slot 0x80, layer 5, priority 1000); the class mask and a filter of it; the placement parameters; the bridge kind lists (below); +0x354; the city's size and the vertex steps derived from it; the terrain (its slot 0x18 into +0x238), the occupant manager and the city's slot 0x84 object, each referenced; +0x23C = FPTAN of the parameters' float +0x18 times +0x238 (the x87 instructions themselves); the render service's slot 0x28 object's +0x14 into +0xFC; a network occupant made from the pool, its slots 3 and 4 run and released; the message 0xA6B79602 subscribed. AL 1 |
| Shutdown (slot 4) | 0x00626D30 | when initialised: the message unsubscribed, the possible bridges emptied, slot 17, the anchors and the dragged cells emptied, slot 16, the city object +0x4C, the occupant manager, the terrain and the filter released and nulled, the drawable unregistered (slot 0x84), its material released (0x0061FBC0) and the drawable released; +0x18 cleared. AL 1 |
| Reset (slot 5) | 0x00626F90 | the world cache emptied, slots 17 and 16, the anchors and dragged cells emptied, the bridge settings back to none, the possible bridges emptied, slot 13, the rule cell records +0xF0 emptied through 0x00624070 (EAX their new end, as the game's last instruction leaves it) |
| GetUITool (slot 6) | 0x00623B20 | a new 0x134-byte tool UI object (0x00645D50), referenced, attached to the tool and its type (0x00645240, part ui1), given to the caller. AL 1 |
| DoMessage | 0x00624480 | the part at +4 of every tool of the family: the message 0xA6B79602's first data word into +0xFC. AL 1 |
| ClearOccupantHighlights (slot 16) | 0x00625580 | the drawable's trees emptied (0x006208D0; the drawable is not tested for null, as in the game), each highlighted occupant given to the occupant manager's slot 0x54 (the occupant, 1, 0) and released, then the tree emptied (SC4RefSet::EraseSubtree, which releases each again, as the game's does) |
| ClearCellPreviews (slot 17) | 0x00625F10 | each highlighted occupant's SetHighlight (slot 0x44) with two zeros, the set emptied, then the highlight region +0x210 freed with its rows (despite the header's name, part t4 found these are the highlights) |
| the destructor | 0x0062CAF0 | the vtables back, Shutdown at its address, then every container freed in the game's order and the world cache destroyed |
| the deleting destructor (slot 51) | 0x0062CD30 | the destructor, operator delete when bit 0 is set; EAX this |
| DestroyPlacementParams | 0x00624F70 | the parameters' five vectors freed (also the RUL engine's 0x00643510 and the tool UI's 0x00645E90) |
| DestroyIntersections | 0x0062BE40 | the 0x28-byte elements' pointer +0x1C freed, then the vector (also 0x0062BFE0) |
| DestroyMajorBridges | 0x0062C590 | cdecl (first, last, a tag): each element's 0x50-byte entries' pointer +0x44 freed, then its vector (also called by 0x0062CD50, 0x006323E0, 0x0063B830) |
| ListBridgeKinds | 0x0062F9D0 | cdecl (the row's bridge records, the tool's +0x33C, +0x348): both emptied, then each record's index appended to +0x348 when its byte +0x98 is set and to +0x33C when its byte +0x99 is |

One oddity kept: Init's +0x354 ("the type has a bridge record") tests the address of each record's byte +0x98, not the byte, so any record sets it.

### The preview drawable

0x58 bytes (constructor 0x00620AD0, made by Init, kept at +0x1F0): +0x0 its vtable 0x00AA8314, +0x4 a cRZBaseUnknown part (vtable 0x00AA82FC, the reference count +0x8), +0xC a material the render state setup makes, +0x10 / +0x14 a tree of 0x7C-byte cell nodes, +0x1C / +0x20 a tree of 0x80-byte nodes each holding a texture binding at +0x7C, +0x28 to +0x44 four render states, +0x48 to +0x53 render state bytes, +0x54 the setup has run.

| Function | Address | What it does |
| --- | --- | --- |
| the constructor | 0x00620AD0 | the first vtable 0x00A881C0, the cRZBaseUnknown part (0x0090DA1E), the final vtables, the two tree heads, the states cleared, the state bytes; EAX this |
| the destructor | 0x00620770 | both trees emptied and their heads freed, the material released, the cRZBaseUnknown part's destructor (0x0090D990) |
| the deleting destructor | 0x00620BA0 | the destructor, operator delete when bit 0 is set (the part at +4 reaches it through the game's thunk 0x00620810) |
| Clear | 0x006208D0 | both trees emptied, the heads kept |
| ReleaseMaterial | 0x0061FBC0 | when the setup has run, the material released and nulled, the flag cleared |
| the tree helpers | 0x0061FCD0, 0x006203A0, 0x006202E0, 0x006206A0 | a subtree freed (the textured tree's releasing each texture binding, 0x008090E0) and a range erased node by node (0x0040DAF0), or the whole tree when the range is all of it; the range erasers' caller is part t4 (0x00620CF0) |

### What stays the game's

- The drawable's drawing: the render state setup 0x0061FD10 (Init calls it at its address), the draw slot 0x00620500 and the batch drawing 0x00620160 under it. Drawing is the Renderer thread's.
- The adjustor thunk 0x00620810 (`sub ecx, 4; jmp 0x00620BA0`) of the drawable's part at +4: it jumps to our deleting destructor.
- Called at their addresses and not this part's: the rule loader 0x006253D0 and its reset callback 0x006232F0 (r1), the tool UI's constructor and attach (ui1), the satisfiers' constructor and destructor (s1), the network occupant's pool and constructor, the filter's constructor, CopyPlacementParams (t1), the world cache's constructor, destructor and Reset (t2), and the shared template code (the hash set's construction and clear, SC4RefSet::EraseSubtree, SC4PtrVector::InsertN, the record move 0x00624070, the tree rebalance, the texture binding's release).

Registers: plain detours (ECX and EDX clobbered). The register scan of every direct game caller (52 call sites of the 23 functions) found none that reads ECX or EDX after the call; the only callers that read EAX take the constructors' `this`, which ours return.

## The satisfier (part s1)

`src/core/nettool_satisfier.h` (the layouts and shared inline helpers), `nettool_satisfier_1.cpp` and `nettool_satisfier_2.cpp`, two groups, off by default: `simulators.nettool.satisfier` (22 detours: the object's life, its containers, the vertex and constraint inserts and the results) and `simulators.nettool.solve` (23 detours: the range arithmetic, the constraint propagation and the solve). Either group works without the other; every call between part functions goes to the game's address. The source files' top comments have the full call list with argument counts.

The satisfier fits the heights of a drag's vertices to its constraints. The tool keeps two (+0x124 and +0x188). Its height pass 0x00637A80 runs Reset, Begin(-FLT_MAX, FLT_MAX), adds vertices and constraints (through slot 76, the NAM-guarded 0x00636690 and 0x00636030), then Solve, and reads the results through slot 57 and GetResult. The underground tools use it too.

### Layout

0x64 bytes (constructor 0x00660810, destructor 0x006602C0, deleting destructor 0x006606C0 in the one-slot vtable 0x00AAAEF8):

| Offset | What |
| --- | --- |
| +0x00 | the vtable |
| +0x04 | byte: begun (Begin sets it; Reset and the destructor clear it and then Clear) |
| +0x05 | byte: Solve's last answer |
| +0x08, +0x0C | floats: the start range of a new vertex (Begin's arguments) |
| +0x10 | the vertex whose range came out empty |
| +0x14 | a hash table from vertex to settled height (100 buckets; the buckets at +0x18, the count at +0x24) |
| +0x28 | a hash table from vertex to entry (0x24-byte nodes: next, key, seven words; the buckets at +0x2C, the count at +0x38) |
| +0x3C | the equality groups (a list of sets of vertices) |
| +0x40 | the slope constraints: abs(a - b) <= s |
| +0x44 | the curvature constraints: abs(v1 - 2 v2 + v3) <= k |
| +0x48 | the sum constraints: a + b in [lo, hi] |
| +0x4C | the midpoint constraints: a - 2b in [lo, hi] |
| +0x50 | the pending vector of (vertex, height) pairs NextEntry takes first |
| +0x5C, +0x60 | the results iterator |

An entry: +0x0 and +0x4 its range, +0x8 the preferred height (flag 2), +0xC and +0x10 the preferred range (flag 4), +0x14 the wanted height, +0x18 flags (1 restricted, 2 and 4 the preferences, 8 in a group, 0x10 slope, 0x20 curvature, 0x40 pushed as pending).

### What stays the game's

- NextEntry 0x0065ED80 (guarded: sc4fix patches it, and `fixes.puzzle_piece` detours its entry). Solve calls it at its address.
- The template code under it all: the vertex set's insert, subtree erase and construction (0x0046C0E0, 0x0065E740, 0x0065E8C0), the hash tables' construction, clear, begin and next bucket, the map lookup, erase and index (0x004F8D30, 0x00489CF0, 0x0065FAD0), the list's destroy and remove (0x005650A0, 0x00809580), the vector insert 0x007C61A0, the string append 0x0040EBC0.

### NAM, registers and floats

NAM's DLL calls InsertSlopeConstraint 0x0065FF60 at its address (thiscall, `ret 0xC`, kept), and its slopes hook inside 0x00636690 calls it and InsertEqualityConstraint 0x0065FDB0 and reads the satisfier by layout: every field, list, set and table stays in the game's memory, in its layout, from its allocator, and nothing is cached across a call. The game leaves EAX, ECX and EDX of those entries path-dependent (a node, flags, the caller's own values on early exits), and the scan of every game caller (127 call sites of the 45) found none that reads ECX or EDX after the call, so they are plain detours. AL is the answer where the game's is a byte (every caller tests AL); EAX where it is a pointer.

The x87 work is sums, differences, products with -0.5 and doublings of floats: done in double in the game's order and association, rounded to float where the game stores a float, with -0.5 (0x00A949FC) and the gap 0.01 (0x00AA8A74) read from the game's data. Every compare is written so that NaN takes the game's branch (the test masks 5, 0x41, 1 and 0x44 of the x87 status word). There is no transcendental, no float-to-integer conversion and no x87 value kept across a call.

Three places where the game reads or writes leftovers of its stack, none observable: AddVertex 0x0065FB60 writes the entry's preferred height and range words from its stack (ours writes 0; they are read only under flags 2 and 4, which the two setters write together with the words); ApplyMidpointFixed 0x0065EFA0 and ApplyMidpointRange 0x0065F180 read an unwritten "changed" byte only when a midpoint constraint's other vertex has left the entry table, which a vertex does only after its midpoint constraints were removed (ours starts it at 0); DumpGroups 0x006608C0 walks three lists at its end without effect (ours does not). DumpGroups' body never runs in the game either: Solve clears the groups before it calls it.

### The RUL engine (part r1)

`src/core/nettool_rul.h` (the layouts) and `src/core/nettool_rul_1.cpp`, two groups, off by default: `simulators.nettool.rulload` (8 detours: LoadRules 0x006253D0, the per-network ini 0x00623310, the reset callback 0x006232F0 (a naked thunk keeping the game's registers), the power line tool's once-load 0x00626990, the RUL2 and RUL1 map erasers, and the type row's static constructor and destructor) and `simulators.nettool.rul0` (21 detours: the RUL0 rule's constructor, copy constructor, destructor and the copies under them, the store's Clear, InsertNode and FindOrInsert, the two layout parsers, the transforms Translate, Transpose and Rotate with their grid, point and edge helpers, and a pair table's Find 0x00633970). The file's top comment has the full call list.

The load runs at the first network tool Init after every resource load: the network ini (type 0, group 0x8A5971C5, instance 0x8A5993B9) fills the 13 type rows in the game's order (0, 10, 11, 1, 8, 9, 3, 2, 12, 6, 4, 7, 5), then RUL 0x10000001 (RUL1, its type IDs into each row's +0xFC), RUL 0x10000002 (the RUL2 multimap 0x00B466D0 emptied and filled) and RUL 0x10000000 (RUL0 into the store 0x00B452B0). The RUL text parsers (parts r3 to r5) are called at their addresses; the guarded RUL2 engine 0x00634D70 and line callback 0x0063D1E0 are not called by this part at all (LoadRul2 0x0063D3E0 passes the callback itself).

Layouts (nettool_rul.h has them with evidence): the RUL0 rule, 0xFC bytes (its ID, records, bounds, the '+' points, the cells and check types, the cons grid and cell grid with their widths and anchors, the rotation and ring words; NAM's FlexPieces edit it in place), the store (a hash table of 0x104-byte nodes: next, ID, rule), the RUL2 multimap (an STL tree; a value is the key and four (ID, rotation) pairs), the type row (0x114 bytes). All stay in the game's memory, built through the game's allocator and template helpers; nothing is copied or cached.

Floats: Translate adds an integer to a float (exact in double, rounded to float); Transpose and Rotate move and negate floats with the x87's own FLD, FCHS and FSTP (naked helpers: an x87 load and store quiets a signalling NaN, a C++ copy does not).

Stack leftovers the game stores and ours cannot repeat: FindOrInsert 0x0062C2F0 builds a new rule on its stack, and the constructor leaves +0x0, +0x18 to +0x24, +0x40, +0x44, +0x7C and +0xC4 unwritten; ParseCellLayout stores a cell's key byte in a stack word whose other three bytes the game never writes. Ours are zero. A probe of the original (test log line `autotest: ntrul: leftovers`) on Rush Hour t0000: +0x7C holds address-like leftovers on 28 of 249 rules (a real PlaceQueryID on the rest), +0x40/+0x44 on 237, and one rule without a cell grid has leftover bounds. What reads them: +0x40/+0x44 only behind the byte +0x48 the constructor clears; a grid-less rule's bounds and width only behind a grid size test; the cells' padding is copied, never compared; +0x0 is written by the loader right after the insert. +0x7C has one reader, the intersection placement tool's text at 0x00661778 (an LTEXT of group 0x2A592FD1 at that instance): that group holds 0x0C369C73, 0x8AADD2B5, 0xA0000000 to 0xA0000004 and 0xF00000nn (SimCityLocale.DAT), nothing near 0 or the leftovers, so both miss and show the same "missing" text.

Not run as ours: the type row's static constructor 0x00641F70 (the CRT calls it before our groups install). Only at exit or on a second load: the static destructor, the store's Clear and the two map erasers. Only on an app reset: the reset callback.

### The rule files (part r2)

`src/core/nettool_rul_2.cpp` (layouts added to `nettool_rul.h`), two groups, off by default: `simulators.nettool.rulfile` (11 detours: the RUL text reader's open, construction, destruction, line reader and tokenizer, the tile, edge and entry parsers, and the rule file object's constructor, deleting destructor and Load) and `simulators.nettool.rulrec` (24 detours: the check and tile vectors' copies, assignment and growth, the edge, check, tile and entry rotations and mirrors, entry equality, the per-file multimap's node creation, insert, range insert, erase, construction and destruction, and Transmogrify). Neither guarded RUL2 function is called or touched; every object, multimap, node and vector stays in the game's memory, from its allocator, through its template helpers.

What these are: each type row's vector at +0x24 holds pointers to rule file objects. LoadTypeInfo (r1) adds two per row (none for the power line, row 5) through AddRuleFile 0x0063D360 (operator new 0x10, the constructor 0x00622A90, Load 0x00623020 with the row's key); the row's Clear deletes them through their one vtable slot (0x00622AE0). The tile solver reads them (0x00635D70 hands each to 0x006358C0) just before the guarded RUL2 engine. They are neither the RUL2 multimap nor the RUL1 map.

Layouts: the object (0x10 bytes) is its vtable 0x00AA8338 and an STL multimap (head at +0x4, count at +0x8) of 0x34-byte nodes whose value (+0x10) is a key (the entry's edge word) and a 0x20-byte entry: four edge bytes (each `((n / 10) << 3) | n % 10` of the text's number), a vector of 12-byte checks (a position byte, edges, a mask: 0xFF per byte, 0 for '?'), a vector of 8-byte tiles (an ID, a position byte, a turn byte with 0x80 for flipped) and a variant mask (0xFF, 0xF for `noflip`, 1 for `notrans`). Positions: 0 the cell, 1 to 8 the ring around it, 9 to 24 the next ring. The text: comma-separated, ';' lines are comments, the first token must start with '#'; a file that starts with `#Transmogrify` has each entry replaced by its distinct variants (up to eight: bit i of the mask, turns i & 3, mirrored when i & 4), deduplicated by edges and checks, inserted in the game's order so that the multimap's node order (which the tile solver walks) is the same.

Stack leftovers the game leaves and never reads: Transmogrify's variant slots' mask word (the duplicate path copies an unwritten word, the mirror path never writes it; Equals and Rotate never read it and inserted values get mask 1), and the padding bytes of checks and tiles built on the stack (every copy into a vector is field by field). Ours zero them. No floats.

The test (in `nettool_rul_autotest.cpp`, the same switch): the daily part adds ` f<files>:<hash>` after r1's `t`: every type row's rule files, each multimap walked in order (key, edges, checks, tiles, mask; no padding). From the drive day ` e<hash>` after r1's `d`: the edge mirror and rotation on 64 words, the check and tile rotations on records of the test's, Mirror, Rotate and Equals on the first 48 entries of row 0's first file, a local multimap of 32 copies transmogrified into a second one, and row 0's first rule file loaded again into a new object (the reader and every parser), compared with the loaded one and deleted through its slot.

## The RUL parsers (part r3)

`src/core/nettool_rul_3.cpp` (layouts added to `nettool_rul.h`), three groups, off by default:

- `simulators.nettool.rulparse1` (11): SetLoaded, AddRuleFile 0x0063D360 (r2's rule files), LoadParams 0x0063D460 and the placement parameters' load 0x0063CBE0 (its exemplar instance comes in EAX, an LTCG convention: the detour is a naked thunk, and LoadParams calls the game's through one), LoadRul2 0x0063D3E0, the type name index, the RUL1 type and pair line callbacks, ReadRul1TypeIds, and the pair table's insert and find-or-insert.
- `simulators.nettool.rullex` (12): the RUL0 lexer (constructor, NextToken: tokens 0 for an end, a line end or ';', 0x100 integer, 0x101 float, 0x102 identifier, else the character sign-extended; '#' comments; hex), the three character tests, the RUL0 record vector (fill, growth, push, assignment), the check-type table's inserts and the loader's list destructor.
- `simulators.nettool.rulmaps` (21): the RUL2 multimap's insert (the two functions the guarded line callback calls), the RUL1 pair table's resize and copy, the 20-byte element vector's copy, the bridge loader's string map, the row table +0x3C, the point table, and the bridge record's int map (node creation and cloning, erase, inserts, subtree copy, assignment).

The guarded RUL2 line callback 0x0063D1E0 stays entirely the game's: LoadRul2 passes the game's address with the multimap 0x00B466D0 as user data, and the cIGZIniFile walk calls it as before; after its calls of the multimap insert it reads nothing the callee leaves. All maps, tables, vectors and nodes stay in the game's memory, from its allocator, through its template helpers.

Floats: LoadParams' tangents are the x87's own FPTAN, FST and FMUL sequence (a naked helper); the placement parameters' degrees-to-radians products are exact in double and rounded once to float, as the x87 does at 53 bits; the lexer's floats are parsed by the game's parser 0x00910991 at its address and stored with the x87's FSTP.

Stack leftovers: Rul1PairLine 0x0063D080 stores a turn byte in a stack word it never initialised, and the word goes into the pair table: the three upper bytes are the original's leftovers (ours zero); the solver (0x00634C0F) reads only the byte. The bridge parser 0x0063EA50 (part r4, still the game's) builds each 20-byte element on its stack and writes only one byte at +0x8 before copying five words, so bytes +0x9 to +0xB of every bridge element are its stack's leftovers: they follow what deeper calls left there, which includes this part's lexer once ours, so they differ between A and B (and between any two programs whose calls below that frame differ); our copies carry them word for word as the game's do, and the test leaves them out. Not proven: that no game code reads those three bytes.

The test (`OPENSC4_TEST_NETTOOL_RUL`): the daily part adds ` p<hash> x<nodes>:<hash> r<count>:<hash> b<records>:<hash>/<hash>` (the type rows' placement parameters, their +0x3C tables, the RUL1 map with its pair tables (the turn byte, not its word), and the bridge records: names, int maps in key order, and the 20-byte elements by their fields, not the leftover bytes); the drive adds ` g<hash>`: the character tests on 0 to 255, the type name index, the lexer on five texts and a pushed-back token, the record vector, the check-type, pair (with resize and copy), +0x3C and point tables, the string map, the int map (insert, assignment to a copy, to itself, from an empty map) and a RUL2 multimap, all on the test's own memory.

## The RUL parsers, second part (r4)

`src/core/nettool_rul_4.cpp` (layouts added to `nettool_rul.h`), two groups, off by default:

- `simulators.nettool.rulparse2` (12): the RUL0 line callback 0x00640320 (21 static key strings created on its first call, as the game's, then 19 keys tested in the game's order: Piece, StepOffsets, CellLayout, ConsLayout, AutoTileBase, AutoPathBase, CheckType, CopyFrom, Transpose, Rotate, Translate, ReplacementIntersection, ConvertQueryID, PlaceQueryID, HandleOffset, PreviewEffect, Costs, AutoPlace, OneWayDir), the check type parser, the bridge layout parser 0x0063EA50, the integer and float token parsers, the type name append 0x0063E240 (type in ECX, string in EAX: a naked thunk), the RUL1 `IntersectionSolutions` reader, the exemplar callback and loaders (the row's +0x3C table), and the tile verifier loader and line callback (the table 0x00B466DC).
- `simulators.nettool.rulmaps2` (11): the RUL0 rule's assignment, the +0x3C table's find-or-insert, the bridge int map's hinted insert and copy constructor, the bridge string map's copy and insert, the RUL1 map's node creation and insert, and the bridge record's (0x178 bytes) constructor, assignment and destructor.

Nothing of this part is guarded or calls a guarded function; every rule, record, table and map stays in the game's memory, from its allocator, through its template helpers.

Floats: the bridge parser's chains (positions and lengths of the pieces, the height added at the end) are float loads, sums and differences done in double in the game's order and association and stored as floats; an integer token read as a float is FILD plus 2^32 (exact); the exemplar callback's angles are exact products and its rotation is the game's 0x005D77A0 at its address; the lexer's floats as in r3.

Stack leftovers: the bridge parser builds each 20-byte element on its stack and writes one byte at +0x8 (r3 found it from the game's side): ours writes the same byte into a zeroed local, so ours stores zeros in +0x9 to +0xB where the original stores leftovers; the only code that reads elements as values (the bridge code 0x00632060, through the record's int map) reads +0x0, +0x4, the byte +0x8, +0xC and +0x10, never those three bytes. The RUL0 lexer the line callback keeps on its stack never writes +0x2E, +0x2F, +0x43, nor the edge word and mask of a side without a type, and the check type node gets them (its +0xA, +0xB, +0x1F, and +0xC/+0x10 or +0x14/+0x18): every reader (the check type insert, the rule copy, Transpose, Rotate, MarkRuleCells 0x006097E0, 0x006099A0, 0x00609A00, ParseCellLayout, the rule destructor) reads none of the three bytes, and reads a side's edge word and mask only when its type is not negative. The exemplar callback's key word for a missing property 0xE974EAA1 is a leftover in the original, 0 in ours. Ours zero all of these.

The test (`OPENSC4_TEST_NETTOOL_RUL`): the daily part adds ` v<n>:<hash> w<hash>` (the tile verifier table, order-free; every bridge record's floats and vectors); the drive adds ` k<hash>`: the type name append, the token parsers, the check type parser on nine values (its three error paths included), the RUL0 line callback on a rule of the test's (a copy of a stored rule) with every key, the rule assignment (self-assignment included), the int map's hinted insert and copy, the string map's copy, the RUL1 map's insert and node creation, the +0x3C table, the tile verifier line callback on a table of the test's, the bridge record's life and assignment, and the bridge parser on lexers and records of the test's.

## The RUL parsers, third part (r5)

`src/core/nettool_rul_5.cpp` (layouts added to `nettool_rul.h`), two groups, off by default:

- `simulators.nettool.rulparse3` (14): LoadRul0 0x00641B60 (the context, the store cleared, the sections walked) with its section callback (`Ordering`, `HighwayIntersectionInfo_<hex>`) and the `Ordering` line callback (`RotationRing` links rules into a ring through +0xEC/+0xF0, `AddTypes` through +0xF4/+0xF8, its static strings made on the first call as the game's), LoadRul1 0x00641D30 (the RUL1 map for every type pair), the type row's Clear 0x006421D0, LoadBridges 0x006437A0 with the bridge ini line 0x00642220 (`ID = type,group,instance,name`: a record built from the exemplar's properties), the bridge layout load (type 0x0A5BCF4B, group 0xAA5BCF57, instance 0x1000 + the type) and its lexer and parsers (`section`, `bridge`, `length`, `end`, `stop`).
- `simulators.nettool.rulmaps3` (11): the int and string maps' find-or-insert, the RUL1 map's inserts (unique, hinted) and find-or-insert, the bridge record's copy constructor, and the record vector's copy, fill, erase and growth.

The ini callbacks passed are the game's addresses (0x006416B0, 0x00640320, 0x00641A50, 0x00642220); neither guarded function is called or passed. With r1 to r5 the whole RUL engine is ours but the RUL2 engine 0x00634D70 and line callback 0x0063D1E0.

Floats: only the bridge ini line: a compare with 0.0 (false for NaN, as the game's), quarter turns (exact products, the game's rotation at its address), and the +0x60/+0x6C chain and the angles +0x12C to +0x13C (products with -1.0) through naked helpers with the game's own instructions, so NaNs propagate exactly as on the x87.

Stack leftovers: the bridge ini line builds each record on its stack, and the fields it writes only when the exemplar has the property keep the original's stack leftovers (ours zero). Some are read without a gate (+0x2C the middle piece ID and the byte +0x30 at 0x00631759 to 0x0063176D, +0x5C the automatic bridge's deck height at 0x0062EB1E, +0x7C at 0x0063135B, the bytes +0x98/+0x99 at 0x0062FA66/0x0062FAA1), the others only behind gates the same exemplar sets (+0x3C behind +0x40, +0xDC/+0xE0/+0x144 behind the byte +0xD8) or never (+0x31 to +0x33, +0x9A, +0x9B, +0xD9 to +0xDB). They would differ only for a bridge exemplar without the property: the test's ` y` hash keeps the property-dependent fields in a part of their own, and it is the same in every A and B run of the batch below for all 47 shipped bridges, so the shipped exemplars have every property (a plugin bridge without them would get zeros where the original gets leftovers). The RUL1 value's pair table word and LoadRul0's context word are never read before written.

The test (`OPENSC4_TEST_NETTOOL_RUL`): the daily part adds ` y<records>:<always>/<property>` (the bridge record fields the ini line writes, those it writes only with a property after the slash); the drive adds ` h<hash>`: a RUL1 map of the test's (find-or-insert, unique and hinted inserts in every hint position), and for each row with bridges copies of its records (int maps emptied) refilled by the bridge layout load (the lexer and every parser), the vector's growth and erase.

## The previews and highlights (part t4)

`src/core/nettool_preview.cpp`, three groups, off by default:

- `simulators.nettool.cellpreviews` (7): slot 58 AddCellPreview (which cells of the placement the preview drawable shows, and with which piece, turn, colour and texture key: the cell erased, then one textured cell for the lowest flag's type, or one per type for a layered cell), and the drawable's trees: AddCell, EraseCell, the cell and textured node inserts and the textured node's creation (the trees are multimaps keyed by (z << 16) + x; a cell node holds a 0x68-byte record of four vertices, the corner bytes and the type's height).
- `simulators.nettool.piecematch` (8): the piece lookup 0x00633FA0 (the type row's rule files, part r2, searched for the tile that steps onto the cell), the rule file's match and entry match, the neighbour walker's constructor and its memoised edges, and the check vector's growth, copy and fill.
- `simulators.nettool.highlights` (5): slot 59 UpdateOccupantHighlights (the highlight region of the drag's cells, the occupants on it collected by the demolition's DemolishRegion in query mode, the new and old sets diffed into SetHighlight calls; EAX the count), slot 60 DestroyHighlightedOccupants (DemolishRegion for real, then slot 17; ours calls slot 17 where the game jumps to it), the collector 0x0061F7C0 (which the NAM-guarded 0x0062CD50 calls: cdecl, AL; that caller reads nothing else it leaves), the diff 0x0061F5D0, and slot 80 SetQuadTrianglesToDraw (three of the network occupant's own flags, by 20 edge words in five outcomes).

Left the game's as drawing: 0x0061FE60 (the preview record's vertices, colours and UVs, turned and flipped), 0x0061FBF0 (their heights from the terrain), 0x00620BC0 (fetches the preview's texture from the texture service and calls the tree inserts and AddCell at their addresses).

The tool's fields this part names (and part t3's comments called otherwise): +0x210 the highlight region (an SC4CellRegion of 0x24 bytes: a rectangle and a bitmap of cells), +0x214 the highlighted occupants (a set ordered by address), +0x220 the cost of the last highlight query, +0x228 a tree of occupants slot 16 releases. Slot 17 (ClearCellPreviews by the header's name) clears the highlights and the region; slot 16 (ClearOccupantHighlights) the drawable's trees.

No floats in ours. Stack leftovers, all without a reader: AddCellPreview's flip word (only its low byte is written and read), the walker slot's direction word (its upper bytes are the walker's address, as in the game), the memoised edges' record padding (copied field by field).

The test, `src/core/nettool_preview_autotest.cpp`: the daily part ` | ntpv p<cells>,<textured> h<highlighted> r<regions> <hash>` (order 178) over the manager's 13 tools: the drawable's trees (keys and records, order-free), the highlight region, the cost, the highlighted occupants (type, highlight, cells; order-free) and the +0x228 count. `OPENSC4_TEST_NETTOOL_PREVIEW=1` with `OPENSC4_TEST_NETTOOL=<day>` (hook order 200) on a road tool of its own: up to 12 network cells previewed through slot 58 (the trees hashed, then slot 16), the piece lookup per cell and type, slot 80 with five edge words on the cells' occupants (their flags put back and checked), slot 59 with no steps and then with six cells around a lot (query mode only), the highlighted occupants cleared by slot 17 and read back, and the tool's fields put back. Slot 60 is not driven (it demolishes); the crew test's placements reach it.

## The tile solver (part t8)

`src/core/nettool_solve.h` (layouts and the hash set helpers as the game inlines them), `nettool_solve_1.cpp` and `nettool_solve_2.cpp`, three groups, off by default:

- `simulators.nettool.tiles` (11): the solver SolveTiles 0x00635CB0 (EAX the first step left unsolved), the rule file application and match (the walker and the rule file match of part t4), the multi-type resolution with the RUL1 overrides, the tile verifiers, the verifier table's equal range, the cell set's subtract, erase and union, the set's growth around cells, and the clamped rectangle around a cell.
- `simulators.nettool.tilefix` (9): the edge fix-ups chosen by the type row's kind word (kind 1 and kind 2, the diagonal runs, the side byte helpers) and PatchTilePair 0x006337E0 (one RUL2 override applied to a pair of cells; EAX 0, 1 or 2).
- `simulators.nettool.dragcells` (5): CollectDraggedCells 0x006355C0 (the drag's cells into the set +0x84; the guarded 0x0062CD50 and the segment placer call it), the four-pass spread along combined edges, the intersections' cells, and the erasers of the +0xE4 records' and the major bridges' cells.

The RUL2 tile-conflict pass 0x00634D70 stays the game's (NAM's Rul2Engine replaces 0x00634D79 to 0x00635282 inside it): the solver calls it at its address, and the functions it calls (PatchTilePair among them) keep the game's convention; the scan of its code after each call (and of the guarded 0x0062CD50's) found it reads nothing ours leaves differently (CL is written before it is tested as a flag). Every set, vector and cell info stays in the game's memory, through the game's template helpers.

Fields this part names: +0x84 the cells to solve (a hash set of (z << 16) + x), +0x98 the solved cells (12 bytes: the piece ID, a rotation byte with the turn in bits 0 and 1 and the flip in bit 7, the cell key), +0x08 the RUL1 override records (12 bytes: x, z, flags; the destructor frees it), +0xCC the intersections (+0x1C a vector of their cells' keys), +0xE4 records of 0x10 bytes (first and last step).

No floats. Stack leftovers: the solved cells' bytes +0x5 to +0x7 (the rotation byte is stored in the low byte of a stack word whose upper bytes the solver never writes, and the whole word goes into +0x98): every game reader of the solved cells (slot 55, AddCellPreview, the tile verifiers, PatchTilePair, the cost) reads only the byte and the key, the copies carry the word unchanged; NAM reads the solved cells by layout, but those bytes are the original's stack and so nothing NAM could rely on. Ours zero them; the test hashes only the byte. The fix-ups' byte arguments pushed as words (only the low byte read by the callees).

The test, `src/core/nettool_solve_autotest.cpp`: the daily part ` | ntsv s<solved> c<cells> o<overrides> i<intersection keys> <hash>` (order 179) over the manager's 13 tools (the solved cells in order with the rotation byte, the cell set order-free, the override records, the intersections' keys). `OPENSC4_TEST_NETTOOL_TILES=1` with `OPENSC4_TEST_NETTOOL=<day>` (hook order 201) on a road tool of its own: up to 25 network cells of a 5x5 block written as the drag's cells with one step, CollectDraggedCells and then SolveTiles(-1, 1) at their addresses (as 0x0062A5C0 re-solves), the set, the solved cells, the override records and the cells' infos hashed; the tool's vectors freed and Shutdown. (`OPENSC4_TEST_NETTOOL_SOLVER` is the satisfier's.)

## The heights (part t9)

`src/core/nettool_heights.h`, `nettool_heights_1.cpp` and `nettool_heights_2.cpp`, two groups, off by default:

- `simulators.nettool.heights` (14): the height pass FitHeights 0x00637A80 (the cells to fit and the border vertices collected, the intersections' cells added, the bridge and +0xE4 portions' heights, the constraints inserted into the first satisfier and solved, the heights written; AL the satisfier's answer), slot 77 ApplyPlaneConstraintToRegularCells with the segment plane and the vertex blocking test, the intersections' constraints (the cons grid's pieces 1 to 8), slot 56 CanTilesBeSupportedOnTerrain (the second satisfier; AL), slot 57 UpdateTerrainHeights (writes the terrain's altitudes and redraws the rectangle), slot 73 GetNetworkVertexHt (a double in ST0), and their helpers.
- `simulators.nettool.heightcons` (5): slot 76 InsertRangeConstraintsForCell, the height set test, and the three functions NAM's NetworkSlopes calls at their addresses: InsertEqualityConstraint 0x00636000, InsertSmoothnessConstraint 0x00636030 and GetCrossSections 0x00636360 (ret 0x1C). These three are naked thunks that leave EAX, ECX, EDX, the flags and the callee-saved registers exactly as the game's do on every exit (where the game's last call is to another function, the thunk makes that call itself, so what it leaves comes back as in the game).

0x00636690 (InsertSlopeAndSmoothnessConstraintsForCell; NAM's NetworkSlopes replaces 0x006366C5 to 0x00636907) stays the game's and is called at its address; after its calls into this part it reads only AL, its own locals and callee-saved registers. Also called at their addresses: 0x00632EF0 (left the game's in part t2), the piece record lookup 0x00625860, the plane from points 0x005D67E0 and the satisfier.

Floats: every x87 sequence in the game's order, rounded to float where the game stores floats and kept unrounded where the game compares before storing (slot 77's upper bound, slot 56's sums), integers loaded as unsigned with +2^32 where the game does, every compare's NaN branch as the game's; slot 73 passes the terrain's ST0 back as a double.

Stack leftovers: AddIntersectionCells' solved records (bytes +5 to +7, as in part t8; FitHeights cuts the vector back in the same call, only +8 is read of them meanwhile). Ours zero; nothing reads them.

The test, `src/core/nettool_heights_autotest.cpp`: the daily part ` | ntht a<n> b<n> r<n> <hash>` (order 180): the tools' cells to fit and border vertices, both satisfiers' state and results (not the answer byte +5, which the constructor leaves unwritten), and an initialised road tool's row spans. `OPENSC4_TEST_NETTOOL_HEIGHTS=1` with `OPENSC4_TEST_NETTOOL=<day>` (hook order 202) on a road tool of its own: the tiles drive's drag (CollectDraggedCells, SolveTiles), the step counts and direction set (and put back), FitHeights(3), slot 56(0) and slot 73 on the cells' vertices, all hashed. Slot 57 is not driven (it writes the terrain); the crew test's placements reach it.

## The occupants, tunnels and cost (part t7)

`src/core/nettool_occupants.h`, `nettool_occupants_1.cpp` and `nettool_occupants_2.cpp`, five groups, off by default:

- `simulators.nettool.occupants` (10): slot 54 CreateNetworkOccupant (the occupant of a cell by kind: network, pre-built model 0x49C1A034, bridge 0x49CC1BCD or tunnel 0x8A4BD52B; its box, flags, edge words, one-way byte and demolition cost), slot 55 InsertOccupantsForSolvedCells (placing: slot 72 per solved cell and the network lots and immovable pieces updated in place; previewing: heights, slot 58 and the intersections' previews), slot 72 InsertNetworkOccupant (a pre-built piece with its pylon and set-up, or a network occupant; supports, the cell cleared, the occupant inserted into the occupant manager and the list), the vertex clearance category, the piece exemplar and record lookups, the neighbour tests along x and z, the pre-built set-up and the cell clearing (occupants the filter matches removed, power poles demolished).
- `simulators.nettool.ruleoccupants` (4): the intersections' occupants (PlaceRuleOccupants 0x0062C3A0, which the guarded 0x0062CD50 reaches through 0x0062C4B0) and the cons table lookup.
- `simulators.nettool.tunnels` (3): the tunnels (PlaceTunnels 0x006287D0 and MakeTunnelPiece 0x00628390, which write the terrain's vertices at the entrances and redraw) and AddEdgeConnection 0x00628BD0 (the connections at the city's edge, the neighbour simulator asked).
- `simulators.nettool.cost` (1): slot 20 GetCostOfSolution (EAX the cost: the cells', bridges', records' and intersections' costs, the height differences summed as floats and made an integer by the game's own trick: float(sum times the scale plus 12582912.0f), whose bits less 0x4B400000 are added).
- `simulators.nettool.bridgecells` (5): the bridge cell vector's assignment, copy, allocation and destruction.

None is guarded or calls a guarded function; the guarded 0x0062CD50 calls slot 55 through the vtable and 0x0062C4B0. No wrapper of ours tests return addresses in this range, and the crew's caller range (sim_timers) is outside it. Every occupant is made, inserted, removed and demolished in the game's order, through the game's code at its addresses.

Floats: slot 54's box (unsigned cell coordinates times the cell size), the clearance category (NaN gives 2, as the game's), slot 72's pylon (the rail height, the offsets with 0.65, the turn angle in sixteenths of pi, the slope from two normals and the final height), the intersections' unrounded height compare, the tunnels' terrain heights, and the cost's sum rounded at every step; the subtraction and division forms checked against the exe's opcodes.

Stack leftovers (ours zero, none read): slot 72's rotation word, slot 55's flip, usable and one-way words (each reader takes the low byte), PlaceTunnels' direction word, and the edge records' bytes +0x15 to +0x17.

The test, `src/core/nettool_occupants_autotest.cpp`: the daily part ` | ntoc e<n> <hash>` (order 203): the tools' edge records (+0x100 and +0x10C) in order, values only. `OPENSC4_TEST_NETTOOL_COST=1` with `OPENSC4_TEST_NETTOOL=<day>` (hook 204) on a road tool of its own: the tiles drive's drag, then slot 20, and per solved cell the piece record, the exemplar lookup, the clearance category and the neighbour tests, and the cons table lookup on every checked point of every stored rule. Slots 54, 55 and 72, the intersections' occupants, the tunnels and AddEdgeConnection change the city and are reached only by the crew test's placements.

## The drag (part t5)

`src/core/nettool_drag.cpp` (the layouts are in its anonymous namespace), two groups, off by default. 17 functions, 15 ours; the two NAM-guarded ones stay the game's. What the survey called "the drag" is what the network view's mouse move does as the player drags: DrawNetworkLine (slot 18) classifies the move and calls BuildDragPath, which fills the tool's drag steps (+0x54, 12 bytes each) and dragged cells (+0x60, 8 bytes each) from the start to the end; the segment placer (part t6) then asks the slots below about the steps. 21 address entries went into `data/addresses/nettool.toml`; two names were already there and are reused (SC4Vector12::InsertN 0x00638D40 and cSC4NetworkTool::OneWayRoadPass 0x0063A640).

- `simulators.nettool.drag` (6): BuildDragPath 0x00639790 (5 arguments, `ret 0x14`; the steps and cells from start to end. Wide pens widen the ranges and can shift the line onto a network found at either end; the pen-1 types can grow a step at each end. A tie, |dx| equal to |dz|, alternates the axes: x after a z move, z after an x move, x the last axis at the start), OneWayRoadPass 0x0063A640 (1 argument; one-way roads: spreads the one-way bits from the first and last step, adds the marked cells to the set at +0x84, then resolves conflicting directions until none change; the conflict pass adds the current cell's key to the work set, not the neighbour's, and ours keeps that as the game does), and the containers under them: SC4Vector12::InsertN 0x00638D40, SC4Vector12::Insert 0x00639070, SC4Uint32HashSet::InsertRange 0x00638FA0 and SC4HashIterator::Distance 0x00638AE0 (cdecl).
- `simulators.nettool.dragchecks` (9): the five tool slots, slot 65 TestForAutoConnectOccupant 0x00638680, slot 66 IdentifyStartingConditions 0x00638710 (a water test on the first dragged cell), slot 67 CheckForReservedTilesAt 0x006392F0, slot 68 CheckForReservedTiles 0x00639170 and slot 69 CheckForGoodTerrain 0x00638990, and the auto-connect filter they use: IsOccupantIncluded 0x006385D0 (slot 3), IsPropertyHolderIncluded 0x00637EA0 (slot 5), the deleting destructor 0x00638650 (slot 6) and IsPlacedNetworkOccupant 0x00638600 (cdecl: the interface 0xA821EF94, then its slot 0x54). The filter is a 0x10-byte object with vtables 0x00AA8BBC and 0x00AA8BA4 that wraps the tool's filter at +0x364 (in its +0xC); slot 65 builds one inline, and BuildDragPath builds two. Three of these are the filter class's, not the tool's.

Survey correction: the table above had slots 67 and 68 as one function listed twice. Slot 67 checks one cell, slot 68 one step (and calls slot 67 for each of its cells).

### What stays the game's

DrawNetworkLine 0x0063AF40 (slot 18) and the step classifier 0x00637EB0. NAM patches both for diagonal streets: 0x0063AFF2 and 0x00637F80, each behind a test of the tool's byte +0x2B5. They are the part's two guarded functions. DrawNetworkLine calls only BuildDragPath, and after it reads only callee-saved registers and memory on both sides of NAM's patched jump, so ours is safe to call from it; the guarded 0x0062CD50 calls slot 68 and reads only the count. Nothing in the part draws.

### Registers and stack leftovers

No floats. The one thunk is IsPropertyHolderIncluded, a function of 8 bytes that the game's callers reach with its three instructions replayed by a naked thunk. Every direct caller was scanned: none reads ECX or EDX after a call; EAX is read only after IsPlacedNetworkOccupant (AL) and Distance. No wrapper of ours detours or tests return addresses in 0x00637EB0 to 0x0063B130 (game_quiet.cpp calls DrawNetworkLine through the interface in its own test only).

OneWayRoadPass has two local hash sets whose word +0 the game never writes and nothing reads; ours is 0.

### Audit

An independent read found one bug before the batch: the tie rule was inverted. The crew test's straight drags did not catch it, so the test now probes a 45 degree line (below).

### The test

`OPENSC4_TEST_CREW_DRAWLINE=1`, with `OPENSC4_TEST_CREW=2`, in `src/core/crew_autotest.cpp`, off by default (legs without it are unchanged). Before each crew drag's PlaceAllSegments pair it does what the view control's mouse move (0x00645640) does: Reset, ClearFailureReason (slot 13), then DrawNetworkLine (start, end, 0, type) on copies of the two points. When that drew a line it calls slots 66, 68 (type, not skipped), 69 and 65 (on the first and the last cell), and slot 67 on each cell, up to 64. The six types' drags also probe a 45 degree line from their start (up to 8 cells, every move a tie). With `OPENSC4_TEST_CREW_BRIDGE` the bridge previews and builds probe too. The daily crew part adds ` dl <lines>,<drawn>,<hash>` (values only): the answer, the points as DrawNetworkLine left them, the classifier's words, every step and cell, and each check's answer and count. The release itself (0x006453FB to 0x00645516) does not call slot 18: PlaceAllSegments (part t6) calls DrawNetworkLine for each segment, so every crew drag already runs BuildDragPath.

Trial (the crew leg, before the tie fix): 13 of the 15 functions ran. Not reached: SC4Vector12::Insert (it runs only when the end of the drag grows a step, which needs slot 64 ShouldAutoConnect to answer true, and only the pipe and subway tools do) and the filter's slot 5 IsPropertyHolderIncluded (only if an occupant search asks it).

Parity (batch net-t5-b1, B list `nall32` = `nall31` with the two t5 groups): plain 12/12, net 3/3, setnet 3/3, all on the first pair; set 11/12 and 1 partly: on Big City d0090 one B run of four had an `adv` (advisor) value from day 1, before the test day and before any t5 function's first call in that run; the other three B runs equal A's (the rare `adv` B value of docs/mysim-agents.md, "Open point"). Crew legs (Rush Hour t0000, 20 days): crew same (A 2 outcomes, B 2, both shared), with OPENSC4_TEST_CREW_DRAWLINE=1 same (A 2, B 1, shared), with the bridge switch too same on the first pair; ` dl` was 12,12 (six drags, each straight and at 45 degrees, all drawn) and 23,23 with the bridge drags, the same hash in every A and B run. 13 of the 15 ran with the probe (12 without it: slot 65 runs only from the probe in these drags); not reached: SC4Vector12::Insert 0x00639070 and the filter's slot 5 0x00637EA0.

## Puzzle and resolve (part t12)

Two files, two groups, off by default. 6 functions, 4 ours; the two NAM-guarded ones stay the game's. 8 address entries went into `data/addresses/nettool.toml` (MatchAt 0x00609A00 was already named and is reused).

- `simulators.nettool.puzzle` (3, `src/core/nettool_puzzle.cpp`): slot 49 QueryUserForPotentialIntersections 0x0062D450, SC4Rul0Rule::MatchAt 0x00609A00 and SC4Rul0Store::Find 0x00625380.
  - Find (cdecl, a rule ID) looks the ID up in the RUL0 store 0x00B452B0 and returns the rule or 0.
  - MatchAt (this a RUL0 rule; x, z, the tool; `ret 0xC`) answers 0 when the rule's pattern fits at the cell, 1 when it does not, 2 when a cell disagrees or has no cell info. It walks the rule's cells (+0x58) with their check types (+0x64), tests the occupant's origin, the network flags (table 0x00AA74F8), the edge words under the masks, multi-ok and the one-way bits. A rule with points (+0x28) also needs them dry, free of network occupants and accepted by the lots' collector 0x0061F7C0.
  - Slot 49 (no arguments) is what the network view control's release (0x00645370, the call at 0x00645544) asks after a paid drag. For each override record the placing code left (+0x08, 12 bytes) it makes a new highway tool and takes the first rule of the store with a query text (+0x78) that MatchAt fits there. The guarded puzzle placement 0x0062CD50 must accept it, and its cost must not be above the city's cash. The rule's effect is started with a transform, and the localized text 0x2A592FD1 / the rule ID (else `## Intersection query string missing ##`) is asked in the question dialog 0x0078DFF0. On yes the placement is made for real, the budget charged and the sound 0xEA5EC59F played.
  - Five vtables share slot 49's code (0x00AA880C, 0x00AA9A6C, 0x00AA9FF4, 0x00AAA684, 0x00AAAA14). The crew's calls of a slot 0xC4 (0x00605568, 0x00605D53, 0x00607327, 0x00628B13) are the network occupant's GetPathInfo, not this slot.
- `simulators.nettool.resolve` (1, `src/core/nettool_resolve.cpp`): slot 50 DestroyOccupantsAndResolve 0x0062A5C0 (`ret 4`, a vector of network occupants), which the network manager's DestroyNetworkOccupants (0x00612269) calls for the bulldozer, the pipe burst and the lot developer.
  - The occupants are queued, their neighbours on four sides lose their connections to them, and tunnel ends and intersection pieces follow.
  - Each touched occupant gets the message 0xAA7A014D and is taken off the city: through the occupant manager's slot 0x54, or the network manager's RemoveOrReplaceOccupant.
  - The cells they leave get their heights fixed through the satisfier and are solved again (SolveTiles(-1, 1), slot 55). A cell with two types drops the weaker one, and the crew's FinishPieces makes the new pieces.
  - This repeats until nothing changes.

### What stays the game's

The puzzle placement 0x0062CD50 (NAM's FlexPieces jumps out at 0x0062CEDC and back at 0x0062CF42, using the game's frame and the rule pointer it took from Find) and DefaultCheckType 0x006099A0 (FlexPieces changes the immediate at 0x006099C4 from 2 to 0). Slot 49 calls 0x0062CD50 at its address with the game's four arguments; only 0x0062CD50 calls 0x006099A0. The guarded function calls Find (at 0x0062CE1E) and MatchAt (at 0x0062D2A5), and after either reads only EAX on both branches. NAM's DLL (its source, `nam-dll/src/FlexPieces.cpp`) calls none of the four, and edits the rule's cell lists and the tool's dragged cells in place; this part writes nothing of the rules, the store, the multimap or the type table, and keeps every set, queue and vector of slot 50 in the game's memory through the game's helpers.

### Registers, floats and stack leftovers

No thunks. Every direct caller of Find and MatchAt reads only EAX; slots 49 and 50 are called through vtables. GetFirstNetworkTypeFromFlags keeps EDX for slot 50's code at 0x0062B8E9; ours passes the flags explicitly.

Floats:
- Slot 49's effect transform is the cell's position in metres plus the record's floats, with the turn byte times -pi/2 (0x00AA88B4). The sums are done in doubles with one rounding to float, and the signs of the zeros are kept. FCOS and FSIN are a naked helper, each result stored straight to a float as the game does.
- Slot 50 has one float: the terrain's slot 0x60 plus the double 0x00AA2010, rounded to float for the satisfier's Begin.

Stack leftovers (zeros in ours, unread as far as the code shows): the transform's two padding bytes, the upper bytes of the word slot 50 passes for a one-way byte, and some scratch out buffers.

No wrapper of ours detours these addresses or tests return addresses in their ranges. fixes.puzzle_piece (0x0065ED80, the satisfier) is not called here, and the question dialog is called at its address.

### The test

`OPENSC4_TEST_CREW_PUZZLE=<level>`, with `OPENSC4_TEST_CREW=2`, in `src/core/crew_autotest.cpp`, off by default (legs without it are unchanged).
- After the six straight drags, a four-cell drag of road, street and avenue crosses the middle of that type's strip, released as the view control releases. After the placement and the cleared previews, the test hashes the tool's override records and MatchAt's answers for the rules with a query text and cells that fit there, then calls slot 49 with the question answered yes by a modal stand-in.
- Level 2 also hands the crossing cell's new pieces to slot 50, as DestroyNetworkOccupants does.
- Level 3 also does the view control's puzzle piece click (the guarded 0x0062CD50 with flag 0, which calls Find and MatchAt) for four rules with cells that fit, at two cells of the road strip.
- The daily crew part adds ` pz <calls>,<candidates>,<hash>`.

The probes skip RUL0 rules without a cell grid. Such a rule (0x2109 in the shipped files) keeps the original parser's stack leftovers in its bounds +0x18 to +0x24, where ours (part r1) stores zeros, so FitsInCity 0x00608CD0 (the game's) answers no in A and yes in B. The game itself asks FitsInCity only of rules it has chosen: 0x0063B340 tests the rule's byte +0xE9 and the type mask first, and the view control asks of the player's piece. A first trial without the skip showed that difference.

Reach (the level 3 leg): all four ran. Slot 49 ran at every crossing, but the crossings left 0 or 2 override records and no rule with a query text fitted them, so the question was never asked. The direct clicks found Find and MatchAt (all eight rules answered 2, and 0x0062CD50 placed none). Slot 50 ran at level 2, and runs in the bulldoze legs.

Parity (batches net-t12-b1 and net-t12-b2, 2026-10-16; B list `nall33` = `nall32` with the two t12 groups):

| Leg | Saves | Result |
|---|---|---|
| plain | the 12 saves | 12 of 12 (Rush Hour d1095 on the second pair: A has two outcomes there) |
| `net` | `saves-networks` | 3 of 3, first pair |
| `set` (every network tool drive) | the 12 saves | 12 of 12, first pair |
| `setnet` | `saves-networks` | 3 of 3, first pair |
| `OPENSC4_TEST_BULLDOZE=3` | `saves-networks` | n0000 and n0090 the same. t0000 partly: the fingerprints are the same on all days in all four pairs, and one B run of four saves a stray removed piece at 632,1576, which one A run of four also saves (the original's known stray piece after this bulldoze, docs/netmgr.md) |
| crew (20 days) | Rush Hour t0000 | partly: from day 9, the `propocc` part takes the second value of the original's known two (above); none of the four t12 functions ran in that leg |
| crew with `OPENSC4_TEST_CREW_DRAWLINE=1` | Rush Hour t0000 | the same on all 21 days, first pair |
| crew with `OPENSC4_TEST_CREW_PUZZLE=1` | Rush Hour t0000 | the same (A 2 outcomes, B 2, both shared) |
| crew with `OPENSC4_TEST_CREW_PUZZLE=3` | Rush Hour t0000; `saves-networks` | the same on all 21 days, first pair; 3 of 3, first pair |

All four ran: slot 50 in the bulldoze leg (six B runs) and the puzzle legs; slot 49 and MatchAt in the puzzle legs (MatchAt also in the bulldoze leg); Find in the level 3 legs. A control run of the PUZZLE=1 leg with the t12 groups off (`net-t12-c1`) gave the same day-15 `propocc` B value as the first trial.

## The underground tools (part u1)

Three files, one group, off by default: `simulators.nettool.underground` (`src/core/nettool_under.h`, `nettool_under.cpp` with 16 functions, `nettool_under_2.cpp` with 16 more; the group installs both detour lists, and `OPENSC4_NETTOOL_SKIP` works on both). 34 functions, 32 ours. Address entries in `data/addresses/nettool.toml`; the three cdecl lookups keep their names from `data/addresses/netmgr.toml`.

- The underground tools' base (vtables 0x00AAA950/0x00AAA93C, constructor 0x00657FD0), the pipe tool (0x00AA99A8/0x00AA9994, constructor 0x00648EC0) and the subway tool (0x00AAA5C0/0x00AAA5AC, constructor 0x00653D60). Fields past the general tool's: +0x368 Init once, +0x36C the depth below the terrain (float), +0x370/+0x374/+0x378 what slot 82 hands each new occupant, +0x384 and +0x388 two lists (the occupants a preview made, and the existing ones it met); the pipe and subway tools add a byte at +0x390.
- First half: the constructors and destructors, Init, Shutdown, slots 51, 54, 59, 65 (TestForAutoConnectOccupant), 68, 73 to 75 and 79.
- Second half: slot 82 MakeOccupant of the pipe (0x00648D10) and the subway (0x00653A00), the network manager's lookups 0x00648F70 (pipe at a cell), 0x00649080 (pipe connection at a cell) and 0x00653B80 (subway station), slots 78, 80 (SetQuadTrianglesToDraw: it only sets and clears occupant flags, no drawing), 16 ClearOccupantHighlights, 71, 76, 70 StartPlacing, 58 AddCellPreview, 55 InsertOccupantsForSolvedCells and PlaceOccupant 0x006596C0.

### What stays the game's

- 0x00659240 PropagateFromCell: the 12-byte solved-cell record it appends gets one byte stored into a word whose upper three bytes are never written, and the record is copied whole, so it carries the game's stack leftovers.
- 0x006588D0, the network manager's underground rebuild (2,402 bytes): it keeps the depth on the x87 stack across a loop.
- Slot 81 of the pipe and subway tools (0x00648F40, 0x00653D90) is folded with the occupants' class-ID getters (class registration calls them): 6-byte constant getters, not detoured and not swapped; not counted in the 34.

### Registers and floats

Slots 54, 59, 68 and 74 are naked thunks that leave every register as the game's (slot 68 is a bare `ret 0xC`: its AL is the caller's leftover EAX). Every x87 value comes back as a double (the terrain's slot 0x30, the tool's slot 73), each operation is one double +, - or x with float stores where the game stores, and the compares keep the game's unordered branches. Slot 55 passes the placer's byte out to slot 0x158 inside the whole word of its first argument, as the game does. The vtable callers of every slot that is ours read only EAX after slots 65 and 59, which return exactly the game's values; no caller reads ECX or EDX. No wrapper of ours touches 0x00648D10 to 0x00659A00 or tests return addresses there.

### The test

`OPENSC4_TEST_CREW_UNDERGROUND=1`, with `OPENSC4_TEST_CREW=2`, in `src/core/crew_autotest.cpp`, off by default (legs without it are unchanged): last of the drive, for pipes and then subways, a straight drag on land found as for the six types and a second drag from the cell below its end, down, so it starts against the first one's pieces (the auto connection, slot 65), each through the release. The daily crew part adds ` ug <drags>,<hash>` (the depth and the two lists' lengths after Init and after each drag, each drag's cost and failures and its new pieces). On Rush Hour t0000 it builds 24 + 6 pipe pieces and 24 + 6 subway pieces.

Parity (batch net-u1b-b1, 2026-10-16; B list `nall34` = `nall33` with the underground group):

| Leg | Saves | Result |
|---|---|---|
| plain | the 12 saves | 12 of 12, first pair |
| `net` | `saves-networks` | 3 of 3, first pair |
| crew with `OPENSC4_TEST_CREW_UNDERGROUND=1` (20 days) | Rush Hour t0000 | the same (A 2 outcomes, B 2, both shared: from day 9 the `propocc` part takes the original's known two values) |

Ran with ours (first-call log lines): 30 of the 32 (the three lookups already at city load); not slot 73 0x00657790 and the base's deleting destructor 0x00658250 (the base is never made alone). A trial with only the first half (net-u1b-tr1) was the same on all 21 days.

## The power line tool (part w1)

Four files, one group, off by default: `simulators.nettool.power` (`src/core/nettool_power.h`, `nettool_power.cpp` with 26 functions, `nettool_power_2.cpp` with 12, `nettool_power_3.cpp` with the 2 of part w2; the group installs all three lists, and `OPENSC4_NETTOOL_SKIP` works on each). 43 functions, 40 ours. Address entries in `data/addresses/nettool.toml` (specs `w1/nettool_w1.spec` and `w2/nettool_w2.spec`). No function of the part is detoured or swapped by another group (the `slots` group touches only slots 31 and 32 of the power vtable), none is shared with other classes, and no wrapper of ours touches 0x0064EFE0 to 0x00652A6B or tests return addresses there.

- The tool (0x3B8 bytes, vtables 0x00AA9F30/0x00AA9F20, constructor 0x006503C0, made by the network manager). Fields past the general tool's: +0x368 Init once, +0x36C a vector of 0x30-byte pole records (index, cell, dragged cell, pole occupant, a new byte, the wire sides byte, a vector of links, two referenced objects, two bytes), +0x378 a vector of vectors of 16-byte elements, +0x384 and +0x388 the counts the cost multiplies by the floats +0x3B0 and +0x3AC, +0x38C/+0x390 two occupant lists, +0x394 a list of pole pairs, +0x398 a vector of int vectors, +0x3A4 to +0x3B0 tuning from the power exemplar. 0x00B467B0 is the pole exemplar table by sides byte, filled once per run from the network ini's `PowerPoles` section (flag 0x00B467A8).
- `nettool_power.cpp`: the constructor, destructor, slot 51, slot 3 Init (the once-per-run ini load 0x0064F880 with its line callback 0x0064F140, then the general Init and the tuning), slot 4 Shutdown, slot 20 GetCostOfSolution, slot 52 (a reason of 0 asks the zone manager at the last anchor point for its own text), slot 16 ClearOccupantHighlights (the three lists), slot 82 (the cell previews in two colours), and the vectors' template code.
- `nettool_power_2.cpp`: the pole position on the terrain 0x0064EFE0, the pole record lookups 0x0064F170/0x0064F1D0, the diagonal cell 0x0064F230, LinkPoles 0x0064FB40, ClearPoleRecords 0x006505D0, the wire at a cell 0x0064FBF0, the cell kind 0x0064FD00, the pole at a cell 0x0064FFE0, MakePole 0x00650140, slot 53 DoNetworkSpecificPlacement 0x00652980 and slot 10 UpdateOnZoneChange 0x006522F0.
- `nettool_power_3.cpp` (part w2): the pole planner 0x00650840 and the pole placer 0x00651560, which slot 53 (and the game's 0x006520C0, slot 10's walk) calls; see [the pole planner and placer](#the-pole-planner-and-placer-part-w2).

### What stays the game's

- 0x00650760: the 0x30-byte pole record it builds on the stack never gets its byte +0x14, and the record is copied whole into the vector (stack leftovers, as u1's PropagateFromCell).
- Slot 81 0x0064FED0: its "previous kind" local is read before it is written on a one-step drag whose first cell is a network that is not a pole (stack leftover).
- 0x006520C0 (slot 10's helper, 10 arguments): it tests the record byte +0x14, which MakePole leaves unwritten where a pole exists and no exemplar is found.

### Registers and floats

The direct callers of every function that is ours and the vtable callers of slot 82 read no ECX or EDX after the call; AL or EAX is read only where the game's function sets it, and ours gives the same. No naked thunks. GetCostOfSolution is the count as an unsigned value times the float in one double product, then __ftol2's truncation (NaN and out-of-range give FISTP's indefinite value). The pole position: (x + 0.5) times the cell size as a double product stored as a float, the terrain heights as doubles, and the highest corner with the game's unordered compares (NaN never wins). MakePole pushes slot 0x38's flag as a byte where the game pushes the whole EAX with leftover upper bytes; the callee 0x00649250 reads only the byte.

### The pole planner and placer (part w2)

Both are ours, at their addresses; each calls everything else at the game's address, the planner and the placer included (the placer calls both again).

- The planner (no arguments) walks the valid steps' dragged cells. It forces a pole at each anchor point and the last cell, at the marked step's (+0x74) first cell on the first anchor point's row or column, and on a diagonal cell when the spacing (+0x3A4) of steps since the last pole is reached. On a free cell it takes an existing pole, or a wire's two poles as the record's key, and else places a pole on a diagonal cell when the line of sight from the last pole's top to the next cell's pole top is clear (the renderer 0x00B43DD0 slot 0xF4). A pole is a record (0x00650760) and a run vector of the cells since the last pole (+0x378); the other cells count in +0x384. A zone the line may not cross, flora, lots and other networks end the run, set the failure reason (0xF0000012 for kind 2, 0xF0000013 for kind 3) and may take a lone record back. At the end +0x78 is the last step with a free cell plus 1 and +0x388 the record count.
- The placer (a byte) links the first records of neighbouring dragged cells (+0x398), makes or finds each record's pole (0x00650140), connects the key poles, makes a cSC4PowerLineOccupant at each run cell without one, wires the poles to each other (0x0064E3E0) and puts the new poles in the lists +0x38C and +0x390. With +0x50 and the byte set it redoes the drag between the key poles (the tool's slot 0x48, the NAM-guarded DrawNetworkLine, through the vtable) and runs the planner and itself (0) again.
- Floats. The line of sight keeps nothing on the x87 stack across a call: the differences, the squared length and the hit distance are doubles, the direction, its scale, dy, dz and the squared length are stored as floats where the game stores them, and the pole goes up when the square root of the stored squared length is at least the hit distance (unordered: no pole). The placer's neighbour test is |dz| + |dx| of the cells as unsigned values, equal to 1.0: exact.
- Stack leftovers, none of them read: the planner's key padding (and its two bytes before the first wire is looked at) goes into the record's +0x2C to +0x2F, which is read only for records whose key words are set; the "a pole before" byte goes to 0x00650760 as a word the callee reads only the low byte of; the placer's pairs carry a third word nobody reads. Ours writes zeros or the byte there. SetPreview of a new wire gets the flag where the game pushes all of ECX; the callee reads the byte. The byte +0x14 that 0x00650760 (the game's) leaves from its own stack depends on the frames below the caller, as it already did under part w1's helpers.
- Registers: no caller reads ECX, EDX or EAX after either function. No naked thunks.

### The test

`OPENSC4_TEST_CREW_POWER=1`, with `OPENSC4_TEST_CREW=2`, in `src/core/crew_autotest.cpp`, off by default (legs without it are unchanged): last of the drive, a straight power line on land found as for the six types, a second from the cell below its end, down (a corner pole), and one across the middle of the road's strip from the row above to two rows below, each through the release. The daily crew part adds ` pw <drags>,<hash>` (each drag's cost, failures and new pieces). On Rush Hour t0000: 24 cells for 100, 6 for 38, and the road crossing refused (failure 1) by both sides.

Parity (batch net-w1-b1, 2026-10-17; B list `nall35` = `nall34` with the power group):

| Leg | Saves | Result |
|---|---|---|
| plain | the 12 saves | 12 of 12, first pair |
| `net` | `saves-networks` | 3 of 3, first pair |
| crew with `OPENSC4_TEST_CREW_POWER=1` (20 days) | Rush Hour t0000 | the ` pw` token and every log line the same; from day 9 the `propocc` part differs (two pairs, A one value, B the other): both are the original's known two values of that save while crews work (A runs of earlier batches give each, 7 and 10 times) |

Ran with ours (first-call log lines): 31 of the 38, slot 10 among them (in the power leg only, right after the drive). The same leg reaches part w2's two functions: the w1 runs logged FindDiagonalCell and FindWireAt, which only the planner calls, and slot 53 calls the placer right after the planner. Not reached there: the wire found at a free cell (HasPoleRecordWithKey and HasPoleInLine never ran), so the record keys stay empty and the placer's key pole branches, the pairs and the redone drag (+0x50 with the byte set) do not run. Whether slot 10's walk 0x006520C0 (the planner and placer with +0x50 set) ran is not in the log. Part w2 parity (batch net-w2-b1, B list with determinism.dispatch_info): plain 12/12, saves-networks 3/3, the power crew leg (Rush Hour t0000, 20 days) 1/1, all on the first pair; both functions logged their first call in the crew leg. Not reached: slot 52's own text for a reason of 0 (only the UI asks for it), the pole lookups 0x0064F170 and the line test 0x0064F1D0, and four vector templates (the fill and the 16-byte vectors' copies and assignment, used only when vectors grow by more than one or are copied).

## Placing (part t6)

`src/core/nettool_place.h`, `nettool_place_1.cpp` and `nettool_place_2.cpp`, five groups, off by default:

- `simulators.nettool.place` (8): slot 19 PlaceAllSegments 0x0063BDC0 (a construction crew made for every type but 4, 5 and 7, by its jump table 0x0063BF20, the line drawn and the segment placer run per segment, the crew started), the segment placer 0x0063B830 (the cell infos, reserved tiles, the solver, the height pass, bridges, tunnels, occupants and the crew's AddSegment in the game's order), slot 7 PlaceNetworkOccupants 0x00629210 and slot 8 PlaceNetworkOccupantById 0x00624FD0 (the network manager's lot connection code calls them), MarkInvalidSteps 0x00624BF0, the neighbour and edge connections' application and the override records' clearing.
- `simulators.nettool.placerules` (5): the RUL0 intersections found along a drag (FindRuleIntersections 0x0063B340, EAX the count; the rule's fit in the city 0x00608CD0), their cells marked, and their occupants placed.
- `simulators.nettool.placecells` (9): the steps' edges, the wide pens' sides, the rule cell records, the tunnels found and their ends, the changed cells' preview heights, and the changed cells' and vertices' resolution.
- `simulators.nettool.edgewords` (4): the side byte helpers (0x00620FC0 and 0x00621440 among them, which earlier parts called at their addresses).
- `simulators.nettool.isectvector` (8): the intersections' and major bridges' vectors.

The guarded DrawNetworkLine 0x0063AF40 is called through the vtable, the guarded 0x0062CD50's calls into this part keep the game's conventions (the register scan of every caller found nothing read after them); the bridge, tunnel and occupant code of parts t7, t10 and t11 is called at its addresses. The crew is made, initialised (with the tuning table's values), fed and started through the crew's own code at its addresses: game.quiet.network's observer and sim_timers do not look at the caller. Nothing of this part draws.

Floats: slot 8's heights (the margin around the unrounded height), the changed cells' compare (NaN as the x87's), the tunnels' ground (four altitudes, partial sums rounded, times 0.25) and the tunnel end's slope through the x87's FPTAN (a naked helper).

Stack leftovers (ours zero, none read): slot 8's solved record and its stack cell info's unwritten fields, the changed cells' rotation word, and the intersections' +0x18 (no reader in the tool's code; NAM's DLL is not checked).

The test, `src/core/nettool_place_autotest.cpp`: the daily part ` | ntpl i<n> t<n> a<n> <hash>` (order 205): per tool the failure reason, the intersections (but +0x18), the tunnel records and the anchors. `OPENSC4_TEST_NETTOOL_PLACE=1` with `OPENSC4_TEST_NETTOOL=<day>`: the four edge word helpers on words of the test's and FitsInCity for every RUL0 rule at 49 points, read only. Placing changes the city (even the preview places intersection occupants), so it is reached only by the crew test's drags; slots 7 and 8 by the network manager's lot connections.

## The bridges (parts t10 and t11)

`src/core/nettool_bridge.h` (the layouts), `nettool_bridge_a.cpp` (t10: 27 functions, 16 ours) and `nettool_bridge_b.cpp` (t11: 20 functions, all ours), four groups, off by default. The survey only inferred "bridges"; checked against the code:

- A major bridge is a 0x28-byte element of the tool's vector +0xD8: +0x0 the network type, +0x4 and +0x8 the first and last step, +0xC the index of its record in the type row's 0x178-byte bridge records (+0x30), +0x10 the deck altitude, +0x14 the reference altitude (a minor bridge: its height in both), the byte +0x18 (land bridge; 1 for a minor bridge) and +0x1C the cell vector (begin, end, capacity).
- A bridge cell is 0x50 bytes: +0x0 the kind (0 ramp, 1 pier, 2 span), +0x4 and +0x8 x and z, +0xC the piece ID, the byte +0x10 its turn, +0x14 the four corner heights (part t9 reads them), +0x24 the deck height, +0x28 the pier's depth below the deck (-1.0 none), +0x2C four floats and +0x3C a float (the record's +0xA4), the byte +0x40 the pier flags (1 or 2; 4 for a pylon; 8 or 0x10 for the side) and +0x44 a vector of 32-byte cable parts. The copy constructor 0x006267D0 copies the words, the bytes +0x10 and +0x40 alone and the part vector (through 0x00624EA0); the padding +0x11 to +0x13 and +0x41 to +0x43 is never copied.
- The record's fields the code reads are listed in `nettool_bridge.h` (spans in steps at +0x4 and +0x8, the cells across at +0xC, the ramp and pier pieces, the pier spacing, the wall and cable fields, the water depth range +0xC8 and +0xCC, the deck heights +0x5C over water and +0x78 over land). The tool's +0x33C lists the records usable over water (byte +0x99) and +0x348 those over land (byte +0x98), made by ListBridgeKinds (part t3).

Part t11 decides where a bridge goes; part t10 builds its cells and occupants:

- `simulators.nettool.bridgepick` (11): FindMajorBridges 0x00632830 (over water: the records whose byte +0x99 is set, picked by the water's depth and the span's length), FitMinorBridges 0x00632AD0 (over a dip: the records whose byte +0x98 is set, depth 0), AddMinorBridge 0x00632580, FindBridgeSpan 0x00630570, FitBridgeSpan 0x0062FED0 (the bridge height), PickBridgeRecord 0x0062F760, ExtendBridgeSpan 0x0062ECD0, StepCellsFitBridge 0x0062EB80, CellsAreFree 0x0062FDB0, LowestCellCorner 0x0062E630 and RaiseMajorBridgeAltitude 0x0062EA90.
- `simulators.nettool.bridgeplace` (9): LayBridgeCells 0x00632060 (the cells from the record's layout for the span's length, or through 0x00631300 when it has none) and the containers under the bridges: the 32-byte element vector's copy constructor 0x00624EA0, the bridge cell's copy constructor 0x006267D0, the cell vector's Free 0x0062BF20 and copy constructor 0x0062FB00, the major bridges' uninitialized copy 0x00631200 and fill 0x00631260, InsertOverflow 0x006323E0 and PushBack 0x00632510.
- `simulators.nettool.bridges` (6): PlaceMajorBridgeOccupants 0x0062F610 (PlaceAllSegments' placing pass, part t6, calls it), PlaceBridgeCellOccupant 0x0062EEB0 (the occupant through slot 54 with the kind 0x49CC1BCD, set up as a pre-built model by 0x00624860 with the deck offset, its piece, flags and passage ceiling; the cell cleared; the occupant inserted into the occupant manager and the placement's list), BuildMajorBridgeCells 0x00631300 (a ramp piece at each end, pier pieces at the record's spacing, span pieces between) and the cell vector's PushBack 0x006312C0, InsertOverflow 0x006310F0 and UninitializedCopy 0x0062F6B0.
- `simulators.nettool.bridgesort` (10): the float range template code the cable geometry sorts with and the occupant code's lowest corner: MinElement 0x0062DB90, CopyBackward 0x0062DC30, PushHeap 0x0062DBD0, AdjustHeap 0x0062EA20, MakeHeap 0x0062EC70, SortHeap 0x0062EE50, UnguardedPartition 0x0062E9E0, LinearInsert 0x0062EDF0, PartialSort 0x0062F6E0 and IntrosortLoop 0x006306D0.

### What stays the game's

Eleven functions of part t10: 0x0062DF30 AddBridgeCellWalls (the side walls of an arch or suspension cell) with its helpers 0x0062DCB0 and 0x0062DB30, 0x0062E690 RaiseWallsAlongCurve, 0x00630F70 AddCableParts, 0x006307A0 AddCableSegmentParts, and 0x00643FB0 SetUpPieceSupports with 0x00643830, 0x006439C0, 0x00643B70 and 0x00643E10.

They are not only drawing. They build the occupants' model parts through AddPart (0x0061C800, slot 0x7C), which appends 0x4C-byte records (model triangles) to the occupant's vector +0x118 and recomputes its box +0x124 to +0x138 (slot 0x140, 0x0061DB50, for pre-built and bridge occupants). The network occupant's Write 0x0061A190 saves both, and the occupant manager places the occupant by that box (PlaceBridgeCellOccupant inserts the occupant after its AddPart calls). In the saves-networks corpus all 48 bridge pieces of Big City n0000 and n0090 carry wall records (keys 0xAFAFEA00 and 0xAFAFEE00), and plain occupants carry the support quads of 0x00643FB0 (keys 0x00BB0200 and 0x00BB0000).

They stay the game's because they are model geometry (the AddPart family is fenced for the Renderer thread, [netocc.md](netocc.md)), and 0x0062E690 uses FLDL2E, FRNDINT, F2XM1 and FSCALE: the result of F2XM1 is implementation-specific and cannot be reproduced in C++. Ours call them at their addresses with the game's arguments, so parity holds. One consequence: PlaceBridgeCellOccupant's own pier quads and cable triangles (ours) are saved state too, which is why they were checked bit for bit.

None of the part is guarded or calls a guarded function; no NAM or sc4fix patch site lies in 0x0062DB30 to 0x00632D44 or 0x00643830 to 0x006444E8, and NAM's DLL does not call them. NAM reads the tool by layout (+0x1C, +0x54, +0x60, +0x98, +0xCC, +0x124, +0x188, +0x240, +0x244, +0x2B5): bridges, cells and records stay in the game's memory with the game's layout and allocator. No wrapper of ours tests return addresses in these ranges (the crew's caller range is 0x00605490 to 0x00608C50), so nothing needed a mark.

### Registers

Every game caller was scanned after each call: none reads ECX or EDX before writing it, so all but two are plain detours. CellsAreFree takes the occupant manager in ESI (an LTCG convention; its four callers load ESI from the tool's +0x48 and never read it after as anything else), and LowestCellCorner takes the terrain in ECX with two stack arguments the caller pops (`ret` with no count, ST0 the answer): both go through naked thunks. EAX is read after PickBridgeRecord (the index), the uninitialized copy and fill (the end pointer), UnguardedPartition (the cut), MinElement (the pointer) and, as AL, after FindBridgeSpan, ExtendBridgeSpan, StepCellsFitBridge and CellsAreFree. LinearInsert's EAX on its second path is the x87 status word in the game; no caller reads it.

### Floats

The x87 runs at 53 bits: intermediates are doubles where every operation is +, -, * or a compare, and every float load or store rounds as `float`, in the game's order and association. No function keeps an x87 value across a call. The ones that matter:

- The pier count fit is the game's round by the magic constant: float((x - 0.5) + 12582912.0) whose bits less 0x4B400000 are k, and k + 1 when x equals k + 1 exactly (equal only when ordered).
- PlaceBridgeCellOccupant: the deck offset, the passage ceiling and the pier top rounded once each; the lowest terrain altitude of the cell's four vertices compared unrounded with the running float; the water line w - 0.01 (the double 0x00A92688) for the first three corners rounded and for the fourth unrounded (the game compares the stack value); `test ah, 5` and `test ah, 0x41` compares, which take the game's branch for a NaN.
- The sort and heap code: float compares only, written so that a NaN takes the game's branch. A value the game moves through FLD and FSTP differs from a bit copy only for a signalling NaN.
- LowestCellCorner: the least of the terrain's four corner heights (slot 0x44), `test ah, 5; jnp` (a NaN picks the second operand), loaded with FLD.
- FitBridgeSpan: the limits params+0 * 14.0 and * 4.0, the highest of the cells' least corners, the depth the largest float(h - cell height) over the first water cell of each step; PickBridgeRecord's depth range compares (ordered only).

### Stack leftovers

Ours are zero where the original stores whatever was on its stack.

- BuildMajorBridgeCells and LayBridgeCells build each cell from a 0x50-byte frame template whose part vector alone is cleared. The floats +0x2C to +0x38 and +0x3C are written only for a cell that gets pier flags (BuildMajorBridgeCells: both pier markers set and the record's +0xB0 and +0xB4 non-zero; LayBridgeCells: a piece kind not 0 or a pier group pending); a cell with +0x40 = 0 keeps the frame's leftovers there, and the copy constructor carries them into every copy. Readers: 0x0062DF30 (only when +0x40 is not 0), 0x0062E690 (when the record's +0xB0 and +0xB4 are set and a pier was placed, every cell then has flags; for a group of steps it takes the minimum with the old word, so the group's first step, whose plain cells were laid before the group began, can mix in a leftover), the copies, and nothing else. Verdict: never read as a value.
- AddMinorBridge and FindMajorBridges append a major bridge from a 0x28-byte local; both overwrite every word and the byte before the copy. Harmless.
- FitMinorBridges' dip top and first highest are read only after a dip's start writes them. Ours start at 0.
- LayBridgeCells' previous group kind is read only when a group is pending, which sets it.
- FitBridgeSpan's after-span corner pass (0x006301A0 to 0x006301A8): terrain slot 0x44 (0x007415F0) writes nothing for a cell outside the city, so the game would compare its stack's leftover corners where ours compares zeros. Those cells passed CellsAreFree and the fit check, so they are inside the city: harmless.
- LowestCellCorner: its 16-byte corner buffer is not written by slot 0x44 when x >= [terrain+0x28] or z >= [terrain+0x2C] (unsigned); the game then returns the previous call's corners (likely the previous cell's), ours 0.0. Reader: FitBridgeSpan (the bridge height choice, 0x0062FFEF). Gate: an off-map cell in a drag step, which cannot occur. Harmless.
- PlaceBridgeCellOccupant: slot 54's box out, the occupant's position, the pier quads and the cable buffers are written before each use. None.

### The test

`OPENSC4_TEST_CREW_BRIDGE=<1|2>` in `src/core/crew_autotest.cpp`, off by default (the crew test without it is unchanged); 2 adds avenue and highway. After the `work` part it drags across water for road and rail (rail backwards, the reversed paths): a straight row or column with exactly W water cells between four land cells on each side and only water and land of that verdict in the band around it, W taken from the type's water records (+0x99: each record's shortest span and the middle of its range, up to four lines). The first line whose preview has a major bridge is dragged for real: the preview, with `OPENSC4_TEST_CREW_BRIDGE_CHOICES` (default 1) a preview per possible bridge (+0x1F4) with Reset, the preference, the anchors and the preview, the default preview, and the build with the game's own choice. A road over a dip covers FitMinorBridges (logged when the city has none). The city's network pieces are counted before the first preview and after each Reset (bridge occupants must go with it). The daily part adds ` brg <drags>,<bridges>,<choices>,<hash>`: the major bridges' words and land byte, their cells' words +0x0 to +0x28 with the turn byte alone, the flag byte +0x40 and, where it is set, +0x2C to +0x3C, the cable parts, the possible bridges, the cost and the failure reason. One log line per drag, `autotest: crew: bridge ...`.

Parity (batch b1, B list `nall31`): plain 12/12, net 3/3, set 12/12, setnet 3/3, all on the first pair. Crew and bridge legs (Rush Hour t0000, 20 days, 4 pairs each) partly, as the crew leg is in the original: the bridge leg's original gave 4 outcomes in 4 runs, and over 16 runs every part of every B fingerprint on every day was a value an A run also had, ` brg` identical in all of them; the crew leg's B values of `propocc` from day 9 not in its own A runs were all among the 62 earlier A runs of that leg. One A run of the crew leg crashed in the game's own code while loading (0x00975CC8, writing 0x00AD8D88, before day 1; none of our groups installed) and was rerun. In the bridge leg on Rush Hour t0000 the road line (109,59)-(109,70) crosses 4 water cells: one major bridge, six possible bridges, 12 pieces built; no rail line fit the rail records' lengths, and no land dip passed the search (FitMinorBridges ran, AddMinorBridge did not). 24 of the 36 ran; not reached: AddMinorBridge, the cable sort (IntrosortLoop, UnguardedPartition, LinearInsert, CopyBackward: only through a cable bridge's geometry), the heap fallbacks (PartialSort, MakeHeap, AdjustHeap, PushHeap, SortHeap: only when introsort runs out of depth), the cell vector's UninitializedCopy and the major bridges' UninitializedFill.

## The test

`src/core/nettool_rul_autotest.cpp`, with `OPENSC4_TEST_NETTOOL_RUL=<day>`: every day the fingerprint part ` | ntrul s<count>:<hash> m<count>:<hash> t<hash>` (order 177): a read-only checksum of the RUL0 store (each rule's fields but the leftovers above), the RUL2 multimap and the 13 type rows, order-free over the hash tables. On that day (hook order 199, ` d<hash>` after): the edge word mirror on 64 words, FindOrInsert of an existing ID (it must give the stored rule), and for up to 48 rules with a cell grid a copy on the test's stack (the copy constructor) rotated, transposed, translated and rotated again, hashed after each step, then destroyed. The log line `autotest: ntrul: leftovers` (never hashed) reports the leftover probe above.

## The test

`src/core/nettool_satisfier_autotest.cpp`. `OPENSC4_TEST_NETTOOL_SOLVER=<day>` (day hook order 198; the fingerprint part ` | ntsolve set <hash>`, order 176, once it has run) makes satisfiers of its own (operator new and the constructor at the game's address; the tools' two are never touched) and calls every function at the game's address, with vertex numbers from the city's size: a solvable problem (a fixed vertex, free ones, slopes, equalities that make, join and merge groups, curvatures, both preferences, the results read every way), one with real ranges (RestrictVertex, the sum and midpoint merges called directly, PropagateAll alone, the entry table hashed, then Solve; deleted through the vtable), an impossible one (Solve fails, +0x10 and +0x05 hashed, DumpGroups), and the range arithmetic on pairs of the test's. Only values are hashed. One log line per scenario, `autotest: ntsolve: ...`.

## The test

`src/core/nettool_autotest.cpp`. The fingerprint part ` | nettool ...` (order 175) is always on (` | nettool none` before the network manager is initialised). It reads the manager's 13 kept tools (GetNetworkTool with `unique` 0) through their vtables, so it runs the game's functions or ours, and mixes:

- `tools`, `init`, `rows`, `used` (tools with steps or solved cells: a drag ran), `cost` (tools GetCostOfSolution was asked of), `bridges` (records, with a bitmap key), `fail`: counts.
- `st`: the type, the vtable as an index among the family's five, the type row as an index in the type table, the flag bytes, the masks, the step counts, the city size and the lengths of every container.
- `q`: QueryInterface with four IIDs (the answer's low byte and the interface's offset in the object; the reference given back), the preferred bridge, the altitude override's bits, the failure state, the four major bridge getters, the possible neighbour connection, the possible bridges, IsRegularStep and GetLastValidPoint where the vectors make the read an inside one, GetCostOfSolution only for an empty solution (slot 20 the general tool's, Init run once, no solved cells at +0x98), DoNetworkSpecificPlacement (not on the power line tool) and GetNetworkHeightForTerrainHeight of three heights where slot 74 is 0x0062C990.
- `ids`: on tools with a type row, the four IDs, PenWidth and the placement parameters (words, bytes and the five vectors' contents; their memory freed).
- `br`: on tools with a type row, each bridge record's bitmap key and name.

The tools are met in four states: untouched (never initialised: no row, empty vectors, no terrain or occupant manager), initialised, used (a drag ran) and shut down after use. Shutdown 0x00626D30 releases and nulls the terrain +0x44, the occupant manager +0x48, +0x4C, the filter +0x364 and the preview drawable +0x1F0, empties the possible bridges, the anchors and the dragged cells and clears +0x18; it leaves the row, the parameters, the steps, the solved cells, the major bridges and the world cache. Everything the part reads is valid in all four. GetCostOfSolution is not: for each solved cell it calls the cell entry 0x00633220 (which makes a cell info through 0x00624B70 and the occupant manager when the cache has none) and the terrain's slot 0x30, both null after Shutdown. The first version of the part asked it on the road tool the crew test had used and shut down, and the game exited silently there on both sides; so it is now asked only with no solved cells, where it reads vectors and words and calls nothing.

Not in it: OccupantExists, because its lookup makes a cell info when the cache has none (and on a tool that was never initialised the maker would use the null occupant manager at +0x48). The failure reason at +0x20C is not written by the constructor; on a tool that was never Reset it is what the allocator left, zero under `determinism.zero_alloc` (in the parity base list).

`OPENSC4_TEST_NETTOOL=<day>` (day hook `nettool.drive`, order 197) on that day calls the setters on each of the 13 tools and puts the old values back: SetPreferredBridge, SetBridgeAltitudeOverride (the old four bytes back as they were), EnablePreviewUpdate, ForceReload (the byte written back: no setter clears it), ClearFailureReason around a reason the test writes (0xF0000003) with GetFailureState and GetFailureReasonString before and after, slot 52 with three of the game's reasons and one without a text, SetValidStepCount (only on a tool with no steps, where MarkInvalidSteps has nothing to mark), SetAnchorPoints with two points of the test's and then the old points, GetDraggedCells, and UpdateOnZoneChange. The power line tool gets neither the texts (its slot 52 does other work for a reason of 0) nor UpdateOnZoneChange (its own slot 10). The part then ends with ` set <checksum>`, and one log line `autotest: nettool: drive: ...` says on how many tools each step ran.

The part also prints, for part t2, ` cells <cells>,<copies>,<found> ch <hash>`: over the 13 tools' world caches, the cells walked in the cells' tables, the cells in the copies, how many walked cells GetCell (through its address) gives back as the same node, and per tool the type, both tables' counts and an order-free sum and xor of each cell's hash (the key and every field but the occupant pointer, the padding and the second pairs +0x88 to +0xA4, which the game's 0x00632EF0 leaves as its stack had them: that stack differs once our code has run at that depth before it). The drive below hashes the second pairs too: its cells come only from the maker 0x00633030, which sets them. It only reads: the cell entry 0x00633220 is not called (it makes cell infos, and on a shut-down tool its maker would use the null terrain and occupant manager). The cache is valid in every state the game leaves a tool in: the constructor makes it, Shutdown leaves it, only Reset and the placement code clear it.

`OPENSC4_TEST_NETTOOL_CELLS=1`, with `OPENSC4_TEST_NETTOOL=<day>`, adds on that day the world cache's functions on the road tool (type 0), as the game's own callers use a tool (AddRef, Init, the calls, Shutdown, Release), and only when the game has not left it initialised: the cache emptied (Reset 0x00647B20), the cell entry 0x00633220 for every fourth cell in x and z (it makes the cell info of each cell that has a network occupant, through the maker, the constructor, the edge sum, the copy, the assignment, the find-or-insert and the node insert) and each found cell's hash; the cache's sums; SaveCells and the copy's sums; the cells' table cleared and RestoreCells; ClearCellIndices; CollectCells into a vector of the test's (each cell's hash in order; the occupants released and the memory freed after); the neighbour walker's two slots in four directions on the first 16 cells found (an object on the test's stack with the vtable 0x00AA8700, laid out as 0x00627080 makes it); and the cache emptied again. It leaves the road tool's cache empty, whatever a drag had left there, so it is a switch of its own. One log line `autotest: nettool: cells: ...` gives the counts; the checksum goes into ` set`.

Part t3 adds ` lf <hash>` after `ch`: per tool what the constructor, Init and DoMessage leave: the vertex counts and the sixteen vertex steps, the terrain's two floats and the angle, the message's word +0xFC, the two bridge kind lists, the filter's mask, whether there is a preview list, and the preview drawable's state (whether its vtables are the known ones, its reference count, its two trees' counts, whether it has a material, and the bytes +0x48 to +0x57). `OPENSC4_TEST_NETTOOL_LIFE=<bits>`, with `OPENSC4_TEST_NETTOOL=<day>`, adds on that day a new tool of each type (GetNetworkTool with `unique`, so the constructors, the three classes of their own calling ours) through its life, each step's fields and `lf` hash mixed: AddRef; Init twice (the second does nothing); Reset (whether its answer is the rule records' begin); slots 17 and 16; with bit 2 GetUITool (the UI object released after); DoMessage with a stand-in message of another type and then of 0xA6B79602 (its word put back after); Shutdown twice; Init and Shutdown again; Release, which deletes the tool through slot 51 (the destructors of the tool and of its drawable). One log line `autotest: nettool: life: ...` gives the counts.

Unreached by these tests and by every coverage run so far: everything that needs a drag (the steps, the dragged cells, the major bridges, the neighbour connections and the possible bridges are empty on the kept tools), GetNetworkOccupantAt, the direction and edge helpers, CopyWord and GetLowestNetworkTypeFromFlags (their callers are drag and placement code). OPENSC4_TEST_CREW's drags reach some of them through PlaceAllSegments.

## Parity

Batch y2 (2026-10-14), part t1: before the first run `ret N` of the 35 plain detours against our signatures (no mismatch; the four naked thunks checked by hand), the caller register scan (two hits, 0x0062B8E9 pushing EDX after 0x00623E60 and 0x00644492 pushing ECX after 0x00620EC0: the thunks leave both registers as the game does, and the second is only stack room) and the EAX scan (the power tool's slot 52 tail-jumps into GetFailureReasonText: no caller reads the answer), and an independent reading of all 43 functions and the four slot swaps against the disassembly (no difference in behaviour; each swap writes only the tool family's vtables, the 64 other places the folded code sits keep the game's).

The original against every network group of main, the seven crew groups and the three groups of this part:

| Leg | Saves | Result |
|---|---|---|
| plain | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair |
| `OPENSC4_TEST_NETTOOL=3` (with the other network tests) | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair |
| `OPENSC4_TEST_CREW=2 OPENSC4_TEST_NETTOOL=3 OPENSC4_TEST_TUTORIAL_OFF=1`, 40 days | `saves-networks` Rush Hour t0000 | the same on all 41 days, the first pair |

On a city where no tool has been used the part reads only the shallow state (`tools 13 init 0 rows 0 used 0`); after the crew test's drags it reads six tools with a type row and a used solution and their 24 bridge records, the same on both sides.

A first version of the part asked every tool with a type row for its cost; on a tool the crew test had used and shut down that makes the game's own cost function look up its solved cells through the terrain and occupant manager Shutdown has nulled, and both sides, the original included, left the game silently. The cost is now asked of an empty solution only.

Ran with ours (first-call log lines): 41 of the 43.

Batch z1 (2026-10-14), part t2: before the first run `ret N` of the plain detours against our signatures (no mismatch), the caller register scan (one hit, 0x00633B0E after the cell entry: the next call, 0x00623C90, is cdecl and loads ECX itself before using it) and an independent reading of all 23 functions and the four naked thunks against the disassembly, with every game caller of 0x00633220, 0x00647A20 and 0x00647830 scanned for a read of ECX, EDX or the flags they leave (no difference in behaviour; no caller reads a register the thunks leave differently).

The original against every network group of main, the seven crew groups and the five groups of parts t1 and t2:

| Leg | Saves | Result |
|---|---|---|
| plain | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair |
| `OPENSC4_TEST_NETLOT=3 OPENSC4_TEST_NETTOOL=3 OPENSC4_TEST_NETTOOL_CELLS=1` (with the other network tests) | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair |
| `OPENSC4_TEST_CREW=2 OPENSC4_TEST_NETTOOL=3 OPENSC4_TEST_TUTORIAL_OFF=1`, 20 days | `saves-networks` Rush Hour t0000 | the same on all 21 days, the first pair |

The cells drive on Rush Hour t0000 asks 1,024 cells, finds 46, and caches, saves, restores and collects 46 and walks 16, the same on both sides.

Ran with ours (first-call log lines): 22 of the 23. MarkRuleCells did not run: it is reached only when a RUL0 override rule matches during a drag, and no test drag in these cities meets one.

Batch b1 (2026-10-14), part t3: before the first run `ret N` of the 23 detours against our signatures (no mismatch), the caller register scan (52 direct call sites; none reads ECX or EDX after the call) and the EAX scan (only the constructors' `this` is read, which ours return), and an independent reading of all 23 functions against the disassembly (no difference in behaviour).

The original against every network group of main, the seven crew groups and the seven groups of parts t1 to t3 (B list `nall19.txt`):

| Leg | Saves | Result |
|---|---|---|
| plain | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair |
| `OPENSC4_TEST_NETTOOL=3 OPENSC4_TEST_NETTOOL_CELLS=1 OPENSC4_TEST_NETTOOL_LIFE=3` (with the other network tests) | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair |
| `OPENSC4_TEST_CREW=2 OPENSC4_TEST_NETTOOL=3 OPENSC4_TEST_NETTOOL_LIFE=3 OPENSC4_TEST_TUTORIAL_OFF=1`, 20 days | `saves-networks` Rush Hour t0000 | partly: from day 9 the `propocc` part has two values (four prop records of one lot in another order), A one of them in 4 runs, B both in 4; B's second value is one the original gives too (both sides of the original-only crew controls `net5c-p1`, `net5c-w1a`, `net5c-y1c`), the known two-valued `propocc` of that save while crews work |

The life drive makes, initialises, resets, shuts down and deletes 13 tools, with 13 UI objects and 13 messages, the same on both sides.

Ran with ours (first-call log lines): 22 of the 23. The drawable's cell tree eraser 0x0061FCD0 did not run: the drives never left nodes in that tree when it was emptied (the crew's drags reached the range erasers and the textured tree's eraser).

Batch s1b1 (2026-10-14), part s1: before the first run `ret N` of the 45 detours against our signatures (no mismatch), the caller register scan (127 call sites; none reads ECX or EDX after the call), the EAX scan (every read is AL or a pointer answer ours gives) and an independent reading of all 45 against the disassembly, every x87 compare's NaN branch included (no difference in behaviour). A trial pair on Rush Hour t0000 with the solver test: the four scenario hashes the same on both sides, all 45 functions run with ours.

The original against every network group of main, the crew groups and the nine groups of parts t1, t2, t3 and s1 (B list `nall20.txt`):

| Leg | Saves | Result |
|---|---|---|
| plain | the 12 saves; `saves-networks` | 12 of 12 (Rush Hour d0000 on the second pair: the original gave two outcomes there, ours one of them); 3 of 3, the first pair |
| `OPENSC4_TEST_NETTOOL=3 OPENSC4_TEST_NETTOOL_CELLS=1 OPENSC4_TEST_NETTOOL_LIFE=3 OPENSC4_TEST_NETTOOL_SOLVER=3` (with the other network tests) | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair |
| `OPENSC4_TEST_CREW=2 OPENSC4_TEST_NETTOOL=3 OPENSC4_TEST_NETTOOL_LIFE=3 OPENSC4_TEST_TUTORIAL_OFF=1`, 20 days | `saves-networks` Rush Hour t0000 | the same on all 21 days, the first pair |

Ran with ours (first-call log lines): all 45 with the solver test; the crew's drags alone reach 44 (all but the deleting destructor, which has no caller in the game: only the vtable slot).

Batch r1b1 (2026-10-14), part r1: before the first run `ret N` of the 28 plain detours against our signatures (no mismatch; the thunk checked by hand), the caller register scan (54 call sites; the four flagged sites only move bytes through CL and DL and reload the full registers before any full use) and an independent reading of all 29 against the disassembly, the three jump tables and the flag map decoded from the exe's bytes (no difference in behaviour). A trial pair on Rush Hour t0000: the store (249 rules), the multimap (844 entries), the type rows and the transform drive hash the same on both sides.

The original against every network group of main and the eleven nettool groups (B list `nall21.txt`):

| Leg | Saves | Result |
|---|---|---|
| plain | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair |
| `OPENSC4_TEST_NETTOOL=3 OPENSC4_TEST_NETTOOL_CELLS=1 OPENSC4_TEST_NETTOOL_LIFE=3 OPENSC4_TEST_NETTOOL_SOLVER=3 OPENSC4_TEST_NETTOOL_RUL=3` (with the other network tests) | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair (the `ntrul` store and multimap hashes the same on every save) |
| `OPENSC4_TEST_CREW=2 OPENSC4_TEST_NETTOOL=3 OPENSC4_TEST_NETTOOL_LIFE=3 OPENSC4_TEST_TUTORIAL_OFF=1`, 20 days | `saves-networks` Rush Hour t0000 | partly: A 3 outcomes in 4 runs, B 3, 2 shared. The runs part in two places, each two-valued in the original: `propocc` from day 9 (above) and `citylot` with `occmgr` from day 15 (A's own runs part there); B's unshared outcome is the second day-9 value with the first day-15 value, a combination that original-only crew controls (`net5c-c1`, `net5c-p1`, `net5c-p2`) also show |

Ran with ours (first-call log lines): 24 of the 29 (all but the two map erasers, the reset callback, and the type row's static constructor and destructor: second load, exit, app reset and CRT only).

Batch r2b1 (2026-10-14), part r2: before the first run `ret N` of the 35 detours against our signatures (no mismatch), the caller register scan (68 call sites; the three flagged sites only move bytes through CL) and an independent reading of all 35 against the disassembly, the record's cIGZFile slots and the multimap's insert order included (no difference in behaviour). A trial pair on Rush Hour t0000: the 24 rule files, the r2 drive and a reload of row 0's first file (326 entries) the same on both sides.

The original against every network group of main and the thirteen nettool groups (B list `nall22.txt`):

| Leg | Saves | Result |
|---|---|---|
| plain | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair |
| every drive, `OPENSC4_TEST_NETTOOL_RUL=3` included | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair (the rule files' hash the same on all 15) |
| `OPENSC4_TEST_CREW=2 OPENSC4_TEST_NETTOOL=3 OPENSC4_TEST_NETTOOL_LIFE=3 OPENSC4_TEST_TUTORIAL_OFF=1`, 20 days | `saves-networks` Rush Hour t0000 | the same on all 21 days, the first pair |

Ran with ours (first-call log lines): 33 of the 35 (not the tile vector's uninitialised copy and fill 0x00621110 and 0x00621150, used only when a tile vector grows by more than one at a time).

Batch r3b1 (2026-10-14), part r3: before the first run `ret N` of the 43 plain detours against our signatures (no mismatch; the EAX thunk of 0x0063CBE0 checked by hand), the caller register scan (152 call sites; the two flagged sites write EDX first, by `imul` and by a byte move) and an independent reading of all 44 against the disassembly, the guarded callback's code after its calls, the x87 helpers and the lexer included (no difference in behaviour). The first trial pair differed only in the test's hash of the bridge elements, through the three leftover bytes above (the original's stack, shifted by our deeper code); the test no longer hashes them.

The original against every network group of main and the sixteen nettool groups (B list `nall23.txt`):

| Leg | Saves | Result |
|---|---|---|
| plain | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair |
| every drive, `OPENSC4_TEST_NETTOOL_RUL=3` included | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair (the r3 hashes the same on all 15) |
| `OPENSC4_TEST_CREW=2 OPENSC4_TEST_NETTOOL=3 OPENSC4_TEST_NETTOOL_LIFE=3 OPENSC4_TEST_TUTORIAL_OFF=1`, 20 days | `saves-networks` Rush Hour t0000 | the same on all 21 days, the first pair |

Ran with ours (first-call log lines): all 44.

Batches r4b1 and r4b2 (2026-10-14), part r4: before the first run `ret N` of the 22 plain detours against our signatures (no mismatch; the type name thunk checked by hand), the caller register scan (46 call sites; the one flagged site writes CL) and an independent reading of all 23 against the disassembly, the key order, the float chains, the jump table and the leftovers' readers included (no difference in behaviour). The same reading found two flaws in the first version of the test's drive (a RUL0 test line that scanned only half and left a stack address in a rule, and a `length(n)` leg that could read past a real record's floats): fixed, and the drive legs run again (r4b2).

The original against every network group of main and the eighteen nettool groups (B list `nall24.txt`):

| Leg | Saves | Result |
|---|---|---|
| plain (r4b1) | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair |
| every drive, `OPENSC4_TEST_NETTOOL_RUL=3` included (r4b2, the fixed test) | the 12 saves; `saves-networks` | 12 of 12 (Making Money d1095 on the second pair: the original gave two outcomes there, ours one of them); 3 of 3, the first pair (the r4 hashes the same on all 15) |
| `OPENSC4_TEST_CREW=2 OPENSC4_TEST_NETTOOL=3 OPENSC4_TEST_NETTOOL_LIFE=3 OPENSC4_TEST_TUTORIAL_OFF=1`, 20 days (r4b1) | `saves-networks` Rush Hour t0000 | partly: A 3 outcomes in 4 runs, B 2, 1 shared; B's other outcome (the second day-9 `propocc` value with the first day-15 `citylot` value) is identical on all 21 days to an original run of the same leg in batch r1b1 (`net-t3-r1b1-crew`, try 3, A side) |

Ran with ours (first-call log lines): all 23.

Batch r5b1 (2026-10-14), part r5: before the first run `ret N` of the 25 detours against our signatures (no mismatch), the caller register scan (54 call sites, none flagged) and an independent reading of all 25 against the disassembly, the bridge line's property slots and x87 helpers included (no difference in behaviour). The same reading decoded the 47 shipped bridge exemplars (all in SimCity_1.dat, group 0xA82CA30F, none overridden): every one has the six properties whose fields are read without a gate (0x482CB441, 0x482CB445, 0x482CB446, 0x482CB44D, 0x482CB44F, 0x482CB450), so ours and the original store the same values there. (The network ini names one section "MonoRailBridgeExemplars" where the exe asks for "MonorailBridgeExemplars"; ours asks the same.)

The original against every network group of main and the twenty nettool groups (B list `nall25.txt`):

| Leg | Saves | Result |
|---|---|---|
| plain | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair |
| every drive, `OPENSC4_TEST_NETTOOL_RUL=3` included | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair (the ` y` bridge hash the same in all 30 runs) |
| `OPENSC4_TEST_CREW=2 OPENSC4_TEST_NETTOOL=3 OPENSC4_TEST_NETTOOL_LIFE=3 OPENSC4_TEST_TUTORIAL_OFF=1`, 20 days | `saves-networks` Rush Hour t0000 | partly: A 2 outcomes in 4 runs, B 3, 2 shared; B's third outcome is identical on all 21 days to original runs of the same leg in five earlier batches (b1, s1b1, r2b1, r3b1, r4b1: the original's most frequent outcome) |

Ran with ours (first-call log lines): all 25.

Batches t4b1 and t4b2 (2026-10-14), part t4: before the first run `ret N` of the 20 detours against our signatures (no mismatch), the caller register scan (32 call sites, none flagged; the slot callers in the tool's range too) and an independent reading of all 20 against the disassembly, slot 80's compare tree, slot 60's tail jump, the collector's query and the guarded 0x0062CD50's use of it included (no difference in behaviour; the drawing split confirmed). In t4b1 the drive's hash differed on Big City n0000 and n0090 (each side the same in four runs): the 81 highlighted occupants and every value read of them were the same on both sides; the test had hashed what GetBoundingCityCells (slot 0x2C) leaves in AL, which is no answer for these occupants (0x33 in A, 0x35 in B: what the caller's registers held). The test now hashes the rectangle only, and the drive legs ran again (t4b2).

The original against every network group of main and the twenty-three nettool groups (B list `nall26.txt`):

| Leg | Saves | Result |
|---|---|---|
| plain (t4b1) | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair |
| every drive, `OPENSC4_TEST_NETTOOL_PREVIEW=1` included (t4b2, the fixed test) | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair |
| `OPENSC4_TEST_CREW=2 OPENSC4_TEST_NETTOOL=3 OPENSC4_TEST_NETTOOL_LIFE=3 OPENSC4_TEST_TUTORIAL_OFF=1`, 20 days (t4b1) | `saves-networks` Rush Hour t0000 | the same on all 21 days, the first pair |

Ran with ours (first-call log lines): 18 of the 20 (slot 60 in the crew leg's placements); not the check vector's copy and fill 0x00621580 and 0x00621550 (a check vector grows by more than one at once only when a match collects several, which no run met).

Batch t8b1 (2026-10-14), part t8: before the first run `ret N` of the 25 detours against our signatures (no mismatch), the caller register scan (35 call sites; the two flagged sites, in the guarded 0x00634D70 after PatchTilePair, write CL before testing it as a flag and reload ECX before any full use) and an independent reading of all 25 against the disassembly, the guarded callers' code after each call, the RUL2 condition and the leftovers' readers included (no difference in behaviour). The same reading suggested the drive refuse a tool whose intersections, major bridges, +0xE4 or rule cell records are not empty (with intersections the collector would look rules up in the shared RUL0 store): added, and the drive checked again on Big City n0000 (the same hash on both sides).

The original against every network group of main and the twenty-six nettool groups (B list `nall27.txt`):

| Leg | Saves | Result |
|---|---|---|
| plain | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair |
| every drive, `OPENSC4_TEST_NETTOOL_TILES=1` included | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair |
| `OPENSC4_TEST_CREW=2 OPENSC4_TEST_NETTOOL=3 OPENSC4_TEST_NETTOOL_LIFE=3 OPENSC4_TEST_TUTORIAL_OFF=1`, 20 days | `saves-networks` Rush Hour t0000 | the same on all 21 days, the first pair |

Ran with ours (first-call log lines): 21 of the 25; not the diagonal run marking 0x006335E0 and the helpers only it and the kind 2 fix-up reach (0x00623C90, 0x00621070, 0x006335B0): no drag in these cities crosses a diagonal piece where it matters.

Batch t9b1 (2026-10-14), part t9: before the first run `ret N` of the 16 plain detours against our signatures (no mismatch; the three thunks checked exit by exit, against the built object too), the caller register scan (25 call sites, none flagged) and an independent reading of all 19 against the disassembly, every x87 sequence, the thunks and the guarded 0x00636690's code after its calls included (no difference in behaviour).

The original against every network group of main and the twenty-eight nettool groups (B list `nall28.txt`):

| Leg | Saves | Result |
|---|---|---|
| plain | the 12 saves; `saves-networks` | 11 of 12 and one failed run (below); 3 of 3, the first pair |
| every drive, `OPENSC4_TEST_NETTOOL_HEIGHTS=1` included | the 12 saves; `saves-networks` | 12 of 12 (Rush Hour d0365 on the second pair: the original gave two outcomes there, ours one of them); 3 of 3, the first pair |
| `OPENSC4_TEST_CREW=2 OPENSC4_TEST_NETTOOL=3 OPENSC4_TEST_NETTOOL_LIFE=3 OPENSC4_TEST_TUTORIAL_OFF=1`, 20 days | `saves-networks` Rush Hour t0000 | partly: A 3 outcomes in 4 runs, B 3, 2 shared; B's third outcome is the second day-9 `propocc` value with the first day-15 `citylot` value, the combination an original run of batch r1b1 showed (whole lines can no longer be compared across batches: other threads' merges added fingerprint parts since) |

The failed run: on Making Money d0090 (plain leg) the B run left the game at city load, reading address 0 in part r2's rule file Load (a first token that was null); the A run saved. Six more pairs on that save were all the same. A reading of the whole reader path (Construct, Open, ReadLine, NextToken, Load, the Transmogrify that ran just before) against the disassembly found no difference and no read of memory ours leaves unwritten: a null first token needs the first ReadLine to get nothing from the record (a closed record, a position at or past its length, or a failed first read), which is the game's resource layer on both sides, and the original's Load faults the same way on it (0x00623079). One such run in about a hundred B runs, none in about as many A runs, does not show it is ours. The minidump of that run (opensc4-crash-20261014-172923-49108.dmp) settles it: the record was closed. The reader (on RuleFileLoad's stack) had opened its view (+0x8 = 1) and the first ReadLine found it closed; the view's record was a pool slot that by then held another record (a closed in-memory LTEXT record, key 0x2026960B-0x0A5BCDA5-0xFEC82C82, no buffer), while the packed file segment's map of open records still had row 3's first rule file (instance 0xA) pointing at that slot: the game's stale record corruption with a slot reused by another record (docs/crashes.md, the variant resources.stale_record_fix does not catch; it logged nothing in that run). None of our code was below the resource calls, and our reader opens, releases and closes as the game's does (one Release, 0x00621C90, 0x0062311A, 0x0062322C); the rule files open and close one after another and reuse the same slot, as in the game. The A run of the same batch's crew leg logged two stale record drops at the same pool slots. So the failure is the game's resource layer, not part r2; the original's Load faults on it the same way.

Ran with ours (first-call log lines): all 19 (slot 57 in the crew leg's placements).

Batch t7b1 (2026-10-14), part t7: before the first run `ret N` of the 23 detours against our signatures (no mismatch), the caller register scan (34 call sites; the two flagged sites write DL) and an independent reading of all 23 against the disassembly, slot 72's jump table decoded from the exe, the subtraction and division opcodes and the cost's integer conversion included (no difference in behaviour).

The original against every network group of main and the thirty-three nettool groups (B list `nall29.txt`):

| Leg | Saves | Result |
|---|---|---|
| plain | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair |
| every drive, `OPENSC4_TEST_NETTOOL_COST=1` included | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair |
| `OPENSC4_TEST_CREW=2 OPENSC4_TEST_NETTOOL=3 OPENSC4_TEST_NETTOOL_LIFE=3 OPENSC4_TEST_TUTORIAL_OFF=1`, 20 days | `saves-networks` Rush Hour t0000 | the same on all 21 days, the first pair |

Ran with ours (first-call log lines): 14 of the 23 (slots 54, 55 and 72, the cell clearing, PlaceTunnels and AddEdgeConnection in the crew's placements); not the tunnel pieces, the intersections' occupants, the pre-built set-up and the bridge cell vector: no crew drag in these cities needs a tunnel, an intersection rule, a pre-built piece or a bridge.

Batches t6b1 and t6b2 (2026-10-14), part t6: before the first run `ret N` of the 34 detours against our signatures (no mismatch), the caller register scan (61 call sites; the one flagged site writes DL) and an independent reading of all 34 against the disassembly, the crew table, the segment placer's order, slot 8's floats, the tunnels' ground and the FPTAN helper included (no difference in behaviour; the reading found one return value ours did not give: MarkInvalidSteps leaves the step count in EAX, which SetValidStepCount, slot 24, hands on; ours now returns it). In t6b1 every save of the drive legs differed, only in the drive's FitsInCity hash: it asked FitsInCity of every RUL0 rule, and one rule has no cell grid, whose bounds are the original's stack leftovers (ours zero, part r1; the game reads them only behind a grid test). The drive now asks only rules with a cell grid; t6b2 ran every leg again on the fixed build.

The original against every network group of main and the thirty-eight nettool groups (B list `nall30.txt`), t6b2:

| Leg | Saves | Result |
|---|---|---|
| plain | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair |
| every drive, `OPENSC4_TEST_NETTOOL_PLACE=1` included | the 12 saves; `saves-networks` | 12 of 12; 3 of 3, the first pair |
| `OPENSC4_TEST_CREW=2 OPENSC4_TEST_NETTOOL=3 OPENSC4_TEST_NETTOOL_LIFE=3 OPENSC4_TEST_TUTORIAL_OFF=1`, 20 days | `saves-networks` Rush Hour t0000 | the same on all 21 days, the first pair |

In t6b1 (before the fix) plain was 11 of 12 the same with one failed run on the original's side (Rush Hour d0000: the original left the game silently at load), and the crew leg the same.

Ran with ours (first-call log lines): 24 of the 34 (PlaceAllSegments, the segment placer and their passes in the crew's drags; FitsInCity in the drive); not slots 7 and 8 (the network manager's lot connections, which these runs do not make), the tunnel end, the neighbour connections, the intersections' cell marking and the intersection vector's growth: no crew drag here meets a tunnel, an intersection or a neighbour city's network.
