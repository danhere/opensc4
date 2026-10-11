# The region

The region screen is the last of the P8 world systems to be started ([ROADMAP.md](../ROADMAP.md)). This page maps what the game's region code contains and describes the parts that are ours. Build 641 addresses throughout. Rewritten the same way as the simulators: function by function on the original objects under the parity harness ([parity.md](parity.md)).

## What the region code contains

About 93 KB of game code in five parts (survey of 2026-10-10: vtables, the exe's class name table, callers).

| Part | Code | Size | State |
|---|---|---|---|
| Region data, `cSC4Region`: the open region's folder, `region.ini`, the `config.bmp` layout, the cities found in the folder | 0x004A44E0 to 0x004A74E0 | 12 KB, 41 functions | ours (`region.data.*`), below |
| Regional city, `cSC4RegionalCity` (class 0x6A037C24, main vtable 0x00A8FB20): what the region knows of one city without loading it (name, position, size, population, neighbour connections and deals) | 0x004A74E0 to 0x004ADC00 | 25 KB, 129 functions | the game's |
| City files and thumbnails: opening a city file as a regional city (0x005DC220), file names (0x005DC400, 0x005DC790), serial numbers (0x005DBD40), the saved pictures of a city (resource type 0x8A2482B9; 0x005DCA10 to 0x005DEA60) | 0x005DBC10 to 0x005DEA60 | 10 KB | the game's |
| Region screen window, `cSC4WinRegionScreen` (window class 0xEA659793; vtables 0x00AB92A0 and 0x00AB926C): opening and closing a region, the commands `LoadRegion` and `RegionBitmapLoad`, the terrain made from a region bitmap (0x007AEDD0), the panels | 0x007AC5C0 to 0x007B2440 | 24 KB | the game's |
| Region map window, `cSC4WinRegionView` (window class 0x2BA6BB97; vtables 0x00AB9698 and 0x00AB9664): the map the cities are picked on | 0x007B2440 to 0x007B7B00 | 22 KB | the game's |

The application's own region code (choosing and loading the region at start, around 0x00497D00 to 0x00499700) belongs to `cSC4App`. The map is one large picture composed on the CPU from the cities' thumbnails ([ui-scaling.md](ui-scaling.md), "Text and the region view").

## The region data class

`cSC4Region` (0x64 bytes, interface `cISC4Region`, ID 0x89D2BE26, vtable 0x00A8FA40 with 26 slots) is not a GZCOM class: the application makes it with the constructor (0x00497DA1) or through `Create`.

| Offset | Content |
|---|---|
| +0x04 | started (byte) |
| +0x08 | reference count |
| +0x0C | name (string) |
| +0x20 | folder name (string) |
| +0x34 | terrain type (0 or 1) |
| +0x38, +0x3C | water settings, minimum (60) and maximum (100) |
| +0x40 | list of cities found in the folder that fit no layout tile |
| +0x44 | vector of layout tiles, 12 bytes each: x, y, log2 of the side in km |
| +0x50 | cell map: 1 km cell (x, y) to the holder of the city covering it (buckets +0x54, count +0x60; hash `((x & 0xF) << 4 | y & 0xF) % buckets`) |

A holder is four bytes with a counted pointer to the regional city; all cells of one city share one holder, and `cISC4App::LoadCity` is given the holder.

What the class does:

- **Init** (slot 8) on first use reads the settings (`LoadConfig`, slot 7: `region.ini`, section "Regional Settings", keys "Name", "Terrain type", "Water Min", "Water Max"; a name of `#` and a number is a text of group 0x6A231EAA), the layout (`LoadLayout`: `config.bmp` from the folder, or the built-in picture 0x2A1F0276 when it is missing) and the cities (`MatchCities`).
- **The layout** (`DecodeLayout` 0x004A5640): each pixel is a 1 km square; red 255 counts 1, green 255 counts 2, blue 255 counts 4, and a sum of exactly 1, 2 or 4 that fits in the picture and is not yet covered makes a tile of that many km ([city-size.md](city-size.md)).
- **The cities** (`ScanFolderForCities` 0x004A58D0, `MatchCities` 0x004A6330): every `.sc4` file in the folder is opened as a regional city; one that sits exactly on a tile of its size is inserted (`InsertCity`, slot 12: its holder in every cell under it). The rest go to the list at +0x40, and `MoveCitiesAway` (0x004A51C0) then moves their files out of the region folder.
- **Slots for the screen:** `GetCity(x, y)` (11), `GetAllCities` (17; in pointer order), `GetCityLocations` (23; the tiles), `GetBoundingRect` (24; of tile origins, sizes not counted), `RemoveCity` (13), `DeleteCity` (14; with its file), `ReloadCity` (15), `MoveCity` (16; to another tile or region, deleting what is there), `ResetTutorialCity` (22), terrain and water getters and setters (19 to 21).
- **Shutdown** (slot 9) removes every city and writes `region.ini`; **Delete** (slot 10) deletes every city file, `region.ini`, `config.bmp` and the folder. `Create` (0x004A67B0) makes a new region folder (the name with a number added if taken) with one of the built-in layout pictures, picked at random.

### Replacements

| Group | Functions |
|---|---|
| `region.data.part1` | QueryInterface, Release, name and folder setters, the cell map (find, insert, erase, resize), GetCity, CityCellsTaken, InsertCity, RemoveCity, ReloadCity, DeleteCity, GetAllCities, GetCityLocations, GetBoundingRect, terrain and water slots, MoveCitiesAway, the destructor (22 functions; [region_data_1.cpp](../src/core/region_data_1.cpp)) |
| `region.data.part2` | the constructor and deleting destructor, DecodeLayout, ScanFolderForCities, Shutdown, ResetTutorialCity (6; [region_data_2.cpp](../src/core/region_data_2.cpp)) |
| `region.data.part3` | Init, LoadConfig, LoadLayout, MatchCities, Delete, Create, CreateUnnamed, MoveCity (8; [region_data_3.cpp](../src/core/region_data_3.cpp)) |

Left the game's: two generic helpers in the range that unrelated code also calls (0x004A4530 list transfer, 0x004A46F0 bit vector), two shared container helpers, the three slots folded with other classes' getters (AddRef, the name and folder getters), and everything the class calls in the string, file, INI, image and resource classes.

### Odd things in the original, kept

- `Release` never lowers the count below 1 before destroying the object.
- `GetBoundingRect` on a region with no tiles answers the largest and smallest int, not zeros.
- `RemoveCity` frees the holder and leaves the caller's pointer dangling; `InsertCity` overwrites a holder already in a cell without releasing it.
- `ResetTutorialCity` returns 1 only when writing the file failed.
- `MoveCity` within one region still builds a new file path and moves the file (the branch that would keep the path cannot be reached), and it shuts the target region down without starting it again.
- `GetAllCities`, `ReloadCity`, `DeleteCity`, `Delete`, `LoadConfig`, `LoadLayout` and `MatchCities` return whatever their last call left in EAX. Tests must not compare those.

### How it is tested

Every unattended run opens a region before it loads a city, so Init, the layout, the folder scan, GetAllCities and GetCity run in every parity run. The fingerprint's `rgn` part ([region_autotest.cpp](../src/core/region_autotest.cpp)) has the region's name, a checksum of its settings, the bounding box, the tiles, the leftover list, the cell map's count, and for every cell of the box the position and size of the city found there.

- `OPENSC4_TEST_REGION_DRIVE=1` makes a second region object on the open region's folder and works it through the class: constructor, folder name, Init, GetAllCities, QueryInterface, the terrain slots, ReloadCity on the first tile's city, LoadConfig, LoadLayout and MatchCities again, Shutdown (which writes `region.ini`) and the last Release, describing the object after each stage.
- `OPENSC4_TEST_REGION_CREATE=1` creates a region as the game does ("OpenSC4 region test"), starts it, describes it and deletes it again, folder and all.

Not reached: DeleteCity, MoveCity, MoveCitiesAway with a city to move and ResetTutorialCity with a real tutorial (they delete or overwrite city files), and SetName.

Parity (2026-10-10, all three groups on, both switches, twelve saves for two days): the region part of the fingerprint is identical in all 34 runs, original and ours. Whole-city verdicts: 11 of 12 same; Big City day 365 differed in traffic cells in one run of ours while the original itself gave two outcomes there under load, and was same on the first pair when run on its own.
