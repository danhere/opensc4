# Pixel helpers and blits (0x008D01F0 to 0x008D2993, 0x008D8760 to 0x008DA8BF)

Two blocks of the GZ framework that [uncharted-code.md](uncharted-code.md) left without entries: leaf helpers for vertex formats and pixels, and the 2D blits of `GZBuffer` with their neighbours. 45 of their 46 functions are ours, in nine groups, all off by default and installed at Attach. Still the game's: 0x008D2320 (below).

| Group | Functions | What |
|---|---:|---|
| `renderer.pixels.vertex` | 6 | vertex formats: legacy IDs expanded 0x008D01F0, a format from a list of element types 0x008D02C0, stride 0x008D0370, an element's offset 0x008D04A0 and count 0x008D04F0, one element of n vertices gathered 0x008D0520 |
| `renderer.pixels.key` | 5 | copies that clear the pixels matching a colour key: 16-bit 0x008D0660 and 32-bit 0x008D0750 (MMX in the game) and their 1555, 4444, 8888 wrappers 0x008D0820, 0x008D0860, 0x008D08A0 (UIPictureTextures::Upload) |
| `renderer.pixels.modulate` | 6 | pixels times a colour: 8888 0x008D08E0, 888 0x008D09C0, 4444 0x008D0A60, 1555 0x008D0B60, DXT1 0x008D0C60, DXT3 0x008D0E10 (picked from a table by 0x00492CC0) |
| `renderer.pixels.dxt` | 8 | DXT decoders: DXT1 to 8888 0x008D1030, 1555 0x008D1F80, 4444 0x008D2140; DXT3 to 8888 0x008D1A30, 1555 0x008D1240, 4444 0x008D1630; the block writers 0x008D0FD0 (stdcall) and 0x008D1E70 (registers) |
| `renderer.pixels.format` | 1 | the bits per channel of a texture source, 0x008D2790 |
| `renderer.blits.tile` | 4 | a nine-cell frame 0x008D8800, TileBlit 0x008D8BC0, a stretched copy 0x008D9020, a three-part strip 0x008D9550 |
| `renderer.blits.frame` | 4 | on a drawing context: a thick outline 0x008D9360, a bevel 0x008D9470, a filled bevelled panel 0x008D9870 and its 0x00RRGGBB form 0x008D9900 |
| `renderer.blits.fill` | 6 | a buffer's flood fill 0x008DA370, its 8, 16, 24 and 32-bit fills 0x008D9990, 0x008D9BE0, 0x008D9E30, 0x008DA100 (x in EAX), the span push 0x008D9930 |
| `renderer.blits.math` | 5 | a colour shaded by a delta 0x008D8760, the Software driver's class ID 0x008DA490, a colour from four floats 0x008DA4A0, a 4x4 matrix product 0x008DA610, a vector times a matrix 0x008DA830 |

Code: `src/core/pixels.cpp`, `src/core/gzbuffer_blit.cpp`; tests `src/core/pixels_autotest.cpp`, `src/core/gzbuffer_blit_autotest.cpp`; addresses `data/addresses/pixels.toml`, `data/addresses/blits.toml` (TileBlit stays in `region.toml`; `cRZRect::IsEmpty` 0x007C1870 is new in `blits.toml`).

**Not translated:** 0x008D2320, called by 0x007CF780. Not a pixel helper: a driver test that draws 64 white quads for 20 frames, snapshots the 19th through the driver's 0xE69BFE2A interface and counts lit pixels. It needs a live driver and cannot be compared; it belongs with the driver code.

## Vertex formats

A format is a bit field with bit 31 set, a count per element type at the bit positions of the table 0x00ACC0D4 (masks 0x00ACC0FC, sizes 0x00ACC0AC); the legacy IDs 1, 2, 3, 10, 11 and 0x20 to 0x26 expand to fixed fields. Our D3D11 driver already had its own copy (`src/core/gd/vertex_format.cpp`); the detours are separate and follow the game exactly: 0x008D02C0 packs counts unmasked (a count past its field spills into the next), 0x008D0370 adds the second group of elements only when the low nibble is non-zero, 0x008D04F0 shifts arithmetically. Element types outside 0 to 9 read past the game's tables (in 0x008D02C0 they write past its counters on the stack); ours ignore them. No caller passes them.

0x008D04A0 and 0x008D0520 keep a value in ECX across their call to 0x008D01F0, which leaves ECX and EDX alone. Every detour of both files is therefore entered through a thunk that keeps ECX and EDX (`OPENSC4_CDECL_THUNK` in `core/thunk.h`, new; `OPENSC4_THUNK` for the stdcall and thiscall ones).

## Colour keys, modulation, DXT

- The key copies compare `pixel & mask` with `key & mask` (the mask's low word, replicated, for 16 bits) and write 0 for a match. The game does a row from its end in index: EBX from 15 - 2w up, 16 bytes while the add does not carry, then 8, 4 and 2. Ours does the same pieces, reading both halves of a 16-byte piece before writing, so overlapping source and target come out the same.
- The modulates multiply two channels at once (0xFF00FF masks) with rounding constants; ours keep every intermediate in 32 bits. Quirks kept: the 16-bit ones step rows by the pitch less **four** bytes a pixel (as the 32-bit one), so rows drift back by 2w a row; the DXT3 one takes the indices it remaps from the alpha's second dword (+4) and writes the two colours back unswapped after the branch, so only the indices change. 0x00492CC0 calls them with pitch 0.
- DXT decoders: 565 colours widened (8888) or narrowed (555, 444), the third and fourth colours as thirds (0xAB/0x55 or 21/11, 11/5) or the average and 0. DXT3 alpha: top bit of each nibble (1555), the nibble (4444), nibble × 0x11 (8888). The 16-bit ones add alpha and palette entry in 16 bits.
- 0x008D1E70 takes the target in EAX, the indices in ECX, the palette in EDX and the pitch on the stack (ret 4), and leaves EAX three rows on and ECX the last pixel; its thunk leaves them so too.

## Blits

The blits work through vtables: `cIGZBuffer` (slots as in gzcom-dll's header, overloads in MSVC's reverse order: 0x30 the area, 0x50 Fill, 0x54/0x58 Get/SetPixel, 0x74 Blt, 0x78 to 0x84 the colour conversions, 0x88 bits, 0x8C stride), the drawing context a target's interface 0xAB300B2B leads to (its slot 4; context slots 0x28/0x2C begin/end, 0x54 colour, 0x64 pen width, 0x8C fill rect, 0x98 blit, 0x9C tiled blit, 0x0C/0x14/0x24 clip, 0xA0 flush), and a picture's colours (0x86D72B57, slot 4: four bytes). Ours make the same calls with the same arguments in the same order.

- **TileBlit** (0x008D8BC0): a 1 by 1 source fills the target area with its pixel (unless transparent; false if outside the source); a target with a drawing context gets one tiled blit with the shift; else tiles are blitted row by row from the shifted origin, cut to the target's area and the clip. The per-tile clip checks are dead code (the area was already cut to the clip), kept as they are.
- **0x008D9020** stretches in 16.16 steps sampled at their middles, pixel by pixel through RGBA, over the target area cut by 0x0079DD60 to the target's area and the clip (0x007C1870 for empty).
- **0x008D9550** and **0x008D8800** lay three or nine cells: corners copied, edges tiled, the middle tiled when asked.
- **Flood fills**: a scanline fill with spans (row, x1, x2, direction) on a list in the game's allocator (`SC4::stl_allocate`), each node freed as it is popped, the list destroyed with `SC4PairList::Destroy`; ours allocate and free in the same order. The start pushes (y, x, x, 1) and (y + 1, x, x, -1), so the row past the buffer is read (and written where it matches) when the start is on the last row. A colour that matches the start only on the bits compared (the 32-bit fill's 24 bits, the low byte of an 8-bit one's colour) never ends, in the game as in ours.
- **Float helpers**: 0x008DA4A0 clamps to 0..1 (NaN kept) and adds 49152.0f to get v × 256 rounded in the float's low byte, folding 256 to 255. 0x008DA610 and 0x008DA830 sum products in the game's order per element, stored before the next element is read (out may be an input). The game runs the x87 at 53-bit precision; ours sum in doubles and store floats, which gives the same bits.
- Return values: only AL is meaningful. The callers checked (every call site of TileBlit, 0x008D9020, 0x008D9550, 0x008DA370) read nothing above AL or ignore the result.

## Testing

**Compare mode** for the pixel groups and `renderer.blits.fill`/`.math`: ours runs first, what it wrote is kept aside and the memory put back, then the game's runs (its result stays), and results and memory are compared byte for byte. The span push runs the game's alone in compare mode. The tile and frame groups draw through other objects and have none.

**`OPENSC4_TEST_PIXELS=<day>`** (fingerprint ` | px`) calls the 26 pixel functions by address on canvases with a margin all round: every legacy ID and odd bit fields, element lists past their masks, odd widths 1 to 33 (every piece of the key copies), padded, zero, negative and in-place pitches, every DXT colour branch (c0 > c1, <, =, colours made equal by the modulate), every format pair, and the two register functions with their registers checked. Stages: format, make, gather, key, modulate, modulatedxt, decode, blocks, bits.

**`OPENSC4_TEST_BLITS=<day>`** (fingerprint ` | bl`) calls the 19 blits on mocks of our own: buffers, drawing contexts, their interface and a picture's colours, whose vtables record every call (the slot, values, rectangles' contents, mock ids) into the hash and answer by fixed rules; flood fills on real memory with a margin row above and below, four patterns, starts inside, on edges and outside. Stages: shade, floats, matrix, frames, frame, tile, stretch, three, flood, fill, push. A mutation check (one changed rectangle edge in our TileBlit, one in Bevel, one in the fill) changed the hashes it should.

Results, 2026-10-23:

| Check | Result |
|---|---|
| Test runs, groups off vs on | every stage hash the same: `px` 63848321, `bl` 444D8192 |
| Compare mode, both tests and play | 0 divergences in all 26 pixel ids and the 10 comparable blit ids (2 million calls of 0x008D01F0 and 1.5 million of 0x008D04F0 in a short run) |
| Screenshots (`run-shots.sh`, Making Money, all nine groups on) vs the game | 20 of 20 within `--tolerance 32 --max-differing 1.2`; a second run of the game against the first gives the same (raw, 0 of 20 match either way: the harness's noise) |
| Parity `px1-r1` (and `px1-r2` on the final build), 12 saves, 7 days, muted, own folder `p4env-px1`, two jobs, A `determinism.dispatch_info`, B that and the nine groups | 12 same |
| The same with `OPENSC4_TEST_PIXELS=2 OPENSC4_TEST_BLITS=2` | 12 same; `px` 63848321 and `bl` 444D8192 in all 120 runs, both sides |

**Reached** in plain play (all 12 saves): 11 of 45: 0x008D01F0, 0x008D0370, 0x008D04F0, 0x008D0520, 0x008D08A0 (and our 0x008D0750 through it), 0x008D0E10 (11 saves), 0x008D2790, TileBlit, 0x008D8800, 0x008D9550, 0x008DA490 (at startup). The tests reach all 45. The DXT decoders, the other modulates, the flood fills and the float helpers did not run (the DXT decoders and float helpers serve the stock drivers' fallbacks and the Software driver).

**Audit**: each translation was checked against the disassembly by the author and by an independent audit (memory writes and their order, reads, loop bounds, signedness, every formula, argument mapping after each push, virtual calls and their arguments, allocation order, return values, the thunks' byte counts and registers, the compare regions). Differences left, none reachable from a caller: the game writes into its own argument slots (counters, skips; no caller reads them back), element types outside 0 to 9, upper bytes of EAX where only AL is answered, ECX after 0x008D0520 (the game's is the stride; ours the caller's), and the palette read of 0x008D1E70's thunk coming after the writes (differs only if the palette overlaps the block). Also left: bytes the game passes from uninitialised stack slots where a callee might not write its outs (0x008D8800's colour bytes, 0x008D9020's RGBA bytes; ours start at 0), and full dword slots where ours pass clean bytes to callees that read bytes. The floats match only with the x87 at 53-bit precision, which the game keeps; the Software driver under 24-bit precision would differ. Fixed from the audits: the DXT3 to 16-bit decoders read the alpha again before each pixel, as the game does (an in-place decode now matches too); TileBlit's 1 by 1 path starts its red byte from w's slot, as the game's; compare mode snapshots the row past a fill's buffer only when the start is on the last row (the only case the fill touches it). All checks above were run again on the final build: the same hashes, 0 divergences, shots 20 of 20, and parity batch `px1-r2` (both legs) 12 same each, `px` and `bl` as above in all 120 runs.
