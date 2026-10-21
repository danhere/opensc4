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
| 0x00943917 to 0x00944461 | 20 | 8 | 0 | A shuffle bag of uint32s (vtable 0x00AD4648, interface 0xDB1ACFB2, 0x18 bytes: two lists at +8 and +0xC, a threshold at +0x10; the subclass 0x0094048B adds a random generator at +0x18; a count of live bags at 0x00B633B8): slot 8 draws a random entry from the first list and moves it to the second, refilling when the first runs low. Plus two slots each of the classes 0x00AD2804 and 0x00AD2830 (never ran) |
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
| u2 | 0x00940000 to 0x00941FFF, less u1 | 83 | 16 | The binary reader and writer, the factory, the XML reader-writer object, the Track subclasses with their TKDT text form, the window 0xA3CD96CF and its 37 thunks |
| u3 | 0x00942000 to 0x00944461 | 44 | 9 | The XML reader and writer, the factory's create, the container, the shuffle bag |
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
