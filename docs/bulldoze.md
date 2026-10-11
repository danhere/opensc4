# Bulldozer modes

Three more ways to use the bulldozer, chosen by the keys held while it works. An improvement beyond the original ([ROADMAP](../ROADMAP.md), "Improvements: the rules"): off by default, and with it off nothing changes.

```ini
[replacements]
game.bulldoze = ours
```

| Held | The bulldozer removes |
|---|---|
| nothing | everything, as always |
| Ctrl | only trees and other flora |
| Shift | only roads, rails and the other transport networks |
| Ctrl + Shift | everything but the transport networks |

Water pipes and power lines do not count as transport networks. The keys are read each time the tool looks at the area (as the mouse moves, and when the button is let go), so the highlight and the price follow them; what is held when the button is let go decides.

Trees that belong to a lot (a park's, a building's garden) stay in the flora mode: the game removes those only with their lot. Street lights and other props of a road go with the road.

## How

`game.bulldoze` ([src/core/game_bulldoze.cpp](../src/core/game_bulldoze.cpp)) changes nothing in the demolishing itself. The game's `cSC4Demolition::DemolishRegion` (0x00468410) takes an occupant filter and puts it in its default filter for the call; the bulldoze tool (`cSC4ViewInputControlDemolish`, vtable 0x00A901A0) passes none. A detour passes ours when the call comes from the tool's code (0x004B8830 to 0x004B9E20: the preview at 0x004B97F1, which also returns the occupants to highlight, and the mouse-up at 0x004B9D04) and a key is held. Other callers (disasters, growth, zoning) are never touched. A second detour on the tool's own occupant test (0x004B8880, used for the single object under the cursor) applies the same rule.

The filter: flora is occupant type 0x74758926; a transport piece answers `cISC4NetworkOccupant` and is neither a water pipe nor a power pole.

The idea of flora-only and network-only bulldozing is from the Bulldoze Extensions plugin by Nicholas Hayes (LGPL-3.0). None of its code is used and its source was not read; only its public description. It picks modes with shortcuts (Ctrl+B, Shift+B); ours with held keys, which needs no change to the game's key handling. Its diagonal bulldozing and its de-zoning that keeps roads are not in this change.

## Checked

2026-10-11, unattended. `OPENSC4_TEST_BULLDOZE=<n>` bulldozes the middle half of the city on the n-th day through `DemolishRegion` with the tool's arguments; `OPENSC4_TEST_BULLDOZE_MODE=<1 flora, 2 networks, 3 all but networks>` sets the mode for it in place of the keys. Occupant entries by type (`OPENSC4_TEST_OCCMGR_TYPES=1`), Big City, before and after:

| Type | Before | Original | Flora | Networks | All but networks |
|---|---|---|---|---|---|
| building 0x278128A0 | 3667 | 2603 | 3667 | 3667 | 2603 |
| flora 0x74758926 | 2939 | 2055 | 2811 | 2938 | 2055 |
| network 0xC772BF98 | 3818 | 2611 | 3818 | 2611 | 3818 |
| prop 0xA823821E | 71342 | 44090 | 71375 | 69681 | 44563 |
| price | | 106,953 | 0 | 1,933 | 105,020 |

The prices of the two network modes add up to the original's. Prop counts move a little by themselves from day to day. On Making Money, which has woods, the original removed 662 flora entries and the flora mode 604; the rest are lot trees.

Not checked: the tool itself with real keys and mouse (the highlight, the price shown, the single-object click). That part rests on reading the tool's code.
