# The demolition service (cSC4Demolition)

Takes things out of the city. The bulldozer, zoning and dezoning, the disasters, the region tools and the lot code all go through it: DemolishRegion removes everything in a rectangle of cells that a filter lets through, DemolishLots does the same for a list of lots, DemolishOccupant removes one occupant, and ModifyTerrainHeight levels the ground under a rectangle. A lot that is demolished is dezoned afterwards (the cells go back to unzoned, or to a fresh one-cell lot, depending on the zone type). Two functions play the demolition effects (animation and sound). Reimplemented like the other classes: one writer with every virtual call's argument count checked against the callee's own code, then the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/demolition.toml).

## The class

Class ID 0x0990C38A. Object size 0x34, made by the factory 0x00466E40 (registered at 0x00460090). Code 0x00466480 to 0x004695A0. One per city, handed out by the city's GetDemolitionUtility (slot 0x24).

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00A88004 | `cISC4Demolition` | 13: QueryInterface 0x004664B0 (0), AddRef (1), Release 0x00466950 (2), Init 0x00466EF0 (3), Shutdown 0x00466980 (4), SetDefaultOccupantFilter (5, shared no-op 0x00648F10), DemolishRegion 0x00468410 (6), DemolishLots 0x004670C0 (7), DemolishOccupant (8), ModifyTerrainHeight (9), DoDemolishOccupantEffect 0x004673B0 (10), DoDemolishGroupEffect 0x00467530 (11), deleting destructor 0x00466920 (12) |
| +0x4 | 0x00A87FF4 | `cIGZMessageTarget2` | 4: DoMessage 0x00466FF0 (slot 3, called with this + 4) |
| +0x8 | 0x00A87FDC | `cIGZSerializable` | 6: Write and Read are shared no-ops (0x0042E440, 0x006B8A70), GetGZCLSID 0x00466490 (slot 5) |

QueryInterface: 0x00000001 and 0x26DDE2EA give the object, 0x452294AA the message target (+4), 0xE4FDA3D4 the serializable (+8). The answer is referenced. Any other ID is false and leaves the out pointer alone. The service is never saved: Write and Read do nothing.

| Offset | Field |
| --- | --- |
| +0x0C | reference count |
| +0x10 | initialised (Init sets it first, Shutdown clears it last) |
| +0x14, +0x18 | the city's width and depth in cells (city slots 0x174, 0x178) |
| +0x1C | the default occupant filter (a demolition filter made by Init, referenced) |
| +0x20 | the city (global 0x00B43CEC, referenced) |
| +0x24 | the lot developer (the building development simulator, 0x00B43D68, its slot 0x90; referenced) |
| +0x28 | the occupant manager (global 0x00B43D0C, referenced; the address entry for it is still called SC4::NetworkService, an old mistake) |
| +0x2C | the terrain (0x00B43CF4, referenced) |
| +0x30 | the region of the DemolishRegion call in progress; set and cleared by that call |

Init takes the services (once), makes the filter, registers the message target for three messages and reads the tuning. Shutdown gives the references back in the order filter, terrain, lot developer, occupant manager, city, and takes the target out of the three messages.

## Interface slots

DemolishRegion (slot 6, `ret 0x2C`, eleven arguments):

| Argument | Meaning |
| --- | --- |
| demolish | false only prices and collects (the bulldoze tool's preview); true removes |
| region | a cell region: the rectangle (x1, z1, x2, z2), then a bit per cell (one row per x from x1, a bit per z from z1) |
| privilege | 0 none; 1 the player's own tool (a building that may not be removed fails the call); anything else counts as 2 in the filter |
| flags | 0x2000: a lot's other occupants only when they belong to the lot; 0x300 (either bit): the lot developer's StartDemolishLot is used only for lots whose configuration allows it; 0x1000: handed to ExplodeLot as its second argument, inverted; low byte not zero: show the effects (the calls go through the two effect slots); 0x100 is also tested by DemolishOccupant |
| clearZoned | a byte, handed to the dezoning of a demolished lot |
| filter | an occupant filter or null; kept in the default filter for the length of the call |
| cost | 64-bit price out, or null |
| demolished | a set that receives every occupant the call took, or null |
| occupant | the occupant the bulldoze tool points at; compared with each one for the effect's fourth argument |
| a, b | the effect functions' last two arguments |

It returns false when a building that may not go stood in the area (privilege 1), true otherwise.

In order: the setup (the region kept at +0x30, the view's filter from the service at 0x00B43DC4, the default filter's fields, a local filter, the set of affected occupants, the list of lots to finish, the set of lots seen); then every set cell, x outer and z inner. The lot on a cell is looked at once. When its building passes the default filter (or the lot is empty of type 6 or 1 and the view's filter takes buildings) all its occupants are tested, and when every building and prop passes, the lot is queued and its related occupants join the set; otherwise the call fails. The occupants of the cell join the set when the local filter takes them. Then every occupant of the set goes through DemolishOccupant (twice when the effects are shown: asked first, then done) and its price is added. The group effect is played and the network manager told to refresh. Every queued lot is finished: a type 6 lot adds width times depth times the cost scale (0x00B07F50) to the price, and the lot developer finishes it (StartDemolishLot, EndDemolishLot and the dezoning, or ExplodeLot). Last, the occupants DemolishOccupant listed join the set and the caller's set, the set goes to the network helper 0x004681A0, the cost is stored and everything is destroyed.

DemolishLots (slot 7, `ret 0x20`: demolish, a list of lots, privilege, flags, clearZoned, filter, cost, taken): makes the rectangle round all the lots, with a bit set for every cell of every lot that can be demolished (lot slot 39), adds 0x2000 to the flags and calls DemolishRegion. An empty list is true at once.

DemolishOccupant (slot 8, `ret 0x1C`: demolish, occupant, privilege, flags, cost, excludeNetworks, taken): one occupant, by type (power pole, building, construction, foundation, network piece, prop, flora). The price goes to `cost` (zero for an unknown type). The occupants of a removed power pole go to the list `taken`.

The network helper 0x004681A0 takes the set of occupants DemolishRegion or DemolishOccupant listed. For each one flagged for it: a power pole sends message 0xAA7A014D and is removed from the occupant manager, and a network piece joins a list that the network manager takes.

ModifyTerrainHeight (slot 9, `ret 0x28`, ten arguments of which the tenth is not read): sets the terrain of a cell rectangle to a height unless a lot with locked vertices other than the given one is nearby, or (per-cell mode) an occupant is on the cell. It uses the small terrain filter below. It returns the count of cells that differ. With apply false it only counts, but it still tells the terrain whenever cells would change.

DoDemolishOccupantEffect (slot 10, `ret 0x18`: occupant, kind, lot, aim, x, z) and DoDemolishGroupEffect (slot 11, `ret 0x10`: kind, count, and the cell rectangle): both build a key from the kind byte shifted up eight, plus 1 or 2 from the size (an occupant) or the count (a group) against the tuning, ask the effects manager for the effect with that key (lowering the key until it finds one, for an occupant) and start it. The occupant effect also plays the occupant's demolition sound (property 0x0A93036F) and, with `aim` set, gives the effect the camera's screen ray for the point x, z. The group effect puts the effect on the middle cell of the rectangle. An occupant too small for the camera gets no effect.

## The filters

The demolition filter (`cSC4DemolitionFilter`, 0x34 bytes, made by Init; constructor 0x00466700, vtable 0x00A880B8, second vtable 0x00A880A0, Test 0x00466730 at slot 3, deleting destructor 0x00466EA0): it decides what a bulldozer may take. Its second vtable answers with the realistic occupant filter's class ID (0x8A7A1C2C).

| Offset | Field |
| --- | --- |
| +0x00, +0x04 | the base (SC4::OccupantFilterBase, 0x005EA080) |
| +0x0C to +0x0E | the base's three flags |
| +0x10 | limited to a rectangle (CollectOccupants sets it for its query) |
| +0x14 to +0x20 | the rectangle in metres: x low, z low, x high, z high |
| +0x24 | ask CanRemove (occupant slot 14) with the flags at +0x28 |
| +0x28 | the removal flags (the privilege mode) |
| +0x2C | the caller's filter (referenced, released by the destructor) |
| +0x30 | the view's filter (referenced, released by the destructor) |

Test, in order: the realistic and default filters' tests (an occupant the default test takes and the realistic one refuses is out); the caller's filter; the rectangle against the occupant's position; the removal test; then by type. A foundation passes when its interface says 1. Building, construction, power pole, flora, prop and network pieces go on to the view's filter, and a network piece also to the view's level: unless the view is at level 1, a piece for which the network interface's slot 9 says yes for 0x1F4F is out. Any other type is out.

The small terrain filter (0xC bytes, vtable 0x00A88050, second vtable 0x00A88038, Test 0x00466530) is made on the stack by ModifyTerrainHeight. It takes a prop, building, power pole or network piece whose exemplar does not have the bool 0x29F013E6 set.

## Tuning and messages

Two exemplars, read by Init and again on message 0xC53D10AA (tuning changed):

| Global | Property | Default | Use |
| --- | --- | --- | --- |
| 0x00B07F40 | 0x6A8A09D2 | 0x40 | an occupant larger than this gets effect key bit 1 |
| 0x00B07F44 | 0x6A8A09D3 | 0x80 | an occupant larger than this gets bit 2 |
| 0x00B07F48 | 0x6A8A09D4 | 0x14 | a group of more than this many occupants gets bit 1 |
| 0x00B07F4C | 0x6A8A09D5 | 0x64 | a group of more than this many gets bit 2 |
| 0x00B07F50 | 0x2AB845E7 | 3 | the price of a cell of a type 6 lot |

The first four come from the exemplar 0x6534284A / 0x690F693F / 0x2A8A09CA, the cost scale from 0x6534284A / 0xE7E2C2DB / 0xE8DA7677. A property that is not there leaves its global as it was.

The message target is registered for three messages. 0xC53D10AA reads the tuning again. 0x26D31EC1 (the city has been set up) is ignored. 0xA9DED179 (a lot has been demolished) carries a lot and a byte: if the lot is initialised, the lot developer finishes its demolition, and with the byte set the lot's whole rectangle is dezoned through the zone manager's PlaceZone ([zonemgr.md](zonemgr.md)); the lot is then given back. DoMessage is always true.

The dezoning that follows a demolished lot (0x004677F0, called with its arguments in registers, hooked behind a small thunk): with the clearZoned byte set, or for the zone types 11, 12 and 15, the lot's whole rectangle is dezoned. Otherwise, only for the zone types 7 to 9 and unless the flags have 0x800, the lot is given up to the lot manager and each of its cells still of that zone type gets a new one-cell lot.

## What is ours

Two groups, both installed at PreAppInit, both off by default. Together they hook 28 functions.

| Piece | Code | Group |
| --- | --- | --- |
| The object: factory, QueryInterface, Release, the deleting destructor, Shutdown, Init, the class IDs of the service and of the two filters | src/core/demolition_1.cpp | `simulators.demolition.part1` (25 functions) |
| The tuning, DoMessage | src/core/demolition_1.cpp | part1 |
| The demolition filter (constructor, Test, destructors) and the small terrain filter's Test | src/core/demolition_1.cpp | part1 |
| DemolishLots, the dezoning of a lot, the occupants of a rectangle (0x00466B90), the related occupants of a lot (0x00467AA0), the message maker (0x00466C50), the destructor of a set of lots (0x00467A50) | src/core/demolition_1.cpp | part1 |
| The two effect slots and the demolition sound (0x00466A20) | src/core/demolition_1.cpp | part1 |
| DemolishRegion, DemolishOccupant, ModifyTerrainHeight | src/core/demolition_2.cpp | `simulators.demolition.part2` (3 functions) |
| The network helper 0x004681A0 (a plain function in part2, called only from the other two big functions) | src/core/demolition_2.cpp | part2, no hook |
| Where the two functions other code wraps can be found | src/core/demolition.h | |
| The test | src/core/demolition_autotest.cpp | `OPENSC4_TEST_DEMOLITION` |

Part1 covers 0x00466480 to 0x00467BB0 and part2 0x00467BB0 to 0x004695A0. Part2 calls part1's functions by address, as the original does. Part1 hooks 25 functions from one table; part2 installs three through `hook::Install`.

## With the bulldozer modes and quiet demolish

Two other pieces of code sit on this class. `game.bulldoze` ([bulldoze.md](bulldoze.md)) wraps DemolishRegion (0x00468410), and `game.quiet.demolish` ([quiet.md](quiet.md)) replaces DoDemolishOccupantEffect (0x004673B0). The hook layer cannot put two detours on one address. So the demolition groups install at PreAppInit, one phase before the wrappers (PostAppInit). Each wrapper then hooks our function when ours sits at the game's entry, and the game's entry otherwise. The accessors in src/core/demolition.h (`RegionIsOurs`, `RegionEntry`, `EffectIsOurs`, `EffectEntry`) give the answer. If another detour is already at an entry when ours installs, ours is skipped there and the log says so; the wrapper then sits on the game's entry.

The wrappers' behaviour does not change. The bulldoze wrapper still passes its filter when the tool's code calls and a key is held. With quiet demolish on, the effect function is replaced by the wrapper's own version: nothing of the effect function runs, ours or the game's.

## Left the game's

- Shared helpers: the rectangle union (0x004665C0), the cell region test (0x00466610), the vector length (0x00466660), the model corner constructor (0x00466E10), the list push (0x00467B50), the lot set insert (0x00467950) and its helper (0x00467720), the set erase code (0x004676A0, 0x004676E0), 0x00467A10 and the set insert 0x00467B80 with a caller in another class. Many classes use them.
- 0x00466590 (four dwords copied from four pointers, called from another class) and 0x004666C0 (the view filter's constructor, another class's).
- Thunks and the shared AddRef: 0x004666A0, 0x004666B0, 0x00466DF0, 0x00466E00 and 0x004664A0.
- Two helpers that take their arguments in registers and have no other caller are written inline and not hooked: 0x00466A90 (the occupant in ESI, a flags word in EDI; true unless a prop lacks its property) and 0x004672E0 (the occupant in ECX, an optional float out in ESI; true when the occupant is too small for an effect).
- The bulldoze tool (see "Not done").

## Oddities of the original, kept

- Init and the filter's constructor call through a null pointer when the allocator fails. The message maker 0x00466C50 and QueryInterface do the same.
- The filter's Test for a foundation asks for the foundation interface without testing the answer.
- The filter's Test accepts an occupant the default test takes only by calling both tests (the realistic test calls the default one).
- DoDemolishGroupEffect's matrix has two bytes at its start that the original never sets. Ours sets them to 0.
- The demolition sound (0x00466A20) reads its property value from an uninitialised local when the property call says yes without writing. Ours starts it at 0.
- The group effect passes the cell numbers to the terrain as floats.
- DemolishRegion keeps some flags as bytes, and the dword it hands the lot developer has stack garbage above bit 7, which the callee does not read.
- The ModifyTerrainHeight call tells the terrain of a change at its end whether apply is set or not.

## How it is tested

Three existing switches drive the service for real:

- `OPENSC4_TEST_BULLDOZE=<day>` bulldozes the middle half of the city through DemolishRegion with the tool's arguments. Variants: `OPENSC4_TEST_BULLDOZE_MODE`, `_DIAGONAL`, `_WIDE` ([bulldoze.md](bulldoze.md)).
- `OPENSC4_TEST_ZONING=<day>` and `OPENSC4_TEST_DEZONING=<day>` zone and dezone land, so that lots are demolished through the zone manager ([zone.md](zone.md), [zonemgr.md](zonemgr.md)).
- `OPENSC4_TEST_QUIET` with quiet demolish on skips the animations of a bulldoze ([quiet.md](quiet.md)).

`OPENSC4_TEST_DEMOLITION=<day>` (src/core/demolition_autotest.cpp) calls the game's functions through the service's vtable on that day and writes a sub-hash for each of eight sections on `autotest: demolition:` lines. The fingerprint gets the token ` demolition` with one hash of the eight. It changes nothing in the city: every call prices or reads (demolish and apply are always false).

| Section | What it does |
| --- | --- |
| object | QueryInterface with the four IDs it takes and one it does not (which part came back, as an offset), AddRef and Release as changes of the count, the documented fields |
| tuning | the five globals before and after the tuning is read again |
| filters | the demolition filter's Test over a fixed sample of up to 300 of the city's occupants, in eight configurations of its fields (none of the options; the rectangle; the removal test with mode 1; the removal test with mode 2; the rectangle with the removal test; a caller's filter; the removal test with a caller's and a view filter; the rectangle with the removal test and a view filter), and the small terrain filter's Test over the same sample |
| regions | DemolishRegion with demolish false over three regions (the middle of the city, a strip of four columns, one lot) with four argument sets (the bulldoze tool's, a dezoning, a region tool's with a filter, the tool with a filter by kind and clearZoned): result, cost and the sorted types of the occupants collected; the bulldoze and region-tool argument sets also once without a set |
| occupants | DemolishOccupant with demolish false for a share of up to 14 occupants of each type the first region collected (about a hundred in all), with three argument sets: result and cost |
| terrain | ModifyTerrainHeight with apply false over four rectangles with three heights each, then a fixed walk that finds small rectangles where it counts cells and asks those with three more heights, then the per-cell mode over one cell with the height it has |
| lots | DemolishLots with demolish false for up to ten lots in a fixed order, five calls with different lists and arguments: result, cost, the size of the set taken and its types |
| after | the fields the calls were to leave alone: +0x30 empty and the default filter's +0x24, +0x28, +0x2C and +0x30 |

The test's header comment lists seven parts; the eighth hash, "after", is in the code.

### What the test does not tell

- The filter section tells few configurations apart on the tutorial cities: most of the eight configurations give the same answers for the sample.
- Power poles and construction occupants were not in the single-occupant sample (the occupants section) of the saves used.
- Per-cell terrain levelling is only called with heights that change nothing, because the original notifies the terrain whenever cells would change, even in dry-run mode.
- The effect slots (sound and effects, camera-dependent) are not called by the test. They run only in real bulldozes.
- The lot message 0xA9DED179 has no known sender, so DoMessage's lot branch is not reached.
- Nothing that removes (demolish or apply true) is called by this switch; that is what the bulldoze, zoning and dezoning switches are for.

## Trial results

2026-10-12, single runs of 7 days, not the parity harness's full retries. Not part of the parity results below.

Acceptance matrix: one save from each of seven corpora (saves: Big City d0000, Making Money d0090, Rush Hour d1095; saves-plopped p0000; saves-mysim Big City m0010; saves-ports q0000; saves-fire f0002), three switch combinations (the test on day 3; the test on day 3 with a real bulldoze on day 5; a real bulldoze on day 3 with the test on day 5), two runs of the original and one with our code per cell. All 21 cells agree in every sub-hash and token. Every run ended normally.

Side by side with the existing tests on Big City and Making Money: bulldoze (plain, a mode, diagonal), zoning then dezoning, and quiet demolish with bulldoze. The tests' own results are equal on both sides. A plain bulldoze cost 107,653 on Big City m0010 and 11,931 on Making Money d0090. With quiet on, 30,103 demolition animations were skipped, on both sides.

Unchanged-city check: an original run with the test against one with the test set to a day that never comes (leaving the switch out is not a fair comparison, because the environment's size shifts the original's heap layout and with it the outcome). The fingerprints are identical in 15 of 21 cells. In the other 6 (the fire save in all three combinations; the plopped and ports saves after a real bulldoze) the original has several outcomes with and without the test, and the sets overlap. On the ports save with the test before a bulldoze, one outcome appeared only with the test (2 of 6 runs, against 4 runs without). That is consistent with noise but not proven.

Two single-run oddities from the first side-by-side runs, not repeated: on Making Money d0090 with bulldoze, one of four runs of ours had another `citylot` value; on Making Money with zoning, one run of ours stopped silently while loading the city, before any test.

## Parity

Batch dm5, 2026-10-12: all groups pushed so far plus the two demolition groups on side B (115 groups), 7 game days per save, verdicts by outcome sets as in parity.md.

| Leg | Saves | Switches | Result |
|---|---|---|---|
| plain | 12 | none | 12 same on the first pair |
| bz | 12 | `OPENSC4_TEST_DEMOLITION=3 OPENSC4_TEST_BULLDOZE=5` | 11 same; Rush Hour d0365 partly |
| dz | 12 | `OPENSC4_TEST_ZONING=2 OPENSC4_TEST_DEZONING=3` | 12 same |
| plop | 3 (`saves-plopped`) | the bz switches | 2 same on the first pair (p0090 same only after retries); p0000 partly |

In the dz leg the zoning and dezoning lines (lots placed, refused, dezoned, with the city's lot counts) are equal on both sides on all twelve saves.

None of the 90 runs failed, crashed or stopped, and every fingerprint line is complete (the longest is 4,362 characters). The ` demolition` token is equal on both sides in every run of all 15 saves of the two legs with the test, and so is the real bulldoze's cost (for example 106,953 on Big City d0000, 11,931 on Making Money d0000, 900 on Rush Hour d0000, 106,231 on plopped p0000). In every B log the two groups are `ours`, our DemolishRegion sits at 0x00468410, and the bulldoze wrapper logs that it sits on our translation. No hook was skipped.

Rush Hour d0365 (leg bz): all six runs have identical fingerprint lines on all eight days. The first B run stands alone only in the decoded records. The prop developer's record has 14 of 104 list entries differing by small amounts (the first is 695708233 against 695708231). In that run the test's logged "in order" hash of the collected occupants also differs, while the count, the cost and the hashed, sorted form are equal; the collected set is ordered by address, which is why the in-order hash is only logged. Both retries have no differing record. This is not explained further; it reads as an address-order effect.

Plopped p0000: lines 1 to 7 are identical in all six runs. On the last day, two days after the real bulldoze, the `terrain` part's `f` value differs. The original's own runs have two values (f131A4D25 twice, f44CED3C5 once); ours have those two and a third (f2DE0AEEF, one run). The only differing record is the terrain flags record (22 to 25 of 16,641 entries). On p0090 the original alone shows three values of that field in three runs. So the original is several-valued in that field after a bulldoze on these saves; the third value on p0000 was seen only with our code, in one run.

The two single-run oddities from the first side-by-side trials (another `citylot` value once on Making Money d0090; one silent stop while loading Making Money) did not occur in this batch.

## Not done

The bulldoze tool (`cSC4ViewInputControlDemolish`, 0x004B8830 to 0x004B9E20) is interface code and stays the game's. It is a later round, with the other tools (plop and zone).
