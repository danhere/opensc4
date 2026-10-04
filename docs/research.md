# Research

Findings from the 2026-10-04 feasibility research. Re-verify before relying on anything time-sensitive.

## Existing reimplementation efforts

| Project | What | Status |
|---|---|---|
| [OpenSC4-org/OpenSC4](https://github.com/OpenSC4-org/OpenSC4) | Godot/GDScript viewer: DBPF, terrain, S3D, region view | Dormant since 2024-03; no simulation; **no license** |
| [nsgomez/scion](https://github.com/nsgomez/scion) | C++03 reimplementation of Maxis's GZ/RZ framework aiming for matching machine code | Framework work in progress, all else not started; dormant since 2025-06 |
| [snow-developments/open-sc4](https://github.com/snow-developments/open-sc4) | D-language stub | Effectively empty |
| [KillaVolt/SimCity-4-Deluxe-GOG-Decompiled](https://github.com/KillaVolt/SimCity-4-Deluxe-GOG-Decompiled) | Ghidra output for GOG 1.1.641 | **Bundles extracted game assets. Avoid.** |

No SC4 reimplementation has a running simulation.

## Codebase assessment

| Repo | License | Fit | Notes |
|---|---|---|---|
| [nsgomez/gzcom-dll](https://github.com/nsgomez/gzcom-dll) | LGPL-2.1+ (relicensed from MIT 2025-09-28) | **Dependency** | The SDK every SC4 DLL builds on (290 headers). In practice maintained by 0xC0000054. No tests. |
| [0xC0000054/sc4-ghidra-symbols](https://github.com/0xC0000054/sc4-ghidra-symbols) | Apache-2.0 | **RE tooling** | Types from the Mac debug symbols; **no Windows addresses**. Function ID databases show the Windows build mixes VC7 and Intel C++ 6 and uses STLport 4.6 and Lua 5.0. |
| [0xC0000054/sc4-dll-utilities](https://github.com/0xC0000054/sc4-dll-utilities) | LGPL-2.1+ | **Dependency** | Patcher (RAII VirtualProtect, jmp/call install), version detection, logging |
| [0xC0000054/sc4-gzids](https://github.com/0xC0000054/sc4-gzids) | - | RE tooling | Class and interface ID constants |
| [aspctt/scvk](https://github.com/aspctt/scvk) | LGPL-2.1+ | Reference | Vulkan `cIGZGDriver` through the GZCOM class-ID seam. One author, two months old, ~6 fps scrolling, no tests. |
| [nsgomez/scgl](https://github.com/nsgomez/scgl) | LGPL-2.1 | Reference | OpenGL 3 `cIGZGDriver` |
| [0xC0000054/sc4-render-services](https://github.com/0xC0000054/sc4-render-services) | LGPL-2.1 | Reference | Call-site rel32 patching, DX7 vtable hook, ImGui overlay, custom GZCOM service interfaces |
| [NAMTeam/nam-dll](https://github.com/NAMTeam/nam-dll) | LGPL-3.0 | Reference | ~2.4k lines; the "RUL2 engine" is a small rule injection. **Its Makefile cross-builds with clang-cl + xwin.** |
| [nsgomez/scion](https://github.com/nsgomez/scion) | LGPL-2.1+ | Reference for P9 | Tied to VS.NET 2003 + STLport 4.6 |
| [OpenSC4-org/OpenSC4](https://github.com/OpenSC4-org/OpenSC4) | None | Read only | **Never copy code** |

**Nobody has built these yet:** a function-entry detour layer, a Windows 641 address database, a replacement tracker, differential tests, early-boot injection, or a Wine test harness.

## Reverse-engineering state

- The Mac (Aspyr PPC/x86) builds shipped with debug symbols; the Windows build never did.
- Formats well documented by several libraries: DBPF, exemplars/cohorts, S3D, FSH, LTEXT, RUL0/1/2.
- **Savegame:** a DBPF with a CRC per record. Of 180 subfile types, ~64% have no documented status, including almost every simulator (traffic, demand, RCI, crime, pollution, land value). Plumbing is documented.
- **Rendering:** DX7 era, orthographic. Buildings are baked sprites (4 rotations x 5 zooms x day/night) on box geometry. Hardware rendering above ~2048 px crashes without a wrapper. The UI was built for 1024x768 with no scaling.
- **DLL mods support only Windows build 1.1.641.**

## Modding ecosystem

- **Key people:** nsgomez/simmaster07 (SC4Fix, gzcom-dll, scion); Null 45 / 0xC0000054 / Nicholas Hayes (most DLLs, Ghidra symbols); memo33 (sc4pac, submenus, NAM); the NAM team (Tarkus, memo, jflann, caspervg).
- **NAM 50** (released 2026-08-19) requires the NAM DLL.
- **sc4pac** is the community package manager.

## AI use in the community (as of 2026-10)

- No SC4 project has an AI policy; no maintainer has made a public AI statement. The core maintainers write their code by hand.
- **Precedent:** [gzcom-dll PR #39](https://github.com/nsgomez/gzcom-dll/pull/39) ("Generated with Claude Code", Co-Authored-By Claude) was merged by 0xC0000054 on 2026-10-02 without comment. It came with careful issue writing and in-game verification.
- 0xC0000054 also engaged constructively with an AI-found bug in a Paint.NET plugin.
- scvk shows only speculative signs of AI use.
- The Simtropolis guidelines couldn't be read (blocked to automated access). Check them by hand before posting anything.
- In modding communities, AI art generally draws much more backlash than AI-assisted code.

## Legal context

- **OpenRCT2:** began in 2014 as incremental decompilation; never sued; now integrated with RCT Classic. It took ~4 years to stop needing the original exe.
- **OpenTTD:** began in 2003 by converting TTD's assembly to C. In 2026 Atari re-released TTD bundled with OpenTTD on Steam/GOG.
- **EA vs OpenSC2K (2018):** taken down for **bundling SimCity 2000 sprites**, not for its code.
- No known EA action against SC4 DLL or NAM modding in 20+ years.
- EA released C&C source under GPLv3 in 2025: a goodwill signal, not legal cover for SC4, which is still sold.
- **Safe pattern:** code only, original install required, no extracted assets or redistributed binaries.

## Current state of fixes in the original game

People stack 3-5 separate fixes:

- Launch flags `-CustomResolution:enabled -rWxHx32`, or the sc4-graphics-options DLL.
- dgVoodoo2 for resolutions above 2048 px.
- The UI-scaling DLL (2026-08, needs dgVoodoo).
- Single-core CPU affinity.
- The 4GB patch (recommended, no longer required after NAM 50).

Everything is tied to one Windows exe build.
