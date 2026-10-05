# Third-Party Code

Git submodules, pinned to the commits recorded in the superproject. Fetch them with `git submodule update --init`. Only the sources listed in [CMakeLists.txt](CMakeLists.txt) are compiled, and they are linked statically into `core.dll`.

| Directory | Project | Version | License | Used for |
|---|---|---|---|---|
| `gzcom-dll` | [nsgomez/gzcom-dll](https://github.com/nsgomez/gzcom-dll) | 70dc199 (2026-10-04) | LGPL-2.1-or-later | GZCOM interfaces, `cRZCOMDllDirector`, `SC4String`, system service base |
| `minhook` | [TsudaKageyu/minhook](https://github.com/TsudaKageyu/minhook) | v1.3.4 | BSD-2-Clause (MinHook and Hacker Disassembler Engine 32) | Inline detours at function entry |

Binary releases must carry these licenses: the BSD notices for MinHook and HDE (`minhook/LICENSE.txt`), and the LGPL terms for gzcom-dll.
