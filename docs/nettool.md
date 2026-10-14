# The network tool (cSC4NetworkTool)

The tool a player's drag of a network goes through: it turns the dragged cells into solved cells (a piece ID, a rotation and a flip per cell), fits the heights to the terrain, picks bridges and tunnels, asks the player about intersections, previews the result, works out the cost and places the pieces (handing them to a construction crew where the type has one). The network manager keeps one tool per network type and hands them to the network view's input controls, to its own lot and removal code and to the demolition. Nothing of the tool is saved. With it come the RUL engine that loads the networks' rule files at every city load, a vertex height constraint satisfier, a world cache of cell infos, a tool UI object and three tools of their own (pipe, subway, power line). The whole piece is 612 functions (196.5 KB); the tool proper is about 250. It is reimplemented in parts (below); this page has the first, the state and queries (46 functions, 43 of them ours). Build 641 addresses throughout (the address entries of this part come from the spec `p6/nettool1.spec` in the tools folder, with the survey's files).

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

- NAM calls the game at fixed addresses in the piece: 0x00633220 (a cell's entry), 0x00647A20 (the world cache's GetCell), 0x00623E60 (GetFirstNetworkTypeFromFlags, this part), 0x00636000, 0x00636030, 0x00636360, 0x0065FF60. A detour there is fine (NAM then calls ours) if it keeps the function's convention, its arguments and the registers its game callers rely on.
- NAM reads and writes by layout: the tool object (+0x1C, +0x54, +0x60, +0x98, +0xCC, +0x124, +0x188, +0x240, +0x244, +0x2B5), the 0xBC-byte cell info (+0x8 flags, +0xC occupant, +0x18 the words by type, +0x4C edge flags, +0x53, +0x56, +0xA8 vertices, +0xB8 the solved cell's index), the 0xC-byte solved cells, the type table's rows (+0xF0 to +0x110 among others), the RUL0 rule objects (0xFC bytes, edited in place by FlexPieces) and the RUL2 multimap at 0x00B466D0. So every part reads and writes these in the game's memory with the game's layouts, and nothing is cached privately across a call into a guarded function.
- NAM is not installed on the test machine, so parity runs the guarded functions without the stubs: the rule above is a compatibility rule that parity cannot show.

## The parts

The survey's split (`p6/survey.txt`, section 5), in the order planned: state and queries, life, the owned objects, the RUL engine (separate, all at load), then solving and placing.

| Part | Functions | Bytes | Guarded | What |
| --- | --- | --- | --- | --- |
| t1 state and queries | 46 | 1,749 | none | this page |
| t2 cache and cells | 25 | 4,834 | none | the world cache, the cell info lookup and making (NAM calls 0x00647A20 and 0x00633220), slot 71 |
| t3 life and previews | 27 | 5,715 | none | constructor, destructors, Init, Shutdown, Reset, DoMessage, GetUITool, the two Clear slots, the preview drawable (its three drawing functions may stay for the Renderer) |
| s1 satisfier | 46 | 8,850 | 0x0065ED80 | the vertex height constraint satisfier |
| r1 to r5 the RUL engine | 157 | 47,007 | 0x0063D1E0 | the load, the network ini, the rule records and the three parsers |
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
