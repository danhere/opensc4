# Audio code

The game's own sound code, rewritten piece by piece like the simulators ([parity.md](parity.md)). Build 641 addresses throughout. The range is 0x004E1320 to 0x004FA6A0 (about 103 KB); what follows 0x004FA6A0 is not audio code. Entries are in `data/addresses/audio.toml` (placeholder names `SC4Audio::F<address>`, a few named ones). The code is in `src/core/audio_1.cpp` to `audio_9.cpp`, one group each (`audio.part1` to `audio.part9`), all in the subsystem `audio` and all off by default.

"Ours" means rewritten here and hooked at the original's entry. "The game's" means left alone and called by address. Whatever is heard is the business of EA's sound library, which stays the game's (see the fence below): this page is about the code that decides what to ask of it.

In total 575 functions are ours (573 under audio names, 2 under UI names: the options dialog's maker 0x004F2450 and its run function 0x004F25F0). The other functions of the range are listed under "What is fenced off" and "Not done".

## The pieces

| Piece | Code | State |
| --- | --- | --- |
| The sound event system, `cSC4AudioAEMSEventSystem` (0x004E1320 to 0x004E2570), its sound bank, volume and tuning | 43 functions | ours (`audio.part1`) |
| The audio conditions (0x004E2570 to 0x004E3F80): 74 one-function classes, each a filter on occupants, cells or the city's state | 74 functions | ours (`audio.part1`) |
| Eight ambience filters by building purpose (0x004E4040 to 0x004E4A60) | 8 functions | ours (`audio.part2`) |
| `cSC4AudioAmbienceLayer`, first half (0x004E4A60 to 0x004E6C10): the places, the measure, the volume and gain, the sounds and emitters | 28 functions | ours (`audio.part2`) |
| `cSC4AudioAmbienceLayer`, second half (0x004E6C10 to about 0x004E82B0): constructor, destructor, Init, Shutdown, message handler, tuning, the shared filter table, freshness scores | 17 functions | ours (`audio.part3`) |
| `cSC4AudioAmbienceManager`, first half (0x004E82B0 to 0x004E9810): QueryInterface, Init, Shutdown, occupant messages, sounds at hours of the day, tuning, the view rectangle, the sweep, its occupant filter | 25 functions | ours (`audio.part3`) |
| `cSC4AudioAmbienceManager`, second half (0x004E9810 to 0x004EAF20): city start and end, occupant and lot sounds, timed sounds, the tick, sound events, constructor and thunks | 29 functions | ours (`audio.part4`) |
| The audio COM director (0x004EB080 to 0x004EC3D0): eight factories of the large classes, 65 factories of event handlers, its start, the hooks after the application's start and before its shutdown, its destructor | 79 functions | ours (`audio.part4`) |
| The director's small classes (0x004EC3D0 to 0x004ECDA0): eighteen further factories, the classes' QueryInterface and Release, the director's constructor and deleting destructor | 25 functions | ours (`audio.part5`) |
| The three sound kinds and their holder (0x004ECDA0 to 0x004EDD30) | 23 functions | ours (`audio.part5`) |
| `cSC4AudioEventHandler`, first half (0x004EDD30 to 0x004EF010): QueryInterface, Release, stop, the music starter, four message cases | 13 functions | ours (`audio.part5`) |
| `cSC4AudioEventHandler`, second half (0x004EF010 to 0x004F0250): pointer sounds, the sound service's events, the click outside the front window, constructor, destructor, both message handlers, Init | 19 functions | ours (`audio.part6`) |
| `cSC4AudioEventHandlerDisasterFire` (0x004F0250 to 0x004F08F0) | 21 functions, 12 of them thunks | ours (`audio.part6`) |
| The cluster set (0x004F08F0 to 0x004F1330) | 15 functions | ours (`audio.part6`) |
| `cSC4AudioListener`, first part (0x004F1330 to 0x004F1A80): QueryInterface, Release, constructor, destructor, Init, Shutdown, the camera update, the height table | 9 functions | ours (`audio.part6`) |
| `cSC4AudioListener`, last part (0x004F1A80 to 0x004F1F70): the tuning reader, the city message handler, DoMessage | 3 functions | ours (`audio.part7`) |
| The music lists and the audio options dialog (0x004F1F70 to 0x004F511D, with the dialog's Init at 0x004F5120 in part 8): the lists' reading, the switches written to the settings, the post to the music player, the dialog's maker, run function, window procedure, Init, Shutdown, track rows and buttons | 24 functions in `audio.part7` (two of them under UI names), 1 in `audio.part8` | ours |
| `cSC4AudioScapeManager` (0x004F5200 to 0x004F7300; the dialog function above is not counted): making, Init, Shutdown, the interface methods, the cluster's methods, the scape methods, OnTick, AddPoint, Collect | 47 functions | ours (`audio.part8`) |
| The scape manager's last four methods (0x004F7300 to 0x004F7B70) | 4 functions | ours (`audio.part9`) |
| The mover and the fade (0x004F7BD0 to 0x004F8790), `cSC4AudioSoundManager` (to 0x004FA6A0) and its voices | 68 functions | ours (`audio.part9`) |

Function counts per group, counted from each file's hook table: part1 117, part2 36, part3 42, part4 108, part5 61, part6 64, part7 27, part8 48, part9 72. Total 575. The splits inside a group are by address and approximate at the seams.

## What is fenced off

- **EA's sound library (AEMS)**, 0x00864D10 to 0x00867A00, and its pointer table of eight functions at 0x00B107D8 to 0x00B107F4. Never rewritten. The event system calls it as the original does.
- **The framework's sound service** (service 0x23CECBA0, interface 0x106077D2, vtable 0x00AD7180) and **the music player** (service 0x1D07B39F, interface 0xE0510260, vtable 0x00ACC310). Only called. The ambience layers, the scape manager and the sound manager talk to the sound service by messages: 0xEA451903 starts a sound, 0xCA45190F places it (whole numbers), 0x6A451908 stops it, 0x4A451915 sets its level.
- **The camera's code**, which belongs to the renderer work. The camera-to-view helper 0x007F78E0 and the view's getter of zoom and rotation 0x007F66A0 are called by address.
- **Code folded with other classes, and generic container code.** The files name these and give the reason:

| Function | Why it stays |
| --- | --- |
| 0x004E13E0 (returns 0) and 0x004E2410 (returns its argument) | callbacks given to the sound library; three bytes each, shared by code folding with many classes |
| 0x004E3F80 | assignment of a map with 0x18-byte nodes; generic container code whose callers are in `cSC4Lot` |
| 0x004E4040, 0x004E40A0, 0x004E41F0 | the building profile's assignment and the copy of its vector of 12-byte records; another subsystem's container code (0x004E40A0 is also called from 0x0069C098) |
| 0x004E4BB0 | `SC4RefPtr::Copy`, called from all over the game |
| 0x004E76A0, 0x004E82A0 | getters held by vtables of other classes too |
| 0x004E8380 | overlap of two rectangles, also called from 0x0076237C |
| 0x004ECD60 | `SC4Sound::StaticObject`; makes the director on first use |
| 0x004ED100, 0x004ED130, 0x004EDF00, 0x004EDFD0 | reference holders' constructors and destructors shared with the framework |
| 0x004ED900 | returns the dword at +0x20; folded with the same getter of twenty unrelated classes |
| 0x004EF7F0, 0x004F0240 | AddRef thunks held by other classes' vtables |
| 0x004F0940 | `SC4Uint32HashSet::Resize`, the whole game's |
| 0x004F1F30, 0x004F2020, 0x004F20A0, 0x004F20C0 | a QueryInterface shared with nine other window procedures; reference holders' helpers called from everywhere |
| 0x004F2720 | constructor of one music list record; only the static initialiser at 0x00A72520 calls it, before any hook is in place |
| 0x004F5290, 0x004F52D0, 0x004F5660, 0x004F5670, 0x004F59B0, 0x004F59C0 | block builders and item frees of two pool templates |
| 0x004F5310, 0x004F5D50, 0x004F63B0, 0x004F6DF0 | vector code (the first two are called by the renderer's code too); fill and resize of the cell vector |
| 0x004F5A00 | `SC4RefList::Construct`, called from all over the game |
| 0x004F7C10, 0x004F82A0 | the pool functions another class's pool at 0x00AA2CE0 has in its vtable too |
| 0x004F88C0 | AddRef thunk, also slot 1 of the vtable 0x00A93758 |
| 0x004F7BA0, 0x004F7BB0, 0x004F8D30 | `SC4::AddRefAt10`, `SC4Sound::NextInstance` and `SC4Uint32HashMap::Lookup`, named by other work |

Four functions of the options dialog's range take an argument in a register and are called only from functions that are ours: 0x004F2140 and 0x004F21B0 (a child window's flag 2, read and set; the window in ECX), 0x004F27A0 (the files of a folder; the list in EDI) and 0x004F31C0 (whether a folder has files of its own; the index in ECX). They are written out for those callers and not hooked.

Several addresses in the lists are labels inside a neighbouring function, not functions (for example 0x004E22A0, 0x004E7050, 0x004EEF80, 0x004F4C20, 0x004FA6A0 onward); the file headers name them.

## The classes

The audio director is a COM director (ID 0xFDB5CA17, vtables 0x00A93E18 and, at +4, 0x00A93DE8). It is a static object at 0x00B21978, made on first use by `SC4Sound::StaticObject` (0x004ECD60). Its constructor (0x004EC710) registers 92 class IDs. It holds seven audio objects at +0x4C to +0x68:

| Offset | Class ID | What |
| --- | --- | --- |
| +0x4C | 0xBDB6171E | `cSC4AudioEventHandler` |
| +0x50 | 0x69E500BC | `cSC4AudioEventHandlerDisasterFire` |
| +0x54 | 0x6A12F51C | `cSC4AudioAmbienceManager` |
| +0x5C | 0xAA355871 | the holder of the sound kinds |
| +0x60 | 0x4A47DC0D | `cSC4AudioSoundManager` |
| +0x64 | 0xCA4A66B9 | `cSC4AudioListener` |
| +0x68 | 0x29E2797F | `cSC4AudioScapeManager` |

The full field layouts are in the header comments of the source files. What follows is the short form.

### Sound event system

`cSC4AudioAEMSEventSystem`, 0x150 bytes, constructor 0x004E1C20, system service 0xAC1FAE31, made by the application at 0x004511BF. Five parts: +0 the sound service's message target (vtable 0x00A93284, interface 0xBB523BB2; slot 3 takes the master volume message 0x2A5B69C0), +4 the event interface (0x00A9325C, 0x8C1FAEB7: load and unload a bank, start and stop an event, set the library's three channel volumes), +8 a message target for the view-changed message 0xA6B79602 (0x00A9324C), +0xC the system service base (0x00A93218; Init is slot 8, Shutdown slot 9), +0x10 the base's last part (0x00A93200). It keeps the handles of the running events (21), the loaded banks and the numbers the master volume is made of: the player's volume, the tuning's scale and a volume by zoom, multiplied, sent through a curve and scaled to 0..127.

A sound bank is 0x24 bytes (vtable 0x00A932EC): reference count at +4, its bytes at +8, its resource key at +0x14.

### Audio conditions

74 classes of three kinds, each a six-slot vtable whose last slot is the test. Occupant condition (interface 0xCA400753, 0x10 bytes, `Test(occupant)`), cell condition (0xCA400757, 0x14 bytes, `Test(x, z)`), state condition (0xEA40074F, `Test()`). The first two keep the last question and answer and give the kept answer when asked the same thing twice running.

### Ambience layer

`cSC4AudioAmbienceLayer`, vtable 0x00A934A8 with 29 slots, interface 0x4A2835FE, at least 0x25C bytes, made by the manager at 0x004E9C4C. One layer of the city's background sound: it collects the places of the occupants and cells its filters let through, works out a measure (how many, how near, the population, the zoom), turns it into a volume and a gain through three curves, and keeps its looping sounds and emitters in step by messages to the sound service. It is its own message target (slot 3, DoMessage; message 0xC53D10AA reads the tuning again, exemplar group 0xCA25875D). Three cluster sets sit at +0x28, +0x2C and +0x30; the follower that eases the middle and the value over time is at +0xE8 (code 0x004F7BD0 to 0x004F8790). The filters the layers share are in a hash table at 0x00B21958 from class ID to filter.

### Ambience manager

`cSC4AudioAmbienceManager`, 0x120 bytes, class and interface ID 0x6A12F51C, service ID 0xAA12F911, constructor 0x004EA390, factory 0x004EB0C0. Four vtables: +0 the system service (0x00A9378C, 14 slots; Init 0x004E84D0, Shutdown 0x004E96A0, OnTick 0x004EA150), +4 a message target (0x00A9377C, DoMessage 0x004EAF20), +8 the audio managers' start and stop interface (0x00A93768), +0xC an event target (0x00A93758, DoEvent 0x004E9870). It answers QueryInterface only for itself. On the city's start (message 0x26D31EC1) it makes the layers from the list of layer IDs at +0x60, takes the occupant manager, joins the framework's tick and looks at every building, prop and flora occupant once; the city's end (0x26D31EC2) undoes that. Occupant messages (0x99EF1142, 0x99EF1143, 0x99EF1146) and a lot state change (0xE99B3D4D) start and stop the sounds the occupants' properties name. Timed sounds are the list at +0x4C (0x28-byte nodes). The tick walks the city's tracts a slice at a time for the layers (state at +0xB0: 0 or 1 starts a walk, 2 walks, 3 finishes). Its tuning is exemplar instance 0x6A359C5C. Its occupant filter (0x00A93640) passes seven occupant types.

### Sound kinds and holder

Three kinds, 0x28 bytes each: "2D SC4K" (ID 0x7DCDED2D, vtables 0x00A94068 and 0x00A94054), "3D SC4K" (0x7DCDED3D, 0x00A940A4 and 0x00A94090), "3DEffect" (0x0A63A543, 0x00A940E4 and 0x00A940D0). Slot 5 works out a voice's level, side and distances from the sound's own values; slot 6 copies a fixed set of values. The holder (0x20 bytes, vtable 0x00A9403C, ID 0xAA355871) puts the kinds into the framework's registry of kinds (service 0x1B2CE671, vtable 0x00AD2968) in place of the framework's own 2D and 3D kinds, which it keeps to put back.

### Event handler and fire handler

`cSC4AudioEventHandler`, class 0xBDB6171E, 0x50 bytes, made by 0x004EB0A0. Vtables: +0 0x00A94164 (the sound service's hook, interface 0xBB523BB2), +4 0x00A94154 (an old-style message target that keeps the list of open windows that asked for clicks outside them), +8 0x00A94144 (the message target; 24 message types), +0xC 0x00A94130 (start and stop). It holds the music player (+0x10), the sound service (+0x14), and at +0x18 the object of service 0x49E50BA1 (see the notes on naming below). It plays the music, three looping pointer sounds whose pitch follows the pointer's speed, and the click sounds.

`cSC4AudioEventHandlerDisasterFire`, class 0x69E500BC, 0x34 bytes, made by 0x004EB100. It tells the object of service 0x49E50BA1 about every burning occupant, under the group "Fires".

The 65 other event handler classes are 0x10 or 0x14 bytes each (thirty of 0x10 bytes from 0x004EB260, thirteen and twenty-two timed ones of 0x14 bytes from 0x004EB800 and 0x004EBAA0), with one reference count and nothing else.

### Cluster set

0x28 bytes, vtable 0x00A94200, no class ID. Points are gathered into clusters by distance. +4 reference count, +8 a hash map from a point's key to its record (0x1C bytes), +0x1C the list of clusters (0x20 bytes each), +0x20 their number, +0x24 the radius a new cluster gets. Every ambience layer keeps three.

### Listener

`cSC4AudioListener`, class 0xCA4A66B9, 0x7C bytes, made by 0x004EB0E0, vtable 0x00A9421C (four slots, a message target). It keeps the place the sounds are heard from (+0xC), a point behind it, what the camera looks at, and four vectors of five floats by zoom (+0x3C, +0x48, +0x54, +0x60), and hands the place to the sound service. DoMessage is 0x004F1E50; the tuning reader is 0x004F1A80. The camera update is 0x004F14B0, the height table 0x004F18C0.

### Music lists

Two static records of 0x30 bytes at 0x00B21A88: list 0 for the region view, list 1 for the city, from the `Radio` folders ("Radio\Stations\Region\Music\" and "...\Mayor\Music\" under the data folder). Each holds up to 30 tracks read from an exemplar (group 0xCA25875D). A track is 16 bytes: the ID of its check box, the text instance of its file name, the text instance of its title (group 0x8A635D24), its switch. Free functions: 0x004F1F70 (a list by index), 0x004F2910 (reads a list's tracks), 0x004F2B70 (sets both up), 0x004F2D40 (does both once), 0x004F2580 (writes the switches to the settings), 0x004F3410 (posts a list's file names to the music player, message 0x0A611B34), 0x004F1F90 (a slider position as a volume), 0x004F22C0 (reads the volume limits).

### Audio options dialog

0x80 bytes, vtable 0x00A94264 with seven slots, made by 0x004F2450 and run by 0x004F25F0 from the region view (0x007B0218) and the city view (0x007F2CDE). Two volume sliders, switches for sound and music, a list box with a row and a check box per track, play and stop buttons, OK and cancel. +4 the window, +8 the list box, +0xC a byte for "the music player was told a preview is on" (message 0xAA650861), +0x10 the settings (0x00B43CD8), +0x14 a copy of the audio settings from when the dialog opened (0x68 bytes). Cancel puts the copy back. Init is slot 5 (0x004F5120).

### Scape manager

`cSC4AudioScapeManager`, class 0x29E2797F, interface 0x29E39DB4, 0x98 bytes, made by the factory 0x004EB120. Vtable 0x00A94340 (19 slots), the system service at +4 (0x00A94308, service ID 0x49E50BA1 at +0x80), a message target at +8 (0x00A942F8) that takes every message and does nothing. It keeps "scapes", named sets of sounding points in the city, in the list at +0x54, and clusters on their way out in the list at +0x58. Points near each other are gathered into clusters, each cluster sits at the average of its points, and every cluster plays the scape's emitters while its number of points is inside an emitter's range. A cluster is an item of 0x4C bytes in the pool 0x00B098A8. OnTick counts the leaving clusters down on a stopwatch and walks every cluster's two levels a seventh of the way to their targets.

### Sound manager

`cSC4AudioSoundManager`, class and interface ID 0x4A47DC0D, 0x80 bytes, constructor 0x004F9830, made by the factory at 0x004EB093; 0x004BB9E0 hands it out. Callers do not touch a sound: they ask the manager, which makes a message of the sound service (slot 13), posts it (slot 8), and gets it back at its slot 3 (0x004FA590), where the work is done. Vtables: +0 0x00A94428, +4 the sound service's tick (0x00A94418), +8 the "sound ended" callback (0x00A94408), +0xC a message target (0x00A943F8) for seven messages of the game's message server. +0x14 is the master level (0x400 is full), +0x34 a hash map of voices by instance, +0x48 the list of voices, +0x70 a curve from the number of voices of one sound to their level. Three limits at +0x64, +0x68, +0x6C (3, 10, 30).

A voice is 0x60 bytes of the pool 0x00B098D0: sound ID, instance number, the framework's sound, level, place, flags (1 level set, 2 place set, 8 out of earshot, 0x10 held by the paused simulation, 0x20 stops with the simulation), the clock at its start, near and far distances, a mute count and a fade. Voices of a sound whose parameter 0x33 is set are grouped: only the first plays, placed at an average of the group worked out at 0x004F8920 from the listener or the camera. A mover (0x3C bytes: a place and a level that walk to their targets at given rates) and a fade (now, target, rate) are shared with the ambience layers.

### Naming notes

- The event handler's +0x18 object and the fire handler's +0x20 object carry service ID 0x49E50BA1 and the vtable 0x00A94340. Those are the scape manager's (part 8). The comments of parts 5 and 6 call the object "the ambience manager". The ambience manager proper has service ID 0xAA12F911.
- The exe's own ID table calls the holder of the sound kinds (0xAA355871) `cSC4AudioAmbienceLayer`. It is not the layer (0x4A2835FE, vtable 0x00A934A8).

## Oddities in the original, kept

Each of these is reproduced as the original does it. They come from the comments in the source files.

- **The ambience manager's "first view" byte (0x00B09824).** A byte that is 1 at start. The first update of the view rectangle (0x004E92A0) sets the earlier cells equal to the new ones and clears it (`audio_3.cpp`). None of the nine files sets it again, so it is a once-per-process switch.
- **Timed sounds over midnight.** A sound that lasts between two hours, where the second is earlier than the first, is tested in the morning part by comparing `now - 24` with the two hours (`audio_3.cpp`, near line 1366). From reading the code, not from a test: for a start hour of 0 or more that comparison cannot succeed, so only the evening part of such a span can sound.
- **A cluster's founding point is not in its member list.** A point that began its own cluster has no cluster recorded, and removing it (0x004F0AC0) reads through that null. Ours does the same. The test keeps clear of it (`audio_6.cpp`, `audio_autotest.cpp`).
- **`VoiceSendPlace` (0x004F9050) compares the sound kind with 0x7DCDED3D** for the listener, where two other places in the sound manager use 0x7DCDED2D (`audio_9.cpp`, near line 1148).
- **The options dialog takes 31 check box IDs for each list of 30 tracks** (`audio_7.cpp`, near line 1325).
- **A scape cluster is averaged over the count before the count goes down by one** when a point moves out of it (`audio_9.cpp`).
- **`ClearScapes` (0x004F6620) never leaves its loop without the sound service** while a cluster needs to go (`audio_8.cpp`).
- **A failed allocation in `LookAtOccupants` (0x004EAAE0) is followed by a read through null** (`audio_4.cpp`).
- **Answers kept in the low byte of an argument.** Several tests (for example 0x004E2A40) put their answer where their argument was; without the property they read the low byte of the occupant's address (`audio_1.cpp`). The network conditions ask the zone developer twice and return the second answer, keeping the first (`audio_1.cpp`).
- **Small leftovers.** The building-purpose filter leaves one field of the profile it builds (+0x2C) unset; nothing reads it (`audio_2.cpp`). The layer keeps a distance in a local nothing reads. The sound manager's third limit (+0x6C) is read and never used. The listener's third factor array is unused.
- **Exception frames.** 0x004EDF50 has one that swallows what the old object's Release throws; ours is plain C++.

## How it is tested

Both tests are in `src/core/audio_autotest.cpp`. They call the game's audio functions by address, so a run with the `audio` replacements on and one with them off can be compared, and what they leave goes into the daily fingerprint line. Neither test starts or stops a sound.

`OPENSC4_TEST_AUDIO=<day>` adds the token ` audio <hash>`. The hash is made of ten sub-hashes, which are written to three log lines that begin `autotest: audio:` (the first has mover, fade, clusters and handlers with their step counts; the second ambience, private, sound with the master level, listener and music; the third conditions with their call counts). Compare the sub-hashes to see which part parted. What each does:

| Section | What the test calls and compares |
| --- | --- |
| mover | the mover on a buffer of the test's own: made, placed, rates and level set, stepped once on the spot, given a place and a level to walk to, stepped by different times (three cases) |
| fade | the fade stepped until it is there and once more |
| clusters | a cluster set: points under keys 1 to 20 that join one of five centres or begin their own, joiners moved close, to another cluster and far away, points taken out, a missing key, a second set and the merges, clear, drop empty clusters, add again, free. The state of the set (clusters, map, per cluster the count, peak, place and radius) and the nodes `Collect` makes are hashed |
| handlers | the 65 event handler factories (30 of 0x10 bytes, 13 and 22 of 0x14): the object's fields, its reference count, QueryInterface for an unknown ID and for 0x4A40074C and 0xCA400757, and the final Release |
| conditions | 82 condition handlers (30 occupant, 36 cell and 16 state conditions) made through their factories: Init, QueryInterface for the condition interface, the test function (slot 5), Shutdown and the last Release. The test function is called for a sample of up to 300 of the city's occupants (taken by walking the cells in a fixed order), for a grid of up to 1,000 cells, and once for the state conditions. The low bytes of the answers are hashed; a city gives about 25,000 to 33,000 calls. One handler is left out (factory 0x004EB6B0, function 0x004E2A40): without the property it tests, the original returns the low byte of the occupant's address |
| ambience | the live ambience manager read, read again, and read after its tuning is read (the layers' with their own instance, the layers' freshness scores, the manager's, 0x004E91D0). Per layer: its fields, filters, vectors and curves; the filter table at 0x00B21958 bucket by bucket |
| private | a second ambience manager made by its factory (0x004EB0C0), its fields, lists and one Release |
| sound | the live sound manager's limits and curve, then 0x004F9250, then the same again. The master level (+0x14) is logged before and after (` level <before>/<after>` on the second line) and not hashed: a posted message sets it and the sound service delivers that in real time, and the section's hash was two-valued in the original while the level was included |
| listener | the live listener's four vectors and two floats, the height table at 0x00B21A20, with the tuning reader (0x004F1A80, only if both vectors already hold five floats) and the height table (0x004F18C0) called in between |
| music | 0x004F2D40, both music lists (index, ID, exemplar instance, the tracks, the file name list), lookup by index (-1, 0, 1, 2), the volume tuning (0x004F22C0) and the slider conversion 0x004F1F90 at five positions for both lists |

`OPENSC4_TEST_AUDIO_DIALOG=<day>` adds the token ` audiodlg <ran> <hash>` and one log line `autotest: audio dialog driven:`. It makes the options dialog (0x004F2450), calls Init, runs it with a player in place of the modal loop, and lets it go. The player hashes what the dialog holds when it is up (the list box, the 0x68 bytes of saved settings), presses the leave-out switch (button 0x8A550C5A) through the window procedure and reads it back, then presses cancel, which puts the settings back. The test puts the leave-out byte back by hand at the end. Nothing is accepted and nothing is saved.

Both run on one day of the run (hooks `audio` and `audio.dialog`) and need build 641.

### What the tests cannot compare

- **Playback itself.** It is real time and goes to the sound library. Nothing that is heard is compared, and nothing that starts or stops a sound is called by the tests.
- **The tick (0x004EA150) and the sweep (0x004E94E0).** They run on stopwatches, which are real time. What a layer holds after a tick depends on how many ticks the run had.
- **Voices.** They end in real time. The sound manager's clock is the sound service's tick, which is real time, and so are the fades, the 50 millisecond rule for a sound started twice and whether the framework says a sound still plays. The test hashes only the limits and the curve; the level is logged.
- **The listener's position.** It follows the camera, frame by frame, so it is left out of the hash.
- **Pointer sounds.** They use the time stamp the event carries, which is real time as the window system gives it.
- **The scape manager's leaving clusters.** OnTick counts them down on a stopwatch, so when a cluster goes is real time.
- **The director's start-up code.** The factories, the constructors, the Init of the seven objects and the hook 0x004EBF40 run before the replacements are installed at PostAppInit. In a run the objects therefore come from the game's code, and our versions are reached only where the test calls them directly (the handler factories, the private ambience manager, the dialog) and at shutdown. This also holds for the director's constructor (0x004EC710), which runs once, when `SC4Sound::StaticObject` is first called, long before any hook.
- **Hearing.** Whether a sound is the right one, at the right volume, in the right place and at the right time can be judged only by a person at the machine.

## Parity

Batch `au1` (2026-10-11) ran every group pushed so far plus `audio.part1` to `audio.part9` on side B, 108 groups in all, for 7 game days per save. The verdict goes by outcome sets, as in [parity.md](parity.md).

| Leg | Saves | Test switches | Result |
| --- | --- | --- | --- |
| plain | 12 | none | 11 same on the first pair; Big City d0000 partly after retries |
| audio | 12 | `OPENSC4_TEST_AUDIO=3`, `OPENSC4_TEST_AUDIO_DIALOG=4` | 10 same on the first pair; Big City d0000 and d0365 partly after retries |
| plop | 3 (`saves-plopped`) | `OPENSC4_TEST_AUDIO=3` | 3 same |

No run log of the 76 shows a crash, a watchdog or a timeout; every run ended normally.

### Where the partly saves part

- **Big City d0000, both legs.** The original's own runs part on fingerprint line 2, in `citylot` (two outcomes). The first B run also has other `flam` numbers from line 2 (sum 9483/180, cells 31, against 8986/172, cells 29). This is before the sound test runs (day 3; its token first shows on line 5), and it is the first-pair noise of that save known from earlier batches (see parity.md, load noise).
- **Big City d0365.** The only difference of the first B run is the tail of the fingerprint log line. The log cut a line at 4,093 characters (plus the line end); the number in the log prefix is the thread ID, not the process ID, and it had five digits where the others had six, so one more character of the last part (`checks`) was visible. The log now writes long lines whole, so that cut no longer happens from this change on. No named part differs. The cut also means that the last parts of the line, `sim` and `checks`, are partly outside the log comparison on days where the line is that long. That is a property of the harness's logging, not of the sound code.

### The test's tokens

In the au1 audio leg, 11 of 12 saves have one ` audio` token on both sides (it differs from save to save). ` audiodlg 1 AC67D7CF` is in every run of both sides. In the plop leg every B token is among the original's.

### Batch au2

Batch `au2` (2026-10-11) ran the same 108 groups and the same legs (audio and plop) after the conditions section and the logger fix (c2bdf34) were in. First pair: 11 of 12 saves same in the audio leg and 2 of 3 in the plop leg. No crash, watchdog or timeout in the 40 run logs, and every fingerprint line is complete (the longest is 4,067 characters without the prefix).

- **Big City d0000.** Partly after three pairs. The original's first run and the first B run each stand alone, parting on fingerprint line 2 (`citylot`, `flam`), before the test runs; the other four runs agree.
- **saves-plopped p0000.** Partly. The first B run differs from the other five runs only in the sound test's token, in the sound manager section only.
- **Conditions.** The section agrees between the original and ours on all 15 saves. For example Big City d0000: 33,352 calls, 5,120 true; Making Money d0000: 25,582 calls, 2,968 true; Rush Hour d0000: 25,552 calls, 3,167 true.

### The exception

The test has a rare second outcome in which two sections change together, the ambience manager's and the sound manager's (sound sub-hash 48CEBA38, where the usual one is 86832618). With both batches it was seen six times: three times in the original (a trial run on Big City m0010, saves-plopped p0000 in au1, Big City d0090 in au2) and three times with our code (Big City d0000 in au1 and in au2, saves-plopped p0000 in au2). It was mostly the first run of a series. In two of the six the ambience manager's section changed with it. So the original has both outcomes. Twelve further runs of the original on Big City d0000 and thirteen on m0010 gave only the usual value, so on Big City d0000 the second one was seen only with our code on; on other saves it was seen in the original. The cause is not found. The sound manager's fields were its master level (+0x14), three limits and the curve. The master level is set by a posted message that the sound service delivers in real time, which is the likeliest source. After au2 the level was taken out of the hash and is logged instead. Whether that removes the second outcome is not yet known from a batch, and the ambience section's rarer change is not explained.

### What this batch shows and what it does not

The parts the test compares agree between the original and ours on every save, apart from the outcome above: the mover and fade arithmetic, the cluster set, the 65 handler factories, the tuning readers of layer, manager, sound manager and listener, a private manager's construction, the music lists and volume tuning, and the options dialog opened, filled and cancelled.

The sound code that runs by itself during these days (city start, occupant and lot sounds, the tick, voices) ran as ours on side B without a crash and without changing any other part of the fingerprint. Nothing it does is in the comparison beyond that.

### Coverage

Three coverage runs of the original with both test switches on (Big City d0000 for 7 days, the same with a fire disaster for 10 days, Rush Hour d0000 for 7 days; results in `analysis/cov-audio`) entered 334 of the 575 functions.

| Group | Functions | Entered |
| --- | --- | --- |
| `audio.part1` | 117 | 22 |
| `audio.part2` | 36 | 20 |
| `audio.part3` | 42 | 28 |
| `audio.part4` | 108 | 87 |
| `audio.part5` | 61 | 43 |
| `audio.part6` | 64 | 36 |
| `audio.part7` | 27 | 20 |
| `audio.part8` | 48 | 28 |
| `audio.part9` | 72 | 50 |

The fire run adds 15 functions of the scape manager. What was not entered:

- The condition classes (0x004E2570 to 0x004E3F00) and the seven filters of part 2 that work the same way: 81 functions. Nothing called their tests in these three runs. The conditions section of the test now enters them (not re-measured; expected 81 fewer unentered functions, less the one skipped).
- Start-up code, which the meter cannot see (it is armed after start-up) and which a run does not reach as ours either: the event system's bank loading, constructors and Init of the seven objects, the director's own functions.
- Code that needs a person or a real event: pointer sounds, clicks outside a window, the tool, occupant and placed-sound messages of the event handler, the play and track functions of the options dialog, the sound manager's ask and let-go calls.

## Open: runs part from the original after a dispatch

Not solved. Found on 2026-10-13 by the test of the dispatch tool ([tools.md](tools.md), "The dispatch, sign removal and Life tools: test"), the first test that makes a dispatch.

What is seen: with `audio.part5` or `audio.part6` ours (either one alone is enough), a run parts from the original at the first fingerprint line after a fire unit is dispatched. On Making Money d0090 and Rush Hour d0090 the parts `weather`, `adv`, `zone`, `terrainobj` and `powerpoleocc` differ; on Big City d0000 only `adv` does. Afterwards the view differs too: the next day's ground-pick grid finds 256 of 272 points on one side and 126 on the other. The outcome is stable in ours (the same in repeated runs).

What it is not: it is the same when the game's own dispatch tool makes the dispatch, so it is not the tools' code. With both audio groups off the runs are identical.

Not explained. Leads:

- The dispatch messages 0xE94671EB and 0x894671F6 reach the handler at 0x004EE300 (`OnToolMessages`, src/core/audio_5.cpp) through the message dispatcher in src/core/audio_6.cpp. The handler was compared with the disassembly again without finding a slip.
- Either half alone gives the difference, so the candidates are the message registration list (part 5), another case of the dispatcher (part 6), or the random draw of the sound made for a dispatch.

Repro, about 40 s a run: the tools test with `OPENSC4_TEST_TOOLS3=2 OPENSC4_TEST_TOOLS3_COMMIT=1 OPENSC4_TEST_TUTORIAL_OFF=1` on Making Money d0090, the original against a run with only `audio.part5=ours`.

Until it is fixed, parity legs that dispatch run without those two groups or with `OPENSC4_TEST_TOOLS3_SEND=0`.

## Not done

- The functions in the fence table above that are shared with other classes or are generic container code. They could become ours only together with their other users.
- The sound library (AEMS) and its pointer table. They are EA's code; a replacement would be a new sound engine, not a rewrite.
- The framework's sound service and the music player.
- Playback checks. Everything above is compared by state; nothing here shows that the rewritten code sounds the same.
- The director's start-up code is ours but is not run in a normal test: the game's versions have already run by PostAppInit (see above).
- Tracks and sounds that are not the game's. New sounds and soundtracks are on the [roadmap](../ROADMAP.md) as an idea only and are not started.
