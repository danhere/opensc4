# Uncharted code and the true function total

A survey of the three address blocks that had almost no address-database entries (0x00830000 to 0x00863FFF, 0x008C0000 to 0x008FFFFF, 0x00940000 to 0x0095FFFF), and of the whole `.text` section, to settle how many of the exe's functions are Maxis code we would translate. Survey only: nothing here is translated, and no game was run for it. Measured 2026-10-19 on main at 4e32fd5.

## Summary

- Ghidra's project has **34,557 functions** in `.text` (0x00401000 to 0x00A7FA23), one more than the 34,556 that `tools/progress.py` and the coverage meter use.
- **4,491** of them are compiler-generated stubs and **2,593** are third-party libraries the project keeps as they are. That leaves **27,473 functions of Maxis code** (game and GZ framework), 5.7 MB of the 6.4 MB.
- The three blocks hold 2,905 functions. Only 783 of them (block 1) are one thing, and it is a dead end: span routines of a software rasterizer, of which 10 ever ran. Block 2 is seven different things, a third of it the Lua runtime. Block 3 is the GZ framework's property sets, its UI script tree and the stock `GZWin` widgets.
- With the honest denominator the same counter gives **21.3%** (5,839 of 27,473), not 17.5%. Fixing the counter's wording problem as well (about 2,260 placeholder entries the docs call ours) would put it near **29%** (inferred, not re-measured).

## Method

Read-only headless Ghidra scripts over the project in `%LOCALAPPDATA%\opensc4\ghidra` (kept out of the repo, since their output describes the game's code):

- one row per function in `.text` (thunks to imports left out): size, name, number of calls and data references to it, the strings, imports and named functions it uses;
- the first instructions and first four references of each function;
- for each candidate range, every reference into it from outside and every call out of it.

"Ran" counts come from the union of the 14 coverage CSVs in `%LOCALAPPDATA%\opensc4\analysis` (Making Money, Big City and Rush Hour, 0 to 1,095 days): 12,403 functions ran at least once. The meter is armed after startup, so code that runs only while the game starts counts as not run.

Two signals did most of the grouping. Strings, imports and Function ID names say what a range is. **Alignment** says which toolchain built it: game code (0x00401000 to 0x0086FFFF) and the libraries start every function on 16 bytes (96 to 100% per 64 KB), while the GZ framework from about 0x00874000 to 0x009DC72F packs them (4 to 30%). That boundary is how the end of AEMS and the start of STLport were placed.

## The three blocks

| Range | Functions | Ran | What it is |
|---|---:|---:|---|
| **Block 1** 0x00830000 to 0x00863FFF | 783 | 10 | Span routines of a 16-bit software rasterizer (below) |
| **Block 2** 0x008C0000 to 0x008D01EF | 361 | 202 | Lua 5.0 runtime, the end of it (lstrlib, ldblib, lmathlib, liolib, lbaselib, lgc, ltable, lparser, lcode). Third-party; [lua.md](lua.md) already keeps it |
| 0x008D01F0 to 0x008D2993 | 27 | 8 | Pixel and picture conversion helpers without calls of their own, called by `UIPictureTextures::Upload`, the drivers and game code (purpose inferred from callers) |
| 0x008D2994 to 0x008D7FFF | 144 | 39 | GZSound's music player: radio stations, playlists (`.m3u`, `.pls`), `Res\Music\Radio\`, `Welcome.wav`. 15 are this-adjustor thunks. 138 are ours (`audio.music.part1` to `part4`, [audio.md](audio.md), "Music player") |
| 0x008D8000 to 0x008DA8BF | 19 | 3 | `GZBuffer` 2D blits (`TileBlit` 0x008D8BC0, [renderer.md](renderer.md)) |
| 0x008DA8C0 to 0x008F47FF | 359 | 5 | The stock **Software** graphics driver (vtable 0x00ACC4D8; `GDriverClass--Software`, `Maxis 3D GDriver Software 1.0`), its vertex pipeline (one 10.8 KB dispatcher at 0x008EFFF0 over ~100 variants) and its DirectDraw/DrawDib presentation. A few hash-table helpers folded in by the linker sit at 0x008EB800 to 0x008EC5FF (0x008EBA60 has 198 callers) |
| 0x008F4800 to 0x008F67FF | 99 | 0 | Unidentified: no strings, no imports, never ran |
| 0x008F6800 to 0x008FFFFF | 354 | 117 | Start of the HTML viewer: HTTP cache and cookies (`CookieData.ini`, `Maxis Network Cache\`), the `HTML Window`, then the parser. The viewer continues to about 0x0090BFFF (734 functions in all, 283 ran: the parser and layout run, the cache and cookies do not) |
| **Block 3** 0x00940000 to 0x00941FFF | 96 | 23 | Framework helpers (text encoding 0x00940DA8, `TKDT` serialisation); 33 are thunks |
| 0x00942000 to 0x00947FFF | 215 | 106 | The property set's XML reader and writer, a shuffle bag, an XML parser with its text tokenizer, the base `QueryInterface` at 0x009457C6. The property set itself and most of its values' code are below the block, from 0x00939040 ([gzui.md](gzui.md)) |
| 0x00948000 to 0x0094BFFF | 205 | 71 | Containers and the UI script tree (`_children`, `_define`, `_null`; list node 0x0094B33A); 29 are thunks |
| 0x0094C000 to 0x0095FFFF | 243 | 84 | The framework window (AddRef 0x0094C47C) and the stock widgets with the readers and writers of their UI-script properties: TextTicker, TextEdit, ListBox, Combo, Btn, BMP, Slider, Scrollbar, LineInput, Spinner, Outline, Gen, FileBrowser, OptGrp, TreeView, Grid. Continues past 0x00960000 |

So the blocks are 2,905 functions: 361 third-party, 119 compiler-generated thunks, 2,425 Maxis code.

### Block 1: the software rasterizer

783 functions, 204 KB, cdecl, no strings, no imports. Each fills or blends horizontal spans of 16-bit pixels (1555 and 565 formats) for one combination of source format, blend mode, colour key and scaling; the decompiled ones are loops over `(x, length)` pairs writing `ushort`s. They are reached only through 0x30-byte tables of function pointers at 0x00AC17D0 to 0x00AC26FF, which only the module's own code reads.

The whole module is 0x0082B780 to 0x00864B5F (916 functions): a class with vtable 0x00AC16C8 (constructor 0x0082C9A0, made by the factory 0x0082B980 listed at 0x00AC167C) that picks a routine and runs it, then the span routines. A companion of 26 functions at 0x00A64110 to 0x00A66741 uses the same tables. In the 14 coverage runs the class runs (63 of its functions) but only **4 of the ~770 span variants** do (0x00854E10, 0x008551D0, 0x0085B030, 0x0085B550). Which feature draws through it is not settled; the city view does not.

Translating it function by function would be the worst value in the exe. The useful move is to find the caller, and either write the four formats that run or replace the class with a small rasterizer of our own.

## Other near-empty areas

The survey's three blocks are not the only gaps. Ranges with few entries for their size:

| Range | Functions | Ran | What it is |
|---|---:|---:|---|
| 0x00810000 to 0x0082B77F | 529 | 99 | Network layer: FTP (`PORT`, `RETR`, `CWD`), HTTP (`cRZNetCommandGenerator`), sockets, RAS dial-up |
| 0x00864B60 to 0x008747D3 | 333 | 19 | **AEMS, EA's sound library.** [audio.md](audio.md) gives 0x00864D10 to 0x00867A00 (30 functions); the call graph and the alignment change say it runs to about 0x008747D3 (inferred): calls into the range come only from the game's AEMS event system (0x004E1xxx), the framework's sound code and static initializers |
| 0x00894000 to 0x008A7FFF | 380 | 176 | Font engine (`*.mxf`, `*.otf`, `*.pfb`, `*.tt?`, Type 1 `eexec` and `/BlueValues` parsing, `uni%04X`). Could be a third-party rasterizer (not checked) |
| 0x009DC730 to 0x00A07FFF | 1,049 | 162 | STLport 4.6 iostreams and locales, then the MSVC C runtime |
| 0x00A08000 to 0x00A6410F | 600 | | EA's Gimex image library with libpng 1.0.5, zlib 1.1.3, a JPEG codec, an IJL loader (`ijl15.dll`) and EAGL texture formats |
| 0x00A66742 to 0x00A6EE4F | 91 | | `CMpegBase` MPEG audio decoder, then the rest of the C runtime |
| 0x00A6EE50 to 0x00A7FA23 | 1,623 | 0 | The compiler's tail: static initializers, their `atexit` destructors, exception handler stubs (they run before the meter is armed or at exit) |

## The true total

| Kind | Functions | How it was told apart |
|---|---:|---|
| This-adjustor thunks (`sub ecx, N; jmp`, `add ecx, N; jmp`) | 2,474 | whole body is the two instructions |
| Virtual-base thunks (`sub ecx, [ecx-N]; ...; jmp`) | 387 | same, 24 bytes at most |
| Static initializers | 772 | listed in the initializer table 0x00B07004 to 0x00B07C20 |
| Their `atexit` destructors | 601 | referenced only by a static initializer |
| Exception handler stubs (`mov eax, FuncInfo; jmp 0x009EF0F4`) | 85 | body pattern |
| Other static teardown in the compiler's tail | 172 | address (0x00A6EE50 on) |
| **Compiler-generated** | **4,491** | |
| AEMS | 333 | 0x00864B60 to 0x008747D3 |
| Lua 5.0 runtime | 527 | 0x008B96F0 to 0x008D01EF, less the thread table and tag library that are ours ([lua.md](lua.md)) |
| STLport and the C runtime | 1,042 | 0x009DC730 to 0x00A07FFF |
| Gimex and its codecs | 600 | 0x00A08000 to 0x00A6410F |
| MPEG decoder and the C runtime's tail | 91 | 0x00A66742 to 0x00A6EE4F |
| **Third-party** | **2,593** | |
| **Maxis code** | **27,473** | the rest |

What is left out of the third-party line, so the 27,473 is if anything a little high: STLport templates instantiated inside game code (container helpers, which the project keeps as the game's), CRT functions linked outside the two CRT ranges, and the font engine if it turns out to be a library.

Judgment calls that are not in the table, with their sizes, in case the measure should leave out code that never runs in our setup: the Software driver (344 functions, 5 ran), the rasterizer's span variants (783, 10 ran), the network layer's FTP/HTTP/RAS (529, 99 ran) and the HTML viewer's cache and cookies. The stock DirectX and OpenGL drivers are already replaced wholesale by ours when it is on ([renderer.md](renderer.md)).

## What it does to the percentage

The address database has 9,979 function entries. Matched against the classes above: 9,153 are Maxis code, 510 compiler-generated, 174 third-party and 142 not at a Ghidra function start. Of the 6,064 that `progress.py` counts as ours, 5,839 are Maxis code.

| Measure | Ours | Of | Share |
|---|---:|---:|---:|
| `progress.py` today | 6,064 | 34,556 | 17.5% |
| Same counter, Maxis code only | 5,839 | 27,473 | 21.3% |
| Also counting the placeholder entries the docs call ours (about 2,260, from the 2026-10-20 survey, inferred) | about 8,100 | 27,473 | about 29% |

`tools/progress.py` now divides by the Maxis count and leaves entries in the third-party and compiler-tail ranges out of its numerator (2026-10-20: 6,141 of 27,473, 22.4%). Still to do: count an entry as ours by its toggle group rather than by the wording of its `source`.
