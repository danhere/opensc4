# Audio code

The game's own sound code, rewritten piece by piece like the simulators ([parity.md](parity.md)). Build 641 addresses throughout. The range is 0x004E1320 to 0x004FA6A0 (about 103 KB); what follows 0x004FA6A0 is not audio code. Entries are in `data/addresses/audio.toml` (placeholder names `SC4Audio::F<address>`, a few named ones). The code is in `src/core/audio_1.cpp` to `audio_9.cpp`, one group each (`audio.part1` to `audio.part9`), all in the subsystem `audio` and all off by default.

"Ours" means rewritten here and hooked at the original's entry. "The game's" means left alone and called by address. Whatever is heard is the business of EA's sound library, which stays the game's (see the fence below): this page is about the code that decides what to ask of it.

In total 575 functions are ours (573 under audio names, 2 under UI names: the options dialog's maker 0x004F2450 and its run function 0x004F25F0). The music player's four pieces add 138 (`audio.music.part1` 42, `audio.music.part2` 20, `audio.music.part3` 27, `audio.music.part4` 49; see "Music player"). The other functions of the range are listed under "What is fenced off" and "Not done".

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

`OPENSC4_TEST_MUSIC=<day>` adds the token ` music <stations> <switched> <hash>` and one log line `autotest: music stations driven:`. It gets the music player (service 0x1D07B39F, interface 0xE0510260) and hashes the number of stations and the current one (the interface's slots 17 and 10), each station's ID and whether it has something of each of the four kinds (station slots 9 and 20), then makes the next station current (slot 14: the old one stops, the new one starts when the player is on, which reads its folders and plays its welcome), reads it, and makes the first one current again. A muted run has no player: the token is ` music -1 0 <hash>`. It is the one test here that starts a sound.

`OPENSC4_TEST_PLAYLIST=<day>` adds the token ` playlist <entries> <hash>` and one log line `autotest: playlists driven:`. The stations of the install have no playlists, so the music player's reader (part 4) never runs in a game; this test runs it on files of its own. In `PlaylistTest\` of the run's user folder (the game's `-UserDir`, the same for both runs of a pair) it writes an extended `.m3u` (three pairs, with a drive path, a `sub\..\` part and a title with a comma), a plain `.m3u` (with a blank line, leading spaces, `.\`, `..\`, a path from the root and a zero byte), a `.pls` (one entry with title and length, one with a length only, one with a title only), a `.pls` with its `NumberOfEntries` line first, and names a missing one; each is read by a reader of its own (0x008D569F, 0x008D6B22, 0x008D56C8), and the result, the entries (title, path, length) and the folder are hashed. The entries of each are assigned into one vector in turn (0x008D56EB: it grows, shrinks and grows again within its capacity), which is freed at the end (0x008D51FC). Then 20 names of empty files with set modification times are sorted by time (0x008D5E50), reversed, partly sorted (0x008D5D06 with 5 of 20, the heap code) and shuffled (0x008D546F, which draws from the sound code's sequence under `determinism.fixed_seed`); the order is hashed after each. Everything is called by address; it needs no player and works muted. `<entries>` is the number of entries the five reads left (11). With `fixes.playlist_last_line` on, ` nobreak <entries> <hash>` follows: four playlists without a last line break ("The playlist hang").

Each runs on one day of the run (hooks `audio`, `audio.dialog`, `audio.music` and `audio.playlist`) and needs build 641.

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
| The station class | 0x100 bytes, class 0x5CFBDBD5, constructor 0x008D376E, made by the factory 0x004EB180 and by the player's slot 16 (0x008D49F8); vtable 0x00ACC290 of 27 slots; destructor 0x008D3506. Its constructor shuffles its play order (0x008D3442, `rand`); see "Part 3". The player's station vector holds these. Its folders, as slot 16 sets them: +0x70 its own, +0x84 `Music\`, +0xA0 `DJ\<language>\`, +0xBC the shared `Commercials\<language>\`, +0xD8 `Jingle\<language>\`. +0x24 is a byte DoMessage clears, and slot 9 copies out its 4-character ID |
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
| `audio.music.part3` | 27 ours | the station class: constructor 0x008D376E, destructor 0x008D3506, deleting destructor 0x008D38AD (slot 15), the station's own vtable slots 0, 5 to 7, 9, 10, 13, 14 and 16 to 26, the shuffle 0x008D3442, and the helpers 0x008D34D1, 0x008D34EC, 0x008D3EE0 and 0x008D3BE2 (see "Part 3") |
| `audio.music.part4` | 49 ours | the playlist reader (0x008D569F, 0x008D56C8, 0x008D5F5B to 0x008D6B22: `.m3u`, `.pls`, plain lists), its entries and their vector, and the sort of names by file time with the shuffle of names (0x008D3C38, 0x008D3C67, 0x008D3DEF, 0x008D50B2 to 0x008D5E50 but 0x008D51DB, 0x008D5409 and 0x008D5827); see "Part 4" |
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

### Part 3

`audio.music.part3`, `src/core/music_3.cpp`, phase Attach as parts 1 and 2 (the stations are made while the application starts). 27 functions, all ours: the constructor 0x008D376E, the destructor 0x008D3506, the deleting destructor 0x008D38AD (slot 15), the station's own slots of the vtable 0x00ACC290 (0 QueryInterface 0x008D2994, 5 Update 0x008D2CFD, 6 0x008D3593, 7 Start 0x008D29D0, 9 the ID 0x008D2E5F, 10 the name 0x008D2E88, 13 0x008D29FC, 14 the filter 0x008D5409, 16 the folders 0x008D3960, 17 0x008D4317, 18 0x008D5EC3, 19 0x008D3485, 20 0x008D2EB0, 21 0x008D338F, 22 0x008D4579, 23 0x008D4127, 24 the welcome 0x008D3E7E, 25 the playlists 0x008D5827, 26 0x008D3F8E), the shuffle 0x008D3442, and four helpers: 0x008D34D1 and 0x008D34EC (the play order's constructor and destructor), 0x008D3EE0 (slot 26's file check) and 0x008D3BE2 (a string copy for slot 18).

Left the game's: slots 1 (AddRef 0x00968B60), 3 and 4 (0x00949ADE, return 1), 8 (0x00A6D837, a bare ret), 11 (0x00891D96, the byte +8) and 12 (0x00994E71, sets +0xF8), framework functions shared with other classes; and Release 0x008D29B9 (slot 2), which three other vtables hold too (0x00A82A28, 0x00AD16D0, 0x00AD4720): the first try had it ours, and its first call came before any station existed. Called by address: part 4's playlist reader (0x008D569F, 0x008D56C8, 0x008D6B22), its entries (0x008D56EB, 0x008D51FC), the sort by file time 0x008D5E50 and the shuffle of names 0x008D546F; the string, list, folder, file and stopwatch code.

The station:

| Offset | What |
| --- | --- |
| +0x04 | the reference count |
| +0x08, +0x09 | bytes: on (1, slot 11), the welcome played |
| +0x10 to +0x18 | the play order: a vector of the four kinds (0 music, 1 DJ, 2 commercials, 3 jingles), shuffled by the constructor; +0x28 (+0x18 of it) a stopwatch |
| +0x1C, +0x20 | the place in the play order; a count of music played again in place of the next kind |
| +0x24 | byte: waiting (DoMessage's pause clears it) |
| +0x40, +0x44 | the player interface (not held), its slot 6 |
| +0x48, +0x5C | the ID (the `*.id` file's name, `????` before) and the name (the `*.txt` file's) |
| +0x70 | the station's folder |
| +0x84, +0x98, +0x9C | music: the folder, the list of file names (an STL list), the place in it |
| +0xA0, +0xB4, +0xB8 | DJ, the same |
| +0xBC, +0xD0, +0xD4 | commercials, the same |
| +0xD8, +0xEC, +0xF0 | jingles, the same |
| +0xF4, +0xF8 | the music filter (a list of names) and its mode (slot 12) |
| +0xFC | slot 17's argument (slot 13; the player's option bit +0x2C) |

What the code does:

- **Start** (slot 7, what the player calls when a station becomes current) reads the ID and name (slot 22), lists the four folders (slot 16), orders the lists (slot 17) and, once, plays the station's `Welcome.wav` (slot 24).
- **Slot 16** empties the four lists and fills each with the folder's files that match `*.mp3,*.wav,*.xa,*.m3u,*.pls`, one pattern at a time, then has slot 25 replace every `.m3u` and `.pls` name by the files the playlist names. **Slot 17** orders each list (slot 18: sorted by the files' modification times, newest first, when +0xFC is 1, else shuffled), swaps first and last when the name that was last came out first, and sets the list's place to the first.
- **Update** (slot 5, which the player's slot 5 runs on every tick) waits until the player is done with a file, then four seconds more (the stopwatch), then takes the next kind of the play order. After music it plays more music with 75 in 100; DJ is played with 50 in 100, commercials and jingles with 10 in 100. A kind with nothing to play (slot 20: an empty list, or the player's options 0 to 2 off for DJ, commercials, jingles) is passed over. At the end of the order the order is shuffled again (slot 19), with the last kind not first again unless it is music. Slot 23 plays the next file of the kind through the player's slot 7; music with a filter goes to slot 26, which looks for the next name that is (mode other than 1) or is not (mode 1) in the filter.
- Its `rand()` draws (Update and the shuffle) come from the sound code's own sequence through `fixed_seed::SoundRand()`, as the game's own draws do under `determinism.fixed_seed`, which tells them apart by the caller's address.

Kept quirks: the constructor leaves the four list places unset; slot 17 sets them only for lists that are not empty, and slot 23 on an empty list plays the head node's (unset) string. Slot 25, after taking a playlist's name out of the list, still compares the next pattern with the freed node's name. Slot 23 looks for a music name in the Music folder only when it is not a file as it is; the other kinds are always joined to their folder. Slot 18 does not use the station.

#### Parity of part 3

Batch `mus3` (2026-10-20), 7 days, `audio.*=ours` on side B (parts 1 to 9 and music parts 1 to 3), the base list with `determinism.dispatch_info`, `OPENSC4_TEST_AUDIO=3`, `OPENSC4_TEST_AUDIO_DIALOG=4` and `OPENSC4_TEST_MUSIC=5`, three jobs (other games running on the machine):

| Leg | Saves | Result |
| --- | --- | --- |
| unmuted | 3 (Big City d0000, Making Money d0090, Rush Hour d0365) | Making Money and Rush Hour same on the first pair; Big City d0000 partly after four pairs (below), same when rerun alone (8 of 8 days, first pair) |
| muted trial | the same 3 | 3 same on the first pair |
| muted | 12 | 12 same on the first pair |

Big City d0000's first B run had other `adv` and `advisors` values from day 1 on, and the rest of the line was the same, the audio and music sub-hashes among them; its three repeats had A's values. This is the first-run advisor outcome of [parity.md](parity.md) ("The advisor system's generator"), not the sound test's exception above. The music token was ` music 2 1 825A73C3` in every unmuted run of both sides (two stations, the switch made) and ` music -1 0 6B0B6AB1` muted.

Reached (first-call lines in every unmuted B run, the three saves and the retries): the constructor, both destructors, the play order's constructor and destructor, the shuffle, slots 5, 6, 7, 9, 13, 14, 16, 17, 18, 20, 22, 23, 24, 25 and 26, and slot 26's file check. 22 of 27. Not reached: QueryInterface (slot 0), the name (slot 10), the new order (slot 19, which needs the play order run through, longer than these runs), slot 21 (nothing in the player calls it), and the string copy 0x008D3BE2 (only slot 18's sort by file time, with the player's option bit +0x2C set). The muted legs log no first call: there is no player. Parts 1 and 2 reached what they did in `mus2`.

### Part 4

`audio.music.part4`, `src/core/music_4.cpp`, phase Attach as parts 1 to 3. 49 functions, all ours: every function of the piece's ranges but Init's thunk 0x008D51DB (part 1) and part 3's 0x008D5409 and 0x008D5827. Each is called only from the music player and none is in a vtable (Ghidra's references and a search of the exe for their addresses), and the shortest is 25 bytes, so none had to stay. The piece's table counted 48; the ranges hold 52 functions.

| Group | Functions |
| --- | --- |
| The reader | constructor 0x008D569F, destructor 0x008D56C8, Read 0x008D6B22; the three formats: extended `.m3u` 0x008D64E3, `.pls` 0x008D6824, a plain list 0x008D66C9; the line reader 0x008D6258 with its searches 0x008D5F5B (first character not in a set) and 0x008D5FDC (first in a set); the paths: 0x008D6352 (a name made a full path), 0x008D6123 (a root), 0x008D6196 (a part added), 0x008D6056 (a part split off), 0x008D60C4 (a folder up) |
| The entries | an entry made 0x008D6171, copied 0x008D3C38, assigned 0x008D50DB; their vector (STLport): copy into raw memory 0x008D50B2, fill 0x008D6230, destroy 0x008D51E3, free 0x008D51FC, allocate and copy 0x008D521A, assign ranges 0x008D5245 and 0x008D54C2, assign 0x008D56EB, the insert that grows it 0x008D63F3, push_back 0x008D64AB |
| The sort | 0x008D5E50 (what part 3's slot 18 calls) and STLport's introsort under it: the comparator 0x008D3C67, the median 0x008D3DEF, the partition 0x008D5277, the loop 0x008D5D3F, the insertion sorts 0x008D5C8F, 0x008D5B62, 0x008D5A7D, 0x008D54DF, 0x008D52DF, 0x008D5102, the heap code 0x008D5D06, 0x008D5BBA, 0x008D57B9, 0x008D5580, 0x008D5355, 0x008D5158, 0x008D5B03, 0x008D57F1, 0x008D562B, 0x008D5515; and the shuffle of names 0x008D546F |

Called by address and left the game's: the allocation of entries 0x008D3376 (also called from 0x00875337), the strings' copy_backward 0x005C82C0 and the destructor of a pair of strings 0x0091C947 (both shared), the framework's string, file and path code (the full path 0x0091AA3B, `RZFile::F91AA3B`, is not analysed), and the file time 0x00918CB1.

The reader is 0x48 bytes: the vector of entries, the playlist's full path (+0x0C), its folder (+0x20) and the text still to read (+0x34). An entry is 0x2C bytes: the title, the path and the length in seconds. Part 3's slot 25 makes a reader for each `.m3u` or `.pls` name, reads it, and when the read returns 1 adds each entry's path to the list.

What the code does:

- **Read** keeps the full path (0x0091AA3B, else the path as given) and its folder, reads the whole file into the text with its zero bytes (but the last byte) made spaces, and reads the first line: `#EXTM3U` is an extended `.m3u`, `[playlist]` a `.pls`; anything else is a plain list, read from the start again. It returns what the format's reader returns, 0 when the file cannot be opened or read.
- **A line** is what follows the white space (space, tab, line breaks) up to the next `\n` or `\r`, trimmed and converted (code page -1). The white space and the line are taken off the text; blank lines are skipped.
- **Paths.** Each entry's path is made a full path: the playlist's folder, then each part of the name: a drive (`C:\`) or a leading backslash sets the root, a part of n dots goes n - 1 folders up, any other part is added.
- **Extended `.m3u`.** Pairs of lines: `#EXTINF:<seconds>,<title>` and the path. Returns 1 at the end of the text; 0 when a first line is not `#EXTINF:` or the last pair has no path.
- **Plain list.** A path a line; the title is the file's name without its extension, the length 0. Returns 1.
- **`.pls`.** Lines are read up to the one that begins `NumberOfEntries`. A `File<n>=` line's path is taken with the `Title<n>=` and `Length<n>=` lines right after it when they are there; a line that is neither is kept for the next turn. Returns whether the number of entries read is the number after `NumberOfEntries=` (1 for an empty text, 0 when the text ends first).
- **The sort** puts a name before another when the folder plus that name has the later modification time (`stat`'s, through 0x00918CB1 with 2; 0 for a missing file): newest first. Part 3's slot 18 uses it when the player's option bit +0x2C is set, which the game does not set in these tests.
- **The shuffle** of names draws from the sound code's own sequence through `fixed_seed::SoundRand()`, as the game's does under `determinism.fixed_seed` (its range 0x008D546F to 0x008D54C3 is in `kSoundCallers`).

Kept quirks:

- A playlist whose last line has no line break hangs the game; this one is fixed, on by default (see "The playlist hang" below).
- A `.pls` whose `NumberOfEntries` line comes before the entries (a common layout) returns 0, and part 3 then takes none of its entries.
- An extended `.m3u` or a `.pls` that returns 0 has its entries read so far dropped the same way.
- In a `.pls`, while it looks for the line after an entry, it tests whether the entry's path, not that line, begins with `File`.
- The plain list joins a path to the playlist's when the line has `:` and `\\` at its first place (a "not found" of -1 was taken for 0), which no line can have; every path is made full from the folder anyway.
- Read does not free its buffer when the read fails, and an empty file runs the loop that makes zero bytes spaces past its buffer (its count is the size minus 1, unsigned).
- A part split off a path takes one character more than there is when it has no backslash (the string's end); the string's assignment stops at its end.
- The insert that grows the vector is always asked for one entry, so the fill 0x008D6230 is never called.

#### The playlist hang

A game bug: in a playlist whose last line has no line break, the line reader 0x008D6258 does not take that line off the text, so every later call reads it again. A plain list or a `.pls` without its `NumberOfEntries` line then never ends and the game hangs; an extended `.m3u` returns 0. A playlist saved by an editor that leaves out the final line break is enough.

`fixes.playlist_last_line` (on by default; `fixes.playlist_last_line = original` turns it off) takes that line off the text too, so the next read finds the end. It installs our 0x008D6258 by itself, with or without `audio.music.part4` ([src/core/music_4.cpp](../src/core/music_4.cpp)). A playlist that ends with a line break reads as before. `scripts/run-parity.sh` turns it off on both sides.

With the fix on, `OPENSC4_TEST_PLAYLIST` also reads four playlists without a last line break (a plain list, an extended `.m3u`, a `.pls` with its count last, a `.pls` with no count) and adds ` nobreak <entries> <hash>` to its token; with it off it logs that they were not read (the original would hang on them).

Batch `mlf` (2026-10-22), 7 days, three jobs, the mus4 settings below. Fix off on both sides (the base list), `audio.*=ours` on B: unmuted 3 same on the first pair; muted 12 same (Rush Hour d0000 on the second pair and Making Money d0365 on a solo rerun, both the first-run advisor outcome). Fix on, A `fixes.playlist_last_line=ours`, B that and `audio.*=ours`, muted 3: same on the first pair, no run hung, every log with `playlist last line fix: on`, and ` nobreak 4 <hash>` the same on both sides of each pair (4 entries from the four playlists). With the fix on, the ReadLine first-call line appears on A as well.

#### Parity of part 4

Batch `mus4` (2026-10-21), 7 days, `audio.*=ours` on side B (parts 1 to 9 and music parts 1 to 4), the base list with `determinism.dispatch_info`, `OPENSC4_TEST_AUDIO=3`, `OPENSC4_TEST_AUDIO_DIALOG=4`, `OPENSC4_TEST_MUSIC=3` and `OPENSC4_TEST_PLAYLIST=2`, three jobs (another game running on the machine):

| Leg | Saves | Result |
| --- | --- | --- |
| unmuted | 3 (Big City d0000, Making Money d0090, Rush Hour d0365) | 3 same on the first pair (8 of 8 days) |
| muted trial | the same 3 | 3 same on the first pair |
| muted | 12 | 12 same on the first pair |

No retry was needed: neither the first-run advisor outcome nor the sound test's exception came up. The playlist token was the same on both sides of every pair (` playlist 11 <hash>`: 3 entries of the extended `.m3u`, 5 of the plain list, 3 of the `.pls`, none of the `.pls` with its count first or of the missing file). Its hash differs from save to save: the paths hold the job's user folder, and the shuffle's draws follow the sound sequence. The music token was ` music 2 1 825A73C3` unmuted and ` music -1 0 6B0B6AB1` muted, as in `mus3`.

Reached (first-call lines in every B run of the three legs, muted ones too, since the playlist test needs no player): 48 of 49, all but the fill 0x008D6230, which nothing calls (see the quirks). In a game without the test only the shuffle of names is reached (part 3's slot 18; the stations have no playlists and the sort by file time needs the player's option bit). Parts 1 to 3 reached what they did in `mus3` in the unmuted leg and nothing muted.

## Not done

- The functions in the fence table above that are shared with other classes or are generic container code. They could become ours only together with their other users.
- The sound library (AEMS) and its pointer table. They are EA's code; a replacement would be a new sound engine, not a rewrite.
- The framework's sound service. Of the music player's range, six functions stay the game's: the three in the "not the player's" row of its pieces table, the two 4-byte slots of part 1 and the shared Release of part 3 (see "Music player").
- Playback checks. Everything above is compared by state; nothing here shows that the rewritten code sounds the same.
- The director's start-up code is ours but is not run in a normal test: the game's versions have already run by PostAppInit (see above).
- Tracks and sounds that are not the game's. New sounds and soundtracks are on the [roadmap](../ROADMAP.md) as an idea only and are not started.
