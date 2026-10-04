# Decisions

Each entry records what was decided and why. When a decision changes, replace the entry rather than appending to it.

## Approach: incremental replacement inside the original exe (option B)

**Decided.** OpenRCT2-style: inject into SimCity 4 build 1.1.641 and replace subsystems one at a time until the exe is no longer needed.

- **Why:** playable at every step, so subsystems can be targeted as needed; keeps the plugin ecosystem working; avoids having to reverse-engineer all simulator state before anything runs.
- **Rejected:**
  - Clean-room rewrite: ~64% of save and simulator state is undocumented, and OpenMW (a comparable clean-room 3D engine) is still pre-1.0 after ~17 years.
  - Renderer-only front end: too narrow as an end state. It is effectively P1 of this plan.

## Language: C++20

**Decided.**

- **Why:** through P0-P8 our code lives inside SC4's process and must match MSVC's `__thiscall`, vtable layout (with multiple inheritance on every GZCOM interface) and byte-exact struct offsets. gzcom-dll, the Ghidra type archives and every SC4 DLL are C++.
- **Rejected:**
  - Rust for the in-process core: vtables and multiple inheritance would have to be mirrored by hand, and the code would be mostly `unsafe`.
  - Zig: no C++ interop.
  - C: every vtable written by hand.
  - Managed languages: can't live inside a 32-bit MSVC process.
- **Allowed:** Rust (or anything else) for offline tooling: save parser and differ, address database generator, DBPF tools.
- **Revisit:** only after exe independence (P9).

## Compiler: clang-cl + lld-link, MSVC ABI

**Decided.** Target `i386-pc-windows-msvc`. The same compiler on Windows and Linux (xwin supplies the Windows SDK/CRT on Linux), so every build machine produces the same binary. The NAM DLL's Makefile already proves this cross-build works.

- **Rejected:** MinGW, whose C++ ABI (vtable thunks, destructors, RTTI, exceptions) differs from MSVC's.
- **Rule:** no MSVC-only constructs such as `__declspec(naked)` inline asm without a portable macro (the NAM DLL's `NAKED_FUN` pattern).

## License: LGPL-2.1-or-later

**Decided.**

- **Why:** matches gzcom-dll, scvk, scion and sc4-dll-utilities. LGPL was written for libraries loaded into proprietary programs, which is exactly the case of a DLL inside `SimCity 4.exe`.
- **Rejected:** GPL-3, because a GPL DLL loaded into a proprietary exe is legally murky.
- **Condition:** move to LGPL-3.0-or-later only if we take code from the NAM DLL (LGPL-3.0).

## Name: OpenSC4

**Decided (working name).**

- Collides with the dormant [OpenSC4-org/OpenSC4](https://github.com/OpenSC4-org/OpenSC4): a Godot viewer, last commit 2024-03-29, no license, main author Adrien Jaguenet.
- Deferred until the name actually becomes a problem. If the project goes public, reach out to Adrien first as a courtesy.
- Never use "SimCity", "Maxis", "Micropolis" or "Rush Hour" in names; they are EA trademarks or already taken.

## Platform roles

**Decided.** Windows is the primary development, reverse-engineering and parity-testing machine. CachyOS is the Wine/Proton target you'll play on. macOS gets no dev tools; it's used only for docs and source analysis that need just git. See [dev-environment.md](dev-environment.md).

## AI use and community etiquette

**Decided.**

- Disclose AI assistance in commits and PRs.
- Back any upstream contribution with verification in the real game.
- Use other projects' libraries as published, under their licenses; never push AI-assisted changes into their repos uninvited.
- No AI-generated art in anything posted to Simtropolis/STEX until its rules have been read directly.

## Open decisions

| Decision | Options | Recommendation |
|---|---|---|
| How to write replacement code | Transcribe decompiler output (OpenRCT2-style, faster) vs write from understanding (slower, legally cleaner) | Write from understanding, with decompiler output as reference |
| Renderer backend | D3D11 vs Vulkan vs both | D3D11 first |
| When to contact other modders | At P0 vs after P1 | At P0. The patch census needs them, and their trust gates P7. |
| Save compatibility | Read/write original saves forever vs read-only after some phase | Read/write parity through P9 |
| Native macOS/Linux build | Never vs after P9 | Keep Win32 calls behind a platform layer from the start; it's cheap now and expensive later |
