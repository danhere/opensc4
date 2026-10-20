# Audio code

The game's own sound code, rewritten piece by piece like the simulators ([parity.md](parity.md)). Build 641 addresses throughout. The range is 0x004E1320 to 0x004FA6A0 (about 103 KB); what follows 0x004FA6A0 is not audio code. Entries are in `data/addresses/audio.toml` (placeholder names `SC4Audio::F<address>`, a few named ones). The code is in `src/core/audio_1.cpp` to `audio_9.cpp`, one group each (`audio.part1` to `audio.part9`), all in the subsystem `audio` and all off by default.

"Ours" means rewritten here and hooked at the original's entry. "The game's" means left alone and called by address. Whatever is heard is the business of EA's sound library, which stays the game's (see the fence below): this page is about the code that decides what to ask of it.

In total 575 functions are ours (573 under audio names, 2 under UI names: the options dialog's maker 0x004F2450 and its run function 0x004F25F0). The music player's first two pieces add 62 (`audio.music.part1` 42, `audio.music.part2` 20; see "Music player"). The other functions of the range are listed under "What is fenced off" and "Not done".

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
- **The framework's sound service** (service 0x23CECBA0, interface 0x106077D2, vtable 0x00AD7180). Only called. **The music player** (service 0x1D07B39F, interface 0xE0510260, vtable 0x00ACC310) is being rewritten in pieces of its own (see "Music player" below); the audio code only calls it. The ambience layers, the scape manager and the sound manager talk to the sound service by messages: 0xEA451903 starts a sound, 0xCA45190F places it (whole numbers), 0x6A451908 stops it, 0x4A451915 sets its level.
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

## The runs that parted after a dispatch: not the sound code

Found on 2026-10-13 by the test of the dispatch tool ([tools.md](tools.md), "The dispatch, sign removal and Life tools: test"), the first test that makes a dispatch; explained and closed the same day.

What was seen: with `audio.part5` or `audio.part6` ours (either one alone was enough), a run parted from the original at the first fingerprint line after a fire unit was dispatched and called off. On Making Money d0090 and Rush Hour d0090 the parts `weather`, `adv`, `zone`, `terrainobj` and `powerpoleocc` differed, on Big City d0000 only `adv`.

The cause is in the game's tutorial system, and the sound code only moved it:

- The message for a dispatch made (0xE94671EB) is posted, so it is delivered after the tick that made the dispatch. The test calls the dispatch off in that same tick. When the message comes there is no such dispatch any more.
- The event handler here is one receiver of that message (0x004EE300, `OnToolMessages`); the tutorial system is the next (handler 0x00444480). It asks the dispatch manager for the dispatch's record (GetDispatchInfo, 0x00469D40, the call at 0x004447A6) and does not test the answer. The manager leaves the caller's record alone when it finds nothing, and the tutorial system's record is a local it never set. It takes the position out of it, and when that cell lies in the rectangle of the tutorial's step it moves the tutorial on a step, which sets the view's zoom.
- So the position is whatever the receiver before it left on the stack. With the game's sound code those bytes are a place outside the step's rectangle. With ours (our `OnToolMessages` under the game's dispatcher, or our dispatcher calling the game's function: the stack is used differently either way) they are zeros, the map's corner cell, which a step with no rectangle set takes for its place. The tutorial moved on, the zoom went from 0 to 1, and the parts that differed are the ones that follow the zoom.

How it was found: the hooks of `audio.part5` were halved until the one on 0x004EE300 was left; that function and the dispatcher's cases were compared with the disassembly again and are the same; a trace line added to the function made the difference go away; a list of the return addresses on the stack at the view's SetZoom showed the tutorial system's handler calling it with the dispatcher's return address still below. Of the five callers of GetDispatchInfo in the game, the tutorial system's is the only one that does not test the answer.

Nothing in the sound code was changed. The fix is a switch for test runs, `determinism.dispatch_info` (src/core/dispatch_info.cpp, [experiments/determinism.md](experiments/determinism.md), "A dispatch called off before its message"): a record that is not found is cleared and given a place off the map. It is in the base list of scripts/run-parity.sh for both sides.

Parity (batch aud9, 7 days, the full list of groups with `audio.part5` and `audio.part6` in it, the switch on both sides): with the dispatch tool's test and its commits (`OPENSC4_TEST_TOOLS3=2 OPENSC4_TEST_TOOLS3_COMMIT=1 OPENSC4_TEST_TUTORIAL_OFF=1`) 12 of 12 saves identical and 3 of 3 on the plopped corpus, where the batch before had 0 of 12; plain 12 of 12; all on the first pair. Single runs on Making Money d0090, Big City d0000 and Rush Hour d0090: the original with and without the switch, and `audio.part5`, `audio.part6` and both with it, have one fingerprint per save; `audio.part5` without the switch has another.

## Music player

GZSound's music player, 0x008D2994 to 0x008D7FFF (144 functions, [uncharted-code.md](uncharted-code.md)): the radio stations, the playlists (`.m3u`, `.pls`), `Res\Music\Radio\` and `Welcome.wav`. It is framework code linked into the exe (its functions are not aligned to 16 bytes like the game's), and the audio code holds it: the event handler at +0x10, the music lists post to it (part 7). It is rewritten in four pieces of its own, `audio.music.part1` to `part4`, in `src/core/music_<n>.cpp`, addresses in `data/addresses/music.toml` (its constructor 0x008D35AB stays in `audio.toml`, where part 4's factory names it). Each group is off by default.

### The classes

| Class | What |
| --- | --- |
| The player service | 0x90 bytes, class 0x5CFBDBBF, system service ID 0x1D07B39F, priority 0x7A116. Made by the director's factory 0x004EB150 (part 4), which hands out its +0x1C part. Constructor 0x008D35AB, destructor 0x008D36EF, deleting destructor 0x008D38C9 |
| The station class | 0x100 bytes, class 0x5CFBDBD5, constructor 0x008D376E, made by the factory 0x004EB180 and by the player's slot 16 (0x008D49F8); vtable 0x00ACC290 of 27 slots; destructor 0x008D3506. Its constructor shuffles its list (0x008D3442, `rand`). The player's station vector holds these. Its folders, as slot 16 sets them: +0x70 its own, +0x84 `Music\`, +0xA0 `DJ\<language>\`, +0xBC the shared `Commercials\<language>\`, +0xD8 `Jingle\<language>\`. +0x24 is a byte DoMessage clears, and slot 9 writes its 4-character ID |
| The stream object | At the player's +0x6C; not a station. Init makes it through the COM director (RZGetCOM, slot 3) with the class ID that service 0x1AF85BFB gives (its slot 9) and interface 0x206077D2: the framework's sound data source player. DoMessage drives it (its slots 6 to 8, 20 to 23 and 36) |

The player has four vtables:

| Offset | Vtable | What |
| --- | --- | --- |
| +0x00 | 0x00ACC3C0 | the system service, 20 slots: 0 QueryInterface 0x008D2A6E, 1 AddRef 0x008D2AAF, 2 Release 0x008D2AB6, 8 Init 0x008D4687, 9 Shutdown 0x008D2A09, 10 OnTick 0x008D365B, 13 0x008D3651 (sets +0x64), 15 0x008D366C (+0x89), 16 0x008D49F8, 17 0x008D3D5D and 18 0x008D3D33 (the file filters: the extensions mp3, wav, xa, m3u and pls), 19 0x008D2ACB (Shutdown and delete, what the last Release calls); 3 to 7, 11, 12 and 14 are the base's |
| +0x04 | 0x00ACC3A4 | the base's unknown part: thunks of QueryInterface, AddRef, Release and the deleting destructor |
| +0x1C | 0x00ACC310 | the player interface 0xE0510260, 37 slots (0 to 4 thunks of QueryInterface, AddRef, Release, Init, Shutdown; 6, 15 and 30 framework functions shared with other classes) |
| +0x20 | 0x00ACC2FC | a message target: three thunks and DoMessage 0x008D319B |

The player does little by itself: its methods post a message to the message server at +0x68 (0x0AB1A61E the volume, 0x0AB186E5 stop, 0x6AB1A480 pause, 0x4AB186E9, 0xEAB1A483, 0xEAB186DF), and DoMessage, which gets them back, works on the stream object (see the classes above). Its fields: +0x24 started, +0x28 the reference count, +0x2C the eighth dword of the settings, +0x30 the vector of stations, +0x3C the volume (0 to 0x400), +0x40 a mute count, +0x44 on, +0x48 the current station, +0x4C/+0x4D/+0x4E three option bytes (by number 0, 1 and 3), +0x50 the folder (a string), +0x64 a pointer slot 13 sets, +0x68 the message server, +0x6C the stream object, +0x74 a file name (a string) with +0x88 saying it is set, +0x8C the volume kept for unmuting. The settings slots 35 and 36 move eight dwords at once: volume, on, station, options 0 and 1, an unused one, option 3, and +0x2C.

### The pieces

| Piece | Functions | What |
| --- | --- | --- |
| `audio.music.part1` | 42 ours, 2 kept | the player service's life (constructor, destructor, deleting destructor), QueryInterface, AddRef, Release and their twelve this-adjustor thunks, Shutdown, OnTick, service slots 13, 15 and 19, the volume post 0x008D2C5F, and the interface's simple methods: slots 8, 9, 10, 12, 16, 17, 18, 20, 25 to 29, 31 to 33, 35, 36 |
| `audio.music.part2` | 20 ours | the rest of the player: Init 0x008D4687, slot 16 0x008D49F8, DoMessage 0x008D319B and 0x008D3132, the interface's slots 5 (0x008D30D1, 0x008D2C7D), 7 (0x008D3DC6, 0x008D3BF8), 11 (0x008D484A), 13 (0x008D490B, 0x008D2BFF), 14 (0x008D3066), 19 (0x008D38E5), 21 (0x008D33FF with the find 0x008D335B, 0x008D2C96), 22 (0x008D3733), 23 (0x008D2F21), and the file filters 0x008D3D5D, 0x008D3D33 |
| part 3 | 29 | the station class: constructor 0x008D376E, destructor 0x008D3506 and 0x008D38AD, its vtable's slots (0x008D2994 to 0x008D5EC3), the shuffle 0x008D3442 and 0x008D3485 and its helpers |
| part 4 | 48 | the playlist readers (0x008D5F5B to 0x008D6B22, `.m3u` and `.pls`) and the string and tree container code they use (0x008D3C38, 0x008D3C67, 0x008D3DEF, 0x008D50B2 to 0x008D5E50); container code shared with other code will stay the game's |
| not the player's | 3 | 0x008D6CF0 (6,749 bytes, called from 0x008FF8AB, 0x0099EF11 and 0x009B6AE5, none of them the player), 0x008D3333 (a vector erase with ten callers elsewhere), 0x008D3376 (an allocation helper also called from 0x00875337) |

Part 1 in `src/core/music_1.cpp`. The group goes in when core.dll is injected (phase Attach), as the framework's service base does, because the player is made while the application starts, before PostAppInit; its constructor and Shutdown therefore run as ours. The thunk of Init (0x008D51DB) is ours and calls Init by address (ours when part 2 is on).

Kept as the game's in part 1: slots 24 (0x008D2B70, the byte +0x44) and 34 (0x008D3668, the byte +0x88). Each is 4 bytes (`MOV AL,[ECX+n]; RET`) with the next function right after it and no padding, too short for the entry hook. 0x008D2C20 and 0x008D2C39 are not functions: they are the bodies the adjustors of slots 31 (0x008D3122) and 8 (0x008D312A) jump to, and are written out in those two.

Two quirks, kept: Shutdown releases the stream object without clearing its pointer at +0x6C, and the last Release does not count the reference down before slot 19 deletes the object.

#### Parity

Batch `mus1` (2026-10-19), 7 days per save, `audio.*=ours` on side B (parts 1 to 9 and `audio.music.part1`), the base list of `scripts/run-parity.sh` on both sides (with `determinism.dispatch_info`), `OPENSC4_TEST_AUDIO=3` and `OPENSC4_TEST_AUDIO_DIALOG=4`, three jobs at a time:

| Leg | Saves | Result |
| --- | --- | --- |
| trial | 3 (Big City d0000, Making Money d0090, Rush Hour d0365) | 3 same on the first pair |
| main | 12 | 11 same on the first pair; Big City d1095's B run did not start (below), same when rerun alone |

The failed run: three launches of the B run stopped right after core.dll's Attach phase (the last log line is the registry's, before the game's own start-up logs anything; a working run logs the player's constructor about ten seconds later), and the harness reported "the game did not start (Steam relaunch?)", the third time after its 600-second timeout. No crash, no exception report. Other games were being started on the machine at the same time, and Steam refused several launches with a "Steam Error" window. The same build launched in the other 14 B runs. Rerun alone (one job, the same switches, 2026-10-20): both runs started and the save was the same on the first pair (8 of 8 days), with 23 first-call lines on side B. So 12 of 12.

Reached (our first-call log lines, in every B run that started): the constructor, destructor, deleting destructor, QueryInterface, AddRef, Release, Shutdown, OnTick, slots 13, 15 and 19 of the service, the volume post, and the interface's slots 10, 12, 16, 29, 31 and 33; of the thunks, QueryInterface and Release of the +0x1C part, AddRef and Release of the +0x20 part, and the +4 part's deleting destructor. 23 of 42. Not reached: the other seven thunks (the Init thunk among them: Init is called through the service part), and slots 8, 9, 17, 18, 20, 25 to 28, 32, 35 and 36, which only the options dialog's real use, a station change or code of part 2 would call.

### Part 2

`audio.music.part2`, `src/core/music_2.cpp`, phase Attach as part 1 (Init runs while the application starts). 20 functions, all ours: Init 0x008D4687, the station loader 0x008D49F8 (service slot 16), the file filters 0x008D3D5D and 0x008D3D33 (slots 17 and 18), DoMessage 0x008D319B and its file helper 0x008D3132, and the interface's slots 5 (0x008D30D1, with the post 0x008D2C7D), 7 (0x008D3DC6, with the send 0x008D3BF8), 11 (0x008D484A), 13 (0x008D490B, with the post 0x008D2BFF), 14 (0x008D3066), 19 (0x008D38E5), 21 (0x008D33FF, with the find 0x008D335B and its loop 0x008D2C96), 22 (0x008D3733) and 23 (0x008D2F21).

Called by address and left the game's: 0x008D3333 (a vector's erase at one place, with ten callers elsewhere), 0x009A1ADD and 0x008FF785 (a vector's erase of a range and push_back, called from all over the game), the framework's string, folder (`cRZDirectory`) and file code, the reference holders of the services, and the station's constructor 0x008D376E (part 3).

What the code does:

- **Init** gets the sound service; without it, it fails. It gives the object the sound service (slot 14) and the language service 0x441E5070 (slot 13, kept at +0x64 without a reference). On its first call it makes the stream object (if there is a message server), sets the folder to the data folder (service 0x2073215D, its slot 7) plus `Res\Music\Radio\`, loads the stations (slot 16), marks itself started and turns the player on if it was on before. It joins the message server as a target and the framework's tick and idle lists.
- **Slot 16** lists the folders under `<folder>Stations\` and makes a station for each folder that holds a `*.id` file. Each station gets the player's interface, a reference, the option bit +0x2C, and its five folders. For the three language folders it takes language 0. When there is more than one language and that folder has no music files (slot 17: `*.mp3,*.wav,*.xa,*.m3u,*.pls`), it takes language 1 if option 3 is set, otherwise no language folder. The current station is set back to 0 if it is past the end. It returns whether a station was made.
- **DoMessage** takes the six messages the player posts to itself: stop (the stream's slot 8), the volume (kept at +0x3C, given to the stream unless the player is held, +0x89), a seek (0x4AB186E9: the stream's slot 22 with its position and the message's two numbers; sets +0x71), pause (0x6AB1A480), play a file (0xEAB186DF, through 0x008D3132) and the update (0xEAB1A483: the stream's "playing" and "held" into +0x88 and +0x89).
- **Slot 5** (from OnTick) posts the update and, unless the player is quiet (+0x70) or held, runs the current station's slot 5. **Slot 23** turns the player on (loading stations if there are none, starting the current one, giving the stream the volume) or off. **Slots 14, 19, 21, 22** change the station list and the current station. **Slots 11 and 13** find a station by its 4-character ID.

Kept quirks: Init's failure after the sound service (no data source class) returns 0 without joining the tick lists. DoMessage's stop does not test for a stream. Slot 19 calls slot 12 (the station at an index) for its effect when the first station is added while the player is on.

#### Parity of part 2

From 2026-10-20 the project runs parity muted (`OPENSC4_MUTE=1`, the game's `-audio:off`) on both sides ([parity.md](parity.md)). Batch `mus2`, 7 days, `audio.*=ours` on side B (parts 1 to 9 and music parts 1 and 2), the base list with `determinism.dispatch_info`, `OPENSC4_TEST_AUDIO=3` and `OPENSC4_TEST_AUDIO_DIALOG=4`, three jobs:

| Leg | Saves | Result |
| --- | --- | --- |
| muted trial | 3 (Big City d0000, Making Money d0090, Rush Hour d0365) | 3 same on the first pair |
| muted | 12 | 12 same on the first pair |
| unmuted | the same 3 | 3 same: Making Money on the first pair, the other two after two pairs |

**Muted, the music player does not exist.** No B run of either muted leg logged a single first call of parts 1 or 2: with the sound off the player is never made. The muted legs therefore show only that the hooks in place change nothing; they do not test the code. The unmuted leg does.

The unmuted saves that needed a second pair: in both, the original's first run was the odd one out. Its sound test hash differs on day 4 (master level 0 instead of 0x400, another ambience sub-hash), the rare second outcome described under "The exception" above. The other three runs, both of ours among them, agree.

Reached unmuted (first-call lines in all three B runs), part 2: Init, slot 16, slot 18, DoMessage, 0x008D3132, slots 5, 7, 11, 19, 22 and 23, the find and its loop, and the three posts' helpers 0x008D2C7D and 0x008D3BF8. That is 15 of 20. Not reached: slot 17 (a run has one language, so the fallback is never tested), slots 13, 14 and 21 and the post 0x008D2BFF. Nothing in the harness changes stations or seeks. Part 1: the same 23 of 42 as in batch `mus1`.

## Not done

- The functions in the fence table above that are shared with other classes or are generic container code. They could become ours only together with their other users.
- The sound library (AEMS) and its pointer table. They are EA's code; a replacement would be a new sound engine, not a rewrite.
- The framework's sound service, and the music player's pieces 3 and 4 (see "Music player").
- Playback checks. Everything above is compared by state; nothing here shows that the rewritten code sounds the same.
- The director's start-up code is ours but is not run in a normal test: the game's versions have already run by PostAppInit (see above).
- Tracks and sounds that are not the game's. New sounds and soundtracks are on the [roadmap](../ROADMAP.md) as an idea only and are not started.
