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
| 0x00948E63 to 0x0094A078 | 71 | 7 | 5 | A class with three vtables (constructor 0x00949902: 0x00AD4C4C, 0x00AD4C60, 0x00AD4CF4); 7 functions ran |
| 0x0094A079 to 0x0094BFFF | 108 | 51 | 26 | The UI script tree (`_null`, `_children`, `_/children`, `_define`, `_name`, `_val`, `none`, `children` at 0x0094B73A; list node 0x0094B33A); its node classes (constructor 0x0094AF64: eleven vtables 0x00AD502C to 0x00AD5158; 0x0094A4D8; 0x0094ABC9; 0x0094C0AF) |
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
| u6, u7 | 0x00946000 to 0x00948E62 | 75 | 45 | The 65-slot class |
| u8 | 0x00948E63 to 0x0094A078 | 71 | 7 | The class 0x00949902 |
| u9, u10 | 0x0094A079 to 0x0094BFFF | 108 | 51 | The UI script tree |
| u11 | 0x0094C000 to 0x0094C8FF | 50 | 35 | The framework window's small methods |
| u12 | 0x0094C900 to 0x00951268 | 49 | 16 | The window's script properties (18.7 KB) |
| u13 | 0x00951269 to 0x00953FFF | 17 | 17 | The widget readers that run (11.8 KB) |
| u14, u15, u16 | 0x00954000 to 0x0095BBFF | 98 | 16 | The other widgets' readers and helpers (31 KB) |
| u17 | 0x0095BC00 to 0x0095FFFF | 29 | 0 | The writers (17.7 KB, never run) |

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
