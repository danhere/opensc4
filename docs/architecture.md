# Architecture

## Core constraint

The address and symbol database is the real critical path. The renderer can be swapped through GZCOM with no patching; everything after that needs detours into an exe that has no symbols.

## Loader

| Option | Verdict | Why |
|---|---|---|
| GZCOM plugin DLL in Plugins | Necessary, not enough | Loads after framework init, so it can't hook startup, resource manager init or COM setup |
| Proxy DLL (fake system DLL) | Fallback | Runs before `main`, but clashes with dgVoodoo/ddraw wrappers and is fragile under Wine |
| Patched exe | Rejected | Modifying and redistributing the exe is a legal and practical non-starter |
| **Launcher exe** | **Chosen** | Starts SC4 suspended, injects `core.dll`, resumes. Deterministic early load, leaves `SimCity 4.exe` untouched, becomes the standalone exe later. |

- `core.dll` installs hooks before the CRT and `WinMain` run.
- `core.dll` also registers as a GZCOM director, so the game treats it as a normal plugin and it can register services.
- A proxy-DLL entry point keeps Steam/GOG launches and mod managers working.
- **End state (host mode):** our exe becomes the process image and maps the original exe's sections at 0x400000 as a code library. Exe independence means removing that mapping.
  - Ecosystem DLLs that check the game version through the exe will need a version-reporting shim.

## Hooking layer: three mechanisms, one API

| Mechanism | Use for | Notes |
|---|---|---|
| GZCOM service replacement | Renderer (`cIGZGDriver`) and anything else created by class ID | Register the same class ID at a higher version; GZCOM hands out the highest. Proven by scvk. No code patching. |
| Vtable swaps | Concrete classes the game creates with `new` | One write per class in .rdata; must keep the exact slot layout from the Ghidra types |
| Inline detours at function entry | Free functions, non-virtual methods, simulator internals | 5-byte jmp plus trampoline (MinHook or Detours behind our wrapper). **Entry only, never mid-function.** That rule keeps coexistence with other DLLs tractable. |

Renderer class IDs: DirectX `0xBADB6906`, OpenGL `0xC4554841`, Software `0x7ACA35C6`.

## Replacement registry

A declarative table generated from the address database. Each row:

`{id, subsystem, symbol, win641_addr, size, kind: gzcom|vtable|detour, impl, status: original|ours|compare|disabled, deps, known_third_party_patches}`

- **Toggles per subsystem**, read from config when hooks are installed. Switching happens only at safe points (main menu, before a city loads), never mid-city.
- **Compare mode:** call both implementations on snapshotted inputs, log any divergence, return the original's result. Only for pure functions or functions whose state can be snapshotted.
- **Coverage meter:** samples which original functions still execute. This measures progress towards exe independence.
- **Startup manifest log:** which hooks are active and which third-party DLLs patched which bytes. This goes first in every bug report.

## ABI rules (fallback to original code)

- **clang-cl only** (MSVC ABI). See [decisions.md](decisions.md).
- **Calling the original:** typed function-pointer thunks generated from the address database, with exact `__thiscall` / `__stdcall` / `__cdecl` declarations. The Windows build mixes VC7 and Intel C++ 6, so some functions use non-standard register conventions and need naked-asm wrappers.
- **Never pass our STL across the boundary.** The game uses STLport 4.6 (confirmed by Ghidra Function ID databases). For containers the game owns, write layout mirrors (`orig::vector`, `orig::map`, `orig::string`) that use the game's allocator.
- **Heap ownership:** anything the original might free must be allocated with the original's allocator. Freeing memory across CRTs is the #1 crash class in projects like this.
- **Object layouts:** replacement classes stay field-for-field identical (`static_assert` offsets generated from Ghidra) until the last consumer of that class is ours. Ecosystem DLLs also read fields by offset.
- **No exceptions or SEH across the boundary.** Wrap every hook entry in a guard that turns failures into logged crashes.

## Subsystem map

Difficulty: L / M / H / VH.

| Subsystem | Mechanism | Depends on | Difficulty | User value |
|---|---|---|---|---|
| Address/symbol DB | Tooling | - | H | None directly; gates everything |
| Framework / GZCOM | Last; we stay a guest until host mode | - | H-VH | None until standalone |
| Resource manager / DBPF | Detour | Framework | M | Faster loads, diagnostics; override order must be exact |
| Renderer | **GZCOM swap** | Framework | M-H | **High** |
| UI (`cGZWin`) | Detour + vtable | Renderer, resources | H | **High** (scaling past 1024x768) |
| Input | Detour | Framework | L-M | Medium |
| Audio | GZCOM swap or detour | Resources | M | Low-medium |
| Lua 5.0 | Detour around bindings | Resources, sims | M | Low |
| Terrain | Detour | Renderer, save/load | H | Medium |
| Networks / RUL | Detour, **shared with the NAM DLL** | Terrain, lots | VH | High for modders |
| Lots / growth | Detour + vtable | Resources, networks, demand | VH | Medium-high |
| Save/load | Detour per serializer | Every stateful subsystem | VH | The foundation for verification |
| Grid sims (pollution, crime, coverage, land value) | Detour | Save RE | M-H each | Medium |
| Demand / RCI / budget | Detour | Grid sims, lots | H | High |
| Traffic | Detour | Networks, lots, demand | VH | High (NAM already covers much) |
| Region view | Detour | Save, demand, traffic | H | Medium |
| App shell | Detour, then our own | Framework, UI | M | Medium |

## Verification

| Technique | Applies to | How |
|---|---|---|
| Deterministic lockstep replay | Simulators | Hook time sources, capture RNG, fixed tick stepping; replay the same save and inputs in original vs ours; diff state per tick |
| In-memory state diff | Sims, networks, lots | Dumpers read original struct layouts directly, without needing the save format |
| Save diffing | Save/load, sims | Byte-identical saves where possible, semantic diff otherwise; round-trip checks |
| Function-level differential | Pure functions | Registry compare mode; fuzz inputs taken from recorded sessions |
| Screenshot diff | Renderer, UI | Scripted cameras on fixed cities; on real Windows (Wine adds noise) |
| Plugin regression suite | Everything | Pinned plugin set (NAM 50, sc4fix, graphics-options, submenus); must pass with each subsystem toggled ours or original |

All harnesses, recorded sessions and golden saves live in the repo and run from a fresh clone.

## Top risks

1. **Patch collisions with ecosystem DLLs.** They patch fixed addresses, often mid-function; replacing a function silently disables those patches. Mitigation:
   - Scan the code ranges we own at startup for foreign modifications.
   - Keep a per-DLL compatibility manifest (defer / absorb / chain).
   - Engage the DLL authors early.
   - Offer a registry API so new DLLs hook through `core.dll`.
2. **NAM dependence.** NAM 50 requires the NAM DLL's rule injection. Networks go late (P7) and only jointly with the NAM team.
3. **Undocumented simulator state.** P4 front-loads it; in-memory dumpers work around it.
4. **Non-determinism in the original game.** Prove determinism first, or fall back to statistical parity.
5. **ABI, heap and STL mismatches.** Follow the ABI rules above; use guard pages in debug builds.
6. **Testing under Wine.** Authoritative parity testing happens on real Windows.
7. **Legal.** Never ship assets or binaries; write code from understanding.
8. **Scope / bus factor.** Every phase ships standalone value, and the P4 documentation outlives the code.
