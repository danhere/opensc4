# Third-Party Code

Git submodules, pinned to the commits recorded in the superproject. Fetch them with `git submodule update --init`. Only the sources listed in [CMakeLists.txt](CMakeLists.txt) are compiled, and they are linked statically into `core.dll`.

| Directory | Project | Version | License | Used for |
|---|---|---|---|---|
| `gzcom-dll` | [nsgomez/gzcom-dll](https://github.com/nsgomez/gzcom-dll) | 70dc199 (2026-10-04) | LGPL-2.1-or-later | GZCOM interfaces, `cRZCOMDllDirector`, `SC4String`, system service base |
| `minhook` | [TsudaKageyu/minhook](https://github.com/TsudaKageyu/minhook) | v1.3.4 | BSD-2-Clause (MinHook and Hacker Disassembler Engine 32) | Inline detours at function entry |
| `freetype` | [freetype/freetype](https://github.com/freetype/freetype) | VER-2-13-3 | FreeType License (FTL), chosen over its GPLv2 option | Glyphs at the physical size for crisp interface text (`renderer.interface.sharp_text`); modules listed in `freetype-config/opensc4_ftmodule.h` |

Binary releases must carry these licenses: the BSD notices for MinHook and HDE (`minhook/LICENSE.txt`), the LGPL terms for gzcom-dll, and FreeType's credit (`freetype/docs/FTL.TXT`: "Portions of this software are copyright © The FreeType Project (www.freetype.org). All rights reserved.").
