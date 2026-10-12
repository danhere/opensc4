# The city view's tools

The view input controls that the city view makes current while the player holds a tool: the bulldoze tool, the zone tool and the lot (plop) tool. Each is a small object on the view input control base class (constructor 0x005FB320, destructor 0x005FAFC0, about 0x005FAFC0 to 0x005FB320; the base stays the game's). The view makes the tool, calls Init, and from then on hands it the keys and the mouse. Build 641 addresses throughout (data/addresses/tools.toml).

| Tool | State |
| --- | --- |
| Bulldoze | ours (`ui.tools.bulldoze`, below) |
| Zone | ours (`ui.tools.zone`, below) |
| Lot (plop) | ours (`ui.tools.plop1` to `ui.tools.plop3`, below) |

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

Lot tool, batch `tp9`

2026-10-13: all groups pushed so far plus `ui.tools.plop1` to `ui.tools.plop3` on side B (123 groups), 7 game days per save. The leg with the commit runs with the tutorial off on both sides.

| Leg | Saves | Test switches | Result |
|---|---|---|---|
| plain | 12 | none | 12 same on the first pair |
| pt | 12 | `OPENSC4_TEST_PLOP_TOOL=3 OPENSC4_TEST_PLOP_TOOL_COMMIT=1 OPENSC4_TEST_TUTORIAL_OFF=1` | 12 same |
| vm | 12 | `OPENSC4_TEST_VIEW_MENUS=3` (the menu test, which makes this tool) | 12 same |
| plop | 3 (`saves-plopped`) | `OPENSC4_TEST_PLOP_TOOL=3 OPENSC4_TEST_PLOP_TOOL_COMMIT=1` | 3 same |

All 39 first pairs are the same, so no retries ran and no run failed. A first pair that is the same has identical fingerprint lines on every day, the ` ploptool` token and the city after the placed lot included. Quiet plop was not part of this batch; it was compared in the trial runs above.

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

`cSC4ViewInputControlPlaceLot` (our name): 0x158 bytes, main vtable 0x00A90738, message target vtable 0x00A90728 at +0x28, code 0x004BDF70 to about 0x004C36C0 (src/core/tool_plop_1.cpp, tool_plop_2.cpp, tool_plop_3.cpp, header tool_plop.h). The constructor is 0x004C0430, with four arguments: mode (0 places a lot configuration, else a building), configuration ID, building ID, accept-all. The tool places the lot or building the player picked from a menu.

What it does:

1. Init (slot 17, 0x004C13E0) registers the message target for six messages, takes the lot configuration manager (0x00B43D18) and the building development simulator (0x00B43D68), and works out the lot configuration to place. With a configuration ID it takes that. Without one it fills a set with the configurations the first filter lets through and asks the chooser window (0x004BEEF0) for one, offering the first of the set as the default. In building mode it takes the building ID the same way (chooser 0x004BF2F0), fills the set with the configurations of that building type, and asks the first chooser for one. It then reads the base cost from the building, builds the preview and starts the cursor effect. It returns false, after Shutdown, when the choice leaves nothing.
2. A move picks the point under the cursor, snaps the footprint to cells, fits the rotation to the roads and builds the cursor text and the cost. The cost indicator shows the price when it is above 0. The preview moves when anything changed (the point by 0.0001 or more).
3. The placement test (TestPlacement, 0x004C1110) snaps the position to cells, adds the demolition price of what stands in the rectangle to the base cost, and asks the lot manager (slot 21, CanCreatePloppedLot) for a verdict. Verdict 8 means it fits; the ground check (PriceDemolition, 0x004BEAB0) then adds its own price. Verdicts 0 to 7 give a refusal text from the language group 0x0A419226. A position inside the box of the last placed lot (+0x78) is refused without a text and with the cost left at 0.
4. A press asks PlaceLot (0x004C1740) when the move said the place is allowed. PlaceLot checks the funds, demolishes the footprint (flags 0x2000, with the tool's occupant filter), zones it empty and then with zone type 0xF (0xB or 0xC for some building kinds), makes the lot through the lot manager (slot 22), has the building development simulator build on it, and takes the cost from the budget. It adds marker occupants for two kinds of building (constructors 0x006483C0 and 0x00652C30) and, when the smoke argument is set, starts two smoke effects, the first chosen by the lot's size. A refusal plays the sound 0xAA62C9D5.
5. After a placement the tool counts +0x154 down and puts itself away below 1 (it starts at 1000).
6. Exit hides the preview and clears the cursor text and the cost indicator.
7. The rotate keys go through the view's key accelerator: the commands 0x6A935CD5 (right) and 0x6A935CD1 (left) turn the lot a quarter, and the move runs again for the last mouse position.

PlaceLot has a second caller outside the tool: the function at 0x005C5380 (call at 0x005C5554) builds a tool on the stack with the constructor, points it at a lot configuration and places a lot by code, with a smoke flag of its own. One translation of PlaceLot serves both callers.

Nothing in it reads a clock or draws a random number. The picking goes through the view (slot 0x90), a function of the camera and the terrain.

### Pieces

Three groups, off by default, 36 functions in all.

| Group | File | Functions | Phase | What |
| --- | --- | --- | --- | --- |
| `ui.tools.plop1` | tool_plop_1.cpp | 27 | PostAppInit | The tool's occupant filter (constructor, type test), the two lot configuration filters (constructors, tests), MovePreview, SnapFootprint, ScoreSide, PriceDemolition, FitRotation, FloodCheck, FitNetworkPiece, GroundEffect, ClearOverlay, the record vector and its helpers (CopyRange, FillRecords, CopyForward, InsertOverflow, PushBack), the constructor, the destructor body, the deleting destructor, ClearPreview, Shutdown, the two chooser windows |
| `ui.tools.plop2` | tool_plop_2.cpp | 4 | PreAppInit | Init, SetConfiguration (0x004C1360), TestPlacement, PlaceLot |
| `ui.tools.plop3` | tool_plop_3.cpp | 5 | PostAppInit | DoMessage (0x004C2F20), OnKeyDown (0x004C3060), OnMouseDownL (0x004C3120), OnMouseMove (0x004C3300), OnMouseExit (0x004C36C0) |

The hook tables hold 27, 3 and 5 entries. PlaceLot is installed separately in part 2, so that `game.quiet.plop` can wrap it (see "With quiet plop"). Part 2 installs at PreAppInit, one phase before the other two and before the wrapper.

### Fields

Offsets from the start of the object. The three files name some of them differently (see the note under the table).

| Offset | Field |
| --- | --- |
| +0x04 | initialised (set by the base Init) |
| +0x0C, +0x10 | the tool's IDs 0x88F154FB and 0x88E70B76 |
| +0x1C | the city view |
| +0x20 | an object released after a move |
| +0x28 | the message target part |
| +0x2C | mode: 0 places a lot configuration, else a building |
| +0x30 | the lot configuration ID |
| +0x34 | the building ID |
| +0x38, +0x3C | the rotation the player chose, the rotation in use (0 to 3) |
| +0x40 | the rotation at the last preview update (-1 at construction) |
| +0x44, +0x48 | the last mouse position, in screen pixels |
| +0x4C | the lot configuration manager (referenced) |
| +0x50 | accept-all byte, given to the first configuration filter |
| +0x54 | a hash set of the configuration IDs the filter let through (0x14 bytes, count at +0x64) |
| +0x68 | the building development simulator (referenced) |
| +0x6C | a vector of building IDs |
| +0x78 to +0x84 | the bounds of the last placed lot, four ints |
| +0x88 to +0x90 | the picked point, three floats |
| +0x94, +0x95 | the placement is allowed; the preview is shown |
| +0x98 | the cursor text (a game string, characters at +0x9C) |
| +0xAC | an overlay holder |
| +0xB0, +0xC4 | game strings: the names of the cursor effect and of the smoke effect |
| +0xD8 | the cursor effect made from +0xB0 |
| +0xDC, +0xE0 | the cursor overlay objects, let go on a placement |
| +0xE4 | the preview (referenced) |
| +0xE8, +0xF4 | two vectors of preview objects |
| +0x100 | a texture binding |
| +0x108 to +0x114 | a rotation offset byte; the preview's offset, three floats (the game's "unset" float at the start) |
| +0x118 | a byte: the cursor overlay is shown; PlaceLot sets it when it starts the smoke |
| +0x120 | a float, the scale of the smoke effect |
| +0x128 | the base cost, 64 bits |
| +0x138 | the cached cost, 64 bits, -100000 for none |
| +0x140 | the cursor ID in use (0 for none) |
| +0x144 | a vector of 0x14-byte records, each moved with the preview |
| +0x150 | a byte: the special mode, set by Init when the configuration ID is the cursor 0xEBF9A440 |
| +0x154 | placements left (1000 at construction) |

The files' constant names differ for +0x40 (cell flags in part 1), +0x50 (accept-all in part 1, filter flag in part 2), +0x68 (zone service in part 1, development simulator in part 2), +0x118 (overlay shown in part 1, marker in part 2, byte 118 in part 3), +0x154 (tuning in part 1, placements left in part 3) and +0xD8 to +0xE0 (three windows in part 1, the effect at +0xD8 and the overlays at +0xDC and +0xE0 in parts 2 and 3). The table gives the meaning the code supports.

### The placement test and the cost

TestPlacement works the cost out once per rectangle change and the answer is kept at +0x138. PlaceLot recomputes it only while it is -100000. The answer of PlaceLot is true when the cost was worked out in that call and nothing was placed, and otherwise the result of the placement or its failure.

### With quiet plop

`game.quiet.plop` ([quiet.md](quiet.md)) detours PlaceLot and clears the smoke argument for the tool's own call, recognised by the return address 0x004C3190 (after the call at 0x004C318B in OnMouseDownL). Our OnMouseDownL calls PlaceLot from core.dll, so the return address the wrapper sees is ours. Two things make it work:

- Our PlaceLot installs at the game's entry at PreAppInit. The wrapper (PostAppInit) hooks our function when ours is in place, and the game's entry otherwise. The log says which: `quiet: lot placing wrapper sits on our translation` or `... on the game's entry`.
- The wrapper's test also accepts a call our tool marks (`InToolPlaceLot`, `PlaceLotCallSite`, set around the call in OnMouseDownL). The call from 0x005C5554 returns elsewhere and keeps its own smoke flag.

The wrapper logs once which kind it saw: "return address in the game's tool" or "marked call of our tool".

The smoke flag is not display only. Placing with it off changes the `occbase` and `occmgr` parts of the daily fingerprint, on the original as with ours.

### Left the game's

- UpdatePreview (0x004C2660) and its footprint builder (0x004C2090): drawing, the cursor's overlay objects (the arrow, the footprint rectangles and their tint), built with inline vertex arithmetic through the effects service.
- BuildPreview (0x004C0680): it mixes logic with building the preview texture.
- The shared helpers 0x004BDF10 (QueryInterface, also the network lot tool's), 0x004BDF90 (the filter slot), 0x004BDFD0 (RotateRectangle), 0x004BFF30 (the reference vector's fill insert), 0x004C1390, 0x004C3730, 0x004C3750 and 0x004C37A0.
- The adjustor thunks 0x004BFB00, 0x004BFB10 and 0x004BFB20 (the network lot tool's message vtable shares them).
- The view input control base class.

### Two slips found at the merge

The tool was translated by three people in parallel, one part each. Two mistakes showed at the merge and were fixed against the disassembly before the first run: in OnMouseMove the call to the placement test passed the allowed-flag slot where the picked point belongs as the position, and the rectangle scratch was initialised four bytes off from where the callees read it.

### Oddities of the original, kept

- A position inside the last placed lot's box is refused with no text and a cost of 0.
- Init's building path reads the view's slot 0xD0 without testing the view for null.
- OnMouseMove and the marker code reuse stack slots for other values (the marker's position vector is the x, the cost's high word and the height), and the frame is kept so the callees see what they saw.
- PlaceLot passes the zone manager a whole stack word where the callee reads a byte; ours passes the byte.
- PlaceLot returns true when it only worked out the cost.

### How it is tested

`OPENSC4_TEST_PLOP_TOOL=<day>` (src/core/tool_plop_autotest.cpp, token ` ploptool`). It calls only the game's addresses and the tool's vtable slots, never our functions, so a run with the groups on and one with them off compare. Pointers are left out of the hashes.

1. The configuration: the first lot, scanning the cells from the north-west, whose configuration the lot configuration manager knows and which fits in 6x6 cells. This is 0x30000B61 on Big City d0000 and the plopped save, and 0x30000B51 on Making Money d0090. A tool made with a configuration ID never opens a chooser window.
2. Made: the tool as the city view makes it (constructor with mode 0, the configuration, no building, accept-all 0; then the view's SetCurrentViewInputControl, slot 0x64, which runs Init). Its plain fields.
3. Checks: three spots (a free place, a place on lots, a place on water or off the map, found with the lot manager's CanCreateLot), each with the four rotations: SnapFootprint, FitRotation, PriceDemolition and TestPlacement. Their returns, rectangles, snapped points, prices, verdicts and refusal texts.
4. Handlers: OnKeyDown with 14 keys, DoMessage with four messages that open no window (three of the tool's and one unknown), OnMouseExit.
5. Away: the view's slot 0x68; the view's current control is null again.

`OPENSC4_TEST_PLOP_TOOL_COMMIT=1` adds one real placement. A new tool is made and the mouse is moved (OnMouseMove) over a grid of screen points in a fixed order (rows from z = 60 to 700 in steps of 40, columns from x = 40 to 980 in steps of 60) until the tool reports placing allowed (+0x94). Then OnMouseDownL goes to that point, picks the terrain again and asks PlaceLot. The pick is reproducible with the camera untouched. The point, the return and the fields after are hashed. The lot count and the funds before and after are only logged; the daily fingerprint shows the rest. A tutorial city holds the placing back, so the commit runs with `OPENSC4_TEST_TUTORIAL_OFF=1` there.

What the test does not reach, 13 of the 36 functions (23 run as ours, by first-call logging in a run with the commit and quiet plop on Big City d0000): the two chooser windows (never triggered on purpose), FitNetworkPiece and FloodCheck (the special mode), SetConfiguration, the two lot configuration filter classes, four container helpers, and the rotate keys: no key tried produced a rotate command, and the accelerator's mapping is not known. They rest on reading the code.

### Trial results

2026-10-13, single runs of 7 days on Big City d0000, Making Money d0090 and plopped p0000. The original twice and ours once per case.

| Case | Result |
| --- | --- |
| No test; the menu test; the port plopping test | 9 of 9 identical |
| The new test alone; with commit; with commit and quiet plop; after the menu test | 12 of 12 identical |
| Unchanged-city check, no commit switch | identical on all three saves |

The commit:

| Save | Screen point | Tries | Result |
| --- | --- | --- | --- |
| Big City d0000 | (280,140) | 37 | funds 1,035,332 to 1,035,240 |
| Making Money d0090 | (340,220) | 70 | lots 429 to 430 |
| Plopped p0000 | (40,60) | 1 | lots 3518 to 3519; funds 1,261,124 to 1,261,032 |

Quiet plop: the wrapper's line reads "return address in the game's tool" on the original and "marked call of our tool" on ours. Both count one lot placed without smoke.

One B run of the no-test case on Big City d0000 differed in the `adv` part from day 2 while a build ran on the machine. The repeat was identical to both A runs.
