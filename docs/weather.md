# The weather simulator (cSC4WeatherSimulator)

Temperature, humidity, wind and the moisture map flora grows from. Reimplemented the same way as the other simulators ([aura.md](aura.md), [water.md](water.md)): function by function on the original object under the parity harness ([parity.md](parity.md)), then our own object under the class ID. Build 641 addresses throughout.

## The class

Class ID 0x2990C1BC. Object size 0x168. Factory 0x00729310 (`SC4::operator_new(0x168)`, then the constructor 0x00728C40). Code from about 0x007255C0 to 0x00729EB0. Four interfaces:

| Vtable | Interface | Slots |
| --- | --- | --- |
| 0x00AB3610 | `cISC4WeatherSimulator` (primary) | 24 and the scalar deleting destructor (slot 24): QueryInterface 0x00725720 (0xC6C66245 and 1 at +0, 0xE98B2F57 at +0x4, 0x452294AA at +0x8, 0xE4FDA3D4 at +0xC), AddRef 0x00695330 (shared, count at +0x14), Release 0x00725A40, Init 0x00729900, Shutdown 0x00725940, DoPostCityInit 0x007296F0, then the cell geometry, season, moisture, humidity and wind slots |
| 0x00AB35FC | a listener (IID 0xE98B2F57) | slot 3 an empty stub (0x004FC900) |
| 0x00AB35EC | `cIGZMessageTarget2` | DoMessage 0x00729720 |
| 0x00AB35D4 | `cIGZSerializable` | Write 0x00725E80, Read 0x00728F40, GetGZCLSID 0x007255B0 |

### Fields

| Offset | What |
| --- | --- |
| +0x10, +0x18, +0x19 | initialised; loaded from a save; DoPostCityInit done |
| +0x1C, +0x20, +0x28 | the city, the simulator service, the terrain (references) |
| +0x2C, +0x30 | weather cells (one per 16 m; the city size / 16) |
| +0x34 ... +0x50 | cell width (16), its inverse, the city size in metres, city cells per weather cell |
| +0x54, +0x5C | cells per tick divisor, the spread radius of a cell's own moisture |
| +0x60, +0x160 | season, month |
| +0x64 ... +0x70 | base temperature and its yearly swing; this month's temperature |
| +0x74 | rows of bytes: each cell's own moisture |
| +0x80 | rows of bits: cells near water bodies |
| +0x8C | water bodies {x0, z0, x1, z1} |
| +0x98 | the tick's walk over the cells (a tract iterator) |
| +0xC8 | cells per tick |
| +0xCC ... +0xD8 | body margin; water, height and wind factors |
| +0xDC | rows of bytes: the moisture map (slots 17-19) |
| +0xE8 | rows of bytes: manual moisture (slot 20; seeded at random for a new city) |
| +0xF4 ... +0x10C | this month's humidity; base, swing; slope scale, rain, slope limit |
| +0x118 | a new season's wind |
| +0x124 ... +0x144 | today's wind (speed, direction; slot 22), the month's start and target |
| +0x148 | random generator |
| +0x164 | the spread of the month's wind direction |

### Messages

| Message | What |
| --- | --- |
| 0x66956814 | a day: today's wind, between the month's start and target |
| 0x66956815 | a tick (0x00728A40): the next cells of the walk get their own moisture; the whole moisture map is rebuilt |
| 0x66956816 MonthPassed | the month (0x00729630): in January the water bodies are found again; the month's temperature, humidity and target wind; a new season |
| 0x26D31EC1 | DoPostCityInit |

## The moisture

- A cell's own moisture (0x00726890) is the month's temperature (warmer near water bodies) less the height and the wind, mapped from -15..45 to 0..255 and averaged into the cells around it.
- The map (0x00726BF0) blurs the terrain heights [1 2 1] both ways; each cell starts from the manual moisture (0xFF over water); each city cell in it adds the humidity, less the slope against the wind where the ground curves down and lies higher than the blurred ground, or plus the rain in hollows; that is blurred [1 4 6 4 1] both ways into the map. Message 0x09B6D58E follows.
- Water bodies (0x007283D0) are rectangles of cells at or below the water level at least 3 by 5; only those touching the city's edge are kept, grown by +0xCC into the water bits.

The game's x87 instructions FCOS (the month) and FPTAN (the tuning) are run as inline x87 code, so their results match bit for bit.

## What is ours

| Replacement | What is ours |
| --- | --- |
| `simulators.weather.interface` | slots 5-23 |
| `simulators.weather.messages` | DoMessage (+0x8 slot 3), with the day's wind |
| `simulators.weather.month` | the month (0x00729630), the month's weather (0x00729350), the message post (0x00725A70) |
| `simulators.weather.tick` | the tick (0x00728A40), a cell's own moisture (0x00726890, 0x00726360, 0x007259B0) |
| `simulators.weather.moisture` | the moisture map rebuild (0x00726BF0) |
| `simulators.weather.bodies` | the water bodies (0x007283D0, 0x00728220, 0x00726510) and the seeded manual moisture (0x00726400) |
| `simulators.weather.tuning` | the tuning (0x00725AF0) |
| `simulators.weather.init` | Init and Shutdown |
| `simulators.weather.save` | Write (version 2) and Read (versions up to 2) |
| `simulators.weather.object` | our own object under the class ID: constructor (0x00728C40), destructor (0x00728ED0), identity, our copies of the four vtables |

Still the game's: the container and stream helpers (row and bit vectors, the tract iterator, dates, random draws), GetGZCLSID. The month's sort of the four city edges is ours (an insertion sort; the keys never tie).

## Test cities

The January water-body search only runs in January: `OPENSC4_TEST_WEATHER_YEAR=<n>` runs the month handler as for January on the n-th day of a run. Days and ticks run the rest every day.

The fingerprint's `weather` part: checksums of the cell, moisture and manual maps and the water bits, the bodies, cells per tick, season, month, temperature, humidity and the wind as float bits. The save record has no references, so `tools/re/sc4save.py` compares it byte for byte.

## Parity

Run with `OPENSC4_TEST_WEATHER_YEAR=2` (the January handler on day 2), two jobs at a time. Results (2026-10-09; folders under `%LOCALAPPDATA%\opensc4\parity\`):

| Run | Result |
| --- | --- |
| everything, our object included, 7 days (`weather2`) | **12 of 12**; weather records identical in all 12; our object made and every replaced function reached in every B run but `SeedCellMoisture` (new cities only). Making Money d0365 "partly": only the first B run parted, in the harness's tick count (`checks`) on day 6, records identical; its three retry pairs matched |
| everything, `saves-plopped`, 7 days (`weather2-plopped`) | 3 of 3 |
| everything, 35 days (`weather2-35`) | 7 of 12 identical. In the other five the original itself reaches two to four outcomes (its own runs part, first in the tick count or the cell moisture map +0x74, which follows the ticks), and on four of them B shares an outcome with A; Rush Hour d0090 gave eight different outcomes from eight runs |
| everything, `saves-mm2`, 90 days (`weather2-90`) | d0365 identical for 91 days; d0090 "partly": the first B run parted on day 2, the three retry pairs matched; a fresh pair (`weather2-90-rerun`) was identical for 91 days, all eight records identical |

The weather's cell moisture walks a fixed number of cells per tick, so it is the first thing to show a day with one tick more or less; over 35 days that happens to the original as often as to ours.
