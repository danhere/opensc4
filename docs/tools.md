# The city view's tools

The view input controls that the city view makes current while the player holds a tool: the bulldoze tool, the zone tool and the lot (plop) tool. Each is a small object on the view input control base class (constructor 0x005FB320, destructor 0x005FAFC0, about 0x005FAFC0 to 0x005FB320; the base stays the game's). The view makes the tool, calls Init, and from then on hands it the keys and the mouse. Build 641 addresses throughout (data/addresses/tools.toml).

| Tool | State |
| --- | --- |
| Bulldoze | ours (`ui.tools.bulldoze`, below) |
| Zone | ours (`ui.tools.zone`, below) |
| Lot (plop) | translated, being merged |

## The bulldoze tool

`cSC4ViewInputControlDemolish`: 0xD8 bytes, ID 0x46DDB5F1 0xE1855ADC, vtable 0x00A901A0, code 0x004B8830 to 0x004B9D56 (src/core/tool_bulldoze.cpp, header tool_bulldoze.h). The constructor is 0x004B9070; the view makes it with 0xD8 bytes of new memory.

What it does:

1. A press on the terrain picks a cell, takes the mouse and starts a drag.
2. Every move that changes the cell under the cursor runs the preview: the cell region of the drag is built and the demolition service is asked for the price without demolishing. The overlay shows the cells and the cost indicator shows the price.
3. The release asks again, for real: the price comes off the funds, the service demolishes, the tool is put away from the drag.
4. Escape during a drag lets it go without demolishing.

Nothing in it draws: the overlay is the game's helper object (0x50 bytes), made and fed by calls by address. It reads no clock and draws no random numbers.

### Pieces

One group, `ui.tools.bulldoze`, installed at PreAppInit, off by default. The hook table has 14 entries; four more functions are installed separately (see "With the bulldozer modes"). 18 in all.

| Function | Address | Vtable slot |
| --- | --- | --- |
| Init | 0x004B8AB0 | 3 |
| Shutdown | 0x004B8F30 | 4 |
| OnKeyDown | 0x004B8830 | 14 |
| OnMouseDownL | 0x004B9AE0 | 16 |
| OnMouseUpL | 0x004B9C50 | 18 |
| OnMouseMove (separate) | 0x004B9B70 | 20 |
| Slot 22 (the cursor left) | 0x004B9020 | 22 |
| Deleting destructor | 0x004B91C0 | 26 |
| Cancel | 0x004B9040 | 27 |
| Constructor | 0x004B9070 | |
| Destructor body | 0x004B91E0 | |
| PickCell | 0x004B8980 | |
| FindOccupant | 0x004B8A00 | |
| UpdateHover | 0x004B8D10 | |
| AddCells (the overlay's, in the tool's range) | 0x004B9250 | |
| Preview (separate) | 0x004B93B0 | |
| EndDrag (separate) | 0x004B8ED0 | |
| IsOccupantShown (separate; the tool's filter, slot 3 of 0x00A90180) | 0x004B8880 | |

### Fields

| Offset | Field |
| --- | --- |
| +0x04 | initialised (set by the base Init) |
| +0x0C | the tool's ID (two dwords) |
| +0x1C | the city view |
| +0x28 | the budget simulator (referenced) |
| +0x2C | the city (referenced) |
| +0x30 | the demolition service (global 0x00B43D00, referenced) |
| +0x34 | the lot developer (global 0x00B43D68, its slot 0x90; referenced) |
| +0x38 | the lot manager (referenced) |
| +0x3C | the tool's occupant filter (0xC bytes made by Init, referenced) |
| +0x40 | the occupant manager (global 0x00B43D0C, referenced) |
| +0x44 | press flag: a press is held |
| +0x48 | the hover rectangle: four ints, the cell or lot under the cursor |
| +0x58, +0x5C | the drag's start cell |
| +0x60, +0x64 | the drag's end cell |
| +0x68 | the cell region of the drag (0x24 bytes: the rectangle, then a bit per cell) |
| +0x6C | allowed: the service said the demolition can be done |
| +0x70 | the hovered occupant (referenced) |
| +0x74, +0x78 | the occupant given the highlight (referenced), and its highlight before |
| +0x7C, +0x80 | a set of referenced objects and its count (never filled by the tool) |
| +0x88 | the price, 64 bits |
| +0x90 | the overlay helper (referenced) |
| +0x94 | the hovered occupant is one the tool takes alone |
| +0x98 to +0xD7 | four colours of four dwords: line allowed (+0x98), line denied (+0xA8), fill allowed (+0xB8), fill denied (+0xC8) |

The colours are copied by Init from the renderer's settings. The constructor fills all sixteen words from the dword at 0x00B217FC.

### The calls into the demolition service

All through the service at +0x30 ([demolition.md](demolition.md)).

| Where | Call | Arguments |
| --- | --- | --- |
| Preview, one hovered occupant taken alone | DemolishOccupant (slot 8) | demolish 0, the hovered occupant, privilege 1, flags 1, the price at +0x88, 0, 0 |
| Preview, otherwise (call site 0x004B97F1) | DemolishRegion (slot 6) | demolish 0, the region, privilege 1, flags 1, clearZoned 0, no filter, the price, no set, no occupant, 0, 0 |
| Release, press did not move, occupant taken alone | DemolishOccupant | demolish 1, the hovered occupant, 1, 1, the price, 0, 0 |
| Release, press did not move, occupant not alone (call site 0x004B9D04) | DemolishRegion | demolish 1, the region, 1, 1, 0, no filter, the price, no set, the hovered occupant, the screen point x and z |
| Release, a real drag (call site 0x004B9D04) | DemolishRegion | demolish 1, the region, 1, 1, 0, no filter, the price, no set, no occupant, 0, 0 |

So there are three DemolishRegion calls in the code, from two original call sites, and two DemolishOccupant calls. The tool passes no filter of its own; the tool's filter at +0x3C is used only to pick occupants under the cursor. The allowed answer is stored at +0x6C. After the release the cost indicator is set, a sound plays (the free-demolition sound when the price is zero, a denial sound when the service said no), the drag ends and the mouse is let go.

The region: the selection (the drag's rectangle, or the hovered cell's), widened to the whole of every built lot the view's filter takes that reaches out of it. A lot counts when its state is above 1.

### From a screen point to a cell

PickCell asks the city view for the ground at the point (view slot 0x90, with the pick flag from the terrain's slot 0x14C) and turns the position into a cell with the city's slot 0x17C. The answer is a function of the camera and the terrain. It is false when there is no view or no ground under the point. The tool does not hold a camera of its own.

### With the bulldozer modes

`game.bulldoze` ([bulldoze.md](bulldoze.md)) wraps four functions of this tool at their game entries: Preview, OnMouseMove, EndDrag and IsOccupantShown. It also recognises the tool's DemolishRegion calls by their return address (inside 0x004B8830 to 0x004B9E20). With our tool in place both would break: our code does not sit at those addresses and its calls return into core.dll.

So the tool's group installs at PreAppInit, before game.bulldoze (PostAppInit), and puts our translations of the four functions at the game's entries too. They are not in the hook table; the calls among them go through the game's entries, so the wrapper sees them. Then:

- Each wrapper hooks our function when ours is in place at that entry, and the game's entry otherwise. The case is per function: a foreign detour already at an entry leaves that one the game's and the other three ours.
- The wrapper's test for a call of the tool also accepts any call our code marks as the tool's. Every DemolishRegion call of ours goes through one helper that records the original call site (0x004B97F1 or 0x004B9D04) for the length of the call (`InToolCall`, `ToolCallSite`).
- The behaviour of the modes does not change.

The logs say which case applies. The tool's group writes `bulldoze tool: ours at the game's entries of Preview 1, OnMouseMove 1, EndDrag 1, IsOccupantShown 1` (a 0 for a function left the game's), or `bulldoze tool: <name> is already hooked; ours not installed there`. game.bulldoze writes, per function, `bulldoze: <name> wrapper sits on our translation` or `... on the game's entry`.

### Left the game's

- 0x004B8860, which lies inside the tool's range between OnKeyDown and IsOccupantShown and is not the tool's.
- The view input control base class (constructor, destructor, Init, Shutdown, current-tool test, BeginCapture, EndCapture).
- Shared helpers: the cell region's constructor and cell destructor, the overlay helper's methods (made, scaled, coloured and cleared by calls), the reference set's subtree erase, the operator new and delete, the STL allocator.

### Oddities of the original, kept

- IsOccupantShown does not read `this`.
- The destructor body lets go of the hovered and the highlighted occupants without clearing the fields.
- The press always plays the click sound, even when no cell was picked.
- The set at +0x7C is made and freed but nothing in the tool fills it.
- Init copies the colours out of the renderer's settings on every Init, but the constructor first fills them from a global that is zero in the file.
- The funds are charged before the demolition is asked for.

### How it is tested

`OPENSC4_TEST_BULLDOZE_TOOL=<day>` (src/core/tool_bulldoze_autotest.cpp, token ` bulldozetool`). It calls only the game's addresses and the tool's vtable slots, never our functions, so a run with the group on and one with it off compare. Pointers are left out of the hashes.

1. Made: the tool is made the way the city view makes it (constructor, then the view's SetCurrentViewInputControl, slot 0x64, which runs Init). The ID, that it is the view's current control, its plain fields and which service pointers are set.
2. Prices: three price-only drags, set through the fields (press flag, hover rectangle, start and end cells): one cell, a 4x4 block in the middle quarter of the city, a 1x12 strip. Each runs Preview. The allowed flag, the price, the region's rectangle and bits, whether an occupant is highlighted, and the fields after Cancel.
3. Keys: Escape during a press, Escape with no press, another key during a press, slot 22, Cancel with and without a press.
4. Away: the view's slot 0x68 puts the tool away; the view's current control is null again.

`OPENSC4_TEST_BULLDOZE_TOOL_COMMIT=1` adds one real drag: the 4x4 block through the release handler (OnMouseUpL). The screen point given, (-1000, -1000), fails the pick, so the end cell stays as set; this was checked in the disassembly of 0x004B9C50 (the answer of the pick is not read). The return value and the fields after are hashed. The lot count and funds before and after, and the price, are only logged; the daily fingerprint shows the demolished lots.

`OPENSC4_TEST_BULLDOZE` and the mode switches of [bulldoze.md](bulldoze.md) go through the same code when set.

What the test does not reach, 5 of the 18 functions: OnMouseDownL, OnMouseMove, UpdateHover, FindOccupant and the tool's own IsOccupantShown. They need a real cursor over the terrain (they pick through the view). The Alt diagonal also needs the real key. Rests on reading the code.

### Trial results

2026-10-12, single runs of 7 days. Saves: Big City d0000, Making Money d0090, plopped p0000. The original twice and ours once per case.

| Case | Result |
| --- | --- |
| Tool ours, no test; `OPENSC4_TEST_BULLDOZE=3`; its mode and diagonal switches; the menu test | 12 of 12 comparisons identical |
| The new test alone; with the commit switch; with commit and `OPENSC4_TEST_BULLDOZE_MODE=1`; with the diagonal switch on Big City | 10 of 10 identical |
| Unchanged-city check, no commit switch | identical on all three saves |

That the modes apply with the tool ours: the 4x4 block on Big City d0000 and on plopped p0000 costs 904 plain (28 cells' worth of occupants; funds down by 904) and 0 with the flora-only mode, identically on both sides. With the diagonal switch it covers 9 cells and costs 451 on both sides. Making Money's block has nothing to demolish (0 in every case), so it is a weak case.

## Parity

Bulldoze tool, batch `tb9` (2026-10-12): all groups pushed so far plus `ui.tools.bulldoze` on side B (119 groups), 7 game days per save, verdicts by outcome sets as in [parity.md](parity.md).

| Leg | Saves | Test switches | Result |
|---|---|---|---|
| plain | 12 | none | 12 same on the first pair |
| bt | 12 | `OPENSC4_TEST_BULLDOZE_TOOL=3 OPENSC4_TEST_BULLDOZE_TOOL_COMMIT=1` | 12 same |
| btm | 12 | the bt switches and `OPENSC4_TEST_BULLDOZE_MODE=1` | 12 same |
| plop | 3 (`saves-plopped`) | the bt switches | 3 same |

All 39 first pairs are the same, so no retries ran and no run failed. A first pair that is the same has identical fingerprint lines on every day, the ` bulldozetool` token and the city after the committed drag included.

Zone tool, batch `tz9` (2026-10-13): all groups pushed so far plus `ui.tools.zone` on side B (120 groups), 7 game days per save. The legs with the commit run with the tutorial off on both sides, because the tutorial holds zoning back in the tutorial cities.

| Leg | Saves | Test switches | Result |
|---|---|---|---|
| plain | 12 | none | 12 same on the first pair |
| zt | 12 | `OPENSC4_TEST_ZONE_TOOL=3 OPENSC4_TEST_ZONE_TOOL_COMMIT=1 OPENSC4_TEST_TUTORIAL_OFF=1` | 11 same; Big City d1095 partly |
| ztk | 12 | the zt switches and `OPENSC4_TEST_DEZONE_KEEP=1` | 12 same |
| plop | 3 (`saves-plopped`) | `OPENSC4_TEST_ZONE_TOOL=3 OPENSC4_TEST_ZONE_TOOL_COMMIT=1` | 3 same |

No run failed. In every B log the group is `ours`. In the zt and plop legs the wrapper's line "PlaceZone call from the zone tool recognised" reads "return address in the game's tool" on side A and "marked call of our tool" on side B. In the ztk leg that line is absent on both sides, because the keep-roads test switch skips the caller test.

Big City d1095 (leg zt): all six runs have identical fingerprint lines on all eight days and identical ` zonetool` tokens and test lines (lots 3539 to 3547 after the zoning, to 2589 after the de-zoning; funds the same on both sides). Two of the three B runs differ from the A runs only in the prop developer's saved record, where two references in its lists carry the neighbouring class IDs 0x2977AA47 and 0x2977AA49 the other way round (8 of 59 and 42 of 340 entries). In the ztk leg the original's own two runs of this save differ from each other in exactly that way, so the original produces both orders. The same was seen in batches dm5 and pm9 (see [demolition.md](demolition.md) and [propmgr.md](propmgr.md)); what decides the order is not found.

## The zone tool

`cSC4ViewInputControlZone` (our name): 0x98 bytes, control ID 0xA766C101, vtable 0x00A92CA8, message target vtable 0x00A92C94 at +0x28, code 0x004DA5A0 to 0x004DC1A0 (src/core/tool_zone.cpp, header tool_zone.h). The constructor is 0x004DAE80 (`SC4ViewTool::Construct4DAE80`); the city view makes it in 0x007E6DA0 with a zone type from 0 to 15, 0 being de-zoning. One group, `ui.tools.zone`, PostAppInit, off by default: 25 hooks, the tool's functions except OnMouseMove (0x004DBE40, slot 20: it builds the outline model's transform inline and stays the game's), plus four functions of the cheat tool before it (vtable 0x00A92AC0: Init 0x004D9E50, constructor 0x004D9E80, Shutdown 0x004D9EA0, destructor body 0x004DA180).

What it does: a press picks a cell and starts a drag; every change asks the zone manager's PlaceZone (slot 11) with place = 0 what the rectangle would do (price on the cost indicator, lots in the way highlighted, cells shown through the terrain overlay, a text for the buildings lost or the error); the release asks with place = 1. When the pointer rests for 750 on the cell the drag began on, the frame message grows a region of up to 8 cells around it and previews that.

Fields (from the start): +0x10 cursor ID (table 0x00A92C40), +0x1C the view, +0x24 the overlay's state before the drag, +0x28 the message target, +0x2C overlay, +0x30 zone manager, +0x34 lot manager, +0x38 sound service, +0x3C zone type, +0x40 drag flag, +0x44 start cell, +0x4C current cell, +0x54 hover time (float), +0x58 grown region (0x24 bytes) or null, +0x5C ordered rectangle, +0x6C cell size (float), +0x70 cursor text (a game string), +0x84 set of lots that would go, +0x88 its count, +0x90 pointer model, +0x94 outline model.

### With game.dezone

`game.dezone` ([dezone.md](dezone.md)) acts on PlaceZone calls whose return address is in the tool's range. Only two of the five call sites it listed are PlaceZone calls: 0x004DB312 (UpdatePreview) and 0x004DA846 (Place); the other three (0x004DB40A, 0x004DB764, 0x004DBA3E) call the sound service, whose slot 11 has the same offset 0x2C. Our tool's calls return into core.dll, so every PlaceZone call goes through one helper that records the original site for its length (`tool_zone::PlaceZoneSite()`), and `KeepRoads` accepts a call from the tool's range or a marked one. No function of the zone tool is wrapped by anything, so no entry accessors are needed (unlike the bulldoze tool). PlaceZone is reached through the zone manager's vtable slot 0x2C: with `simulators.zonemgr.place` on, the slot holds our function, which calls `KeepRoads` itself with its caller's return address; with it off, the slot holds the game's entry, which the wrapper has detoured. Both work with the marked call. The first recognised call writes `dezone: PlaceZone call from the zone tool recognised (...)` to the log.

### Left the game's

OnMouseMove; the view input control base class; the cell region's constructor and destructor, the overlay's methods, the string helpers, the operator new and delete.

### How it is tested

`OPENSC4_TEST_ZONE_TOOL=<day>` (src/core/tool_zone_autotest.cpp, token ` zonetool`). It calls only the game's addresses and the tool's vtable slots, for three tools in turn (zone type 1 low density residential, 8 medium industry, 0 de-zone):

1. Made by the view's maker 0x007E6DA0 (constructor, then the view's SetCurrentViewInputControl, which runs Init): ID, cursor, type, plain fields, which service pointers are set.
2. Prices: three drags set through the fields (a free 6x4 rectangle, a 5x5 over lots, a 5x5 over a road, found with the lot manager's CanCreateLot), each followed by UpdatePreview: its return, the ordered rectangle, the lot count, the cursor text, and the fields after CancelDrag. The price goes to the cost indicator and cannot be read back.
3. Keys: OnKeyDown with Escape in a drag and with another key, OnKeyUp, OnMouseExit.
4. Cells: BuildHoverRegion from a free cell, a lot's cell and a road cell (rectangle and bits of the region), CanAddCell for the same.
5. Tips: SetTipText for the error codes 0 to 5.
6. Away: the view's slot 0x68; the view's current control is null again.

`OPENSC4_TEST_ZONE_TOOL_COMMIT=1` adds two real drags through OnMouseUpL: zone type 1 over a free 6x4 rectangle found from the south-east corner, and the de-zone tool over the middle half of the city (where the roads are). The screen point (-1000, -1000) fails the pick and the end cell stays as set (0x004DBB6C leaves the cell fields alone when the pick answers false). A tutorial city holds the placing back (the tutorial service 0x00B43D38, in Place); with `OPENSC4_TEST_TUTORIAL_OFF=1` the placing happens (lots 3569 to 3577 and then 2621 on Big City d0000). Lots and funds are logged; the daily fingerprint shows the rest.

Not reached by the test, 7 of the 25 functions: OnMouseDownL, UpdateHover, DoMessage (the frame message does not arrive inside the day hook) and the four cheat tool functions. They rest on reading the code.

### Trial results

2026-10-12, single runs of 7 days on Big City d0000, Making Money d0090 and plopped p0000 (A = original twice, B = `ui.tools.zone=ours`).

| Case | Result |
| --- | --- |
| No switches; `OPENSC4_TEST_ZONING=2 OPENSC4_TEST_DEZONING=3`; the same with `OPENSC4_TEST_DEZONE_KEEP=1` | 9 of 9 identical (one B run of the first case exited during the load and was repeated; it was identical) |
| Test alone; with commit; commit and keep-roads; commit with the tutorial off; that and keep-roads | 15 of 15 identical |

Keep roads, de-zone commit over the middle half, tutorial off: on Big City d0000 the de-zone costs 92,920 plain (funds 1,035,092 to 942,172) and 91,941 with `OPENSC4_TEST_DEZONE_KEEP=1` (to 943,151), on both sides alike; Making Money d0090 (47,946 to 36,049 and 36,211) and the plopped save (1,260,884 to 1,172,707 and 1,173,632) likewise. The switch bypasses the caller test; that the tool's calls are recognised without it is shown by the log line `dezone: PlaceZone call from the zone tool recognised`, which appears in the original run (return address) and in ours (marked call) with the switch off.

## The lot (plop) tool

Translated, being merged. Section to follow.
