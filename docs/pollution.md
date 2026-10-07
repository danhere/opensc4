# Pollution simulator (P5)

`cSC4PollutionSimulator` (class ID `0x8990C065`), Steam SimCity 4 Deluxe 1.1.641. The first simulator being reimplemented: what the class is made of, how ours replaces it piece by piece, and what is ours so far. Save layout: [save-format.md](save-format.md). Parity method: [parity.md](parity.md).

## How it is replaced

In increments, each on the original object. The game keeps creating, loading, saving and ticking its own `cSC4PollutionSimulator`; a replacement swaps some of its functions for ours, which read and write the original's fields at the original's offsets. Each increment is a replacement of its own, so it can be switched off alone and compared against the original under the parity harness. When everything that touches the object is ours, the object can become ours too (registered under the class ID, as the pass-through proxy already is: [experiments/pollution-proxy.md](experiments/pollution-proxy.md)).

| Replacement | What is ours | State |
| --- | --- | --- |
| `simulators.pollution.interface` | 59 of the 71 `cISC4PollutionSimulator` slots: the getters, setters and queries ([src/core/pollution_sim.cpp](../src/core/pollution_sim.cpp)) | done; parity below |
| | Init, Shutdown, the message handlers (the simulation itself), the remaining slots | not started |

The interface methods are swapped in the primary vtable (0x00AB2630), not detoured at their entries: nothing calls them directly (every reference is the vtable's), and several are compiler-folded with other classes' functions (0x006E3000 sits in nine vtables, the functions of slots 10, 30, 31 and 32 in two to five), so an entry detour would change those classes too. The swap checks that every slot still holds the game's function and changes all of them or none.

Not replaced yet, and why: 0-2 (QueryInterface, AddRef, Release; the object's identity), 3 Init and 4 Shutdown (large), 46 `GetGarbageBuildings` (builds a list), 53 `GetCurrentCapacity`, 55 to 57 and 65 (x87 arithmetic on property values: exact float results need care, see below), 66 `AddRadiationToCity` (calls into the CityInited handler).

## The class

Object size 0x238. Constructor 0x006E56E0, factory 0x006E5F00. Four interfaces:

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AB2630 | `cISC4PollutionSimulator` (0x877181EC) | 71 |
| +0x4 | 0x00AB261C | `cIGZMessageTarget2` (0x452294AA) | slot 3 `DoMessage` 0x006E8E20 |
| +0x8 | 0x00AB2600 | 0x89C498BE, unnamed | slot 3 0x006E3BC0 (average of `GetGarbageValue` over a cell rectangle, log-scaled), slot 4 returns 0xE9A4300B |
| +0xC | 0x00AB25E8 | `cIGZSerializable` (0xE4FDA3D4) | `Write` 0x006E3380, `Read` 0x006E5270 |

### Messages

`DoMessage` (0x006E8E20) is where the simulation happens:

| Message | Handler | What it does |
| --- | --- | --- |
| 0x26D31EC1 CityInited | 0x006E63C0 (2.5 KB) | rebuilds the grids; also called by MonthPassed and `AddRadiationToCity` |
| 0x66956810 | 0x006E44B0 | sets the float at +0x228 from a city value (used by the capacity getters as a scale) |
| 0x66956814 | 0x006E7FD0 (3.6 KB) | the garbage simulation: supply from the incinerator, waste-to-energy and recycling lists, then a walk over garbage tracts (cursor +0x60/+0x64, +0x1D4 tracts per call) moving garbage into landfills |
| 0x66956816 MonthPassed | 0x006E7B60 | copies this month's garbage figures to last month's, publishes them, clears them, refreshes the building lists, then calls the CityInited handler |
| 0x99EF1142/43 occupant inserted/removed | 0x006E4680 (1.5 KB) | adds or removes a building's pollution footprint in the grids and keeps the four building lists |
| 0xC6CDBF1A | inline | when its flag differs from +0x208, sets it and calls the CityInited handler |
| 0xCA1B151C | 0x006E5B10 `LoadTuning` | reloads the tuning globals from the simulator's exemplar (also called by Init) |

Message names from gzcom-dll where it has them; the others are unnamed.

### Fields

What the interface methods use (names ours unless a getter confirms them):

| Offset | Type | Meaning |
| --- | --- | --- |
| +0x24 | `cISC4ZoneManager*` | zone types (landfill = 14) |
| +0x40 to +0x4C | `cISC4SimGrid<short>*`[4] | read by `GetPollutionValue(type)` |
| +0x4C | grid | radiation (`GetPollutionGrid(3)`) |
| +0x50, +0x54, +0x58 | grids | air, water, garbage (`GetPollutionGrid(0..2)`); bit 15 of a garbage cell marks an active landfill tile |
| +0x68, +0x6C, +0x70, +0x74 | list sentinels | buildings: water treatment, incinerators, waste-to-energy, recycling (as the count getters read them; the names are gzcom-dll's) |
| +0x100, +0x104, +0x128 | int32, int32, uint16 | average air, water, garbage |
| +0x12C, +0x130 | int32 | monthly incinerator and waste-to-energy capacity |
| +0x134 | float | recycling center effect (slot 33 returns its address) |
| +0x138, +0x13C | bool, int32 | garbage uncollected, total uncollected |
| +0x140 to +0x174 | int64 x3, double, int64 x3 | this month's garbage: produced, imported, exported, recycled, incinerated, converted, landfilled (+0x170, added to by `GarbagePutInLandfill`) |
| +0x178 to +0x1AC | same | last month's (MonthPassed copies them over) |
| +0x1B8, +0x1BC | uint32 | garbage abducted the previous month; active landfill tiles |
| +0x208 to +0x20B | bool x4 | set by the setters: radiation, air, water, garbage changed |
| +0x210, +0x218, +0x220 | uint16 x, uint16 z, int32 value | worst air, water, garbage tract |
| +0x228 | float | capacity scale (message 0x66956810) |
| +0x22C, +0x230 | int32 | garbage supply, garbage capacity |

Tuning globals, all written by `LoadTuning` (0x006E5B10) and listed in the address database: thresholds for `IsAirPolluted`/`IsWaterPolluted`/`IsGarbageAccumulated` (0x00B49B7C, 0x00B49B78, 0x00B49B80), the UI maxima (0x00B49BA0 to 0x00B49B94), landfill tile capacity (0x00B49BA4), garbage scaling factor (0x00B49B6C). 0x00B49BC4 is a float that is never written through its address, the starting value of percentage outputs.

## What the game does that gzcom-dll's header does not say

Our functions reproduce these; callers can see them.

- **Return registers.** The bool getters return whatever the game's code leaves in AL: `GetAirValue`, `GetWaterValue`, `GetGarbageValue` and the three `GetWorst...Tract` return the low byte of the value they report (false when it is a multiple of 256); `AddToGarbageSupply` the low byte of the amount added. The setters return what `SetTractValue` left in EAX. The eight garbage figures (slots 34-41) return 64 bits in EDX:EAX ([experiments/pollution-proxy.md](experiments/pollution-proxy.md)); `GarbagePutInLandfill` (51) returns EDX = the sign of the amount and EAX = the new total's high word.
- **Slot 33** (`GetRecyclingCenterEffect`, declared `int32_t`) returns a `float*` to +0x134; the Lua globals code (0x004220A0) reads it for `g_recycling_center_effect_p`.
- **`GetGarbageValue`** clears the landfill bit after sign extension, so a landfill cell reads as a negative number, and `IsGarbageAccumulated` (unsigned comparison) then reports it as accumulated.
- **`IsAirPolluted` and friends** pass their `z` argument's stack slot as the getter's output.
- **`GetPollutionValue`** reads the grids at +0x40, not the ones `GetPollutionGrid` returns (+0x50); negative values read as 0.
- **Cells and tracts.** `SetAirValue`, `SetWaterValue`, `SetGarbageValue` and `SetIsRadioactive` take city cells and shift them to tracts (`GetTractShift`); `SetIsActiveLandfill` passes cells straight to `SetTractValue`. `SetIsActiveLandfill(true)` only works on landfill-zoned cells.
- **Clamps.** `AddToGarbageSupply` saturates at 0x7FFFFFFF and 0. `GarbagePutInLandfill`'s clamps compare against the old total instead of the limits they seem meant for: the overflow clamp computes from the low word only, and the one for negative amounts can never fire.
- **`GetGarbageSupply`** resets a negative supply to 0 when it is read.
- **Capacity getters.** `GetWaterTreatmentUsedCapacity` asks `GetMaxCapacity` for property 0x28ECF510; the incinerator and waste-to-energy "current" and "used" getters use different properties (0xA8EBCF0C/0xE8ECF641, 0x48EBD0B1/0x88ECF76C), and `GetIncineratorCount` counts the list at +0x6C, which the garbage simulation reads with the "used" incinerator property.

## Floating point

Our code is compiled for SSE; the game's uses x87. **The simulation runs with the x87 control word at 0x027F: 53-bit (double) precision, round to nearest, all exceptions masked**, the Windows default for 32-bit processes. Measured inside the simulator's tick (the autotest's new-day agent logs it the first time and whenever it changes) with the stock DirectX 7 driver windowed and full screen, and with our D3D11 driver: the same word, never changed during a run (2026-10-07). So neither driver changes it, though Direct3D 7 can switch to single precision.

The game expects that it might. 0x00892A0A reads the precision and 0x008929E0 sets it (0 = 24-bit, 1 = 53-bit, 2 = 64-bit), and the Lua glue (hundreds of small functions at 0x008B6C30 to 0x008BF160) and a number parser (0x0067BA40) switch to 53-bit around their work only if the FPU is in single precision. Other `FLDCW` sites save and restore around their own code: the C runtime (`__ftol`, `_control87`, math functions), libpng, and functions at 0x008246C8 and 0x00824F08 that switch to truncation for a conversion.

What this means for reproducing x87 code exactly with SSE:
- Operations on doubles give the same results (same precision and rounding).
- Single operations on floats also give the same results: the game computes them at 53-bit precision and rounds to float only when it stores (`FSTP float`), and for one addition, subtraction, multiplication, division or square root that rounding twice equals rounding once.
- **Chains** do not: the game keeps intermediate results in registers at 53-bit precision. Our code has to compute them as `double` and convert to `float` exactly where the game stores to a float in memory.
- Integer conversions: `FILD` is exact; `__ftol2` (0x009EEF04) truncates toward zero, like a C++ cast.

The slots replaced so far only copy floats (`GetGarbageScalingFactor`, `GetRecyclingCenterMaxCapacity`'s default) or truncate a double (`GetGarbageRecycledLastMonth`). The slots left out (53, 55-57, 65) and the message handlers compute with x87 chains and follow the rules above.

## Parity

Run with `scripts/run-parity.sh "$B" <out> 7 "$LOCALAPPDATA/opensc4/parity/saves" OPENSC4_PARITY_B=simulators.pollution.interface=ours` ([parity.md](parity.md)).

Results (2026-10-07, 12 saves, 7 game days each; result folders under `%LOCALAPPDATA%\opensc4\parity\`):

| Run | Result |
| --- | --- |
| original vs ours (`iface1`) | **12 of 12 identical, all on the first pair**; ours installed in every B run (a 60 s smoke run made 1.87 million `GetPollutionValue` calls through it) |
| original vs ours with B's heap shifted by 1,072 bytes (`iface1-shifted`) | 10 of 12 identical. Making Money d0090 and d0365 "partly": in each, the odd outcome came from a first run (b0, and on d0365 also the original's a0) and every retry matched the original's main outcome; on d0365 the original itself reached two outcomes. Repeated on those two saves (`iface1-shifted-mm2`): both identical on the first pair, as is the shifted control without our code (`shiftctl-mm`). Read as the harness's own noise, which the shifted control showed before (Rush Hour d0000: a day's tick count off by one, [parity.md](parity.md)) |

The odd outcomes all being first runs of a pair is a lead for the remaining noise in the harness, not followed up here.
