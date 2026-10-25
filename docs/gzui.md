# The GZ framework's UI layer

Block 3 of [uncharted-code.md](uncharted-code.md), 0x00940000 to 0x0095FFFF: 767 functions, 284 of which ran in the 14 coverage runs, 87 of them two-instruction thunks. A survey of what is there, a split of the block into pieces, and part u1, the property set and its values, which is ours. Framework code: functions packed, not aligned to 16 bytes like the game's ([uncharted-code.md](uncharted-code.md), "Method").

Everything here was read from the disassembly. Names are ours; the exe has no class names for this code apart from the strings it uses (`cGZPropertySetUint32`, `GZWinBtn`, ...).

## Survey

### What the block holds

The survey of 2026-10-19 put "`cGZPropertySet` and its property values" in 0x00942000 to 0x00947FFF, from the strings there. That range holds the property set's **readers and writers** (XML and binary) and other classes; the property set itself starts below the block, at 0x00939040, next to its values' vtables (0x00AD25FC and on) and the window classes that hold one. Part u1 is therefore mostly outside the block (21 of its 39 functions).

| Range | Functions | Ran | Thunks | What |
|---|---:|---:|---:|---|
| 0x00937EC2 to 0x009397FF (below the block) | 21 of u1 | | | The property set (vtable 0x00AD27D4) and the uint32, float and string values' slots that live there; part u1 |
| 0x00940000 to 0x00941FFF | 101 | 23 | 37 | Value slots (18 of u1); the binary property-set reader and writer (vtable 0x00AD40F8, interface 0xEBE701F3, record 0x2BEA4862); a class factory (vtable 0x00AD40D0: classes 0x0B8D821A, 0x0B9EB87E, 0x7B1ACFCD, 0xA3CD96CF, 0xEBFEE345, 0xEBFEE349); the XML property-set reader-writer object (constructor 0x009406CB, vtables 0x00AD442C and 0x00AD43FC, made for 0x2BEA4862 and 0xCBE750E0 by 0x009408A5); the property set's two subclasses' slots (vtables 0x00AD2920 and 0x00AD2898: the "Track" objects the factory makes; their text form `TKDT`/`ETKD` is 0x00940E5C, writer, and 0x00941930, reader, 5 KB); the window of class 0xA3CD96CF (constructor 0x009402A1, 0x114 bytes, nine vtables, 37 adjustor thunks); the shuffle bag's subclass |
| 0x00942000 to 0x00943916 | 24 | 1 | 0 | The XML side: a window's attribute reader 0x009425A2 (`PropertyStringString`, `PropertyUint32String`, `PropertyUint32Uint32`, `PropertyUint32Float32`), the writer 0x0094286B (`<?xml version=...>`, `<AnyUint32 key=... type=...>`, `<AnyString ...>` and the other three), 0x00942D87 and 0x00943809 (`Name`, `Value`), the factory's create 0x00942F24 (ran), and 13 functions of a container (0x0094309C to 0x009437DB) that never ran |
| 0x00943917 to 0x00944461 | 20 | 8 | 0 | A shuffle bag of uint32s (vtable 0x00AD4648, interface 0xDB1ACFB2, 0x18 bytes: two lists at +8 and +0xC, a threshold at +0x10; the subclass 0x0094048B, the factory's class 0x7B1ACFCD, adds a resource key interface at +0x18 and the key at +0x1C, part u2; a count of live bags at 0x00B633B8): slot 8 draws a random entry from the first list and moves it to the second, refilling when the first runs low. Plus two slots each of the classes 0x00AD2804 and 0x00AD2830 (never ran) |
| 0x00944462 to 0x009456EB | 67 | 40 | 0 | The UI file parser (part u4; not XML, see there): a director (0x0094448E) that registers the parser 0xBA2E7953 (0x5C bytes, constructor 0x00945404, vtable 0x00AD4718, interface 0x5A2C1570) and its handler 0xBA2E7954 (0x14 bytes, vtable 0x00AD4764, interface 0x1A2BDEBB); an element stack, an attribute vector, the text tokenizer (`\t\n\r <>="`) |
| 0x009456EC to 0x00945FFF | 58 | 25 | 5 | The framework's sound director (part u5; constructor 0x009456EC, ID 0xFD53FF17, vtables 0x00AD47C0 and 0x00AD4790; the audio director adds it through the thunk 0x00945785, in audio.toml) and three of the five classes it registers: a 64 by 64 table of sound descriptions (0x5C1CF5A3, constructor 0x0094585F, vtable 0x00AD4818, whose QueryInterface is the base one at 0x009457C6 that dozens of classes jump to); the sound instance (0xBDD9D7A5, constructors 0x009458D6 and 0x00945C6C, two vtables after an abstract base's); the sound description (0x7DD9C9F2, constructor 0x00945DCC, 43 slots, mostly setters). The `0123456789abcdefABCDEF` text is the instance's 8-hex-digit test 0x00945F38 |
| 0x00946000 to 0x00948E62 | 75 | 45 | 4 | One large class (constructor 0x009489AE, vtables 0x00AD4B34 and 0x00AD4B48, 65 slots), and its helpers |
| 0x00948E63 to 0x0094A078 | 71 | 7 | 5 | The framework's sound mixer 0xBDDA72DD (part u8; constructor 0x00949902, vtables 0x00AD4CF4, 0x00AD4C60 and 0x00AD4C4C); the 7 that ran are shared code |
| 0x0094A079 to 0x0094BFFF | 108 | 51 | 26 | The UI script loader 0xFA3562FB and its director (part u9: 0x0094AF19 to 0x0094BBFD; the loader's constructor 0x0094B400 behind its factory 0x0094AF64, seven vtables 0x00AD50B8 to 0x00AD5158; the words `_null` to `_val`, `none`, `children` ... defined at 0x0094B73A; list node 0x0094B33A); below and above it the MP3 decoder director 0x1CEC20BE and its stream and maker (0x0094A079 to 0x0094AF18) and the UI script root holder's slots (0x0094BBFE to 0x0094BF3B, vtable 0x00AD5248), part u10 |
| 0x0094C000 to 0x00951268 | 99 | 51 | 7 | The framework window (`GZWin`; AddRef 0x0094C47C, in ui.toml) and its UI script properties (`clsid`, `iid`, `area`, `{%x,%x}`, `(%d,%d,%d)`); 0x0094C900 on are few, big functions (18.7 KB in 49) |
| 0x00951269 to 0x00953FFF | 17 | 17 | 0 | The stock widgets' UI script readers, one function each: TextTicker, Folders, Custom, FlatRect, Text, TextEdit, ListBox, Combo, Btn, BMP, Slider, Scrollbar, LineInput, Spinner, Outline, Gen (11.8 KB, all ran) |
| 0x00954000 to 0x0095BBFF | 98 | 16 | 3 | FileBrowser, OptGrp, TreeView and Grid readers and their helpers; most never ran |
| 0x0095BC00 to 0x0095FFFF | 29 | 0 | 0 | The UI script writers (`%s=(%u,%u,%u)`, `%s=0x%08x`, `%s="%s"`, ...) and each widget's writer: saving a UI script, which the game never does |

The block continues past 0x00960000 (more widgets; not surveyed here).

### The property set and its values

The property set (`cIGZPropertySet` interface 0xEBB10CDD in our naming) maps uint32 keys to values of five types. Windows hold one each (the window constructors 0x0093EAA1 and 0x0093FE12 make it), and the UI scripts' readers and the class factory fill them. It runs in every game.

The set is 0x18 bytes: vtable 0x00AD27D4, a reference count at +4, an STLport map at +8 (its header; its size at +0xC) and the cursor of GetFirst and GetNext at +0x14. A node holds the key at +0x10 and a byte block (begin, end, capacity) from +0x14, and the value object lives in that block: SetProperty makes it there with the value's Clone and RemoveProperty destroys it there with the value's slot 0 and flag 0. Two subclasses (vtables 0x00AD2920 and 0x00AD2898, the factory's "Track" objects) add a second interface at +0x18 and share slots 3 to 10.

| Slot | Function | What |
|---:|---|---|
| 0 | 0x00939254 | QueryInterface: 1 and 0xEBB10CDD |
| 1 | 0x00968B60 | AddRef (shared with 14 other classes; the game's) |
| 2 | 0x00939279 | Release: at a count of 1 or less the deleting destructor, without lowering the count |
| 3 | 0x009393D4 | SetProperty(key*, value*): replaces in place (old value destroyed, block resized, new value cloned) or inserts (0x00939431) |
| 4 | 0x0093934A | RemoveProperty(key*), with the erase 0x00939380 |
| 5 | 0x00939040 | RemoveAll: every value destroyed, then the map's clear |
| 6, 7 | 0x00939290, 0x009392C7 | GetFirst(key*, value**), GetNext: nothing with a null pointer; GetNext moves the cursor first |
| 8 | 0x00939306 | GetProperty(key*, out*): the out value's CopyFrom with the stored one, so a type mismatch answers false |
| 9 | 0x0093932E | HasProperty(key*) |
| 10 | 0x009CEDE1 | Count: the map's size (shared with ten other classes; the game's) |
| 11 | 0x00939607 | The deleting destructor (destructor 0x009393BD) |

A value is 8 bytes, its vtable and its data at +4; the game makes them on the stack. Nine slots: 0 the deleting destructor, 1 CopyFrom (null or another type: false), 2 Assign (CopyFrom without the null test), 3 Clone(place), 4 GetType, 5 the size (8; 0x00940DA8, shared with unrelated code, the game's), 6 DataSize (the size less 4), 7 and 8 a comparison.

| Type | ID | Vtable | Data |
|---|---|---|---|
| uint32 | 0xEB61E4F7 | 0x00AD25FC | a dword |
| sint32 | 0x0C264712 | 0x00AD4468 | a dword |
| float | 0xABC78708 | 0x00AD2BA4 | a float |
| bool | 0xCBA908E1 | 0x00AD4444 | a byte (three bytes of padding) |
| string | 0x0B8BEA18 | 0x00AD43D8 | a counted game string |

The three numbers share CopyFrom, Assign, the deleting destructor and the comparison, which compares the four data bytes in memory order as signed chars: it does not order numbers, and for a bool it compares the padding too. The string's comparison is the game string's CompareTo with 1.

There are no array values: the "string and uint32 arrays" of the brief do not exist in this class.

## The split

Pieces of about 35 to 45 functions, by address and class; thunks count. Sizes vary a lot: the widgets' readers and writers are few, big functions, and are better split by bytes.

| Piece | Range | Functions | Ran | What |
|---|---|---:|---:|---|
| **u1** | property set and values | 39 | 19 | **Ours** (below) |
| **u2** | 0x00940000 to 0x00941FFF, less u1 | 36 of 83 | 7 | **Ours** (below): the binary reader and writer, the factory, the XML reader-writer object, the Track objects' slots, the window 0xA3CD96CF, the keyed shuffle bag; 38 thunks, 6 STLport list functions and a shared getter stay the game's |
| **u2b** | 0x00940E5C, 0x00941930 | 2 | 1 | **Ours** (below): the Track objects' TKDT text form, the writer (2,010 bytes) and the reader (3,186 bytes, ran in all 12 coverage runs) |
| **u3** | 0x00942000 to 0x00944461 | 29 of 44 | 9 | **Ours** (below): the XML writer and StartElement, the UI script handler's StartElement and EndElement, the factory's Create, the window's Clone and copy, the shuffle bag, the framework's 2D and 3D sound kinds' slots 5 and 6; 15 STLport functions stay the game's |
| **u4** | 0x00944462 to 0x009456EB | 49 of 67 | 40 | **Ours** (below): the UI file parser 0xBA2E7953, its variable table 0xBA2E7954, its director; the next director's deleting destructor and factories; 16 STLport functions and two ID getters stay the game's |
| **u5** | 0x009456EC to 0x00945FFF | 44 of 58 | 25 | **Ours** (below): the framework's sound director, its sound table, the sound instance and the sound description; 4 adjustors, a thunk, the base QueryInterface, 4 two-instruction getters and 4 setters other classes share stay the game's (and the instance's AddRef 0x00945942, which the function list lacks and five other vtables share) |
| **u6** | 0x00946000 to 0x00948E62, first half | 40 of 75 | 29 | **Ours** (below): the framework's sound service 0x3C1CF59E (the audio layer's score player): constructor, interface, destructors, Init with the sound table's reader and text loader, Prepare, Play, the fade, the tempo, the window and slots 3 to 37; 2 adjustors, 2 AddRefs, 2 thunks and a getter the description shares stay the game's |
| **u7** | 0x00946000 to 0x00948E62, the rest | 30 | 14 | **Ours** (below): the service's slots 38 to 64 but the shared 44 and 45, its message target's slot 3 (the beats) and the neighbour pick, the table's reader by path, the sound instance's slot 5 and its listener path |
| **u8** | 0x00948E63 to 0x0094A078 | 36 of 71 | 7 | **Ours** (below): the framework's sound mixer 0xBDDA72DD (constructor 0x00949902); 5 adjustors, 5 shared getters and stubs, 20 STLport functions and 5 shared reference and list functions stay the game's |
| **u9** | 0x0094AF19 to 0x0094BBFD | 31 of 54 | 26 | **Ours** (below): the UI script loader 0xFA3562FB (the framework's system service that makes the UI file parser and builds windows from UI scripts) and its director; 19 adjustors, the base service's two AddRef and Release adjustors, the director's ID getter and the shared list node stay the game's |
| **u10** | 0x0094A079 to 0x0094AF18, 0x0094BBFE to 0x0094BF3B | 48 of 54 | 25 | **Ours** (below): the MP3 decoder director 0x1CEC20BE ("HitMP3Decoder"), its stream 0x3CEC2B47 and maker 0x9CEC2B58, the decoder's feed, decode, reset, frame header and ID3 size; the UI script root holder 0x5A354595's slots, its vector's insert and copies, the rectangle test; two jump thunks, a shared stub, the ID getter and the holder's two folded slots stay the game's |
| **u11** | 0x0094C000 to 0x0094C8FF | 21 of 50 | 35 | **Ours** (below): the rest of the UI script root holder 0x5A354595 (constructor, slot 4, destructors, the vector's constructor, push and erase) and the LEGACY reader 0xFA450242's own slots; 8 adjustors, helpers that take their arguments in registers and other classes' small functions stay the game's |
| **u12** | 0x0094C900 to 0x00951268 | 19 of 49 | 16 | **Ours** (below), four slices: the readers of a window's script properties, the variable table's words and all 14 property handlers (6 of them never run); the STLport map and tree code, the shared AddRef and 7 small functions of other classes stay the game's |
| **u13** | 0x00951269 to 0x00953FFF | 17 of 17 | 17 | **Ours** (below): the 16 stock widget classes' registrations (their names and property words into the LEGACY readers' variable table, their handlers and interfaces into two maps) and GZWinGen's property handler |
| **u14** | 0x00954000 to 0x00956463 | 2 of 41 | 2 | **Ours** (below): GZWinFileBrowser's registration and property handler; the STLport tree, list and pair code and a counted object's Release stay the game's |
| **u15** | 0x00956464 to 0x009586AF | 4 of 23 | 3 | **Ours** (below): GZWinOptGrp's registration and property handler, the base window's handler and the dispatcher; TreeView's and Grid's STLport map code, Grid's text helper and cell lookups stay the game's |
| **u16a** | 0x009586B0 to 0x00959E30 | 2 of 3 | 1 | **Ours** (below): GZWinTreeView's registration and property handler; a map clear stays the game's |
| **u16b** | 0x00959E31 to 0x0095BBFF | 9 of 32 | 10 | **Ours** (below): GZWinGrid's registration and property handler (with its text helper 0x00956464 of u15's range), GZWinCustom's class code and the LEGACY reader's constructor; the grid maps' STLport code, GZWinCustom's adjustors and three of the writers' helpers stay the game's |
| **u17** | 0x0095BC00 to 0x0095FFFF | 29 of 29 | 0 | **Ours** (below): the UI script writers (17.7 KB, never run): 14 printers, the font printer, the flag table, a key compare and 11 classes' writers, with the three property helpers below the range (0x0095BAD7, 0x0095BB29, 0x0095BB82) |

Leave the game's: the base QueryInterface 0x009457C6, AddRef 0x00968B60, the shared one-instruction getters, the empty functions 0x00949ADE and 0x0091559A, and the STLport and counted-reference code these classes share with the rest of the framework.

## Part u1: the property set and its values

`src/core/gzui_propset.cpp`, entry detours, off by default:

| Replacement | Functions |
|---|---|
| `framework.propset.set` | the constructor 0x00939237, QueryInterface 0x00939254, Release 0x00939279, SetProperty 0x009393D4 with Insert 0x00939431, RemoveProperty 0x0093934A with EraseNode 0x00939380, RemoveAll 0x00939040, GetFirst 0x00939290, GetNext 0x009392C7, GetProperty 0x00939306, HasProperty 0x0093932E, the destructor 0x009393BD and the deleting destructor 0x00939607 (14) |
| `framework.propset.values` | the base's deleting destructor 0x00940BC5, DataSize 0x00940DAC, the comparison 0x009378D3, the numbers' CopyFrom 0x00940DB5 and Assign 0x00937EC2, the bool's 0x00940BE2 and 0x00940C15, the Clones 0x00937EF1, 0x0093BB22, 0x00940DE8, 0x00940C44 and the GetTypes 0x009378CD, 0x0093AAAF, 0x00940DA2, 0x00940BBF (15) |
| `framework.propset.string` | the destructor 0x0094002A and deleting destructor 0x0094065E, the constructors 0x00940627 (copy), 0x009407F4 (from a game string) and 0x0094082E (from text), CopyFrom 0x0094059B, Assign 0x009405D9, Clone 0x00940613, GetType 0x0093FFCE, Compare 0x00940649 (10) |

39 functions, addresses in `data/addresses/gzui.toml`. Game code calls some of them directly (the constructor from the window constructors, the string value's constructors from the readers), so every detour is entered through a thunk that keeps ECX and EDX.

Still the game's, called by address: the map's STLport code (the find 0x0098129C, insert_unique 0x009394CF, the tree's erase 0x0040DAF0, the iterator's increment 0x0040D4C0, clear 0x00938C27 and destroy 0x00938C85, the map's constructor 0x0096F57B, the pair's destroy 0x009383F4), the byte vector's constructors and resize, the counted reference's assignment 0x0094909A, the game string's constructors, and the shared AddRef, Count and size.

The methods answer a byte; the game leaves something in the rest of EAX. Ours give the same EAX where it is known (the find's result, the type, the copied value, the clone's place) and 0 above the byte where the game leaves its caller's EAX (QueryInterface and CopyFrom refused, GetFirst with a null pointer).

### Testing

`OPENSC4_TEST_PROPSETS=<day>` makes property sets with the game's constructor (one on the heap, one on the stack) and values as the game lays them out (the string with its constructor from text and from a game string), and calls every u1 function through its vtable slot or its address: all values compared with each other through slots 7 and 8 (bytes above 0x7F, a bool's padding), copied into each other through CopyFrom and Assign (same and other types, null), cloned at a place and at null; the string value copied from another type, from itself and from another string, cloned and destroyed in place, made on the heap from a game string and deleted; then a set with values of all five types under seven keys (0 to 0x80000000 and 0xFF000001), replaced by values of other types, read back into a uint32, a float and a string (the wrong type answers false), looked for, enumerated with a null pointer half way, removed (there, not there), the interfaces, emptied, filled again and released to its end, after which the strings' reference counts are back; and a set on the stack destroyed by the destructor's address. Results go into the fingerprint's `pset` part (bytes only where the game's EAX holds an address); addresses go nowhere.

With the test on, every one of the 39 logs its first call.

An independent audit of all 39 against the disassembly (ret sizes, arguments, call order, EAX, the 5-byte entry rule) found no behavioural difference.

### Parity

Batch `gzu1-r1` (2026-10-20), muted on both sides (`OPENSC4_MUTE=1`), the base list of `scripts/run-parity.sh` (with `determinism.dispatch_info`) on both sides, `framework.propset.*=ours` on side B, two jobs:

| Leg | Saves | Result |
|---|---|---|
| Making Money d0000, 4 days | 1 | same, first pair |
| Rush Hour t0000, 7 days | 1 | same, first pair |
| 12 saves, 7 days | 12 | 12 same, first pair |
| 12 saves, 7 days, `OPENSC4_TEST_PROPSETS=2` | 12 | 12 same, first pair; `pset` 2B2F43D2 in all 24 runs |

**Reached:** in the plain legs, none. Nothing calls the property set while a city runs after PostAppInit, when the groups go in (the windows that hold sets are made while the game starts; the coverage runs saw 19 of the 39 run then). So the plain legs show only that the hooks change nothing. All 39 were reached in every B run of the test's leg.

Not compared: the game's own use of these functions while the game starts and while UI scripts are read (a group installed at Attach would be needed for that).

## Part u2: the readers and writers, the factory and what it makes

`src/core/gzui_propio.cpp`, entry detours, off by default, installed at Attach (so the game's own use while it starts would run through them too):

| Replacement | Functions |
|---|---|
| `framework.propio.binary` | the binary reader-writer's Read 0x009409E0, Write 0x0094017F and deleting destructor 0x0094000D; ReadSet 0x009408A5 and WriteSet 0x00940973, which make a binary or an XML one for a stream (5) |
| `framework.propio.xml` | the XML reader-writer's constructor 0x009406CB, QueryInterface 0x00940722, Release 0x0094070E, Read 0x00941679, its handler's EndElement 0x00940C5B and Characters 0x00940773, the destructor 0x009407CB and deleting destructor 0x009407AF; a UI script handler's SetText 0x00940868 (9) |
| `framework.propio.factory` | the factory's constructor 0x00940294, CreateFromResource 0x009400B2 (slot 3) and WriteToStream 0x00940041 (slot 6) (3) |
| `framework.propio.track` | the Track objects' ReadResource 0x00940DFF, WriteXml 0x00941636, WriteBinary 0x0094164D and ReadSet 0x00941664; the key interface's GetKey 0x00940512, SetType 0x0094052A and SetInstance 0x00940536 (7) |
| `framework.propio.window` | the window 0xA3CD96CF's constructor 0x009402A1, QueryInterface 0x00940312, destructor 0x00940371 and deleting destructor 0x00940355 (4) |
| `framework.propio.bag` | the keyed shuffle bag's constructor 0x0094048B, QueryInterface 0x009404D2, deleting destructor 0x00940552, Clone 0x00941799 and CopyAssign 0x009417D8; the bag's SetThreshold 0x009404C2, Count 0x0094067A and Assign 0x009417FF (8) |

36 functions, addresses in `data/addresses/gzui.toml` with the game's code they call. Every detour is entered through a thunk that keeps ECX and EDX (game code calls the constructors, ReadSet, WriteSet and Assign directly).

### The classes

- **The binary reader-writer** (0xC bytes: vtable 0x00AD40F8, a count, the version read at +8). The record: 0x2BEA4862, a word version (at most 1), the count, then per value its type, key and data (uint32, bool as a byte, float, string). Write writes nothing for an empty set and stops with false at a sint32 value; Read skips a value of a type it does not know without reading anything for it. The float goes out through the FPU (FLD, FSTP), as the game pushes it.
- **ReadSet and WriteSet** take the stream's seekable interface (0xC56B8F08), read the record type and rewind; for 0x2BEA4862 or 0xCBE750E0 they make a binary reader, else an XML one, and call it through a counted reference. ECX is not read (callers pass a stack address).
- **The XML reader-writer** (0x54 bytes: vtable 0x00AD442C, the handler interface at +4, a count, the element depth, three game strings for key, type and text, the set at +0x50). Read makes the parser through the COM (class 0x6B6B01D1, interface 0x6B6B01CD), reads the stream whole and runs it. Nothing in the exe registers 0x6B6B01D1 (its only use is Read's push), so in the game as it ships an XML property set is never read: Read answers false at the parser. The writer (0x0094286B, part u3) works. EndElement makes a value from the text by the type (uint32 and sint32 by 0x0090FE93, float by 0x0090FC5B, bool against `true` with no case, string as it is); Characters keeps the last chunk that has anything but whitespace (signed chars below 9 count as text).
- **The factory** (vtable 0x00AD40D0). CreateFromResource: the resource's key, an object of the key's type made by slot 4 (the Track objects, the window 0xA3CD96CF, the keyed shuffle bag), given the key, asked for the interface the caller wants, then read from the resource's stream (interface 0x051B11BD) by its key interface's slot 12. WriteToStream: the object's key interface's slot 13 to the target's interface 0xC51B11CB.
- **The Track objects** are property sets with a resource key interface at +0x18. Slot 12 of the key interface of 0x00AD2920 (ReadResource) keeps the key's instance under 0xFF6F9546 and reads the set: the TKDT text form for type 0x0B8D821A (part u2b, below), ReadSet otherwise. The set's slots 12 to 15 of 0x00AD2898 read with ReadSet and write XML or binary.
- **The key interface** (four classes share GetKey, SetType and SetInstance): type, group and instance at +4, +8 and +0xC.
- **The window 0xA3CD96CF** is the script window 0x0093E8F4 (eight interfaces) with the key interface at +0x104; its QueryInterface answers 0x3B93C5E7 and 0x456B8F1D with the key interface.
- **The keyed shuffle bag 0x7B1ACFCD** is the shuffle bag with the key interface at +0x18. Clone makes a bag with the same key and copies this one into it, the count at +4 included, so the clone's count starts one above the original's once its QueryInterface has run.

Still the game's: the 38 small thunks of these classes (25 adjustor thunks of the window, 6 of the bag, 2 of the XML handler, 5 bare jumps), as [occbase.md](occbase.md) leaves them: a `this` adjustment and a jump, or a jump alone, that ends in a function here or in the game's; the STLport list code (0x00940695, 0x009406B5, 0x00941838, 0x009418A0, 0x009418D0, 0x009418F7); and the shared getter 0x00940DA8. The u3 pieces they call (the XML writer, StartElement, the factory's Create, the shuffle bag's own slots) are ours since part u3; the keyed bag calls the shuffle bag's constructor, QueryInterface and destructor directly.

The methods answer a byte; ours give the game's EAX where it comes from the last call, and 0 above the byte where the game leaves its caller's EAX.

### Testing

`OPENSC4_TEST_PROPIO=<day>` drives every u2 function with a stream, a resource and a text sink of its own in memory (the stream's slots 0x14 to 0x44 and its seekable interface's 0x6C, 0xD0 and 0xDC; every call goes into the hash; a stream can refuse its interfaces or fail its n-th read or write):

- binary: a set with every type written by WriteSet and read back by ReadSet; every truncation of the bytes; the record 0xCBE750E0 (the binary reader, which refuses it), version 2, an unknown value type, no seekable interface, no bytes; the writer failing at each write, an empty set, a sint32; the object on the stack through its slots (the version kept), its deleting destructor with flag 0;
- XML: a set written by the game's XML writer and read back (no parser: false); the object's and its handler's QueryInterface, AddRef and Release down to its end; Characters with whitespace, text, bytes above 0x7F, 0x0B and 0x0C, length 0; EndElement at the root and with each type, a type it does not know, `TRUE`, `true ` (with a space); Read without the seekable interface and with a failed read; one on the stack destroyed by its destructor's address;
- the Track objects made by the factory's Create: written binary and XML, read back through the set's slots and the key interface's slot 12, the key interface's slots;
- the factory: CreateFromResource for both Track classes, the window, the bag (its bytes as its write lays them out), an unknown type, a resource that refuses its stream, an interface the object does not have; WriteToStream with each (and a target that refuses);
- the window: QueryInterface with twelve interface IDs through each of its nine interfaces, AddRef and Release through each, the key interface, released to its end; one made by Create;
- the bag: entries, the count, the threshold, the interfaces, the key, a clone, written and read into a third bag, all released to their end;
- the UI script handler's SetText on an object laid out as it expects.

Results go into the fingerprint's `pio` part (bytes only where the game's EAX can hold an address); addresses go nowhere. The log has a hash per stage (`autotest: propio: binary`, `xml`, `track`, `factory`, `window`, `bag`). With the groups on, each of the 36 logs its first call.

An independent audit of all 36 against the disassembly (ret sizes, arguments, call order, EAX, the 5-byte entry rule) found no behavioural difference; its one EAX note (Read's EAX after a skipped value of an unknown type, the type) is fixed.

### Parity

Batch `gzu2-r1` (2026-10-20), muted on both sides (`OPENSC4_MUTE=1`), the base list of `scripts/run-parity.sh` (with `determinism.dispatch_info`) on both sides, `framework.propio.*=ours` on side B, two jobs; `gzu2-r2` repeated two legs on the final build (the audit's EAX fix):

| Leg | Saves | Result |
|---|---|---|
| Making Money d0000, 4 days | 1 | same, first pair (r1 and r2) |
| Rush Hour t0000, 7 days | 1 | same, first pair |
| 12 saves, 7 days | 12 | 12 same, first pair |
| 12 saves, 7 days, `OPENSC4_TEST_PROPIO=2` | 12 | 12 same, first pair (r1 and r2); `pio` 1427E9F4 in all 48 runs |

**Reached:** in the plain legs, none, although the groups go in at Attach: nothing calls these classes while a parity run starts and plays (the 7 of the 36 that ran in the coverage runs come from paths those runs took and parity runs do not). So the plain legs show only that the hooks change nothing. All 36 were reached in every B run of the test's leg, and the hash of every stage matched the game's.

## Part u2b: the Track objects' TKDT text form

`src/core/gzui_propio.cpp`, entry detours, off by default, installed at Attach like u2:

| Replacement | Functions |
|---|---|
| `framework.propio.tkdt` | the Track objects' text writer 0x00940E5C (slot 13 of 0x00AD2920, 2,010 bytes) and reader 0x00941930 (slot 12, 3,186 bytes; ReadResource calls it for type 0x0B8D821A) (2) |

Addresses in `data/addresses/gzui.toml` (the "Part u2" part), with the texts, the writer's two static strings, their guard bits and their atexit destructors.

### The text form

- **The writer** builds `TKDT,2,`, an empty field, then 27 values in a fixed order, each `0x%08lx,` (the IDs: 0xFFAEC905, 0xFF5F15E1, 0xFF891EAA, 0xFF3C2160, 0xFFD7EE4D, 0xFF59FDD8, 0xFF2D7CD1) or `%ld,` (the rest), or a comma alone where the set has no value under the key (GetProperty into a uint32 value), then `ETKD`. The text goes to the stream in one write (slot 0x44); its result is not looked at, so the writer always answers true. Its `ETKD` and `,` strings are statics made on first use (0x00B633E0, 0x00B633CC, guard bits 1 and 2 of 0x00B633F4) and given to atexit.
- **The reader** reads the stream whole through its seekable interface (0xC56B8F08: the size, a dword at slot 0x34, the bytes at 0x6C). The first dword is not part of the text; when it equals the size less 4 it is a length, and one more dword is skipped. Text after a `TKD2` dword starts 8 bytes later. The text is split at commas: the first field is skipped, the second is the version, and the third (a name: the writer's empty field) is copied and never used. Then come the writer's 27 values in the same order, each put in the set as a uint32 (TextToUint32) only when its field is not empty. The sixth (0xFF706616) is read only from version 1 on, the last six only from version 2 on, and a field that is not read is not split off either. If the first value equals the set's own instance (+0x24), the reader stops with false. 0xFF891EAA is always set: to its field when that is not 0, else to 0x7DCDED2D, or to 0x7DCDED3D when 0xFFCC156F was given as a number other than 0. So the writer's text reads back.

Both answer a byte over the last call's EAX (the last free of the cleanup).

### Testing

`OPENSC4_TEST_PROPIO=<day>` has a new stage (`autotest: propio: tkdt`) after the factory's. The fake stream gained slot 0x34 (a dword on the seekable interface), and its reads no longer overflow on a size below 0.

- Writes go through a Track object of type 0x0B8D821A (made by the factory's Create, its instance set to 0x1234): with no values; with all 27 (one missing, others of float, bool, string and sint32 type; values 0, 0xFFFFFFFF, 0x80000000, 4000000000); with the write failing; and a plain set written by the writer's address.
- The written text is read back through slot 12: after a plain first dword, after a length dword (and with each of its three reads failing), and after a `TKD2` dword with 8 bytes before the text. Then every truncation, no seekable interface, and once through the key interface's slot 12 (ReadResource).
- Hand-written texts: versions 0 to 3; the first value equal to the instance (decimal and hex); the 0xFF891EAA default each way; malformed fields (`-5`, ` 7`, `0x`, `0xZZ`, 99999999999, `abc`, `+3`, `0X1F`, `1e3`); and texts with no commas, no ETKD, the wrong separator, a non-numeric version, empty text, and 3 bytes.

The `pio` token is now BB3FF4B6 (u2's 1427E9F4 plus this stage). An independent audit of both functions against the disassembly (call order, arguments, globals, EAX on both ways out) found no difference.

### Parity

Batch `gzu2b-r1` (2026-10-20) ran in the same setup as u2: muted on both sides, the base list on both sides, `framework.propio.*=ours` (with `.tkdt`) on side B, two jobs:

| Leg | Saves | Result |
|---|---|---|
| Making Money d0000, 4 days | 1 | same, first pair |
| Rush Hour t0000, 7 days | 1 | same, first pair |
| 12 saves, 7 days | 12 | 12 same, first pair |
| 12 saves, 7 days, `OPENSC4_TEST_PROPIO=2` | 12 | 12 same: 11 on the first pair, Big City d0000 after 2 pairs (side A itself gave two outcomes, in the advisor tokens; B matched one); `pio` BB3FF4B6 and `tkdt` 7193D340 in all 26 runs |

**Reached:** in the plain legs, none: no first-call line from any propio function, the TKDT reader included. The coverage runs' reader calls come from paths parity runs do not take, like the rest of u2. So the plain legs show only that the hooks change nothing. Both functions (and ReadResource) were reached in every B run of the test's leg.

## Part u3: the XML writer, the script handler, Create, the window's copy, the shuffle bag, the sound kinds

`src/core/gzui_propio.cpp`, entry detours, off by default, installed at Attach like u2:

| Replacement | Functions |
|---|---|
| `framework.propio.xmltext` | the XML reader-writer's Write 0x0094286B (slot 4), its root name 0x00942D50, the XML handler's StartElement 0x00942D87 (3) |
| `framework.propio.script` | the UI script property handler's StartElement 0x00943809 and EndElement 0x009425A2 (2) |
| `framework.propio.create` | the factory's Create 0x00942F24 (slot 4) (1) |
| `framework.propio.wincopy` | the window 0xA3CD96CF's Clone 0x0094309C (its key interface's slot 14) and CopyAssign 0x009430E1, the script window's Assign 0x00943112 and its part's 0x009436BF (4) |
| `framework.propio.shufflebag` | the shuffle bag's constructor 0x009439CF, destructor 0x00943A0E, deleting destructor 0x00943BE5, QueryInterface 0x0094392E, Release 0x00943917, Add 0x00943C01, Remove 0x00943A2E, Clear 0x0094397E, Random 0x00943953, Draw 0x00943C46, Get 0x00943A9B, Set 0x00943B07, Erase 0x00943B71, Write 0x00943CF2, Read 0x00943DBC (15) |
| `framework.propio.kinds` | the framework's own 2D sound kind's slots 5 and 6 (0x00944088, 0x00944117) and the 3D kind's (0x009441E3, 0x00944351) (4) |

29 of the range's 44 functions, addresses in `data/addresses/gzui.toml` (the "Part u3" part) with the texts and game code they use. Every detour is entered through a thunk that keeps ECX and EDX. Ours call each other directly (u2's keyed bag calls the shuffle bag's constructor, QueryInterface and destructor, Create calls u2's window and keyed bag constructors, Clone the window's constructor and CopyAssign), so a group that is off still runs ours where another group's function calls it.

Still the game's: 15 STLport functions. The script window's Assign copies its members with the containers' own assignments: the lists at +0x78 (0x009434EC, with 0x00943554 and 0x0094357B), +0x98 (0x009435B4, with 0x0094362B, 0x0094365F and 0x00943686) and the part's +0x34 (0x00943730, with 0x009437A7 and 0x009437DB), the vector at +0xA0 (0x00943376, with 0x009434B7 and 0x00943489) and the tree at +0xB4 (0x00943303); and the shuffle bag's Remove uses list::remove 0x00943995, which three other classes share. Ours call them by address (the internal ones are named in `data/names-641.txt`).

The survey's "container" (0x0094309C to 0x009437DB) is the window's copy: Clone and its Assigns and the STLport code they use.

### The classes

- **The XML writer** (slot 4 of the XML reader-writer, `ret 0xC`): nothing for an empty set (GetFirst's EAX); else `<?xml version="1.0"?>`, `<cGZPropertySetUint32>`, then per value three spaces, `<AnyUint32 key="0x..." type="0x...">` (or AnySint32, AnyFloat32, AnyBoolean, AnyString), the value's text (`%d` for both integer types, `%f`, `true` or `false`, the string as it is, nothing escaped) and the closing tag, and the root's end: every piece one write through the stream's slot 0x44. A value of another type or a failed write stops it with false.
- **StartElement** of the XML handler (`ret 0x10`) compares the first element's name with the root's and answers false unless it is; after the root it copies the `key` and `type` attributes (names compared with no case) and empties the text in place. It does not touch the depth that EndElement lowers (u2's "the depth raised" was wrong). With the parser never made (u2), neither runs in the game.
- **The UI script property handler** (constructor 0x00935E15, 0x30 bytes: the handler interface 0x00AD22C4, a second interface at +4, the count at +8, the property set at +0xC, a map of attribute names to values at +0x10, the text at +0x1C; u2's SetText 0x00940868 is its Characters). StartElement empties the text and puts each attribute in the map. EndElement takes `Name` and `Value` from the map (the text when there is no `Value`) and, by the element's name, puts a string (`PropertyUint32String`), a uint32 (`PropertyUint32Uint32`) or a float (`PropertyUint32Float32`, through a script number) in the set under the name's ID (0x00932F76) with the top byte set; the two number elements answer false, keeping the map, when the value is empty or does not start with a digit. `PropertyStringString` and other names put nothing. Then the map is emptied.
- **Create** makes the object of the type with the key (type, 0, 0) and holds it by a counted reference while it asks for the interface: the Track objects (the text form's class for 0x0B8D821A and 0x0B9EB87E, which gets 0xFF4C97BE = 0xAB903A1B and 0xFF1CDF6A = `Track`; 0x00AD2898 for 0xEBFEE345 and 0xEBFEE349), the window 0xA3CD96CF and the keyed shuffle bag. Another type: false.
- **The window's Clone** makes a window with the key, copies this one into it (CopyAssign: the script window's members and the key) and asks it for the interface. The script window's reference count is at +0x50 among the plain members Assign copies, so a clone's count starts at the original's.
- **The shuffle bag** (vtable 0x00AD4648, 0x18 bytes; interface 0xDB1ACFB2; a count of live bags at 0x00B633B8): two lists of uint32, the entries not yet drawn (+8) and the ones drawn (+0xC), a threshold (+0x10, 10 at first) and whether it is fixed (+0x14, SetThreshold fixes it). Add pushes onto the first list, Remove takes a value out of both; without a fixed threshold both make it half of both lists' sizes. Draw takes an entry at a random place of the first list (Random: the registry of sound kinds' generator, service 0x1B2CE671) and pushes it on the second; while the second holds more than the threshold its oldest go back to the first. With the first list empty it turns the second round by one. Get, Set and Erase count the first list, then the second. Write writes 1, the count and the entries; Read takes, by the stream's size, a text (entries split at commas, `a-b` adds each number of the range), version 1 with a count, or a count, and sets the threshold to a third of the first list plus 1.
- **The framework's 2D and 3D sound kinds** (vtables 0x00AD2804 and 0x00AD2830; the game's own SC4K kinds replace them, [audio.md](audio.md)): slot 5 works out a voice's values from the sound's (0x15 as 0x15 * 0x14 / 1024, halved when the registry's 0xFFAE9DDB is not 0 and the sound's is below it; the 3D kind's 0x1E and 0x1F from 0x38 when the sound's 0x1F is 0), slot 6 copies a fixed set of values. `this` is not read.

The methods answer a byte; ours give the game's EAX where it comes from a call or a value, and 0 above the byte where the game leaves its caller's EAX.

### Testing

`OPENSC4_TEST_PROPIO=<day>` has six new stages after u2's (`autotest: propio: xmltext`, `script`, `create`, `wincopy`, `shufflebag`, `kinds`):

- xmltext: a set of every type (uint32 0, 0xFFFFFFFF; sint32; floats 2.5, -0, 1e30, the largest, a NaN, the smallest denormal, -1; bools with padding; strings with markup and empty) written through the object's slot and by address, with every write failing in turn; a type the writer does not know, last and in the middle; an empty set; a real set; the root name into a string with text; StartElement before the root (wrong names, a trailing space, another case), at the root, and after it with `key` and `type` in either order, in other cases, repeated, other attributes, none and null.
- script: the handler made by its constructor with a set: elements of each property type, the value from the text, the refusals (`x12`, empty, `-5`, ` 7`) and the map they keep, `PropertyStringString`, an unknown element, no attributes, extra attributes, a hex value, `1e-3`.
- create: every class with its interface and with one it has not got, an unknown type, through the slot and by address.
- wincopy: a window with its plain members set, cloned through its key interface for three interfaces, copied into another window and into itself by CopyAssign and Assign, the part's Assign.
- shufflebag: a bag filled, its entries got, set and erased (in both lists, past the end), removed (present, twice, absent), written; draws with the threshold free, fixed at 1 and at 100 (only what does not depend on the random numbers goes into the hash: the lists' sizes, the count, the sum and XOR of the entries, whole rounds' sums), Random by address; the interfaces; Read of the written bytes, the three forms, bad sizes and texts (`5-9`, `3-1`, `1,,2`, `,7`, `-4,8-,-`, a length that does not fit); the destructors on a bag on the stack.
- kinds: fake value sets (every Get and Set into the hash) through both kinds' slots 5 and 6 with values that overflow and divide negative, the sound's 0xFFAE9DDB each side of the registry's, and the 3D slot 5 with the same object.

A muted game (`OPENSC4_MUTE=1`, as the parity runs are) has no registry of sound kinds: Random reads through a null pointer there, in the game's code as in ours (the first trial crashed on both sides). The test then leaves out the draws, Random and the kinds' slot 5, and mixes that it did.

With the groups on, each of the 29 logs its first call.

The `pio` token is now C2AC1940 in a muted game (u2b's BB3FF4B6 plus these stages). An independent audit of all 29 against the disassembly (ret sizes, arguments, call order, EAX, the 5-byte entry rule) found no behavioural difference; its one EAX note (EndElement's EAX above the byte on the number check's refusal when the value has no body) is fixed.

### Parity

Batches `gzu3-r1` to `gzu3-r3` (2026-10-21), muted on both sides, the base list (with `determinism.dispatch_info`) on both sides, every `framework.propio.*` group (u2's and u3's) `=ours` on side B, two jobs. r1 ran the first test (it crashed on both sides at the draws, which led to the registry check) and the 4-day plain leg; r2 the test legs and Rush Hour; r3, the final build (the audit's fix), the 12 saves and the Making Money test leg again:

| Leg | Saves | Result |
|---|---|---|
| Making Money d0000, 4 days | 1 | same, first pair (r1) |
| Rush Hour t0000, 7 days | 1 | same, first pair (r2) |
| 12 saves, 7 days | 12 | 12 same, first pair (r3) |
| 12 saves, 7 days, `OPENSC4_TEST_PROPIO=2` | 12 | 12 same: 11 on the first pair, Rush Hour d0090 after 2 pairs (side A itself gave two outcomes; B matched one); `pio` C2AC1940 in all 24 runs (r2) |
| Making Money d0000 and Rush Hour t0000, `OPENSC4_TEST_PROPIO=2` | 2 | same, first pair (r2; Making Money again in r3) |
| Making Money d0000, 4 days, `OPENSC4_TEST_PROPIO=2`, not muted | 1 | same after 2 pairs (side A gave two outcomes); the shufflebag and kinds stages matched (F67C12BA, FB26BA1D) |

**Reached:** in the plain legs, none of the 67 propio functions logs a first call: nothing calls this code while a parity run starts and plays, so those legs show only that the hooks change nothing. In the muted test legs 26 of the 29 were reached in every B run; Draw, Random and the 2D kind's slot 5 need the registry of sound kinds and were reached in the unmuted leg.

## Part u4: the UI file parser, its variable table and its director

`src/core/gzui_xmlparse.cpp`, entry detours, off by default, installed at Attach:

| Replacement | Functions |
|---|---|
| `framework.xmlparse.parser` | the parser's factory 0x00944462, constructor 0x00945404, destructor 0x00944B7E, deleting destructor 0x00944C2F (slot 15), QueryInterface 0x00944650, SetListener 0x0094451C (3), SetContext 0x00944589 (4), ParseString 0x00945229 (6), ParseFile 0x00945198 (7), ParseResource 0x00944C72 (8) (10) |
| `framework.xmlparse.tokens` | the parse 0x00944FB6, the tokenizer 0x00944D45, the start and end of the document 0x009445B2, 0x00944F7C, 0x00944F99, text 0x00944636, a tag's start 0x00944F3C and the end of a start tag 0x009445C8, an attribute's name 0x009449BB, its value 0x009445FD (quoted) and 0x009449DD (bare), the attribute committed 0x00944AC8, the list cleared 0x00944EEA, a name resolved to a number 0x00944940 (14) |
| `framework.xmlparse.stack` | the stack of variable tables: push 0x00944C4B (13), pop 0x009448E4 (14), lookup from the top 0x0094490C, and slots 9 to 12 on the top table, 0x0094489C, 0x00944891, 0x009448CE, 0x009448D9 (7) |
| `framework.xmlparse.table` | the variable table's factory 0x00944478, constructor 0x009453D4, destructor 0x009453ED, deleting destructor 0x0094550B, QueryInterface 0x009444FA, SetNumber 0x00945527, Set 0x009455C1, Remove 0x009452A1, Get 0x00944BDE, Clear 0x00945310 (10) |
| `framework.xmlparse.director` | the parser's director's constructor 0x0094448E and the static one's getter 0x009444CF; in the range too, the next director's (part u5) deleting destructor 0x00945636 and its five factories 0x0094565F, 0x00945675, 0x0094568E, 0x009456A4, 0x009456C8 (8) |

49 of the range's 67 functions, addresses in `data/addresses/gzui.toml` (the "Part u4" part) with the game code they call. Every detour is entered through a thunk that keeps ECX and EDX; ours call each other directly (the parse calls its steps, the parser's constructor the table's, the destructor SetListener and pop, the director's getter its constructor).

Still the game's: the two directors' ID getters 0x009444C9 and 0x00945630 (two instructions each) and 16 STLport functions: the attribute list's vector of (number, value) pairs with its own allocator object (constructor 0x00944A77, destructor 0x009446E4 shared with 0x0094C144, push_back 0x00944A48, the growing insert 0x0094470A, erase 0x00944B4E, the copies 0x00944672, 0x0094469A, 0x00944B33 and 0x00944874, the last shared with two other callers), and the table's map of game strings to values (find 0x0094481E, the strings' less 0x009446C1, insert_unique 0x0094547A and its insert 0x0094533F, subtree erase 0x00945243, clear 0x00945278, destructor 0x009452F9).

### Not XML: the UI file parser

The survey's "XML parser" reads SimCity 4's UI files, the `<LEGACY clsid=GZWinBtn area=(0,0,10,10) ...>` tag form. It knows no end tags, comments, entities or `<?xml`: a tag is `<`, a name, attributes `name=value` and `>`. Tag and attribute names must be numbers: a variable of the parser's whose value is a number (type 6), or text that starts with a digit (read by 0x0090FE93). A tag whose name is not one is skipped to its `>`; an attribute whose name is not one is skipped with its value; `/children` is just a name. A value is a quoted text (a script value given the text without the quotes; no escapes, and single quotes are not special), a variable (the variable's own value object), a number, or the word as a script value. Words end at a space, tab, line feed, carriage return, `"`, `<`, `>` or `=`. A token out of place (`<tag = 1>`, `>` outside a tag, a missing value) fails the parse.

- **The parser** (0x5C bytes, interface 0x5A2C1570): a listener at +8, which it asks for the tag events 0x1A2BDEB9 (+0xC: a tag's start, the end of the start tag, each attribute as it is read) and the attribute events 0x1A2BDEBA (+0x10: the whole list at the end of the start tag); the listener itself gets the document's start and end (with whether it failed) and the text between tags. A stack of variable tables at +0x14 (one made by the constructor; lookups go from the top down; slots 9 to 12 work on the top one), the state, text and cursor (+0x24 to +0x2C), the open tag (+0x30, +0x34; the constructor sets neither), the pending attribute (+0x38 to +0x40) and the attribute list (+0x44). Three ways in: a game string, a file (read whole) and a resource by its key (read whole through its interface 0xC56B8F08). SetContext (slot 4) keeps an object that nothing reads and the destructor does not release.
- **The variable table** 0xBA2E7954 (0x14 bytes, interface 0x1A2BDEBB): a map of names to counted script values with SetNumber, Set (both refuse a name that is there; SetNumber destroys the value it made), Remove, Get and Clear.
- **The director** (ID 0xFA2E78C2; the static one at 0x00B633F8, made by 0x009444CF on first use) registers the parser and the table by their factories.

Quirks kept: the token read when the parse turns to its stop state is not released; after a lone `"` at the end the tokenizer releases the token but leaves the caller's pointer to it; a parse already running is refused; ParseFile leaves +0x28 pointing at the buffer it deleted; a value read while another is pending overwrites it without releasing it.

**Who makes it.** Parser 0xBA2E7953 is made: the UI script tree (0x0094B73A, part u9) asks the COM for it once, defines `_null` 0, `_children` 1, `_/children` 2, `_define` 3, `_name` 4 and `_val` 5 with slot 9, and reads every UI file with it while the game starts (the coverage runs ran its parse, tokenizer and steps in all 12). The tables are made by 0x0044D810, 0x0094AF90 and 0x0094B19D. It is unrelated to u2's XML reader-writer, which asks for the class 0x6B6B01D1 with interface 0x6B6B01CD (an expat-style parser calling a handler 0x4B6B9C9B with StartElement, EndElement and Characters): nothing in the exe registers 0x6B6B01D1, and 0xBA2E7953 could not stand in for it (other interfaces, other events, no XML). So an XML property set is still never read in the game as it ships; this part changes nothing there.

The methods answer a byte; ours give the game's EAX where it comes from a call or a value (a QueryInterface's refusal leaves iid less the interface above AL), and 0 above the byte where the game leaves its caller's EAX.

### Testing

`OPENSC4_TEST_XMLPARSE=<day>` (a test of its own; the fingerprint's `xp` part) drives every u4 function through its slot or its address, with a listener of its own (its interfaces can be refused; every event goes into the hash: tags, attribute numbers and values, the attribute lists, texts, whether the document failed), script values and game strings, in seven stages logged as `autotest: xmlparse: <stage> <hash>`:

- table: the table made by its factory, its interfaces, SetNumber and Set (new, already there, the empty name, another case), Get, Remove (twice, absent), Clear, one on the stack destroyed by the destructor's address and one by the deleting destructor's.
- stack: the parser's interfaces, slots 9 to 12, push (a table, null), lookups from the top and the bottom, names resolved (variables holding a number and a text, `42`, `0x1F`, `7abc`, a word, empty, absent), pop to the last table, the context object, the listener with both interfaces, one and none.
- tokens: the tokenizer by its address over 20 texts in text mode, in tag mode and with only the high byte of the mode set: quotes (unterminated, empty, a lone one at the end, back to back), `<>=`, words up to each delimiter, whitespace, bytes above 0x7F, empty text, no cursor.
- parse: 25 documents (well formed, nested, numbers as names, variables as values, `name='q'`, quoted markup, unknown tags and attributes, malformed, truncated, empty, whitespace only, text only, a UI file's `LEGACY` form) with five listener shapes (all, no tag events, no attribute events, neither, no listener); a parse while one runs; each step by its address.
- sources: a file written to the temp folder, an empty one and a missing one; a UI file of the game's (0x00000000, 0x96A006B0, 0xCBC905CD) and a key that is not there.
- objects: a parser on the stack by the constructor's and destructor's addresses, the deleting destructor with and without its flag.
- directors: a director made on the stack, its classes through the COM's EnumClassObjects and GetClassObject (each class with its interface, with cIGZUnknown, an unknown class), the static one's getter, the next director made on the stack and destroyed by its deleting destructor, its five factories' objects made and released.

With the groups on, each of the 49 logs its first call; all 49 do in a test run. `xp` is 8D77ED96. A first trial gave a different `objects` hash on every run, side A against itself too: the test hashed the open tag's flag and number of a new parser, which the constructor leaves as they are. The test now reads them only once a parse has started.

An independent audit of all 49 against the disassembly (ret sizes, arguments, call order, EAX, the 5-byte entry rule) found no difference in behaviour. Its one EAX note (Clear of an empty table, where the game answers the map's head) is fixed.

### Parity

Batch `gzu4-r1` (2026-10-22, the final build), muted on both sides, the base list (with `determinism.dispatch_info`) on both sides, every `framework.xmlparse.*` group and every `framework.propio.*` group (u2's and u3's) `=ours` on side B, two jobs, own test folder `p4env-gzu4`:

| Leg | Saves | Result |
|---|---|---|
| Making Money d0000, 4 days | 1 | same, first pair |
| Rush Hour t0000, 7 days | 1 | same, first pair |
| 12 saves, 7 days | 12 | 12 same, first pair |
| 12 saves, 7 days, `OPENSC4_TEST_XMLPARSE=2` | 12 | 12 same, first pair; `xp` 8D77ED96 in all 24 runs |
| Making Money d0000, 4 days, `OPENSC4_TEST_XMLPARSE=2` | 1 | same, first pair |

**Reached:** unlike u1 to u3, this code runs in every game: in the plain legs 36 of the 49 log a first call in every B run (the director while the game starts, then the parser and its tables as the UI script tree reads the UI files through ParseResource, and the tokenizer and every step of the parse). Not reached without the test: ParseFile, SetContext, slots 10 to 12 and the table's Remove, EndFailed (no UI file fails to parse), and the next director's deleting destructor and five factories. In the test runs all 49 are reached.

## Part u5: the framework's sound director, its table, the sound instance and the sound description

`src/core/gzui_sound.cpp`, entry detours, off by default, installed at Attach:

| Replacement | Functions |
|---|---|
| `framework.sound.director` | the sound director's constructor 0x009456EC and the static one's getter 0x0094575A (2) |
| `framework.sound.table` | the table 0x5C1CF5A3: constructor 0x0094585F, Get 0x009458C4 (slot 3), Release 0x009457AF (2), destructor 0x00945FC4 (4) |
| `framework.sound.instance` | the instance 0xBDD9D7A5: constructors 0x009458D6 (with the service) and 0x00945C6C, Release 0x009457E1 and QueryInterface 0x009457FB (through the adjustors of the part at +4), destructor 0x00945CEE, deleting destructor 0x00945CD2, the level worked out 0x00945820, SetDesc 0x00945E70 (4; the description's slot 26 too), Update 0x009459BF (6), Start 0x00945D1A (7), Stop 0x00945A70 (8), SetLevel 0x00945A89 (9), IsPlaying 0x00945949 (10), IsActive 0x00945953 (11), 0x00945960 (13), Place 0x00945AAF (14), 0x0094596A (15), SetPlace 0x00945976 (16), SetDistances 0x00945994 (17), Restart 0x00945C20 and the 8-hex-digit test 0x00945F38 (21) |
| `framework.sound.desc` | the description 0x7DD9C9F2: constructor 0x00945DCC, QueryInterface 0x0094578A (0), Release 0x00945FFC (2), destructor 0x00945F24, SetName 0x00945E53 (24) and its own 12 setters 0x00945E64 to 0x00945F18 (slots 25 to 42) (17) |

44 of the range's 58 functions. Addresses in `data/addresses/gzui.toml` (the "Part u5" part; part u4's placeholder names `cGZUIDirector2::*` are now `cGZSoundDirector::*`, and its three constructor entries describe the classes). Every detour is entered through a thunk that keeps ECX and EDX; ours call each other directly (the table's destructor the description's, SetLevel the level, Start Restart, the deleting destructor the destructor, the getter the constructor); where the game calls through a vtable (Update's Stop and Start, Release's deleting destructor), ours do too.

Still the game's: the thunk 0x00945785; the base QueryInterface 0x009457C6 ([occbase.md](occbase.md)); the instance's AddRef 0x00945942, a 7-byte function after the constructor 0x009458D6 that Ghidra's list lacks, which five other vtables hold too (0x00AADA68, 0x00AC2A6C, 0x00ACE534, 0x00ACE580, 0x00ACE5B4); the adjustors 0x009459A7, 0x009459AF, 0x009459B7 and 0x00945C64; the two-instruction getters 0x0094595C, 0x00945E3B, 0x00945E3F and 0x00945E43; and the setters 0x00945E47, 0x00945EAC, 0x00945ED0 and 0x00945EDC, which other classes' vtables share.

### The classes

The survey took these for UI classes; they are the framework's sound layer. The audio director adds the sound director at its start ([audio.md](audio.md)), and it registers five classes: the sound service 0x3C1CF59E (0x4144 bytes, constructor 0x009489AE: part u6), the table, 0xBDDA72DD (0x190 bytes, constructor 0x00949902: part u8), the description and the instance.

- **The sound description** (0x68 bytes, interface 0x1DD9C9DE, 43 slots): a name (a game string at +0xC), a level at +0x24 (0x400 is full), a side at +0x28 and +0x2C (0x200 is the middle of 0 to 0x400), a flag at +0x31 that makes a centred side random, a time at +0x38, a loop count at +0x48 (8) and a looping flag at +0x50 (1), and a list at +0x64. The constructor leaves +0x40 as it is. Slots 23 to 42 are setters, one field each.
- **The sound table** (0x4048 bytes): a count and a few settings, then 64 by 64 descriptions at +0x48. Get (slot 3) reads a cell without checking the indexes and does not read its third argument. The destructor destroys and deletes every description in the table.
- **The sound instance** (0x5C bytes, interface 0x5DD9D787; a base part at +4, the count at +0x14). Its fields:
  - the service (+0x18) and a buffer (+0x1C: the framework's sound buffer, with interfaces 0x306077D2 and 0x5B34F0AF);
  - a state (+0x20: 0 idle, 2 playing, 3 waiting), the description (+0x24) and a loop count;
  - a level, worked out into +0x38: the description's level times +0x30, times the service's level over 0x100000, times its master level and +0x34 over 0x100000;
  - a place, distances and a listener (+0x54).

  The constructors leave +0x28, +0x30 and +0x38 as they are. Three slots do the work:
  - **Update** (slot 6) runs the states. A looping description counts its loops. Otherwise the listener is asked whether to go on (if not, it is told the sound finished, with the description's time as a float), then the buffer whether it still plays.
  - **Start** (slot 7) restarts through the listener when there is one. Otherwise it hands the buffer to the sound service and plays it: the buffer's slot 22 gets slot 20's answer, 0, the time and 1.
  - **Place** (slot 14) gives the buffer its side: x and z are (side - 0x200) / 512 times 100. A centred side is made random: a corner from the service's generator, or one forced by the bytes 0x00B6349C and 0x00B634A0, or a random sign.

Quirks kept:
- Place checks neither the buffer nor its interface 0x306077D2.
- With +0x2D set, Update stops a looping sound while its count is not 0, and leaves it alone once the count reaches 0.
- Update sets the state to 0 after calling Start.
- The 8-hex-digit test uses strchr, so a zero byte would count as a digit.

The methods answer a byte. Ours give the game's EAX where it comes from a call or a value, and 0 above the byte where the game leaves its caller's EAX.

### Testing

`OPENSC4_TEST_GZSOUND=<day>` is a test of its own (the fingerprint's `gzs` part). It drives every u5 function through its slot or its address, with a sound service, a buffer (and its place interface) and a listener of its own, each of which hashes every call it gets. It runs in eight stages, each logged as `autotest: gzsound: <stage> <hash>`:

- **desc:** a description on the stack, made over a fixed byte pattern so that the field the constructor leaves is the same in every run. The test covers its interfaces, references, the name, every setter by its slot (its own and the shared ones), the getters and the destructor, then counts one on the heap down to its deletion.
- **table:** one table made by its factory, with descriptions in its corners and inside. Get is called on them, on an empty cell and past a row's end. The table is then counted down to the game's deleting destructor, which calls ours. A second table on the stack uses the constructor's and destructor's addresses.
- **instance:** one on the stack with the service. The test covers:
  - its interfaces and references, also through the part at +4;
  - the description, the states and every setter;
  - the level with and without a buffer, and worked out over edge values (overflowing products, negative levels);
  - Stop, the hex digit test over 14 texts, and the destructor with a listener and a buffer.
- **update:** Update in every state:
  - idle and unknown states;
  - waiting and playing, with and without a listener (going on or not) and a buffer (done or not, playing or not; a stopped one starts again);
  - a looping description counting down, stopping or starting over;
  - Restart by its address.
- **start:** Start with a listener, with a buffer only and with neither, in four states with three description times.
- **place:** sides off the middle and at the int32 edges; centred sides with and without the random flag; the forced corners 0 to 3 and one past; the forced flag; no description.
- **objects:** an instance without a service destroyed by the deleting destructor's address, with and without its flag; one released to its end by the address; a description likewise.
- **director:** one made on the stack, its five classes through the COM lookup (each with its interface, with 1, with a wrong one, and an unknown class), and the static one's getter.

With the groups on, each of the 44 logs its first call, and all 44 do in a test run. `gzs` is B35CDF29.

A first trial crashed side A in the game's own code. The fake buffer's slot 22 took one argument, where Start passes four: the framework's buffer (vtable 0x00AD3FC0) has `ret 0x10`, and the game pushes three of the four before it calls slot 20. The game's Start therefore restored the wrong EBX. The fake now takes four arguments, as ours passes.

An independent audit compared all 45 functions (the AddRef included, before it went back to the game for being shared) with the disassembly: ret sizes, arguments, call order, EAX and the 5-byte entry rule. It found no difference in behaviour or EAX. Its two notes were wording in the address file (what a forced corner past 3 keeps, and what the Release methods answer), and both are fixed.

### Parity

Batch `gzu5-r2` (2026-10-22, the final build), muted on both sides, the base list (with `determinism.dispatch_info`) on both sides, every `framework.sound.*`, `framework.xmlparse.*` and `framework.propio.*` group `=ours` on side B, two jobs, own test folder `p4env-gzu5`:

| Leg | Saves | Result |
|---|---|---|
| Making Money d0000, 4 days | 1 | same, first pair |
| Rush Hour t0000, 7 days | 1 | same, first pair |
| 12 saves, 7 days | 12 | 12 same, first pair |
| 12 saves, 7 days, `OPENSC4_TEST_GZSOUND=2` | 12 | 12 same, first pair; `gzs` B35CDF29 in all 24 runs |
| Making Money d0000, 4 days, `OPENSC4_TEST_GZSOUND=2` | 1 | same, first pair |

An earlier batch (`gzu5-r1`, the same legs, all same) had the instance's AddRef 0x00945942 ours: it was the one u5 function reached in every plain run, called at start-up through one of the other classes' vtables that hold it, which is how it was found to be shared.

**Reached:** in the plain (muted) legs none of the 44 logs a first call: nothing in these runs makes a sound description, table or instance, and the sound director's getter is not reached after Attach. The legs show that the hooks change nothing. In the test runs all 44 are reached.

## Part u6: the framework's sound service, first half

`src/core/gzui_soundsvc.cpp`, entry detours, off by default, installed at Attach:

| Replacement | Functions |
|---|---|
| `framework.soundsvc.object` | the service's constructor 0x009489AE, QueryInterface 0x00946FE6 (0), Release 0x00947004 (2), destructor 0x00948C54, deleting destructor 0x00948C98 (37); the sound table's deleting destructor 0x0094601D (its slot 4) |
| `framework.soundsvc.load` | Init 0x00947C38 (3); the table's file reader 0x00946A79 and text loader 0x00946039 |
| `framework.soundsvc.play` | Prepare 0x0094701E (4), Shutdown 0x00947D28 (7), Play 0x00947DB8 (8), Stop 0x00947167 (10), SetLevel 0x00947559 (11), Fade 0x0094832A (12) and its timer's callback 0x0094761C, SetTempo 0x00948420 (17), the beat time 0x009475FB, the cell count 0x009476C3, Reseed 0x009479D4 (27) |
| `framework.soundsvc.window` | LoadByName 0x00948C11 (5), the table 0x00948B7F (6), SetRow 0x00948504 (13), SetColumn 0x009484A0 (14), the setters 0x0094776D (15), 0x0094777F (16) and 0x00947794 (21), the window's edges 0x00947996, 0x009479A1, 0x009479B5 and 0x009479C0 (22 to 25), the instance 0x00948B89 (26) |
| `framework.soundsvc.settings` | slots 28 to 35: 0x00948B9B, 0x00948BAA, the grids' size 0x00947AA9 and 0x00947AC1, the byte grid 0x00947A2A, the dword grids 0x00947A55, the key 0x00948BDD and 0x00948BFA |

40 of the range's 75 functions. Addresses in `data/addresses/gzui.toml` (the "Part u6" part; part u4's placeholder `cGZSoundDirector::Construct4144` is now `cGZSoundService::Construct`). Ours call each other directly where the game does (the destructor Shutdown, Init the reader and the reader the loader, Play, SetRow, SetColumn and Reseed the count, SetTempo the beat time); every call the game makes through a vtable stays one. Fade still hands the timer the game's thunk 0x00948321, which jumps to our callback.

Still the game's: the adjustors 0x00948C01 and 0x00948C09 of the part at +4; the AddRefs 0x0094C47C (slot 1, the framework window's) and 0x009359BD (the part at +4's); the getter 0x00948B85 (slot 9, IsPlaying, which the sound description's vtable holds too, at its slot 5); the thunks 0x00948321 (the timer's callback) and 0x00948568 (slot 36, GZRefPtr::Assign on +0x4140); and part u7's 30 functions.

### The class

The audio layer makes one service for each freshness score its tuning lists (`LoadFreshnessScore`, [audio.md](audio.md)): about 50 at start-up, each Init with an LTEXT record (type 0x2026960B; the first is group 0x0A5BCDA5, instance 0x7EC82BF0). A service plays a score: a sound table (part u5) of up to 64 by 64 descriptions, of which a window around a row and a column sounds, stepped by the beat.

The service (0x4144 bytes; vtable 0x00AD4B48, 65 slots; the message target part at +4, vtable 0x00AD4B34, whose slot 3 0x00947F58 runs the beats; interface 0xDC1CF595, and QueryInterface answers only that one, not 1):

- +8 the byte Prepare sets, +0xC the count, +0x10 a generator (0x00913A5C, seeded with -1, so from the clock), +0x14 the level (0x400 is full), +0x18 a stopwatch, +0x1C the table;
- +0x20 playing, +0x21 a byte, +0x24 the tempo in beats a minute, +0x28 the column and +0x2C the row, +0x38 the descriptions in the window and +0x50 their groups, +0x3C to +0x44 the beats (+0x44 the first), the spreads +0x54 (columns) and +0x58 (rows), +0x5C to +0x68;
- +0x74 an instance (part u5) for each description of the table, 64 by 64;
- +0x4074 a name, +0x4088, the fade +0x4091 to +0x40A0 (a timer at +0x4094, the step, the target, whether to stop at the end), +0x40A4 and +0x40A8, a 3 by 3 byte grid at +0x40BD (its four edge middles set) and three 3 by 3 dword grids at +0x40C8, +0x40EC and +0x4110 (their sizes at +0x40B4 and +0x40B8), the key at +0x4134, a reference at +0x4140.

Everything that changes the window or the play runs under one lock, a critical section at 0x00B634AC that every service shares; the timer's callback does nothing while anyone holds it.

| Slot | Function | Part |
|---|---|---|
| 0, 1, 2 | QueryInterface 0x00946FE6, AddRef 0x0094C47C (shared), Release 0x00947004 | u6, game, u6 |
| 3 | Init: the key, the record through the resource manager, its interface 0xC56B8F08 read as a raw file view into a new table, then Prepare | u6 |
| 4 | Prepare: settings from the table, an instance for every description, the window counted (slot 52) | u6 |
| 5 | LoadByName: slot 38 with the name | u6 |
| 6 | the table | u6 |
| 7 | Shutdown: the fade stopped, Stop, the table and every instance deleted | u6 |
| 8 | Play: tempo and first beat from the table, a new stopwatch, the column and row, the part at +4 handed to the framework's sound service (its slot 30, with 250) | u6 |
| 9 | IsPlaying 0x00948B85 (shared) | game |
| 10, 11 | Stop (slot 53) and SetLevel (slot 43) under the lock | u6 |
| 12 | Fade: a step a twentieth of the way, a timer every time / 20 | u6 |
| 13, 14 | SetRow, SetColumn: a place from 0 to 1000 to a row or column kept the spread off the edges, snapped to a grid when the table asks | u6 |
| 15, 16, 21 | setters of +0x34, +0x4088 and the byte +0x21 once prepared | u6 |
| 17 | SetTempo | u6 |
| 18, 19, 20 | getters 0x009CDC27, 0x009CDC23 and 0x009DB800 (shared, outside the range) | game |
| 22 to 25 | the window's first and last row and column | u6 |
| 26 | the instance at a row and column | u6 |
| 27 | Reseed | u6 |
| 28 to 35 | settings: +0x40BC, +0x40A4 and +0x40A8, the grids' size, the byte grid, the dword grids, the key | u6 |
| 36 | the thunk 0x00948568 (GZRefPtr::Assign on +0x4140) | game |
| 37 | the deleting destructor 0x00948C98 | u6 |
| 38, 39, 40 | load by path 0x00948CB4 (the name split into a folder and a file), load a file 0x00948D5F (a new table read by 0x00946B30), take a table and a name and Prepare 0x00948E0B | u7 ours |
| 41, 42, 43 | 0x00947150, stop the fade 0x00947197, every instance's level 0x0094758E | u7 ours |
| 44, 45 | the empty 0x0091559A, 0x009DB663 (shared) | game |
| 46 to 51 | 0x00948E4E, 0x009478EA, 0x009478FD, 0x00948BC3, 0x00948BD0, 0x00947914 | u7 ours |
| 52, 53 | whether a cell is in the window 0x009471EE, Stop 0x00947E90 (the part at +4 taken back from the sound service, the stopwatch deleted, the instances stopped) | u7 ours |
| 54 to 64 | 0x00948719, 0x00947220, 0x00947519, 0x009482C5, 0x00948573, 0x009477A6, 0x0094780D, 0x00947858, 0x009478BF, 0x00947925, 0x00947982 | u7 ours |

**The table's text form** (0x00946039, versions 5 to 7): a line starting `version` and the number after it, a line, 14 tab-separated settings (the level +8, a grid flag +0xC, +0x10, +0x14, the rows +0x28, the columns +0x2C, the tempo +0x30, the first beat +0x34, the spreads +0x24 and +0x20, +0x38, +0x3C, +0x18, +0x1C), a line, then a line per cell, row by row: 17 numbers (18 in version 7, the last the description's +0x60) for the description's level, flags and fields, its name, and pairs `(a,b)` for its list. A cell named `NONE` in any case is left empty. The loader checks neither the counts against 64 nor the version line's place.

Quirks kept:
- QueryInterface does not answer 1; Release deletes only at a count of exactly 1 (0 goes to -1).
- The destructor deletes the generator without its destructor.
- Init and slot 39 leave an old table where it was; Play leaves an old stopwatch.
- The pairs: the text up to the first `(` is taken off the line first, and the loop stops at the first empty piece before a `(`, so in `(1,2)(3,4)` only the first pair is read.
- The name is kept as written; only the comparison with NONE is in capitals.
- SetRow and SetColumn apply their last two limits in the opposite order.
- Fade makes a stopwatch on its stack and never starts it; SetTempo asks for the elapsed time and the beat time twice each and uses neither.

Two differences, both on input the game itself mishandles:
- The count marks the groups it has seen (the descriptions' +0x5C) in 32 bytes of its stack, unchecked: a group above 31 writes over the game's loop counters. Ours counts such a group every time.
- The loader's six flag numbers live on its stack across cells; a short first line leaves the game's at whatever the stack held. Ours start at 0.

The methods answer a byte. Ours give the game's EAX where it comes from a call or a value (most often the lock's release), and 0 above the byte where the game leaves its caller's EAX.

### Testing

`OPENSC4_TEST_GZSOUNDSVC=<day>` is a test of its own (the fingerprint's `gzv` part). It drives every u6 function through its slot or its address, on services made by the constructor's address, in seven stages, each logged as `autotest: gzsoundsvc: <stage> <hash>`:

- **load:** the loader by its address over texts in versions 7 (with and without the grid flag), 6 and 5 (NONE in either case, names with and without pairs, adjacent pairs, a short line after full ones), version 8, a capital `Version`, no version, no rows, no columns and an empty text; every table's settings and cells (fields, names and pairs) and what is left of the text. The reader by its address over a file of ours (an empty one and two texts); each table counted down to the game's Release, which calls our deleting destructor.
- **init:** Init by its slot with a key that has no record and with the score 0x2026960B-0x0A5BCDA5-0x7EC82BF0 the game ships, which reads the table and prepares the service; the service and its table.
- **play** (on that service) and **play2** (on one over a 6 by 7 table of ours): Prepare again, Play twice, SetTempo (the same tempo and a new one), the beat time by its address over edge beats and without a tempo, SetLevel, Fade (to the same level, up with a timer, again while it runs, down to a stop, refused while not playing; the timers are long and stopped before they fire), the callback by its address over steps up and down, clamped at 0 and 0x400, reaching the target or not, not fading and with the lock held, Stop, Reseed while stopped and while playing (with the generator's next draw), the count by its address over windows, Shutdown twice, the count down to deletion.
- **window:** the setters on a service not prepared; on a prepared one SetRow and SetColumn over places, spreads (0, 1, 3, -1) and the grid flag; the setters; the edges over extreme places and spreads; the instance getter; LoadByName with a name that is not there (slots 38 to 40 run, the game's, and leave an empty table).
- **settings:** slots 28 to 35 over edge values.
- **objects:** QueryInterface with four interfaces, also through the part at +4; the count down and past 0; the destructor by its address with a reference held; the deleting destructor with and without its flag; the table's deleting destructor by its slot and its address.

The constructor leaves +0x50 and the byte +0x40A0 unset, so the test reads them only after the count and Fade write them. Where the game leaves its caller's EAX above AL the test hashes only AL. With the groups on, each of the 40 logs its first call, and all 40 do in a test run. `gzv` is F68B98BA.

A first trial differed from the game in its first stage: the test's texts had one comment line too many, so the game read the settings line as the first cell and that cell's flags from its stack. A second showed the two fields the constructor leaves unset; a third the setters that answer over their caller's EAX. None was a difference in the code.

An independent audit compared all 40 with the disassembly: ret sizes, arguments, call order, the addresses and vtable offsets called, branch signedness, loop limits, field writes and their order, EAX on every path and the 5-byte entry rule. It found no difference in behaviour or EAX besides the two above, and no note on the comments or the address file.

### Parity

Batch `gzu6-r1` (2026-10-22), muted on both sides, the base list (with `determinism.dispatch_info`) on both sides, every `framework.soundsvc.*`, `framework.sound.*`, `framework.xmlparse.*` and `framework.propio.*` group `=ours` on side B, two jobs, own test folder `p4env-gzu6`:

| Leg | Saves | Result |
|---|---|---|
| Making Money d0000, 4 days | 1 | same, first pair |
| Rush Hour t0000, 7 days | 1 | same, first pair |
| 12 saves, 7 days | 12 | 11 same, first pair; Big City d0090 different (A one outcome, B two: b0 alone, b1 and b2 the same as A); rerun `gzu6-r2`: 12 same, first pair |
| 12 saves, 7 days, `OPENSC4_TEST_GZSOUNDSVC=2` | 12 | 12 same, 11 on the first pair (Rush Hour d0000 after two: the game side gave two outcomes); `gzv` F68B98BA in all runs |
| Making Money d0000, 4 days, `OPENSC4_TEST_GZSOUNDSVC=2` | 1 | same, first pair |

The Big City d0090 pair in `gzu6-r1` ran none of the 40 (no first call in any of its B runs), and B matched A in two of three runs: the load noise of the original, which the rerun confirms.

**Reached:** in the plain legs none of the 40 logs a first call. The legs are muted (`-audio:off`), and a muted game makes no score, so no service. An unmuted run with the groups on (Making Money, 30 s) reached 26 of them at start-up and shutdown: the constructor, QueryInterface, Release, Init, SetKey, the reader, the loader, Prepare, slots 28 and 29, Play, the count, the four edges, SetColumn, SetRow, SetLevel, slot 16, the beat time, Stop, Shutdown, the table's deleting destructor, the service's deleting destructor and the destructor. In the test runs all 40 are reached.

**What u7 has left:** the 30 other functions of the range: slots 38 to 43 and 46 to 64 (0x00948CB4, 0x00948D5F, 0x00948E0B, 0x00947150, 0x00947197, 0x0094758E, 0x00948E4E, 0x009478EA, 0x009478FD, 0x00948BC3, 0x00948BD0, 0x00947914, 0x009471EE, 0x00947E90, 0x00948719, 0x00947220, 0x00947519, 0x009482C5, 0x00948573, 0x009477A6, 0x0094780D, 0x00947858, 0x009478BF, 0x00947925, 0x00947982), the message target's slot 3 0x00947F58 (the beats) and its helper 0x00947AD9 (also slot 57's), slot 39's file reader 0x00946B30, and the sound instance's slot 5 0x00946C2C with its helper 0x0094680F (both call u5's 8-hex-digit test). 14 of the 30 ran in the survey's coverage runs.

## Part u7: the framework's sound service, the rest

`src/core/gzui_soundsvc.cpp`, entry detours, off by default, installed at Attach:

| Replacement | Functions |
|---|---|
| `framework.soundsvc.file` | load by path 0x00948CB4 (38), load a folder and file 0x00948D5F (39), take a table and a name 0x00948E0B (40), the name 0x00948E4E (46); the table's reader by path 0x00946B30 |
| `framework.soundsvc.control` | level and play 0x00947150 (41), stop the fade 0x00947197 (42), every level 0x0094758E (43), the spreads 0x009478EA (47) and 0x009478FD (48), the setters 0x00948BC3 (49), 0x00948BD0 (50, the pause) and 0x00947914 (51), the unlocked Stop 0x00947E90 (53), a cell again 0x00947519 (56) |
| `framework.soundsvc.count` | in the window 0x009471EE (52), the counts 0x009477A6 (59, active), 0x0094780D (60, marked) and 0x00947858 (61, playing), 0x009478BF (62), how many may sound 0x00947925 (63) and the spare 0x00947982 (64) |
| `framework.soundsvc.beat` | the beats 0x00947F58 (the message target's slot 3), the neighbour pick 0x00947AD9, the best cell 0x00948719 (54), one to let go 0x00947220 (55), play a cell 0x009482C5 (57), a cell's score 0x00948573 (58) |
| `framework.soundsvc.instance` | the sound instance's play 0x00946C2C (its slot 5) and its listener path 0x0094680F |

All 30 of the part's functions. Addresses in `data/addresses/gzui.toml` (the "Part u7" part, with the audio system's getter 0x00913CCA, four file spec getters, the semitone and chance tables, the counters and the two cached objects the test swaps). Ours call each other directly where the game does (the beats the pick and u6's beat time, slot 57 the pick, slot 39 the reader, the reader u6's loader, the play its listener path); every call the game makes through a vtable stays one. Nothing in the range is left to the game but what part u6 left: the adjustors 0x00948C01 and 0x00948C09, the AddRefs 0x0094C47C and 0x009359BD, the getter 0x00948B85 and the thunks 0x00948321 and 0x00948568; the shared slots 18 to 20, 44 and 45 are outside it.

### The beats

The framework's sound service calls the message target's slot 3 for each service that plays (u6's Play hands it the part at +4, with 250). Under the lock it runs every beat the stopwatch has passed:

- On a beat one or six before the bar's end (the table's +0x34), with as many sounding as slot 63 wants, slot 55 picks one to let go (its byte +0x2D marked: Update lets a marked looping sound end); with fewer active unmarked than slot 64's, slot 54 finds the best cell in the window, the pick 0x00947AD9 places its instance by the neighbour grids (part u6's slots 28 to 33), and it gets its description, the service's reference at +0x4140 (as its listener) and plays.
- Then every instance's Update, and the beat steps.

A cell's score (slot 58) is 0 when it cannot sound (busy, outside the window, the last cell of slot 56, or another of its group or of a cell its pairs name sounding), 0x64 at weight 20, 2 for the group asked, else 1 when `rand()` falls below the weight's chance over the cells in the window. The play (the instance's slot 5) asks the audio system for a buffer by a hex name's number or by the file's name, places it, plays it, sets its level and a pitch from the description's range of semitones; with a listener it hands all of that to the listener instead.

Quirks kept:
- Slots 52, 55, 56, 57 and 58 do not check the instances: an empty cell in the table makes them read a null instance (only full tables are tested).
- Slot 55 allocates two buffers the size of the window and never frees them; slot 54 makes a list for nothing (and walks it for group 3).
- The listener path reads a negative semitone index at its opposite and does not divide; the buffer path divides. Neither checks the index against the table's 13.
- The beats count calls, beats, tries and plays in globals (0x00B634A8 to 0x00B634CC) that nothing reads; slot 39 ignores the reader's answer; slots 39 and 40 leave an old table where it was.

The methods answer a byte. Ours give the game's EAX where it comes from a call or a value, and 0 or 1 above the byte where the game leaves its caller's EAX (slots 42, 43, 50, 53 and 62 on their early paths).

### Testing

`OPENSC4_TEST_GZSOUNDSVC=<day>` (part u6's test, the fingerprint's `gzv` part) has five more stages. For the whole test (part u6's stages too) ours stand in for the sound service and the audio system the game caches at 0x00B628C8 and 0x00B628CC, with buffers, a listener and a stopwatch of our own, so nothing reaches real audio:

- **slots:** on a prepared 4 by 5 table, slots 41 to 53 (Play taken by our sound service, a fade stopped, the levels, the name, the spreads, the setters, the window over cells and spreads), 59 to 64 over five patterns of states and marks, the shares and sizes of slot 63, slot 56 on cells in each state, slot 53 letting the target go.
- **pick:** slot 55 (strays outside the window, inside it, none), slot 58 on every cell with and without a group (the cells in the window set so that `rand()` never decides: huge, or 1 with weights of 11 and up; the scores of a chance between 1 and 32767 are not hashed), slot 54, slot 57 and the pick by address over the grid set, set in one cell or all, and the last pick kept.
- **beat:** the beats by the target's address on our stopwatch (the game's mode 1, stopped): not playing, paused, times before, on and past beats over three patterns, the four settings that stop letting go and playing, a day passed, the counts cleared after 10 s; the game's counters are set first and put back after.
- **file:** a table text written to the temporary folder, loaded by slot 38 (a path) and 39 (a folder and a file, and one that is not there), the reader by its address (the path by value), slot 40 with a table and slot 46; the score's name (the folder) is checked, not hashed.
- **instance:** the play over six names (a hex number, a hex 0, plain names, paths), random levels, looping, pitch ranges, place and time, with the audio system refusing, giving no buffer or one (with and without its two optional interfaces), through the listener (accepting or not) and its path by address, and busy.

With the groups on, each of the 30 logs its first call, and all 30 do in a test run. `gzv` is now 06E40C6A (part u6's `load` and `init` hash as before; from `play` on, the stand-ins' calls go into the hash too).

A first unmuted trial of the test died on the game's side in part u6's play stage: a service played by the real sound service gets its beats from the audio layer while the test changes it. Muted games make no audio layer, which is why part u6's runs never showed it. With our stand-ins the test runs unmuted too, the same on both sides. Two later faults were the test's, found by parity: the batch `gzu7-r1` had three saves of the test leg differ, the game's side as much as ours, because the test hashed slot 42's answer while a fade ran (the timer's deletion, whose EAX is its address); then the token differed between a plain run and one with the parity base list, because the test hashed three fields the instance's constructor leaves unset (+0x28, +0x30, +0x38, heap contents). The test now sets them, and hashes only what the play writes in an instance.

An independent audit compared all 30 with the disassembly: ret sizes, arguments and their order, call order, the addresses and vtable offsets called, field offsets (the beats run on the part at +4), signedness, loop bounds and whether they are read again, the x87 conversions, EAX on every path and the 5-byte entry rule. It found no difference; its one wording note is fixed.

### Parity

Batch `gzu7-r3` (2026-10-22, the final build), muted on both sides, the base list (with `determinism.dispatch_info`) on both sides, every `framework.soundsvc.*`, `framework.sound.*`, `framework.xmlparse.*` and `framework.propio.*` group `=ours` on side B, two jobs, own test folder `p4env-gzu7`:

| Leg | Saves | Result |
|---|---|---|
| Making Money d0000, 4 days | 1 | same, first pair |
| Rush Hour t0000, 7 days | 1 | same, first pair |
| 12 saves, 7 days | 12 | 12 same, first pair |
| 12 saves, 7 days, `OPENSC4_TEST_GZSOUNDSVC=2` | 12 | 12 same, first pair; `gzv` 06E40C6A in all 24 runs |
| Making Money d0000, 4 days, `OPENSC4_TEST_GZSOUNDSVC=2` | 1 | same, first pair |

The two earlier batches on the same code ran the same legs: `gzu7-r1` all same but the test leg's three saves above (the test's fault), `gzu7-r2` all same on the first pair (its token still the environment's).

**Reached:** in the plain (muted) legs none of the 30 logs a first call: a muted game makes no score. An unmuted run with the groups on (Making Money, 30 s, no test) reached 14 of them, with real sounds: the beats, the neighbour pick, slots 54 and 58, the instance's play, the counts (59 to 61), slots 63 and 64, slot 52, slot 43, the fade's stop and the unlocked Stop. In the test runs all 30 are reached.

**Part u6 and u7 complete:** the range 0x00946000 to 0x00948E62 is ours but for the adjustors, AddRefs, the shared getter and the two thunks.

## Part u8: the framework's sound mixer

`src/core/gzui_soundmix.cpp`, entry detours, off by default, installed at Attach:

| Replacement | Functions |
|---|---|
| `framework.soundmix.object` | the constructor 0x00949902, QueryInterface 0x00948E7A (0), Release 0x00948E63 (2), the destructor 0x00949BC7, the deleting destructor 0x00949BAB (5) |
| `framework.soundmix.play` | Prepare 0x009490E6 (interface slot 4), add a score 0x00949C27 (5), a score's table 0x00949348 (6), Play 0x00949100 (8), IsPlaying 0x00949AE1 (9), Stop 0x0094915A (10); a score on or off 0x00949EA4 (primary slot 4), the pool 0x00949D19, the tick 0x0094970D (the message target's slot 3), the neighbour pick 0x00948F62 |
| `framework.soundmix.control` | the interface's slots 11 to 16 (0x00949222, 0x00948EBB, 0x00949FF9, 0x00949FB9, 0x0094A039, 0x00949E8D), 20 (0x00949B40), 21 (0x00949AE8), 26 (0x00949B4A), 27 (Reseed 0x00949F6F); the primary slots 6 to 8 (0x00949B1C, 0x00949B39, 0x00949B76) |
| `framework.soundmix.grid` | the interface's slots 28 to 35: 0x00949AF7, 0x00949B03, 0x00948F38, 0x00948F4D, 0x00948ED1, 0x00948EFB, 0x00949B4F, 0x00949B6C |

36 of the range's 71 functions. Addresses in `data/addresses/gzui.toml` (the "Part u8" part; part u4's placeholder `cGZSoundDirector::Construct190` is now `cGZSoundMixer::Construct`, and part u4's director makes it under that name). Ours call each other directly where the game does (the setters and Reseed the pool, the tick the pick, the deleting destructor the destructor); every call the game makes through a vtable stays one (Add turns the new score off through the primary slot 4).

Still the game's, 35:
- the five adjustors 0x00949B83, 0x00949B8B, 0x00949B93, 0x00949B9B and 0x00949BA3;
- the shared getters 0x00949B2B (interface slot 19) and 0x00949B32 (18), which two other vtables hold, and the shared stubs 0x00949ADE (slot 7, `mov al, 1`), 0x00949B47 (slots 22 to 25, `xor eax, eax`) and 0x00949B73 (36, `ret 4`), which dozens to hundreds of other vtables hold;
- the STLport code the class's maps and list instantiate, 20 functions: the node and pair constructors 0x009490C9, 0x00949188 and 0x00949327, the erases 0x009491A5, 0x009491D2, 0x009492F0 and 0x009493C7, the clears 0x00949279, 0x009492A2, 0x0094939E and 0x009495A5, the destructors 0x00949370, 0x00949387, 0x0094958E and 0x00949873, the inserts 0x009493FE, 0x00949486 and 0x009495CE, 0x0094988A and the map's operator[] 0x009498A7;
- and the counted-reference and list code other classes share: GZRefPtr::Assign 0x0094909A (84 callers), the reference holder's destructor 0x009491FF, the list's erase 0x00949259, the map's lower bound 0x009492CB and the insert 0x0094950A.

The AddRef 0x0094D34F and the slots 0x009CFA36 (primary 3) and 0x0093878E (interface 3 and 17) are outside the range.

### The class

The sound director registers it as 0xBDDA72DD (0x190 bytes, interface 0xDDDA72F1). It answers the sound service's interface 0xDC1CF595 too, with a part at +4 whose 37 slots follow the sound service's first 37 (part u6): a mixer can stand where a score can. It holds several scores (sound services made through the COM) under keys, turns them on and off, and plays their instances itself instead of their beats. Three vtables: 0x00AD4CF4 (9 slots), the interface's 0x00AD4C60 (37) and the message target's 0x00AD4C4C (4), whose slot 3 the framework's sound service calls once Play has handed it the target (with 250, as the sound service's Play does).

- +0x10 the count, +0x14 the framework's sound service (Prepare);
- maps (STLport, by uint32 key): +0x18 the scores, +0x24 their tables, +0x30 whether each is on, +0x3C the pool of instances and +0x48 their descriptions (by an index); a list +0x54 of the instances that sound;
- the neighbour grids as the sound service's (its slots 28 to 33): the size +0x58 and +0x5C (2), the byte +0x60, a 3 by 3 byte grid +0x61, three 3 by 3 dword grids +0x6C, +0x90 and +0xB4, the distances +0xD8 and +0xDC (100 and 1000), the last pick +0xE0;
- a stopwatch +0xE8, its start +0x100 and last time +0x104; the pool's size +0x108, its weights +0x10C, the scores on +0x110; a generator +0x118;
- running +0x134, the level +0x138, the column +0x13C, the row +0x140, +0x144, +0x14C, +0x164, the bytes +0x16C, +0x16D and +0x17C, the key +0x180.

**The pool** (0x00949D19) is made again whenever a score is turned on or off and whenever the row, column, slot 15, slot 16 or the seed change: every instance in the window of each score that is on (its key not 0), with its description, whose weight adds up. Nothing changes while the framework's sound service answers its slot 49.

**The tick** (0x0094970D): the first sounding instance that no longer plays leaves the list; with a level, every 7 less the scores on (at least 2) seconds and while fewer than 4 sound, an instance of the pool is drawn by weight, joins the list, gets the level and a place from the neighbour grids (0x00948F62, the sound service's 0x00947AD9 on the mixer's fields) and plays; then every sounding instance's Update.

Quirks kept:
- Release deletes at a count of 1 or less (signed), so 0 and below delete too.
- The tick asks the first listed instance whether it plays without a null check, and lets only one go per tick; slots 13 to 15 and 27 do not check the scores they hold.
- The pool's descriptions map is never cleared, only written over; the destructor leaves the generator.
- Reseed seeds the mixer's generator once for each score held (with none, not at all).
- Turning a score off gives every instance of its window its slot 7 and leaves the list of sounding instances as it is.
- Slot 5 keeps a counted reference to the score's table (+0x24), but the score's own Shutdown deletes its table regardless; the destructor releases the tables before the scores, so a mixer destroyed while it holds a score of slot 5 deletes that table twice. The game never makes a mixer (below), so it never happens there.

The methods answer a byte. Ours give the game's EAX where it comes from a call or a value, and 0 or 1 above the byte where the game leaves its caller's EAX (QueryInterface refused, IsPlaying, Play while running, Stop while not, slots 12, 21 and 28, the primary slot 8).

### Testing

`OPENSC4_TEST_GZSOUNDMIX=<day>` is a test of its own (the fingerprint's `gzm` part). The framework's sound service the game caches (0x00B628C8) is ours while it runs (with the audio system of part u7's test), and so are the mixer's scores, instances and descriptions in all but the last stage; each hashes every call it gets. Seven stages, each logged as `autotest: gzsoundmix: <stage> <hash>`:

- **objects:** the constructor by its address and every field it sets; QueryInterface with four interfaces through each of the three parts; Release through each part, by its address and below 0; the end with a sound service held; the destructor and the deleting destructor (flag 0) by their addresses.
- **grid:** slots 28 to 35 over edge values; the pick by its address over no cell set, one, all, the last pick kept, grid sizes 0 to 2, on a seeded generator.
- **control:** slots 11 to 16, 20, 21, 26 and 27 and the primary 6 to 8, with no score and with four (one under key 0) and two sounding instances; the setters while the sound service waits.
- **pool:** scores turned on and off (keys held, not held, 0, a score missing, a flag above a byte), the pool by its address over an empty window, missing descriptions and a large weight; while the sound service waits; a mixer without one.
- **tick:** the tick by the target's address on the mixer's stopwatch set to a time of ours (mode 1, stopped): no level; times before, on and past the wait with 3, 5, 9 and a negative count of scores on; a time below the last; four sounding; none playing (one leaves a tick); a weight that reaches every draw; waiting; a null listed instance behind a finished one.
- **play:** Prepare, Play, IsPlaying and Stop twice each, the table getter.
- **add:** slot 5 with a score file of ours and with one that is not there; a sound service of part u6 over a table of ours, held by hand; those keys turned on, the settings, Reseed, turned off; the mixer counted down to its end, which releases them.

With the groups on, each of the 36 logs its first call, and all 36 do in a test run. `gzm` is 54A5D5D9 (muted).

In a muted game the COM has no sound service class (the audio layer registers the sound director), so slot 5 answers its refusal there; an unmuted test run, where the COM makes the score, gives `add` another hash (F530DDC9), the same on both sides. A first unmuted trial died there on both sides on the table deleted twice (the quirk above); the test now drops the mixer's table references before its end.

Two faults of a first trial were the test's: the tick answers the list's header when nothing sounds and the test hashed its low byte; Stop while not running answers over its caller's EAX and the test hashed all of it. A first build also refused the primary slot 7: its address entry's expected bytes ran into slot 20's first byte, which the same group had already hooked. The entries of the range now expect only the function's own bytes.

An independent audit compared all 36 with the disassembly: ret sizes against the thunks, arguments and their order, call order, the addresses and vtable offsets called, field offsets, signedness, loop bounds and whether they are read again, null checks, EAX on every path and the constructor's and destructor's writes. It found no difference; its two wording notes on the comments are fixed.

### Parity

Batch `gzu8-r2` (2026-10-23, the final build), muted on both sides, the base list (with `determinism.dispatch_info`) on both sides, every `framework.soundmix.*`, `framework.soundsvc.*`, `framework.sound.*`, `framework.xmlparse.*` and `framework.propio.*` group `=ours` on side B, two jobs, own test folder `p4env-gzu8`:

| Leg | Saves | Result |
|---|---|---|
| Making Money d0000, 4 days | 1 | same, first pair |
| Rush Hour t0000, 7 days | 1 | same, first pair |
| 12 saves, 7 days | 12 | 12 same, first pair |
| 12 saves, 7 days, `OPENSC4_TEST_GZSOUNDMIX=2` | 12 | 12 same, first pair; `gzm` 54A5D5D9 in all 24 runs |
| Making Money d0000, 4 days, `OPENSC4_TEST_GZSOUNDMIX=2` | 1 | same, first pair |

The batch `gzu8-r1` on the build before the test's last change (which touches only the unmuted `add` stage) ran the same legs with the same results.

**Reached:** in the plain (muted) legs none of the 36 logs a first call. An unmuted run with the groups on (Making Money, 30 s, no test) reached none either, while the sound services of parts u6 and u7 ran (40 of their first calls): the game registers the mixer's class but never makes one in play. In the test runs all 36 are reached.

**Part u8 complete:** the range 0x00948E63 to 0x0094A078 is ours but for the adjustors, the shared getters and stubs and the STLport and shared reference and list code.

## Part u9: the UI script loader and its director

`src/core/gzui_uiscript.cpp`, entry detours, off by default, installed at Attach:

| Replacement | Functions |
|---|---|
| `framework.uiscript.object` | the director's start 0x0094AF1F (slot 4), constructor 0x0094AFF6, deleting destructor 0x0094AFCD (16) and the static one's getter 0x0094B053; its four factories 0x0094AF64 (the loader, whose constructor 0x0094B400 ours does inside the factory), 0x0094AF7A, 0x0094AF90 and 0x0094AFA6; the loader's QueryInterface 0x0094B120 (0), Create 0x0094B08B (3), Load 0x0094B210 (4), SetHeld 0x0094B0C7 (10), SetContext 0x0094B0F3 (slot 4 at +4), Init 0x0094B07E and Shutdown 0x0094B5F4 (the base service's slots 8 and 9), the destructor 0x0094B6EC and the deleting destructor 0x0094BBE2 (17) |
| `framework.uiscript.parse` | the parser made and its words defined 0x0094B73A, ParseString 0x0094BA22 (5), ParseFile 0x0094BA51 (6), ParseResource 0x0094BA80 (7), the parser's getter 0x0094BAAF (8), the parent set 0x0094B676 (slot 3 at +4) (6) |
| `framework.uiscript.events` | the parser's events: the document's start 0x0094B19D and end 0x0094B55D (the listener's slots 3 and 4), a tag's start 0x0094B364 and the end of its start tag 0x0094B1F4 (the tag events' 3 and 4), the attribute list 0x0094BABC (the attribute events' 3); the stack of parents: its top 0x0094B354, push 0x0094B653 and pop 0x0094B53A (8) |

31 of the 54 functions of 0x0094AF19 to 0x0094BBFD, the first half of the range by count (54 of 108; 26 of its 51 that ran). Addresses in `data/addresses/gzui.toml` (the "Part u9" part) with the game code and data they use. Ours call each other directly where the game does (the parses the parser's maker, the destructor Shutdown, the events the stack's top, push and pop, the factory the constructor); every call the game makes through a vtable stays one (Create calls Load, Load calls ParseResource and the parent's setter through the vtables).

Still the game's, 23: the 19 adjustors 0x0094B4A2 to 0x0094B532 (three per interface vtable and the deleting destructor's 0x0094B532), the base service's AddRef and Release adjustors 0x0094B18D and 0x0094B195 (`add ecx, 0x14` and a jump to cRZBaseSystemService's), the director's ID getter 0x0094AF19 (`mov eax, 0x9A3559B0`), and the list node 0x0094B33A, an STLport node maker two other lists call. The primary slot 9 (0x009D41AD, `mov eax, [ecx+0x5C]`) and the listener's and tag events' slot 5 (0x0092B33A, 0x0092973B: `mov al, 1`) are shared code outside the range.

### The class

The director (ID 0x9A3559B0, the static one at 0x00B63538, made by its getter, which 0x0046019A calls) registers four classes and, at its start, makes the loader through its COM with interface 0x287FB697 and gives it to the framework as a system service:

| Class | Factory | Size | What |
|---|---|---:|---|
| 0xFA3562FB | 0x0094AF64 | 0x60 | the loader (below) |
| 0x5A354595 | 0x0094AF7A | 0x24 | the root holder a parse fills with the windows it makes (constructor 0x0094C0AF, part u11's range); slot 4 takes a reader, slot 5 gives a window by its id |
| 0xFA450242 | 0x0094AF90 | 0x20 | the LEGACY reader: the class of the `LEGACY` tag (constructor 0x0095B8C4, which once makes a variable table at 0x00B63588 and has every widget reader add its words, 21 calls) |
| 0x478D1E6F | 0x0094AFA6 | 0xDC | (constructor 0x0095BAB3; the factory answers its interface at +0xD8) |

**The loader** is the framework's system service 0x5A356E15 (cRZBaseSystemService at +0x14 with priority 0xFFD23940). Seven vtables: the primary one 0x00AD5158 (11 slots), the interface 0x7A4A48ED at +4 (0x00AD5144: the parent, the context), and the three faces it shows the UI file parser of part u4: its listener 0x1A2BDEB8 at +8 (0x00AD512C), its tag events 0x1A2BDEB9 at +0xC (0x00AD5114) and its attribute events 0x1A2BDEBA at +0x10 (0x00AD5104); the base service's at +0x14 and +0x18 (0x00AD50D0, 0x00AD50B8). Fields: +0x30 Init ran, +0x34 the parser, +0x38 the variable table pushed at a document's start, +0x3C set while the words are defined, +0x40 the stack of parents (an STLport list, its top at the front), +0x44 the last reader (not counted), +0x48 the reader's table pushed, +0x4C the tag, +0x50 the root holder, +0x54 the tag's reader, +0x58 the readers' context, +0x5C slot 10's object.

**How a UI script is read.** The first parse makes the parser (0xBA2E7953) and defines `_null` 0, `_children` 1, `_/children` 2, `_define` 3, `_name` 4 and `_val` 5, then (with the listener given) `none`, `children`, `/children`, `define`, `name`, `val` the same, `LEGACY` 0xFA450242, `comic9` 0x12 and `comic10` 0x13. A parse gives the parser the listener and the source (a game string, a file or a resource key) and answers the root holder, which the caller then owns:

- the document's start pushes a fresh variable table on the parser and makes the root holder;
- a tag's start makes the tag's number as a class through the COM (interface 0x1A356083): a reader. It gets the context, the stack's top as its parent, and its own variable table goes on the parser for the tag's attributes (`clsid`, `area` ...), popped at the end of the start tag. A tag whose number is no class is refused;
- the attribute list goes to the reader, which makes its window, and the reader to the root holder. With no reader, `children` pushes the last reader as the parent of what follows, `/children` pops back, and `define name="x" val=...` sets a parser variable (only text names; without `val` it is removed), so a later tag or value can use `x`;
- the document's end lets go of the table and empties the stack; a failed document also drops the root holder.

Load (slot 4) reads a UI file by its resource key with the parent window (slot 3 at +4 puts a LEGACY reader for it at the bottom of the stack) and answers the window under an id; the key's group is first tried in the language's form (`0x%.4u%.4u` of the locale service's slots 41 and 42). Create (slot 3) asks that window for an interface.

Quirks kept: the document's start overwrites +0x38 and +0x50 without releasing what they held; the end releases +0x38 without a null check; +0x44 holds the reader without a reference while the stack holds one; slot 10 adds the new reference before it releases the old one, the context setter the other way round; Init only sets its byte, and Shutdown does nothing without it (so the destructor of a loader never Init'ed leaves its parser).

The methods answer a byte. Ours give the game's EAX where it comes from a call or a value, and 0 or 1 above the byte where the game leaves its caller's EAX (Init, slot 10 when nothing changes, a push or Shutdown with nothing to do, the end of a start tag without a table).

### Testing

`OPENSC4_TEST_UISCRIPT=<day>` is a test of its own (the fingerprint's `us` part). It drives the loader through its slots and addresses with real loaders, parsers, variable tables, root holders and LEGACY readers where the game makes them, and readers, root holders, windows, values and a parser of its own where a field or an argument can hold one (each hashes every call it gets). Four stages, each logged as `autotest: uiscript: <stage> <hash>`:

- **objects:** a loader by the factory's address and its fields; QueryInterface with eight interfaces by its address and through the listener's adjustor; Init and Shutdown twice each, by slot and address; slot 10 and the context setter over the same object, another, none; the destructor and the deleting destructor (flags 0 and 1) by their addresses; the other three factories' objects made, held and released; the director made on the stack, its start with a COM of ours that has no loader, its deleting destructor (flag 0); the static director's getter.
- **stack:** pop and push on an empty stack, three readers pushed, the top after each, `/children` and `children` through the attribute events with and without a reader, pops past the bottom, a loader released with readers on its stack.
- **events:** the document's start (a real table and root holder), tag starts with numbers that are no class, the end of a start tag with and without a table; the attribute list with a reader and a root holder of ours; `define` with a name and a value, the pairs reversed, a name only, a name that is not a text, no name, no pairs; tags that are none of these; the document's end done and not, and while the words are defined; Load and Create (slot and address) with a parser and root holder of ours.
- **parse:** the loader's own parser made and its words defined (twice), 19 UI scripts through ParseString's slot and address: empty, white space, text only, defines (and a defined word used as a tag, then as a name), numbers as names, a name that is not a text, the `_` words, nested and unbalanced `children`, unknown tags, malformed and truncated, and LEGACY windows (`GZWinGen`, one alone and one with a child: the root holder gives the windows by their ids); a file, an empty one and a missing one; a UI file of the game's (0x00000000, 0x96A006B0, 0xCBC905CD) and a key that is not there through ParseResource, Load and Create; the parent set on its own; Shutdown.

With the groups on, each of the 31 logs its first call (and the constructor inside the factory as `LoaderConstruct`), and all 32 do in a test run. `us` is 9F09A91A (muted), the same on both sides.

A first trial died in the parse stage on both sides: the test's loader had no reference of its own, and the parser, swapping the listener for the same one, released the loader to nothing in the middle of the parse. The game's loader is always held by the framework; the test now holds each of its loaders.

An independent audit compared all 31 with the disassembly: ret sizes against the thunks, arguments and their order, the vtable offsets and addresses called (and their conventions), call order, the reference counts, field offsets through each interface's `this`, branches, EAX on every path, the constructor's writes and the address entries' expected bytes. It found no difference in behaviour; its three notes on the text (the director's deleting destructor is its slot 16, not 15; two methods' comments lacked "ours 0" where the game leaves its caller's EAX) are fixed.

### Parity

Batch `gzu9-r2` (2026-10-23, the final build), muted on both sides, the base list (with `determinism.dispatch_info`) on both sides, every `framework.uiscript.*`, `framework.soundmix.*`, `framework.soundsvc.*`, `framework.sound.*`, `framework.xmlparse.*` and `framework.propio.*` group `=ours` on side B, two jobs, own test folder `p4env-gzu9`:

| Leg | Saves | Result |
|---|---|---|
| Making Money d0000, 4 days | 1 | same |
| Rush Hour t0000, 7 days | 1 | same |
| 12 saves, 7 days | 12 | 12 same |
| 12 saves, 7 days, `OPENSC4_TEST_UISCRIPT=2` | 12 | 12 same; `us` 9F09A91A in all 24 runs |
| Making Money d0000, 4 days, `OPENSC4_TEST_UISCRIPT=2` | 1 | same; `us` 9F09A91A |

The batch `gzu9-r1` on the build before the audit's fixes (comments and address-entry text only) ran the same legs: all the same, on the first pair, `us` 9F09A91A in all 26 test runs.

**Reached:** this code runs at every start-up. In every plain (muted) leg side B logs 28 first calls: 27 of the 31 and the constructor. The director is got, made and started, the loader made, held and Init'ed, its parser made, and every UI file read through Load and ParseResource with the parent set, the document's and tags' events, the stack, the LEGACY reader, the root holder and the class 0x478D1E6F; Create, and at the end Shutdown and the destructors. Not reached in play: SetContext, ParseString, ParseFile and the director's deleting destructor (the static director is never destroyed before the process ends). The test runs reach all 31.

**Part u9 complete:** 0x0094AF19 to 0x0094BBFD is ours but for the adjustors, the director's ID getter and the shared list node.

**What is left for u10:** the other 54 functions of the range, 25 of which ran: 0x0094A079 to 0x0094AF18 (37: the classes made by 0x0094A4D8 and 0x0094ABC9, vtables 0x00AD4EC0 to 0x00AD4FA0, with 0x0094A5EA shared by many vtables and 0x0094AC43 already named in audio.toml) and 0x0094BBFE to 0x0094BF3B (17: vtable 0x00AD5240 to 0x00AD5274; 0x0094BC3F and 0x0094BC5D are shared by other vtables, 0x0094BBFE and 0x0094BC46 are jumped to from 0x0094C0DF and 0x0094C0E7). The loader's constructor 0x0094B400 needs no entry of its own (only its factory calls it).

## Part u10: the MP3 decoder and the UI script's root holder

`src/core/gzui_mp3.cpp`, entry detours, off by default, installed at Attach:

| Replacement | Functions |
|---|---|
| `framework.mp3.object` | the director's constructor 0x0094ABC9, deleting destructor 0x0094AC27 (slot 16) and the static one's getter 0x0094AC43 (`GZDirector::Get94AC43` in audio.toml, which the audio director calls at its start); the maker's constructor 0x0094A437, QueryInterface 0x0094A457 (0), destructor 0x0094A498, deleting destructor 0x0094A47C (4), factory 0x0094A4A6 and MakeStream 0x0094AB03 (3) (9) |
| `framework.mp3.stream` | the stream's constructor 0x0094A4D8, destructor 0x0094AAB8, deleting destructor 0x0094AA9C (21), factory 0x0094AB94, Release 0x0094A5C0 (2), OpenPath 0x0094AEAE (3), OpenKey 0x0094A977 (5), Open 0x0094AC88 (7), Close 0x0094A907 (8), the file's slots 4 and 6 passed on 0x0094A5DB (9) and 0x0094A8CA (19), Length 0x0094A5E3 (10), Read 0x0094A5ED (14), Rewind 0x0094A81A (15), Format 0x0094A892 (16), the flag's getter 0x0094A899 (17), setter 0x0094A8AC (18) and clearer 0x0094A8C2 (22), Seek 0x0094A8D2 (20) (19) |
| `framework.mp3.decode` | the decoder's Feed 0x0094A079, Decode 0x0094A0E4 and Reset 0x0094A2AB, the frame header 0x0094A2C1 and the ID3 tag's size 0x0094A3F8 (both cdecl) (5) |
| `framework.uiscript.root` | the root holder's QueryInterface 0x0094BBFE (0), Release 0x0094BC46 (2), by id 0x0094BD5E (5), by index 0x0094BD96 (6), a window's value by id 0x0094BC69 (7) and by index 0x0094BCAA (8), each window's slot 7 0x0094BDB4 (9), the count 0x0094BDE0 (10), all that give an interface 0x0094BDEA (11), the interface at +4's slots 3 0x0094BE64 and 4 0x0094BEBA; the vector's insert 0x0094BF3B and its two copies 0x0094BCEB and 0x0094BD13; the rectangle test 0x0094BD38 (15) |

48 of the 54 functions of 0x0094A079 to 0x0094AF18 and 0x0094BBFE to 0x0094BF3B (25 of them ran in the coverage runs). Addresses in `data/addresses/gzui.toml` (the "Part u10" part) with the game code and data they use; 0x0094AC43 keeps its entry in audio.toml. Ours call each other directly where the game does (the stream's Read the header, Feed and Decode, Rewind the ID3 size, Open Reset, the destructor Close, the factories and MakeStream the constructors, the director's getter its constructor, PartAt the rectangle test, the insert the copies); every call the game makes through a vtable stays one.

Still the game's, 6: the jump thunks 0x0094A432 (to cRZCOMDllDirector::Destruct) and 0x0094A972 (to the decoder's destructor 0x00A66BBE), the `xor al, al; ret` stub 0x0094A5EA that many vtables share, the director's ID getter 0x0094AC21 (`mov eax, 0x1CEC20BE`), and the root holder's AddRef 0x0094BC3F and slot 3 0x0094BC5D, both folded with other classes' vtables. The decoder itself (constructor 0x00A667B4, header 0x00A667FE, frame 0x00A66B32, first frame 0x00A66BEF, destructor 0x00A66BBE) is code far above the block, from 0x00A667B4 on.

### The classes

The director 0x1CEC20BE is "HitMP3Decoder" (the text after its vtable 0x00AD4F60), a framework COM director that the audio director adds at its start through its getter 0x0094AC43 (the static one at 0x00B634D0, guard bit 1 of 0x00B6351C, destroyed at exit by 0x00A7F63C). It registers two classes:

| Class | Factory | Size | What |
|---|---|---:|---|
| 0x3CEC2B47 | 0x0094AB94 | 0x1104 | the MP3 stream (vtable 0x00AD4ED8, 24 slots; below) |
| 0x9CEC2B58 | 0x0094A4A6 | 0xC | the maker (vtable 0x00AD4EC0: QueryInterface for 1 and 0xFCEFF549, the shared count's AddRef and Release, slot 3 MakeStream, the deleting destructor; a list at +8) |

**The stream** reads an MP3 file through the cRZFile at +0x20 and gives 16-bit PCM: +4 the count, +8 the last frame header's six dwords (layer, size, samples, channels, sample rate, bit rate), +0xA0 the decoder (0x4A60 bytes from the C library's malloc), +0xA4 and +0xA8 the left and right sample buffers (0x2000 bytes each) and at +0xAC their copies for the decoder, +0xB4 the frame as read, +0x10B8 the ID3 tag's size, +0x10BC the samples decoded and +0x10C0 given, +0x10C8 and +0x10CC the next left and right sample, +0x10D0 open, +0x10D4 the path (a game string), +0x10E8 a flag and +0x10E9 whether it was set (Open sets it to whether there is audio), +0x10EC a wave format (tag 1, channels, rate, bytes a second, block, 16 bits) and +0x1100 the length in bytes.

- OpenPath (slot 3) clears the flag, sets the path and calls Open (7); OpenKey (5) gets the path of a resource key (the resource manager's slot 32 finds the record, its interface 0x856C8CF7's slot 18 the path) and calls slot 3. The maker's MakeStream (its slot 3) makes a stream for an IGZString path, opens it and asks it for an interface; it never releases the stream itself.
- Open needs the path's extension, lower case, to match the game's text at 0x00B63528 (a static string made at start-up; `.mp3` and `.MP3` files pass); else the flag is set to 0 and it answers 0. Unless already open: a new decoder (constructed and reset), the two buffers, the file opened (slot 3 with 1, 2, 1). Rewound, a 4-byte read sets the flag (unless set), rewound again, and the length is the file's size in bits over the bit rate in 1024s (a float) times the bytes a second, through the game's `_ftol`.
- Rewind (15) skips an ID3v2 tag byte by byte. Read (14) gives whole blocks: what is left of the last frame, then frame after frame (the 4-byte header parsed, the rest read, fed to the decoder and decoded into the two buffers, a mono frame's right channel being its left), until enough or the file ends; the format then takes the last frame's channels and rate, and the answer is the bytes given. Seek (20) to 0 rewinds; else the file's own seek (its slot 12).
- Close (8) closes the file and frees the buffers and the decoder.

**The decoder's own functions** (the state is the game's mpglib-like decoder): Feed gives it one frame (it refuses one while it still holds input, and a header the decoder refuses); Decode gives up to a count of samples a channel into the channels' buffers (a null one skipped), decoding the frame when nothing is ready (the first frame through 0x00A66BEF, which sets samples to drop from the table 0x00AD4D20; a frame that fails is silence), within a limit Feed sets (-1 none); Reset forgets the input and makes the next frame the first. The header 0x0094A2C1 reads MPEG 1, 2 and 2.5, layers I to III (the tables 0x00AD4D1C and 0x00AD4D30).

**The root holder** 0x5A354595 (0x24 bytes; part u9's loader fills one per UI script) keeps windows in an STLport vector at +8 (begin, end, capacity; its allocator at +0x18) with its count at +0x20; the primary vtable 0x00AD5248 and the interface 0xFA35615F at +4 (0x00AD5234). Slot 4 (0x0094C0EC, u11's range) adds one (held), slot 5 finds one by the id its slot 5 gives, slot 6 by index, 7 and 8 give the value of a window's interface 0x9A4527CB (its slot 3), 9 calls each window's slot 7, 10 the count, 11 calls slot 8 of each window that gives an interface while all said yes; at +4, slot 3 calls every window's interface 0x5A356159's slot 3, slot 4 finds, from the last window back, one whose rectangle (that interface's slot 5) holds a point and whose slot 6 takes it. The vector's insert 0x0094BF3B is reached only through the push 0x0094C037 when the vector is full.

Quirks kept: the stream's constructor makes the block +0x10F8 from the bits word before it sets it (what the memory held); the frame header's six dwords are not cleared by the constructor, and a read takes the format from them whatever they hold (after a header that is no header, 0 channels: the next read divides by 0); Open on a missing file stays marked open with its buffers, and resets the new decoder twice (the second time through a null one when malloc fails); Close does not clear what was decoded, so a read after it uses freed buffers (Rewind clears it); MakeStream calls through a null stream when the memory is not there; All releases the window, not the interface it gave; the vector's insert with room copies backward from the end, so an insert at the end itself would never stop (only the push, which handles that case, calls it); a copy with a null destination still moves on (the next pointer would be written at 4).

The methods answer a byte. Ours give the game's EAX where it comes from a call or a value, and 0 or 1 above the byte where the game leaves its caller's EAX (the maker's and the holder's QueryInterface for other interfaces, the flag's getter, setter and clearer, Reset, Close when closed, the holder's slot 9 and the +4 interface's slots with no windows).

### Testing

`OPENSC4_TEST_MP3=<day>` is a test of its own (the fingerprint's `mp` part). It drives the functions through their slots and addresses with real streams, decoders, makers, directors and root holders where the game makes them, one of the game's radio files (`Radio\Stations\Mayor\Music\SC4ep1_01_M.mp3` in the game's folder, 6,815,448 bytes) and files made from it, and windows, interfaces and an allocator of its own for the root holder (each hashes every call it gets). Four stages, each logged as `autotest: mp3: <stage> <hash>`:

- **decode:** 16 frame headers (MPEG 1, 2 and 2.5, layers I to III, padded, mono, free format, and each way one is refused), 7 ID3 headers; a decoder (its memory cleared first) fed 40 frames of the music with limits of none, 0 and 300 samples, a second Feed while it holds input, Decode with 8 counts (0, negative, less and more than a frame) into both channels or the right one only, two resets; every sample given.
- **stream:** the music opened by slot 3 and by address, a copy with a 30-byte ID3 tag, its first 2,000 bytes as `.MP3`, an empty one, a missing one, a text file, and one with no frame (opened, rewound and closed only): the file's slots 4 and 6, Length, Open again, 7 reads of 4 to 0xC000 bytes (every byte given), Seek to 1000, 0 and past the end, Rewind and a read, Format, the flag set and cleared, Close twice, Release; OpenKey by slot and address with a key that is not there and one whose record is no path; the constructor on memory of its own filled two ways (the block from what it held), the destructor and the deleting destructor (flag 0), Release from 2.
- **object:** the static director's getter twice, a director constructed on memory of its own and its deleting destructor (flag 0); the maker by its factory, QueryInterface with four interfaces by slot and address, MakeStream by slot and address for the music (then a read), a text file and a missing file, and with an interface the stream does not give; Release to nothing; the constructor, destructor and deleting destructor on memory of its own.
- **root:** a root holder by the UI script director's factory, QueryInterface with five interfaces by address and through the +4 adjustor; every slot by slot and address while empty, with six windows of ours added (one with no interfaces), and after an insert with room; 28 rectangle tests; the copies (a null destination, nothing to copy) and the insert on a vector of ours (8 inserts at the front, the middle and the end, growing 1, 2, 4, 8); released to nothing (the game's destructor releases each window).

The test's streams made by the factory get their frame header dwords set (2 channels, 22050) before they are used, and the decoder's memory is cleared: both are what the memory held, and differ between runs. A first trial died in the root stage on both sides: a copy of three pointers to a null destination writes the second at 4 (the game's own code does); the test copies one.

With the groups on, each of the 48 logs its first call in a test run. `mp` is 2A108216 (muted), the same on both sides; the stages 9C99BDEC, B5590718, 2A9B6347 and 2A108216. The music's reads give 4608 of 4608 bytes at 44100 Hz, 2 channels.

An independent audit compared all 48 with the disassembly: ret sizes against the thunks, arguments and their order, the vtable offsets and addresses called (and their conventions), call order, the reference counts, field offsets, signed and unsigned compares and divisions, loops, EAX on every path, the constructors' writes, the x87 sequence of Open and the address entries' expected bytes. It found one difference: Close's EAX when the decoder is null (the game loads each buffer into EAX before testing it, so it answers the right buffer's free's EAX, else 0, not the left one's or the file's); fixed. Also matched now: the upper bytes of the flag Open passes to slot 18 (the read's EAX with AL replaced; slot 18 reads only the byte). Its notes on the text (a negative limit is none, the path's buffer is +0x10D8, a first-call name) are fixed. It also found that `SC4UI::F43AB70` (0x0043AB70, in ui.toml) is cdecl, not thiscall as its entry says; ours calls it as cdecl, and the entry was left for a change of its own (part u11 fixed it).

### Parity

Batch `gzu10-r2` (2026-10-23, the final build), muted on both sides, the base list (with `determinism.dispatch_info`) on both sides, every `framework.mp3.*`, `framework.uiscript.*`, `framework.soundmix.*`, `framework.soundsvc.*`, `framework.sound.*`, `framework.xmlparse.*` and `framework.propio.*` group `=ours` on side B, two jobs, own test folder `p4env-gzu10`:

| Leg | Saves | Result |
|---|---|---|
| Making Money d0000, 4 days | 1 | same (in `gzu10-r2b` and `gzu10-r2c`; see below) |
| Rush Hour t0000, 7 days | 1 | same |
| 12 saves, 7 days | 12 | 12 same |
| 12 saves, 7 days, `OPENSC4_TEST_MP3=2` | 12 | 12 same; `mp` 2A108216 in all 24 runs |
| Making Money d0000, 4 days, `OPENSC4_TEST_MP3=2` | 1 | same; `mp` 2A108216 |

In `gzu10-r2` itself the first Making Money leg was "partly": the first side-B run differed from day 2 while the three other side-B runs matched both side-A runs. It ran while two test runs of the same build were going on the machine. Two fresh batches of the same build, `gzu10-r2b` and `gzu10-r2c`, run with nothing else going, were the same on the first pair, and in that leg ours run only the root holder's six functions that every other leg also runs. The batch `gzu10-r1` on the build before the audit's fix ran all five legs: all the same (one Rush Hour save of the 12 after a second pair, its side A having two outcomes), `mp` 2A108216 in all 26 test runs.

**Reached:** in every plain (muted) leg side B logs 6 first calls, all the root holder's: QueryInterface, by id, slot 9, Release, the vector's insert and its forward copy (every UI script the loader reads fills one). With the sound muted the MP3 director is not even made. A plain unmuted test run (2 days, no test) adds the director's getter and constructor (8): the audio director adds it at start, but no music played in it. The coverage runs saw 25 of the 54 run (the stream's open, read, rewind, close, the decoder's functions and the root holder's). The test runs reach all 48.

**Part u10 complete:** with part u9, 0x0094A079 to 0x0094BF3B is ours but for the adjustors, the two jump thunks, the shared stub 0x0094A5EA, the two ID getters, the shared list node and the root holder's two folded slots.

**What is left for u11:** 0x0094C000 to 0x0094C8FF (50 functions, 35 of which ran), the framework window's small methods; it begins with the rest of the root holder: the vector's push 0x0094C037, constructor 0x0094C05E and free 0x0094C113, the holder's constructor 0x0094C0AF, its adjustors 0x0094C0DF and 0x0094C0E7, slot 4 0x0094C0EC, destructor 0x0094C144 and deleting destructor 0x0094C188.

## Part u11: the root holder's life and the LEGACY reader

0x0094C000 to 0x0094C8FF holds 50 functions: the rest of the root holder 0x5A354595 (part u10 has its slots), the LEGACY reader 0xFA450242 (the class the `LEGACY` tag of a UI script makes; part u9's director has its factory 0x0094AF90, its constructor 0x0095B8C4 is in part u16's range), and small functions of other classes. Ours: 21, in `src/core/gzui_legacy.cpp`, two groups, off by default, installed at Attach:

| Group | Detours | What |
|---|---:|---|
| `framework.uiscript.rootlife` | 7 | the root holder's constructor 0x0094C0AF, slot 4 0x0094C0EC, destructor 0x0094C144 and deleting destructor 0x0094C188; its vector's push 0x0094C037, constructor 0x0094C05E and erase 0x0094C113 |
| `framework.uiscript.legacy` | 14 | the LEGACY reader's QueryInterface 0x0094C431, AddRef 0x0094C47C, Release 0x0094C483, destructor 0x0094C288, deleting destructor 0x0094C761, slots 4 to 7 and 9 (0x0094C30E, 0x0094C3CD, 0x0094C3E1, 0x0094C314, 0x0094C31E) and its two faces' slots (0x0094C3F5, 0x0094C413, 0x0094C3A1, 0x0094C375) |

**The root holder's life.** The constructor puts interim vtables (0x00AD51F0 and 0x00AD5220, every slot the stub 0x005D4A10), makes the vector and puts the real ones. The vector (at +8: begin, end, capacity, +0xC unused, +0x10 its allocator, +0x14 unused) makes a counted allocator of 8 bytes (vtable 0x00AD1DC0, its count at +4) with SC4::operator_new, holds it and takes a block of 8 pointers from its slot 3. Slot 4 holds a window (its slot 1) and pushes it: with room it is put at the end, else the insert 0x0094BF3B grows the vector. The destructor releases every window, empties the vector (the erase moves the tail down with 0x00407A50, a memmove of pointers) and ends in SC4AttrVector::Destruct (0x009446E4: the block back to the allocator, the allocator released). Release (part u10) calls the deleting destructor at 1.

**The LEGACY reader** (0x20 bytes): the vtable 0x00AD5360, the face 0x5A356159 at +4 (0x00AD5344), the face 0x9A4527CB at +8 (0x00AD5324), the count +0xC, the window it reads for +0x10, a value from its parent +0x14, another object +0x18 and a flag byte +0x1C. Every reader holds the variable table 0x00B63588 (0xBA2E7954, part u4) its constructor makes once and fills with the widget readers' words; the destructor releases it and forgets it when that answers 0.

- QueryInterface gives the reader for 1 and 0x1A356083, its faces for 0x5A356159 and 0x9A4527CB (AddRef through the primary vtable); the faces' slots 0 to 2 are adjustors to the primary's.
- Slot 4 answers the variable table, 7 (and the +8 face's 4) sets the flag, 9 takes a parent: the old value released, the parent's interface 0x9A4527CB's slot 3 the new value (held). The +8 face's slots 5 and 6 set the window and the object (held, the old released; nothing when the same).
- Slots 5 and 6 pass on to the window's slots 63 and 64 (tail jumps), the +4 face's 4 and 5 to its 54 and 47 (answering 1); without a window 0.
- The destructor: with the flag, the window's slot 11 gives an object that gets slot 15 with the window, then the window's slot 5; the window, the value, the variable table and the object released.

**Still the game's in the range:** the adjustors 0x0094C0DC and 0x0094C0E4 (the root holder's +4 face) and 0x0094C4B8 to 0x0094C4D8 (the reader's faces), each a jump into one of ours; helpers that take their arguments in registers (property makers that give a window's slot 114 a new 0x14-byte property, and lookups in vectors of pairs: 0x0094C1A4, 0x0094C1DA, 0x0094C222, 0x0094C255, 0x0094C4E0, 0x0094C58D, 0x0094C64E, 0x0094C675, 0x0094C6C7, 0x0094C719); and other classes' small functions: AddRefs at +8 (0x0094C4B1) and through 0x0087B212 (0x0094C7CE, 0x0094C7E5), a rectangle's emptiness 0x0094C49A, a QueryInterface 0x0094C813, an interface test 0x0094C835, a destructor 0x0094C851, three tree erasers (0x0094C86E, 0x0094C89B, 0x0094C8C8), a map's lower bound 0x0094C8F5 and 0x0094C77D (an object at +0x18 handed to an interface 0x5386D516 and released; no vtable of the reader holds it). They go with the classes they belong to (parts u12 to u16).

Quirks kept: the vector's constructor calls through a null allocator when the memory is not there; the reader's QueryInterface answers AL 0 over the asked interface's upper bytes; SetParent uses its argument's own stack slot as the out of the parent's QueryInterface; every reader's destructor releases the shared variable table, so the last one destroys it and the next constructor makes it again (and has every widget reader add its words again).

The methods answer a byte. Ours give the game's EAX where it comes from a call or a value, and 0 above the byte (or 0) where the game leaves its caller's EAX (the root holder's slot 4 for no window, the flag setter, SetParent and the face setters with nothing to do, slot 6 and the +4 face's slots without a window).

Also fixed: `SC4UI::F43AB70` (0x0043AB70, ui.toml) is cdecl (two game strings' bodies compared byte for byte, EAX 1 when the same, else 0), not thiscall; every caller already called it as cdecl.

### Testing

`OPENSC4_TEST_LEGACY=<day>` is a test of its own (the fingerprint's `lg` part). It drives the 21 through their slots and addresses with root holders and LEGACY readers made by the game's factories and constructors (and on memory of its own), and windows, parents, values and objects of its own (each hashes every call it gets). Two stages, each logged as `autotest: legacy: <stage> <hash>`:

- **root:** a root holder by the UI script director's factory (the constructor's fields, the vector's 8 places, the allocator's count), 12 windows added by slot 4 and by address (the vector grows to 16) and none twice, 6 pushed by address (it grows to 32), erased in the middle, empty and at the end; released to nothing (the deleting destructor releases every window); the constructor on memory of its own with the destructor and with the deleting destructor (flag 0); a vector of its own: the constructor, 11 pushes, two erases, SC4AttrVector::Destruct; every window's count at the end.
- **legacy:** a reader kept for the whole stage (so the shared table stays), then four readers by the factory, by slot and by address in turn: QueryInterface with six interfaces (by the primary and the +8 face), slot 4, the window's calls without a window, five parents (none, one without the face, a face without a value, two with values), the window and the object set five times each (the same again, another, none), the window's four calls, the flag set by the primary slot, the face's and the address; released to nothing with the flag on and the window's slot 11 giving an object, giving none, and with the flag off; the constructor on memory of its own with the destructor and the deleting destructor (flag 0); every object's count at the end.

With the groups on, each of the 21 logs its first call in a test run. `lg` is 831BDB32 (muted), the same on both sides; the stages 93339387 and 831BDB32.

An independent audit compared the 21 with the disassembly (memory written and its order, calls, their arguments and order, null and zero counts, EAX on every path, bytes popped, the address entries): no difference in what they do. It found three comments to fix (the flag setter's EAX, the held setters' "ours 0", the order the destructor releases in); fixed.

### Parity

Batches `gzu11-r1` (before the comment fixes) and `gzu11-r2` (the final build), 2026-10-23, muted on both sides, the base list (with `determinism.dispatch_info`) on both sides, every `framework.uiscript.*`, `framework.mp3.*`, `framework.soundmix.*`, `framework.soundsvc.*`, `framework.sound.*`, `framework.xmlparse.*` and `framework.propio.*` group `=ours` on side B, two jobs, own test folder `p4env-gzu11`:

| Leg | Saves | Result |
|---|---|---|
| Making Money d0000, 4 days | 1 | same |
| Rush Hour t0000, 7 days | 1 | same |
| 12 saves, 7 days | 12 | 12 same |
| 12 saves, 7 days, `OPENSC4_TEST_LEGACY=2` | 12 | 12 same; `lg` 831BDB32 in every run |
| Making Money d0000, 4 days, `OPENSC4_TEST_LEGACY=2` | 1 | same; `lg` 831BDB32 |

Both batches: all five legs the same on the first pair.

**Reached:** in every plain (muted) leg side B logs 18 first calls: all 7 of the root holder's (every UI script read makes, fills and destroys one) and 11 of the reader's (every `LEGACY` tag makes one, sets its parent, window, object and flag, and releases it); slot 6 and the +4 face's two slots are reached only by the test. The coverage runs saw 35 of the range's 50 run.

**Part u11 complete:** the root holder 0x5A354595 is ours but for its adjustors and two folded slots, the LEGACY reader's own slots are ours but for its adjustors and its constructor (part u16's range).

**What is left for u12:** 0x0094C900 to 0x00951268 (49 functions, 16 of which ran), the window's script properties; the small functions part u11 left in its range go with their classes.

## Part u12: the readers of a window's script properties (first slice)

0x0094C900 to 0x00951268 holds 49 functions: the window's script properties. A window's UI script gives it properties as an STLport vector of 8-byte pairs (a key, a property object); a property whose slot 3 answers 0x800E is text, and its slot 55 puts that text into a string. The large handlers 0x0094CE28 to 0x00951084 (each in a vtable of its own, 0x009512F3 to 0x00953B27) read a window's properties through four small readers. This slice makes those four ours, in `src/core/gzui_props.cpp`, one group, off by default, installed at Attach with the UI script loader's:

| Group | Detours | What |
|---|---:|---|
| `framework.uiscript.propread` | 4 | the text finder 0x0094CCD9 and the scanners of three bytes 0x0094D4CD, a pair 0x0094D55A and a rectangle 0x0094D5CA |

**The readers** take arguments in registers as well as on the stack (cdecl): the vector of pairs in EDI, the finder's string in EBX, the rectangle's four ints in ESI. Each is entered through a naked thunk that hands the registers' values and the stack arguments to a cdecl body, which keeps EBX, ESI, EDI and EBP as the game's do.

- The finder empties the string (when not already empty), then takes the first pair with the key whose property is set and answers 0x800E (slot 3), and answers that property's slot 55 with the string (its EAX); AL 0 when none. The count is taken once, the vector's start again each time.
- The scanners make a string of their own (SC4String's constructor 0x00578390), find the key's text and scan it with sscanf: "(%d,%d,%d)" (0x00AD5404) into three ints whose low bytes go to the three pointers only when all three were read; "(%d,%d)" (0x00AD542C) straight into the two pointers; "(%d,%d,%d,%d)" (0x00AD5434) straight into the rectangle. AL 1 when all were read. The string is destroyed inline (the vtable 0x00A80810 back, the characters freed with 0x0090CF63).

Ours answer 0 above the byte where the game leaves what it had in EAX (the finder with no match, the scanners' false paths); every caller tests AL.

### Testing

`OPENSC4_TEST_PROPREAD=<day>` is a test of its own (the fingerprint's `pr` part). It calls the four by address with their registers set, on a vector of 12 pairs of its own (keys 0 to 11, a pair without a property, a property that is not text ahead of one that is, the same key twice) with text properties of its own (each hashes every call it gets) and strings made by the game's constructor. The texts: three, two, four and five numbers, a cut-off pair, words, an empty text, numbers above a byte and below zero, spaces. Two stages, each logged as `autotest: propread: <stage> <hash>`:

- **find:** the finder for every key (and on an empty vector), the string's text after each;
- **scan:** every scanner for every key, the values after each (pre-set, so what sscanf leaves on a failed read counts too).

With the group on, each of the 4 logs its first call in a test run. `pr` is 71C32265 (muted), the same on both sides; the stages 9CAA1DFD and 71C32265.

The four were checked against the disassembly (memory written and its order, calls and their arguments, registers kept, EAX on every path, the address entries) by the author and by an independent audit (about 170 call sites checked too: each tests AL, ignores the answer or sets EAX, ECX and EDX before reading them; some keep ESI or EBX across the call, which ours keep): no difference in what they do. It found one comment to fix (the finder's EAX on a miss holds leftovers of its own, not the caller's).

### Parity

Batch `gzu12-r1`, 2026-10-23, muted on both sides, the base list (with `determinism.dispatch_info`) on both sides, every `framework.uiscript.*` (with `propread`), `framework.mp3.*`, `framework.soundmix.*`, `framework.soundsvc.*`, `framework.sound.*`, `framework.xmlparse.*` and `framework.propio.*` group `=ours` on side B, two jobs, own test folder `p4env-gzu12`:

| Leg | Saves | Result |
|---|---|---|
| Making Money d0000, 4 days | 1 | same |
| Rush Hour t0000, 7 days | 1 | same |
| 12 saves, 7 days | 12 | 12 same |
| 12 saves, 7 days, `OPENSC4_TEST_PROPREAD=2` | 12 | 12 same; `pr` 71C32265 in every run |
| Making Money d0000, 4 days, `OPENSC4_TEST_PROPREAD=2` | 1 | same; `pr` 71C32265 |

All five legs the same on the first pair.

**Reached:** in every plain (muted) leg side B logs all 4 first calls (the property handlers read every window's properties through them when a city's UI loads).

**What is left of u12:** 45 functions, 12 of which ran: the property handlers 0x0094CE28, 0x0094D17E, 0x0094E33B to 0x00951084 and the large 0x0094D641 (all of which call these readers and the register-argument property makers part u11 left), STLport map and tree code 0x0094C92D to 0x0094CDB5, the shared AddRef 0x0094D34F (count +0x10, 14 vtables) and small wrappers 0x0094D147 to 0x0094D490.

## Part u12, slice 2: the makers, loaders and lookups of a window's script properties

The second slice makes ours the ten helpers part u11 left in its range (0x0094C1A4 to 0x0094C719), which the property handlers of this part call with arguments in registers as well as on the stack (cdecl). They are in `src/core/gzui_props.cpp` too, three groups, off by default, installed at Attach:

| Group | Detours | What |
|---|---:|---|
| `framework.uiscript.propmake` | 4 | the makers that give a window a new 0x14-byte script value through its slot 114: 12 bytes 0x0094C1A4, 8 bytes 0x0094C1DA, a number 0x0094C222 and a text 0x0094C255 |
| `framework.uiscript.propload` | 2 | the loaders by key: an image 0x0094C4E0 and a text 0x0094C58D |
| `framework.uiscript.proplook` | 4 | the lookups in vectors of pairs: a key 0x0094C64E, a number's word through slot 73 0x0094C675 or slot 75 0x0094C6C7, and flags from a table of masks 0x0094C719 |

Each is entered through a naked thunk that hands the registers' values and the stack arguments to a cdecl body, which keeps EBX, ESI, EDI and EBP as the game's do. Where the game writes into its own argument slots (the loaders' out, the flags reader's flags), the body is given those slots, so the caller's stack is written the same.

**The script values** (SC4ScriptNumber, 0x14 bytes, vtable 0x00ACECD0): +4 the value or a pointer to its bytes, +0xC a word, +0xE the kind (slot 3 answers it: 6 a number, 0x800E a text, 0x8000 bytes), +0x10 the size. The makers take 0x14 bytes from SC4::operator_new (none when there is no memory) and build the value with 0x00915E53 (a copy of the bytes, kind 0x8000; new in the address database as `SC4ScriptNumber::FromBytes`), 0x00915D6D (a number) or 0x00915CDE (a game string's text), then put it under a key with the window's slot 114 (the window in ESI):

- 0x0094C1A4 puts 12 bytes (a resource key's three words, the handlers' images and texts) under the key itself, slot 114's EAX answered; without a window it does nothing and leaves the caller's EAX.
- 0x0094C1DA puts two words (the first in EAX) under the key + 0xDAA3B860; 0x0094C222 a number and 0x0094C255 a text under the key + 0xFAA3BE85. AL 1 over slot 114's EAX; the window is not checked.

**The loaders** make the key (0x856DDBAC an image, 0x2026960B a text; the group in EAX, the instance on the stack) and ask the UI script loader's held object for it (the loader 0x5A356E15 by a reference holder, 0x00449750, new in the database as `SC4RefPtr::UIScriptLoader`; the object its slot 10 set, read by its slot 9): the image by the object's slot 3 (its EAX answered), the text by its slot 4 with the string in EDI (its AL answered). Without a held object they ask the resource manager (slot 3 or 4 with the interface 0x06F1568E or 0x089B7DC8, the out being the instance's own argument slot, zeroed first): the image's slot 3 is answered, else 0; the text goes into the string by its slot 13, AL 1. The resource is released and the holders destroyed (0x004495C0) in the game's order; the text loader's AL sits over the last destruction's EAX. In a game run the loader has a held object, so the resource manager's path is reached only by the test.

**The lookups.** The vectors are the STLport vectors of 8-byte pairs (key, value) of slice 1.

- 0x0094C64E (the vector in EAX): whether a pair has the key; AL over the pair count.
- 0x0094C675 and 0x0094C6C7 (the vector in EDI, a key and an out): the first pair with the key whose value is set and a number (slot 3 answers 6); the word its slot 73 (or 75) points at goes to the out, AL 1 over that word. On a miss AL 0 over what EAX last held: the caller's EAX for an empty vector, else the last pair's place, its null value or its slot 3's answer. The count is taken once, the vector's start each time.
- 0x0094C719 (a table of (key, mask) pairs ending with key 0 in EAX, the vector and the flags on the stack): each key found as a number (0x0094C675, the out zeroed first) clears its mask from the flags and sets it again when the word is 1; the flags are kept in their own argument slot and answered. Ours calls our 0x0094C675.

Ours give the game's EAX on every path, its upper bytes and leftovers too. The number finders also leave EDX as the game's do (after slot 3, with the value's vtable in EDX, and after slot 73 or 75): 54 of 0x0094C675's call sites push `SETcc DL` as a bool argument to a window's slot, over EDX's upper bytes. Ours call the value's two slots through two small naked helpers that hand EDX back, and the thunk pops it into EDX.

### Testing

`OPENSC4_TEST_PROPMAKE=<day>` is a test of its own (the fingerprint's `pm` part). It calls the ten by address with their registers set (EBX, ESI, EDI and EBP checked kept after each call) and their argument slots read back after. Three stages, each logged as `autotest: propmake: <stage> <hash>`:

- **make:** the makers on a window of our own (its slot 114 hashes the key and the value's fields and bytes, then destroys the value by its slot 127), 0x0094C1A4 without a window and with three keys, 0x0094C1DA with three pairs of words, 0x0094C222 with four numbers, 0x0094C255 with four game strings (one empty);
- **load:** the UI script loader's held object (the game's, which is set) taken and kept, ours set by slot 10 (slot 3 answers a word of the key's, slot 4 writes a text for odd instances), both loaders for four instances, then no held object: the resource manager with a picture the game ships (the region screen's first edge picture) and a missing key, the text of the audio layer's first freshness score (0x0A5BCDA5, 0x7EC82BF0) and a missing key; the game's held object put back;
- **look:** on a vector of ten pairs of our own (number values answering 6, a value that is not a number, null values, keys twice) and an empty one: 0x0094C64E for nine keys, 0x0094C675 and 0x0094C6C7 for the nine with the out pre-set and EAX a mark, 0x0094C719 with a table of eight masks and an empty table for four flag values.

The number values' slots 73 and 75 leave a mark in EDX, which the hash takes after every find. Addresses never go into the hash: where EAX holds a place on a miss, the hash takes whether it is the last pair's; the loaders' resources only whether there is one.

With the groups on, each of the 10 logs its first call in a test run. `pm` is B940D88F (muted), the same on both sides; the stages make 19EB3E7D, load 6F79C575 and look B940D88F.

The ten were checked against the disassembly (memory written and its order, calls and their arguments, vtable slots, registers kept, EAX on every path, the thunks' stack offsets, the address entries) by the author and by an independent audit, which also scanned the 260 direct call sites (none reads ECX or the flags after the call; none reads EDX after nine of them; no jump or call lands inside the first five bytes of any of the ten): no difference in what they do. It found one thing to fix: the EDX the number finders leave, which their callers push as a bool's upper bytes (above); fixed.

### Parity

Batches `gzu12b-r1` (before the EDX fix) and `gzu12b-r2` (the final build), 2026-10-23, muted on both sides, the base list (with `determinism.dispatch_info`) on both sides, every `framework.uiscript.*` (with `propread`, `propmake`, `propload` and `proplook`), `framework.mp3.*`, `framework.soundmix.*`, `framework.soundsvc.*`, `framework.sound.*`, `framework.xmlparse.*` and `framework.propio.*` group `=ours` on side B, two jobs, own test folder `p4env-gzu12b`:

| Leg | Saves | Result |
|---|---|---|
| Making Money d0000, 4 days | 1 | same |
| Rush Hour t0000, 7 days | 1 | same |
| 12 saves, 7 days | 12 | 12 same |
| 12 saves, 7 days, `OPENSC4_TEST_PROPREAD=2 OPENSC4_TEST_PROPMAKE=2` | 12 | 12 same; `pm` B940D88F and `pr` 71C32265 in every run |
| Making Money d0000, 4 days, the same two tests | 1 | same; `pm` B940D88F |

Both batches: all five legs the same (r1's `pm` was BA41AF31, before the test hashed EDX).

**Reached:** in every plain (muted) leg side B logs 13 first calls: slice 1's 4 and 9 of these 10 (the property handlers make, load and look up through them when a city's UI loads); the text maker 0x0094C255 is reached only by the test (it did not run in the coverage runs either). In a game run the loader has a held object, so the loaders' resource manager path is the test's alone.

**What is left:** of part u11's range, 19 of the 29 it left (its adjustors and the other classes' small functions, which go with their classes); of u12's range, the 45 of slice 1 (the property handlers 0x0094CE28, 0x0094D17E, 0x0094E33B to 0x00951084 and the large 0x0094D641, STLport map and tree code 0x0094C92D to 0x0094CDB5, the shared AddRef 0x0094D34F and small wrappers 0x0094D147 to 0x0094D490). With the readers, makers, loaders and lookups ours, the handlers are the natural next slice: every helper they call is now ours.

## Part u12, slice 3: the property handlers and the variable table's words

The third slice makes ours the property handlers that run in a game, and the function that gives the UI script variable table its words. They are in `src/core/gzui_props.cpp` too, two groups, off by default, installed at Attach:

| Group | Detours | What |
|---|---:|---|
| `framework.uiscript.propwords` | 1 | 0x0094D641: the 64 words of a window's script properties given to the variable table |
| `framework.uiscript.prophandle` | 8 | the handlers of eight controls' interfaces: 0x0094E33B (0xC2AFA76F), 0x0094E516 (0x212CDC1F, the text control), 0x0094E7C8 (0x231A1C57, the text edit), 0x0094EE39 (0x4132242B), 0x0094F757 (0x8810, the button), 0x0095002C (0xC12CEA13), 0x00950218 (0x21325207) and 0x00950AF6 (0x612CE0C3) |

**The words.** 0x0094D641 (no arguments) is called once by the first LEGACY reader's constructor (0x0095B8C4) when it makes the variable table 0x00B63588 (part u11). It gives the table's slot 3 each word as a game string (SC4String::FromText, destroyed inline after) with its value: `clsid` -1, `iid` -2, `yes`, `true` and `on` 1, `no`, `false` and `off` 0, `GZWin` 0xE2BA00EE, `id` 0x100 to `transparent` 0x107, `font` 0xF000 to `outline` 0xF019 (`bkgcolor`, `forecolor`, `notify`, `gutters`, `style`, `left`, `right`, `center`, `flat`, `normal`, `sunken`, `raised`, `fill`, `nofill`, the six `colorfont...` colours, `align`, `image`, `imagetype`, `imagerect`), `comments` 0x108 to `commentskd` 0x10F, and the `winflag_...` words 0xF01A to 0xF026 (`winflag_visible` to `winflag_acceptfocus`). These are the keys the handlers look for.

**The handlers** (cdecl: the window, its script's vector of pairs, and the pass, a byte: 0 first, 1 second) are kept as function pointers in a map (0x00B6358C) by the code that registers the controls (0x009512EB to 0x00953B2A). Each asks the window for its control's interface (the window's slot 0 with an out, released at the end) and, with one, reads the properties through the readers, makers, loaders and lookups of slices 1 and 2 (ours call our bodies directly) into the control's slots, the window's slots 53, 68 and 114, and back as script values:

- 0x0094E33B (0xC2AFA76F), second pass only: a flags byte from every 0xF005 number pair (0xF006 1, 0x204 2, 0x205 8, 0x206 0xF, 0x207 0xD, 0x208 0xE, 0xF007 4, 0xF00B 0x20, 0xF00C 0x10, 0xF00E 0x40) to slot 12, the colours 0x200 to 0x203 as three bytes each to slot 10 with their index.
- 0x0094E516, the text control: first pass, the alignment 0xF015 (0xF008 as 4) to slot 21; second, `notify` and the flags 0x300 and 0x301 to slot 7 (0x301 off also the window's slot 68 with 0x20000), the font number to slot 19 and back (0x0094C222; else the font's text back, 0x0094C255), the fore and background colours to slot 9, the gutters to slot 23 (0, 0 without), 0x305 to slot 25.
- 0x0094E7C8, the text edit: second pass, its flags (slot 80 through 0x0094C719 with the table 0x00AD56C8: 0x400 1, 0x300 2, 0x401 4, 0x402 8, 0xF019 0x10, 0x301 0x20, 0x403 to 0x406 0x40 to 0x200, 0x415 0x400, 0x416 0x800, 0x417 0x8000, 0x418 0x1000, 0x419 0x2000) to slot 77, the font, the six font colours to slot 12, 0x407 and 0x408, 0x40B's colour with the outline to slot 37, 0x409, the gutters, 0x40C to 0x410, and two images by key ("{%x,%x}", 0x0094C4E0) with their rectangles to slots 88 and 87, written back under 0x8B4BFEFC and 0x8B4BFEFD.
- 0x0094EE39 (0x4132242B): first pass, every 0x502 text to slot 7 (the text, -2, 0), 0x503, 0x501, 0x505, 0x506; second, 0x402 and 0x401 to slot 35, the font through 0x00913C72's slot 5 to slot 38, three colours, 0x408, the outline, 0x40B's "(%d,%d,%d)(%d,%d,%d)" as two colours to slot 55, 0xF00B, 0x504 and two pairs.
- 0x0094F757, the button: first pass, 0xA02 to slot 5; second, ten flags to slot 5, the style 0xF005 as a kind (0xA00, 0xA0C, 0xA0D 3, 0xA09 1, 0xA0A, 0xA0B 2, 0xA0E, 0xA0F, 0xA1B 4), the font (else the default, the `main9` font through 0x00913C72's slots 38 and 5), six colours to slot 23, the gutters by the commas of their text ("(%d,%d)" to slot 30, "(%d,%d,%d,%d)" to slot 29), the image (its area, slot 12, copied) with the kind to slot 12, the alignment, the 0xA10 text and 0xA12's text by key (0x0094C58D) to slot 35, 0xA11, 0xA18 (else 0x1000000) and two keys as 8-byte values under 0x0A5BE39D and 0x4A5BE2F8 to slot 45.
- 0x0095002C (0xC12CEA13): first pass, the 0xF018 rectangle to slot 8 and the flag 0x10 to slot 11; second, 0xB00 (at most 0xFF) times 1/255 as a float to slot 6, three flags to slot 11, the image to slot 4 and its width and height (slots 9 and 10) to the window's slot 53.
- 0x00950218 (0x21325207): first pass, 0x40A through slot 75 to slot 5; second, the 0xC00 range (the larger to slot 8, the smaller to slot 6), 0xC01 (0xC03 as 2, else 1) to slot 10, the image with the 0xF018 rectangle to slot 12.
- 0x00950AF6 (0x612CE0C3): first pass, 0x1004, 0x1005 and 0x40A; second, 0x1000, five flags to slot 6, the font (else the default), three colours, the image to slot 14, the alignment, the gutters, and five colours to slot 31.

The first pass of the text control, the text edit and 0x4132242B ends with 0x304: whether the key is there, as a boolean value (0x00915E8E) under 0xDACF8F9C through the window's slot 114. Images and texts by key are written back as the resource key (0x856DDBAC or 0x2026960B and the two words) by 0x0094C1A4 and as the two words by 0x0094C1DA.

**What ours keep.** The handlers write into their own argument slots (the window's is the out of several finds, of a scan and of 0x0094E33B's flags byte, kept in its top byte), so ours are given those slots by a thunk. Where the game pushes `SETcc DL` after a number finder, the bool sits over the EDX the finder left (its value's slots); ours push the same, and the text edit's outline over the colour (`SETNE AL`). Where the game pushes a dword of which it wrote only the low byte, or a local it never wrote (the text edit's outline when 0xF019 is not there, some colour bytes), the bytes above are its stack's leftovers; ours are 0. 0x0094EE39's pair of colours: the second triple's colour is made first and left on the stack under the first's.

### Testing

`OPENSC4_TEST_PROPHANDLE=<day>` is a test of its own (the fingerprint's `ph` part). Two stages, each logged as `autotest: prophandle: <stage> <hash>`:

- **words:** 0x0094D641 by address twice with a variable table of our own in the global's place (its slot 3 hashes each word's text and value; the game's put back);
- **handle:** every handler by address (EBX, ESI, EDI and EBP checked kept; the argument slots read back) with a window of our own (slot 0 gives a control of our own for the handler's interface, or none for the first call; slots 53, 68 and 114 hash what they get) on four vectors of pairs of our own (every read succeeding; the other branches; malformed texts and values that are not numbers; none), both passes, the UI script loader's held object ours (images of our own for odd instances, texts for odd instances; the game's put back). The controls hash every call: the slot and its arguments (words; a string's text; a rectangle's ints; which image; whether a pointer is there, for fonts; the low byte where the game leaves its stack's leftovers above). The number values' slots 73 and 75 leave a mark in EDX. Addresses never go into the hash (the window is 256-aligned, so the window argument's slot after 0x0094E33B is hashed by its top and low bytes).

With the groups on, each of the 9 logs its first call in a test run. `ph` is BAAA5D6E (muted), the same on both sides; the stages words 3667D729 and handle BAAA5D6E. With the EDX bools broken on purpose, side B's `ph` differs from the text control on.

The nine were checked against the disassembly (every path: calls, their objects, arguments and order, pushes left on the stack across another call, memory written and in which slots, byte against dword writes and tests, signed against unsigned compares, the switches, which scans are checked, thiscall argument counts, strings made and destroyed in the game's order, registers kept, the address entries) by the author and by an independent audit. The audit also found the call through the map (0x00957BFB: `call edi`, then nothing reads EAX, ECX or EDX) and checked that no jump or call lands inside the first five bytes of any of the nine. It found no difference in what they do; one difference in order, which nothing can see (0x0094EE39 asks for the colour singleton twice before its two colours, ours asked before each), and three comments to fix. All four were fixed.

### Parity

Batches `gzu12c-r1` (before the audit's fixes) and `gzu12c-r2` (the final build), 2026-10-23, muted on both sides, the base list (with `determinism.dispatch_info`) on both sides, every `framework.uiscript.*` (with `propwords` and `prophandle`), `framework.mp3.*`, `framework.soundmix.*`, `framework.soundsvc.*`, `framework.sound.*`, `framework.xmlparse.*` and `framework.propio.*` group `=ours` on side B, two jobs, own test folder `p4env-gzu12c`:

| Leg | Saves | Result |
|---|---|---|
| Making Money d0000, 4 days | 1 | same |
| Rush Hour t0000, 7 days | 1 | same |
| 12 saves, 7 days | 12 | r1 12 same; r2 11 same, Making Money d0090 partly (see below) |
| 12 saves, 7 days, `OPENSC4_TEST_PROPREAD=2 OPENSC4_TEST_PROPMAKE=2 OPENSC4_TEST_PROPHANDLE=2` | 12 | 12 same; `ph` BAAA5D6E in every run |
| Making Money d0000, 4 days, the same three tests | 1 | same; `ph` BAAA5D6E |

r2's Making Money d0090 was one odd first run: side B's first run differed from day 2 on, its second and third runs and all three of side A's agreed (A 1 outcome, B 2, shared 1). The same save was the same in r1, in r2's test leg and again alone on the r2 build (8 of 8 days on the first pair), as with the first-run flakes on d0090 of earlier batches.

Batch `gzu12c-r3`, after merging main (with part u13), the same five legs with u13's `widgets` and `widgetgen` on side B too: all the same on the first pair (1, 1, 12, 12 with `ph` BAAA5D6E, 1).

**Reached:** in every plain (muted) leg side B logs 22 first calls: all 9 of this slice (the words when the first LEGACY reader is made, every handler when a city's UI loads) and 13 of slices 1 and 2; the text maker 0x0094C255 is still reached only by the test.

**What was left of u12 after slice 3:** 36 of the 49 functions. The six handlers that did not run in the coverage runs or the plain legs: 0x0094CE28, 0x0094D17E, 0x0094F36D, 0x009503AB, 0x009505B6 and 0x00951084 (slice 4, below). Then, staying the game's: the STLport map and tree code 0x0094C92D to 0x0094CDB5 (22 functions, but for the text finder 0x0094CCD9), the AddRef 0x0094D34F that 14 vtables share, and seven small functions that belong to other classes, not to the handlers: 0x0094D147 and 0x0094D163 (three strings made and destroyed), 0x0094D356 and 0x0094D3D4 (maps copied), 0x0094D452 and 0x0094D471 (deleting destructors), 0x0094D490 (a constructor). Their callers are in parts u13 to u16's ranges, and they go with those classes.

## Part u12, slice 4: the property handlers that never run

The fourth slice makes ours the six property handlers left in the range. Each belongs to a stock widget that part u13's registrations put in the handler map, but no UI script the game ships makes one of these widgets, so the handlers never run in the game (none ran in the coverage runs or in any plain leg). They are in `src/core/gzui_props.cpp`, one group, off by default, installed at Attach:

| Group | Detours | What |
|---|---:|---|
| `framework.uiscript.prophandle2` | 6 | 0x0094CE28 (0x032F0B3C, GZWinTextTicker), 0x0094D17E (0xA13351B8, GZWinFolders), 0x0094F36D (0x412CE496, GZWinCombo), 0x009503AB (0x61325A2D, GZWinScrollbar), 0x009505B6 (0x21335C59, GZWinLineInput) and 0x00951084 (0xE303E5AE, GZWinOutline) |

They have the shape of slice 3's: cdecl (the window, its script's vector of pairs, the pass byte), the control's interface asked of the window's slot 0 and released at the end, the properties read through slices 1 and 2's helpers (ours call our bodies directly):

- 0x0094CE28, the text ticker, second pass only: 0x1200 and 0x1201 to slots 8 and 9; the font to slot 7 (a number through 0x00913C72's slot 5, put back by 0x0094C222; else a text put back by 0x0094C255; else the default `main9`); then the pairs in order: each 0xF002 and 0xF001 text an unchecked "(%d,%d,%d)" colour (0xFFFFFFFF and 0 until one is read), each 0x1202 text a new game string (SC4::operator_new and the string constructor, unchecked; its slot 1 before and slot 2 after) given to slot 3 with the two colours read so far, 0 and 0. So a ticker item takes the colours of the pairs before it.
- 0x0094D17E, the folders: first pass, slot 7, then every 0x502 text to slot 4 (the text, 0); second, the font to slot 10 (a number, else a text back), the 0xF016 image (unchecked "{%x,%x}") to slot 11 and its two words as 8 bytes under 0xDAA3B860.
- 0x0094F36D, the combo: first pass, the colours 0xF00F to 0xF011 to slot 12 (with 0 to 2), 0x408 to slot 16, 0x40B to slot 22 and 0x802 to slot 35, 0x400 at 1 to slot 21, every 0x502 text to slot 27 (the text, -2), 0x501 to slot 28 (the word, 1); second, the font to slot 11, the 0x800 image with the 0x801 rectangle (else none) to slot 33 and its words back by 0x0094C1DA, 0x803 to slot 37, the 0xF004 pair to slot 39.
- 0x009503AB, the scrollbar: first pass, 0x40A through slot 75 to slot 5; second, the 0xC00 range (the smaller first) to slot 7, 0xC01 (0xC03 as 2, else 1) to slot 9, 0xD00 and 0xD01 to slots 11 and 13, the 0xD02 pair to slot 15, the 0xF016 image to slot 17 with the 0xF018 rectangle (else the image's own area, its slot 12) and 0, written back under 0x6B4BCBD4.
- 0x009505B6, the line input: first pass, 0xE09 to slot 25, 0x400 to slot 28, 0xE0A to slot 29, the 0xE0B pair to slot 31 as the first and the second less the first; second, the font to slot 4 (or the default), eight colours (0xF00F to 0xF011 to slot 5, 0xE03 to 0xE06 to slot 14, by index; 0xE07 to slot 17), 0xE08 (at most 0xFF) times 1/255 as a float to slot 19, 0xE00 to slot 9, the style 0xF005 (0xE01 2, 0xE02 3, 0xF00A 1) to slot 11, and the gutters 0xF004 by the commas of their text to slot 33 ("(%d,%d)" given as both pairs, or "(%d,%d,%d,%d)"; unchecked).
- 0x00951084, the outline, second pass only: flags from every 0xF005 number pair (0xF009 1, 0xF00B 2, 0xF00C 4, 0xF00D 0x20) to slot 3 when any; the 0x1100 and 0x1101 bytes to slot 5 (with 1 and 2); every 0x1102 text read as an unchecked "(%d,%d)" into a table of at most 32 pairs on its stack, given to slot 13 (the count, the table, 1, 1) when it has any. Pairs beyond the 32nd are left unread.

**What ours keep.** As in slice 3: the argument slots (the window's is the out of several finds and scans, the outline's flags and both loops' index, so the ticker's and the outline's end as an index), the bools pushed over the EDX the number finder left, the order of the calls. Where the game scans a text without checking the count, the locals it does not fill keep what they held: an earlier pair's values (the same in ours) or, for the first, the stack's leftovers (ours 0). The folders' image, the ticker's first colours, the outline's first pair and the line input's gutters are read that way.

### Testing

`OPENSC4_TEST_PROPHANDLE2=<day>` is a test of its own (the fingerprint's `p2` part). One stage, logged as `autotest: prophandle2: handle <hash>`, with a line per handler:

- **handle:** every handler by address with slice 3's windows, controls (every slot hashes its number and what it gets; the outline's table hashed by its count), number and text values and images, the UI script loader's held object ours (the game's put back), on seven vectors (every read good, the ticker's texts before, between and after its colours; the other branches; malformed texts and values that are not numbers, the default fonts; an empty gutters text and one without commas; more outline pairs than its table holds; none), each with the pass byte 0, 1, 0xFFFFFF01 and 0x100, and once with no control. None of the vectors has a malformed text where a handler scans unchecked into locals the game leaves unset (the folders skip the malformed one).

With the group on, each of the 6 logs its first call in a test run. `p2` is 603A9C90 (muted), the same on both sides; `ph` stays BAAA5D6E. A build with four slips on purpose (the combo's bool without the finder's EDX, the line input's two-number gutters in the wrong order, the outline's table one short, the ticker's first colour default) changed side B's hash from the ticker's line on.

The six were checked against the disassembly by the author and by an independent audit (every path: calls, their objects, slots, arguments and order, the singleton calls against the control's, which scans and values are checked, signed compares, the switches, byte against dword writes, the argument slots and their final values, loop counts and the outline's 32-pair cap). The audit found no difference in behaviour; only locals ours start at 0 where the game leaves them unset, which nothing reads unless a scan fails.

### Parity

Batch `gzu12d-r1`, 2026-10-25, muted on both sides, the base list on both sides, every `framework.uiscript.*` (with `prophandle2`), `framework.mp3.*`, `framework.soundmix.*`, `framework.soundsvc.*`, `framework.sound.*`, `framework.xmlparse.*` and `framework.propio.*` group `=ours` on side B, two jobs, own test folder `p4env-gzu12d`:

| Leg | Saves | Result |
|---|---|---|
| Making Money d0000, 4 days | 1 | same |
| Rush Hour t0000, 7 days | 1 | same |
| 12 saves, 7 days | 12 | 12 same |
| 12 saves, 7 days, `OPENSC4_TEST_PROPHANDLE=2 OPENSC4_TEST_PROPHANDLE2=2` | 12 | 12 same; `p2` 603A9C90 in every run |
| Making Money d0000, 4 days, the same two tests | 1 | same; `p2` 603A9C90 |

All on the first pair.

**Reached:** in plain (muted) legs none of the 6 runs (no shipped script makes these widgets); only the test reaches them.

**What is left of u12:** 30 of the 49 functions, all staying the game's: the STLport map and tree code 0x0094C92D to 0x0094CDB5 (22 functions, but for the text finder 0x0094CCD9), the AddRef 0x0094D34F that 14 vtables share, and the seven small functions of other classes (0x0094D147, 0x0094D163, 0x0094D356, 0x0094D3D4, 0x0094D452, 0x0094D471, 0x0094D490), whose callers are in parts u13 to u16's ranges.

## Part u13: the stock widgets' registrations and GZWinGen's property handler

0x00951269 to 0x00953FFF holds 17 functions, all of which ran in the coverage runs: 16 registrations, one for each stock widget class, and GZWinGen's property handler. The LEGACY reader's constructor 0x0095B8C4 calls the 16 (in the order FlatRect, Text, TextEdit, ListBox, Combo, Btn, BMP, Slider, Scrollbar, LineInput, Spinner, Outline, TextTicker, Folders, Custom, Gen, with other classes' among them) whenever it makes the shared variable table (part u11). Ours: all 17, in `src/core/gzui_widgets.cpp`, two groups, off by default, installed at Attach:

| Group | Detours | What |
|---|---:|---|
| `framework.uiscript.widgets` | 16 | the registrations of GZWinTextTicker 0x00951269, Folders 0x009518A8, Custom 0x0095195E, FlatRect 0x00951A14, Text 0x00951C9F, TextEdit 0x00952046, ListBox 0x00952645, Combo 0x00952868, Btn 0x009529EF, BMP 0x00953056, Slider 0x009531A9, Scrollbar 0x00953330, LineInput 0x00953483, Spinner 0x009537AA, Outline 0x00953A9D and Gen 0x00953BF0 |
| `framework.uiscript.widgetgen` | 1 | GZWinGen's property handler 0x009513BC |

**The registrations** (void, no arguments) are one shape: each name goes into the variable table by its slot 3 (a string made from the game's literal by SC4String::FromText 0x00408480, and the word; the string destroyed inline), first the class's names (the class and its interface; GZWinGen's also `SC3WinGen`, the same word as `GZWinGen`), then the class's handler under its interface into the handler map 0x00B6358C (an STLport map, inserted by 0x0094950A) and its interface under the class into the interface map 0x00B63598 (0x004FB6C0), then its property words (none for Folders and Custom; 3 to 28 for the others; 179 names in all). The dispatcher (the LEGACY reader's 0x00957A9B) finds a script's class in the interface map and its handler in the handler map, and calls it with the window, its properties and 1, then (after the base window's handler and the window's slot 23) with 0. Ours are tables of the game's literals (by address) and words, in the game's order; the maps get the game's handler addresses (Custom's is 0x00A6D837, a shared stub), so they hold what the game's would. EAX at the end is the last string's (the free's) or, for the two classes without words, the interface map insert's; the constructor does not read it.

**GZWinGen's handler** (cdecl: the window, its properties' vector of 8-byte (key, value) pairs and a byte) asks the window for IGZWinGen (QueryInterface 0x5386D516); without it nothing. With the byte set (the dispatcher's 1):

- 14 flags, each a number found by 0x0094C675: 0x10004 to 0x10009 (masks 1, 2, 4, 0x10, 0x20, 0x40), 0x1000A to 0x1000C (0x100, 0x200, 0x400) and 0x1000F (0x10000) set when the word is not 0; 0x1000D, 0xF00D, 0xF019 and 0x1000E (0x1000, 0x2000, 0x4000, 0x8000) set when it is 0; given to the interface's slot 6 (mask, bool), the bool pushed over the EDX 0x0094C675 leaves (part u12, slice 2);
- the user word 0x10000 (slot 19);
- the background image: the text of 0xF016 (0x0094CCD9) scanned as `{%x,%x}` (0x00AD5418) into a group and an instance; the image loaded by 0x0094C4E0; the mode a number of 0x10001 (0 when there is none; 0 and 2 as they are, else 1) given with the image to slot 13; the key (0x856DDBAC, group, instance) put under 0xAB428C5D (0x0094C1A4) and the two words under 0 (0x0094C1DA) through the window's slot 114; the image released;
- the pair of 0xF004 (0x0094D55A, slot 26);
- a new 8-byte object (vtable 0x00AD53EC, +4 zeroed; none without memory) given to slot 7.

With the byte clear, every pair (the vector's start read each time) with a text value: 0x10002 scanned as `(%u,%d)` (0x00AD5784) for slot 30, 0x10003 as `(%u,%d,%d,%d,%d)` (0x00AD578C) for slot 29 (the word, 0, 0 and the rectangle); the loop's index is kept in the window's own argument slot (the count there at the end, 0 for an empty vector), as the game does. The interface is released at the end and its EAX answered (without it the QueryInterface's).

The handler calls part u12's register-argument helpers by address (ours when their groups are on) through a naked helper that sets EAX, EBX, ESI and EDI to the game's values at each call and hands EDX back. Its thunk gives the body the argument slots.

### Testing

`OPENSC4_TEST_WIDGETREAD=<day>` is a test of its own (the fingerprint's `wr` part). It calls the 17 by address (EBX, ESI, EDI and EBP checked kept after each call). Two stages, each logged as `autotest: widgetread: <stage> <hash>`:

- **reg:** a variable table of our own put in the shared table's place (its slot 3 hashes every name's text and word), the 16 registrations in the constructor's order, after each the count of names and both maps' entries for the class (read from the trees); the game's table put back;
- **gen:** the UI script loader's held object set to one of our own (slot 3 gives one of two images of our own for even instances, none for odd), windows of our own (QueryInterface failing, answering the interface, and giving it with AL 0; slot 114 part u12's, which hashes what it gets), an interface of our own (every slot the handler calls hashes its arguments; slot 7 checks the object's vtable and frees it), number and text values of our own (slot 73 leaves a mark in EDX): the set pass on six vectors (every flag, words 0, 1, 2, 3, 5 and 7, a text and a null among them, good and bad image texts, odd and even instances, a missing and a present mode, good and bad pairs) and an empty one, with the byte 1 and 0xFFFFFF01; the raw pass on vectors of good and bad texts of both keys (a null, a number, a big word, negative numbers, another key) and an empty one, with 0 and 0x100; the handler's EAX and argument slots after each call; the counts of the interface, the images and the held object at the end.

With the groups on, each of the 17 logs its first call in a test run. `wr` is FBA9BCE8 (muted), the same on both sides; the stages reg 2C51068A and gen FBA9BCE8. A check with two deliberate slips in ours (the last property word dropped, the flags' bool without EDX's upper bytes) changed both stages' hashes.

The 17 were checked against the disassembly by the author and by an independent audit: every table row (literal and word) and both map inserts in order, the calls, their arguments and order, vtable slots, memory written and its order, the registers at each helper call (EAX, EBX, ESI and EDI where the helper reads them; the EDX the flags' bool is pushed over), EAX on every path, the argument slot the raw pass writes, the callers (the constructor reads no register after the registrations; the dispatcher none after the handler) and that nothing jumps into the first five bytes of any of the 17: no difference in what they do. Ours give the helpers 0 in registers they do not read where the game has leftovers there, and zero two locals the game leaves unset that are read only after a hit writes them. The handler's only reference is its map entry; the dispatcher, the map's only reader, calls it with 1 and then with 0, so both passes run in game runs (part u14's audit found the second call; this part first said the raw pass ran only in the test). The audit found three comments to fix (the tables' order, two property-word lists in the address database, the handler's EAX without an interface); fixed.

### Parity

Batches `gzu13-r1` (before the comment fixes) and `gzu13-r2` (the final build), 2026-10-23, muted on both sides, the base list (with `determinism.dispatch_info`) on both sides, every `framework.uiscript.*` (with `widgets` and `widgetgen`), `framework.mp3.*`, `framework.soundmix.*`, `framework.soundsvc.*`, `framework.sound.*`, `framework.xmlparse.*` and `framework.propio.*` group `=ours` on side B, two jobs, own test folder `p4env-gzu13`:

| Leg | Saves | Result |
|---|---|---|
| Making Money d0000, 4 days | 1 | same |
| Rush Hour t0000, 7 days | 1 | same |
| 12 saves, 7 days | 12 | 12 same |
| 12 saves, 7 days, `OPENSC4_TEST_WIDGETREAD=2` | 12 | 12 same; `wr` FBA9BCE8 in every run |
| Making Money d0000, 4 days, `OPENSC4_TEST_WIDGETREAD=2` | 1 | same; `wr` FBA9BCE8 |

r2: all five legs the same on the first pair. r1: the same but for Big City d0000 in the test leg, partly: B's first run parted from A at token 357 on the first day, and its two retry runs both matched A (B differing from itself; the same save parted this way in the audio batches, docs/audio.md).

**Reached:** in every plain (muted) leg side B logs all 17 first calls (14 of 14 runs): every LEGACY reader's constructor that makes the table registers the 16 classes, and the dispatcher calls GZWinGen's handler (both passes) when a city's UI loads.

**Part u13 complete:** the whole range is ours.

**What is left:** u14 to u16 (0x00954000 to 0x0095BBFF, the other widgets' registrations, readers and helpers) and u17 (the writers); u12's property handlers, which the other widgets' registrations put in the handler map.

## Part u14: GZWinFileBrowser's registration and property handler

0x00954000 to 0x0095BBFF holds the other widget classes' code (98 functions; parts u14 to u16): four more registrations of part u13's shape (GZWinFileBrowser 0x009552BE, GZWinOptGrp 0x00957C88, GZWinTreeView 0x009599D2, GZWinGrid 0x0095B021), each class's property handler (0x00954212, 0x00956C46, 0x009586B0, 0x0095A250), the base window's handler 0x0095660D and the dispatcher 0x00957A9B (the LEGACY reader's, which calls it and the class's handler), helpers and STLport code. Part u14 is the first class: 0x0095407A to 0x00956463, 41 functions (9.2 KB) ending where the next class's code starts (Grid's text helper 0x00956464, then the base handler). Ours: the class's two, in `src/core/gzui_widgets2.cpp`, two groups, off by default, installed at Attach:

| Group | Detours | What |
|---|---:|---|
| `framework.uiscript.filebrowser` | 1 | the registration 0x009552BE |
| `framework.uiscript.filebrowserread` | 1 | the property handler 0x00954212 (3,761 bytes) |

The other 39 stay the game's: STLport tree, list and node code (erases, node makers, the inserts 0x009562CC to 0x009563DC, the clears), pair and record copies (0x009550C3 a 0x66-byte record of three strings, 0x00955191 one of a string and six words) and a counted object's Release (0x0095407A), which the file browser window's own code (elsewhere) and the next classes use.

**The registration** is part u13's shape (its table code is copied): GZWinFileBrowser (0x1AA52EA4) and IGZWinFileBrowser (0x3AA52E64) into the variable table, the handler 0x00954212 under the interface into the handler map, the interface under the class into the interface map, then 66 property words, 0x1301 `defaultpath` to 0x1342 `filter`, in the game's order (the names of the values too: `backdir`, `dir`, `file`, the sorts, `icon`, `name`, `extension`, `empty`, `unused`, `default`). The constructor 0x0095B8C4 calls it among the others.

**The handler** (cdecl: the window, its properties' vector of 8-byte (key, value) pairs and a byte) asks the window for IGZWinFileBrowser (QueryInterface 0x3AA52E64). The dispatcher calls every class's handler twice: with the byte 1, then (after the base window's handler 0x0095660D and the window's slot 23) with 0; with 0 this one only asks and releases. With it and the byte set (the dispatcher's 1), in this order, each through the interface's slot named (word offsets):

- the default path, the text of 0x1301 (0x0094CCD9), slot 15;
- the default selection: the texts of 0x1306 and 0x1307 always read, then the kind of 0x1305 (`backdir` 0, `dir` 1, `file` 2) with both texts to slot 28;
- the file sorts: slot 34 gives the two, 0x130C and 0x130D replace them (the four `byname`/`byextension` words, 0 to 3), slot 35; the directory sort 0x1312 (`top`/`bottom` words, 0 to 3), slot 37;
- the three columns: slot 38 gives them, 0x1319 to 0x131B replace them (`icon` 0, `name` 1, `extension` 2, `empty` 5), slot 40;
- for the file names, the extensions and the drive names (slots 45 to 47, 51 to 53, 57 to 59): the font, a number (the window manager 0x00913C72's slot 5 makes the font for the face's slot; the number put under the window's key 0, 1 or 2 by 0x0094C222) or else a text (put under it by 0x0094C255); six colours (0x0094D4CD, the graphics system 0x00913C1A's slot 34 makes the colour of the three bytes, given with 0 to 5); the alignment (`unused` 0, `default` 1, 0xF006 2, 0xF007 4, 0xF008 3);
- the icon column's width 0x1334 (slot 61), the extension column's 0x1335 (63), the row height 0x1336 (65);
- the line grid and the column grid: slots 66 and 68 give a flag and a colour, 0x1337 or 0x1339 sets the flag's low byte (word not 0), 0x1338 or 0x133A the colour, slots 67 and 69 take them; the two share their places, so a getter that writes nothing hands on the line grid's;
- the highlight's two colours (slots 70 and 71, 0x133B and 0x133C);
- the back text 0x133D (slot 77), the drive list's size 0x133E (81), the background colour 0x133F (83);
- 0x1341 (`backgroundtransparent`, slot 87) and 0xF019 (slot 79), each a bool pushed over the EDX 0x0094C675 leaves (part u12, slice 2);
- with a text of 0x1340 (`backgroundimage`): scanned as `{%x,%x}`, the image loaded (0x0094C4E0) and given to slot 85, the group and instance put under the window's 0 (0x0094C1DA), the image released; then the filters: every pair of 0x1342 whose value is a text (slot 3 answers 0x800E; a null value is called as the game's is), its text cut at `:` (0x00AA0424, SplitOff 0x0090E98C; each part copied by 0x00408BE0) into a name and a number (`%d`, 0 when it is not one), the rest scanned as `{%x,%x}` for an image (loaded, and put under the window's index + 1), slot 21 with the name, the image, the number and 0, the image released. Without the background image's text there are no filters.

The texts go into four strings the handler makes (the path's also takes the back text, the background image's and each filter's; another every font text); they are destroyed inline at the end in the game's order. The interface is released and its EAX answered (without one the QueryInterface's). The colour reader writes only the low bytes of three places the game shares between all its calls; above them ours are 0 where the game has what its stack held (the graphics system's slot 34 hands them to the driver's slot 30). The helpers are called by address (ours when their groups are on) through part u13's naked helper, here with four stack arguments, which sets EAX, EBX, ESI and EDI to the game's values where the helper reads them and hands EDX back.

### Testing

`OPENSC4_TEST_FILEBROWSER=<day>` is a test of its own (the fingerprint's `fb` part). It calls the two by address (EBX, ESI, EDI and EBP checked kept after each call). Two stages, each logged as `autotest: filebrowser: <stage> <hash>`:

- **reg:** part u13's variable table of our own in the shared table's place (its slot 3 hashes every name's text and word), the registration, the count of names and both maps' entries for the class; the game's table put back;
- **read:** the UI script loader's held object set to part u13's (an image of our own for even instances, none for odd), part u13's windows (QueryInterface failing, answering the interface, and giving it with AL 0; slot 114 part u12's, which hashes what it gets), an interface of our own (every slot the handler calls hashes its number and what it gets; the getters give words of their own, the column grid's only when told to, so the shared places are seen), number and text values of our own (slot 73 leaves a mark in EDX): four vectors (every key, good and bad words, number and text fonts, colours good, short, out of a byte's range and not numbers, good and bad images, filters good, without a number, with a bad image, with a number value and under another key) and an empty one, with the byte 1, 0xFFFFFF01, 0 and 0x100 and both column grid getters; the handler's EAX and argument slots after each call; the counts of the interface, the images and the held object at the end. The fonts are the window manager's for three of the game's font words (the slot hashes which it got); the colours are the graphics system's.

With the groups on, each of the 2 logs its first call in a test run. `fb` is 02DB227F (muted), the same on both sides; the stages reg 900193A7 and read 02DB227F. A check with two deliberate slips in ours (the last property word dropped, the transparency bool without EDX's upper bytes) changed both stages' hashes.

The two were checked against the disassembly by the author and by an independent audit: every table row (literal and word) and both map inserts in order; in the handler every call, its arguments and order, the vtable slots, the registers each helper reads (EBX and EDI for 0x0094CCD9, EDI for 0x0094C675 and 0x0094D4CD, ESI for 0x0094C222 and 0x0094C255, EAX for 0x0094C4E0, EAX and ESI for 0x0094C1DA, checked in the helpers' own code), the places the game shares (the colour bytes, the grids' pair, the strings), the byte-only flag writes, the EDX under the bools, the filter loop, the strings' destruction order, EAX on every path; the callers (the constructor's one call of the registration, which reads no register after; the handler referenced only by its map entry, the dispatcher reading only kept registers after both its calls) and that nothing jumps into the first five bytes of either: no difference in what they do. Ours give the helpers 0 in registers they do not read, keep the colour bytes' upper bytes 0 and zero the getters' places before the getters write them. The audit found the database's descriptions incomplete (the dispatcher's second call with 0, the slots of the fonts and alignments, the row height); fixed. It also found that the dispatcher's second call is unconditional, so part u13's raw pass of GZWinGen's handler runs in game runs too, not only in the test as part u13 says.

### Parity

Batch `gzu14-r1` (the final build), 2026-10-23, muted on both sides, the base list (with `determinism.dispatch_info`) on both sides, every `framework.uiscript.*` (with `filebrowser` and `filebrowserread`), `framework.mp3.*`, `framework.soundmix.*`, `framework.soundsvc.*`, `framework.sound.*`, `framework.xmlparse.*` and `framework.propio.*` group `=ours` on side B, two jobs, own test folder `p4env-gzu14`:

| Leg | Saves | Result |
|---|---|---|
| Making Money d0000, 4 days | 1 | same |
| Rush Hour t0000, 7 days | 1 | same |
| 12 saves, 7 days | 12 | 12 same |
| 12 saves, 7 days, `OPENSC4_TEST_FILEBROWSER=2` | 12 | 12 same: 11 on the first pair, Rush Hour d0090 partly (B's first run parted from A at token 200 on the first day, B's two retry runs both matched A); `fb` 02DB227F in every run |
| Making Money d0000, 4 days, `OPENSC4_TEST_FILEBROWSER=2` | 1 | same; `fb` 02DB227F |

The test leg again (`gzu14-r1b`, the same build): 12 same on the first pair. `fb` was 02DB227F in all 54 test runs of the two batches. B differing from itself on that save is the pattern of earlier batches (parts u3 and u10, the same save in part u3).

**Reached:** in every plain (muted) leg side B logs 1 first call (14 of 14 runs): the registration, which every LEGACY reader's constructor that makes the table runs. No script the runs load makes a file browser, so the handler is reached only by the test (it did not run in the coverage runs either).

**What is left:** u15 and u16 (0x00956464 to 0x0095BBFF: GZWinOptGrp, GZWinTreeView and GZWinGrid, the base window's handler, the dispatcher and their helpers) and u17 (the writers); u12's property handlers.

## Part u15: GZWinOptGrp, the base window's handler and the dispatcher

Part u15 is the second slice of 0x00954000 to 0x0095BBFF: 0x00956464 to 0x009586AF, 23 functions (8.8 KB), ending where GZWinTreeView's handler 0x009586B0 starts. Ours: four, in `src/core/gzui_widgets2.cpp` (with part u14's), four groups, off by default, installed at Attach:

| Group | Detours | What |
|---|---:|---|
| `framework.uiscript.optgrp` | 1 | GZWinOptGrp's registration 0x00957C88 |
| `framework.uiscript.optgrpread` | 1 | GZWinOptGrp's property handler 0x00956C46 (2,629 bytes) |
| `framework.uiscript.baseread` | 1 | the base window's property handler 0x0095660D (1,593 bytes) |
| `framework.uiscript.dispatch` | 1 | the dispatcher 0x00957A9B, the LEGACY reader's slot 3 |

The other 19 stay the game's; none is called by the four: Grid's text helper 0x00956464 (its only caller is GZWinGrid's handler, part u16), the STLport inserts and finds of TreeView's and Grid's maps (0x0095768B to 0x009579DA, 0x0095811B to 0x009582C1), their node copies (0x00957A5E, 0x00958352, 0x009585FB) and erasers (0x00957A7C, 0x00958679), and two of Grid's cell lookups (0x009583C5, 0x009584E0). TreeView's handler (4,857 bytes) and the rest go to part u16.

**The registration** is part u13's shape (part u14's table code): GZWinOptGrp (0xA1336CC1) and IGZWinOptGrp (0xA1336CC0) into the variable table, the handler 0x00956C46 under the interface, the interface under the class, then 19 property words, 0xF00 `maincaption` to 0xF12 `optionsetsize`.

**The dispatcher** (thiscall on the LEGACY reader: the properties' vector; `ret 4`) makes a script's window. The class is the number under -1 (0x0094C675; none or 0: nothing more), the interface the number under -2, else the interface map's for the class. The COM (0x0090DDF1, slot 3, IGZWin 0x22BA0121) makes the window into the reader's +0x10 for the class; failing that for the first class in the interface map whose interface it is (the map walked by 0x0040D4C0); failing that for GZWinCustom 0x478D1E6F (failing that, nothing more). The class and, when there is one, the interface go to the window as numbers (0x00915D6D) under 0xE85EEBDE and 0x08D6E462 (slot 114); the reader's +0x1C is set; the class's handler is the handler map's for the interface (0x0098129C). Then: the class's handler with 1, the base handler with 1, 0x0094C77D (the reader's +0x18 object handed on), the window's Init (slot 4), the window to the parent at +0x14 (slot 14; the parent released and cleared) or, without one, to the window manager's top window, the window's slot 23, the class's handler with 0, the base handler with 0. AL 0 over the last call's EAX.

**The base window's handler** (cdecl: the window, the vector, a byte) runs for every scripted window. With the byte set, through the window's slots: slot 68 (mask, bool) with seven defaults (1, 0x10000, 0x20000, 0x40000, 0x8000 on; 0x800 and 4 off), then eight keys' words (0xF01A 1, 0xF01C 0x100, 0xF01D 0x200, 0xF01E 0x800, 0xF025 0x8000, 0xF026 4, 0xF023 0x80000, 0xF024 0x200000; on when not 0); the user word 0x100 (slot 64); the area, the text of 0x101 scanned as `( %d, %d, %d, %d)` (0x00AD657C, slot 54), or without it the pairs of 0x102 (slot 56) and 0x103 (slot 53); the colour 0x104 (slot 107); the caption 0x105 (slot 74); the caption's resource, the text of 0x106 scanned as `{%x,%x}` (the count not checked), its text (0x0094C58D) as the caption and the key (0x2026960B, group, instance) under 0x0B31771A (0x0094C1A4); the two words (0 without 0x106) as an 8-byte value under 0x3ACA6906 (slot 114); 0x107 present: 0x20000 on; the texts of 0x108 to 0x10F (each pushed, found or not, so a missing one repeats the last found) in a counted list (0x14 bytes, vtable 0x00AD5420, its string vector at +4) given to a new script value's slot 61, the value under 0xBACA6879. With the byte clear five more: 0xF01B 2, 0xF01F 0x10000, 0xF020 0x20000, 0xF021 0x40000, 0xF022 0x100000. The handler keeps the found words, a colour's third byte and the list in its byte's own argument slot, and pushes the bools over that slot's address (the game's ECX); ours does the same through a naked thunk that hands the body the argument slots.

**GZWinOptGrp's handler** asks for IGZWinOptGrp (QueryInterface 0xA1336CC0). With the byte set: four flags (0xF00, 0xF01, 0xF02, 0xF019; masks 1, 2, 4, 8; on when the word is 1; slot 5, the bool over the EDX 0x0094C675 leaves); the pair 0xF004 (slot 6); two fonts (0xF03 slot 8 and the window's 0, 0xF04 slot 9 and 1: a number through the font service's slot 5 and 0x0094C222, else a text through 0x0094C255); five colours 0xF05 to 0xF09 (slot 10 with 0 to 4; the found word's place is the first byte's); the caption's resource 0xF0A (`{%x,%x}`, the count not checked: the text to the interface's window's (slot 3) slot 74, the two words under 0x4A4AF4AC); the back image 0xF0B (`{%x,%x}`: slot 20, the words under the window's 0 by 0x0094C1DA); four colours 0x40B scanned as `(%d,%d,%d)` four times (0x00AD6590; the colour service asked four times, the colours made last to first, slot 21; the window's argument slot then holds the service); autofill 0xF0C (into the window's argument slot): `automatic` 0xF0E with the size 0xF10 gives slot 28 (0, size), nothing without the size; anything else (1, 0). With the byte clear: every option (0xF0D `option`, a text `id:name:number:{group,instance}:(x,y,w,h)` cut at its colons by SplitOff 0x0090E98C and 0x00424380): slot 15 with the id, the name, the number, the image (none unless the key scans) and the rectangle (0, 0, 1, 1 unless the rest scans as four numbers); slot 19's item for the id gives its window (slot 3) the key's words (0x0094C1DA), released; the image released. Then every move (0xF11) and size (0xF12), `id:(x,y)` (slots 17 and 18); the size word 0x501 (slot 14); 0x304 present: a bool 1 under 0xDACF8F9C and slot 24, else a bool 0. The interface released, its EAX answered (the QueryInterface's without one).

**Where the game reads its stack:** the base handler's area rectangle where the text has fewer than four numbers, the colour's first byte above its low byte and its second's when no pair was read before; GZWinOptGrp's colours' second and third bytes above their low bytes (and the first's when no word was found before), the caption's two words when 0xF0A does not scan, and the first option's id and number when they do not scan. Ours has 0 there; the slot that gets them sees only those leftovers differ.

### Testing

`OPENSC4_TEST_OPTGRP=<day>` is a test of its own (the fingerprint's `og` part). It calls the four by address (EBX, ESI, EDI and EBP checked kept after each call). Four stages, each logged as `autotest: optgrp: <stage> <hash>`:

- **reg:** part u13's variable table of our own, the registration, the count of names and both maps' entries;
- **base:** the base handler on a window of our own (its slots 53, 54, 56, 64, 68, 74, 107 and 114 hash what they get; the bools' upper bytes as which they are: clean or the argument slot's address; the colours' low bytes; the text list's texts read through the argument slot), part u12's held object (images and texts), four vectors and an empty one, with the byte 1, 0xFFFFFF01, 0 and 0x100; the argument slots after each call;
- **read:** GZWinOptGrp's handler on part u13's windows (QueryInterface failing, answering, answering with AL 0) and an interface of our own (every slot it calls hashes its number and what it gets; slot 19 gives an item of ours for even ids), the game's font and colour services, four set vectors (flags, pairs, number and text fonts, colours good, short, out of range and not numbers, good and bad images, four colours good, short and out of range, autofill automatic with and without a size, manual and 0) and three raw ones (good options, options with a bad id, number, image or rectangle, without parts, with a part too many, a number value, another key; moves and sizes good and bad), with the byte 1, 0xFFFFFF01, 0 and 0x100; the window's argument slot after (the window, the colour service or a word);
- **disp:** the dispatcher on a reader of our own (a parent of our own at +0x14) with real windows: no class, class 0, a text class, GZWinFlatRect under a test interface whose handler (ours, put in the handler map under 0x7E570001) hashes each pass, an unknown class with FlatRect's interface, an unknown class with no interface and with an unknown one (GZWinCustom made), GZWinFlatRect by the interface map (its own handler), GZWinCustom under the test interface; AL, the reader's fields, the parent's calls; the window released.

The inputs avoid the places where the game reads its stack. With the groups on, each of the 4 logs its first call in a test run. `og` is 34E30829 (muted), the same on both sides; the stages reg D02087C6, base 825ED40E, read F732ABC4, disp 34E30829. A check with four deliberate slips in ours (the last property word dropped, the base handler's bools without the slot's address above them, the options' default rectangle's height 0, the reader's +0x1C not set) changed all four stages; each slip alone (but the first, whose stage comes first) changed its own stage and none before it. A GZWinGen window made by the dispatcher and released without ever having a parent leaves the game hanging when it quits (in the game's code as in ours), so the test makes FlatRect and Custom windows only.

The four were checked against the disassembly by the author and by an independent audit: the table's 21 rows and both map inserts in order; in the handlers every call, its arguments and order, the vtable slots, the places locals share (the found word and the colours' first byte, the pairs' first word and the colour's second byte, the byte's argument slot for words, a pair's second word, a colour's third byte and the list, the window's argument slot for the colour service and the autofill words), the registers each helper reads, the upper bytes of the bools (the argument slot's address in the base handler, the EDX 0x0094C675 leaves in GZWinOptGrp's), the strings' order of making and destroying, the loops (the counts read once per loop, the vector's start read for every pair), EAX on every path; in the dispatcher the lookups, the three ways the window is made, the order of the calls, the window read again from +0x10 at each; the callers (the constructor 0x0095B8C4 reads nothing after the registration; 0x0094BABC, the dispatcher's only caller through the reader's vtable at 0x00AD536C, ignores its EAX; the handlers referenced only by the map and the dispatcher, which reads nothing after them): no difference in what they do. The only edge it found is unreachable: if a QueryInterface answered yes and gave no interface, GZWinOptGrp's handler would answer the last string's EAX where ours answers the QueryInterface's (part u14's handler has the same shape). It found five comments to fix (the options' key written 0xF00D in a comment and the database, which is 0xF0D; what the argument slot holds; the place the caption's second word shares; the flags' words turning a flag off as well as on; a class of 0 doing nothing); fixed. The places where the game reads its stack (above) are its list.

### Parity

Batch `gzu15-r1` (the final build, before the merge of main), 2026-10-23, muted on both sides, the base list (with `determinism.dispatch_info`) on both sides, every `framework.uiscript.*` (with `optgrp`, `optgrpread`, `baseread` and `dispatch`), `framework.mp3.*`, `framework.soundmix.*`, `framework.soundsvc.*`, `framework.sound.*`, `framework.xmlparse.*` and `framework.propio.*` group `=ours` on side B, two jobs, own test folder `p4env-gzu15`:

| Leg | Saves | Result |
|---|---|---|
| Making Money d0000, 4 days | 1 | same |
| Rush Hour t0000, 7 days | 1 | same |
| 12 saves, 7 days | 12 | 12 same |
| 12 saves, 7 days, `OPENSC4_TEST_OPTGRP=2` | 12 | 12 same; `og` 34E30829 in every run |
| Making Money d0000, 4 days, `OPENSC4_TEST_OPTGRP=2` | 1 | same; `og` 34E30829 |

All five legs the same on the first pair; `og` was 34E30829 in all 26 test runs.

**Reached:** in every plain (muted) leg side B logs 3 of the 4 first calls (14 of 14 runs): the registration (every LEGACY reader's constructor that makes the table), the dispatcher and the base handler (every scripted window of a city's UI). No script the runs load makes an option group, so its handler is reached only by the test.

**What is left:** u16 (0x009586B0 to 0x0095BBFF: GZWinTreeView's handler 0x009586B0 and registration 0x009599D2, GZWinGrid's handler 0x0095A250, registration 0x0095B021 and text helper 0x00956464, the LEGACY reader's constructor 0x0095B8C4 and their helpers) and u17 (the writers); u12's property handlers.

## Part u16a: GZWinTreeView's registration and property handler

Part u16a is the first slice of u16's range (0x009586B0 to 0x0095BBFF, 34 functions): GZWinTreeView's handler 0x009586B0 (4,857 bytes), the map clear 0x009599A9 and the registration 0x009599D2. Ours: the class's two, in `src/core/gzui_widgets2.cpp` (with parts u14 and u15's), two groups, off by default, installed at Attach:

| Group | Detours | What |
|---|---:|---|
| `framework.uiscript.treeview` | 1 | GZWinTreeView's registration 0x009599D2 |
| `framework.uiscript.treeviewread` | 1 | GZWinTreeView's property handler 0x009586B0 (4,857 bytes) |

Still the game's, called by ours by address: the STLport code of the handler's maps, lists and records (the inserts 0x0095811B, 0x009581AC, 0x00958230 and 0x009582C1 of part u15's range; the map makers 0x00939D41, 0x009561DC and 0x0089508C; the name find 0x0094CA22; the record's constructor, destructor and copy 0x0094D147, 0x0094D163 and 0x009550C3; the record list's 0x0094C975, 0x009561F5, 0x0095409E and 0x0095517A; the work list's 0x009C9DDB, 0x009CAA9D, 0x009CAA5C, 0x00955144 and 0x009D7712; the pairs 0x00894585, 0x0095515B, 0x009CA065 and 0x004CCF20; the rectangle test 0x0094C49A), all with their database entries (`gzui.toml`, "Part u16a"). The map clear 0x009599A9 is called only by the STLport code after the registration (0x00959F09, 0x0095A0D9), not by the two; it goes with u16b.

**The registration** is part u13's shape (part u14's table code): GZWinTreeView (0x3AE8BAE1) and IGZWinTreeView (0x3AE8BAD2) into the variable table, the handler 0x009586B0 under the interface, the interface under the class, then 18 property words, 0x1401 `displayroot` to 0x1412 `rootselected`.

**The handler** (cdecl: the window, the vector, a byte) asks for IGZWinTreeView (QueryInterface 0x3AE8BAD2). With the byte set: four flags 0x1401 to 0x1404 (slot 3, masks 2, 4, 8 and 0x10, on when the word is not 0, the bool over the EDX 0x0094C675 leaves); the words 0x1405 `icongutter` (slot 7), 0x1336 (the row height, slot 11) and 0x1406 `minmaxboxsize` (slot 13); two colours, 0x133F (slot 15) and 0x408 (slot 17). The found words go into the window's argument slot, which is also the colours' third byte's place; ours writes it through a naked thunk that hands the body the argument slots, as part u15's.

With the byte clear, five steps, each map handed to the window as a number of its address (0x00915D6D, slot 114):

- **Images:** every 0xF016 text, `name:{group,instance}:(x,y,w,h)` cut at its colons (SplitOff 0x0090E98C). The name and its key into the key map (0x66C20802); with an image loaded (0x0094C4E0), slot 20 with the image and the rectangle (the image's own, its slot 12, when the four are 0) gives its index, put into the name map (name to index, 0x46C207A5) and the index map (index to name, 0xC6C207C8); the image released.
- **The root:** its text 0x1407 (slot 32 with node 0); four icons 0x1408 to 0x140B by name (slot 36 with 0 to 3: the name map's index, else 0 to 3); four colours 0x140C to 0x140F (slot 43); the font 0x1410 (a number: the font service's slot 5 for slot 39, put through the window by 0x0094C222; else a text, by 0x0094C255); expanded 0x1411 (slot 45 with the word's being 1, the bool over the EDX 0x0094C675 leaves); selected 0x1412 (slot 56 when 1); then slot 9 with the colour (0,0,0) and 1.
- **Nodes:** every 0x1102 text, `name:text:parent:position:icon:icon:icon:icon:(r,g,b)x4:font:expanded:selected`, into a record (0x68 bytes) put at the end of a list: the position `first`, `last` or `sorted` (-2, -3, -4; any other word leaves the place as it was), the icons by name (else 0 to 3), the colours (`(%d,%d,%d)`, the colour service's slot 34), the font by name (the font service's slot 38, then slot 5), `yes` for each byte (the last compared with the whole rest). The font's word by the node's name into the font map (0x86C21DA0).
- **The tree:** a work list starts with (`Root`, 0); while it has pairs, the last pair's name is looked for as a record's parent (the first in the list). Found: slot 24 with the pair's node, the record's position and text gives the node; slots 36, 39, 43 and 45 with the record's icons, font, colours and expanded byte (pushed as the dword at +0x64), slot 56 when selected; the node into the node map (name to node, 0xE6C21D83) and the node-name map (node to name, 0x26C21E35), its (name, node) pair at the work list's end, the record erased from the list. Not found: the last pair taken off. So the tree is built depth first and a node whose parent never comes is never added.
- The work list and the record list destroyed, the strings last.

The interface released, its EAX answered (the QueryInterface's without one). The game keeps the raw pass's locals in one frame, and ours keeps the places it shares: the images' last index, instance and the pair counter are the root colours' three bytes' places and the root's words'; one record's place serves every node and the copy the tree loop makes.

**Where the game reads its stack:** the set pass's colours' first two bytes above their low bytes (ReadBytes writes only the low bytes); the root's colours' first two when no 0xF016 text came before; the first node's position when its word is none of the three; and the record's two bytes after the selected byte, which go to slot 45 above the expanded byte. Ours has 0 there; the slot that gets them sees only those leftovers differ.

### Testing

`OPENSC4_TEST_TREEVIEW=<day>` is a test of its own (the fingerprint's `tv` part). It calls the two by address (EBX, ESI, EDI and EBP checked kept after each call). Two stages, each logged as `autotest: treeview: <stage> <hash>`:

- **reg:** part u13's variable table of our own, the registration, the count of names and both maps' entries;
- **read:** the handler on part u13's windows (QueryInterface failing, answering, answering with AL 0; slot 114 hashes the six maps' entries, read by the game's tree code, and part u12's values) and an interface of our own (every slot it calls hashes its number and what it gets; slot 20 gives indices of its own, slot 24 nodes of its own), part u13's images with a slot 12 of our own, the game's font and colour services; three set vectors (flags as numbers and text, words, colours good, short and out of range) and three raw ones (images with and without an image, an empty and a bad rectangle, a bad key, a number value; the root's text, icons found and not, colours, the font as a number and a text, expanded and selected; nodes under the root and under nodes, an unknown position word, an orphan, a short text, a duplicate name, a part too many), with the byte 1, 0xFFFFFF01, 0 and 0x100, and empty vectors; the argument slots after each call.

The inputs avoid the places where the game reads its stack: every vector's first node names its position, the root's colours come only in vectors with an image text, and a node's expanded bool is hashed by its low byte. With the groups on, each of the 2 logs its first call in a test run. `tv` is BDD5B16A (muted), the same on both sides; the stages reg 1FF0635E, read BDD5B16A. A check with five deliberate slips in ours, one build each (the `rootfont` word 0x1411, the fourth flag's mask 0x20, a node's unfound icon 0, the selection by the expanded byte, the image's own rectangle for a non-empty one): the first changed reg (and so read), each of the others changed read.

The two were checked against the disassembly by the author and by an independent audit: the table's rows and both map inserts in order; in the handler every call, its arguments and order, the vtable slots, the helpers' bodies, the places locals share (above), the registers each helper reads, the bools' upper bytes, the strings' order of making and destroying, the loops (the counts read once per loop, the vector's start read for every pair), the results the work list's insert and the list's erase write over the node's place, EAX on every path: no difference in what they do. The only edge is unreachable (a QueryInterface answering yes with no interface: the game would answer the last call's EAX, ours the QueryInterface's; parts u14 and u15's handlers have the same shape). It found four comments to fix (what the expanded dword holds, the record's unwritten bytes, how the root's font is put, which two maps the tree fills) and one in the database (the root colours' third byte); fixed.

### Parity

Batch `gzu16a-r1` (the final build), 2026-10-24, muted on both sides, the base list (with `determinism.dispatch_info`) on both sides, every `framework.uiscript.*` (with `treeview` and `treeviewread`), `framework.mp3.*`, `framework.soundmix.*`, `framework.soundsvc.*`, `framework.sound.*`, `framework.xmlparse.*` and `framework.propio.*` group `=ours` on side B, two jobs, own test folder `p4env-gzu16a`:

| Leg | Saves | Result |
|---|---|---|
| Making Money d0000, 4 days | 1 | same |
| Rush Hour t0000, 7 days | 1 | same |
| 12 saves, 7 days | 12 | 12 same |
| 12 saves, 7 days, `OPENSC4_TEST_TREEVIEW=2` | 12 | 12 same; `tv` BDD5B16A in every run |
| Making Money d0000, 4 days, `OPENSC4_TEST_TREEVIEW=2` | 1 | same; `tv` BDD5B16A |

All five legs the same on the first pair; `tv` was BDD5B16A in all 26 test runs.

**Reached:** in every plain (muted) leg side B logs 1 of the 2 first calls (14 of 14 runs): the registration (every LEGACY reader's constructor that makes the table). No script the runs load makes a tree view, so its handler is reached only by the test.

**What is left:** u16b (0x00959E31 to 0x0095BBFF, 31 functions, and the map clear 0x009599A9: GZWinGrid's handler 0x0095A250 and registration 0x0095B021, its text helper 0x00956464 (in part u15's range), the LEGACY reader's constructor 0x0095B8C4, the constructor bits from 0x0095B9A1, the STLport code from 0x00959E31) and u17 (the writers); u12's property handlers.

## Part u16b: GZWinGrid, GZWinCustom's class code and the LEGACY reader's constructor

Part u16b is the rest of u16's range: 0x00959E31 to 0x0095BBFF, 32 functions. That count includes the LEGACY reader's constructor 0x0095B8C4, which the function list folds into the registration before it. The part also takes the map clear 0x009599A9 and GZWinGrid's text helper 0x00956464, which sits in part u15's range. Ours: 10, in four groups, off by default, installed at Attach:

| Group | Detours | What |
|---|---:|---|
| `framework.uiscript.grid` | 1 | GZWinGrid's registration 0x0095B021 (`src/core/gzui_widgets2.cpp`) |
| `framework.uiscript.gridread` | 2 | GZWinGrid's property handler 0x0095A250 (3,537 bytes) and its cells' text helper 0x00956464 (`src/core/gzui_widgets2.cpp`) |
| `framework.uiscript.custom` | 6 | GZWinCustom 0x478D1E6F's constructor 0x0095BAB3, destructor 0x0095B9A1, deleting destructor 0x0095BA97, QueryInterface 0x0095B9C1, slot 3 0x0095B9F8 and slot 88 0x0095BA43 (`src/core/gzui_widgets2.cpp`) |
| `framework.uiscript.legacyctor` | 1 | The LEGACY reader's constructor 0x0095B8C4 (`src/core/gzui_legacy.cpp`, with part u11's slots) |

Still the game's, with their database entries (`gzui.toml`, "Part u16b"):

- **Not called by ours:** the STLport code of the grid's three maps (0x00959E31 to 0x0095A0D9: the holders' Release and destructors, the maps' clears and destructors, the outer map's pair copies, node maker, insert and insert_unique) and the map clear 0x009599A9.
- **Called by ours by address:** the cell set 0x0095A0F3; the column and row sets 0x009583C5 and 0x009584E0 (part u15's range); the makers of the record, the map and the style, 0x0094D490, 0x009ABB63 and 0x009A5BF2.
- **GZWinCustom's four adjustors** 0x0095BA6F to 0x0095BA8C: jumps into ours and into the window's AddRef and Release.

The writers are the only callers of 0x0095BAD7, 0x0095BB29 and 0x0095BB82, so these three go with u17.

The "constructor bits from 0x0095B9A1" belong to GZWinCustom, not to the reader. GZWinCustom is the class the dispatcher (part u15) makes a script's window of when the script names no class it knows. Its factory 0x0094AFA6 (part u9) is its only maker.

**The registration** has part u13's shape. GZWinGrid (0xDAA6B9BF) and IGZWinGrid (0xDAA6B9BE) go into the table, the handler under the interface, the interface under the class. Then 39 words, in the game's order: 0x1601 `fontcolor` to 0x1624 `rowgridclr`, then `wingridcell` 0x1625, `wingridrow` 0x1627 and `wingridcol` 0x1626.

**The text helper** is cdecl, with the cell's text in EAX; ours enters through a thunk that hands the body EAX and the argument slots. It cuts the text at its commas into 11 strings (SC4::SplitText). With 4 or more parts:

- the row and column come from TextToUint32, the flags from TextToInt (hex);
- with the flags' 0x40000 the fourth part is assigned to the out string; otherwise the fourth and fifth are printed into it as `%s,%s`;
- with 0x1000 the next part is a font name (the font service's slot 38) and the five after it are numbers.

The game's array destructor then destroys the 11 strings; AL is 1 or 0 over its EAX.

**The handler** is cdecl (the window, the vector, a byte) and asks for IGZWinGrid (QueryInterface 0xDAA6B9BE). With the byte set it does three things.

**Settings**, in this order:

- **Text colour 0x1601:** the interface's window (slot 5), then that window's slot 108, for slot 17.
- **Font 0xF000:** a number goes through the font service's slot 5 to slot 15, then is put through the window by 0x0094C222; a text is put by 0x0094C255 alone; with neither, slot 15 gets the font 0x68963C4C.
- **Simple words:** 0x1602 (slot 60); 0x1603 (slot 21, on when the word is not 0); the gutters 0xF004 (slot 24, zeros without four numbers); 0x1605 and 0x1606 (slot 39; 2 and 1 by default).
- **Outline:** 0x1616 (word 1) and its colour 0x1604 go to slot 27, as the bool over the colour's upper bytes, then the colour.
- **Sizes:** 0x1607 and 0x1608 (slots 64 and 66; 100 and 20 by default).
- **Headers:** 0x1609 (slot 74: 0 both, 1 the first, 2 the second, else none) and their sizes 0x160A and 0x160B (slot 76).
- **Flags:** 23 keys, 0x160C to 0x1622. Slot 10 when the word is not 0, else slot 12. The masks are 0, 1, 2, 4 to 0x80000, 0x19BFF and -1. Turning 0x160F off also calls the window's slot 92.

**Cells:** every 0x1625 text goes through the text helper.

- The key `%08x,%08x` is read from the out string when the flags' word has no 0x40000. The flags' word is the last flag word found, not the cell's own flags.
- The first cell makes the cells' map: a map 0x009ABB63 in a held holder (vtable 0x00AD538C), given to a new number value (slot 61) that is put under 0x287A7136 through the window; then the holder is released.
- With the cell's 0x40000, the out string is printed (`%s`) into the record's string and given as the text. Slot 112 gets the row, column, characters, length, 1 and the flags' 0x80000.
- Without 0x40000, the key goes into the record. With 0x2000 its image (0x0094C4E0) goes to slot 114 and is released; with 0x10000 its text (0x0094C58D) goes to slot 112.
- With 0x1000, slot 85 gets the style (0x009A5BF2): the font word through the font service's slot 5 (when not 0), a word, three bytes, a word, and the byte 1 or -1 by 0x100000.
- The record is set in the map (0x0095A0F3) and its string destroyed.

**Columns and rows:** every 0x1626 and 0x1627 text of three numbers `%d,%d,%d`. Before the scan the first two are 0xDEADBEEF and the third 0. The first one makes its map, under 0xC88CCB6D or 0xC88CCB6E (holders 0x00AD5398 and 0x00AD53A4). Then:

- a column: 0x009583C5 with (first, 0), and slot 65 with (first, 1, third);
- a row: 0x009584E0 with (0, first), and slot 67 with (first, second - first + 1, third).

With the byte clear, the line colours 0x1623 and 0x1624 go through the window's slot 108 to slot 58. The window's argument slot is the third byte's place, with its low byte cleared first. So ours, like part u15's handlers, enters through a naked thunk that hands the body the argument slots. The interface is released and its EAX answered.

**A bug of the game's.** At 0x0095A2C4 the handler pushes four arguments to the window's slot 108, which takes three (`ret 0xC`), so the fourth stays on the stack.

- When 0x1601 is found, the handler's last pops take that leftover dword. The game returns with EBX 0, EDI holding the caller's EBX and ESI the caller's EDI.
- Its dispatcher then pushes `[ESI]` (the handler's own first bytes) as the window for the base handler, and the base handler fails.

So a grid that sets its text colour would stop the game. No script the runs load does that, which is why the bug was never seen. Ours keeps the registers, and with the game's dispatcher it then goes on. This is the one difference from the game, and it is only on a path where the game fails.

**Where the game reads its stack:**

- the colours' places above their low bytes (ReadBytes writes only the low byte, and the set pass never writes the first colour's places before);
- the flags' word when no flag key is found (it decides whether the cells read a key);
- the cells' key when none has been read yet.

Ours has 0 in all three places.

**GZWinCustom** is 0xDC bytes: the window base with the vtable 0x00AD6AA0, and IGZWinCustom (0x678D1E84) at +0xD8 with the vtable 0x00AD6A8C. The constructor writes the interim vtable 0x00AD6CFC first.

- **QueryInterface:** gives +0xD8 for IGZWinCustom (EAX 0 when there is no out) and passes every other interface to the window's 0x0099B774.
- **Slot 3:** for messages 7, 0xA and 0xD, adds the window's slots 43 and 44 (+0xA8 and +0xAC) to the message's +4 and +8. Then it hands the message to the target at +0x48 (AL 0 without one).
- **Slot 88:** gives the surface at +0x6C its slot 21 with the window's slot 126, then its slot 35 with +0x24.

**The LEGACY reader's constructor** (the factory 0x0094AF90 jumps to it) writes the interim face vtables 0x00AD53B0 and 0x00AD53CC, then part u11's three, and zeroes the fields.

- **With a variable table at 0x00B63588:** the table is held (slot 1).
- **Without one:** the COM's slot 3 makes it (0xBA2E7954, 0x1A2BDEBB). If it did, 21 calls by address fill it, in the game's order: the words (0x0094D641, part u12) and the 20 widget classes' registrations (ours when their groups are on).

### Testing

`OPENSC4_TEST_GRID=<day>` is a test of its own: the fingerprint's `gd` part. It calls the ten by address and checks after each call that EBX, ESI, EDI and EBP were kept. The one exception is the handler's set passes with a text colour (the bug above). There are five stages, each logged as `autotest: grid: <stage> <hash>`:

- **reg:** the registration with part u13's variable table of our own. Hashes the count of names and both maps' entries.
- **text:** the text helper on ten strings of the game's: too few parts, the two forms of the out string, styles after both, more than 11 parts, spaces and bad numbers. Hashes every out and the string after.
- **read:** the handler, set up as follows:
  - **Fakes:** part u13's windows and an interface of our own. Every slot it calls hashes its number and what it gets; its window's slot 108 hashes the low bytes of the three bytes.
  - **Values and services:** part u13's values with a slot 75, part u15's held object (images for even instances, texts for odd), and the game's font and colour services.
  - **Maps:** the window's slot 114 keeps the three maps' values until the call is over, because the handler goes on using the maps. Then the test hashes each map, read by the game's tree code (the outer and inner trees, every record or span). Last, it destroys the value, which releases the holder and the map through the game's STLport code.
  - **Inputs:** set vectors with every setting, every flag key on and off, cells of every kind (a key, an image and none, a text and none, literals, styles, a short one, a number, a repeat), and columns and rows good and bad. One vector's flags' word has 0x40000. Also raw vectors and empty vectors, with the byte 1, 0xFFFFFF01, 0 and 0x100. The argument slots are hashed after each call.
- **custom:** GZWinCustom on windows of the game's:
  - the constructor;
  - QueryInterface: IGZWinCustom with and without an out, IGZWin, and another interface;
  - slot 3 with five messages, without a target and with one of ours (with the fields behind slots 43 and 44 set);
  - slot 88 with a surface of ours;
  - the destructor, and the deleting destructor with 0 and with 1.
- **ctor:** the constructor with a table of our own, which it holds, and with none. With none, the game's COM makes one and the registrations fill it; part u11's reader destructor then releases it, which forgets it.

The inputs avoid the places where the game reads its stack:

- the colours are hashed by their low bytes only;
- every vector with cells has a flag word before them;
- the vector whose flags' word has 0x40000 holds only cells that need no key.

With the groups on, each of the 10 logs its first call in a test run. `gd` is AE2D225E (muted) on both sides. The stages: reg 56301CDF, text EDE19A5E, read 7815A398, custom 0EFBC55C, ctor AE2D225E.

**Deliberate slips.** Three slips in ours, one build each, each changed its stage:

| Slip | Stage changed |
|---|---|
| 0x1613's mask set to 0x41 | read |
| slot 43's value added to the message's +8 | custom |
| the style's part, after a printed string, read one part too early | text |

At first the first two slips changed nothing. That showed two gaps in the test: only five flag keys were found, and a new window's slots 43 and 44 are 0. The test was widened and the slips checked again.

**Audit.** The ten were checked against the disassembly by the author and by an independent audit:

- the table's rows and both map inserts;
- every call, its arguments and order, the vtable slots, the registers the helpers read, and the places locals share;
- the loops (the counts read once, the vector's start read for every pair);
- the strings' order of making and destroying;
- the stack argument counts (the array destructor is stdcall, the colour slot `ret 0xC`);
- EAX on every path;
- GZWinCustom's slots, and the constructor's 21 calls in order.

The audit found no differences and no wrong comments. The test found the register bug above, which the audit had missed. As in parts u14 to u16a, one edge cannot happen: if a QueryInterface answered yes with no interface, the game would answer the last call's EAX where ours answers the QueryInterface's.

### Parity

Batch `gzu16b-r1`, the final build, run 2026-10-24:

- muted on both sides, two jobs, in its own test folder `p4env-gzu16b`;
- the base list (with `determinism.dispatch_info`) on both sides;
- on side B, every one of these groups `=ours`: `framework.uiscript.*` (with `grid`, `gridread`, `custom` and `legacyctor`), `framework.mp3.*`, `framework.soundmix.*`, `framework.soundsvc.*`, `framework.sound.*`, `framework.xmlparse.*` and `framework.propio.*`.

| Leg | Saves | Result |
|---|---|---|
| Making Money d0000, 4 days | 1 | same |
| Rush Hour t0000, 7 days | 1 | same |
| 12 saves, 7 days | 12 | 12 same |
| 12 saves, 7 days, `OPENSC4_TEST_GRID=2` | 12 | 12 same (Making Money d0000 on the second pair: side A gave two outcomes, B one, one shared); `gd` C31166DE in every run |
| Making Money d0000, 4 days, `OPENSC4_TEST_GRID=2` | 1 | same; `gd` C31166DE |

All five legs were the same, all but one run on the first pair. `gd` was C31166DE in all 28 test runs (14 pairs, the repeated one included). The single test runs above, a day earlier in their own folder, give AE2D225E on both sides. The difference is the game state the test reads (the fresh windows' fields and the services); the hash is constant across the saves.

**Reached:** in every plain (muted) leg, side B logs 7 of the 10 first calls (14 of 14 runs):

- the grid's registration and the LEGACY reader's constructor, which run whenever the first reader makes the table;
- GZWinCustom's constructor, both destructors, QueryInterface and message slot (every scripted window of a class it does not name).

No script the runs load makes a grid, and no GZWinCustom is drawn. So the grid's handler, its text helper and GZWinCustom's slot 88 are reached only by the test.

**What is left:**

- **u17** (0x0095BC00 to 0x0095FFFF): the writers, with the three helpers they call, 0x0095BAD7, 0x0095BB29 and 0x0095BB82;
- **u12's property handlers** (slice 4).

The grid's STLport map code stays the game's.

## Part u17: the UI script writers

0x0095BC00 to 0x0095FFFF holds the UI script writers: 29 functions, 17.7 KB. They write a window back out as a LEGACY UI script, the reverse of the readers of parts u13 to u16b. The game never saves a UI script, so none of them ran in the coverage runs. The part also takes the three property helpers just below the range, 0x0095BAD7, 0x0095BB29 and 0x0095BB82, which only writers call. Ours: all 32, in `src/core/gzui_writers.cpp`, four groups, off by default, installed at Attach:

| Group | Detours | What |
|---|---:|---|
| `framework.uiscript.writelines` | 17 | the 14 printers 0x0095BC92 to 0x0095C130 (one per form of a line), the font printer 0x0095BC21, the flag table 0x0095C185 and a key compare 0x0095BC74 |
| `framework.uiscript.writeprops` | 3 | the property helpers 0x0095BAD7, 0x0095BB29 and 0x0095BB82 |
| `framework.uiscript.writers` | 6 | the writers of GZWinFlatRect 0x0095C211, Text 0x0095C55A, TextEdit 0x0095CC7B, ListBox 0x0095D439, LineInput 0x0095DB1A and Combo 0x0095E1E2 |
| `framework.uiscript.writers2` | 6 | the writers of GZWinBtn 0x0095E6F0, BMP 0x0095F2CA, Slider 0x0095F4EC, Scrollbar 0x0095F663 and OptGrp 0x0095F9B0, and OptGrp's option callback 0x0095F87B |

Still the game's: the dispatcher 0x00965F73 above the block, which writes `<LEGACY `, the lines of 0x00962FC2 (which calls the printers too), the class's writer and `>`; and the other classes' writers above 0x00960000 (TextTicker, Folders, Spinner, Outline, Gen, FileBrowser, TreeView and Grid), which call our printers and helpers by address when their groups are on. Nothing in the range is STLport code or an adjustor. 46 address entries (`gzui.toml`, "Part u17"): the 32 functions, the printers' 13 formats and the alphas' scale 255.0f (0x00A9D8B0).

**The printers** (cdecl: the out string, the name string, the values) each make a string on the stack, print `name=value ` into it (cRZBaseString::Sprintf), append it to the out string's body (SC4String::AppendStd on both +4s) and destroy it inline:

| Printer | Line |
|---|---|
| 0x0095BC92, 0x0095BCED, 0x0095BD57 | one, two or four colours, `%s=(%u,%u,%u) `, each byte argument's low byte |
| 0x0095BDDF, 0x0095BE2E | `%s=%d `, `%s=0x%08x ` |
| 0x0095BE7D | a resource key, `%s={%08x,%08x} ` |
| 0x0095BECF, 0x0095BF21 | `%s=(%d,%d) `, `%s=(%d,%d,%d,%d) ` |
| 0x0095BF79, 0x0095BFCB | `%s=%s ` with a string, or `yes`/`no` from a byte |
| 0x0095C028 | `%s="%s" ` |
| 0x0095C0A1 | an option (the name in EDI, the text in ESI): `%s="%u:%s:%u:{%08x,%08x}:(%d,%d,%d,%d)" ` when the key's group and instance are both not 0, else `%s="%u:%s:%u::" ` |
| 0x0095C130 | an option's move or size, `%s="%u:(%d,%d)" ` |
| 0x0095C07A | a word alone (the string in EAX, the out string in ECX): its body and a space |

The font printer 0x0095BC21 (the string in ESI) prints a font word into a string: the face's name (the font service's slot 37, the face's slot 5) when the service knows it, nothing for the default font 0x68963C4C (AL 0), else `0x%08x`. The flag table 0x0095C185 (the table in EAX) walks 12-byte rows (mask, name, byte): each row whose flag differs from the default, or whose byte is set, as `yes` or `no`. 0x0095BC74 compares two keys by their +4 and +8 (`ret 4`); the grid's writer and 0x009A9892 call it.

**The property helpers** read a window's script property by its slot 113 (the window in ECX):

- 0x0095BAD7 (a key and a place): a value of type 0xC gives its 12 bytes (its slot 57). Without a window: AL 0.
- 0x0095BB29 (two places and an index): the property 0xDAA3B860 + the index; a value whose kind has 0x8000 gives its two words (slot 103).
- 0x0095BB82 (an index; the string in EDI): the property 0xFAA3BE85 + the index. A number is a font: the face's name, or `%d` when the font service has no face. A text goes into the string by the value's slot 55.

Each answers AL 1 when it wrote, and releases the value.

**The writers** ask their interface for each setting and print it. Most also ask the interface's window for properties: a font (0x0095BB82 with index 0), an image key, `autofit` (the property 0xDACF8F9C, whose value's byte is behind slot 63). Colours go through the colour service's slot 36 into three bytes. Each writer's lines, in order:

| Writer | Interface, in | Lines |
|---|---|---|
| FlatRect 0x0095C211 | 0xC2AFA76F in EAX, out in EBX | colorleft, colortop, colorright, colorbottom (slot 11 with 0 to 3); style words for the style's seven bits (slot 13): left, top, right, bottom, raised, sunken, nofill |
| Text 0x0095C55A | 0x212CDC1F, cdecl (face, out) | font (slot 20); align (slot 22: nine words, else hex); notify, wrapped, opaque (slot 6 with 1, 2, 4); forecolor, bkgcolor (slot 12 with 0 and 1); gutters, textoffsets (slots 24 and 26, signed bytes); autofit |
| TextEdit 0x0095CC7B | 0x231A1C57 in EBX, cdecl (out) | the flags (slot 80) against 0x11FF by the table 0x00AD6D10: editable to allowundo and singleline always, the other five when they differ; font (11); initvalue (46, quoted); six font colours (slot 14 with 0, 2, 4, 1, 3, 5); caretcolor (19), highlightcolor (22), outlinecolor (38, when its byte is set); maxtext (32), gutters (39, 41); overwrite (`no` when slot 65 answers yes); insertindex (67), insertpos (69), caretperiod (73), maxundo (82); hscrollimage and vscrollimage (the window's 12 bytes under 0x8B4BFEFC and 0x8B4BFEFD); hscrollimageRect and vscrollimageRect (slots 86 and 85); autofit |
| ListBox 0x0095D439 | 0x4132242B, cdecl (face), out in EBX | vscrollbar, hscrollbar (36); font; three font colours (40); highlightcolor (43); outlinecolor (56, two colours); sunken (54), outline (52); scrollbargutters (58), gutters (60); sort (15), drop (18), initselection (12); every element (slot 30's count, slot 49) as a quoted listelement; autofit |
| LineInput 0x0095DB1A | 0x21335C59, cdecl (face), out in EBX | font; three font colours (7); colorLT, colorRB, colorLTinside, colorRBinside (15); fillcolorhighlite (18); gutters (34, four signed bytes, always); filled (10), editable (27); style (12: normal, outline3d, outline3dEx); fillalphahilite (slot 20's float times 255.0, truncated by the game's _ftol); caretpos (26), charlimit (30); selection (32: start, start + length) |
| Combo 0x0095E1E2 | 0x412CE496, cdecl (face), out in EBX | font; three font colours (13); highlightcolor (17); editable (20); outlinecolor (23); initselection (30); combodownarrowimage (the window's pair by 0x0095BB29); combodownarrowrect (34), combodowncolor (36), buttongutter (38), gutters (40); every item (slot 31 from 0 while it answers) as a quoted listelement |
| Btn 0x0095E6F0 | 0x8810, cdecl (face, out) | image (the window's 12 bytes under 0x2B343BEC); font (21); six font colours (slot 25 with 0, 4, 2, 1, 5, 3); ten flags (slot 4): toggle and triggerondown `on`/`off`, showcaption, autosize, wrapcaption, shiftcaption and tips `yes`/`no`, fill, tipsdelay and tipstimeout `no` when set; style (11: label, standard, toggle, radiocheck); gutters (31, always); tiptext (36); tipres (0x2B340FFE, when not 0); tipoffsets (41), tipflag (39), align (28: center, left, right); btnclicksnd and btnupsnd (the window's pairs 0x0A5BE39D and 0x4A5BE2F8) |
| BMP 0x0095F2CA | 0xC12CEA13, cdecl (face, out) | image (0xAB428C5D); imagerect (slot 9, when slot 10 answers for 0x10); notify, transparentbkg, edgeimage (slot 10 with 1, 2, 8); alpha (slot 7: with its byte set, the float times 255.0, truncated, its low byte) |
| Slider 0x0095F4EC | 0x21325207 in EDI, out in EBX | minmaxvalue (slots 7 and 9); direction (11: 2 vertical, 1 horizontal); image (0x6B4BCBD4); initvalue (4) |
| Scrollbar 0x0095F663 | 0x61325A2D in EDI, out in EBX | minmaxvalue (8); direction (10); pagesize (12), linesize (14); linepagecount (16, always); image (0x6B4BCBD4); initvalue (6) |
| OptGrp 0x0095F9B0 | 0xA1336CC0, cdecl (face, out) | maincaption, itemcaption, drawfill, outline (slot 4 with 1, 2, 4, 8); gutters (7, always); labelfont and buttonfont (the window's fonts 0 and 1); captiontextresid (the window's pair 0x4A4AF4AC); backimage (0x0095BB29); five font colours (11); outlinecolor (22, four colours); initselection (13); autofilltype and autofillsize (29); autofit; then each option button |

The writers make their strings on the stack and destroy them inline, in the game's order. For a word line the value is made first and the name destroyed first. The writers' EAX is not the game's: their only caller, the dispatcher, reads none.

**OptGrp's options.** OptGrp hands the window's slot 32 (the base window's 0x0099D708) IGZWinBtn, the callback 0x0095F87B and the out string. Slot 32 calls the callback (cdecl: the window, the child's id, the child, the word) for each child, and counts its AL 0 answers. The callback prints three lines for each button:

- `option`: its caption (slot 50, SC4String::FromIGZString), its window's pair 0 as the image key, and its image rectangle (slot 16);
- `optionmoveto` and `optionsetsize`: its window's area (slot 47).

It answers AL 1.

**Odd but the game's:** Btn writes `fill`, `tipsdelay` and `tipstimeout` as `no` when their flags are set, and uses `toggle` both as a flag's name and a style's word; TextEdit writes `overwrite=no` when slot 65 answers yes. Several writers print a slot's places without testing its answer (LineInput's gutters, style and alpha; Btn's gutters; Scrollbar's counts; OptGrp's gutters). No bugs were found.

**Where the game reads its stack.** Many getters write into places the writer never set first. If a getter answers yes without writing, the game prints what its stack held there. The cases:

- the font words, the alignments and the colour words, and the colour bytes when the colour service writes none;
- the gutters', offsets' and scroll bars' bytes;
- the rectangles: BMP's lies on the words of the destroyed `image` name;
- LineInput's and Scrollbar's always-printed words;
- OptGrp's outline colours and its autofill words;
- the option callback's rectangle and area.

Ours has 0 there. Places the game shares between calls are shared in ours too, so a getter that writes nothing prints the last value, as in the game. The one place left uninitialised is 0x0095BAD7's value pointer, which the game does not zero either. So both crash alike if slot 113 answers yes and writes nothing.

### Testing

`OPENSC4_TEST_WRITERS=<day>` is a test of its own (the fingerprint's `wt` part). It calls all 32 by address and checks after each call that EBX, ESI, EDI and EBP were kept. After each call it hashes the out string's text. There are 13 stages, each logged as `autotest: writers: <stage> <hash>`:

- **lines:** each printer on strings of the game's: colours with their upper bytes set, numbers at the edges, empty strings, options with and without a key, a flag table with rows that differ and rows always written (a byte with upper bits) and an empty table, the font printer on the known font words, the default and an unknown one, the key compare.
- **props:** the three helpers on a window of our own whose slot 113 gives script values of our own (bytes, numbers, fonts, texts, pairs, the wrong kinds, missing keys) and on no window.
- **flatrect, text, textedit, listbox, lineinput, combo, btn, bmp, slider, scrollbar, optgrp:** each writer on interfaces of our own. Every slot it calls hashes its number and arguments, and answers by a case number so that every branch runs: getters answering no, every switch value and its default, every flag, known, default and unknown fonts, texts and none, elements failing, autofit on, off and missing. The interfaces' windows are ours (slot 113, plus OptGrp's slots 32 and 47). The font and colour services are the game's. OptGrp's option callback runs through a fake slot 32 and directly by address, with a null button among the cases.

The inputs keep clear of the places where the game reads its stack: a getter that answers yes always writes. With the groups on, side B logs all 32 first calls in a test run. `wt` is F68A8309 (muted) on both sides. The stages: lines 2061B908, props 18877EC9, flatrect ABA387DF, text C8B69844, textedit 3299ED13, listbox DC69E7B5, lineinput 83F33DC5, combo FA0B055B, btn C20F759D, bmp EFFC34BB, slider 3C53646D, scrollbar ADE0D7AA, optgrp F68A8309.

**Deliberate slips.** Four slips in ours, one build each. Each changed its own stage, and none before it:

| Slip | Stage changed first |
|---|---|
| the flag table's byte tested as a dword | lines |
| ListBox's first scrollbar gutter not sign-extended | listbox |
| Btn's `fill` not inverted | btn |
| an option's height from the wrong side | optgrp |

**Audit.** The author and three independent audits checked the 32 against the disassembly, reading every instruction:

- the register inputs and the thunks that hand them over, and the stack arguments and who pops them;
- every call, its arguments and order, the vtable slots, and the registers set for each register-argument callee;
- every literal, checked against the exe, the align jump table and TextEdit's flag table;
- byte against dword tests, signed and unsigned compares, sign extension, the loops;
- the order of making and destroying strings, and the places shared between calls;
- AL and EAX where a caller reads them.

All callers of the register-argument helpers in the whole exe were checked: none relies on ECX or EDX after the call. Some rely on EDI, ESI and EBX, which ours keep. Nothing jumps into the first five bytes of any of the 32; the only references are their callers' calls and OptGrp's push of the callback.

The audits found no differences in what the functions do. They found comments to fix, now fixed: TextEdit's `singleline` written always, not when it differs; the printers' count; the empty flag table's EAX; which places were shared and which only unset; the option callback's EAX; BMP's and OptGrp's lists of stack places. They also found duplicate helpers in OptGrp's writer, now folded into the shared ones.

### Parity

Batches `gzu17-r1` (before the audits' comment fixes and the folding of OptGrp's helpers) and `gzu17-r2` (the final build), run 2026-10-24:

- muted on both sides, two jobs, in their own test folder `p4env-gzu17`;
- the base list (with `determinism.dispatch_info`) on both sides;
- on side B, every one of these groups `=ours`: `framework.uiscript.*` (with `writelines`, `writeprops`, `writers` and `writers2`), `framework.mp3.*`, `framework.soundmix.*`, `framework.soundsvc.*`, `framework.sound.*`, `framework.xmlparse.*` and `framework.propio.*`.

| Leg | Saves | r2 (final) | r1 |
|---|---|---|---|
| Making Money d0000, 4 days | 1 | same | same |
| Rush Hour t0000, 7 days | 1 | same | same |
| 12 saves, 7 days | 12 | 12 same | 10 same, 2 partly |
| 12 saves, 7 days, `OPENSC4_TEST_WRITERS=2` | 12 | 12 same; `wt` F68A8309 in every run | 11 same, 1 partly; `wt` F68A8309 |
| Making Money d0000, 4 days, `OPENSC4_TEST_WRITERS=2` | 1 | same; `wt` F68A8309 | same |

In r2 three saves took a second or third pair (Making Money d0000 and Rush Hour d0090 in the plain leg, Rush Hour d0000 in the test leg): in each, side A, the game's, gave more than one outcome and B one, shared. In r1 three saves were partly the same (Big City d0000 and Making Money d1095 in the plain leg, Big City d1095 in the test leg): B gave two outcomes, one of them A's. r1 ran while three audits, four slip builds and their test runs shared the machine. In plain legs none of the 32 runs, so these are the earlier batches' pattern of a side differing from itself (parts u3, u10 and u14). `wt` was F68A8309 in all 28 test runs of r2 and in every test run of r1.

**Reached:** in plain (muted) legs, none of the 32 runs (0 of 14 runs log a first call): the game never writes a UI script. They are reached only by the test.

**What is left:** u12's property handlers (slice 4, done: see part u12, slice 4). The writers above 0x00960000 (TextTicker, Folders, Spinner, Outline, Gen, FileBrowser, TreeView and Grid, with the dispatcher 0x00965F73) are outside the block.
