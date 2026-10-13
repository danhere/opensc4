# The intermediate class of the underground pieces

The class between the network occupant ([netocc.md](netocc.md)) and the two classes whose pieces are drawn in the underground view: the pipe occupant ([pipeocc.md](pipeocc.md)) and the subway occupant. It adds what such a piece needs to be drawn as a trench cut into the ground: the cell it lies in, the heights of the ground over its four corners, the depth of its floor, which of its sides are open, the corner records of its side walls and floor, a matrix and its textures. The game has no name for it in its ID table; the address names use `SC4PipeBase::` and the files `pipebase`. Build 641 addresses throughout. Reimplemented like the other occupant classes: from the disassembly, in two halves by two writers (`src/core/pipebase_1.cpp`: the small slots, the geometry state and Write; `src/core/pipebase_2.cpp`: the class's life, Read and the walls), tested through the parity harness ([parity.md](parity.md)).

## The class

No class ID and no factory: the class is abstract (slots 101, 102 and 105 of its own vtable and GetGZCLSID are the pure-call stub 0x005D4A10) and only the pipe's and the subway's constructors call its constructor 0x0065C330, with 0. It answers the IIDs 0x4A10AFC2 (its own), 0xA821EF94 (the network occupant's) and 0xA9764C8C with the object itself; every other ID goes to the occupant base and then to the model part. The occupant type is the network occupant's, 0xC772BF98. The word at +0x264 is the derived class's ID (0x49C05B9F a pipe, 0xCA16374F a subway piece): Init is given it, slot 82 answers it, and the searches for neighbours use it as an interface ID.

0x278 bytes of its own (a pipe is 0x280, a subway piece 0x2D8). When the class is the most derived (it never is) its virtual base table is 0x00AAAEE8 (-4, then 0x238), the virtual base is at +0x274 and its objects would go back to the pool at 0x00B0D008 (64 KB pages). Code 0x00659B00 to 0x0065E800, and 0x00648460 and 0x00648470, which lie in the pipe's range but are in all three vtables.

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AAAD40 | the network occupant interface, 106 slots (81 of the network occupant's, then 25 of this class) | 0 QueryInterface, 4 Shutdown, 10 and 11 the flag setters, 21 IsPlaced, 30 SetPiece, 33 GetModelInstances, 78, 80 the box, 81 Init, 82 the class ID, 84 to 90, 92 to 100, 103 the tint, 104 the textures; 101 (the descriptor), 102 (the network type: 4 a pipe, 7 a subway) and 105 (the default flags) are the derived classes' |
| +0x4 | 0x00AAACB8 | `cISC4Occupant` | 3 OccupantInit 0x0065C070, 8 GetPosition 0x0065A720, 10 GetBoundingBox 0x00659B00, 15 PostOccupantMessage 0x0065A980 |
| +0x8 | 0x00AAAC9C | `cIGZSerializable` | Write 0x0065B140, Read 0x0065E510; GetGZCLSID is pure |
| +0xC, +0x20, +0x38 | 0x00AAAC38, 0x00AAAC20, 0x00AAAC1C | the property and exemplar holders, the model part's thunk | adjustor thunks |
| +0x274 | 0x00AAABD0 | the virtual base, 19 slots, all thunks | 12 the draw 0x0065B290, 17 the piece key 0x0065A0E0 |

The fields, beyond the network occupant's:

| Offset | Field | Set by | Saved |
| --- | --- | --- | --- |
| +0x50, +0x51, +0x60 | two bytes and the float of the model part's state (the network occupant's field; the constructor zeroes all 0x14 bytes again) | Read | yes |
| +0x178, +0x1B0 | texture bindings (Init loads them by the descriptor's keys at +0x78 and +0x8C, type 0x7AB50E44, group 0x891B0E1A) | Init, slot 87; released by Shutdown | no |
| +0x17C | the number of side walls in use | slot 89, Read | yes |
| +0x180, +0x18C, +0x198, +0x1A4 | four vectors of 0x18-byte corner records, one per side wall (four records each) | slot 89, Read | yes |
| +0x1B4 | the floor's vector: the four corners with white bytes | slot 90, Read | yes |
| +0x1C0 | a sixth vector (empty in every corpus save) | Read | yes |
| +0x1CC to +0x1E0 | six texture bindings, three pairs: slot 104 picks a pair by the network flags 0x20000 and 0x10000 | slot 87; released by Shutdown and slot 87 | no |
| +0x1E4, +0x1F0 | two vectors (the draw's) | slot 87 through 0x0065D160 | no |
| +0x1FC | a 4 by 4 float matrix | the constructor and Init (identity), slot 85, Read | yes |
| +0x23C, +0x240 | the piece's cell (Init asks the city, slot 0x17C, with the occupant's position) | Init, Read | yes (signed) |
| +0x244 | a referenced object (the draw reads a model from it) | slot 87; released by Shutdown and slot 87 | no |
| +0x248 to +0x24A | three bytes: +0x248 picks the corners of the piece's own wall, +0x249 and +0x24A differ when the piece has one | slot 88, Read | yes (as bools) |
| +0x24C to +0x258 | four floats: the height of the ground over each corner | Init, slot 95, Read | yes |
| +0x25C | a float, the floor depth (slot 85 stores it as the transform's and the matrix's height) | 0x0065AB50, Read | yes |
| +0x260 | a byte: dirty | the refresh and the flag setters set it, the draw clears it | no |
| +0x261 | a byte: rebuild wanted | the refresh and Read set it; the draw rebuilds (slot 87) and clears it | no |
| +0x262 | a byte | slot 87 sets it, Read clears it | no |
| +0x264 | the derived class's ID | Init, Read | yes |
| +0x268 | a word (zero in every corpus save) | Read | yes |
| +0x26C | the descriptor (slot 101's answer) | Init, the occupant part's Init | no |

The object has eight vectors: the six that are saved and the two at +0x1E4 and +0x1F0. The constructor empties all eight and the destructor frees all eight. A corner record is the network occupant's corner: x, y, z, four bytes, two floats.

Three statics at 0x00B0CFD0, which slot 97 sets, steer the floor depth: the depth of the floor under the ground (3.0), a byte that says the floor is measured from the ground (1) and not from the corners, and the rise of the floor over the corners used otherwise (10.0). After them, at 0x00B0CFE8, the cell steps over the four edges: (-1, 0), (0, -1), (1, 0), (0, 1). The piece keys (+0xFC) the class tells apart: 0x4B00 and 0x300 the flat pieces, 0x200, 0x800, 0xA00 and 0x1400 slopes (0xC00, 0x1E00 and 0x14E00 are treated as slopes by one function each). In the three tutorial cities nearly every piece is flat: 459 of Big City's 483 pipes are 0x4B00 and 13 are 0x300; Rush Hour has 100 pipes and 16 subway pieces (14 and 2).

## What each function does

First half (`src/core/pipebase_1.cpp`):

| Function | Address | Ours | What |
| --- | --- | --- | --- |
| QueryInterface | 0x0065A080 | `slots` | slot 0, ret 8: the three IIDs give the object with one reference; the rest goes to the occupant base (0x005E89C0 on the object + 4) and, when that says no, to the model part (0x00801940 on the object + 0x68) |
| GetClassId | 0x00648460 | `slots` | slot 82: the word at +0x264 |
| IsPlaced | 0x00648470 | `slots` | slot 21: bit 0 of the occupant's flags, and the object at +0x244 or the descriptor there |
| GetPieceKey | 0x0065A0E0 | `slots` | the virtual base's slot 17: the word at +0xFC when any of the flags 0x06000000 is set, else the IID; never reached (the pipe and the subway have their own) |
| SetNetworkFlag, ClearNetworkFlag | 0x0065A900, 0x0065A940 | `slots` | slots 10 and 11: the network occupant's setters, and the dirty byte set when the flag bits 0x30000 changed |
| GetSides | 0x0065A6F0 | `slots` | slot 92: the three bytes at +0x248 |
| GetSurfaceHeights | 0x00659DD0 | `slots` | slot 94: the four heights of the ground |
| GetShape | 0x0065A810 | `slots` | slot 96: the piece key, the word at +0x268, the four corner heights, the floor depth |
| SetDepthStatics | 0x00659DB0 | `slots` | slot 97: the three statics |
| GetTint | 0x0065B020 | `slots` | slot 103: three floats of 1.0 (the subway's; the pipe has its own) |
| GetEdgeDepth | 0x0065AA70 | `slots` | slot 93 (a float out, a byte out, the edge): the floor depth, and for a flat piece the mean of the two corner heights at the ends of the edge plus the rise; the byte is 1 for a slope |
| 0x0065A640 | 0x0065A640 | `slots` | called by slot 89 with an edge: false for a piece without bit 31 of its network flags; else whether the piece over that edge lacks the bit |
| CountRun | 0x0065A120 | `slots` | slot 98 (a piece, a direction, how many more): the flat pieces in a row from that piece, 0 when it is not flat or its cell holds a piece of this type |
| GetSideRuns | 0x0065A200 | `slots` | slot 99 (a byte out): for a subway's flat piece the runs from the two edges across its direction: 2 when both are 2 or more; 1 and the byte when one is exactly 1 and the other 3 or more; else 0 |
| PostOccupantMessage | 0x0065A980 | `slots` | the occupant part's slot 15: the base's post, then for the occupant manager's messages 0x99EF1142 and 0x99EF1143 a second queued message, 0x8B87A974 or 0x8B87A975 |
| InitStatics | 0x0065B040 | `slots` | once (the byte at 0x00B46870): the render states (0x0065AE80), then the network occupant's statics when not done |
| UpdateSides | 0x0065A3F0 | `state` | slot 88: the three bytes at +0x248 from the piece key, the rotation and the flip, then the network flags 0x01000000 (as the first byte), 0x02000000 and 0x04000000 (as the opposite of the other two) |
| GetPosition | 0x0065A720 | `state` | the occupant part's slot 8: x and z the mean of the four corners, y the lowest height of the ground over them |
| GetBoundingBox, ComputeBoundingBox | 0x00659B00, 0x00659E00 | `state` | the occupant part's slot 10 and the interface's slot 80: the position twice, widened by the corners, the high y by the heights of the ground: from the floor to the ground |
| 0x0065AB50 | 0x0065AB50 | `state` | the floor depth: for a slope the lower of the mean of two heights and a third (which, by the rotation and flip), for a flat piece the position's y less the depth static; with the from-the-ground static clear the corners' own heights plus the rise |
| SetPiece | 0x0065ADB0 | `state` | slot 30: the piece key and the rotation and flip byte, then the floor depth |
| SetHeights | 0x0065AE30 | `state` | slot 84: the four corner heights, the floor depth, the side walls and the floor (slots 89 and 90 with 1) |
| SetSurfaceHeights | 0x0065B6B0 | `state` | slot 95: the four heights of the ground, the box into +0x124, the floor depth |
| Rebuild | 0x0065ADD0 | `state` | slot 86: the floor depth, the matrix (slot 85), slot 87, the side walls and the floor |
| Refresh | 0x0065AF10 | `state` | slot 100 (a byte: tell the neighbours): outside the city the dirty and rebuild bytes are cleared; a dirty piece is left alone; else both bytes set, the box into +0x124, the view told (0x00B43DD0, slot 0x7C) when the occupant's flag 2 is set, and with the byte the refresh of each connected neighbour |
| Write | 0x0065B140 | `state` | version 3, the network occupant's record, then the fields of the record below |
| GetModelInstances | 0x00659D70 | no | slot 33, see below |
| GetTextures | 0x0065A880 | no | slot 104, see below |
| 0x0065AE80, the draw 0x0065B290 | | no | drawing, see below |
| 0x0065B070 | | no | the corner vector writer, see below |

Second half (`src/core/pipebase_2.cpp`):

| Function | Address | Ours | What |
| --- | --- | --- | --- |
| Constructor | 0x0065C330 | `object` | ret 4 (non-zero for the most derived object): the network occupant's constructor with 0, the vtables, the fields from +0x178 to +0x1F8 zero, the matrix filled with the float at 0x00B46874 (zero) and then the identity, the cell, +0x244, the bytes and the words at +0x264 and +0x268 zero, the model part's state zero, the eight vectors emptied |
| Destructor | 0x0065C0C0 | `object` | the vtables, the memory of the eight vectors freed, the network occupant's destructor |
| Deleting destructor | 0x0065C650 | `object` | the destructor and with flag 1 the item back to the pool at 0x00B0D008; reached only through the thunks of the class's own vtables (0x0065C1C0, 0x0065C1E3), which no live object has: the game never runs it |
| Shutdown | 0x0065CD90 | `object` | slot 4: the eight texture bindings released (0x008090E0) and zeroed, the eight vectors emptied, the object at +0x244 released, the occupant base's Shutdown |
| OccupantInit | 0x0065C070 | `init` | the occupant part's Init: the statics once (the byte at 0x00B46870, 0x0065AE80, the network occupant's statics); an object not initialised goes through the network occupant's occupant Init and stores slot 101's answer at +0x26C |
| Read | 0x0065E510 | `init` | version 3 only; the network occupant's record, then the fields in the order of the record below; +0x261 set to 1 and +0x262 to 0 |
| AddSideWall | 0x0065CFF0 | `walls` | ret 0xC (corner from, corner to, vector): four records: `from` at the height of the ground over it with (0, 0), `from` as it is with (0, d), `to` as it is with (1, d), `to` at its ground height with (1, 0); d is (the ground height of `from` less its own) / 16; the first three bytes of each are 0xFF |
| SetSideWalls | 0x0065E260 | `walls` | slot 89, ret 4 (refresh after): the four wall vectors emptied, slot 88, the count zero; when 0x006585E0 finds no piece of this type (slot 82) in the piece's own cell and the bytes at +0x249 and +0x24A differ, one wall between the corners 1 and 3 (+0x248 set) or 0 and 2, and two edges struck out; then a wall for every other edge (corners 1-0, 0-3, 3-2, 2-1) unless the piece connects over it (slot 47 with slot 102's network type) and 0x0065A640 says no; the refresh (slot 100) with 1 when asked |
| SetFloor | 0x0065E410 | `walls` | slot 90, ret 4 (refresh after): the vector at +0x1B4 emptied and the four corners pushed with their first three bytes 0xFF; the refresh with 1 when asked |
| Init | 0x0065C6B0 | no | slot 81, ret 0xC (description, class ID, descriptor), 1,737 bytes: see below |
| slot 78 | 0x0065CF20 | no | ret 4: reloads the textures of a shared table, see below |
| slot 85 | 0x0065B700 | no | the transform and the matrix, 2,414 bytes, see below |
| 0x0065D160 | 0x0065D160 | no | fills one of the two unsaved vectors for slot 87, see below |
| slot 87 | 0x0065D810 | no | ret 0xC, 2,640 bytes: reloads the piece's textures and rebuilds the unsaved vectors, see below |

Two searches of a cell's occupants lie just before the class's range and are the game's: 0x006584D0 (cell x, cell z, out, interface ID: the last occupant of the cell that answers the interface, referenced) and 0x006585E0 (cell x, cell z, type, out: an occupant that answers the interface 0x8A139184 and whose slot 0x98 gives the type).

## The save record

The class's part of a pipe's or a subway piece's record (`tools/re/save_schemas/pipeocc.py` decodes it inside the pipe's; Write and Read agree with it field for field, nothing in the schema had to change). After the u16 version (3; Read answers false for another without reading more) and the network occupant's record (Write 0x0061A190, Read 0x0061CC00; when it fails the rest is skipped and the answer is the stream's state):

| Field | Object offset | Write's call | Read's call |
| --- | --- | --- | --- |
| two bytes | +0x50, +0x51 | SetUint8 | GetUint8 |
| float | +0x60 | SetFloat32 | GetFloat32 |
| uint32 | +0x17C | SetUint32 | GetUint32 |
| six vectors: a uint32 count, then per record x, y, z, the floats at +0x10 and +0x14, the bytes at +0xE, +0xD, +0xC, +0xF | +0x180, +0x18C, +0x198, +0x1A4, +0x1B4, +0x1C0 | the writer 0x0065B070 (vector in EBX, stream in ESI) | the reader 0x006574A0 (the same registers) |
| sixteen floats | +0x1FC | 0x005DA6E0 | 0x005DA6B0 |
| two signed words | +0x23C, +0x240 | SetSint32 | GetSint32, each into a local and then copied |
| three bools | +0x248 to +0x24A | 0x005D9FD0 | 0x005D9FA0 |
| four floats, a float | +0x24C, +0x25C | SetFloat32 | GetFloat32 |
| two uint32 | +0x264, +0x268 | SetUint32 | GetUint32 |

The two register-called vector helpers are written out in ours (Read's resizes with 0x005E1B30 as the game does). In Read the version lands in the argument's stack slot and the two signed words in the slot of the game's `push ecx`: with a stream already in error they keep the stream pointer and the part's address, and ours start from the same values. The vector reader's count is in a slot nothing sets first; ours starts from 0.

All pipes of the parity corpora decode with the pipe's schema (27,545 in 116 saves; [pipeocc.md](pipeocc.md)). The four Rush Hour saves of `parity/saves` hold 16 subway pieces each.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.pipebase.slots` | 17 detours: QueryInterface, the class ID, IsPlaced, the piece key, the two flag setters, the sides, the heights of the ground, the shape, the depth statics, the tint, the edge depth, 0x0065A640, the runs (slots 98 and 99), the occupant message, the statics |
| `simulators.pipebase.state` | 11 detours: slot 88, the position, the two boxes, the floor depth, SetPiece, SetHeights, SetSurfaceHeights, the rebuild, the refresh, Write |
| `simulators.pipebase.object` | 4 detours: the constructor, the destructor, the deleting destructor, Shutdown (slot 4) |
| `simulators.pipebase.init` | 2 detours: the occupant part's Init and Read |
| `simulators.pipebase.walls` | 3 detours: slots 89 and 90 and the wall helper 0x0065CFF0 |
| `simulators.pipebase.own` | no detour: the objects carry our copies of the class's seven vtables (only with `simulators.pipebase.object`; see below) |

`OPENSC4_PIPEBASE_SKIP` takes a list of function addresses (hex, eight digits) to leave out of a group, for bisecting. Functions of the class call each other at the game's addresses or through the vtable (the floor depth, the wall helper, the destructor), so each can be left out alone.

The `walls` group is state by this reading: the vectors it fills are in the save record (the four walls, the floor) and nothing in the three functions touches a texture, a model or a vertex buffer. The records are, all the same, what the underground view draws from (position, four bytes, two texture coordinates). The same holds for the refresh (slot 100): it keeps the dirty bytes the draw reads and tells the view, but builds nothing. If the Renderer thread claims either, the `walls` group can be dropped whole and the refresh left out by its address; nothing else depends on them.

Where the game reads memory it has not set, ours takes a fixed value: the floor depth of a slope whose rotation and flip byte is none of 0 to 3 and 0x80 to 0x83 (the game takes three corner numbers from a stack slot nothing has set; ours takes corner 0), and the edge depth for an edge above 3 (the game reads past a table on its stack; ours stays inside the table). Neither happens with the pieces the game makes.

### Our own object

`simulators.pipebase.own` makes the objects carry our copies of the class's seven vtables instead of the game's: the interface's (0x00AAAD40, 106 slots), the occupant part's (0x00AAACB8, 34 slots), the cIGZSerializable part's (0x00AAAC9C, 6 slots), the property holder's (0x00AAAC38, 25 slots), the exemplar holder part's (0x00AAAC20, 5 slots), the model part's (0x00AAAC1C, 1 slot) and the virtual base's (0x00AAABD0, 19 slots), 196 slots in all. The counts are from the executable: each table ends where the next one the constructor stores begins, or at the virtual base table, padding or other data, and nothing refers to an address inside one. The virtual base table (0x00AAAEE8) is data and stays the game's. The copies are taken once, the first time the constructor or the destructor wants them, so after every replacement is installed; each has the word before the game's table too. Every entry is what the game's table holds, a function's address or an adjustor thunk, so the other groups' detours decide what runs exactly as before. An entry that holds a function of the `object` group directly is pointed at ours, so that slot is ours without the detour: one here, Shutdown in slot 4 of the interface's table. The deleting destructor is in no table directly (thunks reach it). The group installs nothing: it only has an effect together with `simulators.pipebase.object`, whose constructor and destructor store the tables, and it does not turn that group on. The log has one line when the copies are taken (`pipebase own: vtables copied ...`, with the table sizes and the number of redirected entries). No object keeps these tables: the class is abstract, so a pipe or a subway piece has them only while this class's constructor or destructor runs. What was checked in the game for all eight classes is in [netocc.md](netocc.md), "Our own object".

## What is left, and why

- **GetModelInstances, 0x00659D70** (slot 33, ret 0x14): stores 1 through its fifth argument and the object's virtual base (the model instance) through its third, then AddRef. Left with the renderer, as the network occupant's own slot 33 was ([netocc.md](netocc.md)).
- **GetTextures, 0x0065A880** (slot 104, ret 8): hands out two of the six texture bindings (+0x1DC with the network flag 0x20000, else +0x1D4 with 0x10000, else +0x1CC). Its only caller is the draw (0x0065B4D7): drawing.
- **The draw, 0x0065B290** (the virtual base's slot 12, 1,050 bytes): rebuilds the piece through slot 87 when the rebuild byte is set, then sets render states and textures and draws the walls, the floor and the model through the render context (0x007D2D20, 0x007D4530, 0x007D4070, 0x007D2990). **0x0065AE80**: three render states (0x007D2B50) at 0x00B46878, 0x00B46880 and 0x00B46888. Drawing.
- **0x0065B070**: the writer of a corner vector takes the vector in EBX and the stream in ESI, so a plain detour cannot stand in for it; two other classes' Writes call it (0x0064B4D0, 0x00654C81). Ours Write has its own copy.
- **0x00657730** (slot 83, a bare `ret 0xC`): nine vtables of several classes share it. **0x0065A860, 0x0065A870** (slots 1 and 2): jumps to the occupant base's AddRef and Release thunks. **0x0065A6C0** (slot 34, GetVertexHeights): the network occupant's function, ours there.
- **Init, 0x0065C6B0** (slot 81, ret 0xC, 1,737 bytes): state and drawing mixed. It stores the class ID (+0x264) and the descriptor (+0x26C), the four given heights (+0x24C), runs the network occupant's Init (0x00619050), sets the corners' two floats ((0,0), (0,1), (1,1), (1,0)), asks the city (0x00B43CEC, slot 0x17C) for the cell, sets the matrix to the identity and empties the vectors; but in the middle it gets the service 0x1AC0E11A / 0xFAC0E219 from the framework and loads two textures through its slot 0x18 (key type 0x7AB50E44, group 0x891B0E1A, instances from the descriptor's +0x8C and +0x78) into +0x1B0 and +0x178, and at the end it writes two tables of the class's shared descriptor (+0x48 and +0x5C: ratios of distances between the descriptor's points, in a loop unrolled by four). Left for the texture loading (and its length).
- **Slot 78, 0x0065CF20** (ret 4): does not read `this`. It stores the argument's +0x14 at 0x00B0B374 and walks the hash table at 0x00B45278 (0x004B0390), releasing each entry's texture binding (0x008090E0) and loading it again through the object at 0x00B45258 (slot 0x18) with the key type 0x7AB50E44, group 0x1ABE787D and the entry's key plus that value. Texture reloading: drawing.
- **Slot 87, 0x0065D810** (ret 0xC, 2,640 bytes): empties the two unsaved vectors, releases the six texture bindings at +0x1CC to +0x1E0 and the object at +0x244, loads textures again through the service's slot 0x18 with keys from the descriptor (many times, by case), fills the unsaved vectors through 0x0065D160, calls slot 85, and at the end releases and reloads +0x1B0 and +0x178 and sets +0x262. Drawing.
- **0x0065D160** (1,704 bytes; called only by slot 87, seven times): empties the vector it is given (+0x1E4 or +0x1F0) and fills it with records made from two neighbour lookups (0x006584D0), slot 93, the float at +0x25C, the matrix at +0x1FC and texture coordinate constants (0x00A8E174, 0x00A8E178), pushed through 0x0082CDF0. The vectors are not in the save record: view geometry, drawing.
- **Slot 85, 0x0065B700** (2,414 bytes): state, no drawing call. It takes the rotation (slot 24) through 0x007F6CB0 as an angle, resets the transform at +0x64 when its byte at +0x65 is set, rotates the transform's matrix by the angle's cosine and sine, sets the matrix at +0x1FC to the identity and multiplies it with the transform (and with a second pattern when the piece is flipped, slot 25), then puts the occupant's position (the occupant part's slot 8) and the float at +0x25C into the matrix and the transform's translation and marks the transform changed (+0x64 counted up, bit 0 of +0x65). Left for its size: about 520 instructions of FPU stack code with the values kept on the stack between stores. It is the one large function of the class that is in scope.
- **0x0065E6A0, 0x0065E710, 0x0065E740, 0x0065E780, 0x0065E7C0** (at the end of the range): not this class's. The first two are called only from the code after it (0x0065EC84 to 0x0065F8DE) and from 0x0062B415, 0x00637B46 and 0x00644B59; the last three are a tree's erase, clear and destructor with callers elsewhere.
- The adjustor thunks: 0x0065C190 to 0x0065C320 (QueryInterface, AddRef, Release, the deleting destructor, the virtual base's 19 slots, the save thunks 0x0065C310 and 0x0065E690), 0x0065CFE0 (the occupant part's Shutdown).

## Test

The fingerprint's `pipebase` part (order 129): the number of underground pieces (through the occupant manager's grid: occupant type 0xC772BF98 and a class ID of 0x49C05B9F or 0xCA16374F) and the sum and XOR of a hash per piece, through the object's vtables: the class ID (slot 82 and GetGZCLSID), IsPlaced, the occupant type, the sides (slot 92), the heights of the ground (slot 94), the shape (slot 96), the depth of each edge (slot 93), the position and the box (the occupant part's slots 8 and 10, and slot 80 into the test's own floats, with whether it is the box kept at +0x124), the tint (slot 103), whether the stored descriptor is slot 101's answer, the class's own piece key (called at its address with the object + 0x274), for each edge 0x0065A640's answer and the run of flat pieces (slot 98 with one more to follow), the side runs (slot 99), the network flags the class sets or reads, and the saved fields; for the pieces of every fifth cell QueryInterface through the interface part and the occupant part for eight IDs (the reference given released). Then `geo`, the sum of a checksum of the saved geometry (the wall count, the six saved vectors, the matrix), kept apart so that a difference says where it is; counters (pipes, subway pieces, placed, flat, edges of slopes, side walls in use, edges whose neighbour lacks the top flag, runs longer than one, side runs, boxes that are the kept box, keys) and `st`, a checksum of the depth statics and the statics byte. Not mixed: the dirty bytes at +0x260 to +0x262, the texture bindings, the object at +0x244, the two unsaved vectors, pointers, reference counts.

`OPENSC4_TEST_PIPEBASE=<day>` adds ` set <checksum>,<count>,<changed>,<same vectors>,<once>`. The pieces of every seventh cell (chosen by the cell, in the order of the cells) go through the setters, each read back: the flag setters with the texture bit 0x10000 both ways and with no bit (the dirty byte cleared before and read after); SetSurfaceHeights with every height two higher and with the old heights (the heights, the floor depth and the kept box after each); the depth statics as they are and as (5, from the corners, 7), and under each SetPiece with eight piece keys and eight rotation and flip bytes, with slot 88 and the four edge depths after every one, and for the flat key the runs (slot 98 with three more) and the side runs with the class ID word set to the subway's for the call; then the old statics and the old piece; then the floor and the side walls made again (slots 90 and 89 with 0), and SetHeights with the heights the corners have, which makes them a third time with the refresh (the dirty bytes cleared before, so the refresh does its work, and read after). The dirty bytes are put back by hand, and the box, the floor depth, the side bytes and the flags when the functions did not bring them back themselves (counted as `changed`); the saved vectors stay as slots 89 and 90 made them, and how many pieces kept theirs is counted. On the first piece once: the occupant message slot with a message nobody listens to, and the class's occupant Init and statics at their addresses (which leave an initialised object and done statics alone). One log line says what ran, also in a city without underground pieces.

How the rest is reached: loading a city with pipes or subway runs Read and the constructor for each piece; saving runs Write (the save tests, [save-format.md](save-format.md)); leaving the city runs Shutdown and the destructor; the pipe's life test (`OPENSC4_TEST_PIPEOCC`) makes and releases a pipe. Not reached by any unattended test: the rebuild (slot 86; it runs slot 87, which loads textures, so the test does not call it; the game runs it when a piece is laid), the second message of the occupant message slot (only for the occupant manager's inserted and removed messages), the refresh's walk over neighbours that are not dirty (loaded pieces are dirty until the underground view has drawn them), the deleting destructor and the class's own piece key in the game itself.

## Parity

Run on 2026-10-13 (batch b1) together with the subway piece ([subwayocc.md](subwayocc.md)) and every network occupant group of the earlier steps ([netocc.md](netocc.md), [pipeocc.md](pipeocc.md), [powerlineocc.md](powerlineocc.md), [powerpoleocc.md](powerpoleocc.md), [prebuiltocc.md](prebuiltocc.md), [bridgeocc.md](bridgeocc.md), [tunnelocc.md](tunnelocc.md)), the original against all of them for a week, with `determinism.dispatch_info` in the base list. Before the first run: `ret N` of all 55 replaced functions of the two classes against our signatures (no mismatch), the caller register scan and the EAX scan (no hit), and an independent reading of each half against its disassembly (all 37 functions agree; three places where ours is not the game's on input the game was not seen to make are left as they are: Read on a record that ends inside a corner vector sizes the vector to 0 where the game uses a stale stack word (the load fails either way); GetEdgeDepth with an edge above 3 wraps where the game reads past its stack table (its two callers pass 0 to 3); ComputeFloorDepth on a slope piece whose rotation byte is outside 0 to 3 and 0x80 to 0x83 uses corner 0 where the game reads an unset local).

Plain: the 12 saves 12 of 12 the same on the first pair, `saves-networks` 3 of 3 (n0000, n0090, t0000), `saves-plopped` 3 of 3, `saves-ports` 3 of 3. With `OPENSC4_TEST_PIPEBASE=3` and `OPENSC4_TEST_SUBWAYOCC=3` (and the three switches of the step before) together: the 12 saves 12 of 12 and `saves-networks` 3 of 3 on the first pair, the parts and their `set` checksums the same on both sides in every save. On `saves-networks/Rush Hour Tutorial/t0000` the part reads `pipebase 116,D1F2F48C,59DEA6C4 geo 17BF01E2 pipe 100 sub 16 placed 116 flat 112 slope 0 wall 238 open 0 run 182 side 12 box 116 key 0 st C96BD426 set 44E0FFA5,17,15,2,6C1151A5`; Big City n0000 has 486 pipes, 69 of them driven.

Ran with ours (first-call log lines over the whole batch): 36 of the 37 replaced functions; the one that did not is the deleting destructor 0x0065C650 (the pipe and the subway piece have their own). Write ran at every save, Read and the constructor at every load.

Our own objects (batch o1, 2026-10-13): the original against every network occupant group and the eight `.own` groups for a week, every class's copies in use in the runs that made or destroyed its objects (the `own: vtables copied` log line). Plain: the 12 saves 12 of 12 the same on the first pair, `saves-networks` 3 of 3, `saves-plopped` 3 of 3, `saves-ports` 3 of 3. With the test switches of all the network occupant classes but `OPENSC4_TEST_NETOCC`: `saves-networks` 3 of 3 and the 12 saves 11 of 12 on the first pair; on Making Money d0365 one run of ours in four gave other `adv` and `advisors` values from the first day on (two days before the tests run; every decoded record the same), and twelve more pairs of that save were all the same, so one run of ours in 16 against none of the original's 16: not explained and not reproduced.
