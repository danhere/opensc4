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

## Toolchain (same on Windows and CachyOS)

| Tool | Purpose |
|---|---|
| clang-cl + lld-link (LLVM) | Compiler and linker, target `i386-pc-windows-msvc` |
| CMake + Ninja | Build system |
| xwin | MSVC CRT and Windows SDK, on every host, so all machines build against the same headers and libraries |
| Ghidra | Reverse engineering, with sc4-ghidra-symbols type archives and Function ID databases |
| Git | Version control |

No Visual Studio install is needed to build. The static release CRT (`/MT`) is used in every configuration, so the outputs import only `KERNEL32.dll`.

### Windows

- [Scoop](https://scoop.sh): `scoop install llvm cmake ninja ./tools/scoop/xwin.json`, run from the repo root.
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
