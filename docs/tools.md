# The city view's tools

The view input controls that the city view makes current while the player holds a tool: the bulldoze tool, the zone tool and the lot (plop) tool. Each is a small object on the view input control base class (constructor 0x005FB320, destructor 0x005FAFC0, vtable 0x00AA6658; the base class is ours too, see "The base class"). The view makes the tool, calls Init, and from then on hands it the keys and the mouse. Build 641 addresses throughout (data/addresses/tools.toml).

| Tool | State |
| --- | --- |
| Bulldoze | ours (`ui.tools.bulldoze`, below) |
| Zone | ours (`ui.tools.zone`, below) |
| Lot (plop) | ours (`ui.tools.plop1` to `ui.tools.plop3`, below) |
| Base class | ours (`ui.tools.base`, below) |

## The tool classes

Every city tool is a view input control: a class on the base class below, with a main vtable of 30 slots (the effect trigger tool and the common disaster tool have 31). There are 27 derived classes. The sizes are the sizes of the objects; the code ranges are the class's own code, and a few contain foreign functions (the obliterate tool's range holds a tract iterator, the U-Drive control's range holds a few helpers of other classes). Purposes marked "guess" are read from strings and IDs and not confirmed. Counts of functions are given for the classes that are not ours.

| Class | Constructor | Main vtable | Size | Code range | Functions | State |
| --- | --- | --- | --- | --- | --- | --- |
| MySim home tool | 0x00437AF0 | 0x00A85800 | 0x4C | 0x00437AF0 to 0x00438390 | | ours (`ui.mysim_tools.part1` to `part3`, [ui.md](ui.md)) |
| MySim job tool | 0x00438740 | 0x00A858D0 | 0x4C | 0x00438740 to 0x00439377 | | ours (same groups) |
| MySim dispatch tool | 0x00439E50 | 0x00A85998 | 0x90 | 0x00439380 to 0x00439F7E | | ours (same groups) |
| MySim move-in tool | 0x00439FC0 | 0x00A85A20 | 0x4C | 0x00439FC0 to 0x0043A724 | | ours (same groups) |
| Microphone tool (the MySim street sim's advisor) | 0x0043E760 | 0x00A85F40 | 0x30 | 0x0043E740 to 0x0043EB90 | | ours (`simulators.mysim_agents.street_tool`) |
| Obliterate city tool (strings obliterate_finale, sparkles_on_demolish) | 0x0049CF90 | 0x00A8F6D8 | 0xA8 | 0x0049C790 to 0x0049D8A8 | 18 | still the game's |
| Scroll drag tool (right button drag; made by 0x007E6A00) | 0x004B7DC0 | 0x00A90040 | 0x48 | 0x004B7DC0 to 0x004B8479 | 8 | still the game's |
| Mission icon click control (a persistent control the view makes at city load, 0x007ED0F0) | 0x004B8690 | 0x00A900D8 | 0x3C | 0x004B84B0 to 0x004B8822 | 9 | still the game's |
| Bulldoze | 0x004B9070 | 0x00A901A0 | 0xD8 | 0x004B8830 to 0x004B9D56 | 18 | ours (`ui.tools.bulldoze`, below) |
| Dispatch tool (police and fire dispatch; guess) | 0x004BA7D0 | 0x00A90268 | 0x90 | 0x004B9D60 to 0x004BABC0 | 15 | still the game's |
| Effect trigger tool, the base of the common disaster tool (strings _button_trigger, _trigger; never made by itself) | 0x004BAF60 | 0x00A902E0 | | 0x004BABE0 to 0x004BB8FD | 13 | still the game's |
| Flora (plant trees) tool | 0x004BB900 | 0x00A903B8 | 0x50 | 0x004BB900 to 0x004BBE41 | 7 | still the game's |
| Cell painting tool, made by a cheat (purpose a guess) | 0x004BC310 | 0x00A90458 | 0x88 | 0x004BBE50 to 0x004BC975 | 16 | still the game's |
| Text decal tool (a label on the ground; guess) | 0x004BD230 | 0x00A90528 | 0x44 | 0x004BC980 to 0x004BDAFC | 16 | still the game's |
| Level-a-rectangle terrain tool | 0x004BDC60 | 0x00A905C0 | 0x58 | 0x004BDB00 to 0x004BDEBC | 8 | still the game's |
| Lot (plop) | 0x004C0430 | 0x00A90738 | 0x158 | 0x004BDF70 to 0x004C36C0 | 36 | ours (`ui.tools.plop1` to `plop3`, below) |
| Query tool (`cSC4ViewInputControlQuery`; strings Query.txt, "Query cell (%d, %d)") | 0x004C4590 | 0x00A90A88 | 0xA0 | 0x004C3730 to 0x004D767F | 223 | still the game's; not a network tool |
| Signpost pick tool (guess) | 0x004D76F0 | 0x00A928A0 | 0x30 | 0x004D7680 to 0x004D7AE2 | 8 | still the game's |
| Sign text tool (types the text of a sign; guess) | 0x004D7F00 | 0x00A92968 | 0x80 | 0x004D7AF0 to 0x004D8ABA | 17 | still the game's |
| Terrain brush tool | 0x004D9430 | 0x00A92A10 | 0x130 | 0x004D8AC0 to 0x004D9E50 | 23 | still the game's |
| Cheat tool (building footprint) | 0x004D9E80 | 0x00A92AC0 | 0x38 | 0x004D9E50 to 0x004DA593 | 6 | partly ours: four functions in `ui.tools.zone`; the footprint drawing (0x004D9EE0) and OnMouseMove (0x004DA1B0) stay the game's |
| Zone | 0x004DAE80 | 0x00A92CA8 | 0x98 | 0x004DA5A0 to 0x004DC1A0 | 25 | ours (`ui.tools.zone`, below) |
| U-Drive-It driving control (made by the park automaton 0x00506850, not by the view) | 0x00565480 | 0x00A9D9B0 | 0xC0 | 0x00562C90 to 0x00566D55 | 45 | still the game's |
| Fire (torch) disaster tool | 0x005805C0 | 0x00A9EB40 | 0x80 | 0x00580510 to 0x00580B46 | 13 | still the game's |
| Common disaster tool, on the effect trigger tool | 0x00580220 | 0x00A9EAA0 | 0xB4 | 0x00580220 to 0x005804F4 | 7 | still the game's |
| Network intersection placement tool | 0x00661130 | 0x00AAAF20 | 0x90 | 0x00660C30 to 0x00661D5F | 11 | networks (not part of this work) |
| Network drawing tool (13 kinds, one per network type) | 0x006620A0 | 0x00AAB008 | 0x58 | 0x00661DC0 to 0x006625E9 | 10 | networks (not part of this work) |

The network tools are the last two rows, at 0x00660C30 to 0x006625F0. The range 0x004C3730 to 0x004D767F belongs to the query tool.

The classes that are not ours, apart from the two network tools, come to about 446 functions and 126 KB, of which the query tool is 223 functions and 78 KB. Sizes are from the function list and are a lower bound: some functions are a tail or a jump table longer than the list shows, and the query range holds about five small pieces of code the list does not define as functions. The drawing in these classes (the query tool's route and highlight overlay, the terrain brush's cursor, the U-Drive control's nine drawing functions, the network tools' OnMouseMove) is a first sorting, not a reading of every function.

## The base class

The view input control base class: 0x24 bytes, vtable 0x00AA6658 with 30 slots, code 0x005FAFC0 to 0x005FB339 and Release at 0x004B9E20. Every tool above sits on it. It holds the window, the city view and the window manager, the cursor, the reference count and the tool's ID, and does the mouse capture. It does no work of a tool's own.

Group `ui.tools.base`, 28 detours at the game's entries, installed at PreAppInit, off by default (src/core/tool_base.cpp). They are detours at the game's entries because our tools and the game's remaining ones call these functions by address and have them in their vtables. Three parts: the base class's 17 functions, the small model holder's 4 and 7 small functions several tool classes share.

### The base class's functions

| Function | Address | Vtable slot |
| --- | --- | --- |
| Constructor (sets the vtable and clears +4, +8, +0x14, +0x18, +0x1C; leaves +0xC, +0x10 and +0x20) | 0x005FB320 | |
| Destructor (sets the vtable) | 0x005FAFC0 | |
| QueryInterface (IDs 1 and 0xC6BB8345 give the object, referenced) | 0x005FB100 | 0 |
| Release (the count goes down while above 1; the last reference, and a count of 0, destroys through slot 29) | 0x004B9E20 | 2 |
| Init (once: the cursor of the ID at +0x10 from the window manager's cursor set, the cursor to the window, the view and the window manager kept) | 0x005FB1D0 | 3 |
| Shutdown (lets go of the window manager, the view and the cursor) | 0x005FAFD0 | 4 |
| SetCursor by ID (only an ID the cursor set knows changes anything) | 0x005FB070 | 8 |
| SetCursor | 0x005FB020 | 9 |
| SetWindow | 0x005FB0B0 | 10 |
| AmCapturing (the window manager says the window has the mouse) | 0x005FB170 | 25 |
| Deleting destructor | 0x005FB1B0 | 26 |
| PutAway (asks the view to put its current tool away: view slot 0x68 with 0) | 0x005FB0C0 | 27 |
| PlaySound (an ID that is not zero goes to the sound service 0x00B43CB8, slot 0x2C) | 0x005FB260 | 28 |
| Destroy (Shutdown, then the deleting destructor with 1) | 0x005FB0E0 | 29 |
| BeginCapture (window manager slot 0x74 with the window) | 0x005FB130 | |
| EndCapture (window manager slot 0x78 with the window) | 0x005FB150 | |
| IsCurrent (the view's current control, view slot 0x60, is this one) | 0x005FB190 | |

### Fields

| Offset | Field |
| --- | --- |
| +0x04 | initialised (byte, set by Init) |
| +0x08 | the reference count |
| +0x0C | the tool's ID |
| +0x10 | the cursor ID |
| +0x14 | the cursor (referenced) |
| +0x18 | the window (not referenced) |
| +0x1C | the city view (global 0x00B43DC4, referenced by Init) |
| +0x20 | the window manager (referenced by Init) |

The tools put their own fields from +0x24 on. The ones with a message target (a second vtable inside the object) have it at +0x28.

### The model holder

A holder is one pointer; the plop and zone tools keep one for the pointer model and one for the outline model (the plop and zone sections give the offsets). The game's model objects are the holder's callee, not part of it.

| Function | Address | What |
| --- | --- | --- |
| Construct | 0x005FB290 | the holder starts empty |
| Reset | 0x005FB2A0 | hides the model when it is shown (slot 5 asked, slot 4 with 0), clears the holder and releases the model |
| Show | 0x005FB2D0 | with 1 shows the model (slot 3 with 0) unless it is shown; with 0 hides it when it is shown; nothing without a model |
| Make | 0x005FB340 | when the holder is empty, asks the service 0x00B43D1C (slot 7) for the model named local_grid |

### Shared by several tool classes

| Function | Address | Used by |
| --- | --- | --- |
| Deleting destructor with nothing of its own to destroy | 0x004B7ED0 | scroll drag, flora, terrain command |
| Deleting destructor that runs the cheat tool's destructor body (0x004DA180) | 0x004D7AA0 | cheat tool, sign pick tool |
| QueryInterface: the message target's ID gives the object itself and counts the reference at +8 without the vtable; the rest is the base's | 0x004B8480 | the view's default control, the cell paint tool, U-Drive |
| QueryInterface: the IDs 1 and 0x452294AA give the part at +0x28; nothing else, the control's own ID included | 0x004B9DE0 | dispatch tool, MySim dispatch tool |
| QueryInterface: the message target's ID gives the part at +0x28; the rest is the base's | 0x00661D70 | obliterate, terrain brush, zone, network drawing tool |
| A key slot that answers false (the opposite of 0x00505BE0) | 0x004BDBE0 | six tools |
| SetGrabOffset: where on the screen the figure's occupant is, less the cursor, truncated, into +0x34 and +0x38 | 0x004BA070 | dispatch tool, MySim dispatch tool |

### Left the game's

- The vtable slots the base shares with unrelated classes, folded into one stub each: AddRef 0x004BDF00, GetID 0x00517F70, SetID 0x00989960, GetCursor 0x0040CE70, and the handlers that only answer (0x00648EF0, 0x00648F00, 0x005DEB40, 0x0062C980, 0x005BE2E0, 0x00735290, 0x00407990).
- The adjustor thunks of the message target at +0x28 (0x00565570, 0x00565580, 0x00565590, 0x004BA870, 0x004BD2A0, 0x00661DB0) and three jump thunks of one instruction (0x004BD280, 0x004BD290, 0x00580500). What they jump to is ours.
- The model holder's 0x005FB360, which builds the model's transform. It is drawing, called from the OnMouseMove functions left as drawing (0x004C2660, 0x004DBE40, 0x00661260, 0x00662290).
- Two overlay helper functions, 0x004BC3B0 and 0x004BC4A0 (they append cells). The fenced map drawing calls them.

### Test

`OPENSC4_TEST_TOOL_BASE=<day>` (src/core/tool_base_autotest.cpp, token ` toolbase`, sub-hashes on the log lines `autotest: toolbase: ...`). It calls only the game's addresses and the vtable slots, never our functions, so a run with the group on and one with it off compare. Pointers are left out of the hashes. PlaySound is only called with 0 (no sound). Five sections:

1. Object: a bare base object of 0x24 bytes, filled with 0x5A, then made. What the constructor writes and leaves; AddRef and QueryInterface (slot 0 and the three shared variants) with good and bad IDs, each good one released again; the key slot 0x004BDBE0; the capture functions without a window; Init twice, IsCurrent, SetCursor by ID with an unknown and a known ID, SetCursor with null, the old cursor and the same again, PlaySound with 0, PutAway (only while the view has no tool), Shutdown twice. Then SetWindow with the view's window, Init, AmCapturing around BeginCapture and EndCapture, a cursor change, and the last Release, which destroys through slots 29 and 26.
2. Destructors: 0x005FAFC0, 0x005FB1B0 and 0x004B7ED0 without and with the free bit, on objects whose vtable word was cleared; 0x004D7AA0 on a 0x38 byte object holding two stand-in models that count their releases.
3. Model: the holder's Construct, Show and Reset while empty, Make twice, Show 1, 1, 0, 0, 1, Reset; whether a model is held and shown after each step.
4. Grab: SetGrabOffset on a scratch block with a stand-in figure whose occupant stands at the middle of the city; the two offsets it stores.
5. Tool: a bulldoze tool made the way the city view makes it (the view's slot 0x64): IsCurrent, the base fields, the capture and cursor functions and PlaySound through the tool's vtable; PutAway takes it off the view; the last Release destroys it. Two more tools that never saw the view go by Release at a count of 0 and of 1.

### Trial results

Single runs of 7 days on Making Money d0090, Big City d0000 and Rush Hour d0090, the original twice and ours once per case. Plain runs with the group on: identical on all three saves. With the base group and the bulldoze, zone and lot tool tests on: identical on two saves; on Making Money ours equals one of the original's two outcomes (the original's lot test places its lot at a different point between its own two runs).

Parity (batch t1b1, 7 days): plain, 12 of 12 saves identical. With the base, bulldoze, zone and lot tool tests on and their commits: 8 of 12 identical and 4 partly, none failed; on the plopped corpus 1 of 3 identical and 2 partly. Every partly save of the 12 has all daily fingerprints identical and differs only in the saved prop developer record, where the prop types 0x2977AA47 and 0x2977AA49 stand in swapped order in its lists. Two control legs from the same build, one without `ui.tools.base` and one with it, show the same swap in runs of the original (Big City d0090, Rush Hour d1095), so it is the original's own variation, as in the zone tool's batch. The plopped corpus varies in the original too (3 outcomes on two of its saves).

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
- The base class is ours, in its own group (see "The base class"); this tool calls its functions by address.
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

OnMouseMove; the cell region's constructor and destructor, the overlay's methods, the string helpers, the operator new and delete.

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
- The shared helpers 0x004BDF10 (QueryInterface, also the query tool's), 0x004BDF90 (the filter slot), 0x004BDFD0 (RotateRectangle), 0x004BFF30 (the reference vector's fill insert), 0x004C1390, 0x004C3730, 0x004C3750 and 0x004C37A0.
- The adjustor thunks 0x004BFB00, 0x004BFB10 and 0x004BFB20 (the query tool's message vtable shares them).
- The base class is ours, in its own group (see "The base class"); this tool calls its functions by address.

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
