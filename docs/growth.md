# The growth developers (cSC4GrowthDeveloper)

One growth developer per developer type (residential, commercial and industrial by wealth, agriculture, and the other industries) keeps the maps that type grows from and the growth and decline lists the building development simulator ([development.md](development.md)) reads. Reimplemented after the building development simulator, the same way: written in three parts at once, the calls audited against the game's pushes and RET sizes, then checked under the parity harness ([parity.md](parity.md)). Build 641 addresses throughout (data/addresses/growth.toml).

## The class

Class ID 0x0990C138. Object size 0x13C. Constructor 0x006B7310, destructor 0x006B7640, factory 0x006B7870 (used when a save is read). Code 0x006B3790 to 0x006B8960. The building development simulator makes one per developer type in its tuning callback 0x006991A0 (operator new, the constructor, AddRef, Init with the type's exemplar), and keeps them in its map +0x5C.

| Offset | Vtable | Interface | Slots |
| --- | --- | --- | --- |
| +0x0 | 0x00AB1488 | `cISC4GrowthDeveloper` | 32: QueryInterface 0x006B37A0, AddRef (shared), Release 0x006B3D00, Init 0x006B8510, Shutdown 0x006B59A0, the queries (5-30), the deleting destructor 0x006B7890 (31) |
| +0x4 | 0x00AB1474 | `cIGZMessageTarget2` | DoMessage 0x006B7030 |
| +0x8 | 0x00AB145C | `cIGZSerializable` | Write 0x006B3960, Read 0x006B39F0, GetGZCLSID 0x006B3790 |

Twelve slots of the primary are one-line getters the game shares with other classes (5 the zone kind +0xD0, 6 the subtype +0xCC, 7 +0x38, 8 the wealth +0x3C, 11 the growth list +0xEC, 13 to 16 the grids +0x114, +0x11C, +0x118, +0x120, 17 the total capacity +0x108, 18 the demand +0x34, 25 the monopoly use +0xBC); they stay the game's.

### Fields

`src/core/growth_sim.h` names them; the full lists with their evidence are in the header comments of the three source files. In short: +0x14 to +0x30 the services and the exemplar Init was given; +0x34 the developer's demand; +0x38, +0x3C purpose and wealth; +0x44 the grids' tract size; +0x45 to +0x47 the land value range and a flag; +0x48 to +0x8C the tuning curves (other simulators' grids to desirability), +0x50 a pointer to four pollution curves; +0x90 to +0x98 the base desirability and the growth and decline thresholds; +0xAC to +0xAF the power, water, fire and park caps; +0xB0 the abandonment thresholds by density; +0xC0 the style percentages; +0xCC the subtype, +0xD0 the zone kind (1-3); +0xD4 the map update's state, +0xD8 its start day, +0xDC its cell iterator; +0xE4 the developer's occupants; +0xEC and +0xF8 the growth and decline lists (12-byte cells); +0x104 the capacity last told to the demand; +0x108 the total capacity; +0x10C to +0x120 six grids (fit counts, work and desirability, population, capacity, maximum capacity); +0x124, +0x130 the subjective thresholds.

## The simulation

The building development simulator starts a map update on every developer (slot 27) when its queue state machine starts, and waits until all are finished (slot 28). The developer runs one state per simulator message (0x66956814 with a 5 ms budget, 0x6695681A with data 3 as the budget):

| State | What happens |
| --- | --- |
| 1 | waits for Init, stamps the start day |
| 2 | the work grid +0x110 filled with the base desirability |
| 3-14, 16 | the tuning curves applied to other simulators' grids (land value, pollution, crime, traffic, services and so on) and added |
| 15 | the developer's own occupants' effects spread in, one occupant per call |
| 17 | the work grid copied to the desirability grid; the growth list made from the cells that fit |
| 18 | the capacity grid scaled by desirability into the population grid, within the budget |
| 19 | the demand told the change of the weighted capacity |
| 20 | the growth list sorted by desirability, the decline list made (shuffled for agriculture and industry), the growth list thinned; back to idle |

Message 0x07699455 updates the capacity of a changed rectangle; 0xE8BDA2E7 (an occupant's stage changed) adds or takes its capacity when the occupant is the developer's.

The save record is version 1 (Read takes only 1): the capacity last told to the demand, the total capacity and five grid references (+0x110 is not saved). `tools/re/save_schemas/growth.py` decodes it.

## What is ours

| Replacement | What is ours | File |
| --- | --- | --- |
| `simulators.growth.queries` | slots 9, 10, 12, 19-24, 26-30 (EstimateDesirability and GetSubjectiveThresholds among them) | growth_queries.cpp |
| `simulators.growth.messages` | DoMessage and everything it reaches: the map update, the grid mappers, the sort of the lists, the rectangle and stage-change updates; a detour on the uint16 mapper 0x006B4710, which code outside the class calls too | growth_messages.cpp |
| `simulators.growth.init`, `.shutdown`, `.save`, `.load` | Init with the tuning (0x006B78B0, about 40 exemplar properties) and the class's curve-vector helpers, Shutdown, Write, Read | growth_object.cpp |
| `simulators.growth.object` | our own object: a detour on the constructor (both ways of making one go through it), the destructor, identity, our copies of the three vtables | growth_object.cpp |

Still the game's: the shared getter slots, the generic map lookups 0x006B5840 and 0x006B58C0 (other classes call them), the int16 grid mapper 0x0068C270 (outside the class), the curve, grid, cell iterator, deadline, stopwatch, random generator and container helpers. Copied on purpose from the game: Shutdown frees the curve +0x4C without clearing the pointer; Read starts its version from whatever its argument's stack slot held; one tuning loop reads one element past an odd count. Where the game reads stack memory it never wrote, ours uses zeros.

## Test cities

The fingerprint's `growth` part: per developer its type, the map update state, the total capacity, the sizes of the growth and decline lists and checksums of the four grids slots 13 to 16 return.

## Parity

All runs on 2026-10-09 with every `simulators.growth.*` group on, two games at a time, other threads' games running too. Result folders under `%LOCALAPPDATA%\opensc4\parity\crime\`.

| Run | Result |
| --- | --- |
| everything but the messages, 12 saves for a week (`gro-p1`) | 11 of 12; one B run crashed while loading, in game code (0x00623079, reading a `#Transmogrify` header), before any developer existed |
| everything, 12 saves for 30 days (`gro-all30`) | 12 of 12 (Big City d1095 and Rush Hour d0090 on a retry pair; the original reached 2 and 3 outcomes there); growth and decline lists filled in every city |
| everything with `OPENSC4_TEST_GROWTH_QUERIES=1`, a week (`gro-q7`) | 12 of 12 |
| `saves-plopped`, 30 days, with the queries (`gro-pl30`) | 3 of 3 |
| 90 days on `saves-mm2` (`gro-mm90`) | 2 of 2 |
| on main with fire, police and flammability objects and the simulator core merged, a week with the queries (`gro-main7`) | 11 of 12; on Rush Hour d0365 the original's run did not start (empty log), the rerun (`gro-main7-rh365`) same |

Our object was made for every developer in every B run, and the developer records were identical wherever a pair was the same. Before the first run, an audit of every call against the game's pushes and RET sizes found one wrong argument (the stage change gave the development simulator's purpose lookup the occupant instead of message data 3).
