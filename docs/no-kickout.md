# No kick-out of lower wealth

A building's occupants are no longer thrown out to make room for a wealthier group. An improvement beyond the original that is **on by default** (Dan, 2026-10-11), an exception to "behind a setting, off by default" ([ROADMAP](../ROADMAP.md), "Improvements: the rules").

**This changes the simulation on purpose.** A city played with it develops differently from the original game. To get the original behaviour back:

```ini
[replacements]
game.no_kickout = original
```

The parity harness does that by itself: `scripts/run-parity.sh` adds `game.no_kickout=original` to each side's list unless the list names `game.no_kickout`, so replacement code is still compared with the original game. The other harness scripts do not: `scripts/run-autotest.sh`, `scripts/run-save-test.sh` when used by itself, and the screenshot runs (`scripts/run-shots.sh`, `run-shots-all.sh`) run with it on unless `OPENSC4_REPLACE` says otherwise.

## What it does

The tract developer has one tuning value, "kick out lower wealth": property 0x47E2C540 of the exemplar 0x6534284A, 0xE7E2C2DB, 0xE8DA7677, true in the game's files. The developer reads it into its +0x6C when a city starts (LoadTuning, 0x00706FC0, called from Init) and asks it in three places: when a lot is weighed for a tract and when a building is occupied ([tract.md](tract.md), `tract_lots.cpp`), and when a farm is developed (`tract_buildings.cpp`).

`game.no_kickout` ([src/core/game_no_kickout.cpp](../src/core/game_no_kickout.cpp)) listens for the message the application sends before a city's Init (0x26D31EC0; sent by both LoadCity, at 0x0045588F, and MakeCity, at 0x0045492F), asks the resource manager for that exemplar and sets the property to false. The resource manager keeps loaded exemplars for a while, so the developer's LoadTuning, which follows at once, gets the changed copy. No game code is patched and no file is changed. After the city's Init the log says what the developer ended up with; if that is not 0, it says the setting is not in effect for that city.

It works the same with the game's tract developer and with ours (`simulators.tract.*`): both read the exemplar through the resource manager. The value stays false in the cached exemplar for the session; nothing else reads that property.

## Credit

The method is from [sc4-no-kick-out](https://github.com/0xC0000054/sc4-no-kick-out) by Nicholas Hayes (MIT License; the notice is in the source file). If that plugin is installed too, both do the same thing to the same value.

## Checked

2026-10-11, unattended, Making Money, three game days each, from the log:

- On (the first three runs were made while the setting was still off by default, with `game.no_kickout = ours`), the game's tract developer: the exemplar's value was 1 and is set to 0 before the city's Init; the developer loaded 0.
- On, our tract developer (`simulators.tract.* = ours`): the same; our LoadTuning ran after the change and loaded 0.
- With the test switch `OPENSC4_TEST_NO_KICKOUT=observe` (the value is read but left alone): the exemplar says 1 and the developer loaded 1, the original value.
- `game.no_kickout = original`: no line of this setting in the log but the registry's `game.no_kickout original`; nothing is subscribed.
- On by default, nothing set: the same two lines as the first run (the value set to 0, the developer loaded 0).
- Parity batch with the edited `run-parity.sh` and nothing extra on either side (12 saves, 7 days, two jobs): both sides' lists end in `game.no_kickout=original` and their logs say `original`; 11 same, 1 partly (Big City d0365: the two sides, with the same settings, gave 1 and 2 outcomes, 1 shared).

Not checked: a new city (MakeCity sends the same message before the city's Init, by the disassembly at 0x0045492F, but no unattended test founds a city), and what a city played with it on looks like after some years.
