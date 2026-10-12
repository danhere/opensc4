# The occupant filters

Small objects that the occupant manager's queries take to say which occupants to return. The query asks the filter about each occupant it finds (slot 3), about an occupant type (slot 4) or about a property holder (slot 5). This page covers the filter classes at 0x005E95B0 to 0x005EA610 and the lot's own filter (0x006BFB80, 0x006BFC10). Nine of the classes are registered GZCOM classes, plus one group filter that has no class ID. The game's own code never creates a filter by class ID: it builds them inline, and the factories exist for plugins. Build 641 addresses throughout (data/addresses/occfilters.toml).

## Pieces

One group, installed at PostAppInit, off by default: `simulators.occfilters.part1` (src/core/occfilters_1.cpp). It hooks 50 functions, counted from the table `kDetours`. The test is in src/core/occfilters_autotest.cpp.

Every filter object is 0xC bytes of base plus its own fields. Offset +0 is the filter vtable, +4 the cIGZSerializable vtable, +8 the reference count.

| Class | Class ID | Vtables (+0 / +4) | Object | Its test | State |
| --- | --- | --- | --- | --- | --- |
| Base | 0xA9C74DC9 | 0x00AA42C8 / 0x00AA42B0 | 0xC | none (the base) | ours |
| Default | 0xC9C75EAE | 0x00A87FC0 / 0x00A87FA8 | 0xC | not excluded by the interface, group or property checks | ours |
| Type | 0xE9C75ED1 | 0x00A8F79C / 0x00A8F784 | 0x10 | occupant type equals the one at +0xC | ours |
| Types | 0xE9C75EE8 | 0x00A8EDBC / 0x00A8EDA4 | 0x18 | type is in the vector at +0xC; an empty vector passes all | ours |
| Types, hashed | 0xAA25CB90 | 0x00A8EE38 / 0x00A8EE20 | | type is in the hash set at +0xC | factory 0x005EB970 and test are the game's; GetGZCLSID, Write and Read ours |
| Realistic | 0x8A7A1C2C | 0x00A88084 / 0x00A8806C | 0x10 | the default test, then three flags at +0xC, +0xD, +0xE | ours |
| Network | 0xEAC3A3D2 | 0x00AA42FC / 0x00AA42E4 | 0x10 | network occupants whose bits meet the mask at +0xC (0x1FFF) | ours |
| View level (kind) | 0xAAC3C01D | 0x00A8FAD4 / 0x00A8FABC | 0x10 | occupant kind equals the one at +0xC (1) | ours |
| String specification | 0x0BCEB6C2 | 0x00AA442C / not ours | 0x48 | types, groups and kinds in three vectors | factory 0x005EBBD0 the game's; two small functions ours |
| Group (no class ID) | none | 0x00AA4330 / 0x00AA4318 | 0x10 | belongs to any group in the vector at +0xC | ours |
| Lot's filter | none | | 0x38 | inside the lot's box, and both embedded filters | ours |

## The base and its interface

Slots of the filter vtable (in 44 or 45 vtables across the game):

| Slot | Function |
| --- | --- |
| 0 | QueryInterface 0x005E95D0 |
| 1 | AddRef 0x004BDF00 |
| 2 | Release 0x007CB920 |
| 3 | IsOccupantIncluded |
| 4 | IsOccupantTypeIncluded |
| 5 | IsPropertyHolderIncluded |
| 6 | the deleting destructor |

QueryInterface takes the IIDs 0x00000001 and 0x46A80026 (the object) and 0xE4FDA3D4 (the serializable part at +4). It adds a reference and answers true. Any other IID is false. The serializable vtable is three thunks, Write (3), Read (4) and GetGZCLSID (5); Write and Read get the part at object +4.

Defaults of the base: IsOccupantIncluded and IsOccupantTypeIncluded say yes unless a class overrides them. The base writes and reads a version word, 3, as 16 bits. Classes write more after the version; the sections below give each one.

The constructor is 0x005EA080 (the address file calls it `SC4::OccupantFilterBase`). The base destroy function 0x005E95C0 resets the two vtables to the base's.

## The filters

**Default** (slot 3 0x005E9630, slot 5 0x005E96E0). Slot 3 is false if the occupant answers the interface 0x4A10AFC2 or 0x8A139184 (the underground connection), or belongs to group 0x4217, 0x4208 or 0x421D. Otherwise it asks slot 5 with the occupant's property holder. Slot 5 is false if the holder has property 0x49512A09 or 0x8A15F1BD, or its bool property 0xCAA45670 is set. Written as two version words (3, 3).

**Type** (+0xC the type). Slot 4 is `type == +0xC`. Slot 3 is the base's, so it says yes. Written as two version words and the type.

**Types** (+0xC a vector of uint32). Slot 4 is true for an empty vector or a listed type. Written as two version words, a count and the entries; Read resizes the vector.

**Types, hashed** (+0xC a hash set). Written as two version words, the count and the keys in the set's iteration order: the nodes of a bucket, then the next bucket with a node. Read inserts each key.

**Realistic.** Starts as the default test. Then a group 0x5001 occupant is removed unless the flag at +0xD is set. A lot texture (type 0x497F6D9D) is removed if its lot is in state 1, unless the flag at +0xC is set. A prop (type 0xA823821E) is removed if it answers the interface 0x4B44FBE2 or 0x2B3B7D86, unless the flag at +0xE is set. Slot 5 is the default's. Written as version 4, the three flags, then the default's two version words. Read of version 3 copies the second flag into the third.

**Network.** Asks the occupant for the interface 0xA821EF94 and tests `mask & bits`, where bits come from its slot 0x30. A non-network occupant is false. Written as version 1 and the mask.

**View level (kind).** An occupant's kind is 1, 2 or 3. It is 3 for a pipe (0x49C05B9F), a subway (0xEA0A5ACF) or a holder with property 0x49512A09; 2 for the interfaces 0xCA16374F or 0xCA15ED6B, the three groups of the default filter, or a holder with property 0x8A15F1BD; kind 1 may be raised by the bool properties 0xCAA45670 to 0xCAA45672. The function 0x005E9CA0 takes the occupant in ESI, so its hook is a naked thunk. Written as version 1 and the kind as a byte.

**Group** (+0xC a vector). Slot 3 is false for a null occupant, otherwise true if the occupant belongs to any listed group (occupant slot 0x58). The constructor 0x005EA280 takes one group; slot 7, AddGroup 0x005EA2F0, adds one (0 is ignored); 0x005EA440 copies a vector. Nothing here has a class ID or a factory, and Write and Read are the base's.

**String specification, the two small functions.** 0x005E9EE0 asks the occupant manager (slot 0x90) for the occupants in the filter's own box with the filter. 0x005E9F20 is slot 3: an empty vector passes, otherwise the occupant's type, a group and its kind must be found in their vectors.

**The lot's filter** (0x38 bytes). The box (min x +0xC, min z +0x10, max x +0x14, max z +0x18), then an embedded default filter at +0x1C and an embedded realistic filter at +0x28. Slot 3 0x006BFB80 reads the occupant's position and is true if x and z are inside the box (inclusive; a NaN is out) and both embedded filters say yes. Slot 6 0x006BFC10 runs the three base destroys and frees. The lot's `GetLotOccupants` makes it.

## How each is made in play

The game does not create these by class ID. It builds them in its own code, usually on the stack or inline, sets the fields and passes them to a query. The lot filter comes from `GetLotOccupants`. The factories (0x005EA130, 0x005EA160, 0x005EA190, 0x005EA1C0, 0x005EA200, 0x005EA240, 0x005EA330) set the vtables, a count of 0 and the class's default fields: type 0, mask 0x1FFF, kind 1, flags clear, empty vector. They are for plugins.

## Used all over the game

The base's slots sit in 44 or 45 vtables, and its constructor has 78 direct callers. All of it is hooked as entry detours. Destroy (0x005E95C0, 16 direct callers) goes through a naked thunk that keeps EAX, ECX and EDX, because four callers jump into it as their last act (0x00466EDE, 0x00491226, 0x005204C0, 0x007A04DE) and so return whatever EAX held. No other caller keeps ECX or EDX across a hooked function. Side-by-side runs showed no difference with the hooks on.

## Left the game's

| What | Where | Why |
| --- | --- | --- |
| The string specification filter | from 0x005EA610, about 5.9 KB | never entered in any stored coverage run. Two small functions that start inside the range are ours (0x005E9EE0, 0x005E9F20) |
| The lot's pool functions | 0x006BF3F0, 0x006BFFF0 | the lot's memory pool, not filter code |
| The forwarder | 0x005EA480 | a thunk to the vector copy 0x00462C10 |
| Shared helpers | | vector and hash set code, stream helpers, allocator |

## Oddities of the original, kept

- QueryInterface with the serializable IID and a null object stores null, and then the AddRef through the null object crashes. Ours does the same.
- The 16-bit version words in several Read functions are stack slots the game never sets. When the stream gives nothing they hold the low word of a pointer (the stream's or the part's), or 0. Ours reproduces which pointer, because the result of Read depends on it.
- The group filter's constructor tests `end == 0`, not `end == capacity`, so it always grows a new vector. The other branch cannot be reached.
- The realistic filter's vtables carry the address names `SC4CityFiles_PictureFilterVtable` and `..4`, an old mistake in the address database. They are this class's.
- The occupant manager's address entry is still called `SC4_NetworkService`.
- The game stores 0x00A80784 in +4 first in each constructor and overwrites it at once. Ours leaves that out.

## How it is tested

`OPENSC4_TEST_OCCFILTERS=<day>` (token ` occfilters`). It is read-only: the city is not changed. It takes a sample of up to 300 occupants: the first occupant of eight types in every eighth cell (props, flora, lot textures, network pieces, foundations, construction, walls, fire) plus the lots' buildings. Then it makes 26 configurations:

| Class | Configurations |
| --- | --- |
| Base, default | 1 each |
| Type | 3 (the three most frequent types of the sample) |
| Types | 2 (empty, two types) |
| Network | 3 (masks 0x1F4F, 0x1FFF, 0x0003) |
| View level | 3 (kinds 1, 2, 3) |
| Realistic | 5 (flags clear; lot; group; prop; all three) |
| Group | 5 (three fixed sets, then up to two from groups that occur in the sample) |
| Lot's filter | 3 (the first three lots) |

For each, all three tests run over the sample and the answers are hashed. Then QueryInterface is called on every filter (the hashed one too) with IDs 1, 0x46A80026, 0xE4FDA3D4 and one it does not take, with the change of the reference count, and GetGZCLSID of every serializable part. Each filter is made with count 0, given one reference and released once at the end.

### What the test does not tell

- Write and Read are not called. There is no stand-in stream, and nothing in play calls them.
- On the tutorial cities the realistic filter's flags change nothing. Its five configurations answer like the default (290 of 300 on Big City d0000).
- The property-holder test is true for every occupant in every configuration.
- Kind 2 and three of the five group sets match nothing.
- The string filter's two functions never ran.

## Trial results

2026-10-12, single runs, 7 days, three saves: Big City d0000, Making Money d0090 and plopped p0000.

- Side by side without the test (no switches; a real bulldoze; the occupant manager query test with the demolition test): 27 of 27 comparisons were identical between the original's two runs and ours.
- The test alone and after a real bulldoze: 17 of 18 agree. The exception is the known `terrain` variation after a bulldoze on the plopped save, where ours differed from itself too.
- Unchanged-city check: identical on all three saves.

True answers per configuration on Big City d0000, out of 300:

| Configuration | True |
| --- | --- |
| Base | 300 / 300 (occupant / type test) |
| Default | 290 / 300 |
| Type filters | 84, 60, 58 (both tests) |
| Types, empty | 300 / 300 |
| Types, two types | 144 / 144 |
| Network masks 0x1F4F, 0x1FFF, 0x0003 | 11, 21, 5 |
| View level kinds 1, 2, 3 | 290, 0, 10 |
| Realistic (all five) | 290 |
| Groups (five sets) | 0, 0, 0, 1, 17 |
| Lots (three) | 0, 3, 0 |

## Reached

Existing tests already run the constructors, destroy, the default, type, types, realistic, kind, group and lot tests by themselves. The new test adds the class ID stubs, QueryInterface, the seven factories, the group filter's constructor and AddGroup, and the network test. Never reached: every Write and Read, and the string filter's two functions.

## Parity

Batch `of9` (2026-10-12): all groups pushed so far plus `simulators.occfilters.part1` on side B (118 groups), 7 game days per save, verdicts by outcome sets as in [parity.md](parity.md).

| Leg | Saves | Test switches | Result |
|---|---|---|---|
| plain | 12 | none | 12 same on the first pair |
| of | 12 | `OPENSC4_TEST_OCCFILTERS=3` | 12 same |
| ofb | 12 | `OPENSC4_TEST_BULLDOZE=3 OPENSC4_TEST_OCCFILTERS=5` | 12 same |
| plop | 3 (`saves-plopped`) | `OPENSC4_TEST_OCCFILTERS=3` | 3 same |

All 39 first pairs are the same, so no retries ran and no run failed. A first pair that is the same has identical fingerprint lines on every day, the ` occfilters` token included.

This batch happened to meet none of the saves' known noise (see parity.md, "Flammability and weather from the first day"); that says nothing about the filters either way.
