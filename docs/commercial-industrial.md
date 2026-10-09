# The commercial and industrial simulators

Reimplemented after the MySim agent simulator ([mysim.md](mysim.md)) the same way: function by function on the original objects under the parity harness ([parity.md](parity.md)), then our own objects under the class IDs. Build 641 addresses throughout. Both are small: the work of growing commercial and industrial zones is done by the demand and building development simulators.

## cSC4CommercialSimulator

Class ID 0x8990C080. Object size 0x3C. Factory 0x0069EF70 (`SC4::operator_new(0x3C)`, constructor 0x0069EE50). Code 0x0069EA40 to 0x0069F1A0.

| Vtable | Interface | Slots |
| --- | --- | --- |
| 0x00AB0CCC | `cISC4CommercialSimulator` (IID 0x077ABEB2) | 7: QueryInterface 0x0069EA50, AddRef (shared), Release 0x0069EB50, Init 0x0069EF90, Shutdown 0x0069EB80, GetProximityMap 0x0069EAD0, the deleting destructor 0x0069EB20 |
| 0x00AB0CBC | `cIGZMessageTarget2` | DoMessage 0x0069EEB0 |
| 0x00AB0CA4 | `cIGZSerializable` | Write 0x0069EC50, Read 0x0069ECB0, GetGZCLSID 0x0069EA40 |

| Offset | Field |
| --- | --- |
| +0xC | initialised |
| +0x10 | reference count |
| +0x14, +0x18, +0x1C | references: the building development simulator, the city, the message server |
| +0x20 | a commercial occupant came or went (starts set) |
| +0x24 | the step of the proximity-map cycle, 0 to 3 (starts at 3) |
| +0x28, +0x2C | the demand simulator's demands 0x3000 and 0x3800 (its slot 6), held but not read |
| +0x30 | a flag handed to new maps (their slot 9; starts set) |
| +0x34, +0x38 | the proximity maps for developer types 0x3320 and 0x3330 (`SC4SimGridBool8`, 0x44 bytes, IDs 0x89D5B788 and 0x89D5B6EF) |

GetProximityMap returns +0x34 for 0x3320, +0x38 for 0x3330, and null otherwise.

The cycle (0x0069ED30, on messages 0x66956814 and 0x6695681A, timed with the game's stopwatch): from step 0 or 1, the 0x3320 map is filled from the building development simulator's building map (its slot 41) for the building key of purpose 3, wealth 2 (its slot 42), and the step becomes 2; from step 2, the 0x3330 map for wealth 3, and the step becomes 3; at step 3, if +0x20 is set, it is cleared and the step goes back to 0. +0x20 is set when an occupant message (0x99EF1142 added, 0x99EF1143 removed, kind 0x278128A0) is about an occupant whose first type word (its interface 0x87DDA3A9, slot 11) is 3.

Init registers with the simulator service (phases 0, 10, 4, 6) and for the two occupant messages, takes its references, and returns false without the two demands; it makes the maps only when no save gave them. The save record is version 1: whether a cycle is pending (+0x20 set, or the step not 3; Read puts it back into +0x20) and the two maps as references.

## cSC4IndustrialSimulator

Class ID 0xA990C022. Object size 0x14. Factory 0x006B8AD0 (constructor inline). A stub: Init (0x006B89F0) sets +0xC and, once, the global byte 0x00B490A4; Shutdown (0x006B8A10) clears +0xC; DoMessage, Write and Read are functions the game shares with other classes (0x006B8AA0 reads the message type and returns true; Write and Read handle an empty version-1 record). Its interface `cISC4IndustrialSimulator` (IID 0x477A9E7D) is vtable 0x00AB1530: QueryInterface 0x006B8970, AddRef (shared), Release (shared 0x006D5140), Init, Shutdown, the deleting destructor 0x006B8A40; cIGZMessageTarget2 0x00AB1520, cIGZSerializable 0x00AB1508.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.commercial.sim` | Init, Shutdown, GetProximityMap, DoMessage with the cycle, Write, Read |
| `simulators.commercial.object` | our own commercial object: constructor, destructor, identity, our copies of the three vtables |
| `simulators.industrial.sim` | Init, Shutdown, and DoMessage, Write and Read in its vtables only |
| `simulators.industrial.object` | our own industrial object |

Still the game's: the bit grid and its fill from the building map (0x0045DDB0, 0x005E5130), the stopwatch.

## Test cities

No test save runs the cycle on its own: every save starts at step 3 with +0x20 clear, and no commercial occupant comes or goes in a week. `OPENSC4_TEST_COMMERCIAL=<n>` hands the commercial simulator "removed" then "added" for every building on a lot (at most 400) on day n, which starts it. The fingerprint's `ci` part: the step, +0x20, a checksum of each map's cells (`SC4SimGridBool8` keeps one 12-byte vector per x at +0x38, sizes at +0x20 and +0x24) and the industrial +0xC. On every test save the maps are 128 by 128 and the same before and after they are filled.

## Parity

All runs on 2026-10-09 with every `simulators.commercial.*` and `simulators.industrial.*` group on, two games at a time, other threads' games running too. Result folders under `%LOCALAPPDATA%\opensc4\parity\crime\`. `tools/re/sc4save.py` decodes the commercial record (all 15 corpus saves); the industrial simulator saves none.

| Run | Result |
| --- | --- |
| 12 saves for a week (`ci-all1`) | 12 of 12, but the cycle never ran (step 3 throughout) |
| the same with `OPENSC4_TEST_COMMERCIAL=1` (`ci-drive1`) | 7 same, 5 B runs crashed: our cycle called the building development simulator's slot 42 with one argument instead of two (purpose and wealth), which unbalanced the stack. Fixed |
| after the fix (`ci-drive3`, with the maps' cells in the fingerprint) | 12 of 12, the cycle running in every save; commercial records identical in all 12 pairs |
| `saves-plopped` with the switch (`ci-pl`) | 3 of 3 |
| 90 days on `saves-mm2` with the switch (`ci-mm90`) | 2 of 2, 91 of 91 days each |

Our objects were made in every B run. Not reached: a map that changes when filled, GetProximityMap for a type other than the two.
