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

Still the game's in these ranges: generic string, list and smart-pointer code with callers all over the exe, and functions the compiler folded with unrelated classes.

## What is fenced off as drawing

The map's picture belongs to the renderer work and is not touched: its drawing 0x007A2F60 (which runs to 0x007A4884, with jump tables to 0x007A49B0), the functions it calls (0x007A2380, 0x007A2740, 0x007A1310, 0x007A1120, 0x007A00B0; the grid converters 0x007A2AE0, 0x007A2C70, 0x007A2E00; the point collector 0x007A1020 and the mapper 0x007A2080), and the scalers, painters and blenders only it uses (0x0079E600, 0x0079EB40, 0x0079ED90, 0x0079EFD0, 0x0079F230, 0x0079F4D0, 0x0079FEF0). None of these is rewritten, hooked or named in the address database. The controls that ask for a redraw (the view switch 0x007A49B0, the timed tick in 0x007A54D0, two buttons in 0x007A56E0) call 0x007A2F60 by its address, as the original does.

Left the same way, as drawing of other pictures: the small map's paint (0x007A79B0), its two picture builders (0x007A7FF0, 0x007A8640) and their nine pixel routines (0x007A65D0 to 0x007A6FB0); the advisor head view's Draw (0x004209D0); the graph control's box (0x0076B660); a blend routine of the city picture class (0x00776C30).

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
