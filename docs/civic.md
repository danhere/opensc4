# The civic building simulator (cSC4CivicBuildingSimulator)

Landmarks, the conditional (reward) buildings' status, the building families other buildings may not repeat, and the ape escape. Reimplemented the same way as the other simulators ([aura.md](aura.md), [weather.md](weather.md)): function by function on the original object under the parity harness ([parity.md](parity.md)), then our own object under the class ID. Build 641 addresses throughout.

## The class

Class ID 0x0A413481. Object size 0x48. Factory 0x0069E800 (`SC4::operator_new(0x48)`, then the constructor 0x0069E670). Code from 0x0069D6B0 to 0x0069EA40 (the commercial simulator follows). Three interfaces:

| Vtable | Interface | Slots |
| --- | --- | --- |
| 0x00AB0C7C | `cISC4CivicBuildingSimulator` (primary, IIDs 0x8A41322F and 1) | QueryInterface 0x0069D6C0, AddRef 0x004664A0 (shared, count at +0xC), Release 0x0069D7A0 (the deleting destructor when the last reference goes, no Shutdown), Init 0x0069DC10, Shutdown 0x0069D7C0, IsBuildingExcluded 0x0069E5B0, GetConditionalBuildingStatus 0x0069D8D0, UpdateConditionalBuildingStatus 0x0069E9F0, GetLandmarkCount 0x0069E5D0, the deleting destructor 0x0069E700 |
| 0x00AB0C6C | `cIGZMessageTarget2` (+0x4) | DoMessage 0x0069E1A0 |
| 0x00AB0C54 | `cIGZSerializable` (+0x8) | Write 0x0069D920, Read 0x0069E820, GetGZCLSID 0x0069D6B0 |

### Fields

| Offset | What |
| --- | --- |
| +0xC, +0x10 | references; initialised |
| +0x14 | a budget department (0x8A416368), made by Init and saved |
| +0x18 | hash set of uint32: the building families (property 0xEA2E078B) of the city's buildings (slot 5) |
| +0x2C | hash map of building instance to status: a flag (uint8), a count of such buildings (uint16 at +2), a message (a string at +4) (slots 6, 7) |
| +0x40 | list of the occupants with flag 0x1917 (the zoo: candidates for the ape escape) |
| +0x44 | list of the occupants with a budget department (property 0xAA59670C): the landmarks (slot 8) |

The hash tables are the game's SGI `hashtable` (a word, buckets begin, end, capacity, the count; nodes next, key, value), 100 buckets to start.

### Messages

| Message | What |
| --- | --- |
| 0x99EF1142, 0x99EF1143 | a building (occupant type 0x278128A0) inserted or removed: a conditional building's count up or down (property 0xEA3209F8, its instance in +0x2C), its family into or out of +0x18; removed also runs 0x0069DAA0 |
| 0xE8BDA2E7 | a lot (its `IGZUnknown`) with state 3 (data 2): its building's department line and the lists (0x0069DE20) |
| 0x66956816 MonthPassed | 0x0069E010: one chance in a hundred (a `cRZRandom` seeded with -1, its float at most 0.01) that a random building of +0x40 gets an ape escape (city slot 47, then its slot 10 with "ape_escape"); then each landmark's lot is abandoned (lot slot 36 to 2) while its department's funding is below half, and back (0) when it is not |

The building handlers ask the budget simulator (slot 51) for the building's department line: 0xAA59670C for a landmark department, 0x4A5A495E for a second line (the building's item in a department, its cost added when developed and taken off when removed).

Init makes the department at +0x14 and two more if they are missing (0xEA59717A, 0x2A5A723F), takes the three messages, registers with the simulator service (phase 6) and registers the script table `civic` with the command `get_landmark_count` (0x0069D740).

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.civic.interface` | slots 5-8 |
| `simulators.civic.messages` | DoMessage (+0x4 slot 3) |
| `simulators.civic.month` | the month (0x0069E010) |
| `simulators.civic.buildings` | a lot developed (0x0069DE20), a building removed (0x0069DAA0) |
| `simulators.civic.init` | Init, Shutdown and the script command `get_landmark_count` |
| `simulators.civic.save` | Write and Read (version 6) |
| `simulators.civic.object` | our own object under the class ID: constructor, destructor, identity, our copies of the three vtables |

Still the game's: the container helpers (lists, the two hash tables' find, insert, erase, resize and clear, strings), the random generator, GetGZCLSID.

## Test cities

The test cities have landmarks and 60 to 70 conditional building statuses, but no zoo and no month's end in a week. `OPENSC4_TEST_CIVIC=<n>` drives the simulator on the n-th day of a run: MonthPassed, then the first landmark's building removed and inserted again (0x99EF1143, 0x99EF1142) and its lot's development message (0xE8BDA2E7, state 3), then slots 5 to 7 (the excluded check for the first status's key, the status set to itself and copied to a new key).

The fingerprint's `civic` part: the count and a checksum (in bucket order) of the excluded families and of the statuses (key, flag, count and message), the sizes of the two lists and whether the department is set. `tools/re/sc4save.py` decodes the record (version 6).

Not reached by the test cities: the ape escape (no zoo), the script command (called by the advisors' scripts).

## Parity

With everything on and `OPENSC4_TEST_CIVIC=2`, two jobs at a time. Results (2026-10-09; folders under `%LOCALAPPDATA%\opensc4\parity\`):

| Run | Result |
| --- | --- |
| everything, our object included, 7 days (`civic2`) | 11 of 12; civic records identical in all 12; every replaced function reached in every B run but the ape escape and the script command. Big City d1095 "partly": only the first B run parted, in the traffic record (density, cells), with every fingerprint identical; its three retry pairs matched |
| everything, `saves-plopped`, 7 days (`civic2-plopped`) | 3 of 3 |
| everything, `saves-mm2`, 90 days (`civic2-90`) | 2 of 2 (d0365 identical for 91 days; d0090 same after 3 pairs, where the original itself reached 3 outcomes) |
