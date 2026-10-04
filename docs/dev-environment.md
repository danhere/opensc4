# Development Environment

## Machine roles

| Machine | Role | Why |
|---|---|---|
| **Windows (x86-64)** | **Primary development and parity testing** | Runs the 32-bit game natively; real debuggers (Visual Studio, x64dbg, WinDbg) attach directly; the authoritative reference for behaviour and screenshot diffs |
| **CachyOS** | Playing; Wine/Proton compatibility target | x86-64 Linux runs the 32-bit game under Wine/Proton with no CPU emulation. The NAM DLL lists Linux as supported. Can also cross-compile and run CI. |
| **macOS** | Reverse engineering, docs, cross-compiling | Ghidra and clang-cl + xwin work fine. On Apple Silicon, running a 32-bit x86 Windows exe means Wine plus x86 emulation: slow, and poor for debugging. Not a test target. |

CachyOS is closer to Windows than macOS is for this project: same CPU architecture, mature Wine/Proton, and Wine bugs there are what Linux players will actually hit.

## Requirements

- SimCity 4 Deluxe, **build 1.1.641** (Steam, GOG or EA app).
- A test plugin set pinned to known versions: NAM 50 (NAM DLL), sc4fix, sc4-graphics-options, submenus.

## Toolchain (same on every machine)

| Tool | Purpose |
|---|---|
| clang-cl + lld-link (LLVM) | Compiler and linker, target `i386-pc-windows-msvc` |
| CMake + Ninja | Build system |
| xwin | Windows SDK/CRT headers and libraries on Linux/macOS (not needed on Windows) |
| Ghidra | Reverse engineering, with sc4-ghidra-symbols type archives and Function ID databases |
| Git | Version control |

### Windows

- Visual Studio Build Tools (Windows SDK) plus LLVM. Use clang-cl, not cl.exe, so builds match the other machines.
- x64dbg (32-bit) or the Visual Studio debugger for attaching to `SimCity 4.exe`.

### CachyOS

- `pacman`: `clang`, `lld`, `llvm`, `cmake`, `ninja`, `wine` (or Proton via Steam).
- `xwin` (cargo or AUR) to fetch the Windows SDK/CRT.
- Reference recipe: the NAM DLL's `Makefile`.

### macOS

- Homebrew: `llvm` (ships clang-cl and lld), `cmake`, `ninja`.
- `xwin` for the Windows SDK/CRT.

## Testing policy

- **Parity and screenshot diffs:** real Windows only.
- **Smoke tests:** CachyOS under Wine/Proton, to catch Wine-specific issues early.
- Wine on macOS is never treated as a reference.
