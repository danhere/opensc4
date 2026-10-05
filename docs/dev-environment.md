# Development Environment

## Machine roles

| Machine | Role | Why |
|---|---|---|
| **Windows (x86-64)** | **Primary development and parity testing** | Runs the 32-bit game natively; real debuggers (Visual Studio, x64dbg, WinDbg) attach directly; the authoritative reference for behaviour and screenshot diffs |
| **CachyOS** | Playing; Wine/Proton compatibility target | x86-64 Linux runs the 32-bit game under Wine/Proton with no CPU emulation. The NAM DLL lists Linux as supported. Can also cross-compile and run CI. |
| **macOS** | Docs and source analysis only | No dev tools installed by choice; work here is limited to what needs only git and reading files (e.g. the [patch census](patch-census.md)). On Apple Silicon, running a 32-bit x86 Windows exe means Wine plus x86 emulation anyway: slow, and poor for debugging. Not a build or test machine. |

CachyOS is closer to Windows than macOS is for this project: same CPU architecture, mature Wine/Proton, and Wine bugs there are what Linux players will actually hit.

## Requirements

- SimCity 4 Deluxe, **build 1.1.641** (Steam, GOG or EA app).
- A test plugin set pinned to known versions: NAM 50 (NAM DLL), sc4fix, sc4-graphics-options, submenus.

### The Steam build

- The Steam `SimCity 4.exe` (file version 1.1.641.0) is wrapped in Steam's DRM stub: an extra `.bind` section holds the entry point (0x7C82ED). `.text` is **not** encrypted, so the file on disk can be analysed directly.
- Started outside Steam, the stub hands off to Steam and exits, and Steam starts a fresh copy without `core.dll`. The launcher sets `SteamAppId`/`SteamGameId` (24780) when they aren't already set, which is what Steam itself does, so the game keeps running in the injected process. Steam still checks ownership.
- Never modify or strip the exe (see [architecture.md](architecture.md), "Loader").

## Running in the game

- `scripts/deploy-plugin.sh <build dir>` installs `core.dll` as `OpenSC4.dll` in a Plugins folder (default: the user's; `SC4_PLUGINS` overrides it) so the game calls its GZCOM director; `--remove` uninstalls. Launch with `opensc4.exe --core <that OpenSC4.dll>` so the injected and plugin-loaded copies are the same module.
- `scripts/run-autotest.sh <build dir> [VAR=value...]` runs the game unattended: it loads a city, runs the simulation, quits without saving and prints `core.dll`'s log. It uses an isolated `-UserDir` (`%LOCALAPPDATA%\opensc4\testenv\userdir`, seeded with a copy of one region) whose Plugins folder holds only `OpenSC4.dll`, so the user's own plugins and saves are never involved. Variables are documented in `src/core/autotest.h` and the script header.
- Build the plugin from a clone with submodules: `git submodule update --init` fetches gzcom-dll into `third_party/`.

## Address database and hooks

- Every game address OpenSC4 relies on lives in [`data/addresses-641.toml`](../data/addresses-641.toml): name, address, kind, calling convention, signature, the first bytes, and how it was identified. The build generates `opensc4/addresses.h` from it (`tools/gen_addresses.py`, Python 3.11+); C++ code uses `opensc4::addr::<name>` constants instead of literal addresses.
- `python3 tools/gen_addresses.py --check data/addresses-641.toml "<SimCity 4.exe>"` checks every entry's bytes against an exe; `scripts/ghidra-setup.sh` runs it too.
- In the game, `core.dll` logs a manifest after all plugins have loaded: which entries still have their expected bytes, and for patched ones, which module the patch jumps into.
- Hooks (`src/core/hook.h`) go through MinHook, only at `function` entries from the database, and are refused when the entry's bytes don't match (a different build, or another DLL got there first). Detours for `__thiscall` functions are written as `__fastcall` free functions (`this` in ECX, unused EDX).
- `OPENSC4_TRACE=1` installs trace hooks (currently `cSC4App::LoadCity`) that log calls and pass them through.

## Crashes

- SimCity 4 catches crashes in its main loop with its own `__except` handler (it installs no top-level filter) and writes `SimCity Exception Report …txt` plus a minidump to the **real** `Documents\SimCity 4\Exception Reports`, even under `-UserDir`.
- `core.dll` adds (`src/core/crash.h`): a first-chance handler that logs any fatal exception raised in OpenSC4's own code and writes `opensc4-crash-<time>-<pid>.dmp` next to `opensc4.log` before the game's handler runs; a top-level filter for crashes nothing else catches; and a guard for hook entry points that logs which hook an exception passed through. Dumps use the system `dbghelp.dll`, not the XP-era copy in the game folder.
- `OPENSC4_TEST_CRASH=1` makes an autotest run crash on purpose inside a guarded section, to check the whole chain.

## Ghidra

- `scoop install ghidra` (extras bucket). Ghidra needs JDK 21 or newer. If an older `java` comes first on `PATH`, let Ghidra pick a JDK and remember it per Ghidra version (saved in `%APPDATA%\ghidra\ghidra_<version>_PUBLIC\java_home.save`):

  ```bash
  g="$HOME/scoop/apps/ghidra/current"
  java -cp "$(cygpath -w "$g/support/LaunchSupport.jar")" LaunchSupport "$(cygpath -w "$g")" -jdk_home -save
  ```

- Keep Ghidra projects **outside the repo** (for example `%LOCALAPPDATA%\opensc4\ghidra`). They contain the game's code and must never be committed.
- **`scripts/ghidra-setup.sh`** builds the project and runs the whole pipeline (about 4 minutes from scratch; `--fresh` starts over): import with auto-analysis, the class censuses (before anything is renamed, so the numbers in [class-creation.md](class-creation.md) are reproducible), the exe's ID-to-name table, gzcom-dll's interface slot table, sc4-ghidra-symbols (Function ID databases and data type archives), and names for 197 classes' factories, constructors, vtables and interface methods (about 1,600 functions). Needs `python3` (`scoop install python`) and a clone of [sc4-ghidra-symbols](https://github.com/0xC0000054/sc4-ghidra-symbols) (`SC4_GHIDRA_SYMBOLS`).
- Pitfalls the script works around:
  - `analyzeHeadless.bat` fails on paths containing parentheses, such as `Program Files (x86)`, so it imports a copy of the exe.
  - From Git Bash, `.bat` arguments reach `cmd.exe` unquoted (breaking on spaces), and `\\wsl.localhost\...` paths don't work, so it uses 8.3 short paths and local copies of the scripts and symbol files.
  - Attached Function ID databases are remembered in Ghidra's user settings and are then applied by every later auto-analysis.
- Individual scripts in `tools/ghidra` document their own arguments; `LookupNames.java <address...>` prints what the project knows about addresses.

## Toolchain (same on Windows and CachyOS)

| Tool | Purpose |
|---|---|
| clang-cl + lld-link (LLVM) | Compiler and linker, target `i386-pc-windows-msvc` |
| CMake + Ninja | Build system |
| Python 3.11+ | Generates `addresses.h` from the address database; analysis tools in `tools/` |
| xwin | MSVC CRT and Windows SDK, on every host, so all machines build against the same headers and libraries |
| Ghidra | Reverse engineering, with sc4-ghidra-symbols type archives and Function ID databases |
| Git | Version control |

No Visual Studio install is needed to build. The static release CRT (`/MT`) is used in every configuration, so the outputs import only `KERNEL32.dll`.

### Windows

- [Scoop](https://scoop.sh): `scoop install llvm cmake ninja python ./tools/scoop/xwin.json`, run from the repo root.
  - The xwin manifest lives in this repo because xwin is not in a public bucket. Keep the `./`: without it scoop reads `tools` as a bucket name.
  - Scoop's `llvm` adds its `bin` folder to `PATH` instead of creating shims, so open a new terminal after installing it.
- x64dbg (32-bit) or the Visual Studio debugger for attaching to `SimCity 4.exe`.

### Building

From Git Bash (or any POSIX shell on Linux):

```bash
scripts/fetch-sdk.sh            # once; accepts Microsoft's license for the CRT/SDK
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

`scripts/fetch-sdk.sh` and the toolchain file both read `OPENSC4_WINSYSROOT`; it defaults to `.xwin/sysroot` in the repo.

**Sources in WSL, building on Windows.** CMake's link step runs through `cmd.exe`, which can't use a `\\wsl.localhost\...` working directory. Keep the SDK and build directory on a Windows drive:

```bash
export OPENSC4_WINSYSROOT="$LOCALAPPDATA/opensc4/xwin/sysroot"
cmake --preset debug -B "$LOCALAPPDATA/opensc4/build/debug"
cmake --build "$LOCALAPPDATA/opensc4/build/debug"
```

### CachyOS

- `pacman`: `clang`, `lld`, `llvm`, `cmake`, `ninja`, `wine` (or Proton via Steam).
- `xwin` (cargo or AUR) to fetch the Windows SDK/CRT. Run `scripts/fetch-sdk.sh` on Linux itself, not over a Windows-made splat: on a case-sensitive file system xwin adds the case-variant symlinks that the SDK's mixed-case includes need.
- Reference recipe: the NAM DLL's `Makefile`.

### macOS

- Nothing installed beyond git. No builds, Ghidra or game runs here.

## Testing policy

- **Parity and screenshot diffs:** real Windows only.
- **Smoke tests:** CachyOS under Wine/Proton, to catch Wine-specific issues early.
- Wine on macOS is never treated as a reference.
