# The water simulator (cSC4PlumbingSimulator)

The third simulator to be reimplemented, after pollution ([pollution.md](pollution.md)) and power ([power.md](power.md)), the same way: function by function on the original object under the parity harness ([parity.md](parity.md)), then our own object under the class ID. Build 641 addresses throughout.

## The class

Class ID 0x0990C075. Object size 0x1C8. Factory 0x006DC9A0 (`SC4::operator_new(0x1C8)`, then the constructor 0x006DC3D0). Code from about 0x006DA3B0 to 0x006DF750. Three interfaces:

| Vtable | Interface | Slots |
| --- | --- | --- |
| 0x00AB2228 | `cISC4PlumbingSimulator` (primary) | 29 and the scalar deleting destructor (slot 29): QueryInterface 0x006DA3C0 (0x47782CCC and 1 at +0, 0x452294AA at +0x4, 0xE4FDA3D4 at +0x8), AddRef 0x004F7BA0 (shared), Release 0x006DA610, Init 0x006DF0D0, Shutdown 0x006DA900, then the getters; several are shared one-line getters (0x00A6CAB0, 0x00423850, ...) |
| 0x00AB2214 | `cIGZMessageTarget2` | DoMessage 0x006DF570 |
| 0x00AB21FC | `cIGZSerializable` | Write 0x006DB090, Read 0x006DB620, GetGZCLSID 0x006DA3B0 |

There is no gzcom-dll header for it; the slot names come from the exe's own table (analysis `interface-slots.csv`).

### Messages

| Message | Handler | What it is |
| --- | --- | --- |
| 0x66956814 | 0x006DD380 with 5 | the simulation's step (the amount is the time budget of states 6 and 7); its result is DoMessage's |
| 0x6695681A | 0x006DD380 with the message's value | the same |
| 0x66956816 MonthPassed | 0x006DF540 | costs to refresh (+0xC5), a change (+0xC4), pipe wear (0x006DD110) |
| 0x6AC284F3 | inline | wakes the step (+0x14), as for power |
| 0x99EF1142 / 0x99EF1143 | 0x006DC870 / 0x006DC8F0 | a building (0x278128A0) or network piece (0xC772BF98) added / removed, unless its exemplar has 0x029244DB |
| 0xCA1B151C | 0x006DC9E0 | the tuning |
| 0xEA1F0DCD | inline | a cell's flags cleared (low five bits kept off), then as 0xEA418D8F |
| 0xEA418D8F | inline | a consumer (0x4773FFE2/E3) changed: the step starts over |

## The cells and the step

The simulator keeps a byte of flags per cell (+0x88, stride +0x8C, height +0x90): bits 0-3 the pipe's connections (west, north, east, south as the step reads them), 0x10 a pipe, 0x20 under a producer's lot, 0x40 a building with water demand (property 0xC8ED2D84). Producers (pumps, property 0x88ED3303) are a list at +0xBC: cell, lot rectangle, reached/served flags, capacity, output, scale, polluted flag, building. Pipes are a list at +0x170.

The step (0x006DD380, `simulators.water.step`) runs one state per call (+0xC0) while the network is awake (+0x14):

- 0: the month count (+0x9C), the spread rate (+0xE8, service 0x00B43D90 value 0xA8F4EB0C), consumers of types 0x4773FFE2/E3 reset (slot 21, 0), lots of type 5 switched off, the month's totals (+0xF0 to +0x10C) cleared.
- 1: `UpdateProducers` (0x006DC050): each pump's output is its capacity, cut to a share (+0xB4 %) or to 0 by its source kind's search for water around it (0x006DB2B0, radius +0xAC or +0xB0), times the funding curve (+0x1A0) or the default when its pollution is over +0xB8, times its condition curve; a pump whose lot is off gives nothing.
- 2: the pipe bit (flag 0x10) into bit 3 of the grid being built (+0x30); producers marked not reached.
- 3: the flood: from the first consumer of type 0x4773FFE2 still short (at its first unserved lot), else from the next producer not reached, over pipe cells along their connections (deque +0x38), collecting the output of producers reached (+0xC8); the cells reached also go to the spread queue (+0x60).
- 4: the supply serves consumers of type 0x4773FFE3 (0x006DCEE0 finds a lot the flood reached), then spreads over the cells around the network, orthogonally and in the first half of the rounds (+0xA8) diagonally, each building cell taking its share (0x006DCDA0 × the rate); what is left serves the consumer the flood started from; producers' used capacity (property 0x08D9763D) follows. Back to 3 until no source is left, then 5.
- 5: pipes and lots of type 5 told whether their cell is watered.
- 6: a pass over the city (cell iterator +0x144) telling the buildings on producer and demand cells their state (0x006DAE30), counting watered buildings; cut short when the time budget runs out (0x0068BF50 deadlines, checked every 0x400 cells); then the grids swap (+0x30, +0x34).
- 7: the rectangle where the new grid differs from the old goes to the city's view; also cut short by time.
- 8: producers' costs (0x2A58D350 × the curve 0x0911E2E7 at their age) into their budget lines, usage (+0x148), the month's figures (0x0A14EBB4 ...), message 0x6946DC34, consumers still short told; then message 0xEA418D8F and state 9.
- 9: waits for a change (+0xC4), then 0.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.water.interface` | slots 5-28 of `cISC4PlumbingSimulator` |
| `simulators.water.step` | the step (0x006DD380) |
| `simulators.water.helpers` | `UpdateProducers` (0x006DC050), `FindOtherSources` (0x006DB2B0, a spiral walk over the terrain map), `UpdateProducer` (0x006DA700, a pump's wear and its lot's condition), `ObjectCell` (0x006DA480), `CellValue` (0x006DCDA0), `Serve` (0x006DCEE0), `NotifyBuilding` (0x006DAE30) |
| `simulators.water.month` | `OnMonthPassed` (0x006DF540), `ApplyFunding` (0x006DD110): pipe wear (distressed share +0x180 by the curve +0x1B0, distressed and burst pipes) |
| `simulators.water.buildings` | `OnOccupantInserted`/`Removed` (0x006DC870, 0x006DC8F0), `OnBuilding` (0x006DBBC0), `OnPipe` (0x006DB9A0) |
| `simulators.water.tuning` | `LoadTuning` (0x006DC9E0) |
| `simulators.water.messages` | `DoMessage` (+0x4 slot 3) |
| `simulators.water.init`, `.shutdown` | `Init`, `Shutdown` |
| `simulators.water.save`, `.load` | `Write`, `Read` (version 4) |
| `simulators.water.object` | our own object under the class ID: constructor (0x006DC3D0), destructor (0x006DC730), identity, our copies of the three vtables |

Still the game's: the deque, list and iterator helpers (generic containers), GetGZCLSID.

## Test cities

Like power, the step does nothing until message 0x6AC284F3 (the network tools) sets +0x14: `OPENSC4_TEST_WATER_WAKE=<n>` sets it on the n-th day of a run. Making Money has a pump (capacity 19,994, 53 pipes); Big City has pipes and no pump; Rush Hour neither.

The fingerprint's `water` part: a checksum of the watered grid (slot 7), sources and polluted sources (12, 13), production capacity, consumption, import and export (14-17), pipes, distressed and burst pipes (18-20), watered and unwatered buildings (21, 22) and demand (23).

## Save record

Version 4 (Write and Read, the cIGZSerializable part at +0x8): the two grids (references), stride and height, the cell flags, +0x9C, +0xA0, +0x148 (int64), the producer count, +0x198, each producer (cell, lot rectangle, capacity, output, building), the current one, the cell iterator (a byte and seven 32-bit fields), +0x130 (int64), the pipe count, +0x178, +0x17C, each pipe (reference), the department, the month's totals +0x110 to +0x128 (int64). A reference to nothing is a single 0 (not 8 bytes). Read sets +0xD (Init then leaves the grids and cells alone) and state 0.

## Parity

Control (the original against itself) with the water part: 12 of 12, 3 of 3 on `saves-plopped` for a week, 2 of 2 on `saves-mm2` for 90 days.

Everything but Write, Read and the object (`OPENSC4_TEST_WATER_WAKE=1`): 12 of 12, 3 of 3 for a week, 2 of 2 for 90 days. With Write and Read, then also our object: the same, save records identical (two pairs failed with a crash on the A side, the unmodified game, and passed twice each on rerun; [parity.md](parity.md)). Everything, `UpdateProducer` and `FindOtherSources` included: 12 of 12, 3 of 3, 2 of 2, water records identical in all 17 pairs, our object made in all 17 runs of our side.

First calls in those runs: Init, Read, LoadTuning, DoMessage, the step, `UpdateProducers`, `NotifyBuilding`, Write and Shutdown in every run; `CellValue` in 10, the month and `ApplyFunding` in 6, `OnBuilding` and `UpdateProducer` in 2. Never reached: `FindOtherSources` (the test pump's source kind needs no search), `ObjectCell` and `Serve` (no consumer of type 0x4773FFE2 short, no lot of type 5), `OnPipe` (no pipe laid).
