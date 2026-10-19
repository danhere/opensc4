# The derived prop classes: the terrain decal, the visual effect, the automata controller, the burnt building and the text decal

Two of the six classes that derive from the prop occupant ([propocc.md](propocc.md)). A terrain decal is a picture laid flat on the ground, with an opacity that can fade away. A visual effect is a prop that is an effect of the effects manager (smoke, a fountain, a flare) at the prop's place. Both are made by the prop manager when a prop type's class says so ([propmgr.md](propmgr.md)); the terrain code makes decals itself (0x0075EDC0, 0x006CC250), and two functions not yet identified make effect props (0x005CBC10, 0x005CEC20). Reimplemented like the other classes: one writer with every virtual call's argument count checked against the callee's own code (91 virtual calls), then an independent audit of all 39 functions against the disassembly, then the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/propkinds.toml). This page covers pieces 1 to 3 of 4 of the prop family: piece 1 the decal and the effect, piece 2 the automata controller prop (cSC4PropOccupantAutomataController) and the burnt building's logic (cSC4PropOccupantBurntBuilding), done the same way (55 virtual calls checked, all 28 functions audited), piece 3 the text decal (cSC4PropOccupantTextDecal, the label; every virtual call checked, all 26 functions audited). The survey below says what is left.

## The family

Six classes derive from the prop. Logic only, without drawing:

| Class | Class ID | Constructor | Logic | Drawing |
| --- | --- | --- | --- | --- |
| Terrain decal | 0x2977AA48 | 0x005EE360 | 18 functions, 2,408 bytes | none |
| Visual effect | 0x2977AA49 | 0x005F0210 | 21 functions, 2,832 bytes | none |
| Automata controller | 0x2A4017A0 | 0x005EFDB0 | 15 functions, 1,670 bytes | none |
| Burnt building | 0xC9EFB016 | 0x005EF810 | 10 functions, 1,295 bytes | the quad builder 0x005EDC60 and about 19 small model-interface functions, 0x005EF9D0 to 0x005EFB10 |
| Sign | 0xAB72FBB3 | 0x005F5510 | 20 functions, 2,985 bytes | 14 functions, 6,280 bytes (the geometry builder 0x005F5FB0 alone is 4,964) |
| Text decal (the label) | 0x8B79C707 | 0x005F8300 | 28 functions, 3,596 bytes (26 the class's, 2 its pool's) | none |

The logic is 113 functions and about 14.8 KB in all. 82 more are eight-byte adjustor thunks, which stay: they jump into the detours. Piece 1 took the first two rows (39 functions: 13 + 5 for the decal, 16 + 5 for the effect). Piece 2 took the next two: the controller's 15 (10 + 5 for the object) and the burnt building's 13. That is 3 more than the table's 10: its QueryInterface (a single jump), the allocator and SetOccupant were counted outside it. Piece 3 took the text decal: 26 functions (21 + 5 for the object). The survey's 28 counted the pool's two page functions, 0x005F7680 (makes a page of 0x57 items) and 0x005F82A0 (frees an item), which are reached only from the pool's vtables 0x00AA54A8 and 0x00AA54E4 and stay the game's like the other pools' page code. Still the game's after piece 3: the sign's logic (20) and all drawing (the burnt building's quad builder and model interface among it).

cSC4PylonOccupant (class 0x49B6D69B, constructor 0x005F1730, code 0x005F0820 to 0x005F2A8D) lies in the same range but derives from the occupant base, not from the prop. It is not part of this family.

## The classes

Both objects have an interface of their own at +0 and hold the whole prop (0x68 bytes, [propocc.md](propocc.md)) at +4. The parts after it lie where the prop's lie, four bytes on: +4 the prop interface (24 slots, the prop's), +8 the message target, +0xC the occupant (the base starts here; 34 slots), +0x10 the serializable, +0x14 the property holder, +0x28 the exemplar holder. The constructors call the prop's constructor 0x005EE050 on object + 4 and then store seven vtables of their own. Most slots are the prop's and the base's functions or eight-byte thunks that adjust `this`. The class factories and the prop manager hand out object + 4, the prop interface; the text decal's factory alone hands out the object itself (a jump to the constructor).

### Terrain decal

Class ID 0x2977AA48, interface 0xE982078A (gzcom-dll's header has the order of slots right, but gives the class ID as the interface ID, and calls fade action 2 "InsertOccupant" where the code takes the occupant out). 0xA0 bytes from a pool (0x00B0A8F0, 16 KB pages; allocator 0x005ED6E0, and the factory 0x005EF180 has the same code inline). Constructor 0x005EE360, destructor body 0x005ED500, deleting destructor 0x005EF1C0.

| Part | Vtable | The class's own |
| --- | --- | --- |
| +0x0 | 0x00AA4790, 7 slots | QueryInterface 0x005ECB10, SetDecalTexture 0x005ED7E0 (key, size), SetOpacity 0x005ED840, SetOpacityFade 0x005ECB90 (time, action, from, to), SetDecalSize 0x005ED870 |
| +0x8 | 0x00AA4720 | DoMessage 0x005EE580 |
| +0xC | 0x00AA4698 | Init 0x005EE410, Shutdown 0x005EE4B0, SetPosition 0x005EE6D0, SetAllFlags 0x005ED8D0 |
| +0x10 | 0x00AA467C | Write 0x005ED710, Read 0x005EF4E0, GetGZCLSID 0x005ED5A0 |

Fields: +0x6C the decal's ID in the overlay manager (-1 for none); +0x70 the texture's resource key (three dwords); +0x7C the size in metres; +0x80 the centre (x and z); +0x88 the shown opacity (1 from the constructor); +0x8C the fading opacity (1); +0x90 where the fade ends (0); +0x94 the fade's rate, opacity per second (1 from the constructor, 0 from the Init of a new decal; 0 means no fade); +0x98 what happens at the end (0 nothing, 1 the overlay is removed, 2 the occupant is removed); +0x99 the object holds a reference on itself (action 2); +0x9A it is registered with the frame service; +0x9B never written; +0x9C the overlay manager, referenced from the first time the decal is shown until Shutdown.

A decal draws nothing. When the prop is first shown the terrain view hands out one of its overlay managers (the Renderer's): number 0 for a decal whose fade rate is below 0.1 in magnitude, else number 2. That manager is told, through its interface, what to show: the key, the centre, the size and the opacity; later it is told of every change and switched off and on as the prop is hidden and shown.

### Visual effect

Class ID 0x2977AA49, interface 0x09820792 (gzcom-dll's header has the two SetEffectType overloads the wrong way round). 0x94 bytes from a pool (0x00B0A8FC, 16 KB pages; allocator 0x005ED9A0, factory 0x005F0560). Constructor 0x005F0210, destructor body 0x005F0340, deleting destructor 0x005F05A0.

| Part | Vtable | The class's own |
| --- | --- | --- |
| +0x0 | 0x00AA4ED8, 9 slots | QueryInterface 0x005ECC10, SetEffectType by ID 0x005EF7D0, SetEffectType by name 0x005F0780, StartEffect 0x005EDA50, StopEffect 0x005EDAA0. Slots 3 and 4 (the occupant and prop views) are shared stubs and stay the game's |
| +0x4 | 0x00AA4E78 | SetOrientation 0x005EF280, SetState 0x005EF2C0 |
| +0xC | 0x00AA4DE0 | Init 0x005EF5D0, Shutdown 0x005EE790, SetPosition 0x005EF220, SetAllFlags 0x005ED9D0 |
| +0x10 | 0x00AA4DC4 | Write 0x005F03B0, Read 0x005EF750, GetGZCLSID 0x005F02B0 |

Not virtual: UpdateTransform 0x005EE800 (tells the effect where it is) and MakeEffect 0x005EE9C0 (asks the effects manager for the effect). Both are ours.

Fields: +0x6C the effect (referenced, or null); +0x70 the effect's name (a game string, 0x14 bytes); +0x84 the effect's ID (0 for none); +0x88 and +0x8C how many name properties (0xC98204B8) and ID properties (0x2A6DEF88) the exemplar has, one per prop state; +0x90 the started byte; +0x91 the fresh byte (1 from the constructor: not shown yet; the first start of the effect is of kind 0, later ones of kind 1); +0x92 and +0x93 never written.

The effect comes from one of three places, in this order: the exemplar's name properties, taken by the prop's state (no effect for a state past the count, an empty name or the name "off"); else the ID properties the same way (none for ID 0); else the name kept (set by a caller or read from a save) when it is not empty, else the ID kept when it is not 0. The effects manager (0x00B43D1C) is asked for it. Init makes the effect, and so do SetState and the two SetEffectType; when none can be made the started byte is set all the same. StartEffect and StopEffect reach the effect only while base flag 2 is set; SetAllFlags starts and stops it as that flag changes. UpdateTransform gives the effect the prop's position and a rotation about the vertical by minus the orientation, in quarter turns, with scale 1.

### Automata controller

Class ID 0x2A4017A0, interface 0x8A401788. Like the decal and the effect, the prop is at +4. 0x88 bytes from a pool (0x00B0A914; allocator 0x005EDD80, factory 0x005F0490). Constructor 0x005EFDB0, destructor body 0x005EFEF0, deleting destructor 0x005F04D0. The prop tables of the three test cities have no type of this class. The one maker outside the factory is 0x00529600 (allocator at 0x00529628, constructor at 0x00529636), which was not traced further.

| Part | Vtable | The class's own |
| --- | --- | --- |
| +0x0 | 0x00AA4D28, 7 slots | QueryInterface 0x005ECC70, SetTemplateName 0x005F06F0 (a C string), SetEnabled 0x005EDE60 (bool). Slots 3 and 4 are the shared stubs, the game's |
| +0x4 | 0x00AA4CC8 | SetState 0x005F0530 |
| +0xC | 0x00AA4C30 | Init 0x005EFF60, Shutdown 0x005EEDD0 (SetPosition and SetAllFlags are the prop's and the base's) |
| +0x10 | 0x00AA4C14 | Write 0x005EDDB0, Read 0x005EFBF0, GetGZCLSID 0x005EFE40 |

Not virtual: MakeController 0x005F00B0, which makes the controller again. Fields: +0x6C the controller (referenced, or null); +0x70 the template's name (a game string, 0x14 bytes); +0x84 the enabled byte.

The prop asks the automata controller manager (the city's slot 0xBC, [automata.md](automata.md)) for a controller of its template, with the prop's occupant part and position 0. In state 0 the name is used as it is; in any other state the name is "name_state" (format `%s_%d` at 0x00AA4D44). The prop keeps the reference, and hands the enabled byte on to the controller's SetActive. Init takes the box's size from property 0x27812810 (16 m a side without it) and the name from property 0xC98204B8, or else from the exemplar's name (0x20). A name sets the enabled byte. A shown prop with a controller has it made active at the end of Init. Making it again first makes the old controller inactive, takes it off the manager and releases it. Shutdown takes it off and releases it without making it inactive.

### Burnt building

Class ID 0xC9EFB016. The class has no interface of its own: its QueryInterface is a jump to the prop's, so it does not answer its class ID. 0xD8 bytes from a pool (0x00B0A908; allocator 0x005EDAE0, factory 0x005EFC80). Constructor 0x005EF810, destructor body 0x005EFB20, deleting destructor 0x005EFCC0. These are the addresses lot.toml had as SC4ExplosionEffect.

Unlike the other five classes, it holds the prop at +0, so the prop's parts lie four bytes before theirs: +4 the message target, +8 the occupant, +0xC the serializable, +0x10 the property holder, +0x24 the exemplar holder. After the prop comes a model part at +0x68 (constructor 0x00801CF0 as a base, destructor 0x00801810), with its vbptr at +0x6C (table 0x00AA4B90: -4, 0x68). Its virtual base, the model interface, is at +0xD4 (vtable 0x00AA49B0, 19 thunks through the displacement at +0xD0). The constructor takes an argument: not 0 for the most derived object, which then sets the vbptr and the virtual base's first vtable (0x00AA1F38). The factory and ExplodeLot pass 1.

| Part | Vtable | The class's own |
| --- | --- | --- |
| +0x0 | 0x00AA4B30, 24 slots | QueryInterface 0x005ECC60 |
| +0x8 | 0x00AA4A98 | Init 0x005F0600, Shutdown 0x005EFD20, SetAllFlags 0x005EDB50 |
| +0xC | 0x00AA4A7C | Write 0x005EDB10, Read 0x005EFBA0, GetGZCLSID 0x005EF950 |
| +0x68 | 0x00AA49FC, 1 slot | the deleting destructor's thunk 0x005EF9C0 |
| +0xD4 | 0x00AA49B0, 19 slots | the model interface: the drawing, the game's |

Not virtual: SetOccupant 0x005EEB70 and the quad builder 0x005EDC60 (drawing, the game's). Fields: +0x94 a vector of five texture bindings; +0xA0 the burnt box (six floats, saved); +0xB8 the quad (0x50 bytes, made by the quad builder); +0xBC the render state (8 bytes); +0xC4 the box's low corner, scaled.

The lot developer's ExplodeLot (0x006CC250) makes one when a building burns down: it calls the allocator, then the constructor with 1, then SetOccupant with the building (the building's box goes into the prop's box and the burnt box, and the quad is built), then base flag 0x40, then Init, and hands it to the lot. Init loads five texture bindings, all of texture 0x7AB50E44, 0x1ABE787D, 0xA9F0E710, from the framework's texture binding factory, and sets up the render state. For a burnt building read from a save it also builds the quad. SetAllFlags adds the model (the virtual base) to the renderer (0x00B43DD0) when base flag 2 comes on, and takes it off when the flag goes. Shutdown lets the bindings go and frees the quad. It does not take the model off the renderer, and it does not free the prop's date range.

### Text decal

Class ID 0x8B79C707, interface 0x2B3B7D86 (the label interface; QueryInterface answers it, not the class ID). Init gives the prop the prop type 0xCB79919B, the "kind" the sign removal tool and the label tool's filter test ([tools.md](tools.md)). 0xBC bytes from a pool (0x00B0A9B8; allocator 0x005F7D80, factory 0x005F84D0). Constructor 0x005F8300, destructor body 0x005F8570, deleting destructor 0x005F8510. The prop manager's TextDecal command (0x004A363F / 0x004A364D, [propmgr.md](propmgr.md)) and the label tool (0x004BD971 / 0x004BD97F, [tools.md](tools.md)) make them with the allocator and the constructor. The IDs in tools.md (0xBC bytes, kind 0xCB79919B, interface 0x2B3B7D86) and in the survey (class 0x8B79C707) are all right; tools.md now says which is which.

| Part | Vtable | The class's own |
| --- | --- | --- |
| +0x0 | 0x00AA5680, 11 slots | QueryInterface 0x005F7650, SetText 0x005F82E0, GetText 0x005F8410, SetSize 0x005F7B10, SetMoving 0x005F8170, SetColour 0x005F7B50 (r, g, b), SetDirection 0x005F7D60, SetOpacity 0x005F7C00. Slot 3 is the shared AsOccupant stub, the game's |
| +0x8 | 0x00AA5610 | DoMessage 0x005F8030 |
| +0xC | 0x00AA5588 | Init 0x005F7F40, Shutdown 0x005F76C0, SetPosition 0x005F7AA0, SetAllFlags 0x005F81F0 |
| +0x10 | 0x00AA556C | Write 0x005F7DB0, Read 0x005F7E50, GetGZCLSID 0x005F8430 |

Not virtual: RemoveOverlays 0x005F77C0, MakeOverlays 0x005F7810, MoveOverlays 0x005F7A50, PickOverlayManagers 0x005F7C80 and MakeText 0x005F8190. All ours.

Fields: +0x6C the text (a game string, 0x14 bytes); +0x80 the size (1); +0x84 and +0x88 the text's IDs in the two overlay managers (-1 for none); +0x8C the moving byte (the label tool sets it while a label is dragged); +0x90 the width in metres (100 from the constructor); +0x94 the place (x and z); +0x9C the colour (three floats, 255 each); +0xA8 the opacity (1); +0xAC the direction (1); +0xB0 the text's texture; +0xB4 and +0xB8 the two overlay managers, referenced.

A label draws nothing itself. The service at 0x00B43CE4 (the application's slot 46) makes a texture of the text (its slot 5 with 0xCC1FD62C). The terrain view hands out two of its overlay managers, 0 and 1, or 2 and 3 while the label is moving; both are given the texture, the place, the width and a turn from the table at 0x00B0A978 (by the camera's rotation and the direction), then the colour and the opacity. The width is the texture's width over its height times the size times 16, and the prop's box is centred on the place, half the width one way and the size times 8 the other (swapped for direction 0), from 0.1 below the ground to 0.1 above. MakeOverlays does all this again for every change of the size, direction or text, for a change of colour or opacity unless the label is moving (then the overlays are told), for the view's message 0xA6B79602 (the camera moved) and when the label is shown. SetPosition moves the overlays while moving and makes them again otherwise.

A label is shown (base flag 2) only while the city shows signs and labels (the service 0x00B43DB8's slot 10): SetAllFlags drops flag 2 otherwise. Init (for a label not read from a save), the city's message 0x26D31EC1 and a new visibility filter (message 0x06CDB65B, its data 2 pointing at the filter) show it when the filter (the service 0x00B43DC4's slot 18) takes it and labels are shown; the filter message also hides it. Shown, it makes its overlays (and a texture, when it has text and none); hidden, it takes them off.

### What follows frames and the view, not game days

- Base flag 2 of both (the occupant's flags at object + 0x3B). SetAllFlags (occupant slot 30) acts on every change of it. What changes it through that slot in play was not traced (the prop's own SetPlaceableFlag writes the byte directly); AddCityProp alone does not set it, and in the test cities nearly every effect prop has it set. With it come the decal's overlay (+0x9C) and ID (+0x6C), made when it is first set, and the effect's fresh byte.
- The decal's fade. The frame service's message 0x86AD10EE carries the frame's time in milliseconds; the fading opacity, the shown opacity and the end of the fade follow it.

- The burnt building's model in the renderer (SetAllFlags with base flag 2).
- The text decal's base flag 2 (whether the city shows signs and labels, the visibility filter), its texture, overlay managers and IDs, and with them its width and box (the texture's size, the ground's height) and the turn it gives the overlays (the camera).

The rest is plain state: the decal's texture, size, centre and fade settings; the effect's name, ID, property counts and started byte; the controller prop's template name and enabled byte, and whether the manager gave it a controller; the burnt building's box, texture bindings, render state and quad.

## What is ours

All ten groups are off by default. They call the prop's functions at their addresses.

| Replacement | What is ours |
| --- | --- |
| `simulators.propkinds.decal` | 13 detours: QueryInterface, SetDecalTexture, SetOpacity, SetOpacityFade, SetDecalSize, DoMessage, Init, Shutdown, SetPosition, SetAllFlags, Write, Read, GetGZCLSID |
| `simulators.propkinds.decal_object` | our own decal: detours on the constructor, the pool's allocator, the factory, the destructor body and the deleting destructor, with copies of the seven vtables taken at the first construction (after every other group's slot swaps) |
| `simulators.propkinds.effect` | 16 detours: QueryInterface, the two SetEffectType, StartEffect, StopEffect, SetOrientation, SetState, UpdateTransform, MakeEffect, Init, Shutdown, SetPosition, SetAllFlags, Write, Read, GetGZCLSID |
| `simulators.propkinds.effect_object` | the same five for the effect |
| `simulators.propkinds.automata` | 10 detours: QueryInterface, SetTemplateName, SetEnabled, MakeController, SetState, Init, Shutdown, Write, Read, GetGZCLSID |
| `simulators.propkinds.automata_object` | the same five for the controller prop |
| `simulators.propkinds.burnt` | 8 detours: QueryInterface, SetOccupant, Init, Shutdown, SetAllFlags, Write, Read, GetGZCLSID |
| `simulators.propkinds.textdecal` | 21 detours: QueryInterface, SetText, GetText, SetSize, SetMoving, SetColour, SetDirection, SetOpacity, RemoveOverlays, MakeOverlays, MoveOverlays, PickOverlayManagers, MakeText, DoMessage, Init, Shutdown, SetPosition, SetAllFlags, Write, Read, GetGZCLSID |
| `simulators.propkinds.textdecal_object` | the same five for the text decal (copies of its seven vtables); its factory hands out the object itself |
| `simulators.propkinds.burnt_object` | the same five for the burnt building, with copies of its eight vtables (the virtual base's included; the vbptr's table stays the game's) |

Two address entries were renamed: 0x005EE360 and 0x005ED6E0 were SC4CraterProp::Construct and ::Allocate, and are now cSC4PropOccupantTerrainDecal::Construct and ::Allocate. In piece 2, three more: 0x005EF810, 0x005EDAE0 and 0x005EEB70 were SC4ExplosionEffect::Construct, ::Allocate and ::SetOccupant, and are now cSC4PropOccupantBurntBuilding's.

Still the game's: the eight-byte adjustor thunks (they jump into our detours), the prop's and the base's code the two classes call, the shared stubs in the effect's slots 3 and 4, the overlay manager, the effect and the effects manager, the pools' page code (0x0090CF9A and neighbours), the burnt building's quad builder 0x005EDC60, its model part (0x00801CF0, 0x00801810) and its model interface (the 19 thunks 0x005EF9D0 to 0x005EFB10 and what they jump to: 0x005EDC20, 0x005EEC00, 0x005EEBD0), the automata controller manager and its controllers (the automata groups), the texture bindings and the render state, and the other two classes of the family.

Callers outside the class: the class registration (0x0046078B to 0x004607A2 calls each GetGZCLSID and keeps each factory's address, before any replacement is installed), and four places that call an allocator and then the constructor: the prop manager (0x004A256A / 0x004A2578 and 0x004A30B7 / 0x004A30C5 for the effect, 0x004A258B / 0x004A2599 and 0x004A30D8 / 0x004A30E6 for the decal), the terrain code (0x0075EE44 / 0x0075EE52 and 0x006CC3B1 / 0x006CC3C3, decals) and 0x005CBC1E / 0x005CBC2C and 0x005CECC5 / 0x005CECD3 (effects). Each reads only EAX after either call, so the detours are plain functions. No wrapper of the `game.*` kind touches any of the 39 addresses. The same holds for piece 2: the registration (0x004607AF to 0x004607C6), 0x00529628 / 0x00529636 (the controller prop) and ExplodeLot's 0x006CC310 / 0x006CC320 / 0x006CC33C (the burnt building).

Piece 3: the registration (0x00460851 / 0x00460856), the prop manager's TextDecal command (0x004A363F / 0x004A364D) and the label tool (0x004BD971 / 0x004BD97F). Each reads only EAX after either call. Ours of the command (src/core/propmgr_2.cpp) and of the tool (src/core/tool_decal.cpp) call the same two addresses, so they work with `simulators.propkinds.textdecal_object` off and on. MakeOverlays, RemoveOverlays, MoveOverlays, PickOverlayManagers and MakeText have no caller outside the class. No jump or call in the exe lands inside the first five bytes of any of the 26.

## Kept as the game does it

Copied on purpose and commented in the code:

- Init of a decal that was not read from a save gives it a box of 32 m a side and rate 0 (no fade). Whatever SetDecalTexture or SetOpacityFade set before Init is lost.
- SetDecalTexture changes the prop's box but does not tell the overlay.
- SetOpacityFade does not test for time 0.
- The fade's end: with action 0 the overlay keeps getting the stored opacity, not the end value, and the fading opacity goes on past the end. Action 1 removes the overlay; action 2 takes an initialized prop out of the occupant manager and shuts it down. The overlay manager is not tested for null.
- A decal's Shutdown and an effect's Shutdown call the base's Shutdown, not the prop's; they free the date range themselves. The decal's also zeroes the third dword of the texture key.
- A decal's Shutdown releases the reference the object holds on itself. When that was the last one the object is gone and the byte after is written into freed memory, as in the game.
- The effect's SetState stores the byte and makes the effect again. Unlike the prop's: no message, and the models are not told.
- The effect's Shutdown leaves the started byte as it is.
- When no effect can be made (a state past the properties, an empty name, "off", an ID or name the effects manager does not know) the started byte is set, whatever it was. StartEffect without an effect sets it too and answers false.
- The effect's destructor releases the effect without stopping it (Shutdown stops it).
- SetEffectType by name hands the string assignment the address of its own argument as the allocator tag.
- The decal's Write saves the overlay ID as it is and Read puts it back: the ID of an overlay of the session that saved. Nothing uses it while the overlay manager is null, and showing the decal makes a new one. The decal's Read answers in AL only.
- Neither destructor frees the date range (Shutdown does), and the deleting destructors run the body before they test the object for null. The abstract vtable the game stores at +0 before the prop's constructor is not stored (nothing reads it); +0x9B of the decal and +0x92, +0x93 of the effect are left as the pool had them.
- The controller prop: MakeController asks the manager without a null test; SetEnabled is handed the enabled byte read before anything else. The game pushes that byte as a dword whose upper bytes are stack leftovers, and every reader takes only the byte; ours passes 0 above it. An empty name makes no controller, and SetEnabled is not called. Init copies the first three floats of the size property however many it has, ignores whether the exemplar's name was found, and sets the enabled byte whenever it hands a name to SetTemplateName. Shutdown takes the controller off the manager without making it inactive. The destructor releases it without taking it off. A controller prop that is read from a save and has no exemplar name keeps its saved name but gets no controller, because Init makes one only from the exemplar.
- SetState of the controller prop stores the byte and makes the controller again; like the effect's, it sends no message.
- The burnt building: QueryInterface is the prop's, so its class ID is not answered. Init calls the base's Init without testing again, does not test the texture factory for null, and asks for the same texture five times. Shutdown runs the base's Shutdown first, releases five bindings whatever the vector's size (the game's erase of the whole range moves nothing, then sets the end to the beginning), frees the quad, and does not take the model off the renderer or free the date range. The destructor frees the vector's buffer but not the bindings or the quad. SetAllFlags does not test the renderer for null.
- The text decal: QueryInterface answers its interface only, not its class ID; the factory hands out the object, not the object + 4. SetSize takes only sizes above 0 and below 8 and does not compare with the size kept. SetColour does nothing when each part is within 0.0001 of the one kept; SetOpacity when the value is the same (not a number always counts as new). While moving, colour and opacity go to the overlays there are; otherwise every setter lays the text down again. The overlay managers are not tested for null where an ID is there, nor the terrain view, the camera, the texture service or the filter of the filter message. MakeOverlays reads the camera's rotation and a table of turns without a bounds test. SetPosition asks the city for the cell and does not use the answer. The texture is read again before each call that takes it, and MakeText lets it go twice over (the second finds null), as the game does. Shutdown calls the prop's own Shutdown (it frees the date range). The destructor does not store the vtables first and releases the managers and the texture without taking the overlays off.
- The text decal's SetAllFlags: where flag 2 changes, the game returns whatever the last call left in EAX; ours returns the base's flags byte there too. No caller is known to read it.
- The text decal's Read refuses no version: before version 3 a dword after the text is dropped, and in version 1 the size is an unsigned integer. Its version is read into a stack slot the game never set; ours starts it at 0, which matters only when the read itself fails. The direction is saved as a byte and read back into the argument's stack slot, then zero-extended.
- Where the game leaves stack leftovers (the overlay manager's 0x24-byte info block, past its first three dwords; two bytes of the effect's 0x38-byte transform block) ours zeroes them.

Version 3 is also the controller prop's record (the prop's, the enabled byte, the name) and the burnt building's (the prop's, the burnt box). For the decal and the effect, version 3 is: the prop's record, then the class's fields in order. The decal saves the overlay ID as it is, the key, the size, the centre, the opacity, the fade's three values, the action and two bytes. The effect saves the started and fresh bytes, then the name and ID as they are when no property gave them, else an empty string and ID 0. Read refuses any other version. When the prop's Read fails the decal's Read answers false; the effect's reads nothing more and answers whether the stream is free of errors. The property counts are not saved: Init counts them again.

The text decal's version 3 is: the prop's record, the text, the place, the size, the width, the place again, the colour, the opacity and the direction's byte.

## Tests

`OPENSC4_TEST_PROPKINDS=<day>` (fingerprint token ` propkinds <hash>`, log lines `autotest: propkinds:`). Level 1 reads only. It walks the occupant manager's grid for occupants of type 0xA823821E whose class ID is one of the two, and counts them and sums and XORs a hash of each one's plain state. Then it makes props it never inserts: one of each class from the allocator and constructor (as the terrain code makes a decal), and up to two of each from the prop manager's CreateProp when its tables have types of the class. On each it calls QueryInterface (the IDs the class takes and one it does not), GetGZCLSID and the setters that only write fields while there is no overlay and no effect (for the decal SetDecalTexture, SetOpacity, SetDecalSize, SetPosition and SetAllFlags with another flag; for the effect StopEffect, StartEffect, SetOrientation, SetPosition, SetState, SetEffectType with the ID it has), hashes what they left, and releases the prop. Left out of the hashes because frames or the view drive them: the overlay's ID and manager, the base flags, the fresh byte of city effects, every pointer.

`OPENSC4_TEST_PROPKINDS_EDIT=1` changes the city and puts it back. At a spot with no lot whose 64 m occupant cell is empty it initializes a decal, adds it, shows it (base flag 2 through SetAllFlags, as the view does), fades it from 1 to 0.25 with SetOpacityFade and six 500 ms frame messages the test makes (action 0; the fade's values are on the log line), hides, shows and removes it; the removal's Shutdown takes it off the frame service again, and the test checks that. Then it adds an effect prop, switches its states, shows it, stops, starts and turns it, gives it a name and an ID no effect has and the name "off", and removes it. The counts are taken again and must be the same; the log says `restored yes`. `OPENSC4_TEST_PROPKINDS_EDIT=2` does the same and then leaves a decal (no fade) and an effect prop in the city, so the run's save holds them and a run started from that save reads them back. The tutorial does not hold any of this back; `OPENSC4_TEST_TUTORIAL_OFF` is not needed.

Piece 2 extends the same switch to the controller prop and the burnt building (the log line `city ...` gives counts and hashes for all four classes; the token changes with it).

- The walk finds them by class ID like the others. The burnt building is confirmed through the prop's interface, since it does not answer its class ID.
- Made props: one of each from the allocator and constructor; the burnt building's is made with 1, as ExplodeLot does. The controller prop gets a template no script has (the manager is asked and answers null), the enabled byte, state changes ("name_state") and the setters. The burnt building gets SetOccupant with the first building of the grid, flag 0x40 and a flag change. Neither is initialized: the base's Init and Shutdown send the occupant messages 0x99EF1140 and 0x99EF1141, which a prop outside the city should not. So the burnt building's quad leaks, as it would in the game.
- With `_EDIT=1` there are two more round trips.
  - The controller prop is added. Its template is the exemplar's; the tables have no type of the class, so it is the smallest template name among the automata controller manager's own controllers (`accident_attract` on Making Money, `crowd_sm` on Big City, `construction_vehicle_attractor` on Rush Hour), and the manager makes a real controller. It is disabled and enabled, put in state 1 (no "name_1" template: no controller) and back, given a template no script has and its own back, and removed.
  - The burnt building is set up at the spot as ExplodeLot sets one up, but from its own box (SetSize 16 by 8 by 16, SetPosition, SetOccupant with its own occupant part, flag 0x40). It is added (Init loads the texture bindings), shown, hidden, shown and hidden: the renderer holds the model after each show and not after each hide. Then it is removed.
- `_EDIT=2` leaves one of each in the city, the controller prop with that template.

Piece 3 extends it again to the text decal (the label; `city` gives its count, hashes, how many have a texture and how many overlays, and one line per label with its text in hex, sorted; the token changes with it).

- The walk confirms a label through its interface 0x2B3B7D86, since it does not answer its class ID. Its plain state: the text, size, moving byte, place, colour, opacity and direction (the width and the box follow the texture and the ground, so they are left out).
- Made label: from the allocator and the constructor as the label tool makes one, never inserted or shown. The setters (refused sizes 0, 8 and -1, a colour within 0.0001, the same opacity and direction), SetPosition, the moving byte (managers 2 and 3 picked and given back), SetText with "Label" and then "Label é" (UTF-8 C3 A9) read back through GetText, a flag other than 2.
- With `_EDIT=1` one more round trip. A label is made small (size 0.1, so that its box, about 6 m wide with this text, stays inside the spot's empty 64 m occupant cell; at size 1 it is 64 m wide and the flammability simulator's map then differs from the unchanged city's), placed and added as the label tool does it (Init gives it the prop type and its managers and shows it when the city shows signs and labels), given "Label é", shown, set a size (0.15), colour, opacity and direction (each lays the text down again), dragged (the moving byte, a move, a colour and an opacity while moving), sent the view's messages 0xA6B79602, 0x26D31EC1 and 0x06CDB65B with the city's filter, hidden and shown. Then signs and labels are switched off: showing it is refused. The switch is put back, and the label is removed. When signs and labels were off before, the test switches them on for the round trip and back after.
- `_EDIT=2` leaves two labels, "Label é" at the spot and "Label" 4 m off, each with a colour and a size of its own, so the save holds both texts.

The test never calls Write, Read or a destructor: the save at the end of every run and the load of a save reach the first two, Release the others. Not tested at all: fade actions 1 and 2.

## What the test cities hold

None of the three tutorial cities (Making Money d0090, Big City d0000, Rush Hour d0090) holds a single terrain decal, and the prop tables have 0 decal types and 89 effect types. The cities hold 53, 262 and 113 effect props. So the decal is carried by props the test makes. None of the three holds a controller prop or a burnt building either, and the prop tables have no type of the two classes. Both are carried by props the test makes and, for the save, the props it leaves in the city.

AddCityProp does not set base flag 2 (shown); the test's SetAllFlags does. At the end of a fade with action 0 the overlay keeps getting the stored opacity: in the test the fourth 500 ms step lands at 0.24999999, just past the end, and the opacity stays at 0.4375.

## Acceptance and parity

Acceptance (three saves): runs on 2026-10-13 and 14 with the debug build's snapshot pp1, 7 days, results in `%LOCALAPPDATA%\opensc4\analysis\pp-trial\`. Making Money d0090, Big City d0000 and Rush Hour d0090, each in three cases: the switch set to a day that never comes (the unchanged city), level 1, and level 1 with the round trip (`_EDIT=1`); each case run four times: the original twice, every group of the project ours, and only the four `simulators.propkinds.*` and the four `simulators.propocc.*` groups ours. 36 of 36 runs finished, and in every one of the nine save and case pairs the four runs agree in every `autotest:` line and every fingerprint line. The original's two runs agreed with each other everywhere, so nothing had to be taken out of the token. With the round trip the log says `restored yes` in all twelve runs, and on all three saves the fingerprint without the token equals the unchanged city's on all eight days, in the level 1 runs and in the round trip runs, the original's and ours alike: the round trip leaves the city as it found it.

| Save | Level 1 | With the round trip |
| --- | --- | --- |
| Making Money d0090 | token BFDB8F16 | token 5D3D7A10 |
| Big City d0000 | token 22257F83 | token C75EE1EF |
| Rush Hour d0090 | token 2B464764 | token B63A8311 |

The decal's save: a Making Money run with `_EDIT=2` on the original and one with the eight prop groups ours each left a decal and an effect prop in the city and saved it (the same log lines in both; the decal's plain hash 01426569). The decal was made by the constructor and has no prop type: the original saves and loads it all the same. Each of the two saves was then loaded by the original and by ours with level 1: all four runs finished with the same fingerprint lines and found 1 decal (01426569, with an overlay, no fade) and 54 effect props. In the two saves the terrain decal's record (class 0x2977AA48, 137 bytes) and the effect props' record (0x2977AA49, 5,480 bytes) are byte for byte the same.

`tools/re/savediff.py --pointers` on the original's save and ours leaves two records it cannot explain by moved addresses: the traffic network map (0x6A0F82B2, a few hundred bytes near the end of its 339 KB) and a record with no decoder (0xCA14CA03, 16.8 KB, a list of references to flora occupants). Neither is a prop record. The same two records differ the same way between two runs of the original (level 1 and round trip runs of the matrix: 492 to 576 bytes of the first, 266 to 12,451 of the second) and between the original's two loads of the decal saves, so this is the original's own variation; the traffic network map follows frames ([traffic-map.md](traffic-map.md)). In the runs where the test day never comes, two runs of the original differ in 14 records, the plain prop record among them (431 KB of it), and ours differs from the original in the same 14 by about the same amounts: that too is the original's, and was not looked into here. The decoder (`tools/re/sc4save.py`, `tools/re/save_schemas/propocc.py`) knows the plain prop's record only; it has no schema for the decal's or the effect prop's record and skips both.

Reached: all 39 functions (each logs its first call) in the runs with the groups ours. The effect's 21 are reached by the tutorial cities themselves and the test; the decal's Init, Shutdown, DoMessage and SetOpacityFade only by the round trip, and its factory, Write and Read only by the save leg above. Not reached inside them: fade actions 1 and 2, the decal's Init of a saved decal whose fade was running, an effect made from an ID property or from a kept ID or name the effects manager knows.

No corpus save holds a terrain decal. Besides the three cities, one save of each disaster corpus was looked at with level 1 on the original: `saves-damaged` f0000 has 0 decals and 50 effect props, `saves-fire` f0003 0 and 92, `saves-riot` Big City r0365 0 and 281.

Parity (batch pp1b, 7 days, 2026-10-14, the four groups with the four `simulators.propocc.*` groups and every group of the tools pieces on the B side): plain 12 of 12 saves identical, with `OPENSC4_TEST_PROPKINDS=2` 12 of 12, with `OPENSC4_TEST_PROPKINDS_EDIT=1` 12 of 12, on the plopped corpus with the edit 3 of 3; every save on its first pair. All 30 runs of the two edit legs logged `restored yes` and none `STILL REGISTERED`. Result folders `%LOCALAPPDATA%\opensc4\parity\pp1b-*`.

The audit compared all 39 functions instruction by instruction with their call sites, and found no difference in behaviour. Two assumptions it could not close are not measured:

1. UpdateTransform zeroes two pad bytes of the 0x38-byte transform block where the game leaves stack leftovers. The callee behind effect slot 7 (0x005BE860) was not traced, so it is not known that it never reads them.
2. The x87 arithmetic (the effect's rotation, the decal's fade) is computed in doubles, which equals the game at 53-bit precision. That is measured in the simulator's tick only, and these functions run from view and frame paths ([pollution.md](pollution.md)).

### Piece 2

Acceptance (three saves): runs on 2026-10-19 with the debug build's snapshot pp2, 7 days, results in `%LOCALAPPDATA%\opensc4\analysis\pp2-trial\`. The same nine save and case pairs as piece 1, with `OPENSC4_TEST_FINGERPRINT=1`, each run four times:

- the original, twice;
- every group of piece 1's B list ours, with the six `simulators.automata.*` groups and the four new ones;
- only the eight `simulators.propkinds.*` and the four `simulators.propocc.*` groups ours.

36 of 36 runs finished. One run of the original stopped on its first day with no crash report (killed from outside) and was run again. In every pair the four runs agree in every `autotest: propkinds` line and every fingerprint line. All twelve round trips log `restored yes`, and with the token left out, every level 1 and round-trip run has the same fingerprints as the unchanged city on all eight days.

| Save | Level 1 | With the round trip |
| --- | --- | --- |
| Making Money d0090 | token A4410A04 | token 63E605A7 |
| Big City d0000 | token 2AFCA012 | token 15B585A3 |
| Rush Hour d0090 | token A191413E | token 34992FCF |

The save leg: Making Money d0090 with `_EDIT=2`, once on the original and once with the twelve groups ours. Each left one prop of each class in the city, the controller prop with `accident_attract` and a controller. Both runs logged the same lines and saved. Each save was then loaded by both sides with level 1, and all four loads logged the same lines. They found the controller prop with its name and enabled byte but no controller, because a prop with no exemplar gets none from Init. They found the burnt building with the same plain hash as when it was left (A8EDE9D0). In the two saves the controller prop's record (0x2A4017A0, 111 bytes) and the burnt building's (0xC9EFB016, 114 bytes) are the same but for the record header's CRC and ID, which differ the same way in the decal's record.

Reached: all 28 functions in the runs with the groups ours. The matrix reaches 22 of them; the factories, Write and Read only the save leg. Not reached inside them:

- a controller prop with a template from its exemplar: the tables have no type of the class;
- a "name_state" template that exists;
- ExplodeLot's own path to the burnt building: no test city burns down a building in a week;
- a burnt building read from a save that had a quad.

Parity (batches pp2-*, 7 days, 2026-10-19, one job, the B side as in the matrix: piece 1's B list with `determinism.dispatch_info=ours` among the base, the six `simulators.automata.*` groups and the eight `simulators.propkinds.*` groups):

| Batch | Result |
| --- | --- |
| plain | 12 of 12 saves the same |
| `OPENSC4_TEST_PROPKINDS=2` | 12 of 12 |
| `OPENSC4_TEST_PROPKINDS_EDIT=1` | 11 of 12, Rush Hour d0000 partly (below) |
| the plopped corpus with the edit | 3 of 3 |
| `saves-fire` with `OPENSC4_TEST_PROPKINDS=6` | 2 of 2 |

All 34 runs of the two edit batches logged `restored yes` and none `STILL`. Results are in `%LOCALAPPDATA%\opensc4\parity\pp2-*`.

Rush Hour d0000 with the edit:

- The first B run differed from the original in the advisor fields of the fingerprint (`adv` and `advisors`). The difference was already in its first fingerprint after the start, on day 2455318.
- That fingerprint was logged before the test ran and before the first call of any piece 2 function. The city holds neither class, so none of piece 2's code had run.
- Every `autotest: propkinds` line of that run agrees with the original's. Its two retries agree with all three runs of the original.
- The same save and case against only the twelve prop groups (pp2-edit-rh0m1 to 3): 3 of 3 pairs the same.

So that one run's difference comes from another group of the B list, not from this piece.

Fires: `saves-fire` f0002 and f0003 with `OPENSC4_TEST_FIRE_PUSH=100` (the original and the twelve groups, test on day 7) agree too. But no building burned down in the week, so ExplodeLot's path to the burnt building (and SetOccupant from a building) was not reached by a fire. It is reached only by the test, which calls SetOccupant with the grid's first building on a burnt building it never inserts.

The audit compared all 28 functions instruction by instruction with the disassembly and found no difference in behaviour. Where ours differs at all, it is in bits nobody reads: the upper bits of EAX where callers read AL, and the upper bytes of the enabled dword MakeController hands on, where every reader takes the byte. The one comment it found wrong, the virtual base's slot count, was fixed.

### Piece 3

Acceptance (three saves): runs on 2026-10-19 with the debug build's snapshot pp3 (main as of #300 merged in), 7 days, results in `%LOCALAPPDATA%\opensc4\analysis\pp3-trial\`. The same nine save and case pairs as pieces 1 and 2, with `OPENSC4_TEST_FINGERPRINT=1`, each run four times:

- the original, twice;
- piece 2's B list ours, with the two new groups;
- only the ten `simulators.propkinds.*` and the four `simulators.propocc.*` groups ours.

36 of 36 runs finished. In every pair the four runs agree in every `autotest: propkinds` line and every fingerprint line. All twelve round trips log `restored yes`. With the token left out, every level 1 and round-trip run has the same fingerprints as the unchanged city on all eight days.

| Save | Level 1 | With the round trip |
| --- | --- | --- |
| Making Money d0090 | token D3B04CBB | token 32F41D1D |
| Big City d0000 | token DB2849AD | token D001FA88 |
| Rush Hour d0090 | token E2D864FB | token 8BBE8AA4 |

A first matrix gave the label of the round trip size 1. Its text is then 64 m wide, and its box reached past the spot's empty 64 m occupant cell. On Making Money and Big City the flammability simulator's map hash (`flam`) then differed from the unchanged city's, in the original's runs and ours alike. The round trip now makes the label small (size 0.1).

The save leg: Making Money d0090 with `_EDIT=2`, once on the original and once with the fourteen groups ours. Both runs logged the same lines and saved. Each save was then loaded by both sides with level 1, and all four loads logged the same lines. They found both labels with the texts they were given, `Label` (4C6162656C) and `Label é` (4C6162656C20C3A9), and with the same plain hashes as when they were left (sum 7409E392, exclusive or 6BE9D050), each with a texture and its two overlays. The decal, the effect props, the controller prop and the burnt building came back as in piece 2.

The label tool's test (`OPENSC4_TEST_TOOLS4=2 OPENSC4_TEST_TOOLS4_COMMIT=1 OPENSC4_TEST_TOOLS4_PARTS=1`, Making Money d0090) makes labels in the game through the tool. The original and the propkinds and propocc groups ours log the same `autotest: tools4` lines. The first run of the original differed from ours in the advisor fields of the fingerprint from the day before the test on. A second run of the original matched ours exactly.

Reached: all 26 functions in the runs with the groups ours. The matrix reaches 23 of them; the factory, Write and Read only the save leg. Not reached inside them:

- Read of a version below 3;
- a label shown by the filter message;
- the setters of a label read from a save.

No test city holds a label, and the prop tables have no type of the class.

Parity (batches pp3-*, 7 days, 2026-10-19, one job, the B side as in the matrix, with `determinism.dispatch_info=ours` among the base):

| Batch | Result |
| --- | --- |
| plain | 11 of 12 saves the same, Making Money d0365 partly (below) |
| `OPENSC4_TEST_PROPKINDS=2` | 12 of 12 |
| `OPENSC4_TEST_PROPKINDS_EDIT=1` | 12 of 12 (Making Money d0365 on its second pair: the original had two outcomes there) |
| the plopped corpus with the edit | 3 of 3 |

All 32 runs of the two edit batches logged `restored yes`, and none logged `STILL`. Results are in `%LOCALAPPDATA%\opensc4\parity\pp3-*`.

Main's `tools/re/paritygroups.py` stopped on the regional city record (its schema has no CRC or ID) and so judged every save different. The batches were judged with a local copy that skips the two fields. That fix is not part of this change: it lands with another branch.

Making Money d0365, plain: one B run of three differed from the original in the advisor fields of the fingerprint (`adv` and `advisors`), from its second day on. The other two agree with all three runs of the original. That run never called a text decal function (the city holds no label), and the original's advisor fields vary in the same way between its own runs (above). So the difference does not come from this piece.

The audit compared all 26 functions instruction by instruction with the disassembly and found no difference in behaviour. It noted three places where ours did less than the game:

- MakeOverlays read the texture once where the game reads it before each call;
- MakeText let it go once where the game tests it twice;
- SetAllFlags returns the base's byte where the game leaves the last call's EAX.

The first two were changed to match. Not measured:

1. Whether any caller of occupant slot 30 (SetAllFlags) reads more than AL. Where flag 2 changes, the game's EAX is whatever the last call left.
2. The x87 arithmetic of MakeOverlays (the width) and SetColour (the comparison) is computed in doubles. This equals the game at 53-bit precision, as for the decal.
