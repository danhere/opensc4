# The traffic simulator (cSC4TrafficSimulator)

The fourth simulator, after pollution, power and water ([pollution.md](pollution.md), [power.md](power.md), [water.md](water.md)), the same way: function by function on the original object under the parity harness ([parity.md](parity.md)). Build 641 addresses throughout.

## The class

Class ID 0x6990C1AA. Object size 0xD58. Factory 0x00721580, constructor 0x0071F830, destructor 0x007200C0. Code from 0x0070D7E0 to about 0x0072A000 (the simulator, about 80 KB) besides the pathfinder. Three interfaces:

| Vtable | Interface | Slots |
| --- | --- | --- |
| 0x00AB33D0 | `cISC4TrafficSimulator` (primary) | 43 and the scalar deleting destructor (slot 43): QueryInterface 0x0070D7E0, AddRef 0x004F7BA0 (shared), Release 0x0070E2F0, Init 0x0071E7E0, Shutdown 0x00719290, then the queries (pathfinder setup, background traffic, edge density, trip capacities, travel times, ferry routes, subnetworks) |
| 0x00AB33BC | `cIGZMessageTarget2` | DoMessage 0x007241B0 |
| 0x00AB33A4 | `cIGZSerializable` | Write 0x00712400 (4.6 KB), Read 0x00721990 (7.2 KB), GetGZCLSID 0x0070D7D0 |

### Messages

DoMessage (0x007241B0, on the `+0x4` interface) hands each message to a handler; offsets here are from the object's start.

| Message | Handler | What it is |
| --- | --- | --- |
| 0x66956814 | 0x007215C0 | the step, below |
| 0x66956816 MonthPassed | 0x00718AC0 | the month, below |
| 0x26D31EC1 CityInited | 0x00720430 (2.7 KB) | |
| 0x08DAC3E7 | inline | with slot 14 = 0xC921CEEB: +0x720 = the curve at +0xD10 at department 2's funding x 100 |
| 0x66956810 | inline | sets +0x920 |
| 0x99EF1142, 0x99EF1143 (occupant inserted, removed) | inline | buildings (tag 0xC772BF98, interface 0xA821EF94): by their kinds (slot 22) 0x007155D0 (kinds 6, 0, and the first of 10, 3, 2, 12), 0x00715790 (1), 0x0070EA70 (7 without flag 0x80000000); a kind marked in 0xAB3164 also calls 0x007140E0 and 0x0071A860 on its cell (on removal 18 keys into the set at +0x178 first); then sets +0x920 |
| 0x8A0D15DA, 0xEA0D163E | 0x007153C0, 0x00713AB0 | message slots 9, 14, 19 |
| 0xC53D10AA | 0x00716AE0 | with slots 9, 14 = 0xE7E2C2DB, 0xC9133286 |
| 0xC2C09554 (disaster) | 0x00711810 | |
| 0xE8BDA2E7 | 0x0071E2F0 or 0x00723AF0 | an interface 0xE818FCED from slot 32; slot 9 = 3 the first, else slot 14 = 3 the second |

### The step

0x007215C0 runs on every 0x66956814: on a new day of the month (simulator service slot 6), on the 1st, last month's incomes (+0x780, +0x784) go to two budget line items (department 2 item 2, department 1 item 5; their sum is slot 18's); every +0xC48 / +0xC4C days the lots of the next bucket of the multimap at +0x1A0 get a random 0-99 in property 0x4A611773. Then, until its time budget (0x0068BF50 with 4) is spent, the sub-step 0x00720F00.

The sub-step calls the function of the current phase (+0x814, 20 phases; 1, 9, 12 and 14 have none and are done at once), and when it is done moves to the next phase from the table at 0xAB3480 (12 becomes 13 unless +0x62 is set) and starts it (restarting cell iterators and walks, clearing totals; phase 19 copies this month's counts to last month's and collects the cells of the occupants of type 9). The phases 16-18 work through a count in parts; the step returns after a part. Phases 17 and 18 route trips through the path finder (0x0071C6C0, 0x0070EC40) that NAM modifies; they stay the game's for now. The time-sliced phases keep their own deadline steps (0xB4B300 and on: a step, a counter and a once flag each) and adapt them as 0x0068B960 does; integers from floats use the game's 1.5 x 2^23 addition (`MagicRound`).

### The month

0x00718AC0: the two departments' funding move a level by the curve at +0xD20 (0x00717D30, 0x00717E00: clamped to [0, 1]; that share of a count is active, the change through 0x00715FB0), the strike check (0x0070DBA0: a state at +0x7A4; underfunded department 2 starts a strike for up to +0xB9C months, messages 0x69247DC3-6), a random event in view (0x007184E0, on a stopwatch), then last month's figures (IDs 0xEA5BA01D, 0x8A5D4116, 0xABB8FAAC-0xABB8FAB4, 0xEC0BDBF8).

## The Network Addon Mod

NAM patches the traffic code in the middle of functions (its DLL, [patch-census.md](patch-census.md) section 9): 0x0070EAE7 in 0x0070EAB0, 0x00714222 in 0x007140E0, 0x0071AB60 in 0x0071A860, 0x00729FFF in 0x00729F90, and the pathfinder at 0x006D79BF, 0x006D7EBE/0x006D7EF0 and 0x006D95B1 (0x006D7960, 0x006D7E50, 0x006D91B0). The roadmap leaves networks to work with the NAM team (P7); these functions stay the game's. Mods also retarget call sites inside callers, and replacing a caller drops such a patch: no site in the census falls inside a function replaced here (checked for each one); our replacements call the patched functions, so NAM's patches in them stay in effect.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.traffic.interface` | 24 of the 43 slots: the fields (8-10, 13), the trip scales, last month's income and the freight scaling (14, 15, 18, 27; floats in ST0), `GetTrafficEdgeDensity` (12: hash table at +0x17C keyed by commute, travel type and cell; the empty record 0xAB31D0 otherwise), `IsRoadDamaged` and `CheckRailAccident` (16, 17: chained hash tables keyed by cell, the second with a draw of the simulator's generator below +0xD04), `SetMaxTripCapacity`, `GetMaxTripCapacity` and `GetTrafficArrived` (19-21: the building's property 0x8A0B4100, 12 values as uint16, widened to uint32 for a value of 0xFFFF or more), `GetDesiredLotInsertionPoint` (22), `GetCapacity`, `GetTravelTimeRatio` and `GetConnectionCount` (23-25: the network edge of a type at a cell, the chain at +0x7C [x][z]; the ratio is the curve at 0xB4B2F0 at the cell's traffic x the edge's first float), `GetTravelStrategyPercentages` (26: bytes at +0xC5C x 0.01), `GetFerryTerminalCount` (32), `GetWaterRoute` (33: the finder at +0x108), `GetTrafficStats` (34), `GetSubnetworksForLot` (39: lot property 0x2A4F9C74) |
| `simulators.traffic.step` | the step (0x007215C0) |
| `simulators.traffic.substep` | the sub-step (0x00720F00): the phase dispatch and the start of each phase |
| `simulators.traffic.month` | the month (0x00718AC0), the funding helpers (0x00717D30, 0x00717E00) and the strike check (0x0070DBA0) |
| `simulators.traffic.messages` | DoMessage (0x007241B0) |
| `simulators.traffic.phases` | the phase functions 0 (0x00717C90: the month's counts cleared), 2 (0x0071A0C0: the month's per-cell statistics: congestion and load grids, block values, per-type sums, counts and maxima, the cost records, decay, then averages copied to last month's), 3 (0x00713750: costs at +0x70C scaled by +0x97C), 4 (0x0070F460: the byte grids +0x30 and +0x48 from the float grid +0x34 and the travel time curve at +0x174; posts 0x69247DC7), 5 (0x0070E4F0: each lot's trip values 4-7 moved to 8-11, kinds 0-2 summed at +0x1C0), 6 (0x007110B0: edge density counters cleared), 7 (0x007137D0: 16 counters per node of +0x178 moved, their sum clamped), 8 (0x0070E7D0: the month's incomes, average trip and commute byte), 10 (0x0070F6E0: trip-kind bits of each cell's lot in the bordered grid +0x4C), 11 (0x00714BE0: per lot, the properties 0xAA9EA81A-C copied 3 on and zeroed, trip values moved), 12 (0x007183B0: 0x00717EC0 for each empty direction of each type-1 edge, forward then backward), 13 (0x0071DC50), 15 (0x0070FEF0: occupants of types 8 and 9 paint their rectangle in the byte grid at +0x44 with a linked or nearby occupant's value), 16 (0x0071E1B0: the lot walk under a sliced deadline) and 19 (0x0071DC90: pairs entries of the list at +0x104 and routes each to the collected cells, in states at +0x124; posts 0x2C06BA82 when done) |

Parity (12 saves x 7 days, the 3 plopped saves, 2 saves x 90 days) with every replacement above on: all identical, every replaced phase run (phase 15 in 2 of the 17 runs only). Not reached in those runs: `GetTravelTimeRatio`, `GetWaterRoute` and `GetSubnetworksForLot` (slots 24, 33, 39), and the building paths of DoMessage (no building was inserted or removed).

The fingerprint's `traffic` part: the trip scale and the one shown, last month's income and the freight scaling (slots 14, 15, 18, 27), as float bits, besides the congestion, trip length, commercial and air-polluting traffic grids already in it.
