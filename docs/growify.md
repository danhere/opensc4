# Growify

A cheat that turns plopped buildings into growable lots of a zone, so a plopped house or shop becomes part of the zoned city around it. A tool beyond the original, always there with no setting: it does nothing until typed, so the game plays exactly like SC4 without it (Dan, 2026-10-11: plain tools are base game).

## Using it

In a city, the cheat box (Ctrl+X) takes:

```
growify <zone type> <zone density> [make historical]
```

- `<zone type>`: `Residential`, `Commercial`, `Agriculture` or `Industrial`. Only the first letter counts, so `R`, `Res` and `residential` are the same.
- `<zone density>`: `Low`, `Medium` or `High`, again by the first letter. Agriculture has one density and needs none (`growify Agriculture`); industry has no low density.
- `[make historical]`: `true` (the default) or `false` (also `1`, `0`). Historical keeps the game's redevelopment from replacing the building; with `false` the lot keeps whatever historical setting it had.

Examples: `growify Commercial Medium`, `growify r h`, `growify I M false`. The answer ("Growified 12 commercial lot(s).") comes in a message box; we have no in-game interface of our own yet (ROADMAP P2). Nothing is undone by the cheat: save first if unsure.

## What it does

[src/core/game_growify.cpp](../src/core/game_growify.cpp) registers the cheat with the game's cheat manager. For each building in the city whose exemplar `Purpose` (property 0x27812833) matches the zone type, it looks at the building's lot, and if the lot is plopped (zone type 15), it:

1. writes the new zone into the zone grid on every cell of the lot,
2. asks the lot to refresh its zone type from the grid (`cISC4Lot::UpdateZoneType`),
3. marks the lot historical, unless told not to.

| Zone type | Building purposes converted |
| --- | --- |
| Residential | residence (1) |
| Commercial | services (2), office (3) |
| Agriculture | agriculture (5) |
| Industrial | processing (6), manufacturing (7), high tech (8) |

Tourism (4) and other (9) buildings are left alone. Every step is a call through the game's public interfaces; nothing is patched. The command parsing and the purpose table are in [src/core/growify_command.cpp](../src/core/growify_command.cpp), tested offline (`tests/growify_command_test.cpp`).

## Credit

From [sc4-growify](https://github.com/0xC0000054/sc4-growify) by Nicholas Hayes (MIT License; the notice is in the source file): the cheat's syntax, the purpose table and the conversion steps. Ours answers in a Windows message box where the mod uses the game's notification dialog, and registers under its own cheat ID; do not install the mod's DLL as well, or one command may run twice.

## Checked

Offline: the command parser and the purpose table (`growify_command` test). Not yet checked in the game.
