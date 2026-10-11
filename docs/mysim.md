# The MySim agent simulator (cSC4MySimAgentSimulator)

Reimplemented after neighbors ([neighbors.md](neighbors.md)) the same way: function by function on the original object under the parity harness ([parity.md](parity.md)), then our own object under the class ID. Build 641 addresses throughout.

The simulator is a thin owner. It keeps the city's MySims (the player's own Sims who move into the city, the "My Sim" mode) in six places and passes the game's messages on to them; the MySims themselves (`cSC4MySim`, code 0x00424B00 to 0x0042E2F0, and the Microphone Tool's `cSC4MyStreetSim` on top of it, 0x0043D0F0 to 0x0043E740) are agents that live, work, travel and react. They are not part of this replacement; they have their own replacements and page ([mysim-agents.md](mysim-agents.md)).

## The class

Class ID 0x29E37DEC. Object size 0x3C. Factory 0x0042F6B0 (`SC4::operator_new(0x3C)`, constructor 0x0042F5D0). Code 0x0042E300 to 0x0042F6C0. Three interfaces:

| Vtable | Interface | Slots |
| --- | --- | --- |
| 0x00A85544 | `cISC4MySimAgentSimulator` (IID 0xA9E37EBB) | 14: QueryInterface 0x0042E310, AddRef (shared 0x004F7BA0), Release 0x0042E410, Init 0x0042EC30, Shutdown 0x0042E500, GetActiveMySims 0x0042F4A0, MoveIn 0x0042E990, TrackLocation 0x0042E9F0, HideLocation 0x0042EA70, MySimLivesHere 0x0042EB60, CancelMoveIn (shared 0x00425150), SetActiveUIPanelSim 0x0042E910, GetActiveUIPanelSim 0x0042E960, the deleting destructor 0x0042F450 |
| 0x00A85534 | `cIGZMessageTarget2` | DoMessage 0x0042EFA0 |
| 0x00A8551C | `cIGZSerializable` | Write (shared 0x0042E440), Read (shared 0x006B8A70), GetGZCLSID 0x0042E300 |

gzcom-dll's `cISC4MySimAgentSimulator.h` has the slots in this order. GetActiveUIPanelSim returns the MySim (or null), not the `bool` gzcom-dll declares.

### Fields

| Offset | Field |
| --- | --- |
| +0xC | initialised |
| +0x10 | reference count |
| +0x14 | the MySims: a vector of six referenced `cISC4MySim` (begin, end, capacity). The five MySim slots in the order the city lists them, and the one whose slot index is 30 in the last place |
| +0x20 | a key from message 0xCA1B1E64 (its slot 24), handed on with occupant news during a disaster |
| +0x24, +0x28, +0x2C | the uint32 at 0x00B20E10, copied by the constructor and not read again |
| +0x30, +0x34 | references: the city, the message server |
| +0x38 | the MySim shown in the UI panel (index, -1 for none) |

Most functions only look at the first five places; TrackLocation, HideLocation, Shutdown and the constructor look at all six.

## What it does

Init takes the MySims from the city (its slot 81, a map; each value's `cISC4MySim` interface, IID 0x0A1DA42F) and registers for eleven messages and two script commands, `get_sim` and `get_count`. Write and Read handle a version-1 record with no data, but the city saves no record of this class (none in any test save, nor in the saves the harness writes), so neither runs: the MySims are saved by their own records.

| Message | What happens |
| --- | --- |
| 0x44778230 | by its code (slot 14): 8, 0x11 and 0x16 call each MySim's slots 38, 39 and 54 |
| 0x230E27AC | the cheat (code 0xEB6F3C97): `print` posts 0x0B6F3E27; `newjob <n>`, `newhouse <n>`, `dispatch <n>` post 0xAB6F3E2D, 0x4B6F3EBF, 0x0BB04126 with the number |
| 0x0A36F1E9, 0xEA1F0DCD | a cell turned into a position (the city's slot 97), to each MySim's slot 47 or 46 |
| 0x8A09FA6C | to the MySim the message names (its slot 41) |
| 0x99EF1142 | an occupant added (kind 0x278128A0) to each MySim's slot 42, or a position (kind 0x69D25C5F) to slot 45; then as 0x99EF1145 |
| 0x99EF1145 | an occupant (kind 0x278128A0) to each MySim's slot 43 |
| 0x99EF1143 | an occupant removed (kind 0x278128A0) to each MySim's slot 44, with the key +0x20 when the reason is 4 and a disaster is on |
| 0xA6B79602, 0xA6B79612 | each MySim's slot 37 |
| 0xCA1B1E64 | the key into +0x20, then it and two values to each MySim's slot 48 |
| 0xEA0D163E | two values to each MySim's slot 49 |

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.mysim.interface` | slots 5 to 9, 11 and 12 |
| `simulators.mysim.messages` | DoMessage, its handlers (0x0042E640 to 0x0042EAE0, copied inline) and the cheat |
| `simulators.mysim.init`, `.shutdown` | Init, Shutdown |
| `simulators.mysim.save`, `.load` | Write, Read (version 1), in this vtable only (the functions are shared) |
| `simulators.mysim.scripts` | the script commands `get_count` (0x0042E460) and `get_sim` (0x0042EBA0) |
| `simulators.mysim.object` | our own simulator object: constructor, destructor, identity, our copies of the three vtables |

Still the game's: CancelMoveIn (0x00425150, shared: it releases the object held at 0x00B20E0C, first taking it off service 0x00B43DC4 when that service holds it), the generic vector, string, tokenizer and script helpers, and the MySims themselves.

## Test cities

Every test save has six MySim places filled with MySims that are not active (no MySim has moved in), so the interface, the message loops and the script commands run against real objects that do nothing. The fingerprint's `ms` part: the number of places, the MySims present, which are active (a bit each), a checksum of their slot numbers, the key +0x20, the panel index +0x38 and +0xC.

## Parity

All runs on 2026-10-09 with every `simulators.mysim.*` group on, two games at a time, other threads' games running too, and whole fingerprint lines (the log writer used to cut them at 1,024 characters, which dropped the `ms` part). Result folders under `%LOCALAPPDATA%\opensc4\parity\crime\`.

| Run | Result |
| --- | --- |
| 12 saves for a week (`ms-all3`) | 11 of 12; Big City d0090 partly (one of four B runs parted in pollution on day 4, before anything of ours could matter); two reruns of that save (`ms-bc90-1`, `ms-bc90-2`) same |
| `saves-plopped` (`ms-pl`) | 3 of 3 |
| 90 days on `saves-mm2` (`ms-mm90`) | 2 of 2, 91 of 91 days each |

Our object was made in every B run. Not reached: an active MySim (none has moved in on any test save, so the messages reach MySims that ignore them), MoveIn, the UI panel, the cheat, and Write and Read (no record is saved).
