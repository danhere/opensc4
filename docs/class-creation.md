# Class Creation Census: GZCOM vs. Direct Construction

The P0 question: which game classes are created through GZCOM by class ID (swappable with no patching), and which are constructed directly (needing vtable swaps or detours)? The answer decides how much of option B can avoid touching code.

Method: static analysis of the Steam `SimCity 4.exe` 1.1.641 in Ghidra 12.1.4, after full auto-analysis, using the scripts in [`tools/ghidra`](../tools/ghidra). Dated 2026-10-05. Nothing was run in the game. Addresses are virtual addresses (image base 0x400000).

## Findings

1. **The Windows exe has no RTTI for game classes.** Its 60 RTTI type descriptors are all STLport's. Class names on Windows can only come from the Mac debug symbols (sc4-ghidra-symbols) or from GZCOM class IDs.
2. **GZCOM registration matches gzcom-dll's `cRZCOMDllDirector::AddCls`.** Two adjacent functions receive (class ID, factory) pairs:

   | Function | Calls | Distinct class IDs | Calling functions |
   |---|---|---|---|
   | 0x0090E133 (`AddCls`, factory type 1, inferred) | 260 | 259 | 20 (directors) |
   | 0x0090E160 (`AddCls`, factory type 2, inferred) | 4 | 4 | 1 |

   A third function, 0x0091DE0F, takes 7 more (class ID, function) pairs from 7 different places; its role is not identified yet.
3. **Classes by how they are created.** A class is identified by its vtable, and every call to its constructor is classified by its caller:

   | Bucket | Classes | Share |
   |---|---|---|
   | GZCOM factory only | 219 | 20% |
   | Both GZCOM and `new` | 45 | 4% |
   | `new` only | 445 | 40% |
   | Stack, member or destructor calls only | 294 | 27% |
   | Only constructed as a base or member of another class | 105 | 9% |
   | **Total (distinct primary vtables)** | **1108** | |

   The 264 classes with a GZCOM factory match the 263 registered class IDs, which cross-checks both methods.
4. **Allocators.** 0x005E55E0 precedes 839 constructor calls (operator `new`, inferred); 0x009133DA precedes 273 and 0x0090CF9A 31 (likely framework allocators; not yet identified).
5. Only 9 of the 263 registered class IDs are named in gzcom-dll, all framework classes: DB segments (`cGZDBSegmentPackedFile`, `cGZDBSegmentDirectoryFiles`, `cGZPersistDBSegmentMultiPackedFiles`, `cIGZDBSerialRecordAsIGZFile`, `cIGZPersistResourceKeyList`), windows (`cGZWinGen`, `cGZWinKeyAccelerator`), `cIGZGimexFactory` and `cISCLua`.

## What this means for option B

- **About a quarter of the game's classes can be replaced through GZCOM** with no code patching, as scvk does for the renderer.
- **The rest is reached through vtables and detours, as the architecture expected.** That is less fragile than the raw "`new` only" count suggests: a vtable lives once in `.rdata`, so a vtable swap affects every instance however it was created. Detours are needed only for non-virtual methods, free functions and inlined code.
- **Not yet known: which subsystems fall where.** Counts are per class, not weighted by importance. Whether the simulators (pollution, crime, demand, traffic) are GZCOM-created decides how cleanly P5-P7 can start, and that needs names: the next step is porting the Mac types and matching class IDs to them.
- Vtable swaps collide with ecosystem DLLs that patch vtable slots (kind V in the [patch census](patch-census.md)), so the per-DLL compatibility manifest applies to them too.

## Limits of the method

- Heuristics, not ground truth. A constructor is any function whose last `MOV dword ptr [reg], <vtable>` names the class, so destructors match too; their callers land in the "stack, member or destructor" bucket, which is therefore an upper bound.
- A `new` site is recognised only in the MSVC shape `CALL allocator … MOV ECX,EAX … CALL ctor`. Constructors inlined at `new` sites (outside factories) are not seen.
- Classes are counted by primary vtable; a class with multiple inheritance writes several vtables, and only the last one written at offset 0 identifies it.
- The 7 pairs passed to 0x0091DE0F are not counted as GZCOM.

## Reproducing

Ghidra project and outputs live outside the repo (they contain game code):

```bash
proj="$LOCALAPPDATA/opensc4/ghidra"; out="$LOCALAPPDATA/opensc4/analysis"
"$HOME/scoop/apps/ghidra/current/support/analyzeHeadless.bat" "$proj" SC4_1.1.641 \
  -process "SimCity 4.exe" -noanalysis -readOnly -scriptPath tools/ghidra \
  -postScript ClassRegistrationCensus.java "$out/class-registrations.csv"
"$HOME/scoop/apps/ghidra/current/support/analyzeHeadless.bat" "$proj" SC4_1.1.641 \
  -process "SimCity 4.exe" -noanalysis -readOnly -scriptPath tools/ghidra \
  -postScript ClassCreationCensus.java "$out/class-registrations.csv" "$out/class-creation.csv"
```
