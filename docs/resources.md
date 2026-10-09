# Resources and DBPF (P3)

How the game finds its data: which package files it opens, in what order, and which file wins when two have the same resource. Survey of 2026-10-06 on the clean Steam install (build 641), measured in the running game with `diagnostics.dbpf_survey` and `diagnostics.trace.setup_resources` ([src/core/dbpf_survey.cpp](../src/core/dbpf_survey.cpp)).

## How it fits together

Every resource has a key: type, group, instance (three 32-bit numbers). The resource manager (`cIGZPersistResourceManager`, service 0x056B906E) keeps an ordered list of **DB segments** and asks them in turn. A segment is either one package file (`cGZDBSegmentPackedFile`, class 0x086D4A89; the DBPF format) or a folder (`cGZPersistDBSegmentMultiPackedFiles`, class 0x090F8619), which holds one packed-file segment per package found under it. Resource factories (`RegisterObjectFactory`) turn records into objects by type; mods hook in there (sc4-resource-loading-hooks wraps the exemplar factory).

The segments are built once at startup by `cSC4App::SetupResources` (our name, 0x004572B0, called from 0x0044C97E), which creates both kinds of segment. Its decompile is hard to follow (stack arguments Ghidra does not settle), so the order below comes from the running game, not from reading it.

## The order on a clean install

`GetSegmentByIndex` lists 10 segments (11 with a Plugins folder that has packages in it):

| Index | Segment | Records |
| --- | --- | --- |
| (0) | user Plugins folder, when it has packages | |
| 0 | folder `Sku_Data\`: `Sku1_US\Plugin_002_TheLivingMall.dat`, `Sku2_European\Plugin_002_TheLivingMall.dat` | 208 each, the same keys |
| 1 | `Sound.dat` | 2,775 |
| 2-6 | `SimCity_5.dat` down to `SimCity_1.dat` | 5,618 / 9,796 / 7,494 / 15,237 / 60,439 |
| 7 | `SimCityLocale.DAT` | 5,962 |
| 8 | `Intro.dat` | 3 |
| 9 | `EP1.dat` | 4,098 |

111,838 records, 111,629 distinct keys: the game's own files barely overlap (the mall pack twice, and one locale key that the mall pack also has). The install folder's `Plugins` holds only PNGs in a clean install, so it has no segment; the relative order of the install and user Plugins folders when both have packages is not measured yet.

**Which file wins** (asked of the resource manager with `FindDBSegment` for duplicated keys, not inferred from the listing):

- **Between segments, the first listed wins.** The user Plugins folder comes first, so plugins override the game's files; `Sku_Data` overrides `SimCityLocale.DAT`.
- **Inside a folder, the last listed file wins.** `Sku2_European` beats `Sku1_US`.
- **Files in a folder are listed top-level files first, alphabetically, then subfolders, alphabetically** (probes `aa_probe.dat`, `mm_probe.dat`, `aa_sub\cc_probe.dat`, `zz_sub\bb_probe.dat` were listed in that order, and `zz_sub\bb_probe.dat` served the shared key). So any file in a subfolder overrides any top-level file, and later names win, which is why override packs are named `zzz_...`.
- **Only `.dat` files are in a Plugins folder's segment; every other package file is a segment of its own, registered just before the folder** (`SetupResources` 0x00457A07-0x00457B50 for the install Plugins folder, 0x00457C86-0x00457DCF for the user's: every file under the folder, `*` and subfolders, whose extension is not `.dat` without case, and that starts with the package signature or holds the embedded-package marker (0x009728D5), becomes a packed-file segment of its own; the folder segment's own filter is `*.dat`, set in its constructor 0x0097EB25). The resource manager asks the last registered segment first, so **any `.dat` wins over any `.SC4Lot`, `.SC4Desc`, `.SC4Model` or extensionless plugin, wherever it is**, and among those the last listed wins, in the same folder order. Checked on 2026-10-09 with probe packages (`a.dat`, `B.dat`, `_c.dat`, `sub\x.dat`, `Sub2\deeper\z.DAT`, `aa.SC4Desc`, `Mid.SC4Lot`, `zz.SC4Lot`, `noext`, `q.dat.bak`, `sub\y.SC4Lot`, a package behind 100 junk bytes and the marker, a text file): the folder segment held the five `.dat` files in the order `a`, `B`, `_c`, `sub\x`, `Sub2\deeper\z` (names compared by their uppercase form: `_` after letters), the other packages were seven segments of their own in folder order, the text file was skipped, and every duplicated key was served as these rules say.

**Our loader.** [src/dbpf/plugins.h](../src/dbpf/plugins.h) applies these rules to files on disk. `LoadOrder` lists a Plugins folder in the game's order (its `.dat` files, then its other files, as two segments), indexes every key once, and reads any key from the file that serves it (keeping at most 64 files open, so folders of thousands of plugins work). It is also the data behind the in-game mod manager: plugins can be turned off (they stay listed but serve nothing, kept in a plain list of paths, one per line, where `NAM/` means a whole folder), and each file gets a summary of how many keys it provides, overrides and has hidden, and by which files. `opensc4-dbpf plugins <folder> [--keys] [--disabled <list>] [--game <package>...]` prints all of it, including whether the copies of a contested key are identical (a shared dependency shipped twice) or differ (a real override). Assumed, not measured: names outside ASCII sort like NTFS (only ASCII is folded to uppercase). In the game, plugins turned off stay out with `resources.plugin_loader` (next section).

## Our loader in the game

`resources.plugin_loader = ours` ([src/core/plugin_loader.cpp](../src/core/plugin_loader.cpp), off by default) replaces `cSC4App::SetupResources` with our own, which registers the same segments in the same order, call for call: the debug hook (only with the app's debug functionality on), the `.dat` files of the data folders one by one (`GetDataDirectory` -1 and 0), the SKU folder's folder segment, then for the install Plugins folder and the user Plugins folder first their other package files one by one and then their folder segment, and last the 15 resource types the manager is told about. Folders are listed by `dbpf::ListFolder`, and the folder segment is our class (`FolderSegment`), which does what the game's `cGZPersistDBSegmentMultiPackedFiles` does (one game packed-file segment per `.dat` file, a key map in which the last file wins, record calls passed to the file that serves the key). Records are still read by the game's packed-file segments, so what the game reads does not change.

Plugins named in a list file stay out: the user Plugins folder's files that the list names are neither in its folder segment nor registered on their own, as if they were not there. The list is `opensc4.ini`'s `[plugins] disabled`, a path relative to core.dll's folder or absolute (default `disabled-plugins.txt` next to core.dll; `OPENSC4_PLUGINS_DISABLED` overrides it), in `ReadPathList`'s format: one path per line relative to the user Plugins folder, `/` or `\`, `NAM/` for a whole folder, `#` comments. The log says how many entries the list has and how many files they kept out.

Checked on 2026-10-09 (Making Money, probe plugins of the section above, `diagnostics.dbpf_survey` on both sides): with nothing turned off, the survey of the original and of ours is identical line for line (18 segments, every path, every file in folder order, the record counts, and the serving file of all 223 duplicated keys); with `B.dat` and `sub/` turned off, those three files are nowhere in the game and their keys are served by the next file in line. Building the segments took 66 ms with ours against 103 ms with the game's in that folder (debug build; the scan of non-package files for the marker is most of it). Parity on the 12-save corpus (7 days, the usual run-parity.sh settings, two jobs): 12 same, 11 on the first pair; Big City d0000 on the second, where the original itself had two outcomes.

Not covered: the install folder's Plugins folder ignores the list (relative paths name the user's folder); `SetPathFilter` and copying records into a new package file (`ConsolidateDatabaseRecords` with a path) are not implemented, since only our setup makes these segments; `diagnostics.trace.setup_resources` hooks the same function and cannot run with it.

## Time

Building all segments takes **33-38 ms** on the clean install (`trace: cSC4App::SetupResources`), out of about 3.5 s from core.dll attaching to the end of app init; loading a city takes about 0.7 s. Listing every key of every segment (the survey itself) takes about 200 ms.

So **on the vanilla game there is no loading time for P3 to win.** The slow loading players report comes from large plugin folders: sc4-dbpf-loading ([docs/patch-census.md](patch-census.md) 3.12) reports up to 90% shorter startup with many plugins, from skipping the signature scan of files that are not DBPF, filtering by file extension, long paths and large-read buffering, by replacing the folder segment class and detouring file I/O.

## Package format

OpenSC4's reader is [src/dbpf](../src/dbpf) (portable C++20, no game or Windows code), with a command-line tool, `opensc4-dbpf list <package>` and `opensc4-dbpf check <package...>`, and unit tests on hand-built packages ([tests/dbpf_test.cpp](../tests/dbpf_test.cpp)). It reads what SimCity 4 writes: DBPF 1.0 with index version 7.0. Everything little-endian.

- **Header**, 96 bytes: `DBPF`, major version (1), minor (0), user version major and minor, a reserved word, created and modified dates, index major version (7), index entry count, index offset, index size, hole count, hole offset, hole size, then reserved space.
- **Index**: one 20-byte entry per record: type, group, instance, offset, stored size. Holes are free space left by rewritten records (saved cities have dozens); readers ignore them.
- **Directory record** (type, group and instance 0xE86B1EEF, 0xE86B1EEF, 0x286B1F03): 16 bytes per compressed record, its key and uncompressed size. A record is compressed exactly when the directory lists it. The game does not list the directory itself as a resource.
- **Compressed records**: a 4-byte compressed size, the QFS header (flags 0x10, 0xFB, 3-byte big-endian uncompressed size), then a RefPack stream: commands of 1 to 4 bytes, each copying 0 to 3 (or 4 to 112) literal bytes and then up to 1,028 bytes from earlier output (offsets up to 128 KB, overlapping copies allowed), ending with a 0xFC-0xFF command.

Checked against the game on 2026-10-06:

- `opensc4-dbpf check` reads all 11 package files of the clean install (111,848 records, 88,843 of them compressed, 1.79 GB decompressed) in about 8 s, every compressed record decoding to exactly its directory size; and all 91 saved cities of the test region (10,829 records).
- `diagnostics.dbpf_compare` reads every record of every package the game has open both through the game's own segment (`GetRecordSize`/`ReadRecord`) and through the reader: all 111,838 records, 1.79 GB, are byte-identical, with the same key set per file (10 s, in the running game).

Not checked yet: which entry the game uses when one package lists a key twice (the reader returns the last), and packages written by modding tools, which may differ in details SimCity 4's own writer never produces.

## What P3 can offer, and to whom

1. **Our own DBPF reader** (format, index, compression), as a library with tests. **Done** ("Package format" above). Needed beyond P3: saved cities (`.sc4`) are DBPF files, so P4 (save format) builds on it, and so would any tool that inspects plugins. Pure file-format work against the game's own files; days.
2. **Load-order and override report** from the running game, extending `diagnostics.dbpf_survey`: for every key served from a plugin, which file wins and what it hides. Useful to modders and for our own debugging once mods are allowed (vanilla first, so later).
3. **Our own segment classes** in place of 0x086D4A89 and 0x090F8619 (both GZCOM classes, so replaceable without patching): the loading speedups for big plugin folders, absorbing sc4-dbpf-loading. Only pays off with mods. The folder segment and the setup are ours now ("Our loader in the game"); the packed-file segment is still the game's.
4. **Key rebinding** (P2 input): shortcut mods change the game's keyboard shortcuts by overriding a resource in its data files (community practice; not checked here), so an override generated from our own settings is the likely route, after 1.

Recommendation: 1 now (it is the foundation for P4 and costs little), 2 and 3 when mod support starts.
