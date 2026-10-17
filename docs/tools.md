# The city view's tools

The view input controls that the city view makes current while the player holds a tool: the bulldoze tool, the zone tool, the lot (plop) tool, the drag-to-scroll control, the mission icon control, the flora tool, the level terrain tool, the dispatch tool, the sign removal tool, the Life tool, the label tool, the sign tool, the terraform tool, the obliterate tool, the effect trigger tool, the common disaster tool and the fire (torch) disaster tool. The query tool is ours (Q1 to Q7, 218 functions) but for what its pieces left the game's: drawing, containers and folded helpers with callers outside the class, each listed in its piece. Each is a small object on the view input control base class (constructor 0x005FB320, destructor 0x005FAFC0, vtable 0x00AA6658; the base class is ours too, see "The base class"). The view makes the tool, calls Init, and from then on hands it the keys and the mouse. Build 641 addresses throughout (data/addresses/tools.toml).

| Tool | State |
| --- | --- |
| Bulldoze | ours (`ui.tools.bulldoze`, below) |
| Zone | ours (`ui.tools.zone`, below) |
| Lot (plop) | ours (`ui.tools.plop1` to `ui.tools.plop3`, below) |
| Drag-to-scroll control | ours (`ui.tools.small1`, below) |
| Mission icon control | ours (`ui.tools.small1`, below) |
| Flora tool | ours (`ui.tools.small2`, below) |
| Level terrain tool | ours (`ui.tools.small2`, below) |
| Dispatch tool | ours (`ui.tools.dispatch`, below) |
| Sign removal tool | ours (`ui.tools.signpick`, below) |
| Life tool (the cheat GOL) | ours (`ui.tools.cellpaint`, below) |
| Label tool | ours (`ui.tools.decal`, below) |
| Sign tool | ours (`ui.tools.signtext`, below) |
| Terraform tool | ours (`ui.tools.brush`, below) |
| Obliterate tool | ours (`ui.tools.obliterate`, below) |
| Effect trigger tool | ours (`ui.tools.disaster_effect`, below) |
| Common disaster tool | ours (`ui.tools.disaster`, below) |
| Fire (torch) disaster tool | ours (`ui.tools.disaster_fire`, below) |
| Query tool | ours but for what its pieces left the game's; Q1 to Q7, 218 functions, are ours (`ui.tools.query1a`, `ui.tools.query1b`, `ui.tools.query2`, `ui.tools.query3a`, `ui.tools.query3b`, `ui.tools.query4a`, `ui.tools.query4b`, `ui.tools.query5a`, `ui.tools.query5b`, `ui.tools.query6a`, `ui.tools.query6b`, `ui.tools.query7a`, `ui.tools.query7b`, below) |
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
| Obliterate city tool (strings obliterate_finale, sparkles_on_demolish) | 0x0049CF90 | 0x00A8F6D8 | 0xA8 | 0x0049C790 to 0x0049D8A8 | 18 | ours (`ui.tools.obliterate`, below) |
| Scroll drag tool (right button drag; made by 0x007E6A00) | 0x004B7DC0 | 0x00A90040 | 0x48 | 0x004B7DC0 to 0x004B8479 | 8 | ours (`ui.tools.small1`, below) |
| Mission icon click control (a persistent control the view makes at city load, 0x007ED0F0) | 0x004B8690 | 0x00A900D8 | 0x3C | 0x004B84B0 to 0x004B8822 | 9 | ours (`ui.tools.small1`, below) |
| Bulldoze | 0x004B9070 | 0x00A901A0 | 0xD8 | 0x004B8830 to 0x004B9D56 | 18 | ours (`ui.tools.bulldoze`, below) |
| Dispatch tool (police and fire dispatch) | 0x004BA7D0 | 0x00A90268 | 0x90 | 0x004B9D60 to 0x004BABC0 | 15 | ours (`ui.tools.dispatch`, below) |
| Effect trigger tool, the base of the common disaster tool (strings _button_trigger, _trigger; never made by itself) | 0x004BAF60 | 0x00A902E0 | | 0x004BABE0 to 0x004BB8FD | 13 | ours (`ui.tools.disaster_effect`, below) |
| Flora (plant trees) tool | 0x004BB900 | 0x00A903B8 | 0x50 | 0x004BB900 to 0x004BBE41 | 7 | ours (`ui.tools.small2`, below) |
| Life tool (Conway's Game of Life, made by the cheat GOL) | 0x004BC310 | 0x00A90458 | 0x88 | 0x004BBE50 to 0x004BC975 | 16 | ours (`ui.tools.cellpaint`, below) |
| Label tool (text decals on the ground, command 0x0B980587) | 0x004BD230 | 0x00A90528 | 0x44 | 0x004BC980 to 0x004BDAFC | 16 | ours (`ui.tools.decal`, below) |
| Level-a-rectangle terrain tool | 0x004BDC60 | 0x00A905C0 | 0x58 | 0x004BDB00 to 0x004BDEBC | 8 | ours (`ui.tools.small2`, below) |
| Lot (plop) | 0x004C0430 | 0x00A90738 | 0x158 | 0x004BDF70 to 0x004C36C0 | 36 | ours (`ui.tools.plop1` to `plop3`, below) |
| Query tool (`cSC4ViewInputControlQuery`; strings Query.txt, "Query cell (%d, %d)") | 0x004C4590 | 0x00A90A88 | 0xA0 | 0x004C3730 to 0x004D767F | 223 | ours but for what its pieces left: 218 functions (0x004C3730 to 0x004D767F, `ui.tools.query1a`, `ui.tools.query1b`, `ui.tools.query2`, `ui.tools.query3a`, `ui.tools.query3b`, `ui.tools.query4a`, `ui.tools.query4b`, `ui.tools.query5a`, `ui.tools.query5b`, `ui.tools.query6a`, `ui.tools.query6b`, `ui.tools.query7a`, `ui.tools.query7b`, below); the 223 is the survey's count, and the pieces' 218 include functions it did not list (such as 0x004C44E0 and 0x004CB880), so the functions left are those each piece lists, not 223 minus 218; not a network tool |
| Sign removal tool (removes signs; the survey had called it a pick tool) | 0x004D76F0 | 0x00A928A0 | 0x30 | 0x004D7680 to 0x004D7AE2 | 8 | ours (`ui.tools.signpick`, below) |
| Sign tool (places signs, drags them and edits their text; the survey had called it a sign text tool) | 0x004D7F00 | 0x00A92968 | 0x80 | 0x004D7AF0 to 0x004D8ABA | 16 | ours (`ui.tools.signtext`, below) |
| Terraform tool (the terrain brush tool) | 0x004D9430 | 0x00A92A10 | 0x130 | 0x004D8AC0 to 0x004D9E50 | 22 | ours (`ui.tools.brush`, below) |
| Cheat tool (building footprint) | 0x004D9E80 | 0x00A92AC0 | 0x38 | 0x004D9E50 to 0x004DA593 | 6 | partly ours: four functions in `ui.tools.zone`; the footprint drawing (0x004D9EE0) and OnMouseMove (0x004DA1B0) stay the game's |
| Zone | 0x004DAE80 | 0x00A92CA8 | 0x98 | 0x004DA5A0 to 0x004DC1A0 | 25 | ours (`ui.tools.zone`, below) |
| U-Drive-It driving control (made by the park automaton 0x00506850, not by the view) | 0x00565480 | 0x00A9D9B0 | 0xC0 | 0x00562C90 to 0x00566D55 | 45 | ours (`ui.tools.udrive1`, T7a; `ui.tools.udrive2`, `ui.tools.udrive3`, T8; `ui.tools.udrive4`, T9) |
| Fire (torch) disaster tool | 0x005805C0 | 0x00A9EB40 | 0x80 | 0x00580510 to 0x00580B46 | 13 | ours (`ui.tools.disaster_fire`, below) |
| Common disaster tool, on the effect trigger tool | 0x00580220 | 0x00A9EAA0 | 0xB4 | 0x00580220 to 0x005804F4 | 7 | ours (`ui.tools.disaster`, below) |
| Network intersection placement tool | 0x00661130 | 0x00AAAF20 | 0x90 | 0x00660C30 to 0x00661D5F | 11 | networks (not part of this work) |
| Network drawing tool (13 kinds, one per network type) | 0x006620A0 | 0x00AAB008 | 0x58 | 0x00661DC0 to 0x006625E9 | 10 | networks (not part of this work) |

The network tools are the last two rows, at 0x00660C30 to 0x006625F0. The range 0x004C3730 to 0x004D767F belongs to the query tool.

The classes that are not ours, apart from the two network tools, come to about 60 functions: what the query tool's pieces left (the KB figures in this paragraph were not redone for Q2 to Q7, so they are too high; the 84 KB is the figure before them) and the two functions of the cheat tool that stay the game's. The 84 KB is the earlier 126 KB less about 5 KB for the four small classes, about 7 KB for the dispatch, sign removal and Life tools (39 functions), about 8 KB for the label and sign tools (32 functions), about 9 KB for the terraform and obliterate tools (40 functions) and about 6 KB for the three disaster tools (33 functions) and about 7 KB for the first piece of the query tool (57 functions), taken from their function sizes and code ranges. Sizes are from the function list and are a lower bound: some functions are a tail or a jump table longer than the list shows, and the query range holds about five small pieces of code the list does not define as functions. The drawing in these classes (the query tool's route and highlight overlay, the network tools' OnMouseMove) is a first sorting, not a reading of every function.

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
| Deleting destructor that runs the cheat tool's destructor body (0x004DA180) | 0x004D7AA0 | cheat tool, sign removal tool |
| QueryInterface: the message target's ID gives the object itself and counts the reference at +8 without the vtable; the rest is the base's | 0x004B8480 | the view's default control, the Life tool, U-Drive |
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
- The shared helpers 0x004BDF10 (QueryInterface, also the query tool's), 0x004BDF90 (the filter slot), 0x004BDFD0 (RotateRectangle), 0x004BFF30 (the reference vector's fill insert), 0x004C1390, 0x004C3730, 0x004C3750 and 0x004C37A0. (Of these, 0x004C3750 is ours now, in `ui.tools.query1a`; the other two stay the game's, see "The query tool, first piece".)
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

## The drag-to-scroll control

The control the city view makes when the right button goes down and no other tool took the press: 0x48 bytes, ID 0x48E2A1F8, vtable 0x00A90040, code 0x004B7DC0 to 0x004B8479 (src/core/tool_small_1.cpp). The constructor is 0x004B7DC0; the view's WindowMouseDownR (0x007E6A00) makes it, puts it on the tool stack (view slot 0x64 with 0) and calls Start with the point of the press. One group with the mission icon control, `ui.tools.small1`, PostAppInit, off by default: 17 hooks in all, 8 of them this control's.

What it does:

1. Start (0x004B82F0) keeps the screen point as the anchor. The first time it also loads the anchor picture (PNG 0x094AC89A of the interface group, magenta see-through), has the window factory make a window of it with the ID 0x48E945B4, centres that on the point and gives it to the view's window as a child.
2. Every mouse move asks Windows whether the right button is still down (the left one when the buttons are swapped). When it is not, the move is an OnMouseUpR: the control puts itself away.
3. While the button is held, the offset is half the way from the anchor to the pointer. Within the inner distance in both directions the cursor is the neutral one and the view does not scroll. Outside it the cursor is one of eight arrows by direction, and the view's SetScrolling (slot 0x84) gets a direction (an angle: 0 along +x, a quarter turn along +z) and a speed, the offset's length over the span between the two distances, 1 at most.
4. The inner distance (12) and the outer one (80) are the constructor's values. Init replaces them from the properties 0xE90F5DE4 and 0xA90F5DEC of the exemplar 0x6534284A, 0x690F693F, 0xE90F6C84; either keeps its value when the exemplar or the property is not there.
5. Releasing the button, or the view taking the control off, stops the scroll and takes the anchor picture's window away.

It reads the button through GetSystemMetrics(SM_SWAPBUTTON) and GetAsyncKeyState, so it follows the real button and real time, not the simulation. It draws nothing itself (the anchor picture is the game's window) and draws no random number.

### Pieces

| Function | Address | Vtable slot |
| --- | --- | --- |
| Constructor | 0x004B7DC0 | |
| Shutdown (lets go of the mouse unless it was captured before Init, stops the scroll, takes the anchor window away, lets go of the picture) | 0x004B7E00 | 4 |
| PutAway (stops the scroll, takes the anchor window away, then the base's PutAway) | 0x004B7E70 | 27 |
| OnMouseUpR (calls slot 27, answers true) | 0x004B7EC0 | 19 |
| OnMouseMove | 0x004B7EF0 | 20 |
| LoadTuning (the two distances from the exemplar) | 0x004B8220 | |
| Init (the ID, the neutral cursor, the tuning, the base Init, the mouse captured unless it already is) | 0x004B82A0 | 3 |
| Start (ret 8: the anchor point and the picture) | 0x004B82F0 | |

### Fields

| Offset | Field |
| --- | --- |
| +0x28, +0x2C | the press point (the anchor), screen pixels |
| +0x30, +0x34 | two floats, the offset held to the outer distance; written by OnMouseMove and read nowhere in the class |
| +0x38 | an arrow cursor has been shown (byte; cleared by Init) |
| +0x39 | the mouse was already captured when Init ran (byte; Shutdown lets go of it only when this is 0) |
| +0x3C | the anchor picture (referenced) |
| +0x40 | inner distance (12) |
| +0x44 | outer distance (80) |

### Left the game's

- The shared deleting destructor 0x004B7ED0 and the key and mouse slots that only answer, which are the base's and the shared stubs (see "The base class").
- The window factory, the image loader, the window methods and the view's SetScrolling are called by address or by slot.

### Oddities of the original, kept

- The two floats at +0x30 and +0x34 are written and never read by the class.
- The angle comes from the x87 instruction sequence (FILD, FDIV, FLD1, FPATAN, with pi added for a negative x) in a single rounding to a float, so ours uses the same instructions.
- The flag handed to SetScrolling is the low byte of the stack slot the modifiers came in, as in the game.

## The mission icon control

A persistent view input control: 0x3C bytes, ID 0xABEC6CFE, main vtable 0x00A900D8, message target vtable 0x00A900C4 at +0x28, code 0x004B84B0 to 0x004B8822, 9 functions, group `ui.tools.small1` (src/core/tool_small_1.cpp). The constructor is 0x004B8690; the view makes one for every loaded city, in 0x007ED0F0, and keeps it under the tools. "Mission icons" is our reading of the code; it was not seen in a running game.

What it does: in view level 1 the dispatch manager's vehicle view (dispatch manager slot 22) draws icons over buildings. The control lights the icon under the pointer. A press on an icon marks it as the pressed one, and a release on the same icon asks the city situation manager (slot 22) to start the situation for that icon's occupant. A zoom, a turn or a change of the view's filter makes it forget the lit and the pressed icon.

### Pieces

| Function | Address | Vtable slot |
| --- | --- | --- |
| Init (the base Init, the city's situation manager and dispatch manager, the message 0xA6B79602 asked for) | 0x004B84B0 | 3 |
| Shutdown (the message given back, the two managers let go) | 0x004B8550 | 4 |
| DoMessage (on 0x06CDB65B, 0xA6B79602 and 0xA6B79612 the lit icon goes dark and the lit and the pressed icon are forgotten; true always) | 0x004B85B0 | 3 of the part at +0x28 |
| OnMouseMove | 0x004B8620 | 20 |
| Constructor | 0x004B8690 | |
| Deleting destructor | 0x004B86D0 | 26 |
| Destructor body | 0x004B86F0 | |
| OnMouseDownL | 0x004B8720 | 16 |
| OnMouseUpL | 0x004B8790 | 18 |

### Fields

| Offset | Field |
| --- | --- |
| +0x28 | the message target part |
| +0x2C | the city situation manager (city slot 49, referenced) |
| +0x30 | the dispatch manager (city slot 39, referenced) |
| +0x34 | the lit icon (0 for none) |
| +0x38 | the pressed icon (0 for none) |

### Behaviour as the game has it

- OnMouseMove with no lit icon lights the icon under the pointer, keeps it and makes it the active one with state 3; the pick answers 0 when there is none, and that is stored all the same. With a lit icon it does nothing while the pointer stays on it; when the pointer has left it, the icon goes dark and is forgotten, and the next move lights the new one. It answers false.
- OnMouseDownL needs view level 1 (read from the view global 0x00B43DC4, not from the control's own pointer). The icon under the pointer becomes the active one with state 2 and the pressed one, and the answer is true. With no icon it answers false and the pressed icon is 0.
- OnMouseUpL in view level 1 makes the icon under the pointer the active one with state 1. When that icon is the pressed one and the vehicle view has an occupant for it, the situation manager gets the occupant (with a null second argument) and the answer is true with the pressed icon kept. Everything else forgets the pressed icon and answers false.

### Left the game's

- The shared QueryInterface 0x004B8480 and the adjustor thunks of the message target (0x00565570, 0x00565580, 0x00565590), which are the base's group.
- The vehicle view's methods (pick at a point, light, make active, the active icon's occupant), which are called by address.

## The flora tool

`cSC4ViewInputControl` for planting trees: 0x50 bytes, ID 0x26D7881C (set by Init), vtable 0x00A903B8, code 0x004BB900 to 0x004BBE41, 7 functions, group `ui.tools.small2`, PostAppInit, off by default (src/core/tool_small_2.cpp). The constructor is 0x004BB900 and takes the flora type; the view makes it from its menu command 0xEA5AD08D.

What it does: the press plants one tree of the type at the point under the cursor, and each mouse move with the button held plants one more. A planting asks the flora simulator (slot 17) to add a flora occupant at the picked point, plays two sounds, withdraws the type's price from the budget and starts the aura simulator's transient effect 1 at the point. The cost indicator shows the sum of the drag. A move with the button up only asks the simulator (slot 15) whether the spot would take a tree and keeps the answer. A planting that fails over water plays the refused sound (0xAA62C9D5); a press with the funds below the price plants nothing.

### Pieces

Group `ui.tools.small2`, 15 hooks in all, 7 of them the flora tool's.

| Function | Address | Vtable slot |
| --- | --- | --- |
| Constructor (the type; the ID and the cursor are Init's) | 0x004BB900 | |
| Shutdown (lets go of the simulator, clears the cost indicator, the base) | 0x004BB940 | 4 |
| OnMouseUpL | 0x004BB980 | 18 |
| Init (the ID, the cursor 0x66DB057F, the base Init, the simulator, the price from the type's record, shown on the cost indicator) | 0x004BBA20 | 3 |
| IsWaterAt (cdecl, two floats: whether the vertex of the cell is under water) | 0x004BBAA0 | |
| OnMouseDownL | 0x004BBAF0 | 16 |
| OnMouseMove | 0x004BBC80 | 20 |

### Fields

| Offset | Field |
| --- | --- |
| +0x28 | the flora type |
| +0x2C | the flora simulator (referenced) |
| +0x30 | the spot under the pointer would take a tree (byte) |
| +0x31 | planting: the button is down (byte) |
| +0x38 | the price of one tree, 64 bits (from the type's record at +0x160) |
| +0x40 | the sum of this drag, 64 bits |
| +0x48 | a move found the funds below the price (byte) |

### Behaviour as the game has it

- A press with funds at or above the price plants, and a picked ground starts the planting whatever came of it.
- In OnMouseDownL the sum and the cost indicator move only when the budget agreed to the withdrawal. In OnMouseMove they move whether it agreed or not.
- A move with funds below the price sets the flag at +0x48; from then on every move ends with the refused sound. The press does not set it.
- A release after a drag that spent something clears the cost indicator; after one that spent nothing it shows the price again. The sum and the flag start over.
- A type the simulator does not know is not tested for in Init.
- IsWaterAt holds the terrain and does not test it for null.

### Left the game's

- The shared deleting destructor 0x004B7ED0 and the shared key slot 0x004BDBE0 (see "The base class").

## The level terrain tool

Drags a rectangle of cells over the terrain and levels it: 0x58 bytes, ID 0x88CEA620, vtable 0x00A905C0, code 0x004BDB00 to 0x004BDEBC, 8 functions, group `ui.tools.small2` (src/core/tool_small_2.cpp). The constructor is 0x004BDC60. The view makes it from its terrain command 7 (0x007E75B0). Which button, if any, sends that command in the shipped menus was not found.

What it does: a press picks a cell and starts a drag; the mouse is captured and the terrain view's cell cursor is turned off (what it was is kept). Every move to a new cell orders the two cells into a rectangle and has the terrain view mark it (colour 2). The release moves the end to the cell under the pointer when the ground is picked, orders the rectangle, and sets every vertex of it to the altitude of the vertex where the drag began (the terrain's SetAltitude for a rectangle, slot 49). The rectangle's far side is one past the last cell, held to the size of the terrain. The marks are cleared, the cell cursor is put back as it was and the mouse is let go.

### Pieces

| Function | Address | Vtable slot |
| --- | --- | --- |
| UpdateRect (orders the drag's cells into the rectangle at +0x44 and marks it) | 0x004BDB00 | |
| Apply (the levelling) | 0x004BDB50 | |
| Shutdown (ends a drag, clears the marks, lets go of the terrain view and the terrain) | 0x004BDC00 | 4 |
| Constructor | 0x004BDC60 | |
| Init (the base Init, the terrain, its cell size, its view) | 0x004BDC90 | 3 |
| OnMouseDownL | 0x004BDCE0 | 16 |
| OnMouseUpL | 0x004BDD80 | 18 |
| OnMouseMove | 0x004BDE30 | 20 |

### Fields

| Offset | Field |
| --- | --- |
| +0x24 | the terrain view's cell cursor state before the drag (byte) |
| +0x28 | the terrain (referenced) |
| +0x2C | the terrain view (terrain slot 66, referenced) |
| +0x30 | dragging (byte) |
| +0x34, +0x38 | the cell the drag began on |
| +0x3C, +0x40 | the cell under the pointer (the drag's end) |
| +0x44 to +0x50 | the ordered rectangle (x0, z0, x1, z1) |
| +0x54 | the cell size, a float from the terrain (16.0) |

### Behaviour as the game has it

- The cell of a picked point is the coordinate over the cell size, truncated. OnMouseMove multiplies by one over the cell size instead of dividing, so ours does the same; the other two handlers divide.
- OnMouseMove only marks again when the cell changed.
- The altitude is read by vertex index: the cells along x plus one, times the start row, plus the start column.
- The three floats handed to the view's pick are set to the float at 0x00B21834 first (zero in the file).
- Shutdown in a drag lets go of the mouse but does not put the cell cursor back.

### Left the game's

- The shared deleting destructor 0x004B7ED0 and the shared key slot 0x004BDBE0 (see "The base class").

## The small tools: test

`OPENSC4_TEST_SMALL_TOOLS=<day>` (src/core/tool_small_autotest.cpp, token ` smalltools`, sub-hashes on the log lines `autotest: smalltools: ...`). It calls only the game's addresses and the tools' vtable slots, never our functions, so a run with `ui.tools.small1` and `ui.tools.small2` on and one with them off compare. Pointers are left out of the hashes. `OPENSC4_TEST_SMALL_TOOLS_COMMIT=1` adds the steps that change the city (a tutorial city may hold them back: `OPENSC4_TEST_TUTORIAL_OFF=1`); `OPENSC4_TEST_SMALL_TOOLS_LEVEL=0` leaves the levelling step out. Six sections:

1. Scroll: the control made as the view's right button handler makes it (constructor on 0x48 new bytes filled with 0x5A, the view's slot 0x64 with 0, Start at 400, 300). The fields after the constructor, Init and Start; whether the anchor picture's window is a child of the view's window and where; Start again at another point; Init again. It goes twice: once taken off by the view (slot 0x68), once by its own OnMouseMove (slot 20), which with the button up is OnMouseUpR and PutAway. Should the button really be down then, OnMouseUpR is called instead and the log says so. After each: the anchor window gone, the view's tool as before, the fields and what the last Release answers.
2. Icons: the view's own mission icon control. Its fields; OnMouseMove, OnMouseDownL and OnMouseUpL at a point far off the screen, with and without a made-up lit or pressed icon in +0x34 and +0x38; DoMessage with the three message types it knows and one it does not; Shutdown and Init, each twice. Then a control of our own: the constructor, Init without a window, the last Release, and the deleting destructor without the free bit on one that holds the two managers.
3. Flora: the tool made as the menu command makes it (the lowest flora type that has a price, the view's slot 0x64 with 2). The fields; the three handlers at a point off the ground; OnMouseMove over a grid of screen points with the button up (whether each spot would take a tree); put away by the view.
4. Level: the tool made the same way. The fields; the handlers with no drag on; a drag set by hand and UpdateRect with the cells in both orders; a real press at the first screen point that picks the ground and a move to the last one; then the view takes the tool off in the drag (Shutdown lets go of the mouse) and the test puts the terrain view's cell cursor back as OnMouseUpL would have.
5. Plant (commit): a second flora tool. OnMouseDownL at the first spot that takes a tree, three OnMouseMove with the button down at the next spots, OnMouseUpL; then with the tool's price field set far above the funds a press, a move (the refused sound) and a release; then the same three at the first screen point over water. The fields, the answers and the funds after every call. Afterwards the trees that were not there before are found and counted (see "The lot tag of a planted tree").
6. Flatten (commit, unless `_LEVEL=0`): a second level tool with a drag of two by two cells set by hand in the middle of the first eight by eight cells the lot manager calls free; OnMouseUpL at a point off the ground levels them to the altitude of the start vertex. The nine altitudes before and after.

The grid for the screen points is rows z = 60 to 700 in steps of 40 and columns x = 40 to 980 in steps of 60.

Not reached: the scroll control's OnMouseMove with the button held (the rates, the arrow cursors, SetScrolling: it moves the camera on frame time); anything with a real mission icon (the pick reads what the last frame drew, and a press and release on one starts a mission); the flora tool with a withdrawal the budget refuses while the funds suffice (the poor-funds steps stop at the funds test); the level tool's release with a successful ground pick (the release is called at a point off the ground). They rest on reading the code.

### The lot tag of a planted tree

Planting made the original stop repeating on Rush Hour d0090: six runs, six outcomes, and only the tree occupants' part of the fingerprint differed. A newly planted tree's lot tag field (+0x3C of the flora occupant), which the class never writes at planting, held a value that differed in every run on one of the four planted trees. Making Money d0090 and Big City d0000 showed none. So the test clears the lot tag of the trees it planted, on both sides, hashes how many trees were new, and only logs the old value (`a new tree's lot tag was ...`). After that the original repeated 3 of 3. Where the value comes from is not found. A player planting trees gets the same.

### Trial results

First acceptance, 7 days on Making Money d0090, Big City d0000 and Rush Hour d0090: the original twice, ours once, and once with only the tools groups ours. Identical in every case, plain and with the test and its commits. The test without the commit switch leaves the city unchanged.

Parity (batch t2b1, 7 days, with the base class's test too): plain 12 of 12 saves identical; with the tests and their commits 12 of 12; the plopped corpus with the tests 3 of 3. All on the first pair.

## The dispatch tool

The police and fire dispatch tool: 0x90 bytes, ID 0x68E1900A, main vtable 0x00A90268, message target vtable 0x00A90258 at +0x28, code 0x004B9D60 to 0x004BABC0, 15 functions, group `ui.tools.dispatch` (src/core/tool_dispatch.cpp). The constructor is 0x004BA7D0 and takes the dispatch type. The survey had marked it "guess"; it is the dispatch tool: its markers are pylons (`cISC4PylonOccupant`, interface 0xC9B6D7D6, occupant type 0xA9B6D7DE) and the service at 0x00B43D04 is the dispatch manager (`cISC4DispatchManager`, the city's slot 39, filled from city slot 0x9C).

The dispatch types are 0 police, 1 fire, 5 fire plane and 6 police helicopter. Type 4 is the MySim dispatch tool's, which has its own class; type 8 is refused by the tool's own code.

The view makes it for four commands and four menu choices, with `new` of 0x90 bytes, the constructor with the type, and the view's slot 0x64 with the operation 2:

| Command | Menu choice (0x007E7130) | Type |
| --- | --- | --- |
| 0xCA935DBD (only in an established city) | 0x33 | 0 |
| 0x6A935D39 (only in an established city) | 0x69A30679 | 1 |
| 0x8C0B0E46 | 0x6BFC8C55 | 5 |
| 0xEC0B0E6A | 0x4BFC8C2F | 6 |

What it does:

1. A click on the ground, with no marker held, asks the manager whether a dispatch of the tool's type is allowed there (CanDispatch, slot 5) and then for one (Dispatch, slot 6). That puts a pylon on the ground and sends a vehicle out.
2. A click on a pylon that is not the held one picks it up: any held one is put down first, the pylon becomes the manager's active pylon, its place and dispatch state are kept in the tool, the offset of the pointer from the pylon is kept (SetGrabOffset, the base's), and message 0xC9F1011A is posted. Pylons of a MySim are not taken (the filter refuses them).
3. While a pylon is held, a move puts it at the pointer plus the offset, with state 3 when a dispatch is allowed there and state 1 when not. The allowed flag is kept at +0x30. The modifier bit with the value 2 on a move says never allowed.
4. The next click puts the pylon down (PutDown). A pylon back inside the cells of the building it came from calls the dispatch off (the manager's RevokeDispatchByPylon, slot 8). Anywhere else a dispatch is asked for at the new place. Either way the manager has no active pylon after.
5. A click with the modifier bit of the value 1, or a second click within 500 ms of the first, selects the pylon's vehicle (the vehicle manager's slot 34) and posts message 0x49F10111.
6. The cursor text shows how many units of the type are free (the manager's GetAvailablePylonCount, slot 21). The text 0x4A54ED13 of group 0x6A231EAA is the format; the view shows it under 0x49D02C4D.

The 500 ms stopwatch (+0x40, mode 4) pairs two clicks into a double click. It is real time, not sim time, as in the original: the range 0x004B9D60 to 0x004BABC0 is not one of the `determinism.sim_timers` ranges, and ours claims nothing. It gates only the pairing of clicks. The tool draws nothing and draws no random number. The picks (the renderer's pick of a model, and the view's ground pick) are a function of the camera and of what the last frame drew.

### Pieces

One group, `ui.tools.dispatch`, PostAppInit, off by default, 15 hooks, all the tool's functions. The occupant filter is a 0xC byte object with the vtables 0x00A9023C and, at +4, 0x00A90224.

| Function | Address | Vtable slot |
| --- | --- | --- |
| OnKeyDown (true for key 9, Tab; for any other the opposite of the shared key stub, so false) | 0x004B9D60 | 14 |
| SetHighlighted (the pylon under the pointer changes; the old one gets state 1, the new one state 3) | 0x004B9D90 | |
| FilterIncludes (a pylon occupant with a UI state, not being dragged, whose owner is not a MySim) | 0x004B9E40 | 3 of the filter |
| Shutdown | 0x004B9EF0 | 4 |
| PickMarker (the pylon at a screen point, referenced, through the renderer's pick with the filter) | 0x004B9FF0 | |
| Dispatch (type, position, really: the manager's Dispatch; false for type 8) | 0x004BA0D0 | |
| Post (posts a message of the framework with a type, an object and four numbers) | 0x004BA120 | |
| PutDown | 0x004BA1B0 | |
| OnMouseMove | 0x004BA320 | 20 |
| OnMouseDownL | 0x004BA510 | 16 |
| Constructor (the type) | 0x004BA7D0 | |
| Deleting destructor | 0x004BA880 | 26 |
| UpdateCursorText | 0x004BA8B0 | |
| DoMessage | 0x004BA940 | 3 of the part at +0x28 |
| Init | 0x004BAAB0 | 3 |

Init sets the ID, the cursor by the type (0xC2E4353D for 0 and 6, 0x02E43537 for 1 and 5, else 1, also kept at +0x58), has the manager begin the dispatch tool (slot 16), makes the stopwatch again (mode 4), runs the base Init, asks the message server for five messages (0x09C8B8EA the view's tool changed, 0xE94671EB to 0xE94671ED dispatches made, moved or called off, 0x894671F6 a dispatch called back), sets +0x74, shows the cursor text and makes the filter. Shutdown gives the five messages back, puts a held pylon back where it was picked up (place and state), has the manager end the dispatch tool (slot 17) and lets go of the filter.

Shared code is the base's: QueryInterface 0x004B9DE0, SetGrabOffset 0x004BA070, the target's adjustor thunk 0x004BA870 (see "The base class").

### Fields

| Offset | Field |
| --- | --- |
| +0x04 | initialised (set by the base Init) |
| +0x0C | the tool's ID |
| +0x10 | the cursor ID |
| +0x1C | the city view |
| +0x28 | the message target part |
| +0x2C | the dispatch type |
| +0x30 | a dispatch is allowed under the pointer (byte) |
| +0x34, +0x38 | the pointer's offset from the held pylon when it was picked up |
| +0x40 | the stopwatch between clicks (0x18 bytes, mode 4) |
| +0x58 | the idle cursor |
| +0x5C | the pylon under the pointer (lit, referenced) |
| +0x60 | the cursor text (a game string) |
| +0x74 | the tool is the view's current one (byte) |
| +0x78 | the occupant filter (referenced) |
| +0x7C | the held pylon's place when it was picked up, three floats |
| +0x88 | its dispatch state then |

### Makers

The ones in the table above, all in the city view's code (src/core/ui_cityview_1.cpp, ui_cityview_6.cpp).

### Behaviour as the game has it

- The constructor never sets +0x88. Shutdown and the tool-changed message write it into the dispatch state of a pylon that the manager has as active and this tool did not pick up.
- The destructor does not let go of the pylon at +0x5C or of the filter; Shutdown does. Init overwrites +0x78 without releasing the old filter, and a failed allocation of the filter is not checked.
- Shutdown releases the lit pylon without putting its UI state back to 1. The tool-changed message does put it back.
- The message 0x99EF1143 (an occupant removed) reads +0x5C without testing it for null. Init never asks for it, so the case is dead in practice.
- PutDown reads the manager from its global each time; where the game jumps to its last call with a null manager, ours calls through the null pointer as the game does.
- The filter's QueryInterface out slot is not set before the call.
- The inside test in PutDown is the four comparisons as the x87 flags give them: inside when x is at or above the first corner and at or below the second, the same for z, and outside when a value is unordered.
- The constructor writes a vtable word into the target slot and then the proper one; ours does the same.

### Left the game's

- The base class's functions the tool shares (see "The base class").
- The dispatch manager, the pylon, the vehicle manager, the renderer's pick, the stopwatch (called by address: IsRunning 0x0088FEFB, ElapsedMilliseconds 0x00890198, Start 0x0089058F, Reset 0x0088FF0A), the localised string, the message class.
- Nothing wraps this tool's functions. `game.bulldoze` has a return-address range that reaches 0x004B9D60 and 0x004B9D90 (its end is 0x004B9E20), but neither function calls the demolition service, so moving them to core.dll changes nothing there.

## The sign removal tool

The survey had called this a signpost pick tool. It removes signs: 0x30 bytes, ID 0xCBCC987E, vtable 0x00A928A0, code 0x004D7680 to 0x004D7AE2, 8 functions, group `ui.tools.signpick` (src/core/tool_signpick.cpp). The constructor is 0x004D76F0. The view makes it for its command 0xABCDEE11 with `new` of 0x30 bytes, and puts it away when signs and labels are switched off (src/core/ui_cityview_6.cpp). The class is on the cheat tool's layout and shares its destructor body 0x004DA180 and deleting destructor 0x004D7AA0, both already ours.

What it does:

1. Init gives it the ID and the cursor 0x8BD4B428, makes the occupant filter (0xC bytes, vtables 0x00A92880 and, at +4, 0x00A92868), and turns the city's signs and labels on (the prop developer's slot 8 with true). This changes a city display setting.
2. A move asks SignAt for the sign under the pointer and hands it to SetSign. SignAt first asks the renderer to pick a model with the filter. When that finds nothing, the terrain point under the pointer goes to the occupant manager, which gives the first occupant there that the filter takes. A sign is a prop occupant (type 0xA823821E, interface 0xE9793A65) of kind 0xAB72FBB3 (a sign on the terrain) or 0xCB79919B (a sign on a network).
3. SetSign marks the hovered sign and unmarks the old one. A sign on a network gets 0.7 when hovered and 0.5 when not, through its interface 0x2B3B7D86 (slot 10). A sign on the terrain has its flag set to 1 or 0 through 0x4B44FBE2 (slot 6).
4. Letting the left button go removes the kept sign through the prop manager (slot 29, RemoveProp) and plays the sound 0x8A5EC047 through the sound service. It does not use the demolition service, so `game.bulldoze` and `game.quiet.demolish` never see it, in the original or in ours.

There is no drag, no cost and no clock, and no random number.

### Pieces

One group, `ui.tools.signpick`, PostAppInit, off by default, 8 hooks, all the tool's functions.

| Function | Address | Vtable slot |
| --- | --- | --- |
| FilterIncludes (a prop occupant that is a sign on the terrain or on a network) | 0x004D7680 | 3 of the filter |
| Constructor | 0x004D76F0 | |
| Init | 0x004D7710 | 3 |
| Shutdown (lets go of the filter, then the base; the sign at +0x2C is left to the destructor) | 0x004D7790 | 4 |
| OnMouseUpL | 0x004D77C0 | 18 |
| SignAt (ret 8: x and z) | 0x004D7820 | |
| SetSign (ret 4) | 0x004D7950 | |
| OnMouseMove | 0x004D7AC0 | 20 |

### Fields

| Offset | Field |
| --- | --- |
| +0x28 | the occupant filter (referenced) |
| +0x2C | the sign under the pointer (its interface 0xE9793A65, referenced) |

### With the UI Modernization probe

A probe of the UI Modernization mod sits on 0x004D7950, the function that makes the hovered sign change. With `ui.tools.signpick` on, our detour is installed at that address too, and two detours cannot share one address, so the two cannot both be installed. The mod's probe is off by default (patch-census.md), and no wrapper of ours touches this tool. Do not run both.

### Behaviour as the game has it

- SignAt hands back a pointer that it has already released; whoever keeps it references it again. The owner of the first model is not tested for null.
- SetSign with the same sign again unmarks and marks it again.
- Shutdown does not let go of the sign at +0x2C.

### Left the game's

- The cheat tool's destructor body 0x004DA180 and the deleting destructor 0x004D7AA0 are ours already, in other groups (`ui.tools.zone`, `ui.tools.base`).
- The renderer's pick, the occupant manager, the prop manager and the prop developer are called by slot.

## The Life tool

The cheat GOL (0x8973A69C, the first name in the city view's cheat table) makes Conway's Game of Life on one byte per city cell, with the live cells shown through a map overlay. The survey had called the class a cell painting tool. 0x88 bytes, ID 0x69701127, main vtable 0x00A90458, message target vtable 0x00A90448 at +0x28, code 0x004BBE50 to 0x004BC975, 16 functions, group `ui.tools.cellpaint` (src/core/tool_cellpaint.cpp). The constructor is 0x004BC310; the cheat handler 0x007F3510 makes it with `new` of 0x88 bytes and no arguments. The class holds a grid of 0x24 bytes at +0x40 (the game's "SC4LifeGrid" in our names).

It changes no city state. It reads the terrain's size and the terrain pick, and nothing else of the city.

What it does:

1. Init fills the grid at random (see the random fill below), asks the framework's frame service (0x86AD10EC, slot 5, with the code 0xFF) for its message, and makes the overlay once: scaled to the cell width, coloured from the four floats at 0x00A90438 (0, 0.8, 0.2, 0.7) and handed to the renderer.
2. Every message the frame service sends is one generation (DoMessage does not look at the type): the overlay's cells are emptied and the grid steps, which tells the overlay every live cell. The number of generations a game day therefore depends on the frame rate.
3. A step counts the live cells among the eight neighbours of each cell in the generation before, the grid wrapped at its edges. Three neighbours make a dead cell live; two or three let a live cell go on, a year older until 0xBF; anything else is dead.
4. A press on a cell flips it and decides what the drag does: on a dead cell the drag paints live cells, on a live one it erases.
5. C, Delete and Backspace empty the grid; R fills it at random again.

### Pieces

One group, `ui.tools.cellpaint`, PostAppInit, off by default, 16 hooks, all the tool's functions.

| Function | Address | Vtable slot |
| --- | --- | --- |
| CellLives (cdecl: x, z, the tool; the grid's function for a live cell) | 0x004BBE50 | |
| OnMouseUpL (lets go of the mouse when the tool has it) | 0x004BBE70 | 18 |
| Grid Randomize | 0x004BBE90 | |
| Grid Paint (ret 8: x and z) | 0x004BBF00 | |
| Grid Step | 0x004BBF50 | |
| Shutdown | 0x004BC190 | 4 |
| Grid Resize (ret 0xC: width, height, random) | 0x004BC230 | |
| DoMessage | 0x004BC2F0 | 3 of the part at +0x28 |
| Constructor | 0x004BC310 | |
| Deleting destructor | 0x004BC420 | 26 |
| Destructor body | 0x004BC440 | |
| Refresh (the live cells into the vector at +0x68 and from there into the overlay) | 0x004BC4D0 | |
| Init | 0x004BC670 | 3 |
| OnKeyDown | 0x004BC7D0 | 14 |
| OnMouseDownL | 0x004BC870 | 16 |
| OnMouseMove | 0x004BC900 | 20 |

### Fields

The tool:

| Offset | Field |
| --- | --- |
| +0x04 | initialised (set by the base Init) |
| +0x0C | the tool's ID |
| +0x10 | the cursor ID (0) |
| +0x1C | the city view |
| +0x28 | the message target part |
| +0x2C | the terrain (referenced) |
| +0x30 | the width of a cell, a float |
| +0x34 | one over it, a float |
| +0x38, +0x3C | the cell the drag was last at (-1 for none) |
| +0x40 | the grid (0x24 bytes, below) |
| +0x64 | the overlay (referenced) |
| +0x68 | the live cells as a vector of (x, z) pairs: begin, end, capacity |
| +0x74 | a string; the text 0x4A56B5FB of group 0x6A231EAA is loaded into it by Init and never shown |

The grid, offsets from its start (+0x40 in the tool):

| Offset | Field |
| --- | --- |
| +0x00 | stopped (byte; no generations; nothing in the game sets it) |
| +0x04 | what a drag does: 0 paints, 1 erases |
| +0x08, +0x0C | width, height in cells |
| +0x10 | the cells (0 dead, else the age, at most 0xBF) |
| +0x14 | the cells of the generation before |
| +0x18 | the function called with (x, z, context) for a cell that lives |
| +0x1C | the same for a cell that does not (always null) |
| +0x20 | their context (the tool) |

### The random fill

Randomize makes a generator of 0x18 bytes on its stack with seed -1, which the game's constructor turns into a clock seed (time xor the clock), and then draws one number below 2 for every cell, row by row, and puts the low byte into both arrays. It uses no city generator, so the tool cannot move the simulation's random streams. Ours wraps the generator's construction in `fixed_seed::SiteOverride(0x004BBEA2)`, the original's return address, so that the fixed seed replacement gives the site what the original's site gets, and keeps the draws one for one. It runs from Init (through Resize) and from key R.

### The overlay helpers

Two helper functions of the map overlay, 0x004BC3B0 and 0x004BC4A0, stay the game's (the map drawing calls them too). Only 0x004BC3B0 (append every pair of a vector to the overlay's cells) is called by us, by address, from Refresh. 0x004BC4A0 (empty the overlay's cells, then jump to 0x004BC3B0) is not called: its body is inlined in the game's Refresh and ours writes the emptying inline. The overlay's other methods (construct, start, scale, colour, add a cell, clear, stop) are also called by address.

### Behaviour as the game has it

- OnMouseDownL and the grid's Paint do not check the picked cell against the grid: a pick outside the map writes out of bounds, as in the game.
- Init stores the game's address 0x004BBE50 as the live-cell callback, which is where our detour sits, so memory holds the same value as in the original.
- A step also writes the new age back into the generation before while it passes; it does not change the count, which tests for not zero.
- In a grid one cell wide or high, a cell counts itself as a neighbour. Ours wraps all eight offsets, so it does the same.
- Ours writes the wrapped form of the neighbour count for every cell. The game reads an inner cell's neighbours directly and wraps only the edges; the eight bytes read are the same.
- The grid's stopped byte, the dead-cell callback and the text of Init are never used.
- Resize takes a width or height of 0 as 16.

### Left the game's

- The overlay helpers above, and the map overlay class's methods.
- The shared slots: QueryInterface 0x004B8480 and the key-up slot 0x004BDBE0 are the base's; the adjustor thunks of the target stay the game's.

## The dispatch, sign removal and Life tools: test

`OPENSC4_TEST_TOOLS3=<day>` (src/core/tool_t3_autotest.cpp, token ` tools3`, sub-hashes on the log lines `autotest: tools3: ...`). It calls only the game's addresses and the tools' vtable slots, never our functions, so a run with `ui.tools.dispatch`, `ui.tools.signpick` and `ui.tools.cellpaint` on and one with them off compare. Pointers are left out of the hashes. The three tools are made as the view makes them: `new` filled with 0x5A, the constructor, the view's slot 0x64 with 2. The screen points are the same grid as the small tools' test, and the point (-1000, -1000) picks nothing. The random fills are logged and kept out of the token.

Switches:

- `OPENSC4_TEST_TOOLS3_COMMIT=1` adds the two steps that change the city (a tutorial city may hold them back: `OPENSC4_TEST_TUTORIAL_OFF=1`).
- `OPENSC4_TEST_TOOLS3_SEND=0` leaves the dispatch step out of the commit (all that stays is the removal).
- `OPENSC4_TEST_TOOLS3_PARTS=<bits>` picks the parts: 1 the dispatch tool, 2 the sign removal tool, 4 the Life tool. The default is 7.

Log sections:

1. Dispatch, once for the police type (0) and once for the fire type (1): the fields after the constructor and after Init, the cursor text, the manager's counts of the type; the filter over every pylon of the city and the first 20 trees; the key slot (Tab and another key); the handlers at a point off the ground (the stopwatch is reset after the press); OnMouseMove over the grid, which notes where a dispatch is allowed, and once more with the modifier of the value 2; then by address PickMarker, Dispatch with type 8 and with really false, PutDown with nothing held, SetHighlighted with null and UpdateCursorText; DoMessage of the part at +0x28 with the dispatch messages, an unknown one and the tool-changed message in four shapes; put away by the view and the last Release. Line `dispatch type <n>`.
2. Signs: whether signs and labels were shown before and after Init (put back at the end), the fields, the filter over every prop and over the first trees, how many props are signs, the handlers off the ground and over the grid, SignAt and SetSign by address (with a sign of the city when there is one), put away. Line `signs`.
3. Grid: the grid's functions on a scratch grid: Resize, Paint in both modes and with a mode that is neither, Step with counting functions (a glider on 7 by 6 for 30 generations, a block for 200 generations so the age stops at 0xBF), grids one cell wide, high and square, the stopped byte, a 16 by 16 grid from Resize with zeroes and a random fill of 24 by 10. Line `grid`.
4. Life: the tool as the cheat makes it: the fields, the random grid of Init (hashed apart, then emptied by key C so that the rest does not depend on the seed), the keys (C, R, Delete, Backspace and one the tool does not take), a press, a drag and a release on the ground, a glider by hand and three generations through the message target, the handlers off the ground, put away. Line `life`.
5. Send (commit): with the fire type, OnMouseDownL at the first grid point where a dispatch is allowed; the manager's records of the type; the new pylon made the manager's active one by hand (as a pick-up does) and moved with OnMouseMove to the next allowed point; PutDown; then every dispatch called off (the manager's slot 9). The records after each step. Line `send`. With `_SEND=0` this step is left out.
6. Remove (commit): with a sign in the city, the first one by position is kept with SetSign and removed with OnMouseUpL, and the signs left are counted. A city without signs keeps what it has. Line `signs` (the commit adds to the same section).

Not reached:

- Sign marking and removal and the hit branches of SignAt: none of the corpus saves has a sign, so this test runs only the no-sign paths. The label and sign tools' test makes a label and a sign and runs this tool on them (see "The label and sign tools: test").
- The dispatch tool's pick-up, vehicle selection and double-click branches, and its Post function: they need a marker picked from a drawn frame, and the pick reads what the last frame drew. The test sets the held pylon by hand instead.
- The tool-changed handling when the tool is replaced by the scroll control, and DoMessage's type 0x99EF1143 (the tool never asks for it).
- PutDown's revoke branch, where a held pylon is put down inside the building it came from: the pylon the test holds has no such building.

The dispatch step changes the city and the original is noisy after a dispatch (the vehicles travel on frame time): judge the commit legs by outcome sets and compare the manager's records, not the vehicles' positions.

### Trial results

2026-10-13, single runs of 7 days on Making Money d0090, Big City d0000 and Rush Hour d0090. The original twice and ours once per case.

| Case | Result |
| --- | --- |
| Plain, tools groups on | identical on all three saves |
| The test alone, no commit | the city unchanged on all three saves |
| Only the tools groups ours (`ui.tools.base` and the three), test with commits | identical to the original on all three saves |
| The full list of groups, test with commits | parted from the original after the first dispatch until `determinism.dispatch_info` was added to the base list; identical with it (see [audio.md](audio.md), "The runs that parted after a dispatch") |

Parity (batch t3b1, 7 days, with the base class's and the small tools' tests): plain 12 of 12 saves identical; with the tests and their commits but without the dispatch step (`OPENSC4_TEST_TOOLS3_SEND=0`) 12 of 12; with the dispatch step and the groups `audio.part5` and `audio.part6` left out 12 of 12, and 3 of 3 on the plopped corpus; all on the first pair. With the dispatch step and the full list of groups 0 of 12 in that batch: every save parted from the original after the dispatch. The cause was the tutorial system reading a record of the called-off dispatch that nobody had filled ([audio.md](audio.md), "The runs that parted after a dispatch"), not the tools' code or the sound code. With `determinism.dispatch_info` in the base list (batch aud9, the dispatch tool's test with its commits, the full list): 12 of 12 and 3 of 3 on the plopped corpus, plain 12 of 12, all on the first pair.

## The label tool

The tool that writes text on the ground. The survey had called it a text decal tool and marked it "guess"; it is the label tool: 0x44 bytes, main vtable 0x00A90528, window procedure part with vtable 0x00A90518 at +0x28, code 0x004BC980 to 0x004BDAFC, 16 functions, group `ui.tools.decal` (src/core/tool_decal.cpp). The constructor is 0x004BD230 (our name `SC4LabelTool::Construct`). The view makes it for its command 0x0B980587, sent by the button 0xCB95412C, with `new` of 0x44 bytes, the constructor with no arguments and the view's slot 0x64 with the operation 2 (src/core/ui_cityview_6.cpp). A label is a text decal prop occupant (`cSC4PropOccupantTextDecal`, 0xBC bytes, kind 0xCB79919B, interface 0x2B3B7D86). It is the "sign on a network" that the sign removal tool also takes.

What it does:

1. Init gives it the ID 0x8B3CE416 and the cursor 0xEB72FEF4, turns the city's signs and labels on (the prop developer's ShowSignsAndLabels with true; this changes a city display setting) and makes the filter (0xC bytes, vtables 0x00A904FC and, at +4, 0x00A904E4), which takes a prop occupant of type 0xA823821E that answers the label interface.
2. A click on empty ground adds a new decal to the city (the prop manager's AddCityProp, slot 19), opens the text window and, when it ends with text, places the label and sets the text. A click on a label presses it (state 1); letting go without a drag opens the text window for that label.
3. A move while the label is held (state 2) places it again at the point under the pointer. While idle, the label under the pointer is shown at 0.7 and the one before at 0.5.
4. Delete, while idle, removes the label under the pointer (RemoveProp, prop manager slot 29). Home (0x24) and End (0x23) turn the label: they step the direction at +0x40 through the table at 0x00A905A0 (3 2, 2 3, 0 1, 1 0) and place it again.

### Where a label goes

Place gives a label its colour, size, position and direction from what is in the cell under it (the report does not say how the position is set on water and open ground, so those rows give no position):

| Cell | Colour | Size | Position and direction |
| --- | --- | --- | --- |
| Water (terrain slot 34) | 0.5, 0.5, 1 | 2 | |
| A network piece with flag 0x40 (rail) | 1, 0.9, 0 | 2 | RailPlacement |
| Another network piece | 1, 0.9, 0 | 0.8 if the piece has any of the flags 0x403, else 0.64 | RoadPlacement |
| Open ground | 0.5, 1, 0.5 | 2 | |

RoadPlacement and RailPlacement read the edge value of the traffic network's piece in the cell (the traffic network is the global 0x00B43D54, the city's slot 40, not the network manager) and give a direction from 0 to 3, a shift of plus or minus 4 (road) or 8 (rail), and the cell's centre. The survey had called these three functions "preview geometry". They are logic, and nothing in the tool draws. RailPlacement fills a static table on first use (0x00B2180C, guard bit 0 of 0x00B2182C); ours writes the game's own table and guard.

### The text window

EditText loads the window 0x8A8DFCF5 of the layout {0, 0x96A006B0, 0x6B704690} under the window at 0x00B43DC8, makes the tool's +0x28 part its window procedure, fills the edit field 0xAA4149BF with the label's text, selects it all, sets the length limit to 0x20 and runs the window manager's modal loop (slot 0xA4). The text is kept in the decal's game string at +0x6C.

| Ended with | Result |
| --- | --- |
| 0x8A8DFD4E (OK) | the field's text goes to the label; empty text means the label does not stay |
| 0x8A8DFD4A (cancel) | the label does not stay and the caller removes it; cancelling the edit of an existing label deletes that label |
| 0x6BA18178 or anything else | unchanged, the label stays |

The window procedure ends the run on those three buttons, and on Enter (message 0x1A, 4, key 0x0D) as OK.

### Pieces

One group, `ui.tools.decal`, PostAppInit, off by default, 16 hooks, all the tool's functions. The three jumps and the thunk at 0x004BD280 to 0x004BD2A0 stay the game's (project rule for such tiny code).

| Function | Address | Vtable slot |
| --- | --- | --- |
| Init | 0x004BC980 | 3 |
| FilterIncludes | 0x004BC9B0 | 3 of the filter |
| DoWinProc (ret 8: window and message) | 0x004BCA10 | 3 of the part at +0x28 |
| RoadPlacement (ret 8: position and direction) | 0x004BCB00 | |
| LabelAt (ret 4: a terrain point) | 0x004BCDC0 | |
| SetLabel (ret 4) | 0x004BCE80 | |
| RailPlacement (ret 8) | 0x004BCEE0 | |
| Constructor | 0x004BD230 | |
| Deleting destructor | 0x004BD2B0 | 26 |
| Shutdown | 0x004BD2E0 | 4 |
| Place (ret 0x10: x, y, z by value and the label) | 0x004BD320 | |
| OnKeyDown | 0x004BD4B0 | 14 |
| OnMouseMove | 0x004BD5A0 | 20 |
| EditText (ret 4: the label; returns whether it stays) | 0x004BD6B0 | |
| OnMouseDownL | 0x004BD890 | 16 |
| OnMouseUpL | 0x004BDA40 | 18 |

### Fields

| Offset | Field |
| --- | --- |
| +0x04 | initialised (set by the base Init) |
| +0x0C | the tool's ID |
| +0x10 | the cursor ID |
| +0x1C | the city view |
| +0x28 | the window procedure part |
| +0x2C | state: 0 idle, 1 a label pressed, 2 dragging |
| +0x30 | the label (referenced); while idle the one under the pointer |
| +0x34 | three floats: the label's position less the picked point at the press; written and never read |
| +0x40 | the direction for placing (1 from the constructor) |

### Behaviour as the game has it

- A drag snaps the label to the point under the pointer; the grab offsets at +0x34 are computed and never used.
- A failed allocation of a new decal is not checked (a null dereference, as in the game).
- DoWinProc and EditText hand an unset `edit` pointer to GetChildAs, as the game does.

### Left the game's

- The base class's functions the tool shares (see "The base class"), the prop manager, the occupant manager, the traffic network, the renderer's terrain pick, the window manager and its modal loop, the sound service (the sounds 0x2C16DA45 on picking up and 0x2C16DA29 on putting down).
- Nothing wraps these functions. No wrapper in `src/core/game_*.cpp` has a range or a return-address test that reaches either tool, and neither calls DemolishRegion, PlaceZone or PlaceLot: their changes to the city go through the prop manager's AddCityProp and RemoveProp, which are ours and carry no return-address test.

## The sign tool

The tool that places signs on the terrain, drags them and edits their text in place. The survey had called it a sign text tool and marked it "guess", and it had listed only the typing; it is the whole sign tool: 0x80 bytes, main vtable 0x00A92968, notice target part with vtable 0x00A92958 at +0x28, code 0x004D7AF0 to 0x004D8ABA, 16 functions, group `ui.tools.signtext` (src/core/tool_signtext.cpp). The constructor is 0x004D7F00 (our name `SC4SignPlaceTool::Construct`). The view makes it for its command 0x0B980581, sent by the button 0x0B954123, with `new` of 0x80 bytes, the constructor with no arguments and the view's slot 0x64 with the operation 2. A sign is a prop of 0x590 bytes (kind 0xAB72FBB3, interface 0x4B44FBE2, primary vtable 0x00AA5478, constructor 0x005F5510). The exe has no name string for that class: `SC4TerrainSignProp` is our name, and the survey's `cSC4SignpostOccupant` is unconfirmed.

The 0x004D7AF0 to 0x004D8ABA range also holds six functions that are not the class's: the reference holder helpers 0x004D7B30, 0x004D7B60 and 0x004D7BB0, and the shared string code 0x004D7E50, 0x004D7ED0 and 0x004D8650. They stay the game's, and 0x004D7B60 and 0x004D7ED0 are called by address. So the class has 16 functions and one thunk (the target's adjustor thunk 0x004D7FB0), not the 17 the survey counted.

What it does:

1. Init gives it the ID 0x8B4CE5A0 and the cursor 0x6B72FECF, reads the text encoding (0x00940DA8) into +0x64, takes the clipboard service (0x27EBFFFD, interface 0x07EC0010) into +0x7C, asks the service 0x64063387 for notice 0x0BE10A20 for the part at +0x28, and turns the city's signs and labels on (ShowSignsAndLabels with true).
2. A click on empty ground adds a new sign to the city at the picked point (the prop manager's AddCityProp), its height the higher of the terrain's height and its slot 29 (GroundHeight), with empty text, and starts editing it (state 3). A click on a sign presses it (state 1), found through the renderer's pick of a model and then the model's owner asked for the sign interface.
3. A move with a pressed sign starts the drag (state 2) and moves the sign to the terrain point under the pointer plus the grab offset. Letting go puts it down with the put-down sound (0x2C16DA29), or, when it has no text, or when it was only pressed, edits its text (state 3).
4. While idle, the sign under the pointer becomes the kept one and is marked. Delete (0x2E) removes it (RemoveProp).
5. A sign whose text is empty is removed from the city whenever the tool lets go of it: SetSign with another sign or with none, Shutdown, and Enter.

The prop developer keeps signs (kind 0xAB72FBB3) and labels in its list at +0xA6C, which is saved. It moves signs with the terrain and drops labels when the network piece in their cell is removed (prop.toml entries at 0x006F5104 and 0x006F5228). A label test followed by bulldozing its road therefore reaches that code too.

### Typing

The view's `WindowCharacter` (0x007E6600, ours) calls the tool's slot 13 with one dword per byte of typed text. The encoding from 0x00940DA8 is the constant 8, which the string helpers at 0x009106A9 to 0x009108ED treat as UTF-8, so text arrives as UTF-8 bytes, one call per byte. A lead byte (0xC0 to 0xFD) with nothing pending is kept at +0x68; the following bytes are added there until the string holds the lead byte's trailing bytes plus one, and then AppendText adds the whole character to the text at +0x3C and +0x68 is cleared. The path for a single-byte encoding (neither 8 nor 100 or above) is translated and cannot run in this build.

OnCharacter returns false unless a text is being edited. While editing:

| Input | Result |
| --- | --- |
| 0x1B (Escape as a character) | taken, nothing done |
| 0x0D (Enter) | the edit ends, SetSign(null), state 0; a sign with empty text is removed |
| 8 (Backspace) | the last whole character is taken off |
| another byte below 0x20 | taken and dropped |

OnKeyDown, not editing: Delete with a sign removes it and returns true; any other key returns false. Editing: Escape puts the text the edit began with (+0x50) back into the sign and the text, stays in state 3 and returns false; V (0x56) with the modifier bit of the value 2 set and the bits of the values 1 and 4 clear appends the clipboard's text (service slot 18); every key but Escape returns true.

The length limit: AppendText counts the characters in the text (0x009107D8) and stops before a character only when the count is over 32, so 33 characters fit. A line feed is skipped, and is counted in a paste. There is no limit in bytes; a paste with a cut-off UTF-8 tail would read past the string, and ours does the same. After every change the sign's slot 4 gets the text, which the sign keeps in its game string at +0x98.

The caret is not the tool's. It is the sign's own: its slot 7 (SetEditing) registers with the service 0x86AD10EC for notice 0x80 and keeps a counter at +0xB0 and a caret byte at +0xAD. That code belongs to the prop class and is outside this piece; which clock drives that notice was not read.

### The notice part

Init asks the service 0x64063387 (interface 0xC1085722, slot 5) for notice 0x0BE10A20, and Shutdown gives it back (slot 6). The only sender found is the framework's window code at 0x00993DE0. The handler answers only in state 3: it writes 1 into the answer and the screen point over the middle of the sign's box into the last two words (the camera view's WorldToView, truncated). The survey had read this as placing a text window; the tool has no window. That the notice places the input method's window is a guess; the sender was not traced.

### Pieces

One group, `ui.tools.signtext`, PostAppInit, off by default, 16 hooks, all the tool's functions. The filter is a 0xC byte object with the vtables 0x00A9293C and, at +4, 0x00A92924.

| Function | Address | Vtable slot |
| --- | --- | --- |
| QueryInterface (0xE98B2F57 gives the part at +0x28) | 0x004D7AF0 | 0 |
| Init | 0x004D7BE0 | 3 |
| FilterIncludes (a prop occupant that answers the terrain sign interface) | 0x004D7CC0 | 3 of the filter |
| GroundHeight (ret 8: x and z; the result in st(0)) | 0x004D7D20 | |
| DoNotice (ret 4: the four words of a notice) | 0x004D7D70 | 3 of the part at +0x28 |
| Constructor | 0x004D7F00 | |
| SetSign (ret 4) | 0x004D7FC0 | |
| Deleting destructor | 0x004D80A0 | 26 |
| Destructor body (jumps to the base destructor 0x005FAFC0; ours pops no argument) | 0x004D80C0 | |
| Shutdown | 0x004D8130 | 4 |
| OnMouseDownL | 0x004D81D0 | 16 |
| OnMouseMove | 0x004D84B0 | 20 |
| AppendText (ret 4: a string) | 0x004D8670 | |
| OnMouseUpL | 0x004D8730 | 18 |
| OnCharacter | 0x004D8830 | 13 |
| OnKeyDown | 0x004D89D0 | 14 |

### Fields

| Offset | Field |
| --- | --- |
| +0x04 | initialised (set by the base Init) |
| +0x0C | the tool's ID |
| +0x10 | the cursor ID |
| +0x1C | the city view |
| +0x28 | the notice target part |
| +0x2C | state: 0 idle, 1 a sign pressed, 2 dragging, 3 editing |
| +0x30 | the sign (referenced; marked through its slot 6) |
| +0x34, +0x38 | integers: the sign's screen place less the pointer at the press |
| +0x3C | a game string: the text being edited |
| +0x50 | a game string: the text the edit began with |
| +0x64 | the text encoding |
| +0x68 | a game string: the bytes of a character not yet whole |
| +0x7C | the clipboard service (referenced) |

The constructor does not set +0x34, +0x38 and +0x64.

### Behaviour as the game has it

- OnMouseDownL never releases the model that the renderer's pick returned on a hit (the sign removal tool does release it). One reference leaks per click on a sign.
- The state 3 test inside the pick path is dead (state 3 is handled at the top); kept.
- A dragged sign takes the picked terrain point as its position without GroundHeight; only a new sign gets the height.
- A failed allocation of a new sign is not checked.
- QueryInterface with a null `this` writes null and then faults, as the game.
- The grab offset is the truncated pointer position subtracted as integers (the base class's SetGrabOffset subtracts floats).

### Left the game's

- The base class's functions the tool shares, the five helpers listed above, the prop class (including its caret), the clipboard, the notice service, the string and encoding code at 0x009106A9 to 0x009108ED, 0x00940DA8 and 0x009107D8, the renderer's pick, the prop manager.
- Nothing wraps these functions (see the label tool).

## The label and sign tools: test

`OPENSC4_TEST_TOOLS4=<day>` (src/core/tool_t4_autotest.cpp, token ` tools4`, eight sub-hash lines `autotest: tools4: ...`). It calls only the game's addresses and the tools' vtable slots, never our functions, so a run with `ui.tools.decal` and `ui.tools.signtext` on and one with them off compare. The tools are made as the view makes them: `new` filled with 0x5A, the constructor, the view's slot 0x64 with 2. Pointers are left out of the hashes, and so are the notice's screen point (it is the camera's), the sign's caret and counter, and the sounds. The paste is not tested.

Without any switch the test leaves the city as it was. Switches:

- `OPENSC4_TEST_TOOLS4_COMMIT=1` adds what changes the city (a tutorial city may hold it back: `OPENSC4_TEST_TUTORIAL_OFF=1`). It leaves one label "Label 2" and one sign "Sign é", and runs the label tool's text window under the modal stand-in (the player reads the edit field, sets its text, sends the tool's window procedure the keyboard message with the real window and answers a button). It also runs the cancel and empty-text variants, which remove their labels, and the Place sweep over every third cell.
- `OPENSC4_TEST_TOOLS4_REMOVE=1` needs the commit. It makes one extra label and one extra sign and runs the sign removal tool (0x004D76F0, see above) on each.
- `OPENSC4_TEST_TOOLS4_PARTS=<bits>` picks the parts: 1 the label tool, 2 the sign tool, 4 and 8 their round trips without the commit. The default is 15.

What the parts do:

1. Label: the fields after the constructor and after Init; the filter over every prop and the first trees; the window procedure with no window (the three buttons, another button, another command, Enter, another key, an unknown message); RoadPlacement and RailPlacement by address on every cell; LabelAt on the grid's terrain points; the keys with no label; the handlers at a point off the ground and over the grid of screen points. Then a label of the test's own, added to the city: Place by address, SetLabel, Home and End, LabelAt where it stands, a drag (state 1 set by hand, OnMouseMove twice, OnMouseUpL), the hover over it, Delete.
2. Sign: the fields, QueryInterface, the filter, GroundHeight on every fourth cell, the notice with nothing edited, the keys and a character while idle, the handlers off the ground and over the grid. Then signs that are gone again when the test ends. A new sign is edited by typing: 'A', 'b', the two bytes of U+00E9 one at a time, Backspace, a tab, the three bytes of U+20AC, a lead byte followed by a letter, Escape as a character, Backspace until nothing is left and once more, 40 letters, two more bytes, a key that is not Escape, V without a modifier, AppendText with a line feed in the text, Escape, and Enter with no text (the sign is removed). A sign typed "D" and ended with Enter stays until SetSign and Delete remove it. A sign typed "Hi" is dragged, pressed and let go (edited again), extended and cut with '!' and Backspace, and removed when the view puts the tool away while it is edited with no text.

Typing, in the original and in ours alike: the text is unchanged after the lead byte 0xC3 and reads "Abé" after 0xA9; Backspace takes the whole character; 40 letters leave 33; a line feed is skipped.

### Why some round trips need an empty cell

Adding and removing a prop makes the original's flammability simulator recalculate (docs/propmgr.md), and that shows in the daily fingerprint's `flam` part. So the round trips without the commit are done only at terrain points under the grid whose 64 m cell of the occupant manager is empty. A city with fewer than two such points gets no round trips without the commit (the first log line says so); the three acceptance saves have 11, 25 and 57.

### The sign removal tool on real objects

The sign removal tool's first run on real objects was in this test. OnMouseMove finds the label and the sign, and OnMouseUpL removes each; it removes both kinds. The original and ours did the same on three saves.

### Not reached

- A sign picked on screen through its model, and the hover in OnMouseMove that finds a sign: the pick reads what the last frame drew, so the test sets the kept sign by hand.
- The paste, and the single-byte encoding path.
- DoWinProc ending a real modal run with a real window.
- RailPlacement's first-use table fill, unless the save has the right rail piece.
- No coverage run was made.

### Trial results

2026-10-13, 7 days on Making Money d0090, Big City d0000 and Rush Hour d0090, four cases (plain, the test alone, the test with the commit, the test with the commit and the removal), each as the original twice, with all groups ours and with only the tools groups ours. 48 runs, all finished. All four sides were identical in every cell, and the test alone equals the never-run reference.

Parity (batch t4b1, on a5860eb, 7 days): plain 12 of 12 the same, the test alone 12 of 12, the test with the commit and the removal 12 of 12, plopped 3 of 3.

## The terraform tool

The tool that raises and lowers the ground while the button is held. The survey had called it the terrain brush tool; the view makes it for the terraform commands, with a brush ID and a flag that says whether the brush is free. It is 0x130 bytes, main vtable 0x00A92A10, message target part with vtable 0x00A92A00 at +0x28, code 0x004D8AC0 to 0x004D9E50, 22 functions, group `ui.tools.brush` (src/core/tool_brush.cpp). The constructor is 0x004D9430 (our name `SC4TerraformTool::Construct`). The view's StartTool130 (0x007E5910, ours) makes it with `free` = 1 for command 0x4A2B2462 and layout button 0x899231E6 (god mode), and with `free` = 0 for command 0x8A313C52, button 0x0A32DDD2 and the three fixed brushes 0x0A33E37B, 0xEA33E2FD and 0x4A524342 (mayor mode, which costs money).

What it does:

1. A press on the ground (OnMouseDownL) picks the terrain point, records the altitude there, and starts a scrub. The tool captures the pointer and shows its looks (two effects and a cursor, chosen by brush ID from the terrain).
2. Every application is one call of the terrain's ApplyBrush (slot 57) with the brush parameters at +0x74. The first application of a scrub is made at the press. Moves queue a stretch from the last point to the new one, and a move is applied at most once between two frame messages.
3. While the button is held, every third frame message without a move queues the last point again, so a brush held still keeps digging at frame cadence.
4. A paid brush takes its price from the budget at each application. The price is the cost per application times the strength, rounded; a refusal by the budget ends the application.
5. Letting go (OnMouseUpL) ends the scrub, takes the tool off the frame service and flushes the terrain's textures. The texture flush also happens on its own after a fixed number of applications (the first tuning value below).
6. With Caps Lock on, the scrub follows the level plane of the altitude at the press, not the ground.

The keys (OnKeyDown, with the modifier bit of the value 1): F1 to F10 set the strength 0.1 to 1.0 and the digits 1 to 9 and 0 set the radius scaling 0.1 to 1.0. Which key each modifier bit is has not been checked (shift and control are assumed from the key codes 0x10 and 0x11), so the spec names the bits neutrally.

### Pieces

One group, `ui.tools.brush`, PostAppInit, off by default, 22 hooks, all the tool's functions.

| Function | Address | Vtable slot |
| --- | --- | --- |
| ApplyModifiers (ret 4; hooked through a naked thunk that keeps ECX, because two callers use ECX after the call) | 0x004D8AC0 | |
| CountApplication | 0x004D8B20 | |
| TuningReset (cdecl callback) | 0x004D8B70 | |
| LoadTuning (cdecl) | 0x004D8BC0 | |
| PressAltitude (ret 8: x and z) | 0x004D8CB0 | |
| CursorPoint (ret 0xC: x, z and a result of three floats) | 0x004D8D10 | |
| Shutdown | 0x004D8DD0 | 4 |
| SetLooks (ret 4: whether scrubbing) | 0x004D8F50 | |
| Init | 0x004D9080 | 3 |
| UpdateCursor (ret 8: a point of three floats and whether to show) | 0x004D9160 | |
| Constructor (ret 8: brush and free) | 0x004D9430 | |
| Destructor body (jumps to the base destructor) | 0x004D9560 | |
| OnKeyDown | 0x004D95D0 | 14 |
| OnKeyUp | 0x004D96E0 | 15 |
| OnMouseExit | 0x004D9720 | 22 |
| Apply (ret 4: whether to flush) | 0x004D9740 | |
| Deleting destructor | 0x004D9950 | 26 |
| OnMouseUpL | 0x004D9970 | 18 |
| Queue (ret 8: from and to, two floats each) | 0x004D9A30 | |
| DoMessage | 0x004D9AC0 | 3 of the part at +0x28 |
| OnMouseDownL | 0x004D9B90 | 16 |
| OnMouseMove | 0x004D9D80 | 20 |

Two functions in the range are not in the group. 0x004D8B90 is the terrain's brush parameters constructor (an entry in terrainsim4.toml, called from 0x006F37BF and 0x0074AA06 in the terrain code), not the tool's. 0x004D8EF0 (PowInt, a power function in x87 that serves only the effect's zoom scale) is called by address: it has one caller, and calling it gives the same floating point for free. The survey's count of 23 included PowInt; the class itself has 22 functions.

UpdateCursor and CursorPoint are translated: the cursor is a few virtual calls on an effect object plus one camera getter, and CursorPoint is a terrain pick or a camera ray onto a level plane. Neither draws.

### Fields

| Offset | Field |
| --- | --- |
| +0x24 | byte: the terrain view's cell cursor state saved for the scrub |
| +0x28 | the message target part |
| +0x2C | brush ID |
| +0x30 to +0x54 | ten IDs from the terrain's slot 63: [0, 1] and [4, 5] the two effects while scrubbing, [2, 3] and [6, 7] the two effects while idle, [8] and [9] the cursor while scrubbing and idle |
| +0x58 | frame messages since the last application |
| +0x5C, +0x60, +0x70 | the terrain, the terrain view, the window manager |
| +0x64 | scrubbing |
| +0x65 | pointer inside |
| +0x68, +0x6C | the last scrub point (x, z) |
| +0x74 to +0xAB | the brush parameters: from and to (x, z each), zoom -1 at +0x84, redisplay 1 at +0x88, strength at +0x8C, radius scaling at +0x90, message data at +0x94 (3, then 1 free or 2 paid), altitude at +0xA8 (100, then the pressed point's y) |
| +0xAC | a move was applied since the last message |
| +0xB0 | applications since the texture flush |
| +0xB4 | free |
| +0xB8 | 64-bit integer: spent in this scrub |
| +0xC0 | the real-time stopwatch (mode 3, microseconds) |
| +0xD8 | "first application of the scrub" |
| +0xDC | float 1.0, read nowhere |
| +0xE0, +0xE4 | the strength and the radius scaling the keys set |
| +0xE8, +0xEC | the two effects (pointers) |
| +0xF0 | the last looks point (three floats) |
| +0xFC, +0x100 | the cursor when shown and when hidden |
| +0x108 | the second stopwatch (mode 4, milliseconds) |
| +0x120 | the queue's delay, always 0 |
| +0x124 | the altitude at the press |
| +0x128 | the queue's list sentinel; the nodes are 0x1C bytes (next, previous, time, four floats) |

### Tuning

LoadTuning reads the property group 0x6534284A, instance 0x88CD66E9 (language 1), into six globals. The exe's own defaults, and the values the shipped exemplars give:

| Global | Meaning | Exe default | Loaded |
| --- | --- | --- | --- |
| 0x00B097A8 | applications per texture flush | 100 | 1000 |
| 0x00B097AC, 0x00B097B0 | strength and radius scaling for the modifier bit of the value 2 | 0.25, 0.25 | 0.25, 0.8 |
| 0x00B097B4, 0x00B097B8 | the same for the modifier bit of the value 1 | 4.0, 1.0 | 2.0, 2.0 |
| 0x00B097BC | strength rate, one over property 0x69DB8D05 | 10.0 | 10 |

### Clocks

The terrain result of a scrub depends on real time in the original, and it is not on sim time.

- **Stopwatch +0xC0 (microseconds).** Read in Apply and restarted there. For every application except the first of a scrub, the strength is the base strength times the smaller of (elapsed seconds times the rate) and 2.0. With the rate of 10, 100 ms between applications gives the factor 1.0, and 200 ms or more the cap of 2.0. A paid brush's price follows the same factor.
- **Stopwatch +0x108 (milliseconds).** Started in OnMouseDownL, read in Queue and Apply. It only stamps the queued stretches. Since the delay at +0x120 is always 0, every stretch is due at once, so no outcome depends on it.
- **The frame service (0x86AD10EC, code 0xC0).** The frame messages above.
- 0x004D8AC0 to 0x004D9E50 is in none of the `sim_timers` owner ranges, so the stopwatches read the same clock in the original and in ours. If the tool is ever to be deterministic under the harness, `sim_timers::Claim` on +0xC0 is the lever; nothing claims it now.

There are no random draws in the tool.

### Behaviour as the game has it

- Shutdown releases an effect only when it is shown, and the second effect's block releases the first effect's pointer again (0x004D8E88). The destructor releases both.
- Shutdown does not take the tool off the frame service; only OnMouseUpL does.
- Apply uses the budget without a null test.
- DoMessage hands the window manager two out slots, one unset and one holding the message pointer.
- Brush type 5 (0x8A31301C) is not in the terrain's brush table; the lookup says not found and the cost is the default of 32.
- OnKeyDown on a tool the view has put away crashes the original (0x005FB196): IsCurrent reads the view pointer that the base class's Shutdown cleared. Ours does the same; the test does not do it.
- The OnMouseMove inside a scrub asks the real keyboard whether Caps Lock is on. With Caps Lock on, both sides take the level plane path and the terrain result changes.

### Left the game's

- PowInt (0x004D8EF0), the base class's functions the tool shares (see "The base class"; the QueryInterface at 0x00661D70 is shared with the obliterate tool), the terrain's ApplyBrush and the other terrain slots, the effects manager and the effects, the camera getters (0x007F66A0, 0x007F6AB0, 0x007F7710), the budget, the window manager.
- Nothing wraps these functions. No wrapper in `src/core/game_*.cpp` has a range or a return-address test that reaches the tool.

## The obliterate tool

The tool that wipes the city. The view makes it after the question window 0x2A41436C is answered 0x6A41BBB5 (AskAndStartTool, 0x007E9150, ours). It takes no clicks: it runs the "wipe the city" scene off the frame service's message. It is 0xA8 bytes, main vtable 0x00A8F6D8, message target part with vtable 0x00A8F6C4 at +0x28, code 0x0049C790 to 0x0049D8A8, 18 functions, group `ui.tools.obliterate` (src/core/tool_obliterate.cpp). The constructor is 0x0049CF90 (our name `SC4ObliterateTool::Construct`).

**This tool ends by rewriting the save on disk.** When the scene is over, End (0x0049CC30) posts the message 0xEA65224E. The application handles it with its Obliterate (0x00454E50, `ui_app_5.cpp`): it writes the city to a temporary file, destroys the city, deletes the city's file and saves again under the old path. Anything that reaches End, AfterWipe (0x0049CE20) or the tool's message 0xEA623590 destroys the file the city was loaded from. The test (below) keeps away from it.

What it does:

1. Init captures the mouse, turns off the city's saving (slot 104), pauses the simulator (HiddenPause), turns the situation manager off, registers with the frame service, shuffles the list of blocks, and posts 0xAA650861 (1, 0, 0) and 0x2A611B51 (the audio code handles both). It does this in every run, test or not.
2. State 1, the sweep: on each frame message, DemolishRegion(1, the block's region, 0, 0x2000, 0, the filter, 0 five times) on as many blocks as the curve of blocks over seconds asks for, with the effect `sparkles_on_demolish`.
3. State 2, the finale: the effect `obliterate_finale`. After the tuning's time, RemoveOccupants(filter, 1, 0), DemolishRegion on the whole city, and PlaceZone(region, 0, 1, 1, 1, 1, 0, 0, 0, 0), which dezones everything.
4. State 3, the wipe is done: it waits for the message 0xEA623590 from the effect, or 20 seconds.
5. State 4, End: PutAway, and posting 0xEA65224E.

Terrain and flora are not touched. The filter (0xC bytes, vtables 0x00A8F694 and, at +4, 0x00A8F67C) refuses types 0x74758926 (flora) and 0x69D25C5F. Escape (OnKeyDown 0x1B) puts the tool away mid-sweep and leaves a half-demolished city, as in the game.

### Pieces

One group, `ui.tools.obliterate`, PostAppInit, off by default, 18 hooks.

| Function | Address | Vtable slot |
| --- | --- | --- |
| OnKeyDown (Escape calls slot 27, PutAway) | 0x0049C790 | 14 |
| TuningReset (cdecl callback) | 0x0049C7C0 | |
| FilterIncludesType (ret 4) | 0x0049C7E0 | 4 of the filter |
| Shutdown | 0x0049C8B0 | 4 |
| LoadTuning (cdecl) | 0x0049C9B0 | |
| Post (cdecl: type and three words) | 0x0049CA70 | |
| End | 0x0049CC30 | |
| StartEffect (cdecl, 5 arguments: name, place, lift, keep, out) | 0x0049CC60 | |
| StartFinale | 0x0049CD90 | |
| AfterWipe (ret 4: microseconds) | 0x0049CE20 | |
| DemolishRect (ret 8: a rectangle and whether to dezone) | 0x0049CEA0 | |
| Constructor | 0x0049CF90 | |
| Deleting destructor | 0x0049D020 | 26 |
| FinaleTick (ret 4: microseconds) | 0x0049D070 | |
| BlockHasOccupants (ret 4: a rectangle) | 0x0049D0E0 | |
| Init | 0x0049D390 | 3 |
| SweepTick | 0x0049D5A0 | |
| DoMessage | 0x0049D820 | 3 of the part at +0x28 |

Left the game's inside the range: the stub 0x0049C7B0 (it returns true; slots 16, 18 and 20 hold it, and five other classes' vtables do too), and the foreign code of the tract iterator (0x0049C800, 0x0049C820 SetBounds, 0x0049C860, 0x0049CAF0, 0x0049CB10 Next, 0x0049CBE0 the rand() shuffle, 0x0049CF50, 0x0049D0C0 and 0x0049D1C0 Restart). The iterator is not hooked anywhere; the tool calls Next and Restart by address and writes the inlined constructor and SetBounds field by field, as the original does.

### Fields

| Offset | Field |
| --- | --- |
| +0x28 | the message target part |
| +0x2C | the filter (0xC bytes) |
| +0x30, +0x34, +0x38 | the occupant manager, the zone manager, the demolition service |
| +0x3C, +0x40 | the city's last cell along x and along z |
| +0x44 | state: 0 made, 1 sweep, 2 finale, 3 wiped, 4 over |
| +0x48 to +0x77 | the tract iterator over the blocks |
| +0x78 | microseconds since Init |
| +0x7C | microseconds of the finale |
| +0x80 to +0x8C | the city's rectangle |
| +0x90 | blocks asked for so far |
| +0x94 | microseconds since the wipe |
| +0x98 | the finale effect |
| +0x9C | blocks demolished |
| +0xA0 | empty blocks in a row |
| +0xA4 | the situation manager's switch before Init |

### Tuning

LoadTuning reads the property group 0x6534284A, instances 0x7A4A8458 and 0xCA5A576F:

| Global | Meaning | Exe default | Loaded |
| --- | --- | --- | --- |
| 0x00B09530 | block side, in cells | 8 | 4 (1024 blocks on the tutorial cities) |
| 0x00B21740 and 0x00B21734 | the curve of blocks over seconds, and its last x | | sweep 10 seconds |
| 0x00B09534 | the finale, in microseconds | 250000 | 10 seconds |

### Clocks and random numbers

- There is no stopwatch. All timing is the frame message's slot 14 value, added into +0x78. It is microseconds by the arithmetic (times 1e-6 against curve seconds; tuning seconds times 1e6; the limit of 20000000 in state 3). It is real frame time, so how many blocks each message takes, and how the DemolishRegion calls are batched, vary from run to run. The end state (everything gone and dezoned) does not.
- Init calls the iterator's Restart in list mode, which shuffles the blocks with the C runtime's `rand()` (0x0049CBE0): one draw per block after the first, from the sequence the simulation shares. No generator objects are made, so no `fixed_seed::SiteOverride` is needed.

### Behaviour as the game has it

- Post uses a message that failed to allocate unchecked (it crashes as the game would).
- Init uses the city, the filter and the result of operator new unchecked.
- LoadTuning's sweep seconds stay unset in the game when property 0x6A5A6702 is missing; ours start at 0.
- SweepTick takes the cell region apart in line, where DemolishRect calls 0x00441A60.

### Left the game's

- The stub and the iterator code above, the demolition service, the zone manager, the occupant manager, the situation manager, the effects, the frame service, the application's Obliterate.
- Wrappers: `game.bulldoze` detours DemolishRegion and tests the return address against [0x004B8830, 0x004B9E20); the tool's calls return to 0x0049CF0F and 0x0049D77D, outside it, and they pass a filter and flags 0x2000, so no branch of the wrapper applies. `game.dezone` tests PlaceZone callers against [0x004DA7A0, 0x004DC1A0); the tool's call returns to 0x0049CF36, outside it. `game.quiet.demolish` is not reached (the flags' low byte is 0). No marking is needed.

## The terraform and obliterate tools: test

`OPENSC4_TEST_TOOLS5=<day>` (src/core/tool_t5_autotest.cpp, token ` tools5` in the fingerprint line, details on the log lines `autotest: tools5: ...`). It calls only the game's addresses and the tools' vtable slots, never our functions, so a run with `ui.tools.brush` and `ui.tools.obliterate` on and one with them off compare. Without any further switch the test leaves the city as it was.

**The test never reaches the obliterate tool's End.** End, AfterWipe and the tool's message 0xEA623590 are not called. In the wipe leg the test sets the state of the put-away tool to 4 (End does nothing then), keeps a reference to the tool (a deliberate 0xA8-byte leak, so the tool is never freed) and listens for 0xEA65224E for the rest of the run. The last log line says how often it came: "the day after: message 0xEA65224E came 0 times". A token with "SCENEOVER" on the fingerprint line would say it came.

Switches:

- `OPENSC4_TEST_TOOLS5_PARTS=<bits>`: 1 the terraform tool, 2 the obliterate tool. The default is 3.
- `OPENSC4_TEST_TOOLS5_COMMIT=1` adds what changes the ground and, for the brush that costs, the funds: a scrub with a free brush and with a paid one.
- `OPENSC4_TEST_TOOLS5_OBLITERATE=1` sweeps and wipes the city in memory, up to state 3.
- The commit and obliterate legs need `OPENSC4_TEST_TUTORIAL_OFF=1`; a tutorial city holds them back.

What the parts do:

1. Terraform: what the terrain says of the ten brushes (types 0 to 6 of its slot 58 and the mayor's three fixed ones): radius, cost and the ten looks IDs. The tool made as the view makes it (constructor on 0x130 new bytes filled with 0x5A, with a brush and the free flag; the view's slot 0x64 with 2): the fields after the constructor (free and paid) and after Init; LoadTuning and the six values; TuningReset; SetLooks, UpdateCursor and ApplyModifiers (0 to 3) by address; the keys (F1 to F10 and the digits with modifier 1, other keys, 0x10 and 0x11 down and up with modifiers 0 to 3); CountApplication once and at the tuning's count; Apply with nothing queued; PressAltitude and CursorPoint by address and OnMouseMove over a grid of screen points; the handlers at a point off the ground (a press there begins nothing); the frame message with no scrub; two stretches queued by address (the destructor drops them); the view puts the tool away. The test sends no key after that.
2. Obliterate: the fields; LoadTuning, the three values and the curve at a few times; TuningReset; the filter's function over a few types; Init through the view's slot 0x64, the fields after it, the filter object, the list of blocks; BlockHasOccupants over every block; a key; the frame message in states 4 and 0 (set by hand; in state 1 it would demolish); Post with a type nobody takes; Escape, which has the view put the tool away before any frame message reaches it.
3. The commit leg, for a free brush and for a paid one, in one hook: OnMouseDownL on the ground (the first application), OnMouseMove to a second point (an application), a second move (not taken before a message), three frame messages (the third queues the last point again: an application), OnMouseExit, OnMouseUpL. Then a press at a third point, with the view putting the tool away in the scrub. The test sets +0xD8 before each application, so every application has the plain strength and the stopwatch is never read. Shutdown leaves the frame service to OnMouseUpL, so the test takes the tool off it.
4. The obliterate leg: a second tool is inited, 50000 microseconds at a time are added to its time and SweepTick is called until the finale begins (state 2), then FinaleTick with the tuning's time removes, demolishes and dezones everything (state 3), and the view puts the tool away. All of it in one hook: no frame message reaches the tool.

### Rules the test keeps

- **rand().** Init shuffles the block list with the C runtime's `rand()`, which the simulation shares. In the leg without the commit, the test saves the generator's state before Init and puts it back after, so the tool leaves no trace in the simulation's sequence.
- **Audio messages.** The obliterate Init posts 0xAA650861 and 0x2A611B51 to the audio code in every leg, as the game does. They are the same on both sides.
- **Caps Lock.** The OnMouseMove in a scrub asks the real keyboard for Caps Lock. Run the commit leg with it off; with it on, both sides take the level plane and the terrain result changes.
- **After put-away, no key.** OnKeyDown on a terraform tool the view has put away crashes the original (see "Behaviour as the game has it" above), so the test does not send one.

### What the token holds

The hashes leave out every pointer (only whether it is set), the two stopwatches, the order of the shuffled blocks and the count of empty blocks in a row at the end (both follow rand()). These are on the log.

**Finding: a prop that comes and goes.** On Making Money d0090 the original itself differs from run to run in whether one prop (type 0xA823821E, block 0x37F at the east edge) exists: 120 or 121 blocks have occupants (3 of 8 extra runs of the original had it). The token therefore does not hash which blocks have occupants. It hashes how many blocks BlockHasOccupants answers the same as the test's own occupant-manager query (1024 of 1024 on the tutorial cities). The list of blocks with occupants is on the `tools5: varies:` log lines.

### Not reached

- The terraform DoMessage's two view messages (they ask the window manager where the real pointer is), CursorPoint's Caps Lock and level plane path, Apply's time-scaled strength and the budget refusing, OnMouseDownL with funds at 0.
- The obliterate tool's End (0x0049CC30), AfterWipe (0x0049CE20), the 0xEA623590 branch of DoMessage, and its dispatch in states 1 to 3.
- No coverage run was made.

### Trial results

2026-10-13, 7 days on Making Money d0090, Big City d0000 and Rush Hour d0090, four cases (the test alone, the test switch set to a day that never comes as the unchanged-city reference, the test with the commit, the test with the wipe), each as the original twice, with all groups ours and with only the T5 groups ours. 48 runs, all equal in every cell. The commit leg: the paid scrub spends 30 and the second press 10. The obliterate leg: 20 blocks swept, then the wipe; lots 429 to 16, 3569 to 12, 451 to 13 on the three saves.

Parity (batch t5b1, 7 days): plain 11 of 12 and 1 failed, test 11 of 12 and 1 failed, test with commit 11 of 12 and 1 partly, test with obliterate 12 of 12, plopped with commit 3 of 3. The three odd pairs were all a d0365 save and none repeated: in the plain leg a run with our groups exited silently while Making Money d0365 loaded, before any day ran; in the test leg a run of the original died on the first day of Rush Hour d0365; in the commit leg on Rush Hour d0365 only the prop developer's list differed, in the known swapped pair 0x2977AA47 and 0x2977AA49. Each of the three pairs was run again on the same build and was the same on its first pair. All 81 logs of the legs with the test say the message 0xEA65224E came 0 times.

## The disaster tools

Three classes the view makes when the player starts a disaster from the disaster menu: the effect trigger tool, the common disaster tool built on it, and the fire (torch) disaster tool. 33 functions in three groups, off by default, PostAppInit. Nothing in the three ranges draws and nothing was fenced. The disaster managers they talk to are described in [disasters.md](disasters.md).

The maker is the view's StartDisaster (0x007E9DB0, ours, `ui_cityview_2.cpp`), run from the view command 0x4A2B2464 with the disaster's ID:

- ID 0xA9DE8784 makes the torch tool.
- Any other ID loads the exemplar {0x6534284A, 0x29B6C670, id} and makes the common disaster tool. If property 0x4A08C700 exists, SetEffectName (0x004BB590) gets it; otherwise SetEffectNumber (0x004BB160) gets property 0x4A08C702. Then SetDisasterType (0x00580460) is called if property 0x4A08C701 is not 0.

The effect trigger tool is never made by itself.

### The effect trigger tool

0x004BAF60 is the constructor (our name `SC4EffectTool::Construct`); main vtable 0x00A902E0 with 31 slots; code 0x004BABE0 to 0x004BB8FD; 13 functions; group `ui.tools.disaster_effect` (src/core/tool_effect.cpp). The constructor builds the first 0xA4 bytes of the common disaster tool's 0xB4.

It holds five effects from the effects manager, found by name plus suffix or by number and kind:

| Offset | Suffix | Kind | Use |
| --- | --- | --- | --- |
| +0x90 | `_button` | 5 | started when the tool takes it |
| +0x94 | `_control` | 7 | what slot 30 (Switch) starts and stops |
| +0x98 | `_single` | 4 | started again at each press |
| +0x9C | `_active` | 1 | runs during a press |
| +0xA0 | `_inactive` | 2 | follows the pointer between presses |

When the manager knows the name, a one-shot `_trigger` effect (kind 3) also runs at each press.

- **Press (OnMouseDownL).** Picks the ground, captures the mouse and records at +0x2A whether a control effect exists and is not running.
- **Release (OnMouseUpL).** Calls slot 30 with 1 when +0x2A is set, otherwise slot 30 with 0 when modifier bit 2 is set.
- The tool touches no city state. The object layout is in the layout comment of the source.

| Function | Address | Vtable slot |
| --- | --- | --- |
| Init | 0x004BABE0 | 3 |
| Shutdown | 0x004BAC30 | 4 |
| OnMouseMove (ret 0xC) | 0x004BAD30 | 20 |
| OnMouseUpL (ret 0xC) | 0x004BAE20 | 18 |
| OnMouseExit | 0x004BAF00 | 22 |
| Switch (ret 4) | 0x004BAF20 | 30 |
| Constructor | 0x004BAF60 | |
| Destructor body | 0x004BB0B0 | |
| Deleting destructor | 0x004BB140 | 26 |
| SetEffectNumber | 0x004BB160 | |
| Trigger (ret 4) | 0x004BB360 | |
| OnMouseDownL (ret 0xC) | 0x004BB410 | 16 |
| SetEffectName | 0x004BB590 | |

### The common disaster tool

Vtable 0x00A9EAA0 (31 slots), object 0xB4 bytes, constructor 0x00580220, code 0x00580220 to 0x005804F4, 7 functions, group `ui.tools.disaster` (src/core/tool_disaster.cpp). It calls the effect tool's functions by address, so it works with or without `ui.tools.disaster_effect`.

- Init takes the effect disaster's manager (0x6A7B4397).
- Slot 30 with 1 asks the manager for its effect part (0xCA7B3F3C) and calls the part's slot 3 with the control effect, the place and the type. The result is an effect disaster instance, kept at +0xB0, and the disaster layer is told that a disaster began. Slot 30 with 0 ends that instance.
- With a type given by the view (+0xA4), every successful release also calls the type manager's slot 6 with the pick's x and z. They are in metres, truncated to integers, where the manager takes cells. A run confirmed it: with the pointer at 0.5 m and 62.7 m, the "cell" was 0,62. Kept as the game has it.
- Shutdown drops the control effect without stopping it (kept).

| Function | Address | Vtable slot |
| --- | --- | --- |
| Constructor | 0x00580220 | |
| Init | 0x00580290 | 3 |
| Shutdown | 0x00580300 | 4 |
| OnMouseUpL (ret 0xC) | 0x00580370 | 18 |
| Switch (ret 4) | 0x005803D0 | 30 |
| SetDisasterType | 0x00580460 | |
| Deleting destructor | 0x005804B0 | 26 |

The shipped exemplars of group 0x29B6C670 (12 of them):

| Instance | Disaster | Type | Effects |
| --- | --- | --- | --- |
| 1 | volcano | 0xC4C90997 | control, active, inactive |
| 2 | earthquake | 0x42963812 | active, inactive |
| 3 | by effect number 1 | 0x4A1332E8 (pipe burst) | none |
| 4 | by effect number 1 | none | none |
| 5 | MeteorStrike | 0x6A6D0EE7 | control, active, inactive |
| 6 | tornado | 0x02963821 | control, active, inactive |
| 7 | LightningStrike | none | active, inactive (`_trigger` is known) |
| 0x0A760F65 | robot_attack | 0xCA9C3DA7 | control, active, inactive |
| 0x0C059A77 | tsaurus_attack | 0x6A6D0EE7 | control, active, inactive |
| 0x4A38206D | by effect number 1 | none | none |
| 0x4B9547F1 | ufo | 0x6A6D0EE7 | control, active, inactive |
| 0xC9B6B73D | fire tuning | | no tool |

Only the volcano, the earthquake and the pipe burst have a manager of their own type (+0xA4 = 1). For the others the type goes to the effect disaster.

### The torch tool

Vtable 0x00A9EB40 (30 slots), message target part at +0x28 with vtable 0x00A9EB30, object 0x80 bytes, constructor 0x005805C0, code 0x00580510 to 0x00580B46, 13 functions, group `ui.tools.disaster_fire` (src/core/tool_torch.cpp).

- Init queries the fire manager (0x621CDA33) for its part 0x49B43F57, kept at +0x2C.
- A press over ground lights the torch, subscribes the +0x28 target to the frame service, calls the part's slot 5 StartFireAt(cell x, cell z, byChance 0, forced 1, group 0) and the part's slot 9 with 1, and sets the cursor 0x0A08C54E.
- A move with modifier 0x10 lights a torch that is out. While it burns the tool keeps the pointer's cell at +0x40 and +0x44.
- Every frame message (0x86AD10EE) while burning calls StartFireAt(the kept cell, byChance 1, forced 1, group 0). How many fires a held torch starts therefore depends on real frame time.
- Release unsubscribes and puts the torch out. The tool never captures the mouse.

| Function | Address | Vtable slot |
| --- | --- | --- |
| DoMessage | 0x00580510 | 3 of the part at +0x28 |
| PlaySound (ret 0x10) | 0x00580550 | |
| Constructor | 0x005805C0 | |
| Deleting destructor | 0x00580630 | 26 |
| Init | 0x00580660 | 3 |
| SetBurning (ret 4) | 0x005806D0 | |
| MoveTorch | 0x00580760 | |
| DropTorch | 0x005807F0 | |
| Shutdown | 0x00580840 | 4 |
| OnMouseDownL (ret 0xC) | 0x005808B0 | 16 |
| OnMouseUpL (ret 0xC) | 0x005809D0 | 18 |
| OnMouseMove (ret 0xC) | 0x00580A30 | 20 |
| OnMouseExit | 0x00580B00 | 22 |

### Clocks and random numbers

- The stopwatch at +0x68 is real time. It feeds only a number passed to three sounds.
- A random generator at +0x48 is constructed from the clock and never used. Ours constructs it under `fixed_seed::SiteOverride(0x0058060F)`.

### Behaviour as the game has it

- The torch's press does not set the kept cell. Frame messages right after a press fire at the cell of the last drag, which is 0,0 in a new tool.
- The torch's destructor releases nothing.
- The common tool's metres-as-cells call and the unstopped control effect at Shutdown (above).

### Left the game's

- 0x00580500, a 5-byte jump to the base QueryInterface, shared with other classes. It is not hooked.
- The base class's functions (see "The base class"), the effects manager and the effects, the disaster managers, the fire manager and its part, the frame service, the sound service.
- No wrapper in `src/core/game_*.cpp` touches these ranges.
- Checks on the translation: ret N of every hooked function and of every virtual callee (the effect interface vtable 0x00AA1170, the effects manager 0x00A9F264, the city 0x00A87620), and no caller relies on ECX or EDX after any hooked function, so no naked thunks.

## The disaster tools: test

`OPENSC4_TEST_TOOLS6=<day>` (src/core/tool_t6_autotest.cpp, token ` tools6 <hash>` in the fingerprint line, details on the log lines `autotest: tools6: ...`). Without any further switch the test leaves the city as it was.

Switches:

- `OPENSC4_TEST_TOOLS6_PARTS=<bits>`: 1 the common tool, 2 the torch.
- `OPENSC4_TEST_TOOLS6_COMMIT=1` with `OPENSC4_TEST_TUTORIAL_OFF=1` changes the city. The camera-to-disaster option is turned off first.
- `OPENSC4_TEST_TOOLS6_LEGS=<bits>`: 1 torch, 2 effect disaster, 4 typed. The default is 7.
- `OPENSC4_TEST_TOOLS6_FRAME=1`: one hand-made frame message to a burning torch, log only.

What the parts do:

1. Common tool: enumerates the exemplar group and makes each of the 11 tools through the view's StartDisaster. It hashes the fields after making, then runs an off-ground move, press, release and exit, moves over a 17 by 16 screen grid, and puts the tool away through view slot 0x68. There is no press on the ground at this level: a press starts effects that, for the meteor, the lightning and the like, are the disaster itself.
2. Torch: the fields, moves, exit and release; MoveTorch, SetBurning and DropTorch by address; a frame message while the torch is not burning.
3. Commit leg, torch: a press and an immediate release until the layer counts a disaster (one forced fire).
4. Commit leg, effect disaster: with MeteorStrike, press and release make the instance and the layer count rises by 1. The test ends it the day after with a new tool's slot 30 with 0, so the fingerprint shows two token values.
5. Commit leg, typed: with the pipe burst exemplar.

### Finding: ending an effect disaster in the step that started it

Ending the effect disaster (slot 30 with 0) in the same test step that started it makes the ORIGINAL game exit silently within the day (Making Money d0090). Holding a reference to the instance did not cure it; ending a day later is fine. The cause is not established. The test ends it a day later and keeps one reference to the instance on purpose.

### Not reached

- Trigger (0x004BB360) and the press's trigger branch (only LightningStrike knows `_trigger`).
- The base's own slot 30 (0x004BAF20). The common tool overrides it, so the game cannot reach it either.
- OnMouseUpL's modifier-bit-2 branch (0x004BAE20) and a press that finds the control effect running.
- A typed start that starts something (the volcano and earthquake tools were made and moved, never released).
- The torch's DoMessage while burning, lighting by a move with modifier 0x10, and the kept cell.
- The destructors of a tool that is still inited.
- No coverage run was made.

### Trial results

2026-10-13, Making Money d0090, Big City d0000 and Rush Hour d0090, sides original twice, all groups ours, and only the base and the three disaster groups ours: 36 runs, all finished. The test alone and the plain leg were identical on all sides, and the city equals the never-run reference. The commit leg was identical on Big City and Rush Hour; on Making Money one run of the original differed from the other three in the `adv` part only, and its rerun matched.

Per save, the commit leg's torch fire was at cell 109,37 (Making Money), 21,98 (Big City) and 61,67 after 2 presses (Rush Hour). The pipe burst leg took nothing (the layer was unchanged) on Making Money and Big City and was skipped on Rush Hour, which has no ground point whose metres are cells.

Parity (batch t6b1, 7 days): plain 12 of 12, test 12 of 12, test with commit and the tutorial off 12 of 12, plopped with the test 3 of 3; every pair the same on its first run. The token was in all 54 logs of the legs with the test.

## The query tool, first piece

The query tool (`cSC4ViewInputControlQuery`) is the tool behind the Query button: the player clicks a building, a road or a Sim and a window tells them about it. It is the biggest tool class, 223 functions at 0x004C3730 to 0x004D767F, so it is rewritten in pieces Q1 to Q7. This is Q1: 57 functions at 0x004C3730 to 0x004C56F0. Two groups, off by default, PostAppInit, detours at the game's entries (the 166 functions still the game's call these by address):

| Group | Functions | File | What |
| --- | --- | --- | --- |
| `ui.tools.query1a` | 34 | src/core/tool_query_1a.cpp | the log line, the window control helpers, 12 panel fillers, the lot measures, two child walks, five map levels |
| `ui.tools.query1b` | 23 | src/core/tool_query_1b.cpp | the constructor, OnMouseUpL, methods, five occupant filter tests, three picks, two timer callbacks, the query sound, the helper classes |

57 is 56 of the 60 functions in the function list, plus 0x004C44E0, a QueryInterface of the window runner (vtable 0x00A90B6C, slot 0). Ghidra files that one as a tail of 0x004429B0, so it is not in the list. The shared layouts are in src/core/tool_query.h.

Object: 0xA0 bytes, main vtable 0x00A90A88 (30 slots), constructor 0x004C4590. The view makes it from its commands 0x6A935CF3 (kind 0, ID 0xC7AF928E) and 0x6A935CF4 (kind 1, ID 0xC7AF928F), second argument 0x32.

### Fields

| Offset | Meaning |
| --- | --- |
| +0x28 | message target part (vtable 0x00A90A78, DoMessage 0x004D7160) |
| +0x2C | the occupant asked about (0x004C3810 reads its property holder) |
| +0x30 | 16 bytes handed to 0x004CA460; the constructor does not set them |
| +0x40 | a dword, 0; the destructor does not let it go |
| +0x44 | an object (referenced) whose window OnMouseUpL hides |
| +0x48, +0x4C | two windows (referenced); 0x004C4630 shows one and hides the other |
| +0x50 | a cRZTimer of 0x28 bytes (vtable 0x00AD1F14) |
| +0x78 | byte, 1 from the constructor and from Init |
| +0x7C | three dwords, each a copy of the dword at 0x00B21858 |
| +0x88, +0x89 | bytes, 0; OnMouseUpL clears +0x89 |
| +0x8C | the constructor's first argument (0 or 1) |
| +0x90 | the constructor's second argument (0x32 from the view) |
| +0x94 | byte, 0; Init stores the view's slot 39 here |
| +0x98 | route mode, 3; 1 = all route cells but kind 6, 2 = only kind 6 (0x004C4AE0) |
| +0x9C | an object (referenced), let go by the destructor |

### Helper classes

| Class | Vtable | Size | Notes |
| --- | --- | --- | --- |
| Panel target | 0x00A90980 (6 slots) | 0xC | the target the tool gives a window of its own; count at +4, the tool at +8; slot 3 is the window's message handler 0x004CB900, slot 5 the deleting destructor 0x004C4570 |
| Window runner | 0x00A90B6C (7 slots) | 0x18 | runs a window of the query; constructor 0x004C5620; procedure part at +8 (IID 0x22E85D8E, vtable 0x00A90B58, slot 3 0x004D3F40), message target part at +0xC (IID 0x452294AA, vtable 0x00A90B48); window at +0x10 (referenced twice), an object at +0x14 set by slot 6 (0x004C4530) |
| Occupant filter | five, 0xC bytes each, made with the base constructor 0x005EA080 | 0xC | see the next table; slot 3 tests an occupant, slot 4 a type, slot 6 deletes |

| Filter | Vtables (main / serializable part) | Test |
| --- | --- | --- |
| askable | 0x00A909B0 / 0x00A90998 | 0x004C37B0 |
| building or network | 0x00A909E4 / 0x00A909CC | 0x004C39A0 |
| automaton | 0x00A90A18 / 0x00A90A00 | 0x004C3AB0 (slot 4) |
| group prop | 0x00A90A4C / 0x00A90A34 | 0x004C3AD0 |
| unmarked | 0x00A90B18 / 0x00A90B00 (made at 0x004D42A2) | 0x004C46C0 (0x004C37B0, but false when the property holder has property 0xCAA9AB92) |

### The panel fillers

The query windows are filled from one table at 0x00A92488: 0x7F function pointers, 24 of them null. The survey had called it "helper vtables 0x00A92488 to 0x00A92654"; it is one table. It is read at 0x004D3AB9, 0x004D3B06 and 0x004D3BF6. A control whose ID has bit 0x1000 is filled by entry (ID & 0xFFF). Each filler is cdecl (window, control ID, fill, occupant). With `fill` it puts the control's value into the window. Without it, it reads the control back; only 0x004C55A0 does that.

| Index | Address | Value |
| --- | --- | --- |
| 1 | 0x004C5180 | a float from service 0x00B43D78 (slot 10), through a place |
| 6 | 0x004C51C0 | a float from the police simulator (slot 25) |
| 12 | 0x004C5200 | the police share: slot 36 float times slot 28 number, the share left |
| 21 | 0x004C5310 | the lot share (0x004C3D70) at the occupant's first cell |
| 41 | 0x004C3E70 | the pollution simulator's slot 57 float times 100, truncated |
| 45 | 0x004C5350 | as 0x004C3F90 |
| 52 | 0x004C3F90 | a float from service 0x00B43DA0 (slot 45) |
| 58 and 109 | 0x004C42F0 | the plumbing simulator's float (service 0x00B43D94, slot 24) |
| 65 | 0x004C3FD0 | the share of the occupant's cell (service 0x00B43DA8, slot 35) |
| 94 | 0x004C3EE0 | the pollution simulator's slot 55 float over its slot 54 float, at most 1 |
| 122 | 0x004C55A0 | the historical check box (lot slots 43 and 44) |

0x004C3D70 and 0x004C5280 (the lot share with the city's slot 76 and slot 77 objects) and 0x004C3E10 (the city share) are called directly, not through the table.

### The other functions

| Function | Address | What |
| --- | --- | --- |
| Log | 0x004C3750 | variadic log line, formatted into 0x200 bytes (`_vsnprintf` 0x009EFBC1) and given to the object at 0x00B21850 when there is one; 207 callers |
| Share | 0x004C3B10 | (used, capacity): 0 without a capacity, 1 while no more than 0.8 is used, else what is not used over 0.2 of the capacity; result on the x87 stack |
| SetChildEdit, SetChildText, GetChildNumber, SetChildValue | 0x004C3B90, 0x004C3C00, 0x004C3C50, 0x004C3CA0 | window control helpers (window, child ID, value) |
| KindOfNumber | 0x004C40B0 | 2, 3, 8, 10, 12 give 3; 4 gives 1; 5 gives 2; else -1 |
| TypesOfKind | 0x004C4100 | the two occupant types of kind 1, 2 or 3 (0x4773FFE0 to 0x4773FFE5) |
| LotCapacity, LotPopulation, LotAverage, CellGradeOf | 0x004C4160, 0x004C53A0, 0x004C5490, 0x004C4210 | lot measures |
| LevelText | 0x004C4260 | the text 0x4A5FE266 below 33.3, 0x0A5FE260 below 66.6, else 0xAA5FE249, of group 0xEA5524EB |
| AttachChildren, DetachChildren | 0x004C4340, 0x004C4370 | the walks a window's slot 32 calls for each child |
| ShowOtherPanel | 0x004C4630 | shows one of the windows at +0x48 and +0x4C and hides the other |
| DebugKeysDown | 0x004C4700 | the application says yes and keys 0x10, 0x11, 0x12 are down |
| Level functions | 0x004C4750, 0x004C47F0, 0x004C4890, 0x004C4930, 0x004C4A30 | five map levels at a lot's cell: kinds 2 to 6, each through a service grid and a curve of the growth developer at +0x74 to +0x84 |
| RouteKinds, RouteTouches | 0x004C3860, 0x004C4AE0 | the kinds of a trip's route cells as bits; whether the record's cell or a route cell is inside a box |
| OnMouseUpL (ret 0xC) | 0x004C4C50 | slot 18: stops the timer, clears the view's text 0x2A456CB1, hides the window of +0x44, clears +0x89; true |
| PickOccupant, PickAutomaton, PickGroupProp | 0x004C4C90, 0x004C4D10, 0x004C4DC0 | the object at 0x00B43DD0 picks at a screen point with a filter (its slot 65) |
| TimerPost, TimerSend | 0x004C4E70, 0x004C4EE0 | the two timer callbacks: post message 0x0A456D95 to the target part, send 0x0A456D96 to all |
| QuerySound | 0x004C4F40 | the sound of asking: property 0x0A902434 on a lot of condition 4, 0xAA905AB9 on condition 2 or 3, else 0xAA1DD397 |
| SetObject, End | 0x004C4530, 0x004C5690 | window runner slot 6; the runner's end (window manager slot 9) |

### Clocks and random numbers

- The cRZTimer at +0x50 is on real time. The constructor makes it and OnMouseUpL stops it. The two timer callbacks post 0x0A456D95 and send 0x0A456D96.
- No random numbers are drawn.

### Left the game's

- 0x004C3730: a folded float compare, also called from 0x006B9F53 in another class.
- 0x004C37A0: a wrapper of `_CIpow`; its only caller is 0x008993BB.
- 0x004C38D0: the object is in ESI, and only the overlay builders (drawing) call it.
- 0x004C5040: the bar blit (drawing, `GZBuffer::TileBlit`).
- The destructor (0x004C5730 and 0x004C5750) is past the range: Q2.
- The survey's idea that the range holds containers was wrong: there are none.
- No wrapper in `src/core/game_*.cpp` touches this range.

### Mods

No mod patch site is inside a Q1 function. We hook entries, so patched calls still win:

- sc4-query-ui-hooks patches the calls at 0x004D747F and 0x004D74BF (to 0x004C3810) and 0x004D4406 (to 0x004C4F40).
- sc4-advanced-water-management changes the table word 0x00A92570, which is filler index 58 (control 0x103A). Its shipped content, 0x004C42F0, is ours. We never write the table, so the mod's replacement wins.
- Not run with mods installed. The unattended test cannot reach those call sites before Q7.

### Behaviour as the game has it

- Panel filler 1 (0x004C5180, service 0x00B43D78 slot 10) makes the game compute and cache a lot's road access in the lot's map at +0x40. That is not saved, but it is part of the citylot fingerprint. So the test runs it only in the commit leg, and after the commit leg the city differs from an untouched run in the citylot part only, in the original too.

## The query tool, first piece: test

`OPENSC4_TEST_Q1=<day>` (src/core/tool_q1_autotest.cpp, token ` query1` in the fingerprint line, sub-hashes on the log lines `autotest: query1: ...`, and `varies:` lines for the debug keys 0x004C4700 and the automaton picks, which differ between runs). It calls only the game's addresses and vtable slots, never our functions. Without more switches it leaves the city as it was.

Switches:

- `OPENSC4_TEST_Q1_PARTS=<bits>` (default all): 1 pure, 2 filters, 4 lot measures, 8 fillers, 16 window helpers and the log line, 32 object, 64 picks, 128 small classes.
- `OPENSC4_TEST_Q1_FILLERS=<bits>`: a mask over the twelve fillers, default 0xFFE; 0xFFF with the commit leg.
- `OPENSC4_TEST_Q1_COMMIT=1`: sets the historical flag on one building's lot through 0x004C55A0 with fill 0, reads it back and sets it back.

What the parts do:

1. Pure: KindOfNumber, TypesOfKind, Share over a grid, LevelText for seven levels.
2. Filters: the five filters made as the game makes them; slot 3 over a fixed sample of occupants and slot 4 over ten type IDs.
3. Lot measures: for up to 24 buildings, the lot measures and the five map levels.
4. Fillers: on a stand-in window that keeps what a filler stores, the table's reader 0x004D3A90 with fill 1.
5. Window helpers: the control helpers with and without a window, the two walks, and Log with and without a sink at 0x00B21850.
6. Object: the constructor for both kinds, 0x004C3810, RouteKinds and RouteTouches on a hand-made trip record, then the tool taken up through the view's command handler 0x007F1C10 with 0x6A935CF3 and 0x6A935CF4, Init, one OnMouseUpL, and put away. All within one tick, so the timer cannot come.
7. Picks: over a grid of screen points.
8. Small classes: the panel target's destructor and the window runner on a counting stand-in.

### Translation bugs the test caught

- 0x004C4260 had reversed `test ah,5 / jp`: 33.3 must give the middle text.
- 0x004C3B10 used `>` for `>=`.
- The unmarked filter's vtable address was wrong. That made the ORIGINAL exit silently in the test.

### Not reached

- 0x004C5690 (it ends a modal run) and the runner's last release.
- Delivery of the two timer messages.
- Routes from real trips.
- Any use with mods installed (see "Mods").

### Trial results

Second pass, snapshot q1c, 7 days, on Making Money d0090, Big City d0000, Rush Hour d0090 and the plopped p0000: the original twice (A1, A2), all groups ours (B) and only `ui.tools.base` with the two Q1 groups ours (M) were the same in the test, the commit and the never-run cases. The city after the test alone equals the never-run city on all four.

Parity (batch q1b1, 7 days): plain 12 of 12, test 12 of 12, test with commit 12 of 12, plopped with the test and commit 3 of 3, every pair the same on its first run.

## The query tool, second piece

Q2 is 35 functions in the range 0x004C56F0 to 0x004C7A70 (41 functions in the function list: 6 stay the game's, see below). One group, off by default, PostAppInit, detours at the game's entries: `ui.tools.query2`, src/core/tool_query_2.cpp. Layouts are in src/core/tool_query.h (the field comments for Init's fields, the text entry, Init's layout IDs). Q3 follows (below). The remaining 76 functions of the query tool (Q4 to Q7) are still the game's and call these by address.

| What | Functions | Addresses |
| --- | --- | --- |
| Init, destructor | Init (slot 3), the deleting destructor (slot 26) and the destructor it calls | 0x004C57A0, 0x004C5730, 0x004C5750 |
| ShowTip | the info tip's text and place | 0x004C5BA0 |
| Map levels | two, pollution and garbage | 0x004C5C90, 0x004C5D70 |
| Window helpers | the edit's text, a number, a percent, money, money one control on, the slider | 0x004C6B10, 0x004C6BF0, 0x004C6CA0, 0x004C6D50, 0x004C6E00, 0x004C6E30 |
| Panel fillers | 16, with their table indexes below | 0x004C67F0, 0x004C70C0 to 0x004C7660, 0x004C7910, 0x004C7980 |
| Airport and seaport helpers | six, called by fillers and by 0x004CC240 | 0x004C7690 to 0x004C78B0 |
| Window runner | the deleting destructor (slot 3 of 0x00A90B6C) | 0x004C68E0 |
| Text entry | destroys the string of a (key, string) pair; 13 callers in 0x004D5xxx | 0x004C6AF0 |

### Init

Init (slot 3, no arguments, returns true, and at once if the tool is already inited) does this:

1. ID and cursor by kind (+0x8C): kind 0 gets ID 0xC7AF928E and cursor 0x816D7F74, kind 1 gets 0xC7AF928F and 0xCB8D5B54. Then the base's Init.
2. When the message server exists, the target part at +0x28 is registered for six messages: 0x0A456D95, 0x0A456D96 (the two timer messages), 0x26D31EC2, 0xA6B79602, 0x66956814 and 0x69247DC7.
3. The timer at +0x50 gets the callback 0x004C4E70 (TimerPost) with the tool. It is not started. The timer stays on real time, as in Q1.
4. +0x78 is set to 1, and +0x94 gets the view's slot 39.
5. A new info tip (0x1B4 bytes) replaces the one at +0x44. It gets the picture 0xA85F1A83, the text key 0x0A52F5C6 and the box -40, -40, 40, 40; its window is hidden and made a child of the view's window.
6. Kind 1 also loads the window 0x6BB92BCB of the layout {0, 0x96A006B0, 0xABB0120F} into the view's window. When it comes, a panel target (0xC bytes, vtable 0x00A90980) is made, and the layout's children 0x0BB0F5E7 and 0x6BB92BCA replace the windows at +0x48 and +0x4C. Each is taken out of the layout window and made a child of the view's window, gets the panel target as its target, and is shown or hidden. The first goes 2 from the view window's right edge and 2 from the top and is shown. The second goes next to the first, by its own width, and is hidden. Then the layout window is taken out of the view.

Nothing made is checked for null except the panel target. A tip that failed to build calls through null, as in the game.

### Panel fillers

These are entries of the table at 0x00A92488 described in the first piece (cdecl, window, control ID, fill, occupant).

| Index | Address | Value |
| --- | --- | --- |
| 0 | 0x004C70C0 | the building's name into a text edit; without fill a changed text renames the building |
| 2 | 0x004C7200 | a fire station's funding as a slider; without fill the slider's number over 100 is set |
| 3 | 0x004C72E0 | the third of a station's numbers from the fire simulator |
| 4 | 0x004C7350 | a fire station's cost as money |
| 7 | 0x004C7390 | arrests (police simulator slot 43) |
| 8 | 0x004C73D0 | crimes (slot 44) |
| 9 | 0x004C7410 | a police station's funding as a slider; without fill it sets the share |
| 10 | 0x004C74F0 | the third of a station's numbers from the police simulator |
| 11 | 0x004C7560 | a police station's cost as money |
| 13 | 0x004C75A0 | the jail capacity (slot 28) times 0.8, truncated |
| 14 | 0x004C75F0 | the jailed share (slot 36) times the capacity, truncated |
| 15 | 0x004C7660 | the jail's upkeep as money (a tail jump to SetChildMoney 0x004C6D50) |
| 17 | 0x004C67F0 | the lot share's second value at the occupant's first cell (0x004C5280) |
| 22 | 0x004C7910 | the second number of the occupant's property 0x27812851 |
| 23 | 0x004C7980 | the residential simulator's school record, its float at +0x28 as the value |

Readers: only indexes 0, 2 and 9 (as well as Q1's 122) read the control back without `fill`; the rest answer true.

The six airport and seaport helpers take (window, control ID, fill, cell x, cell z) instead of an occupant: 0x004C7690 airport cost, 0x004C76F0 and 0x004C7760 the airport's two usage numbers, 0x004C77D0 and 0x004C7840 the seaport's two usage numbers, 0x004C78B0 seaport cost. The airport developer is city slot 77's object and the seaport developer city slot 76's. The game's own fillers reach them through the table at indexes 19 (airport cost, 0x004C9E60), 123 (0x004C9EA0), 124 (0x004C9EE0), 125 (0x004C9F20), 126 (0x004C9F60) and 120 (seaport cost, 0x004C9FA0), each at the first cell of the occupant's box; those are Q3 (below).

### Window helpers: SetChildSlider

SetChildSlider (0x004C6E30, 7 callers) takes (window, child ID, low, high, value, int64 cost, an argument it does not read, handler, table of IDs, count). The last two are a table of uint32 control IDs and its count. The game copies them into a window property value (a uint32 array, type 0x8006). The first reading of the translation had them as "two extra numbers". It sets the number control's range when low is below high, and the value; the properties 0x6A8CC9D6 (handler), 0x2AA6E8AD (cost) and 0x0AB3F6DB (the table copy, when the table is not null); and the money value times 0.01 times the cost plus 0.5 into the child ID plus 0x100.

### Left the game's

- 0x004C56F0 (a folded tree erase, `SC4Uint32FloatMap::EraseSubtree`) and 0x004C6920 (`SC4BuildingCellHash::Find`): generic containers, with callers outside the tool. They already have address entries in other files.
- 0x004C69A0 (`SC4LotTravelMap::CopySubtree`): a tree copy that audio code calls too (0x004C9470 and 0x004E3F80 are two of its callers).
- 0x004C5E20 (BuildRouteLine, seven arguments, ret 0x1C, one caller 0x004CA460): builds the route line's display geometry (calls 0x007DDB30 and 0x007DDC50). Drawing.
- 0x004C6830: the bar painter's message slot (slot 3 of the vtable 0x00A90B38). It is undefined code in Ghidra. On message type 0x16 it draws the window's value (property 0xEA563AB0) as a bar with 0x004C5040. Drawing. The painter is a 4-byte object the tool makes at 0x004CCAD7, 0x004CCDAF and 0x004D3D96 and gives to every child of a query window.
- 0x004C6A60 (LogDestination): the arguments are in registers (cell x in EBX, cell z in EDI, one stack argument). It only writes a debug log line.

Correction to the first piece: the one caller of DrawBar 0x004C5040 is the bar painter 0x004C6830, not the filler 0x004C67F0 (the address file said so; fixed in data/addresses/tools.toml).

Calls still waiting for later pieces (Q4 onward): DoMessage 0x004D7160 calls ShowTip, 0x004D4FD0 calls the two levels, 0x004D3F40 calls SetChildMoneyNext, and the overlay builder 0x004CA460 calls 0x004C5E20. (Q4 made 0x004CA460 ours: it is route selection, not drawing; 0x004C5E20 stays the game's.)

### Behaviour as the game has it

Some game functions read locals they never set. Ours set them to 0, which is a reading, not something the game does the same way every run:

- The fire and police cost fillers' cost, when the getter fails (the building is not a station).
- The station filler's three outputs.
- The port usage helper's other place.
- The funding slider's number when the window has no child.
- The school record's unset words.
- The bounds of the two levels.

The test only runs these on real stations, or with its stand-in's child present, so the game's stack leftovers do not reach the hashes. Where the game keeps a local in an argument slot, ours mirrors that: ShowTip's mouse point starts as the two string pointers, and the cost out and the shown place of the cost and usage fillers start as the fill dword.

Every function here calls virtuals, so no caller can keep ECX or EDX across them (the register scan agrees). If a run dies in a query window after this group is on, drop the hook of 0x004C6AF0 first: it has 13 callers.

No wrapper in `src/core/game_*.cpp` touches this range, and no mod patch site is inside it.

## The query tool, second piece: test

`OPENSC4_TEST_Q2=<day>` (src/core/tool_q2_autotest.cpp, token ` query2` in the fingerprint line, order 22, sub-hashes on the log lines `autotest: query2: ...`). It calls only the game's addresses and vtable slots, never our functions. Without more switches it leaves the city as it was.

Switches:

- `OPENSC4_TEST_Q2_PARTS=<bits>` (default 31): 1 helpers, 2 fillers, 4 levels, 8 object, 16 small classes.
- `OPENSC4_TEST_Q2_COMMIT=1`: renames the first named building and puts the name back (filler 0 with fill 0); sets the first fire station's and the first police station's funding to 50 (fillers 2 and 9 with fill 0), then restores the exact share they had through the simulator's own setter.

What the parts do:

1. Helpers, on stand-ins: the number, percent and money helpers over fixed numbers, with and without a window or child; the slider with a range, without one, with and without the table of IDs (the number control's calls, the three property values, the money text); the edit's text.
2. Fillers through the table's reader 0x004D3A90 with fill 1: indexes 0, 7, 8, 13, 14, 15, 17, 22, 23 and the port fillers 19, 120, 123 to 126 for up to 24 buildings; 2, 3, 4 for up to three fire stations and 7 to 11 and 13 to 15 for up to three police stations. A station is a building the simulator's funding getter knows: on anything else the game reads unset stack. A save without stations skips them and logs it.
3. Levels: the two levels at the first cell of each building's lot.
4. Object: the tool made as the view makes it for 0x6A935CF3 and 0x6A935CF4 (Init): the fields, whether the tip and the two windows are there and shown, their places; the tip at the pointer hidden, shown and hidden again (whether it shows, never where: that is the mouse); then the view puts the tool away (the destructor).
5. Small classes: the window runner made on a counting stand-in and deleted through its slot 3; a text entry destroyed.

### Not reached

- Police stations: only Making Money d0090 has any.
- No save has a port, so the airport and seaport helpers meet no lot with a port (they answer false).
- The tool's own destructor runs after the test's tick, so the test only sees it as far as the view's put-away.
- Delivery of the timer messages (as in Q1).
- Any use with mods installed.

### Trial results

First pass, snapshot q2b, on Making Money d0090, Big City d0000 and Rush Hour d0090: the original twice (A1, A2), all groups ours (B) and only `ui.tools.base` with `ui.tools.query2` ours (M) were the same in the whole test and fingerprint lines, in the test, the commit and the never-run cases. The test without commit leaves the city as the untouched run on all three. Build debug and release clean, ctest 20 of 20, retcheck 33 OK and 2 tail jumps (0x004C5750, 0x004C7660), register scan and EAX scan clean. One pass sufficed; nothing moved out of the hash.

Parity (batch q2b1, 7 days): plain 12 of 12, test 12 of 12, every pair the same on its first run. The commit legs (12 saves, and 3 plopped) had every day's fingerprint identical on every pair, but run-parity called them different: the renamed building keeps its name in property 0x899AFBAD, a string (type 0x800C), and the save decoder (`tools/re/sc4save.py`) stopped on it in both saves. With string properties decoded (`tools/re/save_schemas/citylot.py`), every record of all 15 first pairs is identical, judged again from the saved runs: test with commit 12 of 12, plopped with the test and commit 3 of 3. Renaming a building back to its old name leaves the name property in the save (the original does the same).

## The query tool, third piece

Q3 is 55 functions in the range 0x004C7A70 to 0x004CA460 (59 in the function list: 4 stay the game's, see below). Two groups, off by default, PostAppInit, detours at the game's entries: `ui.tools.query3a` (33 functions, 0x004C7A70 to 0x004C8E2E, src/core/tool_query_3a.cpp) and `ui.tools.query3b` (22 functions, 0x004C8E30 to 0x004CA45D, src/core/tool_query_3b.cpp). All of it is panel fillers of the table at 0x00A92488 (cdecl, window, control ID, fill, occupant) and their helpers, so the layouts of Q1 and Q2 hold. New layouts are in src/core/tool_query.h: the school record (`school_record`), the transit record (`transit_record`), the growth developer's fields (`developer`) and the building development service's lock hash (`development`). The remaining 76 functions (Q4 to Q7) are still the game's and call these by address.

### The fillers by area

| Area | Table indexes | Addresses |
| --- | --- | --- |
| Schools and hospitals (funding, capacity, grades) | 24 to 37 | 0x004C7A70 to 0x004C81B0 |
| Garbage buildings | 42, 43, 44, 51, 95, 96 | 0x004C8330 to 0x004C8550, 0x004C85F0 |
| Power plants and producers | 46 to 49, 53 to 56, 97, 98, 99, 118 | 0x004C8660 to 0x004C8760, 0x004C9FF0, 0x004CA040, 0x004CA2C0 |
| Water buildings | 59, 60, 61, 62, 64, 115, 119 | 0x004C8880 to 0x004C89F0, 0x004CA090, 0x004CA390 |
| Transit switches | 66, 67, 68, 69 | 0x004C8A60 to 0x004C8C50 |
| Budget items | 70, 71, 73, 108 (16) | 0x004C8CE0 to 0x004C8DB0, 0x004C9310 |
| Lots, ports and texts | 19, 74, 81, 82, 86, 88, 92, 120, 123 to 126 | 0x004C8FF0 to 0x004C92A0, 0x004C9E60 to 0x004C9FA0, 0x004CA130 |

Table entries that share a function: 26 and 35 are 0x004C8160; 27, 29, 31 and 36 are 0x004C7CE0; 28 and 30 are 0x004C7D30; 47 and 54 are 0x004C86D0; 48 and 55 are 0x004C8710; 49 and 56 are 0x004C9FF0; 97 and 98 are 0x004CA040. Two entries are five-byte jumps in the table's own code: 16 (0x004C9E50, to 0x004C9310, which is index 108) and 46 (0x004C9FE0, to 0x004C8660, which is index 53). Both jump targets are hooked, so the jumps need nothing. Q2's port entries are the same ones: 19, 120 and 123 to 126 (0x004C9E60 to 0x004C9FA0) call Q2's six helpers 0x004C7690 to 0x004C78B0, checked against the table.

Not table entries, all called only from the window filler 0x004CC240 (a later piece; the arguments are the window, a control ID or the box's first cell, the fill and the box): 0x004C8290 and 0x004C82F0 (garbage, five cdecl arguments), 0x004C8E30 (a neighbour's mayor and city name), 0x004C8F20 (the traffic at a cell) and 0x004CA1E0 (six cdecl arguments pushed).

0x004C8D40 (filler 71) and 0x004C8DB0 (filler 73) are two functions, not one: 0x004C8DB0 is not a function in Ghidra's list. 0x004CA130 (filler 74) is undefined code in Ghidra too; it is reached only through the table, and the hook needs only the expected bytes.

0x004C9520 is not drawing, though the cell log (0x004D12A0) is the only caller. It is the cell log's desirability dump: for a type (0x1010, 0x1020, 0x1030, 0x3110, 0x3120, 0x3130, 0x3320, 0x3330, 0x4100, 0x4200, 0x4300 or 0x4400) and a cell it asks the growth developer of the type for each map it reads and logs one line per curve. It reads the simulator's day number (the simulator service's slot 7, then its slot 26: sim time) and makes four static strings (Air, Water, Garbage, Radiation, 0x14 bytes each at 0x00B21898) on its first call through the game's own guard (bit 0 of 0x00B218E8) and exit handler (0x00A7B120). Whichever side's code runs first makes them, once.

### Fields and records

- The school record (school_record, 0x40 bytes) is what the residential simulator fills for a school (its slot 26) or a hospital (slot 27): a game string, then a number at +0x24, a float shown as a value at +0x28, the first funding share at +0x2C with its cost at +0x30, the second share at +0x34 with its cost at +0x38. The fillers set the string, +0x28, +0x2C and +0x34 before the call. Shares and costs go back through the simulator's slot 19 (the occupant, the first share, the second share, 1 for a school or 0 for a hospital).
- The transit record (transit_record, five dwords) is what the traffic simulator's slot 35 fills for a transit switch from a cell. The query sets only +0x8 before the call.
- The growth developer (developer) is the building development service's object for a type (service 0x00B43D68, slot 32) or 0x004D32D0's for a lot. It holds one curve per effect (land value, slope, the four pollution types, parks, the three wealth proximities, the two commercial maps, school, hospital, crime, trips, traffic, landmarks, transient) and four tract maps (desirability at +0x114, population +0x118, capacity +0x11C, maximum capacity +0x120). A null curve is an effect that is off; each curve is evaluated (0x005F4250) at what its map holds at the cell. The development service (development) has the lock hash at +0x9C (key: developer, tract x, tract z, a byte 1 to 8; the value at the node's +0x14 is the day the lock was set) and the lock length in days at +0xBC.

### Behaviour as the game has it, kept

- The hospital ID table at 0x00B096F8 holds a count of 4 but only two IDs. The slider copy that uses it copies two more dwords (the 4 and 0x3F800000), and ours does too.
- Some functions read locals they never set. Ours zero them, which is a reading, not a promise the game keeps from run to run: the residential slot 28's two outputs (fillers 26, 27, 29, 31, 35, 36), the school record's unset words, GetChildNumber's output when there is no window or child (fillers 24, 25, 33, 34 and 99 with fill 0), plumbing slot 24's amount used when the property 0x08D9763D is missing (59, 62), and the budget item when slot 51 finds none (71 and 73; 73 then asks department 0 and calls through what comes back, as the game). The test calls these only where the callee writes the value, with two sentinels first.
- Where the game keeps a local in an argument slot, ours mirrors it: 47 and 54 (power slot 34), 59 to 62 (plumbing slot 24) and 97 and 98 (power slot 46) start from the `fill` dword. Filler 64's flag clears only the low byte; ours is a fresh byte and only that byte is read.
- Converting a float that is out of range or not a number to an integer gives 0x8000000000000000, low dword 0, as the x87 does. Filler 92 divides by a capacity that can be 0.
- Five fillers change the city when a window reads its control back (fill 0): 24 and 25 (a school's two shares) and 33 and 34 (a hospital's) through the residential simulator's slot 19, and 99 (a power plant's funding) through the power simulator's slot 51. Fillers 26 to 31 and 35 to 37 with fill 0 only answer, some after asking a getter (slots 26, 27 and 28). Nothing else here changes the city.
- Nothing reads a clock or draws a random number. No mod patch site is inside the range: 0x004D4406, 0x004D4494, 0x004D4EAA, 0x004D7335 to 0x004D74BF, 0x004D09D1 and the word at 0x00A92570 are all outside, and no patched call targets a Q3 function (they target 0x004C4F40, 0x004D3C30, 0x0090F574 and 0x004C3810). No wrapper in `src/core/game_*.cpp` and no return-address test touches the range.

### Left the game's

- 0x004C9430: a generic list's copy constructor (thiscall, ret 4); its callers are 0x004D12A0 and 0x0051A7D0, the latter outside the tool.
- 0x004C9470: `SC4LotTravelMap::CopyConstruct`, already ours in the lots' code (citylot).
- 0x004C9E50 and 0x004C9FE0: the five-byte jumps of fillers 16 and 46 (above).
- The static strings' exit handler 0x00A7B120 is also the game's; only its address is passed to atexit.

If a run dies in a query window after these groups are on, drop the hooks of 0x004C9310 (a vector and a budget loop) and 0x004C9520 first.

## The query tool, third piece: test

`OPENSC4_TEST_Q3=<day>` (src/core/tool_q3_autotest.cpp, token ` query3` in the fingerprint line, order 23, sub-hashes on the log lines `autotest: query3: ...`). It calls only the game's addresses and vtable slots, never our functions. Without more switches it leaves the city as it was.

Switches:

- `OPENSC4_TEST_Q3_PARTS=<bits>` (default 255): 1 schools and hospitals, 2 garbage (and the helpers 0x004C8290 and 0x004C82F0), 4 power, 8 water, 16 transit, 32 budget, 64 lots, ports and texts (and 0x004C8F20 at a few cells, 0x004C8E30 and 0x004CA1E0 over the cells just outside the city's edges for the connection types 0 to 12), 128 the desirability log.
- `OPENSC4_TEST_Q3_COMMIT=1`: fillers 24 and 25 on a school and 33 and 34 on a hospital with fill 0 and a stand-in slider at 50 and then 30, then both shares the record had put back through the residential simulator's slot 19; filler 99 on a power plant, its share put back through the power simulator's slot 51.

Every filler is called through the table's reader 0x004D3A90 with fill 1 on Q2's stand-in window (text, number and value controls), and with fill 0 where that does not change the city. The desirability log runs for the twelve types the cell log uses at three cells, through a stand-in sink (as in Q1's test). Fillers that read stack leftovers are called only where the callee writes (sentinel checks first). Pointers are not hashed, only whether they are set.

### Not reached

- Big City d0000 has no school, hospital or water producer; Rush Hour d0090 has no hospital. The sample per save (first run, test): Making Money d0090 1 school, 1 hospital, 2 capacity-safe buildings, 1 plant, 1 producer, 1 budget item, 1 neighbour place; Big City d0000 3 plants, 1 item, 13 neighbour places; Rush Hour d0090 1 school, 3 plants, 2 producers, no item, 6 neighbour places. The desirability log gave 972 lines (3 cells, 12 types).
- Any use with mods installed.

### Trial results

First pass, snapshot q3a, on Making Money d0090, Big City d0000 and Rush Hour d0090, each with the test, with the test and commit, and never run: the original twice (A1, A2), all groups ours (B) and only `ui.tools.base` with the Q3 groups ours (M) were the same in the whole test and fingerprint lines, in all nine cases (36 of 36 runs finished, none crashed). The fingerprint without the token is the same in the untouched, test and commit runs on all three saves (the commit puts everything back). Build debug and release clean, ctest 20 of 20, retcheck 54 OK and 1 to check (0x004C9520: the function list's size stops before its plain ret at 0x004C9E46; it is cdecl), register scan 55 hooked and none suspicious, EAX scan clean. One pass sufficed; nothing moved out of the hash. One sweep stopped at the wait script's 20-minute budget (other threads' games running) and was resumed; finished runs were skipped.

Parity (batch q3b1, 7 days): plain 12 of 12, test 12 of 12, test with commit 11 of 12 plus 1 partly, plopped with test and commit 3 of 3, every same pair on its first run. The partly one is Making Money d0365: the first B run differs from day 1 in the advisor part (`adv`) only, the day before the test runs; the two retried pairs (b1, b2) equal every A run. Not Q3 code (nothing of the piece runs before the test day); left as a one-off of that part.

## The query tool, fourth piece

Q4 is 25 functions in the range 0x004CA460 to 0x004CC6E0 (24 in the function list plus filler 107 at 0x004CB880, which the list had not split from the tail of the lister 0x004CB740). Two groups, off by default, PostAppInit, detours at the game's entries: `ui.tools.query4a` (11 functions, src/core/tool_query_4a.cpp: the route lines, the highlight, the kind 1 window's message handler and three of the tool's slots) and `ui.tools.query4b` (14 functions, src/core/tool_query_4b.cpp: panel fillers and the two child walks). Everything in the range is ours; nothing is left the game's. The remaining 51 functions (Q5 to Q7, 0x004CC6E0 to 0x004D767F) are still the game's and call these by address.

| Address | Name | What it does |
| --- | --- | --- |
| 0x004CA460 | BuildRoutes (ret 4) | Draws nothing itself: decides which routes the line gets (below) |
| 0x004CAA60 | LotRoutes | The box for a building's lot, then BuildRoutes |
| 0x004CAAE0 | NetworkRoutes | The box for a network piece, then BuildRoutes |
| 0x004CAC30 | RebuildRoutes | Empties the line's points and builds again |
| 0x004CAC50 | ShowRoutes | Takes the old line off, then LotRoutes or NetworkRoutes by the occupant's type (tail calls) |
| 0x004CACC0 | HighlightLot (ret 4) | Highlights or restores the occupants of the picked occupant's lot |
| 0x004CADE0 | FillLotNetworkNumber | Filler for the lot's network number |
| 0x004CAF80, 0x004CB140, 0x004CB340, 0x004CB4E0 | FillNeighbourDealText, Amount (7 arguments, float last), Cost, Kind | Neighbour deal fillers |
| 0x004CB740, 0x004CB880 | FillMayorRatingCurve, FillOccupantMayorRating (filler 107) | Mayor rating through a curve property |
| 0x004CB8B0 | EraseTexts (ret 4) | Frees a subtree of the query's text map |
| 0x004CB900 | SC4QueryPanelTarget::OnMessage (ret 8) | The kind 1 window's message handler |
| 0x004CBA80, 0x004CBC70, 0x004CC0B0 | Shutdown, OnMouseMove (ret 0xC), OnMouseExit | Slots 4, 20 and 22 of the tool |
| 0x004CC120, 0x004CC160 | FillOccupantLotNetworkNumber (filler 18), FillCityMayorRating | Fillers |
| 0x004CC1D0, 0x004CC210 | FillNeighbourDealScaled, FillNeighbourDealCount | Deal amount with a scale |
| 0x004CC240 | FillCell | The filler for a cell: a switch over 22 control IDs |
| 0x004CC660, 0x004CC6A0 | FillCellChildren, ReadCellChildren | Child walks (with fill, without) |

The thiscall functions are hooked as `__fastcall` with a dummy EDX; the fillers are cdecl. Filler 18 (0x004CC120) and filler 107 (0x004CB880) are the only entries of the filler table at 0x00A92488 inside Q4.

### The overlay builder

0x004CA460 was expected to be drawing. It is not: it is route selection logic, so it is ours. It picks the lots (the lot manager's slot 15 iterator) and the neighbour connections (the neighbours simulator's slot 16) whose trips cover the box in the tool's +0x30 or whose route touches it (0x004C4AE0), reads the window's check boxes at +0x48 for the route flag and the mask of route kinds, makes the line object when there is none and sets its width by zoom (six floats; the zoom indexes them unchecked, as in the game). The drawing stays the game's and is called by address: the line object (0x007DDD50 construct, 0x007DCD90 set up, 0x007DDC30 empty, 0x0053C940 unset; the last is folded and has six callers in several classes), the route line 0x004C5E20 for each route, and the render service's slots 32 (take the line, layer 5, order 1000) and 33 (give it back). 0x004C38D0 takes the terrain in ESI, so it is called through a naked thunk. The four line functions are in the address file under SC4RouteLine (the Renderer's code).

### The main vtable slots

0x004CBA80, 0x004CBC70 and 0x004CC0B0 are slots 4, 20 and 22 of the tool's main vtable 0x00A90A88, not of 0x00A90A78 as the first notes said (0x00A90A78 is the target part; its slots are not these). 0x004CB900 is slot 3 of the panel target (vtable 0x00A90980).

### How the cell walks are reached

0x004CC660 and 0x004CC6A0 are given to a window's slot 32 (interface 0x22BA0121, a function and a cell query) by code in Q5: 0x004CCB3A and 0x004CCE1C give 0x004CC660, 0x004CCE93 gives 0x004CC6A0. 0x004CCB01 and 0x004CCDF2 also keep 0x004CC660 at 0x00B21848 (window at 0x00B2184C, cell at 0x00B21840 and 0x00B21844) for DoMessage (0x004D75B8, 0x004D75E4); 0x004D3DC0 stores 0x004D3B40 there instead. The walks always recurse into the children, because FillCell returns false. FillCell takes five arguments (window, ID, fill, x, z) for 13 IDs and six (window, ID plus the cell query's offset, fill, x, z, the cell query's type) for 9: 0x1011 to 0x1015, 0x1026, 0x1027, 0x1028, 0x1078 and 0x107B to 0x107E take five; 0x104B to 0x104E, 0x1066 to 0x1069 and 0x106A take six. The jump table is at 0x004CC588 with its index bytes at 0x004CC5E4 (control ID less 0x1011); the translation is a switch.

### Fields and layouts

New layouts in src/core/tool_query.h, no field renamed: the route line object (`route_line`: set up byte +0xC, width +0x10, points +0x14, 0x2C bytes), the trip node a traffic source keeps (`trip_node`: next, record from +0x8, flags byte at +0x26; routes with bit 8 set and bit 2 clear are drawn, the way back when bit 1 is clear), the text map's node (`text_node`: left +0x8, right +0xC, key +0x10, game string from +0x14) and the cell query (`cell_query`: x, z, ID offset, type; 0x10 bytes). Field comments of the tool now say what Q4 shows: +0x2C the occupant under the pointer (highlighted with 7), +0x30 the box, +0x40 the occupant's own highlight, given back when it is let go, +0x7C the ground point (three floats), +0x88 whether something was picked, +0x90 the timer's milliseconds (50 from the view), +0x94 whether the ground point counts for a pick, +0x9C the line.

### Behaviour as the game has it, kept

- BuildRoutes never gives back the lot manager's slot 15 iterator (8 bytes, registered in the manager's list), on every call.
- A kind 0 tool has no window at +0x48 and BuildRoutes calls through null there in the original too; only a kind 1 tool after Init is driven.
- Some locals are never set in the game and are 0 in ours: NetworkRoutes' position, FillOccupantLotNetworkNumber's cells, the deal fillers' two types, FillCityMayorRating's exemplar.
- The flag handed to 0x004C4AE0 and 0x004C5E20 is a dword with stack leftovers in its upper bytes; both read only the low byte.
- FillCityMayorRating never releases the exemplar it fetched.

### Clocks and random numbers

No random numbers. OnMouseMove stops and starts the tool's timer at +0x50 (a cRZTimer on real time, +0x90 milliseconds) unless the left button is down; Shutdown and OnMouseExit stop it. Nothing reads a clock otherwise.

### Mods

No patch site is inside the range (0x004D4406, 0x004D4494, 0x004D4EAA, 0x004D7335 to 0x004D74BF and 0x004D09D1 are all outside; the word at 0x00A92570 is filler 58, Q1's). No Q4 function is the target of a patched call (0x004C4F40, 0x004D3C30, 0x0090F574, 0x004C3810) and none calls one. No wrapper in `src/core/game_*.cpp` and no return-address test touches the range. The patch sites are expected in Q5 and Q7.

If a run dies in the route view after these groups are on, drop 0x004CA460 first (0x004CAC50 and 0x004CB900 still call it by address).

## The query tool, fourth piece: test

`OPENSC4_TEST_Q4=<day>` (src/core/tool_q4_autotest.cpp, token ` query4` in the fingerprint line, order 24, day hook 64, sub-hashes on the log lines `autotest: query4: ...`). It calls only the game's addresses and vtable slots, never our functions. There is no commit switch: nothing here changes the city (highlights are put back; FillCell with fill 0 reaches only fillers of Q1 to Q3 that read nothing back).

`OPENSC4_TEST_Q4_PARTS=<bits>` (default 63):

- 1 the fillers by cell: FillCell for its 22 control IDs and one it does not know, with fill 1 and 0, at cells inside the city, at the sample's lots and at every fourth cell just outside the edges with the connection types 0 to 12, on Q3's stand-in window; the two walks on a stand-in whose slot 32 records its arguments.
- 2 fillers 18 and 107 through the table's reader 0x004D3A90, 0x004CADE0 at the lots' cells and at a cell without a lot, 0x004CC160, and 0x004CB740 with a holder that has no curve property.
- 4 the tool of kind 1 as the view makes it (view slot 0x1C with command 0x6A935CF4): OnMouseMove over Q1's screen grid and once off the screen, then OnMouseExit.
- 8 the routes: the occupant set to buildings and network pieces, then 0x004CAC50 (the box, the mode, the line, its width bits and its point count); the panel target's handler with fake messages on the real window (radio pair and shown states); 0x004CAC30.
- 16 HighlightLot with 7 then 0 on a sample lot (the occupants' highlights before, between and after).
- 32 EraseTexts on a three-node tree made with the game's allocator (no hash; it must not crash).

The tool is put away with view slot 0x68 (Shutdown) at the end. Pointers are not hashed, only whether they are set. Samples (original, test): Making Money d0090 24 buildings and 3 network pieces, route line points 10 6 9 9 8 9 for the buildings and 0 318 150 for the pieces, the pointer grid found an occupant at 12 of 272 points, a lot of 14 occupants highlighted; Big City d0000 24 and 3, points 8 6 8 5 5 6 and 24 58 77, 81 of 272, 33 occupants; Rush Hour d0090 24 and 3, points 0 for all six buildings and 223 10 29, 14 of 272, 10 occupants. The mayor rating read 53, 65 and 74.

### Not reached

- The deal fillers ran only their "none" paths: the tutorial saves have no neighbour deals.
- The walks over real query windows (Q5) and EraseTexts on the real text map.
- Any use with mods installed. No coverage run.

### Trial results

First pass, snapshot q4a, on Making Money d0090, Big City d0000 and Rush Hour d0090, with the test and never run: the original twice (A1, A2), all groups ours (B) and only `ui.tools.base` with the Q4 groups ours (M) were identical in every autotest and fingerprint line, in all cases (24 of 24 runs finished, none crashed). The fingerprint without the token is the same in the test and untouched runs on all three saves (n == z). Build debug and release clean, ctest 20 of 20, retcheck 23 OK and 2 to check (0x004CAC50 tail jumps into 0x004CAA60 and 0x004CAAE0, both ours with a plain ret; 0x004CB880 is not in the function list, plain ret at 0x004CB8A6 by hand), register scan 25 hooked and none suspicious, EAX scan clean. One pass sufficed; nothing moved out of the hash.

Parity (batch q4b1, 7 days): plain 12 of 12, test on day 2 12 of 12, test on day 5 12 of 12, plopped with the test 3 of 3, every pair the same on its first run (39 of 39). No commit leg: nothing in Q4 changes the city.

## The query tool, fifth piece

Q5 is 15 functions in the range 0x004CC6E0 to 0x004D07C0 (17 in the function list; two stay the game's, below). Two groups, off by default, PostAppInit, detours at the game's entries: `ui.tools.query5a` (8 functions, src/core/tool_query_5a.cpp: the two windows by cell, fillers 20 and 91, the network piece's name and the route counts) and `ui.tools.query5b` (7 functions, src/core/tool_query_5b.cpp: the debug texts and the development log). Nothing in the range is drawing. The remaining 36 functions (Q6 and Q7, 0x004D07C0 to 0x004D767F; Q6 is done below) are still the game's and call these by address.

| Address | Name | What it does |
| --- | --- | --- |
| 0x004CC6E0 | RunEdgeWindow (cdecl, occupant) | For an occupant on the city's edge, the window of the neighbour connections next to it, run modal |
| 0x004CCCB0 | RunCellWindow (cdecl, kind, x, z) | The window for a cell (kinds 0xB, 0xC and 0xE), run modal |
| 0x004CCF60 | AppendBuildingText | A building's capacities, wealth, power, plant figures, water and age as text |
| 0x004CD820 | AppendDemandText | The growth developers' demand at a cell as text |
| 0x004CDC80 | FillOccupantCityMayorRating (filler 20) | The occupant's top left cell to FillCityMayorRating (Q4); false without an occupant |
| 0x004CDCC0 | FillMySimPortrait (filler 91) | The picture of the MySim living in the building |
| 0x004CDF80 | OutcomeName (register convention) | The name of a development outcome (0 to 27) |
| 0x004CE320 | LogDevelopmentFailures | A record's outcome counts as a table on the log lines |
| 0x004CE440 | LogDevelopmentAttempt | A tract developer's record: figures, outcome table and each build attempt, on the log lines |
| 0x004CE8A0 | AppendCellText | A cell's zone, land value, height, flammability, power, pollution, landfill, traffic and trip figures as text |
| 0x004CF2B0 | AppendLotShares | A lot's developer shares against the demand at the cell |
| 0x004CF8A0 | NetworkName (thiscall, ret 8) | The name of the network piece at +0x2C |
| 0x004CFB30 | LotRouteCounts (stdcall, ret 8) | A lot's trips counted by route kind, as text |
| 0x004CFD20 | BoxRouteCounts (thiscall, ret 0xC) | The trips of every lot and neighbour connection that count for a box, by route kind, as text |
| 0x004D0190 | RouteSummary (thiscall, ret 0xC) | Title, text and icon of the route view for a building, by its purposes |

The thiscall functions are hooked as `__fastcall` with a dummy EDX. 0x004CFB30 is stdcall: the game puts the tool in ECX but the function does not read it. 0x004CDF80 takes the index in EAX and the string in ESI, so it is hooked through a naked thunk; our own callers call the assigning function directly.

### Fillers 20 and 91

The two fillers are entries 20 and 91 of the filler table at 0x00A92488 (the words at 0x00A924D8 and 0x00A925F4). The first notes had 6 and 81: those came from the lister's vtable column, which is wrong here (0x00A924A0 and 0x00A925CC are 0x004C51C0 and 0x004C8FF0, entries 6 and 81, which are not Q5's). Neither changes the city: both answer true without `fill` (filler 20 asks for its cell first and answers false without an occupant).

### The windows by cell

Both windows load a layout (`{0, 0x96A006B0, instance}`), give its procedure part a runner of its own (0x004C5620) and the children the bar painter (vtable 0x00A90B38), run it modal through the window manager (slot 41, DoModalWin) and take it off again, giving the main window back the focus. While they run they keep the walk with fill (0x004CC660, Q4) at 0x00B21848, the window at 0x00B2184C and up to two cell queries at 0x00B21840 and 0x00B21844, which DoMessage (0x004D75B8, 0x004D75E4) reads; all are cleared at the end. The walks are given to the window's slot 32 as in "How the cell walks are reached" in Q4.

- The edge window (0x004CC6E0) takes the network piece's types (slot 22) and lets the last one it is of win: 0xB gives 10, 0 gives 2, 6 gives 8, 0xC gives 12, 2 gives 3, 7 gives 6, 8 gives 7, 9 gives 11, 1 gives 1, 4 gives 5; interface 0xA8C86142 gives 4. For each of the next cells on one edge that has a connection (neighbours slot 11) it makes a cell query `{x, z, n * 0x200, type}`; one or two queries give the layout (four layout instances by type, from 0xEA566A49 and 0xCA566F94 to 0x4A566D6E). It answers true when the window ended with 0.
- The cell window (0x004CCCB0) takes the kind (0xB, 0xC or 0xE) and a cell, the layout instance 0x6A561B3A, 0x4A562DA5 or 0xAA561F93 and the query `{x, z, 0x200, 0}`. When it ended with 0 it reads the controls back with the walk without fill (0x004CC6A0) and answers true. Kind 0xE plays a sound (0x00B43CB8, slot 11).

### The debug texts

0x004CCF60, 0x004CD820, 0x004CE8A0 and 0x004CF2B0 append to a game string and only read the simulators (building record, power, pollution, water, growth developers, zone manager, land value, flammability, landfill, traffic, lots). Their figures are formatted with the game's Sprintf (0x0090F574), with the x87 intermediates held in doubles. The development log writes through the query's log lines (0x004C3750, Q1's). 0x004CE440 makes a function-local static string ("unknown building", at 0x00B218EC, guard bit 1 at 0x00B21900, exit handler 0x00A7B150 given to the game's atexit) on its first call; ours does the same once, so the game and ours register it once each.

### Jump tables and gaps

The function list leaves gaps that are code tables, not functions: 0x004CE2A9 to 0x004CE320 is the 28-dword table of 0x004CDF80 (at 0x004CE2AC), and 0x004CF257 to 0x004CF2B0 the 16-dword zone-name table of 0x004CE8A0 (at 0x004CF264). Smaller tables sit at 0x004CCC88 and 0x004CCC9C, 0x004CDC68 and 0x004CDC74, and 0x004CE884. The translations use switches; the tables stay in the exe.

### Fields and layouts

New layouts in src/core/tool_query.h, no field renamed: `window_cell` (the two cell queries kept at 0x00B21840, up to `kQueries` = 2), `development_record` (what a tract developer keeps for one attempt: developer +0x8, tract +0xC and +0x10, population +0x14, capacity +0x1C and +0x20, desirability +0x24, resources +0x2C, the list of attempts +0x30, 13 vectors of 28 outcome counts +0x34), `development_attempt` (lot sizes tree +0xC, candidates vector +0x18, lot configuration +0x28, wanted capacity +0x30 and +0x34, outcome +0x38), `development_candidate` (kind 1 refurbish, 2 build new, 3 aggregate, 4 subdivide; cell, size, capacity, abandoned, score) and `kOutcomeCount` (28).

### Left the game's

- 0x004CCF20, a pair copy constructor (ret 8: it copies a key and a game string with 0x004089B0). It is a folded helper: 0x00895DCF, 0x009586B0 and 0x0097B612 are outside callers, not the query tool's. Hooking it would change code that is not ours.
- 0x004CDF00, SC4MapVector::CopyConstruct, whose caller 0x006C2450 is outside the class.

### Behaviour as the game has it, kept

- Locals the game never sets are 0 in ours: the edge window's type when the piece has none of the types (the test only calls pieces with a known type), the out values of the slots that fail, power consumed, produced and used, the plant capacities, air, water and garbage, and the sixth route kind's volume in AppendCellText (frame float never set).
- The edge window with an unknown type uses the network piece's pointer as the layout instance, and returns its low byte when no layout loads.
- RouteSummary's 0x1003 path hands 0x004CFD20 the icon pointer with the low byte replaced by the check box (RouteTouches and RouteKinds read that byte, as in Q4). It never lets go of the purposes value, which leaks.
- BoxRouteCounts never gives back the lot manager's slot 15 iterator (the same leak as BuildRoutes, Q4).
- The route view sets the tool's route mode (+0x98).

### Clocks and random numbers

No random numbers, no clock. The windows run modal in real time and wait for the player.

### Mods

No patch site is in the range (0x004D4406, 0x004D4494, 0x004D4EAA and 0x004D7335 to 0x004D74BF are in Q7, 0x004D09D1 in Q6). No Q5 function is the target of a patched call (0x004C3810, 0x004C4F40, 0x004D3C30) or calls one. Q5 does call Sprintf 0x0090F574 27 times; that is the target of the patched calls, not a patch site. No wrapper in `src/core/game_*.cpp` and no return-address test touches the range.

If a run dies opening a query window after these groups are on, drop 0x004CC6E0 and 0x004CCCB0 first; if it dies in DoMessage, drop 0x004D0190.

## The query tool, fifth piece: test

`OPENSC4_TEST_Q5=<day>` (src/core/tool_q5_autotest.cpp, token ` query5` in the fingerprint line, order 26, day hook 65, sub-hashes on the log lines `autotest: query5: ...`). It calls only the game's addresses and vtable slots, never our functions. There is no commit switch: nothing here changes the city (the texts only read, and the windows' read-back walk reaches only fillers by cell that read nothing back).

`OPENSC4_TEST_Q5_PARTS=<bits>` (default 63):

- 1 fillers 20 and 91 through the table's reader 0x004D3A90 for the sample's buildings, with fill 1 and 0 and without an occupant, on a stand-in window (a picture control stand-in for filler 91).
- 2 the debug texts: 0x004CCF60 for 24 sample buildings; 0x004CD820, 0x004CE8A0 (with the zone the zone manager gives, and every zone 0 to 16 at one cell) and 0x004CF2B0 at the buildings' cells and 15 others.
- 4 the development log with a stand-in log object at 0x00B21850 (restored): 0x004CE440 without a record (its static string), then a record made for the test (two attempts, a lot size tree, twelve candidates shown as ten and "...", outcomes 27 and 99).
- 8 the tool of kind 1 as the view makes it: 0x004CF8A0 for three network pieces, up to six edge pieces and a building.
- 16 on that tool: 0x004CFB30 for eight lots, 0x004CFD20 (modes 0 and 1) and 0x004D0190 for the 24 buildings (title, text, icon, route mode).
- 32 the windows, with a stand-in for the window manager's DoModalWin (slot 41, as ui_autotest.cpp): 0x004CCCB0 kinds 0xB and 0xC at three cells answered 0 and 1, and 0x004CC6E0 for edge pieces and one building inside; it hashes the answers and the stand-in's view of 0x00B21840 to 0x00B2184C during the run and after.
- 64 (opt-in) kind 0xE, which plays a sound.

Pointers are not hashed. Tokens: Making Money d0090 8B325E58, Big City d0000 7EAC1B45, Rush Hour d0090 C8ED0CF3.

### Not reached

- Edge windows on Making Money d0090 (no edge pieces); on Big City d0000 6 pieces were used and 33 of other types skipped, on Rush Hour d0090 2 and 5. The test only calls pieces of a known type that are inside buildings.
- The neighbour connections' real cells in a window the player answers; the stand-in answers 0 and 1.
- Kind 0xE (sound) is off by default.
- Any use with mods installed. No coverage run.
- The "titled" count on the routes log line is 24 of 24 everywhere: it probably counts a non-empty "?" read from a fresh string. It is only log text; the hash compares the strings themselves.

### Trial results

First pass, snapshot q5a, on Making Money d0090, Big City d0000 and Rush Hour d0090, with the test and never run: the original twice (A1, A2), all groups ours (B) and only `ui.tools.base` with the Q5 groups ours (M) were identical in every autotest and fingerprint line, in all cases (24 of 24 runs finished, none crashed). The fingerprint without the token is the same in the test and untouched runs on all three saves (n == z). One pass sufficed; nothing moved out of the hash. A 7-day run of the test with the windows (no exit hang) took 11 s with the original and 9 s with ours. Build debug and release clean, ctest 20 of 20, retcheck 11 OK and 4 checked by hand (the stdcall function and the naked thunk are not parsed; 0x004CE440 and 0x004CE8A0 are sized differently in the function list, all with a plain ret), register scan 15 hooked and none suspicious, EAX scan clean.

Parity (batch q5b1, 7 days, B list of 143 groups): plain 12/12, test day 2 12/12, test day 5 12/12, plopped 3/3, all same on the first pair.

## The query tool, sixth piece

Q6 is 23 functions in the range 0x004D07C0 to 0x004D4140 (the function list has 25 there; the left ones are below). Two groups, off by default, PostAppInit, detours at the game's entries: `ui.tools.query6a` (22 functions, src/core/tool_query_6a.cpp) and `ui.tools.query6b` (1 function, src/core/tool_query_6b.cpp: the cell log). Nothing in the range is drawing. The last piece, Q7 (0x004D4140 to 0x004D767F, 8 functions), follows below.

| Address | Name | What it does |
| --- | --- | --- |
| 0x004D07C0 | NetworkTrafficInfo (thiscall (title, text), ret 8) | Title and text of the traffic view for a network piece: the title by the piece's type, then for each route kind of that type "name: count", the count from the traffic simulator's slot 12 (GetTrafficEdgeDensity) |
| 0x004D0D30 | SetChildMoneyFloat (cdecl (window, id, double)) | Sets a child's text to a money amount with the app's formatter (slot 19, "%.2f") |
| 0x004D0EE0 | filler 121 | The reason an abandoned lot was vacated |
| 0x004D10F0 | filler 57 | Power: the monthly cost, or the figure of the power simulator's slot 33 |
| 0x004D1190 | filler 63 | Water: upkeep and production |
| 0x004D12A0 | LogCell (cdecl (x, z)) | The query tool's debug log of one cell, written to Query.txt (7,888 bytes in the game; see "The cell log") |
| 0x004D3260 | LotWealth (cdecl, lot) | The wealth of a lot |
| 0x004D32D0 | DeveloperOfLot | The growth developer of a lot (the building development service's object for the lot's type) |
| 0x004D3470 | OccupantLevelText (cdecl (occupant, levelFn, names, text)) | The level of an occupant as text, from a table of names |
| 0x004D3500 | filler 83 | The wealth bar |
| 0x004D3730, 0x004D37A0, 0x004D3810, 0x004D3880, 0x004D3940, 0x004D39B0, 0x004D3A20 | fillers 84, 117, 85, 87, 116, 89, 90 | The level texts (filler 87 has a case of its own for an abandoned lot) |
| 0x004D3A90 | FillControl | The reader of the filler table: finds a control's filler and calls it (table at 0x00A92488, entry = control ID and 0x1000 gives the entry ID and 0xFFF) |
| 0x004D3AE0, 0x004D3B40, 0x004D3BD0 | the window walks | Walk a window's children and fill them; the second skips text edits and interface 0x8810; the third reads the controls back (cdecl (child, id, window, about)) |
| 0x004D3C30 | DoQueryDialog (cdecl, occupant, answers a bool) | Opens the query window for an occupant, modal through the window manager (slot 41) |
| 0x004D3F40 | SC4QueryWindowRunner::OnMessage (thiscall (window, msg), ret 8) | The window runner's handler: the end message and the slider moved |

The filler indexes were worked out from the exe (index = (word - 0x00A92488) / 4). The function lister's vtable column for the filler table is wrong here too, as in Q5. The thiscall functions are hooked as `__fastcall` with a dummy EDX. In OnMessage the object is the runner plus 8.

### What the functions do that a caller relies on

- NetworkTrafficInfo allocates an 8-byte buffer it never uses. Ours does the same. The occupant slot 11 (GetBoundingCityCells) writes four dwords: the first smoke test gave it an 8-byte buffer and ours exited silently; it has 16 bytes now.
- SetChildMoneyFloat keeps the money sign in a static (0x00B21904) with a guard (0x00B21918) and the exit handler 0x00A7B170 given to the game's atexit. Ours makes it once, as the game does.
- Filler 50 reaches filler 57 through a 5-byte jump at 0x004D3250. The jump stays the game's, and so it reaches our filler 57 through the hook at its entry.
- DoQueryDialog has its own cRZTimer on the stack that runs on real time (330 ms, callback TimerSend 0x004C4EE0). It is started only when the view's markers (0x007D1AB0) give the occupant a kind. The test's stand-in for DoModalWin never lets it fire.
- The walks are given to a window's slot 32 as in Q4 and Q5.

### The cell log

LogCell is the debug log the query tool writes when its debug byte is set (callers 0x004D4825 and 0x004D4935 in 0x004D4140, Q7). It writes the file Query.txt through a file log object on the stack (vtable 0x00AD19F8, constructor 0x0092CEF6, mode out, trunc and binary), so every call rewrites the file. The path is relative: the file lands in the current directory of the game, which is the Apps folder of the install. It sets the log sink 0x00B21850 for the call and sets it to null at the end. The function has 7 jump tables (0x004D317C to 0x004D323C) that stay in the exe. Its helper 0x004C6A60 takes x in EBX and z in EDI; ours calls it through a naked thunk and it stays the game's.

### Left the game's

- 0x004D1230, SC4BuildingProfile::CopyConstruct: outside callers.
- The jump at 0x004D3250 (above).
- The jump tables of 0x004D12A0 and the gap between the functions.

### Behaviour as the game has it, kept

- Filler 63's production out-slot is the fill argument's slot.
- OnMessage reads the handler into the slider value's slot and releases the slider before it uses it again.
- 0x004D3B40's QueryInterface writes into the window argument's slot.
- DoQueryDialog calls through null when the window has no procedure part.
- After the dialog the view's markers stay set to the occupant's kind when the kind before was 0.
- Stack leftovers made 0 in ours: the upper bytes of the check box flag dword in NetworkTrafficInfo, the ShowChild flag in filler 121, and filler 57's monthly cost when the power simulator's slot 49 says no.

### New layouts

In src/core/tool_query.h, no field renamed: the namespaces `window_message` and `building_profile`.

### Fillers and the city

None of the fillers changes the city. DoQueryDialog does, in one place: its fill walk reaches filler 1 (the road access cache, Q1), and its read-back after an answer of 0 reaches the rename, funding and historical fillers, which the test does not set back. So that part runs only under `OPENSC4_TEST_Q6_COMMIT=1`, and the commit legs are compared A against B only.

### Clocks and random numbers

No random numbers. The only clock is DoQueryDialog's real-time timer (above).

### Mods

Two mods are involved.

NAM patches inside 0x004D07C0: at 0x004D09D1 it replaces `lea edi,[esp+0xbc]` with its own table of rail travel types and returns to 0x004D0B4A. This adds monorail to the rail route query text. The patch is in nam-dll 1.4.1 (1.3.1 does not have it) and is applied only with `EnableDualNetworkTransitPatch=true` in NAM.ini (default false). `foreign_sites.cpp` has the row {0x004D07C0, 0x004D09D1} for it. The hook layer therefore leaves 0x004D07C0 to the game when the patch is there and logs it, and the rest of the group still applies. The game install stays free of mods: NAM was installed only in a test user folder.

Trials, on Big City d0000 with `OPENSC4_TEST_Q6=2` for 7 days:

- NAM installed with the patch on: A and B were the same (fingerprint and the query6 token A42352A7, which is also the token of a mod-free run). B logged `hook: leaving SC4QueryTool::NetworkTrafficInfo at 0x004D07C0 to the game: patched inside by NAM.dll at 0x004D09D1 (JMP to ...)`, so the game's function ran with NAM's patch.
- NAM loaded with the patch off: A and B the same, and our 0x004D07C0 ran.
- The tutorial cities have no monorail traffic, so NAM's extra line does not change the text there. The trials show that the patch is detected and kept, not what the added line looks like.
- This is the first run of the project with NAM installed.

Parity with NAM (batch q6b2, 7 days, test day 2, NAM 1.4.1 in the job folders' Plugins): NAM loaded with the patch off 12 of 12; NAM with the patch on 10 of 12 plus 2 failed. In the patch-on leg every B run logged the leave line for 0x004D07C0. The two failures (Rush Hour d0000 and d0365) are B runs that died while the city loaded, before any test code: one crash in ntdll (null read) on another thread, one silent exit. They are not Q6's. In 20 single runs on Rush Hour d0000 with NAM (5 each), the original, all groups ours and the Q6 groups alone finished every time. The B list from before Q6 (no Q6 group) failed once the same way (ntdll+0x4E728, null read). Every run that finished had the same fingerprint. So some group merged before Q6 sometimes breaks the city load when NAM is loaded. No NAM patch site lies in a function that list hooks, apart from 0x004D07C0 (left to the game) and the plop tool's ScoreSide 0x004BE5D0 (dirt-road access patch, off in NAM's default ini). Open: which group, and why.

sc4-query-ui-hooks wraps the calls at 0x004D4494 and 0x0040CA7A in a function that calls 0x004D3C30 by address (RealDoQueryDialog). That reaches our entry hook, so DoQueryDialog works with that mod in principle. No patch site is inside Q6, and no wrapper in `src/core/game_*.cpp` and no return-address test touches the range. The mod itself was not run in the Q6 trials; the Q7 trial ran it (see "The query tool, seventh piece").

If a run dies in the query window after these groups are on, drop 0x004D3C30 first; if it dies on a panel, drop 0x004D3A90.

## The query tool, sixth piece: test

`OPENSC4_TEST_Q6=<day>` (src/core/tool_q6_autotest.cpp, token ` query6` in the fingerprint line, order 27, day hook 66, sub-hashes on the log lines `autotest: query6: ...`). It calls only the game's addresses and vtable slots, never our functions.

`OPENSC4_TEST_Q6_PARTS=<bits>` (default 31; 63 with `OPENSC4_TEST_Q6_COMMIT=1`):

- 1 the fillers through 0x004D3A90 on a stand-in window, for 24 buildings, plants, water producers and abandoned lots.
- 2 the helpers (LotWealth, DeveloperOfLot, OccupantLevelText, SetChildMoneyFloat).
- 4 0x004D07C0 on the first network piece of each type.
- 8 the walks and 0x004D3F40 on a stand-in runner.
- 16 the cell log at up to 13 cells. The current directory is moved to %TEMP%\opensc4-q6-<pid> for it; the file is hashed and then deleted.
- 32 (commit only) DoQueryDialog under the DoModalWin stand-in.

Tokens: Making Money d0090 845CCC93, Big City d0000 A42352A7, Rush Hour d0090 B9807DF0 (default parts); with the commit parts on (63): 564612A3, DB6C443F and CB65FC67. The fingerprint without the token is the same with the test and without it (n == z).

### Not reached

- The runner's end path (answers 0 and 1), the dialog's timer callback, and DoQueryDialog on a window without a procedure part.
- Abandoned lots: none on Making Money d0090; 2 on Big City d0000 and 3 on Rush Hour d0090.
- The rail piece was reached on Big City d0000 only.
- NAM's added monorail line (no tutorial city has monorail traffic).
- sc4-query-ui-hooks and any other mod.

### Trial results

First pass, on Making Money d0090, Big City d0000 and Rush Hour d0090, with the test and never run: the original twice (A1, A2), all groups ours (B) and only `ui.tools.base` with the Q6 groups ours (M) were identical everywhere, except one case: Rush Hour d0090 with the test on (no commit), M, whose `adv` part differed on 3 days. A second pass was identical. The fingerprint without the token is the same with the test and without it on all three saves.

Parity (batch q6b1, 7 days, B list of 146 groups): plain 12 of 12, test day 2 12 of 12, test day 5 11 of 12 plus 1 failed, plopped 3 of 3, all same on the first pair. The failed one is Big City d0000: the original (A) run exited silently while the city loaded, the known rare load exit; the pair rerun in batch q6b2 was the same. Batch q6b2: test day 2 with `OPENSC4_TEST_Q6_COMMIT=1` 12 of 12.

## The query tool, seventh piece

Q7 is 8 functions in the range 0x004D4140 to 0x004D767F (all 8 in the function list). Two groups, off by default, PostAppInit, detours at the game's entries: `ui.tools.query7a` (3 functions, src/core/tool_query_7a.cpp: the pick description, the mouse press and the message handler, the three that sc4-query-ui-hooks patches) and `ui.tools.query7b` (5 functions, src/core/tool_query_7b.cpp: the building and cell tips and a lot's level texts). Nothing in the range is drawing. With Q7 every function of the query tool's range is ours except what Q1 to Q6 listed as left (drawing, containers, folded helpers and jumps, listed in each piece's "Left the game's"). The range ends at 0x004D767F; the sign removal tool (`ui.tools.signpick`) starts at 0x004D7680.

| Address | Name | What it does |
| --- | --- | --- |
| 0x004D4140 | DescribePick (cdecl (x, z, three cIGZString*, debug byte), plain ret) | Describes what is under a screen point: picks with the unmarked occupant filter (0x004C4C90; else the view's ground point and the lot's building), plays the query sound (0x004C4F40), runs the edge window, the query dialog or the cell window (Q5, Q6), prints a network piece's figures and the cell's debug texts (lot configuration, building, shares, demand, EQ/HQ, cell figures) into the three strings, and under the debug byte writes the cell log (0x004D12A0, Q6). Only caller is OnMouseDownL |
| 0x004D4D70 | OnMouseDownL (thiscall (x, z, modifiers), ret 0xC; main vtable slot 16) | Stops the timer, clears the pointer text, hides the tip. With shift: the automaton under the point goes to the vehicle manager (city slot 43, its slot 34) or the message 0x2ACD2CF3 goes to all, and a group prop goes to the traffic network map (city slot 40, its slot 37). With control: the residential simulator's slot 59 (a stub) at the ground cell. Sets +0x89. Kind 1 shows the routes (0x004CAC50); otherwise, with no automaton, it calls DescribePick with three static strings and the debug keys. Returns true |
| 0x004D4F90 | InsertText (thiscall (place out, pair), ret 8) | Puts a (number, string) pair into the text multimap after the entries of its number or lower (signed), through SC4TextMultimap::InsertNode 0x004B6490. Only caller is CollectLotTexts |
| 0x004D4FD0 | CollectLotTexts (cdecl (lot, map)) | What the lot's growth developer makes of its cell, as game strings under numbers: the lot's property 0x8999B20B (90), power needed and not there (100), the pollution and garbage levels (2), the first three map levels (0 or 2), by zone the fourth and fifth, the state (constructing or vacant, 0) and former occupants poorer than the lot is built for (1) |
| 0x004D5C90 | AppendLotTexts (cdecl (occupant, text)) | For the occupant's lot (lot manager slot 14), the three texts with the highest numbers from CollectLotTexts appended to the text, each after a line end when the text is not empty |
| 0x004D5E30 | GetBuildingOccupantTipInfo (thiscall (title, text, background out, meter out, share out), ret 0x14) | The hover tip of the building at +0x2C. With the debug keys: the exemplar name and debug texts. Otherwise the name as the title and, by the building's first purpose (property 0xAA1DD396), a background picture, a meter picture, a meter share and the R, C or I developer type's text; then AppendLotTexts |
| 0x004D6C70 | GetCellTipInfo (thiscall (title, text), ret 8) | The tip of the cell under the ground point (+0x7C, +0x84): nothing over water (terrain slot 34), else the title 0x6A5BBCA6, the zone's text (jump table 0x004D7120; zones 0 and 15 set the title) and, for a lot at the cell, the three highest texts of CollectLotTexts |
| 0x004D7160 | DoMessage (thiscall (message), ret 4; this = tool + 0x28, slot 3 of the target part's vtable 0x00A90A78) | The tool's message handler, always false. Handles the six messages below |

The thiscall functions are hooked as `__fastcall` with a dummy EDX. DoMessage's object is the target part, the tool plus 0x28. The convention of GetBuildingOccupantTipInfo matters because sc4-query-ui-hooks calls it by address (below).

The six messages (src/core/tool_query.h, namespace `message`):

| Message | What DoMessage does |
| --- | --- |
| 0x0A456D95 | The tip, while +0x78 is set: by the tool's kind and the occupant's type it calls RouteSummary 0x004D0190, NetworkTrafficInfo 0x004D07C0, GetBuildingOccupantTipInfo, OccupantName 0x004C3810 or NetworkName 0x004CF8A0, or two fixed titles, and GetCellTipInfo for a pick without an occupant. With +0x94 set it adds the point. The background and meter pictures go to the info tip and to ShowTip 0x004C5BA0, otherwise the text goes to the view's pointer text (slot 40) |
| 0x0A456D96 | The view's markers again (0x007D20E0) |
| 0x26D31EC2 | Clears +0x78 |
| 0x66956814 | Every tenth day, the running query window is filled again (the globals 0x00B21840 to 0x00B2184C) |
| 0x69247DC7 | The route line made again (0x007DDC30, 0x004CA460) |
| 0xA6B79602 | The view changed: OnMouseMove at the mouse position (window manager slot 33) |

### What the functions do that a caller relies on

- DescribePick has no caller but OnMouseDownL, and nothing of ours calls OnMouseDownL or DoMessage directly: they are reached through their vtable slots (words 0x00A90AC8 and 0x00A90A84).
- OnMouseDownL passes DescribePick the debug byte from DebugKeysDown (0x004C4700): the application's slot 21 and Shift, Control and Alt all held. Unattended it is 0.
- Every call from our code to a function with a mod patch inside goes through the game's entry: OnMouseDownL calls DescribePick by address, DoMessage calls NetworkTrafficInfo (0x004D07C0, NAM's site, Q6) by address.
- DoMessage's day path reads the window and the walk from the globals only after the first query and does not test them again.

### Left the game's

- The jump tables 0x004D5C6C (3 dwords) with its index bytes 0x004D5C78 (9 bytes: zones 1 to 3, 4 to 6, 7 to 9) in CollectLotTexts, and 0x004D7120 (16 dwords, by zone) in GetCellTipInfo. The translations are switches.
- TextPairCopy 0x004CCF20 (a folded pair copy with outside callers, left in Q5) is called by address.
- Nothing else in the range. The functions Q1 to Q6 left are in their own lists.

### Behaviour as the game has it, kept

- DescribePick makes a school record (0x00414210) and frees it unused for every building. The lot from lot manager slot 6 is not used. The second string's EQ/HQ AppendRange uses the body pointer taken at the start. The third string gets the first string's old text when that was longer than 10 characters. In the "no building" branch the test begin != end on a fresh empty string never holds.
- OnMouseDownL calls through null when there is no memory for the message. The cell z of the control path is written into the modifier argument's slot.
- GetBuildingOccupantTipInfo assigns the title twice in the non-debug path.
- Stack leftovers made 0 in ours: OnMouseDownL's ground point when view slot 36 fails; DescribePick's ground point (read only after a successful pick or slot 8); DoMessage's mouse position when slot 33 does not write.
- Shares are computed as doubles, as the game's x87 does (the police share multiplies an unsigned integer by a float and truncates).

### New layouts

In src/core/tool_query.h: the namespace `message` (the six IDs above) and field comments (+0x78 the tip shows while set, +0x89 set by OnMouseDownL, +0x94 DoMessage adds the point to the tip). No field renamed. The text multimap (header node colour +0x0, parent +0x4, the string's end +0x1C, node size 0x28; the map on the stack is the header and a count) is described only in tool_query_7b.cpp, because nothing else uses it. New data names in data/addresses/tools.toml: SC4QueryTool::DebugText, DebugTextExtra and DebugTextExtra2 (0x00B2185C, 0x00B21870, 0x00B21884; static strings OnMouseDownL gives DescribePick, written and never read), UnmarkedFilterVtable2 0x00A90B00 (the serializable part of the occupant filter DescribePick makes at 0x004D42A2; its filter vtable is 0x00A90B18) and SC4TextMultimap::InsertNode 0x004B6490.

### Fillers and the city

DescribePick runs the query dialog and the windows, so the same rule as Q6 holds: its dialog's fill walk reaches filler 1 (the road access cache, Q1), and its read-back after an answer of 0 reaches the rename, funding and historical fillers. So the test runs it only under `OPENSC4_TEST_Q7_COMMIT=1`, and the commit legs are compared A against B only. Shift clicks (part 16) can leave the vehicle manager following an automaton (0x00B21B8C) and can send 0x2ACD2CF3 to all, so they are opt-in. The other functions change no city state; DoMessage writes only +0x78 of the tool (message 0x26D31EC2).

### Clocks and random numbers

No random numbers, no clock, no stopwatch. Real input only: DoMessage's 0xA6B79602 reads the mouse (window manager slot 33), OnMouseDownL's DebugKeysDown reads the keyboard, and DescribePick's modal windows wait for the player (and DoQueryDialog has its own 330 ms real-time timer, Q6). The tip shown by ShowTip goes to the mouse (Q2). Message 0x66956814 reads the sim date (clock slot 26, sim time). Which automaton a shift click finds is frame driven.

### Mods

sc4-query-ui-hooks (release v2.7.1) patches inside all three functions of `ui.tools.query7a`. It installs everything at its director's OnStart, before our PostAppInit. `foreign_sites.cpp` has all 8 rows, under the comment "sc4-query-ui-hooks, query tool":

| Function | Site | Patch | When installed |
| --- | --- | --- | --- |
| DescribePick | 0x004D4406 | CALL of the query sound (0x004C4F40) | only with `EnableOccupantQuerySounds=false` |
| DescribePick | 0x004D4494 | CALL of the query dialog (0x004D3C30) | always |
| OnMouseDownL | 0x004D4EAA | JMP (shift click copies an occupant) | always |
| DoMessage | 0x004D7335 | CALL of the terrain text (Sprintf 0x0090F574) | always |
| DoMessage | 0x004D7464 | CALL of a building's tip (0x004D5E30) | always |
| DoMessage | 0x004D747F | CALL of flora's tip (OccupantName 0x004C3810) | always |
| DoMessage | 0x004D74A8 | CALL of a network piece's tip (NetworkName 0x004CF8A0) | always |
| DoMessage | 0x004D74BF | CALL of a prop's tip (OccupantName) | always |

The mod also patches a call at 0x0040CA7A, which is in 0x0040C9B0, not the query tool, so there is no row for it. The "always" and "only with" columns are the author's reading of the mod; the trial below ran with the default ini, so 0x004D4406 was not exercised.

In the game the CALL sites are `E8` to a target inside the exe and 0x004D4EAA is `F6 44 24 2C 02` (a byte test, not E8 or E9), so with no mod no row matches. With the mod the sites are E8 or E9 to the plugin's DLL, `foreign_sites` names the DLL, and the hook layer leaves that function to the game and logs it. All eight hooks of Q7 go through `hook::Install` as an entry detour (as Q1 to Q6, including the tool's own vtable slots), which is where the check is made; no hook here is a vtable-kind install that would bypass it. Nothing in `src/core/game_*.cpp` wraps the range and no return-address test touches it.

What the mod calls by address: 0x004D5E30 (as RealGetBuildingOccupantTipInfo, thiscall (this, title, text, background, meter, share), the same signature as ours), 0x004D3C30 (RealDoQueryDialog, Q6), 0x004CF8A0 and 0x004C3810. These reach whatever is at those entries, ours when the groups are on. One effect of the mod's JMP at 0x004D4EAA: it replaces OnMouseDownL's control test, so with the mod a control click of the kind 0 tool goes down its own path (only the shift case does something, then straight to the end at 0x004D4F30, with +0x89 left 1). OnMouseDownL is the game's then, so that path is the mod's.

The mod is installed only in the test folders `%LOCALAPPDATA%\opensc4\p4env-q7mod` (the trial folder's Plugins); the game install stays free of mods. Version 2.7.1 was downloaded as its release.

Trial, on Big City d0000 with `OPENSC4_TEST_Q7=2`, no commit, with the mod (2.7.1) in the test folder: A and B both finished, with the same fingerprint (0a5e7fbe) and the same query7 token (B2560C72), all sub-hashes equal. B logged the leave lines, one for each function:

- `hook: leaving SC4QueryTool::DescribePick at 0x004D4140 ... SC4QueryUIHooks.dll at 0x004D4494 (CALL ...)`
- `hook: leaving SC4QueryTool::OnMouseDownL at 0x004D4D70 ... at 0x004D4EAA (JMP ...)`
- `hook: leaving SC4QueryTool::DoMessage at 0x004D7160 ... at 0x004D7335 (CALL ...)`

The five functions of `ui.tools.query7b` and the Q6 functions stayed installed, and the mod reached them by address. The mod's log read "SC4QueryUIHooks v2.7.1 / Installed the query UI hooks. / Registered the null45_query_ui_extensions.get_property_value function." The tokens differ from a mod-free run (the plugin's tip texts change the messages and click parts), which is expected: compare mod runs only with mod runs. This is the first project run with this plugin, and it shows the leave logic and the by-address calls work. It does not show the plugin's own features (the shift-click copy, the custom tips) working, which need clicks by hand.

If a run dies in a tip after these groups are on, drop the DoMessage hook first (it reaches the 7b functions); if it dies on a click, drop 0x004D4D70.

## The query tool, seventh piece: test

`OPENSC4_TEST_Q7=<day>` (src/core/tool_q7_autotest.cpp, token ` query7` in the fingerprint line, order 28, day hook 67, sub-hashes on the log lines `autotest: query7: ...`). It calls only the game's addresses and vtable slots, never our functions.

`OPENSC4_TEST_Q7_PARTS=<bits>` (default 7; 15 with `OPENSC4_TEST_Q7_COMMIT=1`; 16 only when asked):

- 1 DoMessage on the kind 0 and kind 1 tools the view makes (commands 0x6A935CF3 and 0x6A935CF4): the tip message for no occupant (+0x88 clear and set, at 6 places), up to 6 buildings, one network piece per type, 2 flora, 2 props, a power pole and a landfill, each with +0x94 off and on (the view's slots 40 and 41 are patched to recorders that do nothing else, and the info tip is reset before and read back after); then the tip with +0x78 clear, 0x0A456D96, 0x26D31EC2 (+0x78 hashed), a message of no meaning, 0x66956814 with a stand-in window and 2, 1 and 0 stand-in queries in the four globals (restored; it acts only when the day number ends in 0, and the day is logged), 0x69247DC7 (kind 1, after ShowRoutes made a line; line points counted) and 0xA6B79602 with the window manager's slot 33 patched to 4 fixed points.
- 2 The `ui.tools.query7b` functions on the kind 0 tool: GetBuildingOccupantTipInfo and AppendLotTexts for the building of every lot (up to 300; the outs preset to sentinels 0xA5A5A5A5 and 0.25), GetCellTipInfo at the sample's cells, the pieces' cells, the middle, 0/0 and every 7th cell.
- 4 OnMouseDownL on the kind 1 tool with nothing, 4 buildings and the network pieces at +0x2C, with modifiers 0 and 4, and control (2) with the view's slot 36 patched to 5 fixed ground points; OnMouseUpL after each.
- 8 (commit) DescribePick over a 5 by 4 screen grid under the DoModalWin stand-in (answer 1), with the debug byte 0 and 1 (the current directory is moved to %TEMP%\opensc4-q7-<pid> for each call, Query.txt is hashed and deleted, the log sink restored); the kind 0 tool's OnMouseDownL at the same points (the three static strings hashed); DescribePick at the first 5 points answering 0 (the read-back changes the city).
- 16 (opt-in) shift clicks over the grid. Automata are frame driven, so only the number of calls is hashed.

The view's slots patched in parts 1 and 4 are saved and restored. The tools are put away by the view (slot 0x68, Shutdown) after each part.

Tokens (snapshot q7a): Making Money d0090 85E1181F, Big City d0000 44D4754D, Rush Hour d0090 78ED0ADA (default parts); with `OPENSC4_TEST_Q7_COMMIT=1`: A656EA0C, 175C517D and DF6AFDD4. The fingerprint without the token is the same with the test and without it (n == z): Making Money ef8d770c, Big City 91cc62f4, Rush Hour bfe25554. The sample: 3, 4 and 5 network piece types on the three saves, 5 other occupants (2 flora, 2 props, a pole, a landfill) on all, and 300 buildings. The commit leg's DescribePick part ran 3 windows and 8 log files on Making Money d0090, 9 windows and 8 files on Big City d0000, and 0 windows and 9 files on Rush Hour d0090. No Query.txt appeared in the Apps folder.

### Not reached

- DoMessage's 0x66956814 refill acts only when the day number ends in 0. On day 2 the numbers on the three saves end in 6, 5 and 9 (2454866, 2457285, 2455409). A leg with another day would reach it.
- Shift clicks (part 16, opt-in, not run): the vehicle manager, the traffic network map and message 0x2ACD2CF3.
- DescribePick's network branch (interface 0x4A10AFC2) only where the grid hits such a piece (not counted); the "no building" case with begin != end (impossible); DescribePick without the city view.
- The plugin's shift-click copy and its custom tips (they need clicks by hand).
- GetBuildingOccupantTipInfo always overwrote the background sentinel (300 of 300), so the paths that leave it unset were not seen.
- Any mod other than sc4-query-ui-hooks 2.7.1, and that one only with its default ini (`EnableOccupantQuerySounds=true`).

### Trial results

First pass, snapshot q7a, on Making Money d0090, Big City d0000 and Rush Hour d0090, with the test and never run: the original twice (A1, A2), all groups ours (B) and only `ui.tools.base` with the Q7 groups ours (M) were identical in every log line, in all cases (36 of 36 runs finished, none crashed). One pass sufficed; nothing moved out of the hash. A 7-day commit run on Big City d0000, side B, took 13 s (no exit hang). Build debug and release clean, ctest 20 of 20, retcheck 6 OK and 2 checked by hand (DoMessage is not in the old exe listing and GetBuildingOccupantTipInfo is cut by the function list; every exit of both is the plain `ret 4` and `ret 0x14` of our signatures), register scan 8 hooked and none suspicious, EAX scan clean.

Parity (batch q7b1, 7 days, B list of 148 groups): plain 12 of 12, test day 2 11 of 12 plus 1 partly, test day 5 12 of 12, plopped 3 of 3. The partly one is Making Money d0090: the first B run differed in the `adv` part from day 1, before the test on day 2, and the other two B runs were the same as A; the pair rerun in batch q7b2 was the same. Batch q7b2: commit (`OPENSC4_TEST_Q7_COMMIT=1`) 12 of 12; with sc4-query-ui-hooks 2.7.1 in the job folders' Plugins (p4env-q7mod, test day 2) 12 of 12, every B run logging the three leave lines (0x004D4140, 0x004D4D70, 0x004D7160) and no A run.

## The U-Drive-It control, first piece (T7a)

`cSC4UDriveControl` (our name): 0xC0 bytes, ID 0x2B709676, cursor 0xC16C4E1B, main vtable 0x00A9D9B0, message target vtable 0x00A9D9A0 at +0x28, a third interface 0x00A9D98C at +0x2C, code 0x00562C90 to 0x00566D55. The park automaton 0x00506850 makes it when the player takes over a vehicle; the city view does not. The driven object is at +0x30. One group, `ui.tools.udrive1` (src/core/tool_udrive_1.cpp), PostAppInit, off by default: 7 hooks.

| Function | Address | Vtable slot |
| --- | --- | --- |
| Constructor: the base, the three vtables, the stopwatch at +0x48 (with 4), a list head at +0x64 linked to itself, +0x3C 1, +0x40 0.042, +0x6C and +0x70 1.0, the rest zero but +0x78 | 0x00565480 | |
| Destructor's body: lets go of the ten objects at +0xB0 down to +0x8C, the list +0x64, +0x60, the stopwatch, +0x34 and +0x30, then the base | 0x00565930 | |
| Deleting destructor | 0x00565910 | 26 |
| CanDrive: a driven object whose slot 0x7C answers false for 0x8000000 and for 1 | 0x00562ED0 | |
| OnMouseDownR: when the driven object takes input (both flags clear, and the city's slot 0xC4 object's slot 0x80 false), its slot 0xF0 with (8, 1); true | 0x00564390 | 17 |
| OnMouseUpL: the point at +0x24/+0x28 of the object at +0x9C set to -1, -1; when it takes input and CanDrive, the driven object's slot 0xF0 with (7, 0) and the service of 0x00602640 its slot 0x18 with 4; true | 0x00564300 | 18 |
| OnMouseUpR: as OnMouseDownR with (8, 0) | 0x005643F0 | 19 |

Done in T8 (next section): Init 0x00565A10 (3), Shutdown 0x00563970 (4), OnKeyDown 0x00566C60 (14), OnKeyUp 0x00566CF0 (15), OnMouseDownL 0x005655D0 (16), OnMouseMove 0x005631C0 (20), slot 27 0x005650E0, the message handler 0x00566890, the third interface's 0x00562E20 and the driving helpers. The nine drawing functions were still the game's (Renderer); T9 takes them.

`OPENSC4_TEST_T7=<day>` (src/core/tool_t7_autotest.cpp, token ` udrive1`, fingerprint order 29, day hook 68). It calls only the game's addresses and the control's vtable slots. Nothing changes the city: the control is made on its own (without Init), the driven object and the point holder are stand-ins that record the flags asked and the commands given, and the control is destroyed through slot 26, not Release (which would run the game's Shutdown). Five sections: the fields after the constructor (not the stopwatch and the list pointer), the buttons with nothing to drive, the point holder, the buttons and CanDrive under five scripts of flag answers (the release never reaches command 7: CanDrive's answers are always "set"), and the two releases of the destructor. Not reached: command 7 and the service of 0x00602640; a real driven vehicle.

Trial (2026-10-16, A = the original, B = `ui.tools.base` and `ui.tools.udrive1` ours, test day 2, 5 days, with `determinism.dispatch_info`): Making Money d0090, Big City d0000 and Rush Hour d0090 each identical A = B in the whole fingerprint, token 2282BB82 on all three. No batch yet.

## The U-Drive-It control, second piece (T8)

T8 does the rest of `cSC4UDriveControl` but the drawing: its life (Init, Shutdown), the message handler with the driving panel's update on every tick, the keys, the left press, the mouse move, putting the control away and the start of a drive. Two groups, PostAppInit, off by default: `ui.tools.udrive2` (src/core/tool_udrive_2.cpp, 11 hooks) and `ui.tools.udrive3` (src/core/tool_udrive_3.cpp, 14 hooks). With T7a, 32 of the class's 45 functions are ours.

`ui.tools.udrive2`:

| Function | Address | Vtable slot |
| --- | --- | --- |
| Init: once, the base Init, the target (+0x28) on the tick service (priority 0xFF), the terrain and its size, the two pick filters, four empty strings handed to the object of 0x00913BEE, the city's four automata managers, the marker (made and set up by the game's drawing code), the overlay manager, the message server registrations, the network manager, the simulator, the automata script system; with a view, the driving panel's window resource and the three panel commands registered with the framework's command service; true | 0x00565A10 | 3 |
| Shutdown: the inited byte cleared first, then everything Init took undone in its own order, the simulator and the automata managers resumed, the panel windows closed, the park automaton's hold on the control dropped (0x00506910), the base Shutdown; true | 0x00563970 | 4 |
| DoMessage (this = control + 0x28; message IDs below); true | 0x00566890 | 3 of 0x00A9D9A0 |
| UpdatePanel: on every tick, with a driven automaton neither stopped nor flagged 1: the cursor, the panel's dials (speed, state, height over the ground modulo 100 with its text, angle, three state triples), the button 0x0C16C9B9, the money and count texts, the two engine volumes; then the stopwatch test (below) | 0x00566000 | |
| OnPanelCommand (this = control + 0x2C): a click on 0x4992046A, 0x2C16C9CB, 0x4C1A5CC9 or 0xE9920494 posts 0x6A935CB7, 0xAC1F0449, 0xCC1F0457 or 0x6A935CBA and answers true; false otherwise | 0x00562E20 | 3 of 0x00A9D98C |
| Lets go of the driven automaton (+0x30): callback, marker, vehicle manager follow, view, sound event 0 and the automaton's sound bank; removed or its state popped; +0x30 cleared | 0x00562FD0 | |
| OnAutomatonGone: when it is the driven automaton, 0x00562FD0 and slot 27 | 0x00563310 | |
| CloseWindows: the panel windows +0xA4 and +0xA8 taken off the view's window and released, then the view's slot 0xE4 | 0x00563330 | |
| StopAndPause (caller the park automaton, when it has no vehicle to give): 0x00562FD0, +0x60 let go, the simulator and the four automata managers paused | 0x00564450 | |
| AddListed (caller the park automaton): an object not yet in the list +0x64 is referenced and appended | 0x00565FA0 | |
| FilterTest: the automaton pick's occupant filter (made by Init at +0x84) takes four occupant types | 0x00562C90 | 3 of 0x00A9D894 |

`ui.tools.udrive3`:

| Function | Address | Vtable slot |
| --- | --- | --- |
| OnKeyDown: a repeat is taken; Escape puts the control away; during a city situation countdown taken; without the script system at +0xB0 false; else its slot 0x38 turns the key into a command (13 none), ApplyBinding with (command, 1), then CommandDown when the binding does not take it | 0x00566C60 | 14 |
| OnKeyUp: as OnKeyDown without the repeat and Escape tests, with (command, 0) and CommandUp | 0x00566CF0 | 15 |
| ApplyBinding: the driven automaton's key binding record for the command (from its description's map): the state 0x10's byte at +0x205 plus the command set, toggled or flipped; named effects recreated, started or stopped, and an automata controller told on or off, by the record's flags; true when the record has flag 8 | 0x00566A70 | |
| CommandDown: commands 0 to 12 (jump table 0x0056402C, left in the exe); with CanDrive HandlePlayerDriveCommand (driven object's slot 0xF0) with (command, 1) and for some commands the service of 0x00602640 slot 0x14; 2 and 3 press the driving window's two buttons; 4 and 10 flip +0x44 and +0x45; 9 flips the driving state's +0x229 | 0x00563D40 | |
| CommandUp: the release (jump table 0x005642C8, left in the exe): (command, 0), the service's slot 0x18, the buttons released | 0x00564060 | |
| OnMouseDownL: a network piece the network manager takes gets a sound and its cell in the marker's +0x24/+0x28; while driving, command 7; with nothing driven, an automaton under the pointer starts a drive (0x00565160 with 1); true | 0x005655D0 | 16 |
| PickNetworkOccupant: the renderer's pick (0x00B43DD0 slot 0x104) with the filter at +0x88, asked for 0xA821EF94 | 0x005630C0 | |
| PickAutomaton: the same with the filter at +0x84, asked for 0xA9B40F05 (cISC4Automaton) | 0x00563140 | |
| OnMouseMove: the pointer's cell into the marker's +0x1C/+0x20 (-1, -1 without a ground point); driving, the screen point into the driving state's +0x214/+0x218; else the automaton under the pointer held at +0x60; true | 0x005631C0 | 20 |
| PutAway: the listeners told (1, 0), a disabled driven automaton removed, 0x00562FD0, +0x60 let go, disasters back on, then the base's PutAway | 0x005650E0 | 27 |
| NotifyListeners: a message 0x0BB324BD (the control, a, b) to each listener in the list +0x64 | 0x005644E0 | |
| MakeWindows: once, the driving window (+0xA4) and the second window (+0xA8) from the vehicle's exemplar, centred at the foot of the view; the view's mission status panel | 0x00564570 | |
| The start of a drive (callers the park automaton and OnMouseDownL): disasters off, 0x00562FD0, the UI minimised, the windows, the automaton held at +0x30 and pushed into state 0x10, the vehicle manager follows it, zoom limits from its description, a marker into +0x38, the stopwatch stopped and reset, the simulator resumed | 0x00565160 | |
| Pause (byte): the simulator at +0xA0 and the four automata managers paused or resumed | 0x00562F00 | |

DoMessage handles: 0xA6B79602 (the view's: enables the panel's buttons 0x4992046A and 0xE9920494), 0x86AD10EE (the tick: UpdatePanel), 0x09D92FC3 (an automaton's message, kind in GetData2: 3 OnAutomatonGone; 0xB sound event 0 stopped, the driven automaton's marker flagged through the overlay manager, the stopwatch made again and started; 0xD stops sound event 0 when the automaton has no state 0x10), 0xAC1F0449 (panel small), 0xCC1F0457 (panel big) and 0xAC1F045D (panel switched). The last three are the panel commands MinimizeDrivingPanel, MaximizeDrivingPanel and ToggleDrivingPanel (table 0x00B0A110).

Left the game's, with the reason:

- The nine drawing functions (Renderer's): the marker's constructor 0x005649D0, set-up 0x005633C0 and take-down 0x00563730, 0x005637E0, 0x00564A60, 0x00564BE0, and the drawable's 0x00564A20, 0x00564A30, 0x005657A0. Init and Shutdown call the marker's three at their addresses.
- 0x005658D0: a method of the overlay manager the control holds at +0x34 (it sets 0x400 in the marker of a byte key), compiled with the U-Drive code; not this class's. DoMessage calls it at its address.
- The this-adjusting thunks 0x00565570 to 0x005655C0 (the three at 0x005655A0 to 0x005655C0 are counted in the class's 45).
- Helpers with callers outside the class, not counted in its 45: 0x00565430 (a byte-keyed map's find, also called from 0x00674140), 0x00562DA0, 0x00562DD0 (SC4RefPtr::Destruct), 0x005650A0 (SC4PairList::Destroy), and 0x00562CE0, 0x00562D60, 0x00562D90 (a line object's methods, the park automaton's).

T9 made these thirteen ours (the next section); T8 left them with the reasons above.

So of the 45: 45 ours after T9.

Field names, corrected from T7a's (all three files now use kOverlays for +0x34, kTerrain for +0x68 and kMarker for +0x9C):

| Offset | T7a's name | What it is |
| --- | --- | --- |
| +0x34 | kRef34 | The overlay manager (the dispatch service's slot 0x58) |
| +0x38 | kWord38 | The driven automaton's marker ID on the overlay manager |
| +0x40 | kFloat40 | 0.042, or the third value of the automaton's description: the speed dial's top and the engine volume's scale |
| +0x44, +0x45 | | Command 4's and command 10's toggles (+0x44 is also the button 0x0C16C9B9's state) |
| +0x48 | | The stopwatch, 0x18 bytes, mode 4, real time (below) |
| +0x60 | | The automaton under the pointer (referenced) |
| +0x64 | | The listeners: a list head, 12-byte nodes (next, prev, object), objects referenced |
| +0x68 | kCount68 | The terrain (referenced) once Init ran, not a count |
| +0x6C | kFloat6C | The terrain's slot 6 |
| +0x70 | kFloat70 | 1 over +0x6C |
| +0x74 to +0x80 | | Four empty strings Init makes and hands to the object of 0x00913BEE |
| +0x84, +0x88 | | The automaton pick's filter (vtable 0x00A9D894) and the network pick's filter (0x00A8FE14) |
| +0x8C to +0x98 | | The city's four automata managers (city slots 43 to 46: vehicles, pedestrians, aircraft, watercraft) |
| +0x9C | kPointHolder | The marker (0x4C bytes, constructor 0x005649D0): +0x1C/+0x20 the pointer's cell, +0x24/+0x28 the clicked network piece's cell |
| +0xA0 | | The simulator (global 0x00B43D5C) |
| +0xA4, +0xA8 | | The driving window and the second panel window |
| +0xAC | | The network manager (global 0x00B43D54) |
| +0xB0 | | The automata script system (city slot 48) |
| +0xB4, +0xB8 | | What the view's slot 0x7C gave, and the COM object 0x42E967BE given to the view's slot 0x80 |

The driving state (the automaton's state 0x10) holds a byte per command at +0x205, the last mouse point at +0x214/+0x218 and command 9's toggle at +0x229.

The stopwatch at +0x48 runs on real time and is not under `determinism.sim_timers`. DoMessage starts it again for kind 0xB of 0x09D92FC3, the start of a drive stops and resets it, and UpdatePanel puts the control away (slot 27) once it has run 9000 (the stopwatch's units). The panel's update reads the driven automaton's speed and state, which are frame driven, and sets the sound system's two engine volumes. The picks depend on the camera and the frame. No random number is drawn.

`OPENSC4_TEST_T8=<day>` (src/core/tool_t8_autotest.cpp, token ` udrive2`, fingerprint order 31, day hook 69). As T7a's test, it calls only the game's addresses and the control's vtable slots, so the same run compares with the groups on and off. The control is made on its own (without Init) and gets a copy of its vtable whose slot 8 (SetCursorId) and slot 27 (put away) only record. The driven automaton, its occupant, the view, the panel windows and their children, the simulator, the automata managers, the script system, the overlay manager and the marker are stand-ins that record the slots called and the arguments that are not pointers; any slot a stand-in does not expect is counted, and the total line's `unexpected slots` must be 0. The control is destroyed through slot 26. `OPENSC4_TEST_T8_PARTS` (decimal mask, default 63):

| Bit | What it runs |
| --- | --- |
| 1 | The filter test on stand-in occupants, OnPanelCommand on clicks it does not take, AddListed, NotifyListeners, MakeWindows with an instance no exemplar has (0x12345678) |
| 2 | Letting go with nothing driven, OnAutomatonGone, CloseWindows, StopAndPause, Pause |
| 4 | DoMessage with every message it handles; the stopwatch restarted by kind 0xB is stopped again at once and not hashed; kinds 0xB and 0xD stop the sound system's event 0 (sound only) |
| 8 | UpdatePanel in ten cases (flags, no state, no panel, ground from the terrain, from the region terrain and outside, clamps, cursor, height over 100, the volumes' branches); the city's terrain, cells, region terrain and funds are read only |
| 16 | The keys (slots 14 and 15) with a driven automaton that cannot be driven: the panel's buttons, the two toggles, Escape |
| 32 | The mouse move (slot 20) while driving: the view's pick (stand-in) and the city's cell into the marker |
| 64 | Asked only: the keys with a driven automaton that can be driven (real sound events) |
| 128 | Asked only: the left button at (-1000, -1000) with the real filters and the renderer's pick; letting go of a driven stand-in (real sound events stopped, a bank unloaded) and OnAutomatonGone for it |
| 256 | Asked only, and only with `OPENSC4_TEST_T8_COMMIT=1`: the four panel commands posted (0x6A935CB7 and 0x6A935CBA move the camera); Init, StopAndPause and Shutdown on a second control with the city's real services (the simulator and the automata managers paused and resumed, the driving panel shown and taken away, 0x00506910) |

Not reached: the start of a drive (0x00565160) and the real PutAway (slot 27 is a recorder in the test), MakeWindows with a real exemplar, a key binding record that takes a command, the stopwatch's 9000 put-away (real time), Init and Shutdown outside the commit leg, a real driven vehicle.

Oddities kept: Shutdown clears the inited byte before the base Shutdown, so the base lets nothing go (the view, the window manager and the cursor stay referenced). UpdatePanel allocates and frees an 8-byte buffer it never uses. OnMouseDownL lets the network piece go and then asks it for its cell. NotifyListeners does not test the new message for null. The start of a drive does not test the vehicle manager for null, does not read its second argument, and reads property 0x2BE8FBF3 into the caller's argument slot. MakeWindows shows the mission panel whenever property 0xEC1B2361 is set, even when the window failed to load (its no-op release of the child holders before reuse is dropped in ours: they are always null there). PutAway clears the cursor text only when +0x60 was set; OnMouseMove always when nothing is under the pointer.

Trial (2026-10-17, A = the original, B = `ui.tools.base` and `ui.tools.udrive1` to `ui.tools.udrive3` ours, test day 2, 5 days, with `determinism.dispatch_info`): Making Money d0090, Big City d0000 and Rush Hour d0090 each identical A = B in the whole fingerprint; tokens BFE58D05, D303EA93, B0713745.

Parity (2026-10-17, B = the Q7 batch list (147 groups) plus `ui.tools.udrive1` to `udrive3`, 7 days, one job; batches t8b1/t8b2): plain 12/12 (Making Money d0000 first failed with no B save because the batch was stopped under it and switched to one job; the pair rerun alone, t8b1r, same), `OPENSC4_TEST_T7=2 OPENSC4_TEST_T8=2` 12/12, the same at day 5 12/12, plopped 3/3, all identical.

## The U-Drive-It control, third piece (T9)

T9 makes the last 13 functions ours: the U-Drive marker's nine drawing functions, the control's three this-adjusting thunks and the overlay manager method. One group, PostAppInit, off by default: `ui.tools.udrive4` (src/core/tool_udrive_4.cpp, 13 hooks). With T7a and T8, all 45 of `cSC4UDriveControl`'s functions are ours.

| Function | Address | Vtable slot |
| --- | --- | --- |
| SC4UDriveMarker::Construct: the base, two vtables, byte +0xC clear, +0x1C..+0x28 -1, +0x44/+0x48 clear | 0x005649D0 | |
| SC4UDriveMarker::Init: once, the render state at +0x10, the 16-entry direction table from four points, six model interfaces and two resource objects | 0x005633C0 | |
| SC4UDriveMarker::Shutdown: byte +0xC clear first, six counted model releases and the two adjusted reference holders | 0x00563730 | |
| SC4UDriveMarker::BuildQuad: four textured vertices at the terrain height from the direction table (the table's vector x 12 is used for the offsets; the caller's width and piece ID stack slots are reused by the game) | 0x005637E0 | |
| SC4UDriveMarker::ChooseModel: one of the six model objects from the path direction and the table's cross product | 0x00564A60 | |
| SC4UDriveMarker::DrawCell: one occupant's pieces on a cell, the selected one again, then the cell's marker object | 0x00564BE0 | |
| SC4UDriveMarker::Draw: the city's traffic network over the view rectangle, four sides' piece IDs per record and DrawCell for each | 0x005657A0 | 3 of 0x00A9D974 |
| SC4UDriveMarker::DeletingDestructor: the two reference holders, the base vtable, free when bit 0 | 0x00564A30 | |
| The same through the second vtable's adjustor (this = marker + 4) | 0x00564A20 | 3 of 0x00A9D95C |
| cSC4UDriveControl::QueryInterfaceThird (this = control + 0x2C, back to the shared 0x004B8480) | 0x005655A0 | 0 of 0x00A9D98C |
| cSC4UDriveControl::AddRefThird (the count at +8) | 0x005655B0 | 1 of 0x00A9D98C |
| cSC4UDriveControl::ReleaseThird (the shared 0x004B9E20) | 0x005655C0 | 2 of 0x00A9D98C |
| SC4OverlayManager::F5658D0: finds the marker of a byte key in the map at +0x48 and sets 0x400 in its +0x110; true when found | 0x005658D0 | |

Init and Shutdown still call the marker's three functions at their addresses; the calls reach ours through the hooks. The control's Init and Shutdown (ui.tools.udrive2) does the same for the marker.

Oddities kept: the marker's constructor leaves +0x18 and +0x2C..+0x40 as they were; Init overwrites the shared direction table at 0x00B229B8 (SC4UDrive::VectorFill's table); BuildQuad reuses its caller's width and piece ID argument slots as locals, so the offsets are the table's vector times 12; DrawCell sets its four render calls up before testing whether any piece has a model. The cell marker object's own draw at the end of DrawCell (0x0080B830, the object's second interface and its 0x118-byte block) is not reproduced: the object's reference is taken and released as the game does, but the block is not drawn. No unattended test reaches it (the marker's +0x44/+0x48 are null in the stand-in Init).

`OPENSC4_TEST_T9=<day>` (src/core/tool_t9_autotest.cpp, token ` udrive4`, fingerprint order 32, day hook 70). As T7a's and T8's tests it calls only the game's addresses and the class's vtable slots. The marker is made on its own; its renderer, the framework and the framework's service are stand-ins around Init only, the six model objects are counters that one release leaves alive, the terrain is a stand-in, and the city and traffic network are stand-ins. `OPENSC4_TEST_T9_PARTS` (decimal mask, default 63): 1 the constructor and destructor (both vtables and the adjustor); 2 Init and Shutdown (the direction table is hashed and put back); 4 BuildQuad over four positions, all sides and ways, both piece-flag cases; 8 ChooseModel over the table's branches, a short path and no models; 16 Draw with no city, no map, an empty rectangle, no records and a record whose four sides have no pieces; 32 the third interface's three thunks and the overlay manager's keyed flag on a one-node map. DrawCell itself is not reached: its render context is needed before its first test.

Trial (2026-10-17, A = the original, B = `ui.tools.base` and `ui.tools.udrive1` to `ui.tools.udrive4` ours, test day 2, 5 days, with `determinism.dispatch_info`): Making Money d0090, Big City d0000 and Rush Hour d0090 each identical A = B in the whole fingerprint; the test token is C371903C on all three.

Parity (2026-10-17, B = T8's batch list (164 entries) plus `ui.tools.udrive4`, 7 days, four jobs): plain 12/12, `OPENSC4_TEST_T7=2 OPENSC4_TEST_T8=2 OPENSC4_TEST_T9=2` 12/12, plopped 3/3, all identical.
