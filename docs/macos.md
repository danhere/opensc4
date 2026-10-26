# Running on macOS

Written 2026-10-26. Nothing here has been started; this page records what a Mac build would take, so the work can be planned when it's wanted.

## Where things stand

- **No native Mac build is possible today.** All of OpenSC4 runs inside the original 32-bit Windows exe (`core.dll`, injected by the launcher). About 28% of the game's functions are ours ([ROADMAP.md, "Progress"](../ROADMAP.md#progress)); the rest is still the exe's x86 code.
- **Current Macs can't run 32-bit code natively**, and Apple Silicon isn't x86. The only way to run the game there today is Wine (CrossOver, Whisky) with x86 emulation (Rosetta 2).
- **Mac is not a dev or test machine** ([decisions.md, "Platform roles"](decisions.md#platform-roles), [dev-environment.md](dev-environment.md)). Wine on macOS is never a parity reference.

## Two routes

| Route | What it is | Distance |
|---|---|---|
| A. Wine on Mac | The Windows game plus `core.dll`, run in CrossOver or Whisky | Close: untested, but nothing in the design rules it out |
| B. Native Mac app | Our own 64-bit macOS executable, no original exe | Far: needs exe independence (P9) first |

## Route A: Wine on Mac

The D3D11 renderer was picked partly for its Wine path ([ROADMAP.md](../ROADMAP.md), P1 backend decision). On a Mac, D3D11 goes through D3DMetal (CrossOver/Whisky) or DXVK on MoltenVK.

Steps:
1. Install SC4 Deluxe 1.1.641 (GOG is easiest under Wine) in a CrossOver or Whisky bottle with 32-bit support.
2. Copy in a release build of the launcher and `core.dll` (built on Windows as usual).
3. Start the game through the launcher and check: injection works, the D3D11 renderer draws, windowed and borderless modes, input, sound, a save and load.
4. Write down what breaks and whether it's ours or Wine's. Fix ours; note Wine's in this page.

Things likely to need care:
- The launcher starts the game suspended and injects the DLL. Wine supports this, but Steam's DRM relaunch under Wine may not go through it; GOG avoids that.
- Code that patches the exe in memory (`VirtualProtect` in ~100 files) works under Wine, but Rosetta's x86 translation of patched code is a possible source of bugs.
- Crash dumps (`MiniDump`) may not work under Wine.

## Route B: native Mac app

Each step depends on the one before.

1. **Replace the rest of the game.** Today's gaps: the 2D interface (~30% ours), files and saves (~40%), the framework (~5%), the Lua 5.0 library itself, and EA's sound library (AEMS, 0x00864D10 to 0x00867A00), which [audio.md](audio.md) fences off as never rewritten. A native build needs our own replacement for AEMS, reading the same sound data.
2. **Exe independence (P9).** Host mode first (our exe maps the original's sections), then the coverage meter shows zero original code running. See ROADMAP.md, P9.
3. **Platform layer.** Put Win32 calls behind our own interface ([decisions.md](decisions.md), "Native macOS/Linux build": keep this cheap by starting early). Today 218 files include `windows.h` directly. Covers windows, input, files and paths, threads and timers, the registry, crash reporting.
4. **Drop the hooking machinery.** With no exe to patch, the detours, `VirtualProtect` patches, launcher and DLL injection go. Hand-written code that matches the exe's calling conventions (`__thiscall` / `__fastcall`, ~500 files) becomes plain C++.
5. **64-bit build.** Needed anyway for 16 km cities (ROADMAP.md, "64-bit build after exe independence"). Expect pointer-size fixes wherever save records or structs assumed 32-bit layouts.
6. **Mac renderer backend.** D3D11 sits behind an internal render interface. Add Metal, or Vulkan and use MoltenVK on Mac (one backend for Linux and Mac).
7. **Mac toolchain and packaging.** A CMake preset for Apple clang (arm64 and x86_64), an app bundle, code signing and notarization, a way to point at the player's own SC4 data files.
8. **DLL mods.** Plugin DLLs (NAM's DLL and others) are Windows x86 code and can't load on a native Mac build. They'd need Mac builds by their authors, or our own versions of what they do.

## Smallest useful next steps

- Route A test on a Mac (one thread, needs a Mac with CrossOver or Whisky).
- Start the platform layer in new code now, so step 3 shrinks instead of grows.
