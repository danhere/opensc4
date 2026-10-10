# Interface code

The game's own interface classes, rewritten piece by piece like the simulators ([parity.md](parity.md)). Build 641 addresses throughout. Drawing is not part of this page: a function that itself sets render states or draws stays the game's until the renderer work reaches it ([renderer.md](renderer.md)); calls into drawing, model and window services are kept as plain calls.

| Piece | Code | State |
| --- | --- | --- |
| The advisor window's controller (`SC4AdvisorHelper`, 0x0041D400 to 0x00420030) | 26 functions | ours (`ui.advice_text.part1`, `part2`) |
| The advisor's head view (0x00420030 to 0x00420D30) | 14 functions; `Draw` (0x004209D0) is drawing code and stays the game's | ours (`ui.advice_text.part2`) |
| The MySim home and job tools (0x00437AF0 to 0x00439380) | 15 functions | ours (`ui.mysim_tools.part1`) |
| The MySim dispatch and move-in tools (0x00439380 to 0x0043AA30) | 21 functions | ours (`ui.mysim_tools.part2`) |
| The MySim picker's list of Sims and the import from The Sims (0x0043AA30 to 0x0043D0F0) | 19 functions | ours (`ui.mysim_tools.part2`, `part3`) |

Still the game's in these ranges: generic string, list and smart-pointer code with callers all over the exe, and functions the compiler folded with unrelated classes.

## How interface code is tested

None of this runs unless a window is open or the player clicks, so the usual parity legs do not reach most of it. Two switches call it directly, as its window or the mouse would, on both the original and ours, and put what it produced into the fingerprint:

- `OPENSC4_TEST_ADVICE_WINDOW=<day>` (token `advwin`): for each of the seven advisors the controller's worst mood and advice list; the rows it hands the list control, caught by a stand-in list (three image strings, the message and the title as HTML, the flag, the advice); the head view made, given each mood, its models loaded, advanced and dropped, with no window; then the controller driven through its screens (shown, each advisor's advice and news, the overview, hidden), its state compared after every step. `OPENSC4_TEST_ADVISOR_DUMP=1` logs each step.
- `OPENSC4_TEST_MYSIM_TOOLS=<day>` (token `mstools`): the picker's list of Sims; then for the MySim in the first place the home tool, the job tool and the dispatch tool, each made as its panel button makes it, swept over a grid of screen points, clicked on the first building it accepted, and ended. The MySims' own fields are in the fingerprint already, so a move or a new job shows there.

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

## Parity

Everything on this page on, with the script and MySim simulator groups (2026-10-10; folders `ui4-*`, `ui6-*`, `ui7-*` under `%LOCALAPPDATA%\opensc4\parity\`):

| Leg | Result |
| --- | --- |
| 12 saves, 7 days, the advice window driven on day 3 | 12 of 12 |
| 12 saves, 7 days, a MySim moved in, the tools driven on three days, the advice window on day 5 | 11 same, 1 partly (Big City d0000: the original had two outcomes of its own, in the lots' token) |
| `saves-mysim`, 30 days, the tools driven on 24 days in a row | 3 same, 1 partly (the original had two and three outcomes on two of the four saves) |

In every run of these legs the `advwin` and `mstools` tokens of ours are ones the original also gave; the tools' token was the same on both sides for all 24 days of every `saves-mysim` run. The differences left are in other parts of the fingerprint, on saves where the original does not repeat itself once a MySim travels (its car moves on frame time, [mysim-agents.md](mysim-agents.md)).

One run died: a B run of an earlier batch with the MySim simulator ours and the interface code still the game's, with heap corruption on a free ten game days after its one tools drive. It did not happen again in 46 later runs (16 of them with 24 drives each, in the same configuration), on either side.

Not reached by the tests (from what the two switches call; no coverage run has been made with them yet):

- the controller's window procedure and message handler (button clicks, advices added and removed while the window is open), and the head view's Init, paint and frame tick, which need its window;
- the list's rows beyond what the test cities have: the runs looked at handed over one row in all, so the mood colours and the grey, inactive form are compared on few rows;
- the move-in tool's mouse handlers (the test moves the MySim in without the tool's click), the dispatch tool's click (pick up, put down, double click), and the tools' key handling beyond Escape;
- the import from The Sims past "not installed": it needs that game's files or a made-up tree of them, and it rewrites `MySims.dat`.
