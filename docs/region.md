# The region

The region screen is the last of the P8 world systems to be started ([ROADMAP.md](../ROADMAP.md)). This page maps what the game's region code contains and describes the parts that are ours. Build 641 addresses throughout. Rewritten the same way as the simulators: function by function on the original objects under the parity harness ([parity.md](parity.md)).

## What the region code contains

About 93 KB of game code in five parts (survey of 2026-10-10: vtables, the exe's class name table, callers).

| Part | Code | Size | State |
|---|---|---|---|
| Region data, `cSC4Region`: the open region's folder, `region.ini`, the `config.bmp` layout, the cities found in the folder | 0x004A44E0 to 0x004A74E0 | 12 KB, 41 functions | ours (`region.data.*`), below |
| Regional city, `cSC4RegionalCity` (class 0x6A037C24, main vtable 0x00A8FB20): what the region knows of one city without loading it (name, position, size, population, neighbour connections and deals) | 0x004A74E0 to 0x004ADC00 | 25 KB, 115 functions | ours (`region.city.*`), below |
| City files and pictures: opening a city file as a regional city (0x005DC220), file names (0x005DC400, 0x005DC790), serial numbers (0x005DBD40), the pictures a city file carries for the region map (resource type 0x8A2482B9) | 0x005DBC10 to 0x005DEA50 | 10 KB, 32 functions | ours (`region.files.*`) but the one drawing function, below |
| Region screen window, `cSC4WinRegionScreen` (window ID 0xEA659793, vtable 0x00AB9260), with its scroll window, flat terrain map, tile records and sort, picture helpers: opening and closing a region, the controls and commands, the terrain made from a region picture | 0x007AA2A0 to 0x007B2440 | 31 KB, 121 functions | ours (`region.screen.*`) but the overlay's draw, below |
| Region map window, `cSC4WinRegionView` (window ID 0x2BA6BB97, vtable 0x00AB9658), with its picture grid and label procedure; and the region screen's overlay window (drifting marks, vtable 0x00AB88C0) | 0x007B2440 to 0x007B62D0, 0x007A9880 to 0x007AA2A0 | 18 KB, about 70 functions | ours (`region.view.*`) but two functions that draw, below |

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

## The region screen

The application's window object (global 0x00B43CBC) keeps one screen at +0xEC: the start screen, the region screen or the city screen. `SC4AppWindow::ShowRegionScreen` (0x004E0D40) makes the region screen, a window of 0x200 bytes (`cSC4WinRegionScreen`, ID 0xEA659793; main vtable 0x00AB9260 with 151 slots, its own interface 0xC6AE7085 at +0xD8, a message target at +0xDC). The first survey had its vtables 16 slots off; the table at the top has the right addresses.

What is in the range:

- **The screen** itself. `Init` (slot 4, 0x007B1900) posts 0xABB5BB44, loads the key table, sets up the 3D view (`SetUpView`: tick manager, a flat terrain map, camera and camera control, renderer, effects manager), makes the back window, the map window (`cSC4WinRegionView`, at +0xE0), the scroll window and the overlay window, loads twelve stock pictures, builds the tile records (`BuildCityRecords`), reads its tuning (exemplar 0x6534284A / 0x690F693F / 0xAA383BFE), registers fourteen commands, sets up the side panels and their buttons (`SetUpControls`), starts the effect 'region_global' and posts 0xCBB5BB45. `Shutdown` (slot 5) undoes it between 0x8BB5BB46 and 0x8BB5BB4B.
- **Tile records** (0x80 bytes, vector at screen +0x118), one per layout tile: cells, place in the map picture (x and y in km times the four floats at 0x00B0DBA4), size, the city's pictures from its file or stock ones, shifted by the fraction of a pixel of their place (`ShiftPicture`, bilinear with 14-bit weights), the mask merged into the alpha channel, the mask's runs per row for three channels (`CollectRuns`) and its centre. The tiles are sorted by `(1 << size) + x + y`, then x, with the STL sort.
- **The controls** (`OnControl`, slot 143): the three side panels, the names and borders check boxes, the three map modes, open the picked city, import a city or move one to the picked tile, delete the picked city, the region dialogs (open, new, delete), quit. **The commands** (`DoCommand`): LoadRegion and LoadCity by name, RegionBitmapLoad, the eight scroll commands, Cancel, QuitGame, SetExpandedToolTips.
- **`MakeTerrainFromPicture`** (0x007AEDD0, the command RegionBitmapLoad): asks for a picture file, checks it is the region's size in km times 64 plus 1 each way, deletes every city of the region and makes a new city per tile with that part of the picture as its terrain.
- **The scroll window** (0x138 bytes, vtable 0x00AB8CD0, at screen +0x174): right-button drag scrolling with a marker where the drag began, key and edge scrolling, with speeds that decay. **The flat terrain map** (0x28 bytes, vtable 0x00AB8C00, the interface of the city's terrain map): the 3D view's ground under the region, altitude 270 everywhere. **The back window** (0xDC bytes, vtable 0x00AB8F50) paints the 3D view.
- `Paint` (slot 88) glides the view to its target by elapsed time, clamps it and tells the map window and the overlay where the view is.

### Replacements

| Group | Functions |
|---|---|
| `region.screen.part1` | the flat terrain map, the scroll window (construct, start, drag, tick), the back window, the tile ordering and sort pieces, picture helpers (blend rows, scaled table, opaque centre), the screen's QueryInterface (43; [region_screen_1.cpp](../src/core/region_screen_1.cpp)) |
| `region.screen.part2` | mouse slots, window messages, LayOutMap, stock pictures, MergeMask, SelectPanel, LoadTuning, ApplyMode, CenterCamera, saving the region choice and view position, ShutDownView, the scroll window's marker and destructor (20; [region_screen_2.cpp](../src/core/region_screen_2.cpp)) |
| `region.screen.part3` | Paint, left click, SetUpView, CollectRuns, GoToRegionNamed, OpenCity, the tile record's destructor, sort pieces (13; [region_screen_3.cpp](../src/core/region_screen_3.cpp)) |
| `region.screen.part4` | the tile record's copies, ShiftPixels, ShiftPicture, PrepareTilePictures, CityAtTile, ShowPopulation, the sort's entry and loop (14; [region_screen_4.cpp](../src/core/region_screen_4.cpp)) |
| `region.screen.part5` | MakeTerrainFromPicture, OpenCityByNameOrSize, DoCommand (3; [region_screen_5.cpp](../src/core/region_screen_5.cpp)) |
| `region.screen.part6` | OnControl, SetUpControls (2; [region_screen_6.cpp](../src/core/region_screen_6.cpp)) |
| `region.screen.part7` | the constructor, destructor, Shutdown, the message target, BuildCityRecords, the record vector (9; [region_screen_7.cpp](../src/core/region_screen_7.cpp)) |
| `region.screen.part8` | Init, the deleting destructor, the button target callback, three small functions of the map window (6; [region_screen_8.cpp](../src/core/region_screen_8.cpp)) |

Left the game's: the terrain overlay's draw (0x007AB630: a render state, four vertices, a draw call; renderer code), three 4-byte getters too short to hook, folded thunks and getters, generic list and vector helpers. Functions that take an argument in a register (`OpaqueCentre` in EAX, `CollectRuns` in ESI) sit behind small assembly entries.

### Odd things in the original, kept

- Several bool arguments are pushed as a whole register with only the low byte set (the upper bytes are a pointer's or leftovers); the callees read a byte. Where the upper bytes are deterministic ours passes the same.
- `BuildCityRecords` appends and never clears: a second call would double the records.
- `ShiftPicture` returns true on most failures, and `PrepareTilePictures` ignores its result.
- `CollectRuns` locks its picture and never unlocks it.
- `Paint` divides by the distance to the glide target with no zero check.
- `SetUpControls` uses a panel after releasing it and calls into it even when it did not load.
- `Init` returns 1 whatever failed.

### How it is tested

Unattended runs load their city from the start screen, so the region screen never comes up on its own. `OPENSC4_TEST_REGION_SCREEN=1` puts it up first as the start screen does when the player goes on (the kept screen removed, `ShowRegionScreen`), through a new hook of the test harness that runs with the region open, before the city is loaded. The fingerprint's `scr` part has the screen's plain members, which of its parts exist, every tile record (cells, place, size, both pictures pixel by pixel after their shift, the runs and centre of the mask), the scroll window's tuning and what the flat terrain map answers at chosen points. With `OPENSC4_TEST_REGION_DRIVE=1` the screen is also worked: the map laid out and the mode applied again, the eight scroll commands through `DoCommand`, the three map modes through `OnControl` and back, a drag of the scroll window at four pointer places and its end, a region and a city asked for by names nothing has; and described again (the same as before the drive). The city is then loaded with the region screen up, which shuts the screen down.

Not reached: everything that opens a dialog (region and city dialogs, import, delete, the picture import with `MakeTerrainFromPicture`), a real click on a tile and the open-city control, key messages, the glide of `Paint` with a target, scrolling by the pointer at the screen's edge.

Parity (2026-10-10, the region, regional city, file and screen groups all on, the three switches, twelve saves for two days): the `scr` checksums of ours equal the original's in every run of every save. Whole-city verdicts: 10 of 12 same. Making Money day 90 differed in two runs of ours; run on its own the original gave two outcomes and ours one of them. Big City day 0 differed only in the second run of the first pair, with a lot checksum no earlier run had given; a control with the original on both sides gave the same value at the same place in the run order, so it comes from the order of the runs.

## The region map window

`cSC4WinRegionView` (0x128 bytes, window ID 0x2BA6BB97, made by the region screen's Init and kept at screen +0xE0; main vtable 0x00AB9658, a painter interface 0x00AB9644 at +0xD8) is the map the cities are picked on. What follows it from 0x007B62D0 is a family of menu and option dialogs the city screen uses as well, not region code.

- **The map is a grid of cached pictures**, not one large picture: the picture grid (0x44 bytes, interface 0xC989F960, at window +0x10C) keeps cells of 256 by 256 pixels, enough for the window plus one, each with a "stale" byte. The 3D view calls the grid in its draw pass (`SC4RegionViewGrid::Draw` 0x007B28B0), which has stale cells composed again and copies them to the screen picture. Moving the view (`SetViewPlace`, from the screen's `Paint`) shifts the cells with wrap-around and marks the ones that wrapped stale.
- **Composing a cell** (`PaintArea` 0x007B4150, the painter's slot 3): the background picture tiled, then every tile record whose picture meets the cell. A record's map picture (+0x2C) is made on its first use: the city picture copied and tinted along the border runs (`CopyAndTint`), then outlined, each not fully opaque pixel laid over its neighbours below and to the right, with the runs where a row changes collected (`OutlineAndCollect`). The map picture is laid on by its runs (`BlendRuns`: whole runs copied where opaque, the rest blended pixel by pixel). Then city and mayor names through the game's text engine, the map-mode marks (airports and seaports), and the picked tile's outline (`TintRuns`).
- **Picking:** `TileAt(x, y)` goes through the records from last to first: the point must be inside the picture's area and inside a run of the mask (binary search). `PickAt` and `Unpick` keep the picked record at +0xE4 and renew the labels. A record's label is a small window loaded from a layout (`MakeLabel`; `FillPanel` fills its controls: names, difficulty marks, population and jobs, budget, or the price of an empty tile).
- **Markers:** `PlaceMarkers` gives a tile a marker picture and a running effect ('region_airport', 'region_seaport') per point of interest of its city.
- **The overlay window** (`SC4RegionOverlayWin`, 0x140 bytes, window ID 0x6A0AF41D, at screen +0xE4) is a layer of drifting textured marks over the map, most likely the clouds: a list of marks with place, speed, one of four textures and an alpha, new ones made with a chance of 0.003 a frame, filled at start by area (`SetBounds`).

### Replacements

| Group | Functions |
|---|---|
| `region.view.part1` | the grid's MarkStale and Paint, Invalidate, BlendRuns, TintRuns, TilePlace, TileArea, the mode and flag setters, the label procedure, cell range helpers (16; [region_view_1.cpp](../src/core/region_view_1.cpp)) |
| `region.view.part2` | the constructor, CopyAndTint, OutlineAndCollect, TileAt, the grid's MoveTo, ForgetRecords, the label procedure's object, cell vector helpers, thunks (18; [region_view_2.cpp](../src/core/region_view_2.cpp)) |
| `region.view.part3` | PaintArea, SetViewPlace, FillPanel (3; [region_view_3.cpp](../src/core/region_view_3.cpp)) |
| `region.view.part4` | Init, Shutdown, SetArea, the destructors, the grid's SetUp and Shutdown, PlaceMarkers, MakeLabel, TouchRecord, AddRecord, PickAt, Unpick, SetLabelLayout (15; [region_view_4.cpp](../src/core/region_view_4.cpp)) |
| `region.view.overlay` | the overlay window but its paint: construct, Init, Shutdown, SetBounds, MakeMark, the setters; the two picture filter helpers 0x007AA0E0 and 0x007AA110 (12; [region_view_overlay.cpp](../src/core/region_view_overlay.cpp)) |

Left the game's, for the renderer work: the grid's `Draw` (0x007B28B0: it sets the viewport and copies the grid to the screen picture inside the 3D view's draw pass) and the overlay's `Paint` (0x007A9D60: render states and vertices; it also steps the marks, so that step stays with it). Also left: two cell vector instances that copy a stack leftover into new cells, a generic service holder, a function of the next class. `BlendRuns`, `TintRuns` and `CopyAndTint` take arguments in registers and sit behind small assembly entries.

### Odd things in the original, kept

- `CopyAndTint` asks the source picture for its area twice and never the target, and never unlocks the source.
- `PaintArea` tests a tile's visibility in its first loop with the picture's own area, not its place on the map.
- `BlendRuns` decides by a run's first pixel whether the whole run is copied or blended.
- `Init` returns 0 when the grid cannot be set up but leaves the window started.
- The label procedure's turn is 6.28318, not two pi.

### How it is tested

With the region screen up (`OPENSC4_TEST_REGION_SCREEN=1`) and `OPENSC4_TEST_REGION_DRIVE=1`, the test's `map` checksum has: every record's place and area on the map; the tile found under a grid of points; the map composed through the grid into a picture of the test's own and read pixel by pixel, as it stands, with a tile picked (labels made and filled), and in each of the three modes; the view moved to three places and back, with the grid's state after each. The screen's start and the city load with the screen up run the window's Init, the records' hand-over, Shutdown and the destructors.

Not reached: markers and their effects (the tutorial region's cities have no airports or seaports), the label of the tutorial tile and its flashing, the overlay's marks beyond their set-up (its paint is the game's and random by frame).

Parity (2026-10-10, all 23 region groups on, the three switches, twelve saves for two days): the `scr` and `map` checksums of ours equal the original's in every run of every save. Whole-city verdicts: 11 of 12 same. Making Money day 365 gave two outcomes with ours and one with the original in this batch; the second outcome (one more job in one group, another lot checksum) is one the original itself gave on this save in the batch before.
