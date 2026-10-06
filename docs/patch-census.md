# Patch Census: Ecosystem DLLs vs. the 1.1.641 Executable

## 1. Purpose and method

OpenSC4 replaces game functions one at a time inside the original executable. Existing plugin DLLs patch fixed addresses in that same executable, often in the middle of a function. If OpenSC4 replaces a function that contains such a patch, the plugin's patch either stops working without any error or writes into code that no longer runs. This census lists every patch site we could find so that replacement order can account for them.

Method: a static reading of public source code, dated 2026-10-04. Nothing was built or run. All addresses are virtual addresses for the Windows 1.1.641 executable (image base 0x400000). "Entry" means the patch is at a function prologue. "Mid" means it is inside a function body. "Mid (call site)" means an existing CALL instruction inside some caller was retargeted. Classifications come from source comments, names and continue-address arithmetic, and no disassembly was done, so anything marked "(inferred)" is a judgement rather than a stated fact. Source citations name the repository and the file only.

Every patching DLL in this census gates its patches on build 641. Most detect the build the same way: they read the file-version resource, and if that fails they read a sentinel byte at 0x6E5000 (0x0F means 641). That sentinel read is not a patch.

## 2. Summary

Kinds: **C** = call rel32 rewrite, **J** = jmp E9 hook (usually into a naked stub that returns to fixed addresses), **B** = byte or immediate overwrite, **N** = NOP fill, **V** = vtable, jump-table or function-pointer slot, **D** = data-table write, **E** = entry-of-function inline detour (Detours or MinHook), **I** = IAT, **G** = GZCOM class or factory override only.

| DLL | License | Last commit | Fixed-address patch sites (641) | Kinds | Mid-function sites | Verifies original bytes | GZCOM overrides |
|---|---|---|---|---|---|---|---|
| NAMTeam/nam-dll | LGPL-3.0 | 2026-09-19 | 27 | J, B, D | 16 mid; 3 unknown; 8 data | only the 10 immediate/table writes | none |
| nsgomez/sc4fix | MIT | 2022-05-20 | 4 effective (5 coded) | N, J, E | 3 | no | none (not a GZCOM DLL) |
| memo33/submenus-dll | LGPL-3.0 | 2026-05-01 | 20 | J, B | 20 | no | none |
| caspervg/sc4-render-services (0xC0000054 copy is identical) | LGPL-2.1 | 2026-08-22 | 19 in the core DLL (+ samples) | C (+ E, D in samples) | 19 | yes (opcode E8 check) | none (adds 4 services) |
| Drexel-Macintosh/SimCity4_UI_Modernization | CC0 (own code) | 2026-09-25 | ~385 (~340 byte/data + ~45 hook/slot) | B, J, D, V, E | ~340 | yes, for nearly all | none |
| aspctt/scvk | LGPL-2.1+ | 2026-10-04 | 3 (opt-in) | B | 3 | yes | **0xC4554841** (OpenGL driver) |
| nsgomez/scgl | LGPL-2.1+ | 2026-02-16 | 0 | G | 0 | n/a | **0xC4554841** (OpenGL driver) |
| 0xC0000054/sc4-more-building-styles | LGPL-3.0+ | 2026-06-04 | 18 (5 conditional) | J, C | 18 | no | none (adds 2 classes) |
| 0xC0000054/sc4-data-view-extensions | MIT | 2024-12-25 | 7 | J, B | 7 | no | none |
| 0xC0000054/sc4-bulldoze-extensions | LGPL-3.0+ | 2025-09-16 | 8 | V, B, C | 4 | no | none |
| 0xC0000054/sc4-query-ui-hooks | LGPL-3.0+ | 2026-08-23 | 9 | C, J | 9 | no | none (adds 4 servers) |
| 0xC0000054/sc4-dbpf-loading | LGPL-2.1 | 2026-09-02 | 8 | B, C, J, E | 6 | no | **0x090F8619** (multi-packed-file segment) |
| 0xC0000054/sc4-lua-extensions | LGPL-3.0+ | 2026-06-04 | 3 | C, B | 3 | no | none (adds 1 server) |
| 0xC0000054/sc4-extended-terrain | LGPL-3.0+ | 2025-09-29 | 2 | B | 2 | no | none (redirects 2 IDs) |
| 0xC0000054/sc4-resource-loading-hooks | LGPL-3.0 (public headers MIT) | 2026-03-06 | 0 | G | 0 | n/a | **Exemplar resource factory** (type 0x6534284A) |
| 0xC0000054/sc4-exemplar-load-logging | MIT | 2024-03-15 | 0 | G | 0 | n/a | **Exemplar resource factory** (same class ID as above) |
| memo33/sc4-lua-sandbox-dll | LGPL-3.0 | 2026-03-14 | 31 | J, V | 1 | no | none |
| memo33/sc4-thumbnail-fix-dll | LGPL-3.0 | 2024-08-17 | 2 (+1 debug-only) | J | 2 | no | none |
| memo33/sc4-3d-camera-dll | LGPL-3.0 | 2024-04-01 | 21 (runtime, cheat-triggered) | B, D | 1 | no | none |
| memo33/transparent-texture-fix-dll | LGPL-3.0 | 2024-08-08 | 1 | J | 1 | no | none |
| caspervg/sc4-disable-fps-limits | LGPL-2.1+ | 2026-02-26 | 3 | B | 3 | no | none |
| 0xC0000054/sc4-disable-fps-limits (older MIT snapshot) | MIT | 2024-08-08 | 3 (same sites) | B | 3 | no | none |
| 0xC0000054/sc4-advanced-water-management | MIT | 2025-08-13 | 1 | V | 0 | no | none |
| 0xC0000054/sc4-graphics-options | LGPL-2.1 | 2026-05-10 | 1 (+ user32 detours) | B | 1 | no | none (selects driver via API) |
| 0xC0000054/sc4-full-screen-32-bit | MIT | 2024-05-08 | 1 | B | 1 | no | none |
| 0xC0000054/sc4-disable-network-construction-sounds | MIT | 2024-03-12 | 1 | B | 1 | no | none |
| 0xC0000054/sc4-disable-lot-plop-smoke | MIT | 2024-03-12 | 1 | B | 1 | no | none |
| 0xC0000054/sc4-disable-demolition-animation | MIT | 2024-03-12 | 1 | C | 1 | no | none |
| 0xC0000054/sc4-network-texture-test | MIT | 2024-12-11 | 2 | J, D | 1 | no | none |
| 0xC0000054/sc4-lot-network-scoring-test | LGPL-3.0+ | 2026-04-21 | 2 | J | 1-2 | no | none |
| 0xC0000054/sc4-cpu-options | MIT | 2025-04-26 | 1 (runtime-located) | I | 0 | no | none |

Totals: about 200 fixed-address sites outside the UI Modernization DLL, plus about 385 inside it. Very few DLLs can uninstall: only sc4-render-services (its core call-site rewrites), the Detours hooks on user32 in sc4-graphics-options, and one rollback block in UI Modernization.

**Repositories with no executable patches.** None of these write to the executable image. Some are GZCOM plugins that use only messages and interfaces; the rest do not build a plugin DLL at all.

- GZCOM-only plugin DLLs:
  - sc4-undo
  - sc4-remove-phantom-budget-items
  - sc4-region-census
  - sc4-growify
  - sc4-detokenize
  - sc4-clear-pollution
  - sc4-auto-run-cheats
  - SC4MessageViewer
  - sc4-single-cpu
  - sc4-cpu-priority
  - sc4-set-city-lat-long
  - sc4-regional-supply-demand
  - sc4-park-and-ride-ordinance
  - sc4-no-kick-out
  - sc4-neighbor-connection-logging
  - sc4-more-demand-info
  - sc4-lua-extension-example
  - sc4-legalize-gambling-ordinance-upgrade
  - sc4-humane-society-ordinance
  - sc4-gzcom-dll-template
  - sc4-discord-rich-presence
  - sc4-disable-cheats
  - sc4-custom-ordinance-host
  - sc4-custom-budget-departments
  - sc4-city-lottery-ordinance
  - sc4-city-date-sync
  - sc4-auto-update-city-thumbnails
  - sc4-auto-save
  - sc4-add-new-ordinances
- Skipped (not a plugin DLL):
  - sc4-gzids (ID header)
  - sc4-dll-utilities (static helper library)
  - sc4-lua-file-hash, sc4-assign-building-styles, sc4-create-building-style-exemplar, sc4-network-game-command (C# tools)
  - sc4-lua-scripts, sc4-lua-test-scripts (Lua)
  - sc4-game-command-documentation (docs)
  - SC4ModMigrationAssistant, SC4-Modding-Suite (.NET apps)
  - sc4-ghidra-symbols (Ghidra data)
  - nsgomez/scion (framework reimplementation, not a plugin)

Several of the GZCOM-only DLLs call the game's notification-dialog function at 0x78DD80 by fixed address. That is a call into the game, not a patch.

**Closed-source DLLs referenced but not censused.** These appear in package metadata, but their source is not public:

- Extra Cheats DLLs (two variants)
- Real Upgradeable City Halls DLL

## 3. Per-DLL patch sites

Common helper behaviour. The 0xC0000054 Patcher, whether it comes from sc4-dll-utilities or a local copy in the repo, works like this:

- InstallJump / InstallHook writes E9 rel32 (5 bytes).
- InstallCallHook writes E8 rel32 (5 bytes).
- InstallJumpTableHook writes a 4-byte pointer.
- OverwriteMemory writes 1 or 4 bytes.
- It never verifies the original bytes and never uninstalls.

The NAM-style helpers that memo33's DLLs reuse behave the same way. NAM adds verified variants for immediates and table entries. Unless a row says otherwise, "Verifies" = no and "Uninstall" = no.

### 3.1 NAMTeam/nam-dll

- Gating: exactly 641. All patches are installed at OnStart. Each one is individually gated by a NAM.ini key, and every key defaults to on except the two noted below.
- Sources: src/NAMDllDirector.cpp, src/Rul2Engine.cpp, src/NetworkSlopes.cpp, src/FlexPieces.cpp, src/CommuteLoop.cpp, src/PathJumping.cpp, src/TransitNetworkMapping.cpp, src/DirtRoadAccess.cpp, src/Patching.cpp.

| Address | Kind | Entry/mid | Purpose | Verifies | Uninstall | Source |
|---|---|---|---|---|---|---|
| 0x637F80 | B, 1 byte (Jcc to JMP short) | mid (Ghidra, section 9) | draggable diagonal streets | no | no | src/NAMDllDirector.cpp |
| 0x63AFF2 | B, 1 byte (Jcc to JMP short) | mid (Ghidra, section 9) | draggable diagonal streets | no | no | src/NAMDllDirector.cpp |
| 0x729FFF | B, 1 byte | mid (Ghidra, section 9) | disable auto-connect for RHW and streets | no | no | src/NAMDllDirector.cpp |
| 0x6459BC | B, 4 bytes (absolute operand replaced with a pointer to a DLL float) | mid (inferred) | lower minimum ferry/bridge height | no | no | src/NAMDllDirector.cpp |
| 0x6099C4 | B, 4 bytes (immediate) | mid (inferred) | fallback network type = Road | no | no | src/FlexPieces.cpp |
| 0x714222 | J + stub; returns 0x714238 / 0x714398 | mid | tunnels for RHW, street, light rail | no | no | src/NAMDllDirector.cpp |
| 0x63D316 | J + stub; returns 0x63D33C | mid | RUL2 engine rule overrides | no | no | src/Rul2Engine.cpp |
| 0x634D79 | J + stub; returns 0x635282 (replaces about 1.3 KB of code) | mid | RUL2 engine tile subsets | no | no | src/Rul2Engine.cpp |
| 0x6366C5 | J + stub; returns 0x636907 | mid | network slope constraints | no | no | src/NetworkSlopes.cpp |
| 0x62CEDC | J + stub; returns 0x62CF42 | mid | FLEX puzzle pieces (isolated highway intersection) | no | no | src/FlexPieces.cpp |
| 0x6D7EBE | J + stub; returns 0x6D7EC5 | mid | commute-loop fix (nearest destination search) | no | no | src/CommuteLoop.cpp |
| 0x6D7EF0 | J + stub; returns 0x6D7EF7 | mid | commute-loop fix | no | no | src/CommuteLoop.cpp |
| 0x6D79BF | J + stub; returns 0x6D79C5 / 0x6D7A42 | mid | commute-loop fix (at-goal test) | no | no | src/CommuteLoop.cpp |
| 0x71AB60 | J + stub; returns 0x71ABAB | mid (inferred; the stub uses the caller's frame) | vertical path separation | no | no | src/PathJumping.cpp |
| 0x6D95B1 | J + stub; returns 0x6D95B6 | mid | vertical path separation (path finder) | no | no | src/PathJumping.cpp |
| 0x70EAE7 | J + stub; returns 0x70EAF1 / 0x70EB59 | mid | vertical path separation (path node connection) | no | no | src/PathJumping.cpp |
| 0x4D09D1 | J + stub; returns 0x4D0B4A | mid | monorail in the rail route query text (ini default **off**) | no | no | src/TransitNetworkMapping.cpp |
| 0xAB3174, 0xAB3178, 0xAB3180, 0xAB318C | D, 4 bytes each | data | network-to-transit-type table: Road, Rail, Street, Avenue (indices resolved against upstream gzcom-dll enums, inferred) (default **off**) | yes | no | src/TransitNetworkMapping.cpp |
| 0xB096C0, 0xB096D4 | D, 4 bytes each | data | travel-type attribute network flags: elevated train, monorail (offsets inferred as above) (default **off**) | yes | no | src/TransitNetworkMapping.cpp |
| 0x4BE6DC | B, imm32 of PUSH | mid | dirt road counts as road access (ini default **off**) | yes (opcode + value) | no | src/DirtRoadAccess.cpp |
| 0x4BE70B | B, imm32 of TEST EAX | mid | dirt road counts as road access (ini default **off**) | yes | no | src/DirtRoadAccess.cpp |
| 0x6C1726 | B, imm32 of PUSH | mid | ferry terminal road access (dirt road) | yes | no | src/DirtRoadAccess.cpp |
| 0x6C1BD1 | B, imm32 of PUSH | mid | road access calculation (dirt road) | yes | no | src/DirtRoadAccess.cpp |

### 3.2 nsgomez/sc4fix

- This is a plain DllMain DLL, not a GZCOM plugin.
- Supported builds: 640 and 641. On any other build it refuses to load.
- At load it makes 0x407000 through about 0xB06FFF writable and executable (PAGE_EXECUTE_READWRITE) and never restores the protection. That range covers most of the image, including .text.
- All patches are unconditional.

| Address | Kind | Entry/mid | Purpose | Verifies | Uninstall | Source |
|---|---|---|---|---|---|---|
| 0x87B3D1 | N, 5 bytes | mid (call site) | turns the FreeLibrary call in the GZCOM library free path into a no-op so plugin DLLs stay loaded | no | no | DLLUnloadPreempt.cpp |
| 0x65EE3E | J + stub; returns 0x65EE43, null path 0x65EE66 | mid | puzzle-piece crash fix, part 2 | no | no | PuzzlePieceTE.cpp |
| 0x65EE66 | J + stub; returns 0x65EE6B | mid | puzzle-piece crash fix, part 3 | no | no | PuzzlePieceTE.cpp |
| 0x5D3DE0 | E, 5-byte jmp to a full reimplementation that never returns to the original | entry (inferred) | prop-pox fix: COM serializer save-class-objects rewritten | no | no | PropPoxVaccine.cpp |
| (0x96DA1D) | J + stub; returns 0x96DA23 | mid | puzzle-piece crash fix, part 1. **Not actually installed**: a copy-paste error writes this hook to 0x65EE3E instead, and the correct hook then overwrites it there. | no | no | PuzzlePieceTE.cpp |

### 3.3 memo33/submenus-dll

- Gating: exactly 641.
- Requires the sc4-resource-loading-hooks DLL (version 2.0.1 or later) at runtime.
- All sites are unconditional. Each is a 5-byte E9 into a naked stub that ends with `push <continue>; ret`, except the one byte overwrite.
- Source for every row: src/SubmenusDllDirector.cpp.

| Address | Kind / continue | Entry/mid | Purpose |
|---|---|---|---|
| 0x78F028 | J; 0x78F036 / 0x78F038 / 0x78F053 / 0x78F0B6 | mid (catalog item list builder, 0x78EF30) | submenu and network buttons in catalog lists |
| 0x78EFAC | J; 0x78EFB2 | mid, same function | duplicate-icon fix (clear cache) |
| 0x78EFF1 | J; 0x78EFF6 | mid, same function | duplicate-icon fix |
| 0x78F00F | B, 1 byte | mid, same function | swap the submenu property ID for the parent-ID property |
| 0x7F036A | J; 0x7F0371 | mid (add buildings to item list) | missing-icon placeholder |
| 0x79A984 | J; 0x79A98D | mid (catalog view mouse-up) | click on a submenu button |
| 0x7F46AA | J; 0x7F46B0 / 0x7F4ADD | mid (button activated handler) | extra button-ID range |
| 0x7F46A3 | J; 0x7F46AA | mid, same function | reopen submenu |
| 0x7F4EB5 | J; 0x7F4EBE | mid, same function | civic menu |
| 0x7F4DBE | J; 0x7F4EA0 | mid, same function | reward menu |
| 0x7F3B5D | J; 0x7F3B64 | mid (utilities menu) | utilities menu |
| 0x7EC6E5 | J; 0x7EC6EA | mid (zone catalog view) | zone menu |
| 0x7F3D04 | J; 0x7F3D09 / 0x7F3D50 | mid (transport menu) | virtual transport button |
| 0x7F3D86 | J; 0x7F3D97 | mid, same function | tertiary-menu attach target |
| 0x7F3D68 | J; 0x7F3D70 | mid, same function | transport menu button ID |
| 0x7F074E | J; 0x7F0756 | mid (create building menu) | extra catalog list calls |
| 0x78F16A | J; 0x78F16F | mid (create catalog view) | extra catalog list call |
| 0x7F0073 | J; 0x7F0078 / 0x7F0194 / 0x7F065B | mid (add buildings to item list) | submenu categorisation |
| 0x7F0493 | J; 0x7F049C | mid, same function | unhide locked reward buildings |
| 0x7EB009 | J; 0x7EB00E | mid (invoke tertiary menu) | button tracking |

No row verifies the original bytes and none can be uninstalled.

### 3.4 caspervg/sc4-render-services (and its identical 0xC0000054 copy)

- Gating: 641 only. Every service checks the build.
- The core DLL patches only CALL sites.
  - Before writing, it checks that the opcode at the site is E8. It does not check the original target.
  - It rewrites the 4-byte rel32 and saves the old value so it can restore it.
- The 15 draw-pass sites are **lazy**. Each one is installed when a client registers a callback for that draw pass, and removed when the last such callback unregisters.
- The 4 decal sites are installed at service start. They are on by default and roll back together if any one fails.
- Separately, the core DLL swaps the EndScene slot of the Direct3D 7 device vtable. That table lives in the driver, not in the executable.

| Address | Kind | Entry/mid | Purpose | Verifies | Uninstall | Source |
|---|---|---|---|---|---|---|
| 0x7CB770, 0x7CB777, 0x7CB77E, 0x7CB82A, 0x7CB831, 0x7CB838, 0x7CB84C, 0x7CB853, 0x7CB85A | C (4-byte rel32) | mid (call site) in 3D render Draw (0x7CB530) | draw-pass callbacks (pre/static/post, dynamic) | yes (E8) | yes | src/service/DrawService.cpp |
| 0x7CA964, 0x7CA96B, 0x7CA972 | C | mid (call site), translated-view update | dynamic draw-pass callbacks | yes | yes | src/service/DrawService.cpp |
| 0x7C9522, 0x7C9529, 0x7C9530 | C | mid (call site), viewport-rect update | static draw-pass callbacks | yes | yes | src/service/DrawService.cpp |
| 0x736B88 | C | mid (call site), overlay manager draw-decals to draw-rect | custom terrain decals | yes | yes | src/service/decal/TerrainDecalSymbols.cpp |
| 0x736B7C | C | mid (call site), same function, texture transform | custom terrain decals | yes | yes | src/service/decal/TerrainDecalSymbols.cpp |
| 0x73935F, 0x739373 | C | mid (call site), draw-overlays to draw-shadows (and the rough variant) | decal shadow replay | yes | yes | src/service/decal/TerrainDecalSymbols.cpp |

The optional sample DLLs in the same repository also patch the executable:

- **0x7CB530**: entry detour of 3D render Draw, using a trampoline. It saves the original bytes but does not verify them, and it can restore. It also re-patches the 9 Draw call sites. Source: src/sample/DrawServiceSampleDirector.cpp.
- **0xA8825C**: a float constant (contour interval), written and restored around a single call. Source: src/sample/OverlayManagerSampleDirector.cpp.
- **Camera tables 0xABCFC4, 0xABACB8, 0xABCFD8, 0xABACCC**: four tables of 5 floats each, rewritten at runtime. Source: src/sample/camera-view-input/SC4CameraControlLayout.hpp.

### 3.5 Drexel-Macintosh/SimCity4_UI_Modernization (SC4UIScale.dll)

This is the largest patcher, and the most careful one.

- **Verification.** Nearly every write is checked first: a memcmp against the expected stock bytes, or a pinned prologue for MinHook hooks. If the bytes don't match, that patch declines on its own. So if OpenSC4 changes these bytes, the DLL degrades safely instead of corrupting code.
- **Uninstall.** None, except one rollback inside the cheat-dialog block.
- **Gating.** It needs version 641 or later, and patching relies on byte verification. Almost every patch also requires:
  - a UI scale factor above 1.0 (`ScaleAll=1` is set in the shipped ini);
  - a per-feature ini switch, which defaults to on.
- **Timing.** Patches are applied from the director constructor and from PostAppInit. Some hooks install at PostCityInit, and some writes recur at runtime (region zoom).
- **Source.** All rows are in src/CodePatches.cpp unless noted. Others: src/UiSpike.cpp, src/UiSpikeFlyouts.cpp, src/ScaleTier.cpp, src/WebRedirect.cpp.

Byte and immediate families (all B, all mid, all verified):

| Addresses | Purpose |
|---|---|
| 0x7E87B1, 0x7E89D7, 0x7E8A02 | rating-arrow multiplier (Mayor rating HUD) |
| 0x79880A, 0x7988A9 | tooltip wrap width |
| 0x7EEF43, 0x7EEF54; 0x7EAD4B (J into a VirtualAlloc cave that returns to 0x7EAD53) | placement cost box and text alignment |
| 0x79D063, 0x79D068, 0x79D089, 0x79D0A4 | intro video surface size |
| 0x79BE2D (32 bytes), 0x79BF63 (39 bytes) | cheat dialog rectangle (block rewrite; rolls back) |
| 0x7EE15A | restore-toolbars button position |
| 0x79388F, 0x79388B | advice list row column |
| 0x52CCEE, 0x52CD01, 0x762F85, 0x762F98 | message popup style IDs |
| 0x77D33F, 0x77D35F, 0x77D4BF, 0x77D4DF, 0x7818B6, 0x7818D7, 0x7819D3, 0x7819F4, 0x785512, 0x785532, 0x78562C, 0x78564C, 0x7872AC, 0x7872CC, 0x7873EE, 0x78740E, 0x7893A5, 0x7893C6, 0x7894BB, 0x7894DC | budget dialog button sizes |
| 0x77D4A0, 0x7819AD, 0x785607, 0x7873CF, 0x78949B | budget button x anchors |
| 0x77D31B, 0x77D49C, 0x781897, 0x7819A9, 0x7854F4, 0x785603, 0x78728E, 0x7873CB, 0x789386, 0x789497 | budget button y anchors |
| 0x77C998, 0x77CA88, 0x77CAE0, 0x77CE3E, 0x77CF16, 0x77CF6E, 0x77CC23, 0x77D0E0; blocks 0x77CBFC, 0x77D0B9 (43 bytes each) | ordinance panel insets |
| about 187 further sites in 0x77xxxx-0x789xxx (54 PUSH imm8, 63 PUSH imm32, 53 SUB imm8, 17 LEA disp8; plus 0x786F26, 0x786F2C) | budget department, deals, master and ordinance column constants (the address list is in the source file and was not transcribed here) |
| 0x77C19E, 0x77C1B0, 0x77C1D9, 0x77C2E0, 0x77C301 (each a J into a per-site cave that returns to site+7); 0x77C2BC, 0x77C2BA | business deals box |
| 0x7A07D4, 0x7A07D9, 0x7A07F3, 0x7A080B, 0x7A08F4, 0x7A08FD, 0x7A090B, 0x7A0922 | Data Views legend |
| 0x76E233, 0x76E239, 0x76E23C, 0x76E2AF, 0x76E2C8; blocks 0x76E0E8, 0x76E145, 0x76E1D6; 0x76DE79 | graph legend |
| 0x7A8560 (15 bytes, including a jump-table pointer) | minimap bake dispatch (adds a zoom level) |
| 0x5F20AF, 0x5F20BF, 0x5F1EEC, 0x5F1EFC | signpost and pin balloon size |
| 0x44DC98, 0x44DC9D, 0x44DCD1, 0x44DCD6 | redirect FontStyle.ini to the DLL's own file |
| 0x46CC48, 0x46CCBA, 0x46CB09, 0x46CCCE, 0x46EABD, 0x46EACA, 0x46EAF6, 0x46EB01, 0x46EB2D, 0x46EB38, 0x46EB64, 0x46EB6F | U-Drive-It balloon and marker scale |
| ini-supplied addresses (a debug key, off by default) | arbitrary verified writes, range-checked to 0x400000-0xC00000 |

Data writes (D, verified):

| Addresses | Purpose |
|---|---|
| 0xB0DBA4-0xB0DBB0 (4 floats); 0xB0DBBC-0xB0DBD0 (6 floats) | region view cell size and overlay basis; rewritten live on every region zoom step |
| 0xACD4A0 (7 dwords), 0xAB4AD0 (7 dwords) | rich-text point-size tables |
| 0xA8819C, 0xA881A0, 0xA88260, 0xA88268, 0xB07F80, and 0xA88170 (10 floats) | balloon quad geometry |
| 0xAA523C (5 floats) | marker zoom ramp (off by default) |

Vtable slots (V):

| Address | Verified? | Purpose / condition |
|---|---|---|
| 0xAB4C58, 0xADE598 | yes | chart margins |
| 0xA87284, 0xA872D0 | yes | probe only |
| 0xAC1400, slots 20 and 29 | **no** | flyout present/Blt watch. The first 64 slots are left PAGE_EXECUTE_READWRITE. |
| 0xADB418, slot 20 | no | |
| 0xAB6AA8, 0xAB6D88, plus up to 12 vtables discovered at runtime, slot 88 | no | flash guard |
| every code-pointing slot of the vtables at 0xAA4900, 0xAA4868, 0xAA48F0, 0xAA484C, 0xAA47E8, 0xAA47D0 | no | probe only, off by default |

Entry detours (E, MinHook, 5-byte jmp plus trampoline, no uninstall):

- Behaviour-changing, on by default:

  | Address | Verified? | Purpose |
  |---|---|---|
  | 0x7AE3D0, 0x7AE510 | no | region tiles |
  | 0x9AC43B | pinned prologue | song column width |
  | 0x7E8510 | pinned prologue | rating updater |
  | 0x5939B0 | pinned prologue | effect creation by name |
  | 0x99DB6B | no | window SetFlag |
  | 0x7E5C10, 0x7E5D80 | no | tool flyout openers |
  | 0x79AD00, 0x79A0E0 | no | sub-flyout placement and metrics |
  | 0x99C498 | no | log only |

- Probe hooks, off by default, prologues pinned:
  - 0x46D990, 0x5E90E0, 0x6CC970, 0x7D2990 (hooked by two probes), 0x46F240, 0x5F7810, 0x6D4860, 0x5F20A0, 0x5F1610, 0x5FD2D0, 0x496950, 0x528580, 0x4D7950, 0x5F7C80, 0x5F5FB0, 0x9C16FD
  - DX7 driver: 0x8857E0, 0x885A00, 0x882C80, 0x884BA0
  - Unpinned: 0x602B70, 0x4FBFE0, 0x505370
  - Three renderer vtable targets resolved at runtime.

### 3.6 aspctt/scvk

- This is a Vulkan graphics driver, registered by GZCOM class ID.
- It patches no game code by default. The only writes happen when `MaxFPS` in scvk.ini is greater than 0. They also require build 641, and each byte must still hold its stock value.
- The addresses come from sc4-disable-fps-limits.

| Address | Kind | Entry/mid | Purpose | Verifies | Uninstall | Source |
|---|---|---|---|---|---|---|
| 0x70244A | B, 1 byte | mid (immediate of a MOV, inferred) | Cheetah FPS cap (stock 15) | yes (refuses if the byte is not 15) | no | src/FpsLimit.cpp |
| 0x702457 | B, 1 byte | mid | Rhino cap (stock 20) | yes | no | src/FpsLimit.cpp |
| 0x702462 | B, 1 byte | mid | Turtle cap (stock 30) | yes | no | src/FpsLimit.cpp |

### 3.7 nsgomez/scgl

- An OpenGL 3 graphics driver, registered by GZCOM class ID.
- No executable patches and no version gating. Source: src/cGDriver.h, src/cGDriversCOMDirector.cpp.

### 3.8 0xC0000054/sc4-more-building-styles

- Gating: 641. Patches install at PostAppInit.
- Sources: src/BuildingSelectWinProcHooks.cpp, src/BuildingDevelopmentSimulatorHooks.cpp, src/LotConfigurationManagerHooks.cpp, src/TractDeveloperHooks.cpp.

| Address | Kind | Entry/mid | Purpose | Source |
|---|---|---|---|---|
| 0x769968 | J + stub; returns 0x76998F, 0x7699EA, 0x769A5B, 0x7699D0, 0x769A8F, 0x769A37, 0x769B25 | mid (building-style window message handler) | new style and option button IDs | src/BuildingSelectWinProcHooks.cpp |
| 0x7699A0, 0x7699B6, 0x769999, 0x7699AF | C | mid (call site) | enable / add / remove style buttons | src/BuildingSelectWinProcHooks.cpp |
| 0x769B77 | J to 0x769BEA | mid | skip the stock style-button loop | src/BuildingSelectWinProcHooks.cpp |
| 0x769BEC | C | mid (call site) | set active style buttons | src/BuildingSelectWinProcHooks.cpp |
| 0x69CD78 | J + stub; returns 0x69CD7F | mid (building development simulator message handler) | cheat toggles instead of setting | src/BuildingDevelopmentSimulatorHooks.cpp |
| 0x69CD65 | J + stub; returns 0x69CD6C | mid, same function | cheat toggles instead of setting | src/BuildingDevelopmentSimulatorHooks.cpp |
| 0x48AE4B | C | mid (call site), lot configuration manager building info init | inject the building-styles property | src/LotConfigurationManagerHooks.cpp |
| 0x704CA2 | J + stub; returns 0x704D22 / 0x704CDA | mid (tract developer: lot configuration suitability) | style compatibility | src/TractDeveloperHooks.cpp |
| 0x704E78 | J + stub; returns 0x704EF4 / 0x704ED3 | mid (tract developer: building compatibility) | style compatibility | src/TractDeveloperHooks.cpp |
| 0x70D509 | J + stub; returns 0x70D532 | mid (tract developer Grow, inferred) | lot aggregation and subdivision toggles | src/TractDeveloperHooks.cpp |
| 0x70D4D2, 0x70D4EA, 0x70D4FA, 0x70D647, 0x70D63A | C | mid (call site) in Grow (inferred) | logging only (ini `LogGrowableFunctions`) | src/TractDeveloperHooks.cpp |

No row verifies the original bytes and none can be uninstalled.

### 3.9 0xC0000054/sc4-data-view-extensions

- Gating: 641. All sites are in src/cSC4WinMapViewHooks.cpp.
- All are mid. No row verifies the original bytes and none can be uninstalled.

| Address | Kind / continue | Purpose |
|---|---|---|
| 0x7A5716 | J; 0x7A571B / 0x7A592B | map view message handler: new radio buttons |
| 0x7A0F3D | B, imm32 | raise the maximum radio-button ID |
| 0x7A53D0 | J; 0x7A53D5 | SetDataView: highlight init |
| 0x7A4A1A | J; 0x7A4A1F | SetDataView: highlight shutdown |
| 0x7A62D8 | J; 0x7A62E0 | ShowWindow: notification targets |
| 0x7A30AA | J; 0x7A30B3, 0x7A4375, 0x7A3240, 0x7A331D, 0x7A487C | Update: new data types |
| 0x7A1A68 | J; 0x7A1A6E / 0x7A1FD2 | UpdateHighlights: custom highlights |

### 3.10 0xC0000054/sc4-bulldoze-extensions

- Gating: 641. All sites are in src/cSC4ViewInputControlDemolishHooks.cpp.
- No row verifies the original bytes and none can be uninstalled.

| Address | Kind | Entry/mid | Purpose |
|---|---|---|---|
| 0xA901D8 | V | data (demolish control vtable, inferred) | key-down handler |
| 0xA901E8 | V | data (demolish control vtable, inferred) | mouse-up handler |
| 0xA901F4 | V | data (demolish control vtable, inferred) | mouse-wheel handler |
| 0xA901FC | V | data (demolish control vtable, inferred) | Activate |
| 0x4B97ED | B, 1 byte | mid (update selected region) | argument re-encode |
| 0x4B97EE | B, 1 byte | mid, same function | argument re-encode |
| 0x4B97EF | C, 5 bytes (replaces a 4-byte virtual call, so it may clobber 1 byte; inferred) | mid | diagonal and filtered demolish preview |
| 0x4B9D02 | C | mid (call site) in mouse-up-left | filtered demolish |

### 3.11 0xC0000054/sc4-query-ui-hooks

- Gating: 641. GZCOM classes are registered only on 641.
- Sources: src/BuildingQueryHooks.cpp, src/NetworkQueryHooks.cpp, src/PropQueryHooks.cpp, src/FloraQueryHooks.cpp, src/TerrainQueryHooks.cpp.
- All sites are mid (call site) unless noted. No row verifies the original bytes and none can be uninstalled.

| Address | Kind | Purpose | Source |
|---|---|---|---|
| 0x4D4494 | C | building query dialog (describe pick) | src/BuildingQueryHooks.cpp |
| 0x40CA7A | C | building query dialog (Lua window.query path) | src/BuildingQueryHooks.cpp |
| 0x4D7464 | C | building tooltip | src/BuildingQueryHooks.cpp |
| 0x4D4406 | C | mute the query sound (only when the ini key is false) | src/BuildingQueryHooks.cpp |
| 0x4D4EAA | J + stub; returns 0x4D4F19 / 0x4D4F30 (mid, splits a 7-byte instruction, inferred) | shift-click copy of an occupant | src/BuildingQueryHooks.cpp |
| 0x4D74A8 | C | network tooltip | src/NetworkQueryHooks.cpp |
| 0x4D74BF | C | prop tooltip | src/PropQueryHooks.cpp |
| 0x4D747F | C | flora tooltip | src/FloraQueryHooks.cpp |
| 0x4D7335 | C | terrain query text | src/TerrainQueryHooks.cpp |

### 3.12 0xC0000054/sc4-dbpf-loading

- Gating: 641.
- Sources: src/DBPFLoadingDllDirector.cpp, src/LooseSC4PluginScanPatch.cpp, src/cRZFileHooks.cpp.

| Address | Kind | Entry/mid | Purpose | Verifies | Uninstall | Source |
|---|---|---|---|---|---|---|
| 0x4572CE | B, 1 byte (JZ to JMP) | mid (app resource setup, 0x4572B0) | disable debug load code | no | no | src/DBPFLoadingDllDirector.cpp |
| 0x9729E1 | C | mid (call site), packed-file header-record search | skip the DBPF signature scan | no | no | src/DBPFLoadingDllDirector.cpp |
| 0x48C603 | C | mid (call site), string format call | missing plugin pack ID shown in hex | no | no | src/DBPFLoadingDllDirector.cpp |
| 0x44C97E | C | mid (call site), calls resource setup | load timing trace (command-line switch only) | no | no | src/DBPFLoadingDllDirector.cpp |
| 0x457A07 | J + stub; returns 0x457B50 | mid | load loose .SC4* files from the installation Plugins folder | no | no | src/LooseSC4PluginScanPatch.cpp |
| 0x457C86 | J + stub; returns 0x457DCF | mid | load loose .SC4* files from the user Plugins folder | no | no | src/LooseSC4PluginScanPatch.cpp |
| 0x919B00 | E (Microsoft Detours) | entry (file Open) | file-open hook | no | possible but never called | src/cRZFileHooks.cpp |
| 0x9192A9 | E (Microsoft Detours) | entry (file ReadWithCount; the address is unaligned, worth confirming) | file-read hook | no | possible but never called | src/cRZFileHooks.cpp |

### 3.13 0xC0000054/sc4-lua-extensions

- Gating: 641. Patches install at OnStart.
- No row verifies the original bytes and none can be uninstalled.

| Address | Kind | Entry/mid | Purpose | Source |
|---|---|---|---|---|
| 0x8B09C8 | C | mid (call site), Lua script server init | register native functions, error logging | src/cGZLuaScriptServerHooks.cpp |
| 0x51C50B | B, imm32 (function pointer) | mid, automata script system init | replace the Maxis `print` | src/LuaPrintFunction.cpp |
| 0x5FBD64 | C | mid (call site), package script loading | script naming and compile callback | src/PackageScriptLoadingPatch.cpp |

It also rewrites one entry of a static vector of native Lua functions (vector bounds at 0xB207CC and 0xB207D0) at runtime. Source: src/LuaPrintFunction.cpp.

### 3.14 0xC0000054/sc4-extended-terrain

- Gating: 641. Applied at PostAppInit. Source: src/ExtendedTerrainDllDirector.cpp.
- No row verifies the original bytes and none can be uninstalled.

| Address | Kind | Entry/mid | Purpose |
|---|---|---|---|
| 0x75C132 | B, imm32 | mid (terrain 3D view) | load the DLL's Gimex factory proxy class 0xE75D5221 instead of the stock factory |
| 0x75C176 | B, imm32 | mid, same function | use the DLL's texture-binding factory service 0xF379B4BF |

It also writes the global terrain texture prefix string at 0xB4BC64 at runtime.

### 3.15 memo33/sc4-lua-sandbox-dll

- Gating: 641. Source for every row: src/LuaSandboxDllDirector.cpp.
- No row verifies the original bytes and none can be uninstalled.

| Address | Kind | Entry/mid | Purpose |
|---|---|---|---|
| 0x8BC1E5 | J + stub; returns 0x8BC1F4 | mid (Lua chunk parser) | refuse precompiled (binary) Lua chunks |
| 0xACB374, 0xACB384, 0xACB38C, 0xACB394, 0xACB39C | V (luaL_reg function pointers) | data | block base-library file and loader functions |
| 0xACACB4, 0xACABEC | V | data | block string.dump and math.randomseed |
| 0xACAFF4, 0xACB004, 0xACB00C, 0xACB014, 0xACB01C, 0xACB024, 0xACB02C, 0xACB03C | V | data | block os.* functions |
| 0xACAF0C, 0xACAF14, 0xACAF24, 0xACAF3C, 0xACAF44, 0xACAF54, 0xACAF4C (conditional), 0xACAF6C, 0xACAF84, 0xACAF8C | V | data | block io.* and file write functions |
| 0xACAF1C, 0xACAF34, 0xACAF74, 0xACAF7C, 0xACAF94 | V | data | block file reading (unless the ini allows it) |

### 3.16 Small single-purpose DLLs

No row in this table verifies the original bytes, and none can be uninstalled.

| DLL | Address | Kind | Entry/mid | Purpose | Source |
|---|---|---|---|---|---|
| sc4-thumbnail-fix-dll | 0x752F43 | J + stub; returns 0x752F49 | mid (draw-frustum rect computation) | region thumbnail row count | src/ThumbnailFixDllDirector.cpp |
| sc4-thumbnail-fix-dll | 0x459D4E | J + stub; returns 0x459D53 | mid (call site replaced, inferred) | flag set around the thumbnail write | src/ThumbnailFixDllDirector.cpp |
| sc4-thumbnail-fix-dll | 0x5DE2DB | J (debug builds only) | mid | test magnification | src/ThumbnailFixDllDirector.cpp |
| sc4-3d-camera-dll | 0x7CCB0A | B, 4-byte float immediate (inferred) | mid | camera yaw | src/CameraDllDirector.cpp |
| sc4-3d-camera-dll | 0xABCFC4, 0xABACB8, 0xABCFD8, 0xABACCC (5 floats each) | D | data | per-zoom yaw and pitch tables, written when a cheat is typed | src/CameraDllDirector.cpp |
| transparent-texture-fix-dll | 0x5DF34E | J + stub; returns 0x5DF354 / 0x5DF362 / 0x5DF365 | mid (is-cell-covered test) | render terrain under transparent lot textures | src/TransparentBaseDllDirector.cpp |
| sc4-disable-fps-limits (caspervg) | 0x70244A, 0x702457, 0x702462 | B, 1 byte each | mid | FPS caps (ini `MaxFPS`, default 255) | src/DisableFpsLimitsDllDirector.cpp |
| sc4-disable-fps-limits (0xC0000054 snapshot) | 0x70244A, 0x702457, 0x702462 | B, 1 byte each (hard-coded 0xFF) | mid | FPS caps | src/DisableFpsLimitsDllDirector.cpp |
| sc4-advanced-water-management | 0xA92570 | V | data (query control dispatch table) | water building efficiency display | src/cSC4ViewInputControlQueryHooks.cpp |
| sc4-graphics-options | 0x887738 | B, 1 byte (16 to 32) | mid (inferred) | DirectX full-screen 32-bit color (DirectX + full-screen + 32-bit only). Also detours user32 window APIs, which it can uninstall. | src/GraphicsOptionsDllDirector.cpp; src/SC4WindowCreationHooks.cpp |
| sc4-full-screen-32-bit | 0x887738 | B, 1 byte (16 to 32) | mid (inferred) | DirectX full-screen 32-bit color | src/FullScreen32BitDllDirector.cpp |
| sc4-disable-network-construction-sounds | 0x6071FC | B, 1 byte (JZ to JMP) | mid (network construction crew init) | mute construction sounds | src/DisableNetworkConstructionSoundsDllDirector.cpp |
| sc4-disable-lot-plop-smoke | 0x4C3182 | B, 1 byte (pushed constant 1 to 0) | mid (place-lot tool) | no plop smoke | src/DisableLotPlopSmokeDllDirector.cpp |
| sc4-disable-demolition-animation | 0x4673BF | C | mid (call site) | treat every occupant as too small for a demolition animation | src/DisableDemolitionAnimationDllDirector.cpp |
| sc4-network-texture-test | 0x60F580 | J + stub; returns 0x60F5E7 | mid (network lot road-style update) | base texture selection | src/cSC4NetworkLotManagerHooks.cpp |
| sc4-network-texture-test | 0xAA7768 (16 bytes) | D (written without VirtualProtect) | data | zero the low-density residential base textures (off by default) | src/cSC4NetworkLotManagerHooks.cpp |
| sc4-lot-network-scoring-test | 0x705863 | J + stub; returns 0x705901 | mid (tract developer candidate lot scoring) | configurable network weighting | src/cSC4TractDeveloperHooks.cpp |
| sc4-lot-network-scoring-test | 0x7050E0 | J to 0x7050F2 | unknown (find-networks) | skip the dirt-road count | src/cSC4TractDeveloperHooks.cpp |
| sc4-cpu-options | CreateThread IAT slot (found at runtime) | I | data | pin threads to one core. Its import-name comparison appears inverted, so the hook probably never installs (inferred). | src/CPUOptionsDllDirector.cpp |

## 4. Address collisions

Exact or overlapping executable addresses written by more than one DLL:

| Address / range | DLLs | Nature |
|---|---|---|
| 0x70244A, 0x702457, 0x702462 | sc4-disable-fps-limits (caspervg), its 0xC0000054 snapshot, scvk (opt-in) | Same bytes, same intent. scvk checks for the stock values, so it refuses to write if an FPS DLL ran first. The two FPS DLLs overwrite each other without checking. |
| 0x887738 | sc4-graphics-options, sc4-full-screen-32-bit | Same byte, same value (32). Redundant. The two DLLs gate on different conditions. |
| 0xABCFC4-0xABCFD7, 0xABACB8-0xABACCB, 0xABCFD8-0xABCFEB, 0xABACCC-0xABACDF | sc4-3d-camera-dll, sc4-render-services camera sample | Both rewrite the same per-zoom camera yaw and pitch tables at runtime. The last writer wins. |
| 0x7CB770-0x7CB85A (9 call sites) | sc4-render-services core and its draw sample | The sample re-patches the core's call sites. Both check the opcode, so they chain by whatever order they install in (inferred). |
| 0x7D2990 | UI Modernization, two of its own probes | Same DLL. The second MinHook create fails. Both are off by default. |

Adjacent writes (no byte overlap):

- 0xA8825C (render-services overlay sample) sits next to 0xA88260 and 0xA88268 (UI Modernization), with the UI Modernization float table at 0xA88170-0xA881A3 nearby.
- In the NAM data tables, 0xAB3174-0xAB318F (NAM) and 0xAB4AD0 (UI Modernization) are in the same .rdata neighbourhood but do not overlap.

Function-level collisions are the ones that matter for OpenSC4: different addresses inside the same game function or class. Replacing that function silently disables every DLL listed against it.

| Game area | DLLs and sites |
|---|---|
| Query tool (cSC4ViewInputControlQuery) | sc4-query-ui-hooks (0x4D4406, 0x4D4494, 0x4D4EAA, 0x4D7335, 0x4D7464, 0x4D747F, 0x4D74A8, 0x4D74BF); sc4-advanced-water-management (dispatch slot 0xA92570); UI Modernization probe (0x4D7950); NAM (0x4D09D1, in the network traffic-info helper the query uses) |
| Tract developer (cSC4TractDeveloper) | sc4-more-building-styles (0x704CA2, 0x704E78, 0x70D4D2-0x70D647); sc4-lot-network-scoring-test (0x7050E0, 0x705863) |
| Catalog and menu UI | submenus-dll (0x78EFAC-0x78F16A, 0x79A984, 0x7EB009-0x7F4EB5); UI Modernization (0x79A0E0, 0x79AD00 entry detours, 0x7E5C10, 0x7E5D80, 0x7E8510, 0x7E87B1-0x7E8A02, 0x7EAD4B, 0x7EE15A, 0x7EEF43). Whether 0x79A984 (submenus) and the UI Modernization hook at 0x79A0E0 share a function is unknown. |
| Map / data view window | sc4-data-view-extensions (0x7A0F3D-0x7A62D8); UI Modernization (legend 0x7A07D4-0x7A0922, minimap 0x7A8560) |
| 3D render and camera | sc4-render-services (0x7C9522-0x7CB85A, plus the sample entry at 0x7CB530); sc4-3d-camera-dll (0x7CCB0A immediate, camera tables) |
| String format (cRZString::Sprintf, 0x90F574) | Two call-site rewrites of calls to the same function: sc4-dbpf-loading (0x48C603) and sc4-query-ui-hooks (0x4D7335). This is a collision only if the calling functions are replaced. |
| Lua runtime | sc4-lua-sandbox-dll (0x8BC1E5, tables 0xACABEC-0xACB39C); sc4-lua-extensions (0x8B09C8, 0x51C50B, 0x5FBD64, the native-function vector). The sites differ but the subsystem is the same. |

GZCOM collisions:

- **0xC4554841** (OpenGL driver class): scvk and SCGL both register version 1,000,000. Which one wins is not defined, so they are mutually exclusive.
- **Exemplar factory proxy class 0xEDA309D9**: sc4-resource-loading-hooks and sc4-exemplar-load-logging both register this class, and both register it as the resource factory for type 0x6534284A. They cannot coexist (inferred).
- **Duplicate director IDs** (copy-paste in the source):
  - 0x21E2B214: sc4-auto-run-cheats and sc4-regional-supply-demand
  - 0xC8F8CD0F: sc4-add-new-ordinances and sc4-city-lottery-ordinance

## 5. Hot regions

The game-area labels come from the function names that the DLL sources use (inferred).

| Range | Game area | Patchers (approximate sites) |
|---|---|---|
| 0x44C900-0x48C700 | app startup, resource setup, font path, lot configuration | sc4-dbpf-loading (6), UI Modernization (4), sc4-more-building-styles (1) |
| 0x4B9000-0x4D7FFF | view input controls: demolish, place-lot, query | sc4-bulldoze-extensions (4), sc4-disable-lot-plop-smoke (1), sc4-query-ui-hooks (9), NAM (3 including 0x4BE6DC/0x4BE70B), UI Modernization probe |
| 0x5D3D00-0x5FBE00 | COM serializer, thumbnails, cell coverage, signposts, Lua help | sc4fix, thumbnail-fix, transparent-texture-fix, UI Modernization (8), sc4-lua-extensions |
| 0x6099C0-0x6459C0 | network tool and RUL engine | NAM (10), sc4fix puzzle-piece (2), sc4-network-texture-test, network-construction-sounds |
| 0x69CD60-0x71ABB0 | building development, FPS caps, tract developer, pathfinding | sc4-more-building-styles (12), sc4-lot-network-scoring-test (2), NAM (8), FPS DLLs and scvk (3) |
| 0x729FFF-0x75C180 | auto-connect, overlay-manager decals, thumbnail frustum, terrain view | NAM, sc4-render-services (4), thumbnail-fix, sc4-extended-terrain (2) |
| 0x762F80-0x789500 | budget, ordinance and department dialogs | UI Modernization (about 280) |
| 0x78EF00-0x7A8700 | catalog lists, catalog view, cheat/intro/advice dialogs, map/data view, minimap | submenus-dll (6), UI Modernization (about 40), sc4-data-view-extensions (7) |
| 0x7C9500-0x7CCB10 | 3D render draw passes, camera | sc4-render-services (15 + sample), sc4-3d-camera-dll |
| 0x7E5C00-0x7F4F00 | main toolbars and menus, rating HUD, cost readout | submenus-dll (14), UI Modernization (about 12) |
| 0x882C80-0x887740 | DirectX 7 driver | UI Modernization probes (4), sc4-graphics-options / sc4-full-screen-32-bit (1) |
| 0x87B3D1, 0x8B09C8-0x8BC1F4, 0x9192A9-0x9729E1, 0x99C498-0x9C16FD | GZCOM library free, Lua 5.0 runtime, GZ file/DBPF layer, GZWin framework | sc4fix, the Lua DLLs, sc4-dbpf-loading (3), UI Modernization (4) |
| 0xA87000-0xB20000 (data) | vtables, Lua registration tables, size and geometry tables | sc4-bulldoze-extensions, sc4-advanced-water-management, sc4-lua-sandbox-dll (30), NAM (6), UI Modernization (about 40), 3D camera tables |

## 6. GZCOM class overrides and exposed services

Overrides of existing game classes:

| Class ID | DLL | Version | Replaces |
|---|---|---|---|
| 0xC4554841 | aspctt/scvk | 1,000,000 | the game's OpenGL graphics driver (Vulkan implementation) |
| 0xC4554841 | nsgomez/scgl | 1,000,000 | the game's OpenGL graphics driver (OpenGL 3 implementation) |
| 0x090F8619 (cGZPersistDBSegmentMultiPackedFiles) | sc4-dbpf-loading | 1 (the game registers 0) | the multi-packed DBPF segment used to load plugin folders |
| 0xEDA309D9, registered as the resource factory for type 0x6534284A | sc4-resource-loading-hooks | n/a (resource-manager registration) | the Exemplar resource factory. The proxy wraps the stock factory 0x453429B3. |
| 0xEDA309D9, same registration | sc4-exemplar-load-logging | n/a | the Exemplar resource factory (a logging proxy) |

The redirects in sc4-extended-terrain (0xE75D5221, 0xF379B4BF) are new IDs. The game is pointed at them by immediate patches, not by overriding the stock IDs. sc4-graphics-options selects among the stock driver IDs (0xBADB6906 DirectX, 0xC4554841 OpenGL, 0x7ACA35C6 Software) through the public API.

Custom interfaces that other DLLs consume:

| DLL | Class / service ID | Interface ID | Interface | Header (repo path) |
|---|---|---|---|---|
| sc4-render-services | service 0xA4F2D0C1 (API v3) | 0x9B6F8E21 | ImGui service | src/public/cIGZImGuiService.h |
| sc4-render-services | service 0xC3D0C4A1 | 0xD25E9B31 | S3D camera service | src/public/cIGZS3DCameraService.h |
| sc4-render-services | service 0xD6A70C11 | 0xA43BF2E7 | draw-pass service | src/public/cIGZDrawService.h |
| sc4-render-services | service 0xD4A3B911 | 0xD4A3B912 | terrain decal service | src/public/cIGZTerrainDecalService.h |
| sc4-query-ui-hooks | 0x73B4DE59 | 0x791E53A0 | building query hook server | src/public/include/cIBuildingQueryHookServer.h |
| sc4-query-ui-hooks | 0x32A9898E | 0x384B6D29 | network tooltip hook server | src/public/include/cINetworkQueryToolTipHookServer.h |
| sc4-query-ui-hooks | 0x7DE0764D | 0x7AD86A4B | flora tooltip hook server | src/public/include/cIFloraQueryToolTipHookServer.h |
| sc4-query-ui-hooks | 0x8D73886D | 0x8D73886F | prop tooltip hook server | src/public/include/cIPropQueryToolTipHookServer.h |
| sc4-more-building-styles | 0x683B30D3 | 0xC6C058F0 / 0xC6C058F1 | building style info | src/public/include/cIBuildingStyleInfo.h, cIBuildingStyleInfo2.h |
| sc4-more-building-styles | 0x9AEE8F81 | 0x9AEE8F82 | wall-to-wall styles | src/public/include/cIBuildingStyleWallToWall.h |
| sc4-lua-extensions | 0x480FAD3F | 0xADDFAD50 | package script compilation callback server | src/public/include/cIPackageScriptCompilationCallbackServer.h |
| sc4-lua-extensions | message 0xF6A6C53C | n/a | "add native Lua function" message | src/public/include/LuaScriptServerAddNativeFunctionMessage.h |
| sc4-resource-loading-hooks | 0xFD7BD8C4 | 0xFC1A55E1 | exemplar load hook server | src/public/include/cIExemplarLoadHookServer.h |
| sc4-resource-loading-hooks | 0xD87C03CB (service 0x0E7062FD) | 0xDB594ACF | exemplar patching server | src/public/include/cIExemplarPatchingServer.h |
| sc4-custom-ordinance-host | n/a | 0x8BD4FC74 | simple ordinance | src/cISC4OrdinanceSimple.h |
| sc4-auto-save | service 0x3FA8CBB9 | n/a | auto-save system service | (internal) |

Known dependencies on these interfaces:

- submenus-dll depends on the resource-loading-hooks patching server.
- sc4-advanced-water-management depends on the query-ui-hooks building server.
- sc4-query-ui-hooks optionally consumes building style info.
- sc4-lua-extension-example and others use the Lua extensions.

## 7. Implications for OpenSC4

General findings:

1. **Most patchers write without checking.** Only UI Modernization, scvk, the NAM immediate/table writes and the render-services opcode check verify bytes before writing. Every other patch writes blindly. That has two consequences:
   - If OpenSC4 has already put an entry detour or a rewritten body where a plugin later writes, the plugin corrupts OpenSC4 code.
   - If a plugin patches first, OpenSC4's detour can overwrite the plugin's stub jump.

   Plugins patch during GZCOM startup (constructor, OnStart, PreFrameWorkInit, PostAppInit), and some patch later at runtime (render-services draw callbacks, camera cheats, UI Modernization region zoom).
2. **Recommended runtime guard.** Snapshot the original .text and .rdata/.data bytes at injection. Diff them again after PostAppInit and at PostCityInit. Treat any foreign-modified byte range as "owned by a plugin", and refuse to replace a function that contains one, or fall back to the original code for it. This turns the static census into a live check and also catches closed-source DLLs.
3. **Call-site rewrites and entry detours mostly coexist.** A rewritten CALL that targets the DLL, where the DLL in turn calls the original address, still reaches an OpenSC4 entry detour on that callee. What breaks it is replacing the *caller* that contains the call site.
4. **Entry-patched functions conflict directly with OpenSC4 entry detours:**
   - sc4fix 0x5D3DE0
   - sc4-dbpf-loading 0x919B00, 0x9192A9
   - UI Modernization entry detours (0x5939B0, 0x7AE3D0, 0x7AE510, 0x7E5C10, 0x7E5D80, 0x7E8510, 0x79A0E0, 0x79AD00, 0x99C498, 0x99DB6B, 0x9AC43B)
   - render-services sample 0x7CB530

   Detours and MinHook relocate prologues. Stacking them works only when the second hooker follows the first one's jmp, so OpenSC4's hook layer should follow existing E9 chains rather than assume a stock prologue.
5. **Vtable and table slots.** Replacing slots that a plugin already swapped loses the plugin's behaviour unless OpenSC4 chains to the slot's current value:
   - bulldoze 0xA901D8-0xA901FC
   - advanced-water 0xA92570
   - UI Modernization vtable slots
   - lua-sandbox luaL_reg tables

   OpenSC4's vtable swaps should read the current pointer and chain to it, not to the stock function.
6. **Don't take the OpenGL driver class ID.** The OpenSC4 renderer (P1) should not claim 0xC4554841, or it will collide with scvk and SCGL. It should register a new driver class or keep its renderer internal. (Checked in P1: the graphics system never instantiates a new driver class ID, so OpenSC4 makes its driver current with `cIGZGraphicSystem2::SetGDriver` instead; see [renderer.md](renderer.md).) render-services ImGui needs the DirectX 7 driver (0xBADB6906) and its draw-pass call sites. An OpenSC4 renderer that replaces the 3D render Draw path must keep providing those draw-pass hook points.
7. **sc4fix leaves the image writable.** It makes most of the image RWX for the whole session. OpenSC4 must not rely on .text being read-only, for example as a tamper check.

Per-DLL compatibility policy:

- **Defer:** do not replace the affected functions until we coordinate with the DLL's authors.
- **Absorb:** reimplement the fix natively, then detect the DLL and skip or neutralise it.
- **Chain:** keep its hook points working, or re-expose its interfaces.

| DLL | Policy | Reason |
|---|---|---|
| nam-dll | defer | 12 mid-function stubs with fixed return addresses across the network tool, RUL engine and pathfinding. The roadmap already schedules networks only with the NAM team (P7). |
| sc4fix | absorb | Four small, well-understood fixes (DLL unload, puzzle-piece crash, prop-pox save rewrite). OpenSC4 can ship them correctly, including the part-1 fix that sc4fix never actually installs. |
| submenus-dll | defer | 19 mid-function stubs across catalog and menu UI functions, and widely depended on by content. |
| sc4-render-services | chain | Its patches are checked, reversible call-site rewrites, and it exposes 4 services other DLLs build on. OpenSC4's renderer must keep the draw-pass call sites, or provide the same service IDs. |
| UI Modernization | defer | About 385 sites in UI dialogs. It checks bytes and declines individually, so it fails safe, but replacing those dialogs (P2) duplicates its whole purpose. Coordinate before P2. |
| scvk / scgl | chain | Alternative drivers registered through GZCOM. Leave their class ID alone and keep the driver interface stable. |
| sc4-more-building-styles | defer | Tract developer and style-window mid-function stubs, and it exposes interfaces. |
| sc4-data-view-extensions | defer | 7 stubs inside the map view window functions. |
| sc4-bulldoze-extensions | defer | Demolish control vtable slots plus a mid-function re-encode. |
| sc4-query-ui-hooks | chain | Hook servers used by other DLLs. If OpenSC4 replaces the query tool, it must re-expose the same class IDs and interfaces. |
| sc4-dbpf-loading | absorb | Directly overlaps P3 (resource manager/DBPF). It overrides the multi-packed-file class and detours file I/O, so OpenSC4's loader should provide these speedups and detect the DLL. |
| sc4-resource-loading-hooks | chain | It wraps the stock exemplar factory and exposes servers that submenus-dll requires. OpenSC4's resource manager must honour factory registration. |
| sc4-exemplar-load-logging | chain | A debug factory proxy that uses the same registration path. It conflicts with resource-loading-hooks regardless of OpenSC4. |
| sc4-lua-extensions | chain | Exposes a message and a server. It depends on Lua init call sites. |
| sc4-lua-sandbox-dll | defer | Security-relevant patches to Lua runtime tables. Absorb once OpenSC4 owns Lua. |
| sc4-extended-terrain | defer | Immediate redirects inside terrain view setup, plus a custom factory service. |
| thumbnail-fix, transparent-texture-fix, disable-demolition-animation, disable-lot-plop-smoke, disable-network-construction-sounds | absorb | Single-site fixes or toggles, cheap to reimplement as options. |
| sc4-disable-fps-limits (both), scvk FPS option | absorb | Three bytes. Expose an OpenSC4 frame-cap setting and leave the stock bytes for the verifying writer (scvk). |
| sc4-graphics-options, sc4-full-screen-32-bit | absorb | One byte, plus window-mode handling that a new renderer or window layer subsumes. |
| sc4-3d-camera-dll | absorb | Writes camera data tables. A camera rewrite should keep those tables, or offer the same cheats. |
| sc4-advanced-water-management | chain | One dispatch slot, and it consumes query-ui-hooks. |
| sc4-network-texture-test, sc4-lot-network-scoring-test | defer | Test DLLs in NAM-adjacent code. |
| sc4-cpu-options | n/a | IAT only, with no code patches (and the hook appears not to install). |

Functions and areas we must not replace early (each contains third-party patch sites):

- **Network tool / RUL engine:** functions containing 0x6099C4, 0x62CEDC, 0x634D79, 0x637F80, 0x63AFF2, 0x63D316, 0x6366C5, 0x6459BC, 0x65EE3E/0x65EE66, 0x6071FC, 0x60F580.
- **Pathfinding and traffic:** 0x6D79BF, 0x6D7EBE, 0x6D7EF0, 0x6D95B1, 0x70EAE7, 0x714222, 0x71AB60, 0x729FFF.
- **Tract developer:** 0x704CA2, 0x704E78, 0x7050E0, 0x705863, and Grow (0x70D4D2-0x70D647).
- **Building development simulator:** 0x69CD65, 0x69CD78. **Building style window:** 0x769968-0x769BEC.
- **Query tool:**
  - Describe-pick, mouse-down and message handlers: 0x4D4406, 0x4D4494, 0x4D4EAA, 0x4D7335-0x4D74BF.
  - Network traffic info: 0x4D09D1.
  - Lua window.query path: 0x40CA7A.
  - Road access: 0x4BE6DC, 0x4BE70B, 0x6C1726, 0x6C1BD1.
- **Demolish control:** 0x4B97ED-0x4B97F3, 0x4B9D02, plus its vtable 0xA901D8-0xA901FC. **Place-lot tool:** 0x4C3182. **Demolition animation choice:** 0x4673BF.
- **App resource setup and plugin scan:** 0x44C97E, 0x4572B0 (0x4572CE, 0x457A07, 0x457C86), 0x48C603. **Packed-file header search:** 0x9729E1. **GZ file Open/ReadWithCount:** 0x919B00, 0x9192A9. **Multi-packed-file segment class:** 0x090F8619.
- **COM serializer save:** 0x5D3DE0. **GZCOM library free:** 0x87B3D1.
- **Catalog and menus:**
  - Catalog item list builder: 0x78EF30.
  - Catalog view: 0x78F16A, 0x79A984, 0x79A0E0, 0x79AD00.
  - Toolbar and menu handlers: 0x7E5C10-0x7F4EB5.
- **Map / data view and minimap:** 0x7A0F3D-0x7A62D8, 0x7A07D4-0x7A0922, 0x7A8560.
- **3D render:** Draw (0x7CB530) and its draw-pass call sites (0x7C9522-0x7CB85A). **Camera:** 0x7CCB0A and the tables at 0xABACB8 and 0xABCFC4. **Overlay manager decals and shadows:** 0x736B7C-0x739373. **Terrain view setup:** 0x75C132, 0x75C176. **Thumbnails:** 0x459D4E, 0x752F43. **Cell coverage:** 0x5DF34E.
- **Lua:**
  - Chunk parser: 0x8BC1E5.
  - Script server init: 0x8B09C8.
  - Automata script init: 0x51C50B.
  - Package script loading: 0x5FBD64.
  - luaL_reg tables: 0xACABEC-0xACB39C.
- **FPS cap setter:** 0x70244A-0x702462. **DirectX full-screen setup:** 0x887738.
- **Budget, ordinance and department dialogs** (0x77C000-0x789600), and the other UI dialogs UI Modernization touches, until P2 is coordinated.

## 8. Gaps

- Sections 2-7 come from source only (comments, names, continue-address deltas). Section 9 checks the placements against Ghidra's function boundaries and settles the open ones; what it could not settle is listed there.
- The NAM transit-table addresses use enum indices from upstream gzcom-dll, because NAM's vendored copy was not present. The Rail, Street and Avenue entries and the travel-type offsets are therefore inferred.
- UI Modernization:
  - About 187 budget/ordinance/department immediate sites were counted but not transcribed. They are listed in the repository's src/CodePatches.cpp.
  - Some vtable slots are discovered at runtime, so they cannot be listed statically.
- The 0xC0000054 repositories vendor sc4-dll-utilities and gzcom-dll as submodules that were not checked out. Their Patcher semantics were taken from the current sc4-dll-utilities source, and pinned older revisions may differ.
- Load order between plugins (which decides the winner at shared sites and in the scvk/SCGL tie) is not determined from source.
- Closed-source DLLs (Extra Cheats variants, Real Upgradeable City Halls) and any DLL not referenced from the censused repositories or the sc4pac package metadata are not covered. The runtime diff guard in section 7 is the mitigation.
- Only HEAD of each repository was read. Older released binaries may patch different sites.

## 9. Placement check against Ghidra (2026-10-05)

Every address in the section 3 tables was classified against the function boundaries of the Ghidra project ([`tools/re/census_placement.py`](../tools/re/census_placement.py), function list from `scripts/ghidra-setup.sh`), and the patched functions were matched against a coverage run (Making Money Tutorial, 120 s at speed 2, about six game years; [dev-environment.md](dev-environment.md), "Coverage"). Rechecked the same day after [`DefineOrphanFunctions.java`](../tools/ghidra/DefineOrphanFunctions.java) defined functions for the code Ghidra had left outside function bodies.

| | Sites | Entry | Mid | Data | Code outside function bodies |
|---|---|---|---|---|---|
| All section 3 tables | 293 | 3 | 229 | 61 | 0 |

- **Only 3 of 232 code sites are at a function entry**: sc4fix's prop-pox rewrite at 0x5D3DE0 and two sc4-dbpf-loading hooks. Every other code patch is inside a function. This confirms risk 1 in [architecture.md](architecture.md): replacing almost any patched function silently drops someone's patch, so the per-DLL compatibility manifest is required before replacing anything in these areas.
- **Every placement the census marked inferred is confirmed**, and none of the source-derived placements disagrees with Ghidra.
- **The previously unknown sites:** NAM 0x637F80 and 0x729FFF are mid-function; NAM 0x63AFF2 is mid-function too (FUN_0063AF40); lot-network-scoring-test 0x7050E0 is mid-function (FUN_00705040); the "unaligned" Detours target 0x9192A9 is a function entry.
- **Submenus 0x79A984 and UI Modernization 0x79A0E0 are not in the same function** (0x79A984 is in FUN_0079A940; 0x79A0E0 is the entry of FUN_0079A0E0). All 11 UI Modernization entry detours listed in section 7 are function entries in Ghidra.
- **No function is patched by more than one DLL** among the tabulated sites (UI Modernization's ~187 untranscribed immediates are not included).
- **35 of the 91 patched functions ran** in the coverage run, so those patches sit on code that a quiet city executes.
- **27 sites were first found in code outside every Ghidra function body**, for example kilobytes between FUN_006D90F0 (190 bytes) and the next function at 0x6DA060. Ghidra had left about 950 KB of code undefined: virtual methods, callbacks and exception funclets reached only through pointers, which auto-analysis does not treat as function starts without RTTI. `scripts/ghidra-setup.sh` now defines about 10,400 functions there, and all 27 sites are mid-function.
