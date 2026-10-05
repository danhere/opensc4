# Class Creation Census: GZCOM vs. Direct Construction

The P0 question: which game classes are created through GZCOM by class ID (swappable with no patching), and which are constructed directly (needing vtable swaps or detours)? The answer decides how much of option B can avoid touching code.

Method: static analysis of the Steam `SimCity 4.exe` 1.1.641 in Ghidra 12.1.4, after full auto-analysis, using the scripts in [`tools/ghidra`](../tools/ghidra) and [`tools/re`](../tools/re). Dated 2026-10-05. Nothing was run in the game. Addresses are virtual addresses (image base 0x400000).

## Summary

- **About 44% of instantiated classes have GZCOM factories** (462 of about 1060).
- **All 23 simulators are GZCOM-registered and created by class ID**, including pollution, crime, traffic, demand, land value, budget, RCI, power and water. This is the best case for P5-P7.
- Not GZCOM-registered: the lot manager, path finders, region view, lighting manager, the app object and others (list below). These need vtable swaps or detours.
- GZCOM-registered is necessary but not sufficient for a clean swap: other code may still touch a class's fields by offset or call its non-virtual methods. That has to be checked per class before replacing it.

## Findings

1. **The Windows exe has no RTTI for game classes.** Its 60 RTTI type descriptors are all STLport's.
2. **The exe carries its own ID-to-name table.** A debug table in `.data` (0xB07FDC-0xB0941C) holds 648 `{uint32 id, const char* name}` entries naming class IDs, message IDs and other constants. It is the only source of class names in the Windows exe. [`tools/re/extract_name_table.py`](../tools/re/extract_name_table.py) extracts it. The pairing direction was confirmed against gzcom-dll's `kCanvasMessageType` and `kMouseMessageType`. A few IDs carry two names (0x2990C05A is both `cSC4ProtectionSimulator` and `cSC4FireProtectionSimulator`).
3. **GZCOM registration matches gzcom-dll's `cRZCOMDllDirector::AddCls`.** Directors call it either with an immediate class ID, or with `cFoo::GetClassID()`, a getter compiled as `MOV EAX, imm32; RET`:

   ```
   push <factory>
   call cSC4PollutionSimulator::GetClassID   ; mov eax, 0x8990C065 / ret
   push eax
   mov  ecx, <director>
   call AddCls                               ; 0x0090E133
   ```

   | Function | Calls | Distinct class IDs | Calling functions |
   |---|---|---|---|
   | 0x0090E133 (`AddCls`, factory type 1, inferred) | 455 | 454 | 23 (directors) |
   | 0x0090E160 (`AddCls`, factory type 2, inferred) | 4 | 4 | 1 |

   458 distinct class IDs in all; the name table names 197 of them. A third function, 0x0091DE0F, takes 7 more (class ID, function) pairs from 7 different places; its role is not identified yet.
4. **The city creates its simulators through `cIGZCOM::GetClassObject`.** Around 0x45AD40-0x45AFA0, for each simulator member: `if (!m) com->GetClassObject(clsid, iid, &m)` (vtable slot 0xC). For example, class 0x0990C005 (`cSC4PowerSimulator`) is requested with IID 0xE9E7BFF5, which gzcom-dll names `GZIID_cISC4PowerSimulator`.
5. **Classes by how they are created.** A class is identified by its vtable, and every call to its constructor is classified by its caller:

   | Bucket | Classes | Share |
   |---|---|---|
   | GZCOM factory only | 356 | 31% |
   | Both GZCOM and `new` | 106 | 9% |
   | `new` only | 330 | 29% |
   | Stack, member or destructor calls only | 266 | 23% |
   | Only constructed as a base or member of another class | 93 | 8% |
   | **Total (distinct primary vtables)** | **1151** | |

   The 462 classes with a GZCOM factory match the 461 factory functions, which cross-checks both methods.
6. **Allocators.** 0x005E55E0 precedes 752 constructor calls (operator `new`, inferred); 0x009133DA precedes 272 (likely a framework allocator; not yet identified).

### Simulators (all GZCOM-registered)

| Class ID | Name |
|---|---|
| 0x0990BDD8 | cSC4AuraSimulator |
| 0xE990BE01 | cSC4BudgetSimulator |
| 0x8990BE7A | cSC4BuildingDevelopmentSimulator |
| 0x0A413481 | cSC4CivicBuildingSimulator |
| 0x8990C080 | cSC4CommercialSimulator |
| 0x8990C09A | cSC4CrimeSimulator |
| 0x2990BFF2 | cSC4DemandSimulator |
| 0xE990C045 | cSC4FlammabilitySimulator |
| 0xC990C034 | cSC4FloraSimulator |
| 0xA990C022 | cSC4IndustrialSimulator |
| 0xE990C18E | cSC4LandValueSimulator |
| 0x29E37DEC | cSC4MySimAgentSimulator |
| 0xC990C0A4 | cSC4NeighborsSimulator |
| 0xA990C08E | cSC4OrdinanceSimulator |
| 0x0990C075 | cSC4PlumbingSimulator |
| 0x6990C04F | cSC4PoliceSimulator |
| 0x8990C065 | cSC4PollutionSimulator |
| 0x0990C005 | cSC4PowerSimulator |
| 0x2990C05A | cSC4ProtectionSimulator (also named cSC4FireProtectionSimulator) |
| 0x4990C013 | cSC4ResidentialSimulator |
| 0x2990C1E5 | cSC4Simulator |
| 0x6990C1AA | cSC4TrafficSimulator |
| 0x2990C1BC | cSC4WeatherSimulator |

### Named classes that are not GZCOM-registered

cSC43DPlaceableObject, cSC4AdvisorPopupTracker, cSC4App, cSC4AudioAmbienceLayer, cSC4AutomataController, cSC4AutomataManager, cSC4CellTextureModel, cSC4DBSegmentTester1-5, cSC4DebugConsole, cSC4DisasterInstanceEffect, cSC4DisasterInstanceToxic, cSC4FoundationModel, cSC4LightingManager, cSC4LotConfiguration, cSC4LotManager, cSC4LotRetainingWallModel, cSC4MultiTypeNetworkOccupant, cSC4Nation, cSC4PathFinder, cSC4PortDeveloper, cSC4ScriptServiceResourceFactory, cSC4SimGrid, cSC4ToxicOccupant, cSC4TutorialTask, cSC4UndergroundOccupant, cSC4View3DWin, cSC4WaterPathFinder, cSC4WinIntroVideoScreen, cSC4WinProcSpy, cSC4WinRegionScreen, cSC4WinRegionView, cSC4WinSplashScreen.

## What this means for option B

- **The simulators can be replaced at the GZCOM level**, the same way scvk replaces the renderer, as long as a plugin-registered class ID takes precedence when the city asks for it. scvk shows that works for the renderer's ID; it still has to be confirmed for a simulator.
- **Before replacing a simulator, check who reaches into it.** Other simulators or UI code may read its fields by offset or call non-virtual methods directly. Those accesses decide whether the replacement must keep the original's layout (the ABI rule in [architecture.md](architecture.md)) or whether the access sites need detours too.
- **The rest is reached through vtables and detours, as the architecture expected.** A vtable lives once in `.rdata`, so a vtable swap affects every instance however it was created. Detours are needed only for non-virtual methods, free functions and inlined code.
- Vtable swaps collide with ecosystem DLLs that patch vtable slots (kind V in the [patch census](patch-census.md)), so the per-DLL compatibility manifest applies to them too.

## Limits of the method

- Heuristics, not ground truth. A constructor is any function whose last `MOV dword ptr [reg], <vtable>` names the class, so destructors match too; their callers land in the "stack, member or destructor" bucket, which is therefore an upper bound.
- A `new` site is recognised only in the MSVC shape `CALL allocator … MOV ECX,EAX … CALL ctor`. Constructors inlined at `new` sites (outside factories) are not seen.
- Registrations are recognised only when the class ID is an immediate or comes from a `MOV EAX, imm32; RET` getter called right before the push.
- Classes are counted by primary vtable; a class with multiple inheritance writes several vtables, and only the last one written at offset 0 identifies it.
- The 7 pairs passed to 0x0091DE0F are not counted as GZCOM.

## Reproducing

The Ghidra project and all outputs live outside the repo (they describe the game's code):

```bash
proj="$LOCALAPPDATA/opensc4/ghidra"; out="$LOCALAPPDATA/opensc4/analysis"
ghidra="$HOME/scoop/apps/ghidra/current/support/analyzeHeadless.bat"
"$ghidra" "$proj" SC4_1.1.641 -process "SimCity 4.exe" -noanalysis -readOnly -scriptPath tools/ghidra \
  -postScript ClassRegistrationCensus.java "$out/class-registrations.csv"
"$ghidra" "$proj" SC4_1.1.641 -process "SimCity 4.exe" -noanalysis -readOnly -scriptPath tools/ghidra \
  -postScript ClassCreationCensus.java "$out/class-registrations.csv" "$out/class-creation.csv"
python3 tools/re/extract_name_table.py "<path to SimCity 4.exe>" > "$out/name-table.csv"
```
