# The network tool (cSC4NetworkTool)

The tool a player's drag of a network goes through: it turns the dragged cells into solved cells (a piece ID, a rotation and a flip per cell), fits the heights to the terrain, picks bridges and tunnels, asks the player about intersections, previews the result, works out the cost and places the pieces (handing them to a construction crew where the type has one). The network manager keeps one tool per network type and hands them to the network view's input controls, to its own lot and removal code and to the demolition. Nothing of the tool is saved. With it come the RUL engine that loads the networks' rule files at every city load, a vertex height constraint satisfier, a world cache of cell infos, a tool UI object and three tools of their own (pipe, subway, power line). The whole piece is 612 functions (196.5 KB); the tool proper is about 250. It is reimplemented in parts (below); this page has the first, the state and queries (46 functions, 43 of them ours), the second, the world cache and the cell infos (25 functions, 23 of them ours), the third, the tool's life and its preview drawable (27 functions, 23 of them ours), the vertex height constraint satisfier (s1: 46 functions, 45 of them ours), the RUL engine's load, statics and RUL0 store (r1: 29 functions, all ours), and the type rows' rule files (r2: 35 functions, all ours). Build 641 addresses throughout (the address entries of these parts come from the specs `p6/nettool1.spec`, `p6/nettool2.spec`, `t3/nettool3.spec`, `t3/nettool_s1.spec`, `t3/nettool_r1.spec` and `t3/nettool_r2.spec` in the tools folder, with the survey's files).

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
| 65 to 69 | 0x00638680, 0x00638710, 0x006392F0, 0x00639170, 0x00638990 | TestForAutoConnectOccupant(2), IdentifyStartingConditions(1), CheckForReservedTiles(3), CheckForReservedTiles(3), CheckForGoodTerrain(1) | t5 |
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
| r1 to r5 the RUL engine | 157 | 47,007 | 0x0063D1E0 | the load, the network ini, the rule records and the three parsers; r1 (29: the load, the statics, the RUL0 store) [below](#the-rul-engine-part-r1); r2 (35: the rule files) [below](#the-rule-files-part-r2) |
| t4 previews and highlights | 23 | 6,671 | none | AddCellPreview, UpdateOccupantHighlights, DestroyHighlighted, SetQuadTrianglesToDraw |
| t5 drag | 17 | 9,520 | 0x0063AF40, 0x00637EB0 | the drag path builder 0x00639790 and the start, reserved and terrain slots |
| t6 place | 34 | 11,952 | none | PlaceAllSegments, the segment placer, PlaceNetworkOccupants(ById), MarkInvalidSteps 0x00624BF0 |
| t7 occupants | 23 | 11,634 | none | slots 54, 55, 72, tunnels, the cost (slot 20) |
| t8 solve | 26 | 11,195 | 0x00634D70 | the tile solver 0x00635CB0 |
| t9 heights | 20 | 11,290 | 0x00636690 | the height pass and its constraints |
| t10, t11 bridges | 47 | 23,283 | none | (inferred: bridges) |
| t12 puzzle and resolve | 6 | 11,445 | 0x0062CD50, 0x006099A0 | slots 49 and 50 |
| u1 underground | 34 | 8,442 | none | the underground base, the pipe and subway tools |
| w1 power | 43 | 14,679 | none | the power line tool |
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
| ClearCellPreviews (slot 17) | 0x00625F10 | each cell preview's slot 0x44 with two zeros, the tree emptied, then the preview list +0x210 freed with the objects it holds |
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
