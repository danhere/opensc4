# Quiet demolition and construction

Three effects of building and demolishing that can be turned off, each its own setting. Improvements beyond the original ([ROADMAP](../ROADMAP.md), "Improvements: the rules"): off by default, and with a setting off its hook is not installed and nothing changes.

```ini
[replacements]
game.quiet.demolish = ours   ; no demolition animation when a building goes
game.quiet.plop = ours       ; no smoke when the player places a lot
game.quiet.network = ours    ; no sounds from the crews that build roads and rails
; or all three: game.quiet.* = ours
```

None of them touches what is simulated or saved: they are pictures and sounds only.

## What each does

All three are in [src/core/game_quiet.cpp](../src/core/game_quiet.cpp); the addresses are in `data/addresses/quiet.toml`. The plugins they come from change one instruction inside a game function. Our hooks go at function entries only ([architecture.md](architecture.md), "Hooking layer"), so each is a detour that comes to the same thing.

| Setting | The plugin's change | Ours |
|---|---|---|
| `game.quiet.demolish` | The call at 0x004673BF of 0x004672E0, "is this occupant too small for a demolition animation?", goes to a function that says yes | That call is the first thing in `cSC4Demolition::DoDemolishOccupantEffect` (0x004673B0, slot 10 of the demolition utility, never called directly), its only caller; on a yes the function returns at once. Ours replaces the function by one that returns |
| `game.quiet.plop` | The `push 1` at 0x004C3181 becomes `push 0`: the smoke argument of the lot tool's call of `cSC4ViewInputControlPlaceLot::PlaceLot` (0x004C1740) | A wrapper of PlaceLot that clears the argument when the call returns to 0x004C3190, the lot tool. PlaceLot's other caller (0x005C5554) passes a value of its own and is left alone |
| `game.quiet.network` | The jump at 0x006071FC is always taken: `cSC4NetworkConstructionCrew::Init` (0x00607190) never stores the sound service at the crew's +0x18, and a crew without it plays nothing | A wrapper of Init: if Init stored the sound service, the reference is given back and +0x18 put back as it was. Nothing later in Init reads +0x18 or the service |

With our construction crew's Init (`simulators.crew.life`, [netlot.md](netlot.md), "The construction crew, part C1") at 0x00607190, `game.quiet.network` is not a second detour on the same entry: our crew group installs first (PreAppInit), and the setting (PostAppInit) then turns on a switch in our Init instead, which never takes the sound service and reports each Init to the same counters and the same log line; the log says which of the two it is (`quiet: construction crew Init wrapper is a switch in our translation` or `... sits on the game's entry`). Our Init also reads the plugin's byte at 0x006071FC at every call and leaves the service out when the plugin has written its 0xEB there, so the plugin keeps working with our Init too.

As the plugin's author notes, a crew without the sound service also does not send its two messages (0x89F10866 at the start, 0xA9F10E45 at the end), which only the sounds listen for.

When a city closes, the log has how often each wrapped path was taken.

## Credit

What to change, and where, is from three plugins by Nicholas Hayes (MIT License; the notice is in the source file): [sc4-disable-demolition-animation](https://github.com/0xC0000054/sc4-disable-demolition-animation), [sc4-disable-lot-plop-smoke](https://github.com/0xC0000054/sc4-disable-lot-plop-smoke) and [sc4-disable-network-construction-sounds](https://github.com/0xC0000054/sc4-disable-network-construction-sounds). None of the plugins' sites is at a function entry, so our hooks install with a plugin present too; both then do the same thing.

## Checked

2026-10-11, unattended, windowed, Making Money, with the test switch `OPENSC4_TEST_QUIET` (when the city has started: `demolish` clears the cells from a quarter to three quarters of the city through the game's demolition utility, `network` calls the game's road tool for six short roads (its roads were never built, see below); nothing is saved):

- `game.quiet.* = ours`, `demolish`: the demolition was done (cost 13,333) and the wrapped function was entered 3,888 times, each returning without an animation.
- `game.quiet.* = ours`, `network`: six construction crews were started by the road tool, all six left without the sound service; the city then ran ten days and closed. The same run with only `game.quiet.demolish` on started no wrapped crew (the hook is not there).
- Off: the log has the three `original` lines of the registry and nothing else of this file; no hook is installed.

Not checked: `game.quiet.plop`. Its hook installs, but only a lot placed by hand with the lot tool reaches it (PlaceLot was called 0 times in these runs); the lot plops of the unattended tests go through the lot manager, not the tool. Nor has anyone looked or listened: that the animation and the sounds are in fact gone is by the code paths above, not by eye and ear.

The `network` test counted crew Inits only: its roads were never built, because it drew them with DrawNetworkLine and PlaceAllSegments without anchor points, and PlaceAllSegments builds nothing without them (the crew it makes ends at once; [netlot.md](netlot.md), "The crew's clocks and seeds"). So the six crews above were started and left, but did no work.

The road test leaves its tool to the network manager, and both road runs, with and without the network hook, ended with a game crash while quitting (0x00626E40, network code, after "quitting without saving"): the test's doing, not the setting's.
