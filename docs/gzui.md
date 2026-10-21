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
| 0x00944462 to 0x009456EB | 67 | 40 | 0 | An XML parser: a director (0x0094448E) that registers the parser 0xBA2E7953 (0x5C bytes, constructor 0x00945404, vtable 0x00AD4718, interface 0x5A2C1570) and its handler 0xBA2E7954 (0x14 bytes, vtable 0x00AD4764, interface 0x1A2BDEBB); an element stack, an attribute vector, the text tokenizer (`\t\n\r <>="`) |
| 0x009456EC to 0x00945FFF | 58 | 25 | 5 | A second director (0x00945636, vtables 0x00AD47C0 and 0x00AD4790; its getter 0x00945785 is in audio.toml); a 64 by 64 table of pointers (constructor 0x0094585F, vtable 0x00AD4818, whose QueryInterface is the base one at 0x009457C6 that dozens of classes jump to); a class with three vtables (constructor 0x009458D6); a 43-slot class (constructor 0x00945DCC; `0123456789abcdefABCDEF`, `version`, `NONE`) |
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
| u4 | 0x00944462 to 0x009456EB | 67 | 40 | The XML parser and its director (two pieces) |
| u5 | 0x009456EC to 0x00945FFF | 58 | 25 | The second director, the table, the two classes after it (two pieces) |
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
