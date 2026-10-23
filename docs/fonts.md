# Fonts

The text engine at 0x00893D2C to 0x008AE76D: a Maxis framework layer (a COM director, the font system, the font, a glyph blitter) on top of **T2K**, the font scaler Sampo Kaasila wrote and Bitstream sold as Font Fusion. It is the "Font engine" line of [uncharted-code.md](uncharted-code.md), which gave it as 0x00894000 to 0x008A7FFF; the code runs a little lower and higher than that.

Everything here was read from the disassembly with Ghidra; the glyph blitter (f4) is ours and was checked in the game ("f4: the glyph blitter is ours" below). Names are ours unless they come from T2K. The addresses the doc relies on are in [data/addresses/font.toml](../data/addresses/font.toml) (only f4 hooked) and [data/names-641.txt](../data/names-641.txt). "Ran" counts come from the 17 coverage files on TurboToaster ([dev-environment.md](dev-environment.md), "Coverage"); code that runs only while the game starts is never recorded there.

## Library or Maxis

Both. 0x0089A578 to 0x008AE76D (321 functions, 82 KB) is T2K; the 230 functions around it are Maxis framework code.

- No vendor strings: no FreeType, Bitstream, Agfa, Monotype, Font Fusion, iType or T2K text and no copyright line anywhere near the range (the exe's only copyright strings are Lua's, zlib's and the IJG JPEG library's).
- The memory manager is T2K's `tsimem.c`, constant for constant: the handler (0xB0 bytes, 0x0089A594) is stamped 0xAB1500FF and 0xAA005501 and has 501 block slots; each block has the header 0xAA53C5AA and the trailer bytes 0x5A 0xF0 (0x0089A670), which the free (0x0089A7C6) checks; an error marks the handler 0x5500AAFF and `longjmp`s (0x0089A578, 19 callers), and the API entry points `setjmp` first. The error codes are T2K's (10000 and up: 0x2718 out of memory, 0x2719 bad block). Identified from those constants against what is known of T2K; there is no source to compare with.
- The rest fits T2K's feature set: TrueType tables (`ttcf`, `head` magic 0x5F0F3CF5, `head`, `hhea`, `maxp`, `loca`, `hmtx`, `OS/2`, `cmap`, `glyf` as pushed tags in 0x0089B4C1 to 0x0089D56A), `CFF `, the Type 1 parser (`eexec`, `/lenIV`, `/Subrs`, `/CharStrings`, `/BlueValues` and the other private dictionary keys, 0x0089FAF7 to 0x008A1834), a 72 dpi transform call with 16.16 metrics.
- The library is packed C (no vtables, `cdecl`, not aligned), compiled with the game: its `malloc`, `setjmp` and `strcmp` are the exe's C runtime. Code after 0x008AE770 is aligned to 16 bytes again and is the game's (Lua glue).

So the range is third-party code the project keeps: `tools/progress.py` lists it in `NOT_MAXIS` and [uncharted-code.md](uncharted-code.md) moves its 321 functions from the Maxis count to the third-party one.

## Class map

The fonts' director (0x00895F7B, vtables 0x00AC5548 and 0x00AC5518, made by its static getter 0x00896012 from the game's director list at 0x004600EE) registers three classes and, at its start (slot 4, 0x00895FCD), makes the font system with interface 0x287FB697 and gives it to the framework as a system service.

| Class ID | Size | Vtables | Constructor | What |
|---|---:|---|---|---|
| 0x20673960 | 0x70 | 0x00AC5488 (26 slots), 0x00AC5454 (+4, 13) | 0x00895268 | **Font system.** The faces found in the game's Fonts folder and the Windows one (`*.tt?`, `*.pfb`, `*.mxf`, `*.otf`; slot 15 lists the folders, slot 14 opens each file), an alias table (default faces Times New Roman, Arial, Courier New, Lucida Console in slot 3; the game's `FontStyle.ini` reader, 0x0044D810, is outside the range), GetFont (slot 11, 0x00894ADF: face list, size, style, reuse) which picks a face, and slot 10 (0x00895A1C), which makes the font object for it. Interface 0x00673960; the framework caches it at 0x00B628D4 (ui.toml's `SC4::Singleton913D22`) |
| 0x1AA404AC | 0x15C | 0x00AC55D8 (64 slots) | 0x00899AAB | **Font.** One face at one size: size, scales, colours, style bits, the text encoding (+0x7C; 0x10 is UTF-16), a glyph cache and glyph sheet, its T2K holder at +0xEC and its blitter at +0x8C. Interface 0x10673960 |
| 0x08CDA30B | 8 | 0x00AC55B4 (8 slots) | 0x00897722 | **Glyph blitter.** Locks the picture, composites a list of placed glyphs into it, unlocks it. The first one builds blend tables at 0x00B54418. Not the only blitter: the font system gives its fonts one of the game's own ("Two blitters" below) |

Under the font, not a COM class: the T2K holder (0x34 bytes, 0x00897FED), with Open (0x00898CD9: memory handler, an input stream over the file, the font type, the sfnt and scaler objects; a `.pfb` is unwrapped and reopened), Close (0x008976A4), the style bits (0x0089801E, `italic`) and the glyph lookup by `uniXXXX` name (0x00898EB2) for faces whose type is 1 or 0x16.

The font's slots that matter for drawing:

| Slot | Address | What |
|---:|---|---|
| 3 | 0x0089955C | Load: open the face with T2K, apply size and style, make the blitter |
| 11 | 0x00897AB1 | SetSize (pixels, at most 0x800): ascent, descent, line height |
| 30 | 0x0089854E | SetScale (x, y) |
| 35 | 0x008966EB | Line height |
| 41 | 0x00899D81 | FitString: how much of a text fits a width (the line breaker) |
| 48, 49 | 0x00899F71, 0x00899FF4 | MeasureString, and the same without trailing spaces |
| 50 | 0x00896A2D | Shortens a string object to a width |
| **51** | **0x0089A099** | **DrawString**: one line at (x, y) into a picture |
| **52** | **0x0089A35B** | **DrawStringInRect**: wrapped and aligned in a rectangle, one DrawString per line |
| 60 | 0x0089834C | GetGlyph: the cached glyph of a character, rendered on a miss |
| 61 | 0x00898FDC | RenderGlyph: T2K renders the glyph (monochrome or 8-bit grey) |
| 62, 63 | 0x00897748, 0x0089632D | Store a glyph bitmap in the glyph sheet, grow the sheet |

Text comes in as the font's encoding and is turned into UTF-16 first (0x00899BCA), with a Thai composition pass (0x00899665: SARA AM 0x0E33 and the tone marks).

## Pieces

| Piece | Range | Functions | Ran | What |
|---|---|---:|---:|---|
| f1 | 0x00895F36 to 0x0089604D, less 0x0089603D | 8 | 1 | The director, its three factories, the static getter, slot 4 (the 1 is the font factory) |
| f2 | 0x00893D2C to 0x00895F35 | 85 | 7 | The font system: folder scans, face table, aliases, GetFont, and the STLport map and list code for them. Most of it runs while the game starts, before the meter is armed; the 7 are QueryInterface, Release, GetFont, slot 10 and three helpers |
| f3 | 0x00896254 to 0x0089A577, less f4 and f5 | 113 | 59 | The font: layout, measuring, line breaking, drawing, glyph cache and sheet, encodings, Thai, sample characters per language (slot 25) |
| f4 | 0x00896053 to 0x00896243, 0x00896E63, 0x00897246, 0x00897722 | 11 | 4 | The glyph blitter and its blend tables. **Ours** (`framework.font.blitter.draw` and `.object`, below) |
| f5 | 0x0089603D, 0x00896D2F to 0x00896E4A, 0x008976A4, 0x00897FED to 0x008988F1, 0x00898C6B to 0x00898EB2 | 13 | 11 | The T2K holder: open, close, file stream callbacks, styles, glyph names |
| f6 | 0x0089A578 to 0x008AE76D | 321 | 142 | **T2K** (third-party) |

Inside f6, by the anchors above: the memory manager and its stream (0x0089A578 to 0x0089AB3F), the sfnt reader and table loaders (0x0089B4C1 to 0x0089D56A), the API the Maxis side calls (0x0089D662 to 0x0089F7C6: new sfnt, new scaler, transform 0x0089DC9A, render glyph 0x0089F23B, char to glyph 0x0089F4B2, unwrap `.pfb` 0x0089F7C6), the Type 1 parser (about 0x0089FAF7 to 0x008A1834; it ran, so some of the game's fonts are Type 1), then the scaler, hinting and rasterizer to 0x008AE76D. The largest function, 0x008A489C (8.5 KB), never ran.

Only the Maxis side calls T2K: every call into 0x0089A578 to 0x008AE76D from outside it comes from f5 or the font's slots 3, 11, 25, 27, 30 and 61.

### Calls from outside

The range has 112 entry points called or referenced from outside 0x00894000 to 0x008A7FFF. 16 of them are T2K functions called from T2K's own code past 0x008A8000; most of the rest are slots of the vtables above (references from 0x00AC54xx to 0x00AC56xx). The direct calls from game code are few and not about text: STLport helpers the linker folded into this range and other code shares (0x0089408D list push_back, 0x0089461C map erase, 0x00897C8D list insert, 0x00894585, 0x00894843, 0x0089508C, 0x00897D22), the director getter (0x004600EE) and two static teardowns. Text reaches the fonts through COM: the font system from the framework, fonts from GetFont, then the font's slots.

## Text draw calls

Callers hold a font pointer and call slot 51 or 52 with nine arguments: the picture, the position (x, y for 51; a rectangle for 52), the text and its length, then colours (0x08000000 means "the font's own"), a clip rectangle and a pen position. Calls with exactly nine pushes through +0xCC and +0xD0 that are font draws (a lower bound: calls with branches between their pushes are not found this way):

| Caller | Slot | Ran | What it draws |
|---|---|---:|---|
| 0x0046C8B0 | 51 | 6 | A string into a picture at (0, 0), black |
| 0x0047CF90 | 51 | | A number with a minus sign |
| 0x005F1D00 | 51 | | A string from a 32-byte buffer at (0, 0), black |
| 0x00794BF0 | 51 | | A string at (8, 8) |
| 0x007B4150 | 51 (two) | 13 | Two strings side by side |
| 0x007BE7A0 | 52 (two) | 17 | A list of strings, each in its rectangle |
| 0x0080AB90 | 51 | | A label |
| 0x00909757 | 51 | 15 | Text runs of the HTML viewer (vtable 0x00ACE314) |
| 0x009A7E52, 0x009AF168, 0x009C55F3, 0x009D6F8A, 0x009DA106 | 52 | | Framework controls' text in their rectangles |
| 0x009B1541 | 51 and 52 | 17 | A framework control's text (font at +0x108, picture at +0x6C) |
| 0x009B447C, 0x009B5715 | 51 | | One character centred on a point |
| 0x009BEA28, 0x009C69E0, 0x009D0CB1, 0x009D673B | 51 | | Framework controls' text lines |

Slots 51 and 52 ran in all 17 coverage runs, far more often than these callers, so most text draws come from callers this filter misses (framework widgets in 0x0094 to 0x009D).

## Crisp text hook points

[ui-scaling.md](ui-scaling.md) ("Text and the region view") wants text drawn at the physical resolution instead of scaled up from game pixels. Every text draw goes through the font's slots, so the hooks can be few:

- **DrawString (slot 51, 0x0089A099).** Every line of text, with the picture, position, text, colours and clip: the place to draw the same string again at scale times the size into a shadow picture (route 2 of ui-scaling.md). DrawStringInRect ends in it too.
- **SetSize (slot 11, 0x00897AB1) and SetScale (slot 30).** The font's pixel size. A font object made at scale times the size gives scaled glyphs from the same code; layout then needs its positions scaled too.
- **The blitter's DrawGlyphs (slot 7 of 0x00AC55B4, 0x00897246).** The last step before text is pixels: a list of placed glyphs (0x24 bytes each: box, sheet pixels, pitch, width, height, character) and the picture. A replacement blitter could send glyphs to the driver instead of the picture, which is also a vtable swap rather than a detour. Most fonts draw through the font system's blitter instead (vtable 0x00AC14D0, through a drawing context; "Two blitters" below), so a hook here alone misses most text; the font's SetBlitter (slot 54) is where either is handed over.
- GetFont (0x00894ADF) is where fonts are made, if the larger font objects should be made alongside the game's.

Of these, only the f4 blitter is ours (as a translation, not yet as a crisp-text hook).

## f4: the glyph blitter is ours

The 11 functions of f4 are translated by hand ([src/core/font_blitter.cpp](../src/core/font_blitter.cpp)), each a detour on the game's, in two groups, off by default and installed at Attach (the first fonts are made while the game starts):

| Group | Functions | What |
|---|---:|---|
| `framework.font.blitter.draw` | 5 | DrawGlyphs (slot 7, 0x00897246), the compositor DrawGlyph (0x00896E63) and its three 16-bit blends through the tables: 4444 0x00896053, 555 0x008960C1, 565 0x0089612A |
| `framework.font.blitter.object` | 6 | the constructor 0x00897722, the blend table builder 0x0089618D, QueryInterface 0x0089620D (slot 0), Release 0x008961F6 (slot 2), Begin 0x00896232 and End 0x00896243 (slots 5 and 6) |

Slot 1 (AddRef, 0x00968B60) and slots 3 and 4 (0x0091559A) are the framework's shared functions and stay the game's. Addresses: [data/addresses/font.toml](../data/addresses/font.toml) (the blends, the builder, QueryInterface, Release, the vtable and the tables are new there).

### What the blitter does

- **DrawGlyphs** walks DrawString's list and calls DrawGlyph for each glyph of a character 0x20 or above, its coverage from (0, 0).
- **DrawGlyph** asks the picture (a cIGZBuffer) its width (+0x24), height (+0x28) and bits per pixel (+0x40), and in mode 3 only its colour type (+0x38, starting from AutoFromScreenFormat16). The glyph's box is cut to the clip, then to (0, 0, width, height), the coverage start moving as the left and top move; then come the stride (+0x8C) and the pixels (+0x88). By mode (the font's +0x4C, which 0x00896B2C maps to 1, 2 or 3): 1 writes the colour where the coverage is above 0x7F; 2 writes `colours[coverage]` where it is not 0; 3, on 16-bit pictures, blends by `coverage >> 3` with the table blend of the colour type (555 for R5G5B5 and A1R5G5B5, 565, 4444; any other type draws nothing), and on 32 and 24-bit pictures every mode but 1 and 2 blends every pixel by `coverage / 256`. Returns true.
- **The blend tables** (0x00B54418, 32 x 32 x 32 bytes, built once, flagged by the byte 0x00B5C418): entry (colour, target, alpha) is `alpha / 32 x colour + (1 - alpha / 32) x target`, truncated. The game computes it on the x87 with 1/32 as a float; every value is exact there, so ours uses whole numbers, `(alpha x colour + (32 - alpha) x target) / 32`, which gives the same bytes at any precision.
- **The object**: QueryInterface answers 1 and 0xA8BC2A75 with itself, AddRef'd through its own slot 1; Release counts down while above 1 and deletes the object (no destructor) at 1 or below; Begin and End lock and unlock the picture with 0x8080 and pass its EAX back.

### Quirks kept

- A **24-bit picture is drawn as 32-bit**: four bytes a pixel and the stride divided by four, so rows run past the picture's rows.
- The **565 blend** keeps only the high five bits of green (the lowest comes out 0) and takes red from everything above bit 10, unmasked; the **555 blend** clears bit 15, so an A1R5G5B5 picture loses its alpha bit where text blends; the **4444 blend** sets alpha to 0xF.
- The **32-bit blend** divides by 256 with rounding 0x80 per channel, alpha included (`target x (256 - c) + colour x c`); a pixel of no coverage is written back unchanged.
- A glyph's own width and height never clip it: only its box, the clip and the picture do. A box with right at or before left, or bottom at or before top, draws nothing.
- Mode 2 reads `colours[coverage]` for any level, but DrawString's ramp (0x0089838C) holds 128 colours, while a font's grey glyphs run to 255 unless the font has a table of levels (+0x5C to +0x64, which nothing we found sets). So the game reads up to 0x200 bytes past the ramp, into the heap, for the darker levels; ours reads the same bytes.
- Release at a count of 1 or below deletes without a destructor and does not clear the count.

### Two blitters

The font system's font maker (its slot 10, 0x00895A1C) hands every font it makes the system's own blitter (the font's slot 54, SetBlitter, 0x00896B45, with the system's +0x24) before Load; Load makes an f4 blitter only for a font that has none. The system's blitter is a game class (vtable 0x00AC14D0, Begin 0x0082A610) that asks the picture for a drawing context (interface 0xAB300B2B). So most text in play is drawn by that class, not by f4: in 90 seconds of the Making Money tutorial, one f4 blitter was made (at startup, building the tables) and drew 827 lists; the three 16-bit blends (mode 3) never ran.

### Testing

**Compare mode** (both groups): ours runs first, what it wrote is kept aside and the memory put back, then the game's runs; results and memory are compared byte for byte. DrawGlyphs and DrawGlyph compare the picture's rows the glyphs can reach (whole strides, and for 32 and 24-bit pictures as far past the last row as the glyphs reach); to find them the comparison asks the picture its width, height, bits, stride and pixels once more. The blends compare their result, the builder the tables and their flag, the constructor the object and the tables, QueryInterface the out pointer and the count, Release the count. While the game's side runs, our detours inside it (DrawGlyph and the blends under the game's DrawGlyphs, the builder under the game's constructor) pass through to the game's. Begin and End lock a picture and cannot run twice, and Release at 1 or below deletes: those run the game's alone in compare mode.

**`OPENSC4_TEST_FONTS=<day>`** (fingerprint ` | ft`; [src/core/font_blitter_autotest.cpp](../src/core/font_blitter_autotest.cpp)) calls the 11 by address and draws strings through real fonts. The pictures are mocks of our own whose pixels sit in canvases with 64 bytes of 0xA5 before and after, hashed whole after every draw; every call a mock gets goes into the hash with its arguments. Stages:

| Stage | What |
|---|---|
| tables | the tables spoiled, rebuilt by 0x0089618D, hashed, put back |
| blend | the three blends with their registers (EAX, ECX, EDX in; EAX and EDX out; EBX, ESI, EDI checked kept), 55 pairs of colours by alphas 0 to 32 |
| object | the constructor in place between margins, and once with the tables' flag cleared; QueryInterface for eight interface IDs; Release above 1 in place and down to deletion on objects from the game's allocator; AddRef and Release through the vtable |
| lock | Begin and End with a picture answering 0, 1, 0x12345601, 0xFFFFFF00, 0x80 |
| glyphs | DrawGlyph and DrawGlyphs on 13 pictures: 16-bit of colour types 3, 4, 5, 6, 7 and 9, 32-bit, 24-bit and 8-bit, tight, padded, odd and zero strides, 1 by 1 and 3 by 40; modes 0 to 4; no clip and four clips (inside, all-covering, inverted, a thin column); boxes off every edge and inverted, coverage forwards and upwards (negative pitch), characters below a space |
| strings | Arial MT at 10, 14 (style 1) and 23 and Times New Roman at 17 (style 2), made by GetFont without reuse and given an f4 blitter (SetBlitter) for the test, in modes 1, 2 and 3, into 120 by 34 pictures of 565, 555, 4444 and 32-bit: three strings at five places (cut by the picture's edges, by a clip, with and without a pen) and once with the lock refused. For mode 2 the fonts get a table halving their levels (the quirk above would otherwise read heap bytes) |

A mutation check (the 32-bit blend's rounding 0x800081, the 565 blend's green from bit 5, the clip's top cut one too far into the coverage, Release counting down only above 2) changed the hashes it should each time.

Results, 2026-10-23:

| Check | Result |
|---|---|
| Test runs, groups off (twice) vs on | every stage hash the same: tables 840F071F, blend C65B9C8C, object 554A8BBA, lock E514F939, glyphs 78F44952, strings 1CFB48AA; `ft` 1CFB48AA. 684 of the 720 string draws change pixels |
| Compare mode, test and play | 0 divergences: with the test DrawGlyphs 1056 calls, DrawGlyph 975, each blend 1815, constructor 13, QueryInterface 9, Release 6, tables 1; in 90 s of plain play DrawGlyphs 827 (666 in a second run), constructor 1, QueryInterface 1 |
| Screenshots (`run-shots.sh`, Making Money, both groups on) vs the game | 20 of 20 within `--tolerance 32 --max-differing 1.2`; a second run of the game against the first gives the same (raw, 0 of 20 match either way: the harness's noise) |
| Parity `ft1-r2`, 12 saves, 7 days, muted, own folder `p4env-ft1`, two jobs, A `determinism.dispatch_info`, B that and both groups | 12 same |
| The same with `OPENSC4_TEST_FONTS=2` (`ft1-t3`) | 12 same; `ft` 1CFB48AA in all 120 runs, both sides |
| Earlier build (before the audit's fixes, which touch only compare mode and a read): `ft1-r1` and `ft1-t2` | 11 same and 1 partly each, a different save each time (Big City d1095, Making Money d0365): only the first B run differed, its three retries matched A; read as the harness's noise ("Running it" in [parity.md](parity.md)) |

**Reached** in plain play: the constructor and the table builder (once, at startup), QueryInterface, Begin, DrawGlyphs, DrawGlyph, End, and Release (at exit). Not reached: the three 16-bit blends (no font is in mode 3). The test reaches all 11.

**Audit**: each translation was checked against the disassembly by the author and by an independent audit (memory writes and their order, loop bounds and the signedness of every compare, 32-bit wrap of pointers and coordinates, every formula, virtual calls with their slots, arguments, order and conditions, return values, the thunks' byte counts and registers, the compare regions). No difference reachable by a caller. Left: the upper bytes of EAX where only AL is answered (QueryInterface, DrawGlyph, DrawGlyphs); ECX after the blends, the table builder and Release (the game's lose it, ours keep it; every caller reloads it). The 32-bit blend is written as `((rb >> 8) & 0xFF00FF) | ((ag + 0x800080) & 0xFF00FF00)`; the game's `((rb >> 8) ^ (ag + 0xFF800080)) & 0xFF00FF ^ (ag + 0x800080)` is the same, the two sums differing only above bit 23. Fixed from the audit: the 16-bit palette mode reads two bytes of each colour, as the game does (it read four); compare mode's region past the last row of a 24-bit picture now ends where the glyphs end (it ran to the picture's width, past its memory when the glyphs did not reach the right edge). All checks above were run again on the final build: the same hashes, 0 divergences, shots 20 of 20.
