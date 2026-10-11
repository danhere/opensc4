# The region

The region screen is the last of the P8 world systems to be started ([ROADMAP.md](../ROADMAP.md)). This page maps what the game's region code contains and describes the parts that are ours. Build 641 addresses throughout. Rewritten the same way as the simulators: function by function on the original objects under the parity harness ([parity.md](parity.md)).

## What the region code contains

About 93 KB of game code in five parts (survey of 2026-10-10: vtables, the exe's class name table, callers).

| Part | Code | Size | State |
|---|---|---|---|
| Region data, `cSC4Region`: the open region's folder, `region.ini`, the `config.bmp` layout, the cities found in the folder | 0x004A44E0 to 0x004A74E0 | 12 KB, 41 functions | ours (`region.data.*`), below |
| Regional city, `cSC4RegionalCity` (class 0x6A037C24, main vtable 0x00A8FB20): what the region knows of one city without loading it (name, position, size, population, neighbour connections and deals) | 0x004A74E0 to 0x004ADC00 | 25 KB, 115 functions | ours (`region.city.*`), below |
| City files and pictures: opening a city file as a regional city (0x005DC220), file names (0x005DC400, 0x005DC790), serial numbers (0x005DBD40), the pictures a city file carries for the region map (resource type 0x8A2482B9) | 0x005DBC10 to 0x005DEA50 | 10 KB, 32 functions | ours (`region.files.*`) but the one drawing function, below |
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

## The regional city

`cSC4RegionalCity` (0x1A0 bytes, class 0x6A037C24, factory 0x004AD9A0) is what the region knows of one city without loading it. The region makes one per city file (0x005DC220 opens the file and has the object read its record); the application keeps the open city's as its regional city (slot 0x90). Three vtables: main 0x00A8FB20 (74 slots, interface 0x69D2BE1D), message target 0x00A8FB10 at +4 (0x452294AA), save record 0x00A8FAF8 at +8 (0xE4FDA3D4). The code runs from 0x004A74E0 to 0x004AD9C0; what follows to 0x004ADC00 is `cSC4ToxicOccupant` (disasters).

| Offset | Content |
|---|---|
| +0x0C | started (byte) |
| +0x10 | reference count |
| +0x14, +0x18 | position in the region, km |
| +0x1C, +0x20 | size, km |
| +0x24, +0x28, +0x2C | population, commercial jobs, industrial jobs |
| +0x30 | workforce share (float) |
| +0x34 | mayor rating (signed byte) |
| +0x38 | difficulty |
| +0x3C, +0x48, +0x54 | maps from a population type to its number, its extrapolated number and the limit of that |
| +0x60 | twelve tax rates (floats; 7.0 for types it does not have) |
| +0x90 | list of neighbour connections |
| +0x94 | vector of pending connection changes, 16 bytes each: type, x, z, made or removed |
| +0xA0 | moved (byte) |
| +0xA4 | serial number |
| +0xA8 to +0xB4 | original and last language and country |
| +0xB8 | birth date |
| +0xBC, +0xD0, +0xF8, +0x120, +0x154 | strings: save file path, city name, mayor name, description, utility advisor name (+0xE4, +0x10C, +0x134: three more) |
| +0x148 to +0x150 | world position (two floats), base elevation |
| +0x168 | established (byte) |
| +0x16C | tutorial ID, -1 for none |
| +0x170, +0x178 | budget, income (doubles) |
| +0x180 | map from a commodity (1 power, 2 water, 3 garbage) to five doubles: produced, demanded, exported, imported, cost per unit |
| +0x18C, +0x190 | all neighbour deals; the deals with the open city |
| +0x194 | map from a kind (0 airports, 1 seaports) to a vector of points (three floats) |

What the class does:

- **The save record** (`Write` 0x004A7D10, `Read` 0x004AC5B0; version 13, reads older ones) holds everything above but the file path and the pending changes. After the record, `Read` takes the name of a city not yet established from the game's texts, and the names of a city file that carries its own text records from those.
- **From a loaded city** (`UpdateCityCache`, slot 24): population and jobs by type from the demand simulator, tax rates, budget, the connections, airports and seaports. **Into a city** (`SetupCity`, slot 26): names, difficulty, file path and size. `ApplyPreferences` (slot 25; the exe's table calls it SetupCity) takes names and difficulty from the new city dialog's values and renames the file path.
- **Growth of a city that is not open:** `ExtrapolateGrowth` (slot 19) adds to the extrapolated population of a type, and `GetAllowableExtrapolation` (slot 18) answers how much room is left under the limit: 1.1 times the saved population, at least 1,000 (`SetAllowableGrowth`, from the message handler on 0x26C63343, which also sets the extrapolated numbers back to the saved ones).
- **Neighbour connections:** when the open city starts running (message 0xCA63D517) the regional city collects what its neighbours made or removed at the shared edge (`CollectNeighbourConnections`, `CollectOwnConnections`), sorts the changes and carries them into the city (`ApplyConnectionChanges` 0x004AA6D0): the road, rail or pipe stub is drawn or cleared at the edge and the neighbors simulator's connection made or deleted.
- **Deals:** the two lists, kept in step with the open city (`SetLocalDeals`, `UpdateLocalDeals`), and the commodity totals worked out from them (`UpdateImportExport`, slot 71).

The gzcom-dll header is off for this class in places: budget, income and the commodity figures are doubles, slot 9 is the population by type, slot 16 the tax rate, and slots 25 and 26 are as described here.

### Replacements

| Group | Functions |
|---|---|
| `region.city.part1` | QueryInterface, Release, Init, Shutdown, `Write`, tax rate, budget, income, commodity balance, population lookups, FindConnection, SetupPreferences, the ordering of connection changes (17; [region_city_1.cpp](../src/core/region_city_1.cpp)) |
| `region.city.part2` | `SetupCity`, SetPosition, connections and deals lists, the collectors of connection changes, extrapolation, the commodity getters and setters, points of interest, the class's own map and sort instances (34; [region_city_2.cpp](../src/core/region_city_2.cpp)) |
| `region.city.part3` | `ApplyConnectionChanges`, UpdateImportExport, `ApplyPreferences`, the destructor (5; [region_city_3.cpp](../src/core/region_city_3.cpp)) |
| `region.city.part4` | the constructor, the message handler, the plain getters and setters, the thunks of the two other interfaces (35; [region_city_4.cpp](../src/core/region_city_4.cpp)) |
| `region.city.part5` | `Read`, `UpdateCityCache`, the factory (3; [region_city_5.cpp](../src/core/region_city_5.cpp)) |

Left the game's: the sort helpers the changes are sorted with (eight instances of the STL sort; the ordering they call is ours), map and list helpers, the stream helper 0x004A7560, the folded getters.

### Odd things in the original, kept

- `Read` puts both saved runs of deals on the first list (+0x18C); `Write` saves the two lists apart.
- `Write` counts a connection that fails its interface query but does not write it.
- `UpdateCityCache` releases only the ports that answer yes, and does not empty the connection list before filling it.
- `Release` never lowers the count below 1 before destroying the object.
- The last three bytes of a pending change, and four padding bytes of a new commodity entry, are stack leftovers in the original and zero in ours. Nothing reads them.

### How it is tested

Every regional city is read from its file when the region opens, and the open city's handles its messages in every run. With `OPENSC4_TEST_REGION_DRIVE=1` the fingerprint also has, for every city of the second region object, a checksum of its save record as its own `Write` gives it (into a stream that only sums) and of what its getters answer (the three interfaces, population and its extrapolation by type, tax rates, budget, income, commodity balances, names, hemisphere, number of connections); and for the open city's regional city the same before and after another `UpdateCityCache`, and for a fresh object from the factory.

Not reached: connection changes from a neighbour and deals (the tutorial region has neither), `SetupCity`, `ApplyPreferences`, SetPosition with a move, the setters.

Parity (2026-10-10, the region and regional city groups all on, both switches, twelve saves for two days): 12 of 12 same on the first pair, and the region and city checksums of ours equal the original's in every run.

## City files and pictures

Free functions (0x005DBC10 to 0x005DEA50) that the application, the region classes and the region screen share.

**City files as the region sees them.** A city file is a database segment; `OpenSegment` (0x005DBFE0) opens one and refuses versions outside 0x3E to 0x41 (the version is the dword of record 0xEA3FF4F5 / 0xEA3FF506 / 0xEA3FF4F5; a file without it counts as 0x41). The regional city is record 0xCA027EDB / 0xCA027EE1 / 0 (`LoadFromFile` 0x005DC220, `SaveToFile` 0x005DC2E0). A city's file is `City - <name>.sc4` in the region's folder, with ` (2)`, ` (3)` and so on added while a file of that name is there, unless that file is the same city by serial number (`MakeFilePath` 0x005DC400, `MakeSavePath` 0x005DC790). A new serial number is the seconds since 31 January 2000 shifted left 4, with four random bits (`NewCitySerial` 0x005DBD40). Also here: text records (type 0x2026960B), a resource copied out as a file (`config.bmp` for a new region) and a record read into a byte vector.

**The pictures.** A city file carries up to four pictures for the region map as PNG files in records 0x8A2482B9 / 0x4A2482BB / 0, 2, 4, 6 (a picture and its mask, twice), each followed by a text record with its file name. `ReadPictures` (0x005DD480) reads them through a temporary file; `LoadSavePictures` (0x005DDA40) does so for a path. When the application saves a city, `MakePictures` (0x005DDEC0) sets the view up, takes sixteen screen shots (four view shifts, plain and mask, two rounds), averages each four (`AveragePictures`), finds the drawn part by the mask (`FindPictureBounds`), crops (`CropPicture`) and writes the four (`WritePictures`). If the game is not in front it waits and tries again, and in the end writes stock pictures by city size (`WriteStockPictures` 0x005DDDC0). A small message target (0xC bytes, vtable 0x00AA2B5C) watches for the application losing the focus meanwhile.

### Replacements

| Group | Functions |
|---|---|
| `region.files.part1` | segments (open, close, version), the regional city record, LoadFromFile, SaveToFile, file names and paths, serial numbers, text records, resource to file, record to vector, picture file to image, the focus watcher (19; [region_files_1.cpp](../src/core/region_files_1.cpp)) |
| `region.files.part2` | reading, writing and making the pictures: StoreFileAsPicture, FindPictureBounds, CropPicture, AveragePictures, ReadPictures, LoadSavePictures, WritePictures, WriteStockPictures, MakePictures (9; [region_files_2.cpp](../src/core/region_files_2.cpp)) |

Left the game's: `DrawPictureMask` (0x005DCCF0), which draws the mask (render states, four vertices per terrain cell) and belongs with the renderer; two generic rectangle functions; the watcher's QueryInterface, folded with other classes. `FindPictureBounds` and `CropPicture` take their picture in EBX (and the rectangle in ESI): ours sit behind small assembly entries.

### Odd things in the original, kept

- `ReadPictures` needs only the first picture: a later one that is missing or will not load leaves the result true.
- `WritePictures` still writes the name record of a picture whose PNG could not be saved.
- `MakePictures` skips the cropping when the drawn part starts at the top row.
- `LoadFromFile` and `SaveToFile` leak the segment when the version is right but the file is not open afterwards.

### How it is tested

Region start-up reads every city file through these functions. `OPENSC4_TEST_REGION_DRIVE=1` adds `files` (on the first tile's city file: segment opened, version, name text, the regional city record's bytes, closed; the file read as a regional city; the file names for its name with its serial number, none and another; a new serial number without its random bits) and `pics` (the pictures of that file read, four and two at a time, every pixel; the average of the first with itself and with the second).

The save path needs a full save (`OPENSC4_TEST_QUICK=0`; quick saves leave the pictures out). Compared by the saved files' picture records: three of the four are byte-identical between the original and ours; the third picture (record 4) differs between two runs of the original as well.

Not reached, or not known to be: SaveToFile on its own, SetVersion, the wait-and-retry of `MakePictures` and the stock pictures (the saved pictures were real ones).

Parity (2026-10-10, the region, regional city and file groups all on, both switches, twelve saves for two days): 11 of 12 same, and the `files` and `pics` checksums of ours equal the original's in every run. Big City day 0 gave "partly" in the batch, with both of its outcomes ones the original gave in the earlier batches (its first-day traffic varies under load); run on its own it was same on the first pair.
