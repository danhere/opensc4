# The crime simulator (cSC4CrimeSimulator)

The fifth simulator to be reimplemented, after pollution ([pollution.md](pollution.md)), power ([power.md](power.md)), water ([water.md](water.md)) and traffic ([traffic.md](traffic.md)), the same way: function by function on the original object under the parity harness ([parity.md](parity.md)), then our own object under the class ID. Build 641 addresses throughout. The police simulator (fire and police are another thread's work) is only called through its interface.

## The class

Class ID 0x8990C09A. Object size 0x174. Factory 0x006A05C0 (`SC4::operator_new(0x174)`, then the constructor 0x006A0440). Code from about 0x0069F1A0 to 0x006A2300. Four interfaces:

| Vtable | Interface | Slots |
| --- | --- | --- |
| 0x00AB0DC8 | `cISC4CrimeSimulator` (primary, IID 0x67659555) | 14, the scalar deleting destructor (14) and a type (15, 0x27567222): QueryInterface 0x0069F1B0, AddRef 0x00695330 (shared), Release 0x0069F5D0, Init 0x006A15B0, Shutdown 0x006A1860, then the getters (5-13) |
| 0x00AB0DB8 | `cIGZMessageTarget2` | DoMessage 0x006A21C0 |
| 0x00AB0D9C | interface 0x89C498BE | 3 crime over a rectangle (0x0069F710), 4 a type (0x49A1E996), 5 true, 6 a candidate for an incident (0x006A00D0); Init registers it with the service at 0x00B43DB8 |
| 0x00AB0D84 | `cIGZSerializable` | Write 0x0069F600, Read 0x006A05E0, GetGZCLSID 0x0069F1A0 |

gzcom-dll has `cISC4CrimeSimulator.h`. `GetAverageCrime` (slot 6) returns the float at +0x6C truncated.

### Fields

| Offset | Field |
| --- | --- |
| +0x10, +0x11 | initialised; loaded from a save (Init then leaves the tract walk alone) |
| +0x14 | reference count (stays at 1 when the last reference goes) |
| +0x18 | the tract walk's rectangle iterator (saved as one record, as the pollution simulator's) |
| +0x48 to +0x58 | references: city, message server, occupant manager (city slot 34), the service at 0x00B43D90 (ordinances), the police simulator |
| +0x5C, +0x60 | the crime grid and the effect grid (`cSC4SimGridUint8`, IDs 0x89D5B895 and 0x2A5BA1B3, cell size +0xD8) |
| +0x64, +0x68 | saved, otherwise unused |
| +0x6C | average crime (float) |
| +0x70 | the month's incidents (vector of 0x2C-byte records: lot, crime type, name, key, visuals created) |
| +0x7C | the month's crimes |
| +0x80, +0x84, +0x88 | the hotspot: cell x, z, value |
| +0x8C | the ordinance factor (float, from message 0xC2C09554) |
| +0x90 | the step's state; +0x94 the expected crimes; +0x98 the crimes to place |
| +0x9C | people by group (3) and land value quarter (4) |
| +0xCC | demand factors by group (3 floats) |
| +0xD8 to +0x170 | tuning: the grids' cell size, curves +0xDC, +0xEC (3), +0x11C (3), +0x14C, +0x15C, floats +0x16C, +0x170 |

The crime types are global (vector at 0x00B48620, loaded once, flag 0x00B48618), as is the generator crime draws from (0x00B48630).

### Messages

| Message | What happens |
| --- | --- |
| 0x66956814 | the step with 5 |
| 0x6695681A | the step with the message's value |
| 0x66956816 MonthPassed | state 6 (waiting) back to 0 |
| 0x99EF1142 / 0x99EF1143 | a building added or removed (unless its exemplar has 0x029244DB): its crime effect (0x0069F260) |
| 0xC2C09554 | the ordinance factor: the service at 0x00B43D90, slot 25 (0x28ED0380) |
| 0xC53D10AA | resources reloaded: the tuning (instance 0xC8E8F6C5 of group 0xE7E2C2DB) or the crime types (group 0xEA12F32C) |

## The step

The step (0x006A1FD0, `simulators.crime.step`) runs one state per call (+0x90):

- 0: waits for Init (+0x10).
- 1: `CountPeople` (0x0069FB00): from the service at 0x00B43DA4, three uint16 grids (people of three groups, slot 25) and three float grids (slot 23); each cell's people go to the quarter its value falls in (value x 0.02, truncated) at +0x9C. `ComputeFactors` (0x0069F780): per group, the demands listed at 0x00B0D1F8 (0x4100 0x4200 0x4300 0x3110 0x3120; 0x4400 0x3320 0x3130; 0x3330; through the service at 0x00B43D74, slot 6) summed from their slots 12, 14 and 15, then (12 - 15) / (14 - 15) x 2 - 1 into +0xCC.
- 2: `ExpectedCrimes` (0x0069F360): per group, each quarter's people times the quarter curve at 0, 25, 50, 75 (summed as floats), plus the factor curve at the group's factor times its people; truncated per group, summed into +0x94.
- 3: the crimes to place (+0x98): when the expected crimes exceed the police simulator's criminals (slot 30), the excess through curve +0x15C, plus the unjailed criminals (slot 27) times +0x170; then times the ordinance factor, rounded to nearest (through a float).
- 4: `Decay` (0x0069FC40, each non-zero cell times +0x16C), `ClearIncidents` (0x006A1530), `GenerateCrimes` (0x006A1DE0) into +0x7C, the police simulator's monthly statistics (slot 38), `UpdateAverage` (0x0069FD70: the crime grid's average over zoned cells, zone manager slot 12).
- 5: message 0x88EA4DFF (map changed) with the simulator, then 6: waiting for MonthPassed.

`GenerateCrimes` makes +0x98 tries. Each takes a random lot (lot manager slot 13); the police power over the lot (police grid, slot 8 over its rectangle) less the effect grid's goes through the chance curve (+0x14C); a draw from 0x00B48630 below that value means no crime. Otherwise `CreateIncident` (0x006A1A40) chooses a crime type: those whose four masks match the lot's (`LotMasks` 0x0069F850: its zone's kind, its building's two levels, its wealth) are drawn by weight; the incident is recorded with the type's name and key, message 0x0A159D95 goes out, and for types with the notify flag the lot's building is told (through the city's disaster layer, slot 72, and its object 0x621CDA33). The type's strength is added to the crime grid over the lot (saturating at 255), and the highest cell becomes the hotspot.

## Tuning

`LoadTuning` (0x006A07A0, Init and message 0xC53D10AA): +0xD8 = 2, +0x16C = 0.5, +0x170 = 1, then from the exemplar 6534284A-E7E2C2DB-C8E8F6C5: 0x47E2C200 (cell size), curve 0x49A34301 (+0xDC), 0x8A0CBAAC and 0x8A0CBAAD (the floats), and the curves 0x8A0CBAA4-A6 (quarters, default (0, 0.1) (255, 0)), 0x8A0CBAA7-A9 (factors, default (1, 0) (2, 0.1) (5, 0.5)), 0x8A0CBAAA (+0x15C, default (0, 0) (1e6, 5e4)) and 0x8A0CBAAB (+0x14C, default (0, 0) (255, 1)).

The crime types (`LoadCrimeTypes` 0x006A1450, `LoadCrimeType` 0x006A0BA0): one 0x3C-byte record per exemplar of type 0xEA12F32C in group 0x6534284A: the name (localized text, else 0xAA12FCB0), the key (0xAA12FCB1), four masks (0xAA12FCB2, B3, B4, B7: lists of bit numbers, all bits by default or for a 0), strength (0xAA12FCB5, 0x40), notify (0xAA12FCB6) and weight (0xAA12FCB8, 10). The first Init registers the cleanup 0x006A13E0 with the service at 0x00B43C94 (slot 69).

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.crime.interface` | slots 5-13 of `cISC4CrimeSimulator`; the nearest-prop and nearest-sim queries use our own occupant filters (the game's filter vtables with our tests, 0x0069F490 and 0x0069F4D0) |
| `simulators.crime.step` | the step (0x006A1FD0) |
| `simulators.crime.helpers` | `CountPeople`, `ComputeFactors`, `ExpectedCrimes`, `Decay`, `ClearIncidents`, `UpdateAverage` |
| `simulators.crime.generate` | `GenerateCrimes`, `CreateIncident`, `LotMasks` |
| `simulators.crime.messages` | DoMessage (+4 slot 3) and the building effect handler (0x0069F260) |
| `simulators.crime.tuning` | `LoadTuning`, `LoadCrimeTypes`, `LoadCrimeType` (with the mask helper 0x0069FCF0, which only it calls), the cleanup 0x006A13E0 |
| `simulators.crime.init`, `.shutdown` | Init, Shutdown |
| `simulators.crime.save`, `.load` | Write, Read (version 6) |
| `simulators.crime.targets` | slots 3 and 6 of the interface at +8 |
| `simulators.crime.object` | our own object under the class ID: constructor (0x006A0440), destructor (0x0069FF90), identity, the type slots, our copies of the four vtables |

Still the game's: the vector, string and curve helpers (generic containers), the radial effect (0x005E3D80, shared with other simulators).

## Test cities

The monthly pass runs once a month (state 6 waits for MonthPassed), so a week's run often has none. `OPENSC4_TEST_CRIME_MONTH=<n>` restarts it every n days of a run, as MonthPassed does. With n = 2 every week's run passes through it two or three times. Over longer runs, the original disagrees with itself after a month's end on Big City (pollution and traffic diverge), so the corpus runs are a week long.

The fingerprint's `crime` part: a checksum of the crime grid (slot 5), the average and the month's count (6, 7), the hotspot (8), the step's fields +0x7C to +0xD8, and the incidents (count, and a checksum of each one's type and key).

## Save record

Version 6 (Write and Read, the `cIGZSerializable` part at +0xC): the two grids (references), the tract walk, +0x64, +0x68, the average and the ordinance factor (floats), the month's crimes, the hotspot (x, z, value), then the incident count and each incident: its lot (a reference), crime type, name (string) and key (three uint32). Read appends the incidents and sets +0x11.

## Parity

All runs with `OPENSC4_TEST_CRIME_MONTH=2` (2026-10-09, two games at a time). Result folders under `%LOCALAPPDATA%\opensc4\parity\crime\`.

| Run | Result |
| --- | --- |
| the interface alone (`iface7`) | 12 of 12 for a week |
| every piece but the object (`all7`) | 11 of 12; Rush Hour d0365's B run crashed during load at 0x0096DA20, the rare load crash [parity.md](parity.md) describes (now four times, twice on this save); two reruns of it passed (`rh365-rerun1`, `-rerun2`), crime records identical |
| everything, our object included (`obj7`) | 11 of 12; Big City d1095 differed only in the traffic save record of one B run (fingerprints and crime record identical); the original reaches that split by itself on this save (`iface7`, A with two outcomes), and a rerun with retries passed (`bc1095-obj`) |
| everything on `saves-plopped` (`objpl7`) | 3 of 3, our object made in every B run |
| everything for 90 days on `saves-mm2` (`objmm90`) | 2 of 2, 91 of 91 days identical |
| everything with `OPENSC4_TEST_CRIME_QUERIES=1` (`objq7`) | 12 of 12, all on the first pair |
| the same after rebasing onto main with the land value and traffic objects (`rebased7`, three games) | 11 of 12; Big City d0365 split in the traffic record and a tick count, the split the original reaches by itself on this save; the rerun (`bc365-rebased`) passed with the original showing both outcomes. Crime records identical in all 12 |

`OPENSC4_TEST_CRIME_QUERIES=1` adds the queries only the UI and the automata make to the fingerprint (slots 9 to 13: the incident events, the visuals flag of the first incident, a crime sim and a crime prop near the hotspot); no test city has crime sims or props, so the nearest queries return none on both sides.

Coverage (the original under `diagnostics.coverage`, Big City d0090, a week with the monthly pass): every crime function that ran is replaced. Not run there, and reached only through the queries switch or not at all: the visuals flag getters, the nearest queries and their filters, `GetCrimeIncidentArray`, the cleanup 0x006A13E0 (at exit), the type slots. The candidate pick (+8 slot 6) ran in two of the B runs.
