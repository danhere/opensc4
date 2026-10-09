# Fire and police (cSC4FireProtectionSimulator, cSC4PoliceSimulator)

Item 3 of Dan's order (ROADMAP.md P5), done as the earlier simulators were ([water.md](water.md), [power.md](power.md)): function by function on the original objects under the parity harness ([parity.md](parity.md)). Build 641 addresses throughout. The crime simulator is a separate thread's work.

## The classes

Both derive from one abstract base the game registers as `cSC4ProtectionSimulator` (constructor 0x006F6900, code 0x006F5570 to 0x006F6BC0), placed at +8 of each:

| Class | ID | Size | Factory | Constructor | Code |
| --- | --- | --- | --- | --- | --- |
| `cSC4FireProtectionSimulator` | 0x2990C05A | 0x108 | 0x006ADB80 | 0x006ADA50 | 0x006ACE00 to 0x006AE390 |
| `cSC4PoliceSimulator` | 0x6990C04F | 0x168 | 0x006E2720 | 0x006E1F80 | 0x006DFF70 to 0x006E2CC0 |

Each object has four interfaces; `this` differs by which one a function is reached through, so the code names it: the object (cISC4FireProtectionSimulator, 26 slots at 0x00AB1160; cISC4PoliceSimulator, 45 slots at 0x00AB2490), the base at +8 (its own vtable, 18 slots: 0x00AB1118 for fire, 0x00AB2448 for police), the dispatch interface at +0xC (12 slots: 0x00AB10E8, 0x00AB2414) and cIGZSerializable at +0x10 (0x00AB10D0, 0x00AB23FC). The two primary vtables share code: fire's slot 8 (station funding), 11 (on strike), 15/16 (strike start and length), 19 (average coverage) and 22 (station list) are police code; police's Release, SetStationFunding, station count and the base's step are fire code. gzcom-dll's police header groups overloads, so its slot numbers from 28 on are off by its own order: 28 GetJailCapacity(occupant) 0x006E0AC0, 29 GetJailCapacity() (+0xD8), 30 GetJailPopulation (+0xDC), 31 GetCriminalCount (+0xE0), 32 GetJails 0x006E01A0, 33 IsJail 0x006E01E0, 34 GetArrestCount (+0xE4), 35 GetJailCount 0x006E14B0 (Ghidra's names for 0x006E01A0 to 0x006E14B0 are shifted).

## The base

Offsets from the base (object + 8). The base keeps the stations (list +0x80, buildings of type 0x278128A0 the derived class accepts through its slot 14), the coverage grids (uint8 +0x78 and int16 +0x7C, block size +0x2C), the walk that averages coverage over the developed cells (+0x34, cells a step +0x28 = cells / +0x30 steps), the city (+0x64), message server (+0x68), budget (+0x6C), department line (+0x74), and the dispatch slots of the stations' units (+0xA4 slots).

- Messages (0x006F62C0): step 0x66956814 (slot 5), month 0x66956816 (slot 6), occupants added/removed 0x99EF1142/43 (slots 7/8, unless the occupant has property 0x029244DB), 0xC8DAC052 (slot 9), funding 0x08DAC3E7 (coverage painted again), dispatch 0x894671F6 (a unit back, 0x006F5620) and 0xE94671EC/ED (coverage around a moved source).
- Coverage: the int16 grid is painted around each station and active dispatch source (0x006F5C30); a cell's byte is its int16 value times +0x98, rounded and clamped (0x006F5E40).
- Units: each station has a mask of units at home, a first dispatch slot and a count (the derived class's +0xC slot 7, from properties); a unit leaves (0x006F5570) or comes back (0x006F5620); the nearest station with a unit home answers a call (0x006F5B40).
- Saved: version 3 (0x006F5850 / 0x006F5920): department, walk, grids, stations, counters, average, share (a percentage before version 3), strike flag, strike day, dispatch slots.

## Fire

Offsets from the object. Init (0x006AE240) sets the line item kind (+0xB0, 0xEA567BC3), loads the tuning, sets up the department line (+0x7C) and the fire plane dispatch source (+0xD0, 0x28 bytes, constructor 0x006ACE00). Tuning (0x006ADDA0, exemplar 0x68F5524A): block size 2, 50 steps a walk, the efficiency curve by funding (+0xD4), the strike chance curve (+0xF0), strike length 60 days (+0x104), the funding that ends a strike 0.9 (+0xEC), efficiency on strike 0.5 (+0xE8), for unfunded lots 0.8 (+0x100).

A station's reach comes from its properties (0x08F55DC0 outer, 0x08F55DC1 inner, 0x08F55DC2 strength) times its efficiency (the curve at its line item's funding). Each month every station may start or end a strike (0x006ADBC0: property 0xEAA69F83 = the day it began); the department is on strike (+0xA4) while any station is.

## Police

Offsets from the object. Police adds jails (+0xD4), the jail department (+0xD0), criminals (+0xE0), jail capacity (+0xD8) and population (+0xDC, each jail's in property 0x48D71EE1), this and last month's crimes and arrests (+0xF0/+0xE8, +0xEC/+0xE4), crimes not cleared (+0xF4) and jailbreaks (+0x164). Tuning 0x006E2110 (exemplar 0x28DFCF89).

- A crime (message 0x0A159D95) is an arrest where the coverage byte is over +0x144 (16), else it is not cleared; the nearest station counts it (property 0x8A70A0B9: crimes and arrests last month, then this month).
- Monthly (0x006E2A80): criminals let go (+0x124 of them), strikes as for fire (with a 0-1 draw), jailbreaks ending (jailed share under +0x160) and starting (a draw under the jailbreak curve at the jailed share; some criminals escape).
- Saved: version 8 (0x006E03F0 / 0x006E04E0).

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.protection.base` | the base: messages, the walk, coverage, units, stations, Init, Shutdown, Write, Read (24 functions, protection_sim.cpp) |
| `simulators.fire.sim` | fire: identity, tuning, Init, Shutdown, strikes, efficiency, coverage, the base's slots, the dispatch interface, Write, Read (31 functions, fire_sim.cpp) |
| `simulators.protection.sources` | the fire plane and police helicopter dispatch sources: identity, Init, Shutdown, messages, units, buildings, Write, Read (29 functions, protection_sources.cpp) |
| `simulators.police.sim` | police: the same and jails, crimes, jailbreaks, crime statistics (59 functions, police_sim.cpp) |

Bisect inside a group with `OPENSC4_PROTECTION_SKIP`, `OPENSC4_SOURCES_SKIP`, `OPENSC4_FIRE_SKIP`, `OPENSC4_POLICE_SKIP` (function addresses in hex).

Not ours yet: the constructors, destructors and factories of the simulators and the sources (our own objects).

## Parity

With every group on (`simulators.protection.base`, `.sources`, `simulators.fire.sim`, `simulators.police.sim`): 11 of 12 saves for a week (the twelfth partly: one of three B runs differed early in a field outside fire and police, the other two matched A), 3 of 3 plopped saves, 1 of 2 for 90 days on Making Money (the other partly, the same pattern: one B run off in pollution, two matching A, and A itself with two outcomes). The fingerprint's `fire` and `police` fields (coverage grid hash, average, share, stations, units, strike, jail capacity/population, criminals, arrests, jails) are identical in every run that matched.

The test cities never strike or break out of jail, and only Making Money has a police station and a jail. `OPENSC4_TEST_PROTECTION=<day>` (autotest) sets every fire and police station's funding to 0 (by reference, as the budget panel does), runs both monthly handlers and starts a jailbreak at the first jail; 40 days from two Making Money saves with it: 2 of 2 identical, with strikes on both departments, the jail emptied by the breakout and coverage falling.

Found by parity on the way: the monthly roll-up (0x006E2B50) jails this month's arrests (+0xEC), not the crimes left uncleared (+0xF4); the base's budget line lookups take the station's property holder and the line item kind as two arguments (slot 51), and the occupant's slot 6 takes none.
