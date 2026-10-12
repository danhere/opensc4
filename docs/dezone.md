# De-zoning that keeps roads

The game's de-zone tool also removes the streets inside the area. With this on, holding Shift while de-zoning leaves every cell with a road, rail or other transport network as it is. An improvement beyond the original that is simply part of the game: without Shift held the tool is the original's, so there is nothing to switch on. `game.dezone = original` under `[replacements]` in `opensc4.ini` takes it out.

The key is read each time the tool looks at the area (as the mouse moves, and when the button is let go), so the price shown follows it; what is held when the button is let go decides.

## How

`cSC4ZoneManager::PlaceZone` (0x004DD780) takes the area as a cell region and already clears from the caller's region the cells that cannot take the zone. `game.dezone` ([src/core/game_dezone.cpp](../src/core/game_dezone.cpp)) clears the network cells the same way before the function runs: the traffic network's slot 8 with mask 0x1F4F, every transport network but not water pipes, power lines or subway. The game's code then never sees those cells, so nothing in the zone manager, the parcelling or the messages changes.

It acts only when the zone type is 0, the call comes from the zoning tool's code (0x004DA7A0 to 0x004DC1A0: the preview at 0x004DB312 and the placing at 0x004DA846; with the zone tool translation on, `tool_zone::PlaceZoneSite()` marks those calls, which then come from core.dll) with the player flag set, and Shift is held. Port developers, landfill and every other caller are never touched. It works with the game's PlaceZone (a detour) and with ours (`simulators.zonemgr.place` calls the same function first).

The idea is from the Bulldoze Extensions plugin by Nicholas Hayes (LGPL-3.0), which has a separate tool on Shift+V. None of its code is used and its source was not read.

## Checked

2026-10-11, unattended, Big City. `OPENSC4_TEST_DEZONE_AREA=<n>` de-zones the middle half of the city on the n-th day with the zoning tool's arguments; `OPENSC4_TEST_DEZONE_KEEP=1` makes `game.dezone` act on it without the key. Occupant entries (`OPENSC4_TEST_OCCMGR_TYPES=1`):

| | Buildings | Network pieces | Price |
|---|---|---|---|
| before | 3667 | 3818 | |
| original | 2610 | 3207 | 92,920 |
| keeping roads, the game's PlaceZone | 2610 | 3818 | 91,941 |
| keeping roads, our PlaceZone | 2610 | 3818 | 91,941 |

Not checked: the tool itself with a real key and mouse (the preview and the price shown).
