# City date sync

Keeps the cities of a region on the same date: play a city to 2015, save, open its neighbour still in 2003, and the neighbour starts in 2015 too. A fix beyond the original, on by default: cities of one region drifting years apart is a flaw of the game, not a way to play it (Dan, 2026-10-11). To turn it off:

```ini
[replacements]
game.date_sync = original
```

## What it does

`game.date_sync` ([src/core/game_date_sync.cpp](../src/core/game_date_sync.cpp)) listens to two of the game's messages:

- **After a save** (0x26C63345, also after an auto save): if the city has been established, it remembers the city's date and its region (the folder of the city file).
- **After a city loads** (0x26D31EC1): if the city has been established, is in the same region and its date is earlier than the remembered one, the city's date moves forward to it. A city with a later date is left alone; dates never go back.

The date is moved the way the game's `SimDate` cheat moves it: the simulator's date is set to the day before, then the clock steps on to the remembered day. Nothing else in the city changes; the time skipped is not simulated, as with the cheat.

The remembered date lasts for the session: after restarting the game, the first city saved sets it again. Cities in other regions are not touched.

## Credit

From [sc4-city-date-sync](https://github.com/0xC0000054/sc4-city-date-sync) by Nicholas Hayes (MIT License; the notice is in the source file): the rule (remember on save, move forward on load, established cities in one region only). Two differences: the mod sends the `SimDate` cheat, which the game accepts only with its debug functionality on, so the mod turns that on for the whole session; ours does the cheat's two steps itself and leaves debug off. And the mod tells regions apart by the order of the region view's messages; ours compares the cities' folders. Do not install the mod's DLL as well.

## Checked

Offline: telling regions apart by folder (`date_sync_region` test). In the game, 2026-10-11: a save of Making Money remembered its date (`date sync: saved 2008-11-06 for ...\regions\timbuktu`; the folder may come as a short 8.3 name, the same one each time). Not checked yet: moving a city's date on load, which needs two cities loaded in one session.
