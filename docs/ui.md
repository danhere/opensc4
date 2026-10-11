# Interface code

The game's own interface classes, rewritten piece by piece like the simulators ([parity.md](parity.md)). Build 641 addresses throughout. Drawing is not part of this page: a function that itself sets render states or draws stays the game's until the renderer work reaches it ([renderer.md](renderer.md)); calls into drawing, model and window services are kept as plain calls.

| Piece | Code | State |
| --- | --- | --- |
| The advisor window's controller (`SC4AdvisorHelper`, 0x0041D400 to 0x00420030) | 26 functions | ours (`ui.advice_text.part1`, `part2`) |
| The advisor's head view (0x00420030 to 0x00420D30) | 14 functions; `Draw` (0x004209D0) is drawing code and stays the game's | ours (`ui.advice_text.part2`) |
| The MySim home and job tools (0x00437AF0 to 0x00439380) | 15 functions | ours (`ui.mysim_tools.part1`) |
| The MySim dispatch and move-in tools (0x00439380 to 0x0043AA30) | 21 functions | ours (`ui.mysim_tools.part2`) |
| The MySim picker's list of Sims and the import from The Sims (0x0043AA30 to 0x0043D0F0) | 19 functions | ours (`ui.mysim_tools.part2`, `part3`) |
| What was left of the MySim's interface side (0x00424F30 to 0x0042C8E0, scattered): the two tool makers, Find, ShowResidence, ShowLocation, the picture buffer, the home filter, the MySim's advice class, two script setters | 11 functions | ours (`ui.mysim_panel.sim`) |
| The paced head view (0x0054A5F0 to 0x0054A740): the advisor's head with a stopwatch, used by the advice popup | 5 functions | ours (`ui.mysim_panel.sim`) |
| The MySim panel's tail (0x00771440 to 0x007723F0): the two advice lists, the MySim shown, a place selected, the recount, its window procedure and message handler | 8 functions; the panel's first part (0x0076E6B0 to 0x00771440) is still the game's | ours (`ui.mysim_panel.part1`) |
| The news ticker of the city view (0x007723F0 to 0x00774450) and the five command buttons next to it | 20 functions | ours (`ui.mysim_panel.part1`, `part2`) |
| The MySim picker dialog (0x00774560 to 0x00776BF0) | 19 functions | ours (`ui.mysim_dialog.part1`, `part2`, two in `ui.mysim_panel.part2`) |
| A picture class of the city view (0x00776BF0 to 0x00777220), four functions of it | its blend routine (0x00776C30) is drawing code and stays the game's, as does the rest of the class | ours (`ui.mysim_dialog.part2`) |
| The advice popup (0x00762BA0 to 0x00763790) | 12 functions | ours (`ui.dialogs.base`) |
| The two budget windows (0x007637F0 to 0x00764820), two functions of a gauge control and six thunks shared by the dialogs | 27 functions | ours (`ui.dialogs.base`) |
| The rest of the MySim panel (0x0076E6B0 to 0x00771440): constructor, Start and Stop, the places recounted, the details pane, the advice list's rows, the dialog for picking a car or figure | 26 functions | ours (`ui.mysim_panel.part3`, `part4`) |
| The city view's budget panel (0x00764820 to 0x007693E0): taxes, loans, the department dialogs, both pages by month and by year | 28 functions | ours (`ui.dialogs.part1`, `part2`, `part3`) |
| Six small windows (0x00769400 to 0x0076B110): the building style panel, the rotation indicator, a text page dialog, a key filter for edit boxes, the new city name dialog, the options dialog's first part | 45 functions | ours (`ui.dialogs.part4`) |
| The rest of the options dialog and the graphs window of the city view with its chart feeding (0x0076B110 to 0x0076E500) | 33 functions; the graph control's box drawing (0x0076B660) and the framework graph's own thunks stay the game's | ours (`ui.dialogs.part5`, `part6`) |
| The map window of the city view, the data views (0x0079E400 to 0x007A6590): start and shutdown, the layers' occupant filter, the legend, the marks on buildings, the view switching, the buttons | 51 functions | ours (`ui.map.part1` to `part5`) |
| The small map of the city (cSC4WinMiniMap, 0x007A6580 to 0x007A9240): flags, colours, marked blocks, colour ramps, the mouse, start and messages | 35 functions | ours (`ui.map.part5`, `part6`) |
| The rest of the loading screen's picture (0x00777220 to 0x00777BB0): the shuffled lines of text, the status, start and shutdown | 6 functions; its two draws (0x00777220, 0x007773B0) stay the game's | ours (`ui.windows.part1`) |
| The region view's five dialogs and their shared base (0x00777BB0 to 0x007794E0): new region, load region, the delete question, a plain question, and the pick-a-city dialog with its tree of regions and cities | 40 functions | ours (`ui.windows.part1`, `part2`) |
| The budget panel's department dialog (0x007794E0 to 0x00785740): the window made in code, the controls it adds (texts, buttons, check boxes, pictures, combo boxes, sliders), the three page backgrounds, the ordinance page and its refill, the neighbour deals page (0x0077E600, 14 KB) and its refill (0x00781C90, 15 KB), the funding rows and a building's funding, stop and the destructor | 37 functions | ours (`ui.windows.part2` to `part6`) |
| The rest of the department dialog (0x00785740 to 0x0078C200): the constructor, the start that builds the page for its kind (8.7 KB), a budget group's page of departments and its refill (6.2 KB), a department's page of buildings with its rows, the buildings dialog opened from a row, the command handler and the window procedure | 8 functions | ours (`ui.department.part1` to `part4`) |
| Three small classes (0x0078C200 to 0x0078D140): a clock window, a tips window, the HTML control's handler for `sc4` links | 26 functions | ours (`ui.department.part5`) |
| Shared helpers (0x0078D140 to 0x0078FF40): following an `sc4` link and reading a linked record, a colour ramp, the child under a point, sets of windows (collect, new since, place by anchor), the message, question and plain modal windows, the makers of the city view's build menus from exemplars | 24 functions; set and map template code stays the game's | ours (`ui.panels.part1`) |
| The graphics options dialog (0x0078F550 and 0x0078FF40 to 0x00793070) | 9 functions | ours (`ui.panels.part1` to `part3`) |
| The advice list control (0x00793070 to 0x00793EA0, GZCOM class 0xCA1492AC): rows kept in a list and shown as one HTML table, its link handler for opening and closing a row | 21 functions | ours (`ui.panels.part3`) |
| The overlay picture window of the city view and the first methods of the frame window (0x00793EA0 to 0x007946A0) | 19 functions; the paints stay the game's | ours (`ui.panels.part3`) |
| The developers' animation recorder and the frame window that marks what it records (0x007946A0 to 0x00797BB0): settings, frame buffers, writing frames, start, stop, the mouse on the frame | 26 functions; the frame's paint stays the game's | ours (`ui.panels.part4`, `part5`) |
| The progress bar control (GZCOM class 0xAA5D16A9) and the info tip (0x00797BB0 to 0x0079A040): texts, fonts, icon, meter, anchor and bounds | 30 functions; both paints stay the game's (the tip's, 5.8 KB, also places the bubble) | ours (`ui.panels.part5`, `part6`) |
| The pop-up menu of the city view (0x0079A040 to 0x0079BAB0): items, the menu with its hover, choice, scrolling and timer, the scroll arrows around it | 56 functions; both paints stay the game's | ours (`ui.panels.part6`) |
| The cheat box, a window the mouse passes through, the input method's candidate window, the intro window, the picture file dialog, the map window's small methods (0x0079BAB0 to 0x0079E400) | 53 functions; the intro's and the file dialog's paints stay the game's | ours (`ui.panels.part6`, `part7`) |
| The city view, cSC4View3DWin (0x007E5630 to 0x007F5F00): its interface (zoom, rotation, level, filter, tools, picking, cursor text, cost indicator, effect triggers, last selection, mission panel), its start and shutdown with every panel made and let go, the loaded-city setup, the control panel's texts, the pop-up menus and build menus, the window sets of the four modes, the command handler (about 120 commands), the button handlers, the cheat codes, the key and mouse handlers, its messages, its record in the city file, and the hover tip helper the application keeps for it | 164 functions | ours (`ui.cityview.part1` to `part7`) |
| The main window (0x004E08C0 to 0x004E1320): start and shutdown, the key accelerators that turn keys into commands, the frame counter, the screens it holds (intro, region, city view) made and dropped, the cheat box run | 18 functions | ours (`ui.app.part2`) |
| The application's prompts (in cSC4App, 0x00454370 to 0x00455DA0): go to the region view, quit, quit from the region, close the city, new city, save with its notices, the terrain picture browser, the new city window's buttons, the obliterated city's writing | 9 functions | ours (`ui.app.part4`, `part5`) |

Still the game's in these ranges: generic string, list and smart-pointer code with callers all over the exe, and functions the compiler folded with unrelated classes.

## What is fenced off as drawing

The map's picture belongs to the renderer work and is not touched: its drawing 0x007A2F60 (which runs to 0x007A4884, with jump tables to 0x007A49B0), the functions it calls (0x007A2380, 0x007A2740, 0x007A1310, 0x007A1120, 0x007A00B0; the grid converters 0x007A2AE0, 0x007A2C70, 0x007A2E00; the point collector 0x007A1020 and the mapper 0x007A2080), and the scalers, painters and blenders only it uses (0x0079E600, 0x0079EB40, 0x0079ED90, 0x0079EFD0, 0x0079F230, 0x0079F4D0, 0x0079FEF0). None of these is rewritten, hooked or named in the address database. The controls that ask for a redraw (the view switch 0x007A49B0, the timed tick in 0x007A54D0, two buttons in 0x007A56E0) call 0x007A2F60 by its address, as the original does.

Five helpers just before the map window that only the fenced drawing calls are left with it, without address entries: the pixel blend 0x0079DC00, the blended fill 0x0079E0C0, the ring 0x0079E130 and the coordinate setters 0x0079DDE0 and 0x0079DDF0.

In the city view three functions are left as drawing: the cost indicator's amount drawn nine times into its picture (0x007EAC70); the mayor rating meter (0x007E8510), which builds a picture row by row in its first half and places the meter's arrows in its second, left whole; and the hand-over of the clock's time to the lighting manager with a full redraw (0x007E6E40). Everything the view asks of the renderer, the camera and the camera control goes by address or vtable as before, so the Renderer threads' replacements still take effect. The view's own code does not stop where the camera view class begins (0x007F6010): its paint (window slot 88, 0x007F6200), SetScrolling (interface slot 33, 0x007F64F0) and a few helpers lie among the camera's functions and are not touched.

Around the main window and the prompts, what is not interface code is left the game's on purpose. The two classes on either side of the main window are the application's background file installer (0x004DE2E0 to 0x004E08C0, a worker thread that copies game files) and the sound event system (cSC4AudioAEMSEventSystem, 0x004E1320 to about 0x004E2570). In the application class only the prompts are taken: `LoadCity` and `SaveCity` stay as they are (the test harness and other work depend on them), and so do the things the prompts lead to but that ask nothing: quitting itself (0x004544C0), making a city (0x004548B0), destroying one (0x00454A60), closing one (0x00455330), obliterating one (0x00454E50, which deletes the city's file) and the plain save by the city's own path (0x00454DA0), with the application's start, shutdown, resource setup, preferences and driver selection.

Left for another reason: the function that sets a named render setting from text (0x007ECDA0) has a real C++ `catch` around the game's own throwing number parsers, which our code cannot name; and the usual shared container code.

Left the same way, as drawing of other pictures: the paint functions (window slot 88) of the progress bar (0x00797CC0), the info tip (0x00798710), the overlay picture (0x00794100), the animation recorder (0x007943B0) and its frame (0x00794BF0), the pop-up menu (0x0079AA70) and its arrows (0x0079B0E0), the intro window (0x0079D490, which also times the intro's steps) and the picture file dialog (0x0079D6B0); the loading screen's two draws (0x00777220, 0x007773B0; its `Update` is ours and calls the draw by address); the small map's paint (0x007A79B0), its two picture builders (0x007A7FF0, 0x007A8640) and their nine pixel routines (0x007A65D0 to 0x007A6FB0); the advisor head view's Draw (0x004209D0); the graph control's box (0x0076B660); a blend routine of the city picture class (0x00776C30).

## How interface code is tested

None of this runs unless a window is open or the player clicks, so the usual parity legs do not reach most of it. Two switches call it directly, as its window or the mouse would, on both the original and ours, and put what it produced into the fingerprint:

- `OPENSC4_TEST_ADVICE_WINDOW=<day>` (token `advwin`): for each of the seven advisors the controller's worst mood and advice list; the rows it hands the list control, caught by a stand-in list (three image strings, the message and the title as HTML, the flag, the advice); the head view made, given each mood, its models loaded, advanced and dropped, with no window; then the controller driven through its screens (shown, each advisor's advice and news, the overview, hidden), its state compared after every step. `OPENSC4_TEST_ADVISOR_DUMP=1` logs each step.
- `OPENSC4_TEST_MYSIM_TOOLS=<day>` (token `mstools`): the picker's list of Sims; then for the MySim in the first place the home tool, the job tool and the dispatch tool, each made as its panel button makes it, swept over a grid of screen points, clicked on the first building it accepted, and ended. The MySims' own fields are in the fingerprint already, so a move or a new job shows there.

- `OPENSC4_TEST_TICKER=<day>` (token `ticker`): a news ticker of the test's own making, with no window: every advice of the city added (the list is kept by priority), the news list's rows and the ticker's three rows caught by the stand-in list, the ticker moved on twelve times, the inactive advices purged, some removed. The advice window's switch does the same with the controller's list (`all` in its token), so both row builders see every advice of the city, a few thousand rows, not only the ones on show.
- `OPENSC4_TEST_MYSIM_DIALOG=<day>` (token `msdlg`): the picker dialog run as the MySim panel runs it (made, Run, Close, released), with a stand-in for the window manager's modal loop that plays the player: the list sorted each way, rows picked, the name checked, a page button, the page strip's arrows and a picture button pressed through the window procedure. It does not press Accept.
- `OPENSC4_TEST_BUDGET_WINDOWS=<day>` (token `budwin`): the two budget windows made as the budget simulator makes them and worked through their window procedures. In the game as shipped neither layout loads (the token reports 0 windows on both sides), so this compares the constructors' failed path only.
- The tools' switch also calls the MySim's own panel methods: its picture buffer, Find (the camera sent to it), and the info tips over its home and its place, each put away again.

- The city view (cSC4View3DWin, found as the harness finds it for screenshots) keeps its windows as fields: the news ticker at +0x2DC, the MySim panel at +0x2E4, the budget panel at +0x2E8, the graphs window at +0x2F8, the style panel at +0x2FC, the map window at +0x300, the advice popup at +0x31C. Three switches work those live objects, not on screen:
  - `OPENSC4_TEST_MYSIM_PANEL=<day>` (token `mspanel`): the panel shown (which fills its places), each place with a MySim selected, its two advice lists and their rows, its details pane, the car and figure choices counted for each wealth tab, the recount, hidden again.
  - `OPENSC4_TEST_CITY_PANELS=<day>` (token `cvpanels`): the budget panel's figures read and written for both pages by month and by year, its tax incomes, the page shown and hidden; every graph number up to 24 shown in turn with its kind, counts, scales and rows; the style panel refreshed and hidden.
  - `OPENSC4_TEST_MAP_WINDOW=<day>` (token `mapwin`): the legend built for sixteen views; the marks collected for each kind, as a set; each view chosen in turn with the fields and the two colour tables it sets (this runs the game's own picture drawing, whose result is not compared), the plain view put back; and a small map of the test's own with no window: flags, colours, marked blocks, colour ramps.

- `OPENSC4_TEST_DIALOGS=<day>` (token `dialogs`): the dialogs that only a button opens, opened and worked by pressing their buttons through their window procedures, with a player in place of the window manager's modal loop: the budget panel's tax window (two rates changed, applied when the dialog ends), its loan dialog (the list scrolled, a loan taken) and five department dialogs; the options dialog (both pages, the text page it opens inside itself, Accept); the MySim panel's dialog for a car and for a figure (a choice pressed, then yes); and the map window's controls readied, a button of each row pressed, hidden again. Used with `OPENSC4_TEST_TUTORIAL_OFF=1`, since a tutorial city holds button presses back. The rates, the loan and the car change the city, alike on both sides: the funds in the budget's own fingerprint rise by the loan.

- `OPENSC4_TEST_WINDOWS=<day>` (token `windows`): a loading screen picture of the test's own made, readied and shut down, its shuffled lines compared (not drawn); the region view's dialogs run under the main window with players: new region and load region left with cancel, the two questions answered no and yes, the pick-a-city dialog made twice (its list of the region's cities compared, OK with nothing chosen and cancel pressed); and the budget panel's five department dialogs with their pages built, the scroll arrows pressed where a page has them, the ordinance page filled again and the funding rows refreshed. Nothing is made, deleted or changed. For the department dialogs the player has to do two things the modal loop does in the game: put the window under the main window, and send it the start message (0xA2BF8AD0) that builds the page. It leaves the closing to the game: the dialog posts its own end after the run, and the stop message and the destructor follow from the game's message pump. The same player plays the dialog inside the dialog: on a budget group's page the first two rows' buttons are pressed, each of which opens that department's buildings in a second dialog of the same class, run inside the first run. On the ordinance page it clicks a check box (without flipping it, so nothing is switched) and opens an ordinance's description, which it leaves for the dialog's end to drop: putting it away with its own button (0x384) under the stand-in left the game unable to exit, the original as well as ours (found when a batch's every run sat at exit until the watchdog; isolated by leaving out one press at a time). Last, three small classes as far as they go with no window of their own: the link handler made, started, stopped and deleted, the tips window's two cached answers, the clock made and deleted and then shown (it shows the computer's time, so its text is not compared; its window stays up for the rest of the run).

- `OPENSC4_TEST_PANELS=<day>` (token `panels`): the shared helpers and small windows called directly, none on show. `helpers`: a colour ramp of 256 entries, three links followed that lead nowhere, the child and the deepest child under twelve points of the main window, the main window's children collected twice. `menus`: a build menu made from the game's exemplars (each item's name, description, command and ID), then sized, an item found and chosen, the others switched off; a menu item of the test's own. `lists`: the real advice list control made by its class factory, given three rows, emptied, given one more, its rows read back each time. `tips`: an info tip's title, text, anchor, clear rectangle, bounds, margins and meter, with the "stale" bits after each (the same value set twice must not set them again). `small`: the progress bar's share clamped, the frame window's edge test over a grid of 49 points, the cheat box, the intro window and the picture file dialog made, asked a little and deleted. `modal`: the message window, the question window answered no and yes, the plain modal window, and the graphics options dialog started, a button of each choice group pressed through its window procedure and left without accepting, so the settings stay.

- `OPENSC4_TEST_CITY_VIEW=<day>` (token `cityview`): the live city view worked through its own interface and handlers, keeping to what neither moves the view (its rectangle is saved with the city), nor pauses the simulator (every zoom and rotation holds it for two real seconds), nor opens a window. `ids`: its four parts asked for by interface ID, the plain getters. `state`: the last selected building and lot set and read back; a made-up effect trigger asserted twice, asked for, retracted twice; the cost indicator set both ways and cleared; the level set to 2 and back; the filter read and set again; keyboard scrolling killed; the mouse reported in and out. `texts`: cursor texts set, changed and cleared, the list read back each time. `tools`: four tool commands and a menu command, the tool stack counted and the tool's ID read after each, Escape putting each away. `panel`: the control panel's texts refreshed, the data view left, the interface minimized and restored, the ground under one point picked. The view's start, its loaded-city setup, its messages and its record's writing run in every run without being driven.
- `OPENSC4_TEST_CITY_ZOOM=<day>` (token `cvzoom`, [cityview_autotest.cpp](../src/core/cityview_autotest.cpp)): the view's zoom and rotation methods called through its interface, 52 calls: every zoom set, the limits narrowed and widened, zoomed out and in as far as each goes, every rotation set, turned left and right four times each, zoom and rotation together. After each call the answer, the camera's zoom and rotation values, its fine zoom, CanZoomIn and CanZoomOut, the limits and whether the view holds the simulator; then everything put back (`home 1`: the camera's centre is where it was) and the simulator let go. 41 of the 52 calls change the view in the tutorial cities, through all five zooms and four rotations.
- `OPENSC4_TEST_CITY_MENUS=<day>` (token `cvmenu`, with `OPENSC4_TEST_TUTORIAL_OFF=1`, in an established city): the menus, panels and modes worked the way their buttons work them, as presses reported to the view (`{3, 0x287259F6, button}` to the part at +8), with a snapshot after each of about 110 steps: the mode, both menus (what each was opened for, its window's ID, visibility, place and children), every remembered choice, the open build menu's rows (texts, values, IDs, enabled), the tools in hand, the view window's child count, the five window sets, the bars, the mode buttons, which panels are shown. `popups`: the layout pop-ups opened, shut by their own button, replaced, stacked, Escape taking one thing at a time, the day and night and the signs pop-ups. `builds`: the disasters menu, then each pop-up opened in turn and every build menu button found in it pressed, one menu over the other (27 buttons in the tutorial cities: three zone menus, six transport, seven civic, three of the bar, five of god mode, three mission menus), with one zone row chosen. The buttons sit in the pop-ups, which exist only while open: a press sent without its pop-up is not delivered (`missed`). `panels`: five panels asked for, put away, asked for again, the data view left. `modes`: mayor, MySim, options and god through the buttons' notices, one let up, the toggle command twice, two window sets shown and hidden, the interface minimized and restored. No row that places a building or starts a mission is chosen. At the end the mode, the panels and the remembered choices are put back; `left 0` says no tool, no menu and no extra child window stayed.

- `OPENSC4_TEST_APP=<day>` (token `app`): `window`: the main window asked for its city view and its message target, its four screen makers called while a screen is up (each must refuse), a made-up message and a made-up cheat box request given to it, and the cheat box run with a player that leaves it at once. `prompts`: "go to the region view?" and "quit?" each answered cancel, the terrain picture browser left without a choice, the new city window's button handler given a window that is not its own; the city must still be there afterwards. "Quit from the region?" is not asked: in a city its window does not load, and a window that did not run counts as a yes, in the original as in ours, so the game quits (the first version of this test found that on the original). The main window's makers and its dropping of a screen run on every city load without being driven, as its start and shutdown do at the beginning and end of every run.

- `OPENSC4_TEST_VIEW_MENUS=<day>` (token `viewmenus`): the live city view's buttons pressed through its command handler, as a click does, each with the window it sits in (the menu opener looks the button up there). With the mayor's window set shown, each of five bar buttons opens its pop-up of buttons, up to three of those are pressed, and each menu that comes up (nine in the test cities: civic, transport and menu bar menus) is read (its items' IDs and names), the mouse moved over a row and off, and closed; in the first a row is pressed, which takes up the lot tool. Then the current mode's window set hidden and shown, the budget, graphs and style panels asked for and put away, two cheat codes that only set a flag, and the animation recorder shown (it stays up). Nothing is placed and the view is not moved.
- `OPENSC4_TEST_VIEW_MOVES=<day>` (token `viewmoves`): the view zoomed in and out and set to each zoom, rotated and set, the wheel turned both ways, scroll steps and scroll keys by command, keys and a character given to the tools, the mouse moved, and a right click, which centres the view. After each step the camera's zoom and rotation and the view's scroll state are read; at the end the simulator, which a zoomed view holds on real time, is let go. This switch has a leg of its own, and that leg gives no whole-city verdict: after it the original does not repeat itself (below). What the leg shows is the token. `OPENSC4_TEST_CITY_ZOOM` above, which keeps to the zoom and rotation methods and puts the camera back, does give verdicts; this one adds the wheel, the scroll commands, the keys and the right click.

Two workers translated the ticker's message handler (0x00772B60) independently, because it lay across their ranges; the two readings agreed call for call, and one was kept.

What a function leaves in EAX by accident is not compared: `Show` and `Hide` return whatever their last call left (a reference count), and the first version of the test, which hashed it, differed on every save for that reason alone.

## The advisor window

The advisor system keeps one controller (0x70 bytes, at advisor system +0x5C; no class ID), made for every city by its slot 24 and started at once: Start loads the window layout (resource key 0, 0x96A006B0, 0xCBC905CD), hides its three parts, becomes the window procedure and makes a head view on each of the seven overview panes.

| Offset | Field |
| --- | --- |
| +0x0C, +0x10, +0x14 | the chosen advisor, its type (-1), its index in the type table 0x00A83508 (-1) |
| +0x18 | the window |
| +0x1C, +0x38 | the seven pane windows and their head views |
| +0x54, +0x55, +0x56 | notifications registered, news shown, window shown |
| +0x58 | mode: 0 overview, 1 advice list, 2 news list |
| +0x5C | the list control |
| +0x60, +0x64 | the advices in the list (in reverse of the advisor's order) and a copy of the advisor's set |

`FillList` (0x0041E4B0) builds one row per advice from 28 fixed pieces: a title in the mood's colour (blinking red for mood 0, red for 1, green for 5, blue otherwise, grey when the advice is not active) in "StocletITC TT", the message in italic "Arta", and four pictures by mood.

The head view (0xF8 bytes) shows an advisor's animated head: it maps the mood to one of four animation pairs, loads the two models, and at rest alternates the idle loop one to three times with the second animation, drawing from its own random generator.

## The MySim tools

Four view input controls. None has a class ID; cSC4MySim makes them and hands them to the city view.

| Tool | Size | What it does |
| --- | --- | --- |
| Home (vtable 0x00A85800) | 0x4C | highlights houses the MySim could move to; a click moves it |
| Job (0x00A858D0) | 0x4C | highlights workplaces; a click changes its job |
| Move-in (0x00A85A20) | 0x4C | the first house, when a MySim moves into the city |
| Dispatch (0x00A85998) | 0x90 | picks the MySim's figure up, carries it with the cursor and puts it down; a double click sends a message |

The home, job and move-in tools show a "yes" or "no" picture at the cursor and an info tip with the building's name and two grade texts. The picker's list (0x2C bytes, vtable 0x00A85B14) holds the Sims the move-in dialog offers, from the game's exemplars and from `MySims.dat`, and can import families exported from The Sims: it finds that game through the registry, decodes each exported file, masks the portraits and rewrites `MySims.dat`.

Things the original does that ours keeps: the info tip's bar level for a lot value outside 1 to 3 comes from a stack slot that happens to hold 0; the dispatch tool's highlight takes two references on a figure and gives one back; a record made from an exemplar that lacks a property keeps stack garbage in that field.

## The news ticker, the popup and the picker dialog

The **news ticker** (0x84 bytes; the city view keeps it at +0x2DC) listens for the advisors' four advice messages and keeps the advices in a list by priority. It fills the news list with one row per advice (a twin of the advisor window's rows; a mood 0 title is plain red here and blinks only in the ticker) and shows up to three titles on a line that a timer scrolls: three seconds at rest, then a pixel every 40 ms for one line's height.

The **advice popup** (0x18 bytes) queues the urgent advices it hears of and shows them one at a time in a modal window with the advisor's head: the page as HTML in the mood's colours, the window grown to the text (at most 450) and centred. It shows nothing while a mouse button is down, a modal window is up or the option is off.

The **MySim picker dialog** (0x64 bytes) is what the MySim panel opens for an empty place: 21 pictures a page of the Sims from the picker's list, sortable by kind, sign or name, a name and biography box and a sign list; Accept moves the Sim in. Its Run loads the window, fills it and sits in the window manager's modal loop until Accept or Cancel.

The **budget windows** (0x28 and 0x60 bytes; budget simulator slot 24 makes one or the other) show seven department sliders and three tax spinners. Their layouts (0x691A0FB4, 0xE91A0FA3) do not load in the game as shipped, so they look like leftovers of development; the game's real budget panel is other code.

Things the original does that ours keeps: opening the large budget window overwrites the simulator's income totals until a spinner is moved; the ticker's scroll length is not set by its constructor; the page-number buttons of the dialog do not check their range.

## The region dialogs and the department dialog

The region view's dialogs share a base of 0x1C bytes (vtable 0x00AB5314, counted part at +4): the window at +0xC, the layout's instance at +0x10, the window ID at +0x14, the child that gets the keyboard at +0x18. Its slots: 3 the window's messages, 5 Run (the layout loaded under a parent, run as a modal window, destroyed; the answer is the number the run ended with), 6 and 7 the window came and went, 8 a button's ID, 9 cancel, 10 OK. Every dialog has OK 0x8A5B9959 and cancel 0xCA5B9966. The region view calls a maker function per dialog and acts on the answer itself (loading, deleting), so calling a maker alone changes nothing; only the new region dialog's OK does work of its own (it makes the region).

| Dialog | Maker | Layout, window ID | Answer |
| --- | --- | --- | --- |
| New region | 0x00777FF0 | 0x8A5AB1CB, 0xEA5BA0D1 | the new region's index, -1 on cancel |
| Load region | 0x007780C0 | 0x8A5AB1CC, 0x4A5BA0E7 | the list's choice, -1 on cancel |
| Delete region question | 0x00778130 | 0x8A5AB1CE, 0x6A5BA20C | yes or no |
| Plain question | 0x00778220 | 0x8A5AB1D0, 0x8A5AB1D0 | yes or no |
| Pick a city (0x68 bytes) | 0x00778D80 | 0x8A5AB1CD, 0x0A5BA192 | yes with the chosen record (three numbers and a path) |

The pick dialog fills a tree with every region of the nation and its cities of one size, then the downloads folder; it starts and shuts down every region but the current one to list it (disk reads, nothing written).

The department dialog (0x140 bytes, vtable 0x00AB5494, constructor 0x007862B0) is the budget panel's one dialog for four kinds of page. The number at +0x20 chooses: 1 the ordinances, 2 the neighbour deals, otherwise it is a budget group's ID and the page lists that group's departments with their line items and a funding bar each. The fourth kind is a second dialog of the same class opened over the first from a department's row (0x00786530), with the department's ID at +0x24: that department's buildings, each with a funding bar (hospitals and schools have a second one, for ambulances and buses). Its window is made in code (0x00779E50), not loaded from a layout, and every control is added by a small method that takes a parent, an ID and a place. Its window procedure (slot 4, 0x0078BCA0) takes the message apart into five arguments, unlike the other dialogs' two. The start message 0xA2BF8AD0 builds the page (0x007876B0); 0xA2BF8AD1 stops it.

What the command handler (0x0078B120) takes, by window ID: 0x67, 0xCC, 0x385 close; 0x6D writes the kept funding and closes; 0xCD switches every ordinance whose box was changed and closes; 0xCE and 0xCF close; 0x451 to 0x454 scroll the two lists; 0x12C upward are the ordinance check boxes (kept in the dialog until 0xCD); 0x1F4 upward open an ordinance's description, which 0x68 and 0x384 put away; 0x0ABCE600 upward open a department's buildings dialog; 0x0ABCDE00 upward on the buildings page move the camera to the building. Two notices change the city at once, not on closing: a funding bar moved (0x887113A4) sets the department's or the building's funding, and a deal's slider moved (0xE8711255) makes, resizes or cancels the deal. The service at 0x00B43D90, entered in the address database without a name, is the ordinance simulator: its slots match gzcom-dll's `cISC4OrdinanceSimulator`.

The deals page is two functions of 14 and 15 KB, each six copies of one row block (types 0x4773FFE0 to E5: power, water and garbage, bought and sold). They differ in small ways that are kept: which copies grow their vectors inline, when a row's cost is read, which list bumps its IDs after the blank row. Heading rows carry two dwords of stack garbage in the original (never written, copied into the row vector); ours stores zeros, and nothing was found that reads them.

## Parity

Everything on this page on, with the script and MySim simulator groups (2026-10-10; folders `ui4-*`, `ui6-*`, `ui7-*` under `%LOCALAPPDATA%\opensc4\parity\`):

| Leg | Result |
| --- | --- |
| 12 saves, 7 days, the advice window driven on day 3 | 12 of 12 |
| 12 saves, 7 days, a MySim moved in, the tools driven on three days, the advice window on day 5 | 11 same, 1 partly (Big City d0000: the original had two outcomes of its own, in the lots' token) |
| `saves-mysim`, 30 days, the tools driven on 24 days in a row | 3 same, 1 partly (the original had two and three outcomes on two of the four saves) |

In every run of these legs the `advwin` and `mstools` tokens of ours are ones the original also gave; the tools' token was the same on both sides for all 24 days of every `saves-mysim` run. The differences left are in other parts of the fingerprint, on saves where the original does not repeat itself once a MySim travels (its car moves on frame time, [mysim-agents.md](mysim-agents.md)).

One run died: a B run of an earlier batch with the MySim simulator ours and the interface code still the game's, with heap corruption on a free ten game days after its one tools drive. It did not happen again in 46 later runs (16 of them with 24 drives each, in the same configuration), on either side.

**Second round** (everything on this page on; `ui9-*`, `ui11-*`):

| Leg | Result |
| --- | --- |
| 12 saves, 7 days: the picker dialog, the advice window, the ticker and the budget windows driven | 10 same, 2 partly (a first B run off in the lots' token; both repeats matched) |
| 12 saves, 7 days: a MySim moved in, then the tools, the dialog, the advice window and the ticker | 11 same; Rush Hour d0365 with six different saves from six runs, the original's three among them, and the same fingerprints every day |
| `saves-mysim`, 30 days: the tools on ten days in a row, the dialog, the ticker | 3 same; Making Money m0060 with one token off on one day (below) |

In every run of every leg the five interface tokens of ours are ones the original gave, and in the last two legs the original's own were the same in all of its runs. The ticker and the advice window each built 2,815 rows in Big City, the same on both sides.

Making Money m0060 on day 25: the MySims' query hash had one value in the original's three runs and another in ours. In two earlier batches the original gave both of those values on that save and day (once in the same batch), with a MySim travelling by car; so it is the known noise, but this run alone does not show it.

`Find` (the camera sent to the MySim) is behind `OPENSC4_TEST_MYSIM_FIND=1`: the camera travels on frame time, and with it on, the sweeps of later days stopped repeating even for the original. It ran in one leg with a single drive, where the tools' token still matched on all 12 saves.

**Third round** (the dialog range, the rest of the MySim panel and the map window; all 25 `ui.*` groups on; `ui13-*`, `ui14-*`, `ui15-*`):

| Leg | Result |
| --- | --- |
| 12 saves, 7 days: the map window driven | 12 of 12 |
| 12 saves, 7 days: the picker dialog, the city panels, the advice window, the ticker and the map window on five days | 12 of 12 |
| 12 saves, 7 days: the city panels, the advice window, the ticker (before the map window's parts) | 10 same, 2 partly (a first B run off in the lots' token) |
| 12 saves, 7 days: a MySim moved in, then the MySim panel and the tools | 11 same; Rush Hour d0365 where the original's own three runs gave three results for the tools' sweep |
| `saves-mysim`, 30 days: the MySim panel, the tools, the dialog, the city panels | 2 same, 2 partly, on saves where the original's runs differ among themselves |

In every run the interface tokens of ours are ones the original gave, except on Rush Hour d0365 in the move-in leg, where the original does not repeat its own. The map window's token was the same on both sides on all 12 saves: the legend for sixteen views, the marks of each kind (52 in Big City), all sixteen views chosen, the small map's state. The city panels' token likewise: the budget panel's figures, 18 graphs with 129 rows, the style panel.

Two faults of the tests themselves, found by the original not repeating: the MySim's "show location" makes the view follow a travelling MySim, after which the view rectangle in the traffic map's record differed in every run of all 12 saves (it is now behind `OPENSC4_TEST_MYSIM_FIND` with `Find`); and the MySim panel only fills its places when shown, so its first test compared an empty panel.

A run of the original with the coverage meter and every switch (Big City with MySims, 10 days; `analysis\cov-ui3`) reaches 276 of the 419 functions. Not reached:

- the budget windows of the first table beyond their constructors (their layouts do not load), the advice popup's window, the gauge and the paced head view;
- the budget panel's tax window, loan dialog and department dialogs (modal; they would need the stand-in loop) and what only they call;
- of the six small windows, everything but the style panel and the rotation indicator: the text page, the key filter, the new city name dialog and the options dialog are made only on demand;
- the map window's three button-row choosers, its mode setters and its controls being readied (reached only from button presses); the filter's Read and Write (no test saves or loads one); the small map's mouse handlers;
- the MySim panel's delete, the pick-one dialog's run, the two script callbacks; the picker dialog's Accept;
- the dispatch tool's click and the move-in tool's mouse handlers; the import from The Sims past "not installed".

**The dialogs pressed** (`ui16-*`, all 25 groups): 12 of 12 on the usual saves; 3 of 4 on `saves-mysim` with the car and figure dialog, the fourth off by one in the harness's own tick count in a first B run. The `dialogs` token of ours is the original's in every run. With this switch a coverage run (`analysis\cov-ui4`) reaches 29 more functions, 305 in all: the tax window, the loan dialog, the department dialogs' opening, the text page and options dialogs, the pick-one dialog's run, the map's three button rows and its controls. Still not reached: the new city name dialog (its layout comes from its maker, not traced), the dispatch and move-in clicks, the import from The Sims, and the windows that need an urgent advice or a layout the game does not ship.

**The region dialogs and the department dialog** (`ui18-*`, all 31 `ui.*` groups on, 2026-10-10):

| Leg | Result |
| --- | --- |
| 12 saves, 7 days: the dialogs pressed, then the windows test | 10 same, 2 partly (a first B run off in the lots' token on Big City d0000 and by one harness tick on Rush Hour d0000; the repeats matched) |
| `saves-mysim`, 30 days: the windows test | 3 same, 1 partly, on a save where the original's own runs differ |

The `windows` and `dialogs` tokens of ours are the original's in every run: the loading picture's shuffled lines, the region dialogs' answers and the pick dialog's list, the five department pages with their fields after every arrow press and refill.

The first batch failed on every save, which is what the test is for: the load region dialog passed 1 to a getter of the nation (slot 7, no arguments) where the 1 is the second argument of the list call after it, an argument pushed early. Found by switching hooks on in halves; the nation's and the window class's calls in all six files were then checked against each target's `ret N` (169 window calls), and every call on a deal and on the neighbours simulator likewise, since no test city has a neighbour deal.

A coverage run (`analysis\cov-ui5`) reaches 59 of this range's 83 functions, 364 of 502 in all. Not reached here: the deals page's rows, sliders and its refill (0x00781C90 whole; the tutorial cities have no deals, so only the "no deals" message runs), a building's funding being set and the pending changes written (they change the city), the new region dialog's OK (it makes a region), the load region dialog's OK, the combo box and two text makers, and the copy helpers of vectors that never grew.

**The rest of the department dialog and three small classes** (`ui20-*`, `ui21-*`, all 36 `ui.*` groups on, 2026-10-11):

| Leg | Result |
| --- | --- |
| 12 saves, 7 days: the dialogs pressed, then the windows test with the buildings dialog | 12 of 12 |
| `saves-mysim`, 30 days: the windows test | 4 of 4 (`ui21`; in `ui20` three matched and the fourth lost its A run to the crash below) |

The `windows` token of ours is the original's in every run: all five department pages built by our start, the arrows, a check box, a description, two buildings dialogs opened inside each group's dialog, their rows filled again.

One run of the original died in the windows test with the access violation at 0x005A62A2 that [parity.md](parity.md) lists as a rare crash of the game on either side (a freed resource record read while a resource is opened; the test loads window layouts). It was one run in 68 of this test.

A batch before these (`ui19-*`) gave the same verdicts but took ten minutes a run, each sitting at exit until the watchdog: the test's fault, described with the switch above.

A coverage run (`analysis\cov-ui6`) reaches 23 of this range's 34 functions (all eight of the department dialog's, 15 of the 26 small ones) and four more of the range before, 391 of 536 in all. Not reached: the tips window's own window (no test city has a tip to show, so only its search and key list run), the clock's close and shutdown, the link handler's link test.

**Helpers, menus, the advice list and the small windows** (`ui23-*`, all 43 `ui.*` groups on, 2026-10-11):

| Leg | Result |
| --- | --- |
| 12 saves, 7 days: the panels test, the windows test, the dialogs pressed | 12 of 12 |
| `saves-mysim`, 30 days: the windows test, the panels test | 4 of 4 |

The `panels` token of ours is the original's in every run, all six parts of it: the first single run with the seven groups on already matched.

The batch before (`ui22-*`) gave 11 of 12 and 4 of 4; its twelfth save lost a run of ours to the access violation at 0x005A62A2, at the same step of the windows test where a run of the original had died two batches earlier: just after the test asked the tips window for its key list, which reads every exemplar again. Twice at one step in some 110 runs, once on each side, is the game's stale resource record ([parity.md](parity.md), [crashes.md](crashes.md)) met by a test that provokes it, so the test no longer asks for the key list; `ui23` then ran without a crash.

A coverage run (`analysis\cov-ui7`) reaches 131 of this range's 238 functions, 522 of 774 in all. Not reached:

- the animation recorder and its frame window's mouse (26 functions): a developers' window nothing opens unattended; its start, stop and writing end in message boxes and files;
- the pop-up menu's life in a window (about 40): hover, scrolling, the arrows, the timer, the mouse; a menu is made and worked by the test, but not shown;
- the cheat box, the candidate window, the intro window and the picture file dialog beyond being made and deleted (about 25): they need the keyboard, an input method, the game's start or a browser of the disk;
- the modal buttons object's click (the player answers for it), the graphics options dialog's Accept (it would write the settings), a few class ID getters only the framework's start calls.

**The city view** (`ui24-*`, all 50 `ui.*` groups on, 2026-10-11):

| Leg | Result |
| --- | --- |
| 12 saves, 7 days: the city view test, the panels test, the windows test, the dialogs pressed | 9 same, 3 partly |
| `saves-mysim`, 30 days: the city view test, the windows test | 3 same, 1 partly |

The four interface tokens of ours are the original's in every run, and the original's own repeat in all of its runs. Each part alone already matched the original in a single run but one: the view's start (0x007EDEB0) gave the game's localized string object 0x14 bytes of stack where it takes 0x20, and its constructor overran the neighbours; found by switching the part's hooks on one at a time.

The partly verdicts are in other parts of the fingerprint, and each shares an outcome with the original: the lots' token on Big City d0000 and d0365, one harness tick on Rush Hour d0000, the flammability part on a MySim save. Three in one leg is more than the one or two of earlier rounds, and on six saves the original's own three runs gave two or three outcomes, so I checked whether the new test disturbs a run: 87 more runs of the original alone on Big City d0000, 80 of them with the city view test (whole, or some of its blocks). Seven of those 80 parted from the rest; in every one of the seven the fingerprint already differs on the first day after loading, two days before the test does anything. That is the known noise of that save's first day when the machine is busy ([parity.md](parity.md)), which a batch of two games at once provokes; the test's blocks run alone or in pairs repeated 48 times out of 48, and the seven runs without it happened to repeat.

A coverage run (`analysis\cov-ui8`) reaches 95 of the view's 164 functions, 618 of 938 in all. Not reached, by choice of the test or for want of a trigger:

- everything that moves the view or holds the simulator: zoom, rotation, the wheel, right-click centring, edge and key scrolling (16 or so);
- the pop-up and build menus being opened and closed, the layout's and tool bar's buttons, the menu choices (about 20): they make and remove child windows of the live view;
- the four modes' window sets being switched, the panels asked for by ID, the day and night switch;
- the mouse and key handlers of the 3D view's window, which hand events to the tools;
- the dialog that names a new city, the leave-city question, the terrain setting question (modal);
- the mission panel and menu (no mission in the test cities), the cheat codes, the animation recorder's show and hide, the building menu maker.

**The main window and the application's prompts** (`ui26-*`, all 53 `ui.*` groups on, 2026-10-11):

| Leg | Result |
| --- | --- |
| 12 saves, 7 days: the app test, the city view test, the panels test, the windows test | 9 same, 3 partly |
| `saves-mysim`, 30 days: the app test, the city view test | 4 of 4 |

The interface tokens of ours are the original's in every run. In the three partly saves (Big City d0000 and d0365, Making Money d0090) the runs that part do so at the first or second fingerprint line, and the first test of the leg runs after the third: so the difference is there before any interface test has run, the load noise of the first days again, and each save shares an outcome with the original.

The first version of the test asked "quit from the region?" inside a city and answered cancel; the city went away all the same, with our code and, as the next run showed, with the original alone: that window does not load in a city, and the function takes "no window ran" for a yes. The question is left out of the test.

A coverage run (`analysis\cov-ui9`) reaches 14 of these 27 functions, 636 of 965 in all. Not reached: the main window's constructor, start, shutdown and destructors, which run before the hooks go in and after the meter stops (they do run as ours at the end of every run, where a fault would lose the run's save); its frame rate getter and message target; and of the prompts everything that would save, close, quit or found a city: the save request and its notices, close city, new city, quit from the region, the obliterated city's writing.

**The city view's zoom, rotation and menus** (`cv-all2`, the seven `ui.cityview.*` groups on, 2026-10-11): `OPENSC4_TEST_CITY_MENUS=3`, `OPENSC4_TEST_CITY_ZOOM=4` and the tutorial off, twelve saves for seven days. The `cvzoom` and `cvmenu` tokens of ours equal the original's in every run of every save (41 of 52 zoom and rotation calls changing the view, 27 menu buttons pressed, nothing left open). Whole-city verdicts: 11 same, 1 partly (Big City d0000, where the original gave three outcomes itself). One save with every interface group on gives the same two tokens. Two deliberate faults were tried in a local build and taken out again: the wrong remembered-choice slot for the menu of button 0x4A22EA06 crashed the menu test at once; ZoomIn asking the renderer even at the limit changed nothing, because the renderer refuses there too. A 7-day run exits normally on both sides, so closing the menus the game's own way (slots 59 and 60, Escape) does not bring the exit hang.

**The coverage pass** (`ui28-*`, all 53 `ui.*` groups on, 2026-10-11): two tests for what the earlier ones kept away from.

| Leg | Result |
| --- | --- |
| 12 saves, 7 days: the view's menus, the app test, the city view test | 11 same, 1 partly (Big City d0000) |
| `saves-mysim`, 30 days: the view's menus | 2 same, 2 partly |
| 12 saves, 7 days: the view moved (run twice, `ui28-mv`, `ui29-mv`) | no whole-city verdict (below); the `viewmoves` token the same in all 144 runs |

In the first two legs the interface tokens of ours are the original's in every run and each partly save shares an outcome with the original. In the third the original gave three outcomes in three runs on nearly every save, and so did ours, so the sets say nothing. The first run of the leg left the simulator held (a zoomed view holds it until a stopwatch on real time runs out); the second lets it go at the end of the test, after which single runs of the original on one save repeated three times out of three, but in the batch, two games at a time, it still did not repeat. The cause is not pinned down: the scroll steps and the centring move the camera, and the view's place is known to feed the traffic map ([parity.md](parity.md)), which is the likeliest reason. The leg's one token, the camera's zoom and rotation and the scroll state after each of some thirty steps, was the same in every run of both sides.

Two faults of the menus test, both found on the original: the menu opener looks the pressed button up in the window given with the click, so the anchor must be the button's own parent; and in a city just loaded no mode is up, so the bar's buttons are not there until a window set is shown.

A coverage run with both (`analysis\cov-ui10`) brings the count to 701 of 965. Still not reached: the menus' wheel, arrows and timer, the mission panel and menu, the naming, leaving and terrain questions, the mode buttons themselves, the left click on the 3D view, the recorder's recording, the cheat box in use, the deals page's rows, and what would save, quit or found a city.

