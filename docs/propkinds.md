# The terrain decal and the visual effect (cSC4PropOccupantTerrainDecal, cSC4PropOccupantVisualEffect)

Two of the six classes that derive from the prop occupant ([propocc.md](propocc.md)). A terrain decal is a picture laid flat on the ground, with an opacity that can fade away. A visual effect is a prop that is an effect of the effects manager (smoke, a fountain, a flare) at the prop's place. Both are made by the prop manager when a prop type's class says so ([propmgr.md](propmgr.md)); the terrain code makes decals itself (0x0075EDC0, 0x006CC250), and two functions not yet identified make effect props (0x005CBC10, 0x005CEC20). Reimplemented like the other classes: one writer with every virtual call's argument count checked against the callee's own code (91 virtual calls), then an independent audit of all 39 functions against the disassembly, then the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/propkinds.toml). This is piece 1 of 4 of the prop family; the survey below says what is left.

## The family

Six classes derive from the prop. Logic only, without drawing:

| Class | Class ID | Constructor | Logic | Drawing |
| --- | --- | --- | --- | --- |
| Terrain decal | 0x2977AA48 | 0x005EE360 | 18 functions, 2,408 bytes | none |
| Visual effect | 0x2977AA49 | 0x005F0210 | 21 functions, 2,832 bytes | none |
| Automata controller | 0x2A4017A0 | 0x005EFDB0 | 15 functions, 1,670 bytes | none |
| Burnt building | 0xC9EFB016 | 0x005EF810 | 10 functions, 1,295 bytes | the quad builder 0x005EDC60 and about 19 small model-interface functions, 0x005EF9D0 to 0x005EFB10 |
| Sign | 0xAB72FBB3 | 0x005F5510 | 20 functions, 2,985 bytes | 14 functions, 6,280 bytes (the geometry builder 0x005F5FB0 alone is 4,964) |
| Text decal (the label) | 0x8B79C707 | 0x005F8300 | 28 functions, 3,596 bytes | none |

The logic is 113 functions and about 14.8 KB in all. 82 more are eight-byte adjustor thunks, which stay: they jump into the detours. This piece takes the first two rows (39 functions: 13 + 5 for the decal, 16 + 5 for the effect). Still the game's after it: the automata controller and the burnt building's logic (25 functions), the text decal (28), the sign's logic (20), and all drawing.

cSC4PylonOccupant (class 0x49B6D69B, constructor 0x005F1730, code 0x005F0820 to 0x005F2A8D) lies in the same range but derives from the occupant base, not from the prop. It is not part of this family.

## The classes

Both objects have an interface of their own at +0 and hold the whole prop (0x68 bytes, [propocc.md](propocc.md)) at +4. The parts after it lie where the prop's lie, four bytes on: +4 the prop interface (24 slots, the prop's), +8 the message target, +0xC the occupant (the base starts here; 34 slots), +0x10 the serializable, +0x14 the property holder, +0x28 the exemplar holder. The constructors call the prop's constructor 0x005EE050 on object + 4 and then store seven vtables of their own. Most slots are the prop's and the base's functions or eight-byte thunks that adjust `this`. The class factories and the prop manager hand out object + 4, the prop interface.

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

### What follows frames and the view, not game days

- Base flag 2 of both (the occupant's flags at object + 0x3B). SetAllFlags (occupant slot 30) acts on every change of it. What changes it through that slot in play was not traced (the prop's own SetPlaceableFlag writes the byte directly); AddCityProp alone does not set it, and in the test cities nearly every effect prop has it set. With it come the decal's overlay (+0x9C) and ID (+0x6C), made when it is first set, and the effect's fresh byte.
- The decal's fade. The frame service's message 0x86AD10EE carries the frame's time in milliseconds; the fading opacity, the shown opacity and the end of the fade follow it.

The rest is plain state: the decal's texture, size, centre and fade settings; the effect's name, ID, property counts and started byte.

## What is ours

All four groups are off by default. They call the prop's functions at their addresses.

| Replacement | What is ours |
| --- | --- |
| `simulators.propkinds.decal` | 13 detours: QueryInterface, SetDecalTexture, SetOpacity, SetOpacityFade, SetDecalSize, DoMessage, Init, Shutdown, SetPosition, SetAllFlags, Write, Read, GetGZCLSID |
| `simulators.propkinds.decal_object` | our own decal: detours on the constructor, the pool's allocator, the factory, the destructor body and the deleting destructor, with copies of the seven vtables taken at the first construction (after every other group's slot swaps) |
| `simulators.propkinds.effect` | 16 detours: QueryInterface, the two SetEffectType, StartEffect, StopEffect, SetOrientation, SetState, UpdateTransform, MakeEffect, Init, Shutdown, SetPosition, SetAllFlags, Write, Read, GetGZCLSID |
| `simulators.propkinds.effect_object` | the same five for the effect |

Two address entries were renamed: 0x005EE360 and 0x005ED6E0 were SC4CraterProp::Construct and ::Allocate, and are now cSC4PropOccupantTerrainDecal::Construct and ::Allocate.

Still the game's: the eight-byte adjustor thunks (they jump into our detours), the prop's and the base's code the two classes call, the shared stubs in the effect's slots 3 and 4, the overlay manager, the effect and the effects manager, the pools' page code (0x0090CF9A and neighbours), and the other four classes of the family.

Callers outside the class: the class registration (0x0046078B to 0x004607A2 calls each GetGZCLSID and keeps each factory's address, before any replacement is installed), and four places that call an allocator and then the constructor: the prop manager (0x004A256A / 0x004A2578 and 0x004A30B7 / 0x004A30C5 for the effect, 0x004A258B / 0x004A2599 and 0x004A30D8 / 0x004A30E6 for the decal), the terrain code (0x0075EE44 / 0x0075EE52 and 0x006CC3B1 / 0x006CC3C3, decals) and 0x005CBC1E / 0x005CBC2C and 0x005CECC5 / 0x005CECD3 (effects). Each reads only EAX after either call, so the detours are plain functions. No wrapper of the `game.*` kind touches any of the 39 addresses.

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
- Where the game leaves stack leftovers (the overlay manager's 0x24-byte info block, past its first three dwords; two bytes of the effect's 0x38-byte transform block) ours zeroes them.

Version 3 is the save record of both: the prop's record, then the class's fields in order. The decal saves the overlay ID as it is, the key, the size, the centre, the opacity, the fade's three values, the action and two bytes. The effect saves the started and fresh bytes, then the name and ID as they are when no property gave them, else an empty string and ID 0. Read refuses any other version. When the prop's Read fails the decal's Read answers false; the effect's reads nothing more and answers whether the stream is free of errors. The property counts are not saved: Init counts them again.

## Tests

`OPENSC4_TEST_PROPKINDS=<day>` (fingerprint token ` propkinds <hash>`, log lines `autotest: propkinds:`). Level 1 reads only. It walks the occupant manager's grid for occupants of type 0xA823821E whose class ID is one of the two, and counts them and sums and XORs a hash of each one's plain state. Then it makes props it never inserts: one of each class from the allocator and constructor (as the terrain code makes a decal), and up to two of each from the prop manager's CreateProp when its tables have types of the class. On each it calls QueryInterface (the IDs the class takes and one it does not), GetGZCLSID and the setters that only write fields while there is no overlay and no effect (for the decal SetDecalTexture, SetOpacity, SetDecalSize, SetPosition and SetAllFlags with another flag; for the effect StopEffect, StartEffect, SetOrientation, SetPosition, SetState, SetEffectType with the ID it has), hashes what they left, and releases the prop. Left out of the hashes because frames or the view drive them: the overlay's ID and manager, the base flags, the fresh byte of city effects, every pointer.

`OPENSC4_TEST_PROPKINDS_EDIT=1` changes the city and puts it back. At a spot with no lot whose 64 m occupant cell is empty it initializes a decal, adds it, shows it (base flag 2 through SetAllFlags, as the view does), fades it from 1 to 0.25 with SetOpacityFade and six 500 ms frame messages the test makes (action 0; the fade's values are on the log line), hides, shows and removes it; the removal's Shutdown takes it off the frame service again, and the test checks that. Then it adds an effect prop, switches its states, shows it, stops, starts and turns it, gives it a name and an ID no effect has and the name "off", and removes it. The counts are taken again and must be the same; the log says `restored yes`. `OPENSC4_TEST_PROPKINDS_EDIT=2` does the same and then leaves a decal (no fade) and an effect prop in the city, so the run's save holds them and a run started from that save reads them back. The tutorial does not hold any of this back; `OPENSC4_TEST_TUTORIAL_OFF` is not needed.

The test never calls Write, Read or a destructor: the save at the end of every run and the load of a save reach the first two, Release the others. Not tested at all: fade actions 1 and 2.

## What the test cities hold

None of the three tutorial cities (Making Money d0090, Big City d0000, Rush Hour d0090) holds a single terrain decal, and the prop tables have 0 decal types and 89 effect types. The cities hold 53, 262 and 113 effect props. So the decal is carried by props the test makes.

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
