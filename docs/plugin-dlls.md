# Plugin DLLs

How the game finds and loads GZCOM plugin DLLs (build 641), and our translation of that code. Read from the disassembly on 2026-10-11 and checked in the running game with probe DLLs.

## What the game does

**When.** In start-up state 1 the application's PreFrameWorkInit (0x00452360) builds the exe's own directors and services and then scans for DLLs (0x00451260, called at 0x00452CFC). The package files are set up later (`SetupResources`, [resources.md](resources.md)), so every plugin DLL has been loaded and started before the first package is opened; that is what lets a DLL replace a segment class in time. The scan can run again while the game runs: the application's message handler (0x00451C30) calls it with a folder when message 0x87163754 arrives from one of two of its members (+0x5D4, +0x600; probably watchers on the two Plugins folders, not confirmed), and sends message 0xA42396C6 when that added a library.

**Where.** The install folder's `Plugins` first (`cISC4App::GetPluginDirectory`), then the user folder's `Plugins` (`GetUserPluginDirectory`). Only `*.dll` files directly in the folder: no subfolders. The `Apps` folder is not scanned.

**Which files.** For each file, in the order the folder listing gives them:

- a file whose full path is already in the COM object's library list (same bytes, case sensitive) is skipped;
- if `<name>.ini` lies next to the DLL and its section `Admin` has a key `Enabled` with the value `false` (any case) or the single character `0`, the DLL is skipped; any other value, no key, no section or no file means it is loaded;
- everything else goes to the COM object's AddLibrary.

**Loading one DLL** (`cGZCOM::AddLibrary` 0x0087C081). A record for the path is put in the library tree first (ordered by path); a path that is already there answers true and nothing else happens, also when that library failed before. Then (`cGZCOMLibrary::Load` 0x0087BA7E): `LoadLibraryW` with the path converted to UTF-16 (`LoadLibraryA` on Windows 9x), `GetProcAddress` for `GZDllGetGZCOMDirector`, the export is called, and the director gets `InitializeCOM(COM object, path)`. Then the director's `EnumClassObjects` enters its classes in the class map, and last it gets `OnStart(COM object)`, whose answer is ignored.

**When it fails.** No module, no export, a NULL director or `InitializeCOM` answering false: the DLL is unloaded again at once (`cGZCOMLibrary::Free` 0x0087B3B3) and AddLibrary answers false. The record stays in the list, marked not loaded. The error texts the code builds ("Failed to load the requested dll ...") are thrown away: no message box, nothing logged.

**Two DLLs, one class.** The class map (class ID to version and library) holds the classes of DLLs only. When a class ID is registered again, an entry with a lower version is replaced; with an equal or higher version the first one stays. So the higher version wins whatever the load order, and of equal versions the first loaded. `GetClassObject` (0x0087BE75) asks the class map first and the exe's own directors after it, so a DLL's class always beats an exe class of the same ID. Director IDs are never compared: two DLLs with the same director ID both load.

**Unloading.** `FreeUnusedLibraries` (0x0087B626) frees every loaded library whose director's `CanUnloadNow` says yes; the COM object's shutdown (0x0087B67B) frees them all, in path order. Free clears the record's director pointer without calling into the DLL, calls `FreeLibrary` (the call at 0x0087B3D1) and marks the record not loaded. A later `GetClassObject` for a class of a freed library loads it again.

## Ours

`resources.plugin_dlls.libraries` ([src/core/plugin_dlls_1.cpp](../src/core/plugin_dlls_1.cpp), 28 functions: the library record, its Load and Free, the four guarded Windows calls, the library tree and class map inserts, the class registration callback, AddLibrary, GetClassObject, both EnumerateLibraries, FreeUnusedLibraries and the shutdown loop) and `resources.plugin_dlls.scan` ([src/core/plugin_dlls_2.cpp](../src/core/plugin_dlls_2.cpp): the folder scan), both off by default. They are translations in place: entry detours on the game's own COM object, same layout, same calls in the same order. Because the scan runs before any plugin's director exists, the detours go in when core.dll attaches (phase Attach).

For mods: sc4fix overwrites the `FreeLibrary` call at 0x0087B3D1 with NOPs so that plugin DLLs stay loaded at exit. Our Free reads that instruction every time: a call is made to whatever it targets, anything else means no call. `fixes.pin_plugin_dlls` ([crashes.md](crashes.md)) works unchanged with ours on.

Left the game's: the COM object's constructor, destructor, RealInit, RealShutdown and GUID makers, the record's IsLoaded (4 bytes, too short for a detour), and the container and string helpers shared with other code.

## Checking it

`tests/probe_plugin.cpp` builds ten small plugin DLLs that write every call the game makes to them into a file; `scripts/make-dll-probes.sh <build> <Plugins folder>` arranges them in a test folder, and `diagnostics.gzcom_libraries` logs the COM object's library list and, with `OPENSC4_TEST_GZCOM_CLASSES`, asks it for objects of given classes. The arrangement: two DLLs registering the same two classes (one class with versions 1 and 2, one with equal versions), DLLs with an `.ini` saying `Enabled=false`, `0`, `FALSE` and `true`, one with the key in another section, one whose `InitializeCOM` fails, one whose export answers NULL, one without the export, one with another's director ID, and one in a subfolder.

What the original does with that folder (2026-10-11), which is also what the section above says: 8 libraries in the list, in path order (`OpenSC4.dll`, then the probes); the three DLLs disabled by their `.ini` (`false`, `0`, `FALSE`) and the one in the subfolder never touched; the three failing DLLs loaded, unloaded again at once and left in the list as not loaded; the class with versions 1 and 2 served by the version 2 DLL, the class with equal versions by the first loaded; both DLLs with the one director ID loaded.

Ours against the original, same folder, compared as text: the library list with its loaded marks, the answers for eight class IDs, and the 37-line record of every call into the probe DLLs (load, export, InitializeCOM, EnumClassObjects, OnStart, GetClassObject, unload) are identical with the libraries part alone, with both parts, with both parts and `fixes.pin_plugin_dlls` (against the original with the fix), and with both parts together with `resources.packed_file.*` and `resources.plugin_loader`. The city loads and runs in each. Parity on the 12-save corpus (7 days, the usual `run-parity.sh` settings, two jobs, `resources.plugin_dlls.*` on one side): 12 same, 11 on the first pair; Big City d0365 on the third, where the original had three outcomes.

Not covered: DLLs in the install folder's `Plugins` (the test folders are user folders; the install folder is shared by every test run on the machine), the rescan on message 0x87163754, `FreeUnusedLibraries` with a director that allows unloading, the reload of a freed library through `GetClassObject`, the NOP at 0x0087B3D1 (read and skipped by our Free, but no test patched it), a DLL class with the ID of an exe class, the exception paths of the four guarded Windows calls, and the Windows 9x branch. The address entries of the range carry real names for everything we translate or the docs identify (36 of 60); the 24 left as `cGZCOM::F<address>` are helpers nothing of ours calls (the this-adjusting thunks 0x0087B8BC to 0x0087B8E4, AddRef/Release of the COM object, tree destroy and buffer helpers) and need a disassembly pass.
