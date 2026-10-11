# Auto save

The game saves the city by itself every few minutes of play and keeps the last few of those saves as copies. An improvement beyond the original ([ROADMAP](../ROADMAP.md), "Improvements: the rules"): off by default, and with it off nothing changes.

```ini
[replacements]
game.autosave = ours

[autosave]
minutes = 15        ; between saves, 1 to 120
fast = on           ; off: also update the region view's picture of the city (slower)
count_paused = off  ; on: paused time counts towards the next save
background = off    ; on: save even while another program is in front
keep = 3            ; copies kept per city, 0 to 100; 0: none
```

## What it does

`game.autosave` ([src/core/game_autosave.cpp](../src/core/game_autosave.cpp)) adds a system service the game calls when idle. It counts the time a city has been played, and when the interval has passed it calls the game's own save (`cISC4App::SaveCity`), the one Ctrl+S ends in. The city file is written by the game's code, unchanged, so it loads in the original game and with every mod as any other save.

A save waits until all of these hold; the count is kept meanwhile, so the save comes as soon as they do:

- the city has been established (a new city that has not been named yet is never saved),
- the game allows saving (it does not in some tutorial steps),
- the simulation is not paused,
- no dialog is open,
- the game is the program in front (unless `background = on`).

Time is real time, not game time. Entering another city starts the count again.

## Copies

After each of its saves, the service copies the city file to `<user folder>\AutoSave\<region>\<city file name>\<yyyy-mm-dd>.sc4`, named by the game date, and deletes the oldest copies beyond `keep`. Only auto saves are copied, not the player's own. If a save is ever cut short (a crash, a power cut) and the city no longer loads, the last copy is at most one interval older: copy it over the city's file in `Regions\<region>` under the city's file name. There is no button for that yet.

## Credit

The rules for when a save is allowed and the idle-service shape are from [sc4-auto-save](https://github.com/0xC0000054/sc4-auto-save) by Nicholas Hayes (MIT License; the notice is in the source file). Its settings `IntervalInMinutes`, `FastSave` and `IgnoreTimePaused` are `minutes`, `fast` and `count_paused` (reversed) here. Where the mod follows the game's pause and focus messages, ours asks the simulator and Windows on each idle call. The copies and `background` are ours. If that plugin is installed too, ours leaves the saving to it.

## Checked

2026-10-11, unattended, Making Money, with `OPENSC4_TEST_AUTOSAVE_SECONDS=<n>` (a test-only interval in seconds) and `background = on`:

- An interval of 5 s over a game year: three saves, three dated copies.
- An interval of 4 s with `fast = off` and `keep = 2`, 500 game days: six saves, two copies left. A full save took 0.5 to 0.8 s in some runs and about 11 s in others (the region picture being drawn or not), during which the game stands still; that is why `fast` is on by default.
- A kept copy decodes whole with `tools/re/sc4save.py --check`, loads as the city's file and runs on for 30 days.

Not checked by hand yet: the pause, dialog and in-front rules in a played session. With the test clock (`determinism.virtual_clock`) a full save stalled for two minutes once; that clock is for parity runs, not for play.
